/*
 * Copyright (C) 2000-2004 the xine project
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
 * FLI File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For information on the FLI format, as well as various traps to
 * avoid while programming a FLI decoder, visit:
 *   http://www.pcisys.net/~melanson/codecs/
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

#define FLI_HEADER_SIZE 128
#define FLI_HEADER_SIZE_MC 12  /* header size for Magic Carpet game FLIs */
#define FLI_FILE_MAGIC_1 0xAF11
#define FLI_FILE_MAGIC_2 0xAF12
#define FLI_FILE_MAGIC_3 0xAF13  /* for internal use only */
#define FLI_CHUNK_MAGIC_1 0xF1FA
#define FLI_CHUNK_MAGIC_2 0xF5FA
#define FLI_MC_PTS_INC 6000  /* pts increment for Magic Carpet game FLIs */

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  /* video information */
  xine_bmiheader       bih;
  unsigned char        fli_header[FLI_HEADER_SIZE];

  /* playback info */
  unsigned int         magic_number;
  unsigned int         speed;
  unsigned int         frame_pts_inc;
  unsigned int         frame_count;
  int64_t              pts_counter;

  off_t                stream_len;
} demux_fli_t;

typedef struct {
  demux_class_t     demux_class;
} demux_fli_class_t;

/* returns 1 if the FLI file was opened successfully, 0 otherwise */
static int open_fli_file(demux_fli_t *this) {

  if (_x_demux_read_header(this->input, this->fli_header, FLI_HEADER_SIZE) != FLI_HEADER_SIZE)
    return 0;

  /* validate the file */
  this->magic_number = _X_LE_16(&this->fli_header[4]);
  if ((this->magic_number != FLI_FILE_MAGIC_1) &&
      (this->magic_number != FLI_FILE_MAGIC_2))
    return 0;

  /* file is qualified; skip over the signature bytes in the stream */
  this->input->seek(this->input, FLI_HEADER_SIZE, SEEK_SET);

  /* check if this is a special FLI file from Magic Carpet game */
  if (_X_LE_16(&this->fli_header[16]) == FLI_CHUNK_MAGIC_1) {
    /* if the input is non-seekable, do not bother with playing the
     * special file type */
    if (INPUT_IS_SEEKABLE(this->input)) {
      this->input->seek(this->input, FLI_HEADER_SIZE_MC, SEEK_SET);
    } else {
      return 0;
    }

    /* use a contrived internal FLI type, 0xAF13 */
    this->magic_number = FLI_FILE_MAGIC_3;
  }

  this->frame_count = _X_LE_16(&this->fli_header[6]);
  this->bih.biWidth = _X_LE_16(&this->fli_header[8]);
  this->bih.biHeight = _X_LE_16(&this->fli_header[10]);

  this->speed = _X_LE_32(&this->fli_header[16]);
  if (this->magic_number == FLI_FILE_MAGIC_1) {
    /*
     * in this case, the speed (n) is number of 1/70s ticks between frames:
     *
     *  xine pts     n * frame #
     *  --------  =  -----------  => xine pts = n * (90000/70) * frame #
     *   90000           70
     *
     *  therefore, the frame pts increment = n * 1285.7
     */
     this->frame_pts_inc = this->speed * 1285.7;
  } else if (this->magic_number == FLI_FILE_MAGIC_2) {
    /*
     * in this case, the speed (n) is number of milliseconds between frames:
     *
     *  xine pts     n * frame #
     *  --------  =  -----------  => xine pts = n * 90 * frame #
     *   90000          1000
     *
     *  therefore, the frame pts increment = n * 90
     */
     this->frame_pts_inc = this->speed * 90;
  } else {
    /* special case for Magic Carpet FLIs which don't carry speed info */
    this->frame_pts_inc = FLI_MC_PTS_INC;
  }

  /* sanity check: the FLI file must have non-zero values for width, height,
   * and frame count */
  if ((!this->bih.biWidth) || (!this->bih.biHeight) || (!this->frame_count))
    return 0;

  if (this->magic_number == FLI_FILE_MAGIC_3)
      this->bih.biSize = sizeof(xine_bmiheader) + FLI_HEADER_SIZE_MC;
  else
      this->bih.biSize = sizeof(xine_bmiheader) + FLI_HEADER_SIZE;

  return 1;
}

static int demux_fli_send_chunk(demux_plugin_t *this_gen) {
  demux_fli_t *this = (demux_fli_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned char fli_buf[6];
  unsigned int chunk_size;
  unsigned int chunk_magic;
  off_t current_file_pos;

  current_file_pos = this->input->get_current_pos(this->input);

  /* get the chunk size nd magic number */
  if (this->input->read(this->input, fli_buf, 6) != 6) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  chunk_size = _X_LE_32(&fli_buf[0]);
  chunk_magic = _X_LE_16(&fli_buf[4]);

  if ((chunk_magic == FLI_CHUNK_MAGIC_1) ||
      (chunk_magic == FLI_CHUNK_MAGIC_2)) {

    /* send a buffer with only the chunk header */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_FLI;
    if( this->stream_len )
      buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->stream_len);
    buf->extra_info->input_time = this->pts_counter / 90;
    buf->pts = this->pts_counter;
    buf->size = 6;
    memcpy(buf->content, fli_buf, 6);
    this->video_fifo->put(this->video_fifo, buf);

    chunk_size -= 6;

    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_FLI;
      if( this->stream_len )
        buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->stream_len);
      buf->extra_info->input_time = this->pts_counter / 90;
      buf->pts = this->pts_counter;

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
      this->video_fifo->put(this->video_fifo, buf);
    }
    this->pts_counter += this->frame_pts_inc;
  } else
    this->input->seek(this->input, chunk_size, SEEK_CUR);

  return this->status;
}

static void demux_fli_send_headers(demux_plugin_t *this_gen) {
  demux_fli_t *this = (demux_fli_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
    this->frame_pts_inc);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to FLI decoder */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = this->frame_pts_inc;  /* initial video_step */
  buf->size = this->bih.biSize;
  memcpy(buf->content, &this->bih, this->bih.biSize);
  buf->type = BUF_VIDEO_FLI;
  this->video_fifo->put (this->video_fifo, buf);
}

static int demux_fli_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_fli_t *this = (demux_fli_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status      = DEMUX_OK;
    this->stream_len  = this->input->get_length(this->input);
    this->pts_counter = 0;
  }

  return this->status;
}

static int demux_fli_get_status (demux_plugin_t *this_gen) {
  demux_fli_t *this = (demux_fli_t *) this_gen;

  return this->status;
}

static int demux_fli_get_stream_length (demux_plugin_t *this_gen) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  int64_t end_pts;

  end_pts = this->frame_count;
  end_pts *= this->frame_pts_inc;
  end_pts /= 90;

  return (int)end_pts;
}

static uint32_t demux_fli_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_fli_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_fli_t    *this;

  this         = calloc(1, sizeof(demux_fli_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_fli_send_headers;
  this->demux_plugin.send_chunk        = demux_fli_send_chunk;
  this->demux_plugin.seek              = demux_fli_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_fli_get_status;
  this->demux_plugin.get_stream_length = demux_fli_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_fli_get_capabilities;
  this->demux_plugin.get_optional_data = demux_fli_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_fli_file(this)) {
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
  demux_fli_class_t     *this;

  this = calloc(1, sizeof(demux_fli_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Autodesk Animator FLI/FLC demux plugin");
  this->demux_class.identifier      = "FLI/FLC";
  this->demux_class.mimetypes       = "video/x-flic: fli,flc: Autodesk FLIC files;";
  this->demux_class.extensions      = "fli flc";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_fli = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "fli", XINE_VERSION_CODE, &demux_info_fli, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
