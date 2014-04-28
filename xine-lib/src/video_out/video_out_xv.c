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
 * video_out_xv.c, X11 video extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * Xv image support by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 *
 * overlay support by James Courtier-Dutton <James@superbug.demon.co.uk> - July 2001
 * X11 unscaled overlay support by Miguel Freitas - Nov 2003
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <sys/types.h>
#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <time.h>

#define LOG_MODULE "video_out_xv"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
/* #include "overlay.h" */
#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "x11osd.h"
#include "xv_common.h"

#define LOCK_DISPLAY(this) {if(this->lock_display) this->lock_display(this->user_data); \
                            else XLockDisplay(this->display);}
#define UNLOCK_DISPLAY(this) {if(this->unlock_display) this->unlock_display(this->user_data); \
                            else XUnlockDisplay(this->display);}
typedef struct xv_driver_s xv_driver_t;

typedef struct {
  int                value;
  int                min;
  int                max;
  Atom               atom;

  int                defer;

  cfg_entry_t       *entry;

  xv_driver_t       *this;
} xv_property_t;

typedef struct {
  char              *name;
  int                value;
} xv_portattribute_t;

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format;
  double             ratio;

  XvImage           *image;
  XShmSegmentInfo    shminfo;

  int                req_width, req_height;
} xv_frame_t;


struct xv_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  /* X11 / Xv related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
  unsigned int       xv_format_yv12;
  unsigned int       xv_format_yuy2;
  XVisualInfo        vinfo;
  GC                 gc;
  XvPortID           xv_port;
  XColor             black;

  int                use_shm;
  int                use_pitch_alignment;
  xv_property_t      props[VO_NUM_PROPERTIES];
  uint32_t           capabilities;

  int                ovl_changed;
  xv_frame_t        *recent_frames[VO_NUM_RECENT_FRAMES];
  xv_frame_t        *cur_frame;
  x11osd            *xoverlay;

  /* all scaling information goes here */
  vo_scale_t         sc;

  int                use_colorkey;
  uint32_t           colorkey;

  int                sync_is_vsync;

  /* hold initial port attributes values to restore on exit */
  xine_list_t       *port_attributes;

  int              (*x11_old_error_handler)  (Display *, XErrorEvent *);

  xine_t            *xine;

  alphablend_t       alphablend_extra_data;

  void             (*lock_display) (void *);

  void             (*unlock_display) (void *);

  void              *user_data;

  /* color matrix switching */
  int                cm_active, cm_state;
  Atom               cm_atom, cm_atom2;
  int                fullrange_mode;
};

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  xine_t              *xine;
} xv_class_t;

/* import common color matrix stuff */
#define CM_DRIVER_T xv_driver_t
#include "color_matrix.c"

static int gX11Fail;

VIDEO_DEVICE_XV_DECL_BICUBIC_TYPES;
VIDEO_DEVICE_XV_DECL_PREFER_TYPES;
VIDEO_DEVICE_XV_DECL_SYNC_ATOMS;

static uint32_t xv_get_capabilities (vo_driver_t *this_gen) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  return this->capabilities;
}

static void xv_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for Xv */
}

static void xv_frame_dispose (vo_frame_t *vo_img) {
  xv_frame_t  *frame = (xv_frame_t *) vo_img ;
  xv_driver_t *this  = (xv_driver_t *) vo_img->driver;

  if (frame->image) {

    if (frame->shminfo.shmaddr) {
      LOCK_DISPLAY(this);
      XShmDetach (this->display, &frame->shminfo);
      XFree (frame->image);
      UNLOCK_DISPLAY(this);

      shmdt (frame->shminfo.shmaddr);
      shmctl (frame->shminfo.shmid, IPC_RMID, NULL);
    }
    else {
      LOCK_DISPLAY(this);
      free (frame->image->data);
      XFree (frame->image);
      UNLOCK_DISPLAY(this);
    }
  }

  free (frame);
}

static vo_frame_t *xv_alloc_frame (vo_driver_t *this_gen) {
  /* xv_driver_t  *this = (xv_driver_t *) this_gen; */
  xv_frame_t   *frame ;

  frame = (xv_frame_t *) calloc(1, sizeof(xv_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */
  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = xv_frame_field;
  frame->vo_frame.dispose    = xv_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  return (vo_frame_t *) frame;
}

static int HandleXError (Display *display, XErrorEvent *xevent) {
  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);
  printf ("received X error event: %s\n", str);
  gX11Fail = 1;

  return 0;
}

/* called xlocked */
static void x11_InstallXErrorHandler (xv_driver_t *this) {
  this->x11_old_error_handler = XSetErrorHandler (HandleXError);
  XSync(this->display, False);
}

/* called xlocked */
static void x11_DeInstallXErrorHandler (xv_driver_t *this) {
  XSetErrorHandler (this->x11_old_error_handler);
  XSync(this->display, False);
  this->x11_old_error_handler = NULL;
}

/* called xlocked */
static XvImage *create_ximage (xv_driver_t *this, XShmSegmentInfo *shminfo,
			       int width, int height, int format) {
  unsigned int  xv_format;
  XvImage      *image = NULL;

  if (this->use_pitch_alignment) {
    lprintf ("use_pitch_alignment old width=%d",width);
    width = (width + 7) & ~0x7;
    lprintf ("use_pitch_alignment new width=%d",width);
  }

  switch (format) {
  case XINE_IMGFMT_YV12:
    xv_format = this->xv_format_yv12;
    break;
  case XINE_IMGFMT_YUY2:
    xv_format = this->xv_format_yuy2;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
    _x_abort();
  }

  if (this->use_shm) {

    /*
     * try shm
     */

    gX11Fail = 0;
    x11_InstallXErrorHandler (this);

    lprintf( "XvShmCreateImage format=0x%x, width=%d, height=%d\n", xv_format, width, height );
    image = XvShmCreateImage(this->display, this->xv_port, xv_format, 0,
			     width, height, shminfo);

    if (image == NULL )  {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: XvShmCreateImage failed\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    {
    int q;

    lprintf( "XvImage id %d\n", image->id );
    lprintf( "XvImage width %d\n", image->width );
    lprintf( "XvImage height %d\n", image->height );
    lprintf( "XvImage data_size %d\n", image->data_size );
    lprintf( "XvImage num_planes %d\n", image->num_planes );

        for( q=0; q < image->num_planes; q++)
        {
            lprintf( "XvImage pitches[%d] %d\n",  q, image->pitches[q] );
            lprintf( "XvImage offsets[%d] %d\n",  q, image->offsets[q] );
        }
    }

    shminfo->shmid = shmget(IPC_PRIVATE, image->data_size, IPC_CREAT | 0777);

    if (image->data_size==0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: XvShmCreateImage returned a zero size\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmid < 0 ) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error in shmget: %s\n"), LOG_MODULE, strerror(errno));
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);

    if (shminfo->shmaddr == NULL) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      LOG_MODULE ": shared memory error (address error NULL)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmaddr == ((char *) -1)) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      LOG_MODULE ": shared memory error (address error)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->readOnly = False;
    image->data       = shminfo->shmaddr;

    XShmAttach(this->display, shminfo);

    XSync(this->display, False);
    shmctl(shminfo->shmid, IPC_RMID, 0);

    if (gX11Fail) {
      shmdt (shminfo->shmaddr);
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: x11 error during shared memory XImage creation\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm  = 0;
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
   * fall back to plain Xv if necessary
   */

  if (!this->use_shm) {
    char *data;

    switch (format) {
    case XINE_IMGFMT_YV12:
      data = malloc (width * height * 3/2);
      break;
    case XINE_IMGFMT_YUY2:
      data = malloc (width * height * 2);
      break;
    default:
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
      _x_abort();
    }

    if (data) {
      image = XvCreateImage (this->display, this->xv_port, xv_format, data, width, height);
      if (!image)
        free (data);
    } else
      image = NULL;

    shminfo->shmaddr = 0;
  }
  return image;
}

/* called xlocked */
static void dispose_ximage (xv_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XvImage *myimage) {

  if (shminfo->shmaddr) {

    XShmDetach (this->display, shminfo);
    XFree (myimage);
    shmdt (shminfo->shmaddr);
    if (shminfo->shmid >= 0) {
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
    }

  }
  else {
    free (myimage->data);

    XFree (myimage);
  }
}

static void xv_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags) {
  xv_driver_t  *this  = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  if (this->use_pitch_alignment) {
    width = (width + 7) & ~0x7;
  }

  if ((frame->req_width != width)
      || (frame->req_height != height)
      || (frame->format != format)) {

    /* printf (LOG_MODULE ": updating frame to %d x %d (ratio=%d, format=%08x)\n",width,height,ratio_code,format); */

    LOCK_DISPLAY(this);

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {
      dispose_ximage (this, &frame->shminfo, frame->image);
      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo, width, height, format);
    if (!frame->image) {
      UNLOCK_DISPLAY (this);
      frame->vo_frame.base[0] = NULL;
      frame->vo_frame.base[1] = NULL;
      frame->vo_frame.base[2] = NULL;
      frame->req_width = 0;
      frame->vo_frame.width = 0;
      return;
    }

    if(format == XINE_IMGFMT_YUY2) {
      frame->vo_frame.pitches[0] = frame->image->pitches[0];
      frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
      {
        const union {uint8_t bytes[4]; uint32_t word;} black = {{0, 128, 0, 128}};
        uint32_t *q = (uint32_t *)frame->vo_frame.base[0];
        int i;
        for (i = frame->vo_frame.pitches[0] * frame->image->height / 4; i > 0; i--)
          *q++ = black.word;
      }
    }
    else {
      frame->vo_frame.pitches[0] = frame->image->pitches[0];
      frame->vo_frame.pitches[1] = frame->image->pitches[2];
      frame->vo_frame.pitches[2] = frame->image->pitches[1];
      frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
      frame->vo_frame.base[1] = frame->image->data + frame->image->offsets[2];
      frame->vo_frame.base[2] = frame->image->data + frame->image->offsets[1];
      memset (frame->vo_frame.base[0], 0, frame->vo_frame.pitches[0] * frame->image->height);
      memset (frame->vo_frame.base[1], 128, frame->vo_frame.pitches[1] * frame->image->height / 2);
      memset (frame->vo_frame.base[2], 128, frame->vo_frame.pitches[2] * frame->image->height / 2);
    }

    /* allocated frame size may not match requested size */
    frame->req_width  = width;
    frame->req_height = height;
    frame->width  = frame->image->width;
    frame->height = frame->image->height;
    frame->format = format;

    UNLOCK_DISPLAY(this);
  }

  if (frame->vo_frame.width > frame->width)
    frame->vo_frame.width  = frame->width;
  if (frame->vo_frame.height > frame->height)
    frame->vo_frame.height = frame->height;

  frame->ratio = ratio;
}

static void xv_clean_output_area (xv_driver_t *this) {
  int i;

  LOCK_DISPLAY(this);

  XSetForeground (this->display, this->gc, this->black.pixel);

  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h ) {
      XFillRectangle(this->display, this->drawable, this->gc,
		     this->sc.border[i].x, this->sc.border[i].y,
		     this->sc.border[i].w, this->sc.border[i].h);
    }
  }

  if (this->use_colorkey) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height);
  }

  if (this->xoverlay) {
    x11osd_resize (this->xoverlay, this->sc.gui_width, this->sc.gui_height);
    this->ovl_changed = 1;
  }

  UNLOCK_DISPLAY(this);
}

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

static void xv_compute_ideal_size (xv_driver_t *this) {
  _x_vo_scale_compute_ideal_size( &this->sc );
}


/*
 * make ideal width/height "fit" into the gui
 */

static void xv_compute_output_size (xv_driver_t *this) {

  _x_vo_scale_compute_output_size( &this->sc );
}

static void xv_overlay_begin (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, int changed) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;

  this->ovl_changed += changed;

  if( this->ovl_changed && this->xoverlay ) {
    LOCK_DISPLAY(this);
    x11osd_clear(this->xoverlay);
    UNLOCK_DISPLAY(this);
  }

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void xv_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;

  if( this->ovl_changed && this->xoverlay ) {
    LOCK_DISPLAY(this);
    x11osd_expose(this->xoverlay);
    UNLOCK_DISPLAY(this);
  }

  this->ovl_changed = 0;
}

static void xv_overlay_blend (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  if (overlay->rle) {
    if( overlay->unscaled ) {
      if( this->ovl_changed && this->xoverlay ) {
        LOCK_DISPLAY(this);
        x11osd_blend(this->xoverlay, overlay);
        UNLOCK_DISPLAY(this);
      }
    } else {
      if (frame->format == XINE_IMGFMT_YV12)
        _x_blend_yuv(frame->vo_frame.base, overlay,
		  frame->width, frame->height, frame->vo_frame.pitches,
                  &this->alphablend_extra_data);
      else
        _x_blend_yuy2(frame->vo_frame.base[0], overlay,
		   frame->width, frame->height, frame->vo_frame.pitches[0],
                   &this->alphablend_extra_data);
    }
  }
}

static void xv_add_recent_frame (xv_driver_t *this, xv_frame_t *frame) {
  int i;

  i = VO_NUM_RECENT_FRAMES-1;
  if( this->recent_frames[i] )
    this->recent_frames[i]->vo_frame.free
       (&this->recent_frames[i]->vo_frame);

  for( ; i ; i-- )
    this->recent_frames[i] = this->recent_frames[i-1];

  this->recent_frames[0] = frame;
}

/* currently not used - we could have a method to call this from video loop */
#if 0
static void xv_flush_recent_frames (xv_driver_t *this) {
  int i;

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.free
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }
}
#endif

static int xv_redraw_needed (vo_driver_t *this_gen) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;
  int           ret  = !this->cm_active;

  if( this->cur_frame ) {

    this->sc.delivered_height = this->cur_frame->height;
    this->sc.delivered_width  = this->cur_frame->width;
    this->sc.delivered_ratio  = this->cur_frame->ratio;

    this->sc.crop_left        = this->cur_frame->vo_frame.crop_left;
    this->sc.crop_right       = this->cur_frame->vo_frame.crop_right;
    this->sc.crop_top         = this->cur_frame->vo_frame.crop_top;
    this->sc.crop_bottom      = this->cur_frame->vo_frame.crop_bottom;

    xv_compute_ideal_size(this);

    if( _x_vo_scale_redraw_needed( &this->sc ) ) {

      xv_compute_output_size (this);

      xv_clean_output_area (this);

      ret = 1;
    }
  }
  else
    ret = 1;

  return ret;
}

/* Used in xv_display_frame to determine how long XvShmPutImage takes
   - if slower than 60fps, print a message
*/
static double timeOfDay()
{
    struct timeval t;
    gettimeofday( &t, NULL );
    return ((double)t.tv_sec) + (((double)t.tv_usec)/1000000.0);
}

static void xv_new_color (xv_driver_t *this, int cm) {
  int brig = this->props[VO_PROP_BRIGHTNESS].value;
  int cont = this->props[VO_PROP_CONTRAST].value;
  int satu = this->props[VO_PROP_SATURATION].value;
  int cm2, fr = 0, a, b;
  Atom atom;

  if (cm & 1) {
    /* fullrange emulation. Do not report this via get_property (). */
    if (this->fullrange_mode == 1) {
      /* modification routine 1 for TV set style bcs controls 0% - 200% */
      satu -= this->props[VO_PROP_SATURATION].min;
      satu  = (satu * (112 * 255) + (127 * 219 / 2)) / (127 * 219);
      satu += this->props[VO_PROP_SATURATION].min;
      if (satu > this->props[VO_PROP_SATURATION].max)
        satu = this->props[VO_PROP_SATURATION].max;
      cont -= this->props[VO_PROP_CONTRAST].min;
      cont  = (cont * 219 + 127) / 255;
      a     = cont * (this->props[VO_PROP_BRIGHTNESS].max - this->props[VO_PROP_BRIGHTNESS].min);
      cont += this->props[VO_PROP_CONTRAST].min;
      b     = 256 * (this->props[VO_PROP_CONTRAST].max - this->props[VO_PROP_CONTRAST].min);
      brig += (16 * a + b / 2) / b;
      if (brig > this->props[VO_PROP_BRIGHTNESS].max)
        brig = this->props[VO_PROP_BRIGHTNESS].max;
      fr = 1;
    }
    /* maybe add more routines for non-standard controls later */
  }
  LOCK_DISPLAY (this);
  atom = this->props[VO_PROP_BRIGHTNESS].atom;
  if (atom != None)
    XvSetPortAttribute (this->display, this->xv_port, atom, brig);
  atom = this->props[VO_PROP_CONTRAST].atom;
  if (atom != None)
    XvSetPortAttribute (this->display, this->xv_port, atom, cont);
  atom = this->props[VO_PROP_SATURATION].atom;
  if (atom != None)
    XvSetPortAttribute (this->display, this->xv_port, atom, satu);
  UNLOCK_DISPLAY(this);

  if (this->cm_atom != None) {
    /* 0 = 601 (SD), 1 = 709 (HD) */
    /* so far only binary nvidia drivers support this. why not nuveau? */
    cm2 = (0xc00c >> cm) & 1;
    LOCK_DISPLAY(this);
    XvSetPortAttribute (this->display, this->xv_port, this->cm_atom, cm2);
    UNLOCK_DISPLAY(this);
    cm2 = cm2 ? 2 : 10;
  } else if (this->cm_atom2 != None) {
    /* radeonhd: 0 = size based auto, 1 = 601 (SD), 2 = 709 (HD) */
    cm2 = ((0xc00c >> cm) & 1) + 1;
    LOCK_DISPLAY(this);
    XvSetPortAttribute (this->display, this->xv_port, this->cm_atom2, cm2);
    UNLOCK_DISPLAY(this);
    cm2 = cm2 == 2 ? 2 : 10;
  } else {
    cm2 = 10;
  }

  cm2 |= fr;
  xprintf (this->xine, XINE_VERBOSITY_LOG, "video_out_xv: %s b %d  c %d  s %d  [%s]\n",
    fr ? "modified " : "", brig, cont, satu, cm_names[cm2]);

  this->cm_active = cm;
}

static void xv_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  xv_driver_t  *this  = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;
  int cm;
  /*
  printf (LOG_MODULE ": xv_display_frame...\n");
  */

  cm = cm_from_frame (frame_gen);
  if (cm != this->cm_active)
    xv_new_color (this, cm);

  /*
   * queue frames (deinterlacing)
   * free old frames
   */

  xv_add_recent_frame (this, frame); /* deinterlacing */

  this->cur_frame = frame;

  /*
   * let's see if this frame is different in size / aspect
   * ratio from the previous one
   */
  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio)
       || (frame->vo_frame.crop_left != this->sc.crop_left)
       || (frame->vo_frame.crop_right != this->sc.crop_right)
       || (frame->vo_frame.crop_top != this->sc.crop_top)
       || (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    lprintf("frame format changed\n");
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  xv_redraw_needed (this_gen);
  {
  double start_time;
  double end_time;
  double elapse_time;
  int factor;

  LOCK_DISPLAY(this);
  start_time = timeOfDay();
  if (this->use_shm) {
    XvShmPutImage(this->display, this->xv_port,
                  this->drawable, this->gc, this->cur_frame->image,
                  this->sc.displayed_xoffset, this->sc.displayed_yoffset,
                  this->sc.displayed_width, this->sc.displayed_height,
                  this->sc.output_xoffset, this->sc.output_yoffset,
                  this->sc.output_width, this->sc.output_height, True);

  } else {
    XvPutImage(this->display, this->xv_port,
               this->drawable, this->gc, this->cur_frame->image,
               this->sc.displayed_xoffset, this->sc.displayed_yoffset,
               this->sc.displayed_width, this->sc.displayed_height,
               this->sc.output_xoffset, this->sc.output_yoffset,
               this->sc.output_width, this->sc.output_height);
  }

  XSync(this->display, False);
  end_time = timeOfDay();

  UNLOCK_DISPLAY(this);

  elapse_time = end_time - start_time;
  factor = (int)(elapse_time/(1.0/60.0));

  if( factor > 1 )
  {
    lprintf( "%s PutImage %dX interval (%fs)\n",
             LOG_MODULE, factor, elapse_time );
  }
  }

  /*
  printf (LOG_MODULE ": xv_display_frame... done\n");
  */
}

static int xv_get_property (vo_driver_t *this_gen, int property) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return (0);

  switch (property) {
    case VO_PROP_WINDOW_WIDTH:
      this->props[property].value = this->sc.gui_width;
      break;
    case VO_PROP_WINDOW_HEIGHT:
      this->props[property].value = this->sc.gui_height;
      break;
    case VO_PROP_OUTPUT_WIDTH:
      this->props[property].value = this->sc.output_width;
      break;
    case VO_PROP_OUTPUT_HEIGHT:
      this->props[property].value = this->sc.output_height;
      break;
    case VO_PROP_OUTPUT_XOFFSET:
      this->props[property].value = this->sc.output_xoffset;
      break;
    case VO_PROP_OUTPUT_YOFFSET:
      this->props[property].value = this->sc.output_yoffset;
      break;
  }

  lprintf(LOG_MODULE ": property #%d = %d\n", property, this->props[property].value);

  return this->props[property].value;
}

static void xv_property_callback (void *property_gen, xine_cfg_entry_t *entry) {
  xv_property_t *property = (xv_property_t *) property_gen;
  xv_driver_t   *this = property->this;

  LOCK_DISPLAY(this);
  XvSetPortAttribute (this->display, this->xv_port,
		      property->atom,
		      entry->num_value);
  UNLOCK_DISPLAY(this);
}

static int xv_set_property (vo_driver_t *this_gen,
			    int property, int value) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  printf("xv_set_property: property=%d, value=%d\n", property, value );

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if (this->props[property].defer == 1) {
    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;
    this->props[property].value = value;
    this->cm_active = 0;
    return value;
  }

  if (this->props[property].atom != None) {

    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;

    LOCK_DISPLAY(this);
    XvSetPortAttribute (this->display, this->xv_port,
			this->props[property].atom, value);
    XvGetPortAttribute (this->display, this->xv_port,
			this->props[property].atom,
			&this->props[property].value);
    UNLOCK_DISPLAY(this);

    if (this->props[property].entry)
      this->props[property].entry->num_value = this->props[property].value;

    return this->props[property].value;
  }
  else {
    switch (property) {

    case VO_PROP_ASPECT_RATIO:
      if (value>=XINE_VO_ASPECT_NUM_RATIOS)
	value = XINE_VO_ASPECT_AUTO;

      this->props[property].value = value;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ": VO_PROP_ASPECT_RATIO(%d)\n", this->props[property].value);
      this->sc.user_ratio = value;

      xv_compute_ideal_size (this);

      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      break;

    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
	xprintf(this->xine, XINE_VERBOSITY_LOG,
		LOG_MODULE ": VO_PROP_ZOOM_X = %d\n", this->props[property].value);

	this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;

	xv_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;

    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
	xprintf(this->xine, XINE_VERBOSITY_LOG,
		LOG_MODULE ": VO_PROP_ZOOM_Y = %d\n", this->props[property].value);

	this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;

	xv_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    }
  }

  return value;
}

static void xv_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) {
    *min = *max = 0;
    return;
  }

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int xv_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {
  xv_driver_t     *this = (xv_driver_t *) this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT: {
    /* XExposeEvent * xev = (XExposeEvent *) data; */

    if (this->cur_frame) {
      int i;

      LOCK_DISPLAY(this);

      if (this->use_shm) {
	XvShmPutImage(this->display, this->xv_port,
		      this->drawable, this->gc, this->cur_frame->image,
		      this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		      this->sc.displayed_width, this->sc.displayed_height,
		      this->sc.output_xoffset, this->sc.output_yoffset,
		      this->sc.output_width, this->sc.output_height, True);
      } else {
	XvPutImage(this->display, this->xv_port,
		   this->drawable, this->gc, this->cur_frame->image,
		   this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		   this->sc.displayed_width, this->sc.displayed_height,
		   this->sc.output_xoffset, this->sc.output_yoffset,
		   this->sc.output_width, this->sc.output_height);
      }

      XSetForeground (this->display, this->gc, this->black.pixel);

      for( i = 0; i < 4; i++ ) {
	if( this->sc.border[i].w && this->sc.border[i].h ) {
	  XFillRectangle(this->display, this->drawable, this->gc,
			 this->sc.border[i].x, this->sc.border[i].y,
			 this->sc.border[i].w, this->sc.border[i].h);
	}
      }

      if(this->xoverlay)
	x11osd_expose(this->xoverlay);

      XSync(this->display, False);
      UNLOCK_DISPLAY(this);
    }
  }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    LOCK_DISPLAY(this);
    this->drawable = (Drawable) data;
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC (this->display, this->drawable, 0, NULL);
    if(this->xoverlay)
      x11osd_drawable_changed(this->xoverlay, this->drawable);
    this->ovl_changed = 1;
    UNLOCK_DISPLAY(this);
    this->sc.force_redraw = 1;
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
    return -1;
  }

  return 0;
}

static void xv_store_port_attribute(xv_driver_t *this, const char *name) {
  Atom                 atom;
  xv_portattribute_t  *attr;

  attr = (xv_portattribute_t *)malloc( sizeof(xv_portattribute_t) );
  attr->name = strdup(name);

  LOCK_DISPLAY(this);
  atom = XInternAtom (this->display, attr->name, False);
  XvGetPortAttribute (this->display, this->xv_port, atom, &attr->value);
  UNLOCK_DISPLAY(this);

  xine_list_push_back (this->port_attributes, attr);
}

static void xv_restore_port_attributes(xv_driver_t *this) {
  Atom                 atom;
  xine_list_iterator_t ite;

  while ((ite = xine_list_front(this->port_attributes)) != NULL) {
    xv_portattribute_t *attr = xine_list_get_value(this->port_attributes, ite);
    xine_list_remove (this->port_attributes, ite);

    LOCK_DISPLAY(this);
    atom = XInternAtom (this->display, attr->name, False);
    XvSetPortAttribute (this->display, this->xv_port, atom, attr->value);
    UNLOCK_DISPLAY(this);

    free( attr->name );
    free( attr );
  }

  LOCK_DISPLAY(this);
  XSync(this->display, False);
  UNLOCK_DISPLAY(this);

  xine_list_delete( this->port_attributes );
}

static void xv_dispose (vo_driver_t *this_gen) {
  xv_driver_t *this = (xv_driver_t *) this_gen;
  int          i;

  /* restore port attributes to their initial values */
  xv_restore_port_attributes(this);

  LOCK_DISPLAY(this);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": xv_exit: XvUngrabPort() failed.\n");
  }
  XFreeGC(this->display, this->gc);
  UNLOCK_DISPLAY(this);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.dispose
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  if( this->xoverlay ) {
    LOCK_DISPLAY(this);
    x11osd_destroy (this->xoverlay);
    UNLOCK_DISPLAY(this);
  }

  _x_alphablend_free(&this->alphablend_extra_data);

  cm_close (this);

  free (this);
}

/* called xlocked */
static int xv_check_yv12 (Display *display, XvPortID port) {
  XvImageFormatValues *formatValues;
  int                  formats;
  int                  i;

  formatValues = XvListImageFormats (display, port, &formats);

  for (i = 0; i < formats; i++)
    if ((formatValues[i].id == XINE_IMGFMT_YV12) &&
	(! (strcmp (formatValues[i].guid, "YV12")))) {
      XFree (formatValues);
      return 0;
    }

  XFree (formatValues);
  return 1;
}

/* called xlocked */
static void xv_check_capability (xv_driver_t *this,
				 int property, XvAttribute attr, int base_id,
				 const char *config_name,
				 const char *config_desc,
				 const char *config_help) {
  int          int_default;
  cfg_entry_t *entry;
  const char  *str_prop = attr.name;

  /*
   * some Xv drivers (Gatos ATI) report some ~0 as max values, this is confusing.
   */
  if (VO_PROP_COLORKEY && (attr.max_value == ~0))
    attr.max_value = 2147483615;

  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);

  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": port attribute %s (%d) value is %d\n", str_prop, property, int_default);

  /* disable autopaint colorkey by default */
  /* might be overridden using config entry */
  if(strcmp(str_prop, "XV_AUTOPAINT_COLORKEY") == 0)
    int_default = 0;

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr.min_value == 0) && (attr.max_value == 1)) {
      this->config->register_bool (this->config, config_name, int_default,
				   config_desc,
				   config_help, 20, xv_property_callback, &this->props[property]);

    } else {
      this->config->register_range (this->config, config_name, int_default,
				    this->props[property].min, this->props[property].max,
				    config_desc,
				    config_help, 20, xv_property_callback, &this->props[property]);
    }

    entry = this->config->lookup_entry (this->config, config_name);

    if((entry->num_value < this->props[property].min) ||
       (entry->num_value > this->props[property].max)) {

      this->config->update_num(this->config, config_name,
			       ((this->props[property].min + this->props[property].max) >> 1));

      entry = this->config->lookup_entry (this->config, config_name);
    }

    this->props[property].entry = entry;

    xv_set_property (&this->vo_driver, property, entry->num_value);

    if (strcmp(str_prop, "XV_COLORKEY") == 0) {
      this->use_colorkey |= 1;
      this->colorkey = entry->num_value;
    } else if(strcmp(str_prop, "XV_AUTOPAINT_COLORKEY") == 0) {
      if(entry->num_value==1)
        this->use_colorkey |= 2;	/* colorkey is autopainted */
    }
  } else
    this->props[property].value  = int_default;
}

static void xv_update_attr (void *this_gen, xine_cfg_entry_t *entry,
			    const char *atomstr, const char *debugstr)
{
  xv_driver_t *this = (xv_driver_t *) this_gen;
  Atom atom;

  LOCK_DISPLAY(this);
  atom = XInternAtom (this->display, atomstr, False);
  XvSetPortAttribute (this->display, this->xv_port, atom, entry->num_value);
  UNLOCK_DISPLAY(this);

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": %s = %d\n", debugstr, entry->num_value);
}

static void xv_update_XV_FILTER(void *this_gen, xine_cfg_entry_t *entry) {
  xv_update_attr (this_gen, entry, "XV_FILTER", "bilinear scaling mode");
}

static void xv_update_XV_DOUBLE_BUFFER(void *this_gen, xine_cfg_entry_t *entry) {
  xv_update_attr (this_gen, entry, "XV_DOUBLE_BUFFER", "double buffering mode");
}

static void xv_update_XV_SYNC_TO_VBLANK(void *this_gen, xine_cfg_entry_t *entry) {
  xv_update_attr (this_gen, entry,
		  sync_atoms[((xv_driver_t *)this_gen)->sync_is_vsync],
		  "sync to vblank");
}

static void xv_update_XV_BICUBIC(void *this_gen, xine_cfg_entry_t *entry)
{
  xv_update_attr (this_gen, entry, "XV_BICUBIC", "bicubic filtering mode");
}

static void xv_update_xv_pitch_alignment(void *this_gen, xine_cfg_entry_t *entry) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  this->use_pitch_alignment = entry->num_value;
}

static void xv_fullrange_cb_config (void *this_gen, xine_cfg_entry_t *entry) {
  xv_driver_t *this = (xv_driver_t *)this_gen;
  this->fullrange_mode = entry->num_value;
  if (this->fullrange_mode)
    this->capabilities |= VO_CAP_FULLRANGE;
  else
    this->capabilities &= ~VO_CAP_FULLRANGE;
  this->cm_active = 0;
}

static int xv_open_port (xv_driver_t *this, XvPortID port) {
  int ret;
  x11_InstallXErrorHandler (this);
  ret = ! xv_check_yv12(this->display, port)
    && XvGrabPort(this->display, port, 0) == Success;
  x11_DeInstallXErrorHandler (this);
  return ret;
}

static unsigned int
xv_find_adaptor_by_port (int port, unsigned int adaptors,
			 XvAdaptorInfo *adaptor_info)
{
  unsigned int an;
  for (an = 0; an < adaptors; an++)
    if (adaptor_info[an].type & XvImageMask)
      if (port >= adaptor_info[an].base_id &&
	  port < adaptor_info[an].base_id + adaptor_info[an].num_ports)
	return an;
  return 0; /* shouldn't happen */
}

static XvPortID xv_autodetect_port(xv_driver_t *this,
				   unsigned int adaptors,
				   XvAdaptorInfo *adaptor_info,
				   unsigned int *adaptor_num,
				   XvPortID base,
				   xv_prefertype prefer_type)
{
  unsigned int an, j;

  for (an = 0; an < adaptors; an++)
    if (adaptor_info[an].type & XvImageMask &&
        (prefer_type == xv_prefer_none ||
         strcasestr (adaptor_info[an].name, prefer_substrings[prefer_type])))
      for (j = 0; j < adaptor_info[an].num_ports; j++) {
	XvPortID port = adaptor_info[an].base_id + j;
	if (port >= base && xv_open_port(this, port)) {
	  *adaptor_num = an;
	  return port;
	}
      }

  return 0;
}

/* expects XINE_VISUAL_TYPE_X11_2 with configurable locking */
static vo_driver_t *open_plugin_2 (video_driver_class_t *class_gen, const void *visual_gen) {
  xv_class_t           *class = (xv_class_t *) class_gen;
  config_values_t      *config = class->config;
  xv_driver_t          *this;
  int                   i, formats;
  XvAttribute          *attr;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XColor                dummy;
  XvImage              *myimage;
  unsigned int          adaptors;
  unsigned int          ver,rel,req,ev,err;
  XShmSegmentInfo       myshminfo;
  XvPortID              xv_port;
  XvAdaptorInfo        *adaptor_info;
  unsigned int          adaptor_num;
  xv_prefertype		prefer_type;
  unsigned int          nencode = 0;
  XvEncodingInfo       *encodings = NULL;

  this = (xv_driver_t *) calloc(1, sizeof(xv_driver_t));
  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->display           = visual->display;
  this->screen            = visual->screen;
  this->config            = config;

  /* configurable X11 locking */
  this->lock_display      = visual->lock_display;
  this->unlock_display    = visual->unlock_display;
  this->user_data         = visual->user_data;

  /*
   * check for Xvideo support
   */

  LOCK_DISPLAY(this);
  if (Success != XvQueryExtension(this->display, &ver,&rel, &req, &ev,&err)) {
    xprintf (class->xine, XINE_VERBOSITY_LOG, _("%s: Xv extension not present.\n"), LOG_MODULE);
    UNLOCK_DISPLAY(this);
    return NULL;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if (Success != XvQueryAdaptors(this->display,DefaultRootWindow(this->display), &adaptors, &adaptor_info))  {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": XvQueryAdaptors failed.\n");
    UNLOCK_DISPLAY(this);
    return NULL;
  }

  xv_port = config->register_num (config, "video.device.xv_port", 0,
				  VIDEO_DEVICE_XV_PORT_HELP,
				  20, NULL, NULL);
  prefer_type = config->register_enum (config, "video.device.xv_preferred_method", 0,
				       (char **)prefer_labels, VIDEO_DEVICE_XV_PREFER_TYPE_HELP,
				       10, NULL, NULL);

  if (xv_port != 0) {
    if (! xv_open_port(this, xv_port)) {
      xprintf(class->xine, XINE_VERBOSITY_NONE,
	      _("%s: could not open Xv port %lu - autodetecting\n"),
	      LOG_MODULE, (unsigned long)xv_port);
      xv_port = xv_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, xv_port, prefer_type);
    } else
      adaptor_num = xv_find_adaptor_by_port (xv_port, adaptors, adaptor_info);
  }
  if (!xv_port)
    xv_port = xv_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, 0, prefer_type);
  if (!xv_port)
  {
    if (prefer_type)
      xprintf(class->xine, XINE_VERBOSITY_NONE,
	      _("%s: no available ports of type \"%s\", defaulting...\n"),
	      LOG_MODULE, prefer_labels[prefer_type]);
    xv_port = xv_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, 0, xv_prefer_none);
  }

  if (!xv_port) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: Xv extension is present but I couldn't find a usable yuv12 port.\n"
	      "\tLooks like your graphics hardware driver doesn't support Xv?!\n"),
	    LOG_MODULE);

    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    UNLOCK_DISPLAY(this);
    return NULL;
  }
  else
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: using Xv port %ld from adaptor %s for hardware "
	      "colour space conversion and scaling.\n"), LOG_MODULE, xv_port,
            adaptor_info[adaptor_num].name);

  UNLOCK_DISPLAY(this);

  this->xv_port           = xv_port;

  _x_vo_scale_init (&this->sc, 1, 0, config );
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  this->drawable                = visual->d;
  LOCK_DISPLAY(this);
  this->gc                      = XCreateGC (this->display, this->drawable, 0, NULL);
  UNLOCK_DISPLAY(this);
  this->capabilities            = VO_CAP_CROP | VO_CAP_ZOOM_X | VO_CAP_ZOOM_Y;
  this->use_shm                 = 1;
  this->use_colorkey            = 0;
  this->colorkey                = 0;
  this->xoverlay                = NULL;
  this->ovl_changed             = 0;
  this->x11_old_error_handler   = NULL;
  this->xine                    = class->xine;

  this->cm_atom                 = None;
  this->cm_atom2                = None;

  LOCK_DISPLAY(this);
  XAllocNamedColor (this->display,
		    DefaultColormap(this->display, this->screen),
		    "black", &this->black, &dummy);
  UNLOCK_DISPLAY(this);

  this->vo_driver.get_capabilities     = xv_get_capabilities;
  this->vo_driver.alloc_frame          = xv_alloc_frame;
  this->vo_driver.update_frame_format  = xv_update_frame_format;
  this->vo_driver.overlay_begin        = xv_overlay_begin;
  this->vo_driver.overlay_blend        = xv_overlay_blend;
  this->vo_driver.overlay_end          = xv_overlay_end;
  this->vo_driver.display_frame        = xv_display_frame;
  this->vo_driver.get_property         = xv_get_property;
  this->vo_driver.set_property         = xv_set_property;
  this->vo_driver.get_property_min_max = xv_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xv_gui_data_exchange;
  this->vo_driver.dispose              = xv_dispose;
  this->vo_driver.redraw_needed        = xv_redraw_needed;

  /*
   * init properties
   */

  for (i = 0; i < VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = None;
    this->props[i].defer = 0;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  this->sc.user_ratio                        =
    this->props[VO_PROP_ASPECT_RATIO].value  = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value          = 100;
  this->props[VO_PROP_ZOOM_Y].value          = 100;

  cm_init (this);

  /*
   * check this adaptor's capabilities
   */
  this->port_attributes = xine_list_new();

  LOCK_DISPLAY(this);
  attr = XvQueryPortAttributes(this->display, xv_port, &nattr);
  if(attr && nattr) {
    int k;

    for(k = 0; k < nattr; k++) {
      if((attr[k].flags & XvSettable) && (attr[k].flags & XvGettable)) {
	const char *const name = attr[k].name;
	/* store initial port attribute value */
	xv_store_port_attribute(this, name);

	if(!strcmp(name, "XV_HUE")) {
	  if (!strncmp(adaptor_info[adaptor_num].name, "NV", 2)) {
            xprintf (this->xine, XINE_VERBOSITY_NONE, LOG_MODULE ": ignoring broken XV_HUE settings on NVidia cards\n");
	  } else {
	    this->capabilities |= VO_CAP_HUE;
	    xv_check_capability (this, VO_PROP_HUE, attr[k],
			         adaptor_info[adaptor_num].base_id,
			         NULL, NULL, NULL);
	  }
	} else if(!strcmp(name, "XV_SATURATION")) {
	  this->capabilities |= VO_CAP_SATURATION;
	  xv_check_capability (this, VO_PROP_SATURATION, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_BRIGHTNESS")) {
	  this->capabilities |= VO_CAP_BRIGHTNESS;
	  xv_check_capability (this, VO_PROP_BRIGHTNESS, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_CONTRAST")) {
	  this->capabilities |= VO_CAP_CONTRAST;
	  xv_check_capability (this, VO_PROP_CONTRAST, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_GAMMA")) {
	  this->capabilities |= VO_CAP_GAMMA;
	  xv_check_capability (this, VO_PROP_GAMMA, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_ITURBT_709")) { /* nvidia */
	  LOCK_DISPLAY(this);
	  this->cm_atom = XInternAtom (this->display, name, False);
	  if (this->cm_atom != None) {
	    XvGetPortAttribute (this->display, this->xv_port, this->cm_atom, &this->cm_active);
	    this->cm_active = this->cm_active ? 2 : 10;
	    this->capabilities |= VO_CAP_COLOR_MATRIX;
	  }
	  UNLOCK_DISPLAY(this);
	} else if(!strcmp(name, "XV_COLORSPACE")) { /* radeonhd */
	  LOCK_DISPLAY(this);
	  this->cm_atom2 = XInternAtom (this->display, name, False);
	  if (this->cm_atom2 != None) {
	    XvGetPortAttribute (this->display, this->xv_port, this->cm_atom2, &this->cm_active);
	    this->cm_active = this->cm_active == 2 ? 2 : (this->cm_active == 1 ? 10 : 0);
	    this->capabilities |= VO_CAP_COLOR_MATRIX;
	  }
	  UNLOCK_DISPLAY(this);
	} else if(!strcmp(name, "XV_COLORKEY")) {
	  this->capabilities |= VO_CAP_COLORKEY;
	  xv_check_capability (this, VO_PROP_COLORKEY, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       "video.device.xv_colorkey",
			       VIDEO_DEVICE_XV_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_AUTOPAINT_COLORKEY")) {
	  this->capabilities |= VO_CAP_AUTOPAINT_COLORKEY;
	  xv_check_capability (this, VO_PROP_AUTOPAINT_COLORKEY, attr[k],
			       adaptor_info[adaptor_num].base_id,
			       "video.device.xv_autopaint_colorkey",
			       VIDEO_DEVICE_XV_AUTOPAINT_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_FILTER")) {
	  int xv_filter;
	  /* This setting is specific to Permedia 2/3 cards. */
	  xv_filter = config->register_range (config, "video.device.xv_filter", 0,
					      attr[k].min_value, attr[k].max_value,
					      VIDEO_DEVICE_XV_FILTER_HELP,
					      20, xv_update_XV_FILTER, this);
	  config->update_num(config,"video.device.xv_filter",xv_filter);
	} else if(!strcmp(name, "XV_DOUBLE_BUFFER")) {
	  int xv_double_buffer =
	    config->register_bool (config, "video.device.xv_double_buffer", 1,
				   VIDEO_DEVICE_XV_DOUBLE_BUFFER_HELP,
				   20, xv_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.device.xv_double_buffer",xv_double_buffer);
	} else if(((this->sync_is_vsync = 0), !strcmp(name, sync_atoms[0])) ||
		  ((this->sync_is_vsync = 1), !strcmp(name, sync_atoms[1]))) {
	  int xv_sync_to_vblank;
	  xv_sync_to_vblank =
	    config->register_bool (config, "video.device.xv_sync_to_vblank", 1,
	      _("enable vblank sync"),
	      _("This option will synchronize the update of the video image to the "
		"repainting of the entire screen (\"vertical retrace\"). This eliminates "
		"flickering and tearing artifacts. On nvidia cards one may also "
		"need to run \"nvidia-settings\" and choose which display device to "
		"sync to under the XVideo Settings tab"),
	      20, xv_update_XV_SYNC_TO_VBLANK, this);
	  config->update_num(config,"video.device.xv_sync_to_vblank",xv_sync_to_vblank);
	} else if(!strcmp(name, "XV_BICUBIC")) {
	  int xv_bicubic =
	    config->register_enum (config, "video.device.xv_bicubic", 2,
				   (char **)bicubic_types, VIDEO_DEVICE_XV_BICUBIC_HELP,
				   20, xv_update_XV_BICUBIC, this);
	  config->update_num(config,"video.device.xv_bicubic",xv_bicubic);
	}
      }
    }
    XFree(attr);
  }
  else
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": no port attributes defined.\n");
  XvFreeAdaptorInfo(adaptor_info);

  this->props[VO_PROP_BRIGHTNESS].defer = 1;
  this->props[VO_PROP_CONTRAST].defer   = 1;
  this->props[VO_PROP_SATURATION].defer = 1;
  if ((this->props[VO_PROP_BRIGHTNESS].atom != None) &&
    (this->props[VO_PROP_CONTRAST].atom != None)) {
    static const char * const xv_fullrange_conf_labels[] = {"Off", "Normal", NULL};
    this->fullrange_mode = this->xine->config->register_enum (
      this->xine->config,
      "video.output.xv_fullrange",
      0,
      (char **)xv_fullrange_conf_labels,
      _("Fullrange color emulation"),
      _("Emulate fullrange color by modifying brightness/contrast/saturation settings.\n\n"
        "Off:    Let decoders convert such video.\n"
        "        Works with all graphics cards, but needs a bit more CPU power.\n\n"
        "Normal: Fast and better quality. Should work at least with some newer cards\n"
        "        (GeForce 7+, 210+).\n\n"),
      10,
      xv_fullrange_cb_config,
      this
    );
    if (this->fullrange_mode)
      this->capabilities |= VO_CAP_FULLRANGE;
  } else {
    this->fullrange_mode = 0;
  }

  /*
   * check supported image formats
   */

  fo = XvListImageFormats(this->display, this->xv_port, (int*)&formats);
  UNLOCK_DISPLAY(this);

  this->xv_format_yv12 = 0;
  this->xv_format_yuy2 = 0;

  for(i = 0; i < formats; i++) {
    lprintf ("Xv image format: 0x%x (%4.4s) %s\n",
	     fo[i].id, (char*)&fo[i].id,
	     (fo[i].format == XvPacked) ? "packed" : "planar");

    switch (fo[i].id) {
    case XINE_IMGFMT_YV12:
      this->xv_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YV12");
      break;
    case XINE_IMGFMT_YUY2:
      this->xv_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YUY2");
      break;
    default:
      break;
    }
  }

  if(fo) {
    LOCK_DISPLAY(this);
    XFree(fo);
    UNLOCK_DISPLAY(this);
  }

  /*
   * check max. supported image size
   */

  XvQueryEncodings(this->display, xv_port, &nencode, &encodings);
  if (encodings) {
    int n;
    for (n = 0; n < nencode; n++) {
      if (!strcmp(encodings[n].name, "XV_IMAGE")) {
        xprintf(this->xine, XINE_VERBOSITY_LOG,
                LOG_MODULE ": max XvImage size %li x %li\n",
                encodings[n].width, encodings[n].height);
        this->props[VO_PROP_MAX_VIDEO_WIDTH].value  = encodings[n].width;
        this->props[VO_PROP_MAX_VIDEO_HEIGHT].value = encodings[n].height;
        break;
      }
    }
    XvFreeEncodingInfo(encodings);
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works, using supported format
   */
  LOCK_DISPLAY(this);
  myimage = create_ximage (this, &myshminfo, 100, 100,
			   (this->xv_format_yv12 != 0) ? XINE_IMGFMT_YV12 : XINE_IMGFMT_YUY2);
  dispose_ximage (this, &myshminfo, myimage);
  UNLOCK_DISPLAY(this);

  this->use_pitch_alignment =
    config->register_bool (config, "video.device.xv_pitch_alignment", 0,
			   VIDEO_DEVICE_XV_PITCH_ALIGNMENT_HELP,
			   10, xv_update_xv_pitch_alignment, this);

  LOCK_DISPLAY(this);
  if(this->use_colorkey==1) {
    this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                    this->drawable, X11OSD_COLORKEY);
    if(this->xoverlay)
      x11osd_colorkey(this->xoverlay, this->colorkey, &this->sc);
  } else {
    this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                    this->drawable, X11OSD_SHAPED);
  }
  UNLOCK_DISPLAY(this);

  if( this->xoverlay )
    this->capabilities |= VO_CAP_UNSCALED_OVERLAY;

  return &this->vo_driver;
}

static vo_driver_t *open_plugin_old (video_driver_class_t *class_gen, const void *visual_gen) {
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

  return open_plugin_2(class_gen, (void *)&visual);
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
  xv_class_t        *this = (xv_class_t *) calloc(1, sizeof(xv_class_t));

  this->driver_class.open_plugin     = open_plugin_old;
  this->driver_class.identifier      = "Xv";
  this->driver_class.description     = N_("xine video output plugin using the MIT X video extension");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static void *init_class_2 (xine_t *xine, void *visual_gen) {
  xv_class_t	       *this;
  this = init_class (xine, visual_gen);
  this->driver_class.open_plugin     = open_plugin_2;
  return this;
}

static const vo_info_t vo_info_xv = {
  9,                      /* priority    */
  XINE_VISUAL_TYPE_X11    /* visual type */
};

/* visual type with configurable X11 locking */
static const vo_info_t vo_info_xv_2 = {
  9,                      /* priority    */
  XINE_VISUAL_TYPE_X11_2  /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xv", XINE_VERSION_CODE, &vo_info_xv, init_class },
  { PLUGIN_VIDEO_OUT, 22, "xv", XINE_VERSION_CODE, &vo_info_xv_2, init_class_2 },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
