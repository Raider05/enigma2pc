
/*
 * Copyright (C) 2007-2013 the xine project
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * video_out_raw.c, a video output plugin to pass raw data to frontend
 *
 * Written by Christophe Thommeret <hftom@free.fr>,
 * based on others' video output plugins.
 *
 */

/* #define LOG */
#define LOG_MODULE "video_out_raw"

/* Allow frontend some time to render frames
*  However, frontends are strongly advised to render synchronously */
#define NUM_FRAMES_BACKLOG   4
#define BYTES_PER_PIXEL 3


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <xine.h>
#include <xine/video_out.h>

#include <xine/xine_internal.h>
#include "yuv2rgb.h"
#include <xine/xineutils.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;
  void              *chunk[4]; /* mem alloc by xmalloc_aligned           */
  uint8_t           *rgb, *rgb_dst;
  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */

} raw_frame_t;

typedef struct {
  vo_driver_t        vo_driver;

  void  *user_data;

  void (*raw_output_cb) (void *user_data, int format,
    int frame_width, int frame_height, double frame_aspect,
    void *data0, void *data1, void *data2);

  void (*raw_overlay_cb) (void *user_data, int num_ovl,
    raw_overlay_t *overlays_p);

  int ovl_changed;
  raw_overlay_t overlays[XINE_VORAW_MAX_OVL];
  yuv2rgb_t *ovl_yuv2rgb;

  int doYV12;
  int doYUY2;
  yuv2rgb_factory_t *yuv2rgb_factory;
  /* Frame state */
  raw_frame_t    *frame[NUM_FRAMES_BACKLOG];
  xine_t            *xine;
} raw_driver_t;


typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
} raw_class_t;



static void raw_overlay_clut_yuv2rgb(raw_driver_t  *this, vo_overlay_t *overlay, raw_frame_t *frame)
{
  int i;
  uint32_t *rgb;

  if (!overlay->rgb_clut) {
    rgb = overlay->color;
    for (i = sizeof (overlay->color) / sizeof (overlay->color[0]); i > 0; i--) {
      clut_t *yuv = (clut_t *)rgb;
      *rgb++ = this->ovl_yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, yuv->y, yuv->cb, yuv->cr);
    }
    overlay->rgb_clut++;
  }

  if (!overlay->hili_rgb_clut) {
    rgb = overlay->hili_color;
    for (i = sizeof (overlay->color) / sizeof (overlay->color[0]); i > 0; i--) {
      clut_t *yuv = (clut_t *)rgb;
      *rgb++ = this->ovl_yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, yuv->y, yuv->cb, yuv->cr);
    }
    overlay->hili_rgb_clut++;
  }
}


static int raw_process_ovl( raw_driver_t *this_gen, vo_overlay_t *overlay )
{
  raw_overlay_t *ovl = &this_gen->overlays[this_gen->ovl_changed-1];

  if ( overlay->width<=0 || overlay->height<=0 )
    return 0;

  if ( (overlay->width*overlay->height)!=(ovl->ovl_w*ovl->ovl_h) )
    ovl->ovl_rgba = (uint8_t*)realloc( ovl->ovl_rgba, overlay->width*overlay->height*4 );
  ovl->ovl_w = overlay->width;
  ovl->ovl_h = overlay->height;
  ovl->ovl_x = overlay->x;
  ovl->ovl_y = overlay->y;

  int num_rle = overlay->num_rle;
  rle_elem_t *rle = overlay->rle;
  uint8_t *rgba = ovl->ovl_rgba;
  clut_t *low_colors = (clut_t*)overlay->color;
  clut_t *hili_colors = (clut_t*)overlay->hili_color;
  uint8_t *low_trans = overlay->trans;
  uint8_t *hili_trans = overlay->hili_trans;
  clut_t *colors;
  uint8_t *trans;
  uint8_t alpha;
  int rlelen = 0;
  uint8_t clr = 0;
  int i, pos=0, x, y;

  while ( num_rle>0 ) {
    x = pos%ovl->ovl_w;
    y = pos/ovl->ovl_w;
    if ( (x>=overlay->hili_left && x<=overlay->hili_right) && (y>=overlay->hili_top && y<=overlay->hili_bottom) ) {
	colors = hili_colors;
	trans = hili_trans;
    }
    else {
	colors = low_colors;
	trans = low_trans;
    }
    rlelen = rle->len;
    clr = rle->color;
    alpha = trans[clr];
    for ( i=0; i<rlelen; ++i ) {
      if ( alpha == 0 ) {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
      }
      else {
        rgba[0] = colors[clr].y;
        rgba[1] = colors[clr].cr;
        rgba[2] = colors[clr].cb;
        rgba[3] = alpha*255/15;
      }
      rgba+= 4;
	++pos;
    }
    ++rle;
    --num_rle;
  }
  return 1;
}


static void raw_overlay_begin (vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  raw_driver_t  *this = (raw_driver_t *) this_gen;

  if ( !changed )
	return;

  ++this->ovl_changed;
}


static void raw_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  raw_driver_t  *this = (raw_driver_t *) this_gen;
  raw_frame_t *frame = (raw_frame_t *) frame_gen;

  if ( !this->ovl_changed || this->ovl_changed>XINE_VORAW_MAX_OVL )
    return;

  if (overlay->rle) {
    if (!overlay->rgb_clut || !overlay->hili_rgb_clut)
      raw_overlay_clut_yuv2rgb (this, overlay, frame);
    if ( raw_process_ovl( this, overlay ) )
      ++this->ovl_changed;
  }
}


static void raw_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img)
{
  raw_driver_t  *this = (raw_driver_t *) this_gen;

  if ( !this->ovl_changed )
    return;

  this->raw_overlay_cb( this->user_data, this->ovl_changed-1, this->overlays );

  this->ovl_changed = 0;
}


static void raw_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src)
{
  raw_frame_t  *frame = (raw_frame_t *) vo_img ;

  vo_img->proc_called = 1;
  if (! frame->rgb_dst)
      return;

  if( frame->vo_frame.crop_left || frame->vo_frame.crop_top ||
      frame->vo_frame.crop_right || frame->vo_frame.crop_bottom )
  {
    /* TODO: ?!? */
    return;
  }

  if (frame->format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst, src[0], src[1], src[2]);
  else
    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst, src[0]);
}



static void raw_frame_field (vo_frame_t *vo_img, int which_field)
{
  raw_frame_t  *frame = (raw_frame_t *) vo_img ;
  raw_driver_t *this = (raw_driver_t *) vo_img->driver;

  if ( frame->format==XINE_IMGFMT_YV12 && this->doYV12 ) {
    frame->rgb_dst = 0;
    return;
  }
  else if ( frame->format==XINE_IMGFMT_YUY2 && this->doYUY2 ) {
    frame->rgb_dst = 0;
    return;
  }

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb + frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  }

  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
}



static void raw_frame_dispose (vo_frame_t *vo_img)
{
  raw_frame_t  *frame = (raw_frame_t *) vo_img ;

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);
  av_free (frame->rgb);
  free (frame);
}



static vo_frame_t *raw_alloc_frame (vo_driver_t *this_gen)
{
  raw_frame_t  *frame;
  raw_driver_t *this = (raw_driver_t *) this_gen;

  frame = (raw_frame_t *) calloc(1, sizeof(raw_frame_t));

  if (!frame)
    return NULL;

  frame->vo_frame.base[0] = frame->vo_frame.base[1] = frame->vo_frame.base[2] = frame->rgb = NULL;
  frame->width = frame->height = frame->format = frame->flags = 0;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_slice = raw_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = raw_frame_field;
  frame->vo_frame.dispose    = raw_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  /*
   * colorspace converter for this frame
   */
  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

  return (vo_frame_t *) frame;
}



static void raw_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
      uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  raw_frame_t   *frame = (raw_frame_t *) frame_gen;

  /* Check frame size and format and reallocate if necessary */
  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)
      || (frame->flags  != flags)) {
/*     lprintf ("updating frame to %d x %d (ratio=%g, format=%08x)\n", width, height, ratio, format); */

    /* (re-) allocate render space */
    av_free (frame->vo_frame.base[0]);
    av_free (frame->vo_frame.base[1]);
    av_free (frame->vo_frame.base[2]);
    av_free (frame->rgb);

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz (frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz (frame->vo_frame.pitches[2] * ((height+1)/2));
    } else {
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = NULL;
      frame->vo_frame.base[2] = NULL;
    }
    frame->rgb = av_mallocz (BYTES_PER_PIXEL*width*height);

    /* set up colorspace converter */
    switch (flags & VO_BOTH_FIELDS) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 2*frame->vo_frame.pitches[0],
				 2*frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width * 2);
      break;
    case VO_BOTH_FIELDS:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 frame->vo_frame.pitches[0],
				 frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width);
      break;
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->flags = flags;

    raw_frame_field ((vo_frame_t *)frame, flags);
  }

  frame->ratio = ratio;
}



static int raw_redraw_needed (vo_driver_t *this_gen)
{
  return 0;
}



static void raw_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  raw_driver_t  *this  = (raw_driver_t *) this_gen;
  raw_frame_t   *frame = (raw_frame_t *) frame_gen;
  int i;

  if (this->frame[NUM_FRAMES_BACKLOG-1]) {
    this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame.free (&this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame);
  }
  for (i = NUM_FRAMES_BACKLOG-1; i > 0; i--)
    this->frame[i] = this->frame[i-1];
  this->frame[0] = frame;

  if ( frame->rgb_dst ) {
    this->raw_output_cb( this->user_data, XINE_VORAW_RGB, frame->width, frame->height, frame->ratio, frame->rgb, 0, 0 );
  }
  else if ( frame->format==XINE_IMGFMT_YV12 ) {
    this->raw_output_cb( this->user_data, XINE_VORAW_YV12, frame->width, frame->height, frame->ratio, frame->vo_frame.base[0],
      frame->vo_frame.base[1], frame->vo_frame.base[2] );
  }
  else {
    this->raw_output_cb( this->user_data, XINE_VORAW_YUY2, frame->width, frame->height, frame->ratio, frame->vo_frame.base[0], 0, 0 );
  }
}



static int raw_get_property (vo_driver_t *this_gen, int property)
{
  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    return XINE_VO_ASPECT_AUTO;
  case VO_PROP_MAX_NUM_FRAMES:
    return 15;
  case VO_PROP_BRIGHTNESS:
    return 0;
  case VO_PROP_CONTRAST:
    return 128;
  case VO_PROP_SATURATION:
    return 128;
  case VO_PROP_WINDOW_WIDTH:
    return 0;
  case VO_PROP_WINDOW_HEIGHT:
    return 0;
  default:
    return 0;
  }
}



static int raw_set_property (vo_driver_t *this_gen, int property, int value)
{
  return value;
}



static void raw_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max)
{
  *min = 0;
  *max = 0;
}



static int raw_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data)
{
  return 0;
}



static uint32_t raw_get_capabilities (vo_driver_t *this_gen)
{
  uint32_t capabilities = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP;
  return capabilities;
}



static void raw_dispose (vo_driver_t *this_gen)
{
  raw_driver_t *this = (raw_driver_t *) this_gen;
  int i;

  for (i = 0; i < NUM_FRAMES_BACKLOG; i++)
    if (this->frame[i])
      this->frame[i]->vo_frame.dispose (&this->frame[i]->vo_frame);

  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i )
    free( this->overlays[i].ovl_rgba );

  free (this);
}



static vo_driver_t *raw_open_plugin (video_driver_class_t *class_gen, const void *visual_gen)
{
  raw_class_t       *class   = (raw_class_t *) class_gen;
  raw_visual_t         *visual  = (raw_visual_t *) visual_gen;
  raw_driver_t      *this;
  int i;

  this = (raw_driver_t *) calloc(1, sizeof(raw_driver_t));

  if (!this)
    return NULL;

  this->raw_output_cb  = visual->raw_output_cb;
  this->user_data        = visual->user_data;
  this->xine                = class->xine;
  this->raw_overlay_cb = visual->raw_overlay_cb;
  this->doYV12          = visual->supported_formats&XINE_VORAW_YV12;
  this->doYUY2          = visual->supported_formats&XINE_VORAW_YUY2;

  this->vo_driver.get_capabilities     = raw_get_capabilities;
  this->vo_driver.alloc_frame          = raw_alloc_frame;
  this->vo_driver.update_frame_format  = raw_update_frame_format;
  this->vo_driver.overlay_begin        = raw_overlay_begin;
  this->vo_driver.overlay_blend        = raw_overlay_blend;
  this->vo_driver.overlay_end          = raw_overlay_end;
  this->vo_driver.display_frame        = raw_display_frame;
  this->vo_driver.get_property         = raw_get_property;
  this->vo_driver.set_property         = raw_set_property;
  this->vo_driver.get_property_min_max = raw_get_property_min_max;
  this->vo_driver.gui_data_exchange    = raw_gui_data_exchange;
  this->vo_driver.dispose              = raw_dispose;
  this->vo_driver.redraw_needed        = raw_redraw_needed;

  this->yuv2rgb_factory = yuv2rgb_factory_init (MODE_24_BGR, 1, NULL); /* converts to rgb */

  for (i = 0; i < NUM_FRAMES_BACKLOG; i++)
    this->frame[i] = 0;

  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    this->overlays[i].ovl_w = this->overlays[i].ovl_h = 2;
    this->overlays[i].ovl_rgba = (uint8_t*)malloc(2*2*4);
    this->overlays[i].ovl_x = this->overlays[i].ovl_y = 0;
  }
  this->ovl_changed = 0;

  /* we have to use a second converter for overlays
  *  because "MODE_24_BGR, 1 (swap)" breaks overlays conversion */
  yuv2rgb_factory_t *factory = yuv2rgb_factory_init (MODE_24_BGR, 0, NULL);
  this->ovl_yuv2rgb = factory->create_converter( factory );
  factory->dispose( factory );

  return &this->vo_driver;
}

/*
 * class functions
 */

static void *raw_init_class (xine_t *xine, void *visual_gen)
{
  raw_class_t *this = (raw_class_t *) calloc(1, sizeof(raw_class_t));

  this->driver_class.open_plugin     = raw_open_plugin;
  this->driver_class.identifier      = "raw";
  this->driver_class.description     = _("xine video output plugin passing raw data to supplied callback");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;

  return this;
}



static const vo_info_t vo_info_raw = {
  7,                    /* priority    */
  XINE_VISUAL_TYPE_RAW  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "raw", XINE_VERSION_CODE, &vo_info_raw, raw_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
