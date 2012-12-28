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
 * demultiplexer for elementary mpeg streams
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define LOG_MODULE "demux_elem"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>

#define NUM_PREVIEW_BUFFERS 50
#define SCRATCH_SIZE 256

typedef struct {
  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  uint32_t             blocksize;
} demux_mpeg_elem_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_mpeg_elem_class_t;

static int demux_mpeg_elem_next (demux_mpeg_elem_t *this, int preview_mode) {
  buf_element_t *buf;
  uint32_t blocksize;
  off_t done;

  lprintf ("next piece\n");
  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  blocksize = (this->blocksize ? this->blocksize : buf->max_size);
  done = this->input->read(this->input, buf->mem, blocksize);
  lprintf ("read size = %"PRId64"\n", done);

  if (done <= 0) {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->size                  = done;
  buf->content               = buf->mem;
  buf->pts                   = 0;
  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );
  buf->type                  = BUF_VIDEO_MPEG;

  if (preview_mode)
    buf->decoder_flags = BUF_FLAG_PREVIEW;

  this->video_fifo->put(this->video_fifo, buf);

  return 1;
}

static int demux_mpeg_elem_send_chunk (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  if (!demux_mpeg_elem_next(this, 0))
    this->status = DEMUX_FINISHED;
  return this->status;
}

static int demux_mpeg_elem_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  return this->status;
}


static void demux_mpeg_elem_send_headers (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->blocksize = this->input->get_blocksize(this->input);

  _x_demux_control_start(this->stream);

  if (INPUT_IS_SEEKABLE(this->input)) {
    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->input->seek (this->input, 0, SEEK_SET);

    this->status = DEMUX_OK ;
    while ((num_buffers > 0) && (this->status == DEMUX_OK)) {
      demux_mpeg_elem_next(this, 1);
      num_buffers--;
    }
  }

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
}

static int demux_mpeg_elem_seek (demux_plugin_t *this_gen,
				  off_t start_pos, int start_time, int playing) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  this->status = DEMUX_OK;

  if (playing)
    _x_demux_flush_engine(this->stream);

  if (INPUT_IS_SEEKABLE(this->input)) {

    /* FIXME: implement time seek */

    if (start_pos != this->input->seek (this->input, start_pos, SEEK_SET)) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    lprintf ("seeking to %"PRId64"\n", start_pos);
  }

  /*
   * now start demuxing
   */
  this->status = DEMUX_OK;

  return this->status;
}

static int demux_mpeg_elem_get_stream_length(demux_plugin_t *this_gen) {
  return 0 ; /*FIXME: implement */
}

static uint32_t demux_mpeg_elem_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpeg_elem_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_mpeg_elem_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t scratch[SCRATCH_SIZE];
    int i, read, found;

    read = _x_demux_read_header(input, scratch, SCRATCH_SIZE);
    if (!read)
      return NULL;

    found = 0;
    for (i = 0; i < read - 4; i++) {
      lprintf ("%02x %02x %02x %02x\n", scratch[i], scratch[i+1], scratch[i+2], scratch[i+3]);

      if ((scratch[i] == 0x00) && (scratch[i+1] == 0x00) && (scratch[i+2] == 0x01)) {
	if (scratch[i+3] == 0xb3) {
	  found = 1;
	  lprintf ("found header at offset 0x%x\n", i);
	  break;
	} else
	  return NULL;
      }
    }

    if (found == 0)
      return NULL;
    lprintf ("input accepted.\n");
  }
  break;

  case METHOD_BY_MRL:
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  this         = calloc(1, sizeof(demux_mpeg_elem_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_elem_send_headers;
  this->demux_plugin.send_chunk        = demux_mpeg_elem_send_chunk;
  this->demux_plugin.seek              = demux_mpeg_elem_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_mpeg_elem_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_elem_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpeg_elem_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpeg_elem_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_mpeg_elem_class_t     *this;

  this = calloc(1, sizeof(demux_mpeg_elem_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Elementary MPEG stream demux plugin");
  this->demux_class.identifier      = "MPEG_ELEM";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "mpv";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_elem = {
  0                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "elem", XINE_VERSION_CODE, &demux_info_elem, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
