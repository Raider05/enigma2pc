/*
 * Copyright (C) 2000-2007 the xine project
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
 * Common function for the thin layer to use Real binary-only codecs in xine
 */

#ifndef __REAL_COMMON_H__
#define __REAL_COMMON_H__

#include <xine/xine_internal.h>

/*
 * some fake functions to make real codecs happy
 * These are, on current date (20070316) needed only for Alpha
 * codecs.
 * As they are far from being proper replacements, define them only there
 * until new codecs are available there too.
 */
#ifdef __alpha__

void *__builtin_new(size_t size);
void __builtin_delete (void *foo);
void *__builtin_vec_new(size_t size) EXPORTED;
void __builtin_vec_delete(void *mem) EXPORTED;
void __pure_virtual(void) EXPORTED;

#endif

#ifndef HAVE___ENVIRON
# ifdef HAVE__ENVIRON
  char **__environ __attribute__((weak, alias("_environ")));
# elif defined(HAVE_ENVIRON)
  char **__environ __attribute__((weak, alias("environ")));
# else
  char **fake__environ = { NULL };
  char **__environ __attribute__((weak, alias("fake__environ")));
# endif
#endif

#ifndef HAVE_STDERR
# ifdef HAVE___STDERRP
#  undef stderr
FILE *stderr __attribute__((weak, alias("__stderrp")));
# else
#  error Your stderr alias is not supported, please report to xine developers.
# endif
#endif

#ifndef HAVE____BRK_ADDR
void ___brk_addr(void) EXPORTED;
#endif

#ifndef HAVE___CTYPE_B
void __ctype_b(void) EXPORTED;
#endif

void _x_real_codecs_init(xine_t *const xine);
void *_x_real_codec_open(xine_stream_t *const stream, const char *const path,
			 const char *const codec_name,
			 const char *const codec_alternate);

const decoder_info_t dec_info_realvideo;
void *init_realvdec (xine_t *xine, void *data);

const decoder_info_t dec_info_realaudio;
void *init_realadec (xine_t *xine, void *data);

#endif
