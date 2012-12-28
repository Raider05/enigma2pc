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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_ICONV
#  include <iconv.h>
#endif

#define XINE_ENGINE_INTERNAL

#include <xine/info_helper.h>

/* *******************  Stream Info  *************************** */

/*
 * Compare stream_info, private and public values,
 * return 1 if it's identical, otherwirse 0.
 */
static int stream_info_is_identical(xine_stream_t *stream, int info) {

  if(stream->stream_info_public[info] == stream->stream_info[info])
    return 1;

  return 0;
}

/*
 * Check if 'info' is in bounds.
 */
static int info_valid(int info) {
  if ((info >= 0) && (info < XINE_STREAM_INFO_MAX))
    return 1;
  else {
    fprintf(stderr, "Error: invalid STREAM_INFO %d. Ignored.\n", info);
    return 0;
  }
}

static void stream_info_set_unlocked(xine_stream_t *stream, int info, int value) {
  if(info_valid(info))
    stream->stream_info[info] = value;
}

/*
 * Reset private info.
 */
void _x_stream_info_reset(xine_stream_t *stream, int info) {
  pthread_mutex_lock(&stream->info_mutex);
  stream_info_set_unlocked(stream, info, 0);
  pthread_mutex_unlock(&stream->info_mutex);
}

/*
 * Reset public info value.
 */
void _x_stream_info_public_reset(xine_stream_t *stream, int info) {
  pthread_mutex_lock(&stream->info_mutex);
  if(info_valid(info))
    stream->stream_info_public[info] = 0;
  pthread_mutex_unlock(&stream->info_mutex);
}

/*
 * Set private info value.
 */
void _x_stream_info_set(xine_stream_t *stream, int info, int value) {
  pthread_mutex_lock(&stream->info_mutex);
  stream_info_set_unlocked(stream, info, value);
  pthread_mutex_unlock(&stream->info_mutex);
}

/*
 * Retrieve private info value.
 */
uint32_t _x_stream_info_get(xine_stream_t *stream, int info) {
  uint32_t stream_info = 0;

  pthread_mutex_lock(&stream->info_mutex);
  stream_info = stream->stream_info[info];
  pthread_mutex_unlock(&stream->info_mutex);

  return stream_info;
}

/*
 * Retrieve public info value
 */
uint32_t _x_stream_info_get_public(xine_stream_t *stream, int info) {
  uint32_t stream_info = 0;

  pthread_mutex_lock(&stream->info_mutex);
  stream_info = stream->stream_info_public[info];
  if(info_valid(info) && (!stream_info_is_identical(stream, info)))
    stream_info = stream->stream_info_public[info] = stream->stream_info[info];
  pthread_mutex_unlock(&stream->info_mutex);

  return stream_info;
}

/* ****************  Meta Info  *********************** */

/*
 * Remove trailing separator chars (\n,\r,\t, space,...)
 * at the end of the string
 */
static void meta_info_chomp(char *str) {
  ssize_t i, len;

  len = strlen(str);
  if (!len)
    return;
  i = len - 1;

  while ((i >= 0) && ((unsigned char)str[i] <= 32)) {
    str[i] = 0;
    i--;
  }
}

/*
 * Compare stream_info, public and private values,
 * return 1 if it's identical, otherwise 0.
 */
static int meta_info_is_identical(xine_stream_t *stream, int info) {

  if((!(stream->meta_info_public[info] && stream->meta_info[info])) ||
     ((stream->meta_info_public[info] && stream->meta_info[info]) &&
      strcmp(stream->meta_info_public[info], stream->meta_info[info])))
    return 0;

  return 1;
}

/*
 * Check if 'info' is in bounds.
 */
static int meta_valid(int info) {
  if ((info >= 0) && (info < XINE_STREAM_INFO_MAX))
    return 1;
  else {
    fprintf(stderr, "Error: invalid META_INFO %d. Ignored.\n", info);
    return 0;
  }
}

/*
 * Set private meta info to utf-8 string value (can be NULL).
 */
static void meta_info_set_unlocked_utf8(xine_stream_t *stream, int info, const char *value) {
  if(meta_valid(info)) {

    if(stream->meta_info[info])
      free(stream->meta_info[info]);

    stream->meta_info[info] = (value) ? strdup(value) : NULL;

    if(stream->meta_info[info] && strlen(stream->meta_info[info]))
      meta_info_chomp(stream->meta_info[info]);
  }
}

#ifdef HAVE_ICONV
static int meta_info_validate_utf8 (const char *value)
{
  iconv_t cd;
  char *utf8_value;
  ICONV_CONST char *inbuf;
  char *outbuf;
  size_t inbytesleft, outbytesleft;

  if ((cd = iconv_open("UTF-8", "UTF-8")) == (iconv_t)-1) {
    return 0;
  }

  inbuf = (ICONV_CONST char *)value;
  inbytesleft = strlen(value);
  outbytesleft = 4 * inbytesleft; /* estimative (max) */
  outbuf = utf8_value = malloc(outbytesleft+1);

  iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft );
  free(utf8_value);
  iconv_close(cd);

  return (inbytesleft == 0);
}
#endif

/*
 * Set private meta info to value (can be NULL) with a given encoding.
 * if encoding is NULL assume locale.
 */
static void meta_info_set_unlocked_encoding(xine_stream_t *stream, int info, const char *value, const char *enc) {
#ifdef HAVE_ICONV
  iconv_t cd;
  char *system_enc = NULL;

  if (value) {
    if (enc == NULL) {
      if ((enc = system_enc = xine_get_system_encoding()) == NULL) {
        xprintf(stream->xine, XINE_VERBOSITY_LOG,
                _("info_helper: can't find out current locale character set\n"));
      }
    }

    if (enc && strcmp(enc, "UTF-8")) {
      /* Don't bother converting if it's already in UTF-8, but the encoding
       * is badly reported */
      if (meta_info_validate_utf8(value)) {
        meta_info_set_unlocked_utf8(stream, info, value);
	return;
      }
      cd = iconv_open("UTF-8", enc);
      if (cd == (iconv_t)-1)
        xprintf(stream->xine, XINE_VERBOSITY_LOG,
                _("info_helper: unsupported conversion %s -> UTF-8, no conversion performed\n"), enc);

      if (cd != (iconv_t)-1) {
        char *utf8_value;
        ICONV_CONST char *inbuf;
        char *outbuf;
        size_t inbytesleft, outbytesleft;

        inbuf = (ICONV_CONST char *)value;
        if (!strncmp (enc, "UTF-16", 6) || !strncmp (enc, "UCS-2", 5))
        {
          /* strlen() won't work with UTF-16* or UCS-2* */
          inbytesleft = 0;
          while (value[inbytesleft] || value[inbytesleft + 1])
            inbytesleft += 2;
        } /* ... do we need to handle UCS-4? Probably not. */
        else
          inbytesleft = strlen(value);
        outbytesleft = 4 * inbytesleft; /* estimative (max) */
        outbuf = utf8_value = malloc(outbytesleft+1);

        iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft );
        *outbuf = '\0';

        meta_info_set_unlocked_utf8(stream, info, utf8_value);

        free(utf8_value);
        iconv_close(cd);
        return;
      }
    }

    free(system_enc);
  }
#endif

  meta_info_set_unlocked_utf8(stream, info, value);
}

/*
 * Set private meta info to value (can be NULL)
 * value string must be provided with current locale encoding.
 */
static void meta_info_set_unlocked(xine_stream_t *stream, int info, const char *value) {
  meta_info_set_unlocked_encoding(stream, info, value, NULL);
}

/*
 * Reset (nullify) private info value.
 */
void _x_meta_info_reset(xine_stream_t *stream, int info) {
  pthread_mutex_lock(&stream->meta_mutex);
  meta_info_set_unlocked_utf8(stream, info, NULL);
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Reset (nullify) public info value.
 */
static void meta_info_public_reset_unlocked(xine_stream_t *stream, int info) {
  if(meta_valid(info)) {
    if(stream->meta_info_public[info])
      free(stream->meta_info_public[info]);
    stream->meta_info_public[info] = NULL;
  }
}
void _x_meta_info_public_reset(xine_stream_t *stream, int info) {
  pthread_mutex_lock(&stream->meta_mutex);
  meta_info_public_reset_unlocked(stream, info);
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Set private meta info value using current locale.
 */
void _x_meta_info_set(xine_stream_t *stream, int info, const char *str) {
  pthread_mutex_lock(&stream->meta_mutex);
  if(str)
    meta_info_set_unlocked(stream, info, str);
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Set private meta info value using specified encoding.
 */
void _x_meta_info_set_generic(xine_stream_t *stream, int info, const char *str, const char *enc) {
  pthread_mutex_lock(&stream->meta_mutex);
  if(str)
    meta_info_set_unlocked_encoding(stream, info, str, enc);
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Set private meta info value using utf8.
 */
void _x_meta_info_set_utf8(xine_stream_t *stream, int info, const char *str) {
  pthread_mutex_lock(&stream->meta_mutex);
  if(str)
    meta_info_set_unlocked_utf8(stream, info, str);
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Set private meta info from buf, 'len' bytes long.
 */
void _x_meta_info_n_set(xine_stream_t *stream, int info, const char *buf, int len) {
  pthread_mutex_lock(&stream->meta_mutex);
  if(meta_valid(info) && len) {
    char *str = strndup(buf, len);

    meta_info_set_unlocked(stream, info, str);
    free(str);
  }
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Set private meta info value, from multiple arguments.
 */
void _x_meta_info_set_multi(xine_stream_t *stream, int info, ...) {

  pthread_mutex_lock(&stream->meta_mutex);
  if(meta_valid(info)) {
    va_list   ap;
    char     *args[1025];
    char     *buf;
    size_t    n, len;

    len = n = 0;

    va_start(ap, info);
    while((buf = va_arg(ap, char *)) && (n < 1024)) {
      len += strlen(buf) + 1;
      args[n] = buf;
      n++;
    }
    va_end(ap);

    args[n] = NULL;

    if(len) {
      char *p, *meta;

      p = meta = (char *) malloc(len + 1);

      n = 0;
      while(args[n]) {
	strcpy(meta, args[n]);
	meta += strlen(args[n]) + 1;
	n++;
      }

      *meta = '\0';

      if(stream->meta_info[info])
	free(stream->meta_info[info]);

      stream->meta_info[info] = p;

      if(stream->meta_info[info] && strlen(stream->meta_info[info]))
	  meta_info_chomp(stream->meta_info[info]);
    }

  }
  pthread_mutex_unlock(&stream->meta_mutex);
}

/*
 * Retrieve private info value.
 */
const char *_x_meta_info_get(xine_stream_t *stream, int info) {
  const char *meta_info = NULL;

  pthread_mutex_lock(&stream->meta_mutex);
  meta_info = stream->meta_info[info];
  pthread_mutex_unlock(&stream->meta_mutex);

  return meta_info;
}

/*
 * Retrieve public info value.
 */
const char *_x_meta_info_get_public(xine_stream_t *stream, int info) {
  const char *meta_info = NULL;

  pthread_mutex_lock(&stream->meta_mutex);
  meta_info = stream->meta_info_public[info];
  if(meta_valid(info) && (!meta_info_is_identical(stream, info))) {
    meta_info_public_reset_unlocked(stream, info);

    if(stream->meta_info[info])
      stream->meta_info_public[info] = strdup(stream->meta_info[info]);

    meta_info = stream->meta_info_public[info];
  }
  pthread_mutex_unlock(&stream->meta_mutex);

  return meta_info;
}
