/*
 * libmpeg2_accel.h
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

#ifndef LIBMPEG2_ACCEL_H
#define LIBMPEG2_ACCEL_H

#include "mpeg2_internal.h"

/*
 * Internal context data type.
 */

typedef struct {
  int xvmc_last_slice_code;
  int slices_per_row;
  int row_slice_count;
  unsigned xxmc_mb_pic_height;
} mpeg2dec_accel_t;

extern int libmpeg2_accel_discontinuity(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture);
extern int libmpeg2_accel_new_sequence(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture);
extern int libmpeg2_accel_new_frame(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture, double ratio, uint32_t flags);
extern void libmpeg2_accel_frame_completion(mpeg2dec_accel_t *accel, uint32_t frame_format, picture_t *picture, int code);

extern int libmpeg2_accel_slice(mpeg2dec_accel_t *accel, picture_t *picture, int code, 
				char * buffer, uint32_t chunk_size, uint8_t *chunk_buffer);
extern void libmpeg2_accel_scan( mpeg2dec_accel_t *accel, uint8_t *scan_norm, uint8_t *scan_alt);

#endif
