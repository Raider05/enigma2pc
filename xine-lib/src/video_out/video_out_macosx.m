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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * This output driver makes use of xine's objective-c video_output 
 * classes located in the macosx folder.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define LOG_MODULE "video_out_macosx"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include "xine/video_out.h"
#include "xine/vo_scale.h"
#include "xine/xine_internal.h"
#include "xine/xineutils.h"

#include "macosx/video_window.h"

typedef struct {
  vo_frame_t            vo_frame;
  int                   width;
  int                   height;
  double                ratio;
  int                   format;
  xine_t               *xine;
} macosx_frame_t;

typedef struct {
  vo_driver_t           vo_driver;
  config_values_t      *config;
  int                   ratio;
  xine_t               *xine;
  id                    view;
  alphablend_t          alphablend_extra_data;
} macosx_driver_t;

typedef struct {
  video_driver_class_t  driver_class;
  config_values_t      *config;
  xine_t               *xine;
} macosx_class_t;


static void free_framedata(macosx_frame_t* frame) {
  if(frame->vo_frame.base[0]) {
    free(frame->vo_frame.base[0]);
    frame->vo_frame.base[0] = NULL;
    frame->vo_frame.base[1] = NULL;
    frame->vo_frame.base[2] = NULL;
  }
}

static void macosx_frame_dispose(vo_frame_t *vo_frame) {
  macosx_frame_t *frame = (macosx_frame_t *)vo_frame;
  free_framedata(frame);  
  free (frame);
}

static void macosx_frame_field(vo_frame_t *vo_frame, int which_field) {
  /* do nothing */
}

static uint32_t macosx_get_capabilities(vo_driver_t *vo_driver) {
  /* both styles, country and western */
  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_UNSCALED_OVERLAY;
}

static vo_frame_t *macosx_alloc_frame(vo_driver_t *vo_driver) {
  /* macosx_driver_t *this = (macosx_driver_t *) vo_driver; */
  macosx_frame_t  *frame;
  
  frame = calloc(1, sizeof(macosx_frame_t));
  if(!frame)
    return NULL;

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.base[0] = NULL;
  frame->vo_frame.base[1] = NULL;
  frame->vo_frame.base[2] = NULL;
  
  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = macosx_frame_field;
  frame->vo_frame.dispose    = macosx_frame_dispose;
  frame->vo_frame.driver     = vo_driver;
  
  return (vo_frame_t *)frame;
}

static void macosx_update_frame_format(vo_driver_t *vo_driver, vo_frame_t *vo_frame,
                                     uint32_t width, uint32_t height, 
                                     double ratio, int format, int flags) {
  macosx_driver_t *this = (macosx_driver_t *) vo_driver;
  macosx_frame_t  *frame = (macosx_frame_t *) vo_frame;

  if((frame->width != width) || (frame->height != height) ||
     (frame->format != format)) {
    
    NSSize video_size = NSMakeSize(width, height);
    
    free_framedata(frame);
    
    frame->width  = width;
    frame->height = height;
    frame->format = format;

    lprintf ("frame change, new height:%d width:%d (ratio:%lf) format:%d\n",
             height, width, ratio, format);

    switch(format) {

    case XINE_IMGFMT_YV12: 
      {
        int y_size, uv_size;
        
        frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
        frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
        frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
        
        y_size  = frame->vo_frame.pitches[0] * height;
        uv_size = frame->vo_frame.pitches[1] * ((height+1)/2);
        
        frame->vo_frame.base[0] = malloc (y_size + 2*uv_size);
        frame->vo_frame.base[1] = frame->vo_frame.base[0]+y_size+uv_size;
        frame->vo_frame.base[2] = frame->vo_frame.base[0]+y_size;
      }
      break;

    case XINE_IMGFMT_YUY2:
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = NULL;
      frame->vo_frame.base[2] = NULL;
      break;

    default:
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "video_out_macosx: unknown frame format %04x)\n", format);
      break;

    }

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [this->view setVideoSize:video_size];
    [pool release];

    if((format == XINE_IMGFMT_YV12
        && (frame->vo_frame.base[0] == NULL 
            || frame->vo_frame.base[1] == NULL 
            || frame->vo_frame.base[2] == NULL))
            || (format == XINE_IMGFMT_YUY2 && frame->vo_frame.base[0] == NULL)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "video_out_macosx: error. (framedata allocation failed: out of memory)\n"); 
      free_framedata(frame);
    }
  }

  frame->ratio = ratio;
}

static void macosx_display_frame(vo_driver_t *vo_driver, vo_frame_t *vo_frame) {
  macosx_driver_t  *driver = (macosx_driver_t *)vo_driver;
  macosx_frame_t   *frame = (macosx_frame_t *)vo_frame;
  char *texture_buffer;
  
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  if ((texture_buffer = [driver->view textureBuffer]) != NULL) {
    switch (vo_frame->format) {
      case XINE_IMGFMT_YV12: 
        yv12_to_yuy2 (vo_frame->base[0], vo_frame->pitches[0],
                      vo_frame->base[1], vo_frame->pitches[1],
                      vo_frame->base[2], vo_frame->pitches[2],
                      (unsigned char *)texture_buffer,
                      vo_frame->width * 2,
                      vo_frame->width, vo_frame->height, 0);

        [driver->view updateTexture];
        break;
      case XINE_IMGFMT_YUY2:
        xine_fast_memcpy (texture_buffer, vo_frame->base[0], 
	                  vo_frame->pitches[0] * vo_frame->height);
        [driver->view updateTexture];
        break;
      default:
        /* unsupported frame format, do nothing. */
        break;
    }
  }

  frame->vo_frame.free(&frame->vo_frame);
  [pool release];
}

static void macosx_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                  vo_overlay_t *overlay) {
  macosx_driver_t *this = (macosx_driver_t *) this_gen;
  macosx_frame_t *frame = (macosx_frame_t *) frame_gen;

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
  
  /* TODO: should check here whether the overlay has changed or not: use a
   * ovl_changed boolean variable similarly to video_out_xv */
  if (overlay->rle) {
    if (frame->format == XINE_IMGFMT_YV12)
      /* TODO: It may be possible to accelerate the blending via Quartz
       * Extreme ... */
      _x_blend_yuv(frame->vo_frame.base, overlay,
          frame->width, frame->height, frame->vo_frame.pitches,
          &this->alphablend_extra_data);
    else
      _x_blend_yuy2(frame->vo_frame.base[0], overlay,
          frame->width, frame->height, frame->vo_frame.pitches[0],
          &this->alphablend_extra_data);
  }
}

static int macosx_get_property(vo_driver_t *vo_driver, int property) {
  macosx_driver_t  *driver = (macosx_driver_t *)vo_driver;
  
  switch(property) {

  case VO_PROP_ASPECT_RATIO:
    return driver->ratio;
    break;
    
  default:
    break;
  }

  return 0;
}

static int macosx_set_property(vo_driver_t *vo_driver, int property, int value) {
  macosx_driver_t  *driver = (macosx_driver_t *)vo_driver;
  
  switch(property) {

  case VO_PROP_ASPECT_RATIO:
    if(value >= XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;

    driver->ratio = value;
    break;

  default:
    break;
  }
  return value;
}

static void macosx_get_property_min_max(vo_driver_t *vo_driver,
                                        int property, int *min, int *max) {
  *min = 0;
  *max = 0;
}

static int macosx_gui_data_exchange(vo_driver_t *vo_driver, int data_type, void *data) {
/*   macosx_driver_t     *this = (macosx_driver_t *) vo_driver; */

  switch (data_type) {
  case XINE_GUI_SEND_COMPLETION_EVENT:
  case XINE_GUI_SEND_DRAWABLE_CHANGED:
  case XINE_GUI_SEND_EXPOSE_EVENT:
  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
  case XINE_GUI_SEND_VIDEOWIN_VISIBLE:
  case XINE_GUI_SEND_SELECT_VISUAL:
  default:
    lprintf("unknown GUI data type %d\n", data_type);
    break;
  }

  return 0;
}
static void macosx_dispose(vo_driver_t *vo_driver) {
  macosx_driver_t *this = (macosx_driver_t *) vo_driver;

  _x_alphablend_free(&this->alphablend_extra_data);
  [this->view releaseInMainThread];

  free(this);
}

static int macosx_redraw_needed(vo_driver_t *vo_driver) {
  return 0;
}


static vo_driver_t *open_plugin(video_driver_class_t *driver_class, const void *visual) {
  macosx_class_t    *class = (macosx_class_t *) driver_class;
  macosx_driver_t   *driver;
  XineOpenGLView    *view = (XineOpenGLView *) visual;
  
  driver = calloc(1, sizeof(macosx_driver_t));

  driver->config = class->config;
  driver->xine   = class->xine;
  driver->ratio  = XINE_VO_ASPECT_AUTO;
  driver->view   = [view retain];
  
  driver->vo_driver.get_capabilities     = macosx_get_capabilities;
  driver->vo_driver.alloc_frame          = macosx_alloc_frame;
  driver->vo_driver.update_frame_format  = macosx_update_frame_format;
  driver->vo_driver.overlay_begin        = NULL; /* not used */
  driver->vo_driver.overlay_blend        = macosx_overlay_blend;
  driver->vo_driver.overlay_end          = NULL; /* not used */
  driver->vo_driver.display_frame        = macosx_display_frame;
  driver->vo_driver.get_property         = macosx_get_property;
  driver->vo_driver.set_property         = macosx_set_property;
  driver->vo_driver.get_property_min_max = macosx_get_property_min_max;
  driver->vo_driver.gui_data_exchange    = macosx_gui_data_exchange;
  driver->vo_driver.dispose              = macosx_dispose;
  driver->vo_driver.redraw_needed        = macosx_redraw_needed;
 
  _x_alphablend_init(&driver->alphablend_extra_data, class->xine);

  return &driver->vo_driver;
}    

/*
 * Class related functions.
 */

static void *init_class (xine_t *xine, void *visual) {
  macosx_class_t        *this;
  
  this = calloc(1, sizeof(macosx_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "MacOSX";
  this->driver_class.description     = N_("xine video output plugin for Mac OS X");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static const vo_info_t vo_info_macosx = {
  1,                        /* Priority    */
  XINE_VISUAL_TYPE_MACOSX   /* Visual type */
};

plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */  
  /* work around the problem that dlclose() is not allowed to
   * get rid of an image module which contains objective C code and simply
   * crashes with a Trace/BPT trap when we try to do so */
  { PLUGIN_VIDEO_OUT | PLUGIN_NO_UNLOAD, 22, "macosx", XINE_VERSION_CODE, &vo_info_macosx, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

