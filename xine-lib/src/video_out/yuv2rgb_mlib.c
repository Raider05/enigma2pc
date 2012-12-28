/*
 * yuv2rgb_mlib.c
 * Copyright (C) 2000-2001 Silicon Integrated System Corp.
 * All Rights Reserved.
 *
 * Author: Juergen Keil <jk@tools.de>
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
 */

#include "config.h"

#if HAVE_MLIB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <mlib_algebra.h>
#include <mlib_video.h>

#include <xine/attributes.h>
#include <xine/xineutils.h>
#include "yuv2rgb.h"

static void mlib_yuv420_rgb24(yuv2rgb_t *this,
			      uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv)
{
  int src_height = MIN(this->slice_height, this->source_height-this->slice_offset) &~ 1;
  int dst_height;

  dst_height = this->next_slice(this, &image);
  if (this->do_scale) {
    mlib_u8 *resize_buffer = this->mlib_resize_buffer;
    mlib_s32 resize_stride = this->dest_width << 2;

    mlib_VideoColorYUV420seq_to_ARGBint((mlib_u32*)this->mlib_buffer,
					py, pu, pv, py, 0,
					this->source_width,
					src_height,
					this->source_width<<2,
					this->y_stride,
					this->uv_stride);
    mlib_VideoColorResizeABGR((mlib_u32*)resize_buffer,
			      (mlib_u32*)this->mlib_buffer,
			      this->dest_width,dst_height,resize_stride,
			      this->source_width, src_height,this->source_width<<2,
			      this->mlib_filter_type);

    while(dst_height--) {
      mlib_VideoColorABGR2RGB(image, resize_buffer, this->dest_width);
      image += this->rgb_stride;
      resize_buffer += resize_stride;
    }
  } else {
    mlib_VideoColorYUV2RGB420(image, py, pu, pv,
			      this->source_width,
			      dst_height,
			      this->rgb_stride,
			      this->y_stride,
			      this->uv_stride);
  }
}

static void mlib_yuv420_argb32(yuv2rgb_t *this,
			       uint8_t * image, uint8_t * py,
			       uint8_t * pu, uint8_t * pv)
{
  int src_height = MIN(this->slice_height, this->source_height-this->slice_offset) &~ 1;
  int dst_height;

  dst_height = this->next_slice(this, &image);
  if (this->do_scale) {
    mlib_VideoColorYUV420seq_to_ARGBint((mlib_u32*)this->mlib_buffer,
					py, pu, pv, py, 0,
					this->source_width,
					src_height,
					this->source_width<<2,
					this->y_stride,
					this->uv_stride);
	mlib_VideoColorResizeABGR((mlib_u32*)image,
				  (mlib_u32*)this->mlib_buffer,
				  this->dest_width,dst_height,this->rgb_stride,
				  this->source_width, src_height,this->source_width<<2,
				  this->mlib_filter_type);
  } else {
    mlib_VideoColorYUV420seq_to_ARGBint((mlib_u32*)image,
					py, pu, pv, py, 0,
					this->source_width,
					dst_height,
					this->rgb_stride,
					this->y_stride,
					this->uv_stride);
  }

  if (this->swapped) {
    while (dst_height--) {
      mlib_VectorReverseByteOrder_U32((mlib_u32*)image, this->dest_width);
      image += this->rgb_stride;
    }
  }
}

static void mlib_yuv420_abgr32(yuv2rgb_t *this,
			       uint8_t * image, uint8_t * py,
			       uint8_t * pu, uint8_t * pv)
{
  int src_height = MIN(this->slice_height, this->source_height-this->slice_offset) &~ 1;
  int dst_height;

  dst_height = this->next_slice (this, &image);
  if (this->do_scale) {
    mlib_VideoColorYUV420seq_to_ABGRint((mlib_u32*)this->mlib_buffer,
					py, pu, pv, py, 0,
					this->source_width,
					src_height,
					this->source_width<<2,
					this->y_stride,
					this->uv_stride);
    mlib_VideoColorResizeABGR((mlib_u32*)image,
			      (mlib_u32*)this->mlib_buffer,
			      this->dest_width,dst_height,this->rgb_stride,
			      this->source_width, src_height, this->source_width<<2,
			      this->mlib_filter_type);
  }
  else {
    mlib_VideoColorYUV420seq_to_ABGRint((mlib_u32*)image,
					py, pu, pv, py, 0,
					this->source_width,
					dst_height,
					this->rgb_stride,
					this->y_stride,
					this->uv_stride);
  }

  if (this->swapped) {
    while (dst_height--) {
      mlib_VectorReverseByteOrder_U32((mlib_u32*)image, this->dest_width);
      image += this->rgb_stride;
    }
  }
}

void yuv2rgb_init_mlib (yuv2rgb_factory_t *this)
{
  switch (this->mode) {
  case MODE_24_RGB:
    if (this->swapped) break;
    this->yuv2rgb_fun = mlib_yuv420_rgb24;
    break;
  case MODE_32_RGB:
    this->yuv2rgb_fun = mlib_yuv420_argb32;
    break;
  case MODE_32_BGR:
    this->yuv2rgb_fun = mlib_yuv420_abgr32;
    break;
  }
}

#endif	/* HAVE_MLIB */
