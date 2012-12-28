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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define LOG_MODULE "xine_buffer"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xineutils.h>

#define CHECKS

/*
 * private data structs
 */

typedef struct {

  uint32_t size;
  uint32_t chunk_size;

  uint8_t magic;

} xine_buffer_header_t;

#define XINE_BUFFER_HEADER_SIZE 9
#define XINE_BUFFER_MAGIC 42

/*
 * xine_buffer stores its additional info just in front of
 * the public data pointer:
 *
 * <header 8 bytes> <magic 1 byte> <data>
 *                                  ^public pointer
 *
 * hopefully the magic value will prevent some segfaults,
 * if xine_buffer_* functions are called with user-malloced
 * data chunks...
 */


/*
 * some macros
 */

#define CHECK_MAGIC(x) if (*(((uint8_t *)x)-1)!=XINE_BUFFER_MAGIC) \
  {printf("xine_buffer: FATAL: xine_buffer_header not recognized!\n");exit(1);}

#define GET_HEADER(x) ((xine_buffer_header_t*)(((uint8_t*)x)-XINE_BUFFER_HEADER_SIZE))

/* reallocs buf, if smaller than size. */
#define GROW_TO(buf, to_size) \
    if (GET_HEADER(buf)->size < (to_size)) { \
    int new_size = (to_size) + GET_HEADER(buf)->chunk_size - \
        ((to_size) % GET_HEADER(buf)->chunk_size);\
    \
    buf = ((uint8_t*)realloc(((uint8_t*)buf)-XINE_BUFFER_HEADER_SIZE, new_size+XINE_BUFFER_HEADER_SIZE)) + XINE_BUFFER_HEADER_SIZE;\
    GET_HEADER(buf)->size=new_size; }

/*
 * returns an initialized pointer to a buffer.
 * The buffer will be allocated in blocks of
 * chunk_size bytes. This will prevent permanent
 * reallocation on slow growing buffers.
 */
void *xine_buffer_init(int chunk_size) {

  uint8_t *data=calloc(1, chunk_size+XINE_BUFFER_HEADER_SIZE);
  xine_buffer_header_t *header=(xine_buffer_header_t*)data;

  header->size=chunk_size;
  header->chunk_size=chunk_size;
  header->magic=XINE_BUFFER_MAGIC;

  return data+XINE_BUFFER_HEADER_SIZE;
}

/*
 * frees a buffer, the macro ensures, that a freed
 * buffer pointer is set to NULL
 */
#define xine_buffer_free(buf) buf=_xine_buffer_free(buf)
void *_xine_buffer_free(void *buf) {

#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return NULL;
  }
  CHECK_MAGIC(buf);
#endif

  free(((uint8_t*)buf)-XINE_BUFFER_HEADER_SIZE);

  return NULL;
}

/*
 * duplicates a buffer
 */
void *xine_buffer_dup(const void *buf) {

  uint8_t *new;

#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return NULL;
  }
  CHECK_MAGIC(buf);
#endif

  new = malloc(GET_HEADER(buf)->size+XINE_BUFFER_HEADER_SIZE);

  xine_fast_memcpy(new, ((uint8_t*)buf)-XINE_BUFFER_HEADER_SIZE,
      GET_HEADER(buf)->size+XINE_BUFFER_HEADER_SIZE);

  return new+XINE_BUFFER_HEADER_SIZE;
}

/*
 * will copy len bytes of data into buf at position index.
 */
#define xine_buffer_copyin(buf,i,data,len) \
  buf=_xine_buffer_copyin(buf,i,data,len)
void *_xine_buffer_copyin(void *buf, int index, const void *data, int len) {

#ifdef CHECKS
  if (!buf || !data) {
    lprintf("warning: got NULL pointer\n");
    return NULL;
  }
  CHECK_MAGIC(buf);
#endif

  GROW_TO(buf, (size_t)index+len);

  xine_fast_memcpy(((uint8_t*)buf)+index, data, len);

  return buf;
}

/*
 * will copy len bytes out of buf+index into data.
 * no checks are made in data. It is treated as an ordinary
 * user-malloced data chunk.
 */
void xine_buffer_copyout(const void *buf, int index, void *data, int len) {

#ifdef CHECKS
  if (!buf || !data) {
    lprintf("warning: got NULL pointer\n");
    return;
  }
  CHECK_MAGIC(buf);
#endif

  if (GET_HEADER(buf)->size < index+len)
  {
    lprintf("warning: attempt to read over boundary!\n");
    if (GET_HEADER(buf)->size < index)
      return;
    len = GET_HEADER(buf)->size - index;
  }
  xine_fast_memcpy(data, ((uint8_t*)buf)+index, len);
}

/*
 * set len bytes in buf+index to b.
 */
#define xine_buffer_set(buf,i,b,len) \
  buf=_xine_buffer_set(buf,i,b,len)
void *_xine_buffer_set(void *buf, int index, uint8_t b, int len) {

#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return NULL;
  }
  CHECK_MAGIC(buf);
#endif

  GROW_TO(buf, index+len);

  memset(((uint8_t*)buf)+index, b, len);

  return buf;
}

/*
 * concatenates given buf (which should contain a null terminated string)
 * with another string.
 */
#define xine_buffer_strcat(buf,data) \
  buf=_xine_buffer_strcat(buf,data)
void *_xine_buffer_strcat(void *buf, const char *data) {
  return _xine_buffer_strcpy(buf, strlen(buf), data);
}

/*
 * copies given string to buf+index
 */
#define xine_buffer_strcpy(buf,index,data) \
  buf=_xine_buffer_strcpy(buf,index,data)
void *_xine_buffer_strcpy(void *buf, int index, const char *data) {

#ifdef CHECKS
  if (!buf || !data) {
    lprintf("warning: got NULL pointer\n");
    return NULL;
  }
  CHECK_MAGIC(buf);
#endif

  GROW_TO(buf, index+strlen(data)+1);

  strcpy(((char*)buf)+index, data);

  return buf;
}

/*
 * returns a pointer to the first occurence of needle.
 * note, that the returned pointer cannot be used
 * in any other xine_buffer_* functions.
 */
char *xine_buffer_strchr(const void *buf, int ch) {
#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return 0;
  }
  CHECK_MAGIC(buf);
#endif

  return strchr((const char *)buf, ch);
}

/*
 * get allocated memory size
 */
int xine_buffer_get_size(const void *buf) {

#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return 0;
  }
  CHECK_MAGIC(buf);
#endif

  return GET_HEADER(buf)->size;
}

/*
 * ensures a specified buffer size if the user want to
 * write directly to the buffer. Normally the special
 * access functions defined here should be used.
 */
#define xine_buffer_ensure_size(buf,data) \
  buf=_xine_buffer_ensure_size(buf,data)
void *_xine_buffer_ensure_size(void *buf, int size) {

#ifdef CHECKS
  if (!buf) {
    lprintf("warning: got NULL pointer\n");
    return 0;
  }
  CHECK_MAGIC(buf);
#endif

  GROW_TO(buf, size);

  return buf;
}
