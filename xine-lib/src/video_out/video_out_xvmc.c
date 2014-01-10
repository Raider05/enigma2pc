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
 * video_out_xvmc.c, X11 video motion compensation extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * XvMC image support by Jack Kelliher
 *
 * TODO:
 *  - support non-XvMC output, probably falling back to Xv.
 *  - support XvMC overlays for spu/osd
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_XVMC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/types.h>
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
#include <X11/extensions/XvMC.h>

#define LOG_MODULE "video_out_xvmc"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include "accel_xvmc.h"

#include <xine/xineutils.h>
#include <xine/vo_scale.h>
#include "xv_common.h"

/* #define LOG1 */
/* #define DLOG */

/* #define PRINTDATA */
/* #define PRINTFRAME */

#define MAX_NUM_FRAMES 8

typedef struct {
  xine_macroblocks_t   xine_mc;
  XvMCBlockArray      *blocks;            /* pointer to memory for dct block array  */
  int                  num_blocks;
  XvMCMacroBlock      *macroblockptr;     /* pointer to current macro block         */
  XvMCMacroBlock      *macroblockbaseptr; /* pointer to base MacroBlock in MB array */
  XvMCMacroBlockArray *macro_blocks;      /* pointer to memory for macroblock array */
  int                  slices;
} xvmc_macroblocks_t;

typedef struct {
  void *xid;
} cxid_t;

typedef struct xvmc_driver_s xvmc_driver_t;

typedef struct {
  int                  value;
  int                  min;
  int                  max;
  Atom                 atom;

  cfg_entry_t         *entry;

  xvmc_driver_t       *this;
} xvmc_property_t;


typedef struct {
  vo_frame_t           vo_frame;

  int                  width, height, format;
  double               ratio;

  XvMCSurface          surface;

  /* temporary Xv only storage */
  xine_xvmc_t          xvmc_data;
} xvmc_frame_t;


struct xvmc_driver_s {

  vo_driver_t          vo_driver;

  config_values_t     *config;

  /* X11 / XvMC related stuff */
  Display             *display;
  int                  screen;
  Drawable             drawable;
  unsigned int         xvmc_format_yv12;
  unsigned int         xvmc_format_yuy2;
  XVisualInfo          vinfo;
  GC                   gc;
  XvPortID             xv_port;
  XvMCContext          context;
  xvmc_frame_t        *frames[MAX_NUM_FRAMES];

  int                  surface_type_id;
  int                  max_surface_width;
  int                  max_surface_height;
  int                  num_frame_buffers;

  int                  surface_width;
  int                  surface_height;
  int                  surface_ratio;
  int                  surface_format;
  int                  surface_flags;
  short                acceleration;

  cxid_t               context_id;
  xvmc_macroblocks_t   macroblocks;

  /* all scaling information goes here */
  vo_scale_t           sc;


  XColor               black;

  /* display anatomy */
  double               display_ratio;        /* given by visual parameter
						from init function            */

  xvmc_property_t      props[VO_NUM_PROPERTIES];
  uint32_t             capabilities;


  xvmc_frame_t        *recent_frames[VO_NUM_RECENT_FRAMES];
  xvmc_frame_t        *cur_frame;
  vo_overlay_t        *overlay;

  /* TODO CLEAN THIS UP all unused vars sizes moved to vo_scale */

  /* size / aspect ratio calculations */

  /*
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   */

  int                  delivered_duration;

  /*
   * "ideal" size :
   * displayed width/height corrected by aspect ratio
   */

  double               ratio_factor;         /* output frame must fullfill:
						height = width * ratio_factor  */

  /* gui callback */

  void               (*frame_output_cb) (void *user_data,
					 int video_width, int video_height,
					 int *dest_x, int *dest_y,
					 int *dest_height, int *dest_width,
					 int *win_x, int *win_y);

  int                  use_colorkey;
  uint32_t             colorkey;

  void                *user_data;
  xine_t              *xine;

  alphablend_t         alphablend_extra_data;
};


typedef struct {
  video_driver_class_t driver_class;

  Display             *display;
  config_values_t     *config;
  XvPortID             xv_port;
  XvAdaptorInfo       *adaptor_info;
  unsigned int         adaptor_num;

  int                  surface_type_id;
  unsigned int         max_surface_width;
  unsigned int         max_surface_height;
  short                acceleration;
  xine_t              *xine;
} xvmc_class_t;

static void xvmc_render_macro_blocks(vo_frame_t *current_image,
				     vo_frame_t *backward_ref_image,
				     vo_frame_t *forward_ref_image,
				     int picture_structure,
				     int second_field,
				     xvmc_macroblocks_t *macroblocks);

/*********************** XVMC specific routines *********************/

/**************************************************************************/

/*
 * dmvector: differential motion vector
 * mvx, mvy: decoded mv components (always in field format)
 */
static void calc_DMV(int DMV[][2], int *dmvector,
		     int mvx, int mvy, int picture_structure, int top_field_first) {

  if (picture_structure==VO_BOTH_FIELDS) {
    if (top_field_first) {
      /* vector for prediction of top field from bottom field */
      DMV[0][0] = ((mvx  +(mvx>0))>>1) + dmvector[0];
      DMV[0][1] = ((mvy  +(mvy>0))>>1) + dmvector[1] - 1;

      /* vector for prediction of bottom field from top field */
      DMV[1][0] = ((3*mvx+(mvx>0))>>1) + dmvector[0];
      DMV[1][1] = ((3*mvy+(mvy>0))>>1) + dmvector[1] + 1;
    }
    else {
      /* vector for prediction of top field from bottom field */
      DMV[0][0] = ((3*mvx+(mvx>0))>>1) + dmvector[0];
      DMV[0][1] = ((3*mvy+(mvy>0))>>1) + dmvector[1] - 1;

      /* vector for prediction of bottom field from top field */
      DMV[1][0] = ((mvx  +(mvx>0))>>1) + dmvector[0];
      DMV[1][1] = ((mvy  +(mvy>0))>>1) + dmvector[1] + 1;
    }
  }
  else {
    /* vector for prediction from field of opposite 'parity' */
    DMV[0][0] = ((mvx+(mvx>0))>>1) + dmvector[0];
    DMV[0][1] = ((mvy+(mvy>0))>>1) + dmvector[1];

    /* correct for vertical field shift */
    if (picture_structure==VO_TOP_FIELD)
      DMV[0][1]--;
    else
      DMV[0][1]++;
  }
}

static void xvmc_proc_macro_block(int x, int y, int mb_type, int motion_type,
				  int (*mv_field_sel)[2], int *dmvector, int cbp,
				  int dct_type, vo_frame_t *current_frame,
				  vo_frame_t *forward_ref_frame,
				  vo_frame_t *backward_ref_frame, int picture_structure,
				  int second_field, int (*f_mot_pmv)[2], int (*b_mot_pmv)[2]) {
  xvmc_driver_t        *this                = (xvmc_driver_t *) current_frame->driver;
  xvmc_macroblocks_t   *mbs                 = &this->macroblocks;
  int                   top_field_first     = current_frame->top_field_first;
  int                   picture_coding_type = current_frame->picture_coding_type;

  mbs->macroblockptr->x = x;
  mbs->macroblockptr->y = y;

  if(mb_type & XINE_MACROBLOCK_INTRA) {
    mbs->macroblockptr->macroblock_type = XVMC_MB_TYPE_INTRA;
  }
  else {
    mbs->macroblockptr->macroblock_type = 0;
    /* XvMC doesn't support skips */
    if(!(mb_type & (XINE_MACROBLOCK_MOTION_BACKWARD | XINE_MACROBLOCK_MOTION_FORWARD))) {
      mb_type |= XINE_MACROBLOCK_MOTION_FORWARD;
      motion_type = (picture_structure == VO_BOTH_FIELDS) ? XINE_MC_FRAME : XINE_MC_FIELD;
      mbs->macroblockptr->PMV[0][0][0] = 0;
      mbs->macroblockptr->PMV[0][0][1] = 0;
    }
    else {
      if(mb_type & XINE_MACROBLOCK_MOTION_BACKWARD) {
	mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_MOTION_BACKWARD;
	mbs->macroblockptr->PMV[0][1][0]    = b_mot_pmv[0][0];
	mbs->macroblockptr->PMV[0][1][1]    = b_mot_pmv[0][1];
	mbs->macroblockptr->PMV[1][1][0]    = b_mot_pmv[1][0];
	mbs->macroblockptr->PMV[1][1][1]    = b_mot_pmv[1][1];

      }

      if(mb_type & XINE_MACROBLOCK_MOTION_FORWARD) {
	mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_MOTION_FORWARD;
	mbs->macroblockptr->PMV[0][0][0]    = f_mot_pmv[0][0];
	mbs->macroblockptr->PMV[0][0][1]    = f_mot_pmv[0][1];
	mbs->macroblockptr->PMV[1][0][0]    = f_mot_pmv[1][0];
	mbs->macroblockptr->PMV[1][0][1]    = f_mot_pmv[1][1];
      }
    }

    if((mb_type & XINE_MACROBLOCK_PATTERN) && cbp)
      mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_PATTERN;

    mbs->macroblockptr->motion_type = motion_type;

    if(motion_type == XINE_MC_DMV) {
      int DMV[2][2];

      if(picture_structure == VO_BOTH_FIELDS) {
	calc_DMV(DMV,dmvector, f_mot_pmv[0][0],
		 f_mot_pmv[0][1]>>1, picture_structure,
		 top_field_first);

	mbs->macroblockptr->PMV[1][0][0] = DMV[0][0];
	mbs->macroblockptr->PMV[1][0][1] = DMV[0][1];
	mbs->macroblockptr->PMV[1][1][0] = DMV[1][0];
	mbs->macroblockptr->PMV[1][1][1] = DMV[1][1];
      }
      else {
	calc_DMV(DMV,dmvector, f_mot_pmv[0][0],
		 f_mot_pmv[0][1]>>1, picture_structure,
		 top_field_first);

	mbs->macroblockptr->PMV[0][1][0] = DMV[0][0];
	mbs->macroblockptr->PMV[0][1][1] = DMV[0][1];
      }
    }

    if((motion_type == XINE_MC_FIELD) || (motion_type == XINE_MC_16X8)) {
      mbs->macroblockptr->motion_vertical_field_select = 0;

      if(mv_field_sel[0][0])
	mbs->macroblockptr->motion_vertical_field_select |= 1;
      if(mv_field_sel[0][1])
	mbs->macroblockptr->motion_vertical_field_select |= 2;
      if(mv_field_sel[1][0])
	mbs->macroblockptr->motion_vertical_field_select |= 4;
      if(mv_field_sel[1][1])
	mbs->macroblockptr->motion_vertical_field_select |= 8;
    }
  } /* else of if(mb_type & XINE_MACROBLOCK_INTRA) */

  mbs->macroblockptr->index = ((unsigned long)mbs->xine_mc.blockptr -
			       (unsigned long)mbs->xine_mc.blockbaseptr) >> 7;

  mbs->macroblockptr->dct_type = dct_type;
  mbs->macroblockptr->coded_block_pattern = cbp;

  while(cbp) {
    if(cbp & 1) mbs->macroblockptr->index--;
    cbp >>= 1;
  }

#ifdef PRINTDATA
  printf("\n");
  printf("-- %04d %04d %02x %02x %02x %02x",mbs->macroblockptr->x,mbs->macroblockptr->y,mbs->macroblockptr->macroblock_type,
	 mbs->macroblockptr->motion_type,mbs->macroblockptr->motion_vertical_field_select,mbs->macroblockptr->dct_type);
  printf(" [%04d %04d %04d %04d %04d %04d %04d %04d] ",
	 mbs->macroblockptr->PMV[0][0][0],mbs->macroblockptr->PMV[0][0][1],mbs->macroblockptr->PMV[0][1][0],mbs->macroblockptr->PMV[0][1][1],
	 mbs->macroblockptr->PMV[1][0][0],mbs->macroblockptr->PMV[1][0][1],mbs->macroblockptr->PMV[1][1][0],mbs->macroblockptr->PMV[1][1][1]);

  printf(" %04d %04x\n",mbs->macroblockptr->index,mbs->macroblockptr->coded_block_pattern);
#endif

  mbs->num_blocks++;
  mbs->macroblockptr++;

  if(mbs->num_blocks == mbs->slices) {
#ifdef PRINTDATA
    printf("macroblockptr %lx",  mbs->macroblockptr);
    printf("** RenderSurface %04d %04x\n",picture_structure,
	   second_field ? XVMC_SECOND_FIELD : 0);
    fflush(stdout);
#endif
#ifdef PRINTFRAME
    printf("  target %08x past %08x future %08x\n",
	   current_frame,
	   forward_ref_frame,
	   backward_ref_frame);
#endif
#ifdef PRINTFRAME
    if (picture_coding_type == XINE_PICT_P_TYPE)
      printf(" coding type P_TYPE\n");
    if (picture_coding_type == XINE_PICT_I_TYPE)
      printf(" coding type I_TYPE\n");
    if (picture_coding_type == XINE_PICT_B_TYPE)
      printf(" coding type B_TYPE\n");
    if (picture_coding_type == XINE_PICT_D_TYPE)
      printf(" coding type D_TYPE\n");
    fflush(stdout);
#endif

    if (picture_coding_type == XINE_PICT_B_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  backward_ref_frame,
			  forward_ref_frame,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);
    if (picture_coding_type == XINE_PICT_P_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  NULL,
			  forward_ref_frame,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);
    if (picture_coding_type == XINE_PICT_I_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  NULL,
			  NULL,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);

    mbs->num_blocks       = 0;
    mbs->macroblockptr    = mbs->macroblockbaseptr;
    mbs->xine_mc.blockptr = mbs->xine_mc.blockbaseptr;
  }
}

static uint32_t xvmc_get_capabilities (vo_driver_t *this_gen) {
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_get_capabilities\n");

  return this->capabilities;
}

static void xvmc_frame_field (vo_frame_t *vo_img, int which_field) {
  lprintf ("xvmc_frame_field\n");
}

static void xvmc_frame_dispose (vo_frame_t *vo_img) {
  xvmc_frame_t  *frame = (xvmc_frame_t *) vo_img ;

  lprintf ("xvmc_frame_dispose\n");

  /*
   * TODO - clean up of images/surfaces and frames
   * Note this function is not really needed
   * set_context does the work
   */

  free (frame);
}

static void xvmc_render_macro_blocks(vo_frame_t *current_image,
				     vo_frame_t *backward_ref_image,
				     vo_frame_t *forward_ref_image,
				     int picture_structure,
				     int second_field,
				     xvmc_macroblocks_t *macroblocks) {
  xvmc_driver_t *this           = (xvmc_driver_t *) current_image->driver;
  xvmc_frame_t  *current_frame  = XVMC_FRAME(current_image);
  xvmc_frame_t  *forward_frame  = XVMC_FRAME(forward_ref_image);
  xvmc_frame_t  *backward_frame = XVMC_FRAME(backward_ref_image);
  int           flags;

  lprintf ("xvmc_render_macro_blocks\n");
  lprintf ("slices %d 0x%08lx 0x%08lx 0x%08lx\n",
	   macroblocks->slices,
	   (long) current_frame, (long) backward_frame,
	   (long) forward_frame);
  /* lprintf ("slices %d 0x%08lx 0x%08lx 0x%08lx\n",macroblocks->slices,
	   (long) current_frame->surface, (long) backward_frame->surface,
	   (long) forward_frame->surface);
  */

  flags = second_field;

  if(forward_frame) {
    if(backward_frame) {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			&forward_frame->surface,
			&backward_frame->surface,
			flags,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
    else {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			&forward_frame->surface,
			NULL,
			flags,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
  }
  else {
    if(backward_frame) {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			NULL,
			&backward_frame->surface,
			flags,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
    else {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			NULL,
			NULL,
			flags,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
  }

  XvMCFlushSurface(this->display, &current_frame->surface);

  lprintf ("xvmc_render_macro_blocks done\n");
}

static vo_frame_t *xvmc_alloc_frame (vo_driver_t *this_gen) {
  xvmc_frame_t   *frame;
  xvmc_driver_t  *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_alloc_frame\n");

  frame = calloc(1, sizeof (xvmc_frame_t));

  if (!frame)
    return NULL;

  frame->vo_frame.accel_data = &frame->xvmc_data;
  frame->xvmc_data.vo_frame = &frame->vo_frame;

  /* keep track of frames and how many frames alocated. */
  this->frames[this->num_frame_buffers++] = frame;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.proc_slice       = NULL;
  frame->vo_frame.proc_frame       = NULL;
  frame->vo_frame.field            = xvmc_frame_field;
  frame->vo_frame.dispose          = xvmc_frame_dispose;
  frame->vo_frame.driver           = this_gen;
  frame->xvmc_data.proc_macro_block            = xvmc_proc_macro_block;

  return (vo_frame_t *) frame;
}

static cxid_t *xvmc_set_context (xvmc_driver_t *this,
				 uint32_t width, uint32_t height,
				 double ratio, int format, int flags,
				 xine_macroblocks_t *macro_blocks) {
  int                  i;
  int                  result      = 0;
  int                  slices      = 1;
  xvmc_macroblocks_t  *macroblocks = (xvmc_macroblocks_t *) macro_blocks;

  lprintf ("xvmc_set_context %dx%d %04x\n",width,height,format);

  /* initialize block & macro block pointers first time */
  if(macroblocks->blocks == NULL ||  macroblocks->macro_blocks == NULL) {
    macroblocks->blocks       = calloc(1, sizeof(XvMCBlockArray));
    macroblocks->macro_blocks = calloc(1, sizeof(XvMCMacroBlockArray));

    lprintf("macroblocks->blocks %lx ->macro_blocks %lx\n",
	    macroblocks->blocks,macroblocks->macro_blocks);
  }

  if((this->context_id.xid != NULL)   &&
     (width  == this->surface_width)  &&
     (height == this->surface_height) &&
     (format == this->surface_format) &&
     (flags  == this->surface_flags)) {

    /* don't need to change  context */
    lprintf ("didn't change context\n");

    return(&this->context_id);

  }
  else {
    if(this->context_id.xid != NULL) {

      /*
       * flush any drawing and wait till we are done with the old stuff
       * blow away the old stuff
       */
      lprintf ("freeing previous context\n");

      XvMCDestroyBlocks(this->display, macroblocks->blocks);
      XvMCDestroyMacroBlocks(this->display, macroblocks->macro_blocks);

      for(i = 0; i < this->num_frame_buffers; i++) {
	XvMCFlushSurface(this->display, &this->frames[i]->surface);
	XvMCSyncSurface(this->display, &this->frames[i]->surface);

	XvMCDestroySurface(this->display, &this->frames[i]->surface);
      }
      XvMCDestroyContext(this->display, &this->context);
      this->context_id.xid = NULL;
    }

    lprintf("CreateContext  w %d h %d id %x portNum %x\n",
	    width,height, this->surface_type_id, (int)this->xv_port);

    /* now create a new context */
    result = XvMCCreateContext(this->display, this->xv_port,
			       this->surface_type_id,
			       width, height, XVMC_DIRECT, &this->context);

    if(result != Success) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "set_context: couldn't create XvMCContext\n");
      macroblocks->xine_mc.xvmc_accel = 0;
      _x_abort();
    }

    this->context_id.xid = (void *)this->context.context_id;

    for(i = 0; i < this->num_frame_buffers; i++) {
      result = XvMCCreateSurface(this->display, &this->context,
				 &this->frames[i]->surface);
      if(result != Success) {
	XvMCDestroyContext(this->display, &this->context);
	xprintf(this->xine, XINE_VERBOSITY_DEBUG, "set_context: couldn't create XvMCSurfaces\n");
	this->context_id.xid            = NULL;
	macroblocks->xine_mc.xvmc_accel = 0;
	_x_abort();
      }

      lprintf ("  CreatedSurface %d 0x%lx\n",i,(long)&this->frames[i]->surface);
    }

    slices = (slices * width/16);

    lprintf("CreateBlocks  slices %d\n",slices);

    result = XvMCCreateBlocks(this->display, &this->context, slices * 6,
			      macroblocks->blocks);
    if(result != Success) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "set_context: ERROR XvMCCreateBlocks failed\n");
      macroblocks->xine_mc.xvmc_accel = 0;
      _x_abort();
    }
    result =XvMCCreateMacroBlocks(this->display, &this->context, slices,
				  macroblocks->macro_blocks);
    if(result != Success) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "set_context: ERROR XvMCCreateMacroBlocks failed\n");
      macroblocks->xine_mc.xvmc_accel = 0;
      _x_abort();
    }

    lprintf ("  Created bock and macro block arrays\n");

    macroblocks->xine_mc.blockbaseptr = macroblocks->blocks->blocks;
    macroblocks->xine_mc.blockptr     = macroblocks->xine_mc.blockbaseptr;
    macroblocks->num_blocks           = 0;
    macroblocks->macroblockbaseptr    = macroblocks->macro_blocks->macro_blocks;
    macroblocks->macroblockptr        = macroblocks->macroblockbaseptr;
    macroblocks->slices               = slices;
    macroblocks->xine_mc.xvmc_accel   = this->acceleration;
    return(&this->context_id);
  }

  return NULL;
}

#if 0
static XvImage *create_ximage (xvmc_driver_t *this, XShmSegmentInfo *shminfo,
			       int width, int height, int format) {
  unsigned int  xvmc_format;
  XvImage      *image = NULL;

  lprintf ("create_ximage\n");

  switch (format) {
  case XINE_IMGFMT_YV12:
  case XINE_IMGFMT_XVMC:
    xvmc_format = this->xvmc_format_yv12;
    break;
  case XINE_IMGFMT_YUY2:
    xvmc_format = this->xvmc_format_yuy2;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
    _x_abort();
  }

  /*
   *  plain Xv
   */

  if (1) {
    char *data;

    switch (format) {
    case XINE_IMGFMT_YV12:
    case XINE_IMGFMT_XVMC:
      data = malloc (width * height * 3/2);
      break;
    case XINE_IMGFMT_YUY2:
      data = malloc (width * height * 2);
      break;
    default:
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
      _x_abort();
    }

    image = XvCreateImage (this->display, this->xv_port,
			   xvmc_format, data, width, height);
  }

  return image;
}

/* Already Xlocked */
static void dispose_ximage (xvmc_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XvImage *myimage) {

  lprintf ("dispose_ximage\n");
  XFree(myimage);
}
#endif

static void xvmc_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      double ratio, int format, int flags) {
  xvmc_driver_t  *this  = (xvmc_driver_t *) this_gen;
  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;
  xine_xvmc_t *xvmc = (xine_xvmc_t *) frame_gen->accel_data;

  if (format != XINE_IMGFMT_XVMC) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, "xvmc_update_frame_format: frame format %08x not supported\n", format);
    _x_abort();
  }

  lprintf ("xvmc_update_frame_format\n");

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    lprintf ("updating frame to %d x %d (ratio=%f, format=%08x)\n",
	     width, height, ratio, format);

    /* Note that since we are rendering in hardware, we do not need to
     * allocate any ximage's for the software rendering buffers.
     */
    frame->width               = width;
    frame->height              = height;
    frame->format              = format;
    frame->ratio               = ratio;
  }

  xvmc->macroblocks = &this->macroblocks.xine_mc;
  this->macroblocks.num_blocks = 0;
  this->macroblocks.macroblockptr = this->macroblocks.macroblockbaseptr;
  this->macroblocks.xine_mc.blockptr =
    this->macroblocks.xine_mc.blockbaseptr;
  if( flags & VO_NEW_SEQUENCE_FLAG ) {
    xvmc_set_context (this, width, height, ratio, format, flags,
                      xvmc->macroblocks);
  }
}

static void xvmc_clean_output_area (xvmc_driver_t *this) {
  lprintf ("xvmc_clean_output_area\n");

  XLockDisplay (this->display);
  XSetForeground (this->display, this->gc, this->black.pixel);
  XFillRectangle (this->display, this->drawable, this->gc,
		  this->sc.gui_x, this->sc.gui_y, this->sc.gui_width, this->sc.gui_height);

  if (this->use_colorkey) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height);
  }

  XUnlockDisplay (this->display);
}

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

static void xvmc_compute_ideal_size (xvmc_driver_t *this) {
  _x_vo_scale_compute_ideal_size( &this->sc );
}

/*
 * make ideal width/height "fit" into the gui
 */

static void xvmc_compute_output_size (xvmc_driver_t *this) {
 _x_vo_scale_compute_output_size( &this->sc );
}

static void xvmc_overlay_blend (vo_driver_t *this_gen,
				vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  xvmc_driver_t  *this  = (xvmc_driver_t *) this_gen;
  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;

  lprintf ("xvmc_overlay_blend\n");

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;

  /* Alpha Blend here
   * As XV drivers improve to support Hardware overlay, we will change this function.
   */

  if (overlay->rle) {
    if (frame->format == XINE_IMGFMT_YV12)
      _x_blend_yuv(frame->vo_frame.base, overlay,
		frame->width, frame->height, frame->vo_frame.pitches,
                &this->alphablend_extra_data);
    else if (frame->format != XINE_IMGFMT_XVMC)
      _x_blend_yuy2(frame->vo_frame.base[0], overlay,
		 frame->width, frame->height, frame->vo_frame.pitches[0],
                 &this->alphablend_extra_data);
    else
      xprintf (this->xine, XINE_VERBOSITY_LOG, "xvmc_overlay_blend: overlay blending not supported for frame format %08x\n", frame->format);
  }
}

static void xvmc_add_recent_frame (xvmc_driver_t *this, xvmc_frame_t *frame) {
  int i;

  lprintf ("xvmc_add_recent_frame\n");

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
static void xvmc_flush_recent_frames (xvmc_driver_t *this) {
  int i;

  lprintf ("xvmc_flush_recent_frames\n");

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.free
	(&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }
}
#endif

static int xvmc_redraw_needed (vo_driver_t *this_gen) {
  xvmc_driver_t  *this = (xvmc_driver_t *) this_gen;
  int             ret  = 0;

  if(this->cur_frame) {

    this->sc.delivered_height   = this->cur_frame->height;
    this->sc.delivered_width    = this->cur_frame->width;
    this->sc.delivered_ratio    = this->cur_frame->ratio;

    this->sc.crop_left          = this->cur_frame->vo_frame.crop_left;
    this->sc.crop_right         = this->cur_frame->vo_frame.crop_right;
    this->sc.crop_top           = this->cur_frame->vo_frame.crop_top;
    this->sc.crop_bottom        = this->cur_frame->vo_frame.crop_bottom;

    xvmc_compute_ideal_size(this);

    if(_x_vo_scale_redraw_needed(&this->sc)) {
      xvmc_compute_output_size (this);
      xvmc_clean_output_area (this);
      ret = 1;
    }
  }
  else
    ret = 1;

  return ret;
}

static void xvmc_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  xvmc_driver_t  *this  = (xvmc_driver_t *) this_gen;
  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;

  lprintf ("xvmc_display_frame %d %x\n",frame_gen->id,frame_gen);

  /*
   * queue frames (deinterlacing)
   * free old frames
   */

  xvmc_add_recent_frame (this, frame); /* deinterlacing */

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

    /*
       this->delivered_width      = frame->width;
       this->delivered_height     = frame->height;
       this->delivered_ratio      = frame->ratio;
       this->delivered_duration   = frame->vo_frame.duration;

       xvmc_compute_ideal_size (this);
    */

    /* this->gui_width = 0; */ /* trigger re-calc of output size */
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  xvmc_redraw_needed (this_gen);

  XLockDisplay (this->display);

  /* Make sure the surface has finished rendering before we display */
  XvMCSyncSurface(this->display, &this->cur_frame->surface);

  XvMCPutSurface(this->display, &this->cur_frame->surface,
		 this->drawable,
		 this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		 this->sc.displayed_width, this->sc.displayed_height,
		 this->sc.output_xoffset, this->sc.output_yoffset,
		 this->sc.output_width, this->sc.output_height,
		 XVMC_FRAME_PICTURE);

  XUnlockDisplay (this->display);

  /*
  printf ("video_out_xvmc: xvmc_display_frame... done\n");
  */
}

static int xvmc_get_property (vo_driver_t *this_gen, int property) {
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_get_property\n");

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

  return this->props[property].value;
}

static void xvmc_property_callback (void *property_gen, xine_cfg_entry_t *entry) {
  xvmc_property_t *property = (xvmc_property_t *) property_gen;
  xvmc_driver_t   *this     = property->this;

  lprintf ("xvmc_property_callback\n");

  XLockDisplay(this->display);
  XvSetPortAttribute (this->display, this->xv_port,
		      property->atom, entry->num_value);
  XUnlockDisplay(this->display);
}

static int xvmc_set_property (vo_driver_t *this_gen,
			    int property, int value) {
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_set_property %d value %d\n",property,value);

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if (this->props[property].atom != None) {
    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;

    XLockDisplay(this->display);
    XvSetPortAttribute (this->display, this->xv_port,
			this->props[property].atom, value);
    XvGetPortAttribute (this->display, this->xv_port,
			this->props[property].atom,
			&this->props[property].value);
    XUnlockDisplay(this->display);

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
      lprintf("VO_PROP_ASPECT_RATIO(%d)\n", this->props[property].value);

      xvmc_compute_ideal_size (this);
      xvmc_compute_output_size (this);
      xvmc_clean_output_area (this);

      break;

    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		 "video_out_xvmc: VO_PROP_ZOOM_X = %d\n", this->props[property].value);

	this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
	xvmc_compute_ideal_size (this);
	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;

    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		 "video_out_xvmc: VO_PROP_ZOOM_Y = %d\n", this->props[property].value);

	this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
	xvmc_compute_ideal_size (this);
	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    }
  }

  return value;
}

static void xvmc_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_get_property_min_max\n");

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) {
    *min = *max = 0;
    return;
  }
  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int xvmc_gui_data_exchange (vo_driver_t *this_gen,
				   int data_type, void *data) {
  xvmc_driver_t     *this = (xvmc_driver_t *) this_gen;

  lprintf ("xvmc_gui_data_exchange\n");

  switch (data_type) {
  case XINE_GUI_SEND_EXPOSE_EVENT: {
    /* XExposeEvent * xev = (XExposeEvent *) data; */

    /* FIXME : take care of completion events */
    lprintf ("XINE_GUI_SEND_EXPOSE_EVENT\n");

    if (this->cur_frame) {
      int i;

      XLockDisplay (this->display);

      XSetForeground (this->display, this->gc, this->black.pixel);

      for( i = 0; i < 4; i++ ) {
        if( this->sc.border[i].w && this->sc.border[i].h )
          XFillRectangle(this->display, this->drawable, this->gc,
                         this->sc.border[i].x, this->sc.border[i].y,
                         this->sc.border[i].w, this->sc.border[i].h);
      }

      if (this->use_colorkey) {
	XSetForeground (this->display, this->gc, this->colorkey);
	XFillRectangle (this->display, this->drawable, this->gc,
			this->sc.output_xoffset, this->sc.output_yoffset,
			this->sc.output_width, this->sc.output_height);
      }

      XvMCPutSurface(this->display, &this->cur_frame->surface,
		     this->drawable,
		     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		     this->sc.displayed_width, this->sc.displayed_height,
		     this->sc.output_xoffset, this->sc.output_yoffset,
		     this->sc.output_width, this->sc.output_height,
		     XVMC_FRAME_PICTURE);

      XSync(this->display, False);
      XUnlockDisplay (this->display);
    }
  }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    XLockDisplay(this->display);
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
    XUnlockDisplay(this->display);
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      /*
	 xvmc_translate_gui2video(this, rect->x, rect->y,
		                  &x1, &y1);
	xvmc_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
				 &x2, &y2);
      */

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

static void xvmc_dispose (vo_driver_t *this_gen) {
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;
  int i;

  lprintf ("xvmc_dispose\n");

  if(this->context_id.xid) {
    XLockDisplay(this->display);
    for(i = 0; i < this->num_frame_buffers; i++) {
      /* if(useOverlay)  *//* only one is displaying but I don't want to keep track*/
      XvMCHideSurface(this->display, &this->frames[i]->surface);
      XvMCDestroySurface(this->display, &this->frames[i]->surface);
    }
    /* XvMCDestroyBlocks(this->display, &macroblocks->blocks); */
    /* XvMCDestroyMacroBlocks(this->display, &macroblocks->macro_blocks); */
    XvMCDestroyContext(this->display, &this->context);
    XUnlockDisplay(this->display);
  }

  XLockDisplay (this->display);
  XFreeGC(this->display, this->gc);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    lprintf ("xvmc_dispose: XvUngrabPort() failed.\n");
  }
  XUnlockDisplay (this->display);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if(this->recent_frames[i])
      this->recent_frames[i]->vo_frame.dispose
	(&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

/* called xlocked */
static void xvmc_check_capability (xvmc_driver_t *this,
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
  if (attr.max_value == ~0)
    attr.max_value = 2147483615;

  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);

  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   "video_out_xvmc: port attribute %s (%d) value is %d\n", str_prop, property, int_default);

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr.min_value == 0) && (attr.max_value == 1)) {
      this->config->register_bool (this->config, config_name, int_default,
				   config_desc,
				   config_help, 20, xvmc_property_callback, &this->props[property]);

    } else {
      this->config->register_range (this->config, config_name, int_default,
				    this->props[property].min, this->props[property].max,
				    config_desc,
				    config_help, 20, xvmc_property_callback, &this->props[property]);
    }

    entry = this->config->lookup_entry (this->config, config_name);

    this->props[property].entry = entry;

    xvmc_set_property (&this->vo_driver, property, entry->num_value);

    if (strcmp(str_prop,"XV_COLORKEY") == 0) {
      this->use_colorkey = 1;
      this->colorkey     = entry->num_value;
    }
  }
  else
    this->props[property].value  = int_default;
}

static void xvmc_update_XV_DOUBLE_BUFFER(void *this_gen, xine_cfg_entry_t *entry)
{
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;
  Atom           atom;
  int            xvmc_double_buffer;

  xvmc_double_buffer = entry->num_value;

  XLockDisplay(this->display);
  atom = XInternAtom (this->display, "XV_DOUBLE_BUFFER", False);
  XvSetPortAttribute (this->display, this->xv_port, atom, xvmc_double_buffer);
  XUnlockDisplay(this->display);

  lprintf("double buffering mode = %d\n",xvmc_double_buffer);
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  xvmc_class_t         *class   = (xvmc_class_t *) class_gen;
  config_values_t      *config  = class->config;
  xvmc_driver_t        *this    = NULL;
  unsigned int          i, formats;
  XvPortID              xv_port = class->xv_port;
  XvAttribute          *attr;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual  = (x11_visual_t *) visual_gen;
  XColor                dummy;
  XvAdaptorInfo        *adaptor_info = class->adaptor_info;
  unsigned int          adaptor_num  = class->adaptor_num;
  /* XvImage              *myimage; */

  lprintf ("open_plugin\n");

  /* TODO ???  */
  this = calloc(1, sizeof (xvmc_driver_t));

  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->display            = visual->display;
  this->overlay            = NULL;
  this->screen             = visual->screen;
  this->xv_port            = class->xv_port;
  this->config             = config;
  this->xine               = class->xine;

  _x_vo_scale_init (&this->sc, 1, 0, config );

  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  this->drawable           = visual->d;
  XLockDisplay(this->display);
  this->gc                 = XCreateGC(this->display, this->drawable, 0, NULL);
  XUnlockDisplay(this->display);
  this->capabilities       = VO_CAP_XVMC_MOCOMP | VO_CAP_CROP | VO_CAP_ZOOM_X | VO_CAP_ZOOM_Y;

  this->surface_type_id    = class->surface_type_id;
  this->max_surface_width  = class->max_surface_width;
  this->max_surface_height = class->max_surface_height;
  this->context_id.xid     = NULL;
  this->num_frame_buffers  = 0;
  this->acceleration       = class->acceleration;

  /* TODO CLEAN UP THIS */
  this->user_data          = visual->user_data;

  this->use_colorkey       = 0;
  this->colorkey           = 0;

  XLockDisplay(this->display);
  XAllocNamedColor(this->display,
		   DefaultColormap(this->display, this->screen),
		   "black", &this->black, &dummy);
  XUnlockDisplay(this->display);

  this->vo_driver.get_capabilities     = xvmc_get_capabilities;
  this->vo_driver.alloc_frame          = xvmc_alloc_frame;
  this->vo_driver.update_frame_format  = xvmc_update_frame_format;
  this->vo_driver.overlay_blend        = xvmc_overlay_blend;
  this->vo_driver.display_frame        = xvmc_display_frame;
  this->vo_driver.get_property         = xvmc_get_property;
  this->vo_driver.set_property         = xvmc_set_property;
  this->vo_driver.get_property_min_max = xvmc_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xvmc_gui_data_exchange;
  this->vo_driver.dispose              = xvmc_dispose;
  this->vo_driver.redraw_needed        = xvmc_redraw_needed;

  /*
   * init properties
   */
  for (i=0; i<VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = None;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  this->props[VO_PROP_INTERLACED].value     = 0;
  this->props[VO_PROP_ASPECT_RATIO].value   = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value         = 100;
  this->props[VO_PROP_ZOOM_Y].value         = 100;
  this->props[VO_PROP_MAX_NUM_FRAMES].value = MAX_NUM_FRAMES;

  /*
   * check this adaptor's capabilities
   */
  if(this->acceleration&XINE_VO_IDCT_ACCEL)
    this->capabilities |= VO_CAP_XVMC_IDCT;

  XLockDisplay(this->display);
  attr = XvQueryPortAttributes(this->display, xv_port, &nattr);
  if(attr && nattr) {
    int k;

    for(k = 0; k < nattr; k++) {
      if((attr[k].flags & XvSettable) && (attr[k].flags & XvGettable)) {
	const char *const name = attr[k].name;
	if(!strcmp(name, "XV_HUE")) {
	  if (!strncmp(adaptor_info[adaptor_num].name, "NV", 2)) {
            xprintf (this->xine, XINE_VERBOSITY_NONE, LOG_MODULE ": ignoring broken XV_HUE settings on NVidia cards\n");
	  } else {
	    this->capabilities |= VO_CAP_HUE;
	    xvmc_check_capability (this, VO_PROP_HUE, attr[k],
				   adaptor_info[adaptor_num].base_id,
				   NULL, NULL, NULL);
	  }
	} else if(!strcmp(name, "XV_SATURATION")) {
	  this->capabilities |= VO_CAP_SATURATION;
	  xvmc_check_capability (this, VO_PROP_SATURATION, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(attr[k].name, "XV_BRIGHTNESS")) {
	  this->capabilities |= VO_CAP_BRIGHTNESS;
	  xvmc_check_capability (this, VO_PROP_BRIGHTNESS, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_CONTRAST")) {
	  this->capabilities |= VO_CAP_CONTRAST;
	  xvmc_check_capability (this, VO_PROP_CONTRAST, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_GAMMA")) {
	  this->capabilities |= VO_CAP_GAMMA;
	  xvmc_check_capability (this, VO_PROP_GAMMA, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_COLORKEY")) {
	  this->capabilities |= VO_CAP_COLORKEY;
	  xvmc_check_capability (this, VO_PROP_COLORKEY, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 "video.device.xv_colorkey",
				 VIDEO_DEVICE_XV_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_AUTOPAINT_COLORKEY")) {
	  this->capabilities |= VO_CAP_AUTOPAINT_COLORKEY;
	  xvmc_check_capability (this, VO_PROP_AUTOPAINT_COLORKEY, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 "video.device.xv_autopaint_colorkey",
				 VIDEO_DEVICE_XV_AUTOPAINT_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_DOUBLE_BUFFER")) {
	  int xvmc_double_buffer = config->register_bool (config, "video.device.xv_double_buffer", 1,
				   VIDEO_DEVICE_XV_DOUBLE_BUFFER_HELP,
				   20, xvmc_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.device.xv_double_buffer",xvmc_double_buffer);
	}
      }
    }
    XFree(attr);
  }
  else {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_xvmc: no port attributes defined.\n");
  }


  /*
   * check supported image formats
   */
  fo = XvListImageFormats(this->display, this->xv_port, (int*)&formats);
  XUnlockDisplay(this->display);

  this->xvmc_format_yv12 = 0;
  this->xvmc_format_yuy2 = 0;

  for(i = 0; i < formats; i++) {
    lprintf ("XvMC image format: 0x%x (%4.4s) %s\n",
	     fo[i].id, (char*)&fo[i].id,
	     (fo[i].format == XvPacked) ? "packed" : "planar");

    switch (fo[i].id) {
    case XINE_IMGFMT_YV12:
      this->xvmc_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YV12");
      break;
    case XINE_IMGFMT_YUY2:
      this->xvmc_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YUY2");
      break;
    default:
      break;
    }
  }

  if(fo) {
    XLockDisplay(this->display);
    XFree(fo);
    XUnlockDisplay(this->display);
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works, using supported format
   */
  /*
    XLockDisplay(this->display);
    myimage = create_ximage (this, &myshminfo, 100, 100,
                             (this->xvmc_format_yv12 != 0) ? XINE_IMGFMT_YV12 : IMGFMT_YUY2);
    dispose_ximage (this, &myshminfo, myimage);
    XUnLockDisplay(this->display);
  */

  lprintf("initialization of plugin successful\n");
  return &this->vo_driver;
}

/*
 * class functions
 */

static void dispose_class (video_driver_class_t *this_gen) {
  xvmc_class_t        *this = (xvmc_class_t *) this_gen;

  XLockDisplay(this->display);
  XvFreeAdaptorInfo (this->adaptor_info);
  XUnlockDisplay(this->display);
  
  free (this);
}

static void *init_class (xine_t *xine, void *visual_gen) {
  x11_visual_t      *visual = (x11_visual_t *) visual_gen;
  xvmc_class_t      *this;
  Display           *display = NULL;
  unsigned int       adaptors, j = 0;
  unsigned int       ver,rel,req,ev,err;
  XvPortID           xv_port;
  XvAdaptorInfo     *adaptor_info;
  unsigned int       adaptor_num;
  /* XvMC */
  int                IDCTaccel = 0;
  int                useOverlay = 0;
  int                unsignedIntra = 0;
  unsigned int       surface_num, types;
  unsigned int       max_width=0, max_height=0;
  XvMCSurfaceInfo   *surfaceInfo;
  int                surface_type = 0;

  display = visual->display;

  /*
   * check for Xv and  XvMC video support
   */
  lprintf ("XvMC init_class\n");

  XLockDisplay(display);
  if (Success != XvQueryExtension(display, &ver, &rel, &req, &ev, &err)) {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "video_out_xvmc: Xv extension not present.\n");
    XUnlockDisplay(display);
    return NULL;
  }

  if(!XvMCQueryExtension(display, &ev, &err)) {
    xprintf (xine, XINE_VERBOSITY_LOG, _("video_out_xvmc: XvMC extension not present.\n"));
    XUnlockDisplay(display);
    return 0;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if(Success != XvQueryAdaptors(display, DefaultRootWindow(display),
				&adaptors, &adaptor_info))  {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "video_out_xvmc: XvQueryAdaptors failed.\n");
    XUnlockDisplay(display);
    return 0;
  }

  xv_port = 0;

  for ( adaptor_num = 0; (adaptor_num < adaptors) && !xv_port; adaptor_num++ ) {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "video_out_xvmc: checking adaptor %d\n",adaptor_num);
    if (adaptor_info[adaptor_num].type & XvImageMask) {
      surfaceInfo = XvMCListSurfaceTypes(display, adaptor_info[adaptor_num].base_id,
					 &types);
      if(surfaceInfo) {
	for(surface_num  = 0; surface_num < types; surface_num++) {
	  if((surfaceInfo[surface_num].chroma_format == XVMC_CHROMA_FORMAT_420) &&
	     (surfaceInfo[surface_num].mc_type == (XVMC_IDCT | XVMC_MPEG_2))) {

	    max_width  = surfaceInfo[surface_num].max_width;
	    max_height = surfaceInfo[surface_num].max_height;

	    for(j = 0; j < adaptor_info[adaptor_num].num_ports; j++) {
	      /* try to grab a port */
	      if(Success == XvGrabPort(display,
				       adaptor_info[adaptor_num].base_id + j, CurrentTime)) {
		xv_port = adaptor_info[adaptor_num].base_id + j;
		surface_type = surfaceInfo[surface_num].surface_type_id;
		break;
	      }
	    }

	    if(xv_port)
	      break;
	  }
	}

	if(!xv_port) { /* try for just XVMC_MOCOMP  */
	  xprintf (xine, XINE_VERBOSITY_DEBUG, "didn't find XVMC_IDCT acceleration trying for MC\n");

	  for(surface_num  = 0; surface_num < types; surface_num++) {
	    if((surfaceInfo[surface_num].chroma_format == XVMC_CHROMA_FORMAT_420) &&
	       ((surfaceInfo[surface_num].mc_type == (XVMC_MOCOMP | XVMC_MPEG_2)))) {

	      xprintf (xine, XINE_VERBOSITY_DEBUG, "Found XVMC_MOCOMP\n");
	      max_width = surfaceInfo[surface_num].max_width;
	      max_height = surfaceInfo[surface_num].max_height;

	      for(j = 0; j < adaptor_info[adaptor_num].num_ports; j++) {
		/* try to grab a port */
		if(Success == XvGrabPort(display,
					 adaptor_info[adaptor_num].base_id + j, CurrentTime)) {
		  xv_port = adaptor_info[adaptor_num].base_id + j;
		  surface_type = surfaceInfo[surface_num].surface_type_id;
		  break;
		}
	      }

	      if(xv_port)
		break;
	    }
	  }
	}
	if(xv_port) {
	  lprintf ("port %ld surface %d\n",xv_port,j);

          IDCTaccel = 0;
	  if(surfaceInfo[surface_num].flags & XVMC_OVERLAID_SURFACE)
	    useOverlay = 1;
	  if(surfaceInfo[surface_num].flags & XVMC_INTRA_UNSIGNED)
	    unsignedIntra = 1;
	  if(surfaceInfo[surface_num].mc_type == (XVMC_IDCT | XVMC_MPEG_2))
	    IDCTaccel |= XINE_VO_IDCT_ACCEL | XINE_VO_MOTION_ACCEL;
	  else if(surfaceInfo[surface_num].mc_type == (XVMC_MOCOMP | XVMC_MPEG_2)) {
	    IDCTaccel |= XINE_VO_MOTION_ACCEL;
	    if(!unsignedIntra)
	      IDCTaccel |= XINE_VO_SIGNED_INTRA;
	  }
	  xprintf (xine, XINE_VERBOSITY_DEBUG, "video_out_xvmc: IDCTaccel %02x\n",IDCTaccel);
	  break;
	}
	XFree(surfaceInfo);
      }
    }
  } /* outer for adaptor_num loop */


  if (!xv_port) {
    xprintf (xine, XINE_VERBOSITY_LOG,
	     _("video_out_xvmc: Xv extension is present but I couldn't find a usable yuv12 port.\n"));
    xprintf (xine, XINE_VERBOSITY_LOG, "              Looks like your graphics hardware "
	     "driver doesn't support Xv?!\n");
    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    XUnlockDisplay(display);
    return NULL;
  }
  else {
    xprintf (xine, XINE_VERBOSITY_LOG,
	     _("video_out_xvmc: using Xv port %ld from adaptor %s\n"
	       "                for hardware colour space conversion and scaling\n"),
	     xv_port, adaptor_info[adaptor_num].name);

    if(IDCTaccel&XINE_VO_IDCT_ACCEL)
      xprintf (xine, XINE_VERBOSITY_LOG, _("                idct and motion compensation acceleration \n"));
    else if (IDCTaccel&XINE_VO_MOTION_ACCEL)
      xprintf (xine, XINE_VERBOSITY_LOG, _("                motion compensation acceleration only\n"));
    else
      xprintf (xine, XINE_VERBOSITY_LOG, _("                no XvMC support \n"));
    xprintf (xine, XINE_VERBOSITY_LOG, _("                With Overlay = %d; UnsignedIntra = %d.\n"),
	     useOverlay, unsignedIntra);
  }

  XUnlockDisplay(display);

  this = (xvmc_class_t *) malloc (sizeof (xvmc_class_t));

  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "XvMC";
  this->driver_class.description     = N_("xine video output plugin using the XvMC X video extension");
  this->driver_class.dispose         = dispose_class;

  this->display                      = display;
  this->config                       = xine->config;
  this->xv_port                      = xv_port;
  this->adaptor_info                 = adaptor_info;
  this->adaptor_num                  = adaptor_num;
  this->surface_type_id              = surface_type;
  this->max_surface_width            = max_width;
  this->max_surface_height           = max_height;
  this->acceleration                 = IDCTaccel;

  this->xine                         = xine;

  lprintf("init_class done\n");

  return this;
}

static const vo_info_t vo_info_xvmc = {
  /* priority must be low until it supports displaying non-accelerated stuff */
  0,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xvmc", XINE_VERSION_CODE, &vo_info_xvmc, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

#endif
