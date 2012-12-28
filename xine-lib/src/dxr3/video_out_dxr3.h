/*
 * Copyright (C) 2000-2001 the xine project
 *
 * This file is part of xine, a unix video player.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_X11
#  include <X11/Xlib.h>
#endif

#include <xine/xine_internal.h>
#include <xine/vo_scale.h>
#include "dxr3_scr.h"
#include "dxr3.h"

/* the number of supported encoders */
#define SUPPORTED_ENCODER_COUNT 3


/* plugin structures */
typedef struct encoder_data_s encoder_data_t;
typedef struct spu_encoder_s spu_encoder_t;

typedef enum { ENC_FAME, ENC_RTE, ENC_LAVC } encoder_type;


struct coeff {
  float            k,m;
};

typedef struct dxr3_overlay_s {
  xine_t          *xine;

  int              fd_control;

  int              xoffset;
  int              yoffset;
  int              xcorr;
  int              jitter;
  int              stability;
  int              colorkey;
  float            color_interval;
  int              screen_xres;
  int              screen_yres;
  int              screen_depth;
  int              shrink;

  struct coeff     colcal_upper[3];
  struct coeff     colcal_lower[3];
} dxr3_overlay_t;

typedef struct dxr3_driver_class_s {
  video_driver_class_t  video_driver_class;
  xine_t               *xine;

  int                   visual_type;
  int                   instance;           /* we allow only one instance of this plugin */

  int                   devnum;

  dxr3_scr_t           *scr;                /* to provide dxr3 clocking */
} dxr3_driver_class_t;

typedef struct dxr3_driver_s {
  vo_driver_t          vo_driver;
  dxr3_driver_class_t *class;

  int                  fd_control;
  pthread_mutex_t      video_device_lock;
  int                  fd_video;
  pthread_mutex_t      spu_device_lock;
  int                  fd_spu;              /* to access the relevant dxr3 devices */
  int                  clut_cluttered;      /* to tell spu decoder that it has to restore the palette */

  int                  enhanced_mode;
  int                  swap_fields;         /* swap fields */
  int                  add_bars;            /* add black bars to correct a.r. */

  int                  aspect;
  int                  tv_mode;
  int                  pan_scan;
  int                  overlay_enabled;
  int                  tv_switchable;       /* can switch from overlay<->tvout */
  int                  widescreen_enabled;
  em8300_bcs_t         bcs;

  encoder_data_t      *enc;                 /* mpeg encoder data */
  spu_encoder_t       *spu_enc;             /* spu encoder */
  int                  need_update;         /* the mpeg encoder needs to be updated */

  uint32_t             video_iheight;       /* input height (before adding black bars) */
  uint32_t             video_oheight;       /* output height (after adding black bars) */
  uint32_t             video_width;
  double               video_ratio;
  int                  video_aspect;
  int                  top_bar;             /* the height of the upper black bar */

  vo_scale_t           scale;
  alphablend_t         alphablend_extra_data;

  dxr3_overlay_t       overlay;
#ifdef HAVE_X11
  Display             *display;
  Drawable             win;
  GC                   gc;
  XColor               black;
  XColor               key;
#endif

} dxr3_driver_t;

typedef struct dxr3_frame_s {
  vo_frame_t       vo_frame;
  uint32_t         oheight;
  int              aspect, pan_scan;
  void            *mem;           /* allocated for YV12 or YUY2 buffers */
  uint8_t         *real_base[3];  /* yuv/yuy2 buffers in mem aligned on 16 */
  int              swap_fields;   /* shifts Y buffer one line to exchange odd/even lines */
} dxr3_frame_t;

struct encoder_data_s {
  encoder_type     type;
  int            (*on_update_format)(dxr3_driver_t *, dxr3_frame_t *);
  int            (*on_frame_copy)(dxr3_driver_t *, dxr3_frame_t *, uint8_t **src);
  int            (*on_display_frame)(dxr3_driver_t *, dxr3_frame_t *);
  int            (*on_unneeded)(dxr3_driver_t *);
  int            (*on_close)(dxr3_driver_t *);

  /* this is only used by the libavcodec encoder */
  void            *handle;
};

struct spu_encoder_s {
  vo_overlay_t   *overlay;
  int             need_reencode;
  uint8_t        *target;
  int             size;
  int             malloc_size;
  uint32_t        color[16];
  uint8_t         trans[4];
  int             map[OVL_PALETTE_SIZE];
  uint32_t        hili_color[16];
  uint8_t         hili_trans[4];
  int             clip_map[OVL_PALETTE_SIZE];
};

/* mpeg encoder plugins initialization functions */
#ifdef HAVE_LIBRTE
int dxr3_rte_init(dxr3_driver_t *);
#endif
#ifdef HAVE_LIBFAME
int dxr3_fame_init(dxr3_driver_t *);
#endif
int dxr3_lavc_init(dxr3_driver_t *, plugin_node_t *);

/* spu encoder functions */
spu_encoder_t *dxr3_spu_encoder_init(void);
void           dxr3_spu_encode(spu_encoder_t *);

#define dxr3_video_setpts(fd,arg)	dxr3_compat_ioctl((fd), EM8300_IOCTL_VIDEO_SETPTS, (arg))
#define dxr3_spu_setpts(fd,arg)		dxr3_compat_ioctl((fd), EM8300_IOCTL_SPU_SETPTS, (arg))
#define dxr3_spu_setpalette(fd,arg)	dxr3_compat_ioctl((fd), EM8300_IOCTL_SPU_SETPALETTE, (arg))
#define dxr3_spu_button(fd,arg)		dxr3_compat_ioctl((fd), EM8300_IOCTL_SPU_BUTTON, (arg))
int dxr3_compat_ioctl (int, int, void *);
