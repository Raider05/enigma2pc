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
 *
 * video_out_sdl.c, Simple DirectMedia Layer
 *
 * based on mpeg2dec code from
 *   Ryan C. Gordon <icculus@lokigames.com> and
 *   Dominik Schnitzer <aeneas@linuxvideo.org>
 *
 * xine version by Miguel Freitas (Jan/2002)
 *   Missing features:
 *    - mouse position translation
 *    - fullscreen
 *    - stability, testing, etc?? ;)
 *   Known bugs:
 *    - without X11, a resize is need to show image (?)
 *
 *  BIG WARNING HERE: if you use RedHat SDL packages you will probably
 *  get segfault when no hwaccel is available. more info at:
 *    https://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=58408
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#ifdef HAVE_SDL11_SDL_H
# include <SDL11/SDL.h>
#else
# include <SDL.h>
#endif

#define LOG_MODULE "video_out_sdl"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/xineutils.h>
#include <xine/vo_scale.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

typedef struct sdl_driver_s sdl_driver_t;

typedef struct sdl_frame_s {
    vo_frame_t vo_frame;
    int width, height, format;
    double ratio;
    SDL_Overlay * overlay;
} sdl_frame_t;


struct sdl_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  int                hw_accel;

  SDL_Surface       *surface;
  uint32_t           sdlflags;
  uint8_t            bpp;

  pthread_mutex_t    mutex;

  uint32_t           capabilities;

#ifdef HAVE_X11
   /* X11 / Xv related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
#endif

  vo_scale_t         sc;
  xine_t            *xine;

  alphablend_t       alphablend_extra_data;
};

typedef struct {
  video_driver_class_t driver_class;
  config_values_t     *config;
  xine_t              *xine;
} sdl_class_t;

static uint32_t sdl_get_capabilities (vo_driver_t *this_gen) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;

  return this->capabilities;
}

static void sdl_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for SDL */
}

static void sdl_frame_dispose (vo_frame_t *vo_img) {

  sdl_frame_t  *frame = (sdl_frame_t *) vo_img ;

  if( frame->overlay )
    SDL_FreeYUVOverlay (frame->overlay);

  free (frame);
}

static vo_frame_t *sdl_alloc_frame (vo_driver_t *this_gen) {
  /* sdl_driver_t    *this = (sdl_driver_t *) this_gen; */
  sdl_frame_t     *frame ;

  frame = (sdl_frame_t *) calloc(1, sizeof(sdl_frame_t));

  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = sdl_frame_field;
  frame->vo_frame.dispose    = sdl_frame_dispose;

  return (vo_frame_t *) frame;
}

static void sdl_compute_ideal_size (sdl_driver_t *this) {

  _x_vo_scale_compute_ideal_size( &this->sc );
}

static void sdl_compute_output_size (sdl_driver_t *this) {

  _x_vo_scale_compute_output_size( &this->sc );

  lprintf ("frame source %d x %d => screen output %d x %d\n",
	   this->sc.delivered_width, this->sc.delivered_height,
	   this->sc.output_width, this->sc.output_height);
}


static void sdl_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags) {

  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    /*
     * (re-) allocate image
     */

    if( frame->overlay ) {
      SDL_FreeYUVOverlay (frame->overlay);
      frame->overlay = NULL;
    }

    if( format == XINE_IMGFMT_YV12 ) {
      lprintf ("format YV12 ");
      frame->overlay = SDL_CreateYUVOverlay (width, height, SDL_YV12_OVERLAY,
					     this->surface);

    } else if( format == XINE_IMGFMT_YUY2 ) {
      lprintf ("format YUY2 ");
      frame->overlay = SDL_CreateYUVOverlay (width, height, SDL_YUY2_OVERLAY,
					     this->surface);
    }

    if (frame->overlay == NULL)
      return;

	/*
	 * This needs to be done becuase I have found that
	 * pixels isn't setup until this is called.
	 */
    SDL_LockYUVOverlay (frame->overlay);

    frame->vo_frame.pitches[0] = frame->overlay->pitches[0];
    frame->vo_frame.pitches[1] = frame->overlay->pitches[2];
    frame->vo_frame.pitches[2] = frame->overlay->pitches[1];
    frame->vo_frame.base[0] = frame->overlay->pixels[0];
    frame->vo_frame.base[1] = frame->overlay->pixels[2];
    frame->vo_frame.base[2] = frame->overlay->pixels[1];

    frame->width  = width;
    frame->height = height;
    frame->format = format;
  }
  else {

    SDL_LockYUVOverlay (frame->overlay);
  }

  frame->ratio = ratio;
}


/*
 *
 */
static void sdl_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;

  if (overlay->rle) {
    if( frame->format == XINE_IMGFMT_YV12 )
      _x_blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches, &this->alphablend_extra_data);
    else
      _x_blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0], &this->alphablend_extra_data);
  }

}

static void sdl_check_events (sdl_driver_t * this)
{
  SDL_Event event;

  while (SDL_PollEvent (&event)) {
    if (event.type == SDL_VIDEORESIZE) {
      if( event.resize.w != this->sc.gui_width || event.resize.h != this->sc.gui_height ) {
        this->sc.gui_width = event.resize.w;
        this->sc.gui_height = event.resize.h;

        sdl_compute_output_size(this);

        this->surface = SDL_SetVideoMode (this->sc.gui_width, this->sc.gui_height,
                                      this->bpp, this->sdlflags);
      }
    }
  }
}

static int sdl_redraw_needed (vo_driver_t *this_gen) {
  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  int ret = 0;

#ifdef HAVE_X11

  if( _x_vo_scale_redraw_needed( &this->sc ) ) {

    sdl_compute_output_size (this);

    ret = 1;
  }

  return ret;

#else

  static int last_gui_width, last_gui_height;

  if( last_gui_width != this->sc.gui_width ||
      last_gui_height != this->sc.gui_height ||
      this->sc.force_redraw ) {

    last_gui_width = this->sc.gui_width;
    last_gui_height = this->sc.gui_height;

    sdl_compute_output_size (this);

    ret = 1;
  }

  this->sc.force_redraw = 0;

  return ret;

#endif
}


static void sdl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;
  SDL_Rect clip_rect;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->sc.delivered_width)
	 || (frame->height != this->sc.delivered_height)
	 || (frame->ratio != this->sc.delivered_ratio) ) {
	 xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_sdl: change frame format\n");

      this->sc.delivered_width  = frame->width;
      this->sc.delivered_height = frame->height;
      this->sc.delivered_ratio  = frame->ratio;

      sdl_compute_ideal_size( this );

      this->sc.force_redraw = 1;
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  sdl_check_events (this);
  sdl_redraw_needed (this_gen);

  SDL_UnlockYUVOverlay (frame->overlay);
  clip_rect.x = this->sc.output_xoffset;
  clip_rect.y = this->sc.output_yoffset;
  clip_rect.w = this->sc.output_width;
  clip_rect.h = this->sc.output_height;
  SDL_DisplayYUVOverlay (frame->overlay, &clip_rect);

  frame->vo_frame.free (&frame->vo_frame);

  pthread_mutex_unlock(&this->mutex);
}

static int sdl_get_property (vo_driver_t *this_gen, int property) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;

  switch (property) {
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    case VO_PROP_OUTPUT_WIDTH:
      return this->sc.output_width;
    case VO_PROP_OUTPUT_HEIGHT:
      return this->sc.output_height;
    case VO_PROP_OUTPUT_XOFFSET:
      return this->sc.output_xoffset;
    case VO_PROP_OUTPUT_YOFFSET:
      return this->sc.output_yoffset;
    case VO_PROP_ASPECT_RATIO:
    return this->sc.user_ratio;
  }
  return 0;
}

static int sdl_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;
    this->sc.user_ratio = value;
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_sdl: aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name_table[value]);

    sdl_compute_ideal_size (this);
    this->sc.force_redraw = 1;
  }

  return value;
}

static void sdl_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

/*  sdl_driver_t *this = (sdl_driver_t *) this_gen; */
}

static int sdl_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
#ifdef HAVE_X11
  sdl_driver_t     *this = (sdl_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);

  switch (data_type) {

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    lprintf ("XINE_GUI_SEND_DRAWABLE_CHANGED\n");

    this->drawable = (Drawable) data;
    /* OOPS! Is it possible to change SDL window id? */
    /* probably we need to close and reinitialize SDL */
    break;

  case XINE_GUI_SEND_EXPOSE_EVENT:
    lprintf ("XINE_GUI_SEND_EXPOSE_EVENT\n");
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y,
			     &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h,
			     &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
    break;

  default:
    ret = -1;
  }
  pthread_mutex_unlock(&this->mutex);

#else
  ret = -1;
#endif

  return ret;
}

static void sdl_dispose (vo_driver_t * this_gen) {

  sdl_driver_t      *this = (sdl_driver_t*) this_gen;

  SDL_FreeSurface (this->surface);
  SDL_QuitSubSystem (SDL_INIT_VIDEO);

  _x_alphablend_free(&this->alphablend_extra_data);

  free(this);
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {

  sdl_class_t          *class = (sdl_class_t*) class_gen;
  config_values_t      *config = class->config;
  sdl_driver_t         *this;

  const SDL_VideoInfo  *vidInfo;
#if defined(HAVE_X11) || defined(WIN32)
  static char           SDL_windowhack[32];
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
#endif
#ifdef HAVE_X11
  XWindowAttributes     window_attributes;
#endif

  this = (sdl_driver_t *) calloc(1, sizeof(sdl_driver_t));
  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->sdlflags = SDL_HWSURFACE | SDL_RESIZABLE;

  this->hw_accel = class->config->register_bool(class->config,
    "video.device.sdl_hw_accel", 1,
    _("use hardware acceleration if available"),
    _("When your system supports it, hardware acceleration provided by your "
      "graphics hardware will be used. This might not work, so you can disable it, "
      "if things go wrong."), 10, NULL, NULL);

  xine_setenv("SDL_VIDEO_YUV_HWACCEL", (this->hw_accel) ? "1" : "0", 1);
  xine_setenv("SDL_VIDEO_X11_NODIRECTCOLOR", "1", 1);

  this->xine              = class->xine;
#ifdef HAVE_X11
  this->display           = visual->display;
  this->screen            = visual->screen;
  this->drawable          = visual->d;

  _x_vo_scale_init( &this->sc, 0, 0, config);
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;
#else
  _x_vo_scale_init( &this->sc, 0, 0, config );
#endif

#if defined(HAVE_X11) || defined(WIN32)
  /* set SDL to use our existing X11/win32 window */
  if (visual->d){
    sprintf(SDL_windowhack,"SDL_WINDOWID=0x%x", (uint32_t) visual->d);
    putenv(SDL_windowhack);
  }
#endif

  if ((SDL_Init (SDL_INIT_VIDEO)) < 0) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "video_out_sdl: open_plugin - sdl video initialization failed.\n");
    return NULL;
  }

  vidInfo = SDL_GetVideoInfo ();
  if (!SDL_ListModes (vidInfo->vfmt, SDL_HWSURFACE | SDL_RESIZABLE)) {
    this->sdlflags = SDL_RESIZABLE;
    if (!SDL_ListModes (vidInfo->vfmt, SDL_RESIZABLE)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "video_out_sdl: open_plugin - sdl couldn't get any acceptable video mode\n");
      return NULL;
    }
  }

  this->bpp = vidInfo->vfmt->BitsPerPixel;
  if (this->bpp < 16) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("sdl has to emulate a 16 bit surfaces, that will slow things down.\n"));
    this->bpp = 16;
  }

  this->config            = class->config;
  pthread_mutex_init (&this->mutex, NULL);

  this->capabilities      = VO_CAP_YUY2 | VO_CAP_YV12;

#ifdef HAVE_X11
  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->sc.gui_width         = window_attributes.width;
  this->sc.gui_height        = window_attributes.height;
#else
  this->sc.gui_width         = 320;
  this->sc.gui_height        = 240;
#endif

  this->surface = SDL_SetVideoMode (this->sc.gui_width, this->sc.gui_height,
                                    this->bpp, this->sdlflags);

  this->vo_driver.get_capabilities     = sdl_get_capabilities;
  this->vo_driver.alloc_frame          = sdl_alloc_frame;
  this->vo_driver.update_frame_format  = sdl_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = sdl_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = sdl_display_frame;
  this->vo_driver.get_property         = sdl_get_property;
  this->vo_driver.set_property         = sdl_set_property;
  this->vo_driver.get_property_min_max = sdl_get_property_min_max;
  this->vo_driver.gui_data_exchange    = sdl_gui_data_exchange;
  this->vo_driver.dispose              = sdl_dispose;
  this->vo_driver.redraw_needed        = sdl_redraw_needed;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "video_out_sdl: warning, xine's SDL driver is EXPERIMENTAL\n");
  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "video_out_sdl: in case of trouble, try setting video.device.sdl_hw_accel=0\n");
  xprintf (this->xine, XINE_VERBOSITY_LOG, _("video_out_sdl: fullscreen mode is NOT supported\n"));
  return &this->vo_driver;
}
/**
 * Class Functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
  /* x11_visual_t     *visual = (x11_visual_t *) visual_gen; */
  sdl_class_t      *this;

  /* check if we have SDL */
  if ((SDL_Init (SDL_INIT_VIDEO)) < 0) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "video_out_sdl: open_plugin - sdl video initialization failed.\n");
    return NULL;
  }
  SDL_QuitSubSystem (SDL_INIT_VIDEO);

  this = (sdl_class_t*) calloc(1, sizeof(sdl_class_t));

  this->driver_class.open_plugin      = open_plugin;
  this->driver_class.identifier       = "SDL";
  this->driver_class.description      = N_("xine video output plugin using the Simple Direct Media Layer");
  this->driver_class.dispose          = default_video_driver_class_dispose;

  this->config                        = xine->config;
  this->xine                          = xine;

  return this;
}

static const vo_info_t vo_info_sdl = {
  4,                    /* priority */
  XINE_VISUAL_TYPE_X11, /* visual type supported by this plugin */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "sdl", XINE_VERSION_CODE, &vo_info_sdl, init_class },
  { PLUGIN_NONE, 0, "" , 0 , NULL, NULL}
};
