/*
 * Copyright (C) 2014 the xine project
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
 *
 * libmmal decoder wrapped by Petri Hintukainen <phintuka@users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_default_components.h>

#define LOG_MODULE "mmal_video_decoder"

#define XINE_ENGINE_INTERNAL  /* access to stream->video_decoder_streamtype */

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>


typedef struct mmal_decoder_s {
  video_decoder_t   video_decoder;

  pthread_mutex_t   mutex;

  /* xine */
  xine_stream_t    *stream;

  MMAL_BUFFER_HEADER_T *input_buffer;

  /* mmal decoder */
  MMAL_COMPONENT_T *decoder;
  MMAL_POOL_T      *input_pool;
  MMAL_POOL_T      *output_pool;
  MMAL_QUEUE_T     *decoded_frames;
  MMAL_ES_FORMAT_T *output_format;

  /* decoder output format */
  double            ratio;
  int               width;
  int               height;
  int               frame_flags;
  int               crop_x, crop_y, crop_w, crop_h;

  uint8_t           decoder_ok;
  uint8_t           discontinuity;

} mmal_decoder_t;

/*
 * decoder output buffers
 */

static void free_output_buffer(MMAL_BUFFER_HEADER_T *buffer)
{
  vo_frame_t *frame = (vo_frame_t *)buffer->user_data;

  if (frame) {
    if (buffer->data != frame->base[0]) {
      /* free indirect rendering buffer */
      free(buffer->data);
    }
    frame->free(frame);
  }

  buffer->user_data  = NULL;
  buffer->alloc_size = 0;
  buffer->data       = NULL;

  mmal_buffer_header_release(buffer);
}

static int send_output_buffer(mmal_decoder_t *this)
{
  MMAL_BUFFER_HEADER_T *buffer;
  vo_frame_t           *frame;
  MMAL_STATUS_T         status;

  buffer = mmal_queue_get(this->output_pool->queue);
  if (!buffer) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to get new output buffer\n");
    return -1;
  }

  frame = this->stream->video_out->get_frame (this->stream->video_out,
                                              this->width, this->height,
                                              this->ratio, XINE_IMGFMT_YV12,
                                              this->frame_flags | VO_BOTH_FIELDS);

  if (!frame) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to get new xine frame\n");
    mmal_buffer_header_release(buffer);
    return -1;
  }

  mmal_buffer_header_reset(buffer);
  buffer->user_data  = frame;
  buffer->cmd        = 0;
  buffer->alloc_size = this->decoder->output[0]->buffer_size;
  buffer->data       = frame->base[0];

  /* check if we can render directly to frame */
  if (frame->pitches[0] != this->width || frame->pitches[1] != this->width/2 || frame->pitches[2] != this->width/2 ||
      frame->base[1] - frame->base[0] != this->width * this->height ||
      frame->base[2] - frame->base[1] != this->width * this->height / 4 ) {

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "xine frame not suitable for direct rendering\n");

    buffer->data = malloc(buffer->alloc_size);
  }

  status = mmal_port_send_buffer(this->decoder->output[0], buffer);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to send buffer to output port: %s (%d)\n",
            mmal_status_to_string(status), status);
    free_output_buffer(buffer);
    return -1;
  }

  return 0;
}

static void fill_output_port(mmal_decoder_t *this)
{
  if (this->output_pool) {

    unsigned buffers_available = mmal_queue_length(this->output_pool->queue);
    unsigned buffers_to_send   = this->decoder->output[0]->buffer_num_recommended -
                                 ( this->output_pool->headers_num - buffers_available -
                                   mmal_queue_length(this->decoded_frames));
    unsigned i;

    if (buffers_to_send > buffers_available) {
      buffers_to_send = buffers_available;
    }

    for (i = 0; i < buffers_to_send; ++i) {
      if (send_output_buffer(this) < 0) {
        break;
      }
    }
  }
}

/*
 * MMAL callbacks
 */

static void control_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  mmal_decoder_t *this = (mmal_decoder_t *)port->userdata;

  if (buffer->cmd == MMAL_EVENT_ERROR) {
    MMAL_STATUS_T status = *(uint32_t *)buffer->data;

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "MMAL error: %s (%d)\n",
            mmal_status_to_string(status), status);
  }

  mmal_buffer_header_release(buffer);
}

static void input_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  mmal_buffer_header_release(buffer);
}

static void output_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  mmal_decoder_t *this = (mmal_decoder_t *)port->userdata;

  if (buffer->cmd == 0) {
    if (buffer->length > 0) {
      mmal_queue_put(this->decoded_frames, buffer);
      pthread_mutex_lock(&this->mutex);
      fill_output_port(this);
      pthread_mutex_unlock(&this->mutex);
    } else {
      free_output_buffer(buffer);
    }

  } else if (buffer->cmd == MMAL_EVENT_FORMAT_CHANGED) {
    MMAL_EVENT_FORMAT_CHANGED_T *fmt = mmal_event_format_changed_get(buffer);
    MMAL_ES_FORMAT_T            *format = mmal_format_alloc();
    mmal_format_full_copy(format, fmt->format);

    pthread_mutex_lock(&this->mutex);
    if (this->output_format) {
      mmal_format_free(this->output_format);
      this->output_format = NULL;
    }
    this->output_format = format;
    pthread_mutex_unlock(&this->mutex);

    mmal_buffer_header_release(buffer);

  } else {
    mmal_buffer_header_release(buffer);
  }
}

/*
 * mmal codec
 */

static void stop_codec(mmal_decoder_t *this)
{
  if (this->decoder) {
    if (this->decoder->control->is_enabled) {
      mmal_port_disable(this->decoder->control);
    }

    if (this->decoder->input[0]->is_enabled) {
      mmal_port_disable(this->decoder->input[0]);
    }

    if (this->decoder->output[0]->is_enabled) {
      mmal_port_disable(this->decoder->output[0]);
    }

    if (this->decoder->is_enabled) {
      mmal_component_disable(this->decoder);
    }
  }
}

static int start_codec(mmal_decoder_t *this)
{
  MMAL_PORT_T   *input = this->decoder->input[0];
  MMAL_STATUS_T  status;

  if (!this->decoder->output[0]->is_enabled) {
    status = mmal_port_enable(this->decoder->output[0], output_port_cb);
    if (status != MMAL_SUCCESS) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to enable output port: %s (%d)\n",
              mmal_status_to_string(status), status);
      return -1;
    }
  }

  if (!this->decoder->is_enabled) {
    status = mmal_component_enable(this->decoder);
    if (status != MMAL_SUCCESS) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to enable decoder: %s (%d)\n",
              mmal_status_to_string(status), status);
      return -1;
    }
  }

  if (!this->input_pool) {
    this->input_pool = mmal_pool_create_with_allocator(input->buffer_num,
                                                       input->buffer_size, input,
                                                       (mmal_pool_allocator_alloc_t)mmal_port_payload_alloc,
                                                       (mmal_pool_allocator_free_t)mmal_port_payload_free);
  }

  if (!this->decoded_frames) {
    this->decoded_frames = mmal_queue_create();
  }

  return 0;
}

/*
 * decoder output
 */

static void send_frames(mmal_decoder_t *this)
{
  MMAL_BUFFER_HEADER_T *buffer;
  vo_frame_t           *frame;

  if (!this->decoded_frames) {
    return;
  }

  /* get ready frames */
  while (NULL != (buffer = mmal_queue_get(this->decoded_frames))) {
    frame = (vo_frame_t *)buffer->user_data;
    if (frame) {
      frame->pts         = buffer->pts;
      frame->crop_left   = this->crop_x;
      frame->crop_top    = this->crop_y;
      frame->crop_right  = this->width  - this->crop_x - this->crop_w;
      frame->crop_bottom = this->height - this->crop_y - this->crop_h;

      /* indirect rendering  ? */
      if (buffer->data != frame->base[0]) {
        int sz = this->width * this->height;
        yv12_to_yv12(
            /* Y */
            buffer->data, this->width,
            frame->base[0], frame->pitches[0],
            /* U */
            buffer->data + sz, this->width / 2,
            frame->base[1], frame->pitches[1],
            /* V */
            buffer->data + sz*5/4, this->width / 2,
            frame->base[2], frame->pitches[2],
            /* width x height */
            this->width, this->height);
      }

      frame->draw(frame, this->stream);
    }

    free_output_buffer(buffer);
  }

  if (pthread_mutex_trylock(&this->mutex) == 0) {
    fill_output_port(this);
    pthread_mutex_unlock(&this->mutex);
  }
}

static int change_output_format(mmal_decoder_t *this)
{
  MMAL_PORT_T   *output = this->decoder->output[0];
  MMAL_STATUS_T  status;
  double         rate;
  int            ret = 0;

  status = mmal_port_disable(output);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to disable output port: %s (%d)\n",
            mmal_status_to_string(status), status);
    ret = -1;
    goto out;
  }

  mmal_format_full_copy(output->format, this->output_format);

  status = mmal_port_format_commit(output);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to commit output format: %s (%d)",
            mmal_status_to_string(status), status);
    ret = -1;
    goto out;
  }

  output->buffer_num  = output->buffer_num_recommended;
  output->buffer_size = output->buffer_size_recommended;

  status = mmal_port_enable(output, output_port_cb);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to enable output port: %s (%d)",
            mmal_status_to_string(status), status);
    ret = -1;
    goto out;
  }

  if (!this->output_pool) {
    this->output_pool = mmal_pool_create(output->buffer_num_recommended + 10, 0);
  }

  this->width  = output->format->es->video.width;
  this->height = output->format->es->video.height;
  this->crop_x = output->format->es->video.crop.x;
  this->crop_y = output->format->es->video.crop.y;
  this->crop_w = output->format->es->video.crop.width;
  this->crop_h = output->format->es->video.crop.height;
  this->ratio  = output->format->es->video.par.num;
  this->ratio /= output->format->es->video.par.den;
  this->ratio *= this->width;
  this->ratio /= this->height;
  switch (output->format->es->video.color_space) {
    case MMAL_COLOR_SPACE_ITUR_BT601:
    case MMAL_COLOR_SPACE_BT470_2_BG:
    case MMAL_COLOR_SPACE_JFIF_Y16_255:
      VO_SET_FLAGS_CM (10, this->frame_flags);
      break;
    case MMAL_COLOR_SPACE_ITUR_BT709:
      VO_SET_FLAGS_CM (2, this->frame_flags);
      break;
    case MMAL_COLOR_SPACE_JPEG_JFIF:
      VO_SET_FLAGS_CM (11, this->frame_flags);
      break;
    case MMAL_COLOR_SPACE_FCC:
      VO_SET_FLAGS_CM (8, this->frame_flags);
      break;
    case MMAL_COLOR_SPACE_SMPTE240M:
      VO_SET_FLAGS_CM (14, this->frame_flags);
      break;
    case MMAL_COLOR_SPACE_UNKNOWN:
    default:
      //VO_SET_FLAGS_CM (4, this->frame_flags); /* undefined, mpeg range */
      // might have beed set by demux
      break;
  }
  rate  = output->format->es->video.frame_rate.num;
  rate /= output->format->es->video.frame_rate.den;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, 90000/rate);

 out:
  mmal_format_free(this->output_format);
  this->output_format = NULL;
  return ret;
}

static void handle_output(mmal_decoder_t *this)
{
  /* handle output re-config request */
  if (this->output_format) {
    pthread_mutex_lock(&this->mutex);
    change_output_format(this);
    pthread_mutex_unlock(&this->mutex);
  }

  /* handle decoder output */
  send_frames(this);
}

/*
 * decoder input
 */

static void set_extradata(mmal_decoder_t *this, void *extradata, size_t extradata_size)
{
  MMAL_PORT_T   *input = this->decoder->input[0];
  MMAL_STATUS_T  status;

  status = mmal_port_disable(input);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to disable input port: %s (%d)\n",
            mmal_status_to_string(status), status);
  }

  status = mmal_format_extradata_alloc(input->format, extradata_size);
  if (status == MMAL_SUCCESS) {
    memcpy(input->format->extradata, extradata, extradata_size);
    input->format->extradata_size = extradata_size;
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to allocate extradata: %s (%d)",
            mmal_status_to_string(status), status);
  }

  status = mmal_port_format_commit(input);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to commit input format: %s (%d)\n",
            mmal_status_to_string(status), status);
  }

  status = mmal_port_enable(input, input_port_cb);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to enable input port: %s (%d)\n",
            mmal_status_to_string(status), status);
  }
}

static void free_input_buffer(mmal_decoder_t *this)
{
  if (this->input_buffer) {
    mmal_buffer_header_release(this->input_buffer);
    this->input_buffer = NULL;
  }
}

static MMAL_BUFFER_HEADER_T *get_input_buffer(mmal_decoder_t *this)
{
  if (!this->input_buffer) {
    int retries = 40;

    this->input_buffer = mmal_queue_timedwait(this->input_pool->queue, 200);
    while (!this->input_buffer) {
      handle_output(this);
      if (--retries < 1 || this->stream->emergency_brake) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
                "failed to retrieve buffer header for input data\n");
        this->discontinuity = 1;
        return NULL;
      }
      this->input_buffer = mmal_queue_timedwait(this->input_pool->queue, 200);
    }

    mmal_buffer_header_reset(this->input_buffer);
    this->input_buffer->cmd    = 0;
    this->input_buffer->length = 0;
    this->input_buffer->flags  = 0;
  }

  return this->input_buffer;
}

static int send_input_buffer(mmal_decoder_t *this)
{
  MMAL_STATUS_T status;

  if (this->input_buffer) {
    if (this->discontinuity) {
      this->input_buffer->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
      this->discontinuity = 0;
    }

    status = mmal_port_send_buffer(this->decoder->input[0], this->input_buffer);
    if (status != MMAL_SUCCESS) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to send buffer to input port: %s (%d)",
              mmal_status_to_string(status), status);
      free_input_buffer(this);
      return -1;
    }
    this->input_buffer = NULL;
  }

  return 0;
}

/*
 * xine video decoder plugin functions
 */

static void handle_header(mmal_decoder_t *this, buf_element_t *buf)
{
  xine_bmiheader *bih;
  size_t          extradata_size = 0;
  uint8_t        *extradata = NULL;

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    bih = (xine_bmiheader *) buf->content;
    this->width  = (bih->biWidth + 1) & ~1;
    this->height = (bih->biHeight + 1) & ~1;

    if (buf->decoder_flags & BUF_FLAG_ASPECT)
      this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
    else
      this->ratio = (double)this->width / (double)this->height;

    if (bih->biSize > sizeof(xine_bmiheader)) {
      extradata_size = bih->biSize - sizeof(xine_bmiheader);
      extradata      = buf->content + sizeof(xine_bmiheader);
    }
  }

  if (buf->type == BUF_VIDEO_H264) {
    if (extradata && extradata_size > 0) {
      set_extradata(this, extradata, extradata_size);
    }
  }
}

static void mmal_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  mmal_decoder_t *this = (mmal_decoder_t *) this_gen;

  if (buf->decoder_flags & (BUF_FLAG_PREVIEW | BUF_FLAG_SPECIAL)) {
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_COLOR_MATRIX) {
    VO_SET_FLAGS_CM (buf->decoder_info[4], this->frame_flags);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    handle_header(this, buf);
    return;
  }

  if (!this->decoder_ok) {
    start_codec(this);

    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->decoder_ok = 1;
  }

  /* handle decoder output and config */
  handle_output(this);

  /* feed decoder */
  while (buf->size > 0) {

    MMAL_BUFFER_HEADER_T *buffer = get_input_buffer(this);
    if (!buffer)
      return;

    if (buf->pts > 0)
      buffer->pts = buf->pts;

    uint32_t len = buf->size;
    if (len > buffer->alloc_size - buffer->length)
      len = buffer->alloc_size - buffer->length;

    memcpy(buffer->data + buffer->length, buf->content, len);
    buf->content += len;
    buf->size    -= len;

    buffer->length += len;

    if (buf->size > 0 || (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
      send_input_buffer(this);
    }
  }
}

static void mmal_flush (video_decoder_t *this_gen)
{
  mmal_decoder_t *this = (mmal_decoder_t *) this_gen;

  send_frames(this);
}

static void mmal_reset (video_decoder_t *this_gen)
{
  mmal_decoder_t *this = (mmal_decoder_t *) this_gen;

  free_input_buffer(this);

  if (this->decoder && this->decoder->is_enabled) {

    stop_codec(this);

    /* free frames */
    MMAL_BUFFER_HEADER_T *buffer;
    while ((buffer = mmal_queue_get(this->decoded_frames))) {
      free_output_buffer(buffer);
    }

    mmal_port_enable(this->decoder->control, control_port_cb);
    mmal_port_enable(this->decoder->input[0], input_port_cb);

    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->decoder_ok = 0;
  }
}

static void mmal_discontinuity (video_decoder_t *this_gen)
{
  mmal_decoder_t *this = (mmal_decoder_t *) this_gen;
  send_input_buffer(this);
  this->discontinuity = 1;
}

static void mmal_dispose (video_decoder_t *this_gen)
{
  mmal_decoder_t *this = (mmal_decoder_t *) this_gen;

  free_input_buffer(this);

  stop_codec(this);

  if (this->input_pool) {
    mmal_pool_destroy(this->input_pool);
  }

  if (this->output_format) {
    mmal_format_free(this->output_format);
  }

  /* free frames */
  if (this->decoded_frames) {
    MMAL_BUFFER_HEADER_T *buffer;
    while ((buffer = mmal_queue_get(this->decoded_frames))) {
      free_output_buffer(buffer);
    }

    mmal_queue_destroy(this->decoded_frames);
  }

  if (this->output_pool) {
    mmal_pool_destroy(this->output_pool);
  }

  if (this->decoder) {
    mmal_component_release(this->decoder);
  }


  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  pthread_mutex_destroy (&this->mutex);

  free (this_gen);

  bcm_host_deinit();
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  mmal_decoder_t *this;
  MMAL_STATUS_T   status;

  bcm_host_init();

  this = (mmal_decoder_t *) calloc(1, sizeof(mmal_decoder_t));

  pthread_mutex_init (&this->mutex, NULL);

  this->video_decoder.decode_data   = mmal_decode_data;
  this->video_decoder.flush         = mmal_flush;
  this->video_decoder.reset         = mmal_reset;
  this->video_decoder.discontinuity = mmal_discontinuity;
  this->video_decoder.dispose       = mmal_dispose;

  this->stream                      = stream;

  VO_SET_FLAGS_CM (4, this->frame_flags); /* undefined, mpeg range */

  /* create decoder component */

  status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_DECODER, &this->decoder);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to create "MMAL_COMPONENT_DEFAULT_VIDEO_DECODER": %s (%d)\n",
            mmal_status_to_string(status), status);
    mmal_dispose(&this->video_decoder);
    return NULL;
  }

  this->decoder->control->userdata   = (void *)this;
  this->decoder->input[0]->userdata  = (void *)this;
  this->decoder->output[0]->userdata = (void *)this;

  status = mmal_port_enable(this->decoder->control, control_port_cb);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to enable control port: %s (%d)\n",
            mmal_status_to_string(status), status);
    mmal_dispose(&this->video_decoder);
    return NULL;
  }

  /* test if decoder supports requested codec */

  uint32_t video_type = BUF_VIDEO_BASE | (stream->video_decoder_streamtype << 16);
  MMAL_PORT_T *input = this->decoder->input[0];

  switch (video_type) {
    case BUF_VIDEO_H264:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "H.264");
      input->format->encoding = MMAL_ENCODING_H264;
      break;
    case BUF_VIDEO_VC1:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "VC-1");
      input->format->encoding = MMAL_FOURCC('V','C','-','1');
      break;
    case BUF_VIDEO_MPEG:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "MPEG");
      input->format->encoding = MMAL_ENCODING_MP2V;
      break;
    case BUF_VIDEO_JPEG:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "JPEG");
      input->format->encoding = MMAL_ENCODING_JPEG;
      break;
    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "unsupported video codec: 0x%x\n",
              stream->video_decoder_streamtype);
      mmal_dispose(&this->video_decoder);
      return (video_decoder_t *)1;
  }

  if (video_type == BUF_VIDEO_H264) {
    MMAL_PARAMETER_BOOLEAN_T param;
    param.hdr.id = MMAL_PARAMETER_VIDEO_DECODE_ERROR_CONCEALMENT;
    param.hdr.size = sizeof(MMAL_PARAMETER_BOOLEAN_T);
    param.enable = MMAL_FALSE;
    status = mmal_port_parameter_set(input, &param.hdr);
    if (status != MMAL_SUCCESS)
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to disable error concealment: %s (%d)",
              mmal_status_to_string(status), status);
  }

  status = mmal_port_format_commit(input);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to commit input format: %s (%d)\n",
            mmal_status_to_string(status), status);
    mmal_dispose(&this->video_decoder);
    return NULL;
  }

  input->buffer_size = input->buffer_size_recommended;
  input->buffer_num  = input->buffer_num_recommended * 4;

  status = mmal_port_enable(input, input_port_cb);
  if (status != MMAL_SUCCESS) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to enable input port: %s (%d)\n",
            mmal_status_to_string(status), status);
    mmal_dispose(&this->video_decoder);
    return NULL;
  }

  return &this->video_decoder;
}

static void *init_plugin (xine_t *xine, void *data)
{
  video_decoder_class_t *this;

  this = (video_decoder_class_t *) calloc(1, sizeof(video_decoder_class_t));

  this->open_plugin     = open_plugin;
  this->identifier      = "libmmal";
  this->description     = N_("mmal-based HW video decoder plugin");
  this->dispose         = default_video_decoder_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types[] = {
  BUF_VIDEO_H264,
  BUF_VIDEO_VC1,
  BUF_VIDEO_MPEG,
  BUF_VIDEO_JPEG,
  0
};

static const decoder_info_t dec_info = {
  video_types,     /* supported types */
  10               /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "libmmal", XINE_VERSION_CODE, &dec_info, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
