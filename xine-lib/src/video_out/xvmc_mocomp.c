/*
 * Copyright (C) 2000-2004 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * * xine is free software; you can redistribute it and/or modify
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
 * XvMC image support by Jack Kelliher
 */

#include "xxmc.h"


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




static void xvmc_render_macro_blocks(vo_frame_t *current_image,
				     vo_frame_t *backward_ref_image,
				     vo_frame_t *forward_ref_image,
				     int picture_structure,
				     int second_field,
				     xvmc_macroblocks_t *macroblocks) {
  xxmc_driver_t *this           = (xxmc_driver_t *) current_image->driver;
  xxmc_frame_t  *current_frame  = XXMC_FRAME(current_image);
  xxmc_frame_t  *forward_frame  = XXMC_FRAME(forward_ref_image);
  xxmc_frame_t  *backward_frame = XXMC_FRAME(backward_ref_image);
  int           flags;

  lprintf ("xvmc_render_macro_blocks\n");
  lprintf ("slices %d 0x%08lx 0x%08lx 0x%08lx\n",
	   macroblocks->slices,
	   (long) current_frame, (long) backward_frame,
	   (long) forward_frame);

  flags = second_field;

  XVMCLOCKDISPLAY( this->display);
  XvMCRenderSurface(this->display, &this->context, picture_structure,
		    current_frame->xvmc_surf,
		    forward_frame ? forward_frame->xvmc_surf : NULL,
		    backward_frame ? backward_frame->xvmc_surf : NULL,
                    flags,
		    macroblocks->slices, 0, &macroblocks->macro_blocks,
		    &macroblocks->blocks);
  XVMCUNLOCKDISPLAY( this->display);
}



void xxmc_xvmc_proc_macro_block(int x, int y, int mb_type, int motion_type,
				int (*mv_field_sel)[2], int *dmvector, int cbp,
				int dct_type, vo_frame_t *current_frame,
				vo_frame_t *forward_ref_frame,
				vo_frame_t *backward_ref_frame, int picture_structure,
				int second_field, int (*f_mot_pmv)[2], int (*b_mot_pmv)[2])
{
  xxmc_driver_t        *this                = (xxmc_driver_t *) current_frame->driver;
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

  cbp &= 0x3F;
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

    xvmc_render_macro_blocks(
			     current_frame,
			     (picture_coding_type == XINE_PICT_B_TYPE) ?
			     backward_ref_frame : NULL,
			     (picture_coding_type != XINE_PICT_I_TYPE) ?
			     forward_ref_frame : NULL,
			     picture_structure,
			     second_field ? XVMC_SECOND_FIELD : 0,
			     mbs);

    mbs->num_blocks       = 0;
    mbs->macroblockptr    = mbs->macroblockbaseptr;
    mbs->xine_mc.blockptr = mbs->xine_mc.blockbaseptr;
  }
}



