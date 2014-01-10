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
 *
 * video_out_vidix.c
 *
 * xine video_out driver to vidix library by Miguel Freitas 30/05/2002
 *
 * based on video_out_xv.c, video_out_syncfb.c and video_out_pgx64.c
 *
 * some vidix specific code from mplayer (file vosub_vidix.c)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

#ifdef HAVE_FB
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <errno.h>
#endif

#include "xine.h"
#include "vidixlib.h"
#include "fourcc.h"

#define LOG_MODULE "video_out_vidix"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/vo_scale.h>

#ifdef HAVE_X11
#include "x11osd.h"
#endif

#define NUM_FRAMES 3

typedef struct vidix_driver_s vidix_driver_t;


typedef struct vidix_property_s {
  int                value;
  int                min;
  int                max;

  cfg_entry_t       *entry;

  vidix_driver_t       *this;
} vidix_property_t;


typedef struct vidix_frame_s {
    vo_frame_t vo_frame;
    int width, height, format;
    double ratio;
} vidix_frame_t;


struct vidix_driver_s {

  vo_driver_t         vo_driver;

  config_values_t    *config;

  char               *vidix_name;
  VDL_HANDLE          vidix_handler;
  uint8_t            *vidix_mem;
  vidix_capability_t  vidix_cap;
  vidix_playback_t    vidix_play;
  vidix_grkey_t       vidix_grkey;
  vidix_video_eq_t    vidix_eq;
  vidix_yuv_t         dstrides;
  int                 vidix_started;
  int                 next_frame;
  int                 got_frame_data;

  uint32_t            colourkey;
  int                 use_doublebuffer;

  int                 supports_yv12;

  pthread_mutex_t     mutex;

  vidix_property_t    props[VO_NUM_PROPERTIES];
  uint32_t            capabilities;

  int                 visual_type;

  /* X11 related stuff */
#ifdef HAVE_X11
  Display            *display;
  int                 screen;
  Drawable            drawable;
  GC                  gc;
  x11osd              *xoverlay;
  int                  ovl_changed;
#endif

  /* fb related stuff */
  int                 fb_width;
  int                 fb_height;

  int                 depth;

  vo_scale_t          sc;

  int                 delivered_format;

  xine_t             *xine;

  alphablend_t        alphablend_extra_data;
};

typedef struct vidix_class_s {
  video_driver_class_t driver_class;

  config_values_t     *config;

  VDL_HANDLE          vidix_handler;
  vidix_capability_t  vidix_cap;

  xine_t             *xine;
} vidix_class_t;


static void free_framedata(vidix_frame_t* frame)
{
   if(frame->vo_frame.base[0]) {
      free(frame->vo_frame.base[0]);
      frame->vo_frame.base[0] = NULL;
   }

   if(frame->vo_frame.base[1]) {
      free(frame->vo_frame.base[1]);
      frame->vo_frame.base[1] = NULL;
   }

   if(frame->vo_frame.base[2]) {
      free(frame->vo_frame.base[2]);
      frame->vo_frame.base[2] = NULL;
   }
}

static void write_frame_YUV420P2(vidix_driver_t* this, vidix_frame_t* frame)
{
   uint8_t* y    = frame->vo_frame.base[0] + this->sc.displayed_xoffset +
                     this->sc.displayed_yoffset*frame->vo_frame.pitches[0];
   uint8_t* cb   = frame->vo_frame.base[1] + this->sc.displayed_xoffset/2 +
                     this->sc.displayed_yoffset*frame->vo_frame.pitches[1]/2;
   uint8_t* cr   = frame->vo_frame.base[2]+this->sc.displayed_xoffset/2 +
                     this->sc.displayed_yoffset*frame->vo_frame.pitches[2]/2;
   uint8_t* dst8 = (this->vidix_mem +
                    this->vidix_play.offsets[this->next_frame] +
                    this->vidix_play.offset.y);
   int h, w;

   for(h = 0; h < this->sc.displayed_height; h++) {
      xine_fast_memcpy(dst8, y, this->sc.displayed_width);
      y    += frame->vo_frame.pitches[0];
      dst8 += this->dstrides.y;
   }

   dst8 = (this->vidix_mem + this->vidix_play.offsets[this->next_frame] +
           this->vidix_play.offset.v);

   for(h = 0; h < (this->sc.displayed_height / 2); h++) {
     for(w = 0; w < (this->sc.displayed_height / 2); w++) {
       dst8[2*w+0] = cb[w];
       dst8[2*w+1] = cr[w];
     }
      cb += frame->vo_frame.pitches[2];
      cr += frame->vo_frame.pitches[1];
      dst8 += this->dstrides.y;
   }
}

static void write_frame_sfb(vidix_driver_t* this, vidix_frame_t* frame)
{
  uint8_t *base = this->vidix_mem+this->vidix_play.offsets[this->next_frame];

  switch(frame->format) {
    case XINE_IMGFMT_YUY2:
      yuy2_to_yuy2(
       /* src */
        frame->vo_frame.base[0]+this->sc.displayed_xoffset*2+
          this->sc.displayed_yoffset*frame->vo_frame.pitches[0],
        frame->vo_frame.pitches[0],
       /* dst */
        base+this->vidix_play.offset.y, this->dstrides.y,
       /* width x height */
        this->sc.displayed_width, this->sc.displayed_height);
      break;

    case XINE_IMGFMT_YV12: {
      uint8_t* y  = frame->vo_frame.base[0] + this->sc.displayed_xoffset +
                      this->sc.displayed_yoffset*frame->vo_frame.pitches[0];
      uint8_t* cb = frame->vo_frame.base[1] + this->sc.displayed_xoffset/2 +
                      this->sc.displayed_yoffset*frame->vo_frame.pitches[1]/2;
      uint8_t* cr = frame->vo_frame.base[2] + this->sc.displayed_xoffset/2 +
                      this->sc.displayed_yoffset*frame->vo_frame.pitches[2]/2;

      if(this->supports_yv12) {
        if(this->vidix_play.flags & VID_PLAY_INTERLEAVED_UV)
          write_frame_YUV420P2(this, frame);
        else
          yv12_to_yv12(
           /* Y */
            y, frame->vo_frame.pitches[0],
            base+this->vidix_play.offset.y, this->dstrides.y,
           /* U */
            cr, frame->vo_frame.pitches[2],
            base+this->vidix_play.offset.u, this->dstrides.u/2,
           /* V */
            cb, frame->vo_frame.pitches[1],
            base+this->vidix_play.offset.v, this->dstrides.v/2,
           /* width x height */
            this->sc.displayed_width, this->sc.displayed_height);
      } else
          yv12_to_yuy2(
           /* src */
            y,  frame->vo_frame.pitches[0],
            cb, frame->vo_frame.pitches[1],
            cr, frame->vo_frame.pitches[2],
           /* dst */
            base+this->vidix_play.offset.y, this->dstrides.y,
           /* width x height */
            this->sc.displayed_width, this->sc.displayed_height,
           /* progressive */
            frame->vo_frame.progressive_frame);
      break;
    }

    default:
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_vidix: error. (unknown frame format %04x)\n", frame->format);
      break;
   }
}


static void vidix_clean_output_area(vidix_driver_t *this) {

  if(this->visual_type == XINE_VISUAL_TYPE_X11) {
#ifdef HAVE_X11
    int i;

    XLockDisplay(this->display);

    XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));

    for( i = 0; i < 4; i++ ) {
      if( this->sc.border[i].w && this->sc.border[i].h ) {
        XFillRectangle(this->display, this->drawable, this->gc,
                      this->sc.border[i].x, this->sc.border[i].y,
                      this->sc.border[i].w, this->sc.border[i].h);
      }
    }

    XSetForeground(this->display, this->gc, this->colourkey);
    XFillRectangle(this->display, this->drawable, this->gc, this->sc.output_xoffset, this->sc.output_yoffset, this->sc.output_width, this->sc.output_height);

    if (this->xoverlay) {
      x11osd_resize (this->xoverlay, this->sc.gui_width, this->sc.gui_height);
      this->ovl_changed = 1;
    }

    XFlush(this->display);

    XUnlockDisplay(this->display);
#endif
  }
}


static void vidix_update_colourkey(vidix_driver_t *this) {
  switch(this->depth) {
    case 15:
      this->colourkey = ((this->vidix_grkey.ckey.red   & 0xF8) << 7) |
                        ((this->vidix_grkey.ckey.green & 0xF8) << 2) |
                        ((this->vidix_grkey.ckey.blue  & 0xF8) >> 3);
      break;
    case 16:
      this->colourkey = ((this->vidix_grkey.ckey.red   & 0xF8) << 8) |
                        ((this->vidix_grkey.ckey.green & 0xFC) << 3) |
                        ((this->vidix_grkey.ckey.blue  & 0xF8) >> 3);
      break;
    case 24:
    case 32:
      this->colourkey = ((this->vidix_grkey.ckey.red   & 0xFF) << 16) |
                        ((this->vidix_grkey.ckey.green & 0xFF) << 8) |
                        ((this->vidix_grkey.ckey.blue  & 0xFF));
      break;
    default:
      break;
  }

  vidix_clean_output_area(this);

  vdlSetGrKeys(this->vidix_handler, &this->vidix_grkey);
}


static uint32_t vidix_get_capabilities (vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  return this->capabilities;
}

#ifdef HAVE_FB
static void vidixfb_frame_output_cb(void *user_data, int video_width, int video_height, double video_pixel_aspect, int *dest_x, int *dest_y, int *dest_width, int *dest_height, double *dest_pixel_aspect, int *win_x, int *win_y) {
  vidix_driver_t *this = (vidix_driver_t *) user_data;

  *dest_x            = 0;
  *dest_y            = 0;
  *dest_width        = this->fb_width;
  *dest_height       = this->fb_height;
  *dest_pixel_aspect = 1.0;
  *win_x             = 0;
  *win_y             = 0;
}
#endif

static void vidix_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for vidix */
}

static void vidix_frame_dispose (vo_frame_t *vo_img) {

  vidix_frame_t  *frame = (vidix_frame_t *) vo_img ;

  free_framedata(frame);
  free (frame);
}

static vo_frame_t *vidix_alloc_frame (vo_driver_t *this_gen) {
  /* vidix_driver_t  *this = (vidix_driver_t *) this_gen; */
  vidix_frame_t   *frame ;

  frame = (vidix_frame_t *) calloc(1, sizeof(vidix_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  frame->vo_frame.base[0] = NULL;
  frame->vo_frame.base[1] = NULL;
  frame->vo_frame.base[2] = NULL;

  /*
   * supply required functions
   */

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = vidix_frame_field;
  frame->vo_frame.dispose    = vidix_frame_dispose;

  return (vo_frame_t *) frame;
}


static void vidix_compute_ideal_size (vidix_driver_t *this) {

  _x_vo_scale_compute_ideal_size( &this->sc );

}

/*
 * Configure vidix device
 */

static void vidix_config_playback (vidix_driver_t *this) {

  uint32_t apitch;
  int err,i;

  _x_vo_scale_compute_output_size( &this->sc );

  /* We require that the displayed xoffset and width are even.
   * To prevent displaying more than we're supposed to we round the
   * xoffset up and the width down */
  this->sc.displayed_xoffset = (this->sc.displayed_xoffset+1) & ~1;
  this->sc.displayed_width = this->sc.displayed_width & ~1;

  /* For yv12 source displayed yoffset and height need to be even too */
  if(this->delivered_format == XINE_IMGFMT_YV12) {
    this->sc.displayed_yoffset = (this->sc.displayed_yoffset+1) & ~1;
    this->sc.displayed_height = this->sc.displayed_height & ~1;
  }

  if( this->vidix_started > 0 ) {
    lprintf("video_out_vidix: overlay off\n");
    vdlPlaybackOff(this->vidix_handler);
  }

  memset(&this->vidix_play,0,sizeof(vidix_playback_t));

  if(this->delivered_format == XINE_IMGFMT_YV12 && this->supports_yv12)
    this->vidix_play.fourcc = IMGFMT_YV12;
  else
    this->vidix_play.fourcc = IMGFMT_YUY2;

  this->vidix_play.capability = this->vidix_cap.flags; /* every ;) */
  this->vidix_play.blend_factor = 0; /* for now */
  this->vidix_play.src.x = 0;
  this->vidix_play.src.y = 0;
  this->vidix_play.src.w = this->sc.displayed_width;
  this->vidix_play.src.h = this->sc.displayed_height;
  this->vidix_play.dest.x = this->sc.gui_win_x+this->sc.output_xoffset;
  this->vidix_play.dest.y = this->sc.gui_win_y+this->sc.output_yoffset;
  this->vidix_play.dest.w = this->sc.output_width;
  this->vidix_play.dest.h = this->sc.output_height;
  this->vidix_play.num_frames= this->use_doublebuffer ? NUM_FRAMES : 1;
  this->vidix_play.src.pitch.y = this->vidix_play.src.pitch.u = this->vidix_play.src.pitch.v = 0;

  if((err=vdlConfigPlayback(this->vidix_handler,&this->vidix_play))!=0)
  {
     xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	     "video_out_vidix: Can't configure playback: %s\n",strerror(err));
     this->vidix_started = -1;
     return;
  }

  lprintf("video_out_vidix: dga_addr = %p frame_size = %u frames = %u\n",
	  this->vidix_play.dga_addr, this->vidix_play.frame_size,
	  this->vidix_play.num_frames );

  lprintf("video_out_vidix: offsets[0..2] = %u %u %u\n",
	  this->vidix_play.offsets[0], this->vidix_play.offsets[1],
	  this->vidix_play.offsets[2] );

  lprintf("video_out_vidix: offset.y/u/v = %u/%u/%u\n",
	  this->vidix_play.offset.y, this->vidix_play.offset.u,
	  this->vidix_play.offset.v );

  lprintf("video_out_vidix: src.x/y/w/h = %u/%u/%u/%u\n",
	  this->vidix_play.src.x, this->vidix_play.src.y,
	  this->vidix_play.src.w, this->vidix_play.src.h );

  lprintf("video_out_vidix: dest.x/y/w/h = %u/%u/%u/%u\n",
	  this->vidix_play.dest.x, this->vidix_play.dest.y,
	  this->vidix_play.dest.w, this->vidix_play.dest.h );

  lprintf("video_out_vidix: dest.pitch.y/u/v = %u/%u/%u\n",
	  this->vidix_play.dest.pitch.y, this->vidix_play.dest.pitch.u,
	  this->vidix_play.dest.pitch.v );

  this->vidix_mem = this->vidix_play.dga_addr;

  this->next_frame = 0;

  /* clear every frame with correct address and frame_size */
  for (i = 0; i < this->vidix_play.num_frames; i++)
    memset(this->vidix_mem + this->vidix_play.offsets[i], 0x80,
           this->vidix_play.frame_size);

  switch(this->vidix_play.fourcc) {
    case IMGFMT_YV12:
      apitch = this->vidix_play.dest.pitch.y-1;
      this->dstrides.y = (this->sc.displayed_width + apitch) & ~apitch;
      apitch = this->vidix_play.dest.pitch.v-1;
      this->dstrides.v = (this->sc.displayed_width + apitch) & ~apitch;
      apitch = this->vidix_play.dest.pitch.u-1;
      this->dstrides.u = (this->sc.displayed_width + apitch) & ~apitch;
      break;
    case IMGFMT_YUY2:
      apitch = this->vidix_play.dest.pitch.y-1;
      this->dstrides.y = (this->sc.displayed_width*2 + apitch) & ~apitch;
      break;
    default:
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_vidix: error. (unknown frame format: %04x)\n", this->delivered_format);
 }

  lprintf("video_out_vidix: overlay on\n");
  vdlPlaybackOn(this->vidix_handler);
  this->vidix_started = 1;
}

static void vidix_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    /*
     * (re-) allocate image
     */

      free_framedata(frame);

      frame->width  = width;
      frame->height = height;
      frame->format = format;

      switch(format) {
       case XINE_IMGFMT_YV12:
	 frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	 frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	 frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
         frame->vo_frame.base[1] = malloc(frame->vo_frame.pitches[1] * ((height+1)/2));
	 frame->vo_frame.base[2] = malloc(frame->vo_frame.pitches[2] * ((height+1)/2));
	 break;
       case XINE_IMGFMT_YUY2:
	 frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
	 frame->vo_frame.base[1] = NULL;
	 frame->vo_frame.base[2] = NULL;
	 break;
       default:
	 xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		 "video_out_vidix: error. (unknown frame format: %04x)\n", format);
      }

      if((format == XINE_IMGFMT_YV12 && (frame->vo_frame.base[0] == NULL || frame->vo_frame.base[1] == NULL || frame->vo_frame.base[2] == NULL))
	 || (format == XINE_IMGFMT_YUY2 && frame->vo_frame.base[0] == NULL)) {
	 xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		 "video_out_vidix: error. (framedata allocation failed: out of memory)\n");

	 free_framedata(frame);
      }
   }

  frame->ratio = ratio;
}

static void vidix_overlay_begin (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, int changed) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;

#ifdef HAVE_X11
  this->ovl_changed += changed;

  if( this->ovl_changed && this->xoverlay ) {
    XLockDisplay (this->display);
    x11osd_clear(this->xoverlay);
    XUnlockDisplay (this->display);
  }
#endif

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void vidix_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
#ifdef HAVE_X11
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;

  if( this->ovl_changed && this->xoverlay ) {
    XLockDisplay (this->display);
    x11osd_expose(this->xoverlay);
    XUnlockDisplay (this->display);
  }

  this->ovl_changed = 0;
#endif
}

/*
 *
 */
static void vidix_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;

  if (overlay->rle) {
    if( overlay->unscaled ) {
#ifdef HAVE_X11
      if( this->ovl_changed && this->xoverlay ) {
        XLockDisplay (this->display);
        x11osd_blend(this->xoverlay, overlay);
        XUnlockDisplay (this->display);
      }
#endif
    } else {
      if( frame->format == XINE_IMGFMT_YV12 )
        _x_blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches, &this->alphablend_extra_data);
      else
        _x_blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0], &this->alphablend_extra_data);
    }
  }
}


static int vidix_redraw_needed (vo_driver_t *this_gen) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  int ret = 0;

  if(_x_vo_scale_redraw_needed(&this->sc)) {
    if(this->got_frame_data) {
      vidix_config_playback(this);
      vidix_clean_output_area(this);

      ret = 1;
    }
  }

  return ret;
}


static void vidix_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio)
       || (frame->format != this->delivered_format )
       || (frame->vo_frame.crop_left != this->sc.crop_left)
       || (frame->vo_frame.crop_right != this->sc.crop_right)
       || (frame->vo_frame.crop_top != this->sc.crop_top)
       || (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    lprintf("video_out_vidix: change frame format\n");

    this->sc.delivered_width  = frame->width;
    this->sc.delivered_height = frame->height;
    this->sc.delivered_ratio  = frame->ratio;
    this->delivered_format    = frame->format;

    this->sc.crop_left        = frame->vo_frame.crop_left;
    this->sc.crop_right       = frame->vo_frame.crop_right;
    this->sc.crop_top         = frame->vo_frame.crop_top;
    this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

    vidix_compute_ideal_size( this );
    this->sc.force_redraw = 1;
  }

  /*
   * check if we have to reconfigure vidix because of
   * format/window position change
   */
  this->got_frame_data = 1;
  vidix_redraw_needed(this_gen);

  if(this->vidix_started > 0) {
    write_frame_sfb(this, frame);

    if( this->vidix_play.num_frames > 1 ) {
      vdlPlaybackFrameSelect(this->vidix_handler,this->next_frame);
      this->next_frame=(this->next_frame+1)%this->vidix_play.num_frames;
    }
  }

  frame->vo_frame.free(frame_gen);

  pthread_mutex_unlock(&this->mutex);
}


static int vidix_get_property (vo_driver_t *this_gen, int property) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

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

  lprintf ("video_out_vidix: property #%d = %d\n", property,
	   this->props[property].value);

  return this->props[property].value;
}


static int vidix_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  int err;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if ((value >= this->props[property].min) &&
      (value <= this->props[property].max))
  {
  this->props[property].value = value;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if(value >= XINE_VO_ASPECT_NUM_RATIOS)
      value = this->props[property].value = XINE_VO_ASPECT_AUTO;

    lprintf("video_out_vidix: aspect ratio changed to %s\n",
	    _x_vo_scale_aspect_ratio_name_table[value]);

    this->sc.user_ratio = value;
    vidix_compute_ideal_size (this);
    this->sc.force_redraw = 1;
  }

  if ( property == VO_PROP_ZOOM_X ) {
      this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
  }

  if ( property == VO_PROP_ZOOM_Y ) {
      this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
  }

  if ( property == VO_PROP_HUE ) {
    this->vidix_eq.cap = VEQ_CAP_HUE;
    this->vidix_eq.hue = value;

    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_vidix: can't set hue: %s\n", strerror(err));
  }

  if ( property == VO_PROP_SATURATION ) {
    this->vidix_eq.cap = VEQ_CAP_SATURATION;
    this->vidix_eq.saturation = value;

    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_vidix: can't set saturation: %s\n", strerror(err));
  }

  if ( property == VO_PROP_BRIGHTNESS ) {
    this->vidix_eq.cap = VEQ_CAP_BRIGHTNESS;
    this->vidix_eq.brightness = value;

    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_vidix: can't set brightness: %s\n", strerror(err));
  }

  if ( property == VO_PROP_CONTRAST ) {
    this->vidix_eq.cap = VEQ_CAP_CONTRAST;
    this->vidix_eq.contrast = value;

    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_vidix: can't set contrast: %s\n", strerror(err));
  }
  }

  return value;
}


static void vidix_ckey_callback(vo_driver_t *this_gen, xine_cfg_entry_t *entry) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if(strcmp(entry->key, "video.device.vidix_colour_key_red") == 0) {
    this->vidix_grkey.ckey.red = entry->num_value;
  }

  if(strcmp(entry->key, "video.device.vidix_colour_key_green") == 0) {
    this->vidix_grkey.ckey.green = entry->num_value;
  }

  if(strcmp(entry->key, "video.device.vidix_colour_key_blue") == 0) {
    this->vidix_grkey.ckey.blue = entry->num_value;
  }

  vidix_update_colourkey(this);
  this->sc.force_redraw = 1;
}


static void vidix_db_callback(vo_driver_t *this_gen, xine_cfg_entry_t *entry) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  this->use_doublebuffer = entry->num_value;
  this->sc.force_redraw = 1;
}


static void vidix_rgb_callback(vo_driver_t *this_gen, xine_cfg_entry_t *entry) {
  int err;
  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  this->vidix_eq.cap = VEQ_CAP_RGB_INTENSITY;

  if(!strcmp(entry->key, "video.output.vidix_red_intensity")) {
    this->vidix_eq.red_intensity = entry->num_value;
  } else if(!strcmp(entry->key, "video.output.vidix_green_intensity")) {
    this->vidix_eq.green_intensity = entry->num_value;
  } else if(!strcmp(entry->key, "video.output.vidix_blue_intensity")) {
    this->vidix_eq.blue_intensity = entry->num_value;
  }

  if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_vidix: can't set rgb intensity: %s\n", strerror(err));
}


static void vidix_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) {
    *min = *max = 0;
    return;
  }
  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int vidix_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
  vidix_driver_t     *this = (vidix_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);

  switch (data_type) {

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    lprintf ("video_out_vidix: GUI_DATA_EX_DRAWABLE_CHANGED\n");

    if(this->visual_type == XINE_VISUAL_TYPE_X11) {
#ifdef HAVE_X11
      this->drawable = (Drawable) data;
      XLockDisplay(this->display);
      XFreeGC(this->display, this->gc);
      this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
      if(this->xoverlay)
        x11osd_drawable_changed(this->xoverlay, this->drawable);
      this->ovl_changed = 1;
      XUnlockDisplay(this->display);
#endif
    }
    break;

  case XINE_GUI_SEND_EXPOSE_EVENT:
    lprintf ("video_out_vidix: GUI_DATA_EX_EXPOSE_EVENT\n");
    vidix_clean_output_area(this);
#ifdef HAVE_X11
    XLockDisplay (this->display);
    if(this->xoverlay)
      x11osd_expose(this->xoverlay);

    XSync(this->display, False);
    XUnlockDisplay (this->display);
#endif
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

  return ret;
}

static void vidix_exit (vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if( this->vidix_started > 0 ) {
    vdlPlaybackOff(this->vidix_handler);
  }
  vdlClose(this->vidix_handler);

#ifdef HAVE_X11
  XLockDisplay (this->display);
  XFreeGC(this->display, this->gc);

  if( this->xoverlay )
    x11osd_destroy (this->xoverlay);

  XUnlockDisplay (this->display);
#endif

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

static vidix_driver_t *open_plugin (video_driver_class_t *class_gen) {
  vidix_class_t        *class = (vidix_class_t *) class_gen;
  config_values_t      *config = class->config;
  vidix_driver_t       *this;
  int                   err;

  this = (vidix_driver_t *) calloc(1, sizeof(vidix_driver_t));
  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  pthread_mutex_init (&this->mutex, NULL);

  this->vidix_handler = class->vidix_handler;
  this->vidix_cap = class->vidix_cap;

  _x_vo_scale_init( &this->sc, 1, /*this->vidix_cap.flags & FLAG_UPSCALER,*/ 0, config );

  this->xine              = class->xine;
  this->config            = config;

  this->got_frame_data    = 0;
  this->capabilities      = VO_CAP_CROP | VO_CAP_ZOOM_X | VO_CAP_ZOOM_Y;

  /* Find what equalizer flags are supported */
  if(this->vidix_cap.flags & FLAG_EQUALIZER) {
    if((err = vdlPlaybackGetEq(this->vidix_handler, &this->vidix_eq)) != 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_vidix: couldn't get equalizer capabilities: %s\n", strerror(err));
    } else {
      if(this->vidix_eq.cap & VEQ_CAP_BRIGHTNESS) {
        this->capabilities |= VO_CAP_BRIGHTNESS;
        this->props[VO_PROP_BRIGHTNESS].value = 0;
        this->props[VO_PROP_BRIGHTNESS].min = -1000;
        this->props[VO_PROP_BRIGHTNESS].max = 1000;
     }

      if(this->vidix_eq.cap & VEQ_CAP_CONTRAST) {
        this->capabilities |= VO_CAP_CONTRAST;
        this->props[VO_PROP_CONTRAST].value = 0;
        this->props[VO_PROP_CONTRAST].min = -1000;
        this->props[VO_PROP_CONTRAST].max = 1000;
      }

      if(this->vidix_eq.cap & VEQ_CAP_SATURATION) {
        this->capabilities |= VO_CAP_SATURATION;
        this->props[VO_PROP_SATURATION].value = 0;
        this->props[VO_PROP_SATURATION].min = -1000;
        this->props[VO_PROP_SATURATION].max = 1000;
      }

      if(this->vidix_eq.cap & VEQ_CAP_HUE) {
        this->capabilities |= VO_CAP_HUE;
        this->props[VO_PROP_HUE].value = 0;
        this->props[VO_PROP_HUE].min = -1000;
        this->props[VO_PROP_HUE].max = 1000;
      }

      if(this->vidix_eq.cap & VEQ_CAP_RGB_INTENSITY) {
        this->vidix_eq.red_intensity = config->register_range(config,
          "video.output.vidix_red_intensity", 0, -1000, 1000,
          _("red intensity"), _("The intensity of the red colour components."), 10,
          (void*) vidix_rgb_callback, this);

        this->vidix_eq.green_intensity = config->register_range(config,
          "video.output.vidix_green_intensity", 0, -1000, 1000,
          _("green intensity"), _("The intensity of the green colour components."), 10,
          (void*) vidix_rgb_callback, this);

        this->vidix_eq.blue_intensity = config->register_range(config,
          "video.output.vidix_blue_intensity", 0, -1000, 1000,
          _("blue intensity"), _("The intensity of the blue colour components."), 10,
          (void*) vidix_rgb_callback, this);

       if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)))
         xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		 "video_out_vidix: can't set rgb intensity: %s\n", strerror(err));
      }
    }
  }

  /* Configuration for double buffering */
  this->use_doublebuffer = config->register_bool(config,
    "video.device.vidix_double_buffer", 1, _("enable double buffering"),
    _("Double buffering will synchronize the update of the video image to the repainting of the entire "
      "screen (\"vertical retrace\"). This eliminates flickering and tearing artifacts, but will use "
      "more graphics memory."), 20,
    (void*) vidix_db_callback, this);

  /* Set up remaining props */
  this->props[VO_PROP_ASPECT_RATIO].value = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ASPECT_RATIO].min = 0;
  this->props[VO_PROP_ASPECT_RATIO].max = XINE_VO_ASPECT_NUM_RATIOS;

  this->props[VO_PROP_ZOOM_X].value = 100;
  this->props[VO_PROP_ZOOM_X].min = XINE_VO_ZOOM_MIN;
  this->props[VO_PROP_ZOOM_X].max = XINE_VO_ZOOM_MAX;

  this->props[VO_PROP_ZOOM_Y].value = 100;
  this->props[VO_PROP_ZOOM_Y].min = XINE_VO_ZOOM_MIN;
  this->props[VO_PROP_ZOOM_Y].max = XINE_VO_ZOOM_MAX;

  this->vo_driver.get_capabilities     = vidix_get_capabilities;
  this->vo_driver.alloc_frame          = vidix_alloc_frame;
  this->vo_driver.update_frame_format  = vidix_update_frame_format;
  this->vo_driver.overlay_begin        = vidix_overlay_begin;
  this->vo_driver.overlay_blend        = vidix_overlay_blend;
  this->vo_driver.overlay_end          = vidix_overlay_end;
  this->vo_driver.display_frame        = vidix_display_frame;
  this->vo_driver.get_property         = vidix_get_property;
  this->vo_driver.set_property         = vidix_set_property;
  this->vo_driver.get_property_min_max = vidix_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vidix_gui_data_exchange;
  this->vo_driver.dispose              = vidix_exit;
  this->vo_driver.redraw_needed        = vidix_redraw_needed;

  return this;
}

static void query_fourccs (vidix_driver_t *this) {
  vidix_fourcc_t        vidix_fourcc;
  int                   err;

  /* Detect if YUY2 is supported */
  memset(&vidix_fourcc, 0, sizeof(vidix_fourcc_t));
  vidix_fourcc.fourcc = IMGFMT_YUY2;
  vidix_fourcc.depth = this->depth;

  if((err = vdlQueryFourcc(this->vidix_handler, &vidix_fourcc)) == 0) {
    this->capabilities |= VO_CAP_YUY2;
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("video_out_vidix: adaptor supports the yuy2 format\n"));
  }

  /* Detect if YV12 is supported - we always support yv12 but we need
     to know if we have to convert */
  this->capabilities |= VO_CAP_YV12;
  vidix_fourcc.fourcc = IMGFMT_YV12;

  if((err = vdlQueryFourcc(this->vidix_handler, &vidix_fourcc)) == 0) {
    this->supports_yv12 = 1;
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("video_out_vidix: adaptor supports the yv12 format\n"));
  } else
    this->supports_yv12 = 0;
}

static void *init_class (xine_t *xine, void *visual_gen) {
  vidix_class_t        *this;
  int                   err;

  this = (vidix_class_t *) calloc(1, sizeof(vidix_class_t));
  if (!this)
    return NULL;

  if(vdlGetVersion() != VIDIX_VERSION)
  {
    xprintf(xine, XINE_VERBOSITY_LOG,
	    _("video_out_vidix: You have wrong version of VIDIX library\n"));
    free(this);
    return NULL;
  }
  this->vidix_handler = vdlOpen((XINE_PLUGINDIR"/vidix/"), NULL, TYPE_OUTPUT, 0);
  if(this->vidix_handler == NULL)
  {
    xprintf(xine, XINE_VERBOSITY_LOG,
	    _("video_out_vidix: Couldn't find working VIDIX driver\n"));
    free(this);
    return NULL;
  }
  if((err=vdlGetCapability(this->vidix_handler,&this->vidix_cap)) != 0)
  {
    xprintf(xine, XINE_VERBOSITY_DEBUG,
	    "video_out_vidix: Couldn't get capability: %s\n",strerror(err));
    free(this);
    return NULL;
  }

  xprintf(xine, XINE_VERBOSITY_LOG,
	  _("video_out_vidix: using driver: %s by %s\n"), this->vidix_cap.name, this->vidix_cap.author);

  this->xine              = xine;
  this->config            = xine->config;

  return this;
}

#ifdef HAVE_X11
static vo_driver_t *vidix_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  vidix_driver_t       *this   = open_plugin(class_gen);
  config_values_t      *config = this->config;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes     window_attributes;

  this->visual_type       = XINE_VISUAL_TYPE_X11;

  this->display           = visual->display;
  this->screen            = visual->screen;
  this->drawable          = visual->d;
  this->gc                = XCreateGC(this->display, this->drawable, 0, NULL);
  this->xoverlay          = NULL;
  this->ovl_changed       = 0;

  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->sc.gui_width      = window_attributes.width;
  this->sc.gui_height     = window_attributes.height;
  this->depth             = window_attributes.depth;

  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  /* We'll assume all drivers support colour keying (which they do
     at the moment) */
  this->vidix_grkey.ckey.op = CKEY_TRUE;

  /* Colour key components */
  this->vidix_grkey.ckey.red = config->register_range(config,
    "video.device.vidix_colour_key_red", 255, 0, 255,
    _("video overlay colour key red component"),
    _("The colour key is used to tell the graphics card where to overlay the video image. "
      "Try different values, if you experience windows becoming transparent."), 20,
    (void*) vidix_ckey_callback, this);

  this->vidix_grkey.ckey.green = config->register_range(config,
    "video.device.vidix_colour_key_green", 0, 0, 255,
    _("video overlay colour key green component"),
    _("The colour key is used to tell the graphics card where to overlay the video image. "
      "Try different values, if you experience windows becoming transparent."), 20,
    (void*) vidix_ckey_callback, this);

  this->vidix_grkey.ckey.blue = config->register_range(config,
    "video.device.vidix_colour_key_blue", 255, 0, 255,
    _("video overlay colour key blue component"),
    _("The colour key is used to tell the graphics card where to overlay the video image. "
      "Try different values, if you experience windows becoming transparent."), 20,
    (void*) vidix_ckey_callback, this);

  vidix_update_colourkey(this);

  query_fourccs(this);

  XLockDisplay (this->display);
  if(this->colourkey) {
    this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                    this->drawable, X11OSD_COLORKEY);
    if(this->xoverlay)
      x11osd_colorkey(this->xoverlay, this->colourkey, &this->sc);
  } else {
    this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                    this->drawable, X11OSD_SHAPED);
  }
  XUnlockDisplay (this->display);

  if( this->xoverlay )
    this->capabilities |= VO_CAP_UNSCALED_OVERLAY;

  return &this->vo_driver;
}

static void *vidix_init_class (xine_t *xine, void *visual_gen) {

  vidix_class_t *this = init_class (xine, visual_gen);

  if(this) {
    this->driver_class.open_plugin     = vidix_open_plugin;
    this->driver_class.identifier      = "vidix";
    this->driver_class.description     = N_("xine video output plugin using libvidix for x11");
    this->driver_class.dispose         = default_video_driver_class_dispose;
  }

  return this;
}

static const vo_info_t vo_info_vidix = {
  2,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};
#endif

#ifdef HAVE_FB
static vo_driver_t *vidixfb_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  vidix_driver_t           *this = open_plugin(class_gen);
  config_values_t          *config = this->config;
  char                     *device;
  int                       fd;
  struct fb_var_screeninfo  fb_var;

  this->visual_type = XINE_VISUAL_TYPE_FB;

  /* Register config option for fb device */
  device = config->register_filename(config, "video.device.vidixfb_device", "/dev/fb0", XINE_CONFIG_STRING_IS_DEVICE_NAME,
    _("framebuffer device name"),
    _("Specifies the file name for the framebuffer device to be used.\n"
      "This setting is security critical, because when changed to a different file, xine "
      "can be used to fill this file with arbitrary content. So you should be careful that "
      "the value you enter really is a proper framebuffer device."),
    XINE_CONFIG_SECURITY, NULL, NULL);

  /* Open fb device for reading */
  if((fd = xine_open_cloexec("/dev/fb0", O_RDONLY)) < 0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_vidix: unable to open frame buffer device \"%s\": %s\n", device, strerror(errno));
    return NULL;
  }

  /* Read screen info */
  if(ioctl(fd, FBIOGET_VSCREENINFO, &fb_var) != 0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_vidix: error in ioctl FBIOGET_VSCREENINFO: %s", strerror(errno));
    close(fd);
    return NULL;
  }

  /* Store screen bpp and dimensions */
  this->depth = fb_var.bits_per_pixel;
  this->fb_width = fb_var.xres;
  this->fb_height = fb_var.yres;

  /* Close device */
  close(fd);

  this->sc.frame_output_cb   = vidixfb_frame_output_cb;
  this->sc.user_data         = this;

  /* Make sure colour keying is turned off */
  this->vidix_grkey.ckey.op = CKEY_FALSE;
  vdlSetGrKeys(this->vidix_handler, &this->vidix_grkey);

  query_fourccs(this);

  return &this->vo_driver;
}

static void *vidixfb_init_class (xine_t *xine, void *visual_gen) {

  vidix_class_t *this = init_class (xine, visual_gen);

  if(this) {
    this->driver_class.open_plugin     = vidixfb_open_plugin;
    this->driver_class.identifier      = "vidixfb";
    this->driver_class.description     = N_("xine video output plugin using libvidix for linux frame buffer");
    this->driver_class.dispose         = default_video_driver_class_dispose;
  }

  return this;
}

static const vo_info_t vo_info_vidixfb = {
  2,                    /* priority    */
  XINE_VISUAL_TYPE_FB   /* visual type */
};
#endif

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
#ifdef HAVE_X11
  { PLUGIN_VIDEO_OUT, 22, "vidix", XINE_VERSION_CODE, &vo_info_vidix, vidix_init_class },
#endif
#ifdef HAVE_FB
  { PLUGIN_VIDEO_OUT, 22, "vidixfb", XINE_VERSION_CODE, &vo_info_vidixfb, vidixfb_init_class },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
