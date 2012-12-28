/*
 * Copyright (C) 2002-2003 the xine project
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
 * libmms public header
 */

#ifndef HAVE_MMS_H
#define HAVE_MMS_H

#include <inttypes.h>
#include <xine/xine_internal.h>

typedef struct mms_s mms_t;

char*    mms_connect_common(int *s ,int *port, char *url, char **host, char **path, char **file);
mms_t*   mms_connect (xine_stream_t *stream, const char *url_, int bandwidth);

int      mms_read (mms_t *this, char *data, int len);
uint32_t mms_get_length (mms_t *this);
void     mms_close (mms_t *this);

size_t   mms_peek_header (mms_t *this, char *data, size_t maxsize);

off_t    mms_get_current_pos (mms_t *this);

void     mms_set_start_time (mms_t *this, int time_offset);

#endif

