/*
 * Copyright (c) 2004 The Unichrome project. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTIES OR REPRESENTATIONS; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "xvmc_vld.h"

static const uint8_t zig_zag_scan[64] ATTR_ALIGN(16) =
{
    /* Zig-Zag scan pattern */
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};

static const uint8_t alternate_scan [64] ATTR_ALIGN(16) =
{
    /* Alternate scan pattern */
    0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,
    41,33,26,18,3,11,4,12,19,27,34,42,50,58,35,43,
    51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,
    53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63
};

void mpeg2_xxmc_slice( mpeg2dec_accel_t *accel, picture_t *picture, 
		       int code, uint8_t *buffer, uint32_t chunk_size, 
		       uint8_t *chunk_buffer)

{
  vo_frame_t
    *frame = picture->current_frame;
  xine_xxmc_t 
    *xxmc = (xine_xxmc_t *) frame->accel_data;
  xine_vld_frame_t 
    *vft = &xxmc->vld_frame;
  unsigned
    mb_frame_height;
  int 
    i;
  const uint8_t *
    scan_pattern;
  float
    ms_per_slice;

  if (1 == code && accel->xvmc_last_slice_code != 1) {
    frame->bad_frame = 1;
    accel->slices_per_row = 1;
    accel->row_slice_count = 1;

    /*
     * Check that first field went through OK. Otherwise,
     * indicate bad frame. 
     */
    
    if (picture->second_field) {
      accel->xvmc_last_slice_code = (xxmc->decoded) ? 0 : -1;
      xxmc->decoded = 0;
    } else {
      accel->xvmc_last_slice_code = 0;
    }

    mb_frame_height =
      (!(picture->mpeg1) && (picture->progressive_sequence)) ?
      2*((picture->coded_picture_height+31) >> 5) :
      (picture->coded_picture_height+15) >> 4;
    accel->xxmc_mb_pic_height = (picture->picture_structure == FRAME_PICTURE ) ?
      mb_frame_height : mb_frame_height >> 1;

    ms_per_slice = 1000. / (90000. * mb_frame_height) * frame->duration;
    xxmc->sleep = 1. / (ms_per_slice * 0.45); 
    if (xxmc->sleep < 1.) xxmc->sleep = 1.;

    if (picture->mpeg1) {
      vft->mv_ranges[0][0] = picture->b_motion.f_code[0];
      vft->mv_ranges[0][1] = picture->b_motion.f_code[0];
      vft->mv_ranges[1][0] = picture->f_motion.f_code[0];
      vft->mv_ranges[1][1] = picture->f_motion.f_code[0];
    } else {
      vft->mv_ranges[0][0] = picture->b_motion.f_code[0];
      vft->mv_ranges[0][1] = picture->b_motion.f_code[1];
      vft->mv_ranges[1][0] = picture->f_motion.f_code[0];
      vft->mv_ranges[1][1] = picture->f_motion.f_code[1];
    }

    vft->picture_structure = picture->picture_structure;
    vft->picture_coding_type = picture->picture_coding_type;
    vft->mpeg_coding = (picture->mpeg1) ? 0 : 1;
    vft->progressive_sequence = picture->progressive_sequence;
    vft->scan = (picture->scan == mpeg2_scan_alt);
    vft->pred_dct_frame = picture->frame_pred_frame_dct;
    vft->concealment_motion_vectors = 
      picture->concealment_motion_vectors;
    vft->q_scale_type = picture->q_scale_type;
    vft->intra_vlc_format = picture->intra_vlc_format;
    vft->intra_dc_precision = picture->intra_dc_precision;
    vft->second_field = picture->second_field;

    /*
     * Translation of libmpeg2's Q-matrix layout to VLD XvMC's. 
     * Errors here will give
     * blocky artifacts and sometimes wrong colors.
     */

    scan_pattern = (vft->scan) ? alternate_scan : zig_zag_scan;

    if ((vft->load_intra_quantizer_matrix = picture->load_intra_quantizer_matrix)) {
      for (i=0; i<64; ++i) {
	vft->intra_quantizer_matrix[scan_pattern[i]] = 
	  picture->intra_quantizer_matrix[picture->scan[i]]; 
      }
    }      

    if ((vft->load_non_intra_quantizer_matrix = picture->load_non_intra_quantizer_matrix)) {
      for (i=0; i<64; ++i) {
	vft->non_intra_quantizer_matrix[scan_pattern[i]] = 
	  picture->non_intra_quantizer_matrix[picture->scan[i]];
      }
    }

    picture->load_intra_quantizer_matrix = 0;
    picture->load_non_intra_quantizer_matrix = 0;
    vft->forward_reference_frame = picture->forward_reference_frame;
    vft->backward_reference_frame = picture->backward_reference_frame;
    xxmc->proc_xxmc_begin( frame ); 
    if (xxmc->result != 0) {
      accel->xvmc_last_slice_code=-1;
    }
  }
  
  if (((code == accel->xvmc_last_slice_code + 1) || 
       (code == accel->xvmc_last_slice_code))) {

    /*
     * Send this slice to the output plugin. May stall for a long
     * time in proc_slice;
     */

    frame->bad_frame = 1;
    xxmc->slice_data_size = chunk_size;
    xxmc->slice_data = chunk_buffer;
    xxmc->slice_code = code;
    
    xxmc->proc_xxmc_slice( frame );
    
    if (xxmc->result != 0) {
	accel->xvmc_last_slice_code=-1;
	return;
    }
    /*
     * Keep track of slices.
     */ 

    accel->row_slice_count = (accel->xvmc_last_slice_code == code) ? 
      accel->row_slice_count + 1 : 1;
    accel->slices_per_row = (accel->row_slice_count > accel->slices_per_row) ? 
      accel->row_slice_count:accel->slices_per_row;
    accel->xvmc_last_slice_code = code;

  } else  {

    /*
     * An error has occured.
     */

    lprintf("libmpeg2: VLD XvMC: Slice error.\n");
    accel->xvmc_last_slice_code = -1;
    return;
  }
}

void mpeg2_xxmc_vld_frame_complete(mpeg2dec_accel_t *accel, picture_t *picture, int code) 
{
  vo_frame_t
    *frame = picture->current_frame;
  xine_xxmc_t 
    *xxmc = (xine_xxmc_t *) frame->accel_data;
  
  if (xxmc->decoded) return;
  if (accel->xvmc_last_slice_code == -1) {
    xxmc->proc_xxmc_flush( frame );
    return;
  }

  if ((code != 0xff) || ((accel->xvmc_last_slice_code == 
			  accel->xxmc_mb_pic_height) && 
			 accel->slices_per_row == accel->row_slice_count)) {

    xxmc->proc_xxmc_flush( frame );
    
    if (xxmc->result) {
      accel->xvmc_last_slice_code=-1;
      frame->bad_frame = 1;
      return;
    }
    xxmc->decoded = 1;
    accel->xvmc_last_slice_code = 0;
    if (picture->picture_structure == 3 || picture->second_field) {
      if (xxmc->result == 0) 
	frame->bad_frame = 0;
    } 
  }
}
