/*
 * Copyright (C) 2000-2014 the xine project
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
 * video_out_xcbshm.c, X11 shared memory extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 *
 * ported to xcb by Christoph Pfister - Feb 2007
 *
 * fullrange/HD color and crop support added by Torsten Jager <t.jager@gmx.de>
 */

#define LOG_MODULE "video_out_xcbshm"
#define LOG_VERBOSE
/*
#define LOG
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xine.h"
#include <xine/video_out.h>

#include <errno.h>

#include <xcb/shm.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <pthread.h>
#include <netinet/in.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#include <xine/xine_internal.h>
#include "yuv2rgb.h"
#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "xcbosd.h"

#include "xine_mmx.h"

typedef struct {
  vo_frame_t         vo_frame;

  /* frame properties as delivered by the decoder: */
  /* obs: for width/height use vo_scale_t struct */
  int                format;
  int                flags;

  vo_scale_t         sc;

  uint8_t           *image;
  int                bytes_per_line;
  xcb_shm_seg_t      shmseg;

  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */
  uint8_t           *rgb_dst;

  int                state, offs0, offs1; /* crop helpers */
  uint8_t            *crop_start, *crop_flush, *crop_stop;
} xshm_frame_t;

/* frame.state */
#define FS_DONE  1
#define FS_LATE  2
#define FS_FLAGS 4


typedef struct {

  vo_driver_t        vo_driver;

  /* xcb / shm related stuff */
  xcb_connection_t  *connection;
  xcb_screen_t      *screen;
  xcb_window_t       window;
  xcb_gcontext_t     gc;
  int                depth;
  int                bpp;
  int                scanline_pad;
  int                use_shm;

  int                brightness;
  int                contrast;
  int                saturation;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  /* color matrix switching */
  int                cm_active, cm_state;

  vo_scale_t         sc;

  xshm_frame_t      *cur_frame;
  xcbosd            *xoverlay;
  int                ovl_changed;

  xine_t            *xine;

  alphablend_t       alphablend_extra_data;

  pthread_mutex_t    main_mutex;

} xshm_driver_t;

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  xine_t              *xine;
} xshm_class_t;


/* import common color matrix stuff */
#define CM_DRIVER_T xshm_driver_t
#include "color_matrix.c"

/*
 * allocate an XImage, try XShm first but fall back to
 * plain X11 if XShm should fail
 */
static void create_ximage(xshm_driver_t *this, xshm_frame_t *frame, int width, int height)
{
  frame->bytes_per_line = ((this->bpp * width + this->scanline_pad - 1) &
			   (~(this->scanline_pad - 1))) >> 3;

  if (this->use_shm) {
    int shmid;
    xcb_void_cookie_t shm_attach_cookie;
    xcb_generic_error_t *generic_error;

    /*
     * try shm
     */

    shmid = shmget(IPC_PRIVATE, frame->bytes_per_line * height, IPC_CREAT | 0777);

    if (shmid < 0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: %s: allocating image\n"), LOG_MODULE, strerror(errno));
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      goto shm_fail1;
    }

    frame->image = shmat(shmid, 0, 0);

    if (frame->image == ((void *) -1)) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error (address error) when allocating image \n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      goto shm_fail2;
    }

    frame->shmseg = xcb_generate_id(this->connection);
    shm_attach_cookie = xcb_shm_attach_checked(this->connection, frame->shmseg, shmid, 0);
    generic_error = xcb_request_check(this->connection, shm_attach_cookie);

    if (generic_error != NULL) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: x11 error during shared memory XImage creation\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      free(generic_error);
      goto shm_fail3;
    }

    /*
     * Now that the Xserver has learned about and attached to the
     * shared memory segment,  delete it.  It's actually deleted by
     * the kernel when all users of that segment have detached from
     * it.  Gives an automatic shared memory cleanup in case we crash.
     */

    shmctl(shmid, IPC_RMID, 0);

    return;

  shm_fail3:
    frame->shmseg = 0;
    shmdt(frame->image);
  shm_fail2:
    shmctl(shmid, IPC_RMID, 0);
  shm_fail1:
    this->use_shm = 0;
  }

  /*
   * fall back to plain X11 if necessary
   */

  frame->image = malloc(frame->bytes_per_line * height);
}

static void dispose_ximage(xshm_driver_t *this, xshm_frame_t *frame)
{
  if (frame->shmseg) {
    xcb_shm_detach(this->connection, frame->shmseg);
    frame->shmseg = 0;
    shmdt(frame->image);
  } else
    free(frame->image);
  frame->image = NULL;
}


/*
 * and now, the driver functions
 */

static uint32_t xshm_get_capabilities (vo_driver_t *this_gen) {
  xshm_driver_t *this = (xshm_driver_t *) this_gen;
  uint32_t capabilities = VO_CAP_CROP | VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS
    | VO_CAP_CONTRAST | VO_CAP_SATURATION | VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE;

  if( this->xoverlay )
    capabilities |= VO_CAP_UNSCALED_OVERLAY;

  return capabilities;
}

static void xshm_compute_ideal_size (xshm_driver_t *this, xshm_frame_t *frame) {
  _x_vo_scale_compute_ideal_size( &frame->sc );
}

static void xshm_compute_rgb_size (xshm_driver_t *this, xshm_frame_t *frame) {
  _x_vo_scale_compute_output_size( &frame->sc );

  /* avoid problems in yuv2rgb */
  if (frame->sc.output_height < 1)
    frame->sc.output_height = 1;
  if (frame->sc.output_width < 8)
    frame->sc.output_width = 8;
  if (frame->sc.output_width & 1) /* yuv2rgb_mlib needs an even YUV2 width */
    frame->sc.output_width++;

  lprintf("frame source (%d) %d x %d => screen output %d x %d%s\n",
	  frame->vo_frame.id,
	  frame->sc.delivered_width, frame->sc.delivered_height,
	  frame->sc.output_width, frame->sc.output_height,
	  ( frame->sc.delivered_width != frame->sc.output_width
	    || frame->sc.delivered_height != frame->sc.output_height
	    ? ", software scaling"
	   : "" )
	  );
}

static void xshm_frame_field (vo_frame_t *vo_img, int which_field) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;

  switch (which_field) {
    case VO_BOTH_FIELDS:
    case VO_TOP_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->image;
    break;
    case VO_BOTTOM_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->image + frame->bytes_per_line ;
    break;
  }

  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
}

static void xshm_frame_proc_setup (vo_frame_t *vo_img) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->driver;
  int changed = 0, i;
  int width, height, gui_width, gui_height;
  double gui_pixel_aspect;

  /* Aargh... libmpeg2 decoder calls frame->proc_slice directly, preferredly
    while still in mmx mode. This will trash our floating point aspect ratio
    calculations below. Switching back once per frame should not harm
    performance too much. */
#ifdef HAVE_MMX
  emms ();
#endif

  if (!(frame->state & FS_LATE)) {
    /* adjust cropping to what yuv2rgb can handle */
    if (vo_img->format == XINE_IMGFMT_YV12) {
      vo_img->crop_left &= ~7;
      vo_img->crop_top &= ~1;
    } else {
      vo_img->crop_left &= ~3;
    }
    /* check for crop changes */
    if ((vo_img->crop_left != frame->sc.crop_left)
      || (vo_img->crop_top != frame->sc.crop_top)
      || (vo_img->crop_right != frame->sc.crop_right)
      || (vo_img->crop_bottom != frame->sc.crop_bottom)) {
      frame->sc.crop_left = vo_img->crop_left;
      frame->sc.crop_top = vo_img->crop_top;
      frame->sc.crop_right = vo_img->crop_right;
      frame->sc.crop_bottom = vo_img->crop_bottom;
      changed = 1;
    }
  }

  if (!(frame->state & FS_DONE))
    changed = 1;

  /* just deal with cropped part */
  width  = frame->sc.delivered_width - frame->sc.crop_left - frame->sc.crop_right;
  height = frame->sc.delivered_height - frame->sc.crop_top - frame->sc.crop_bottom;

  if (frame->sc.delivered_ratio == 0.0) {
    frame->sc.delivered_ratio = height ? (double)width / (double)height : 1.0;
    changed = 1;
  }
  
  /* ask gui what output size we'll have for this frame */
  /* get the gui_pixel_aspect before calling xshm_compute_ideal_size() */
  /* note: gui_width and gui_height may be bogus because we may have not yet */
  /*       updated video_pixel_aspect (see _x_vo_scale_compute_ideal_size).  */
  frame->sc.dest_size_cb (frame->sc.user_data, width, height,
    frame->sc.video_pixel_aspect, &gui_width, &gui_height, &gui_pixel_aspect);

  if (changed || (gui_pixel_aspect != frame->sc.gui_pixel_aspect)
    || (this->sc.user_ratio != frame->sc.user_ratio)) {

    frame->sc.gui_pixel_aspect   = gui_pixel_aspect;
    frame->sc.user_ratio         = this->sc.user_ratio;

    xshm_compute_ideal_size (this, frame);

    /* now we have updated video_aspect_pixel we use the callback   */
    /* again to obtain the correct gui_width and gui_height values. */
    frame->sc.dest_size_cb (frame->sc.user_data, width, height,
      frame->sc.video_pixel_aspect, &gui_width, &gui_height, &gui_pixel_aspect);

    changed = 1;
  }

  if (changed || (frame->sc.gui_width != gui_width)
    || (frame->sc.gui_height != gui_height)) {
    int w = frame->sc.output_width, h = frame->sc.output_height;

    frame->sc.gui_width  = gui_width;
    frame->sc.gui_height = gui_height;

    xshm_compute_rgb_size (this, frame);

    if (!frame->image || (w != frame->sc.output_width) || (h != frame->sc.output_height)) {
      /* (re)allocate XImage */
      pthread_mutex_lock(&this->main_mutex);
      if (frame->image)
        dispose_ximage (this, frame);
      create_ximage (this, frame, frame->sc.output_width, frame->sc.output_height);
      pthread_mutex_unlock(&this->main_mutex);
    }

    changed = 1;
  }

  if (changed || !(frame->state & FS_FLAGS)) {
    /* set up colorspace converter */
    switch (vo_img->flags & VO_BOTH_FIELDS) {
      case VO_TOP_FIELD:
      case VO_BOTTOM_FIELD:
        frame->yuv2rgb->configure (frame->yuv2rgb, width, height,
          2 * frame->vo_frame.pitches[0], 2 * frame->vo_frame.pitches[1],
          frame->sc.output_width, frame->sc.output_height,
          frame->bytes_per_line * 2);
      break;
      case VO_BOTH_FIELDS:
        frame->yuv2rgb->configure (frame->yuv2rgb, width, height,
          frame->vo_frame.pitches[0], frame->vo_frame.pitches[1],
          frame->sc.output_width, frame->sc.output_height,
          frame->bytes_per_line);
      break;
    }
  }

  frame->state |= FS_FLAGS | FS_DONE;

  xshm_frame_field (vo_img, vo_img->flags & VO_BOTH_FIELDS);

  /* cache helpers */
  i = frame->sc.crop_top & 15;
  if (i)
    i -= 16;
  if (vo_img->format == XINE_IMGFMT_YV12) {
    frame->offs0 = i * vo_img->pitches[0] + frame->sc.crop_left;
    frame->offs1 = (i * vo_img->pitches[1] + frame->sc.crop_left) / 2;
  } else {
    frame->offs0 = i * vo_img->pitches[0] + frame->sc.crop_left * 2;
  }
  frame->crop_start = vo_img->base[0] + frame->sc.crop_top * vo_img->pitches[0];
  frame->crop_flush = frame->crop_stop = vo_img->base[0]
    + (frame->sc.delivered_height - frame->sc.crop_bottom) * vo_img->pitches[0];
  if (i + frame->sc.crop_bottom < 0)
    frame->crop_flush -= 16 * vo_img->pitches[0];

  /* switch color matrix/range */
  i = cm_from_frame (vo_img);
  if (i != this->cm_active) {
    this->cm_active = i;
    this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
      this->brightness, this->contrast, this->saturation, i);
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "video_out_xcbshm: b %d c %d s %d [%s]\n",
      this->brightness, this->contrast, this->saturation, cm_names[i]);
  }
}

static void xshm_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  uint8_t *src0;

  /* delayed setup */
  if (!vo_img->proc_called) {
    xshm_frame_proc_setup (vo_img);
    vo_img->proc_called = 1;
  }


  src0 = src[0] + frame->offs0;
  if ((src0 < frame->crop_start) || (src0 >= frame->crop_stop))
    return;

  lprintf ("copy... (format %d)\n", frame->format);

  if (vo_img->format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
      src0, src[1] + frame->offs1, src[2] + frame->offs1);
  else
    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
                                  src0);

  if (src0 >= frame->crop_flush) {
    if (vo_img->format == XINE_IMGFMT_YV12) {
      frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
        src0 + 16 * vo_img->pitches[0],
        src[1] + frame->offs1 + 8 * vo_img->pitches[1],
        src[2] + frame->offs1 + 8 * vo_img->pitches[2]);
    } else {
      frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
        src0 + 16 * vo_img->pitches[0]);
    }
  }

  lprintf ("copy...done\n");
}

static void xshm_frame_dispose (vo_frame_t *vo_img) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this  = (xshm_driver_t *) vo_img->driver;

  if (frame->image) {
    pthread_mutex_lock(&this->main_mutex);
    dispose_ximage(this, frame);
    pthread_mutex_unlock(&this->main_mutex);
  }

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);
  free (frame);
}


static vo_frame_t *xshm_alloc_frame (vo_driver_t *this_gen) {
  xshm_frame_t  *frame;
  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  frame = (xshm_frame_t *) calloc(1, sizeof(xshm_frame_t));
  if (!frame)
    return NULL;

  memcpy (&frame->sc, &this->sc, sizeof(vo_scale_t));

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */

  frame->vo_frame.proc_slice = xshm_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = xshm_frame_field;
  frame->vo_frame.dispose    = xshm_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  /*
   * colorspace converter for this frame
   */

  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

  return (vo_frame_t *) frame;
}

static void xshm_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      double ratio, int format, int flags) {
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  int             j, y_pitch, uv_pitch;

  flags &= VO_BOTH_FIELDS;

  /* (re)allocate yuv buffers */
  if ((width != frame->sc.delivered_width)
      || (height != frame->sc.delivered_height)
      || (format != frame->format)) {

    frame->sc.delivered_width   = width;
    frame->sc.delivered_height  = height;
    frame->format               = format;

    av_freep(&frame->vo_frame.base[0]);
    av_freep(&frame->vo_frame.base[1]);
    av_freep(&frame->vo_frame.base[2]);

    /* bottom black pad for certain crop_top > crop_bottom cases */
    if (format == XINE_IMGFMT_YV12) {
      y_pitch = (width + 7) & ~7;
      frame->vo_frame.pitches[0] = y_pitch;
      frame->vo_frame.base[0] = av_malloc (y_pitch * (height + 16));
      uv_pitch = ((width + 15) & ~15) >> 1;
      frame->vo_frame.pitches[1] = uv_pitch;
      frame->vo_frame.pitches[2] = uv_pitch;
      frame->vo_frame.base[1] = av_malloc (uv_pitch * ((height + 17) / 2));
      frame->vo_frame.base[2] = av_malloc (uv_pitch * ((height + 17) / 2));
      if (!frame->vo_frame.base[0] || !frame->vo_frame.base[1] || !frame->vo_frame.base[2]) {
        av_freep (&frame->vo_frame.base[0]);
        av_freep (&frame->vo_frame.base[1]);
        av_freep (&frame->vo_frame.base[2]);
        frame->sc.delivered_width = 0;
        frame->vo_frame.width = 0;
      } else {
        memset (frame->vo_frame.base[0], 0, y_pitch * (height + 16));
        memset (frame->vo_frame.base[1], 128, uv_pitch * (height + 16) / 2);
        memset (frame->vo_frame.base[2], 128, uv_pitch * (height + 16) / 2);
      }
    } else {
      y_pitch = ((width + 3) & ~3) << 1;
      frame->vo_frame.pitches[0] = y_pitch;
      frame->vo_frame.base[0] = av_malloc (y_pitch * (height + 16));
      if (frame->vo_frame.base[0]) {
        const union {uint8_t bytes[4]; uint32_t word;} black = {{0, 128, 0, 128}};
        uint32_t *q = (uint32_t *)frame->vo_frame.base[0];
        for (j = y_pitch * (height + 16) / 4; j > 0; j--)
          *q++ = black.word;
      } else {
        frame->sc.delivered_width = 0;
        frame->vo_frame.width = 0;
      }
    }

    /* defer the rest to xshm_frame_proc_setup () */
    frame->state &= ~(FS_DONE | FS_LATE);
  }
  if (!isnan (ratio) && (ratio < 1000.0) && (ratio > 0.001)
    && (ratio != frame->sc.delivered_ratio)) {
    frame->sc.delivered_ratio  = ratio;
    frame->state &= ~FS_DONE;
  }

  if (flags != frame->flags) {
    frame->flags = flags;
    frame->state &= ~FS_FLAGS;
  }
}


static void xshm_overlay_clut_yuv2rgb(xshm_driver_t  *this, vo_overlay_t *overlay,
				      xshm_frame_t *frame) {
  int i;
  uint32_t *rgb;

  if (!overlay->rgb_clut) {
    rgb = overlay->color;
    for (i = sizeof (overlay->color) / sizeof (overlay->color[0]); i > 0; i--) {
      clut_t *yuv = (clut_t *)rgb;
      *rgb++ = frame->yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, yuv->y, yuv->cb, yuv->cr);
    }
    overlay->rgb_clut++;
  }

  if (!overlay->hili_rgb_clut) {
    rgb = overlay->hili_color;
    for (i = sizeof (overlay->color) / sizeof (overlay->color[0]); i > 0; i--) {
      clut_t *yuv = (clut_t *)rgb;
      *rgb++ = frame->yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, yuv->y, yuv->cb, yuv->cr);
    }
    overlay->hili_rgb_clut++;
  }
}

static void xshm_overlay_begin (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, int changed) {
  xshm_driver_t  *this  = (xshm_driver_t *) this_gen;

  this->ovl_changed += changed;

  if( this->ovl_changed && this->xoverlay ) {
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_clear(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
  }

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void xshm_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  xshm_driver_t  *this  = (xshm_driver_t *) this_gen;

  if( this->ovl_changed && this->xoverlay ) {
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_expose(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
  }

  this->ovl_changed = 0;
}

static void xshm_overlay_blend (vo_driver_t *this_gen,
				vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  xshm_driver_t  *this  = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  int width = frame->sc.delivered_width - frame->sc.crop_left - frame->sc.crop_right;
  int height = frame->sc.delivered_height - frame->sc.crop_top - frame->sc.crop_bottom;

  /* Alpha Blend here */
  if (overlay->rle) {
    if( overlay->unscaled ) {
      if( this->ovl_changed && this->xoverlay ) {
        pthread_mutex_lock(&this->main_mutex);
        xcbosd_blend(this->xoverlay, overlay);
        pthread_mutex_unlock(&this->main_mutex);
      }
    } else {
      if (!overlay->rgb_clut || !overlay->hili_rgb_clut)
        xshm_overlay_clut_yuv2rgb (this, overlay, frame);

      switch (this->bpp) {
        case 16:
         _x_blend_rgb16(frame->image, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        case 24:
         _x_blend_rgb24(frame->image, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        case 32:
         _x_blend_rgb32(frame->image, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        default:
	  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		  "xine-lib:video_out_xcbshm:xshm_overlay_blend: Cannot blend bpp:%i\n", this->bpp);
	/* it should never get here, unless a user tries to play in bpp:8 */
	break;
      }
    }
  }
}

static void clean_output_area (xshm_driver_t *this, xshm_frame_t *frame) {
  int i;
  xcb_rectangle_t rects[4];
  int rects_count = 0;

  memcpy( this->sc.border, frame->sc.border, sizeof(this->sc.border) );

  pthread_mutex_lock(&this->main_mutex);

  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h )
      rects[rects_count].x = this->sc.border[i].x;
      rects[rects_count].y = this->sc.border[i].y;
      rects[rects_count].width = this->sc.border[i].w;
      rects[rects_count].height = this->sc.border[i].h;
      rects_count++;
  }

  if (rects_count > 0)
    xcb_poly_fill_rectangle(this->connection, this->window, this->gc, rects_count, rects);

  if (this->xoverlay) {
    xcbosd_resize(this->xoverlay, this->sc.gui_width, this->sc.gui_height);
    this->ovl_changed = 1;
  }

  pthread_mutex_unlock(&this->main_mutex);
}

static int xshm_redraw_needed (vo_driver_t *this_gen) {
  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  int             ret = 0;

  if( this->cur_frame ) {
    this->sc.delivered_height   = this->cur_frame->sc.delivered_height;
    this->sc.delivered_width    = this->cur_frame->sc.delivered_width;
    this->sc.video_pixel_aspect = this->cur_frame->sc.video_pixel_aspect;

    this->sc.crop_left          = this->cur_frame->sc.crop_left;
    this->sc.crop_right         = this->cur_frame->sc.crop_right;
    this->sc.crop_top           = this->cur_frame->sc.crop_top;
    this->sc.crop_bottom        = this->cur_frame->sc.crop_bottom;

    if( _x_vo_scale_redraw_needed( &this->sc ) ) {

      clean_output_area (this, this->cur_frame);
      ret = 1;
    }
  }
  else
    ret = 1;

  return ret;
}

static void xshm_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  xshm_driver_t  *this  = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;

  lprintf ("display frame...\n");
  lprintf ("about to draw frame (%d) %d x %d...\n",
	   frame->vo_frame.id,
	   frame->sc.output_width, frame->sc.output_height);

  /*
   * tell gui that we are about to display a frame,
   * ask for offset
   */

  this->sc.delivered_height   = frame->sc.delivered_height;
  this->sc.delivered_width    = frame->sc.delivered_width;
  this->sc.video_pixel_aspect = frame->sc.video_pixel_aspect;

    this->sc.crop_left          = frame->sc.crop_left;
    this->sc.crop_right         = frame->sc.crop_right;
    this->sc.crop_top           = frame->sc.crop_top;
    this->sc.crop_bottom        = frame->sc.crop_bottom;

  if( _x_vo_scale_redraw_needed( &this->sc ) ) {

    clean_output_area (this, frame);
  }

  if (this->cur_frame) {

    if ( (this->cur_frame->sc.output_width != frame->sc.output_width)
         || (this->cur_frame->sc.output_height != frame->sc.output_height)
         || (this->cur_frame->sc.output_xoffset != frame->sc.output_xoffset)
         || (this->cur_frame->sc.output_yoffset != frame->sc.output_yoffset) )
      clean_output_area (this, frame);

    this->cur_frame->vo_frame.free (&this->cur_frame->vo_frame);
  }

  this->cur_frame = frame;

  pthread_mutex_lock(&this->main_mutex);
  lprintf ("display locked...\n");

  if (frame->shmseg) {

    lprintf ("put image (shm)\n");
    xcb_shm_put_image(this->connection, this->window, this->gc, this->cur_frame->sc.output_width,
                      this->cur_frame->sc.output_height, 0, 0, this->cur_frame->sc.output_width,
                      this->cur_frame->sc.output_height, this->cur_frame->sc.output_xoffset,
                      this->cur_frame->sc.output_yoffset, this->depth, XCB_IMAGE_FORMAT_Z_PIXMAP,
                      0, this->cur_frame->shmseg, 0);

  } else {

    lprintf ("put image (plain/remote)\n");
    xcb_put_image(this->connection, XCB_IMAGE_FORMAT_Z_PIXMAP, this->window, this->gc,
                  frame->sc.output_width, frame->sc.output_height, frame->sc.output_xoffset, frame->sc.output_yoffset,
                  0, this->depth, frame->bytes_per_line * frame->sc.output_height, frame->image);

  }
  xcb_flush(this->connection);
  pthread_mutex_unlock(&this->main_mutex);

  lprintf ("display frame done\n");

  /* just in case somebody changes crop this late - take over for next time */
  /* adjust cropping to what yuv2rgb can handle */
  if (frame_gen->format == XINE_IMGFMT_YV12) {
    frame_gen->crop_left &= ~7;
    frame_gen->crop_top &= ~1;
  } else {
    frame_gen->crop_left &= ~3;
  }
  /* check for crop changes */
  if ((frame_gen->crop_left != frame->sc.crop_left)
    || (frame_gen->crop_top != frame->sc.crop_top)
    || (frame_gen->crop_right != frame->sc.crop_right)
    || (frame_gen->crop_bottom != frame->sc.crop_bottom)) {
    frame->sc.crop_left = frame_gen->crop_left;
    frame->sc.crop_top = frame_gen->crop_top;
    frame->sc.crop_right = frame_gen->crop_right;
    frame->sc.crop_bottom = frame_gen->crop_bottom;
    frame->state &= ~FS_DONE;
    frame->state |= FS_LATE;
  }
}

static int xshm_get_property (vo_driver_t *this_gen, int property) {
  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    return this->sc.user_ratio;
  case VO_PROP_MAX_NUM_FRAMES:
    return 15;
  case VO_PROP_BRIGHTNESS:
    return this->brightness;
  case VO_PROP_CONTRAST:
    return this->contrast;
  case VO_PROP_SATURATION:
    return this->saturation;
  case VO_PROP_WINDOW_WIDTH:
    return this->sc.gui_width;
  case VO_PROP_WINDOW_HEIGHT:
    return this->sc.gui_height;
  case VO_PROP_OUTPUT_WIDTH:
    return this->cur_frame->sc.output_width;
  case VO_PROP_OUTPUT_HEIGHT:
    return this->cur_frame->sc.output_height;
  case VO_PROP_OUTPUT_XOFFSET:
    return this->cur_frame->sc.output_xoffset;
  case VO_PROP_OUTPUT_YOFFSET:
    return this->cur_frame->sc.output_yoffset;
  default:
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    LOG_MODULE ": tried to get unsupported property %d\n", property);
  }

  return 0;
}

static int xshm_set_property (vo_driver_t *this_gen,
			      int property, int value) {
  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    if (value>=XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;
    this->sc.user_ratio = value;
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    LOG_MODULE ": aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name_table[value]);
    break;

  case VO_PROP_BRIGHTNESS:
    this->brightness = value;
    this->cm_active = 0;
    this->sc.force_redraw = 1;
    break;

  case VO_PROP_CONTRAST:
    this->contrast = value;
    this->cm_active = 0;
    this->sc.force_redraw = 1;
    break;

  case VO_PROP_SATURATION:
    this->saturation = value;
    this->cm_active = 0;
    this->sc.force_redraw = 1;
    break;

  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     LOG_MODULE ": tried to set unsupported property %d\n", property);
  }

  return value;
}

static void xshm_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  /* xshm_driver_t *this = (xshm_driver_t *) this_gen;  */

  if (property == VO_PROP_BRIGHTNESS) {
    *min = -128;
    *max = +127;
  } else if (property == VO_PROP_CONTRAST) {
    *min = 0;
    *max = 255;
  } else if (property == VO_PROP_SATURATION) {
    *min = 0;
    *max = 255;
  } else {
    *min = 0;
    *max = 0;
  }
}

static int xshm_gui_data_exchange (vo_driver_t *this_gen,
				   int data_type, void *data) {
  xshm_driver_t   *this = (xshm_driver_t *) this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT:

    lprintf ("expose event\n");

    if (this->cur_frame) {
      xcb_expose_event_t *xev = (xcb_expose_event_t *) data;

      if (xev && xev->count == 0) {
	int i;
	xcb_rectangle_t rects[4];
	int rects_count = 0;

	pthread_mutex_lock(&this->main_mutex);
	if (this->cur_frame->shmseg)
	  xcb_shm_put_image(this->connection, this->window, this->gc, this->cur_frame->sc.output_width,
			    this->cur_frame->sc.output_height, 0, 0, this->cur_frame->sc.output_width,
			    this->cur_frame->sc.output_height, this->cur_frame->sc.output_xoffset,
			    this->cur_frame->sc.output_yoffset, this->depth, XCB_IMAGE_FORMAT_Z_PIXMAP,
			    0, this->cur_frame->shmseg, 0);
	else
	  xcb_put_image(this->connection, XCB_IMAGE_FORMAT_Z_PIXMAP, this->window, this->gc,
			this->cur_frame->sc.output_width, this->cur_frame->sc.output_height,
			this->cur_frame->sc.output_xoffset, this->cur_frame->sc.output_yoffset,
			0, this->depth, this->cur_frame->bytes_per_line * this->cur_frame->sc.output_height,
			this->cur_frame->image);

	for( i = 0; i < 4; i++ ) {
	  if( this->sc.border[i].w && this->sc.border[i].h )
	    rects[rects_count].x = this->sc.border[i].x;
	    rects[rects_count].y = this->sc.border[i].y;
	    rects[rects_count].width = this->sc.border[i].w;
	    rects[rects_count].height = this->sc.border[i].h;
	    rects_count++;
	}

	if (rects_count > 0)
	  xcb_poly_fill_rectangle(this->connection, this->window, this->gc, rects_count, rects);

        if(this->xoverlay)
          xcbosd_expose(this->xoverlay);

	xcb_flush(this->connection);
	pthread_mutex_unlock(&this->main_mutex);
      }
    }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->window = (xcb_window_t) (long) data;

    pthread_mutex_lock(&this->main_mutex);
    xcb_free_gc(this->connection, this->gc);
    this->gc = xcb_generate_id(this->connection);
    xcb_create_gc(this->connection, this->gc, this->window, XCB_GC_FOREGROUND, &this->screen->black_pixel);
    if(this->xoverlay)
      xcbosd_drawable_changed(this->xoverlay, this->window);
    this->ovl_changed = 1;
    pthread_mutex_unlock(&this->main_mutex);
  break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:

    if (this->cur_frame) {
      x11_rectangle_t *rect = data;
      int              x1, y1, x2, y2;

      _x_vo_scale_translate_gui2video(&this->cur_frame->sc,
			       rect->x, rect->y,
			       &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->cur_frame->sc,
			       rect->x + rect->w, rect->y + rect->h,
			       &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
  break;

  default:
    return -1;
  }

  return 0;
}

static void xshm_dispose (vo_driver_t *this_gen) {
  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  if (this->cur_frame)
    this->cur_frame->vo_frame.dispose (&this->cur_frame->vo_frame);

  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  cm_close (this);

  pthread_mutex_lock(&this->main_mutex);
  xcb_free_gc(this->connection, this->gc);
  pthread_mutex_unlock(&this->main_mutex);

  if( this->xoverlay ) {
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_destroy(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
  }

  pthread_mutex_destroy(&this->main_mutex);

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

static int ImlibPaletteLUTGet(xshm_driver_t *this) {
  static const xcb_atom_t CARDINAL = 6;

  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply;

  xcb_get_property_cookie_t prop_cookie;
  xcb_get_property_reply_t *prop_reply;

  atom_cookie = xcb_intern_atom(this->connection, 0, sizeof("_IMLIB_COLORMAP"), "_IMLIB_COLORMAP");
  atom_reply = xcb_intern_atom_reply(this->connection, atom_cookie, NULL);

  if (atom_reply == NULL)
    return 0;

  prop_cookie = xcb_get_property(this->connection, 0, this->window, atom_reply->atom, CARDINAL, 0, 0x7fffffff);
  prop_reply = xcb_get_property_reply(this->connection, prop_cookie, NULL);

  free(atom_reply);

  if (prop_reply == NULL)
    return 0;

  if (prop_reply->format == 8) {
    unsigned int i;
    unsigned long j;
    int num_ret = xcb_get_property_value_length(prop_reply);
    char *retval = xcb_get_property_value(prop_reply);

    j = 1 + retval[0]*4;
    this->yuv2rgb_cmap = xine_xcalloc(sizeof(uint8_t), 32 * 32 * 32);
    for (i = 0; i < 32 * 32 * 32 && j < num_ret; i++)
      this->yuv2rgb_cmap[i] = retval[1+4*retval[j++]+3];

    free(prop_reply);
    return 1;
  }

  free(prop_reply);
  return 0;
}

/* TODO replace this with a string table. */
static const char *visual_class_name(xcb_visualtype_t *visual) {

  switch (visual->_class) {
  case XCB_VISUAL_CLASS_STATIC_GRAY:
    return "StaticGray";
  case XCB_VISUAL_CLASS_GRAY_SCALE:
    return "GrayScale";
  case XCB_VISUAL_CLASS_STATIC_COLOR:
    return "StaticColor";
  case XCB_VISUAL_CLASS_PSEUDO_COLOR:
    return "PseudoColor";
  case XCB_VISUAL_CLASS_TRUE_COLOR:
    return "TrueColor";
  case XCB_VISUAL_CLASS_DIRECT_COLOR:
    return "DirectColor";
  default:
    return "unknown visual class";
  }
}

static vo_driver_t *xshm_open_plugin(video_driver_class_t *class_gen, const void *visual_gen) {
  xshm_class_t         *class   = (xshm_class_t *) class_gen;
  config_values_t      *config  = class->config;
  xcb_visual_t         *visual  = (xcb_visual_t *) visual_gen;
  xshm_driver_t        *this;
  xcb_visualtype_t     *visualtype;
  int                   mode;
  int			swapped;
  int			cpu_byte_order;
  int			image_byte_order;

  xcb_get_window_attributes_cookie_t window_attrs_cookie;
  xcb_get_window_attributes_reply_t *window_attrs_reply;

  xcb_get_geometry_cookie_t geometry_cookie;
  xcb_get_geometry_reply_t *geometry_reply;

  const xcb_query_extension_reply_t *query_extension_reply;

  this = (xshm_driver_t *) calloc(1, sizeof(xshm_driver_t));

  if (!this)
    return NULL;

  pthread_mutex_init(&this->main_mutex, NULL);

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->connection          = visual->connection;
  this->screen              = visual->screen;
  this->window              = visual->window;

  _x_vo_scale_init( &this->sc, 0, 0, config );
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;

  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  this->cur_frame           = NULL;
  this->gc                  = xcb_generate_id(this->connection);
  xcb_create_gc(this->connection, this->gc, this->window, XCB_GC_FOREGROUND, &this->screen->black_pixel);
  this->xoverlay            = NULL;
  this->ovl_changed         = 0;

  this->xine                = class->xine;

  this->vo_driver.get_capabilities     = xshm_get_capabilities;
  this->vo_driver.alloc_frame          = xshm_alloc_frame;
  this->vo_driver.update_frame_format  = xshm_update_frame_format;
  this->vo_driver.overlay_begin        = xshm_overlay_begin;
  this->vo_driver.overlay_blend        = xshm_overlay_blend;
  this->vo_driver.overlay_end          = xshm_overlay_end;
  this->vo_driver.display_frame        = xshm_display_frame;
  this->vo_driver.get_property         = xshm_get_property;
  this->vo_driver.set_property         = xshm_set_property;
  this->vo_driver.get_property_min_max = xshm_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xshm_gui_data_exchange;
  this->vo_driver.dispose              = xshm_dispose;
  this->vo_driver.redraw_needed        = xshm_redraw_needed;

  /*
   *
   * depth in X11 terminology land is the number of bits used to
   * actually represent the colour.
   *
   * bpp in X11 land means how many bits in the frame buffer per
   * pixel.
   *
   * ex. 15 bit color is 15 bit depth and 16 bpp. Also 24 bit
   *     color is 24 bit depth, but can be 24 bpp or 32 bpp.
   */

  window_attrs_cookie = xcb_get_window_attributes(this->connection, this->window);
  geometry_cookie = xcb_get_geometry(this->connection, this->window);
  xcb_prefetch_extension_data(this->connection, &xcb_shm_id);

  window_attrs_reply = xcb_get_window_attributes_reply(this->connection, window_attrs_cookie, NULL);

  visualtype = NULL;
  {
    xcb_depth_t *depth = xcb_screen_allowed_depths_iterator(this->screen).data;
    xcb_visualtype_t *vis = xcb_depth_visuals(depth);
    xcb_visualtype_t *vis_end = vis + xcb_depth_visuals_length(depth);

    for (; vis != vis_end; ++vis)
      if (window_attrs_reply->visual == vis->visual_id) {
        visualtype = vis;
        break;
      }
  }

  free(window_attrs_reply);

  geometry_reply = xcb_get_geometry_reply(this->connection, geometry_cookie, NULL);

  this->depth = geometry_reply->depth;

  free(geometry_reply);

  if (this->depth>16)
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("\n\nWARNING: current display depth is %d. For better performance\n"
	      "a depth of 16 bpp is recommended!\n\n"), this->depth);

  /*
   * check for X shared memory support
   */

  query_extension_reply = xcb_get_extension_data(this->connection, &xcb_shm_id);
  if (query_extension_reply && query_extension_reply->present) {
    this->use_shm = 1;
  }
  else {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("%s: MIT shared memory extension not present on display.\n"), LOG_MODULE);
    this->use_shm = 0;
  }

  {
    const xcb_setup_t *setup = xcb_get_setup(this->connection);
    xcb_format_t *fmt = xcb_setup_pixmap_formats(setup);
    xcb_format_t *fmt_end = fmt + xcb_setup_pixmap_formats_length(setup);

    for (; fmt != fmt_end; ++fmt)
      if(fmt->depth == this->depth) {
        this->bpp = fmt->bits_per_pixel;
        this->scanline_pad = fmt->scanline_pad;
        break;
    }

    if (fmt == fmt_end) {
      if (this->depth <= 4)
        this->bpp = 4;
      else if (this->depth <= 8)
        this->bpp = 8;
      else if (this->depth <= 16)
        this->bpp = 16;
      else
        this->bpp = 32;
      this->scanline_pad = setup->bitmap_format_scanline_pad;
    }

    image_byte_order = setup->image_byte_order;
  }

  /*
   * Is the same byte order in use on the X11 client and server?
   */
  cpu_byte_order = htonl(1) == 1 ? XCB_IMAGE_ORDER_MSB_FIRST : XCB_IMAGE_ORDER_LSB_FIRST;
  swapped = cpu_byte_order != image_byte_order;

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": video mode depth is %d (%d bpp), %s, %sswapped,\n"
	  LOG_MODULE ": red: %08x, green: %08x, blue: %08x\n",
	  this->depth, this->bpp,
	  visual_class_name(visualtype),
	  swapped ? "" : "not ",
	  visualtype->red_mask, visualtype->green_mask, visualtype->blue_mask);

  mode = 0;

  switch (visualtype->_class) {
  case XCB_VISUAL_CLASS_TRUE_COLOR:
    switch (this->depth) {
    case 24:
    case 32:
      if (this->bpp == 32) {
	if (visualtype->red_mask == 0x00ff0000)
	  mode = MODE_32_RGB;
	else
	  mode = MODE_32_BGR;
      } else {
	if (visualtype->red_mask == 0x00ff0000)
	  mode = MODE_24_RGB;
	else
	  mode = MODE_24_BGR;
      }
      break;
    case 16:
	if (visualtype->red_mask == 0xf800)
	mode = MODE_16_RGB;
      else
	mode = MODE_16_BGR;
      break;
    case 15:
	if (visualtype->red_mask == 0x7C00)
	mode = MODE_15_RGB;
      else
	mode = MODE_15_BGR;
      break;
    case 8:
	if (visualtype->red_mask == 0xE0)
	mode = MODE_8_RGB; /* Solaris x86: RGB332 */
      else
	mode = MODE_8_BGR; /* XFree86: BGR233 */
      break;
    }
    break;

  case XCB_VISUAL_CLASS_STATIC_GRAY:
    if (this->depth == 8)
      mode = MODE_8_GRAY;
    break;

  case XCB_VISUAL_CLASS_PSEUDO_COLOR:
  case XCB_VISUAL_CLASS_GRAY_SCALE:
    if (this->depth <= 8 && ImlibPaletteLUTGet(this))
      mode = MODE_PALETTE;
    break;
  }

  if (!mode) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     _("%s: your video mode was not recognized, sorry :-(\n"), LOG_MODULE);
    return NULL;
  }

  cm_init (this);

  this->brightness = 0;
  this->contrast   = 128;
  this->saturation = 128;

  this->yuv2rgb_factory = yuv2rgb_factory_init (mode, swapped, this->yuv2rgb_cmap);

  this->xoverlay = xcbosd_create(this->xine, this->connection, this->screen,
                                 this->window, XCBOSD_SHAPED);

  return &this->vo_driver;
}

/*
 * class functions
 */
static void *xshm_init_class (xine_t *xine, void *visual_gen) {
  xshm_class_t	       *this = (xshm_class_t *) calloc(1, sizeof(xshm_class_t));

  this->driver_class.open_plugin     = xshm_open_plugin;
  this->driver_class.identifier      = "XShm";
  this->driver_class.description     = N_("xine video output plugin using the MIT X shared memory extension");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}


static const vo_info_t vo_info_xshm = {
  6,                      /* priority    */
  XINE_VISUAL_TYPE_XCB    /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xshm", XINE_VERSION_CODE, &vo_info_xshm, xshm_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
