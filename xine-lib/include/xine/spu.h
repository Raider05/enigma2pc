/*
 * Copyright (C) 2007 the xine project
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef XINE_UTILS_SPU_H
#define XINE_UTILS_SPU_H

#ifndef XINE_PROTECTED
#define XINE_PROTECTED
#endif

typedef struct xine_spu_opacity_s xine_spu_opacity_t;

struct xine_spu_opacity_s {
  uint8_t black, colour;
};

void _x_spu_misc_init (xine_t *);

void _x_spu_get_opacity (xine_t *, xine_spu_opacity_t *) XINE_PROTECTED;

/*  in: trans = 0..255, 0=opaque
 * out:         0..255, 0=transparent
 */
int _x_spu_calculate_opacity (const clut_t *, uint8_t trans, const xine_spu_opacity_t *) XINE_PROTECTED;

#endif
