/*
 * Copyright (C) 2000-2012 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * Compability macros for various ffmpeg versions
 */

#ifndef XINE_AVCODEC_COMPAT_H
#define XINE_AVCODEC_COMPAT_H

#ifndef LIBAVCODEC_VERSION_MAJOR
#  ifdef LIBAVCODEC_VERSION_INT
#    define LIBAVCODEC_VERSION_MAJOR ((LIBAVCODEC_VERSION_INT)>>16)
#    define LIBAVCODEC_VERSION_MINOR (((LIBAVCODEC_VERSION_INT)>>8) & 0xff)
#  else
#    error ffmpeg headers must be included first !
#  endif
#endif

#if LIBAVCODEC_VERSION_MAJOR > 51
#  define bits_per_sample bits_per_coded_sample
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#else
#  define pp_context	pp_context_t
#  define pp_mode	pp_mode_t
#endif

/* reordered_opaque appeared in libavcodec 51.68.0 */
#define AVCODEC_HAS_REORDERED_OPAQUE
#if LIBAVCODEC_VERSION_INT < 0x334400
# undef AVCODEC_HAS_REORDERED_OPAQUE
#endif

/* colorspace and color_range were added before 52.29.0 */
#if LIBAVCODEC_VERSION_MAJOR > 52 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 29)
# define AVCODEC_HAS_COLORSPACE
#endif

/* "unused" as of v54 */
#if LIBAVCODEC_VERSION_MAJOR < 54
# define AVCODEC_HAS_SUB_ID
#endif

/**/
#if LIBAVCODEC_VERSION_MAJOR > 53 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 8)
#  define avcodec_init() do {} while(0)
#endif

/* avcodec_alloc_context() */
#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 6)
#  define AVCONTEXT 3
#  define avcodec_alloc_context() avcodec_alloc_context3(NULL)
#else
#  define AVCONTEXT 1
#endif

/* avcodec_open() */
#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 6)
#  define AVOPEN 2
#  define avcodec_open(ctx,codec) avcodec_open2(ctx, codec, NULL)
#else
#  define AVOPEN 1
#endif

/* avcodec_thread_init() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 112)
#  define DEPRECATED_AVCODEC_THREAD_INIT 1
#endif

/* av_parser_parse() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 94)
#  define AVPARSE 2
#else
#  define AVPARSE 1
#endif

/* avcodec_decode_video() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define AVVIDEO 2
#else
#  define AVVIDEO 1
#endif

/* avcodec_decode_audio() */
#if LIBAVCODEC_VERSION_MAJOR >= 54
#  define AVAUDIO 4
#elif LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define AVAUDIO 3
#else
#  define AVAUDIO 2
#endif

/* AVFrame.age */
#if LIBAVCODEC_VERSION_INT >= 0x351C01 && LIBAVCODEC_VERSION_INT < 0x360000 // not sure about this - original condition was broken
#  define AVFRAMEAGE 1
#endif

#if LIBAVCODEC_VERSION_MAJOR < 53
/* release 0.7.x (libavcodec 52) has deprecated AVCodecContext.palctrl but for backwards compatibility no
   working alternative. */
#  define AVPALETTE 1
#else
/* pass palette as AVPacket side data */
#  define AVPALETTE 2
#endif

#if defined LIBAVUTIL_VERSION_MAJOR && LIBAVUTIL_VERSION_MAJOR >= 52
#  define PIX_FMT_NONE      AV_PIX_FMT_NONE
#  define PIX_FMT_YUV420P   AV_PIX_FMT_YUV420P
#  define PIX_FMT_YUVJ420P  AV_PIX_FMT_YUVJ420P
#  define PIX_FMT_YUV444P   AV_PIX_FMT_YUV444P
#  define PIX_FMT_YUVJ444P  AV_PIX_FMT_YUVJ444P
#  define PIX_FMT_YUV410P   AV_PIX_FMT_YUV410P
#  define PIX_FMT_YUV411P   AV_PIX_FMT_YUV411P
#  define PIX_FMT_VAAPI_VLD AV_PIX_FMT_VAAPI_VLD
#  define PIX_FMT_ARGB      AV_PIX_FMT_ARGB
#  define PIX_FMT_BGRA      AV_PIX_FMT_BGRA
#  define PIX_FMT_RGB24     AV_PIX_FMT_RGB24
#  define PIX_FMT_BGR24     AV_PIX_FMT_BGR24
#  define PIX_FMT_RGB555BE  AV_PIX_FMT_RGB555BE
#  define PIX_FMT_RGB555LE  AV_PIX_FMT_RGB555LE
#  define PIX_FMT_RGB565BE  AV_PIX_FMT_RGB565BE
#  define PIX_FMT_RGB565LE  AV_PIX_FMT_RGB565LE
#  define PIX_FMT_PAL8      AV_PIX_FMT_PAL8
#  define PixelFormat       AVPixelFormat
#endif

#endif /* XINE_AVCODEC_COMPAT_H */
