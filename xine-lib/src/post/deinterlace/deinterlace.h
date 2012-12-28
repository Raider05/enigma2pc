/**
 * Copyright (C) 2002, 2005 Billy Biggs <vektor@dumbterm.net>.
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

#ifndef DEINTERLACE_H_INCLUDED
#define DEINTERLACE_H_INCLUDED

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Our deinterlacer plugin API is modeled after DScaler's.  This module
 * represents the API that all deinterlacer plugins must export, and
 * also provides a registration mechanism for the application to be able
 * to iterate through available plugins and select an appropriate one.
 */

typedef struct deinterlace_method_s deinterlace_method_t;
typedef struct deinterlace_scanline_data_s deinterlace_scanline_data_t;
typedef struct deinterlace_frame_data_s deinterlace_frame_data_t;

/**
 * There are two scanline functions that every deinterlacer plugin
 * must implement to do its work: one for a 'copy' and one for
 * an 'interpolate' for the currently active field.  This so so that
 * while plugins may be delaying fields, the external API assumes that
 * the plugin is completely realtime.
 *
 * Each deinterlacing routine can require data from up to four fields.
 * The most recent field captured is field 0, and increasing numbers go
 * backwards in time.
 */
struct deinterlace_scanline_data_s
{
    uint8_t *tt0, *t0, *m0, *b0, *bb0;
    uint8_t *tt1, *t1, *m1, *b1, *bb1;
    uint8_t *tt2, *t2, *m2, *b2, *bb2;
    uint8_t *tt3, *t3, *m3, *b3, *bb3;
    int bottom_field;
};

/**
 * |   t-3       t-2       t-1       t
 * | Field 3 | Field 2 | Field 1 | Field 0 |
 * |  TT3    |         |   TT1   |         |
 * |         |   T2    |         |   T0    |
 * |   M3    |         |    M1   |         |
 * |         |   B2    |         |   B0    |
 * |  BB3    |         |   BB1   |         |
 *
 * While all pointers are passed in, each plugin is only guarenteed for
 * the ones it indicates it requires (in the fields_required parameter)
 * to be available.
 *
 * Pointers are always to scanlines in the standard packed 4:2:2 format.
 */
typedef void (*deinterlace_interp_scanline_t)( uint8_t *output,
                                               deinterlace_scanline_data_t *data,
                                               int width );
/**
 * For the copy scanline, the API is basically the same, except that
 * we're given a scanline to 'copy'.
 *
 * |   t-3       t-2       t-1       t
 * | Field 3 | Field 2 | Field 1 | Field 0 |
 * |         |   TT2   |         |  TT0    |
 * |   T3    |         |   T1    |         |
 * |         |    M2   |         |   M0    |
 * |   B3    |         |   B1    |         |
 * |         |   BB2   |         |  BB0    |
 */
typedef void (*deinterlace_copy_scanline_t)( uint8_t *output,
                                             deinterlace_scanline_data_t *data,
                                             int width );

/**
 * The frame function is for deinterlacing plugins that can only act
 * on whole frames, rather than on a scanline at a time.
 */
struct deinterlace_frame_data_s
{
    uint8_t *f0;
    uint8_t *f1;
    uint8_t *f2;
    uint8_t *f3;
};

/**
 * Note: second_field is used in xine. For tvtime it should be the same as bottom_field.
 */
typedef void (*deinterlace_frame_t)( uint8_t *output, int outstride,
                                     deinterlace_frame_data_t *data,
                                     int bottom_field, int second_field,
                                     int width, int height );


/**
 * This structure defines the deinterlacer plugin.
 */
struct deinterlace_method_s
{
    const char *name;
    const char *short_name;
    int fields_required;
    int accelrequired;
    int doscalerbob;
    int scanlinemode;
    deinterlace_interp_scanline_t interpolate_scanline;
    deinterlace_copy_scanline_t copy_scanline;
    deinterlace_frame_t deinterlace_frame;
    int delaysfield; /* xine: this method delays output by one field relative to input */
    const char *description;
};

/**
 * Registers a new deinterlace method.
 */
void register_deinterlace_method( deinterlace_method_t *method );

/**
 * Returns how many deinterlacing methods are available.
 */
int get_num_deinterlace_methods( void );

/**
 * Returns the specified method in the list.
 */
deinterlace_method_t *get_deinterlace_method( int i );

/**
 * Builds the usable method list.
 */
void filter_deinterlace_methods( int accel, int fieldsavailable );

#ifdef __cplusplus
};
#endif
#endif /* DEINTERLACE_H_INCLUDED */
