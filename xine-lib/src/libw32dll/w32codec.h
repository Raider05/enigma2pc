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
 * routines for using w32 codecs
 */

#include "wine/msacm.h"
#include "wine/avifmt.h"
#include "wine/vfw.h"


int w32c_init_video(BITMAPINFOHEADER *bih) ;

void w32c_decode_video (unsigned char *data, uint32_t nSize, int bFrameEnd, uint32_t nPTS);

void w32c_close_video ();

int w32c_init_audio (WAVEFORMATEX *in_fmt);

void w32c_decode_audio (unsigned char *data, uint32_t nSize, int bFrameEnd, uint32_t nPTS) ;

void w32c_close_audio ();
