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
 * TechnoTrend PVA File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the PVA file format, refer to this PDF:
 *   http://www.technotrend.de/download/av_format_v1.pdf
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/********** logging **********/
#define LOG_MODULE "demux_pva"
/* #define LOG_VERBOSE */
/* #define LOG */

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"

#define PVA_PREAMBLE_SIZE 8

#define WRAP_THRESHOLD       120000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  send_newpts;
  int64_t              last_pts[2];

  off_t                data_size;
} demux_pva_t;

typedef struct {
  demux_class_t     demux_class;
} demux_pva_class_t;


/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts( demux_pva_t *this, int64_t pts, int video ){
  int64_t diff;

  diff = pts - this->last_pts[video];

  if( pts &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    _x_demux_control_newpts(this->stream, pts, 0);

    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if( pts )
    this->last_pts[video] = pts;
}

/* returns 1 if the PVA file was opened successfully, 0 otherwise */
static int open_pva_file(demux_pva_t *this) {
  unsigned char preamble[PVA_PREAMBLE_SIZE];

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, preamble, PVA_PREAMBLE_SIZE) !=
    PVA_PREAMBLE_SIZE)
    return 0;

  /* PVA file must start with signature "AV" */
  if ((preamble[0] != 'A') || (preamble[1] != 'V'))
    return 0;

  /* enforce the rule that stream ID must be either 1 or 2 */
  if ((preamble[2] != 1) && (preamble[2] != 2))
    return 0;

  /* counter on the first packet should be 0 */
  if (preamble[3] != 0)
    return 0;

  this->data_size = this->input->get_length(this->input);

  return 1;
}

static int demux_pva_send_chunk(demux_plugin_t *this_gen) {
  demux_pva_t *this = (demux_pva_t *) this_gen;

  buf_element_t *buf;
  int chunk_size;
  unsigned char preamble[PVA_PREAMBLE_SIZE];
  unsigned char pts_buf[4];
  off_t current_file_pos;
  int64_t pts;
  unsigned int flags, header_len;

  if (this->input->read(this->input, preamble, PVA_PREAMBLE_SIZE) !=
      PVA_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* make sure the signature is there */
  if ((preamble[0] != 'A') || (preamble[1] != 'V')) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  chunk_size = _X_BE_16(&preamble[6]);

  current_file_pos = this->input->get_current_pos(this->input);

  if (preamble[2] == 1) {

    /* video */

    /* load the pts if it is the first thing in the chunk */
    if (preamble[5] & 0x10) {
      if (this->input->read(this->input, pts_buf, 4) != 4) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }
      chunk_size -= 4;
      pts = _X_BE_32(&pts_buf[0]);
      check_newpts( this, pts, PTS_VIDEO );
    } else
      pts = 0;

    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_MPEG;
      buf->pts = pts;
      pts = 0;
      if( this->data_size )
        buf->extra_info->input_normpos = (int) ((double) current_file_pos * 65535 / this->data_size);
      buf->extra_info->input_time = buf->pts / 90;

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

      this->video_fifo->put (this->video_fifo, buf);
    }

  } else if (preamble[2] == 2) {

    /* audio */
    if(!this->audio_fifo) {
      this->input->seek(this->input, chunk_size, SEEK_CUR);
      return this->status;
    }

    /* mostly cribbed from demux_pes.c */
    /* validate start of packet */
    if (this->input->read(this->input, preamble, 6) != 6) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    if (_X_BE_32(&preamble[0]) != 0x000001C0) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    chunk_size = _X_BE_16(&preamble[4]);

    /* get next 3 header bytes */
    if (this->input->read(this->input, preamble, 3) != 3) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    flags = preamble[1];
    header_len = preamble[2];

    chunk_size -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      if (this->input->read(this->input, preamble, 5) != 5) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      pts = (int64_t)(preamble[0] & 0x0e) << 29 ;
      pts |= (_X_BE_16(&preamble[1]) & 0xFFFE) << 14;
      pts |= (_X_BE_16(&preamble[3]) & 0xFFFE) >> 1;

      header_len -= 5 ;

      check_newpts( this, pts, PTS_AUDIO );
    }

    /* skip rest of header */
    this->input->seek (this->input, header_len, SEEK_CUR);

    buf = this->input->read_block (this->input, this->audio_fifo, chunk_size);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    buf->type = BUF_AUDIO_MPEG;
    buf->pts = pts;

    if( this->data_size )
      buf->extra_info->input_normpos = (int) ((double) this->input->get_current_pos(this->input) *
                                              65535 / this->data_size);

    this->audio_fifo->put (this->audio_fifo, buf);

  } else {

    /* unknown, skip it */
    this->input->seek(this->input, chunk_size, SEEK_CUR);

  }

  return this->status;
}

static void demux_pva_send_headers(demux_plugin_t *this_gen) {
  demux_pva_t *this = (demux_pva_t *) this_gen;
  buf_element_t *buf;
  int n;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to video decoder (cribbed from demux_elem.c) */
  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  n = this->input->read (this->input, buf->mem, 2048);

  if (n<=0) {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
    return;
  }

  buf->size = n;

  buf->pts = 0;
  if( this->data_size )
    buf->extra_info->input_normpos = (int) ((double) this->input->get_current_pos(this->input) *
                                              65535 / this->data_size);
  buf->type = BUF_VIDEO_MPEG;

  buf->decoder_flags = BUF_FLAG_PREVIEW;

  this->video_fifo->put(this->video_fifo, buf);

  /* send init info to the audio decoder */
  if (this->audio_fifo) {

    buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
    buf->content = buf->mem;
    buf->type = BUF_DEMUX_BLOCK;

    n = this->input->read (this->input, buf->mem, 2048);

    if (n<=0) {
      buf->free_buffer (buf);
      this->status = DEMUX_FINISHED;
      return;
    }

    buf->size = n;

    buf->pts = 0;
    if( this->data_size )
      buf->extra_info->input_normpos = (int) ((double) this->input->get_current_pos(this->input) *
                                              65535 / this->data_size);
    buf->type = BUF_AUDIO_MPEG;

    buf->decoder_flags = BUF_FLAG_PREVIEW;

    this->video_fifo->put(this->audio_fifo, buf);
  }
}

#define SEEK_BUFFER_SIZE 1024
static int demux_pva_seek (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time, int playing) {

  demux_pva_t *this = (demux_pva_t *) this_gen;
  unsigned char seek_buffer[SEEK_BUFFER_SIZE];
  int found = 0;
  int i;

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  /* start from the start_pos */
  this->input->seek(this->input, start_pos, SEEK_SET);

  /* find the start of the next packet by searching for an 'A' followed
   * by a 'V' followed by either a 1 or a 2 */
  while (!found) {

    if (this->input->read(this->input, seek_buffer, SEEK_BUFFER_SIZE) !=
      SEEK_BUFFER_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    for (i = 0; i < SEEK_BUFFER_SIZE - 3; i++) {
      if ((seek_buffer[i + 0] == 'A') &&
          (seek_buffer[i + 1] == 'V') &&
          ((seek_buffer[i + 2] == 1) || (seek_buffer[i + 2] == 2))) {

        found = 1;
        break;
      }
    }

    /* rewind 3 bytes since the 3-byte marker may very well be split
     * across the boundary */
    if (!found)
      this->input->seek(this->input, -3, SEEK_CUR);
  }

  /* reposition file at new offset */
  this->input->seek(this->input, -(SEEK_BUFFER_SIZE - i), SEEK_CUR);

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    this->send_newpts = 1;

    this->status = DEMUX_OK;

  } else
    _x_demux_flush_engine(this->stream);

  return this->status;
}

static int demux_pva_get_status (demux_plugin_t *this_gen) {
  demux_pva_t *this = (demux_pva_t *) this_gen;

  return this->status;
}

static int demux_pva_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_pva_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_pva_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_pva_t    *this;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_pva_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_pva_send_headers;
  this->demux_plugin.send_chunk        = demux_pva_send_chunk;
  this->demux_plugin.seek              = demux_pva_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_pva_get_status;
  this->demux_plugin.get_stream_length = demux_pva_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_pva_get_capabilities;
  this->demux_plugin.get_optional_data = demux_pva_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_pva_file(this)) {
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

static void *init_plugin (xine_t *xine, void *data) {
  demux_pva_class_t     *this;

  this = calloc(1, sizeof(demux_pva_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("TechnoTrend PVA demux plugin");
  this->demux_class.identifier      = "TechnoTrend PVA";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "pva";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_pva = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "pva", XINE_VERSION_CODE, &demux_info_pva, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
