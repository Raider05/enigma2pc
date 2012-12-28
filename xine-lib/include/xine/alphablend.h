/*
 *
 * Copyright (C) 2000  Thomas Mirlacher
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 *
 * The author may be reached as <dent@linuxvideo.org>
 *
 *------------------------------------------------------------
 *
 */

#ifndef __ALPHABLEND_H__
#define __ALPHABLEND_H__

#include "video_out.h"

typedef struct {
  void *buffer;
  int buffer_size;

  int disable_exact_blending;

  int offset_x, offset_y;
} alphablend_t;

void _x_alphablend_init(alphablend_t *extra_data, xine_t *xine) XINE_PROTECTED;
void _x_alphablend_free(alphablend_t *extra_data) XINE_PROTECTED;

typedef struct {         /* CLUT == Color LookUp Table */
  uint8_t cb;
  uint8_t cr;
  uint8_t y;
  uint8_t foo;
} XINE_PACKED clut_t;

#define XX44_PALETTE_SIZE 32

typedef struct {
  unsigned size;
  unsigned max_used;
  uint32_t cluts[XX44_PALETTE_SIZE];
  /* cache palette entries for both colors and hili_colors */
  int lookup_cache[OVL_PALETTE_SIZE*2];
} xx44_palette_t;


void _x_blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data) XINE_PROTECTED;

void _x_blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data) XINE_PROTECTED;

void _x_blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data) XINE_PROTECTED;

void _x_blend_yuv (uint8_t *dst_base[3], vo_overlay_t * img_overl,
                int dst_width, int dst_height, int dst_pitches[3],
                alphablend_t *extra_data) XINE_PROTECTED;

void _x_blend_yuy2 (uint8_t * dst_img, vo_overlay_t * img_overl,
                 int dst_width, int dst_height, int dst_pitch,
                 alphablend_t *extra_data) XINE_PROTECTED;

/*
 * This function isn't too smart about blending. We want to avoid creating new
 * colors in the palette as a result from two non-zero colors needed to be
 * blended. Instead we choose the color with the highest alpha value to be
 * visible. Some parts of the code taken from the "VeXP" project.
 */

void _x_blend_xx44 (uint8_t *dst_img, vo_overlay_t *img_overl,
		int dst_width, int dst_height, int dst_pitch,
                alphablend_t *extra_data,
		xx44_palette_t *palette,int ia44) XINE_PROTECTED;

/*
 * Functions to handle the xine-specific palette.
 */

void _x_clear_xx44_palette(xx44_palette_t *p) XINE_PROTECTED;
void _x_init_xx44_palette(xx44_palette_t *p, unsigned num_entries) XINE_PROTECTED;
void _x_dispose_xx44_palette(xx44_palette_t *p) XINE_PROTECTED;

/*
 * Convert the xine-specific palette to something useful.
 */

void _x_xx44_to_xvmc_palette(const xx44_palette_t *p,unsigned char *xvmc_palette,
			  unsigned first_xx44_entry, unsigned num_xx44_entries,
			  unsigned num_xvmc_components, char *xvmc_components) XINE_PROTECTED;


#endif
