/**
 * Linear blend deinterlacing plugin.  The idea for this algorithm came
 * from the linear blend deinterlacer which originated in the mplayer
 * sources.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include <xine/attributes.h>
#include <xine/xineutils.h>
#include "xine_mmx.h"
#include "speedtools.h"
#include "speedy.h"
#include "deinterlace.h"
#include "plugins.h"

static const char linearblendmethod_help[] =
  "Avoids flicker by blurring consecutive frames of input.  Use this if "
  "you want to run your monitor at an arbitrary refresh rate and not use "
  "much CPU, and are willing to sacrifice detail.\n"
  "\n"
  "Temporal mode evenly blurs content for least flicker, but with visible "
  "trails on fast motion. From the linear blend deinterlacer in mplayer.";

static void deinterlace_scanline_linear_blend( uint8_t *output,
                                               deinterlace_scanline_data_t *data,
                                               int width )
{
    uint8_t *t0 = data->t0;
    uint8_t *b0 = data->b0;
    uint8_t *m1 = data->m1;
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    int i;

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    pxor_r2r( mm7, mm7 );
    while( i-- ) {
        movd_m2r( *t0, mm0 );
        movd_m2r( *b0, mm1 );
        movd_m2r( *m1, mm2 );

        movd_m2r( *(t0+4), mm3 );
        movd_m2r( *(b0+4), mm4 );
        movd_m2r( *(m1+4), mm5 );

        punpcklbw_r2r( mm7, mm0 );
        punpcklbw_r2r( mm7, mm1 );
        punpcklbw_r2r( mm7, mm2 );

        punpcklbw_r2r( mm7, mm3 );
        punpcklbw_r2r( mm7, mm4 );
        punpcklbw_r2r( mm7, mm5 );

        psllw_i2r( 1, mm2 );
        psllw_i2r( 1, mm5 );
        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm3, mm5 );
        paddw_r2r( mm1, mm2 );
        paddw_r2r( mm4, mm5 );
        psrlw_i2r( 2, mm2 );
        psrlw_i2r( 2, mm5 );
        packuswb_r2r( mm2, mm2 );
        packuswb_r2r( mm5, mm5 );

        movd_r2m( mm2, *output );
        movd_r2m( mm5, *(output+4) );

        output += 8;
        t0 += 8;
        b0 += 8;
        m1 += 8;
    }
    while( width-- ) {
        *output++ = (*t0++ + *b0++ + (*m1++ << 1)) >> 2;
    }
    emms();
#else
    width *= 2;
    while( width-- ) {
        *output++ = (*t0++ + *b0++ + (*m1++ << 1)) >> 2;
    }
#endif
}

static void deinterlace_scanline_linear_blend2( uint8_t *output,
                                                deinterlace_scanline_data_t *data,
                                                int width )
{
    uint8_t *m0 = data->m0;
    uint8_t *t1 = data->t1;
    uint8_t *b1 = data->b1;

#if defined(ARCH_X86) || defined(ARCH_X86_64)
    int i;

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    pxor_r2r( mm7, mm7 );
    while( i-- ) {
        movd_m2r( *t1, mm0 );
        movd_m2r( *b1, mm1 );
        movd_m2r( *m0, mm2 );

        movd_m2r( *(t1+4), mm3 );
        movd_m2r( *(b1+4), mm4 );
        movd_m2r( *(m0+4), mm5 );

        punpcklbw_r2r( mm7, mm0 );
        punpcklbw_r2r( mm7, mm1 );
        punpcklbw_r2r( mm7, mm2 );

        punpcklbw_r2r( mm7, mm3 );
        punpcklbw_r2r( mm7, mm4 );
        punpcklbw_r2r( mm7, mm5 );

        psllw_i2r( 1, mm2 );
        psllw_i2r( 1, mm5 );
        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm3, mm5 );
        paddw_r2r( mm1, mm2 );
        paddw_r2r( mm4, mm5 );
        psrlw_i2r( 2, mm2 );
        psrlw_i2r( 2, mm5 );
        packuswb_r2r( mm2, mm2 );
        packuswb_r2r( mm5, mm5 );

        movd_r2m( mm2, *output );
        movd_r2m( mm5, *(output+4) );

        output += 8;
        t1 += 8;
        b1 += 8;
        m0 += 8;
    }
    while( width-- ) {
        *output++ = (*t1++ + *b1++ + (*m0++ << 1)) >> 2;
    }
    emms();
#else
    width *= 2;
    while( width-- ) {
        *output++ = (*t1++ + *b1++ + (*m0++ << 1)) >> 2;
    }
#endif
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)

/* MMXEXT version is about 15% faster with Athlon XP [MF] */

static void deinterlace_scanline_linear_blend_mmxext( uint8_t *output,
                                               deinterlace_scanline_data_t *data,
                                               int width )
{
    uint8_t *t0 = data->t0;
    uint8_t *b0 = data->b0;
    uint8_t *m1 = data->m1;
    int i;
    static mmx_t high_mask = {ub:{0xff,0xff,0xff,0xff,0,0,0,0}};

    READ_PREFETCH_2048( t0 );
    READ_PREFETCH_2048( b0 );
    READ_PREFETCH_2048( m1 );

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    movd_m2r( high_mask, mm6 );
    pxor_r2r( mm7, mm7 );
    while( i-- ) {
        movd_m2r( *t0, mm0 );
        movd_m2r( *b0, mm1 );
        movd_m2r( *m1, mm2 );

        movd_m2r( *(t0+4), mm3 );
        movd_m2r( *(b0+4), mm4 );
        movd_m2r( *(m1+4), mm5 );

        punpcklbw_r2r( mm7, mm0 );
        punpcklbw_r2r( mm7, mm1 );
        punpcklbw_r2r( mm7, mm2 );

        punpcklbw_r2r( mm7, mm3 );
        punpcklbw_r2r( mm7, mm4 );
        punpcklbw_r2r( mm7, mm5 );

        psllw_i2r( 1, mm2 );
        psllw_i2r( 1, mm5 );
        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm3, mm5 );
        paddw_r2r( mm1, mm2 );
        paddw_r2r( mm4, mm5 );
        psrlw_i2r( 2, mm2 );
        psrlw_i2r( 2, mm5 );
        packuswb_r2r( mm2, mm2 );
        packuswb_r2r( mm5, mm5 );

        psllq_i2r( 32, mm5 );
        pand_r2r( mm6, mm2 );
        por_r2r( mm2, mm5 );
        movntq_r2m( mm5, *output );

        output += 8;
        t0 += 8;
        b0 += 8;
        m1 += 8;
    }
    while( width-- ) {
        *output++ = (*t0++ + *b0++ + (*m1++ << 1)) >> 2;
    }
    sfence();
    emms();
}

static void deinterlace_scanline_linear_blend2_mmxext( uint8_t *output,
                                                deinterlace_scanline_data_t *data,
                                                int width )
{
    uint8_t *m0 = data->m0;
    uint8_t *t1 = data->t1;
    uint8_t *b1 = data->b1;

    int i;

    READ_PREFETCH_2048( t1 );
    READ_PREFETCH_2048( b1 );
    READ_PREFETCH_2048( m0 );

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    pxor_r2r( mm7, mm7 );
    while( i-- ) {
        movd_m2r( *t1, mm0 );
        movd_m2r( *b1, mm1 );
        movd_m2r( *m0, mm2 );

        movd_m2r( *(t1+4), mm3 );
        movd_m2r( *(b1+4), mm4 );
        movd_m2r( *(m0+4), mm5 );

        punpcklbw_r2r( mm7, mm0 );
        punpcklbw_r2r( mm7, mm1 );
        punpcklbw_r2r( mm7, mm2 );

        punpcklbw_r2r( mm7, mm3 );
        punpcklbw_r2r( mm7, mm4 );
        punpcklbw_r2r( mm7, mm5 );

        psllw_i2r( 1, mm2 );
        psllw_i2r( 1, mm5 );
        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm3, mm5 );
        paddw_r2r( mm1, mm2 );
        paddw_r2r( mm4, mm5 );
        psrlw_i2r( 2, mm2 );
        psrlw_i2r( 2, mm5 );
        packuswb_r2r( mm2, mm2 );
        packuswb_r2r( mm5, mm5 );

        psllq_i2r( 32, mm5 );
        pand_r2r( mm6, mm2 );
        por_r2r( mm2, mm5 );
        movntq_r2m( mm5, *output );

        output += 8;
        t1 += 8;
        b1 += 8;
        m0 += 8;
    }
    while( width-- ) {
        *output++ = (*t1++ + *b1++ + (*m0++ << 1)) >> 2;
    }
    sfence();
    emms();
}

static deinterlace_method_t linearblendmethod_mmxext =
{
    "Linear Blend (mplayer)",
    "LinearBlend",
    2,
    MM_ACCEL_X86_MMXEXT,
    0,
    1,
    deinterlace_scanline_linear_blend_mmxext,
    deinterlace_scanline_linear_blend2_mmxext,
    0,
    0,
    linearblendmethod_help
};

#endif

static deinterlace_method_t linearblendmethod =
{
    "Linear Blend (mplayer)",
    "LinearBlend",
/*
    "Blur: Temporal",
    "BlurTemporal",
*/
    2,
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    MM_ACCEL_X86_MMX,
#else
    0,
#endif
    0,
    1,
    deinterlace_scanline_linear_blend,
    deinterlace_scanline_linear_blend2,
    0,
    0,
    linearblendmethod_help
};

deinterlace_method_t *linearblend_get_method( void )
{
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    if( xine_mm_accel() & MM_ACCEL_X86_MMXEXT )
      return &linearblendmethod_mmxext;
    else
#endif
      return &linearblendmethod;
}

