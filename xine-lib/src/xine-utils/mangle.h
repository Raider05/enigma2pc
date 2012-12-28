/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 * copyright (c) 2008 the xine-project
 *
 * This file is part of FFmpeg.
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
 */

/**
 * @file
 *
 * @brief MANGLE definition from FFmpeg project, until the code is ported
 * not to require this (considered an hack by the FFmpeg project.
 */

#ifndef _XINE_MANGLE_H
#define _XINE_MANGLE_H

#if defined(PIC) && ! defined(__PIC__)
#define __PIC__
#endif

// Use rip-relative addressing if compiling PIC code on x86-64.
#if defined(__MINGW32__) || defined(__CYGWIN__) || defined(__DJGPP__) || \
    defined(__OS2__) || (defined (__OpenBSD__) && !defined(__ELF__))
#    if defined(__MINGW64__)
#      define EXTERN_PREFIX ""
#    else
#      define EXTERN_PREFIX "_"
#    endif
#    if defined(__x86_64__) && defined(__PIC__)
#        define MANGLE(a) EXTERN_PREFIX #a"(%%rip)"
#    else
#        define MANGLE(a) EXTERN_PREFIX #a
#    endif
#else
#    if defined(__x86_64__) && defined(__PIC__)
#        define MANGLE(a) #a"(%%rip)"
#    elif defined(__APPLE__)
#        define MANGLE(a) "_" #a
#    else
#        define MANGLE(a) #a
#    endif
#endif

#endif
