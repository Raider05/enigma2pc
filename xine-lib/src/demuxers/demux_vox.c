/*
 * Copyright (C) 2001-2003 the xine project
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
 * VOX Demuxer by Mike Melanson (melanson@pcisys.net)
 * This a demuxer for .vox files containing raw Dialogic ADPCM data.
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

#define DIALOGIC_SAMPLERATE 8000

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;
} demux_vox_t;

typedef struct {
  demux_class_t     demux_class;
} demux_vox_class_t;

static int demux_vox_send_chunk (demux_plugin_t *this_gen) {
  demux_vox_t *this = (demux_vox_t *) this_gen;

  buf_element_t *buf = NULL;
  off_t current_file_pos;
  int64_t audio_pts;
  int bytes_read;

  current_file_pos = this->input->get_current_pos(this->input);
  audio_pts = current_file_pos;
  /* each byte is 2 samples */
  audio_pts *= 2 * 90000;
  audio_pts /= DIALOGIC_SAMPLERATE;

  /* read a buffer-sized block from the stream; if there is less than a
   * buffer of data, send whatever there is; if there are no bytes returned,
   * demux is finished */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_DIALOGIC_IMA;
  bytes_read = this->input->read(this->input, buf->content, buf->max_size);
  if (bytes_read <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  } else if (bytes_read < buf->max_size)
    buf->size = bytes_read;
  else
    buf->size = buf->max_size;

  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) current_file_pos *
                                     65535 / this->input->get_length (this->input) );
  buf->extra_info->input_time = audio_pts / 90;
  buf->pts = audio_pts;
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_vox_send_headers(demux_plugin_t *this_gen) {
  demux_vox_t *this = (demux_vox_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       DIALOGIC_SAMPLERATE);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, 16);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_DIALOGIC_IMA;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = DIALOGIC_SAMPLERATE;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = 1;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_vox_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_vox_t *this = (demux_vox_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* start at the beginning of the file */
    this->input->seek(this->input, 0, SEEK_SET);
  }

  return this->status;
}

static int demux_vox_get_status (demux_plugin_t *this_gen) {
  demux_vox_t *this = (demux_vox_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_vox_get_stream_length (demux_plugin_t *this_gen) {
  demux_vox_t *this = (demux_vox_t *) this_gen;

  return (int)((int64_t)this->input->get_length(this->input)
               * 2 * 1000 / DIALOGIC_SAMPLERATE);
}

static uint32_t demux_vox_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_vox_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input) {

  demux_vox_t   *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    return NULL;

  case METHOD_EXPLICIT:
  case METHOD_BY_MRL:
    break;

  default:
    return NULL;
  }

  this         = calloc(1, sizeof(demux_vox_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_vox_send_headers;
  this->demux_plugin.send_chunk        = demux_vox_send_chunk;
  this->demux_plugin.seek              = demux_vox_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_vox_get_status;
  this->demux_plugin.get_stream_length = demux_vox_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_vox_get_capabilities;
  this->demux_plugin.get_optional_data = demux_vox_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  return &this->demux_plugin;
}

void *demux_vox_init_plugin (xine_t *xine, void *data) {
  demux_vox_class_t     *this;

  this = calloc(1, sizeof(demux_vox_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Dialogic VOX file demux plugin");
  this->demux_class.identifier      = "VOX";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "vox";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
