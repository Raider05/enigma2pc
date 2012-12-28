/*
 * Copyright (C) 2001-2005 the xine project
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
 */

/*
 * AC3 File Demuxer by Mike Melanson (melanson@pcisys.net)
 * This demuxer detects raw AC3 data in a file and shovels AC3 data
 * directly to the AC3 decoder.
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

#define LOG_MODULE "demux_ac3"
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
  int                  sample_rate;
  int                  frame_size;
  int                  running_time;

  off_t                data_start;

  uint32_t             buf_type;

} demux_ac3_t;

typedef struct {
  demux_class_t     demux_class;
} demux_ac3_class_t;

/* borrow some knowledge from the AC3 decoder */
struct frmsize_s {
  uint16_t bit_rate;
  uint16_t frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[64] =
{
  { 32  ,{64   ,69   ,96   } },
  { 32  ,{64   ,70   ,96   } },
  { 40  ,{80   ,87   ,120  } },
  { 40  ,{80   ,88   ,120  } },
  { 48  ,{96   ,104  ,144  } },
  { 48  ,{96   ,105  ,144  } },
  { 56  ,{112  ,121  ,168  } },
  { 56  ,{112  ,122  ,168  } },
  { 64  ,{128  ,139  ,192  } },
  { 64  ,{128  ,140  ,192  } },
  { 80  ,{160  ,174  ,240  } },
  { 80  ,{160  ,175  ,240  } },
  { 96  ,{192  ,208  ,288  } },
  { 96  ,{192  ,209  ,288  } },
  { 112 ,{224  ,243  ,336  } },
  { 112 ,{224  ,244  ,336  } },
  { 128 ,{256  ,278  ,384  } },
  { 128 ,{256  ,279  ,384  } },
  { 160 ,{320  ,348  ,480  } },
  { 160 ,{320  ,349  ,480  } },
  { 192 ,{384  ,417  ,576  } },
  { 192 ,{384  ,418  ,576  } },
  { 224 ,{448  ,487  ,672  } },
  { 224 ,{448  ,488  ,672  } },
  { 256 ,{512  ,557  ,768  } },
  { 256 ,{512  ,558  ,768  } },
  { 320 ,{640  ,696  ,960  } },
  { 320 ,{640  ,697  ,960  } },
  { 384 ,{768  ,835  ,1152 } },
  { 384 ,{768  ,836  ,1152 } },
  { 448 ,{896  ,975  ,1344 } },
  { 448 ,{896  ,976  ,1344 } },
  { 512 ,{1024 ,1114 ,1536 } },
  { 512 ,{1024 ,1115 ,1536 } },
  { 576 ,{1152 ,1253 ,1728 } },
  { 576 ,{1152 ,1254 ,1728 } },
  { 640 ,{1280 ,1393 ,1920 } },
  { 640 ,{1280 ,1394 ,1920 } }
};

/* returns 1 if the AC3 file was opened successfully, 0 otherwise */
static int open_ac3_file(demux_ac3_t *this) {
  int i;
  int offset = 0;
  size_t peak_size = 0;
  int spdif_mode = 0;
  uint32_t syncword = 0;
  uint32_t blocksize;
  uint8_t *peak;

  blocksize = this->input->get_blocksize(this->input);
  if (blocksize && INPUT_IS_SEEKABLE(this->input)) {
    this->input->seek(this->input, 0, SEEK_SET);
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

  /* Check for wav header, as we'll handle AC3 with a wav header shoved
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

  /* Look for a valid AC3 sync word */
  for (i=offset; i<peak_size; i++) {
    if ((syncword & 0xffff) == 0x0b77) {
      this->data_start = i-2;
      lprintf("found AC3 syncword at offset %d\n", i-2);
      break;
    }

    if ((syncword == 0x72f81f4e) && (peak[i] == 0x01)) {
      spdif_mode = 1;
      this->data_start = i+4;
      lprintf("found AC3 SPDIF header at offset %d\n", i-4);
      break;
    }

    syncword = (syncword << 8) | peak[i];
  }

  if (i >= peak_size-2)
    return 0;

  if (spdif_mode) {
    this->sample_rate = 44100;
    this->frame_size = 256*6*4;
    this->buf_type = BUF_AUDIO_DNET;
  } else {
    int fscod, frmsizecod;

    fscod = peak[this->data_start+4] >> 6;
    frmsizecod = peak[this->data_start+4] & 0x3F;

    if ((fscod > 2) || (frmsizecod > 37))
      return 0;

    this->frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] * 2;

    /* convert the sample rate to a more useful number */
    switch (fscod) {
      case 0:
        this->sample_rate = 48000;
        break;
      case 1:
        this->sample_rate = 44100;
        break;
      default:
        this->sample_rate = 32000;
        break;
    }

    /* Look for a second sync word */
    if ((this->data_start+this->frame_size+1 >= peak_size) ||
        (peak[this->data_start+this->frame_size] != 0x0b) ||
        (peak[this->data_start+this->frame_size + 1] != 0x77)) {
      return 0;
    }

    lprintf("found second AC3 sync word\n");

    this->buf_type = BUF_AUDIO_A52;
  }

  this->running_time = this->input->get_length(this->input) -
                       this->data_start;
  this->running_time /= this->frame_size;
  this->running_time *= (90000 / 1000) * (256 * 6);
  this->running_time /= this->sample_rate;

  lprintf("sample rate: %d\n", this->sample_rate);
  lprintf("frame size: %d\n", this->frame_size);
  lprintf("running time: %d\n", this->running_time);

  return 1;
}

static int demux_ac3_send_chunk (demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  buf_element_t *buf = NULL;
  off_t current_stream_pos;
  int64_t audio_pts;
  int frame_number;
  uint32_t blocksize;

  current_stream_pos = this->input->get_current_pos(this->input);
  frame_number = current_stream_pos / this->frame_size;

  /*
   * Each frame represents 256*6 new audio samples according to the a52 spec.
   * Thus, the pts computation should work something like:
   *
   *   pts = frame #  *  256*6 samples        1 sec        90000 pts
   *                     -------------  *  -----------  *  ---------
   *                        1 frame        sample rate       1 sec
   */
  audio_pts = frame_number;
  audio_pts *= 90000;
  audio_pts *= 256 * 6;
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

  buf->type = this->buf_type;
  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) current_stream_pos *
                                     65535 / this->input->get_length (this->input) );
  buf->extra_info->input_time = audio_pts / 90;
  buf->pts = audio_pts;
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_ac3_send_headers(demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;
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
    buf->type = this->buf_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_ac3_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* divide the requested offset integer-wise by the frame alignment and
   * multiply by the frame alignment to determine the new starting block */
  start_pos /= this->frame_size;
  start_pos *= this->frame_size;
  this->input->seek(this->input, start_pos, SEEK_SET);

  return this->status;
}

static int demux_ac3_get_status (demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  return this->status;
}

/* return the approximate length in milliseconds */
static int demux_ac3_get_stream_length (demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  return this->running_time;
}

static uint32_t demux_ac3_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ac3_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input) {

  demux_ac3_t   *this;

  this         = calloc(1, sizeof(demux_ac3_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ac3_send_headers;
  this->demux_plugin.send_chunk        = demux_ac3_send_chunk;
  this->demux_plugin.seek              = demux_ac3_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_ac3_get_status;
  this->demux_plugin.get_stream_length = demux_ac3_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ac3_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ac3_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_ac3_file(this)) {
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

void *demux_ac3_init_plugin (xine_t *xine, void *data) {
  demux_ac3_class_t     *this;

  this = calloc(1, sizeof(demux_ac3_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Raw AC3 demux plugin");
  this->demux_class.identifier      = "AC3";
  this->demux_class.mimetypes       = "audio/ac3: ac3: Dolby Digital audio;";
  this->demux_class.extensions      = "ac3";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
