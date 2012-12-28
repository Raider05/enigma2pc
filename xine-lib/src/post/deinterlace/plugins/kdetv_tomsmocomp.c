/**
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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
#include <stdlib.h>
#include <string.h>

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include <xine/attributes.h>
#include <xine/xineutils.h>
#include "deinterlace.h"
#include "speedtools.h"
#include "speedy.h"
#include "plugins.h"

#if defined (ARCH_X86) || defined (ARCH_X86_64)

static int Fieldcopy(void *dest, const void *src, size_t count,
                     int rows, int dst_pitch, int src_pitch)
{
   unsigned char* pDest = (unsigned char*) dest;
   unsigned char* pSrc = (unsigned char*) src;
   int i;

   for (i=0; i < rows; i++) {
       xine_fast_memcpy(pDest, pSrc, count);
       pSrc += src_pitch;
       pDest += dst_pitch;
   }
   return 0;
}


#include "tomsmocomp/tomsmocompmacros.h"
#include "x86-64_macros.inc"

#define SearchEffortDefault 5
#define UseStrangeBobDefault 0

static long SearchEffort=SearchEffortDefault;
static int UseStrangeBob=UseStrangeBobDefault;


#define IS_MMX
#define SSE_TYPE MMX
#define FUNCT_NAME tomsmocomp_filter_mmx
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_MMX
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_3DNOW
#define SSE_TYPE 3DNOW
#define FUNCT_NAME tomsmocomp_filter_3dnow
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_3DNOW
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_SSE
#define SSE_TYPE SSE
#define FUNCT_NAME tomsmocomp_filter_sse
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_SSE
#undef  SSE_TYPE
#undef  FUNCT_NAME

#endif

static void deinterlace_frame_di_tomsmocomp( uint8_t *output, int outstride,
                                             deinterlace_frame_data_t *data,
                                             int bottom_field, int second_field,
                                             int width, int height )
{
#if defined (ARCH_X86) || defined (ARCH_X86_64)

    if( xine_mm_accel() & MM_ACCEL_X86_MMXEXT ) {
        tomsmocomp_filter_sse( output, outstride, data,
                               bottom_field, second_field,
                               width, height );
    } else if( xine_mm_accel() & MM_ACCEL_X86_3DNOW ) {
        tomsmocomp_filter_3dnow( output, outstride, data,
                                 bottom_field, second_field,
                                 width, height );
    } else {
        tomsmocomp_filter_mmx( output, outstride, data,
                               bottom_field, second_field,
                               width, height );
    }

#endif
}

static deinterlace_method_t tomsmocompmethod =
{
    "Tom's Motion Compensated (DScaler)",
    "TomsMoComp",
    /*
    "Motion Adaptive: Motion Search",
    "AdaptiveSearch",
    */
    4,
    MM_ACCEL_X86_MMX,
    0,
    0,
    0,
    0,
    deinterlace_frame_di_tomsmocomp,
    0,
    "Uses heuristics to detect motion in the input frames and reconstruct "
    "image detail where possible.  Use this for high quality output even "
    "on monitors set to an arbitrary refresh rate.\n"
    "\n"
    "Motion search mode finds and follows motion vectors for accurate "
    "interpolation.  This is the TomsMoComp deinterlacer from DScaler."
};

deinterlace_method_t *dscaler_tomsmocomp_get_method( void )
{
    return &tomsmocompmethod;
}

