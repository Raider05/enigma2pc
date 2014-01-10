/*
 * Copyright (C) 2000-2012 the xine project
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
 * YUV4MPEG2 File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the YUV4MPEG2 file format and associated
 * tools, visit:
 *   http://mjpeg.sourceforge.net/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"

#define Y4M_SIGNATURE_SIZE 9
#define Y4M_SIGNATURE "YUV4MPEG2"
#define Y4M_FRAME_SIGNATURE_SIZE 6
#define Y4M_FRAME_SIGNATURE "FRAME\x0A"
/* number of header bytes is completely arbitrary */
#define Y4M_HEADER_BYTES 100

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                data_size;

  xine_bmiheader       bih;

  int                  fps_n;
  int                  fps_d;
  int                  aspect_n;
  int                  aspect_d;
  int                  progressive;
  int                  top_field_first;
  int                  color_matrix;

  unsigned int         frame_pts_inc;
  unsigned int         frame_size;

  int                  seek_flag;
} demux_yuv4mpeg2_t;

typedef struct {
  demux_class_t     demux_class;
} demux_yuv4mpeg2_class_t;

/* returns 1 if the YUV4MPEG2 file was opened successfully, 0 otherwise */
static int open_yuv4mpeg2_file(demux_yuv4mpeg2_t *this) {
  char header[Y4M_HEADER_BYTES+1];
  char *header_ptr, *header_endptr, *header_end;

  this->bih.biWidth = this->bih.biHeight = this->fps_n = this->fps_d =
    this->aspect_n = this->aspect_d = this->progressive =
    this->top_field_first = this->data_start = 0;

  this->color_matrix = 4; /* undefined, mpeg range */

  if (_x_demux_read_header(this->input, header, Y4M_HEADER_BYTES) != Y4M_HEADER_BYTES)
    return 0;

  /* check for the Y4M signature */
  if (memcmp(header, Y4M_SIGNATURE, Y4M_SIGNATURE_SIZE) != 0)
    return 0;

  /* null terminate the read data */
  header[Y4M_HEADER_BYTES] = '\0';

  /* check for stream header terminator */
  if ((header_end = strchr(header, '\n')) == NULL)
    return 0;

  /* read tagged fields in stream header */
  header_ptr = &header[Y4M_SIGNATURE_SIZE];
  while (header_ptr < header_end) {
    /* tagged fields should all start with a space */
    if(*header_ptr != ' ')
      break;
    else
      header_ptr++;

    switch (*header_ptr) {
      case 'W':
        /* read the width */
        this->bih.biWidth = strtol(header_ptr + 1, &header_endptr, 10);
        if(header_endptr == header_ptr + 1)
          return 0;
        else
          header_ptr = header_endptr;
        break;
      case 'H':
        /* read the height */
        this->bih.biHeight = strtol(header_ptr + 1, &header_endptr, 10);
        if (header_endptr == header_ptr + 1)
          return 0;
        else
          header_ptr = header_endptr;
        break;
      case 'I':
        /* read interlacing spec */
        switch (*(header_ptr + 1)) {
          case 'p':
            this->progressive = 1;
            break;
          case 't':
            this->top_field_first = 1;
            break;
          case 'b':
          case '?':
          default:
            break;
        }
        header_ptr += 2;
        break;
      case 'F':
        /* read frame rate - stored as a ratio
         * numberator */
        this->fps_n = strtol(header_ptr + 1, &header_endptr, 10);
        if ((header_endptr == header_ptr + 1) || (*header_endptr != ':'))
          return 0;
        else
          header_ptr = header_endptr;

        /* denominator */
        this->fps_d = strtol(header_ptr + 1, &header_endptr, 10);
        if (header_endptr == header_ptr + 1)
          return 0;
        else
          header_ptr = header_endptr;

        break;
      case 'A':
        /* read aspect ratio - stored as a ratio(!)
         * numerator */
        this->aspect_n = strtol(header_ptr + 1, &header_endptr, 10);
        if ((header_endptr == header_ptr + 1) || (*header_endptr != ':'))
          return 0;
        else
          header_ptr = header_endptr;

        /* denominator */
        this->aspect_d = strtol(header_ptr + 1, &header_endptr, 10);
        if (header_endptr == header_ptr + 1)
          return 0;
        else
          header_ptr = header_endptr;

        break;
      case 'X':
        /* private extra info */
        if (!strncasecmp (header_ptr + 1, "XINE_CM=", 8)) {
          int i = strtol(header_ptr + 9, &header_endptr, 10);
          if (header_endptr > header_ptr + 9) {
            this->color_matrix = i;
            header_ptr = header_endptr;
            break;
          }
        }
        /* fall through */
      default:
        /* skip whatever this is */
        while ((*header_ptr != ' ') && (header_ptr < header_end))
          header_ptr++;
    }
  }

  /* make sure all the data was found */
  if (!this->bih.biWidth || !this->bih.biHeight || !this->fps_n || !this->fps_d)
    return 0;

  /* compute the size of an individual frame */
  this->frame_size = this->bih.biWidth * this->bih.biHeight * 3 / 2;

  /* pts difference between frames */
  this->frame_pts_inc = (90000 * this->fps_d) / this->fps_n;

  /* finally, look for the first frame */
  char *data_start_ptr = memmem(header_ptr, Y4M_HEADER_BYTES, "FRAME", 5);

  /* make sure the first frame was found */
  if ( !data_start_ptr )
    return 0;

  this->data_start = data_start_ptr - header;

  /* compute size of all frames */
  if (INPUT_IS_SEEKABLE(this->input)) {
    this->data_size = this->input->get_length(this->input) -
      this->data_start;
  }

  /* file is qualified; seek to first frame */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  return 1;
}

static int demux_yuv4mpeg2_send_chunk(demux_plugin_t *this_gen) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  /* validate that this is an actual frame boundary */
  {
    uint8_t preamble[Y4M_FRAME_SIGNATURE_SIZE];
    if (this->input->read(this->input, preamble, Y4M_FRAME_SIGNATURE_SIZE) !=
	Y4M_FRAME_SIGNATURE_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    if (memcmp(preamble, Y4M_FRAME_SIGNATURE, Y4M_FRAME_SIGNATURE_SIZE) !=
	0) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
  }

  /* load and dispatch the raw frame */
  int bytes_remaining = this->frame_size;
  off_t current_file_pos =
    this->input->get_current_pos(this->input) - this->data_start;
  int64_t pts = current_file_pos;
  pts /= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
  pts *= this->frame_pts_inc;

  /* reset the pts after a seek */
  if (this->seek_flag) {
    _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
    this->seek_flag = 0;
  }

  while(bytes_remaining) {
    buf_element_t *buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_I420;
    if( this->data_size )
      buf->extra_info->input_normpos = (int)((double) current_file_pos * 65535 / this->data_size);
    buf->extra_info->input_time = pts / 90;
    buf->pts = pts;

    buf->decoder_flags |= BUF_FLAG_COLOR_MATRIX;
    buf->decoder_info[4] = this->color_matrix;

    buf->size = MIN(bytes_remaining, buf->max_size);
    bytes_remaining -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!bytes_remaining)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
    this->video_fifo->put(this->video_fifo, buf);
  }

  return this->status;
}

static void demux_yuv4mpeg2_send_headers(demux_plugin_t *this_gen) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
                       this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
                       this->bih.biHeight);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  buf_element_t *buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = this->frame_pts_inc;  /* initial video step */

  if(this->aspect_n && this->aspect_d) {
    buf->decoder_flags  |= BUF_FLAG_ASPECT;
    buf->decoder_info[1] = this->bih.biWidth * this->aspect_n;
    buf->decoder_info[2] = this->bih.biHeight * this->aspect_d;
  }

  buf->decoder_info[3] = this->progressive;
  buf->decoder_info[4] = this->top_field_first;

  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = BUF_VIDEO_I420;
  this->video_fifo->put (this->video_fifo, buf);
}

static int demux_yuv4mpeg2_seek (demux_plugin_t *this_gen,
                                 off_t start_pos, int start_time, int playing) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
  start_time /= 1000;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  if (INPUT_IS_SEEKABLE(this->input)) {

     /* YUV4MPEG2 files are essentially constant bit-rate video. Seek along
      * the calculated frame boundaries. Divide the requested seek offset
      * by the frame size integer-wise to obtain the desired frame number
      * and then multiply the frame number by the frame size to get the
      * starting offset. Add the data_start offset to obtain the final
      * offset. */

    start_pos /= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
    start_pos *= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_yuv4mpeg2_get_status (demux_plugin_t *this_gen) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  return this->status;
}

static int demux_yuv4mpeg2_get_stream_length (demux_plugin_t *this_gen) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  return (int)(((int64_t) this->data_size * 1000 * this->fps_d) /
               ((this->frame_size + Y4M_FRAME_SIGNATURE_SIZE) * this->fps_n));
}

static uint32_t demux_yuv4mpeg2_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_yuv4mpeg2_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {
  demux_yuv4mpeg2_t *this = calloc(1, sizeof(demux_yuv4mpeg2_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_yuv4mpeg2_send_headers;
  this->demux_plugin.send_chunk        = demux_yuv4mpeg2_send_chunk;
  this->demux_plugin.seek              = demux_yuv4mpeg2_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_yuv4mpeg2_get_status;
  this->demux_plugin.get_stream_length = demux_yuv4mpeg2_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_yuv4mpeg2_get_capabilities;
  this->demux_plugin.get_optional_data = demux_yuv4mpeg2_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_yuv4mpeg2_file(this)) {
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
  demux_yuv4mpeg2_class_t     *this;

  this = calloc(1, sizeof(demux_yuv4mpeg2_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("YUV4MPEG2 file demux plugin");
  this->demux_class.identifier      = "YUV4MPEG2";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "y4m";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_yuv4mpeg2 = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "yuv4mpeg2", XINE_VERSION_CODE, &demux_info_yuv4mpeg2, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
