/**
 * Line doubler deinterlacing plugin.
 *
 * Copyright (C) 2002 Billy Biggs <vektor@dumbterm.net>.
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

#include "speedy.h"
#include "deinterlace.h"
#include "plugins.h"

static void deinterlace_scanline_double( uint8_t *output,
                                         deinterlace_scanline_data_t *data,
                                         int width )
{
    blit_packed422_scanline( output, data->t0, width );
}

static void copy_scanline( uint8_t *output,
                           deinterlace_scanline_data_t *data,
                           int width )
{
    blit_packed422_scanline( output, data->m0, width );
}


static deinterlace_method_t doublemethod =
{
    "Line Doubler",
    "LineDoubler",
    1,
    0,
    0,
    1,
    deinterlace_scanline_double,
    copy_scanline,
    0,
    0,
    NULL
};

deinterlace_method_t *double_get_method( void )
{
    return &doublemethod;
}
