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
 * demux_mng.c, Demuxer plugin for Multiple-image Network Graphics format
 *
 * written and currently maintained by Robin Kay <komadori@myrealbox.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#undef HAVE_CONFIG_H
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_STDLIB_H
#undef HAVE_STDLIB_H
#endif

#define LOG_MODULE "demux_mng"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#include <libmng.h>

typedef struct {
  demux_plugin_t     demux_plugin;

  xine_stream_t     *stream;
  fifo_buffer_t     *video_fifo;
  input_plugin_t    *input;
  int                status;

  mng_handle         mngh;
  xine_bmiheader     bih;
  int		     left_edge;
  uint8_t           *image;

  int                started;
  int                tick_count;
  int                timer_count;
} demux_mng_t;

typedef struct {
  demux_class_t     demux_class;
} demux_mng_class_t;

static mng_ptr mymng_alloc(mng_size_t size){
  return (mng_ptr)calloc(1, size);
}

static void mymng_free(mng_ptr p, mng_size_t size){
  free(p);
}

static mng_bool mymng_open_stream(mng_handle mngh){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  if (this->input->get_current_pos(this->input) != 0) {
    if (!INPUT_IS_SEEKABLE(this->input)) {
      return MNG_FALSE;
    }
    this->input->seek(this->input, 0, SEEK_SET);
  }

  return MNG_TRUE;
}

static mng_bool mymng_close_stream(mng_handle mngh){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  this->status = DEMUX_FINISHED;

  return MNG_TRUE;
}

static mng_bool mymng_read_stream(mng_handle mngh, mng_ptr buffer, mng_uint32 size, mng_uint32 *bytesread){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  off_t n = this->input->read(this->input, buffer, size);
  if (n < 0) {
	*bytesread = 0;
	return MNG_FALSE;
  }
  *bytesread = n;

  return MNG_TRUE;
}

static mng_bool mymng_process_header(mng_handle mngh, mng_uint32 width, mng_uint32 height){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  if (width > 0x8000 || height > 0x8000)
      return MNG_FALSE;

  this->bih.biWidth = (width + 7) & ~7;
  this->bih.biHeight = height;
  this->left_edge = (this->bih.biWidth - width) / 2;

  this->image = malloc((mng_size_t)this->bih.biWidth * (mng_size_t)height * 3);
  if (!this->image)
    return MNG_FALSE;

  mng_set_canvasstyle(mngh, MNG_CANVAS_RGB8);

  return MNG_TRUE;
}

static mng_uint32 mymng_get_tick_count(mng_handle mngh){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  return this->tick_count;
}

static mng_bool mymng_set_timer(mng_handle mngh, mng_uint32 msecs){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  this->timer_count = msecs;

  return MNG_TRUE;
}

static mng_ptr mymng_get_canvas_line(mng_handle mngh, mng_uint32 line){
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  return this->image + (this->left_edge + line * this->bih.biWidth) * 3;
}

static mng_bool mymng_refresh(mng_handle mngh, mng_uint32 x, mng_uint32 y, mng_uint32 w, mng_uint32 h){
  return MNG_TRUE;
}

/*
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 * All the following functions are defined by the xine demuxer API
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 */

static int demux_mng_send_chunk(demux_mng_t *this){
  int size = this->bih.biWidth * this->bih.biHeight * 3;
  uint8_t *image_ptr = this->image;

  int err = mng_display_resume(this->mngh);
  if ((err != MNG_NOERROR) && (err != MNG_NEEDTIMERWAIT)) {
    lprintf("mng_display_resume returned an error (%d)\n", err);
    this->status = DEMUX_FINISHED;
  }

  while (size > 0) {
    buf_element_t *buf;

    buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    buf->type = BUF_VIDEO_RGB;
    buf->decoder_flags = BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = 90 * this->timer_count;
    if( this->input->get_length (this->input) )
      buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );
    buf->extra_info->input_time = this->tick_count;
    buf->pts = 90 * this->tick_count;

    if (size > buf->max_size) {
      buf->size = buf->max_size;
    }
    else {
      buf->size = size;
    }
    size -= buf->size;

    memcpy(buf->content, image_ptr, buf->size);
    image_ptr += buf->size;

    if (size == 0) {
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
    }

    this->video_fifo->put(this->video_fifo, buf);
  }

  this->tick_count += this->timer_count;
  this->timer_count = 0;

  return this->status;
}

static void demux_mng_send_headers(demux_mng_t *this){
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* required when loop playing short streams (gapless switch) */
  _x_demux_control_newpts(this->stream, 0, 0);

  /* send init info to decoder */
  this->bih.biBitCount = 24;
  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->type = BUF_VIDEO_RGB;
  buf->size = sizeof(xine_bmiheader);
  memcpy(buf->content, &this->bih, sizeof(xine_bmiheader));
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
  this->video_fifo->put(this->video_fifo, buf);
}

static int demux_mng_seek(demux_mng_t *this, off_t start_pos, int start_time, int playing){
  return this->status;
}

static void demux_mng_dispose(demux_mng_t *this){

  mng_cleanup(&this->mngh);

  if (this->image)
    free(this->image);

  free(this);
}

static int demux_mng_get_status(demux_mng_t *this){
  return this->status;
}

static int demux_mng_get_stream_length(demux_mng_t *this){
  return 0;
}

static uint32_t demux_mng_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mng_get_optional_data(demux_plugin_t *this_gen, void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t* open_plugin(demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input){

  demux_mng_t    *this;

  this         = calloc(1, sizeof(demux_mng_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = (void*)demux_mng_send_headers;
  this->demux_plugin.send_chunk        = (void*)demux_mng_send_chunk;
  this->demux_plugin.seek              = (void*)demux_mng_seek;
  this->demux_plugin.dispose           = (void*)demux_mng_dispose;
  this->demux_plugin.get_status        = (void*)demux_mng_get_status;
  this->demux_plugin.get_stream_length = (void*)demux_mng_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mng_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mng_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      if (!INPUT_IS_SEEKABLE(this->input)) {
        free(this);
        return NULL;
      }
    break;

    case METHOD_BY_MRL:
    break;

    default:
      free(this);
      return NULL;
    break;
  }

  if ((this->mngh = mng_initialize(this, mymng_alloc, mymng_free, MNG_NULL)) == MNG_NULL) {
    free(this);
    return NULL;
  }

  if (mng_setcb_openstream(this->mngh, mymng_open_stream) ||
      mng_setcb_closestream(this->mngh, mymng_close_stream) ||
      mng_setcb_readdata(this->mngh, mymng_read_stream) ||
      mng_setcb_processheader(this->mngh, mymng_process_header) ||
      mng_setcb_gettickcount(this->mngh, mymng_get_tick_count) ||
      mng_setcb_settimer(this->mngh, mymng_set_timer) ||
      mng_setcb_getcanvasline(this->mngh, mymng_get_canvas_line) ||
      mng_setcb_refresh(this->mngh, mymng_refresh)) {
    mng_cleanup(&this->mngh);
    free(this);
    return NULL;
  }

  {
    int err = mng_readdisplay(this->mngh);
    if ((err != MNG_NOERROR) && (err != MNG_NEEDTIMERWAIT)) {
      mng_cleanup(&this->mngh);
      free(this);
      return NULL;
    }
  }

  return &this->demux_plugin;
}

static void *init_plugin(xine_t *xine, void *data){
  demux_mng_class_t     *this;

  this  = calloc(1, sizeof(demux_mng_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Multiple-image Network Graphics demux plugin");
  this->demux_class.identifier      = "MNG";
  this->demux_class.mimetypes       =
    "image/png: png: PNG image;"
    "image/x-png: png: PNG image;"
    "video/mng: mng: MNG animation;"
    "video/x-mng: mng: MNG animation;";
  this->demux_class.extensions      = "png mng";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

static const demuxer_info_t demux_info_mng = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  { PLUGIN_DEMUX, 27, "mng", XINE_VERSION_CODE, &demux_info_mng, (void*)init_plugin},
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
