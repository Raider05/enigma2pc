/**
 * Copyright (c) 2001, 2002, 2003 Billy Biggs <vektor@dumbterm.net>.
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

#ifndef TVTIME_H_INCLUDED
#define TVTIME_H_INCLUDED

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include "deinterlace.h"

/**
 * Which pulldown algorithm we're using.
 */
enum {
    PULLDOWN_NONE = 0,
    PULLDOWN_VEKTOR = 1, /* vektor's adaptive pulldown detection. */
    PULLDOWN_MAX = 2,
};

enum
{
    FRAMERATE_FULL = 0,
    FRAMERATE_HALF_TFF = 1,
    FRAMERATE_HALF_BFF = 2,
    FRAMERATE_MAX = 3
};


typedef struct {
  /**
   * Which pulldown algorithm we're using.
   */
  unsigned int pulldown_alg;

  /**
   * Current deinterlacing method.
   */
  deinterlace_method_t *curmethod;

  /**
   * This is how many frames to wait until deciding if the pulldown phase
   * has changed or if we've really found a pulldown sequence.
   */
  unsigned int pulldown_error_wait;

  /* internal data */
  int last_topdiff;
  int last_botdiff;

  int pdoffset;
  int pderror;
  int pdlastbusted;
  int filmmode;


} tvtime_t;


int tvtime_build_deinterlaced_frame( tvtime_t *this, uint8_t *output,
                                             uint8_t *curframe,
                                             uint8_t *lastframe,
                                             uint8_t *secondlastframe,
                                             int bottom_field, int second_field,
                                             int width,
                                             int frame_height,
                                             int instride,
                                             int outstride );


int tvtime_build_copied_field( tvtime_t *this, uint8_t *output,
                                       uint8_t *curframe,
                                       int bottom_field,
                                       int width,
                                       int frame_height,
                                       int instride,
                                       int outstride );
tvtime_t *tvtime_new_context(void);

void tvtime_reset_context( tvtime_t *this );


#endif
