/*
 * Copyright (C) 2000-2004 the xine project
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
 * video_out_pgx32.c, Sun PGX32 output plugin for xine
 *
 * written and currently maintained by
 *   Robin Kay <komadori [at] gekkou [dot] co [dot] uk>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/fbio.h>
#include <sys/visual_io.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <dga/dga.h>

#include <xine/xine_internal.h>
#include "bswap.h"
#include <xine/vo_scale.h>
#include <xine/xineutils.h>

/* gfxp register defines */

#define GFXP_VRAM_MMAPLEN 0x00800000
#define GFXP_REGS_MMAPLEN 0x00020000
#define GFXP_REGSBASE 0x00800000

#define FIFO_SPACE 0x0003

#define RASTERISER_MODE 0x1014

#define RECT_ORIGIN 0x101A
#define RECT_SIZE 0x101B

#define SCISSOR_MODE 0x1030
#define SCISSOR_MIN_XY 0x1031
#define SCISSOR_MAX_XY 0x1032
#define AREA_STIPPLE_MODE 0x1034
#define WINDOW_ORIGIN 0x1039

#define DY 0x1005

#define TEXTURE_ADDR_MODE 0x1070
#define SSTART 0x1071
#define DSDX 0x1072
#define DSDY_DOM 0x1073
#define TSTART 0x1074
#define DTDX 0x1075
#define DTDY_DOM 0x1076

#define TEXTURE_BASE_ADDR 0x10B0
#define TEXTURE_MAP_FORMAT 0x10B1
#define TEXTURE_DATA_FORMAT 0x10B2
#define TEXTURE_READ_MODE 0x10CE
#define TEXTURE_COLOUR_MODE 0x10D0

#define SHADING_MODE 0x10FC
#define ALPHA_BLENDING_MODE 0x1102
#define DITHERING_MODE 0x1103
#define LOGICAL_OP_MODE 0x1105
#define STENCIL_MODE 0x1131

#define WRITE_MODE 0x1157
#define WRITE_MASK 0x1158

#define YUV_MODE 0x11E0

#define RENDER 0x1007
#define RENDER_BEGIN 0x00000000006020C0L

static const int pitch_code_table[33][2] =
{
  {0,    0000},
  {32,   0001},
  {64,   0011},
  {96,   0111},
  {128,  0112},
  {160,  0122},
  {192,  0222},
  {224,  0123},
  {256,  0223},
  {288,  0133},
  {320,  0233},
  {384,  0333},
  {416,  0134},
  {448,  0234},
  {512,  0334},
  {544,  0144},
  {576,  0244},
  {640,  0344},
  {768,  0444},
  {800,  0145},
  {832,  0245},
  {896,  0345},
  {1024, 0445},
  {1056, 0155},
  {1088, 0255},
  {1152, 0355},
  {1280, 0455},
  {1536, 0555},
  {1568, 0156},
  {1600, 0256},
  {1664, 0356},
  {1792, 0456},
  {2048, 0556}
};

/* Structures */

typedef struct {
  video_driver_class_t vo_driver_class;

  xine_t *xine;
  config_values_t *config;
} pgx32_driver_class_t;

typedef struct {
  vo_frame_t vo_frame;

  uint32_t *packedbuf, *stripe_dst;
  int width, height, format, pitch, pitch_code, packedlen, lines_remaining;
  double ratio;
} pgx32_frame_t;

typedef struct {
  vo_driver_t vo_driver;
  vo_scale_t vo_scale;

  pgx32_driver_class_t *class;

  Display *display;
  int screen, depth;
  Visual *visual;
  Drawable drawable;
  GC gc;
  Dga_drawable dgadraw;

  int devfd, fb_width, fb_height, fb_depth;
  uint8_t *vbase;
  volatile uint64_t *vregs;

  pgx32_frame_t *current;

  int delivered_format, deinterlace_en;

  alphablend_t alphablend_extra_data;
} pgx32_driver_t;

/*
 * Setup X11/DGA
 */

static int setup_dga(pgx32_driver_t *this)
{
  XWindowAttributes win_attrs;

  XLockDisplay(this->display);
  this->dgadraw = XDgaGrabDrawable(this->display, this->drawable);
  if (!this->dgadraw) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx32: Error: can't grab DGA drawable for video window\n"));
    XUnlockDisplay(this->display);
    return 0;
  }

  /* If the framebuffer hasn't already been mapped then open it and check it's
   * a supported type.
   */
  if (!this->vbase) {
    char *devname;
    struct vis_identifier ident;
    struct fbgattr attr;

    DGA_DRAW_LOCK(this->dgadraw, -1);
    this->devfd = dga_draw_devfd(this->dgadraw);
    devname     = dga_draw_devname(this->dgadraw);
    DGA_DRAW_UNLOCK(this->dgadraw);

    if (ioctl(this->devfd, VIS_GETIDENTIFIER, &ident) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx32: Error: ioctl failed, bad device (%s)\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }

    if (strcmp("TSIgfxp", ident.name) != 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx32: Error: '%s' is not a pgx32 framebuffer device\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }

    if (ioctl(this->devfd, FBIOGATTR, &attr) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx32: Error: ioctl failed, bad device (%s)\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }

    this->fb_width  = attr.fbtype.fb_width;
    this->fb_height = attr.fbtype.fb_height;
    this->fb_depth  = attr.fbtype.fb_depth;
  }

  this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
  XGetWindowAttributes(this->display, this->drawable, &win_attrs);
  this->depth  = win_attrs.depth;
  this->visual = win_attrs.visual;
  XUnlockDisplay(this->display);

  return 1;
}

/*
 * Cleanup X11/DGA
 */

static void cleanup_dga(pgx32_driver_t *this)
{
  XLockDisplay(this->display);
  XFreeGC(this->display, this->gc);
  XDgaUnGrabDrawable(this->dgadraw);
  XUnlockDisplay(this->display);
}

/*
 * Dispose of any allocated image data within a pgx32_frame_t
 */

static void dispose_frame_internals(pgx32_frame_t *frame)
{
  if (frame->vo_frame.base[0]) {
    free(frame->vo_frame.base[0]);
    frame->vo_frame.base[0] = NULL;
  }
  if (frame->vo_frame.base[1]) {
    free(frame->vo_frame.base[1]);
    frame->vo_frame.base[1] = NULL;
  }
  if (frame->vo_frame.base[2]) {
    free(frame->vo_frame.base[2]);
    frame->vo_frame.base[2] = NULL;
  }
  if (frame->packedbuf) {
    free(frame->packedbuf);
    frame->packedbuf = NULL;
  }
}

/*
 * Convert yuy2 frame/slice to GFXP internal YUV format
 */

static uint32_t *convert_yuy2(uint32_t *src, int width, int pitch, int height, uint32_t *dst)
{
  int l, x;

  for (l=0; l<height; l++) {
    for(x=0; x<(width+1)/2; x++) {
      *(dst++) = (*src >> 16) | ((*src & 0xffff) << 16);
      src++;
    }
    dst += (pitch-width)/2;
  }

  return dst;
}

/*
 * Convert yv12 frame/slice to GFXP internal YUV format
 */

static uint32_t *convert_yv12(uint16_t *ysrc, uint8_t *usrc, uint8_t *vsrc, int width, int pitch, int height, uint32_t *dst)
{
  int l, x, y, u, v;

  for (l=0; l<height; l++) {
    if (l & 1) {
      usrc -= (width+1)/2;
      vsrc -= (width+1)/2;
    }

    for (x=0; x<(width+1)/2; x++) {
      y = *(ysrc++);
      u = *(usrc++);
      v = *(vsrc++);
      *(dst++) = ((y & 0x00ff) << 24) | (v << 16) | (y & 0xff00) | u;
    }
    dst += (pitch-width)/2;
  }

  return dst;
}

/*
 * XINE VIDEO DRIVER FUNCTIONS
 */

static void pgx32_frame_proc_frame(vo_frame_t *frame_gen)
{
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;

  frame->vo_frame.proc_called = 1;

  switch (frame->format) {
    case XINE_IMGFMT_YUY2:
      convert_yuy2((uint32_t *)(void *)frame->vo_frame.base[0], frame->width, frame->pitch, frame->height, frame->stripe_dst);
    break;

    case XINE_IMGFMT_YV12:
      convert_yv12((uint16_t *)(void *)frame->vo_frame.base[0], frame->vo_frame.base[1], frame->vo_frame.base[2], frame->width, frame->pitch, frame->height, frame->stripe_dst);
    break;
  }
}

static void pgx32_frame_proc_slice(vo_frame_t *frame_gen, uint8_t **src)
{
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;
  int slice_height;

  frame->vo_frame.proc_called = 1;

  slice_height = (frame->lines_remaining > 16) ? 16 : frame->lines_remaining;
  frame->lines_remaining -= slice_height;

  switch (frame->format) {
    case XINE_IMGFMT_YUY2:
      frame->stripe_dst = convert_yuy2((uint32_t *)(void *)src[0], frame->width, frame->pitch, slice_height, frame->stripe_dst);
    break;

    case XINE_IMGFMT_YV12:
      frame->stripe_dst = convert_yv12((uint16_t *)(void *)src[0], src[1], src[2], frame->width, frame->pitch, slice_height, frame->stripe_dst);
    break;
  }
}

static void pgx32_frame_field(vo_frame_t *frame_gen, int which_field)
{
  /*pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;*/
}

static void pgx32_frame_dispose(vo_frame_t *frame_gen)
{
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;

  dispose_frame_internals(frame);
  free(frame);
}

static uint32_t pgx32_get_capabilities(vo_driver_t *this_gen)
{
  /*pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;*/

  return VO_CAP_YV12 |
         VO_CAP_YUY2;
}

static vo_frame_t *pgx32_alloc_frame(vo_driver_t *this_gen)
{
  /*pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;*/
  pgx32_frame_t *frame;

  frame = calloc(1, sizeof(pgx32_frame_t));
  if (!frame) {
    return NULL;
  }

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.proc_frame = pgx32_frame_proc_frame;
  frame->vo_frame.proc_slice = pgx32_frame_proc_slice;
  frame->vo_frame.field      = pgx32_frame_field;
  frame->vo_frame.dispose    = pgx32_frame_dispose;

  return (vo_frame_t *)frame;
}

static void pgx32_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen, uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;

  if ((width != frame->width) ||
      (height != frame->height) ||
      (ratio != frame->ratio) ||
      (format != frame->format)) {
    int i, planes;

    dispose_frame_internals(frame);

    frame->width = width;
    frame->height = height;
    frame->ratio = ratio;
    frame->format = format;

    frame->pitch = 2048;
    for (i=0; i<sizeof(pitch_code_table)/sizeof(pitch_code_table[0]); i++) {
      if ((pitch_code_table[i][0] >= frame->width) && (pitch_code_table[i][0] <= frame->pitch)) {
        frame->pitch = pitch_code_table[i][0];
        frame->pitch_code = pitch_code_table[i][1];
      }
    }

    frame->packedlen = frame->pitch * 2 * height;
    if (!(frame->packedbuf = memalign(8, frame->packedlen))) {
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx32: frame packed buffer malloc failed\n");
      _x_abort();
    }

    planes = 0;

    switch (format) {
      case XINE_IMGFMT_YUY2:
        planes = 1;
        frame->vo_frame.pitches[0] = ((width + 1) / 2) * 4;
        frame->vo_frame.base[0] = memalign(8, frame->vo_frame.pitches[0] * height);
      break;

      case XINE_IMGFMT_YV12:
        planes = 3;
        frame->vo_frame.pitches[0] = ((width + 1) / 2) * 2;
        frame->vo_frame.pitches[1] = (width + 1) / 2;
        frame->vo_frame.pitches[2] = (width + 1) / 2;
        frame->vo_frame.base[0] = memalign(8, frame->vo_frame.pitches[0] * height);
        frame->vo_frame.base[1] = memalign(8, frame->vo_frame.pitches[1] * ((height + 1) / 2));
        frame->vo_frame.base[2] = memalign(8, frame->vo_frame.pitches[2] * ((height + 1) / 2));
      break;
    }

    for (i=0;i<planes;i++) {
      if (!frame->vo_frame.base[i]) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx32: frame plane malloc failed\n");
        _x_abort();
      }
    }
  }

  frame->stripe_dst = frame->packedbuf;
  frame->lines_remaining = frame->height;
}

static void pgx32_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;

  short int *cliprects, wx0, wy0, wx1, wy1, cx0, cy0, cx1, cy1;

  if ((frame->width != this->vo_scale.delivered_width) ||
      (frame->height != this->vo_scale.delivered_height) ||
      (frame->ratio != this->vo_scale.delivered_ratio) ||
      (frame->format != this->delivered_format)) {
    this->vo_scale.delivered_width  = frame->width;
    this->vo_scale.delivered_height = frame->height;
    this->vo_scale.delivered_ratio  = frame->ratio;
    this->delivered_format          = frame->format;

    this->vo_scale.force_redraw = 1;
    _x_vo_scale_compute_ideal_size(&this->vo_scale);
  }

  if (_x_vo_scale_redraw_needed(&this->vo_scale)) {
    int i;

    _x_vo_scale_compute_output_size(&this->vo_scale);

    XLockDisplay(this->display);
    XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
    for (i=0;i<4;i++) {
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[i].x, this->vo_scale.border[i].y, this->vo_scale.border[i].w, this->vo_scale.border[i].h);
    }
    XUnlockDisplay(this->display);
  }

  memcpy((this->vbase+GFXP_VRAM_MMAPLEN)-frame->packedlen, frame->packedbuf, frame->packedlen);

  XLockDisplay(this->display);
  DGA_DRAW_LOCK(this->dgadraw, -1);

  while(le2me_64(this->vregs[FIFO_SPACE]) < 24) {}

  this->vregs[RASTERISER_MODE] = 0;
  this->vregs[SCISSOR_MODE] = 0;
  this->vregs[AREA_STIPPLE_MODE] = 0;
  this->vregs[WINDOW_ORIGIN] = 0;

  this->vregs[DY] = le2me_64(1 << 16);

  this->vregs[TEXTURE_ADDR_MODE] = le2me_64(1);
  this->vregs[SSTART] = 0;
  this->vregs[DSDX] = le2me_64((frame->width << 20) / this->vo_scale.output_width);
  this->vregs[DSDY_DOM] = 0;
  this->vregs[TSTART] = 0;
  this->vregs[DTDX] = 0;
  this->vregs[DTDY_DOM] = le2me_64((frame->height << 20) / this->vo_scale.output_height);

  this->vregs[TEXTURE_MAP_FORMAT] = le2me_64((1 << 19) | frame->pitch_code);

  this->vregs[TEXTURE_DATA_FORMAT] = le2me_64(0x63);
  this->vregs[TEXTURE_READ_MODE] = le2me_64((1 << 17) | (11 << 13) | (11 << 9) | 1);
  this->vregs[TEXTURE_COLOUR_MODE] = le2me_64((0 << 4) | (3 << 1) | 1);

  this->vregs[SHADING_MODE] = 0;
  this->vregs[ALPHA_BLENDING_MODE] = 0;
  this->vregs[DITHERING_MODE] = le2me_64((1 << 10) | 1);
  this->vregs[LOGICAL_OP_MODE] = 0;
  this->vregs[STENCIL_MODE] = 0;

  this->vregs[WRITE_MODE] = le2me_64(1);
  this->vregs[WRITE_MASK] = le2me_64(0x00ffffff);

  this->vregs[YUV_MODE] = le2me_64(1);

  wx0 = this->vo_scale.gui_win_x + this->vo_scale.output_xoffset;
  wy0 = this->vo_scale.gui_win_y + this->vo_scale.output_yoffset;
  wx1 = wx0 + this->vo_scale.output_width;
  wy1 = wy0 + this->vo_scale.output_height;

  cliprects = dga_draw_clipinfo(this->dgadraw);
  while ((cy0 = *cliprects++) != DGA_Y_EOL) {
    cy1 = *cliprects++;
    while ((cx0 = *cliprects++) != DGA_X_EOL) {
      cx1 = *cliprects++;

      if ((cx0 >= wx1) || (cy0 >= wy1)) {
        continue;
      }
      if ((cx1 <= wx0) || (cy1 <= wy0)) {
        continue;
      }
      cx0 = (cx0 < wx0) ? wx0 : cx0;
      cy0 = (cy0 < wy0) ? wy0 : cy0;
      cx1 = (cx1 > wx1) ? wx1 : cx1;
      cy1 = (cy1 > wy1) ? wy1 : cy1;

      while(le2me_64(this->vregs[FIFO_SPACE]) < 4) {}
      this->vregs[RECT_ORIGIN] = le2me_64(cx0 | (cy0 << 16));
      this->vregs[RECT_SIZE] = le2me_64((cx1-cx0) | ((cy1-cy0) << 16));
      this->vregs[TEXTURE_BASE_ADDR] = le2me_64(((GFXP_VRAM_MMAPLEN-frame->packedlen) >> 1) + (((cx0-wx0)*frame->width)/this->vo_scale.output_width) + (((cy0-wy0)*frame->height)/this->vo_scale.output_height) * frame->pitch);
      this->vregs[RENDER] = le2me_64(RENDER_BEGIN);
    }
  }

  while(le2me_64(this->vregs[FIFO_SPACE]) < 5) {}
  this->vregs[TEXTURE_ADDR_MODE] = 0;
  this->vregs[TEXTURE_READ_MODE] = 0;
  this->vregs[TEXTURE_COLOUR_MODE] = 0;
  this->vregs[DITHERING_MODE] = 0;
  this->vregs[YUV_MODE] = 0;

  DGA_DRAW_UNLOCK(this->dgadraw);
  XUnlockDisplay(this->display);

  if (this->current != NULL) {
    this->current->vo_frame.free(&this->current->vo_frame);
  }
  this->current = frame;
}

#define blend(a, b, trans) (((a)*(trans) + (b)*(15-(trans))) / 15)

static void pgx32_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  /*pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;*/
  pgx32_frame_t *frame = (pgx32_frame_t *)frame_gen;

  if (overlay->rle) {
    int i, j, x, y, len, width;
    int use_clip_palette;
    uint16_t *dst;
    clut_t clut;
    uint8_t trans;

    dst = (uint16_t *)frame->packedbuf + (overlay->y * frame->pitch) + overlay->x;

    for (i=0, x=0, y=0; i<overlay->num_rle; i++) {
      len = overlay->rle[i].len;

      while (len > 0) {
        use_clip_palette = 0;
        if (len > overlay->width) {
          width = overlay->width;
          len -= overlay->width;
        }
        else {
          width = len;
          len = 0;
        }

        if ((y >= overlay->hili_top) && (y <= overlay->hili_bottom) && (x <= overlay->hili_right)) {
          if ((x < overlay->hili_left) && (x + width - 1 >= overlay->hili_left)) {
            width -= overlay->hili_left - x;
            len += overlay->hili_left - x;
          }
          else if (x > overlay->hili_left)  {
            use_clip_palette = 1;
            if (x + width - 1 > overlay->hili_right) {
              width -= overlay->hili_right - x;
              len += overlay->hili_right - x;
            }
          }
        }

        if (use_clip_palette) {
          clut = *(clut_t *)&overlay->hili_color[overlay->rle[i].color];
          trans = overlay->hili_trans[overlay->rle[i].color];
        }
        else {
          clut = *(clut_t *)&overlay->color[overlay->rle[i].color];
          trans = overlay->trans[overlay->rle[i].color];
        }

        for (j=0; j<width; j++) {
          if ((overlay->x + x + j) & 1) {
            *(dst-1) = (blend(clut.y, (*(dst-1) >> 8), trans) << 8) | blend(clut.cr, (*(dst-1) & 0xff), trans);
          }
          else {
            *(dst+1) = (blend(clut.y, (*(dst+1) >> 8), trans) << 8) | blend(clut.cb, (*(dst+1) & 0xff), trans);
          }
          dst++;
        }

        x += width;
        if (x == overlay->width) {
          x = 0;
          y++;
          dst += frame->pitch - overlay->width;
        }
      }
    }
  }
}

static int pgx32_get_property(vo_driver_t *this_gen, int property)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_INTERLACED:
      return this->deinterlace_en;
    break;

    case VO_PROP_ASPECT_RATIO:
      return this->vo_scale.user_ratio;
    break;

    default:
      return 0;
    break;
  }
}

static int pgx32_set_property(vo_driver_t *this_gen, int property, int value)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_INTERLACED: {
      this->deinterlace_en = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_ASPECT_RATIO: {
      if (value >= XINE_VO_ASPECT_NUM_RATIOS) {
        value = XINE_VO_ASPECT_AUTO;
      }
      this->vo_scale.user_ratio = value;
      this->vo_scale.force_redraw = 1;
      _x_vo_scale_compute_ideal_size(&this->vo_scale);
    }
    break;
  }
  return value;
}

static void pgx32_get_property_min_max(vo_driver_t *this_gen, int property, int *min, int *max)
{
  /*pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;*/

  switch (property) {
    default:
      *min = 0;
      *max = 0;
    break;
  }
}

static int pgx32_gui_data_exchange(vo_driver_t *this_gen, int data_type, void *data)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;

  switch (data_type) {
    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      XLockDisplay(this->display);
      cleanup_dga(this);
      this->drawable = (Drawable)data;
      if (!setup_dga(this)) {
        /* FIXME! There should be a better way to handle this */
        _x_abort();
      }
      XUnlockDisplay(this->display);

    }
    break;

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      this->vo_scale.force_redraw = 1;
    }
    break;

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      x11_rectangle_t *rect = data;
      int x1, y1, x2, y2;

      _x_vo_scale_translate_gui2video(&this->vo_scale, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->vo_scale, rect->x + rect->w, rect->y + rect->h, &x2, &y2);

      rect->x = x1;
      rect->y = y1;
      rect->w = x2 - x1;
      rect->h = y2 - y1;
    }
    break;
  }

  return 0;
}

static int pgx32_redraw_needed(vo_driver_t *this_gen)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;

  if (_x_vo_scale_redraw_needed(&this->vo_scale)) {
    this->vo_scale.force_redraw = 1;
    return 1;
  }

  return 0;
}

static void pgx32_dispose(vo_driver_t *this_gen)
{
  pgx32_driver_t *this = (pgx32_driver_t *)(void *)this_gen;

  cleanup_dga(this);

  munmap(this->vbase, GFXP_VRAM_MMAPLEN);
  munmap((void *)this->vregs, GFXP_REGS_MMAPLEN);

  _x_alphablend_free(&this->alphablend_extra_data);

  free(this);
}

/*
 * XINE VIDEO DRIVER CLASS FUNCTIONS
 */

static const vo_info_t vo_info_pgx32 = {
  10,
  XINE_VISUAL_TYPE_X11
};

static vo_driver_t *pgx32_init_driver(video_driver_class_t *class_gen, const void *visual_gen)
{
  pgx32_driver_class_t *class = (pgx32_driver_class_t *)(void *)class_gen;
  pgx32_driver_t *this;

  this = calloc(1, sizeof(pgx32_driver_t));
  if (!this) {
    return NULL;
  }

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->vo_driver.get_capabilities     = pgx32_get_capabilities;
  this->vo_driver.alloc_frame          = pgx32_alloc_frame;
  this->vo_driver.update_frame_format  = pgx32_update_frame_format;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = pgx32_overlay_blend;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.display_frame        = pgx32_display_frame;
  this->vo_driver.get_property         = pgx32_get_property;
  this->vo_driver.set_property         = pgx32_set_property;
  this->vo_driver.get_property_min_max = pgx32_get_property_min_max;
  this->vo_driver.gui_data_exchange    = pgx32_gui_data_exchange;
  this->vo_driver.redraw_needed        = pgx32_redraw_needed;
  this->vo_driver.dispose              = pgx32_dispose;

  _x_vo_scale_init(&this->vo_scale, 0, 0, class->config);
  this->vo_scale.user_ratio      = XINE_VO_ASPECT_AUTO;
  this->vo_scale.user_data       = ((x11_visual_t *)visual_gen)->user_data;
  this->vo_scale.frame_output_cb = ((x11_visual_t *)visual_gen)->frame_output_cb;
  this->vo_scale.dest_size_cb    = ((x11_visual_t *)visual_gen)->dest_size_cb;

  this->class = class;

  this->display  = ((x11_visual_t *)visual_gen)->display;
  this->screen   = ((x11_visual_t *)visual_gen)->screen;
  this->drawable = ((x11_visual_t *)visual_gen)->d;

  if (!setup_dga(this)) {
    free(this);
    return NULL;
  }

  if ((this->vbase = mmap(0, GFXP_VRAM_MMAPLEN, PROT_READ | PROT_WRITE, MAP_SHARED, this->devfd, 0x04000000)) == MAP_FAILED) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx32: Error: unable to memory map framebuffer\n");
    cleanup_dga(this);
    free(this);
    return NULL;
  }
  if ((this->vregs = (uint64_t *)(void *)mmap(0, GFXP_REGS_MMAPLEN, PROT_READ | PROT_WRITE, MAP_SHARED, this->devfd, 0x02000000)) == MAP_FAILED) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx32: Error: unable to memory map framebuffer\n");
    munmap(this->vbase, GFXP_VRAM_MMAPLEN);
    cleanup_dga(this);
    free(this);
    return NULL;
  }

  return (vo_driver_t *)this;
}

static void *pgx32_init_class(xine_t *xine, void *visual_gen)
{
  pgx32_driver_class_t *class;

  class = calloc(1, sizeof(pgx32_driver_class_t));
  if (!class) {
    return NULL;
  }

  DGA_INIT();

  class->vo_driver_class.open_plugin     = pgx32_init_driver;
  class->vo_driver_class.identifier      = "pgx32";
  class->vo_driver_class.description     = N_("xine video output plugin for Sun PGX32 framebuffers");
  class->vo_driver_class.dispose         = default_video_driver_class_dispose;

  class->xine   = xine;
  class->config = xine->config;

  return class;
}

const plugin_info_t xine_plugin_info[] EXPORTED = {
  {PLUGIN_VIDEO_OUT, 22, "pgx32", XINE_VERSION_CODE, &vo_info_pgx32, pgx32_init_class},
  {PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
