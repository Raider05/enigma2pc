/*
 * Copyright (C) 2004-2006 the xine project
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
 * Platform dependent types needed by public xine.h.
 * Types not needed by xine.h are specified in os_internal.h.
 *
 * Heavily based on os_types.h from OggVorbis (BSD License),
 * not tested on all platforms with xine.
 */

#ifndef XINE_OS_TYPES_H
#define XINE_OS_TYPES_H

#if defined(_WIN32) && !defined(__GNUC__)

   /* MSVC/Borland */
   typedef __int8 int8_t;
   typedef unsigned __int8 uint8_t;
   typedef __int16 int16_t;
   typedef unsigned __int16 uint16_t;
   typedef __int32 int32_t;
   typedef unsigned __int32 uint32_t;
   typedef __int64 int64_t;
   typedef unsigned __int64 uint64_t;

#elif defined(__MACOS__)

#  include <sys/types.h>
   typedef SInt8 int8_t;
   typedef UInt8 uint8_t;
   typedef SInt16 int16_t;
   typedef UInt16 uint16_t;
   typedef SInt32 int32_t;
   typedef UInt32 uint32_t;
   typedef SInt64 int64_t;
   typedef UInt64 uint64_t;

#elif defined(__MACOSX__) /* MacOS X Framework build */

#  include <sys/types.h>
   typedef u_int8_t uint8_t;
   typedef u_int16_t uint16_t;
   typedef u_int32_t uint32_t;
   typedef u_int64_t uint64_t;

#elif defined (__EMX__)

   /* OS/2 GCC */
   typedef signed char int8_t;
   typedef unsigned char uint8_t;
   typedef short int16_t;
   typedef unsigned short uint16_t;
   typedef int int32_t;
   typedef unsigned int uint32_t;
   typedef long long int64_t;
   typedef unsigned long long int64_t;

#elif defined (DJGPP)

   /* DJGPP */
   typedef signed char int8_t;
   typedef unsigned char uint8_t;
   typedef short int16_t;
   typedef unsigned short uint16_t;
   typedef int int32_t;
   typedef unsigned int uint32_t;
   typedef long long int64_t;
   typedef unsigned long long uint64_t;

#elif defined(R5900)

   /* PS2 EE */
   typedef signed char int8_t;
   typedef unsigned char uint8_t;
   typedef short int16_t;
   typedef unsigned short int16_t;
   typedef int int32_t;
   typedef unsigned uint32_t;
   typedef long int64_t;
   typedef unsigned long int64_t;

#else

  /*
   * CygWin: _WIN32 & __GNUC__
   * BeOS:   __BEOS__
   * Linux, Solaris, Mac and others
   */
#  include <inttypes.h>

#endif

#endif  /* XINE_OS_TYPES_H */
