/*
 * Copyright (C) 2000-2003 the xine project
 * May 2003 - Miguel Freitas
 * This plugin was sponsored by 1Control
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
 * demuxer for slave "protocol"
 * master xine must be started with XINE_PARAM_BROADCASTER_PORT set, that is,
 * 'xine --broadcast-port <port_number>'
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define LOG_MODULE "demux_slave"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>

#define SCRATCH_SIZE        1024
#define CHECK_VPTS_INTERVAL 2*90000
#define NETWORK_PREBUFFER   90000

typedef struct {
  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int64_t              last_vpts;
  int                  send_newpts;

                        /* additional decoder flags and other dec-spec. stuff */
  uint32_t             decoder_info[BUF_NUM_DEC_INFO];
                        /* pointers to dec-spec. stuff */
  void                 *decoder_info_ptr[BUF_NUM_DEC_INFO];
  xine_list_t          *dec_infos;   /* dec-spec. stuff */

  uint8_t              scratch[SCRATCH_SIZE+1];
  int                  scratch_used;
} demux_slave_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_slave_class_t;


#define MAX_COMMAND_SIZE 20

static int demux_slave_next (demux_slave_t *this) {
  buf_element_t *buf;
  int n, i;
  char fifo_name[11];
  uint8_t *p, *s;
  int64_t curvpts;

  /* fill the scratch buffer */
  n = this->input->read(this->input, &this->scratch[this->scratch_used],
                        SCRATCH_SIZE - this->scratch_used);
  if (n > 0)
    this->scratch_used += n;
  this->scratch[this->scratch_used] = '\0';

  if (n <= 0) {
    lprintf("connection closed\n");
    this->status = DEMUX_FINISHED;
    return 0;
  }

  p = strchr(this->scratch,'\n');
  s = strchr(this->scratch,' ');

  if( !s || s > p )
    s = p;

  if( !p || !s || (s-this->scratch+1) > MAX_COMMAND_SIZE ) {
    lprintf("protocol error\n");
    this->status = DEMUX_FINISHED;
    return 0;
  }

  *s++ = '\0';
  p++;

  if( !strcmp(this->scratch,"buffer") ) {
    int32_t    size ;     /* size of _content_                                     */
    uint32_t   type;
    int64_t    pts;       /* presentation time stamp, used for a/v sync            */
    int64_t    disc_off;  /* discontinuity offset                                  */
    uint32_t   decoder_flags; /* stuff like keyframe, is_header ... see below      */

    if( sscanf(s,"fifo=%10s size=%"  SCNd32 " type=%" SCNu32 " pts=%" SCNd64 " disc=%" SCNd64 " flags=%" SCNu32,
               fifo_name, &size, &type, &pts, &disc_off, &decoder_flags) != 6 ) {
      lprintf("'buffer' command error\n");
      this->status = DEMUX_FINISHED;
      return 0;
    }

    if( type == BUF_CONTROL_NEWPTS ) {
      this->send_newpts = 0;
      this->last_vpts = 0;
    }

    /* if we join an already existing broadcaster we must take care
     * of the initial pts.
     */
    if( pts && this->send_newpts ) {
      _x_demux_control_newpts( this->stream, pts, 0 );
      this->send_newpts = 0;
    }

    /* check if we are not late on playback.
     * that might happen if user hits "pause" on the master, for example.
     */
    if( pts &&
        (curvpts = this->stream->xine->clock->get_current_time(this->stream->xine->clock)) >
        (this->last_vpts + CHECK_VPTS_INTERVAL) ) {
      if( this->last_vpts &&
          pts - (NETWORK_PREBUFFER/2) +
          this->stream->metronom->get_option(this->stream->metronom, METRONOM_VPTS_OFFSET) <
          curvpts ) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "we are running late, forcing newpts.\n");
        _x_demux_control_newpts( this->stream, pts - NETWORK_PREBUFFER, 0 );
      }
      this->last_vpts = curvpts;
    }


    if( !strcmp(fifo_name,"video") || !this->audio_fifo )
      buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    else
      buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);

    /* copy data to buf, either from stratch or network */
    n = this->scratch_used - (p-this->scratch);
    if( n > size )
      n = size;
    if( n )
      memcpy(buf->content, p, n);
    if( n < size )
      this->input->read(this->input, &buf->content[n], size-n);

    p += n;
    n = this->scratch_used - (p-this->scratch);
    if( n )
      memmove(this->scratch, p, n);
    this->scratch_used = n;

    /* populate our buf */
    buf->size = size;
    buf->type = type;
    buf->pts = pts;
    buf->disc_off = disc_off;
    buf->decoder_flags = decoder_flags;

    /* set decoder info */
    memcpy(buf->decoder_info, this->decoder_info, sizeof(this->decoder_info));
    memcpy(buf->decoder_info_ptr, this->decoder_info_ptr, sizeof(this->decoder_info));
    memset(this->decoder_info, 0, sizeof(this->decoder_info));
    memset(this->decoder_info_ptr, 0, sizeof(this->decoder_info_ptr));

    if( !strcmp(fifo_name,"video") )
      this->video_fifo->put(this->video_fifo, buf);
    else if (this->audio_fifo)
      this->audio_fifo->put(this->audio_fifo, buf);
    else
      buf->free_buffer(buf);

  } else if( !strcmp(this->scratch,"decoder_info") ) {

    uint32_t decoder_info;
    int has_data;
    int size;

    if( sscanf(s,"index=%d decoder_info=%u has_data=%d",
               &i, &decoder_info, &has_data) != 3 ||
               i < 0 || i >= BUF_NUM_DEC_INFO) {
      lprintf("'decoder_info' command error\n");
      this->status = DEMUX_FINISHED;
      return 0;
    }

    this->decoder_info[i] = decoder_info;

    size = (has_data) ? decoder_info : 0;

    if( size ) {
      this->decoder_info_ptr[i] = malloc(size);
      xine_list_push_back(this->dec_infos, this->decoder_info_ptr[i]);
    }

    n = this->scratch_used - (p-this->scratch);
    if( n > size )
      n = size;
    if( n )
      memcpy(this->decoder_info_ptr[i], p, n);
    if( n < size )
      this->input->read(this->input, (char *)this->decoder_info_ptr[i]+n, size-n);

    p += n;
    n = this->scratch_used - (p-this->scratch);
    if( n )
      memmove(this->scratch, p, n);
    this->scratch_used = n;


  } else if( !strcmp(this->scratch,"flush_engine") ) {

    _x_demux_flush_engine( this->stream );
    n = this->scratch_used - (p-this->scratch);
    if( n )
      memmove(this->scratch, p, n);
    this->scratch_used = n;

  } else {
    lprintf("unknown command '%s'\n", this->scratch);
    n = this->scratch_used - (p-this->scratch);
    if( n )
      memmove(this->scratch, p, n);
    this->scratch_used = n;
  }

  return 1;
}

static int demux_slave_send_chunk (demux_plugin_t *this_gen) {
  demux_slave_t *this = (demux_slave_t *) this_gen;

  demux_slave_next(this);

  return this->status;
}

static int demux_slave_get_status (demux_plugin_t *this_gen) {
  demux_slave_t *this = (demux_slave_t *) this_gen;

  return this->status;
}


static void demux_slave_send_headers (demux_plugin_t *this_gen) {
  demux_slave_t *this = (demux_slave_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  _x_demux_control_start(this->stream);

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  this->last_vpts = 0;
  this->send_newpts = 1;
}

static int demux_slave_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_slave_t *this = (demux_slave_t *) this_gen;

  return this->status;
}

static void demux_slave_dispose (demux_plugin_t *this_gen) {
  demux_slave_t *this = (demux_slave_t *) this_gen;
  void *data;
  xine_list_iterator_t ite;

  /* free all decoder information */
  while ((ite = xine_list_front(this->dec_infos)) != NULL) {
    data = xine_list_get_value(this->dec_infos, ite);
    free(data);
    xine_list_remove (this->dec_infos, ite);
  }
  xine_list_delete(this->dec_infos);

  free (this);
}

static int demux_slave_get_stream_length(demux_plugin_t *this_gen) {
  return 0 ; /*FIXME: implement */
}

static uint32_t demux_slave_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_slave_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}


static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_slave_t *this;
  static char slave_id_str[] = "master xine v1\n";

  this         = calloc(1, sizeof(demux_slave_t));

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {

    if (_x_demux_read_header(input, this->scratch, SCRATCH_SIZE) > 0) {
      if (!strncmp(this->scratch,slave_id_str,strlen(slave_id_str)))
        break;
    }

    free (this);
    return NULL;
  }

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    free (this);
    return NULL;
  }

  this->stream = stream;
  this->input  = input;
  this->dec_infos = xine_list_new();

  this->demux_plugin.send_headers      = demux_slave_send_headers;
  this->demux_plugin.send_chunk        = demux_slave_send_chunk;
  this->demux_plugin.seek              = demux_slave_seek;
  this->demux_plugin.dispose           = demux_slave_dispose;
  this->demux_plugin.get_status        = demux_slave_get_status;
  this->demux_plugin.get_stream_length = demux_slave_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_slave_get_capabilities;
  this->demux_plugin.get_optional_data = demux_slave_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  this->input->read(this->input, this->scratch,strlen(slave_id_str));
  this->scratch_used = 0;

  memset(this->decoder_info, 0, sizeof(this->decoder_info));
  memset(this->decoder_info_ptr, 0, sizeof(this->decoder_info_ptr));

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_slave_class_t     *this;

  this = calloc(1, sizeof(demux_slave_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = "";
  this->demux_class.identifier      = "slave";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "slave://";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_slave = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "slave", XINE_VERSION_CODE, &demux_info_slave, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
