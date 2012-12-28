/**
 * Copyright (c) 2002 Billy Biggs <vektor@dumbterm.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 */

#ifndef SPEEDTOOLS_H_INCLUDED
#define SPEEDTOOLS_H_INCLUDED

#define PREFETCH_2048(x) \
    { int *pfetcha = (int *) x; \
        prefetchnta( pfetcha ); \
        prefetchnta( pfetcha + 64 ); \
        prefetchnta( pfetcha + 128 ); \
        prefetchnta( pfetcha + 192 ); \
        pfetcha += 256; \
        prefetchnta( pfetcha ); \
        prefetchnta( pfetcha + 64 ); \
        prefetchnta( pfetcha + 128 ); \
        prefetchnta( pfetcha + 192 ); }

#define READ_PREFETCH_2048(x) \
    { int *pfetcha = (int *) x; int pfetchtmp; \
        pfetchtmp = pfetcha[ 0 ] + pfetcha[ 16 ] + pfetcha[ 32 ] + pfetcha[ 48 ] + \
            pfetcha[ 64 ] + pfetcha[ 80 ] + pfetcha[ 96 ] + pfetcha[ 112 ] + \
            pfetcha[ 128 ] + pfetcha[ 144 ] + pfetcha[ 160 ] + pfetcha[ 176 ] + \
            pfetcha[ 192 ] + pfetcha[ 208 ] + pfetcha[ 224 ] + pfetcha[ 240 ]; \
        pfetcha += 256; \
        pfetchtmp = pfetcha[ 0 ] + pfetcha[ 16 ] + pfetcha[ 32 ] + pfetcha[ 48 ] + \
            pfetcha[ 64 ] + pfetcha[ 80 ] + pfetcha[ 96 ] + pfetcha[ 112 ] + \
            pfetcha[ 128 ] + pfetcha[ 144 ] + pfetcha[ 160 ] + pfetcha[ 176 ] + \
            pfetcha[ 192 ] + pfetcha[ 208 ] + pfetcha[ 224 ] + pfetcha[ 240 ]; }

#endif /* SPEEDTOOLS_H_INCLUDED */
