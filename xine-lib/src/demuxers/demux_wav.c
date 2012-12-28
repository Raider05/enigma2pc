/*
 * Copyright (C) 2001-2008 the xine project
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
 * MS WAV File Demuxer by Mike Melanson (melanson@pcisys.net)
 * based on WAV specs that are available far and wide
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

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"
#include "group_audio.h"

#define WAV_SIGNATURE_SIZE 12
/* this is the hex value for 'data' */
#define data_TAG 0x61746164
/* this is the hex value for 'fmt ' */
#define fmt_TAG 0x20746D66
#define PCM_BLOCK_ALIGN 1024

#define PREFERED_BLOCK_SIZE 4096

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  xine_waveformatex    *wave;
  int                  wave_size;
  unsigned int         audio_type;

  off_t                data_start;
  off_t                data_size;

  int                  send_newpts;
  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_wav_t;

typedef struct {
  demux_class_t     demux_class;
} demux_wav_class_t;

static int demux_wav_get_stream_length (demux_plugin_t *this_gen);

/* searches for the chunk with the given tag from the beginning of WAV file
 * returns 1 if chunk was found, 0 otherwise,
 * fills chunk_size and chunk_pos if chunk was found
 * NOTE: chunk_pos is set to the position of the first byte of chunk data */
static int find_chunk_by_tag(demux_wav_t *this, const uint32_t given_chunk_tag,
                             uint32_t *found_chunk_size, off_t *found_chunk_pos) {
  uint32_t chunk_tag;
  uint32_t chunk_size;
  uint8_t chunk_preamble[8];

  /* search for the chunks from the start of the WAV file */
  this->input->seek(this->input, WAV_SIGNATURE_SIZE, SEEK_SET);

  while (1) {
    if (this->input->read(this->input, chunk_preamble, 8) != 8) {
      return 0;
    }

    chunk_tag = _X_LE_32(&chunk_preamble[0]);
    chunk_size = _X_LE_32(&chunk_preamble[4]);

    if (chunk_tag == given_chunk_tag) {
      if (found_chunk_size)
        *found_chunk_size = _X_LE_32(&chunk_preamble[4]);
      if (found_chunk_pos)
        *found_chunk_pos = this->input->get_current_pos(this->input);
      return 1;
    } else {
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  }
}

/* returns 1 if the WAV file was opened successfully, 0 otherwise */
static int open_wav_file(demux_wav_t *this) {
  uint8_t signature[WAV_SIGNATURE_SIZE];
  off_t wave_pos;
  uint32_t wave_size;

  /* check the signature */
  if (_x_demux_read_header(this->input, signature, WAV_SIGNATURE_SIZE) != WAV_SIGNATURE_SIZE)
    return 0;

  if (memcmp(signature, "RIFF", 4) || memcmp(&signature[8], "WAVE", 4) )
    return 0;

  /* search for the 'fmt ' chunk first */
  wave_pos = 0;
  if (find_chunk_by_tag(this, fmt_TAG, &wave_size, &wave_pos)==0)
    return 0;
  this->wave_size = wave_size;

  this->input->seek(this->input, wave_pos, SEEK_SET);
  this->wave = malloc( this->wave_size );

  if (!this->wave || this->input->read(this->input, (void *)this->wave, this->wave_size) !=
    this->wave_size) {
    free (this->wave);
    return 0;
  }
  _x_waveformatex_le2me(this->wave);
  this->audio_type = _x_formattag_to_buf_audio(this->wave->wFormatTag);
  if(!this->audio_type) {
    this->audio_type = BUF_AUDIO_UNKNOWN;
  }

  if (this->wave->nChannels <= 0) {
    free (this->wave);
    return 0;
  }

  /* search for the 'data' chunk */
  this->data_start = this->data_size = 0;
  if (find_chunk_by_tag(this, data_TAG, NULL, &this->data_start)==0)
  {
    free (this->wave);
    return 0;
  }
  else
  {
    /* Get the data length from the file itself, instead of the data
     * TAG, for broken files */
    this->input->seek(this->input, this->data_start, SEEK_SET);
    this->data_size = this->input->get_length(this->input);
    return 1;
  }
}

static int demux_wav_send_chunk(demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  if (this->wave->nBlockAlign < PREFERED_BLOCK_SIZE) {
    remaining_sample_bytes = PREFERED_BLOCK_SIZE / this->wave->nBlockAlign *
                             this->wave->nBlockAlign;
  } else{
    remaining_sample_bytes = this->wave->nBlockAlign;
  }
  current_file_pos =
    this->input->get_current_pos(this->input) - this->data_start;

  current_pts = current_file_pos;
  current_pts *= 90000;
  current_pts /= this->wave->nAvgBytesPerSec;

  if (this->send_newpts) {
    _x_demux_control_newpts(this->stream, current_pts, this->seek_flag);
    this->send_newpts = this->seek_flag = 0;
  }

  while (remaining_sample_bytes) {
    if(!this->audio_fifo){
      this->status = DEMUX_FINISHED;
      break;
    }

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    if( this->data_size )
      buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->data_size);
    buf->extra_info->input_time = current_pts / 90;
    buf->pts = current_pts;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    off_t read;
    if ((read = this->input->read(this->input, buf->content, buf->size)) !=
      buf->size) {
      if (read == 0) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      } else {
        buf->size = read;
      }
    }

#if 0
    for(n=0;n<20;n++) {
      printf("%x ",buf->content[n]);
    }
    printf("\n");
#endif

#if 0
    for(n=0;n<20;n++) {
      printf("%x ",buf->content[n]);
    }
    printf("\n");
#endif

    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    buf->type = this->audio_type;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  return this->status;
}

static void demux_wav_send_headers(demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                       this->wave->nChannels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       this->wave->nSamplesPerSec);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                       this->wave->wBitsPerSample);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave->nSamplesPerSec;
    buf->decoder_info[2] = this->wave->wBitsPerSample;
    buf->decoder_info[3] = this->wave->nChannels;
    buf->content = (void *)this->wave;
    buf->size = this->wave_size;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_wav_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  this->status = DEMUX_OK;

  this->send_newpts = 1;
  if (playing) {
    this->seek_flag = 1;
    _x_demux_flush_engine (this->stream);
  }
  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* time-based seeking, the start_pos code will then align the blocks
   * if necessary */
  if (start_time != 0) {
    int length = demux_wav_get_stream_length (this_gen);
    if (length != 0) {
      start_pos = start_time * this->data_size / length;
    }
  }

  /* check the boundary offsets */
  if (start_pos <= 0)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    /* This function must seek along the block alignment. The start_pos
     * is in reference to the start of the data. Divide the start_pos by
     * the block alignment integer-wise, and multiply the quotient by the
     * block alignment to get the new aligned offset. Add the data start
     * offset and seek to the new position. */
    start_pos /= this->wave->nBlockAlign;
    start_pos *= this->wave->nBlockAlign;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  return this->status;
}

static void demux_wav_dispose (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  free(this->wave);
  free(this);
}

static int demux_wav_get_status (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_wav_get_stream_length (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  return (int)((int64_t) this->data_size * 1000 / this->wave->nAvgBytesPerSec);
}

static uint32_t demux_wav_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_wav_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_wav_t    *this;
  uint32_t	align;

  this         = calloc(1, sizeof(demux_wav_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_wav_send_headers;
  this->demux_plugin.send_chunk        = demux_wav_send_chunk;
  this->demux_plugin.seek              = demux_wav_seek;
  this->demux_plugin.dispose           = demux_wav_dispose;
  this->demux_plugin.get_status        = demux_wav_get_status;
  this->demux_plugin.get_stream_length = demux_wav_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_wav_get_capabilities;
  this->demux_plugin.get_optional_data = demux_wav_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_wav_file(this)) {
      free (this);
      return NULL;
    }

  break;

  default:
    free (this);
    return NULL;
  }

  /* special block alignment hack so that the demuxer doesn't send
   * packets with individual PCM samples */
  if ((this->wave->nAvgBytesPerSec / this->wave->nBlockAlign) ==
      this->wave->nSamplesPerSec) {
    align = PCM_BLOCK_ALIGN / this->wave->nBlockAlign;
    align = align * this->wave->nBlockAlign;
    this->wave->nBlockAlign = align;
  }

  return &this->demux_plugin;
}

void *demux_wav_init_plugin (xine_t *xine, void *data) {
  demux_wav_class_t     *this;

  this = calloc(1, sizeof(demux_wav_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("WAV file demux plugin");
  this->demux_class.identifier      = "WAV";
  this->demux_class.mimetypes       =
    "audio/x-wav: wav: WAV audio;"
    "audio/wav: wav: WAV audio;"
    "audio/x-pn-wav: wav: WAV audio;"
    "audio/x-pn-windows-acm: wav: WAV audio;";
  this->demux_class.extensions      = "wav";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
