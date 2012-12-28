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

#ifndef PULLDOWN_H_INCLUDED
#define PULLDOWN_H_INCLUDED

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include "speedy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PULLDOWN_SEQ_AA (1<<0) /* next - prev */
#define PULLDOWN_SEQ_AB (1<<1) /* prev - next */
#define PULLDOWN_SEQ_BC (1<<2) /* prev - next */
#define PULLDOWN_SEQ_CC (1<<3) /* next - prev */
#define PULLDOWN_SEQ_DD (1<<4) /* next - prev */

#define PULLDOWN_ACTION_NEXT_PREV (1<<0) /* next - prev */
#define PULLDOWN_ACTION_PREV_NEXT (1<<1) /* prev - next */

/**
 * Returns 1 if the source is the previous field, 0 if it is
 * the next field, for the given action.
 */
int pulldown_source( int action, int bottom_field );

int determine_pulldown_offset( int top_repeat, int bot_repeat, int tff, int last_offset );
int determine_pulldown_offset_history( int top_repeat, int bot_repeat, int tff, int *realbest );
int determine_pulldown_offset_history_new( int top_repeat, int bot_repeat, int tff, int predicted );
int determine_pulldown_offset_short_history_new( int top_repeat, int bot_repeat, int tff, int predicted );
int determine_pulldown_offset_dalias( pulldown_metrics_t *old_peak, pulldown_metrics_t *old_relative,
                                      pulldown_metrics_t *old_mean, pulldown_metrics_t *new_peak,
                                      pulldown_metrics_t *new_relative, pulldown_metrics_t *new_mean );

void diff_factor_packed422_frame( pulldown_metrics_t *peak, pulldown_metrics_t *rel, pulldown_metrics_t *mean,
                                  uint8_t *old, uint8_t *new, int w, int h, int os, int ns );

int pulldown_drop( int action, int bottom_field );

#ifdef __cplusplus
};
#endif
#endif /* PULLDOWN_H_INCLUDED */
