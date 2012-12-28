/*
 * Copyright (C) 2003 the xine project
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
 * x11osd.h, use X11 Nonrectangular Window Shape Extension to draw xine OSD
 *
 * Nov 2003 - Miguel Freitas
 *
 * based on ideas and code of
 * xosd Copyright (c) 2000 Andre Renaud (andre@ignavus.net)
 */

#ifndef X11OSD_H
#define X11OSD_H

#include <xine/vo_scale.h>
#include <sys/time.h>

typedef struct x11osd x11osd;
enum x11osd_mode {X11OSD_SHAPED, X11OSD_COLORKEY};

x11osd *x11osd_create (xine_t *xine, Display *display, int screen, Window window, enum x11osd_mode mode);

void x11osd_colorkey(x11osd * osd, uint32_t colorkey, vo_scale_t *scaling);

void x11osd_destroy (x11osd * osd);

void x11osd_expose (x11osd * osd);

void x11osd_resize (x11osd * osd, int width, int height);

void x11osd_drawable_changed (x11osd * osd, Window window);

void x11osd_clear(x11osd *osd);

void x11osd_blend(x11osd *osd, vo_overlay_t *overlay);

void x11osd_scale_argb32_image(x11osd *osd, uint32_t* src, uint32_t* dst, int src_width, int src_height, int dst_width, int dst_height);

void x11osd_scale_line(uint32_t* src, uint32_t* dst, int width, int step);

void x11osd_scale_line_mmx(uint32_t* src, uint32_t* dst, int width, int step);

#endif
