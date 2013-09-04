/*****************************************************************************
** Copyright (c) 2000 John Adcock, Tom Barry, Steve Grimm  All rights reserved.
** port copyright (c) 2003 Miguel Freitas
******************************************************************************
**
**  This file is subject to the terms of the GNU General Public License as
**  published by the Free Software Foundation.  A copy of this license is
**  included with this software distribution in the file COPYING.  If you
**  do not have a copy, you may obtain a copy by writing to the Free
**  Software Foundation, 51 Franklin St, Fifth Floor, Boston, MA
**  02110-1301, USA.
**
**  This software is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details
******************************************************************************
** CVS Log
**
** Revision 1.10  2006/12/21 09:54:45  dgp85
** Apply the textrel patch from Gentoo, thanks to PaX team for providing it. The patch was applied and tested for a while in Gentoo and Pardus, and solves also Debian's problems with non-PIC code. If problems will arise, they'll be debugged.
**
** Revision 1.9  2006/02/04 14:06:29  miguelfreitas
** Enable AMD64 mmx/sse support in some plugins (tvtime, libmpeg2, goom...)
** patch by dani3l
**
** Revision 1.8  2005/06/05 16:00:06  miguelfreitas
** quite some hacks for gcc 2.95 compatibility
**
** Revision 1.7  2004/04/09 02:57:06  miguelfreitas
** tvtime deinterlacing algorithms assumed top_field_first=1
** top_field_first=0 (aka bottom_field_first) should now work as expected
**
** Revision 1.6  2004/02/12 20:53:31  mroi
** my gcc (partly 3.4 already) optimizes these away, because they are only used
** inside inline assembler (which the compiler does not recognize); so actually
** the code is wrong (the asm parts should list these as inputs), but telling
** the compiler to keep them is the easier fix
**
** Revision 1.5  2004/01/05 12:15:55  siggi
** wonder why Mike isn't complaining about C++ style comments, any more...
**
** Revision 1.4  2004/01/05 01:47:26  tmmm
** DOS/Win CRs are forbidden, verboten, interdit
**
** Revision 1.3  2004/01/02 20:53:43  miguelfreitas
** better MANGLE from ffmpeg
**
** Revision 1.2  2004/01/02 20:47:03  miguelfreitas
** my small contribution to the cygwin port ;-)
**
** Revision 1.1  2003/06/22 17:30:03  miguelfreitas
** use our own port of greedy2frame (tvtime port is currently broken)
**
** Revision 1.8  2001/11/23 17:18:54  adcockj
** Fixed silly and/or confusion
**
** Revision 1.7  2001/11/22 22:27:00  adcockj
** Bug Fixes
**
** Revision 1.6  2001/11/21 15:21:40  adcockj
** Renamed DEINTERLACE_INFO to TDeinterlaceInfo in line with standards
** Changed TDeinterlaceInfo structure to have history of pictures.
**
** Revision 1.5  2001/07/31 06:48:33  adcockj
** Fixed index bug spotted by Peter Gubanov
**
** Revision 1.4  2001/07/13 16:13:33  adcockj
** Added CVS tags and removed tabs
**
*****************************************************************************/

/*
 * This is the implementation of the Greedy 2-frame deinterlace algorithm
 * described in DI_Greedy2Frame.c.  It's in a separate file so we can compile
 * variants for different CPU types; most of the code is the same in the
 * different variants.
 */


/****************************************************************************
** Field 1 | Field 2 | Field 3 | Field 4 |
**   T0    |         |    T1   |         |
**         |   M0    |         |    M1   |
**   B0    |         |    B1   |         |
*/

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static const sse_t Mask128 = { uq: { 0x7f7f7f7f7f7f7f7fll, 0x7f7f7f7f7f7f7f7fll} };
#define TP GREEDYTWOFRAMETHRESHOLD, GREEDYTWOFRAMETHRESHOLD2
static const sse_t GreedyTwoFrameThreshold128 = { ub: {TP, TP, TP, TP, TP, TP, TP, TP} };
#undef TP
#endif

static void DeinterlaceGreedy2Frame_SSE2(uint8_t *output, int outstride,
                                         deinterlace_frame_data_t *data,
                                         int bottom_field, int second_field,
                                         int width, int height )
{
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    int Line;
    int stride = width * 2;
    register uint8_t* M1;
    register uint8_t* M0;
    register uint8_t* T1;
    register uint8_t* T0;
    uint8_t* Dest = output;
    register uint8_t* Dest2;
    register uint8_t* Destc;
    register int count;
    uint32_t Pitch = stride * 2;
    uint32_t LineLength = stride;
    uint32_t PitchRest = Pitch - (LineLength >> 4)*16;

    if( second_field ) {
        M1 = data->f0;
        T1 = data->f0;
        M0 = data->f1;
        T0 = data->f1;
    } else {
        M1 = data->f0;
        T1 = data->f1;
        M0 = data->f1;
        T0 = data->f2;
    }

    if( bottom_field ) {
        M1 += stride;
        T1 += 0;
        M0 += stride;
        T0 += 0;
    } else {
        M1 += Pitch;
        T1 += stride;
        M0 += Pitch;
        T0 += stride;

        xine_fast_memcpy(Dest, M1, LineLength);
        Dest += outstride;
    }

    for (Line = 0; Line < (height / 2) - 1; ++Line)
    {
      /* Always use the most recent data verbatim.  By definition it's correct
       * (it'd be shown on an interlaced display) and our job is to fill in
       * the spaces between the new lines.
       */
      /* xine_fast_memcpy would be pretty pointless here as we load the same
       * data anyway it's just one additional mov per loop...
       * XXX I believe some cpus with sse2 (early A64?) only have one write
       * buffer. Using movntdq with 2 different streams may have quite
       * bad performance consequences on such cpus.
       */

        Destc = Dest;
        Dest += outstride;
        Dest2 = Dest;

        /* just rely on gcc not using xmm regs... */
        do {
          asm volatile(
            "movdqa  %0, %%xmm6			\n\t"     // xmm6 = Mask
            "pxor    %%xmm7, %%xmm7		\n\t"     // xmm7 = zero
            : /* no output */
            : "m" (Mask128) );
        } while (0);

        count = LineLength >> 4;
        do {
          asm volatile(
       /* Figure out what to do with the scanline above the one we copy.
        * See above for a description of the algorithm.
        * weave if (weave(M) AND (weave(T) OR weave(B)))
        */
            "movdqa  (%2), %%xmm1		\n\t" /* xmm1 = T1 */
            "movdqa  (%3), %%xmm0		\n\t" /* xmm0 = T0 */
            "movdqa  (%q4,%2), %%xmm3		\n\t" /* xmm3 = B1 */
            "movdqa  (%q4,%3), %%xmm2		\n\t" /* xmm2 = B0 */

            /* calculate |T1-T0| keep T1 put result in xmm5 */
            "movdqa  %%xmm1, %%xmm5		\n\t"
            "psubusb %%xmm0, %%xmm5		\n\t"
            "psubusb %%xmm1, %%xmm0		\n\t"
            "por     %%xmm0, %%xmm5		\n\t"

            /* T1 is data for line to copy */
            "movntdq  %%xmm1, %1		\n\t"

            /* if |T1-T0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%xmm5			\n\t"
            "pand    %%xmm6, %%xmm5		\n\t"
            "pcmpgtb %0, %%xmm5			\n\t"
            "pcmpeqd %%xmm7, %%xmm5		\n\t"

            "prefetcht0  64(%q4,%2)		\n\t"
            "prefetcht0  64(%q4,%3)		\n\t"
          :
          : "m" (GreedyTwoFrameThreshold128),
            "m" (*Destc), "r" (T1), "r" (T0), "r" (Pitch) );

          asm volatile (
            /* calculate |B1-B0| keep B1 put result in xmm4 */
            "movdqa  %%xmm3, %%xmm4		\n\t"
            "psubusb %%xmm2, %%xmm4		\n\t"
            "psubusb %%xmm3, %%xmm2		\n\t"
            "por     %%xmm2, %%xmm4		\n\t"

            "movdqa  (%0), %%xmm0		\n\t" /* xmm0 = M1 */
            "movdqa  (%1), %%xmm2		\n\t" /* xmm2 = M0 */

            /* if |B1-B0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%xmm4			\n\t"
            "pand    %%xmm6, %%xmm4		\n\t"
            "pcmpgtb %2, %%xmm4			\n\t"
            "pcmpeqd %%xmm7, %%xmm4		\n\t"

            "por     %%xmm4, %%xmm5		\n\t"

            /* Average T1 and B1 so we can do interpolated bobbing if we bob
             * onto T1 */
            "pavgb   %%xmm3, %%xmm1		\n\t" /* xmm1 = avg(T1,B1) */

            "prefetcht0  64(%0)			\n\t"
            "prefetcht0  64(%1)			\n\t"

            /* make mm0 the average of M1 and M0 which should make weave
             * look better when there is small amounts of movement */
            "movdqa  %%xmm2, %%xmm3		\n\t"
            "pavgb   %%xmm0, %%xmm3		\n\t" /* xmm3 = avg(M1,M0) */

            /* calculate |M1-M0| put result in xmm4 */
            "movdqa  %%xmm0, %%xmm4		\n\t"
            "psubusb %%xmm2, %%xmm4		\n\t"
            "psubusb %%xmm0, %%xmm2		\n\t"
            "por     %%xmm2, %%xmm4		\n\t"

            /* if |M1-M0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%xmm4			\n\t"
            "pand    %%xmm6, %%xmm4		\n\t"
            "pcmpgtb %2, %%xmm4			\n\t"
            "pcmpeqd %%xmm7, %%xmm4		\n\t" /* do we want to bob */

            "pand   %%xmm5, %%xmm4		\n\t"

/* debugging feature
 * output the value of xmm4 at this point which is pink where we will weave
 * and green where we are going to bob
 */
#ifdef CHECK_BOBWEAVE
            "movntdq  %%xmm4, %3		\n\t"
#else
            /* xmm4 now is 1 where we want to weave and 0 where we want to bob */
            "pand    %%xmm4, %%xmm3		\n\t"
            "pandn   %%xmm1, %%xmm4		\n\t"
            "por     %%xmm3, %%xmm4		\n\t"
            "movntdq  %%xmm4, %3		\n\t"
#endif
          :
          : "r" (M1), "r" (M0), "m" (GreedyTwoFrameThreshold128),
            "m" (*Dest2));

          /* Advance to the next set of pixels. */
          T1 += 16;
          M1 += 16;
          M0 += 16;
          T0 += 16;
          Dest2 += 16;
          Destc += 16;

        } while( --count );

        Dest += outstride;

        M1 += PitchRest;
        T1 += PitchRest;
        M0 += PitchRest;
        T0 += PitchRest;
    }

    asm("sfence\n\t");

    if( bottom_field )
    {
        xine_fast_memcpy(Dest, T1, stride);
        Dest += outstride;
        xine_fast_memcpy(Dest, M1, stride);
    }
    else
    {
        xine_fast_memcpy(Dest, T1, stride);
    }
#endif
}

