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
 * video_out_xcbxv.c, X11 video extension interface for xine
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
 * ported to xcb by Christoph Pfister - Feb 2007
 */

#define LOG_MODULE "video_out_xcbxv"
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
#include <errno.h>
#include <math.h>

#include <sys/types.h>
#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <xcb/xv.h>

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
/* #include "overlay.h" */
#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "xcbosd.h"
#include "xv_common.h"

typedef struct xv_driver_s xv_driver_t;

typedef struct {
  int                value;
  int                min;
  int                max;
  xcb_atom_t         atom;

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

  uint8_t           *image;
  xcb_shm_seg_t      shmseg;
  unsigned int       xv_format;
  unsigned int       xv_data_size;
  unsigned int       xv_width;
  unsigned int       xv_height;
  unsigned int       xv_pitches[3];
  unsigned int       xv_offsets[3];

  int                req_width, req_height;
} xv_frame_t;


struct xv_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  /* xcb / xv related stuff */
  xcb_connection_t  *connection;
  xcb_screen_t      *screen;
  xcb_window_t       window;
  unsigned int       xv_format_yv12;
  unsigned int       xv_format_yuy2;
  xcb_gc_t           gc;
  xcb_xv_port_t      xv_port;

  int                use_shm;
  int                use_pitch_alignment;
  uint32_t           capabilities;
  xv_property_t      props[VO_NUM_PROPERTIES];

  xv_frame_t        *recent_frames[VO_NUM_RECENT_FRAMES];
  xv_frame_t        *cur_frame;
  xcbosd            *xoverlay;
  int                ovl_changed;

  /* all scaling information goes here */
  vo_scale_t         sc;

  int                use_colorkey;
  uint32_t           colorkey;

  int                sync_is_vsync;

  /* hold initial port attributes values to restore on exit */
  xine_list_t       *port_attributes;

  xine_t            *xine;

  alphablend_t       alphablend_extra_data;

  pthread_mutex_t    main_mutex;

  /* color matrix switching */
  int                cm_active, cm_state;
  xcb_atom_t         cm_atom, cm_atom2;
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

  if (frame->shmseg) {
    pthread_mutex_lock(&this->main_mutex);
    xcb_shm_detach(this->connection, frame->shmseg);
    frame->shmseg = 0;
    pthread_mutex_unlock(&this->main_mutex);
    shmdt(frame->image);
  }
  else
    free(frame->image);

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

static void create_ximage(xv_driver_t *this, xv_frame_t *frame, int width, int height, int format)
{
  xcb_xv_query_image_attributes_cookie_t query_attributes_cookie;
  xcb_xv_query_image_attributes_reply_t *query_attributes_reply;

  unsigned int length;

  if (this->use_pitch_alignment) {
    width = (width + 7) & ~0x7;
  }

  switch (format) {
  case XINE_IMGFMT_YV12:
    frame->xv_format = this->xv_format_yv12;
    break;
  case XINE_IMGFMT_YUY2:
    frame->xv_format = this->xv_format_yuy2;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
    _x_abort();
  }

  query_attributes_cookie = xcb_xv_query_image_attributes(this->connection, this->xv_port, frame->xv_format, width, height);
  query_attributes_reply = xcb_xv_query_image_attributes_reply(this->connection, query_attributes_cookie, NULL);

  if (query_attributes_reply == NULL)
    return;

  frame->xv_data_size = query_attributes_reply->data_size;
  frame->xv_width = query_attributes_reply->width;
  frame->xv_height = query_attributes_reply->height;

  length = xcb_xv_query_image_attributes_pitches_length(query_attributes_reply);
  if (length > 3)
    length = 3;
  memcpy(frame->xv_pitches, xcb_xv_query_image_attributes_pitches(query_attributes_reply), length * sizeof(frame->xv_pitches[0]));

  length = xcb_xv_query_image_attributes_offsets_length(query_attributes_reply);
  if (length > 3)
    length = 3;
  memcpy(frame->xv_offsets, xcb_xv_query_image_attributes_offsets(query_attributes_reply), length * sizeof(frame->xv_offsets[0]));

  free(query_attributes_reply);

  if (this->use_shm) {
    int shmid;
    xcb_void_cookie_t shm_attach_cookie;
    xcb_generic_error_t *generic_error;

    /*
     * try shm
     */

    if (frame->xv_data_size == 0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: XvShmCreateImage returned a zero size\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      goto shm_fail1;
    }

    shmid = shmget(IPC_PRIVATE, frame->xv_data_size, IPC_CREAT | 0777);

    if (shmid < 0 ) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error in shmget: %s\n"), LOG_MODULE, strerror(errno));
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      goto shm_fail1;
    }

    frame->image = shmat(shmid, 0, 0);

    if (frame->image == ((void *) -1)) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      _("%s: shared memory error (address error)\n"), LOG_MODULE);
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
   * fall back to plain Xv if necessary
   */

  switch (format) {
  case XINE_IMGFMT_YV12:
    frame->image = malloc(width * height * 3/2);
    break;
  case XINE_IMGFMT_YUY2:
    frame->image = malloc(width * height * 2);
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
    _x_abort();
  }
}

static void dispose_ximage(xv_driver_t *this, xv_frame_t *frame)
{
  if (frame->shmseg) {
    xcb_shm_detach(this->connection, frame->shmseg);
    frame->shmseg = 0;
    shmdt(frame->image);
  } else
    free(frame->image);
  frame->image = NULL;
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

    pthread_mutex_lock(&this->main_mutex);

    /*
     * (re-) allocate xvimage
     */

    if (frame->image)
      dispose_ximage(this, frame);

    create_ximage(this, frame, width, height, format);
    if (!frame->image) {
      frame->vo_frame.base[0] = NULL;
      frame->vo_frame.base[1] = NULL;
      frame->vo_frame.base[2] = NULL;
      frame->req_width = 0;
      frame->vo_frame.width = 0;
      return;
    }

    if(format == XINE_IMGFMT_YUY2) {
      frame->vo_frame.pitches[0] = frame->xv_pitches[0];
      frame->vo_frame.base[0] = frame->image + frame->xv_offsets[0];
      {
        const union {uint8_t bytes[4]; uint32_t word;} black = {{0, 128, 0, 128}};
        uint32_t *q = (uint32_t *)frame->vo_frame.base[0];
        int i;
        for (i = frame->vo_frame.pitches[0] * frame->xv_height / 4; i > 0; i--)
          *q++ = black.word;
      }
    }
    else {
      frame->vo_frame.pitches[0] = frame->xv_pitches[0];
      frame->vo_frame.pitches[1] = frame->xv_pitches[2];
      frame->vo_frame.pitches[2] = frame->xv_pitches[1];
      frame->vo_frame.base[0] = frame->image + frame->xv_offsets[0];
      frame->vo_frame.base[1] = frame->image + frame->xv_offsets[2];
      frame->vo_frame.base[2] = frame->image + frame->xv_offsets[1];
      memset (frame->vo_frame.base[0], 0, frame->vo_frame.pitches[0] * frame->xv_height);
      memset (frame->vo_frame.base[1], 128, frame->vo_frame.pitches[1] * frame->xv_height / 2);
      memset (frame->vo_frame.base[2], 128, frame->vo_frame.pitches[2] * frame->xv_height / 2);
    }

    /* allocated frame size may not match requested size */
    frame->req_width  = width;
    frame->req_height = height;
    frame->width  = frame->xv_width;
    frame->height = frame->xv_height;
    frame->format = format;

    pthread_mutex_unlock(&this->main_mutex);
  }

  if (frame->vo_frame.width > frame->width)
    frame->vo_frame.width  = frame->width;
  if (frame->vo_frame.height > frame->height)
    frame->vo_frame.height = frame->height;

  frame->ratio = ratio;
}

static void xv_clean_output_area (xv_driver_t *this) {
  int i;
  xcb_rectangle_t rects[4];
  int rects_count = 0;

  pthread_mutex_lock(&this->main_mutex);

  xcb_change_gc(this->connection, this->gc, XCB_GC_FOREGROUND, &this->screen->black_pixel);

  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h ) {
      rects[rects_count].x = this->sc.border[i].x;
      rects[rects_count].y = this->sc.border[i].y;
      rects[rects_count].width = this->sc.border[i].w;
      rects[rects_count].height = this->sc.border[i].h;
      rects_count++;
    }
  }

  if (rects_count > 0)
    xcb_poly_fill_rectangle(this->connection, this->window, this->gc, rects_count, rects);

  if (this->use_colorkey) {
    xcb_change_gc(this->connection, this->gc, XCB_GC_FOREGROUND, &this->colorkey);
    xcb_rectangle_t rectangle = { this->sc.output_xoffset, this->sc.output_yoffset,
				  this->sc.output_width, this->sc.output_height };
    xcb_poly_fill_rectangle(this->connection, this->window, this->gc, 1, &rectangle);
  }

  if (this->xoverlay) {
    xcbosd_resize(this->xoverlay, this->sc.gui_width, this->sc.gui_height);
    this->ovl_changed = 1;
  }

  pthread_mutex_unlock(&this->main_mutex);
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
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_clear(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
  }

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void xv_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;

  if( this->ovl_changed && this->xoverlay ) {
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_expose(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
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
        pthread_mutex_lock(&this->main_mutex);
        xcbosd_blend(this->xoverlay, overlay);
        pthread_mutex_unlock(&this->main_mutex);
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

static void xv_new_color (xv_driver_t *this, int cm) {
  int brig = this->props[VO_PROP_BRIGHTNESS].value;
  int cont = this->props[VO_PROP_CONTRAST].value;
  int satu = this->props[VO_PROP_SATURATION].value;
  int cm2, fr = 0, a, b;
  xcb_atom_t atom;

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
  pthread_mutex_lock(&this->main_mutex);
  atom = this->props[VO_PROP_BRIGHTNESS].atom;
  if (atom != XCB_NONE)
    xcb_xv_set_port_attribute (this->connection, this->xv_port, atom, brig);
  atom = this->props[VO_PROP_CONTRAST].atom;
  if (atom != XCB_NONE)
    xcb_xv_set_port_attribute (this->connection, this->xv_port, atom, cont);
  atom = this->props[VO_PROP_SATURATION].atom;
  if (atom != XCB_NONE)
    xcb_xv_set_port_attribute (this->connection, this->xv_port, atom, satu);
  pthread_mutex_unlock(&this->main_mutex);

  if (this->cm_atom != XCB_NONE) {
    /* 0 = 601 (SD), 1 = 709 (HD) */
    /* so far only binary nvidia drivers support this. why not nuveau? */
    cm2 = (0xc00c >> cm) & 1;
    pthread_mutex_lock(&this->main_mutex);
    xcb_xv_set_port_attribute (this->connection, this->xv_port, this->cm_atom, cm2);
    pthread_mutex_unlock(&this->main_mutex);
    cm2 = cm2 ? 2 : 10;
  } else if (this->cm_atom2 != XCB_NONE) {
    /* radeonhd: 0 = size based auto, 1 = 601 (SD), 2 = 709 (HD) */
    cm2 = ((0xc00c >> cm) & 1) + 1;
    pthread_mutex_lock(&this->main_mutex);
    xcb_xv_set_port_attribute (this->connection, this->xv_port, this->cm_atom2, cm2);
    pthread_mutex_unlock(&this->main_mutex);
    cm2 = cm2 == 2 ? 2 : 10;
  } else {
    cm2 = 10;
  }

  cm2 |= fr;
  xprintf (this->xine, XINE_VERBOSITY_LOG, "video_out_xcbxv: %s b %d  c %d  s %d  [%s]\n",
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

  pthread_mutex_lock(&this->main_mutex);

  if (this->cur_frame->shmseg) {
    xcb_xv_shm_put_image(this->connection, this->xv_port, this->window, this->gc,
                         this->cur_frame->shmseg, this->cur_frame->xv_format, 0,
                         this->sc.displayed_xoffset, this->sc.displayed_yoffset,
                         this->sc.displayed_width, this->sc.displayed_height,
                         this->sc.output_xoffset, this->sc.output_yoffset,
                         this->sc.output_width, this->sc.output_height,
                         this->cur_frame->xv_width, this->cur_frame->xv_height, 0);

  } else {
    xcb_xv_put_image(this->connection, this->xv_port, this->window, this->gc,
                     this->cur_frame->xv_format,
                     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
                     this->sc.displayed_width, this->sc.displayed_height,
                     this->sc.output_xoffset, this->sc.output_yoffset,
                     this->sc.output_width, this->sc.output_height,
                     this->cur_frame->xv_width, this->cur_frame->xv_height,
                     this->cur_frame->xv_data_size, this->cur_frame->image);
  }

  xcb_flush(this->connection);

  pthread_mutex_unlock(&this->main_mutex);

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

  pthread_mutex_lock(&this->main_mutex);
  xcb_xv_set_port_attribute(this->connection, this->xv_port,
			    property->atom, entry->num_value);
  pthread_mutex_unlock(&this->main_mutex);
}

static int xv_set_property (vo_driver_t *this_gen,
			    int property, int value) {
  xv_driver_t *this = (xv_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if (this->props[property].defer == 1) {
    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;
    this->props[property].value = value;
    this->cm_active = 0;
    return value;
  }

  if (this->props[property].atom != XCB_NONE) {
    xcb_xv_get_port_attribute_cookie_t get_attribute_cookie;
    xcb_xv_get_port_attribute_reply_t *get_attribute_reply;

    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;

    pthread_mutex_lock(&this->main_mutex);
    xcb_xv_set_port_attribute(this->connection, this->xv_port,
			      this->props[property].atom, value);

    get_attribute_cookie = xcb_xv_get_port_attribute(this->connection, this->xv_port, this->props[property].atom);
    get_attribute_reply = xcb_xv_get_port_attribute_reply(this->connection, get_attribute_cookie, NULL);
    this->props[property].value = get_attribute_reply->value;
    free(get_attribute_reply);

    pthread_mutex_unlock(&this->main_mutex);

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
      xcb_rectangle_t rects[4];
      int rects_count = 0;

      pthread_mutex_lock(&this->main_mutex);

      if (this->cur_frame->shmseg) {
	xcb_xv_shm_put_image(this->connection, this->xv_port, this->window, this->gc,
			     this->cur_frame->shmseg, this->cur_frame->xv_format, 0,
			     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
			     this->sc.displayed_width, this->sc.displayed_height,
			     this->sc.output_xoffset, this->sc.output_yoffset,
			     this->sc.output_width, this->sc.output_height,
			     this->cur_frame->xv_width, this->cur_frame->xv_height, 0);
      } else {
	xcb_xv_put_image(this->connection, this->xv_port, this->window, this->gc,
			 this->cur_frame->xv_format,
			 this->sc.displayed_xoffset, this->sc.displayed_yoffset,
			 this->sc.displayed_width, this->sc.displayed_height,
			 this->sc.output_xoffset, this->sc.output_yoffset,
			 this->sc.output_width, this->sc.output_height,
			 this->cur_frame->xv_width, this->cur_frame->xv_height,
			 this->cur_frame->xv_data_size, this->cur_frame->image);
      }

      xcb_change_gc(this->connection, this->gc, XCB_GC_FOREGROUND, &this->screen->black_pixel);

      for( i = 0; i < 4; i++ ) {
	if( this->sc.border[i].w && this->sc.border[i].h ) {
	  rects[rects_count].x = this->sc.border[i].x;
	  rects[rects_count].y = this->sc.border[i].y;
	  rects[rects_count].width = this->sc.border[i].w;
	  rects[rects_count].height = this->sc.border[i].h;
	  rects_count++;
	}
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
    pthread_mutex_lock(&this->main_mutex);
    this->window = (xcb_window_t) (long) data;
    xcb_free_gc(this->connection, this->gc);
    this->gc = xcb_generate_id(this->connection);
    xcb_create_gc(this->connection, this->gc, this->window, 0, NULL);
    if(this->xoverlay)
      xcbosd_drawable_changed(this->xoverlay, this->window);
    this->ovl_changed = 1;
    pthread_mutex_unlock(&this->main_mutex);
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

static void xv_store_port_attribute(xv_driver_t *this, const char * const name) {
  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply;

  xcb_xv_get_port_attribute_cookie_t get_attribute_cookie;
  xcb_xv_get_port_attribute_reply_t *get_attribute_reply;

  pthread_mutex_lock(&this->main_mutex);
  atom_cookie = xcb_intern_atom(this->connection, 0, strlen(name), name);
  atom_reply = xcb_intern_atom_reply(this->connection, atom_cookie, NULL);
  get_attribute_cookie = xcb_xv_get_port_attribute(this->connection, this->xv_port, atom_reply->atom);
  get_attribute_reply = xcb_xv_get_port_attribute_reply(this->connection, get_attribute_cookie, NULL);
  free(atom_reply);
  pthread_mutex_unlock(&this->main_mutex);

  if (get_attribute_reply != NULL) {
    xv_portattribute_t *attr;

    attr = (xv_portattribute_t *) malloc(sizeof(xv_portattribute_t));
    attr->name = strdup(name);
    attr->value = get_attribute_reply->value;
    free(get_attribute_reply);

    xine_list_push_back(this->port_attributes, attr);
  }
}

static void xv_restore_port_attributes(xv_driver_t *this) {
  xine_list_iterator_t ite;

  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply;

  while ((ite = xine_list_front(this->port_attributes)) != NULL) {
    xv_portattribute_t *attr = xine_list_get_value(this->port_attributes, ite);
    xine_list_remove (this->port_attributes, ite);

    pthread_mutex_lock(&this->main_mutex);
    atom_cookie = xcb_intern_atom(this->connection, 0, strlen(attr->name), attr->name);
    atom_reply = xcb_intern_atom_reply(this->connection, atom_cookie, NULL);
    xcb_xv_set_port_attribute(this->connection, this->xv_port, atom_reply->atom, attr->value);
    free(atom_reply);
    pthread_mutex_unlock(&this->main_mutex);

    free( attr->name );
    free( attr );
  }

  pthread_mutex_lock(&this->main_mutex);
  xcb_flush(this->connection);
  pthread_mutex_unlock(&this->main_mutex);

  xine_list_delete( this->port_attributes );
}

static void xv_dispose (vo_driver_t *this_gen) {
  xv_driver_t *this = (xv_driver_t *) this_gen;
  int          i;

  /* restore port attributes to their initial values */
  xv_restore_port_attributes(this);

  pthread_mutex_lock(&this->main_mutex);
  xcb_xv_ungrab_port(this->connection, this->xv_port, XCB_CURRENT_TIME);
  xcb_free_gc(this->connection, this->gc);
  pthread_mutex_unlock(&this->main_mutex);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.dispose
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  if( this->xoverlay ) {
    pthread_mutex_lock(&this->main_mutex);
    xcbosd_destroy(this->xoverlay);
    pthread_mutex_unlock(&this->main_mutex);
  }

  pthread_mutex_destroy(&this->main_mutex);

  _x_alphablend_free(&this->alphablend_extra_data);

  cm_close (this);

  free (this);
}

static int xv_check_yv12(xcb_connection_t *connection, xcb_xv_port_t port) {
  xcb_xv_list_image_formats_cookie_t list_formats_cookie;
  xcb_xv_list_image_formats_reply_t *list_formats_reply;

  xcb_xv_image_format_info_iterator_t format_it;

  list_formats_cookie = xcb_xv_list_image_formats(connection, port);
  list_formats_reply = xcb_xv_list_image_formats_reply(connection, list_formats_cookie, NULL);
  if (!list_formats_reply)
    return 1; /* no formats listed; probably due to an invalid port no. */
  format_it = xcb_xv_list_image_formats_format_iterator(list_formats_reply);

  for (; format_it.rem; xcb_xv_image_format_info_next(&format_it))
    if ((format_it.data->id == XINE_IMGFMT_YV12) &&
	(! (strcmp ((char *) format_it.data->guid, "YV12")))) {
      free(list_formats_reply);
      return 0;
    }

  free(list_formats_reply);
  return 1;
}

static void xv_check_capability (xv_driver_t *this,
				 int property, xcb_xv_attribute_info_t *attr,
				 int base_id,
				 const char *config_name,
				 const char *config_desc,
				 const char *config_help) {
  int          int_default;
  cfg_entry_t *entry;
  const char  *str_prop = xcb_xv_attribute_info_name(attr);

  xcb_xv_get_port_attribute_cookie_t get_attribute_cookie;
  xcb_xv_get_port_attribute_reply_t *get_attribute_reply;

  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply;

  /*
   * some Xv drivers (Gatos ATI) report some ~0 as max values, this is confusing.
   */
  if (VO_PROP_COLORKEY && (attr->max == ~0))
    attr->max = 2147483615;

  atom_cookie = xcb_intern_atom(this->connection, 0, strlen(str_prop), str_prop);
  atom_reply = xcb_intern_atom_reply(this->connection, atom_cookie, NULL);

  this->props[property].min  = attr->min;
  this->props[property].max  = attr->max;
  this->props[property].atom = atom_reply->atom;

  free(atom_reply);

  get_attribute_cookie = xcb_xv_get_port_attribute(this->connection, this->xv_port, this->props[property].atom);
  get_attribute_reply = xcb_xv_get_port_attribute_reply(this->connection, get_attribute_cookie, NULL);

  int_default = get_attribute_reply->value;

  free(get_attribute_reply);

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": port attribute %s (%d) value is %d\n", str_prop, property, int_default);

  /* disable autopaint colorkey by default */
  /* might be overridden using config entry */
  if(strcmp(str_prop, "XV_AUTOPAINT_COLORKEY") == 0)
    int_default = 0;

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr->min == 0) && (attr->max == 1)) {
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

  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply;

  pthread_mutex_lock(&this->main_mutex);
  atom_cookie = xcb_intern_atom(this->connection, 0, strlen (atomstr), atomstr);
  atom_reply = xcb_intern_atom_reply(this->connection, atom_cookie, NULL);
  xcb_xv_set_port_attribute(this->connection, this->xv_port, atom_reply->atom, entry->num_value);
  free(atom_reply);
  pthread_mutex_unlock(&this->main_mutex);

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

static xcb_xv_port_t xv_open_port (xv_driver_t *this, xcb_xv_port_t port) {
  xcb_xv_grab_port_cookie_t grab_port_cookie;
  xcb_xv_grab_port_reply_t *grab_port_reply;

  if (xv_check_yv12 (this->connection, port))
    return 0;

  grab_port_cookie = xcb_xv_grab_port (this->connection, port, XCB_CURRENT_TIME);
  grab_port_reply = xcb_xv_grab_port_reply (this->connection, grab_port_cookie, NULL);

  if (grab_port_reply && (grab_port_reply->result == XCB_GRAB_STATUS_SUCCESS))
  {
    free (grab_port_reply);
    return port;
  }
  free (grab_port_reply);
  return 0;
}

static xcb_xv_adaptor_info_iterator_t *
xv_find_adaptor_by_port (int port, xcb_xv_adaptor_info_iterator_t *adaptor_it)
{
  for (; adaptor_it->rem; xcb_xv_adaptor_info_next(adaptor_it))
    if (adaptor_it->data->type & XCB_XV_TYPE_IMAGE_MASK)
      if (port >= adaptor_it->data->base_id &&
	  port < adaptor_it->data->base_id + adaptor_it->data->num_ports)
	return adaptor_it;
  return NULL; /* shouldn't happen */
}

static xcb_xv_port_t xv_autodetect_port(xv_driver_t *this,
                                        xcb_xv_adaptor_info_iterator_t *adaptor_it,
                                        xcb_xv_port_t base,
					xv_prefertype prefer_type)
{
  for (; adaptor_it->rem; xcb_xv_adaptor_info_next(adaptor_it))
    if (adaptor_it->data->type & XCB_XV_TYPE_IMAGE_MASK &&
        (prefer_type == xv_prefer_none ||
         strcasestr (xcb_xv_adaptor_info_name (adaptor_it->data), prefer_substrings[prefer_type])))
    {
      int j;
      for (j = 0; j < adaptor_it->data->num_ports; ++j)
      {
        xcb_xv_port_t port = adaptor_it->data->base_id + j;
        if (port >= base && xv_open_port (this, port))
          return port;
      }
    }
  return 0;
}

static vo_driver_t *open_plugin(video_driver_class_t *class_gen, const void *visual_gen) {
  xv_class_t           *class = (xv_class_t *) class_gen;
  config_values_t      *config = class->config;
  xv_driver_t          *this;
  int                   i;
  xcb_visual_t         *visual = (xcb_visual_t *) visual_gen;
  xcb_xv_port_t         xv_port;
  xv_prefertype		prefer_type;

  const xcb_query_extension_reply_t *query_extension_reply;

  xcb_xv_query_adaptors_cookie_t query_adaptors_cookie;
  xcb_xv_query_adaptors_reply_t *query_adaptors_reply;
  xcb_xv_query_port_attributes_cookie_t query_attributes_cookie;
  xcb_xv_query_port_attributes_reply_t *query_attributes_reply;
  xcb_xv_list_image_formats_cookie_t list_formats_cookie;
  xcb_xv_list_image_formats_reply_t *list_formats_reply;

  xcb_xv_adaptor_info_iterator_t adaptor_it, adaptor_first;
  xcb_xv_image_format_info_iterator_t format_it;

  this = (xv_driver_t *) calloc(1, sizeof(xv_driver_t));
  if (!this)
    return NULL;

  pthread_mutex_init(&this->main_mutex, NULL);

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->connection        = visual->connection;
  this->screen            = visual->screen;
  this->window            = visual->window;
  this->config            = config;

  /*
   * check for Xvideo support
   */

  query_extension_reply = xcb_get_extension_data(this->connection, &xcb_xv_id);
  if (!query_extension_reply || !query_extension_reply->present) {
    xprintf (class->xine, XINE_VERBOSITY_LOG, _("%s: Xv extension not present.\n"), LOG_MODULE);
    return NULL;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  query_adaptors_cookie = xcb_xv_query_adaptors(this->connection, this->window);
  query_adaptors_reply = xcb_xv_query_adaptors_reply(this->connection, query_adaptors_cookie, NULL);

  if (!query_adaptors_reply) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": XvQueryAdaptors failed.\n");
    return NULL;
  }

  adaptor_first = xcb_xv_query_adaptors_info_iterator(query_adaptors_reply);
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
      adaptor_it = adaptor_first;
      xv_port = xv_autodetect_port (this, &adaptor_it, xv_port, prefer_type);
    } else
      xv_find_adaptor_by_port (xv_port, &adaptor_it);
  }
  if (!xv_port)
  {
    adaptor_it = adaptor_first;
    xv_port = xv_autodetect_port (this, &adaptor_it, 0, prefer_type);
  }
  if (!xv_port)
  {
    if (prefer_type)
      xprintf(class->xine, XINE_VERBOSITY_NONE,
	      _("%s: no available ports of type \"%s\", defaulting...\n"),
	      LOG_MODULE, prefer_labels[prefer_type]);
    adaptor_it = adaptor_first;
    xv_port = xv_autodetect_port (this, &adaptor_it, 0, xv_prefer_none);
  }

  if (!xv_port) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: Xv extension is present but I couldn't find a usable yuv12 port.\n"
	      "\tLooks like your graphics hardware driver doesn't support Xv?!\n"),
	    LOG_MODULE);

    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    return NULL;
  }
  else
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: using Xv port %d from adaptor %s for hardware "
	      "colour space conversion and scaling.\n"), LOG_MODULE, xv_port,
            xcb_xv_adaptor_info_name(adaptor_it.data));

  this->xv_port           = xv_port;

  _x_vo_scale_init (&this->sc, 1, 0, config );
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  this->gc                      = xcb_generate_id(this->connection);
  xcb_create_gc(this->connection, this->gc, this->window, 0, NULL);
  this->capabilities            = VO_CAP_CROP | VO_CAP_ZOOM_X | VO_CAP_ZOOM_Y;
  this->use_shm                 = 1;
  this->use_colorkey            = 0;
  this->colorkey                = 0;
  this->xoverlay                = NULL;
  this->ovl_changed             = 0;
  this->xine                    = class->xine;
  this->cm_atom                 = XCB_NONE;
  this->cm_atom2                = XCB_NONE;

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
    this->props[i].atom  = XCB_NONE;
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

  query_attributes_cookie = xcb_xv_query_port_attributes(this->connection, xv_port);
  query_attributes_reply = xcb_xv_query_port_attributes_reply(this->connection, query_attributes_cookie, NULL);
  if(query_attributes_reply) {
    xcb_xv_attribute_info_iterator_t attribute_it;
    attribute_it = xcb_xv_query_port_attributes_attributes_iterator(query_attributes_reply);

    for (; attribute_it.rem; xcb_xv_attribute_info_next(&attribute_it)) {
      if ((attribute_it.data->flags & XCB_XV_ATTRIBUTE_FLAG_SETTABLE) && (attribute_it.data->flags & XCB_XV_ATTRIBUTE_FLAG_GETTABLE)) {
	const char *const name = xcb_xv_attribute_info_name(attribute_it.data);
	/* store initial port attribute value */
	xv_store_port_attribute(this, name);

	if(!strcmp(name, "XV_HUE")) {
	  if (!strncmp(xcb_xv_adaptor_info_name(adaptor_it.data), "NV", 2)) {
            xprintf (this->xine, XINE_VERBOSITY_NONE, LOG_MODULE ": ignoring broken XV_HUE settings on NVidia cards\n");
	  } else {
	    this->capabilities |= VO_CAP_HUE;
	    xv_check_capability (this, VO_PROP_HUE, attribute_it.data,
			         adaptor_it.data->base_id,
			         NULL, NULL, NULL);
	  }
	} else if(!strcmp(name, "XV_SATURATION")) {
	  this->capabilities |= VO_CAP_SATURATION;
	  xv_check_capability (this, VO_PROP_SATURATION, attribute_it.data,
			       adaptor_it.data->base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_BRIGHTNESS")) {
	  this->capabilities |= VO_CAP_BRIGHTNESS;
	  xv_check_capability (this, VO_PROP_BRIGHTNESS, attribute_it.data,
			       adaptor_it.data->base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_CONTRAST")) {
	  this->capabilities |= VO_CAP_CONTRAST;
	  xv_check_capability (this, VO_PROP_CONTRAST, attribute_it.data,
			       adaptor_it.data->base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_GAMMA")) {
	  this->capabilities |= VO_CAP_GAMMA;
	  xv_check_capability (this, VO_PROP_GAMMA, attribute_it.data,
			       adaptor_it.data->base_id,
			       NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_ITURBT_709")) { /* nvidia */
	  xcb_xv_get_port_attribute_cookie_t get_attribute_cookie;
	  xcb_xv_get_port_attribute_reply_t *get_attribute_reply;
	  xcb_intern_atom_cookie_t atom_cookie;
	  xcb_intern_atom_reply_t *atom_reply;
	  pthread_mutex_lock(&this->main_mutex);
	  atom_cookie = xcb_intern_atom (this->connection, 0, 13, name);
	  atom_reply = xcb_intern_atom_reply (this->connection, atom_cookie, NULL);
	  if (atom_reply) {
	    this->cm_atom = atom_reply->atom;
	    free (atom_reply);
	  }
	  if (this->cm_atom != XCB_NONE) {
	    get_attribute_cookie = xcb_xv_get_port_attribute (this->connection,
	      this->xv_port, this->cm_atom);
	    get_attribute_reply = xcb_xv_get_port_attribute_reply (this->connection,
	      get_attribute_cookie, NULL);
	    if (get_attribute_reply) {
	      this->cm_active = get_attribute_reply->value ? 2 : 10;
	      free(get_attribute_reply);
	      this->capabilities |= VO_CAP_COLOR_MATRIX;
	    }
	  }
	  pthread_mutex_unlock(&this->main_mutex);
	} else if(!strcmp(name, "XV_COLORSPACE")) { /* radeonhd */
	  xcb_xv_get_port_attribute_cookie_t get_attribute_cookie;
	  xcb_xv_get_port_attribute_reply_t *get_attribute_reply;
	  xcb_intern_atom_cookie_t atom_cookie;
	  xcb_intern_atom_reply_t *atom_reply;
	  pthread_mutex_lock(&this->main_mutex);
	  atom_cookie = xcb_intern_atom (this->connection, 0, 13, name);
	  atom_reply = xcb_intern_atom_reply (this->connection, atom_cookie, NULL);
	  if (atom_reply) {
	    this->cm_atom2 = atom_reply->atom;
	    free (atom_reply);
	  }
	  if (this->cm_atom2 != XCB_NONE) {
	    get_attribute_cookie = xcb_xv_get_port_attribute (this->connection,
	      this->xv_port, this->cm_atom2);
	    get_attribute_reply = xcb_xv_get_port_attribute_reply (this->connection,
	      get_attribute_cookie, NULL);
	    if (get_attribute_reply) {
	      this->cm_active = get_attribute_reply->value == 2 ? 2 : (get_attribute_reply->value == 1 ? 10 : 0);
	      free(get_attribute_reply);
	      this->capabilities |= VO_CAP_COLOR_MATRIX;
	    }
	  }
	  pthread_mutex_unlock(&this->main_mutex);
	} else if(!strcmp(name, "XV_COLORKEY")) {
	  this->capabilities |= VO_CAP_COLORKEY;
	  xv_check_capability (this, VO_PROP_COLORKEY, attribute_it.data,
			       adaptor_it.data->base_id,
			       "video.device.xv_colorkey",
			       VIDEO_DEVICE_XV_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_AUTOPAINT_COLORKEY")) {
	  this->capabilities |= VO_CAP_AUTOPAINT_COLORKEY;
	  xv_check_capability (this, VO_PROP_AUTOPAINT_COLORKEY, attribute_it.data,
			       adaptor_it.data->base_id,
			       "video.device.xv_autopaint_colorkey",
			       VIDEO_DEVICE_XV_AUTOPAINT_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_FILTER")) {
	  int xv_filter;
	  /* This setting is specific to Permedia 2/3 cards. */
	  xv_filter = config->register_range (config, "video.device.xv_filter", 0,
					      attribute_it.data->min, attribute_it.data->max,
					      VIDEO_DEVICE_XV_FILTER_HELP,
					      20, xv_update_XV_FILTER, this);
	  config->update_num(config,"video.device.xv_filter",xv_filter);
	} else if(!strcmp(name, "XV_DOUBLE_BUFFER")) {
	  int xv_double_buffer =
	    config->register_bool (config, "video.device.xv_double_buffer", 1,
				   VIDEO_DEVICE_XV_DOUBLE_BUFFER_HELP,
				   20, xv_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.device.xv_double_buffer",xv_double_buffer);
	} else if(!strcmp(name, sync_atoms[this->sync_is_vsync = 0]) ||
		  !strcmp(name, sync_atoms[this->sync_is_vsync = 1])) {
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
    free(query_attributes_reply);
  }
  else
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": no port attributes defined.\n");
  free(query_adaptors_reply);

  this->props[VO_PROP_BRIGHTNESS].defer = 1;
  this->props[VO_PROP_CONTRAST].defer   = 1;
  this->props[VO_PROP_SATURATION].defer = 1;
  if ((this->props[VO_PROP_BRIGHTNESS].atom != XCB_NONE) &&
    (this->props[VO_PROP_CONTRAST].atom != XCB_NONE)) {
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

  list_formats_cookie = xcb_xv_list_image_formats(this->connection, xv_port);
  list_formats_reply = xcb_xv_list_image_formats_reply(this->connection, list_formats_cookie, NULL);

  format_it = xcb_xv_list_image_formats_format_iterator(list_formats_reply);

  this->xv_format_yv12 = 0;
  this->xv_format_yuy2 = 0;

  for (; format_it.rem; xcb_xv_image_format_info_next(&format_it)) {
    lprintf ("Xv image format: 0x%x (%4.4s) %s\n",
	     format_it.data->id, (char*)&format_it.data->id,
	     (format_it.data->format == XCB_XV_IMAGE_FORMAT_INFO_FORMAT_PACKED)
	       ? "packed" : "planar");

    switch (format_it.data->id) {
    case XINE_IMGFMT_YV12:
      this->xv_format_yv12 = format_it.data->id;
      this->capabilities |= VO_CAP_YV12;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YV12");
      break;
    case XINE_IMGFMT_YUY2:
      this->xv_format_yuy2 = format_it.data->id;
      this->capabilities |= VO_CAP_YUY2;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YUY2");
      break;
    default:
      break;
    }
  }

  free(list_formats_reply);

  this->use_pitch_alignment =
    config->register_bool (config, "video.device.xv_pitch_alignment", 0,
			   VIDEO_DEVICE_XV_PITCH_ALIGNMENT_HELP,
			   10, xv_update_xv_pitch_alignment, this);

  if(this->use_colorkey==1) {
    this->xoverlay = xcbosd_create(this->xine, this->connection, this->screen,
                                   this->window, XCBOSD_COLORKEY);
    if(this->xoverlay)
      xcbosd_colorkey(this->xoverlay, this->colorkey, &this->sc);
  } else {
    this->xoverlay = xcbosd_create(this->xine, this->connection, this->screen,
                                   this->window, XCBOSD_SHAPED);
  }

  if( this->xoverlay )
    this->capabilities |= VO_CAP_UNSCALED_OVERLAY;

  return &this->vo_driver;
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
  xv_class_t        *this = (xv_class_t *) calloc(1, sizeof(xv_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "Xv";
  this->driver_class.description     = N_("xine video output plugin using the MIT X video extension");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static const vo_info_t vo_info_xv = {
  9,                      /* priority    */
  XINE_VISUAL_TYPE_XCB    /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xv", XINE_VERSION_CODE, &vo_info_xv, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
