/*
 * yuv2rgb_mmx.c
 * Copyright (C) 2000-2001 Silicon Integrated System Corp.
 * All Rights Reserved.
 *
 * Author: Olie Lho <ollie@sis.com.tw>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

#include "config.h"

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#include "yuv2rgb.h"
#include <xine/xineutils.h>
#include "xine_mmx.h"

#define CPU_MMXEXT 0
#define CPU_MMX 1

/* CPU_MMXEXT/CPU_MMX adaptation layer */

#define movntq(src,dest)	\
do {				\
    if (cpu == CPU_MMXEXT)	\
	movntq_r2m (src, dest);	\
    else			\
	movq_r2m (src, dest);	\
} while (0)

typedef struct mmx_csc_s mmx_csc_t;

struct mmx_csc_s {
  mmx_t x00ffw;
  mmx_t x0080w;
  mmx_t addYw;
  mmx_t U_green;
  mmx_t U_blue;
  mmx_t V_red;
  mmx_t V_green;
  mmx_t Y_coeff;
};

extern const int32_t Inverse_Table_6_9[8][4];

void mmx_yuv2rgb_set_csc_levels(yuv2rgb_factory_t *this,
  int brightness, int contrast, int saturation, int colormatrix)
{
  int i, cty;

  int yoffset = -16;
  int ygain = ((1 << 16) * 255) / 219;

  int cm = (colormatrix >> 1) & 7;
  int crv = Inverse_Table_6_9[cm][0];
  int cbu = Inverse_Table_6_9[cm][1];
  int cgu = Inverse_Table_6_9[cm][2];
  int cgv = Inverse_Table_6_9[cm][3];

  mmx_csc_t *csc;

  /* 'table_mmx' is 64bit aligned for better performance */
  if (this->table_mmx == NULL) {
    this->table_mmx = av_mallocz(sizeof(mmx_csc_t));
  }

  /* full range mode */
  if (colormatrix & 1) {
    yoffset = 0;
    ygain = (1 << 16);

    crv = (crv * 112 + 63) / 127;
    cbu = (cbu * 112 + 63) / 127;
    cgu = (cgu * 112 + 63) / 127;
    cgv = (cgv * 112 + 63) / 127;
  }

  yoffset += brightness;
  /* TV set behaviour: contrast affects color difference as well */
  saturation = (contrast * saturation + 64) >> 7;

  csc = (mmx_csc_t *) this->table_mmx;

  crv = (crv * saturation + 512) / 1024;
  cbu = (cbu * saturation + 512) / 1024;
  cbu = (cbu > 32767) ? 32767 : cbu;
  cgu = (cgu * saturation + 512) / 1024;
  cgv = (cgv * saturation + 512) / 1024;
  cty = (ygain * contrast + 512) / 1024;

  /* the 8 is "+0,5" for later rounding */
  yoffset = ((cty * (yoffset << 7)) >> 16) + 8;

  for (i=0; i < 4; i++) {
    csc->U_green.w[i] = -cgu;
    csc->U_blue.w[i]  =  cbu;
    csc->V_red.w[i]   =  crv;
    csc->V_green.w[i] = -cgv;
    csc->Y_coeff.w[i] =  cty;

    csc->addYw.w[i]   = yoffset;

    csc->x0080w.w[i]  = 128;
    csc->x00ffw.w[i]  = 0xff;
  }
}

static inline void mmx_yuv2rgb (uint8_t * py, uint8_t * pu, uint8_t * pv, mmx_csc_t *csc)
{

    /* OK what we're doing here is
       y = ((cty * (y << 7)) >> 16) + yoffset;
       u = (u - 128) << 7;
       v = (v - 128) << 7;
       r = (y + ((crv * v) >> 16)) >> 4;
       g = (y + ((cgu * u) >> 16) + ((cgv * v) >> 16)) >> 4;
       b = (y + ((cbu * u) >> 16)) >> 4; */

    movq_m2r (*py, mm6);		// mm6 = Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
    pxor_r2r (mm4, mm4);		// mm4 = 0
    movd_m2r (*pu, mm0);		// mm0 = 00 00 00 00 u3 u2 u1 u0

    movq_r2r (mm6, mm7);		// mm7 = Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
    pand_m2r (csc->x00ffw, mm6);	// mm6 =    Y6    Y4    Y2    Y0

    psrlw_i2r (8, mm7);			// mm7 =    Y7    Y5    Y3    Y1
    psllw_i2r (7, mm6);			// promote precision
    movd_m2r (*pv, mm1);		// mm1 = 00 00 00 00 v3 v2 v1 v0

    pmulhw_m2r (csc->Y_coeff, mm6);	// mm6 = luma_rgb even
    psllw_i2r (7, mm7);			// promote precision
    punpcklbw_r2r (mm4, mm0);		// mm0 = u3 u2 u1 u0

    paddsw_m2r (csc->addYw, mm6);	// += yoffset
    psubsw_m2r (csc->x0080w, mm0);	// u -= 128
    punpcklbw_r2r (mm4, mm1);		// mm1 = v3 v2 v1 v0
    pmulhw_m2r (csc->Y_coeff, mm7);	// mm7 = luma_rgb odd

    psllw_i2r (7, mm0);			// promote precision
    psubsw_m2r (csc->x0080w, mm1);	// v -= 128

    movq_r2r (mm0, mm2);		// mm2 = u3 u2 u1 u0
    psllw_i2r (7, mm1);			// promote precision

    movq_r2r (mm1, mm4);		// mm4 = v3 v2 v1 v0

    paddsw_m2r (csc->addYw, mm7);	// += yoffset
    pmulhw_m2r (csc->U_blue, mm0);	// mm0 = chroma_b
    pmulhw_m2r (csc->V_red, mm1);	// mm1 = chroma_r

    movq_r2r (mm0, mm3);		// mm3 = chroma_b

    paddsw_r2r (mm6, mm0);		// mm0 = B6 B4 B2 B0
    paddsw_r2r (mm7, mm3);		// mm3 = B7 B5 B3 B1

    psraw_i2r (4, mm0);			// div round
    pmulhw_m2r (csc->U_green, mm2);	// mm2 = u * u_green
    psraw_i2r (4, mm3);			// div round

    packuswb_r2r (mm0, mm0);		// saturate to 0-255
    packuswb_r2r (mm3, mm3);		// saturate to 0-255
    pmulhw_m2r (csc->V_green, mm4);	// mm4 = v * v_green

    punpcklbw_r2r (mm3, mm0);		// mm0 = B7 B6 B5 B4 B3 B2 B1 B0
    paddsw_r2r (mm4, mm2);		// mm2 = chroma_g
    movq_r2r (mm1, mm4);		// mm4 = chroma_r

    movq_r2r (mm2, mm5);		// mm5 = chroma_g

    paddsw_r2r (mm6, mm2);		// mm2 = G6 G4 G2 G0

    psraw_i2r (4, mm2);			// div round
    paddsw_r2r (mm6, mm1);		// mm1 = R6 R4 R2 R0

    packuswb_r2r (mm2, mm2);		// saturate to 0-255
    psraw_i2r (4, mm1);			// div round
    paddsw_r2r (mm7, mm4);		// mm4 = R7 R5 R3 R1

    packuswb_r2r (mm1, mm1);		// saturate to 0-255
    psraw_i2r (4, mm4);			// div round
    paddsw_r2r (mm7, mm5);		// mm5 = G7 G5 G3 G1

    packuswb_r2r (mm4, mm4);		// saturate to 0-255
    psraw_i2r (4, mm5);			// div round

    punpcklbw_r2r (mm4, mm1);		// mm1 = R7 R6 R5 R4 R3 R2 R1 R0
    packuswb_r2r (mm5, mm5);		// saturate to 0-255

    punpcklbw_r2r (mm5, mm2);		// mm2 = G7 G6 G5 G4 G3 G2 G1 G0
}

// basic opt
static inline void mmx_unpack_16rgb (uint8_t * image, int cpu)
{
    static mmx_t mmx_bluemask = {0xf8f8f8f8f8f8f8f8ULL};
    static mmx_t mmx_greenmask = {0xfcfcfcfcfcfcfcfcULL};
    static mmx_t mmx_redmask = {0xf8f8f8f8f8f8f8f8ULL};

    /*
     * convert RGB plane to RGB 16 bits
     * mm0 -> B, mm1 -> R, mm2 -> G
     * mm4 -> GB, mm5 -> AR pixel 4-7
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    pand_m2r (mmx_bluemask, mm0);	// mm0 = b7b6b5b4b3______
    pxor_r2r (mm4, mm4);		// mm4 = 0

    pand_m2r (mmx_greenmask, mm2);	// mm2 = g7g6g5g4g3g2____
    psrlq_i2r (3, mm0);			// mm0 = ______b7b6b5b4b3

    movq_r2r (mm2, mm7);		// mm7 = g7g6g5g4g3g2____
    movq_r2r (mm0, mm5);		// mm5 = ______b7b6b5b4b3

    pand_m2r (mmx_redmask, mm1);	// mm1 = r7r6r5r4r3______
    punpcklbw_r2r (mm4, mm2);

    punpcklbw_r2r (mm1, mm0);

    psllq_i2r (3, mm2);

    punpckhbw_r2r (mm4, mm7);
    por_r2r (mm2, mm0);

    psllq_i2r (3, mm7);

    movntq (mm0, *image);
    punpckhbw_r2r (mm1, mm5);

    por_r2r (mm7, mm5);

    // U
    // V

    movntq (mm5, *(image+8));
}

static inline void mmx_unpack_15rgb (uint8_t * image, int cpu)
{
    static mmx_t mmx_bluemask = {0xf8f8f8f8f8f8f8f8ULL};
    static mmx_t mmx_greenmask = {0xf8f8f8f8f8f8f8f8ULL};
    static mmx_t mmx_redmask = {0xf8f8f8f8f8f8f8f8ULL};

    /*
     * convert RGB plane to RGB 15 bits
     * mm0 -> B, mm1 -> R, mm2 -> G
     * mm4 -> GB, mm5 -> AR pixel 4-7
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    pand_m2r (mmx_bluemask, mm0);	// mm0 = b7b6b5b4b3______
    pxor_r2r (mm4, mm4);		// mm4 = 0

    pand_m2r (mmx_greenmask, mm2);	// mm2 = g7g6g5g4g3g2____
    psrlq_i2r (3, mm0);			// mm0 = ______b7b6b5b4b3

    movq_r2r (mm2, mm7);		// mm7 = g7g6g5g4g3g2____
    movq_r2r (mm0, mm5);		// mm5 = ______b7b6b5b4b3

    pand_m2r (mmx_redmask, mm1);	// mm1 = r7r6r5r4r3______
    punpcklbw_r2r (mm4, mm2);

    psrlq_i2r (1, mm1);
    punpcklbw_r2r (mm1, mm0);

    psllq_i2r (2, mm2);

    punpckhbw_r2r (mm4, mm7);
    por_r2r (mm2, mm0);

    psllq_i2r (2, mm7);

    movntq (mm0, *image);
    punpckhbw_r2r (mm1, mm5);

    por_r2r (mm7, mm5);

    // U
    // V

    movntq (mm5, *(image+8));
}

static inline void mmx_unpack_32rgb (uint8_t * image, int cpu)
{
    /*
     * convert RGB plane to RGB packed format,
     * mm0 -> B, mm1 -> R, mm2 -> G, mm3 -> 0,
     * mm4 -> GB, mm5 -> AR pixel 4-7,
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    pxor_r2r (mm3, mm3);
    movq_r2r (mm0, mm6);

    punpcklbw_r2r (mm2, mm6);
    movq_r2r (mm1, mm7);

    punpcklbw_r2r (mm3, mm7);
    movq_r2r (mm0, mm4);

    punpcklwd_r2r (mm7, mm6);
    movq_r2r (mm1, mm5);

    /* scheduling: this is hopeless */
    movntq (mm6, *image);
    movq_r2r (mm0, mm6);
    punpcklbw_r2r (mm2, mm6);
    punpckhwd_r2r (mm7, mm6);
    movntq (mm6, *(image+8));
    punpckhbw_r2r (mm2, mm4);
    punpckhbw_r2r (mm3, mm5);
    punpcklwd_r2r (mm5, mm4);
    movntq (mm4, *(image+16));
    movq_r2r (mm0, mm4);
    punpckhbw_r2r (mm2, mm4);
    punpckhwd_r2r (mm5, mm4);
    movntq (mm4, *(image+24));
}

static inline void mmx_unpack_32bgr (uint8_t * image, int cpu)
{
    /*
     * convert RGB plane to RGB packed format,
     * mm0 -> B, mm1 -> R, mm2 -> G, mm3 -> 0,
     * mm4 -> GB, mm5 -> AR pixel 4-7,
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    pxor_r2r (mm3, mm3);
    movq_r2r (mm1, mm6);

    punpcklbw_r2r (mm2, mm6);
    movq_r2r (mm0, mm7);

    punpcklbw_r2r (mm3, mm7);
    movq_r2r (mm1, mm4);

    punpcklwd_r2r (mm7, mm6);
    movq_r2r (mm0, mm5);

    /* scheduling: this is hopeless */
    movntq (mm6, *image);
    movq_r2r (mm1, mm6);
    punpcklbw_r2r (mm2, mm6);
    punpckhwd_r2r (mm7, mm6);
    movntq (mm6, *(image+8));
    punpckhbw_r2r (mm2, mm4);
    punpckhbw_r2r (mm3, mm5);
    punpcklwd_r2r (mm5, mm4);
    movntq (mm4, *(image+16));
    movq_r2r (mm1, mm4);
    punpckhbw_r2r (mm2, mm4);
    punpckhwd_r2r (mm5, mm4);
    movntq (mm4, *(image+24));
}

static inline void mmx_unpack_24rgb (uint8_t * image, int cpu)
{
    static mmx_t mmx_hirgb = {0x00ffffff00000000ULL};
    static mmx_t mmx_lorgb = {0x0000000000ffffffULL};

    /*
     * convert RGB plane to RGB packed format,
     * mm0 -> B, mm1 -> R, mm2 -> G, mm3 -> 0,
     * mm4 -> GB, mm5 -> AR pixel 4-7,
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    movq_r2r (mm1, mm6);
    punpcklbw_r2r (mm2, mm6);

    movq_r2r (mm0, mm7);
    punpcklbw_r2r (mm7, mm7);

    punpcklwd_r2r (mm7, mm6);
    movq_r2r (mm6, mm5);
    pand_m2r (mmx_hirgb, mm5);
    pand_m2r (mmx_lorgb, mm6);
    psrlq_i2r (8, mm5);
    por_r2r(mm6, mm5); /* mm5 = 0x0000B1G1R1B0G0R0 */

    movq_r2r (mm1, mm6);
    punpcklbw_r2r (mm2, mm6);
    punpckhwd_r2r (mm7, mm6); /* mm6 = 0x??B3G3R3??B2G2R2 */

    movq_r2r (mm6, mm4);
    psllq_i2r (48, mm4);
    por_r2r(mm4, mm5);  /* mm5 = 0xG2R2B1G1R1B0G0R0 */
    movntq (mm5, *image);

    movq_r2r (mm6, mm3);
    pand_m2r (mmx_hirgb, mm3);
    pand_m2r (mmx_lorgb, mm6);
    psrlq_i2r (8, mm3);
    por_r2r(mm6, mm3); /* mm3 = 0x0000B3G3R3B2G2R2 */
    psrlq_i2r (16, mm3); /* mm3 = 0x00000000B3G3R3B2 */

    movq_r2r (mm1, mm4);
    punpckhbw_r2r (mm2, mm4);
    movq_r2r (mm0, mm5);
    punpckhbw_r2r (mm3, mm5);
    punpcklwd_r2r (mm5, mm4);

    movq_r2r (mm4, mm6);
    pand_m2r (mmx_hirgb, mm6);
    pand_m2r (mmx_lorgb, mm4);
    psrlq_i2r (8, mm6);
    por_r2r(mm4, mm6); /* mm6 = 0x0000B5G5R5B4G4R4 */

    movq_r2r (mm6, mm4);
    psllq_i2r (32, mm4); /* mm4 = 0xR5B4G4R400000000 */
    por_r2r(mm4, mm3);   /* mm4 = 0xR5B4G4R4B3G3R3B2 */
    movntq (mm3, *(image+8));

    psrlq_i2r (32, mm6); /* mm6 = 0x000000000000B5G5 */

    movq_r2r (mm1, mm4);
    punpckhbw_r2r (mm2, mm4);
    punpckhwd_r2r (mm5, mm4);

    movq_r2r (mm4, mm3);
    pand_m2r (mmx_hirgb, mm3);
    pand_m2r (mmx_lorgb, mm4);
    psrlq_i2r (8, mm3);
    por_r2r (mm4, mm3);  /* mm3 = 0x0000B7G7R7B6G6R6 */
    psllq_i2r (16, mm3); /* mm3 = 0xB7G7R7B6G6R60000 */
    por_r2r (mm3, mm6);  /* mm6 = 0xB7G7R7B6G6R6B5G5 */

    movntq (mm6, *(image+16));
}

static inline void mmx_unpack_24bgr (uint8_t * image, int cpu)
{
    static mmx_t mmx_hirgb = {0x00ffffff00000000ULL};
    static mmx_t mmx_lorgb = {0x0000000000ffffffULL};

    /*
     * convert RGB plane to RGB packed format,
     * mm0 -> B, mm1 -> R, mm2 -> G, mm3 -> 0,
     * mm4 -> GB, mm5 -> AR pixel 4-7,
     * mm6 -> GB, mm7 -> AR pixel 0-3
     */

    movq_r2r (mm0, mm6);
    punpcklbw_r2r (mm2, mm6);

    movq_r2r (mm1, mm7);
    punpcklbw_r2r (mm7, mm7);

    punpcklwd_r2r (mm7, mm6);
    movq_r2r (mm6, mm5);
    pand_m2r (mmx_hirgb, mm5);
    pand_m2r (mmx_lorgb, mm6);
    psrlq_i2r (8, mm5);
    por_r2r(mm6, mm5); /* mm5 = 0x0000R1G1B1R0G0B0 */

    movq_r2r (mm0, mm6);
    punpcklbw_r2r (mm2, mm6);
    punpckhwd_r2r (mm7, mm6); /* mm6 = 0x??R3G3B3??R2G2B2 */

    movq_r2r (mm6, mm4);
    psllq_i2r (48, mm4);
    por_r2r(mm4, mm5);  /* mm5 = 0xG2B2R1G1B1R0G0B0 */
    movntq (mm5, *image);

    movq_r2r (mm6, mm3);
    pand_m2r (mmx_hirgb, mm3);
    pand_m2r (mmx_lorgb, mm6);
    psrlq_i2r (8, mm3);
    por_r2r(mm6, mm3); /* mm3 = 0x0000R3G3B3R2G2B2 */
    psrlq_i2r (16, mm3); /* mm3 = 0x00000000R3G3B3R2 */

    movq_r2r (mm0, mm4);
    punpckhbw_r2r (mm2, mm4);
    movq_r2r (mm1, mm5);
    punpckhbw_r2r (mm3, mm5);
    punpcklwd_r2r (mm5, mm4);

    movq_r2r (mm4, mm6);
    pand_m2r (mmx_hirgb, mm6);
    pand_m2r (mmx_lorgb, mm4);
    psrlq_i2r (8, mm6);
    por_r2r(mm4, mm6); /* mm6 = 0x0000R5G5B5R4G4B4 */

    movq_r2r (mm6, mm4);
    psllq_i2r (32, mm4); /* mm4 = 0xB5R4G4B400000000 */
    por_r2r(mm4, mm3);   /* mm4 = 0xB5R4G4B4R3G3B3R2 */
    movntq (mm3, *(image+8));

    psrlq_i2r (32, mm6); /* mm6 = 0x000000000000R5G5 */

    movq_r2r (mm0, mm4);
    punpckhbw_r2r (mm2, mm4);
    punpckhwd_r2r (mm5, mm4);

    movq_r2r (mm4, mm3);
    pand_m2r (mmx_hirgb, mm3);
    pand_m2r (mmx_lorgb, mm4);
    psrlq_i2r (8, mm3);
    por_r2r (mm4, mm3);  /* mm3 = 0x0000R7G7B7R6G6B6 */
    psllq_i2r (16, mm3); /* mm3 = 0xR7G7B7R6G6B60000 */
    por_r2r (mm3, mm6);  /* mm6 = 0xR7G7B7R6G6B6R5G5 */

    movntq (mm6, *(image+16));
}

static inline void yuv420_rgb16 (yuv2rgb_t *this,
				 uint8_t * image,
				 uint8_t * py, uint8_t * pu, uint8_t * pv,
				 int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {

	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_16rgb (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 16;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);

    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;

	i = this->dest_width >> 3; img = image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_16rgb (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 16;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*2);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;

            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);

            }
            height++;
        } while( dy>=32768);
      }
    }
}

static inline void yuv420_rgb15 (yuv2rgb_t *this,
				 uint8_t * image,
				 uint8_t * py, uint8_t * pu, uint8_t * pv,
				 int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {

	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_15rgb (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 16;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);

    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;

	i = this->dest_width >> 3; img = image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_15rgb (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 16;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*2);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;
            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);

            }
            height++;
        } while( dy>=32768 );
      }
    }
}

static inline void yuv420_rgb24 (yuv2rgb_t *this,
				 uint8_t * image, uint8_t * py,
				 uint8_t * pu, uint8_t * pv, int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    /* rgb_stride -= 4 * this->dest_width; */
    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {
	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_24rgb (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 24;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);
    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;


	i = this->dest_width >> 3; img=image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_24rgb (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 24;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*3);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;
            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);
            }
            height++;
        } while( dy>=32768 );

      }

    }
}

static inline void yuv420_bgr24 (yuv2rgb_t *this,
				 uint8_t * image, uint8_t * py,
				 uint8_t * pu, uint8_t * pv, int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    /* rgb_stride -= 4 * this->dest_width; */
    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {
	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_24bgr (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 24;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);
    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;


	i = this->dest_width >> 3; img=image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_24bgr (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 24;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*3);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;
            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);
            }
            height++;
        } while( dy>=32768 );

      }

    }
}

static inline void yuv420_argb32 (yuv2rgb_t *this,
				  uint8_t * image, uint8_t * py,
				  uint8_t * pu, uint8_t * pv, int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    /* rgb_stride -= 4 * this->dest_width; */
    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {
	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_32rgb (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 32;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);
    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;


	i = this->dest_width >> 3; img=image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_32rgb (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 32;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*4);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;
            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);
            }
            height++;
        } while( dy>=32768 );
      }

    }
}

static inline void yuv420_abgr32 (yuv2rgb_t *this,
				  uint8_t * image, uint8_t * py,
				  uint8_t * pu, uint8_t * pv, int cpu)
{
    int i, height, dst_height;
    int rgb_stride = this->rgb_stride;
    int y_stride   = this->y_stride;
    int uv_stride  = this->uv_stride;
    int width      = this->source_width;
    uint8_t *img;

    /* rgb_stride -= 4 * this->dest_width; */
    width >>= 3;

    if (!this->do_scale) {
      height = this->next_slice (this, &image);
      y_stride -= 8 * width;
      uv_stride -= 4 * width;

      do {
	i = width; img = image;
	do {
	  mmx_yuv2rgb (py, pu, pv, this->table_mmx);
	  mmx_unpack_32bgr (img, cpu);
	  py += 8;
	  pu += 4;
	  pv += 4;
	  img += 32;
	} while (--i);

	py += y_stride;
	image += rgb_stride;
	if (height & 1) {
	  pu += uv_stride;
	  pv += uv_stride;
	} else {
	  pu -= 4 * width;
	  pv -= 4 * width;
	}
      } while (--height);
    } else {

      scale_line_func_t scale_line = this->scale_line;
      uint8_t *y_buf, *u_buf, *v_buf;
      int      dy = 0;

      scale_line (pu, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (pv, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
      scale_line (py, this->y_buffer,
		  this->dest_width, this->step_dx);

      dst_height = this->next_slice (this, &image);

      for (height = 0;; ) {

	y_buf = this->y_buffer;
	u_buf = this->u_buffer;
	v_buf = this->v_buffer;


	i = this->dest_width >> 3; img=image;
	do {
	  /* printf ("i : %d\n",i); */

	  mmx_yuv2rgb (y_buf, u_buf, v_buf, this->table_mmx);
	  mmx_unpack_32bgr (img, cpu);
	  y_buf += 8;
	  u_buf += 4;
	  v_buf += 4;
	  img += 32;
	} while (--i);

	dy += this->step_dy;
	image += rgb_stride;

	while (--dst_height > 0 && dy < 32768) {

	  xine_fast_memcpy (image, image-rgb_stride, this->dest_width*4);

	  dy += this->step_dy;
	  image += rgb_stride;
	}

	if (dst_height <= 0)
	  break;

        do {
            dy -= 32768;
            py += y_stride;

            scale_line (py, this->y_buffer,
                        this->dest_width, this->step_dx);

            if (height & 1) {
                pu += uv_stride;
                pv += uv_stride;

                scale_line (pu, this->u_buffer,
                            this->dest_width >> 1, this->step_dx);
                scale_line (pv, this->v_buffer,
                            this->dest_width >> 1, this->step_dx);
            }
            height++;
        } while( dy>=32768 );

      }

    }
}

static void mmxext_rgb15 (yuv2rgb_t *this, uint8_t * image,
			  uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb15 (this, image, py, pu, pv, CPU_MMXEXT);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmxext_rgb16 (yuv2rgb_t *this, uint8_t * image,
			  uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb16 (this, image, py, pu, pv, CPU_MMXEXT);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmxext_rgb24 (yuv2rgb_t *this, uint8_t * image,
			   uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb24 (this, image, py, pu, pv, CPU_MMXEXT);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmxext_argb32 (yuv2rgb_t *this, uint8_t * image,
			   uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_argb32 (this, image, py, pu, pv, CPU_MMXEXT);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmxext_abgr32 (yuv2rgb_t *this, uint8_t * image,
			   uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_abgr32 (this, image, py, pu, pv, CPU_MMXEXT);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_rgb15 (yuv2rgb_t *this, uint8_t * image,
		       uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb15 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_rgb16 (yuv2rgb_t *this, uint8_t * image,
		       uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb16 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_rgb24 (yuv2rgb_t *this, uint8_t * image,
		       uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_rgb24 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_bgr24 (yuv2rgb_t *this, uint8_t * image,
		       uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_bgr24 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_argb32 (yuv2rgb_t *this, uint8_t * image,
			uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_argb32 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

static void mmx_abgr32 (yuv2rgb_t *this, uint8_t * image,
			uint8_t * py, uint8_t * pu, uint8_t * pv)
{
    yuv420_abgr32 (this, image, py, pu, pv, CPU_MMX);
    emms();	/* re-initialize x86 FPU after MMX use */
}

void yuv2rgb_init_mmxext (yuv2rgb_factory_t *this) {

  if (this->swapped)
    return; /*no swapped pixel output upto now*/

  switch (this->mode) {
  case MODE_15_RGB:
    this->yuv2rgb_fun = mmxext_rgb15;
    break;
  case MODE_16_RGB:
    this->yuv2rgb_fun = mmxext_rgb16;
    break;
  case MODE_24_RGB:
    this->yuv2rgb_fun = mmxext_rgb24;
    break;
  case MODE_32_RGB:
    this->yuv2rgb_fun = mmxext_argb32;
    break;
  case MODE_32_BGR:
    this->yuv2rgb_fun = mmxext_abgr32;
    break;
  }
}

void yuv2rgb_init_mmx (yuv2rgb_factory_t *this) {

  if (this->swapped) switch (this->mode) {
  case MODE_24_RGB:
    this->yuv2rgb_fun = mmx_bgr24;
    return;
  case MODE_24_BGR:
    this->yuv2rgb_fun = mmx_rgb24;
    return;
  default:
    return; /* other swapped formats yet unsupported */
  }

  switch (this->mode) {
  case MODE_15_RGB:
    this->yuv2rgb_fun = mmx_rgb15;
    break;
  case MODE_16_RGB:
    this->yuv2rgb_fun = mmx_rgb16;
    break;
  case MODE_24_RGB:
    this->yuv2rgb_fun = mmx_rgb24;
    break;
  case MODE_24_BGR:
    this->yuv2rgb_fun = mmx_bgr24;
    break;
  case MODE_32_RGB:
    this->yuv2rgb_fun = mmx_argb32;
    break;
  case MODE_32_BGR:
    this->yuv2rgb_fun = mmx_abgr32;
    break;
  }
}


#endif
