/*
 * Copyright (C) 2003, 2007 the xine project
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
 * xcbosd.h, use X11 Nonrectangular Window Shape Extension to draw xine OSD
 *
 * Nov 2003 - Miguel Freitas
 * Feb 2007 - ported to xcb by Christoph Pfister
 *
 * based on ideas and code of
 * xosd Copyright (c) 2000 Andre Renaud (andre@ignavus.net)
 */

#ifndef XCBOSD_H
#define XCBOSD_H

#include <xine/vo_scale.h>

typedef struct xcbosd xcbosd;
enum xcbosd_mode {XCBOSD_SHAPED, XCBOSD_COLORKEY};

xcbosd *xcbosd_create(xine_t *xine, xcb_connection_t *connection, xcb_screen_t *screen, xcb_window_t window, enum xcbosd_mode mode);

void xcbosd_colorkey(xcbosd *osd, uint32_t colorkey, vo_scale_t *scaling);

void xcbosd_destroy(xcbosd *osd);

void xcbosd_expose(xcbosd *osd);

void xcbosd_resize(xcbosd *osd, int width, int height);

void xcbosd_drawable_changed(xcbosd *osd, xcb_window_t window);

void xcbosd_clear(xcbosd *osd);

void xcbosd_blend(xcbosd *osd, vo_overlay_t *overlay);

#endif
