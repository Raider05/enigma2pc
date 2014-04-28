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
 * video_out_xshm.c, X11 shared memory extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 *
 * fullrange/HD color and crop support added by Torsten Jager <t.jager@gmx.de>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <errno.h>

#include <X11/extensions/XShm.h>
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

#define LOG_MODULE "video_out_xshm"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include "yuv2rgb.h"
#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "x11osd.h"
#include "xine_mmx.h"

#define LOCK_DISPLAY(this) {if(this->lock_display) this->lock_display(this->user_data); \
                            else XLockDisplay(this->display);}
#define UNLOCK_DISPLAY(this) {if(this->unlock_display) this->unlock_display(this->user_data); \
                            else XUnlockDisplay(this->display);}
typedef struct {
  vo_frame_t         vo_frame;

  /* frame properties as delivered by the decoder: */
  /* obs: for width/height use vo_scale_t struct */
  int                format;
  int                flags;

  vo_scale_t         sc;

  XImage            *image;
  XShmSegmentInfo    shminfo;

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

  /* X11 / XShm related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
  Visual	    *visual;
  GC                 gc;
  int                depth, bpp, bytes_per_pixel, image_byte_order;
  int                use_shm;
  XColor             black;

  int                brightness;
  int                contrast;
  int                saturation;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  /* color matrix switching */
  int                cm_active, cm_state;

  vo_scale_t         sc;

  xshm_frame_t      *cur_frame;
  x11osd            *xoverlay;
  int                ovl_changed;
  int                video_window_width, video_window_height, video_window_x, video_window_y;

  int (*x11_old_error_handler)  (Display *, XErrorEvent *);

  xine_t            *xine;

  alphablend_t       alphablend_extra_data;

  void             (*lock_display) (void *);

  void             (*unlock_display) (void *);

  void              *user_data;

} xshm_driver_t;

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  xine_t              *xine;
} xshm_class_t;


/* import common color matrix stuff */
#define CM_DRIVER_T xshm_driver_t
#include "color_matrix.c"

static int gX11Fail;

/*
 * first, some utility functions
 */

/* called xlocked */
static int HandleXError (Display *display, XErrorEvent *xevent) {
  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);
  printf (LOG_MODULE ": received X error event: %s\n", str);
  gX11Fail = 1;

  return 0;
}

/* called xlocked */
static void x11_InstallXErrorHandler (xshm_driver_t *this) {
  this->x11_old_error_handler = XSetErrorHandler (HandleXError);
  XSync(this->display, False);
}

static void x11_DeInstallXErrorHandler (xshm_driver_t *this) {
  XSetErrorHandler (this->x11_old_error_handler);
  XSync(this->display, False);
  this->x11_old_error_handler = NULL;
}

/*
 * allocate an XImage, try XShm first but fall back to
 * plain X11 if XShm should fail
 */
/* called xlocked */
static XImage *create_ximage (xshm_driver_t *this, XShmSegmentInfo *shminfo,
			      int width, int height) {
  XImage *myimage = NULL;

  if (this->use_shm) {

    /*
     * try shm
     */

    gX11Fail = 0;
    x11_InstallXErrorHandler (this);

    myimage = XShmCreateImage(this->display,
			      this->visual,
			      this->depth,
			      ZPixmap, NULL,
			      shminfo,
			      width,
			      height);

    if (myimage == NULL )  {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error when allocating image\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    this->bpp = myimage->bits_per_pixel;
    this->bytes_per_pixel = this->bpp / 8;
    this->image_byte_order = myimage->byte_order;

    shminfo->shmid=shmget(IPC_PRIVATE,
			  myimage->bytes_per_line * myimage->height,
			  IPC_CREAT | 0777);

    if (shminfo->shmid < 0 ) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: %s: allocating image\n"), LOG_MODULE, strerror(errno));
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);

    if (shminfo->shmaddr == ((char *) -1)) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error (address error) when allocating image \n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->readOnly = False;
    myimage->data = shminfo->shmaddr;

    XShmAttach(this->display, shminfo);

    XSync(this->display, False);

    if (gX11Fail) {
      shmdt (shminfo->shmaddr);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: x11 error during shared memory XImage creation\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      this->use_shm = 0;
      goto finishShmTesting;
    }

    /*
     * Now that the Xserver has learned about and attached to the
     * shared memory segment,  delete it.  It's actually deleted by
     * the kernel when all users of that segment have detached from
     * it.  Gives an automatic shared memory cleanup in case we crash.
     */
    shmctl (shminfo->shmid, IPC_RMID, 0);
    shminfo->shmid = -1;

  finishShmTesting:
    x11_DeInstallXErrorHandler(this);

  }

  /*
   * fall back to plain X11 if necessary
   */

  if (!this->use_shm) {

    myimage = XCreateImage (this->display,
			    this->visual,
			    this->depth,
			    ZPixmap, 0,
			    NULL,
			    width,
			    height,
			    8, 0);

    this->bpp = myimage->bits_per_pixel;
    this->bytes_per_pixel = this->bpp / 8;
    this->image_byte_order = myimage->byte_order;

    myimage->data = calloc (width * height, this->bytes_per_pixel);
  }

  return myimage;

}

/* called xlocked */
static void dispose_ximage (xshm_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XImage *myimage) {

  if (this->use_shm) {

    XShmDetach (this->display, shminfo);
    XDestroyImage (myimage);
    shmdt (shminfo->shmaddr);
    if (shminfo->shmid >= 0) {
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
    }

  }
  else
    XDestroyImage (myimage);
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
  /* xshm_driver_t *this = (xshm_driver_t *) vo_img->driver; */

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data + frame->image->bytes_per_line ;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
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
      LOCK_DISPLAY(this);
      if (frame->image)
        dispose_ximage (this, &frame->shminfo, frame->image);
      frame->image = create_ximage (this, &frame->shminfo,
        frame->sc.output_width, frame->sc.output_height);
      UNLOCK_DISPLAY(this);
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
          frame->image->bytes_per_line * 2);
      break;
      case VO_BOTH_FIELDS:
        frame->yuv2rgb->configure (frame->yuv2rgb, width, height,
          frame->vo_frame.pitches[0], frame->vo_frame.pitches[1],
          frame->sc.output_width, frame->sc.output_height,
          frame->image->bytes_per_line);
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
      "video_out_xshm: b %d c %d s %d [%s]\n",
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

  if (frame->format == XINE_IMGFMT_YV12)
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
    LOCK_DISPLAY(this);
    dispose_ximage (this, &frame->shminfo, frame->image);
    UNLOCK_DISPLAY(this);
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

    frame->sc.delivered_width    = width;
    frame->sc.delivered_height   = height;
    frame->format                = format;

    av_freep (&frame->vo_frame.base[0]);
    av_freep (&frame->vo_frame.base[1]);
    av_freep (&frame->vo_frame.base[2]);

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
    LOCK_DISPLAY(this);
    x11osd_clear(this->xoverlay);
    UNLOCK_DISPLAY(this);
  }

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void xshm_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  xshm_driver_t  *this  = (xshm_driver_t *) this_gen;

  if( this->ovl_changed && this->xoverlay ) {
    LOCK_DISPLAY(this);
    x11osd_expose(this->xoverlay);
    UNLOCK_DISPLAY(this);
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
        LOCK_DISPLAY(this);
        x11osd_blend(this->xoverlay, overlay);
        UNLOCK_DISPLAY(this);
      }
    } else {
      if (!overlay->rgb_clut || !overlay->hili_rgb_clut)
        xshm_overlay_clut_yuv2rgb (this, overlay, frame);

      switch (this->bpp) {
        case 16:
         _x_blend_rgb16 ((uint8_t *)frame->image->data, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        case 24:
         _x_blend_rgb24 ((uint8_t *)frame->image->data, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        case 32:
         _x_blend_rgb32 ((uint8_t *)frame->image->data, overlay,
		      frame->sc.output_width, frame->sc.output_height,
		      width, height,
                      &this->alphablend_extra_data);
         break;
        default:
	  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		  "xine-lib:video_out_xshm:xshm_overlay_blend: Cannot blend bpp:%i\n", this->bpp);
	/* it should never get here, unless a user tries to play in bpp:8 */
	break;
      }
    }
  }
  else if (overlay && overlay->argb_layer && overlay->argb_layer->buffer && this->ovl_changed)
  {
    pthread_mutex_lock (&overlay->argb_layer->mutex); 
    LOCK_DISPLAY(this);
    x11osd_blend(this->xoverlay, overlay);
    UNLOCK_DISPLAY(this);
    pthread_mutex_unlock (&overlay->argb_layer->mutex);
    this->video_window_width  = overlay->video_window_width;
    this->video_window_height = overlay->video_window_height;
    this->video_window_x      = overlay->video_window_x;
    this->video_window_y      = overlay->video_window_y;
  }
}

static void clean_output_area (xshm_driver_t *this, xshm_frame_t *frame) {
  int i;

  memcpy( this->sc.border, frame->sc.border, sizeof(this->sc.border) );

  LOCK_DISPLAY(this);
  XSetForeground (this->display, this->gc, this->black.pixel);

  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h )
      XFillRectangle(this->display, this->drawable, this->gc,
                     this->sc.border[i].x + this->video_window_x, this->sc.border[i].y + this->video_window_y,
                     this->sc.border[i].w, this->sc.border[i].h);
  }
  if (this->xoverlay) {
    x11osd_resize (this->xoverlay, this->sc.gui_width, this->sc.gui_height);
    this->ovl_changed = 1;
  }

  UNLOCK_DISPLAY(this);
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

  LOCK_DISPLAY(this);
  lprintf ("display locked...\n");

  if (this->use_shm) {

    lprintf ("put image (shm)\n");
    XShmPutImage(this->display,
                 this->drawable, this->gc, frame->image,
                 0, 0, frame->sc.output_xoffset, frame->sc.output_yoffset,
                 frame->sc.output_width, frame->sc.output_height, True);

  } else {

    lprintf ("put image (plain/remote)\n");
    XPutImage(this->display,
              this->drawable, this->gc, frame->image,
              0, 0, frame->sc.output_xoffset, frame->sc.output_yoffset,
              frame->sc.output_width, frame->sc.output_height);

  }
  XSync(this->display, False);
  UNLOCK_DISPLAY(this);

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
      XExposeEvent * xev = (XExposeEvent *) data;

      if (xev && xev->count == 0) {
	int i;

	LOCK_DISPLAY(this);
	if (this->use_shm) {
	  XShmPutImage(this->display,
		       this->drawable, this->gc, this->cur_frame->image,
		       0, 0, this->cur_frame->sc.output_xoffset, this->cur_frame->sc.output_yoffset,
		       this->cur_frame->sc.output_width, this->cur_frame->sc.output_height,
		       False);
	}
	else {
	  XPutImage(this->display,
		    this->drawable, this->gc, this->cur_frame->image,
		    0, 0, this->cur_frame->sc.output_xoffset, this->cur_frame->sc.output_yoffset,
		    this->cur_frame->sc.output_width, this->cur_frame->sc.output_height);
	}

	XSetForeground (this->display, this->gc, this->black.pixel);

	for( i = 0; i < 4; i++ ) {
	  if( this->sc.border[i].w && this->sc.border[i].h )
	    XFillRectangle(this->display, this->drawable, this->gc,
			   this->sc.border[i].x, this->sc.border[i].y,
			   this->sc.border[i].w, this->sc.border[i].h);
	}

        if(this->xoverlay)
          x11osd_expose(this->xoverlay);

	XSync(this->display, False);
	UNLOCK_DISPLAY(this);
      }
    }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;

    LOCK_DISPLAY(this);
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC (this->display, this->drawable, 0, NULL);
    if(this->xoverlay)
      x11osd_drawable_changed(this->xoverlay, this->drawable);
    this->ovl_changed = 1;
    UNLOCK_DISPLAY(this);
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

  LOCK_DISPLAY(this);
  XFreeGC(this->display, this->gc);
  UNLOCK_DISPLAY(this);

  if( this->xoverlay ) {
    LOCK_DISPLAY(this);
    x11osd_destroy (this->xoverlay);
    UNLOCK_DISPLAY(this);
  }

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

/* called xlocked */
static int ImlibPaletteLUTGet(xshm_driver_t *this) {
  unsigned char      *retval;
  Atom                type_ret;
  unsigned long       bytes_after, num_ret;
  int                 format_ret;
  long                length;
  Atom                to_get;

  retval = NULL;
  length = 0x7fffffff;
  to_get = XInternAtom(this->display, "_IMLIB_COLORMAP", False);
  XGetWindowProperty(this->display, RootWindow(this->display, this->screen),
		     to_get, 0, length, False,
		     XA_CARDINAL, &type_ret, &format_ret, &num_ret,
		     &bytes_after, &retval);
  if (retval != 0 && num_ret > 0 && format_ret > 0) {
    if (format_ret == 8) {
      unsigned int i;
      unsigned long j;

      j = 1 + retval[0]*4;
      this->yuv2rgb_cmap = malloc(sizeof(uint8_t) * 32 * 32 * 32);
      for (i = 0; i < 32 * 32 * 32 && j < num_ret; i++)
	this->yuv2rgb_cmap[i] = retval[1+4*retval[j++]+3];

      XFree(retval);
      return 1;
    } else
      XFree(retval);
  }
  return 0;
}


static const char *visual_class_name(Visual *visual) {

  switch (visual->class) {
  case StaticGray:
    return "StaticGray";
  case GrayScale:
    return "GrayScale";
  case StaticColor:
    return "StaticColor";
  case PseudoColor:
    return "PseudoColor";
  case TrueColor:
    return "TrueColor";
  case DirectColor:
    return "DirectColor";
  default:
    return "unknown visual class";
  }
}

/* expects XINE_VISUAL_TYPE_X11_2 with configurable locking */
static vo_driver_t *xshm_open_plugin_2 (video_driver_class_t *class_gen, const void *visual_gen) {
  xshm_class_t         *class   = (xshm_class_t *) class_gen;
  config_values_t      *config  = class->config;
  x11_visual_t         *visual  = (x11_visual_t *) visual_gen;
  xshm_driver_t        *this;
  XWindowAttributes     attribs;
  XImage               *myimage;
  XShmSegmentInfo       myshminfo;
  int                   mode;
  int			swapped;
  int			cpu_byte_order;
  XColor                dummy;

  this = (xshm_driver_t *) calloc(1, sizeof(xshm_driver_t));

  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->display		    = visual->display;
  this->screen		    = visual->screen;

  /* configurable X11 locking */
  this->lock_display        = visual->lock_display;
  this->unlock_display      = visual->unlock_display;
  this->user_data           = visual->user_data;

  _x_vo_scale_init( &this->sc, 0, 0, config );
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;

  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  this->drawable	    = visual->d;
  this->cur_frame           = NULL;
  LOCK_DISPLAY(this);
  this->gc		    = XCreateGC (this->display, this->drawable, 0, NULL);
  UNLOCK_DISPLAY(this);
  this->xoverlay                = NULL;
  this->ovl_changed             = 0;
  this->video_window_width      = 0;
  this->video_window_height     = 0;
  this->video_window_x          = 0;
  this->video_window_y          = 0;

  this->x11_old_error_handler = NULL;
  this->xine                  = class->xine;

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

  LOCK_DISPLAY(this);
  XAllocNamedColor (this->display,
		    DefaultColormap (this->display, this->screen),
		    "black", &this->black, &dummy);

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

  XGetWindowAttributes(this->display, this->drawable, &attribs);
  UNLOCK_DISPLAY(this);
  this->visual = attribs.visual;
  this->depth  = attribs.depth;

  if (this->depth>16)
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("\n\nWARNING: current display depth is %d. For better performance\n"
	      "a depth of 16 bpp is recommended!\n\n"), this->depth);

  /*
   * check for X shared memory support
   */

  LOCK_DISPLAY(this);
  if (XShmQueryExtension(this->display)) {
    this->use_shm = 1;
  }
  else {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("%s: MIT shared memory extension not present on display.\n"), LOG_MODULE);
    this->use_shm = 0;
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works
   * and what bpp it uses
   */

  myimage = create_ximage (this, &myshminfo, 100, 100);
  dispose_ximage (this, &myshminfo, myimage);
  UNLOCK_DISPLAY(this);

  /*
   * Is the same byte order in use on the X11 client and server?
   */
  cpu_byte_order = htonl(1) == 1 ? MSBFirst : LSBFirst;
  swapped = cpu_byte_order != this->image_byte_order;

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": video mode depth is %d (%d bpp), %s, %sswapped,\n"
	  LOG_MODULE ": red: %08lx, green: %08lx, blue: %08lx\n",
	  this->depth, this->bpp,
	  visual_class_name(this->visual),
	  swapped ? "" : "not ",
	  this->visual->red_mask, this->visual->green_mask, this->visual->blue_mask);

  mode = 0;

  switch (this->visual->class) {
  case TrueColor:
    switch (this->depth) {
    case 24:
    case 32:
      if (this->bpp == 32) {
	if (this->visual->red_mask == 0x00ff0000)
	  mode = MODE_32_RGB;
	else
	  mode = MODE_32_BGR;
      } else {
	if (this->visual->red_mask == 0x00ff0000)
	  mode = MODE_24_RGB;
	else
	  mode = MODE_24_BGR;
      }
      break;
    case 16:
      if (this->visual->red_mask == 0xf800)
	mode = MODE_16_RGB;
      else
	mode = MODE_16_BGR;
      break;
    case 15:
      if (this->visual->red_mask == 0x7C00)
	mode = MODE_15_RGB;
      else
	mode = MODE_15_BGR;
      break;
    case 8:
      if (this->visual->red_mask == 0xE0)
	mode = MODE_8_RGB; /* Solaris x86: RGB332 */
      else
	mode = MODE_8_BGR; /* XFree86: BGR233 */
      break;
    }
    break;

  case StaticGray:
    if (this->depth == 8)
      mode = MODE_8_GRAY;
    break;

  case PseudoColor:
  case GrayScale:
    LOCK_DISPLAY(this);
    if (this->depth <= 8 && ImlibPaletteLUTGet(this))
      mode = MODE_PALETTE;
    UNLOCK_DISPLAY(this);
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

  LOCK_DISPLAY(this);
  this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                  this->drawable, X11OSD_SHAPED);
  UNLOCK_DISPLAY(this);

  return &this->vo_driver;
}

static vo_driver_t *xshm_open_plugin_old (video_driver_class_t *class_gen, const void *visual_gen) {
  x11_visual_t         *old_visual  = (x11_visual_t *) visual_gen;
  x11_visual_t         visual;

  /* provides compatibility for XINE_VISUAL_TYPE_X11 */
  visual.display         = old_visual->display;
  visual.screen          = old_visual->screen;
  visual.d               = old_visual->d;
  visual.user_data       = old_visual->user_data;
  visual.dest_size_cb    = old_visual->dest_size_cb;
  visual.frame_output_cb = old_visual->frame_output_cb;
  visual.lock_display    = NULL;
  visual.unlock_display  = NULL;

  return xshm_open_plugin_2(class_gen, (void *)&visual);
}

/*
 * class functions
 */
static void *xshm_init_class (xine_t *xine, void *visual_gen) {
  xshm_class_t	       *this = (xshm_class_t *) calloc(1, sizeof(xshm_class_t));

  this->driver_class.open_plugin     = xshm_open_plugin_old;
  this->driver_class.identifier      = "XShm";
  this->driver_class.description     = N_("xine video output plugin using the MIT X shared memory extension");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static void *xshm_init_class_2 (xine_t *xine, void *visual_gen) {
  xshm_class_t	       *this;
  this = xshm_init_class (xine, visual_gen);
  this->driver_class.open_plugin     = xshm_open_plugin_2;
  return this;
}


static const vo_info_t vo_info_xshm = {
  6,                      /* priority    */
  XINE_VISUAL_TYPE_X11    /* visual type */
};

/* visual type with configurable X11 locking */
static const vo_info_t vo_info_xshm_2 = {
  6,                      /* priority    */
  XINE_VISUAL_TYPE_X11_2  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xshm", XINE_VERSION_CODE, &vo_info_xshm, xshm_init_class },
  { PLUGIN_VIDEO_OUT, 22, "xshm", XINE_VERSION_CODE, &vo_info_xshm_2, xshm_init_class_2 },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
