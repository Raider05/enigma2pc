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
#if !defined(MASKS_DEFINED)
#define MASKS_DEFINED
static const mmx_t Mask = { uq: 0x7f7f7f7f7f7f7f7fll };
#define TP GREEDYTWOFRAMETHRESHOLD, GREEDYTWOFRAMETHRESHOLD2
static const mmx_t GreedyTwoFrameThreshold = { ub: {TP, TP, TP, TP} };
#undef TP
#endif
#endif

#if defined(IS_MMXEXT)
static void DeinterlaceGreedy2Frame_MMXEXT(uint8_t *output, int outstride,
                                 deinterlace_frame_data_t *data,
                                 int bottom_field, int second_field, int width, int height )
#elif defined(IS_3DNOW)
static void DeinterlaceGreedy2Frame_3DNOW(uint8_t *output, int outstride,
                                   deinterlace_frame_data_t *data,
                                   int bottom_field, int second_field, int width, int height )
#else
static void DeinterlaceGreedy2Frame_MMX(uint8_t *output, int outstride,
                                 deinterlace_frame_data_t *data,
                                 int bottom_field, int second_field, int width, int height )
#endif
{
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    int Line;
    int stride = width * 2;
    register uint8_t* M1;
    register uint8_t* M0;
    register uint8_t* T0;
    register uint8_t* T1;
    register uint8_t* B1;
    register uint8_t* B0;
    uint8_t* Dest = output;
    register uint8_t* Dest2;
    register int count;
    uint32_t Pitch = stride*2;
    uint32_t LineLength = stride;
    uint32_t PitchRest = Pitch - (LineLength >> 3)*8;

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
        B1 = T1 + Pitch;
        M0 += stride;
        T0 += 0;
        B0 = T0 + Pitch;
    } else {
        M1 += Pitch;
        T1 += stride;
        B1 = T1 + Pitch;
        M0 += Pitch;
        T0 += stride;
        B0 = T0 + Pitch;

        xine_fast_memcpy(Dest, M1, LineLength);
        Dest += outstride;
    }

    for (Line = 0; Line < (height / 2) - 1; ++Line)
    {
      /* Always use the most recent data verbatim.  By definition it's correct
       * (it'd be shown on an interlaced display) and our job is to fill in
       * the spaces between the new lines.
       */
        xine_fast_memcpy(Dest, T1, stride);
        Dest += outstride;
        Dest2 = Dest;

        count = LineLength >> 3;
        do {
          asm volatile(
       /* Figure out what to do with the scanline above the one we just copied.
        * See above for a description of the algorithm.
        * weave if (weave(M) AND (weave(T) OR weave(B)))
        */
            "movq %0, %%mm1			\n\t"     // T1
            "movq %1, %%mm0			\n\t"     // M1
            "movq %2, %%mm3			\n\t"     // B1
            "movq %3, %%mm2			\n\t"     // M0

            "movq %4, %%mm6			\n\t"     // Mask

            : /* no output */
            : "m" (*T1), "m" (*M1),
              "m" (*B1), "m" (*M0), "m" (Mask) );


          asm volatile(
       /* Figure out what to do with the scanline above the one we just copied.
        * See above for a description of the algorithm.
        * Average T1 and B1 so we can do interpolated bobbing if we bob onto T1
	*/
	    "movq %%mm3, %%mm7			\n\t" /* mm7 = B1 */

#if defined(IS_MMXEXT)
            "pavgb %%mm1, %%mm7			\n\t"
#elif defined(IS_3DNOW)
            "pavgusb %%mm1, %%mm7		\n\t"
#else

            "movq %%mm1, %%mm5			\n\t" /* mm5 = T1            */
            "psrlw $1, %%mm7			\n\t" /* mm7 = B1 / 2        */
            "pand %%mm6, %%mm7			\n\t" /* mask off lower bits */
            "psrlw $1, %%mm5			\n\t" /* mm5 = T1 / 2        */
            "pand %%mm6, %%mm5			\n\t" /* mask off lower bits */
            "paddw %%mm5, %%mm7			\n\t" /* mm7 = (T1 + B1) / 2 */
#endif

	 /* calculate |M1-M0| put result in mm4 need to keep mm0 intact
	  * if we have a good processor then make mm0 the average of M1 and M0
	  * which should make weave look better when there is small amounts of
	  * movement
	  */
#if defined(IS_MMXEXT)
            "movq    %%mm0, %%mm4		\n\t"
            "movq    %%mm2, %%mm5		\n\t"
            "psubusb %%mm2, %%mm4		\n\t"
            "psubusb %%mm0, %%mm5		\n\t"
            "por     %%mm5, %%mm4		\n\t"
            "pavgb   %%mm2, %%mm0		\n\t"
#elif defined(IS_3DNOW)
            "movq    %%mm0, %%mm4		\n\t"
            "movq    %%mm2, %%mm5		\n\t"
            "psubusb %%mm2, %%mm4		\n\t"
            "psubusb %%mm0, %%mm5		\n\t"
            "por     %%mm5, %%mm4		\n\t"
            "pavgusb %%mm2, %%mm0		\n\t"
#else
            "movq    %%mm0, %%mm4		\n\t"
            "psubusb %%mm2, %%mm4		\n\t"
            "psubusb %%mm0, %%mm2		\n\t"
            "por     %%mm2, %%mm4		\n\t"
#endif

            "movq    %1, %%mm2			\n\t" /* mm2 = T0 */

            /* if |M1-M0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%mm4			\n\t"
            "pand    %%mm6, %%mm4		\n\t"
            "pxor    %%mm5, %%mm5		\n\t" // zero
            "pcmpgtb %3, %%mm4			\n\t"
            "pcmpeqd %%mm5, %%mm4		\n\t" /* do we want to bob */

            /* calculate |T1-T0| put result in mm5 */
            "movq    %%mm2, %%mm5		\n\t"
            "psubusb %%mm1, %%mm5		\n\t"
            "psubusb %%mm2, %%mm1		\n\t"
            "por     %%mm1, %%mm5		\n\t"

            "movq    %2, %%mm2			\n\t" /* mm2 = B0 */

            /* if |T1-T0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%mm5			\n\t"
            "pand    %%mm6, %%mm5		\n\t"
            "pxor    %%mm1, %%mm1		\n\t" // zero
            "pcmpgtb %3, %%mm5			\n\t"
            "pcmpeqd %%mm1, %%mm5		\n\t"

            /* calculate |B1-B0| put result in mm1 */
            "movq    %%mm2, %%mm1		\n\t"
            "psubusb %%mm3, %%mm1		\n\t"
            "psubusb %%mm2, %%mm3		\n\t"
            "por     %%mm3, %%mm1		\n\t"

            /* if |B1-B0| > Threshold we want 0 else dword minus one */
            "psrlw   $1, %%mm1			\n\t"
            "pand    %%mm6, %%mm1		\n\t"
            "pxor    %%mm3, %%mm3		\n\t" // zero
            "pcmpgtb %3, %%mm1			\n\t"
            "pcmpeqd %%mm3, %%mm1		\n\t"

            "por     %%mm1, %%mm5		\n\t"
            "pand    %%mm5, %%mm4		\n\t"

/* debugging feature
 * output the value of mm4 at this point which is pink where we will weave
 * and green where we are going to bob
 */
#ifdef CHECK_BOBWEAVE
#ifdef IS_MMXEXT
            "movntq %%mm4, %0			\n\t"
#else
            "movq %%mm4, %0			\n\t"
#endif
#else

            /* mm4 now is 1 where we want to weave and 0 where we want to bob */
            "pand    %%mm4, %%mm0		\n\t"
            "pandn   %%mm7, %%mm4		\n\t"
            "por     %%mm0, %%mm4		\n\t"
#ifdef IS_MMXEXT
            "movntq %%mm4, %0			\n\t"
#else
            "movq %%mm4, %0			\n\t"
#endif
#endif

          : "=m" (*Dest2)
          : "m" (*T0), "m" (*B0), "m" (GreedyTwoFrameThreshold) );

          /* Advance to the next set of pixels. */
          T1 += 8;
          M1 += 8;
          B1 += 8;
          M0 += 8;
          T0 += 8;
          B0 += 8;
          Dest2 += 8;

        } while( --count );

        Dest += outstride;

        M1 += PitchRest;
        T1 += PitchRest;
        B1 += PitchRest;
        M0 += PitchRest;
        T0 += PitchRest;
        B0 += PitchRest;
    }

#ifdef IS_MMXEXT
    asm("sfence\n\t");
#endif

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

    /* clear out the MMX registers ready for doing floating point again */
    asm("emms\n\t");
#endif
}

