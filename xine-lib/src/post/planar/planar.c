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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * catalog for planar post plugins
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>

extern void *invert_init_plugin(xine_t *xine, void *);
static const post_info_t invert_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *expand_init_plugin(xine_t *xine, void *);
static const post_info_t expand_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *fill_init_plugin(xine_t *xine, void*);
static const post_info_t fill_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *eq_init_plugin(xine_t *xine, void *);
static const post_info_t eq_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *boxblur_init_plugin(xine_t *xine, void *);
static const post_info_t boxblur_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *denoise3d_init_plugin(xine_t *xine, void *);
static const post_info_t denoise3d_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *eq2_init_plugin(xine_t *xine, void *);
static const post_info_t eq2_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *unsharp_init_plugin(xine_t *xine, void *);
static const post_info_t unsharp_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *pp_init_plugin(xine_t *xine, void *);
static const post_info_t pp_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

extern void *noise_init_plugin(xine_t *xine, void *);
static const post_info_t noise_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST, 10, "expand", XINE_VERSION_CODE, &expand_special_info, &expand_init_plugin },
  { PLUGIN_POST, 10, "fill", XINE_VERSION_CODE, &fill_special_info, &fill_init_plugin },
  { PLUGIN_POST, 10, "invert", XINE_VERSION_CODE, &invert_special_info, &invert_init_plugin },
  { PLUGIN_POST, 10, "eq", XINE_VERSION_CODE, &eq_special_info, &eq_init_plugin },
  { PLUGIN_POST, 10, "denoise3d", XINE_VERSION_CODE, &denoise3d_special_info, &denoise3d_init_plugin },
  { PLUGIN_POST, 10, "boxblur", XINE_VERSION_CODE, &boxblur_special_info, &boxblur_init_plugin },
  { PLUGIN_POST, 10, "eq2", XINE_VERSION_CODE, &eq2_special_info, &eq2_init_plugin },
  { PLUGIN_POST, 10, "unsharp", XINE_VERSION_CODE, &unsharp_special_info, &unsharp_init_plugin },
  { PLUGIN_POST, 10, "pp", XINE_VERSION_CODE, &pp_special_info, &pp_init_plugin },
  { PLUGIN_POST, 10, "noise", XINE_VERSION_CODE, &noise_special_info, &noise_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
