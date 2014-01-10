/**
 * Copyright (c) 2002, 2003 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 */

/**
 * Includes 420to422, 422to444 scaling filters from the MPEG2 reference
 * implementation.  The v12 source code indicates that they were written
 * by Cheung Auyeung <auyeung@mot.com>.  The file they were in was:
 *
 * store.c, picture output routines
 * Copyright (C) 1996, MPEG Software Simulation Group. All Rights Reserved.
 *
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
 * any and all warranties, whether express, implied, or statuary, including any
 * implied warranties or merchantability or of fitness for a particular
 * purpose.  In no event shall the copyright-holder be liable for any
 * incidental, punitive, or consequential damages of any kind whatsoever
 * arising from the use of these programs.
 *
 * This disclaimer of warranty extends to the user of these programs and user's
 * customers, employees, agents, transferees, successors, and assigns.
 *
 * The MPEG Software Simulation Group does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any third-party
 * patents.
 *
 * Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
 * are subject to royalty fees to patent holders.  Many of these patents are
 * general enough such that they are unavoidable regardless of implementation
 * design.
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

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

/* Function pointer definitions. */
void (*interpolate_packed422_scanline)( uint8_t *output, uint8_t *top,
                                        uint8_t *bot, int width );
void (*blit_colour_packed422_scanline)( uint8_t *output,
                                        int width, int y, int cb, int cr );
void (*blit_colour_packed4444_scanline)( uint8_t *output,
                                         int width, int alpha, int luma,
                                         int cb, int cr );
void (*blit_packed422_scanline)( uint8_t *dest, const uint8_t *src, int width );
void (*composite_packed4444_to_packed422_scanline)( uint8_t *output, uint8_t *input,
                                                    uint8_t *foreground, int width );
void (*composite_packed4444_alpha_to_packed422_scanline)( uint8_t *output,
                                                          uint8_t *input,
                                                          uint8_t *foreground,
                                                          int width, int alpha );
void (*composite_alphamask_to_packed4444_scanline)( uint8_t *output,
                                                uint8_t *input,
                                                uint8_t *mask, int width,
                                                int textluma, int textcb,
                                                int textcr );
void (*composite_alphamask_alpha_to_packed4444_scanline)( uint8_t *output,
                                                       uint8_t *input,
                                                       uint8_t *mask, int width,
                                                       int textluma, int textcb,
                                                       int textcr, int alpha );
void (*premultiply_packed4444_scanline)( uint8_t *output, uint8_t *input, int width );
void (*blend_packed422_scanline)( uint8_t *output, uint8_t *src1,
                                  uint8_t *src2, int width, int pos );
void (*filter_luma_121_packed422_inplace_scanline)( uint8_t *data, int width );
void (*filter_luma_14641_packed422_inplace_scanline)( uint8_t *data, int width );
unsigned int (*diff_factor_packed422_scanline)( uint8_t *cur, uint8_t *old, int width );
unsigned int (*comb_factor_packed422_scanline)( uint8_t *top, uint8_t *mid,
                                                uint8_t *bot, int width );
void (*kill_chroma_packed422_inplace_scanline)( uint8_t *data, int width );
void (*mirror_packed422_inplace_scanline)( uint8_t *data, int width );
void (*halfmirror_packed422_inplace_scanline)( uint8_t *data, int width );
void *(*speedy_memcpy)( void *output, const void *input, size_t size );
void (*diff_packed422_block8x8)( pulldown_metrics_t *m, uint8_t *old,
                                 uint8_t *new, int os, int ns );
void (*a8_subpix_blit_scanline)( uint8_t *output, uint8_t *input,
                                 int lasta, int startpos, int width );
void (*quarter_blit_vertical_packed422_scanline)( uint8_t *output, uint8_t *one,
                                                  uint8_t *three, int width );
void (*subpix_blit_vertical_packed422_scanline)( uint8_t *output, uint8_t *top,
                                                 uint8_t *bot, int subpixpos, int width );
void (*composite_bars_packed4444_scanline)( uint8_t *output,
                                            uint8_t *background, int width,
                                            int a, int luma, int cb, int cr,
                                            int percentage );
void (*packed444_to_nonpremultiplied_packed4444_scanline)( uint8_t *output,
                                                           uint8_t *input,
                                                           int width, int alpha );
void (*aspect_adjust_packed4444_scanline)( uint8_t *output,
                                           uint8_t *input,
                                           int width,
                                           double pixel_aspect );
void (*packed444_to_packed422_scanline)( uint8_t *output,
                                         uint8_t *input,
                                         int width );
void (*packed422_to_packed444_scanline)( uint8_t *output,
                                         uint8_t *input,
                                         int width );
void (*packed422_to_packed444_rec601_scanline)( uint8_t *dest,
                                                uint8_t *src,
                                                int width );
void (*packed444_to_rgb24_rec601_scanline)( uint8_t *output,
                                            uint8_t *input,
                                            int width );
void (*rgb24_to_packed444_rec601_scanline)( uint8_t *output,
                                            uint8_t *input,
                                            int width );
void (*rgba32_to_packed4444_rec601_scanline)( uint8_t *output,
                                              uint8_t *input,
                                              int width );
void (*chroma_422_to_444_mpeg2_plane)( uint8_t *dst, uint8_t *src,
                                       int width, int height );
void (*chroma_420_to_422_mpeg2_plane)( uint8_t *dst, uint8_t *src,
                                       int width, int height, int progressive );
void (*invert_colour_packed422_inplace_scanline)( uint8_t *data, int width );
void (*vfilter_chroma_121_packed422_scanline)( uint8_t *output, int width,
                                               uint8_t *m, uint8_t *t, uint8_t *b );
void (*vfilter_chroma_332_packed422_scanline)( uint8_t *output, int width,
                                               uint8_t *m, uint8_t *t, uint8_t *b );


/**
 * result = (1 - alpha)B + alpha*F
 *        =  B - alpha*B + alpha*F
 *        =  B + alpha*(F - B)
 */

static inline __attribute__ ((always_inline,const)) int multiply_alpha( int a, int r )
{
    int temp;
    temp = (r * a) + 0x80;
    return ((temp + (temp >> 8)) >> 8);
}

static inline __attribute__ ((always_inline,const)) uint8_t clip255( int x )
{
    if( x > 255 ) {
        return 255;
    } else if( x < 0 ) {
        return 0;
    } else {
        return x;
    }
}

static unsigned long CombJaggieThreshold = 73;

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static unsigned int comb_factor_packed422_scanline_mmx( uint8_t *top, uint8_t *mid,
                                                        uint8_t *bot, int width )
{
    const mmx_t qwYMask = { 0x00ff00ff00ff00ffULL };
    const mmx_t qwOnes = { 0x0001000100010001ULL };
    mmx_t qwThreshold;
    unsigned int temp1, temp2;

    width /= 4;

    qwThreshold.uw[ 0 ] = CombJaggieThreshold;
    qwThreshold.uw[ 1 ] = CombJaggieThreshold;
    qwThreshold.uw[ 2 ] = CombJaggieThreshold;
    qwThreshold.uw[ 3 ] = CombJaggieThreshold;

    movq_m2r( qwThreshold, mm0 );
    movq_m2r( qwYMask, mm1 );
    movq_m2r( qwOnes, mm2 );
    pxor_r2r( mm7, mm7 );         /* mm7 = 0. */

    while( width-- ) {
        /* Load and keep just the luma. */
        movq_m2r( *top, mm3 );
        movq_m2r( *mid, mm4 );
        movq_m2r( *bot, mm5 );

        pand_r2r( mm1, mm3 );
        pand_r2r( mm1, mm4 );
        pand_r2r( mm1, mm5 );

        /* Work out mm6 = (top - mid) * (bot - mid) - ( (top - mid)^2 >> 7 ) */
        psrlw_i2r( 1, mm3 );
        psrlw_i2r( 1, mm4 );
        psrlw_i2r( 1, mm5 );

        /* mm6 = (top - mid) */
        movq_r2r( mm3, mm6 );
        psubw_r2r( mm4, mm6 );

        /* mm3 = (top - bot) */
        psubw_r2r( mm5, mm3 );

        /* mm5 = (bot - mid) */
        psubw_r2r( mm4, mm5 );

        /* mm6 = (top - mid) * (bot - mid) */
        pmullw_r2r( mm5, mm6 );

        /* mm3 = (top - bot)^2 >> 7 */
        pmullw_r2r( mm3, mm3 );   /* mm3 = (top - bot)^2 */
        psrlw_i2r( 7, mm3 );      /* mm3 = ((top - bot)^2 >> 7) */

        /* mm6 is what we want. */
        psubw_r2r( mm3, mm6 );

        /* FF's if greater than qwTheshold */
        pcmpgtw_r2r( mm0, mm6 );

        /* Add to count if we are greater than threshold */
        pand_r2r( mm2, mm6 );
        paddw_r2r( mm6, mm7 );

        top += 8;
        mid += 8;
        bot += 8;
    }

    movd_r2m( mm7, temp1 );
    psrlq_i2r( 32, mm7 );
    movd_r2m( mm7, temp2 );
    temp1 += temp2;
    temp2 = temp1;
    temp1 >>= 16;
    temp1 += temp2 & 0xffff;

    emms();

    return temp1;
}
#endif

static unsigned long BitShift = 6;

static unsigned int diff_factor_packed422_scanline_c( uint8_t *cur, uint8_t *old, int width )
{
    unsigned int ret = 0;

    width /= 4;

    while( width-- ) {
        unsigned int tmp1 = (cur[ 0 ] + cur[ 2 ] + cur[ 4 ] + cur[ 6 ] + 2)>>2;
        unsigned int tmp2 = (old[ 0 ] + old[ 2 ] + old[ 4 ] + old[ 6 ] + 2)>>2;
        tmp1  = (tmp1 - tmp2);
        tmp1 *= tmp1;
        tmp1 >>= BitShift;
        ret += tmp1;
        cur += 8;
        old += 8;
    }

    return ret;
}

/*
static unsigned int diff_factor_packed422_scanline_test_c( uint8_t *cur, uint8_t *old, int width )
{
    unsigned int ret = 0;

    width /= 16;

    while( width-- ) {
        unsigned int tmp1 = (cur[ 0 ] + cur[ 2 ] + cur[ 4 ] + cur[ 6 ])>>2;
        unsigned int tmp2 = (old[ 0 ] + old[ 2 ] + old[ 4 ] + old[ 6 ])>>2;
        tmp1  = (tmp1 - tmp2);
        tmp1 *= tmp1;
        tmp1 >>= BitShift;
        ret += tmp1;
        cur += (8*4);
        old += (8*4);
    }

    return ret;
}
*/

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static unsigned int diff_factor_packed422_scanline_mmx( uint8_t *cur, uint8_t *old, int width )
{
    const mmx_t qwYMask = { 0x00ff00ff00ff00ffULL };
    unsigned int temp1, temp2;

    width /= 4;

    movq_m2r( qwYMask, mm1 );
    movd_m2r( BitShift, mm7 );
    pxor_r2r( mm0, mm0 );

    while( width-- ) {
        movq_m2r( *cur, mm4 );
        movq_m2r( *old, mm5 );

        pand_r2r( mm1, mm4 );
        pand_r2r( mm1, mm5 );

        psubw_r2r( mm5, mm4 );   /* mm4 = Y1 - Y2            */
        pmaddwd_r2r( mm4, mm4 ); /* mm4 = (Y1 - Y2)^2        */
        psrld_r2r( mm7, mm4 );   /* divide mm4 by 2^BitShift */
        paddd_r2r( mm4, mm0 );   /* keep total in mm0        */

        cur += 8;
        old += 8;
    }

    movd_r2m( mm0, temp1 );
    psrlq_i2r( 32, mm0 );
    movd_r2m( mm0, temp2 );
    temp1 += temp2;

    emms();

    return temp1;
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)

static const sse_t dqwYMask = { uq: { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL }};
static const sse_t dqwCMask = { uq: { 0xff00ff00ff00ff00ULL, 0xff00ff00ff00ff00ULL }};

static unsigned int diff_factor_packed422_scanline_sse2_aligned( uint8_t *cur, uint8_t *old, int width )
{
    register unsigned int temp;

    width /= 8;

    movdqa_m2r( dqwYMask, xmm1 );
    movd_m2r( BitShift, xmm7 );
    pxor_r2r( xmm0, xmm0 );

    while( width-- ) {
        movdqa_m2r( *cur, xmm4 );
        movdqa_m2r( *old, xmm5 );

        pand_r2r( xmm1, xmm4 );
        pand_r2r( xmm1, xmm5 );

        psubw_r2r( xmm5, xmm4 );   /* mm4 = Y1 - Y2            */
        pmaddwd_r2r( xmm4, xmm4 ); /* mm4 = (Y1 - Y2)^2        */
        psrld_r2r( xmm7, xmm4 );   /* divide mm4 by 2^BitShift */
        paddd_r2r( xmm4, xmm0 );   /* keep total in mm0        */

        cur += 16;
        old += 16;
    }

    pshufd_r2r(xmm0, xmm1, 0x0e);
    paddd_r2r(xmm1, xmm0);
    pshufd_r2r(xmm0, xmm1, 0x01);
    paddd_r2r(xmm1, xmm0);

    movd_r2a(xmm0, temp);
    return temp;
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static unsigned int diff_factor_packed422_scanline_sse2( uint8_t *cur, uint8_t *old, int width )
{
    if (0 == (((intptr_t)cur | (intptr_t)old) & 15)) {
        return diff_factor_packed422_scanline_sse2_aligned(cur, old, width);
    }

    register unsigned int temp;

    width /= 8;

    movdqa_m2r( dqwYMask, xmm1 );
    movd_m2r( BitShift, xmm7 );
    pxor_r2r( xmm0, xmm0 );

    while( width-- ) {
        movdqu_m2r( *cur, xmm4 );
        movdqu_m2r( *old, xmm5 );

        pand_r2r( xmm1, xmm4 );
        pand_r2r( xmm1, xmm5 );

        psubw_r2r( xmm5, xmm4 );   /* mm4 = Y1 - Y2            */
        pmaddwd_r2r( xmm4, xmm4 ); /* mm4 = (Y1 - Y2)^2        */
        psrld_r2r( xmm7, xmm4 );   /* divide mm4 by 2^BitShift */
        paddd_r2r( xmm4, xmm0 );   /* keep total in mm0        */

        cur += 16;
        old += 16;
    }

    pshufd_r2r(xmm0, xmm1, 0x0e);
    paddd_r2r(xmm1, xmm0);
    pshufd_r2r(xmm0, xmm1, 0x01);
    paddd_r2r(xmm1, xmm0);

    movd_r2a(xmm0, temp);

    return temp;
}
#endif

#define ABS(a) (((a) < 0)?-(a):(a))

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void diff_packed422_block8x8_mmx( pulldown_metrics_t *m, uint8_t *old,
                                         uint8_t *new, int os, int ns )
{
    const mmx_t ymask = { 0x00ff00ff00ff00ffULL };
    short out[ 24 ]; /* Output buffer for the partial metrics from the mmx code. */
    uint8_t *outdata = (uint8_t *) out;
    uint8_t *oldp, *newp;
    int i;

    pxor_r2r( mm4, mm4 );  // 4 even difference sums.
    pxor_r2r( mm5, mm5 );  // 4 odd difference sums.
    pxor_r2r( mm7, mm7 );  // zeros

    oldp = old; newp = new;
    for( i = 4; i; --i ) {
        // Even difference.
        movq_m2r( oldp[0], mm0 );
        movq_m2r( oldp[8], mm2 );
        pand_m2r( ymask, mm0 );
        pand_m2r( ymask, mm2 );
        oldp += os;

        movq_m2r( newp[0], mm1 );
        movq_m2r( newp[8], mm3 );
        pand_m2r( ymask, mm1 );
        pand_m2r( ymask, mm3 );
        newp += ns;

        movq_r2r( mm0, mm6 );
        psubusb_r2r( mm1, mm0 );
        psubusb_r2r( mm6, mm1 );
        movq_r2r( mm2, mm6 );
        psubusb_r2r( mm3, mm2 );
        psubusb_r2r( mm6, mm3 );

        paddw_r2r( mm0, mm4 );
        paddw_r2r( mm1, mm4 );
        paddw_r2r( mm2, mm4 );
        paddw_r2r( mm3, mm4 );

        // Odd difference.
        movq_m2r( oldp[0], mm0 );
        movq_m2r( oldp[8], mm2 );
        pand_m2r( ymask, mm0 );
        pand_m2r( ymask, mm2 );
        oldp += os;

        movq_m2r( newp[0], mm1 );
        movq_m2r( newp[8], mm3 );
        pand_m2r( ymask, mm1 );
        pand_m2r( ymask, mm3 );
        newp += ns;

        movq_r2r( mm0, mm6 );
        psubusb_r2r( mm1, mm0 );
        psubusb_r2r( mm6, mm1 );
        movq_r2r( mm2, mm6 );
        psubusb_r2r( mm3, mm2 );
        psubusb_r2r( mm6, mm3 );

        paddw_r2r( mm0, mm5 );
        paddw_r2r( mm1, mm5 );
        paddw_r2r( mm2, mm5 );
        paddw_r2r( mm3, mm5 );
    }
    movq_r2m( mm4, outdata[0] );
    movq_r2m( mm5, outdata[8] );

    m->e = out[0] + out[1] + out[2] + out[3];
    m->o = out[4] + out[5] + out[6] + out[7];
    m->d = m->e + m->o;

    pxor_r2r( mm4, mm4 );  // Past spacial noise.
    pxor_r2r( mm5, mm5 );  // Temporal noise.
    pxor_r2r( mm6, mm6 );  // Current spacial noise.

    // First loop to measure first four columns
    oldp = old; newp = new;
    for( i = 4; i; --i ) {
        movq_m2r( oldp[0], mm0 );
        movq_m2r( oldp[os], mm1 );
        pand_m2r( ymask, mm0 );
        pand_m2r( ymask, mm1 );
        oldp += (os*2);

        movq_m2r( newp[0], mm2 );
        movq_m2r( newp[ns], mm3 );
        pand_m2r( ymask, mm2 );
        pand_m2r( ymask, mm3 );
        newp += (ns*2);

        paddw_r2r( mm1, mm4 );
        paddw_r2r( mm1, mm5 );
        paddw_r2r( mm3, mm6 );
        psubw_r2r( mm0, mm4 );
        psubw_r2r( mm2, mm5 );
        psubw_r2r( mm2, mm6 );
    }
    movq_r2m( mm4, outdata[0] );
    movq_r2m( mm5, outdata[16] );
    movq_r2m( mm6, outdata[32] );

    pxor_r2r( mm4, mm4 );
    pxor_r2r( mm5, mm5 );
    pxor_r2r( mm6, mm6 );

    // Second loop for the last four columns
    oldp = old; newp = new;
    for( i = 4; i; --i ) {
        movq_m2r( oldp[8], mm0 );
        movq_m2r( oldp[os+8], mm1 );
        pand_m2r( ymask, mm0 );
        pand_m2r( ymask, mm1 );
        oldp += (os*2);

        movq_m2r( newp[8], mm2 );
        movq_m2r( newp[ns+8], mm3 );
        pand_m2r( ymask, mm2 );
        pand_m2r( ymask, mm3 );
        newp += (ns*2);

        paddw_r2r( mm1, mm4 );
        paddw_r2r( mm1, mm5 );
        paddw_r2r( mm3, mm6 );
        psubw_r2r( mm0, mm4 );
        psubw_r2r( mm2, mm5 );
        psubw_r2r( mm2, mm6 );
    }
    movq_r2m( mm4, outdata[8] );
    movq_r2m( mm5, outdata[24] );
    movq_r2m( mm6, outdata[40] );

    m->p = m->t = m->s = 0;
    for (i=0; i<8; i++) {
        // FIXME: move abs() into the mmx code!
        m->p += ABS(out[i]);
        m->t += ABS(out[8+i]);
        m->s += ABS(out[16+i]);
    }

    emms();
}
#endif

static void diff_packed422_block8x8_c( pulldown_metrics_t *m, uint8_t *old,
                                       uint8_t *new, int os, int ns )
{
    int x, y, e=0, o=0, s=0, p=0, t=0;
    uint8_t *oldp, *newp;

    m->s = m->p = m->t = 0;
    for (x = 8; x; x--) {
        oldp = old; old += 2;
        newp = new; new += 2;
        s = p = t = 0;
        for (y = 4; y; y--) {
            e += ABS(newp[0] - oldp[0]);
            o += ABS(newp[ns] - oldp[os]);
            s += newp[ns]-newp[0];
            p += oldp[os]-oldp[0];
            t += oldp[os]-newp[0];
            oldp += os<<1;
            newp += ns<<1;
        }
        m->s += ABS(s);
        m->p += ABS(p);
        m->t += ABS(t);
    }
    m->e = e;
    m->o = o;
    m->d = e+o;
}

static void packed444_to_packed422_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    width /= 2;
    while( width-- ) {
        output[ 0 ] = input[ 0 ];
        output[ 1 ] = input[ 1 ];
        output[ 2 ] = input[ 3 ];
        output[ 3 ] = input[ 2 ];
        output += 4;
        input += 6;
    }
}

static void packed422_to_packed444_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    width /= 2;
    while( width-- ) {
        output[ 0 ] = input[ 0 ];
        output[ 1 ] = input[ 1 ];
        output[ 2 ] = input[ 3 ];
        output[ 3 ] = input[ 2 ];
        output[ 4 ] = input[ 1 ];
        output[ 5 ] = input[ 3 ];
        output += 6;
        input += 4;
    }
}

/**
 * For the middle pixels, the filter kernel is:
 *
 * [-1 3 -6 12 -24 80 80 -24 12 -6 3 -1]
 */
static void packed422_to_packed444_rec601_scanline_c( uint8_t *dest, uint8_t *src, int width )
{
    int i;

    /* Process two input pixels at a time.  Input is [Y'][Cb][Y'][Cr]. */
    for( i = 0; i < width / 2; i++ ) {
        dest[ (i*6) + 0 ] = src[ (i*4) + 0 ];
        dest[ (i*6) + 1 ] = src[ (i*4) + 1 ];
        dest[ (i*6) + 2 ] = src[ (i*4) + 3 ];

        dest[ (i*6) + 3 ] = src[ (i*4) + 2 ];
        if( i > (5*2) && i < ((width/2) - (6*2)) ) {
            dest[ (i*6) + 4 ] = clip255( ((  (80*(src[ (i*4) + 1 ] + src[ (i*4) + 5 ]))
                                           - (24*(src[ (i*4) - 3 ] + src[ (i*4) + 9 ]))
                                           + (12*(src[ (i*4) - 7 ] + src[ (i*4) + 13]))
                                           - ( 6*(src[ (i*4) - 11] + src[ (i*4) + 17]))
                                           + ( 3*(src[ (i*4) - 15] + src[ (i*4) + 21]))
                                           - (   (src[ (i*4) - 19] + src[ (i*4) + 25]))) + 64) >> 7 );
            dest[ (i*6) + 5 ] = clip255( ((  (80*(src[ (i*4) + 3 ] + src[ (i*4) + 7 ]))
                                           - (24*(src[ (i*4) - 1 ] + src[ (i*4) + 11]))
                                           + (12*(src[ (i*4) - 5 ] + src[ (i*4) + 15]))
                                           - ( 6*(src[ (i*4) - 9 ] + src[ (i*4) + 19]))
                                           + ( 3*(src[ (i*4) - 13] + src[ (i*4) + 23]))
                                           - (   (src[ (i*4) - 17] + src[ (i*4) + 27]))) + 64) >> 7 );
        } else if( i < ((width/2) - 1) ) {
            dest[ (i*6) + 4 ] = (src[ (i*4) + 1 ] + src[ (i*4) + 5 ] + 1) >> 1;
            dest[ (i*6) + 5 ] = (src[ (i*4) + 3 ] + src[ (i*4) + 7 ] + 1) >> 1;
        } else {
            dest[ (i*6) + 4 ] = src[ (i*4) + 1 ];
            dest[ (i*6) + 5 ] = src[ (i*4) + 3 ];
        }
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void vfilter_chroma_121_packed422_scanline_mmx( uint8_t *output, int width,
                                                       uint8_t *m, uint8_t *t, uint8_t *b )
{
    int i;
    const mmx_t ymask = { 0x00ff00ff00ff00ffULL };
    const mmx_t cmask = { 0xff00ff00ff00ff00ULL };

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    movq_m2r( ymask, mm7 );
    movq_m2r( cmask, mm6 );

    while( i-- ) {
        movq_m2r( *t, mm0 );
        movq_m2r( *b, mm1 );
        movq_m2r( *m, mm2 );

        movq_r2r ( mm2, mm3 );
        pand_r2r ( mm7, mm3 );

        pand_r2r ( mm6, mm0 );
        pand_r2r ( mm6, mm1 );
        pand_r2r ( mm6, mm2 );

        psrlq_i2r( 8, mm0 );
        psrlq_i2r( 8, mm1 );
        psrlq_i2r( 7, mm2 );

        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm1, mm2 );

        psllw_i2r( 6, mm2 );
        pand_r2r( mm6, mm2 );

        por_r2r ( mm3, mm2 );

        movq_r2m( mm2, *output );
        output += 8;
        t += 8;
        b += 8;
        m += 8;
    }
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (*t + *b + (*m << 1)) >> 2;
        output+=2; t+=2; b+=2; m+=2;
    }

    emms();
}
#endif

static void vfilter_chroma_121_packed422_scanline_c( uint8_t *output, int width,
                                                     uint8_t *m, uint8_t *t, uint8_t *b )
{
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (*t + *b + (*m << 1)) >> 2;
        output +=2; t+=2; b+=2; m+=2;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void vfilter_chroma_332_packed422_scanline_mmx( uint8_t *output, int width,
                                                       uint8_t *m, uint8_t *t, uint8_t *b )
{
    int i;
    const mmx_t ymask = { 0x00ff00ff00ff00ffULL };
    const mmx_t cmask = { 0xff00ff00ff00ff00ULL };

    // Get width in bytes.
    width *= 2;
    i = width / 8;
    width -= i * 8;

    movq_m2r( ymask, mm7 );
    movq_m2r( cmask, mm6 );

    while( i-- ) {
        movq_m2r( *t, mm0 );
        movq_m2r( *b, mm1 );
        movq_m2r( *m, mm2 );

        movq_r2r ( mm2, mm3 );
        pand_r2r ( mm7, mm3 );

        pand_r2r ( mm6, mm0 );
        pand_r2r ( mm6, mm1 );
        pand_r2r ( mm6, mm2 );

        psrlq_i2r( 8, mm0 );
        psrlq_i2r( 7, mm1 );
        psrlq_i2r( 8, mm2 );

        movq_r2r ( mm0, mm4 );
        psllw_i2r( 1, mm4 );
        paddw_r2r( mm4, mm0 );

        movq_r2r ( mm2, mm4 );
        psllw_i2r( 1, mm4 );
        paddw_r2r( mm4, mm2 );

        paddw_r2r( mm0, mm2 );
        paddw_r2r( mm1, mm2 );

        psllw_i2r( 5, mm2 );
        pand_r2r( mm6, mm2 );

        por_r2r ( mm3, mm2 );

        movq_r2m( mm2, *output );
        output += 8;
        t += 8;
        b += 8;
        m += 8;
    }
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (3 * *t + 3 * *m + 2 * *b) >> 3;
        output +=2; t+=2; b+=2; m+=2;
    }

    emms();
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void vfilter_chroma_332_packed422_scanline_sse2_aligned( uint8_t *output, int width,
                                                                uint8_t *m, uint8_t *t, uint8_t *b )
{
    int i;

    // Get width in bytes.
    width *= 2;
    i = width / 16;
    width -= i * 16;

    movdqa_m2r( dqwYMask, xmm7 );
    movdqa_m2r( dqwCMask, xmm6 );

    while( i-- ) {
        movdqa_m2r ( *t, xmm0 );
        movdqa_m2r ( *b, xmm1 );
        movdqa_m2r ( *m, xmm2 );

        movdqa_r2r ( xmm2, xmm3 );
        pand_r2r   ( xmm7, xmm3 );

        pand_r2r   ( xmm6, xmm0 );
        pand_r2r   ( xmm6, xmm1 );
        pand_r2r   ( xmm6, xmm2 );

        psrlq_i2r  ( 8, xmm0 );
        psrlq_i2r  ( 7, xmm1 );
        psrlq_i2r  ( 8, xmm2 );

        movdqa_r2r ( xmm0, xmm4 );
        movdqa_r2r ( xmm2, xmm5 );
        psllw_i2r  ( 1, xmm4 );
        psllw_i2r  ( 1, xmm5 );
        paddw_r2r  ( xmm4, xmm0 );
        paddw_r2r  ( xmm5, xmm2 );

        paddw_r2r  ( xmm0, xmm2 );
        paddw_r2r  ( xmm1, xmm2 );

        psllw_i2r  ( 5, xmm2 );
        pand_r2r   ( xmm6, xmm2 );

        por_r2r    ( xmm3, xmm2 );

        movdqa_r2m( xmm2, *output );
        output += 16;
        t += 16;
        b += 16;
        m += 16;
    }
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (3 * *t + 3 * *m + 2 * *b) >> 3;
        output +=2; t+=2; b+=2; m+=2;
    }
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void vfilter_chroma_332_packed422_scanline_sse2( uint8_t *output, int width,
                                                             uint8_t *m, uint8_t *t, uint8_t *b )
{
    int i;

    if (0 == (((intptr_t)output | (intptr_t)m | (intptr_t)t | (intptr_t)b) & 15)) {
      vfilter_chroma_332_packed422_scanline_sse2_aligned(output, width, m, t, b);
      return;
    }

    // Get width in bytes.
    width *= 2;
    i = width / 16;
    width -= i * 16;

    movdqa_m2r( dqwYMask, xmm7 );
    movdqa_m2r( dqwCMask, xmm6 );

    while( i-- ) {
        movdqu_m2r ( *t, xmm0 );
        movdqu_m2r ( *b, xmm1 );
        movdqu_m2r ( *m, xmm2 );

        movdqa_r2r ( xmm2, xmm3 );
        pand_r2r   ( xmm7, xmm3 );

        pand_r2r   ( xmm6, xmm0 );
        pand_r2r   ( xmm6, xmm1 );
        pand_r2r   ( xmm6, xmm2 );

        psrlq_i2r  ( 8, xmm0 );
        psrlq_i2r  ( 7, xmm1 );
        psrlq_i2r  ( 8, xmm2 );

        movdqa_r2r ( xmm0, xmm4 );
        movdqa_r2r ( xmm2, xmm5 );
        psllw_i2r  ( 1, xmm4 );
        psllw_i2r  ( 1, xmm5 );
        paddw_r2r  ( xmm4, xmm0 );
        paddw_r2r  ( xmm5, xmm2 );

        paddw_r2r  ( xmm0, xmm2 );
        paddw_r2r  ( xmm1, xmm2 );

        psllw_i2r  ( 5, xmm2 );
        pand_r2r   ( xmm6, xmm2 );

        por_r2r    ( xmm3, xmm2 );

        movdqu_r2m( xmm2, *output );
        output += 16;
        t += 16;
        b += 16;
        m += 16;
    }
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (3 * *t + 3 * *m + 2 * *b) >> 3;
        output +=2; t+=2; b+=2; m+=2;
    }
}
#endif


static void vfilter_chroma_332_packed422_scanline_c( uint8_t *output, int width,
                                                     uint8_t *m, uint8_t *t, uint8_t *b )
{
    output++; t++; b++; m++;
    while( width-- ) {
        *output = (3 * *t + 3 * *m + 2 * *b) >> 3;
        output +=2; t+=2; b+=2; m+=2;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void kill_chroma_packed422_inplace_scanline_mmx( uint8_t *data, int width )
{
    const mmx_t ymask = { 0x00ff00ff00ff00ffULL };
    const mmx_t nullchroma = { 0x8000800080008000ULL };

    movq_m2r( ymask, mm7 );
    movq_m2r( nullchroma, mm6 );
    for(; width > 4; width -= 4 ) {
        movq_m2r( *data, mm0 );
        pand_r2r( mm7, mm0 );
        paddb_r2r( mm6, mm0 );
        movq_r2m( mm0, *data );
        data += 8;
    }
    emms();

    while( width-- ) {
        data[ 1 ] = 128;
        data += 2;
    }
}
#endif

static void kill_chroma_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    while( width-- ) {
        data[ 1 ] = 128;
        data += 2;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void invert_colour_packed422_inplace_scanline_mmx( uint8_t *data, int width )
{
    const mmx_t allones = { 0xffffffffffffffffULL };

    movq_m2r( allones, mm1 );
    for(; width > 4; width -= 4 ) {
        movq_r2r( mm1, mm2 );
        movq_m2r( *data, mm0 );
        psubb_r2r( mm0, mm2 );
        movq_r2m( mm2, *data );
        data += 8;
    }
    emms();

    width *= 2;
    while( width-- ) {
        *data = 255 - *data;
        data++;
    }
}
#endif

static void invert_colour_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    width *= 2;
    while( width-- ) {
        *data = 255 - *data;
        data++;
    }
}

/*
// this duplicates alternate lines in alternate frames to highlight or mute
// the effects of chroma crawl. it is not a solution or proper filter. it's
// only for testing.
void testing_packed422_inplace_scanline_c( uint8_t *data, int width, int scanline )
{
    volatile static int topbottom = 0;
    static uint8_t scanbuffer[2048];

    if( scanline <= 1 ) {
        topbottom = scanline;
        memcpy(scanbuffer, data, width*2);
    }
    if ( scanline < 10 ) {
        printf("scanline: %d %d\n", scanline, topbottom);
    }
    if ( ((scanline-topbottom)/2)%2 && scanline > 1 ) {
        memcpy(data, scanbuffer, width*2);
    } else {
        memcpy(scanbuffer, data, width*2);
    }
}
*/

static void mirror_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    int x, tmp1, tmp2;
    int width2 = width*2;

    for( x = 0; x < width; x += 2 ) {
        tmp1 = data[ x   ];
        tmp2 = data[ x+1 ];
        data[ x   ] = data[ width2 - x     ];
        data[ x+1 ] = data[ width2 - x + 1 ];
        data[ width2 - x     ] = tmp1;
        data[ width2 - x + 1 ] = tmp2;
    }
}

static void halfmirror_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    int x;

    for( x = 0; x < width; x += 2 ) {
        data[ width + x     ] = data[ width - x     ];
        data[ width + x + 1 ] = data[ width - x + 1 ];
    }
}

static void filter_luma_121_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    int r1 = 0;
    int r2 = 0;

    data += 2;
    width -= 1;
    while( width-- ) {
        int s1, s2;
        s1 = *data + r1; r1 = *data;
        s2 = s1    + r2; r2 = s1;
        *(data - 2) = s2 >> 2;
        data += 2;
    }
}

static void filter_luma_14641_packed422_inplace_scanline_c( uint8_t *data, int width )
{
    int r1 = 0;
    int r2 = 0;
    int r3 = 0;
    int r4 = 0;

    width -= 4;
    data += 4;
    while( width-- ) {
        int s1, s2, s3, s4;
        s1 = *data + r1; r1 = *data;
        s2 = s1    + r2; r2 = s1;
        s3 = s2    + r3; r3 = s2;
        s4 = s3    + r4; r4 = s3;
        *(data - 4) = s4 >> 4;
        data += 2;
    }
}

static void interpolate_packed422_scanline_c( uint8_t *output, uint8_t *top,
                                              uint8_t *bot, int width )
{
    int i;

    for( i = width*2; i; --i ) {
        *output++ = ((*top++) + (*bot++)) >> 1;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void interpolate_packed422_scanline_mmx( uint8_t *output, uint8_t *top,
                                                uint8_t *bot, int width )
{
    const mmx_t shiftmask = { 0xfefffefffefffeffULL };  /* To avoid shifting chroma to luma. */
    int i;

    for( i = width/16; i; --i ) {
        movq_m2r( *bot, mm0 );
        movq_m2r( *top, mm1 );
        movq_m2r( *(bot + 8), mm2 );
        movq_m2r( *(top + 8), mm3 );
        movq_m2r( *(bot + 16), mm4 );
        movq_m2r( *(top + 16), mm5 );
        movq_m2r( *(bot + 24), mm6 );
        movq_m2r( *(top + 24), mm7 );
        pand_m2r( shiftmask, mm0 );
        pand_m2r( shiftmask, mm1 );
        pand_m2r( shiftmask, mm2 );
        pand_m2r( shiftmask, mm3 );
        pand_m2r( shiftmask, mm4 );
        pand_m2r( shiftmask, mm5 );
        pand_m2r( shiftmask, mm6 );
        pand_m2r( shiftmask, mm7 );
        psrlw_i2r( 1, mm0 );
        psrlw_i2r( 1, mm1 );
        psrlw_i2r( 1, mm2 );
        psrlw_i2r( 1, mm3 );
        psrlw_i2r( 1, mm4 );
        psrlw_i2r( 1, mm5 );
        psrlw_i2r( 1, mm6 );
        psrlw_i2r( 1, mm7 );
        paddb_r2r( mm1, mm0 );
        paddb_r2r( mm3, mm2 );
        paddb_r2r( mm5, mm4 );
        paddb_r2r( mm7, mm6 );
        movq_r2m( mm0, *output );
        movq_r2m( mm2, *(output + 8) );
        movq_r2m( mm4, *(output + 16) );
        movq_r2m( mm6, *(output + 24) );
        output += 32;
        top += 32;
        bot += 32;
    }
    width = (width & 0xf);

    for( i = width/4; i; --i ) {
        movq_m2r( *bot, mm0 );
        movq_m2r( *top, mm1 );
        pand_m2r( shiftmask, mm0 );
        pand_m2r( shiftmask, mm1 );
        psrlw_i2r( 1, mm0 );
        psrlw_i2r( 1, mm1 );
        paddb_r2r( mm1, mm0 );
        movq_r2m( mm0, *output );
        output += 8;
        top += 8;
        bot += 8;
    }
    width = width & 0x7;

    /* Handle last few pixels. */
    for( i = width * 2; i; --i ) {
        *output++ = ((*top++) + (*bot++)) >> 1;
    }

    emms();
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void interpolate_packed422_scanline_mmxext( uint8_t *output, uint8_t *top,
                                                   uint8_t *bot, int width )
{
    int i;

    for( i = width/16; i; --i ) {
        movq_m2r( *bot, mm0 );
        movq_m2r( *top, mm1 );
        movq_m2r( *(bot + 8), mm2 );
        movq_m2r( *(top + 8), mm3 );
        movq_m2r( *(bot + 16), mm4 );
        movq_m2r( *(top + 16), mm5 );
        movq_m2r( *(bot + 24), mm6 );
        movq_m2r( *(top + 24), mm7 );
        pavgb_r2r( mm1, mm0 );
        pavgb_r2r( mm3, mm2 );
        pavgb_r2r( mm5, mm4 );
        pavgb_r2r( mm7, mm6 );
        movntq_r2m( mm0, *output );
        movntq_r2m( mm2, *(output + 8) );
        movntq_r2m( mm4, *(output + 16) );
        movntq_r2m( mm6, *(output + 24) );
        output += 32;
        top += 32;
        bot += 32;
    }
    width = (width & 0xf);

    for( i = width/4; i; --i ) {
        movq_m2r( *bot, mm0 );
        movq_m2r( *top, mm1 );
        pavgb_r2r( mm1, mm0 );
        movntq_r2m( mm0, *output );
        output += 8;
        top += 8;
        bot += 8;
    }
    width = width & 0x7;

    /* Handle last few pixels. */
    for( i = width * 2; i; --i ) {
        *output++ = ((*top++) + (*bot++)) >> 1;
    }

    sfence();
    emms();
}
#endif

static void blit_colour_packed422_scanline_c( uint8_t *output, int width, int y, int cb, int cr )
{
    uint32_t colour = cr << 24 | y << 16 | cb << 8 | y;
    uint32_t *o = (uint32_t *) output;

    for( width /= 2; width; --width ) {
        *o++ = colour;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_colour_packed422_scanline_mmx( uint8_t *output, int width, int y, int cb, int cr )
{
    uint32_t colour = cr << 24 | y << 16 | cb << 8 | y;
    int i;

    movd_m2r( colour, mm1 );
    movd_m2r( colour, mm2 );
    psllq_i2r( 32, mm1 );
    por_r2r( mm1, mm2 );

    for( i = width / 16; i; --i ) {
        movq_r2m( mm2, *output );
        movq_r2m( mm2, *(output + 8) );
        movq_r2m( mm2, *(output + 16) );
        movq_r2m( mm2, *(output + 24) );
        output += 32;
    }
    width = (width & 0xf);

    for( i = width / 4; i; --i ) {
        movq_r2m( mm2, *output );
        output += 8;
    }
    width = (width & 0x7);

    for( i = width / 2; i; --i ) {
        *((uint32_t *) output) = colour;
        output += 4;
    }

    if( width & 1 ) {
        *output = y;
        *(output + 1) = cb;
    }

    emms();
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_colour_packed422_scanline_mmxext( uint8_t *output, int width, int y, int cb, int cr )
{
    uint32_t colour = cr << 24 | y << 16 | cb << 8 | y;
    int i;

    movd_m2r( colour, mm1 );
    movd_m2r( colour, mm2 );
    psllq_i2r( 32, mm1 );
    por_r2r( mm1, mm2 );

    for( i = width / 16; i; --i ) {
        movntq_r2m( mm2, *output );
        movntq_r2m( mm2, *(output + 8) );
        movntq_r2m( mm2, *(output + 16) );
        movntq_r2m( mm2, *(output + 24) );
        output += 32;
    }
    width = (width & 0xf);

    for( i = width / 4; i; --i ) {
        movntq_r2m( mm2, *output );
        output += 8;
    }
    width = (width & 0x7);

    for( i = width / 2; i; --i ) {
        *((uint32_t *) output) = colour;
        output += 4;
    }

    if( width & 1 ) {
        *output = y;
        *(output + 1) = cb;
    }

    sfence();
    emms();
}
#endif

static void blit_colour_packed4444_scanline_c( uint8_t *output, int width,
                                               int alpha, int luma, int cb, int cr )
{
    int j;

    for( j = 0; j < width; j++ ) {
        *output++ = alpha;
        *output++ = luma;
        *output++ = cb;
        *output++ = cr;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_colour_packed4444_scanline_mmx( uint8_t *output, int width,
                                                 int alpha, int luma,
                                                 int cb, int cr )
{
    uint32_t colour = (cr << 24) | (cb << 16) | (luma << 8) | alpha;
    int i;

    movd_m2r( colour, mm1 );
    movd_m2r( colour, mm2 );
    psllq_i2r( 32, mm1 );
    por_r2r( mm1, mm2 );

    for( i = width / 8; i; --i ) {
        movq_r2m( mm2, *output );
        movq_r2m( mm2, *(output + 8) );
        movq_r2m( mm2, *(output + 16) );
        movq_r2m( mm2, *(output + 24) );
        output += 32;
    }
    width = (width & 0x7);

    for( i = width / 2; i; --i ) {
        movq_r2m( mm2, *output );
        output += 8;
    }
    width = (width & 0x1);

    if( width ) {
        *((uint32_t *) output) = colour;
        output += 4;
    }

    emms();
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_colour_packed4444_scanline_mmxext( uint8_t *output, int width,
                                                    int alpha, int luma,
                                                    int cb, int cr )
{
    uint32_t colour = (cr << 24) | (cb << 16) | (luma << 8) | alpha;
    int i;

    movd_m2r( colour, mm1 );
    movd_m2r( colour, mm2 );
    psllq_i2r( 32, mm1 );
    por_r2r( mm1, mm2 );

    for( i = width / 8; i; --i ) {
        movntq_r2m( mm2, *output );
        movntq_r2m( mm2, *(output + 8) );
        movntq_r2m( mm2, *(output + 16) );
        movntq_r2m( mm2, *(output + 24) );
        output += 32;
    }
    width = (width & 0x7);

    for( i = width / 2; i; --i ) {
        movntq_r2m( mm2, *output );
        output += 8;
    }
    width = (width & 0x1);

    if( width ) {
        *((uint32_t *) output) = colour;
        output += 4;
    }

    sfence();
    emms();
}
#endif


#define speedy_memcpy_c xine_fast_memcpy
#define speedy_memcpy_mmx xine_fast_memcpy
#define speedy_memcpy_mmxext xine_fast_memcpy


static void blit_packed422_scanline_c( uint8_t *dest, const uint8_t *src, int width )
{
    speedy_memcpy_c( dest, src, width*2 );
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_packed422_scanline_mmx( uint8_t *dest, const uint8_t *src, int width )
{
    speedy_memcpy_mmx( dest, src, width*2 );
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blit_packed422_scanline_mmxext( uint8_t *dest, const uint8_t *src, int width )
{
    speedy_memcpy_mmxext( dest, src, width*2 );
}
#endif

static void composite_packed4444_alpha_to_packed422_scanline_c( uint8_t *output, uint8_t *input,
                                                                uint8_t *foreground, int width, int alpha )
{
    int i;

    for( i = 0; i < width; i++ ) {
        int af = foreground[ 0 ];

        if( af ) {
            int a = ((af * alpha) + 0x80) >> 8;


            if( a == 0xff ) {
                output[ 0 ] = foreground[ 1 ];

                if( ( i & 1 ) == 0 ) {
                    output[ 1 ] = foreground[ 2 ];
                    output[ 3 ] = foreground[ 3 ];
                }
            } else if( a ) {
                /**
                 * (1 - alpha)*B + alpha*F
                 * (1 - af*a)*B + af*a*F
                 *  B - af*a*B + af*a*F
                 *  B + a*(af*F - af*B)
                 */

                output[ 0 ] = input[ 0 ]
                            + ((alpha*( foreground[ 1 ]
                                        - multiply_alpha( foreground[ 0 ], input[ 0 ] ) ) + 0x80) >> 8);

                if( ( i & 1 ) == 0 ) {

                    /**
                     * At first I thought I was doing this incorrectly, but
                     * the following math has convinced me otherwise.
                     *
                     * C_r = (1 - alpha)*B + alpha*F
                     * C_r = B - af*a*B + af*a*F
                     *
                     * C_r = 128 + ((1 - af*a)*(B - 128) + a*af*(F - 128))
                     * C_r = 128 + (B - af*a*B - 128 + af*a*128 + a*af*F - a*af*128)
                     * C_r = B - af*a*B + a*af*F
                     */

                    output[ 1 ] = input[ 1 ] + ((alpha*( foreground[ 2 ]
                                            - multiply_alpha( foreground[ 0 ], input[ 1 ] ) ) + 0x80) >> 8);
                    output[ 3 ] = input[ 3 ] + ((alpha*( foreground[ 3 ]
                                            - multiply_alpha( foreground[ 0 ], input[ 3 ] ) ) + 0x80) >> 8);
                }
            }
        }
        foreground += 4;
        output += 2;
        input += 2;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void composite_packed4444_alpha_to_packed422_scanline_mmxext( uint8_t *output,
                                                                     uint8_t *input,
                                                                     uint8_t *foreground,
                                                                     int width, int alpha )
{
    const mmx_t alpha2 = { 0x0000FFFF00000000ULL };
    const mmx_t alpha1 = { 0xFFFF0000FFFFFFFFULL };
    const mmx_t round  = { 0x0080008000800080ULL };
    int i;

    if( !alpha ) {
        blit_packed422_scanline( output, input, width );
        return;
    }

    if( alpha == 256 ) {
        composite_packed4444_to_packed422_scanline( output, input, foreground, width );
        return;
    }

    READ_PREFETCH_2048( input );
    READ_PREFETCH_2048( foreground );

    movq_m2r( alpha, mm2 );
    pshufw_r2r( mm2, mm2, 0 );
    pxor_r2r( mm7, mm7 );

    for( i = width/2; i; i-- ) {
        int fg1 = *((uint32_t *) foreground);
        int fg2 = *(((uint32_t *) foreground)+1);

        if( fg1 || fg2 ) {
            /* mm1 = [ cr ][ y ][ cb ][ y ] */
            movd_m2r( *input, mm1 );
            punpcklbw_r2r( mm7, mm1 );

            movq_m2r( *foreground, mm3 );
            movq_r2r( mm3, mm4 );
            punpcklbw_r2r( mm7, mm3 );
            punpckhbw_r2r( mm7, mm4 );
            /* mm3 and mm4 will be the appropriate colours, mm5 and mm6 for alpha. */

            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 0 a ][ 0 a ][ 0 a ][ 0 a ] */
            pshufw_r2r( mm3, mm5, 0 );
            pshufw_r2r( mm4, mm6, 0 );
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 3 cr ][ 0 a ][ 2 cb ][ 1 y ]  == 11001000 == 201 */
            pshufw_r2r( mm3, mm3, 201 );
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 0 a ][ 1 y ][ 0 a ][ 0 a ]  == 00010000 == 16 */
            pshufw_r2r( mm4, mm4, 16 );

            pand_m2r( alpha1, mm3 );
            pand_m2r( alpha2, mm4 );
            pand_m2r( alpha1, mm5 );
            pand_m2r( alpha2, mm6 );
            por_r2r( mm4, mm3 );
            por_r2r( mm6, mm5 );

            /* now, mm5 is af and mm1 is B.  Need to multiply them. */
            pmullw_r2r( mm1, mm5 );

            /* Multiply by appalpha. */
            pmullw_r2r( mm2, mm3 );
            paddw_m2r( round, mm3 );
            psrlw_i2r( 8, mm3 );
            /* Result is now B + F. */
            paddw_r2r( mm3, mm1 );

            /* Round up appropriately. */
            paddw_m2r( round, mm5 );

            /* mm6 contains our i>>8; */
            movq_r2r( mm5, mm6 );
            psrlw_i2r( 8, mm6 );

            /* Add mm6 back into mm5.  Now our result is in the high bytes. */
            paddw_r2r( mm6, mm5 );

            /* Shift down. */
            psrlw_i2r( 8, mm5 );

            /* Multiply by appalpha. */
            pmullw_r2r( mm2, mm5 );
            paddw_m2r( round, mm5 );
            psrlw_i2r( 8, mm5 );

            psubusw_r2r( mm5, mm1 );

            /* mm1 = [ B + F - af*B ] */
            packuswb_r2r( mm1, mm1 );
            movd_r2m( mm1, *output );
        }

        foreground += 8;
        output += 4;
        input += 4;
    }
    sfence();
    emms();
}
#endif

static void composite_packed4444_to_packed422_scanline_c( uint8_t *output, uint8_t *input,
                                                          uint8_t *foreground, int width )
{
    int i;
    for( i = 0; i < width; i++ ) {
        int a = foreground[ 0 ];

        if( a == 0xff ) {
            output[ 0 ] = foreground[ 1 ];

            if( ( i & 1 ) == 0 ) {
                output[ 1 ] = foreground[ 2 ];
                output[ 3 ] = foreground[ 3 ];
            }
        } else if( a ) {
            /**
             * (1 - alpha)*B + alpha*F
             *  B + af*F - af*B
             */

            output[ 0 ] = input[ 0 ] + foreground[ 1 ] - multiply_alpha( foreground[ 0 ], input[ 0 ] );

            if( ( i & 1 ) == 0 ) {

                /**
                 * C_r = (1 - af)*B + af*F
                 * C_r = B - af*B + af*F
                 */

                output[ 1 ] = input[ 1 ] + foreground[ 2 ] - multiply_alpha( foreground[ 0 ], input[ 1 ] );
                output[ 3 ] = input[ 3 ] + foreground[ 3 ] - multiply_alpha( foreground[ 0 ], input[ 3 ] );
            }
        }
        foreground += 4;
        output += 2;
        input += 2;
    }
}


#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void composite_packed4444_to_packed422_scanline_mmxext( uint8_t *output, uint8_t *input,
                                                               uint8_t *foreground, int width )
{
    const mmx_t alpha2 = { 0x0000FFFF00000000ULL };
    const mmx_t alpha1 = { 0xFFFF0000FFFFFFFFULL };
    const mmx_t round  = { 0x0080008000800080ULL };
    int i;

    READ_PREFETCH_2048( input );
    READ_PREFETCH_2048( foreground );

    pxor_r2r( mm7, mm7 );
    for( i = width/2; i; i-- ) {
        int fg1 = *((uint32_t *) foreground);
        int fg2 = *(((uint32_t *) foreground)+1);

        if( (fg1 & 0xff) == 0xff && (fg2 & 0xff) == 0xff ) {
            movq_m2r( *foreground, mm3 );
            movq_r2r( mm3, mm4 );
            punpcklbw_r2r( mm7, mm3 );
            punpckhbw_r2r( mm7, mm4 );
            /* mm3 and mm4 will be the appropriate colours, mm5 and mm6 for alpha. */
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 3 cr ][ 0 a ][ 2 cb ][ 1 y ]  == 11001000 == 201 */
            pshufw_r2r( mm3, mm3, 201 );
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 0  a ][ 1 y ][ 0  a ][ 0 a ]  == 00010000 == 16 */
            pshufw_r2r( mm4, mm4, 16 );
            pand_m2r( alpha1, mm3 );
            pand_m2r( alpha2, mm4 );
            por_r2r( mm4, mm3 );
            /* mm1 = [ B + F - af*B ] */
            packuswb_r2r( mm3, mm3 );
            movd_r2m( mm3, *output );
        } else if( fg1 || fg2 ) {

            /* mm1 = [ cr ][ y ][ cb ][ y ] */
            movd_m2r( *input, mm1 );
            punpcklbw_r2r( mm7, mm1 );

            movq_m2r( *foreground, mm3 );
            movq_r2r( mm3, mm4 );
            punpcklbw_r2r( mm7, mm3 );
            punpckhbw_r2r( mm7, mm4 );
            /* mm3 and mm4 will be the appropriate colours, mm5 and mm6 for alpha. */

            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 0 a ][ 0 a ][ 0 a ][ 0 a ] */
            pshufw_r2r( mm3, mm5, 0 );
            pshufw_r2r( mm4, mm6, 0 );
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 3 cr ][ 0 a ][ 2 cb ][ 1 y ]  == 11001000 == 201 */
            pshufw_r2r( mm3, mm3, 201 );
            /* [ 3 cr ][ 2 cb ][ 1 y ][ 0 a ]  -> [ 0  a ][ 1 y ][ 0  a ][ 0 a ]  == 00010000 == 16 */
            pshufw_r2r( mm4, mm4, 16 );

            pand_m2r( alpha1, mm3 );
            pand_m2r( alpha2, mm4 );
            pand_m2r( alpha1, mm5 );
            pand_m2r( alpha2, mm6 );
            por_r2r( mm4, mm3 );
            por_r2r( mm6, mm5 );

            /* now, mm5 is af and mm1 is B.  Need to multiply them. */
            pmullw_r2r( mm1, mm5 );

            /* Result is now B + F. */
            paddw_r2r( mm3, mm1 );

            /* Round up appropriately. */
            paddw_m2r( round, mm5 );

            /* mm6 contains our i>>8; */
            movq_r2r( mm5, mm6 );
            psrlw_i2r( 8, mm6 );

            /* Add mm6 back into mm5.  Now our result is in the high bytes. */
            paddw_r2r( mm6, mm5 );

            /* Shift down. */
            psrlw_i2r( 8, mm5 );

            psubusw_r2r( mm5, mm1 );

            /* mm1 = [ B + F - af*B ] */
            packuswb_r2r( mm1, mm1 );
            movd_r2m( mm1, *output );
        }

        foreground += 8;
        output += 4;
        input += 4;
    }
    sfence();
    emms();
}
#endif

/**
 * um... just need some scrap paper...
 *   D = (1 - alpha)*B + alpha*F
 *   D = (1 - a)*B + a*textluma
 *     = B - a*B + a*textluma
 *     = B + a*(textluma - B)
 *   Da = (1 - a)*b + a
 */
static void composite_alphamask_to_packed4444_scanline_c( uint8_t *output,
                                                          uint8_t *input,
                                                          uint8_t *mask,
                                                          int width,
                                                          int textluma, int textcb,
                                                          int textcr )
{
    uint32_t opaque = (textcr << 24) | (textcb << 16) | (textluma << 8) | 0xff;
    int i;

    for( i = 0; i < width; i++ ) {
        int a = *mask;

        if( a == 0xff ) {
            *((uint32_t *) output) = opaque;
        } else if( (input[ 0 ] == 0x00) ) {
            *((uint32_t *) output) = (multiply_alpha( a, textcr ) << 24)
                                       | (multiply_alpha( a, textcb ) << 16)
                                       | (multiply_alpha( a, textluma ) << 8) | a;
        } else if( a ) {
            *((uint32_t *) output) = ((input[ 3 ] + multiply_alpha( a, textcr - input[ 3 ] )) << 24)
                                       | ((input[ 2 ] + multiply_alpha( a, textcb - input[ 2 ] )) << 16)
                                       | ((input[ 1 ] + multiply_alpha( a, textluma - input[ 1 ] )) << 8)
                                       |  (input[ 0 ] + multiply_alpha( a, 0xff - input[ 0 ] ));
        }
        mask++;
        output += 4;
        input += 4;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void composite_alphamask_to_packed4444_scanline_mmxext( uint8_t *output,
                                                               uint8_t *input,
                                                               uint8_t *mask,
                                                               int width,
                                                               int textluma, int textcb,
                                                               int textcr )
{
    uint32_t opaque = (textcr << 24) | (textcb << 16) | (textluma << 8) | 0xff;
    const mmx_t round = { 0x0080008000800080ULL };
    const mmx_t fullalpha = { 0x00000000000000ffULL };
    mmx_t colour;

    colour.w[ 0 ] = 0x00;
    colour.w[ 1 ] = textluma;
    colour.w[ 2 ] = textcb;
    colour.w[ 3 ] = textcr;

    movq_m2r( colour, mm1 );
    movq_r2r( mm1, mm0 );

    /* mm0 = [ cr ][ cb ][ y ][ 0xff ] */
    paddw_m2r( fullalpha, mm0 );

    /* mm7 = 0 */
    pxor_r2r( mm7, mm7 );

    /* mm6 = round */
    movq_m2r( round, mm6 );

    while( width-- ) {
        int a = *mask;

        if( a == 0xff ) {
            *((uint32_t *) output) = opaque;
        } else if( (input[ 0 ] == 0x00) ) {
            /* We just need to multiply our colour by the alpha value. */

            /* mm2 = [ a ][ a ][ a ][ a ] */
            movd_m2r( a, mm2 );
            movq_r2r( mm2, mm3 );
            pshufw_r2r( mm2, mm2, 0 );

            /* mm5 = [ cr ][ cb ][ y ][ 0 ] */
            movq_r2r( mm1, mm5 );

            /* Multiply by alpha. */
            pmullw_r2r( mm2, mm5 );
            paddw_m2r( round, mm5 );
            movq_r2r( mm5, mm6 );
            psrlw_i2r( 8, mm6 );
            paddw_r2r( mm6, mm5 );
            psrlw_i2r( 8, mm5 );

            /* Set alpha to a. */
            por_r2r( mm3, mm5 );

            /* Pack and write our result. */
            packuswb_r2r( mm5, mm5 );
            movd_r2m( mm5, *output );
        } else if( a ) {
            /* mm2 = [ a ][ a ][ a ][ a ] */
            movd_m2r( a, mm2 );
            pshufw_r2r( mm2, mm2, 0 );

            /* mm3 = [ cr ][ cb ][ y ][ 0xff ] */
            movq_r2r( mm0, mm3 );

            /* mm4 = [ i_cr ][ i_cb ][ i_y ][ i_a ] */
            movd_m2r( *input, mm4 );
            punpcklbw_r2r( mm7, mm4 );

            /* Subtract input and colour. */
            psubw_r2r( mm4, mm3 );  /* mm3 = mm3 - mm4 */

            /* Multiply alpha. */
            pmullw_r2r( mm2, mm3 );
            paddw_r2r( mm6, mm3 );
            movq_r2r( mm3, mm2 );
            psrlw_i2r( 8, mm3 );
            paddw_r2r( mm2, mm3 );
            psrlw_i2r( 8, mm3 );

            /* Add back in the input. */
            paddb_r2r( mm3, mm4 );

            /* Write result. */
            packuswb_r2r( mm4, mm4 );
            movd_r2m( mm4, *output );
        }
        mask++;
        output += 4;
        input += 4;
    }
    sfence();
    emms();
}
#endif

static void composite_alphamask_alpha_to_packed4444_scanline_c( uint8_t *output,
                                                                uint8_t *input,
                                                                uint8_t *mask, int width,
                                                                int textluma, int textcb,
                                                                int textcr, int alpha )
{
    uint32_t opaque = (textcr << 24) | (textcb << 16) | (textluma << 8) | 0xff;
    int i;

    for( i = 0; i < width; i++ ) {
        int af = *mask;

        if( af ) {
           int a = ((af * alpha) + 0x80) >> 8;

           if( a == 0xff ) {
               *((uint32_t *) output) = opaque;
           } else if( input[ 0 ] == 0x00 ) {
               *((uint32_t *) output) = (multiply_alpha( a, textcr ) << 24)
                                          | (multiply_alpha( a, textcb ) << 16)
                                          | (multiply_alpha( a, textluma ) << 8) | a;
           } else if( a ) {
               *((uint32_t *) output) = ((input[ 3 ] + multiply_alpha( a, textcr - input[ 3 ] )) << 24)
                                         | ((input[ 2 ] + multiply_alpha( a, textcb - input[ 2 ] )) << 16)
                                         | ((input[ 1 ] + multiply_alpha( a, textluma - input[ 1 ] )) << 8)
                                         | (a + multiply_alpha( 0xff - a, input[ 0 ] ));
           }
        }
        mask++;
        output += 4;
        input += 4;
    }
}

static void premultiply_packed4444_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    while( width-- ) {
        unsigned int cur_a = input[ 0 ];

        *((uint32_t *) output) = (multiply_alpha( cur_a, input[ 3 ] ) << 24)
                               | (multiply_alpha( cur_a, input[ 2 ] ) << 16)
                               | (multiply_alpha( cur_a, input[ 1 ] ) << 8)
                               | cur_a;

        output += 4;
        input += 4;
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void premultiply_packed4444_scanline_mmxext( uint8_t *output, uint8_t *input, int width )
{
    const mmx_t round  = { 0x0080008000800080ULL };
    const mmx_t alpha  = { 0x00000000000000ffULL };
    const mmx_t noalp  = { 0xffffffffffff0000ULL };

    pxor_r2r( mm7, mm7 );
    while( width-- ) {
        movd_m2r( *input, mm0 );
        punpcklbw_r2r( mm7, mm0 );

        movq_r2r( mm0, mm2 );
        pshufw_r2r( mm2, mm2, 0 );
        movq_r2r( mm2, mm4 );
        pand_m2r( alpha, mm4 );

        pmullw_r2r( mm2, mm0 );
        paddw_m2r( round, mm0 );

        movq_r2r( mm0, mm3 );
        psrlw_i2r( 8, mm3 );
        paddw_r2r( mm3, mm0 );
        psrlw_i2r( 8, mm0 );

        pand_m2r( noalp, mm0 );
        paddw_r2r( mm4, mm0 );

        packuswb_r2r( mm0, mm0 );
        movd_r2m( mm0, *output );

        output += 4;
        input += 4;
    }
    sfence();
    emms();
}
#endif

static void blend_packed422_scanline_c( uint8_t *output, uint8_t *src1,
                                        uint8_t *src2, int width, int pos )
{
    if( pos == 0 ) {
        blit_packed422_scanline( output, src1, width );
    } else if( pos == 256 ) {
        blit_packed422_scanline( output, src2, width );
    } else if( pos == 128 ) {
        interpolate_packed422_scanline( output, src1, src2, width );
    } else {
        width *= 2;
        while( width-- ) {
            *output++ = ( (*src1++ * ( 256 - pos )) + (*src2++ * pos) + 0x80 ) >> 8;
        }
    }
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void blend_packed422_scanline_mmxext( uint8_t *output, uint8_t *src1,
                                             uint8_t *src2, int width, int pos )
{
    if( pos <= 0 ) {
        blit_packed422_scanline( output, src1, width );
    } else if( pos >= 256 ) {
        blit_packed422_scanline( output, src2, width );
    } else if( pos == 128 ) {
        interpolate_packed422_scanline( output, src1, src2, width );
    } else {
        const mmx_t all256 = { 0x0100010001000100ULL };
        const mmx_t round  = { 0x0080008000800080ULL };

        movd_m2r( pos, mm0 );
        pshufw_r2r( mm0, mm0, 0 );
        movq_m2r( all256, mm1 );
        psubw_r2r( mm0, mm1 );
        pxor_r2r( mm7, mm7 );

        for( width /= 2; width; width-- ) {
            movd_m2r( *src1, mm3 );
            movd_m2r( *src2, mm4 );
            punpcklbw_r2r( mm7, mm3 );
            punpcklbw_r2r( mm7, mm4 );

            pmullw_r2r( mm1, mm3 );
            pmullw_r2r( mm0, mm4 );
            paddw_r2r( mm4, mm3 );
            paddw_m2r( round, mm3 );
            psrlw_i2r( 8, mm3 );

            packuswb_r2r( mm3, mm3 );
            movd_r2m( mm3, *output );

            output += 4;
            src1 += 4;
            src2 += 4;
        }
        sfence();
        emms();
    }
}
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void quarter_blit_vertical_packed422_scanline_mmxext( uint8_t *output, uint8_t *one,
                                                             uint8_t *three, int width )
{
    int i;

    for( i = width/16; i; --i ) {
        movq_m2r( *one, mm0 );
        movq_m2r( *three, mm1 );
        movq_m2r( *(one + 8), mm2 );
        movq_m2r( *(three + 8), mm3 );
        movq_m2r( *(one + 16), mm4 );
        movq_m2r( *(three + 16), mm5 );
        movq_m2r( *(one + 24), mm6 );
        movq_m2r( *(three + 24), mm7 );
        pavgb_r2r( mm1, mm0 );
        pavgb_r2r( mm1, mm0 );
        pavgb_r2r( mm3, mm2 );
        pavgb_r2r( mm3, mm2 );
        pavgb_r2r( mm5, mm4 );
        pavgb_r2r( mm5, mm4 );
        pavgb_r2r( mm7, mm6 );
        pavgb_r2r( mm7, mm6 );
        movntq_r2m( mm0, *output );
        movntq_r2m( mm2, *(output + 8) );
        movntq_r2m( mm4, *(output + 16) );
        movntq_r2m( mm6, *(output + 24) );
        output += 32;
        one += 32;
        three += 32;
    }
    width = (width & 0xf);

    for( i = width/4; i; --i ) {
        movq_m2r( *one, mm0 );
        movq_m2r( *three, mm1 );
        pavgb_r2r( mm1, mm0 );
        pavgb_r2r( mm1, mm0 );
        movntq_r2m( mm0, *output );
        output += 8;
        one += 8;
        three += 8;
    }
    width = width & 0x7;

    /* Handle last few pixels. */
    for( i = width * 2; i; --i ) {
        *output++ = (*one + *three + *three + *three + 2) / 4;
        one++;
        three++;
    }

    sfence();
    emms();
}
#endif


static void quarter_blit_vertical_packed422_scanline_c( uint8_t *output, uint8_t *one,
                                                        uint8_t *three, int width )
{
    width *= 2;
    while( width-- ) {
        *output++ = (*one + *three + *three + *three + 2) / 4;
        one++;
        three++;
    }
}

static void subpix_blit_vertical_packed422_scanline_c( uint8_t *output, uint8_t *top,
                                                       uint8_t *bot, int subpixpos, int width )
{
    if( subpixpos == 32768 ) {
        interpolate_packed422_scanline( output, top, bot, width );
    } else if( subpixpos == 16384 ) {
        quarter_blit_vertical_packed422_scanline( output, top, bot, width );
    } else if( subpixpos == 49152 ) {
        quarter_blit_vertical_packed422_scanline( output, bot, top, width );
    } else {
        int x;

        width *= 2;
        for( x = 0; x < width; x++ ) {
            output[ x ] = ( ( top[ x ] * subpixpos ) + ( bot[ x ] * ( 0xffff - subpixpos ) ) ) >> 16;
        }
    }
}

static void a8_subpix_blit_scanline_c( uint8_t *output, uint8_t *input,
                                       int lasta, int startpos, int width )
{
    int pos = 0xffff - (startpos & 0xffff);
    int prev = lasta;
    int x;

    for( x = 0; x < width; x++ ) {
        output[ x ] = ( ( prev * pos ) + ( input[ x ] * ( 0xffff - pos ) ) ) >> 16;
        prev = input[ x ];
    }
}

/**
 * These are from lavtools in mjpegtools:
 *
 * colorspace.c:  Routines to perform colorspace conversions.
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 */

#define FP_BITS 18

/* precomputed tables */

static int Y_R[256];
static int Y_G[256];
static int Y_B[256];
static int Cb_R[256];
static int Cb_G[256];
static int Cb_B[256];
static int Cr_R[256];
static int Cr_G[256];
static int Cr_B[256];
static int conv_RY_inited = 0;

static int RGB_Y[256];
static int R_Cr[256];
static int G_Cb[256];
static int G_Cr[256];
static int B_Cb[256];
static int conv_YR_inited = 0;

static int myround(double n)
{
  if (n >= 0)
    return (int)(n + 0.5);
  else
    return (int)(n - 0.5);
}

static void init_RGB_to_YCbCr_tables(void)
{
  int i;

  /*
   * Q_Z[i] =   (coefficient * i
   *             * (Q-excursion) / (Z-excursion) * fixed-point-factor)
   *
   * to one of each, add the following:
   *             + (fixed-point-factor / 2)         --- for rounding later
   *             + (Q-offset * fixed-point-factor)  --- to add the offset
   *
   */
  for (i = 0; i < 256; i++) {
    Y_R[i] = myround(0.299 * (double)i * 219.0 / 255.0 * (double)(1<<FP_BITS));
    Y_G[i] = myround(0.587 * (double)i * 219.0 / 255.0 * (double)(1<<FP_BITS));
    Y_B[i] = myround((0.114 * (double)i * 219.0 / 255.0 * (double)(1<<FP_BITS))
                     + (double)(1<<(FP_BITS-1)) + (16.0 * (double)(1<<FP_BITS)));

    Cb_R[i] = myround(-0.168736 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cb_G[i] = myround(-0.331264 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cb_B[i] = myround((0.500 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS))
                       + (double)(1<<(FP_BITS-1)) + (128.0 * (double)(1<<FP_BITS)));

    Cr_R[i] = myround(0.500 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cr_G[i] = myround(-0.418688 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cr_B[i] = myround((-0.081312 * (double)i * 224.0 / 255.0 * (double)(1<<FP_BITS))
                      + (double)(1<<(FP_BITS-1)) + (128.0 * (double)(1<<FP_BITS)));
  }
  conv_RY_inited = 1;
}

static void init_YCbCr_to_RGB_tables(void)
{
  int i;

  /*
   * Q_Z[i] =   (coefficient * i
   *             * (Q-excursion) / (Z-excursion) * fixed-point-factor)
   *
   * to one of each, add the following:
   *             + (fixed-point-factor / 2)         --- for rounding later
   *             + (Q-offset * fixed-point-factor)  --- to add the offset
   *
   */

  /* clip Y values under 16 */
  for (i = 0; i < 16; i++) {
    RGB_Y[i] = myround((1.0 * (double)(16) * 255.0 / 219.0 * (double)(1<<FP_BITS))
                       + (double)(1<<(FP_BITS-1)));
  }
  for (i = 16; i < 236; i++) {
    RGB_Y[i] = myround((1.0 * (double)(i - 16) * 255.0 / 219.0 * (double)(1<<FP_BITS))
                       + (double)(1<<(FP_BITS-1)));
  }
  /* clip Y values above 235 */
  for (i = 236; i < 256; i++) {
    RGB_Y[i] = myround((1.0 * (double)(235) * 255.0 / 219.0 * (double)(1<<FP_BITS))
                       + (double)(1<<(FP_BITS-1)));
  }

  /* clip Cb/Cr values below 16 */
  for (i = 0; i < 16; i++) {
    R_Cr[i] = myround(1.402 * (double)(-112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(-112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(-112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(-112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  for (i = 16; i < 241; i++) {
    R_Cr[i] = myround(1.402 * (double)(i - 128) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(i - 128) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(i - 128) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(i - 128) * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  /* clip Cb/Cr values above 240 */
  for (i = 241; i < 256; i++) {
    R_Cr[i] = myround(1.402 * (double)(112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(i - 128) * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(112) * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  conv_YR_inited = 1;
}

static void rgb24_to_packed444_rec601_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    if( !conv_RY_inited ) init_RGB_to_YCbCr_tables();

    while( width-- ) {
        int r = input[ 0 ];
        int g = input[ 1 ];
        int b = input[ 2 ];

        output[ 0 ] = (Y_R[ r ] + Y_G[ g ] + Y_B[ b ]) >> FP_BITS;
        output[ 1 ] = (Cb_R[ r ] + Cb_G[ g ] + Cb_B[ b ]) >> FP_BITS;
        output[ 2 ] = (Cr_R[ r ] + Cr_G[ g ] + Cr_B[ b ]) >> FP_BITS;
        output += 3;
        input += 3;
    }
}

static void rgba32_to_packed4444_rec601_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    if( !conv_RY_inited ) init_RGB_to_YCbCr_tables();

    while( width-- ) {
        int r = input[ 0 ];
        int g = input[ 1 ];
        int b = input[ 2 ];
        int a = input[ 3 ];

        output[ 0 ] = a;
        output[ 1 ] = (Y_R[ r ] + Y_G[ g ] + Y_B[ b ]) >> FP_BITS;
        output[ 2 ] = (Cb_R[ r ] + Cb_G[ g ] + Cb_B[ b ]) >> FP_BITS;
        output[ 3 ] = (Cr_R[ r ] + Cr_G[ g ] + Cr_B[ b ]) >> FP_BITS;
        output += 4;
        input += 4;
    }
}

static void packed444_to_rgb24_rec601_scanline_c( uint8_t *output, uint8_t *input, int width )
{
    if( !conv_YR_inited ) init_YCbCr_to_RGB_tables();

    while( width-- ) {
        int luma = input[ 0 ];
        int cb = input[ 1 ];
        int cr = input[ 2 ];

        output[ 0 ] = clip255( (RGB_Y[ luma ] + R_Cr[ cr ]) >> FP_BITS );
        output[ 1 ] = clip255( (RGB_Y[ luma ] + G_Cb[ cb ] + G_Cr[cr]) >> FP_BITS );
        output[ 2 ] = clip255( (RGB_Y[ luma ] + B_Cb[ cb ]) >> FP_BITS );

        output += 3;
        input += 3;
    }
}

/**
 * 601 numbers:
 *
 * Y' =  0.299*R' + 0.587*G' + 0.114*B' (in  0.0 to  1.0)
 * Cb = -0.169*R' - 0.331*G' + 0.500*B' (in -0.5 to +0.5)
 * Cr =  0.500*R' - 0.419*G' - 0.081*B' (in -0.5 to +0.5)
 *
 * Inverse:
 *      Y         Cb        Cr
 * R  1.0000   -0.0009    1.4017
 * G  1.0000   -0.3437   -0.7142
 * B  1.0000    1.7722    0.0010
 *
 * S170M numbers:
 * Y'   =  0.299*R' + 0.587*G' + 0.114*B' (in  0.0 to 1.0)
 * B-Y' = -0.299*R' - 0.587*G' + 0.886*B'
 * R-Y' =  0.701*R' - 0.587*G' - 0.114*B'
 */
/*
static void packed444_to_rgb24_rec601_reference_scanline( uint8_t *output, uint8_t *input, int width )
{
    while( width-- ) {
        double yp = (((double) input[ 0 ]) - 16.0) / 255.0;
        double cb = (((double) input[ 1 ]) - 128.0) / 255.0;
        double cr = (((double) input[ 2 ]) - 128.0) / 255.0;
        double r, g, b;

        r = yp - (0.0009*cb) + (1.4017*cr);
        g = yp - (0.3437*cb) - (0.7142*cr);
        b = yp + (1.7722*cb) + (0.0010*cr);

        if( r > 1.0 ) r = 1.0; else if( r < 0.0 ) r = 0.0;
        if( g > 1.0 ) g = 1.0; else if( g < 0.0 ) g = 0.0;
        if( b > 1.0 ) b = 1.0; else if( b < 0.0 ) b = 0.0;

        output[ 0 ] = (int) ((r * 255.0) + 0.5);
        output[ 1 ] = (int) ((g * 255.0) + 0.5);
        output[ 2 ] = (int) ((b * 255.0) + 0.5);

        output += 3;
        input += 3;
    }
}
*/

static void packed444_to_nonpremultiplied_packed4444_scanline_c( uint8_t *output,
                                                                 uint8_t *input,
                                                                 int width, int alpha )
{
    int i;

    for( i = 0; i < width; i++ ) {
        output[ 0 ] = alpha & 0xff;
        output[ 1 ] = input[ 0 ] & 0xff;
        output[ 2 ] = input[ 1 ] & 0xff;
        output[ 3 ] = input[ 2 ] & 0xff;

        output += 4;
        input += 3;
    }
}

static void aspect_adjust_packed4444_scanline_c( uint8_t *output,
                                                 uint8_t *input,
                                                 int width,
                                                 double pixel_aspect )
{
    double i;
    int prev_i = 0;
    int w = 0;

    pixel_aspect = 1.0 / pixel_aspect;

    for( i = 0.0; i < width; i += pixel_aspect ) {
        uint8_t *curin = input + ((int) i)*4;

        if( !prev_i ) {
            output[ 0 ] = curin[ 0 ];
            output[ 1 ] = curin[ 1 ];
            output[ 2 ] = curin[ 2 ];
            output[ 3 ] = curin[ 3 ];
        } else {
            int avg_a = 0;
            int avg_y = 0;
            int avg_cb = 0;
            int avg_cr = 0;
            int pos = prev_i * 4;
            int c = 0;
            int j;

            for( j = prev_i; j <= (int) i; j++ ) {
                avg_a += input[ pos++ ];
                avg_y += input[ pos++ ];
                avg_cb += input[ pos++ ];
                avg_cr += input[ pos++ ];
                c++;
            }
            output[ 0 ] = avg_a / c;
            output[ 1 ] = avg_y / c;
            output[ 2 ] = avg_cb / c;
            output[ 3 ] = avg_cr / c;
        }
        output += 4;
        prev_i = (int) i;
        w++;
    }
}

/**
 * Sub-pixel data bar renderer.  There are 128 bars.
 */
static void composite_bars_packed4444_scanline_c( uint8_t *output,
                                                  uint8_t *background, int width,
                                                  int a, int luma, int cb, int cr,
                                                  int percentage )
{
    /**
     * This is the size of both the bar and the spacing in between in subpixel
     * units out of 256.  Yes, as it so happens, that puts it equal to 'width'.
     */
    int barsize = ( width * 256 ) / 256;
    int i;

    /* We only need to composite the bar on the pixels that matter. */
    for( i = 0; i < percentage; i++ ) {
        int barstart = i * barsize * 2;
        int barend = barstart + barsize;
        int pixstart = barstart / 256;
        int pixend = barend / 256;
        int j;

        for( j = pixstart; j <= pixend; j++ ) {
            uint8_t *curout = output + (j*4);
            uint8_t *curin = background + (j*4);
            int curstart = j * 256;
            int curend = curstart + 256;
            int alpha;

            if( barstart > curstart ) curstart = barstart;
            if( barend < curend ) curend = barend;
            if( curend - curstart < 256 ) {
                alpha = ( ( curend - curstart ) * a ) / 256;
            } else {
                alpha = a;
            }

            curout[ 0 ] = curin[ 0 ] + multiply_alpha( alpha - curin[ 0 ], alpha );
            curout[ 1 ] = curin[ 1 ] + multiply_alpha( luma - curin[ 1 ], alpha );
            curout[ 2 ] = curin[ 2 ] + multiply_alpha( cb - curin[ 2 ], alpha );
            curout[ 3 ] = curin[ 3 ] + multiply_alpha( cr - curin[ 3 ], alpha );
        }
    }
}

/* horizontal 1:2 interpolation filter */
static void chroma_422_to_444_mpeg2_plane_c( uint8_t *dst, uint8_t *src, int width, int height )
{
    int i, i2, w, j, im2, im1, ip1, ip2, ip3;

    w = width / 2;

    for( j = 0; j < height; j++ ) {
        for( i = 0; i < w; i++ ) {
            i2 = i << 1;
            im2 = (i<2) ? 0 : i-2;
            im1 = (i<1) ? 0 : i-1;
            ip1 = (i<w-1) ? i+1 : w-1;
            ip2 = (i<w-2) ? i+2 : w-1;
            ip3 = (i<w-3) ? i+3 : w-1;

            /* FIR filter coefficients (*256): 21 0 -52 0 159 256 159 0 -52 0 21 */
            /* even samples (0 0 256 0 0) */
            dst[ i2 ] = src[i];

            /* odd samples (21 -52 159 159 -52 21) */
            dst[ i2 + 1 ] = clip255( (   21*(src[im2]+src[ip3])
                                      -  52*(src[im1]+src[ip2])
                                      + 159*(src[i]+src[ip1]) + 128 ) >> 8 );
        }
        src += w;
        dst += width;
    }
}

/* vertical 1:2 interpolation filter */
static void chroma_420_to_422_mpeg2_plane_c( uint8_t *dst, uint8_t *src,
                                             int width, int height, int progressive )
{
    int w, h, i, j, j2;
    int jm6, jm5, jm4, jm3, jm2, jm1, jp1, jp2, jp3, jp4, jp5, jp6, jp7;

    w = width / 2;
    h = height / 2;

    if( progressive ) {
        /* intra frame */
        for( i = 0; i < w; i++ ) {
            for( j = 0; j < h; j++ ) {
                j2 = j << 1;
                jm3 = (j<3) ? 0 : j-3;
                jm2 = (j<2) ? 0 : j-2;
                jm1 = (j<1) ? 0 : j-1;
                jp1 = (j<h-1) ? j+1 : h-1;
                jp2 = (j<h-2) ? j+2 : h-1;
                jp3 = (j<h-3) ? j+3 : h-1;

                /* FIR filter coefficients (*256): 5 -21 70 228 -37 11 */
                /* New FIR filter coefficients (*256): 3 -16 67 227 -32 7 */
                dst[w*j2] = clip255( (    3*src[w*jm3]
                                       - 16*src[w*jm2]
                                       + 67*src[w*jm1]
                                      + 227*src[w*j]
                                       - 32*src[w*jp1]
                                        + 7*src[w*jp2] + 128 ) >> 8 );

                dst[w*(j2+1)] = clip255( (   3*src[w*jp3]
                                          - 16*src[w*jp2]
                                          + 67*src[w*jp1]
                                         + 227*src[w*j]
                                          - 32*src[w*jm1]
                                          + 7*src[w*jm2] + 128 ) >> 8 );
            }
            src++;
            dst++;
        }
    } else {
        /* intra field */
        for( i = 0; i < w; i++ ) {
            for( j = 0; j < h; j += 2 ) {
                j2 = j << 1;

                /* top field */
                jm6 = (j<6) ? 0 : j-6;
                jm4 = (j<4) ? 0 : j-4;
                jm2 = (j<2) ? 0 : j-2;
                jp2 = (j<h-2) ? j+2 : h-2;
                jp4 = (j<h-4) ? j+4 : h-2;
                jp6 = (j<h-6) ? j+6 : h-2;

                /* Polyphase FIR filter coefficients (*256): 2 -10 35 242 -18 5 */
                /* New polyphase FIR filter coefficients (*256): 1 -7 30 248 -21 5 */
                dst[w*j2] = clip255( (   1*src[w*jm6]
                                       - 7*src[w*jm4]
                                      + 30*src[w*jm2]
                                     + 248*src[w*j]
                                      - 21*src[w*jp2]
                                       + 5*src[w*jp4] + 128 ) >> 8 );

                /* Polyphase FIR filter coefficients (*256): 11 -38 192 113 -30 8 */
                /* New polyphase FIR filter coefficients (*256):7 -35 194 110 -24 4 */
                dst[w*(j2+2)] = clip255( ( 7*src[w*jm4]
                                         -35*src[w*jm2]
                                        +194*src[w*j]
                                        +110*src[w*jp2]
                                         -24*src[w*jp4]
                                          +4*src[w*jp6] + 128 ) >> 8 );

                /* bottom field */
                jm5 = (j<5) ? 1 : j-5;
                jm3 = (j<3) ? 1 : j-3;
                jm1 = (j<1) ? 1 : j-1;
                jp1 = (j<h-1) ? j+1 : h-1;
                jp3 = (j<h-3) ? j+3 : h-1;
                jp5 = (j<h-5) ? j+5 : h-1;
                jp7 = (j<h-7) ? j+7 : h-1;

                /* Polyphase FIR filter coefficients (*256): 11 -38 192 113 -30 8 */
                /* New polyphase FIR filter coefficients (*256):7 -35 194 110 -24 4 */
                dst[w*(j2+1)] = clip255( ( 7*src[w*jp5]
                                         -35*src[w*jp3]
                                        +194*src[w*jp1]
                                        +110*src[w*jm1]
                                         -24*src[w*jm3]
                                          +4*src[w*jm5] + 128 ) >> 8 );

                dst[w*(j2+3)] = clip255( (  1*src[w*jp7]
                                           -7*src[w*jp5]
                                          +30*src[w*jp3]
                                         +248*src[w*jp1]
                                          -21*src[w*jm1]
                                           +5*src[w*jm3] + 128 ) >> 8 );
            }
            src++;
            dst++;
        }
    }
}

static uint32_t speedy_accel;

void setup_speedy_calls( uint32_t accel, int verbose )
{
    speedy_accel = accel;

    interpolate_packed422_scanline = interpolate_packed422_scanline_c;
    blit_colour_packed422_scanline = blit_colour_packed422_scanline_c;
    blit_colour_packed4444_scanline = blit_colour_packed4444_scanline_c;
    blit_packed422_scanline = blit_packed422_scanline_c;
    composite_packed4444_to_packed422_scanline = composite_packed4444_to_packed422_scanline_c;
    composite_packed4444_alpha_to_packed422_scanline = composite_packed4444_alpha_to_packed422_scanline_c;
    composite_alphamask_to_packed4444_scanline = composite_alphamask_to_packed4444_scanline_c;
    composite_alphamask_alpha_to_packed4444_scanline = composite_alphamask_alpha_to_packed4444_scanline_c;
    premultiply_packed4444_scanline = premultiply_packed4444_scanline_c;
    blend_packed422_scanline = blend_packed422_scanline_c;
    filter_luma_121_packed422_inplace_scanline = filter_luma_121_packed422_inplace_scanline_c;
    filter_luma_14641_packed422_inplace_scanline = filter_luma_14641_packed422_inplace_scanline_c;
    comb_factor_packed422_scanline = 0;
    diff_factor_packed422_scanline = diff_factor_packed422_scanline_c;
    kill_chroma_packed422_inplace_scanline = kill_chroma_packed422_inplace_scanline_c;
    mirror_packed422_inplace_scanline = mirror_packed422_inplace_scanline_c;
    halfmirror_packed422_inplace_scanline = halfmirror_packed422_inplace_scanline_c;
    speedy_memcpy = speedy_memcpy_c;
    diff_packed422_block8x8 = diff_packed422_block8x8_c;
    a8_subpix_blit_scanline = a8_subpix_blit_scanline_c;
    quarter_blit_vertical_packed422_scanline = quarter_blit_vertical_packed422_scanline_c;
    subpix_blit_vertical_packed422_scanline = subpix_blit_vertical_packed422_scanline_c;
    composite_bars_packed4444_scanline = composite_bars_packed4444_scanline_c;
    packed444_to_nonpremultiplied_packed4444_scanline = packed444_to_nonpremultiplied_packed4444_scanline_c;
    aspect_adjust_packed4444_scanline = aspect_adjust_packed4444_scanline_c;
    packed444_to_packed422_scanline = packed444_to_packed422_scanline_c;
    packed422_to_packed444_scanline = packed422_to_packed444_scanline_c;
    packed422_to_packed444_rec601_scanline = packed422_to_packed444_rec601_scanline_c;
    packed444_to_rgb24_rec601_scanline = packed444_to_rgb24_rec601_scanline_c;
    rgb24_to_packed444_rec601_scanline = rgb24_to_packed444_rec601_scanline_c;
    rgba32_to_packed4444_rec601_scanline = rgba32_to_packed4444_rec601_scanline_c;
    chroma_422_to_444_mpeg2_plane = chroma_422_to_444_mpeg2_plane_c;
    chroma_420_to_422_mpeg2_plane = chroma_420_to_422_mpeg2_plane_c;
    invert_colour_packed422_inplace_scanline = invert_colour_packed422_inplace_scanline_c;
    vfilter_chroma_121_packed422_scanline = vfilter_chroma_121_packed422_scanline_c;
    vfilter_chroma_332_packed422_scanline = vfilter_chroma_332_packed422_scanline_c;

#if defined(ARCH_X86) || defined(ARCH_X86_64)
    if( speedy_accel & MM_ACCEL_X86_MMXEXT ) {
        if( verbose ) {
            printf( "speedycode: Using MMXEXT optimized functions.\n" );
        }
        interpolate_packed422_scanline = interpolate_packed422_scanline_mmxext;
        blit_colour_packed422_scanline = blit_colour_packed422_scanline_mmxext;
        blit_colour_packed4444_scanline = blit_colour_packed4444_scanline_mmxext;
        blit_packed422_scanline = blit_packed422_scanline_mmxext;
        composite_packed4444_to_packed422_scanline = composite_packed4444_to_packed422_scanline_mmxext;
        composite_packed4444_alpha_to_packed422_scanline = composite_packed4444_alpha_to_packed422_scanline_mmxext;
        composite_alphamask_to_packed4444_scanline = composite_alphamask_to_packed4444_scanline_mmxext;
        premultiply_packed4444_scanline = premultiply_packed4444_scanline_mmxext;
        kill_chroma_packed422_inplace_scanline = kill_chroma_packed422_inplace_scanline_mmx;
        blend_packed422_scanline = blend_packed422_scanline_mmxext;
        diff_factor_packed422_scanline = diff_factor_packed422_scanline_mmx;
        comb_factor_packed422_scanline = comb_factor_packed422_scanline_mmx;
        diff_packed422_block8x8 = diff_packed422_block8x8_mmx;
        quarter_blit_vertical_packed422_scanline = quarter_blit_vertical_packed422_scanline_mmxext;
        invert_colour_packed422_inplace_scanline = invert_colour_packed422_inplace_scanline_mmx;
        vfilter_chroma_121_packed422_scanline = vfilter_chroma_121_packed422_scanline_mmx;
        vfilter_chroma_332_packed422_scanline = vfilter_chroma_332_packed422_scanline_mmx;
        speedy_memcpy = speedy_memcpy_mmxext;
    } else if( speedy_accel & MM_ACCEL_X86_MMX ) {
        if( verbose ) {
            printf( "speedycode: Using MMX optimized functions.\n" );
        }
        interpolate_packed422_scanline = interpolate_packed422_scanline_mmx;
        blit_colour_packed422_scanline = blit_colour_packed422_scanline_mmx;
        blit_colour_packed4444_scanline = blit_colour_packed4444_scanline_mmx;
        blit_packed422_scanline = blit_packed422_scanline_mmx;
        diff_factor_packed422_scanline = diff_factor_packed422_scanline_mmx;
        comb_factor_packed422_scanline = comb_factor_packed422_scanline_mmx;
        kill_chroma_packed422_inplace_scanline = kill_chroma_packed422_inplace_scanline_mmx;
        diff_packed422_block8x8 = diff_packed422_block8x8_mmx;
        invert_colour_packed422_inplace_scanline = invert_colour_packed422_inplace_scanline_mmx;
        vfilter_chroma_121_packed422_scanline = vfilter_chroma_121_packed422_scanline_mmx;
        vfilter_chroma_332_packed422_scanline = vfilter_chroma_332_packed422_scanline_mmx;
        speedy_memcpy = speedy_memcpy_mmx;
    } else {
        if( verbose ) {
            printf( "speedycode: No MMX or MMXEXT support detected, using C fallbacks.\n" );
        }
    }

    if( speedy_accel & MM_ACCEL_X86_SSE2 ) {
        if( verbose ) {
            printf( "speedycode: Using SSE2 optimized functions.\n" );
        }
        diff_factor_packed422_scanline = diff_factor_packed422_scanline_sse2;
        vfilter_chroma_332_packed422_scanline = vfilter_chroma_332_packed422_scanline_sse2;
    }
#endif
}

uint32_t speedy_get_accel( void )
{
    return speedy_accel;
}

