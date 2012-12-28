/**
 * Copyright (C) 2002 Billy Biggs <vektor@dumbterm.net>.
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

#include "speedy.h"
#include "deinterlace.h"
#include "plugins.h"

static void deinterlace_scanline_linear( uint8_t *output,
                                         deinterlace_scanline_data_t *data,
                                         int width )
{
    interpolate_packed422_scanline( output, data->t0, data->b0, width );
}

static void copy_scanline( uint8_t *output,
                           deinterlace_scanline_data_t *data,
                           int width )
{
    blit_packed422_scanline( output, data->m0, width );
}


static deinterlace_method_t linearmethod =
{
    "Linear Interpolation",
    "Linear",
/*
    "Television: Full resolution",
    "TelevisionFull",
*/
    1,
    0,
    0,
    1,
    deinterlace_scanline_linear,
    copy_scanline,
    0,
    0,
    "Expands each field independently without blurring or copying in time.  "
    "Use this if you want TV-quality with low CPU, and you have configured "
    "your monitor to run at the refresh rate of the video signal.\n"
    "\n"
    "Full resolution mode expands each field to full size for high quality "
    "fullscreen use."
};

deinterlace_method_t *linear_get_method( void )
{
    return &linearmethod;
}

