/*
 * Copyright (C) 2000-2014 the xine project
 * Copyright (C) 2014 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_default_components.h>

#define LOG_MODULE "video_out_mmal"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/xineutils.h>

#define MAX_VIDEO_WIDTH  1920
#define MAX_VIDEO_HEIGHT 1088
#define MAX_VIDEO_FRAMES 20


typedef struct {
    vo_frame_t            vo_frame;

    MMAL_BUFFER_HEADER_T *buffer;
    int                   width, height, format;
    double                ratio;

    int                   displayed;
} mmal_frame_t;

typedef struct {

  vo_driver_t        vo_driver;

  /* xine */
  xine_t            *xine;
  alphablend_t       alphablend_extra_data;
  uint32_t           capabilities;
  int                gui_width, gui_height;

  /* mmal */
  MMAL_COMPONENT_T  *renderer;
  MMAL_POOL_T       *pool;
  int                frames_in_renderer;
  double             renderer_ratio;

  pthread_mutex_t    mutex;
  pthread_cond_t     cond;
} mmal_driver_t;

typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
} mmal_class_t;

#define LOG_STATUS(msg) \
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": " msg ": %s (%d)\n", \
          mmal_status_to_string(status), status)

/*
 * display config
 */

static int update_tv_resolution(mmal_driver_t *this) {

  TV_DISPLAY_STATE_T display_state;

  if (vc_tv_get_display_state(&display_state) != 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "failed to query display resolution\n");
    return -1;
  }

  if (display_state.state & 0xFF) {
    this->gui_width  = display_state.display.hdmi.width;
    this->gui_height = display_state.display.hdmi.height;
  } else if (display_state.state & 0xFF00) {
    this->gui_width  = display_state.display.sdtv.width;
    this->gui_height = display_state.display.sdtv.height;
  } else {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "invalid display state %x", (unsigned)display_state.state);
    return -1;
  }

  xprintf(this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE": "
          "display size %dx%d\n", this->gui_width, this->gui_height);
  return 0;
}

static int config_display(mmal_driver_t *this,
                          int src_x, int src_y, int src_w, int src_h) {

  MMAL_DISPLAYREGION_T display_region;
  MMAL_STATUS_T status;

  display_region.hdr.id   = MMAL_PARAMETER_DISPLAYREGION;
  display_region.hdr.size = sizeof(MMAL_DISPLAYREGION_T);
  display_region.fullscreen       = MMAL_FALSE;
  display_region.src_rect.x       = src_x;
  display_region.src_rect.y       = src_y;
  display_region.src_rect.width   = src_w;
  display_region.src_rect.height  = src_h;
  display_region.dest_rect.x      = 0;
  display_region.dest_rect.y      = 0;
  display_region.dest_rect.width  = this->gui_width;
  display_region.dest_rect.height = this->gui_height;
  display_region.layer            = 1;
  display_region.set              = MMAL_DISPLAY_SET_FULLSCREEN |
                                    MMAL_DISPLAY_SET_SRC_RECT |
                                    MMAL_DISPLAY_SET_DEST_RECT |
                                    MMAL_DISPLAY_SET_LAYER;

  status = mmal_port_parameter_set(this->renderer->input[0], &display_region.hdr);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to set display region");
    return -1;
  }
  return 0;
}

/*
 * MMAL callbacks
 */

static void control_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {

  mmal_driver_t *this  = (mmal_driver_t *)port->userdata;
  MMAL_STATUS_T  status;

  if (buffer->cmd == MMAL_EVENT_ERROR) {
    status = *(uint32_t *)buffer->data;
    LOG_STATUS("MMAL error");
  }

  mmal_buffer_header_release(buffer);
}

static void input_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {

  mmal_driver_t *this  = (mmal_driver_t *)port->userdata;
  vo_frame_t    *frame = (vo_frame_t *)buffer->user_data;

  pthread_mutex_lock(&this->mutex);
  --this->frames_in_renderer;
  pthread_cond_signal(&this->cond);
  pthread_mutex_unlock(&this->mutex);

  if (frame) {
    frame->free(frame);
  }
}

/*
 * renderer configuration
 */

static void disable_renderer(mmal_driver_t *this) {

  if (this->renderer) {

    if (this->renderer->control->is_enabled) {
      mmal_port_disable(this->renderer->control);
    }

    if (this->renderer->input[0]->is_enabled) {
      mmal_port_disable(this->renderer->input[0]);
    }

    if (this->renderer->is_enabled) {
      mmal_component_disable(this->renderer);
    }
  }
}

static int configure_renderer(mmal_driver_t *this, int format, int width, int height,
                              int crop_x, int crop_y, int crop_w, int crop_h, double ratio) {

  MMAL_PORT_T   *input = this->renderer->input[0];
  MMAL_STATUS_T  status;

  disable_renderer(this);

  this->renderer_ratio = ratio;

  input->userdata = (struct MMAL_PORT_USERDATA_T *)this;
  input->format->encoding = (format == XINE_IMGFMT_YV12 ? MMAL_ENCODING_I420 : MMAL_ENCODING_YUYV);
  input->format->es->video.width       = width;
  input->format->es->video.height      = height;
  input->format->es->video.crop.x      = crop_x;
  input->format->es->video.crop.y      = crop_y;
  input->format->es->video.crop.width  = crop_w;
  input->format->es->video.crop.height = crop_h;
  input->format->es->video.par.num     = height * ratio;
  input->format->es->video.par.den     = width;

  status = mmal_port_format_commit(input);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to commit input format");
  }

  input->buffer_size = input->buffer_size_recommended;

  status = mmal_port_enable(this->renderer->control, control_port_cb);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to enable control port");
    return -1;
  }

  status = mmal_port_enable(input, input_port_cb);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to enable input port");
    return -1;
  }

  status = mmal_component_enable(this->renderer);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to enable renderer component");
    return -1;
  }

  if (!this->pool) {
    int buffer_size = MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT * 2;
    this->pool = mmal_pool_create_with_allocator(MAX_VIDEO_FRAMES, buffer_size,
                                  input,
                                  (mmal_pool_allocator_alloc_t)mmal_port_payload_alloc,
                                  (mmal_pool_allocator_free_t)mmal_port_payload_free);
    if (!this->pool) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to create MMAL pool for %u buffers of size %d\n",
              MAX_VIDEO_FRAMES, buffer_size);
      return -1;
    }
  }

  return 0;
}

/*
 * xine interface
 */

static uint32_t mmal_get_capabilities (vo_driver_t *this_gen) {

  mmal_driver_t *this = (mmal_driver_t *) this_gen;

  return this->capabilities;
}

static void mmal_frame_field (vo_frame_t *vo_img, int which_field) {
}

static void mmal_frame_dispose (vo_frame_t *vo_img) {

  mmal_frame_t  *frame = (mmal_frame_t *) vo_img ;

  if (frame->buffer) {
    frame->buffer->user_data = NULL;
    mmal_buffer_header_release(frame->buffer);
    frame->buffer = NULL;
  }

  free(frame);
}

static vo_frame_t *mmal_alloc_frame (vo_driver_t *this_gen) {

  mmal_frame_t     *frame;

  frame = (mmal_frame_t *) calloc(1, sizeof(mmal_frame_t));

  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = mmal_frame_field;
  frame->vo_frame.dispose    = mmal_frame_dispose;

  return (vo_frame_t *) frame;
}

static void mmal_update_frame_format (vo_driver_t *this_gen,
                                      vo_frame_t *frame_gen,
                                      uint32_t width, uint32_t height,
                                      double ratio, int format, int flags) {

  mmal_driver_t *this = (mmal_driver_t *)this_gen;
  mmal_frame_t  *frame = (mmal_frame_t *)frame_gen;

  /* limit frame size */
  if (width > MAX_VIDEO_WIDTH) {
    width = MAX_VIDEO_WIDTH;
    frame->vo_frame.width = width;
  }
  if (height > MAX_VIDEO_HEIGHT) {
    height = MAX_VIDEO_HEIGHT;
    frame->vo_frame.height = height;
  }

  /* alignment */
  width  = (width + 31) & ~31;
  height = (height + 1) & ~1;

  if (!frame->buffer) {
    frame->buffer = mmal_queue_wait(this->pool->queue);
    if (!frame->buffer) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
              "failed to get mmal buffer for frame\n");
      frame->vo_frame.width = frame->vo_frame.height = 0;
      return;
    }
    frame->buffer->user_data = frame;
  }

  frame->width  = width;
  frame->height = height;
  frame->format = format;
  frame->ratio  = ratio;

  if (format == XINE_IMGFMT_YV12) {
    frame->vo_frame.pitches[0] = width;
    frame->vo_frame.pitches[1] = width/2;
    frame->vo_frame.pitches[2] = width/2;
    frame->vo_frame.base[0]    = frame->buffer->data;
    frame->vo_frame.base[1]    = frame->vo_frame.base[0] + width * height;
    frame->vo_frame.base[2]    = frame->vo_frame.base[1] + width/2 * height/2;
  } else if (format == XINE_IMGFMT_YUY2) {
    frame->vo_frame.pitches[0] = width;
    frame->vo_frame.base[0]    = frame->buffer->data;
  } else {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE": "
            "unsupported frame format %x\n", format);
    frame->vo_frame.width = frame->vo_frame.height = 0;
  }

  frame->displayed = 0;
}

static void mmal_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame, vo_overlay_t *overlay) {

  mmal_driver_t  *this = (mmal_driver_t *) this_gen;

  if (overlay->width <= 0 || overlay->height <= 0 || !overlay->rle)
    return;

  this->alphablend_extra_data.offset_x = frame->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame->overlay_offset_y;

  if (overlay->rle) {
    if( frame->format == XINE_IMGFMT_YV12 )
      _x_blend_yuv( frame->base, overlay, frame->width, frame->height, frame->pitches, &this->alphablend_extra_data);
    else
      _x_blend_yuy2( frame->base[0], overlay, frame->width, frame->height, frame->pitches[0], &this->alphablend_extra_data);
  }
}

static int mmal_redraw_needed (vo_driver_t *this_gen) {

  return 0;
}


static void mmal_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  mmal_driver_t  *this = (mmal_driver_t *) this_gen;
  mmal_frame_t   *frame = (mmal_frame_t *) frame_gen;
  MMAL_PORT_T    *input = this->renderer->input[0];
  MMAL_STATUS_T   status;

  int visible_width  = frame_gen->width  - frame_gen->crop_left - frame_gen->crop_right;
  int visible_height = frame_gen->height - frame_gen->crop_top  - frame_gen->crop_bottom;

  if (input->format->es->video.width       != frame->width         ||
      input->format->es->video.height      != frame->height        ||
      this->renderer_ratio                 != frame_gen->ratio     ||
      input->format->es->video.crop.x      != frame_gen->crop_left ||
      input->format->es->video.crop.y      != frame_gen->crop_top  ||
      input->format->es->video.crop.width  != visible_width        ||
      input->format->es->video.crop.height != visible_height) {

    configure_renderer(this, frame->format, frame->width, frame->height,
                       frame_gen->crop_left, frame_gen->crop_top, visible_width, visible_height, frame_gen->ratio);

    config_display(this, 0, 0, frame_gen->width, frame_gen->height);
  }

  frame->buffer->cmd = 0;
  frame->buffer->length = this->renderer->input[0]->buffer_size;

  pthread_mutex_lock(&this->mutex);

  while (this->frames_in_renderer > 1) {
    pthread_cond_wait(&this->cond, &this->mutex);
  }

  status = mmal_port_send_buffer(this->renderer->input[0], frame->buffer);
  if (status == MMAL_SUCCESS) {
    this->frames_in_renderer++;
  }

  pthread_mutex_unlock(&this->mutex);

  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to send frame to renderer input port");
    frame_gen->free(frame_gen);
    return;
  }

  if (frame->displayed) {
    frame_gen->free(frame_gen);
    return;
  }

  frame->displayed = 1;
}

static int mmal_get_property (vo_driver_t *this_gen, int property) {

  mmal_driver_t *this = (mmal_driver_t *) this_gen;

  switch (property) {
    case VO_PROP_WINDOW_WIDTH:
      return this->gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->gui_height;
    case VO_PROP_MAX_VIDEO_WIDTH:
      return MAX_VIDEO_WIDTH;
    case VO_PROP_MAX_VIDEO_HEIGHT:
      return MAX_VIDEO_HEIGHT;
    case VO_PROP_MAX_NUM_FRAMES:
      return MAX_VIDEO_FRAMES;
  }
  return 0;
}

static int mmal_set_property (vo_driver_t *this_gen, int property, int value) {

  return value;
}

static void mmal_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max) {

  *min = *max = 0;
}

static int mmal_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data) {

  return -1;
}

static void mmal_dispose (vo_driver_t * this_gen) {

  mmal_driver_t      *this = (mmal_driver_t*) this_gen;

  if (this->renderer) {
    disable_renderer(this);
    mmal_component_release(this->renderer);
  }

  if (this->pool) {
    mmal_pool_destroy(this->pool);
  }

  _x_alphablend_free(&this->alphablend_extra_data);

  pthread_cond_destroy(&this->cond);
  pthread_mutex_destroy(&this->mutex);

  free(this);

  bcm_host_deinit();
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {

  mmal_class_t  *class = (mmal_class_t*) class_gen;
  mmal_driver_t *this;
  MMAL_STATUS_T  status;

  this = (mmal_driver_t *) calloc(1, sizeof(mmal_driver_t));
  if (!this)
    return NULL;

  this->xine          = class->xine;
  this->capabilities  = VO_CAP_YUY2 | VO_CAP_YV12 | VO_CAP_CROP;

  pthread_mutex_init (&this->mutex, NULL);
  pthread_cond_init (&this->cond, NULL);

  bcm_host_init();

  status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &this->renderer);
  if (status != MMAL_SUCCESS) {
    LOG_STATUS("failed to create MMAL component " MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER);
    mmal_dispose(&this->vo_driver);
    return NULL;
  }

  this->renderer->control->userdata  = (struct MMAL_PORT_USERDATA_T *)this;
  this->renderer->input[0]->userdata = (struct MMAL_PORT_USERDATA_T *)this;

  configure_renderer(this, XINE_IMGFMT_YV12, 720, 576, 0, 0, 720, 576, 4.0/3.0);
  update_tv_resolution(this);
  config_display(this, 0, 0, 720, 576);

  this->vo_driver.get_capabilities     = mmal_get_capabilities;
  this->vo_driver.alloc_frame          = mmal_alloc_frame;
  this->vo_driver.update_frame_format  = mmal_update_frame_format;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = mmal_overlay_blend;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.display_frame        = mmal_display_frame;
  this->vo_driver.get_property         = mmal_get_property;
  this->vo_driver.set_property         = mmal_set_property;
  this->vo_driver.get_property_min_max = mmal_get_property_min_max;
  this->vo_driver.gui_data_exchange    = mmal_gui_data_exchange;
  this->vo_driver.dispose              = mmal_dispose;
  this->vo_driver.redraw_needed        = mmal_redraw_needed;

  return &this->vo_driver;
}

/**
 * Class Functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
  mmal_class_t      *this;

  this = (mmal_class_t*) calloc(1, sizeof(mmal_class_t));

  this->driver_class.open_plugin      = open_plugin;
  this->driver_class.identifier       = "MMAL";
  this->driver_class.description      = N_("xine video output plugin using MMAL");
  this->driver_class.dispose          = default_video_driver_class_dispose;

  this->xine                          = xine;

  return this;
}

static const vo_info_t vo_info_mmal = {
  10,                  /* priority */
  XINE_VISUAL_TYPE_FB, /* visual type supported by this plugin */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "mmal", XINE_VERSION_CODE, &vo_info_mmal, init_class },
  { PLUGIN_NONE, 0, "" , 0 , NULL, NULL}
};
