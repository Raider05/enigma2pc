/*
 * Copyright (C) 2000-2006 the xine project
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
 */

#ifndef __BSWAP_H__
#define __BSWAP_H__

#include "config.h"

#define always_inline inline

#include "ffmpeg_bswap.h"

/* These are the Aligned variants */
#define _X_ABE_16(x) (be2me_16(*(uint16_t*)(x)))
#define _X_ABE_32(x) (be2me_32(*(uint32_t*)(x)))
#define _X_ABE_64(x) (be2me_64(*(uint64_t*)(x)))
#define _X_ALE_16(x) (le2me_16(*(uint16_t*)(x)))
#define _X_ALE_32(x) (le2me_32(*(uint32_t*)(x)))
#define _X_ALE_64(x) (le2me_64(*(uint64_t*)(x)))

#define _X_BE_16(x) (((uint16_t)(((uint8_t*)(x))[0]) << 8) | \
                  ((uint16_t)((uint8_t*)(x))[1]))
#define _X_BE_24(x) (((uint32_t)(((uint8_t*)(x))[0]) << 16) | \
                  ((uint32_t)(((uint8_t*)(x))[1]) << 8) | \
                  ((uint32_t)(((uint8_t*)(x))[2])))
#define _X_BE_32(x) (((uint32_t)(((uint8_t*)(x))[0]) << 24) | \
                  ((uint32_t)(((uint8_t*)(x))[1]) << 16) | \
                  ((uint32_t)(((uint8_t*)(x))[2]) << 8) | \
                  ((uint32_t)((uint8_t*)(x))[3]))
#define _X_BE_64(x) (((uint64_t)(((uint8_t*)(x))[0]) << 56) | \
                  ((uint64_t)(((uint8_t*)(x))[1]) << 48) | \
                  ((uint64_t)(((uint8_t*)(x))[2]) << 40) | \
                  ((uint64_t)(((uint8_t*)(x))[3]) << 32) | \
                  ((uint64_t)(((uint8_t*)(x))[4]) << 24) | \
                  ((uint64_t)(((uint8_t*)(x))[5]) << 16) | \
                  ((uint64_t)(((uint8_t*)(x))[6]) << 8) | \
                  ((uint64_t)((uint8_t*)(x))[7]))

#define _X_LE_16(x) (((uint16_t)(((uint8_t*)(x))[1]) << 8) | \
                  ((uint16_t)((uint8_t*)(x))[0]))
#define _X_LE_24(x) (((uint32_t)(((uint8_t*)(x))[2]) << 16) | \
                  ((uint32_t)(((uint8_t*)(x))[1]) << 8) | \
                  ((uint32_t)(((uint8_t*)(x))[0])))
#define _X_LE_32(x) (((uint32_t)(((uint8_t*)(x))[3]) << 24) | \
                  ((uint32_t)(((uint8_t*)(x))[2]) << 16) | \
                  ((uint32_t)(((uint8_t*)(x))[1]) << 8) | \
                  ((uint32_t)((uint8_t*)(x))[0]))
#define _X_LE_64(x) (((uint64_t)(((uint8_t*)(x))[7]) << 56) | \
                  ((uint64_t)(((uint8_t*)(x))[6]) << 48) | \
                  ((uint64_t)(((uint8_t*)(x))[5]) << 40) | \
                  ((uint64_t)(((uint8_t*)(x))[4]) << 32) | \
                  ((uint64_t)(((uint8_t*)(x))[3]) << 24) | \
                  ((uint64_t)(((uint8_t*)(x))[2]) << 16) | \
                  ((uint64_t)(((uint8_t*)(x))[1]) << 8) | \
                  ((uint64_t)((uint8_t*)(x))[0]))

#ifdef WORDS_BIGENDIAN
#define _X_ME_16(x) _X_BE_16(x)
#define _X_ME_32(x) _X_BE_32(x)
#define _X_ME_64(x) _X_BE_64(x)
#define _X_AME_16(x) _X_ABE_16(x)
#define _X_AME_32(x) _X_ABE_32(x)
#define _X_AME_64(x) _X_ABE_64(x)
#else
#define _X_ME_16(x) _X_LE_16(x)
#define _X_ME_32(x) _X_LE_32(x)
#define _X_ME_64(x) _X_LE_64(x)
#define _X_AME_16(x) _X_ALE_16(x)
#define _X_AME_32(x) _X_ALE_32(x)
#define _X_AME_64(x) _X_ALE_64(x)
#endif

#define BE_FOURCC( ch0, ch1, ch2, ch3 )             \
        ( (uint32_t)(unsigned char)(ch3) |          \
        ( (uint32_t)(unsigned char)(ch2) << 8 ) |   \
        ( (uint32_t)(unsigned char)(ch1) << 16 ) |  \
        ( (uint32_t)(unsigned char)(ch0) << 24 ) )

#define LE_FOURCC( ch0, ch1, ch2, ch3 )             \
        ( (uint32_t)(unsigned char)(ch0) |          \
        ( (uint32_t)(unsigned char)(ch1) << 8 ) |   \
        ( (uint32_t)(unsigned char)(ch2) << 16 ) |  \
        ( (uint32_t)(unsigned char)(ch3) << 24 ) )

#ifdef WORDS_BIGENDIAN
#define ME_FOURCC BE_FOURCC
#else
#define ME_FOURCC LE_FOURCC
#endif

#endif
