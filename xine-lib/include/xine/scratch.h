/*
 * Copyright (C) 2000-2006 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * scratch buffer for log output
 */

#ifndef HAVE_SCRATCH_H
#define HAVE_SCRATCH_H

#include <stdarg.h>
#include <pthread.h>

typedef struct scratch_buffer_s scratch_buffer_t;

#define SCRATCH_LINE_LEN_MAX  1024

struct scratch_buffer_s {

  void         XINE_FORMAT_PRINTF(2, 0)
               (*scratch_printf) (scratch_buffer_t *self, const char *format, va_list ap);

  char       **(*get_content) (scratch_buffer_t *self);

  void         (*dispose) (scratch_buffer_t *self);

  char         **lines;
  char         **ordered;

  int            num_lines;
  int            cur;

  pthread_mutex_t lock;
};

scratch_buffer_t *_x_new_scratch_buffer (int num_lines) XINE_MALLOC XINE_PROTECTED;

#endif
