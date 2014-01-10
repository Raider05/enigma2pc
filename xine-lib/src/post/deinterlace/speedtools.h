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

static inline unsigned int read_pf(volatile unsigned int *addr)
{
    return *addr;
}

#define READ_PREFETCH_2048(x) \
    { int * pfetcha = (int *) x; \
        read_pf(pfetcha);       read_pf(pfetcha + 16);  read_pf(pfetcha + 32);  read_pf(pfetcha + 48); \
        read_pf(pfetcha + 64);  read_pf(pfetcha + 80);  read_pf(pfetcha + 96);  read_pf(pfetcha + 112); \
        read_pf(pfetcha + 128); read_pf(pfetcha + 144); read_pf(pfetcha + 160); read_pf(pfetcha + 176); \
        read_pf(pfetcha + 192); read_pf(pfetcha + 208); read_pf(pfetcha + 224); read_pf(pfetcha + 240); \
        pfetcha += 256;                                                 \
        read_pf(pfetcha);       read_pf(pfetcha + 16);  read_pf(pfetcha + 32);  read_pf(pfetcha + 48); \
        read_pf(pfetcha + 64);  read_pf(pfetcha + 80);  read_pf(pfetcha + 96);  read_pf(pfetcha + 112); \
        read_pf(pfetcha + 128); read_pf(pfetcha + 144); read_pf(pfetcha + 160); read_pf(pfetcha + 176); \
        read_pf(pfetcha + 192); read_pf(pfetcha + 208); read_pf(pfetcha + 224); read_pf(pfetcha + 240); }

#endif /* SPEEDTOOLS_H_INCLUDED */
