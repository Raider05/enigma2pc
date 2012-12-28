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
 * RealAudio File Demuxer by Mike Melanson (melanson@pcisys.net)
 *     improved by James Stembridge (jstembridge@users.sourceforge.net)
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

#include "real_common.h"

#define RA_FILE_HEADER_PREV_SIZE 22

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned int         fourcc;
  unsigned int         audio_type;

  unsigned short       block_align;

  uint8_t              seek_flag:1; /* this is set when a seek just occurred */

  off_t                data_start;
  off_t                data_size;

  uint32_t             cfs;
  uint16_t             w, h;
  int                  frame_len;
  size_t               frame_size;
  uint8_t             *frame_buffer;

  unsigned char       *header;
  unsigned int         header_size;
} demux_ra_t;

typedef struct {
  demux_class_t     demux_class;
} demux_ra_class_t;

/* Map flavour to bytes per second */
static const int sipr_fl2bps[4] = {813, 1062, 625, 2000}; // 6.5, 8.5, 5, 16 kbit per second

/* returns 1 if the RealAudio file was opened successfully, 0 otherwise */
static int open_ra_file(demux_ra_t *this) {
  uint8_t file_header[RA_FILE_HEADER_PREV_SIZE];

  /* check the signature */
  if (_x_demux_read_header(this->input, file_header, RA_FILE_HEADER_PREV_SIZE) !=
      RA_FILE_HEADER_PREV_SIZE)
    return 0;

  if ( memcmp(file_header, ".ra", 3) != 0 )
    return 0;

  /* read version */
  const uint16_t version = _X_BE_16(&file_header[0x04]);

  /* read header size according to version */
  if (version == 3)
    this->header_size = _X_BE_16(&file_header[0x06]) + 8;
  else if (version == 4)
    this->header_size = _X_BE_32(&file_header[0x12]) + 16;
  else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unknown version number %d\n", version);
    return 0;
  }

  /* allocate for and read header data */
  this->header = malloc(this->header_size);

  if (!this->header || _x_demux_read_header(this->input, this->header, this->header_size) != this->header_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unable to read header\n");
    free(this->header);
    return 0;
  }

  off_t offset;
  /* read header data according to version */
  if((version == 3) && (this->header_size >= 32)) {
    this->data_size = _X_BE_32(&this->header[0x12]);

    this->block_align = 240;

    offset = 0x16;
  } else if(this->header_size >= 72) {
    this->data_size = _X_BE_32(&this->header[0x1C]);

    this->block_align = _X_BE_16(&this->header[0x2A]);

    if(this->header[0x3D] == 4)
      this->fourcc = _X_ME_32(&this->header[0x3E]);
    else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_realaudio: invalid fourcc size %d\n", this->header[0x3D]);
      free(this->header);
      return 0;
    }

    offset = 0x45;
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: header too small\n");
    free(this->header);
    return 0;
  }

  /* Read title */
  {
    const uint8_t len = this->header[offset];
    if(len && ((offset+len+2) < this->header_size)) {
      _x_meta_info_n_set(this->stream, XINE_META_INFO_TITLE,
			 &this->header[offset+1], len);
      offset += len+1;
    } else
      offset++;
  }

  /* Author */
  {
    const uint8_t len = this->header[offset];
    if(len && ((offset+len+1) < this->header_size)) {
      _x_meta_info_n_set(this->stream, XINE_META_INFO_ARTIST,
			 &this->header[offset+1], len);
      offset += len+1;
    } else
      offset++;
  }

  /* Copyright/Date */
  {
    const uint8_t len = this->header[offset];
    if(len && ((offset+len) <= this->header_size)) {
      _x_meta_info_n_set(this->stream, XINE_META_INFO_YEAR,
			 &this->header[offset+1], len);
      offset += len+1;
    } else
      offset++;
  }

  /* Fourcc for version 3 comes after meta info */
  if(version == 3) {
    if (((offset+7) <= this->header_size)) {
      if(this->header[offset+2] == 4)
        this->fourcc = _X_ME_32(&this->header[offset+3]);
      else {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	        "demux_realaudio: invalid fourcc size %d\n", this->header[offset+2]);
        free(this->header);
        return 0;
      }
    } else {
      this->fourcc = ME_FOURCC('l', 'p', 'c', 'J');
    }
  }

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, this->fourcc);
  this->audio_type = _x_formattag_to_buf_audio(this->fourcc);

  if (version == 4) {
    const uint16_t sps = _X_BE_16 (this->header+44) ? : 1;
    this->w           = _X_BE_16 (this->header+42);
    this->h           = _X_BE_16 (this->header+40);
    this->cfs         = _X_BE_32 (this->header+24);

    if (this->w < 0x8000 && this->h < 0x8000) {
      uint64_t fs;
      this->frame_len = this->w * this->h;
      fs = (uint64_t) this->frame_len * sps;
      if (fs < 0x80000000) {
        this->frame_size = fs;
        this->frame_buffer = calloc(this->frame_size, 1);
      }
    }
    if (! this->frame_buffer) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: malloc failed\n");
      return 0;
    }

    if (this->audio_type == BUF_AUDIO_28_8 || this->audio_type == BUF_AUDIO_SIPRO)
      this->block_align = this->cfs;
  }

  /* seek to start of data */
  this->data_start = this->header_size;
  if (this->input->seek(this->input, this->data_start, SEEK_SET) !=
      this->data_start) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unable to seek to data start\n");
    return 0;
  }

  if( !this->audio_type )
    this->audio_type = BUF_AUDIO_UNKNOWN;

  return 1;
}

static int demux_ra_send_chunk(demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  off_t current_normpos = 0;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  if( this->input->get_length (this->input) )
    current_normpos = (int)( (double) (this->input->get_current_pos (this->input) - this->data_start) *
                      65535 / this->data_size );

  const int64_t current_pts = 0;  /* let the engine sort out the pts for now */

  if (this->seek_flag) {
    _x_demux_control_newpts(this->stream, current_pts, BUF_FLAG_SEEK);
    this->seek_flag = 0;
  }

  if (this->audio_type == BUF_AUDIO_28_8 || this->audio_type == BUF_AUDIO_SIPRO) {
    if (this->audio_type == BUF_AUDIO_SIPRO) {
      if(this->input->read(this->input, this->frame_buffer, this->frame_len) < this->frame_len) {
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"demux_realaudio: failed to read audio chunk\n");

	this->status = DEMUX_FINISHED;
	return this->status;
      }
      demux_real_sipro_swap (this->frame_buffer, this->frame_len * 2 / 96);
    } else {
      int x, y;

      for (y = 0; y < this->h; y++)
	for (x = 0; x < this->h / 2; x++) {
	  const int pos = x * 2 * this->w + y * this->cfs;
	  if(this->input->read(this->input, this->frame_buffer + pos,
			       this->cfs) < this->cfs) {
	    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		    "demux_realaudio: failed to read audio chunk\n");

	    this->status = DEMUX_FINISHED;
	    return this->status;
	  }
	}
    }

    _x_demux_send_data(this->audio_fifo,
		       this->frame_buffer, this->frame_size,
		       current_pts, this->audio_type, 0,
		       current_normpos, current_pts / 90, 0, 0);
  } else if(_x_demux_read_send_data(this->audio_fifo, this->input, this->block_align,
                             current_pts, this->audio_type, 0, current_normpos,
                             current_pts / 90, 0, 0) < 0) {
    this->status = DEMUX_FINISHED;
  }

  return this->status;
}

static void demux_ra_send_headers(demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, this->fourcc);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;

    buf->size = MIN(this->header_size, buf->max_size);

    memcpy(buf->content, this->header, buf->size);

    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_ra_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_ra_t *this = (demux_ra_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

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
    start_pos /= this->block_align;
    start_pos *= this->block_align;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  return this->status;
}


static void demux_ra_dispose (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  free(this->header);
  free(this->frame_buffer);
  free(this);
}

static int demux_ra_get_status (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_ra_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_ra_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ra_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_ra_t     *this;

  this         = calloc(1, sizeof(demux_ra_t));
  this->stream = stream;
  this->input  = input;
  this->frame_buffer = NULL;

  this->demux_plugin.send_headers      = demux_ra_send_headers;
  this->demux_plugin.send_chunk        = demux_ra_send_chunk;
  this->demux_plugin.seek              = demux_ra_seek;
  this->demux_plugin.dispose           = demux_ra_dispose;
  this->demux_plugin.get_status        = demux_ra_get_status;
  this->demux_plugin.get_stream_length = demux_ra_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ra_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ra_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_ra_file(this)) {
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

void *demux_realaudio_init_plugin (xine_t *xine, void *data) {
  demux_ra_class_t     *this;

  this = calloc(1, sizeof(demux_ra_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("RealAudio file demux plugin");
  this->demux_class.identifier      = "RA";
  this->demux_class.mimetypes       = "audio/x-realaudio: ra: RealAudio File;";
  this->demux_class.extensions      = "ra";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
