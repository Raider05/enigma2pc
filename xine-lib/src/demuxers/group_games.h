/*
 * Copyright (C) 2000-2003 the xine project
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

#ifndef HAVE_GROUP_GAMES_H
#define HAVE_GROUP_GAMES_H

#include <xine/xine_internal.h>

void *demux_eawve_init_plugin(xine_t *xine, void *data);
void *demux_idcin_init_plugin (xine_t *xine, void *data);
void *demux_ipmovie_init_plugin (xine_t *xine, void *data);
void *demux_vqa_init_plugin (xine_t *xine, void *data);
void *demux_wc3movie_init_plugin (xine_t *xine, void *data);
void *demux_roq_init_plugin (xine_t *xine, void *data);
void *demux_str_init_plugin (xine_t *xine, void *data);
void *demux_film_init_plugin (xine_t *xine, void *data);
void *demux_smjpeg_init_plugin (xine_t *xine, void *data);
void *demux_fourxm_init_plugin (xine_t *xine, void *data);
void *demux_vmd_init_plugin (xine_t *xine, void *data);

#endif
