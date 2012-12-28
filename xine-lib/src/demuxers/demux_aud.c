/*
 * Copyright (C) 2000-2003 the xine project
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
 * Westwood Studios AUD File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the AUD file format, refer to:
 *   http://www.geocities.com/SiliconValley/8682/aud3.txt
 *
 * Implementation note: There is no definite file signature in this format.
 * This demuxer uses a probabilistic strategy for content detection. This
 * entails performing sanity checks on certain header values in order to
 * qualify a file. Refer to open_aud_file() for the precise parameters.
 *
 * Implementation note #2: The IMA ADPCM data stored in this file format
 * does not encode any initialization information; decoding parameters are
 * initialized to 0 at the start of the file and maintained throughout the
 * data. This makes seeking conceptually impossible. Upshot: Random
 * seeking is not supported.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_audio.h"

#define AUD_HEADER_SIZE 12
#define AUD_CHUNK_PREAMBLE_SIZE 8

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                data_size;

  int                  audio_samplerate;
  int                  audio_channels;
  int                  audio_bits;
  int                  audio_type;
  int64_t              audio_frame_counter;
} demux_aud_t;

typedef struct {
  demux_class_t     demux_class;
} demux_aud_class_t;


/* returns 1 if the AUD file was opened successfully, 0 otherwise */
static int open_aud_file(demux_aud_t *this) {
  unsigned char header[AUD_HEADER_SIZE];

  if (_x_demux_read_header(this->input, header, AUD_HEADER_SIZE) != AUD_HEADER_SIZE)
    return 0;

  /* Probabilistic content detection strategy: There is no file signature
   * so perform sanity checks on various header parameters:
   *   8000 <= sample rate (16 bits) <= 48000  ==> 40001 acceptable numbers
   *   compression type (8 bits) = 1 or 99     ==> 2 acceptable numbers
   * There is a total of 24 bits. The number space contains 2^24 =
   * 16777216 numbers. There are 40001 * 2 = 80002 acceptable combinations
   * of numbers. There is a 80002/16777216 = 0.48% chance of a false
   * positive.
   */
  this->audio_samplerate = _X_LE_16(&header[0]);
  if ((this->audio_samplerate < 8000) || (this->audio_samplerate > 48000))
    return 0;

#if 0
note: This loose content detection strategy is causing a few false positives;
remove this case for the time being since this audio type is not supported
anyway.
  if (header[11] == 1)
    this->audio_type = BUF_AUDIO_WESTWOOD;
  else
#endif
  if (header[11] == 99)
    this->audio_type = BUF_AUDIO_VQA_IMA;
  else
    return 0;

  /* file is qualified; skip over the header bytes in the stream */
  this->input->seek(this->input, AUD_HEADER_SIZE, SEEK_SET);

  /* flag 0 indicates stereo */
  this->audio_channels = (header[10] & 0x1) + 1;
  /* flag 1 indicates 16 bit audio */
  this->audio_bits = (((header[10] & 0x2) >> 1) + 1) * 8;

  this->data_start = AUD_HEADER_SIZE;
  this->data_size = this->input->get_length(this->input) - this->data_start;
  this->audio_frame_counter = 0;

  return 1;
}

static int demux_aud_send_chunk(demux_plugin_t *this_gen) {
  demux_aud_t *this = (demux_aud_t *) this_gen;

  unsigned char chunk_preamble[AUD_CHUNK_PREAMBLE_SIZE];
  int chunk_size;
  off_t current_file_pos;
  int64_t audio_pts;
  buf_element_t *buf;

  if (this->input->read(this->input, chunk_preamble, AUD_CHUNK_PREAMBLE_SIZE) !=
    AUD_CHUNK_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* validate the chunk */
  if (!_x_is_fourcc(&chunk_preamble[4], "\xAF\xDE\x00\x00")) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  chunk_size = _X_LE_16(&chunk_preamble[0]);

  current_file_pos = this->input->get_current_pos(this->input) -
    this->data_start;

  /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
  this->audio_frame_counter += (chunk_size * 2) / this->audio_channels;
  audio_pts = this->audio_frame_counter;
  audio_pts *= 90000;
  audio_pts /= this->audio_samplerate;

  while (chunk_size) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    if( this->data_size )
      buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->data_size);
    buf->extra_info->input_time = audio_pts / 90;
    buf->pts = audio_pts;

    if (chunk_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = chunk_size;
    chunk_size -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!chunk_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }

  return this->status;
}

static void demux_aud_send_headers(demux_plugin_t *this_gen) {
  demux_aud_t *this = (demux_aud_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                       this->audio_channels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       this->audio_samplerate);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                       this->audio_bits);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to the audio decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_samplerate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_aud_seek (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time, int playing) {

  demux_aud_t *this = (demux_aud_t *) this_gen;

  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* no seeking yet */
  return this->status;
}

static int demux_aud_get_status (demux_plugin_t *this_gen) {
  demux_aud_t *this = (demux_aud_t *) this_gen;

  return this->status;
}

static int demux_aud_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_aud_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_aud_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_aud_t    *this;

  this         = calloc(1, sizeof(demux_aud_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_aud_send_headers;
  this->demux_plugin.send_chunk        = demux_aud_send_chunk;
  this->demux_plugin.seek              = demux_aud_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_aud_get_status;
  this->demux_plugin.get_stream_length = demux_aud_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_aud_get_capabilities;
  this->demux_plugin.get_optional_data = demux_aud_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: /* no reliable detection */
  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:

    if (!open_aud_file(this)) {
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

void *demux_aud_init_plugin (xine_t *xine, void *data) {
  demux_aud_class_t     *this;

  this = calloc(1, sizeof(demux_aud_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Westwood Studios AUD file demux plugin");
  this->demux_class.identifier      = "Westwood Studios AUD";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "aud";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
