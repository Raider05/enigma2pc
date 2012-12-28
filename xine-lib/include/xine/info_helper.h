/*
 * Copyright (C) 2000-2003 the xine project
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
 * stream metainfo helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 *
 * $id$
 */

#ifndef INFO_HELPER_H
#define INFO_HELPER_H

#include <stdarg.h>
#include "xine_internal.h"

/*
 * set a stream info
 *
 * params:
 *  *stream        the xine stream
 *   info          stream info id (see xine.h, XINE_STREAM_INFO_*)
 *   value         the value to assign
 *
 */
void _x_stream_info_set(xine_stream_t *stream, int info, int value) XINE_PROTECTED;

/*
 * reset a stream info (internal ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_STREAM_INFO_*)
 *
 */
void _x_stream_info_reset(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * reset a stream info (public ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_STREAM_INFO_*)
 *
 */
void _x_stream_info_public_reset(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * retrieve stream info (internal ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_STREAM_INFO_*)
 *
 */
uint32_t _x_stream_info_get(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * retrieve stream info (public ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_STREAM_INFO_*)
 *
 */
uint32_t _x_stream_info_get_public(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * set a stream meta info
 *
 * params:
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *str           null-terminated string (using current locale)
 *
 */
void _x_meta_info_set(xine_stream_t *stream, int info, const char *str) XINE_PROTECTED;

/*
 * set a stream meta info
 *
 * params:
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *str           null-terminated string (using utf8)
 *
 */
void _x_meta_info_set_utf8(xine_stream_t *stream, int info, const char *str) XINE_PROTECTED;

/*
 * set a stream meta info
 *
 * params:
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *str           null-terminated string (using encoding below)
 *  *enc           charset encoding of the string
 *
 */
void _x_meta_info_set_generic(xine_stream_t *stream, int info, const char *str, const char *enc) XINE_PROTECTED;

/*
 * set a stream meta multiple info
 *
 * params:
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *   ...           one or more meta info, followed by a NULL pointer
 *
 */
void _x_meta_info_set_multi(xine_stream_t *stream, int info, ...) XINE_SENTINEL XINE_PROTECTED;

/*
 * set a stream meta info
 *
 * params:
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *buf           char buffer (not a null-terminated string)
 *   len           length of the metainfo
 *
 */
void _x_meta_info_n_set(xine_stream_t *stream, int info, const char *buf, int len) XINE_PROTECTED;

/*
 * reset a stream meta info (internal ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *
 */
void _x_meta_info_reset(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * reset a stream meta info (public ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *
 */
void _x_meta_info_public_reset(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * retrieve stream meta info (internal ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *
 */
const char *_x_meta_info_get(xine_stream_t *stream, int info) XINE_PROTECTED;

/*
 * retrieve stream meta info (public ones only)
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *
 */
const char *_x_meta_info_get_public(xine_stream_t *stream, int info) XINE_PROTECTED;

#endif /* INFO_HELPER_H */
