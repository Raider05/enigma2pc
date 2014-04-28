/*
 * Copyright (C) 2000-2012 the xine project and Fredrik Noring
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
 */

/**
 * @file
 * @brief Frame buffer xine driver
 *
 * @author Miguel Freitas
 *
 * @author Fredrik Noring <noring@nocrew.org>:
 *         Zero copy buffers and clean up.
 *
 * @author Aaron Holtzman <aholtzma@ess.engr.uvic.ca>:
 *         Based on xine's video_out_xshm.c, based on mpeg2dec code from
 *
 * @author Geert Uytterhoeven and Chris Lawrence:
 *         Ideas from ppmtofb - Display P?M graphics on framebuffer devices.
 *
 * @note Use this with fbxine.
 *
 * @todo VT Switching (configurable)
 */

#define RECOMMENDED_NUM_BUFFERS  5
#define MAXIMUM_NUM_BUFFERS     25

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <pthread.h>
#include <netinet/in.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#define LOG_MODULE "video_out_fb"
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

typedef struct fb_frame_s
{
  vo_frame_t         vo_frame;

  int                format;
  int                flags;

  vo_scale_t         sc;

  void              *chunk[3]; /* mem alloc by xmalloc_aligned           */

  yuv2rgb_t         *yuv2rgb;  /* yuv2rgb converter for this frame */
  uint8_t           *rgb_dst;
  int                yuv_stride;

  int                bytes_per_line;

  uint8_t*           video_mem;            /* mmapped video memory */
  uint8_t*           data;
  int                yoffset;

  struct fb_driver_s *this;
} fb_frame_t;

typedef struct fb_driver_s
{
  vo_driver_t        vo_driver;

  int                fd;
  int                mem_size;
  uint8_t*           video_mem_base;       /* mmapped video memory */

  int                depth, bpp, bytes_per_pixel;

  int                total_num_native_buffers;
  int                used_num_buffers;

  int                yuv2rgb_mode;
  int                yuv2rgb_swap;
  int                yuv2rgb_brightness;
  int                yuv2rgb_contrast;
  int                yuv2rgb_saturation;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  vo_overlay_t      *overlay;

  /* size / aspect ratio calculations */
  vo_scale_t         sc;

  int                fb_bytes_per_line;

  fb_frame_t        *cur_frame, *old_frame;

  struct fb_var_screeninfo fb_var;
  struct fb_fix_screeninfo fb_fix;

  int                use_zero_copy;
  xine_t            *xine;

  alphablend_t       alphablend_extra_data;
} fb_driver_t;

typedef struct
{
  video_driver_class_t driver_class;
  config_values_t     *config;
  xine_t              *xine;
} fb_class_t;

static uint32_t fb_get_capabilities(vo_driver_t *this_gen)
{
  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST | VO_CAP_SATURATION;
}

static void fb_frame_proc_slice(vo_frame_t *vo_img, uint8_t **src)
{
  fb_frame_t *frame = (fb_frame_t *)vo_img ;

  vo_img->proc_called = 1;

  if( frame->vo_frame.crop_left || frame->vo_frame.crop_top ||
      frame->vo_frame.crop_right || frame->vo_frame.crop_bottom )
  {
    /* we don't support crop, so don't even waste cpu cycles.
     * cropping will be performed by video_out.c
     */
    return;
  }

  if(frame->format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun(frame->yuv2rgb, frame->rgb_dst,
				 src[0], src[1], src[2]);
  else
    frame->yuv2rgb->yuy22rgb_fun(frame->yuv2rgb,
				 frame->rgb_dst, src[0]);
}

static void fb_frame_field(vo_frame_t *vo_img, int which_field)
{
  fb_frame_t *frame = (fb_frame_t *)vo_img ;

  switch(which_field)
  {
  case VO_TOP_FIELD:
      frame->rgb_dst    = frame->data;
    break;

  case VO_BOTTOM_FIELD:
      frame->rgb_dst    = frame->data +
			  frame->bytes_per_line ;
    break;

  case VO_BOTH_FIELDS:
      frame->rgb_dst    = frame->data;
    break;
  }
  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
}

static void fb_frame_dispose(vo_frame_t *vo_img)
{
  fb_frame_t *frame = (fb_frame_t *)vo_img;

  if(!frame->this->use_zero_copy)
     free(frame->data);
  free(frame);
}

static vo_frame_t *fb_alloc_frame(vo_driver_t *this_gen)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;
  fb_frame_t *frame;

  if(this->use_zero_copy &&
     this->total_num_native_buffers <= this->used_num_buffers)
    return 0;

  frame = calloc(1, sizeof(fb_frame_t));
  if(!frame)
    return NULL;

  memcpy(&frame->sc, &this->sc, sizeof(vo_scale_t));

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  /* supply required functions */
  frame->vo_frame.proc_slice = fb_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = fb_frame_field;
  frame->vo_frame.dispose    = fb_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  frame->this = this;

  /* colorspace converter for this frame */
  frame->yuv2rgb =
    this->yuv2rgb_factory->create_converter(this->yuv2rgb_factory);

  if(this->use_zero_copy)
  {
    frame->yoffset = this->used_num_buffers * this->fb_var.yres;
    frame->video_mem = this->video_mem_base +
		       this->used_num_buffers * this->fb_var.yres *
		       this->fb_bytes_per_line;

    memset(frame->video_mem, 0,
	   this->fb_var.yres * this->fb_bytes_per_line);
  }
  else
    frame->video_mem = this->video_mem_base;

  this->used_num_buffers++;

  return (vo_frame_t *)frame;
}

static void fb_compute_ideal_size(fb_driver_t *this, fb_frame_t *frame)
{
  _x_vo_scale_compute_ideal_size(&frame->sc);
}

static void fb_compute_rgb_size(fb_driver_t *this, fb_frame_t *frame)
{
  _x_vo_scale_compute_output_size(&frame->sc);

  /* avoid problems in yuv2rgb */
  if(frame->sc.output_height < (frame->sc.delivered_height+15) >> 4)
    frame->sc.output_height = (frame->sc.delivered_height+15) >> 4;

  if (frame->sc.output_width < 8)
    frame->sc.output_width = 8;

  /* yuv2rgb_mlib needs an even YUV2 width */
  if (frame->sc.output_width & 1)
    frame->sc.output_width++;

  lprintf("frame source %d x %d => screen output %d x %d%s\n",
	  frame->sc.delivered_width, frame->sc.delivered_height,
	  frame->sc.output_width, frame->sc.output_height,
	  (frame->sc.delivered_width != frame->sc.output_width ||
	   frame->sc.delivered_height != frame->sc.output_height ?
	   ", software scaling" : ""));
}

static void setup_colorspace_converter(fb_frame_t *frame, int flags)
{
  switch(flags)
  {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->
	configure(frame->yuv2rgb,
		  frame->sc.delivered_width,
		  frame->sc.delivered_height,
		  2 * frame->vo_frame.pitches[0],
		  2 * frame->vo_frame.pitches[1],
		  frame->sc.output_width,
		  frame->sc.output_height,
		  frame->bytes_per_line * 2);
      frame->yuv_stride = frame->bytes_per_line * 2;
      break;

    case VO_BOTH_FIELDS:
      frame->yuv2rgb->
	configure(frame->yuv2rgb,
		  frame->sc.delivered_width,
		  frame->sc.delivered_height,
		  frame->vo_frame.pitches[0],
		  frame->vo_frame.pitches[1],
		  frame->sc.output_width,
		  frame->sc.output_height,
		  frame->bytes_per_line);
      frame->yuv_stride = frame->bytes_per_line;
      break;
  }
}

static void frame_reallocate(fb_driver_t *this, fb_frame_t *frame,
			     uint32_t width, uint32_t height, int format)
{
  av_freep(&frame->vo_frame.base[0]);
  av_freep(&frame->vo_frame.base[1]);
  av_freep(&frame->vo_frame.base[2]);

  if(this->use_zero_copy)
  {
    frame->data = frame->video_mem +
		  frame->sc.output_yoffset*this->fb_bytes_per_line+
		  frame->sc.output_xoffset*this->bytes_per_pixel;
  }
  else
  {
    free(frame->data);
    frame->data = calloc(frame->sc.output_width *
			       frame->sc.output_height,
			       this->bytes_per_pixel);
  }

  if(format == XINE_IMGFMT_YV12)
  {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);

      frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz(frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz(frame->vo_frame.pitches[2] * ((height+1)/2));
  }
  else
  {
    frame->vo_frame.pitches[0] = 8 * ((width + 3) / 4);

    frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
  }
}

static void fb_update_frame_format(vo_driver_t *this_gen,
				   vo_frame_t *frame_gen,
				   uint32_t width, uint32_t height,
				   double ratio, int format, int flags)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;
  fb_frame_t *frame = (fb_frame_t *)frame_gen;

  flags &= VO_BOTH_FIELDS;

  /* Find out if we need to adapt this frame. */
  if (width != frame->sc.delivered_width    ||
      height != frame->sc.delivered_height  ||
      ratio != frame->sc.delivered_ratio    ||
      flags != frame->flags                 ||
      format != frame->format               ||
      this->sc.user_ratio != frame->sc.user_ratio)
  {
    lprintf("frame format (from decoder) has changed => adapt\n");

    frame->sc.delivered_width  = width;
    frame->sc.delivered_height = height;
    frame->sc.delivered_ratio  = ratio;
    frame->flags               = flags;
    frame->format              = format;
    frame->sc.user_ratio       = this->sc.user_ratio;

    fb_compute_ideal_size(this, frame);
    fb_compute_rgb_size(this, frame);

    frame_reallocate(this, frame, width, height, format);

    if(this->use_zero_copy)
      frame->bytes_per_line = this->fb_bytes_per_line;
    else
      frame->bytes_per_line = frame->sc.output_width *
			      this->bytes_per_pixel;

    setup_colorspace_converter(frame, flags);
  }

  fb_frame_field(frame_gen, flags);
}

static void fb_overlay_clut_yuv2rgb(fb_driver_t *this,
				    vo_overlay_t *overlay, fb_frame_t *frame)
{
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

static void fb_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
			      vo_overlay_t *overlay)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;
  fb_frame_t *frame = (fb_frame_t *)frame_gen;

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;

  /* Alpha Blend here */
  if(overlay->rle)
  {
    if(!overlay->rgb_clut || !overlay->hili_rgb_clut)
       fb_overlay_clut_yuv2rgb(this,overlay,frame);

    switch(this->bpp)
    {
       case 16:
	_x_blend_rgb16(frame->data,
		    overlay,
		    frame->sc.output_width,
		    frame->sc.output_height,
		    frame->sc.delivered_width,
		    frame->sc.delivered_height,
                    &this->alphablend_extra_data);
        break;

       case 24:
	_x_blend_rgb24(frame->data,
		    overlay,
		    frame->sc.output_width,
		    frame->sc.output_height,
		    frame->sc.delivered_width,
		    frame->sc.delivered_height,
                    &this->alphablend_extra_data);
        break;

       case 32:
	_x_blend_rgb32(frame->data,
		    overlay,
		    frame->sc.output_width,
		    frame->sc.output_height,
		    frame->sc.delivered_width,
		    frame->sc.delivered_height,
                    &this->alphablend_extra_data);
	break;
     }
   }
}

static int fb_redraw_needed(vo_driver_t *this_gen)
{
  return 0;
}

static void fb_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;
  uint8_t	*dst, *src;
  int y;

  if(frame->sc.output_width  != this->sc.output_width ||
     frame->sc.output_height != this->sc.output_height)
  {
    this->sc.output_width    = frame->sc.output_width;
    this->sc.output_height   = frame->sc.output_height;

    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_fb: gui size %d x %d, frame size %d x %d\n",
            this->sc.gui_width, this->sc.gui_height,
            frame->sc.output_width, frame->sc.output_height);

    memset(this->video_mem_base, 0, this->mem_size);
  }

  if (this->sc.frame_output_cb) {
    this->sc.delivered_height   = frame->sc.delivered_height;
    this->sc.delivered_width    = frame->sc.delivered_width;
    _x_vo_scale_redraw_needed( &this->sc );
  }

  if(this->use_zero_copy)
  {
    if(this->old_frame)
      this->old_frame->vo_frame.free
	(&this->old_frame->vo_frame);
    this->old_frame = this->cur_frame;
    this->cur_frame = frame;

    this->fb_var.yoffset = frame->yoffset;
    if(ioctl(this->fd, FBIOPAN_DISPLAY, &this->fb_var) == -1)
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_fb: ioctl FBIOPAN_DISPLAY failed: %s\n", strerror(errno));
  }
  else
  {
    dst = frame->video_mem +
	  frame->sc.output_yoffset * this->fb_bytes_per_line +
        frame->sc.output_xoffset * this->bytes_per_pixel;
    src = frame->data;

    for(y = 0; y < frame->sc.output_height; y++)
    {
      xine_fast_memcpy(dst, src, frame->bytes_per_line);
      src += frame->bytes_per_line;
      dst += this->fb_bytes_per_line;
    }

    frame->vo_frame.free(&frame->vo_frame);
  }
}

static int fb_get_property(vo_driver_t *this_gen, int property)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;

  switch(property)
  {
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;

    case VO_PROP_BRIGHTNESS:
      return this->yuv2rgb_brightness;
    case VO_PROP_CONTRAST:
      return this->yuv2rgb_contrast;
    case VO_PROP_SATURATION:
      return this->yuv2rgb_saturation;

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
	      "video_out_fb: tried to get unsupported property %d\n", property);
  }

  return 0;
}

static int fb_set_property(vo_driver_t *this_gen, int property, int value)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;

  switch(property)
  {
    case VO_PROP_ASPECT_RATIO:
      if(value>=XINE_VO_ASPECT_NUM_RATIOS)
	value = XINE_VO_ASPECT_AUTO;
      this->sc.user_ratio = value;
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_fb: aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name_table[value]);
      break;

    case VO_PROP_BRIGHTNESS:
      this->yuv2rgb_brightness = value;
      this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
                                             this->yuv2rgb_brightness,
                                             this->yuv2rgb_contrast,
                                             this->yuv2rgb_saturation,
                                             CM_DEFAULT);
      break;

    case VO_PROP_CONTRAST:
      this->yuv2rgb_contrast = value;
      this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
                                             this->yuv2rgb_brightness,
                                             this->yuv2rgb_contrast,
                                             this->yuv2rgb_saturation,
                                             CM_DEFAULT);
      break;

    case VO_PROP_SATURATION:
      this->yuv2rgb_saturation = value;
      this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
                                             this->yuv2rgb_brightness,
                                             this->yuv2rgb_contrast,
                                             this->yuv2rgb_saturation,
                                             CM_DEFAULT);
      break;

    default:
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_fb: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void fb_get_property_min_max(vo_driver_t *this_gen,
				    int property, int *min, int *max)
{
  /* fb_driver_t *this = (fb_driver_t *) this_gen;  */

  switch (property) {
  case VO_PROP_BRIGHTNESS:
    *min = -128;
    *max = +127;
    break;
  case VO_PROP_CONTRAST:
  case VO_PROP_SATURATION:
    *min = 0;
    *max = 255;
    break;
  default:
    *min = 0;
    *max = 0;
    break;
  }
}

static int fb_gui_data_exchange(vo_driver_t *this_gen,
				int data_type, void *data)
{
  return 0;
}

static void fb_dispose(vo_driver_t *this_gen)
{
  fb_driver_t *this = (fb_driver_t *)this_gen;

  munmap(0, this->mem_size);
  close(this->fd);

  _x_alphablend_free(&this->alphablend_extra_data);

  free(this);
}

static int get_fb_var_screeninfo(int fd, struct fb_var_screeninfo *var, xine_t *xine)
{
  int i;

  if(ioctl(fd, FBIOGET_VSCREENINFO, var))
  {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "video_out_fb: ioctl FBIOGET_VSCREENINFO: %s\n", strerror(errno));
    return 0;
  }

  var->xres_virtual = var->xres;
  var->xoffset      = 0;
  var->yoffset      = 0;
  var->nonstd       = 0;
  var->vmode       &= ~FB_VMODE_YWRAP;

  /* Maximize virtual yres to fit as many buffers as possible. */
  for(i = MAXIMUM_NUM_BUFFERS; i > 0; i--)
  {
    var->yres_virtual = i * var->yres;
    if(ioctl(fd, FBIOPUT_VSCREENINFO, var) == -1)
      continue;
    break;
  }

  /* Get proper value for maximized var->yres_virtual. */
  if(ioctl(fd, FBIOGET_VSCREENINFO, var) == -1)
  {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "video_out_fb: ioctl FBIOGET_VSCREENINFO: %s\n", strerror(errno));
    return 0;
  }

  return 1;
}

static int get_fb_fix_screeninfo(int fd, struct fb_fix_screeninfo *fix, xine_t *xine)
{
  if(ioctl(fd, FBIOGET_FSCREENINFO, fix))
  {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "video_out_fb: ioctl FBIOGET_FSCREENINFO: %s\n", strerror(errno));
    return 0;
  }

  if((fix->visual != FB_VISUAL_TRUECOLOR &&
      fix->visual != FB_VISUAL_DIRECTCOLOR) ||
     fix->type != FB_TYPE_PACKED_PIXELS)
  {
    xprintf(xine, XINE_VERBOSITY_LOG,
	    _("video_out_fb: only packed truecolour/directcolour is supported (%d).\n"
	      "     Check 'fbset -i' or try 'fbset -depth 16'.\n"), fix->visual);
    return 0;
  }

  return 1;
}

static int set_fb_palette (int fd, const struct fb_var_screeninfo *var)
{
  unsigned short red[256], green[256], blue[256];
  const struct fb_cmap fb_cmap = {
    0, 256, red, green, blue, NULL
  };
  int i, mask;

  if (!var->red.offset && !var->green.offset && !var->blue.offset)
    return 1; /* we only handle true-colour modes */

  /* Fill in the palette data */
  mask = (1 << var->red.length) - 1;
  for (i = 0; i < 256; ++i)
    red[i] = (i & mask) * 65535.0 / mask;
  mask = (1 << var->green.length) - 1;
  for (i = 0; i < 256; ++i)
    green[i] = (i & mask) * 65535.0 / mask;
  mask = (1 << var->blue.length) - 1;
  for (i = 0; i < 256; ++i)
    blue[i] = (i & mask) * 65535.0 / mask;

  /* Set the palette; return true on success */
  return !ioctl (fd, FBIOPUTCMAP, &fb_cmap);
}

static void register_callbacks(fb_driver_t *this)
{
  this->vo_driver.get_capabilities     = fb_get_capabilities;
  this->vo_driver.alloc_frame          = fb_alloc_frame;
  this->vo_driver.update_frame_format  = fb_update_frame_format;
  this->vo_driver.overlay_begin        = 0; /* not used */
  this->vo_driver.overlay_blend        = fb_overlay_blend;
  this->vo_driver.overlay_end          = 0; /* not used */
  this->vo_driver.display_frame        = fb_display_frame;
  this->vo_driver.get_property         = fb_get_property;
  this->vo_driver.set_property         = fb_set_property;
  this->vo_driver.get_property_min_max = fb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = fb_gui_data_exchange;
  this->vo_driver.dispose              = fb_dispose;
  this->vo_driver.redraw_needed        = fb_redraw_needed;
}

static int open_fb_device(config_values_t *config, xine_t *xine)
{
  static const char devkey[] = "video.device.fb_device";
  const char *device_name;
  int fd;

  /* This config entry is security critical, is it really necessary
   * or is a number enough? */
  device_name = config->register_filename(config, devkey, "", XINE_CONFIG_STRING_IS_DEVICE_NAME,
					_("framebuffer device name"),
					_("Specifies the file name for the framebuffer device "
					  "to be used.\nThis setting is security critical, "
					  "because when changed to a different file, xine "
					  "can be used to fill this file with arbitrary content. "
					  "So you should be careful that the value you enter "
					  "really is a proper framebuffer device."),
					XINE_CONFIG_SECURITY, NULL, NULL);
  if(strlen(device_name) > 3)
  {
    fd = xine_open_cloexec(device_name, O_RDWR);
  }
  else
  {
    device_name = "/dev/fb1";
    fd = xine_open_cloexec(device_name, O_RDWR);

    if(fd < 0)
    {
      device_name = "/dev/fb0";
      fd = xine_open_cloexec(device_name, O_RDWR);
    }
  }

  if(fd < 0)
  {
    xprintf(xine, XINE_VERBOSITY_DEBUG,
	    "video_out_fb: Unable to open device \"%s\", aborting: %s\n", device_name, strerror(errno));
    return -1;
  }

  config->update_string(config, devkey, device_name);

  return fd;
}

static int mode_visual(fb_driver_t *this, config_values_t *config,
		       struct fb_var_screeninfo *var,
		       struct fb_fix_screeninfo *fix)
{
  switch(fix->visual)
  {
    case FB_VISUAL_TRUECOLOR:
    case FB_VISUAL_DIRECTCOLOR:
      switch(this->depth)
      {
	case 24:
	  if(this->bpp == 32)
	  {
	    if(!var->blue.offset)
	      return MODE_32_RGB;
	    return MODE_32_BGR;
	  }
	  if(!var->blue.offset)
	    return MODE_24_RGB;
	  return MODE_24_BGR;

	case 16:
	  if(!var->blue.offset)
	    return MODE_16_RGB;
	  return MODE_16_BGR;

	case 15:
	  if(!var->blue.offset)
	    return MODE_15_RGB;
	  return MODE_15_BGR;

	case 8:
	  if(!var->blue.offset)
	    return MODE_8_RGB;
	  return MODE_8_BGR;

      }
  }

  xprintf(this->xine, XINE_VERBOSITY_LOG, _("%s: Your video mode was not recognized, sorry.\n"), LOG_MODULE);
  return 0;
}

static int setup_yuv2rgb(fb_driver_t *this, config_values_t *config,
			 struct fb_var_screeninfo *var,
			 struct fb_fix_screeninfo *fix)
{
  this->yuv2rgb_mode = mode_visual(this, config, var, fix);
  if(!this->yuv2rgb_mode)
    return 0;

  this->yuv2rgb_swap       = 0;
  this->yuv2rgb_brightness = 0;
  this->yuv2rgb_contrast   = this->yuv2rgb_saturation = 128;

  this->yuv2rgb_factory = yuv2rgb_factory_init(this->yuv2rgb_mode,
					       this->yuv2rgb_swap,
					       this->yuv2rgb_cmap);
  this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
                                         this->yuv2rgb_brightness,
                                         this->yuv2rgb_contrast,
                                         this->yuv2rgb_saturation,
                                         CM_DEFAULT);

  return 1;
}

static void setup_buffers(fb_driver_t *this,
			  struct fb_var_screeninfo *var)
{
  /*
   * depth in X11 terminology land is the number of bits used to
   * actually represent the colour.
   *
   * bpp in X11 land means how many bits in the frame buffer per
   * pixel.
   *
   * ex. 15 bit color is 15 bit depth and 16 bpp. Also 24 bit
   *     color is 24 bit depth, but can be 24 bpp or 32 bpp.
   *
   * fb assumptions: bpp % 8   = 0 (e.g. 8, 16, 24, 32 bpp)
   *                 bpp      <= 32
   *                 msb_right = 0
   */

  this->bytes_per_pixel = (this->fb_var.bits_per_pixel + 7)/8;
  this->bpp = this->bytes_per_pixel * 8;
  this->depth = this->fb_var.red.length +
		this->fb_var.green.length +
		this->fb_var.blue.length;

  this->total_num_native_buffers = var->yres_virtual / var->yres;
  this->used_num_buffers = 0;

  this->cur_frame = this->old_frame = 0;

  xprintf(this->xine, XINE_VERBOSITY_LOG,
	  _("%s: %d video RAM buffers are available.\n"), LOG_MODULE, this->total_num_native_buffers);

  if(this->total_num_native_buffers < RECOMMENDED_NUM_BUFFERS)
  {
    this->use_zero_copy = 0;
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("WARNING: %s: Zero copy buffers are DISABLED because only %d buffers\n"
	      "     are available which is less than the recommended %d buffers. Lowering\n"
	      "     the frame buffer resolution might help.\n"),
	    LOG_MODULE, this->total_num_native_buffers, RECOMMENDED_NUM_BUFFERS);
  }
  else
  {
    /* test if FBIOPAN_DISPLAY works */
    this->fb_var.yoffset = this->fb_var.yres;
    if(ioctl(this->fd, FBIOPAN_DISPLAY, &this->fb_var) == -1) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("WARNING: %s: Zero copy buffers are DISABLED because kernel driver\n"
		"     do not support screen panning (used for frame flips).\n"), LOG_MODULE);
    } else {
      this->fb_var.yoffset = 0;
      ioctl(this->fd, FBIOPAN_DISPLAY, &this->fb_var);

      this->use_zero_copy = 1;
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_fb: Using zero copy buffers.\n");
    }
  }
}

static vo_driver_t *fb_open_plugin(video_driver_class_t *class_gen,
				   const void *visual_gen)
{
  config_values_t *config;
  fb_driver_t *this;
  fb_class_t *class;
  fb_visual_t *visual = NULL;

  if (visual_gen) {
    visual = (fb_visual_t *) visual_gen;
  }

  class = (fb_class_t *)class_gen;
  config = class->config;

  /* allocate plugin struct */
  this = calloc(1, sizeof(fb_driver_t));
  if(!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  register_callbacks(this);

  this->fd = open_fb_device(config, class->xine);
  if(this->fd == -1)
    goto error;
  if(!get_fb_var_screeninfo(this->fd, &this->fb_var, class->xine))
    goto error;
  if(!get_fb_fix_screeninfo(this->fd, &this->fb_fix, class->xine))
    goto error;
  if (!set_fb_palette (this->fd, &this->fb_var))
    goto error;

  this->xine = class->xine;

  if(this->fb_fix.line_length)
    this->fb_bytes_per_line = this->fb_fix.line_length;
  else
    this->fb_bytes_per_line =
      (this->fb_var.xres_virtual *
       this->fb_var.bits_per_pixel)/8;

  _x_vo_scale_init(&this->sc, 0, 0, config);
  this->sc.gui_width  = this->fb_var.xres;
  this->sc.gui_height = this->fb_var.yres;
  this->sc.user_ratio = XINE_VO_ASPECT_AUTO;

  if (visual) {
    this->sc.frame_output_cb = visual->frame_output_cb;
    this->sc.user_data       = visual->user_data;
  }

  setup_buffers(this, &this->fb_var);

  if(this->depth > 16)
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("WARNING: %s: current display depth is %d. For better performance\n"
	      "     a depth of 16 bpp is recommended!\n\n"), LOG_MODULE, this->depth);

  xprintf(class->xine, XINE_VERBOSITY_DEBUG,
	  "%s: video mode depth is %d (%d bpp),\n"
	  "     red: %d/%d, green: %d/%d, blue: %d/%d\n",
	  LOG_MODULE,
	  this->depth, this->bpp,
	  this->fb_var.red.length, this->fb_var.red.offset,
	  this->fb_var.green.length, this->fb_var.green.offset,
	  this->fb_var.blue.length, this->fb_var.blue.offset);

  if(!setup_yuv2rgb(this, config, &this->fb_var, &this->fb_fix))
    goto error;

  /* mmap whole video memory */
  this->mem_size = this->fb_fix.smem_len;
  this->video_mem_base = mmap(0, this->mem_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, this->fd, 0);
  return &this->vo_driver;
error:
  free(this);
  return 0;
}

static void *fb_init_class(xine_t *xine, void *visual_gen)
{
  fb_class_t *this = calloc(1, sizeof(fb_class_t));

  this->driver_class.open_plugin     = fb_open_plugin;
  this->driver_class.identifier      = "fb";
  this->driver_class.description     = N_("Xine video output plugin using the Linux frame buffer device");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config          = xine->config;
  this->xine            = xine;

  return this;
}

static const vo_info_t vo_info_fb =
{
  1,                    /* priority    */
  XINE_VISUAL_TYPE_FB   /* visual type */
};

/* exported plugin catalog entry */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "fb", XINE_VERSION_CODE, &vo_info_fb, fb_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

