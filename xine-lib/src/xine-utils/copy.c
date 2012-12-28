/*
 * Copyright (C) 2004 the xine project
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
 * $Id:
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xineutils.h>

void yv12_to_yv12
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dst, int y_dst_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dst, int u_dst_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dst, int v_dst_pitch,
   int width, int height) {

  int y, half_width = width / 2;

  /* Y Plane */
  if(y_src_pitch == y_dst_pitch)
    xine_fast_memcpy(y_dst, y_src, y_src_pitch*height);
  else {
    for(y = 0; y < height; y++) {
      xine_fast_memcpy(y_dst, y_src, width);
      y_src += y_src_pitch;
      y_dst += y_dst_pitch;
    }
  }

  /* U/V Planes */
  if((u_src_pitch == u_dst_pitch) && (v_src_pitch == v_dst_pitch)) {
    xine_fast_memcpy(u_dst, u_src, u_src_pitch*height/2);
    xine_fast_memcpy(v_dst, v_src, v_src_pitch*height/2);
  } else {
    for(y = 0; y < (height / 2); y++) {
      xine_fast_memcpy(u_dst, u_src, half_width);
      xine_fast_memcpy(v_dst, v_src, half_width);

      u_src += u_src_pitch;
      v_src += v_src_pitch;

      u_dst += u_dst_pitch;
      v_dst += v_dst_pitch;
    }
  }
}

void yuy2_to_yuy2
  (const unsigned char *src, int src_pitch,
   unsigned char *dst, int dst_pitch,
   int width, int height) {

  int y, double_width = width * 2;

  if(src_pitch == dst_pitch)
    xine_fast_memcpy(dst, src, src_pitch*height);
  else {
    for(y = 0; y < height; y++) {
      xine_fast_memcpy(dst, src, double_width);
      src += src_pitch;
      dst += dst_pitch;
    }
  }
}
