/*
 * Copyright (C) 2000-2004 the xine project
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
 * inttypes.h - Standard integer definitions.
 *
 */

#ifndef _SYS_INTTYPES_H_
#define _SYS_INTTYPES_H_

#include <stdint.h>

typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef __int64 intmax_t;
typedef unsigned __int64 uintmax_t;
typedef signed __int64 intmax_t;
typedef unsigned __int64 uintmax_t;

#define __WORDSIZE 32

/* The ISO C99 standard specifies that these macros must only be
   defined if explicitly requested.  */
#if !defined __cplusplus || defined __STDC_FORMAT_MACROS

# if __WORDSIZE == 64
#  define __PRI64_PREFIX        "l"
#  define __PRIPTR_PREFIX       "l"
# else
#  define __PRI64_PREFIX        "I64"
#  define __PRIPTR_PREFIX
# endif

/* Macros for printing format specifiers.  */

/* Decimal notation.  */
# define PRId8          "d"
# define PRId16         "d"
# define PRId32         "d"
# define PRId64         __PRI64_PREFIX "d"

# define PRIdLEAST8     "d"
# define PRIdLEAST16    "d"
# define PRIdLEAST32    "d"
# define PRIdLEAST64    __PRI64_PREFIX "d"

# define PRIdFAST8      "d"
# define PRIdFAST16     "d"
# define PRIdFAST32     "d"
# define PRIdFAST64     __PRI64_PREFIX "d"


# define PRIi8          "i"
# define PRIi16         "i"
# define PRIi32         "i"
# define PRIi64         __PRI64_PREFIX "i"

# define PRIiLEAST8     "i"
# define PRIiLEAST16    "i"
# define PRIiLEAST32    "i"
# define PRIiLEAST64    __PRI64_PREFIX "i"

# define PRIiFAST8      "i"
# define PRIiFAST16     "i"
# define PRIiFAST32     "i"
# define PRIiFAST64     __PRI64_PREFIX "i"

/* Octal notation.  */
# define PRIo8          "o"
# define PRIo16         "o"
# define PRIo32         "o"
# define PRIo64         __PRI64_PREFIX "o"

# define PRIoLEAST8     "o"
# define PRIoLEAST16    "o"
# define PRIoLEAST32    "o"
# define PRIoLEAST64    __PRI64_PREFIX "o"

# define PRIoFAST8      "o"
# define PRIoFAST16     "o"
# define PRIoFAST32     "o"
# define PRIoFAST64     __PRI64_PREFIX "o"

/* Unsigned integers.  */
# define PRIu8          "u"
# define PRIu16         "u"
# define PRIu32         "u"
# define PRIu64         __PRI64_PREFIX "u"

# define PRIuLEAST8     "u"
# define PRIuLEAST16    "u"
# define PRIuLEAST32    "u"
# define PRIuLEAST64    __PRI64_PREFIX "u"

# define PRIuFAST8      "u"
# define PRIuFAST16     "u"
# define PRIuFAST32     "u"
# define PRIuFAST64     __PRI64_PREFIX "u"

/* lowercase hexadecimal notation.  */
# define PRIx8          "x"
# define PRIx16         "x"
# define PRIx32         "x"
# define PRIx64         __PRI64_PREFIX "x"

# define PRIxLEAST8     "x"
# define PRIxLEAST16    "x"
# define PRIxLEAST32    "x"
# define PRIxLEAST64    __PRI64_PREFIX "x"

# define PRIxFAST8      "x"
# define PRIxFAST16     "x"
# define PRIxFAST32     "x"
# define PRIxFAST64     __PRI64_PREFIX "x"

/* UPPERCASE hexadecimal notation.  */
# define PRIX8          "X"
# define PRIX16         "X"
# define PRIX32         "X"
# define PRIX64         __PRI64_PREFIX "X"

# define PRIXLEAST8     "X"
# define PRIXLEAST16    "X"
# define PRIXLEAST32    "X"
# define PRIXLEAST64    __PRI64_PREFIX "X"

# define PRIXFAST8      "X"
# define PRIXFAST16     "X"
# define PRIXFAST32     "X"
# define PRIXFAST64     __PRI64_PREFIX "X"


/* Macros for printing `intmax_t' and `uintmax_t'.  */
# define PRIdMAX        __PRI64_PREFIX "d"
# define PRIiMAX        __PRI64_PREFIX "i"
# define PRIoMAX        __PRI64_PREFIX "o"
# define PRIuMAX        __PRI64_PREFIX "u"
# define PRIxMAX        __PRI64_PREFIX "x"
# define PRIXMAX        __PRI64_PREFIX "X"


/* Macros for printing `intptr_t' and `uintptr_t'.  */
# define PRIdPTR        __PRIPTR_PREFIX "d"
# define PRIiPTR        __PRIPTR_PREFIX "i"
# define PRIoPTR        __PRIPTR_PREFIX "o"
# define PRIuPTR        __PRIPTR_PREFIX "u"
# define PRIxPTR        __PRIPTR_PREFIX "x"
# define PRIXPTR        __PRIPTR_PREFIX "X"


/* Macros for scanning `intmax_t' and `uintmax_t'. */
#define SCNdMAX         __PRI64_PREFIX "d"
#define SCNiMAX         __PRI64_PREFIX "i"
#define SCNoMAX         __PRI64_PREFIX "o"
#define SCNuMAX         __PRI64_PREFIX "u"
#define SCNxMAX         __PRI64_PREFIX "x"
#define SCNXMAX         __PRI64_PREFIX "X"

#endif /* !defined __cplusplus || defined __STDC_FORMAT_MACROS */


#endif
