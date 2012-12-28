/*
 * Copyright (C) 2003, 2004 the xine project
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
 * video_out_caca.c, Color AsCii Art output plugin for xine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <cucul.h>
#include <caca.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include "yuv2rgb.h"
#include <xine/xineutils.h>

/*
 * structures
 */

typedef struct caca_frame_s {

  vo_frame_t         vo_frame;

  cucul_dither_t *pixmap_s;  /* pixmap info structure */
  uint8_t            *pixmap_d;  /* pixmap data */
  int                width, height;

  int                format;  /* XINE_IMGFMT_* flags */

  yuv2rgb_t          *yuv2rgb;

} caca_frame_t;

typedef struct {
  vo_driver_t        vo_driver;

  config_values_t   *config;
  xine_t            *xine;
  int                user_ratio;

  yuv2rgb_factory_t *yuv2rgb_factory;

  cucul_canvas_t *cv;
  caca_display_t *dp;

} caca_driver_t;

typedef struct {

  video_driver_class_t driver_class;
  config_values_t     *config;
  xine_t              *xine;

} caca_class_t;

/*
 * video driver
 */
static uint32_t caca_get_capabilities (vo_driver_t *this) {
  return VO_CAP_YV12 | VO_CAP_YUY2;
}

static void caca_dispose_frame (vo_frame_t *vo_img) {
  caca_frame_t *frame = (caca_frame_t *)vo_img;

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);

  free (frame->pixmap_d);
  if (frame->pixmap_s)
    cucul_free_dither (frame->pixmap_s);

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  free (frame);
}

static void caca_frame_field (vo_frame_t *vo_img, int which_field) {
  /* nothing to be done here */
}


static vo_frame_t *caca_alloc_frame(vo_driver_t *this_gen) {
  caca_driver_t *this = (caca_driver_t*) this_gen;
  caca_frame_t  *frame;

  frame = calloc(1, sizeof (caca_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field = caca_frame_field;
  frame->vo_frame.dispose = caca_dispose_frame;
  frame->vo_frame.driver = this_gen;

  /* colorspace converter for this frame */
  frame->yuv2rgb =
    this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

  return (vo_frame_t*) frame;
}

static void caca_update_frame_format (vo_driver_t *this_gen, vo_frame_t *img,
                                      uint32_t width, uint32_t height,
                                      double ratio, int format, int flags) {
  caca_driver_t *this = (caca_driver_t*) this_gen;
  caca_frame_t  *frame = (caca_frame_t *) img;

  if ((frame->width != width) || (frame->height != height)
      || (frame->format != format)) {

    av_freep (&frame->vo_frame.base[0]);
    av_freep (&frame->vo_frame.base[1]);
    av_freep (&frame->vo_frame.base[2]);

    free (frame->pixmap_d); frame->pixmap_d = NULL;

    if (frame->pixmap_s) {
      cucul_free_dither (frame->pixmap_s);
      frame->pixmap_s = NULL;
    }

    frame->width  = width;
    frame->height = height;
    frame->format = format;

    frame->pixmap_d = (uint8_t *) xine_xmalloc (height * width * 4);
    frame->pixmap_s = cucul_create_dither (32, width, height, width * 4,
      0xff0000, 0xff00, 0xff, 0);

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz(frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz(frame->vo_frame.pitches[2] * ((height+1)/2));
      frame->yuv2rgb->configure (frame->yuv2rgb,
        width, height, frame->vo_frame.pitches[0], frame->vo_frame.pitches[1],
        width, height, width * 4);
    } else if (format == XINE_IMGFMT_YUY2) {
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
      frame->yuv2rgb->configure (frame->yuv2rgb,
        width, height, frame->vo_frame.pitches[0], frame->vo_frame.pitches[0],
        width, height, width * 4);
    } else {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "alert! unsupported image format %04x\n", format);
      _x_abort();
    }
  }
}

static void caca_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  /* caca_driver_t *this = (caca_driver_t*) this_gen; */
  caca_frame_t *frame = (caca_frame_t *) frame_gen;
   caca_driver_t *this = (caca_driver_t*) this_gen;
  if (frame->format == XINE_IMGFMT_YV12) {
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->pixmap_d,
      frame->vo_frame.base[0],
      frame->vo_frame.base[1],
      frame->vo_frame.base[2]);
  } else {  /* frame->format == XINE_IMGFMT_YUY2 */
    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->pixmap_d,
      frame->vo_frame.base[0]);
  }

  frame->vo_frame.free (&frame->vo_frame);

  cucul_dither_bitmap(this->cv, 0, 0, cucul_get_canvas_width(this->cv)-1, cucul_get_canvas_height(this->cv)-1,
    frame->pixmap_s, frame->pixmap_d);
  caca_refresh_display (this->dp);
}

static int caca_get_property (vo_driver_t *this_gen, int property) {
  caca_driver_t *this = (caca_driver_t*) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    return this->user_ratio;
  } else {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
      "video_out_caca: tried to get unsupported property %d\n", property);
  }

  return 0;
}

static int caca_set_property (vo_driver_t *this_gen,
                              int property, int value) {
  caca_driver_t *this = (caca_driver_t*) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;
    this->user_ratio = value;

  } else {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
      "video_out_caca: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void caca_get_property_min_max (vo_driver_t *this_gen,
                                       int property, int *min, int *max) {
  *min = 0;
  *max = 0;
}

static void caca_dispose_driver (vo_driver_t *this_gen) {
  caca_driver_t *this = (caca_driver_t*) this_gen;
  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);
     caca_free_display(this->dp);
    cucul_free_canvas(this->cv);

}

static int caca_redraw_needed (vo_driver_t *this_gen) {
  return 0;
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  caca_class_t         *class = (caca_class_t *) class_gen;
  caca_display_t       *dp = (caca_display_t *)visual_gen;
  caca_driver_t        *this;

  this = calloc(1, sizeof (caca_driver_t));

  this->config = class->config;
  this->xine   = class->xine;

  this->vo_driver.get_capabilities     = caca_get_capabilities;
  this->vo_driver.alloc_frame          = caca_alloc_frame;
  this->vo_driver.update_frame_format  = caca_update_frame_format;
  this->vo_driver.display_frame        = caca_display_frame;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = NULL;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.get_property         = caca_get_property;
  this->vo_driver.set_property         = caca_set_property;
  this->vo_driver.get_property_min_max = caca_get_property_min_max;
  this->vo_driver.gui_data_exchange    = NULL;
  this->vo_driver.redraw_needed        = caca_redraw_needed;
  this->vo_driver.dispose              = caca_dispose_driver;

  this->yuv2rgb_factory = yuv2rgb_factory_init(MODE_32_RGB, 0, NULL);
  this->yuv2rgb_factory->set_csc_levels(this->yuv2rgb_factory, 0, 128, 128, CM_DEFAULT);

  if (dp) {
    this->cv = caca_get_canvas(dp);
    this->dp = dp;
  } else {
    this->cv = cucul_create_canvas(0, 0);
    this->dp = caca_create_display(this->cv);
  }

  caca_refresh_display(this->dp);
  return &this->vo_driver;
}

static void *init_class (xine_t *xine, void *visual_gen) {
  caca_class_t    *this;

  this = calloc(1, sizeof(caca_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "CACA";
  this->driver_class.description     = N_("xine video output plugin using the Color AsCii Art library");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config            = xine->config;
  this->xine              = xine;

  return this;
}

static const vo_info_t vo_info_caca = {
  6,
  XINE_VISUAL_TYPE_CACA
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "caca", XINE_VERSION_CODE, &vo_info_caca, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
