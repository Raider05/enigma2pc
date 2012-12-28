/*
 * Copyright (C) 2000-2004 the xine project
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
 *
 * generic dynamic buffer functions. The goals
 * of these functions are (in fact many of these points
 * are todos):
 * - dynamic allocation and reallocation depending
 *   on the size of data written to it.
 * - fast and transparent access to the data.
 *   The user sees only the raw data chunk as it is
 *   returned by the well-known malloc function.
 *   This is necessary since not all data-accessing
 *   functions can be wrapped here.
 * - some additional health checks are made during
 *   development (eg boundary checks after direct
 *   access to a buffer). This can be turned off in
 *   production state for higher performance.
 * - A lot of convenient string and memory manipulation
 *   functions are implemented here, where the user
 *   do not have to care about memory chunk sizes.
 * - Some garbage collention could be implemented as well;
 *   i think of a global structure containing infos
 *   about all allocated chunks. This must be implemented
 *   in a thread-save way...
 *
 * Here are some drawbacks (aka policies):
 * - The user must not pass indexed buffers to xine_buffer_*
 *   functions.
 * - The pointers passed to xine_buffer_* functions may change
 *   (eg during reallocation). The user must respect that.
 */

#ifndef HAVE_XINE_BUFFER_H
#define HAVE_XINE_BUFFER_H

#include <xine/os_types.h>

/*
 * returns an initialized pointer to a buffer.
 * The buffer will be allocated in blocks of
 * chunk_size bytes. This will prevent permanent
 * reallocation on slow growing buffers.
 */
void *xine_buffer_init(int chunk_size) XINE_PROTECTED;

/*
 * frees a buffer, the macro ensures, that a freed
 * buffer pointer is set to NULL
 */
#define xine_buffer_free(buf) buf=_xine_buffer_free(buf)
void *_xine_buffer_free(void *buf) XINE_PROTECTED;

/*
 * duplicates a buffer
 */
void *xine_buffer_dup(const void *buf) XINE_PROTECTED;

/*
 * will copy len bytes of data into buf at position index.
 */
#define xine_buffer_copyin(buf,i,data,len) \
  buf=_xine_buffer_copyin(buf,i,data,len)
void *_xine_buffer_copyin(void *buf, int index, const void *data, int len) XINE_PROTECTED;

/*
 * will copy len bytes out of buf+index into data.
 * no checks are made in data. It is treated as an ordinary
 * user-malloced data chunk.
 */
void xine_buffer_copyout(const void *buf, int index, void *data, int len) XINE_PROTECTED;

/*
 * set len bytes in buf+index to b.
 */
#define xine_buffer_set(buf,i,b,len) \
  buf=_xine_buffer_set(buf,i,b,len)
void *_xine_buffer_set(void *buf, int index, uint8_t b, int len) XINE_PROTECTED;

/*
 * concatenates given buf (which should contain a null terminated string)
 * with another string.
 */
#define xine_buffer_strcat(buf,data) \
  buf=_xine_buffer_strcat(buf,data)
void *_xine_buffer_strcat(void *buf, const char *data) XINE_PROTECTED;

/*
 * copies given string to buf+index
 */
#define xine_buffer_strcpy(buf,index,data) \
  buf=_xine_buffer_strcpy(buf,index,data)
void *_xine_buffer_strcpy(void *buf, int index, const char *data) XINE_PROTECTED;

/*
 * returns a pointer to the first occurence of ch.
 * note, that the returned pointer cannot be used
 * in any other xine_buffer_* functions.
 */
char *xine_buffer_strchr(const void *buf, int ch) XINE_PROTECTED;

/*
 * get allocated memory size
 */
int xine_buffer_get_size(const void *buf) XINE_PROTECTED;

/*
 * ensures a specified buffer size if the user want to
 * write directly to the buffer. Normally the special
 * access functions defined here should be used.
 */
#define xine_buffer_ensure_size(buf,data) \
  buf=_xine_buffer_ensure_size(buf,data)
void *_xine_buffer_ensure_size(void *buf, int size) XINE_PROTECTED;

#endif
