/* stdint.h - integer types

   Copyright 2003 Red Hat, Inc.

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

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
 * along with self program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

/* Modified original CygWin version for using by MSVC port. */

#ifndef _STDINT_H
#define _STDINT_H

/* Macros for minimum-width integer constant expressions */

#define INT8_C(x) x
#define INT16_C(x) x
#define INT32_C(x) x ## L
#define INT64_C(x) x ## I64

#define UINT8_C(x) x ## U
#define UINT16_C(x) x ## U
#define UINT32_C(x) x ## UL
#define UINT64_C(x) x ## UI64

/* Macros for greatest-width integer constant expressions */

#define INTMAX_C(x) x ## I64
#define UINTMAX_C(x) x ## UI64

/* Limits of exact-width integer types */

#define INT8_MIN (-128)
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN (-INT64_C(9223372036854775807) - 1)

#define INT8_MAX (127)
#define INT16_MAX (32767)
#define INT32_MAX (2147483647)
#define INT64_MAX (INT64_C(9223372036854775807))

#define UINT8_MAX (255)
#define UINT16_MAX (65535)
#define UINT32_MAX (4294967295UL)
#define UINT64_MAX (UINT64_C(18446744073709551615))

/* Limits of minimum-width integer types */

#define INT_LEAST8_MIN (-128)
#define INT_LEAST16_MIN (-32768)
#define INT_LEAST32_MIN (-2147483647 - 1)
#define INT_LEAST64_MIN (-INT64_C(9223372036854775807) - 1)

#define INT_LEAST8_MAX (127)
#define INT_LEAST16_MAX (32767)
#define INT_LEAST32_MAX (2147483647)
#define INT_LEAST64_MAX (INT64_C(9223372036854775807))

#define UINT_LEAST8_MAX (255)
#define UINT_LEAST16_MAX (65535)
#define UINT_LEAST32_MAX (4294967295UL)
#define UINT_LEAST64_MAX (UINT64_C(18446744073709551615))

/* Limits of fastest minimum-width integer types */

#define INT_FAST8_MIN (-128)
#define INT_FAST16_MIN (-2147483647 - 1)
#define INT_FAST32_MIN (-2147483647 - 1)
#define INT_FAST64_MIN (-INT64_C(9223372036854775807) - 1)

#define INT_FAST8_MAX (127)
#define INT_FAST16_MAX (2147483647)
#define INT_FAST32_MAX (2147483647)
#define INT_FAST64_MAX (INT64_C(9223372036854775807))

#define UINT_FAST8_MAX (255)
#define UINT_FAST16_MAX (4294967295UL)
#define UINT_FAST32_MAX (4294967295UL)
#define UINT_FAST64_MAX (UINT64_C(18446744073709551615))

/* Limits of integer types capable of holding object pointers */

#define INTPTR_MIN (-2147483647 - 1)
#define INTPTR_MAX (2147483647)
#define UINTPTR_MAX (4294967295UL)

/* Limits of greatest-width integer types */

#define INTMAX_MIN (-INT64_C(9223372036854775807) - 1)
#define INTMAX_MAX (INT64_C(9223372036854775807))
#define UINTMAX_MAX (UINT64_C(18446744073709551615))

/* Limits of other integer types */

#ifndef PTRDIFF_MIN
#define PTRDIFF_MIN (-2147483647 - 1)
#define PTRDIFF_MAX (2147483647)
#endif

#ifndef SIG_ATOMIC_MIN
#define SIG_ATOMIC_MIN (-2147483647 - 1)
#endif
#ifndef SIG_ATOMIC_MAX
#define SIG_ATOMIC_MAX (2147483647)
#endif

#ifndef SIZE_MAX
#define SIZE_MAX (4294967295UL)
#endif

#ifndef WCHAR_MIN
#ifdef __WCHAR_MIN__
#define WCHAR_MIN __WCHAR_MIN__
#define WCHAR_MAX __WCHAR_MAX__
#else
#define WCHAR_MIN (0)
#define WCHAR_MAX (65535)
#endif
#endif

#ifndef WINT_MIN
#define WINT_MIN (-2147483647 - 1)
#define WINT_MAX (2147483647)
#endif

#endif /* _STDINT_H */
