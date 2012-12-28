/*
 * Copyright (C) 2000-2004 the xine project
 * Copyright (C) 2004 the unichrome project
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
 * video_out_xxmc.c, X11 decoding accelerated video extension interface for xine
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
 * XxMC implementation by Thomas Hellström - August 2004
 */

#ifndef _XXMC_H
#define _XXMC_H

#define XVMC_THREAD_SAFE

/*
 * some implementations are not aware of the display having been locked
 * already before calling the xvmc function and may therefore deadlock.
 */
/*
#define XVMC_LOCKDISPLAY_SAFE
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_XV

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#if defined (__SVR4) && defined (__sun)
# include <sys/int_types.h>
#else
# include <stdint.h>
#endif

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
#include <X11/extensions/XvMClib.h>
#ifdef HAVE_VLDXVMC
  #include <X11/extensions/vldXvMC.h>
#endif

#define LOG_MODULE "video_out_xxmc"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "x11osd.h"
#include "accel_xvmc.h"

#define FOURCC_IA44 0x34344149
#define FOURCC_AI44 0x34344941
#define XVMC_MAX_SURFACES 16
#define XVMC_MAX_SUBPICTURES 4

typedef struct xxmc_driver_s xxmc_driver_t;

typedef struct {
  xine_macroblocks_t   xine_mc;
  XvMCBlockArray       blocks;            /* pointer to memory for dct block array  */
  int                  num_blocks;
  XvMCMacroBlock      *macroblockptr;     /* pointer to current macro block         */
  XvMCMacroBlock      *macroblockbaseptr; /* pointer to base MacroBlock in MB array */
  XvMCMacroBlockArray  macro_blocks;      /* pointer to memory for macroblock array */
  int                  slices;
} xvmc_macroblocks_t;


typedef struct {
  int                value;
  int                min;
  int                max;
  Atom               atom;

  cfg_entry_t       *entry;

  xxmc_driver_t     *this;
} xxmc_property_t;

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format;
  double             ratio;

  XvImage           *image;
  XShmSegmentInfo    shminfo;

  /* XvMC specific stuff */

  XvMCSurface       *xvmc_surf;
  xine_xxmc_t        xxmc_data;
  int                last_sw_format;
} xxmc_frame_t;

typedef struct{
  unsigned int       mpeg_flags;
  unsigned int       accel_flags;
  unsigned int       max_width;
  unsigned int       max_height;
  unsigned int       sub_max_width;
  unsigned int       sub_max_height;
  int                type_id;
  XvImageFormatValues subPicType;
  int                flags;
} xvmc_capabilities_t;

typedef struct xvmc_surface_handler_s {
  XvMCSurface surfaces[XVMC_MAX_SURFACES];
  int surfInUse[XVMC_MAX_SURFACES];
  int surfValid[XVMC_MAX_SURFACES];
  XvMCSubpicture subpictures[XVMC_MAX_SUBPICTURES];
  int subInUse[XVMC_MAX_SUBPICTURES];
  int subValid[XVMC_MAX_SUBPICTURES];
  pthread_mutex_t mutex;
} xvmc_surface_handler_t;

typedef struct context_lock_s {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int num_readers;
} context_lock_t;

#define LOCK_AND_SURFACE_VALID(driver, surface)			\
  xvmc_context_reader_lock( &(driver)->xvmc_lock );		\
  if (!xxmc_xvmc_surface_valid((driver),(surface))) {		\
    xvmc_context_reader_unlock( &(driver)->xvmc_lock );		\
    return;							\
  }

#if defined(XVMC_THREAD_SAFE) && defined(XVMC_LOCKDISPLAY_SAFE)
#define XVMCLOCKDISPLAY(display)
#define XVMCUNLOCKDISPLAY(display)
#else
#define XVMCLOCKDISPLAY(display) XLockDisplay(display)
#define XVMCUNLOCKDISPLAY(display) XUnlockDisplay(display)
#endif

struct xxmc_driver_s {
  vo_driver_t        vo_driver;

  config_values_t    *config;

  /* X11 / Xv related stuff */
  Display            *display;
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
  xxmc_property_t    props[VO_NUM_PROPERTIES];
  uint32_t           capabilities;
  xxmc_frame_t       *recent_frames[VO_NUM_RECENT_FRAMES];
  xxmc_frame_t       *cur_frame;
  int                cur_field;
  int                bob;
  int                disable_bob_for_progressive_frames;
  int                disable_bob_for_scaled_osd;
  int                scaled_osd_active;
  x11osd             *xoverlay;
  int                xv_xoverlay_type;
  int                xoverlay_type;
  int                ovl_changed;

  /* all scaling information goes here */
  vo_scale_t         sc;
  int                deinterlace_enabled;
  int                use_colorkey;
  uint32_t           colorkey;
  int                (*x11_old_error_handler)  (Display *, XErrorEvent *);
  xine_t             *xine;

  /* XvMC related stuff here */
  xvmc_macroblocks_t   macroblocks;
  xvmc_capabilities_t  *xvmc_cap;
  unsigned           xvmc_num_cap;
  unsigned int       xvmc_max_subpic_x;
  unsigned int       xvmc_max_subpic_y;
  int                xvmc_eventbase;
  int                xvmc_errbase;
  int                hwSubpictures;
  XvMCSubpicture     *old_subpic,*new_subpic;
  xx44_palette_t     palette;
  int                first_overlay;
  float              cpu_saver;
  int                cpu_save_enabled;
  int                reverse_nvidia_palette;
  int                context_flags;

  /*
   * These variables are protected by the context lock:
   */

  unsigned           xvmc_cur_cap;
  int                xvmc_backend_subpic;
  XvMCContext        context;
  int                contextActive;
  xvmc_surface_handler_t xvmc_surf_handler;
  unsigned           xvmc_mpeg;
  unsigned           xvmc_accel;
  unsigned           last_accel_request;
  unsigned           xvmc_width;
  unsigned           xvmc_height;
  int                have_xvmc_autopaint;
  int                xvmc_xoverlay_type;
  int                unsigned_intra;

  /*
   * Only creation and destruction of the below.
   */

  char               *xvmc_palette;
  XvImage            *subImage;
  XShmSegmentInfo    subShmInfo;

  /*
   * The mutex below is needed since XlockDisplay wasn't really enough
   * to protect the XvMC Calls.
   */
  context_lock_t     xvmc_lock;

  alphablend_t       alphablend_extra_data;
};

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  xine_t              *xine;
} xxmc_class_t;

extern void xvmc_context_reader_unlock(context_lock_t *c);
extern void xvmc_context_reader_lock(context_lock_t *c);
extern int xxmc_xvmc_surface_valid(xxmc_driver_t *this, XvMCSurface *surf);

extern void xvmc_vld_slice(vo_frame_t *this_gen);
extern void xvmc_vld_frame(struct vo_frame_s *this_gen);

extern void xxmc_xvmc_proc_macro_block(int x, int y, int mb_type, int motion_type,
				       int (*mv_field_sel)[2], int *dmvector,
				       int cbp,
				       int dct_type, vo_frame_t *current_frame,
				       vo_frame_t *forward_ref_frame,
				       vo_frame_t *backward_ref_frame,
				       int picture_structure,
				       int second_field, int (*f_mot_pmv)[2],
				       int (*b_mot_pmv)[2]);

#endif
#endif
