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

#include "greedyhmacros.h"

#define MAXCOMB_DEFAULT          5
#define MOTIONTHRESHOLD_DEFAULT 25
#define MOTIONSENSE_DEFAULT     30

static unsigned int GreedyMaxComb = MAXCOMB_DEFAULT;
static unsigned int GreedyMotionThreshold = MOTIONTHRESHOLD_DEFAULT;
static unsigned int GreedyMotionSense = MOTIONSENSE_DEFAULT;


#define IS_SSE
#define SSE_TYPE SSE
#define FUNCT_NAME greedyh_filter_sse
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_SSE
#undef FUNCT_NAME

#define IS_3DNOW
#define FUNCT_NAME greedyh_filter_3dnow
#define SSE_TYPE 3DNOW
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_3DNOW
#undef FUNCT_NAME

#define IS_MMX
#define SSE_TYPE MMX
#define FUNCT_NAME greedyh_filter_mmx
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_MMX
#undef FUNCT_NAME

#endif

static void deinterlace_frame_di_greedyh( uint8_t *output, int outstride,
                                          deinterlace_frame_data_t *data,
                                          int bottom_field, int second_field,
                                          int width, int height )
{
#if defined (ARCH_X86) || defined (ARCH_X86_64)
    if( xine_mm_accel() & MM_ACCEL_X86_MMXEXT ) {
        greedyh_filter_sse( output, outstride, data,
                            bottom_field, second_field,
                            width, height );
    } else if( xine_mm_accel() & MM_ACCEL_X86_3DNOW ) {
        greedyh_filter_3dnow( output, outstride, data,
                              bottom_field, second_field,
                              width, height );
    } else {
        greedyh_filter_mmx( output, outstride, data,
                            bottom_field, second_field,
                            width, height );
    }
#endif
}


static deinterlace_method_t greedymethod =
{
    "Greedy - High Motion (DScaler)",
    "GreedyH",
    /*
    "Motion Adaptive: Advanced Detection",
    "AdaptiveAdvanced",
    */
    4,
    MM_ACCEL_X86_MMX,
    0,
    0,
    0,
    0,
    deinterlace_frame_di_greedyh,
    0,
    "Uses heuristics to detect motion in the input frames and reconstruct "
    "image detail where possible.  Use this for high quality output even "
    "on monitors set to an arbitrary refresh rate.\n"
    "\n"
    "Advanced detection uses linear interpolation where motion is "
    "detected, using a four-field buffer.  This is the Greedy: High Motion "
    "deinterlacer from DScaler."
};

deinterlace_method_t *dscaler_greedyh_get_method( void )
{
    return &greedymethod;
}

