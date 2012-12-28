/*
 * libmpeg2_accel.c
 * Copyright (C) 2004 The Unichrome Project.
 * Copyright (C) 2005 Thomas Hellstrom.
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */

#include <xine/xine_internal.h>
#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "xvmc_vld.h"
#include "libmpeg2_accel.h"


void 
libmpeg2_accel_scan( mpeg2dec_accel_t *accel, uint8_t *scan_norm, uint8_t *scan_alt)
{
  xvmc_setup_scan_ptable();
}


int
libmpeg2_accel_discontinuity(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture)
{
  accel->xvmc_last_slice_code=-1;
  if ( !picture->current_frame )
    return 0;
  if (frame_format == XINE_IMGFMT_XXMC) {
    xine_xxmc_t *xxmc = (xine_xxmc_t *) 
      picture->current_frame->accel_data;
    switch(xxmc->acceleration) {
    case XINE_XVMC_ACCEL_VLD:
    case XINE_XVMC_ACCEL_IDCT:
    case XINE_XVMC_ACCEL_MOCOMP:
      xxmc->proc_xxmc_flush( picture->current_frame );
      break;
    default:
      break;
    }
  }
  return 0;
}

int 
libmpeg2_accel_new_sequence(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture)
{
  switch(frame_format) {
  case XINE_IMGFMT_XXMC:
  case XINE_IMGFMT_XVMC: {
    xine_xvmc_t *xvmc = (xine_xvmc_t *) 
      picture->current_frame->accel_data;
    picture->mc = xvmc->macroblocks;
    return 0;
  }
  default:
    break;
  }
  return 1;
}

int
libmpeg2_accel_new_frame(mpeg2dec_accel_t *accel, uint32_t frame_format, 
			 picture_t *picture, double ratio, uint32_t flags)
{  
  if (picture->current_frame) {
    if (XINE_IMGFMT_XXMC == frame_format) {
      xine_xxmc_t *xxmc = (xine_xxmc_t *) 
	picture->current_frame->accel_data;
      
      /*
       * Make a request for acceleration type and mpeg coding from
       * the output plugin.
       */
      
      xxmc->fallback_format = XINE_IMGFMT_YV12;
      xxmc->acceleration = XINE_XVMC_ACCEL_VLD| XINE_XVMC_ACCEL_IDCT
	| XINE_XVMC_ACCEL_MOCOMP ;

      /*
       * Standard MOCOMP / IDCT XvMC implementation for interlaced streams 
       * is buggy. The bug is inherited from the old XvMC driver. Don't use it until
       * it has been fixed. (A volunteer ?)
       */

      if ( picture->picture_structure != 3 ) {
	picture->top_field_first = (picture->picture_structure == 1);
	xxmc->acceleration &= ~( XINE_XVMC_ACCEL_IDCT |  XINE_XVMC_ACCEL_MOCOMP );
      } 

      xxmc->mpeg = (picture->mpeg1) ? XINE_XVMC_MPEG_1:XINE_XVMC_MPEG_2;
      xxmc->proc_xxmc_update_frame (picture->current_frame->driver, 
				    picture->current_frame,
				    picture->coded_picture_width,
				    picture->coded_picture_height,
				    ratio,
				    XINE_IMGFMT_XXMC, flags);
    }
  }
  return 0;
}

void
libmpeg2_accel_frame_completion(mpeg2dec_accel_t * accel, uint32_t frame_format, picture_t *picture,
				int code)
{
	
  if ( !picture->current_frame ) return;
  
  if (frame_format == XINE_IMGFMT_XXMC) {
    xine_xxmc_t *xxmc = (xine_xxmc_t *) 
      picture->current_frame->accel_data;
    if (!xxmc->decoded) {
      switch(picture->current_frame->format) {
      case XINE_IMGFMT_XXMC:
	switch(xxmc->acceleration) {
	case XINE_XVMC_ACCEL_VLD:
	  mpeg2_xxmc_vld_frame_complete(accel, picture, code);
	  break;
	case XINE_XVMC_ACCEL_IDCT:
	case XINE_XVMC_ACCEL_MOCOMP:
	  xxmc->decoded = !picture->current_frame->bad_frame;
	  xxmc->proc_xxmc_flush( picture->current_frame );
	  break;
	default:
	  break;
	}
      default:
	break;
      }
    }
  }
}


int 
libmpeg2_accel_slice(mpeg2dec_accel_t *accel, picture_t *picture, int code, char * buffer, 
		     uint32_t chunk_size, uint8_t *chunk_buffer)
{
  /*
   * Don't reference frames of other formats. They are invalid. This may happen if the 
   * xxmc plugin suddenly falls back to software decoding.
   */

  if (( picture->current_frame->picture_coding_type == XINE_PICT_P_TYPE ) ||
      ( picture->current_frame->picture_coding_type == XINE_PICT_B_TYPE )) {
    if (! picture->forward_reference_frame) return 1;
    if (picture->forward_reference_frame->format != picture->current_frame->format) {
      picture->v_offset = 0;
      return 1;
    }
  }

  if ( picture->current_frame->picture_coding_type == XINE_PICT_B_TYPE ) {
    if (! picture->backward_reference_frame) return 1;
    if (picture->backward_reference_frame->format != picture->current_frame->format) {
      picture->v_offset = 0;
      return 1;
    }
  }
      
  switch( picture->current_frame->format ) {

  case XINE_IMGFMT_XXMC:
    {
      xine_xxmc_t *xxmc = (xine_xxmc_t *) 
	picture->current_frame->accel_data;
      
      if ( xxmc->proc_xxmc_lock_valid( picture->current_frame,
				       picture->forward_reference_frame,
				       picture->backward_reference_frame,
				       picture->current_frame->picture_coding_type)) {
	picture->v_offset = 0;
	return 1;
      }
      
      switch(picture->current_frame->format) {
      case XINE_IMGFMT_XXMC:
	switch(xxmc->acceleration) {
	case XINE_XVMC_ACCEL_VLD:
	  mpeg2_xxmc_slice(accel, picture, code, buffer, chunk_size, chunk_buffer);
	  break;
	case XINE_XVMC_ACCEL_IDCT:
	case XINE_XVMC_ACCEL_MOCOMP:
	  mpeg2_xvmc_slice (accel, picture, code, buffer);
	  break;
	default:
	  mpeg2_slice (picture, code, buffer);
	  break;
	}
	break;
      default:
	mpeg2_slice (picture, code, buffer);
	break;
      }
      xxmc->proc_xxmc_unlock(picture->current_frame->driver);
      break;
    }

  case XINE_IMGFMT_XVMC:
    mpeg2_xvmc_slice (accel, picture, code, buffer);
    break;

  default:
    mpeg2_slice (picture, code, buffer);
    break;
  }
  return 0;
}
