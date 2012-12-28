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
 * catalog for audio filter plugins
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>

#include "audio_filters.h"


static const post_info_t upmix_special_info      = { XINE_POST_TYPE_AUDIO_FILTER };
static const post_info_t upmix_mono_special_info = { XINE_POST_TYPE_AUDIO_FILTER };
static const post_info_t stretch_special_info    = { XINE_POST_TYPE_AUDIO_FILTER };
static const post_info_t volnorm_special_info    = { XINE_POST_TYPE_AUDIO_FILTER };


const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST, 10, "upmix",      XINE_VERSION_CODE, &upmix_special_info,      &upmix_init_plugin },
  { PLUGIN_POST, 10, "upmix_mono", XINE_VERSION_CODE, &upmix_mono_special_info, &upmix_mono_init_plugin },
  { PLUGIN_POST, 10, "stretch",    XINE_VERSION_CODE, &stretch_special_info,    &stretch_init_plugin },
  { PLUGIN_POST, 10, "volnorm",    XINE_VERSION_CODE, &volnorm_special_info,    &volnorm_init_plugin },
  { PLUGIN_NONE, 0,  "",           0,                 NULL,                     NULL }
};
