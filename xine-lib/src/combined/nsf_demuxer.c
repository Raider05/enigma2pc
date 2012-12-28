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
 * NSF File "Demuxer" by Mike Melanson (melanson@pcisys.net)
 * This is really just a loader for NES Music File Format (extension NSF)
 * which loads an entire NSF file and passes it over to the NSF audio
 * decoder.
 *
 * After the file is sent over, the demuxer controls the playback by
 * sending empty buffers with incrementing pts values.
 *
 * For more information regarding the NSF format, visit:
 *   http://www.tripoint.org/kevtris/nes/nsfspec.txt
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
#define LOG_MODULE "demux_nsf"
/* #define LOG_VERBOSE */
/* #define LOG */

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"

#include "nsf_combined.h"

#define NSF_HEADER_SIZE 0x80
#define NSF_SAMPLERATE 44100
#define NSF_BITS 8
#define NSF_CHANNELS 1
#define NSF_REFRESH_RATE 60
#define NSF_PTS_INC (90000 / NSF_REFRESH_RATE)

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  total_songs;
  int                  current_song;
  int                  new_song;  /* indicates song change */

  char                *title;
  char                *artist;
  char                *copyright;

  off_t                filesize;

  int64_t              current_pts;
  int                  file_sent;
} demux_nsf_t;

typedef struct {
  demux_class_t     demux_class;
} demux_nsf_class_t;

/* returns 1 if the NSF file was opened successfully, 0 otherwise */
static int open_nsf_file(demux_nsf_t *this) {
  unsigned char header[NSF_HEADER_SIZE];

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, header, NSF_HEADER_SIZE) !=
    NSF_HEADER_SIZE)
    return 0;

  /* check for the signature */
  if (memcmp(header, "NESM\x1A", 5) != 0)
    return 0;

  this->total_songs = header[6];
  this->current_song = header[7];
  this->title = strndup((char*)&header[0x0E], 0x20);
  this->artist = strndup((char*)&header[0x2E], 0x20);
  this->copyright = strndup((char*)&header[0x4E], 0x20);

  this->filesize = this->input->get_length(this->input);

  return 1;
}

static int demux_nsf_send_chunk(demux_plugin_t *this_gen) {
  demux_nsf_t *this = (demux_nsf_t *) this_gen;
  buf_element_t *buf;
  int bytes_read;
  char title[100];

  /* send chunks of the file to the decoder until file is completely
   * loaded; then send control buffers */
  if (!this->file_sent) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_NSF;
    bytes_read = this->input->read(this->input, buf->content, buf->max_size);

    if (bytes_read <= 0) {
      /* the file has been completely loaded, free the buffer and start
       * sending control buffers */
      buf->free_buffer(buf);
      this->file_sent = 1;

    } else {

      /* keep loading the file */
      if (bytes_read < buf->max_size)
        buf->size = bytes_read;
      else
        buf->size = buf->max_size;

      buf->extra_info->input_normpos = 0;
      buf->extra_info->input_time = 0;
      buf->pts = 0;

      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  /* this is not an 'else' because control might fall through from above */
  if (this->file_sent) {
    /* send a control buffer */
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    if (this->new_song) {

      buf->decoder_info[1] = this->current_song;
      this->new_song = 0;
      sprintf(title, "%s, song %d/%d",
        this->title, this->current_song, this->total_songs);

      _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, title);

      _x_demux_control_newpts(this->stream, this->current_pts, 0);

    } else
      buf->decoder_info[1] = 0;

    buf->type = BUF_AUDIO_NSF;
    if(this->total_songs)
      buf->extra_info->input_normpos = (this->current_song - 1) * 65535 / this->total_songs;
    buf->extra_info->input_time = this->current_pts / 90;
    buf->pts = this->current_pts;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);

    this->current_pts += NSF_PTS_INC;
  }

  return this->status;
}

static void demux_nsf_send_headers(demux_plugin_t *this_gen) {
  demux_nsf_t *this = (demux_nsf_t *) this_gen;
  buf_element_t *buf;
  char copyright[100];

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
    NSF_CHANNELS);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
    NSF_SAMPLERATE);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
    NSF_BITS);

  _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->title);
  _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->artist);
  sprintf(copyright, "(C) %s", this->copyright);
  _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, copyright);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to the audio decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_NSF;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 5;
    buf->decoder_info[1] = NSF_SAMPLERATE;
    buf->decoder_info[2] = NSF_BITS;
    buf->decoder_info[3] = NSF_CHANNELS;

    /* send the NSF filesize in the body, big endian format */
    buf->content[0] = (this->filesize >> 24) & 0xFF;
    buf->content[1] = (this->filesize >> 16) & 0xFF;
    buf->content[2] = (this->filesize >>  8) & 0xFF;
    buf->content[3] = (this->filesize >>  0) & 0xFF;
    /* send the requested song */
    buf->content[4] = this->current_song + 5;

    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_nsf_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_nsf_t *this = (demux_nsf_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->total_songs );

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* reposition stream at the start for loading */
    this->input->seek(this->input, 0, SEEK_SET);

    this->file_sent = 0;
    this->current_pts = 0;
    this->new_song = 1;
  } else {
    this->current_song = start_pos + 1;
    this->new_song = 1;
    this->current_pts = 0;
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}

static void demux_nsf_dispose (demux_plugin_t *this_gen) {
  demux_nsf_t *this = (demux_nsf_t *) this_gen;

  free(this->title);
  free(this->artist);
  free(this->copyright);
  free(this);
}

static int demux_nsf_get_status (demux_plugin_t *this_gen) {
  demux_nsf_t *this = (demux_nsf_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_nsf_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_nsf_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_nsf_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_nsf_t   *this;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_nsf_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_nsf_send_headers;
  this->demux_plugin.send_chunk        = demux_nsf_send_chunk;
  this->demux_plugin.seek              = demux_nsf_seek;
  this->demux_plugin.dispose           = demux_nsf_dispose;
  this->demux_plugin.get_status        = demux_nsf_get_status;
  this->demux_plugin.get_stream_length = demux_nsf_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_nsf_get_capabilities;
  this->demux_plugin.get_optional_data = demux_nsf_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_nsf_file(this)) {
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

void *demux_nsf_init_plugin (xine_t *xine, void *data) {
  demux_nsf_class_t     *this;

  this = calloc(1, sizeof(demux_nsf_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("NES Music file demux plugin");
  this->demux_class.identifier      = "NSF";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "nsf";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
