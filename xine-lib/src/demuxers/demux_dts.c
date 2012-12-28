/*
 * Copyright (C) 2005-2008 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Raw DTS Demuxer by James Stembridge (jstembridge@gmail.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#define LOG_MODULE "demux_dts"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"
#include "group_audio.h"

#define DATA_TAG 0x61746164

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  seek_flag;
  int                  samples_per_frame;
  int                  sample_rate;
  int                  frame_size;

  off_t                data_start;
} demux_dts_t;

typedef struct {
  demux_class_t     demux_class;
} demux_dts_class_t;

static const int dts_sample_rates[] =
{
  0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0,
  12000, 24000, 48000, 96000, 192000
};

static int open_dts_file(demux_dts_t *this) {
  int i, offset = 0;
  uint32_t syncword = 0;
  size_t peak_size = 0;
  uint32_t blocksize;
  uint8_t *peak;

  lprintf("open_dts_file\n");

  blocksize = this->input->get_blocksize(this->input);
  if (blocksize && INPUT_IS_SEEKABLE(this->input)) {
    //    this->input->seek(this->input, 0, SEEK_SET);
    buf_element_t *buf = this->input->read_block(this->input,
						 this->stream->audio_fifo,
						 blocksize);
    this->input->seek(this->input, 0, SEEK_SET);

    if (!buf)
      return 0;

    peak = alloca(peak_size = buf->size);
    xine_fast_memcpy(peak, buf->content, peak_size);

    buf->free_buffer(buf);
  } else {
    peak = alloca(peak_size = MAX_PREVIEW_SIZE);

    if (_x_demux_read_header(this->input, peak, peak_size) != peak_size)
      return 0;
  }

  lprintf("peak size: %d\n", peak_size);

  /* Check for wav header, as we'll handle DTS with a wav header shoved
   * on the front for CD burning */
  if ( memcmp(peak, "RIFF", 4) == 0 || memcmp(&peak[8], "WAVEfmt ", 8) == 0 ) {
    /* Check this looks like a cd audio wav */
    unsigned int audio_type;
    xine_waveformatex *wave = (xine_waveformatex *) &peak[20];

    _x_waveformatex_le2me(wave);
    audio_type = _x_formattag_to_buf_audio(wave->wFormatTag);

    if ((audio_type != BUF_AUDIO_LPCM_LE) || (wave->nChannels != 2) ||
        (wave->nSamplesPerSec != 44100) || (wave->wBitsPerSample != 16))
      return 0;

    lprintf("looks like a cd audio wav file\n");

    /* Find the data chunk */
    offset = 20 + _X_LE_32(&peak[16]);
    while (offset < peak_size-8) {
      unsigned int chunk_tag = _X_LE_32(&peak[offset]);
      unsigned int chunk_size = _X_LE_32(&peak[offset+4]);

      if (chunk_tag == DATA_TAG) {
        offset += 8;
        lprintf("found the start of the data at offset %d\n", offset);
        break;
      } else
        offset += chunk_size;
    }
  }

  /* DTS bitstream encoding version
   * -1 - not detected
   *  0 - 16 bits and big endian
   *  1 - 16 bits and low endian (detection not implemented)
   *  2 - 14 bits and big endian (detection not implemented)
   *  3 - 14 bits and low endian
   */
  int dts_version = -1;

  /* Look for a valid DTS syncword */
  for (i=offset; i<peak_size-1; i++) {
    /* 16 bits and big endian bitstream */
    if (syncword == 0x7ffe8001) {
	dts_version = 0;
	break;
    }
    /* 14 bits and little endian bitstream */
    else if ((syncword == 0xff1f00e8) &&
             ((peak[i] & 0xf0) == 0xf0) && (peak[i+1] == 0x07)) {
	dts_version = 3;
	break;
    }

    syncword = (syncword << 8) | peak[i];
  }

  if (dts_version == -1) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             LOG_MODULE ": unsupported DTS stream type, or not a DTS stream\n");
    return 0;
  }

  this->data_start = i-4;
  lprintf("found DTS syncword at offset %d\n", i-4);

  if (i < peak_size-9) {
    unsigned int nblks, fsize, sfreq;
    switch (dts_version)
    {
    case 0: /* BE16 */
      nblks = ((peak[this->data_start+4] & 0x01) << 6) |
               ((peak[this->data_start+5] & 0xfc) >> 2);
      fsize = (((peak[this->data_start+5] & 0x03) << 12) |(peak[this->data_start+6] << 4) |
               ((peak[this->data_start+7] & 0xf0) >> 4)) + 1;
      sfreq = (peak[this->data_start+8] & 0x3c) >> 2;
      break;

    case 3: /* LE14 */
      nblks = ((peak[this->data_start+4] & 0x07) << 4) |
               ((peak[this->data_start+7] & 0x3c) >> 2);
      fsize = (((peak[this->data_start+7] & 0x03) << 12) |
               (peak[this->data_start+6] << 4) |
              ((peak[this->data_start+9] & 0x3c) >> 2)) + 1;
      sfreq = peak[this->data_start+8] & 0x0f;
      break;

    default:
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               LOG_MODULE ": unsupported DTS bitstream encoding %d\n",
               dts_version);
      return 0;

    }

    if ((sfreq > sizeof(dts_sample_rates)/sizeof(int)) ||
        (dts_sample_rates[sfreq] == 0))
      return 0;

    /* Big assumption - this is CBR data */
    this->samples_per_frame = (nblks + 1) * 32;

    switch (dts_version)
    {
    case 0: /* BE16 */
    case 1: /* LE16 */
	this->frame_size = fsize * 8 / 16 * 2;
	break;
    case 2: /* BE14 */
    case 3: /* LE14 */
	this->frame_size = fsize * 8 / 14 * 2;
	break;
    }

    this->sample_rate = dts_sample_rates[sfreq];

    lprintf("samples per frame: %d\n", this->samples_per_frame);
    lprintf("frame size: %d\n", this->frame_size);
    lprintf("sample rate: %d\n", this->sample_rate);

    /* Seek to start of DTS data */
    this->input->seek(this->input, this->data_start, SEEK_SET);

    return 1;
  }

  return 0;
}

static int demux_dts_send_chunk (demux_plugin_t *this_gen) {
  demux_dts_t *this = (demux_dts_t *) this_gen;

  buf_element_t *buf = NULL;
  off_t current_stream_pos;
  int64_t audio_pts;
  int frame_number;
  uint32_t blocksize;

  current_stream_pos = this->input->get_current_pos(this->input) -
                       this->data_start;
  frame_number = current_stream_pos / this->frame_size;

  audio_pts = frame_number;
  audio_pts *= 90000;
  audio_pts *= this->samples_per_frame;
  audio_pts /= this->sample_rate;

  if (this->seek_flag) {
    _x_demux_control_newpts(this->stream, audio_pts, BUF_FLAG_SEEK);
    this->seek_flag = 0;
  }

  blocksize = this->input->get_blocksize(this->input);
  if (blocksize) {
    buf = this->input->read_block(this->input, this->audio_fifo,
                                  blocksize);
    if (!buf) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
  } else {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->size = this->input->read(this->input, buf->content,
                                  this->frame_size);
  }

  if (buf->size <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  buf->type = BUF_AUDIO_DTS;
  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) current_stream_pos *
        65535 / (this->input->get_length(this->input) - this->data_start) );
  buf->extra_info->input_time = audio_pts / 90;
  buf->pts = audio_pts;
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_dts_send_headers(demux_plugin_t *this_gen) {
  demux_dts_t *this = (demux_dts_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_DTS;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_dts_get_stream_length (demux_plugin_t *this_gen) {
  demux_dts_t *this = (demux_dts_t *) this_gen;
  int stream_length = 0;

  if (this->input->get_length(this->input)) {
    stream_length = this->input->get_length(this->input) - this->data_start;
    stream_length /= this->frame_size;
    stream_length *= this->samples_per_frame;
    stream_length /= this->sample_rate;
    stream_length *= 1000;

    lprintf("running time: %u\n", stream_length);
  }

  return stream_length;
}

static int demux_dts_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_dts_t *this = (demux_dts_t *) this_gen;

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  start_pos = (off_t) ( (double) start_pos / 65535 *
      (this->input->get_length(this->input) - this->data_start) );

  if (start_time) {
    int length = demux_dts_get_stream_length (this_gen);
    if (length != 0) {
      start_pos = start_time *
          (this->input->get_length(this->input) - this->data_start) / length;
    }
  }

  /* divide the requested offset integer-wise by the frame alignment and
   * multiply by the frame alignment to determine the new starting block */
  start_pos /= this->frame_size;
  start_pos *= this->frame_size;
  start_pos += this->data_start;
  this->input->seek(this->input, start_pos, SEEK_SET);

  return this->status;
}

static int demux_dts_get_status (demux_plugin_t *this_gen) {
  demux_dts_t *this = (demux_dts_t *) this_gen;

  return this->status;
}

static uint32_t demux_dts_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_dts_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input) {

  demux_dts_t   *this;

  this         = calloc(1, sizeof(demux_dts_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_dts_send_headers;
  this->demux_plugin.send_chunk        = demux_dts_send_chunk;
  this->demux_plugin.seek              = demux_dts_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_dts_get_status;
  this->demux_plugin.get_stream_length = demux_dts_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_dts_get_capabilities;
  this->demux_plugin.get_optional_data = demux_dts_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:
    if (!open_dts_file(this)) {
      free (this);
      return NULL;
    }
  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

void *demux_dts_init_plugin (xine_t *xine, void *data) {
  demux_dts_class_t     *this;

  this = calloc(1, sizeof(demux_dts_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Raw DTS demux plugin");
  this->demux_class.identifier      = "DTS";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "dts";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

