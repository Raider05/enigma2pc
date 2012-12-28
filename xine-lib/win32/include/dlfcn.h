/* 
 * Copyright (C) 2000-2001 the xine project
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
 * WIN32 PORT,
 * by Matthew Grooms <elon@altavista.com>
 *
 * dlfcn.h - Mimic the dl functions of a *nix system
 *
 */

#ifndef _DLFCN_H
#define _DLFCN_H

#include <windows.h>

#define RTLD_LAZY	0
#define RTLD_GLOBAL	0

#define dlopen( A, B ) ( void * )LoadLibrary( A )
#define dlclose( A ) FreeLibrary( (HMODULE)A )
#define dlsym( A, B ) ( void * ) GetProcAddress( (HMODULE)A, B )
#define dlerror() "dlerror"

#endif
