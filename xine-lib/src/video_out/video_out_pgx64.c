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
 * video_out_pgx64.c, Sun XVR100/PGX64/PGX24 output plugin for xine
 *
 * written and currently maintained by
 *   Robin Kay <komadori [at] gekkou [dot] co [dot] uk>
 *
 * Sun XVR-100 framebuffer graciously donated by Jake Goerzen.
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

/*
 * The maximum number of frames that can be used in multi-buffering
 * configuration.
 */
#define MAX_MULTIBUF_FRAMES 15

/*
 * The maximum number of frames that can be safely taken out of circulation.
 */
#define MAX_DETAINED_FRAMES 10

/* m64 register defines */

#define M64_VRAM_MMAPBASE 0x00000000
#define M64_VRAM_MMAPLEN 0x00800000

#define M64_BUS_CNTL 0x128
#define M64_BUS_EXT_REG_EN 0x08000000

#define M64_OVERLAY_X_Y_START 0x000
#define M64_OVERLAY_X_Y_END 0x001
#define M64_OVERLAY_X_Y_LOCK 0x80000000
#define M64_OVERLAY_GRAPHICS_KEY_CLR 0x004
#define M64_OVERLAY_GRAPHICS_KEY_MSK 0x005
#define M64_OVERLAY_KEY_CNTL 0x006
#define M64_OVERLAY_KEY_EN 0x00000050
#define M64_OVERLAY_SCALE_INC 0x008
#define M64_OVERLAY_EXCLUSIVE_HORZ 0x016
#define M64_OVERLAY_EXCLUSIVE_VERT 0x017
#define M64_OVERLAY_EXCLUSIVE_EN 0x80000000
#define M64_OVERLAY_SCALE_CNTL 0x009
#define M64_OVERLAY_SCALE_EN 0xC0000000

#define M64_SCALER_HEIGHT_WIDTH 0x00A
#define M64_SCALER_COLOUR_CNTL 0x054
#define M64_SCALER_H_COEFF0 0x055
#define M64_SCALER_H_COEFF0_DEFAULT 0x00002000
#define M64_SCALER_H_COEFF1 0x056
#define M64_SCALER_H_COEFF1_DEFAULT 0x0D06200D
#define M64_SCALER_H_COEFF2 0x057
#define M64_SCALER_H_COEFF2_DEFAULT 0x0D0A1C0D
#define M64_SCALER_H_COEFF3 0x058
#define M64_SCALER_H_COEFF3_DEFAULT 0x0C0E1A0C
#define M64_SCALER_H_COEFF4 0x059
#define M64_SCALER_H_COEFF4_DEFAULT 0x0C14140C
#define M64_SCALER_BUF0_OFFSET 0x00D
#define M64_SCALER_BUF0_OFFSET_U 0x075
#define M64_SCALER_BUF0_OFFSET_V 0x076
#define M64_SCALER_BUF1_OFFSET 0x00E
#define M64_SCALER_BUF1_OFFSET_U 0x077
#define M64_SCALER_BUF1_OFFSET_V 0x078
#define M64_SCALER_BUF_PITCH 0x00F

#define M64_VIDEO_FORMAT 0x012
#define M64_VIDEO_FORMAT_YUV12 0x000A0000
#define M64_VIDEO_FORMAT_VYUY422 0x000B0000
#define M64_CAPTURE_CONFIG 0x014
#define M64_CAPTURE_CONFIG_BUF0 0x00000000
#define M64_CAPTURE_CONFIG_BUF1 0x20000000

static const int m64_bufaddr_regs_tbl[2][3] = {
  {M64_SCALER_BUF0_OFFSET, M64_SCALER_BUF0_OFFSET_U, M64_SCALER_BUF0_OFFSET_V},
  {M64_SCALER_BUF1_OFFSET, M64_SCALER_BUF1_OFFSET_U, M64_SCALER_BUF1_OFFSET_V}
};

/* pfb register defines */

#define PFB_VRAM_MMAPBASE 0x08000000
#define PFB_VRAM_MMAPLEN 0x02000000
#define PFB_REGS_MMAPBASE 0x10000000
#define PFB_REGS_MMAPLEN 0x00040000

#define PFB_CLOCK_CNTL_INDEX 0x002
#define PFB_CLOCK_CNTL_DATA 0x003

#define PFB_MC_FB_LOCATION 0x052

#define PFB_OV0_Y_X_START 0x100
#define PFB_OV0_Y_X_END 0x101
#define PFB_OV0_REG_LOAD_CNTL 0x104
#define PFB_OV0_REG_LOAD_LOCK 0x00000001
#define PFB_OV0_REG_LOAD_LOCK_READBACK 0x00000008
#define PFB_OV0_SCALE_CNTL 0x108
#define PFB_OV0_SCALE_EN 0x417f0000
#define PFB_OV0_SCALE_YUV12 0x00000A00
#define PFB_OV0_SCALE_VYUY422 0x00000B00
#define PFB_OV0_V_INC 0x109
#define PFB_OV0_P1_V_ACCUM_INIT 0x10A
#define PFB_OV0_P23_V_ACCUM_INIT 0x10B
#define PFB_OV0_P1_BLANK_LINES_AT_TOP 0x10C
#define PFB_OV0_P23_BLANK_LINES_AT_TOP 0x10D
#define PFB_OV0_BASE_ADDR 0x10F
#define PFB_OV0_BUF0_BASE_ADRS 0x110
#define PFB_OV0_BUF1_BASE_ADRS 0x111
#define PFB_OV0_BUF2_BASE_ADRS 0x112
#define PFB_OV0_BUF3_BASE_ADRS 0x113
#define PFB_OV0_BUF4_BASE_ADRS 0x114
#define PFB_OV0_BUF5_BASE_ADRS 0x115
#define PFB_OV0_VID_BUF_PITCH0_VALUE 0x118
#define PFB_OV0_VID_BUF_PITCH1_VALUE 0x119
#define PFB_OV0_AUTO_FLIP_CNTL 0x11C
#define PFB_OV0_AUTO_FLIP_BUF0 0x00000200
#define PFB_OV0_AUTO_FLIP_BUF3 0x00000243
#define PFB_OV0_DEINTERLACE_PATTERN 0x11D
#define PFB_OV0_H_INC 0x120
#define PFB_OV0_STEP_BY 0x121
#define PFB_OV0_P1_H_ACCUM_INIT 0x122
#define PFB_OV0_P23_H_ACCUM_INIT 0x123
#define PFB_OV0_P1_X_START_END 0x125
#define PFB_OV0_P2_X_START_END 0x126
#define PFB_OV0_P3_X_START_END 0x127
#define PFB_OV0_FILTER_CNTL 0x128
#define PFB_OV0_FILTER_EN 0x0000000f
#define PFB_OV0_GRPH_KEY_CLR_LOW 0x13B
#define PFB_OV0_GRPH_KEY_CLR_HIGH 0x13C
#define PFB_OV0_KEY_CNTL 0x13D
#define PFB_OV0_KEY_EN 0x00000121

#define PFB_DISP_MERGE_CNTL 0x358
#define PFB_DISP_MERGE_EN 0xffff0000

static const int pfb_bufaddr_regs_tbl[2][3] = {
  {PFB_OV0_BUF0_BASE_ADRS, PFB_OV0_BUF1_BASE_ADRS, PFB_OV0_BUF2_BASE_ADRS},
  {PFB_OV0_BUF3_BASE_ADRS, PFB_OV0_BUF4_BASE_ADRS, PFB_OV0_BUF5_BASE_ADRS},
};

/* Enumerations */

typedef enum {
  FB_TYPE_M64,
  FB_TYPE_PFB
} fb_type_t;

typedef enum {
  BUF_MODE_MULTI,
  BUF_MODE_MULTI_FAILED,
  BUF_MODE_SINGLE,
  BUF_MODE_DOUBLE
} buf_mode_t;

/* Structures */

struct pgx64_overlay_s {
  int x, y, width, height;
  Pixmap p;
  struct pgx64_overlay_s *next;
};
typedef struct pgx64_overlay_s pgx64_overlay_t;

typedef struct {
  video_driver_class_t vo_driver_class;

  xine_t *xine;
  config_values_t *config;
} pgx64_driver_class_t;

typedef struct {
  vo_frame_t vo_frame;

  int lengths[3], stripe_lengths[3], stripe_offsets[3], buffers[3];
  int width, height, format, pitch, native_format, planes, procs_en;
  double ratio;
  uint8_t *buffer_ptrs[3];
} pgx64_frame_t;

typedef struct {
  vo_driver_t vo_driver;
  vo_scale_t vo_scale;

  pgx64_driver_class_t *class;

  Display *display;
  int screen, depth;
  Drawable drawable;
  Dga_drawable dgadraw;
  GC gc;
  Visual *visual;
  Colormap cmap;

  fb_type_t fb_type;
  int devfd, fb_width, fb_depth, free_top, free_bottom, free_mark;
  int buffers[2][3];
  uint32_t yuv12_native_format, yuv12_align;
  uint32_t vyuy422_native_format, vyuy422_align;
  uint8_t *vbase, *buffer_ptrs[2][3];
  volatile uint32_t *vregs;

  pgx64_frame_t *current;
  pgx64_frame_t *multibuf[MAX_MULTIBUF_FRAMES];
  pgx64_frame_t *detained[MAX_DETAINED_FRAMES];
  int multibuf_frames, detained_frames;

  int chromakey_en, chromakey_changed, chromakey_regen_needed;
  pthread_mutex_t chromakey_mutex;
  pgx64_overlay_t *first_overlay;

  buf_mode_t buf_mode;
  int multibuf_en, dblbuf_select, delivered_format;
  int colour_key, colour_key_rgb, brightness, saturation, deinterlace_en;

  alphablend_t alphablend_extra_data;
} pgx64_driver_t;

/*
 * Dummy X11 error handler
 */

static int dummy_error_handler(Display *disp, XErrorEvent *errev)
{
  return 0;
}

/*
 * Setup X11/DGA
 */

static int setup_dga(pgx64_driver_t *this)
{
  Atom VIDEO_OVERLAY_WINDOW, VIDEO_OVERLAY_IN_USE, type_return;
  int format_return;
  unsigned long nitems_return, bytes_after_return;
  unsigned char *prop_return;
  XWindowAttributes win_attrs;

  XLockDisplay(this->display);
  this->dgadraw = XDgaGrabDrawable(this->display, this->drawable);
  if (!this->dgadraw) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: can't grab DGA drawable for video window\n"));
    XUnlockDisplay(this->display);
    return 0;
  }

  /* If the framebuffer hasn't already been mapped then open it and check it's
   * a supported type. We don't use the file descriptor returned from
   * dga_draw_devfd() because the FBIOVERTICAL ioctl doesn't work with it.
   */
  if (!this->vbase) {
    char *devname;
    struct vis_identifier ident;

    DGA_DRAW_LOCK(this->dgadraw, -1);
    devname = dga_draw_devname(this->dgadraw);
    DGA_DRAW_UNLOCK(this->dgadraw);

    if ((this->devfd = xine_open_cloexec(devname, O_RDWR)) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: can't open framebuffer device '%s'\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }

    if (ioctl(this->devfd, VIS_GETIDENTIFIER, &ident) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: ioctl failed (VIS_GETIDENTIFIER), bad device (%s)\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }

    if (strcmp("SUNWm64", ident.name) == 0) {
      this->fb_type = FB_TYPE_M64;
    }
    else if (strcmp("SUNWpfb", ident.name) == 0) {
      this->fb_type = FB_TYPE_PFB;
    }
    else {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: '%s' is not a xvr100/pgx64/pgx24 framebuffer device\n"), devname);
      XDgaUnGrabDrawable(this->dgadraw);
      XUnlockDisplay(this->display);
      return 0;
    }
  }

  VIDEO_OVERLAY_WINDOW = XInternAtom(this->display, "VIDEO_OVERLAY_WINDOW", False);
  VIDEO_OVERLAY_IN_USE = XInternAtom(this->display, "VIDEO_OVERLAY_IN_USE", False);

  XGrabServer(this->display);

  if (XGetWindowProperty(this->display, RootWindow(this->display, this->screen), VIDEO_OVERLAY_WINDOW, 0, 1, False, XA_WINDOW, &type_return, &format_return, &nitems_return, &bytes_after_return, &prop_return) == Success) {
    if ((type_return == XA_WINDOW) && (format_return == 32) && (nitems_return == 1)) {
      Window wins = *(Window *)(void *)prop_return;
      XErrorHandler old_error_handler;

      old_error_handler = XSetErrorHandler(dummy_error_handler);
      if (XGetWindowProperty(this->display, wins, VIDEO_OVERLAY_IN_USE, 0, 0, False, AnyPropertyType, &type_return, &format_return, &nitems_return, &bytes_after_return, &prop_return) == Success) {
        XFree(prop_return);
        if (type_return != None) {
          xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: video overlay on this screen is already in use\n"));
          close(this->devfd);
          XSetErrorHandler(old_error_handler);
          XUngrabServer(this->display);
          XDgaUnGrabDrawable(this->dgadraw);
          XUnlockDisplay(this->display);
          return 0;
        }
      }
      XSetErrorHandler(old_error_handler);
    }
  }

  if (!(XChangeProperty(this->display, RootWindow(this->display, this->screen), VIDEO_OVERLAY_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&this->drawable, 1) &&
        XChangeProperty(this->display, this->drawable, VIDEO_OVERLAY_IN_USE, XA_STRING, 8, PropModeReplace, NULL, 0))) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: unable to set window properties\n"));
    close(this->devfd);
    XUngrabServer(this->display);
    XDgaUnGrabDrawable(this->dgadraw);
    XUnlockDisplay(this->display);
    return 0;
  }
  XUngrabServer(this->display);

  this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
  XGetWindowAttributes(this->display, this->drawable, &win_attrs);
  this->depth  = win_attrs.depth;
  this->visual = win_attrs.visual;
  this->cmap   = XCreateColormap(this->display, this->drawable, this->visual, AllocNone);
  XSetWindowColormap(this->display, this->drawable, this->cmap);
  XUnlockDisplay(this->display);

  return 1;
}

/*
 * Cleanup X11/DGA
 */

static void cleanup_dga(pgx64_driver_t *this)
{
  XLockDisplay(this->display);
  XFreeColormap(this->display, this->cmap);
  XFreeGC(this->display, this->gc);
  XDeleteProperty(this->display, this->drawable, XInternAtom(this->display, "VIDEO_OVERLAY_WINDOW", True));
  XDeleteProperty(this->display, this->drawable, XInternAtom(this->display, "VIDEO_OVERLAY_IN_USE", True));
  XDgaUnGrabDrawable(this->dgadraw);
  XUnlockDisplay(this->display);
}

/*
 * Dispose of any allocated image data within a pgx64_frame_t
 */

static void dispose_frame_internals(pgx64_frame_t *frame)
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
}

/*
 * Update colour_key_rgb using the colour_key index
 */

static void update_colour_key_rgb(pgx64_driver_t *this)
{
  XColor colour;

  colour.pixel = this->colour_key;
  XQueryColor(this->display, this->cmap, &colour);
  this->colour_key_rgb = ((colour.red & 0xff00) << 8) | (colour.green & 0xff00) | ((colour.blue & 0xff00) >> 8);
}

/*
 * Paint the output area with black borders, colour key, and any chorma keyed
 * overlays
 */

static void draw_overlays(pgx64_driver_t *this)
{
  pgx64_overlay_t *ovl;

  ovl = this->first_overlay;
  XLockDisplay(this->display);
  while (ovl != NULL) {
    XCopyArea(this->display, ovl->p, this->drawable, this->gc, 0, 0, ovl->width, ovl->height, this->vo_scale.output_xoffset + ovl->x, this->vo_scale.output_yoffset + ovl->y);
    ovl = ovl->next;
  }
  XFlush(this->display);
  XUnlockDisplay(this->display);
}

static void repaint_output_area(pgx64_driver_t *this)
{
  int i;

  XLockDisplay(this->display);
  XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
  for (i=0; i<4; i++) {
    XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[i].x, this->vo_scale.border[i].y, this->vo_scale.border[i].w, this->vo_scale.border[i].h);
  }

  XSetForeground(this->display, this->gc, this->colour_key);
  XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset, this->vo_scale.output_yoffset, this->vo_scale.output_width, this->vo_scale.output_height);
  XFlush(this->display);
  XUnlockDisplay(this->display);

  pthread_mutex_lock(&this->chromakey_mutex);
  if (this->chromakey_en) {
    draw_overlays(this);
  }
  pthread_mutex_unlock(&this->chromakey_mutex);
}

/*
 * Reset video memory allocator and release detained frames
 */

static void vram_reset(pgx64_driver_t *this)
{
  int i;

  this->free_mark = this->free_top;

  for (i=0; i<this->multibuf_frames; i++) {
    this->multibuf[i]->procs_en = 0;
  }
  this->multibuf_frames = 0;

  for (i=0; i<this->detained_frames; i++) {
    this->detained[i]->vo_frame.free(&this->detained[i]->vo_frame);
  }
  this->detained_frames = 0;
}

/*
 * Allocate a portion of video memory
 */

static int vram_alloc(pgx64_driver_t *this, int size)
{
  if (this->free_mark - size < this->free_bottom) {
    return -1;
  }
  else {
    return this->free_mark -= size;
  }
}

/*
 * XINE VIDEO DRIVER FUNCTIONS
 */

static void pgx64_frame_proc_frame(vo_frame_t *frame_gen)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;
  int i;

  frame->vo_frame.proc_called = 1;

  if (frame->procs_en) {
    for (i=0; i<frame->planes; i++) {
      memcpy(frame->buffer_ptrs[i], frame->vo_frame.base[i], frame->lengths[i]);
    }
  }
  else {
    frame->vo_frame.proc_frame = NULL;
    frame->vo_frame.proc_slice = NULL;
  }
}

static void pgx64_frame_proc_slice(vo_frame_t *frame_gen, uint8_t **src)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;
  int i, len;

  frame->vo_frame.proc_called = 1;

  if (frame->procs_en) {
    for (i=0; i<frame->planes; i++) {
      len = (frame->lengths[i] - frame->stripe_offsets[i] < frame->stripe_lengths[i]) ? frame->lengths[i] - frame->stripe_offsets[i] : frame->stripe_lengths[i];
      memcpy(frame->buffer_ptrs[i]+frame->stripe_offsets[i], src[i], len);
      frame->stripe_offsets[i] += len;
    }
  }
  else {
    frame->vo_frame.proc_frame = NULL;
    frame->vo_frame.proc_slice = NULL;
  }
}

static void pgx64_frame_field(vo_frame_t *frame_gen, int which_field)
{
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/
}

static void pgx64_frame_dispose(vo_frame_t *frame_gen)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  dispose_frame_internals(frame);
  free(frame);
}

static uint32_t pgx64_get_capabilities(vo_driver_t *this_gen)
{
  /*pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;*/

  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS | VO_CAP_SATURATION;
}

static vo_frame_t *pgx64_alloc_frame(vo_driver_t *this_gen)
{
  /*pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;*/
  pgx64_frame_t *frame;

  frame = calloc(1, sizeof(pgx64_frame_t));
  if (!frame) {
    return NULL;
  }

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.field      = pgx64_frame_field;
  frame->vo_frame.dispose    = pgx64_frame_dispose;

  return (vo_frame_t *)frame;
}

static void pgx64_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen, uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if ((width != frame->width) ||
      (height != frame->height) ||
      (ratio != frame->ratio) ||
      (format != frame->format)) {
    int i;

    frame->procs_en = 0;

    dispose_frame_internals(frame);

    frame->width = width;
    frame->height = height;
    frame->ratio = ratio;
    frame->format = format;

    switch (format) {
      case XINE_IMGFMT_YUY2:
        frame->native_format = this->vyuy422_native_format;
        frame->pitch = ((width + this->vyuy422_align - 1) / this->vyuy422_align) * this->vyuy422_align;
        frame->planes = 1;
        frame->vo_frame.pitches[0] = frame->pitch * 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->vo_frame.base[0] = memalign(8, frame->lengths[0]);
        break;

      case XINE_IMGFMT_YV12:
        frame->native_format = this->yuv12_native_format;
        frame->pitch = ((width + this->yuv12_align - 1) / this->yuv12_align) * this->yuv12_align;
        frame->planes = 3;
        frame->vo_frame.pitches[0] = frame->pitch;
        frame->vo_frame.pitches[1] = ((((width + 1) / 2) + this->yuv12_align - 1) / this->yuv12_align) * this->yuv12_align;
        frame->vo_frame.pitches[2] = ((((width + 1) / 2) + this->yuv12_align - 1) / this->yuv12_align) * this->yuv12_align;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->lengths[1] = frame->vo_frame.pitches[1] * ((height + 1) / 2);
        frame->lengths[2] = frame->vo_frame.pitches[2] * ((height + 1) / 2);
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->stripe_lengths[1] = frame->vo_frame.pitches[1] * 8;
        frame->stripe_lengths[2] = frame->vo_frame.pitches[2] * 8;
        frame->vo_frame.base[0] = memalign(8, frame->lengths[0]);
        frame->vo_frame.base[1] = memalign(8, frame->lengths[1]);
        frame->vo_frame.base[2] = memalign(8, frame->lengths[2]);
        break;
    }

    for (i=0; i<frame->planes; i++) {
      if (!frame->vo_frame.base[i]) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: frame plane malloc failed\n");
        _x_abort();
      }
    }
  }

  frame->stripe_offsets[0] = 0;
  frame->stripe_offsets[1] = 0;
  frame->stripe_offsets[2] = 0;
}

static void pgx64_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

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

    vram_reset(this);
    if (this->multibuf_en) {
      this->buf_mode = BUF_MODE_MULTI;
    }
    else {
      this->buf_mode = BUF_MODE_MULTI_FAILED;
    }
  }

  XLockDisplay(this->display);
  DGA_DRAW_LOCK(this->dgadraw, -1);
  this->vo_scale.force_redraw = this->vo_scale.force_redraw || DGA_DRAW_MODIF(this->dgadraw);
  DGA_DRAW_UNLOCK(this->dgadraw);
  XUnlockDisplay(this->display);

  if (_x_vo_scale_redraw_needed(&this->vo_scale)) {
    short int *cliprects, wx0, wy0, wx1, wy1, cx0, cy0, cx1, cy1;
    int dgavis;

    _x_vo_scale_compute_output_size(&this->vo_scale);
    repaint_output_area(this);
    this->chromakey_regen_needed = 1;

    XLockDisplay(this->display);
    DGA_DRAW_LOCK(this->dgadraw, -1);
    dgavis = DGA_VIS_FULLY_OBSCURED;
    cliprects = dga_draw_clipinfo(this->dgadraw);

    wx0 = this->vo_scale.gui_win_x + this->vo_scale.output_xoffset;
    wy0 = this->vo_scale.gui_win_y + this->vo_scale.output_yoffset;
    wx1 = wx0 + this->vo_scale.output_width;
    wy1 = wy0 + this->vo_scale.output_height;

    while ((cy0 = *cliprects++) != DGA_Y_EOL) {
      cy1 = *cliprects++;
      while ((cx0 = *cliprects++) != DGA_X_EOL) {
        cx1 = *cliprects++;

        if ((cx0 < wx1) && (cy0 < wy1) && (cx1 > wx0) && (cy1 > wy0)) {
          dgavis = DGA_VIS_PARTIALLY_OBSCURED;
        }
        if ((cx0 <= wx0) && (cy0 <= wy0) && (cx1 >= wx1) && (cy1 >= wy1)) {
          dgavis = DGA_VIS_UNOBSCURED;
        }
      }
    }

    DGA_DRAW_UNLOCK(this->dgadraw);
    XUnlockDisplay(this->display);

    switch (this->fb_type) {
      case FB_TYPE_M64:
        this->vregs[M64_BUS_CNTL] |= le2me_32(M64_BUS_EXT_REG_EN);

        if (dgavis == DGA_VIS_FULLY_OBSCURED) {
          this->vregs[M64_OVERLAY_SCALE_CNTL] = 0;
        }
        else {
          this->vregs[M64_OVERLAY_X_Y_START] = le2me_32((wx0 << 16) | wy0 | M64_OVERLAY_X_Y_LOCK);
          this->vregs[M64_OVERLAY_X_Y_END] = le2me_32(((wx1 - 1) << 16) | (wy1 - 1));
          this->vregs[M64_OVERLAY_GRAPHICS_KEY_CLR] = le2me_32(this->colour_key);
          this->vregs[M64_OVERLAY_GRAPHICS_KEY_MSK] = le2me_32(0xffffffff >> (32 - this->fb_depth));
          this->vregs[M64_OVERLAY_KEY_CNTL] = le2me_32(M64_OVERLAY_KEY_EN);
          this->vregs[M64_OVERLAY_SCALE_INC] = le2me_32((((frame->width << 12) / this->vo_scale.output_width) << 16) | (((this->deinterlace_en ? frame->height/2 : frame->height) << 12) / this->vo_scale.output_height));

          this->vregs[M64_SCALER_HEIGHT_WIDTH] = le2me_32((frame->width << 16) | (this->deinterlace_en ? frame->height/2 : frame->height));
          this->vregs[M64_SCALER_COLOUR_CNTL] = le2me_32((this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
          this->vregs[M64_SCALER_H_COEFF0] = le2me_32(M64_SCALER_H_COEFF0_DEFAULT);
          this->vregs[M64_SCALER_H_COEFF1] = le2me_32(M64_SCALER_H_COEFF1_DEFAULT);
          this->vregs[M64_SCALER_H_COEFF2] = le2me_32(M64_SCALER_H_COEFF2_DEFAULT);
          this->vregs[M64_SCALER_H_COEFF3] = le2me_32(M64_SCALER_H_COEFF3_DEFAULT);
          this->vregs[M64_SCALER_H_COEFF4] = le2me_32(M64_SCALER_H_COEFF4_DEFAULT);
          this->vregs[M64_SCALER_BUF_PITCH] = le2me_32(this->deinterlace_en ? frame->pitch*2 : frame->pitch);

          this->vregs[M64_VIDEO_FORMAT] = le2me_32(frame->native_format);
          this->vregs[M64_OVERLAY_SCALE_CNTL] = le2me_32(M64_OVERLAY_SCALE_EN);
        }

        if ((dgavis == DGA_VIS_UNOBSCURED) && !this->chromakey_en) {
          this->vregs[M64_OVERLAY_EXCLUSIVE_VERT] = le2me_32((wy0 - 1) | ((wy1 - 1) << 16));
          this->vregs[M64_OVERLAY_EXCLUSIVE_HORZ] = le2me_32(((wx0 + 7) / 8) | ((wx1 / 8) << 8) | (((this->fb_width / 8) - (wx1 / 8)) << 16) | M64_OVERLAY_EXCLUSIVE_EN);
        }
        else {
          this->vregs[M64_OVERLAY_EXCLUSIVE_HORZ] = 0;
        }
        break;

      case FB_TYPE_PFB:
        if (dgavis == DGA_VIS_FULLY_OBSCURED) {
          this->vregs[PFB_OV0_SCALE_CNTL] = 0;
        }
        else {
          int h_inc, h_step, ecp_div;

          this->vregs[PFB_CLOCK_CNTL_INDEX] = (this->vregs[PFB_CLOCK_CNTL_INDEX] & ~0x0000003f) | 0x00000008;
          ecp_div = (this->vregs[PFB_CLOCK_CNTL_DATA] >> 8) & 0x3;
          h_inc = (frame->width << (12 + ecp_div)) / this->vo_scale.output_width;
          h_step = 1;

          while (h_inc > 0x1fff) {
            h_inc >>= 1;
            h_step++;
          }

          this->vregs[PFB_OV0_REG_LOAD_CNTL] = PFB_OV0_REG_LOAD_LOCK;
          while (!(this->vregs[PFB_OV0_REG_LOAD_CNTL] & PFB_OV0_REG_LOAD_LOCK_READBACK)) {}

          this->vregs[PFB_DISP_MERGE_CNTL] = PFB_DISP_MERGE_EN;
          this->vregs[PFB_OV0_Y_X_START] = (wy0 << 16) | wx0;
          this->vregs[PFB_OV0_Y_X_END] = ((wy1 - 1) << 16) | (wx1 - 1);
          this->vregs[PFB_OV0_V_INC] = ((this->deinterlace_en ? frame->height/2 : frame->height) << 20) / this->vo_scale.output_height;
          this->vregs[PFB_OV0_P1_V_ACCUM_INIT] = 0x00180001;
          this->vregs[PFB_OV0_P23_V_ACCUM_INIT] = 0x00180001;
          this->vregs[PFB_OV0_P1_BLANK_LINES_AT_TOP] = (((this->deinterlace_en ? frame->height/2 : frame->height) - 1) << 16) | 0xfff;
          this->vregs[PFB_OV0_P23_BLANK_LINES_AT_TOP] = (((this->deinterlace_en ? frame->height/2 : frame->height) / 2 - 1) << 16) | 0x7ff;
          this->vregs[PFB_OV0_BASE_ADDR] = (this->vregs[PFB_MC_FB_LOCATION] & 0xffff) << 16;
          this->vregs[PFB_OV0_VID_BUF_PITCH0_VALUE] = this->deinterlace_en ? frame->vo_frame.pitches[0]*2 : frame->vo_frame.pitches[0];
          this->vregs[PFB_OV0_VID_BUF_PITCH1_VALUE] = this->deinterlace_en ? frame->vo_frame.pitches[1]*2 : frame->vo_frame.pitches[1];
          this->vregs[PFB_OV0_DEINTERLACE_PATTERN] = 0x000aaaaa;
          this->vregs[PFB_OV0_H_INC] = ((h_inc / 2) << 16) | h_inc;
          this->vregs[PFB_OV0_STEP_BY] = (h_step << 8) | h_step;
          this->vregs[PFB_OV0_P1_H_ACCUM_INIT] = (((0x00005000 + h_inc) << 7) & 0x000f8000) | (((0x00005000 + h_inc) << 15) & 0xf0000000);
          this->vregs[PFB_OV0_P23_H_ACCUM_INIT] = (((0x0000A000 + h_inc) << 6) & 0x000f8000) | (((0x0000A000 + h_inc) << 14) & 0x70000000);
          this->vregs[PFB_OV0_P1_X_START_END] = frame->width - 1;
          this->vregs[PFB_OV0_P2_X_START_END] = (frame->width / 2) - 1;
          this->vregs[PFB_OV0_P3_X_START_END] = (frame->width / 2) - 1;
          this->vregs[PFB_OV0_FILTER_CNTL] = PFB_OV0_FILTER_EN;
          this->vregs[PFB_OV0_GRPH_KEY_CLR_LOW] = this->colour_key_rgb;
          this->vregs[PFB_OV0_GRPH_KEY_CLR_HIGH] = this->colour_key_rgb | 0xff000000;
          this->vregs[PFB_OV0_KEY_CNTL] = PFB_OV0_KEY_EN;
          this->vregs[PFB_OV0_SCALE_CNTL] = PFB_OV0_SCALE_EN | frame->native_format;

          this->vregs[PFB_OV0_REG_LOAD_CNTL] = 0;
        }
        break;
    }
  }

  if (this->buf_mode == BUF_MODE_MULTI) {
    int i;

    if (!frame->procs_en) {
      for (i=0; i<frame->planes; i++) {
        if ((frame->buffers[i] = vram_alloc(this, frame->lengths[i])) < 0) {
          if (this->detained_frames < MAX_DETAINED_FRAMES) {
            this->detained[this->detained_frames++] = frame;
            return;
          }
          else {
            xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Warning: low video memory, multi-buffering disabled\n"));
            vram_reset(this);
            this->buf_mode = BUF_MODE_MULTI_FAILED;
            break;
          }
        }
        else {
          frame->buffer_ptrs[i] = this->vbase + frame->buffers[i];
          memcpy(frame->buffer_ptrs[i], frame->vo_frame.base[i], frame->lengths[i]);
        }
      }

      if (this->buf_mode == BUF_MODE_MULTI) {
        frame->procs_en = 1;
        frame->vo_frame.proc_frame = pgx64_frame_proc_frame;
        frame->vo_frame.proc_slice = pgx64_frame_proc_slice;
        _x_assert(this->multibuf_frames < MAX_MULTIBUF_FRAMES);
        this->multibuf[this->multibuf_frames++] = frame;
      }
    }

    for (i=0; i<frame->planes; i++) {
      this->buffers[this->dblbuf_select][i] = frame->buffers[i];
    }
  }

  if (this->buf_mode != BUF_MODE_MULTI) {
    int i;

    if (this->buf_mode == BUF_MODE_MULTI_FAILED) {
      for (i=0; i<frame->planes; i++) {
        if ((this->buffers[0][i] = vram_alloc(this, frame->lengths[i])) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: insuffucient video memory\n"));
          if (this->current != NULL) {
            this->current->vo_frame.free(&this->current->vo_frame);
            this->current = NULL;
          }
          frame->vo_frame.free(&frame->vo_frame);
          return;
        }
        else {
          this->buffer_ptrs[0][i] = this->vbase + this->buffers[0][i];
        }
      }

      this->buf_mode = BUF_MODE_DOUBLE;
      for (i=0; i<frame->planes; i++) {
        if ((this->buffers[1][i] = vram_alloc(this, frame->lengths[i])) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Warning: low video memory, double-buffering disabled\n"));
          this->buf_mode = BUF_MODE_SINGLE;
          break;
        }
        else {
          this->buffer_ptrs[1][i] = this->vbase + this->buffers[1][i];
        }
      }

      if (this->buf_mode == BUF_MODE_SINGLE) {
        for (i=0; i<frame->planes; i++) {
          this->buffers[1][i] = this->buffers[0][i];
          this->buffer_ptrs[1][i] = this->vbase + this->buffers[1][i];
        }
      }
    }

    for (i=0; i<frame->planes; i++) {
      memcpy(this->buffer_ptrs[this->dblbuf_select][i], frame->vo_frame.base[i], frame->lengths[i]);
    }
  }

  switch (this->fb_type) {
    case FB_TYPE_M64:
      this->vregs[m64_bufaddr_regs_tbl[this->dblbuf_select][0]] = le2me_32(this->buffers[this->dblbuf_select][0]);
      this->vregs[m64_bufaddr_regs_tbl[this->dblbuf_select][1]] = le2me_32(this->buffers[this->dblbuf_select][1]);
      this->vregs[m64_bufaddr_regs_tbl[this->dblbuf_select][2]] = le2me_32(this->buffers[this->dblbuf_select][2]);
      this->vregs[M64_CAPTURE_CONFIG] = this->dblbuf_select ? le2me_32(M64_CAPTURE_CONFIG_BUF1) : le2me_32(M64_CAPTURE_CONFIG_BUF0);
      ioctl(this->devfd, FBIOVERTICAL);
      break;

    case FB_TYPE_PFB:
      this->vregs[pfb_bufaddr_regs_tbl[this->dblbuf_select][0]] = this->buffers[this->dblbuf_select][0];
      this->vregs[pfb_bufaddr_regs_tbl[this->dblbuf_select][1]] = this->buffers[this->dblbuf_select][1] | 0x00000001;
      this->vregs[pfb_bufaddr_regs_tbl[this->dblbuf_select][2]] = this->buffers[this->dblbuf_select][2] | 0x00000001;
      this->vregs[PFB_OV0_AUTO_FLIP_CNTL] = this->dblbuf_select ? PFB_OV0_AUTO_FLIP_BUF3 : PFB_OV0_AUTO_FLIP_BUF0;
      ioctl(this->devfd, FBIOVERTICAL); /* Two vertical retraces are required for the new buffer to become active */
      ioctl(this->devfd, FBIOVERTICAL);
      break;
  }

  this->dblbuf_select = 1 - this->dblbuf_select;


  if (this->current != NULL) {
    this->current->vo_frame.free(&this->current->vo_frame);
  }
  this->current = frame;
}

static void pgx64_overlay_begin(vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;

  if ((this->chromakey_en) && (changed || this->chromakey_regen_needed)) {
    pgx64_overlay_t *ovl, *next_ovl;

    this->chromakey_regen_needed = 0;
    this->chromakey_changed = 1;
    pthread_mutex_lock(&this->chromakey_mutex);

    XLockDisplay(this->display);
    XSetForeground(this->display, this->gc, this->colour_key);
    ovl = this->first_overlay;
    while (ovl != NULL) {
      next_ovl = ovl->next;
      XFreePixmap(this->display, ovl->p);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset + ovl->x, this->vo_scale.output_yoffset + ovl->y, ovl->width, ovl->height);
      free(ovl);
      ovl = next_ovl;
    }
    this->first_overlay = NULL;
    XFreeColors(this->display, this->cmap, NULL, 0, ~0);
    XUnlockDisplay(this->display);
  }
}

#define scale_up(n)       ((n) << 16)
#define scale_down(n)     ((n) >> 16)
#define saturate(n, l, u) ((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))

static void pgx64_overlay_key_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  pgx64_overlay_t *ovl, **chromakey_ptr;
  int x_scale, y_scale, i, x, y, len, width;
  int use_clip_palette, max_palette_colour[2];
  unsigned long palette[2][OVL_PALETTE_SIZE];

  x_scale = scale_up(this->vo_scale.output_width) / frame->width;
  y_scale = scale_up(this->vo_scale.output_height) / frame->height;

  max_palette_colour[0] = -1;
  max_palette_colour[1] = -1;

  ovl = (pgx64_overlay_t *)malloc(sizeof(pgx64_overlay_t));
  if (!ovl) {
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: overlay malloc failed\n");
    return;
  }
  ovl->x = scale_down(overlay->x * x_scale);
  ovl->y = scale_down(overlay->y * y_scale);
  ovl->width = scale_down(overlay->width * x_scale);
  ovl->height = scale_down(overlay->height * y_scale);
  ovl->next = NULL;

  XLockDisplay(this->display);
  ovl->p = XCreatePixmap(this->display, this->drawable, ovl->width, ovl->height, this->depth);
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

      if (overlay->rle[i].color > max_palette_colour[use_clip_palette]) {
        int j;
        clut_t *src_clut;
        uint8_t *src_trans;

        if (use_clip_palette) {
          src_clut = (clut_t *)&overlay->hili_color;
          src_trans = (uint8_t *)&overlay->hili_trans;
        }
        else {
          src_clut = (clut_t *)&overlay->color;
          src_trans = (uint8_t *)&overlay->trans;
        }

        for (j=max_palette_colour[use_clip_palette]+1; j<=overlay->rle[i].color; j++) {
          if (src_trans[j]) {
            XColor col;
            int y, u, v, r, g, b;

            y = saturate(src_clut[j].y, 16, 235);
            u = saturate(src_clut[j].cb, 16, 240);
            v = saturate(src_clut[j].cr, 16, 240);
            y = (9 * y) / 8;
            r = y + (25 * v) / 16 - 218;
            g = y + (-13 * v) / 16 + (-25 * u) / 64 + 136;
            b = y + 2 * u - 274;

            col.red = saturate(r, 0, 255) << 8;
            col.green = saturate(g, 0, 255) << 8;
            col.blue = saturate(b, 0, 255) << 8;
            if (XAllocColor(this->display, this->cmap, &col)) {
              palette[use_clip_palette][j] = col.pixel;
            }
            else {
              if (src_clut[j].y > 127) {
                palette[use_clip_palette][j] = WhitePixel(this->display, this->screen);
              }
              else {
                palette[use_clip_palette][j] = BlackPixel(this->display, this->screen);
              }
            }
          }
          else {
            palette[use_clip_palette][j] = this->colour_key;
          }
        }
        max_palette_colour[use_clip_palette] = overlay->rle[i].color;
      }

      XSetForeground(this->display, this->gc, palette[use_clip_palette][overlay->rle[i].color]);
      XFillRectangle(this->display, ovl->p, this->gc, scale_down(x * x_scale), scale_down(y * y_scale), scale_down((x + width) * x_scale) - scale_down(x * x_scale), scale_down((y + 1) * y_scale) - scale_down(y * y_scale));
      x += width;
      if (x == overlay->width) {
        x = 0;
        y++;
      }
    }
  }

  if (y < overlay->height) {
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Notice: RLE data doesn't extend to height of overlay\n");
    XFillRectangle(this->display, ovl->p, this->gc, scale_down(x * x_scale), scale_down(y * y_scale), ovl->width, scale_down(overlay->height * y_scale) - scale_down(y * y_scale));
  }
  XUnlockDisplay(this->display);

  chromakey_ptr = &this->first_overlay;
  while ( *chromakey_ptr != NULL) {
    chromakey_ptr = &( *chromakey_ptr)->next;
  }
  *chromakey_ptr = ovl;
}

static void pgx64_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if (overlay->rle) {
    if (this->chromakey_changed) {
      pgx64_overlay_key_blend(this_gen, frame_gen, overlay);
    }
    else {
      if (frame->vo_frame.proc_slice == pgx64_frame_proc_slice) {
        /* FIXME: Implement out of place alphablending functions for better performance */
        switch (frame->format) {
          case XINE_IMGFMT_YV12: {
            _x_blend_yuv(frame->buffer_ptrs, overlay, frame->width, frame->height, frame->vo_frame.pitches, &this->alphablend_extra_data);
          }
          break;

          case XINE_IMGFMT_YUY2: {
            _x_blend_yuy2(frame->buffer_ptrs[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0], &this->alphablend_extra_data);
          }
          break;
        }
      }
      else {
        switch (frame->format) {
          case XINE_IMGFMT_YV12: {
            _x_blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches, &this->alphablend_extra_data);
          }
          break;

          case XINE_IMGFMT_YUY2: {
            _x_blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0], &this->alphablend_extra_data);
          }
          break;
        }
      }
    }
  }
}

static void pgx64_overlay_end(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/

  if (this->chromakey_changed) {
    draw_overlays(this);
    pthread_mutex_unlock(&this->chromakey_mutex);
    this->chromakey_changed = 0;
  }
}

static int pgx64_get_property(vo_driver_t *this_gen, int property)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_INTERLACED:
      return this->deinterlace_en;
    break;

    case VO_PROP_ASPECT_RATIO:
      return this->vo_scale.user_ratio;
    break;

    case VO_PROP_SATURATION:
      return this->saturation;
    break;

    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    break;

    case VO_PROP_COLORKEY:
      return this->colour_key;
    break;

    default:
      return 0;
    break;
  }
}

static int pgx64_set_property(vo_driver_t *this_gen, int property, int value)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

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

    case VO_PROP_SATURATION: {
      this->saturation = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_BRIGHTNESS: {
      this->brightness = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_COLORKEY: {
      this->colour_key = value;
      this->vo_scale.force_redraw = 1;
    }
    break;
  }
  return value;
}

static void pgx64_get_property_min_max(vo_driver_t *this_gen, int property, int *min, int *max)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_SATURATION: {
      *min = 0;
      *max = 31;
    }
    break;

    case VO_PROP_BRIGHTNESS: {
      *min = -64;
      *max = 63;
    }
    break;

    case VO_PROP_COLORKEY: {
      *min = 0;
      *max = 0xffffffff >> (32 - this->fb_depth);
    }

    default:
      *min = 0;
      *max = 0;
    break;
  }
}

static int pgx64_gui_data_exchange(vo_driver_t *this_gen, int data_type, void *data)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

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
      repaint_output_area(this);
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

static int pgx64_redraw_needed(vo_driver_t *this_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  int modif;

  XLockDisplay(this->display);
  DGA_DRAW_LOCK(this->dgadraw, -1);
  modif = DGA_DRAW_MODIF(this->dgadraw);
  DGA_DRAW_UNLOCK(this->dgadraw);
  XUnlockDisplay(this->display);

  if (modif || _x_vo_scale_redraw_needed(&this->vo_scale)) {
    this->vo_scale.force_redraw = 1;
    this->chromakey_regen_needed = 1;
    return 1;
  }

  return 0;
}

static void pgx64_dispose(vo_driver_t *this_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  long page_size;

  cleanup_dga(this);

  page_size = sysconf(_SC_PAGE_SIZE);

  switch (this->fb_type) {
    case FB_TYPE_M64:
      this->vregs[M64_OVERLAY_EXCLUSIVE_HORZ] = 0;
      this->vregs[M64_OVERLAY_SCALE_CNTL] = 0;
      munmap(this->vbase, (((M64_VRAM_MMAPLEN + page_size - 1) / page_size) * page_size));
      break;

    case FB_TYPE_PFB:
      this->vregs[PFB_OV0_SCALE_CNTL] = 0;
      munmap(this->vbase, (((PFB_VRAM_MMAPLEN + page_size - 1) / page_size) * page_size));
      munmap((void *)this->vregs, (((PFB_REGS_MMAPLEN + page_size - 1) / page_size) * page_size));
      break;
  }

  close(this->devfd);

  _x_alphablend_free(&this->alphablend_extra_data);

  free(this);
}

static void pgx64_config_changed(void *user_data, xine_cfg_entry_t *entry)
{
  vo_driver_t *this_gen = (vo_driver_t *)user_data;
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)user_data;

  if (strcmp(entry->key, "video.device.pgx64_colour_key") == 0) {
    pgx64_set_property(this_gen, VO_PROP_COLORKEY, entry->num_value);
    update_colour_key_rgb(this);
  }
  else if (strcmp(entry->key, "video.device.pgx64_chromakey_en") == 0) {
    this->chromakey_en = entry->num_value;
  }
  else if (strcmp(entry->key, "video.device.pgx64_multibuf_en") == 0) {
    this->multibuf_en = entry->num_value;
  }
}

/*
 * XINE VIDEO DRIVER CLASS FUNCTIONS
 */
static const vo_info_t vo_info_pgx64 = {
  10,
  XINE_VISUAL_TYPE_X11
};

static vo_driver_t *pgx64_init_driver(video_driver_class_t *class_gen, const void *visual_gen)
{
  pgx64_driver_class_t *class = (pgx64_driver_class_t *)(void *)class_gen;
  pgx64_driver_t *this;
  struct fbgattr attr;
  long page_size;

  this = calloc(1, sizeof(pgx64_driver_t));
  if (!this) {
    return NULL;
  }

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->vo_driver.get_capabilities     = pgx64_get_capabilities;
  this->vo_driver.alloc_frame          = pgx64_alloc_frame;
  this->vo_driver.update_frame_format  = pgx64_update_frame_format;
  this->vo_driver.overlay_begin        = pgx64_overlay_begin;
  this->vo_driver.overlay_blend        = pgx64_overlay_blend;
  this->vo_driver.overlay_end          = pgx64_overlay_end;
  this->vo_driver.display_frame        = pgx64_display_frame;
  this->vo_driver.get_property         = pgx64_get_property;
  this->vo_driver.set_property         = pgx64_set_property;
  this->vo_driver.get_property_min_max = pgx64_get_property_min_max;
  this->vo_driver.gui_data_exchange    = pgx64_gui_data_exchange;
  this->vo_driver.redraw_needed        = pgx64_redraw_needed;
  this->vo_driver.dispose              = pgx64_dispose;

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

  if (ioctl(this->devfd, FBIOGATTR, &attr) < 0) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("video_out_pgx64: Error: ioctl failed (FBIOGATTR)\n"));
    cleanup_dga(this);
    close(this->devfd);
    free(this);
    return NULL;
  }

  page_size = sysconf(_SC_PAGE_SIZE);

  switch (this->fb_type) {
    case FB_TYPE_M64:
      if ((this->vbase = mmap(NULL, (((M64_VRAM_MMAPLEN + page_size - 1) / page_size) * page_size), PROT_READ | PROT_WRITE, MAP_SHARED, this->devfd, 0)) == MAP_FAILED) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: unable to memory map framebuffer\n");
        cleanup_dga(this);
        close(this->devfd);
        free(this);
        return NULL;
      }

      /* Using the endian swapped register page at 0x00800000 causes the
       * X server to behave abnormally so we use the one at the end of the
       * memory aperture and perform the swap ourselves.
       */
      this->vregs = (uint32_t *)(void *)(this->vbase + 0x007ff800);

      this->fb_width    = attr.fbtype.fb_width;
      this->fb_depth    = attr.fbtype.fb_depth;
      this->free_top    = attr.sattr.dev_specific[0];
      this->free_bottom = attr.sattr.dev_specific[5] + attr.fbtype.fb_size;

      this->yuv12_native_format   = M64_VIDEO_FORMAT_YUV12;
      this->yuv12_align           = 8;
      this->vyuy422_native_format = M64_VIDEO_FORMAT_VYUY422;
      this->vyuy422_align         = 8;
      break;

    case FB_TYPE_PFB:
      if ((this->vbase = mmap(NULL, (((PFB_VRAM_MMAPLEN + page_size - 1) / page_size) * page_size), PROT_READ | PROT_WRITE, MAP_SHARED, this->devfd, PFB_VRAM_MMAPBASE)) == MAP_FAILED) {
        xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: unable to memory map framebuffer\n");
        cleanup_dga(this);
        close(this->devfd);
        free(this);
        return NULL;
      }

      if ((this->vregs = (uint32_t *)(void *)mmap(NULL, (((PFB_REGS_MMAPLEN + page_size - 1) / page_size) * page_size), PROT_READ | PROT_WRITE, MAP_SHARED, this->devfd, PFB_REGS_MMAPBASE)) == MAP_FAILED) {
        xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: unable to memory map framebuffer\n");
        munmap(this->vbase, (((PFB_VRAM_MMAPLEN + page_size - 1) / page_size) * page_size));
        cleanup_dga(this);
        close(this->devfd);
        free(this);
        return NULL;
      }

      this->fb_width    = attr.fbtype.fb_width;
      this->fb_depth    = attr.fbtype.fb_depth;
      this->free_top    = attr.fbtype.fb_size - 0x2000;
      this->free_bottom = ((attr.fbtype.fb_width + 255) / 256) * 256 * attr.fbtype.fb_height * (attr.fbtype.fb_depth / 8);

      this->yuv12_native_format   = PFB_OV0_SCALE_YUV12;
      this->yuv12_align           = 16;
      this->vyuy422_native_format = PFB_OV0_SCALE_VYUY422;
      this->vyuy422_align         = 8;
      break;
  }

  this->colour_key  = class->config->register_num(this->class->config, "video.device.pgx64_colour_key", 1,
    _("video overlay colour key"),
    _("The colour key is used to tell the graphics card where it can overlay the video image. "
      "Try using different values if you see the video showing through other windows."),
    20, pgx64_config_changed, this);
  update_colour_key_rgb(this);
  this->brightness  = 0;
  this->saturation  = 16;
  this->chromakey_en = class->config->register_bool(this->class->config, "video.device.pgx64_chromakey_en", 0,
    _("enable chroma keying"),
    _("Draw OSD graphics on top of the overlay colour key rather than blend them into each frame."),
    20, pgx64_config_changed, this);
  this->multibuf_en = class->config->register_bool(this->class->config, "video.device.pgx64_multibuf_en", 1,
    _("enable multi-buffering"),
    _("Multi buffering increases performance at the expense of using more graphics memory."),
    20, pgx64_config_changed, this);

  pthread_mutex_init(&this->chromakey_mutex, NULL);

  return (vo_driver_t *)this;
}

static void *pgx64_init_class(xine_t *xine, void *visual_gen)
{
  pgx64_driver_class_t *class;

  class = calloc(1, sizeof(pgx64_driver_class_t));
  if (!class) {
    return NULL;
  }

  DGA_INIT();

  class->vo_driver_class.open_plugin     = pgx64_init_driver;
  class->vo_driver_class.identifier      = "pgx64";
  class->vo_driver_class.description     = N_("xine video output plugin for Sun XVR100/PGX64/PGX24 framebuffers");
  class->vo_driver_class.dispose         = default_video_driver_class_dispose;

  class->xine   = xine;
  class->config = xine->config;

  return class;
}

const plugin_info_t xine_plugin_info[] EXPORTED = {
  {PLUGIN_VIDEO_OUT, 22, "pgx64", XINE_VERSION_CODE, &vo_info_pgx64, pgx64_init_class},
  {PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
