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
 * Nullsoft Video (NSV) file demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the NSV file format, visit:
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

#define LOG_MODULE "demux_nsv"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include <xine/buffer.h>

#define FOURCC_TAG BE_FOURCC
#define NSVf_TAG       FOURCC_TAG('N', 'S', 'V', 'f')
#define NSVs_TAG       FOURCC_TAG('N', 'S', 'V', 's')

#define BEEF 0xEFBE

#define NSV_MAX_RESYNC (1024 * 1024)
#define NSV_RESYNC_ERROR 0
#define NSV_RESYNC_BEEF  1
#define NSV_RESYNC_NSVf  2
#define NSV_RESYNC_NSVs  3

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_size;

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  unsigned int         video_type;
  int64_t              video_pts;
  unsigned int         audio_type;
  uint32_t             video_fourcc;
  uint32_t             audio_fourcc;

  int                  keyframe_found;
  int                  is_first_chunk;

  xine_bmiheader       bih;

  /* ultravox stuff */
  int                  is_ultravox;
  int                  ultravox_size;
  int                  ultravox_pos;
  int                  ultravox_first;
} demux_nsv_t;

typedef struct {
  demux_class_t     demux_class;
} demux_nsv_class_t;


static void nsv_parse_framerate(demux_nsv_t *this, uint8_t framerate)
{
  /* need 1 more byte */
  this->fps = framerate;
  if (this->fps & 0x80) {
    switch (this->fps & 0x7F) {
    case 1:
      /* 29.97 fps */
      this->frame_pts_inc = 3003;
      break;

    case 3:
      /* 23.976 fps */
      this->frame_pts_inc = 3753;
      break;

    case 5:
      /* 14.98 fps */
      this->frame_pts_inc = 6006;
      break;

    default:
      lprintf("unknown framerate: 0x%02X\n", this->fps);
      this->frame_pts_inc = 90000;
      break;
    }
  } else
    this->frame_pts_inc = 90000 / this->fps;

  lprintf("frame_pts_inc=%d\n", this->frame_pts_inc);
}

static off_t nsv_read(demux_nsv_t *this, uint8_t *buffer, off_t len) {

  if (this->is_ultravox != 2) {

    return this->input->read(this->input, buffer, len);

  } else {

    int ultravox_rest;
    int buffer_pos = 0;

    /* ultravox stuff */
    while (len) {
      ultravox_rest = this->ultravox_size - this->ultravox_pos;

      if (len > ultravox_rest) {
	uint8_t ultravox_buf[7];

	if (ultravox_rest) {
	  if (this->input->read(this->input, buffer + buffer_pos, ultravox_rest) != ultravox_rest)
	    return -1;
	  buffer_pos += ultravox_rest;
	  len -= ultravox_rest;
	}
	/* parse ultravox packet header */
	if (this->ultravox_first) {
	  /* only 6 bytes */
	  this->ultravox_first = 0;
	  ultravox_buf[0] = 0;
	  if (this->input->read(this->input, ultravox_buf + 1, 6) != 6)
	    return -1;
	} else {
	  if (this->input->read(this->input, ultravox_buf, 7) != 7)
	    return -1;
	}
	/* check signature */
	if ((ultravox_buf[0] != 0x00)  || (ultravox_buf[1] != 0x5A)) {
	  lprintf("lost ultravox sync\n");
	  return -1;
	}
	/* read packet payload len */
	this->ultravox_size = _X_BE_16(&ultravox_buf[5]);
	this->ultravox_pos = 0;
	lprintf("ultravox_size: %d\n", this->ultravox_size);
      } else {
	if (this->input->read(this->input, buffer + buffer_pos, len) != len)
	  return -1;
	buffer_pos += len;
	this->ultravox_pos += len;
	len = 0;
      }
    }
    return buffer_pos;
  }
}

static off_t nsv_seek(demux_nsv_t *this, off_t offset, int origin) {
  if (this->is_ultravox != 2) {

    return this->input->seek(this->input, offset, origin);

  } else {

    /* ultravox stuff */
    if (origin == SEEK_CUR) {
      uint8_t buffer[1024];

      while (offset) {
	if (offset > sizeof(buffer)) {
	  if (nsv_read(this, buffer, sizeof(buffer)) != sizeof(buffer))
	    return -1;
	  offset = 0;
	} else {
	  if (nsv_read(this, buffer, offset) != offset)
	    return -1;
	  offset -= sizeof(buffer);
	}
      }
      return 0;

    } else {
      /* not supported */
      return -1;
    }
  }
}

static int nsv_resync(demux_nsv_t *this) {
  int i;
  uint32_t tag = 0;

  for (i = 0; i < NSV_MAX_RESYNC; i++) {
    uint8_t byte;

    if (nsv_read(this, &byte, 1) != 1)
      return NSV_RESYNC_ERROR;

#ifdef LOG
    printf("%2X ", byte);
#endif
    tag = (tag << 8) | byte;

    if ((tag & 0x0000ffff) == BEEF) {
      lprintf("found BEEF after %d bytes\n", i + 1);
      return NSV_RESYNC_BEEF;
    } else if (tag == NSVs_TAG) {
      lprintf("found NSVs after %d bytes\n", i + 1);
      return NSV_RESYNC_NSVs;
    } else if (tag == NSVf_TAG) {
      lprintf("found NSVf after %d bytes\n", i + 1);
      return NSV_RESYNC_NSVf;
    }
  }
  lprintf("can't resync\n");
  return NSV_RESYNC_ERROR;
}


/* returns 1 if the NSV file was opened successfully, 0 otherwise */
static int open_nsv_file(demux_nsv_t *this) {
  unsigned char preview[28];
  int           NSVs_found = 0;

  if (_x_demux_read_header(this->input, preview, 4) != 4)
    return 0;

  /* check for a 'NSV' signature */
  if ( memcmp(preview, "Z\09", 3) == 0) {
    this->is_ultravox = preview[3];
    this->ultravox_first = 1;
  } else if ( memcmp(preview, "NSV", 3) != 0 )
    return 0;

  lprintf("NSV file detected, ultravox=%d\n", this->is_ultravox);

  this->data_size = this->input->get_length(this->input);

  while (!NSVs_found) {
    switch (nsv_resync(this)) {

    case NSV_RESYNC_NSVf:
      {
	uint32_t chunk_size;

	/* if there is a NSVs tag, load 24 more header bytes; load starting at
	 * offset 4 in buffer to keep header data in line with document */
	if (nsv_read(this, &preview[4], 24) != 24)
	  return 0;

	lprintf("found NSVf chunk\n");
	/*	this->data_size = _X_LE_32(&preview[8]);*/
	/*lprintf("data_size: %lld\n", this->data_size);*/

	/* skip the rest of the data */
	chunk_size = _X_LE_32(&preview[4]);
	nsv_seek(this, chunk_size - 28, SEEK_CUR);
      }
      break;

    case NSV_RESYNC_NSVs:

      /* fetch the remaining 15 header bytes of the first chunk to get the
       * relevant information */
      if (nsv_read(this, &preview[4], 15) != 15)
	return 0;

      this->video_fourcc = _X_ME_32(&preview[4]);
      if (_x_is_fourcc(&preview[4], "NONE"))
	this->video_type = 0;
      else
      {
	this->video_type = _x_fourcc_to_buf_video(this->video_fourcc);
	if (!this->video_type)
	  _x_report_video_fourcc (this->stream->xine, LOG_MODULE, this->video_fourcc);
      }

      this->audio_fourcc = _X_ME_32(&preview[8]);
      if (_x_is_fourcc(&preview[8], "NONE"))
	this->audio_type = 0;
      else
      {
	this->audio_type = _x_formattag_to_buf_audio(this->audio_fourcc);
	if (!this->audio_type)
	  _x_report_audio_format_tag (this->stream->xine, LOG_MODULE, this->audio_fourcc);
      }

      this->bih.biSize = sizeof(this->bih);
      this->bih.biWidth = _X_LE_16(&preview[12]);
      this->bih.biHeight = _X_LE_16(&preview[14]);
      this->bih.biCompression = this->video_fourcc;
      this->video_pts = 0;

      /* may not be true, but set it for the time being */
      this->frame_pts_inc = 3003;

      lprintf("video: %c%c%c%c, buffer type %08X, %dx%d\n",
	      preview[4],
	      preview[5],
	      preview[6],
	      preview[7],
	      this->video_type,
	      this->bih.biWidth,
	      this->bih.biHeight);
      lprintf("audio: %c%c%c%c, buffer type %08X\n",
	      preview[8],
	      preview[9],
	      preview[10],
	      preview[11],
	      this->audio_type);

      nsv_parse_framerate(this, preview[16]);
      NSVs_found = 1;
      break;

    case NSV_RESYNC_ERROR:
      return 0;

    }
  }

  this->is_first_chunk = 1;
  return 1;
}

static int nsv_parse_payload(demux_nsv_t *this, int video_size, int audio_size) {
  buf_element_t *buf;
  off_t current_file_pos;

  lprintf("video_size=%d, audio_size=%d\n", video_size, audio_size);
  current_file_pos = this->input->get_current_pos(this->input);

  while (video_size) {
    int buf_num = 0;

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    if (video_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = video_size;
    video_size -= buf->size;

    if (nsv_read(this, buf->content, buf->size) != buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* HACK: parse decoder data to detect a keyframe */
    if ((buf_num == 0) && (!this->keyframe_found)) {
      switch (this->video_type) {
      case BUF_VIDEO_VP31:
	if ((buf->content[1] == 0x00) && (buf->content[2] == 0x08)) {
	  lprintf("keyframe detected !\n");
	  this->keyframe_found = 1;
	}
	break;

      case BUF_VIDEO_VP6:
	if ((buf->content[1] & 0x0e) == 0x06) {
	  lprintf("keyframe detected !\n");
	  this->keyframe_found = 1;
	}
	break;

      default:
	/* don't know how to detect keyframe */
	this->keyframe_found = 1;
      }
      buf_num++;
    }

    if (this->keyframe_found) {
      buf->type = this->video_type;
      if( this->data_size )
	buf->extra_info->input_normpos = (int)((double)current_file_pos * 65535 / this->data_size);
      buf->extra_info->input_time = this->video_pts / 90;
      buf->pts = this->video_pts;
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = this->frame_pts_inc;

      if (!video_size)
	buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->video_fifo->put(this->video_fifo, buf);
    } else {
      buf->free_buffer(buf);
    }
  }

  while (audio_size) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    if (audio_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = audio_size;
    audio_size -= buf->size;

    if (nsv_read(this, buf->content, buf->size) != buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    buf->type = this->audio_type;

    if( this->data_size )
      buf->extra_info->input_normpos = (int)((double)current_file_pos * 65535 / this->data_size);
    buf->extra_info->input_time = this->video_pts / 90;
    buf->pts = this->video_pts;

    buf->decoder_flags |= BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = this->frame_pts_inc;

    if (!audio_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put(this->audio_fifo, buf);
  }

  this->video_pts += this->frame_pts_inc;

  return this->status;
}


static int demux_nsv_send_chunk(demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  uint8_t  buffer[15];
  off_t    current_file_pos;
  int      video_size;
  int      audio_size;
  int      chunk_type;

  current_file_pos = this->input->get_current_pos(this->input);

  lprintf("dispatching video & audio chunks...\n");

  if (this->is_first_chunk) {
    chunk_type = NSV_RESYNC_BEEF;
    this->is_first_chunk = 0;
  } else {
    chunk_type = nsv_resync(this);
  }

  switch (chunk_type) {
  case NSV_RESYNC_NSVf:
    /* do nothing */
    break;

  case NSV_RESYNC_NSVs:
    /* skip header */
    if (nsv_read(this, buffer, 15) != 15)
      return 0;
    nsv_parse_framerate(this, buffer[12]);

    /* fall thru */

  case NSV_RESYNC_BEEF:
    if (nsv_read(this, buffer, 5) != 5) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    video_size = _X_LE_32(&buffer[0]);
    video_size >>= 4;
    video_size &= 0xFFFFF;
    audio_size = _X_LE_16(&buffer[3]);

    nsv_parse_payload(this, video_size, audio_size);
    break;

  case NSV_RESYNC_ERROR:
    this->status = DEMUX_FINISHED;
    break;
  }

  return this->status;
}

static void demux_nsv_send_headers(demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC,
		     this->video_fourcc);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC,
		     this->audio_fourcc);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO,
    (this->video_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
    (this->audio_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
    this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
    this->bih.biHeight);


  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to the video decoder */
  if (this->video_type) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = this->frame_pts_inc;
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }
}

static int demux_nsv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  lprintf("starting demuxer\n");
  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_nsv_get_status (demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  return this->status;
}

static int demux_nsv_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_nsv_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_nsv_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_nsv_t    *this;

  this         = calloc(1, sizeof(demux_nsv_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_nsv_send_headers;
  this->demux_plugin.send_chunk        = demux_nsv_send_chunk;
  this->demux_plugin.seek              = demux_nsv_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_nsv_get_status;
  this->demux_plugin.get_stream_length = demux_nsv_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_nsv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_nsv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_nsv_file(this)) {
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

static void *demux_nsv_init_plugin (xine_t *xine, void *data) {
  demux_nsv_class_t     *this;

  this = calloc(1, sizeof(demux_nsv_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Nullsoft Video demux plugin");
  this->demux_class.identifier      = "Nullsoft NSV";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "nsv";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_nsv = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "nsv", XINE_VERSION_CODE, &demux_info_nsv, demux_nsv_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
