/*
 * Copyright (C) 2000-2003 the xine project
 * Copyright (C) 2004 the Unichrome project
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
 *
 * Common acceleration definitions for XvMC.
 *
 *
 */

#ifndef HAVE_XINE_ACCEL_H
#define HAVE_XINE_ACCEL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct xine_macroblock_s {
  short  *blockptr;          /* pointer to current dct block */
  short  *blockbaseptr;      /* pointer to base of dct block array in blocks */
  short   xvmc_accel;        /* type of acceleration supported */
} xine_macroblocks_t;

typedef struct xine_vld_frame_s {
  int version;              /* Backward compatibility */
  int mv_ranges[2][2];
  int picture_structure;
  int picture_coding_type;
  int intra_dc_precision;
  int mpeg_coding;
  int progressive_sequence;
  int scan;
  int pred_dct_frame;
  int concealment_motion_vectors;
  int q_scale_type;
  int intra_vlc_format;
  int second_field;
  int load_intra_quantizer_matrix;
  int load_non_intra_quantizer_matrix;
  uint8_t intra_quantizer_matrix[64];
  uint8_t non_intra_quantizer_matrix[64];
  vo_frame_t *backward_reference_frame;
  vo_frame_t *forward_reference_frame;
} xine_vld_frame_t;


typedef struct xine_xvmc_s {
  vo_frame_t *vo_frame;
  xine_macroblocks_t *macroblocks;
  void (*proc_macro_block)(int x,int y,int mb_type,
			   int motion_type,int (*mv_field_sel)[2],
			   int *dmvector,int cbp,int dct_type,
			   vo_frame_t *current_frame,vo_frame_t *forward_ref_frame,
			   vo_frame_t *backward_ref_frame,int picture_structure,
			   int second_field,int (*f_mot_pmv)[2],int (*b_mot_pmv)[2]);
} xine_xvmc_t ;

#define XVMC_DATA(frame_gen)  ((frame_gen) ? (xine_xvmc_t *)(frame_gen)->accel_data : (xine_xvmc_t *)0)
#define XVMC_FRAME(frame_gen) ((frame_gen) ? (xvmc_frame_t *)XVMC_DATA(frame_gen)->vo_frame : (xvmc_frame_t *)0)

typedef struct xine_xxmc_s {

  /*
   * We inherit the xine_xvmc_t properties.
   */

  xine_xvmc_t xvmc;

  unsigned mpeg;
  unsigned acceleration;
  int fallback_format;
  xine_vld_frame_t vld_frame;
  uint8_t *slice_data;
  unsigned slice_data_size;
  unsigned slice_code;
  int result;
  int decoded;
  float sleep;
  void (*proc_xxmc_update_frame) (vo_driver_t *this_gen, vo_frame_t *frame_gen,
				  uint32_t width, uint32_t height, double ratio,
				  int format, int flags);
  void (*proc_xxmc_begin) (vo_frame_t *vo_img);
  void (*proc_xxmc_slice) (vo_frame_t *vo_img);
  void (*proc_xxmc_flush) (vo_frame_t *vo_img);

  /*
   * For thread-safety only.
   */

  int  (*proc_xxmc_lock_valid) (vo_frame_t *cur_frame, vo_frame_t *fw_frame,
				vo_frame_t *bw_frame,unsigned pc_type);
  void (*proc_xxmc_unlock) (vo_driver_t *this_gen);
} xine_xxmc_t;

#define XXMC_DATA(frame_gen)  ((frame_gen) ? (xine_xxmc_t *)(frame_gen)->accel_data : (xine_xxmc_t *)0)
#define XXMC_FRAME(frame_gen) ((frame_gen) ? (xxmc_frame_t *)XXMC_DATA(frame_gen)->xvmc.vo_frame : (xxmc_frame_t *)0)

  /*
   * Register XvMC stream types here.
   */

#define XINE_XVMC_MPEG_1 0x00000001
#define XINE_XVMC_MPEG_2 0x00000002
#define XINE_XVMC_MPEG_4 0x00000004

  /*
   * Register XvMC acceleration levels here.
   */

#define XINE_XVMC_ACCEL_MOCOMP 0x00000001
#define XINE_XVMC_ACCEL_IDCT   0x00000002
#define XINE_XVMC_ACCEL_VLD    0x00000004


/* xvmc acceleration types */
#define XINE_VO_MOTION_ACCEL   1
#define XINE_VO_IDCT_ACCEL     2
#define XINE_VO_SIGNED_INTRA   4

/* motion types */
#define XINE_MC_FIELD 1
#define XINE_MC_FRAME 2
#define XINE_MC_16X8  2
#define XINE_MC_DMV   3

/* picture coding type */
#define XINE_PICT_I_TYPE 1
#define XINE_PICT_P_TYPE 2
#define XINE_PICT_B_TYPE 3
#define XINE_PICT_D_TYPE 4

/* macroblock modes */
#define XINE_MACROBLOCK_INTRA 1
#define XINE_MACROBLOCK_PATTERN 2
#define XINE_MACROBLOCK_MOTION_BACKWARD 4
#define XINE_MACROBLOCK_MOTION_FORWARD 8
#define XINE_MACROBLOCK_QUANT 16
#define XINE_MACROBLOCK_DCT_TYPE_INTERLACED 32

#ifdef __cplusplus
}
#endif

#endif

