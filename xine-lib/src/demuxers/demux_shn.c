/*
 * Copyright (C) 2005 the xine project
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define LOG_MODULE "demux_shn"
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

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_shn_t;

typedef struct {
  demux_class_t     demux_class;
} demux_shn_class_t;


static int open_shn_file(demux_shn_t *this) {
  uint8_t peak[4];

  if (_x_demux_read_header(this->input, peak, 4) != 4)
      return 0;

  if ((peak[0] != 'a') || (peak[1] != 'j') ||
      (peak[2] != 'k') || (peak[3] != 'g')) {
    return 0;
  }

  return 1;
}

static int demux_shn_send_chunk(demux_plugin_t *this_gen) {
  demux_shn_t *this = (demux_shn_t *) this_gen;
  int bytes_read;

  buf_element_t *buf = NULL;

  /* just load an entire buffer from wherever the audio happens to be */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_SHORTEN;
  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );
  buf->pts = 0;

  bytes_read = this->input->read(this->input, buf->content, buf->max_size);
  if (bytes_read <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  } else
    buf->size = bytes_read;

  /* each buffer stands on its own */
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_shn_send_headers(demux_plugin_t *this_gen) {
  demux_shn_t *this = (demux_shn_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_SHORTEN;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    /* this is a guess at the correct parameters */
    buf->decoder_info[1] = 44100;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = 2;
    buf->content = NULL;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_shn_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_shn_t *this = (demux_shn_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_shn_get_status (demux_plugin_t *this_gen) {
  demux_shn_t *this = (demux_shn_t *) this_gen;

  return this->status;
}

static int demux_shn_get_stream_length (demux_plugin_t *this_gen) {
//  demux_shn_t *this = (demux_shn_t *) this_gen;

  return 0;
}

static uint32_t demux_shn_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_shn_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_shn_t    *this;

  this         = calloc(1, sizeof(demux_shn_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_shn_send_headers;
  this->demux_plugin.send_chunk        = demux_shn_send_chunk;
  this->demux_plugin.seek              = demux_shn_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_shn_get_status;
  this->demux_plugin.get_stream_length = demux_shn_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_shn_get_capabilities;
  this->demux_plugin.get_optional_data = demux_shn_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:
    if (!open_shn_file(this)) {
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

void *demux_shn_init_plugin (xine_t *xine, void *data) {
  demux_shn_class_t     *this;

  this = calloc(1, sizeof(demux_shn_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Shorten demux plugin");
  this->demux_class.identifier      = "Shorten";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "shn";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
