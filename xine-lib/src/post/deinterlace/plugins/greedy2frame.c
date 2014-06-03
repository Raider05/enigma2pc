/**
 * Copyright (c) 2000 John Adcock, Tom Barry, Steve Grimm  All rights reserved.
 * port copyright (c) 2003 Miguel Freitas
 *
 * This code is ported from DScaler: http://deinterlace.sf.net/
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
#include "deinterlace.h"
#include "speedtools.h"
#include "speedy.h"
#include "plugins.h"

// debugging feature
// output the value of mm4 at this point which is pink where we will weave
// and green were we are going to bob
// uncomment next line to see this
//#define CHECK_BOBWEAVE

#define GREEDYTWOFRAMETHRESHOLD 4
#define GREEDYTWOFRAMETHRESHOLD2 8

#define IS_MMXEXT 1
#include "greedy2frame_template.c"
#undef IS_MMXEXT

#include "greedy2frame_template_sse2.c"

static void DeinterlaceGreedy2Frame(uint8_t *output, int outstride,
                                    deinterlace_frame_data_t *data,
                                    int bottom_field, int second_field, int width, int height )

{
#if defined(ARCH_X86) || defined(ARCH_X86_64)

    if (xine_mm_accel() & MM_ACCEL_X86_SSE2) {
        if (((uintptr_t)output & 15) || (outstride & 15) ||
            width & 7 ||
            ((uintptr_t)data->f0 & 15) || ((uintptr_t)data->f1 & 15)) {
            /*
             * instead of using an unaligned sse2 version just fall back to mmx
             * which has no alignment restriction (though might be slow unaliged,
             * but shouldn't hit this hopefully anyway). Plus in my experiments this
             * was at least as fast as a naive unaligned sse2 version anyway (due to
             * the inability to use streaming stores).
             */
            DeinterlaceGreedy2Frame_MMXEXT(output, outstride, data,
                                           bottom_field, second_field, width, height );
        } else {
            DeinterlaceGreedy2Frame_SSE2(output, outstride, data,
                                         bottom_field, second_field, width, height );
        }
    }
    else {
        DeinterlaceGreedy2Frame_MMXEXT(output, outstride, data,
                                       bottom_field, second_field, width, height );
        /* could fall back to 3dnow/mmx here too */
    }
#endif /*ARCH_X86 */
}


static deinterlace_method_t greedy2framemethod =
{
    "Greedy 2-frame (DScaler)",
    "Greedy2Frame",
    4,
    MM_ACCEL_X86_MMXEXT,
    0,
    0,
    0,
    0,
    DeinterlaceGreedy2Frame,
    1,
    NULL
};

deinterlace_method_t *greedy2frame_get_method( void )
{
    return &greedy2framemethod;
}

