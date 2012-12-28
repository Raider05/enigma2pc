/**
 * Copyright (C) 2002, 2004 Billy Biggs <vektor@dumbterm.net>.
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

#ifndef TVTIME_PLUGINS_H_INCLUDED
#define TVTIME_PLUGINS_H_INCLUDED

/**
 * tvtime was going to have a plugin system, but there
 * was never any interest in it outside of tvtime, so instead
 * we include all deinterlacer methods right in the tvtime
 * executable.
 */

#include <deinterlace.h>

deinterlace_method_t *greedy_get_method( void );
deinterlace_method_t *greedy2frame_get_method( void );
deinterlace_method_t *weave_get_method( void );
deinterlace_method_t *double_get_method( void );
deinterlace_method_t *linear_get_method( void );
deinterlace_method_t *scalerbob_get_method( void );
deinterlace_method_t *linearblend_get_method( void );
deinterlace_method_t *vfir_get_method( void );
deinterlace_method_t *dscaler_tomsmocomp_get_method( void );
deinterlace_method_t *dscaler_greedyh_get_method( void );
deinterlace_method_t *greedy_get_method( void );
deinterlace_method_t *weave_get_method( void );
deinterlace_method_t *weavetff_get_method( void );
deinterlace_method_t *weavebff_get_method( void );

#endif /* TVTIME_PLUGINS_H_INCLUDED */
