/*
 * Copyright (C) 2007 the xine project
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
 * xine interface to libwavpack by Diego Pettenò <flameeyes@gmail.com>
 */

#include <xine/xine_internal.h>
#include "wavpack_combined.h"

static const demuxer_info_t demux_info_wv = {
  0			/* priority */
};

static const uint32_t audio_types[] = {
  BUF_AUDIO_WAVPACK, 0
 };

static const decoder_info_t decoder_info_wv = {
  audio_types,         /* supported types */
  8                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "wavpack", XINE_VERSION_CODE, &demux_info_wv, demux_wv_init_plugin },
  { PLUGIN_AUDIO_DECODER, 16, "wavpackdec", XINE_VERSION_CODE, &decoder_info_wv, decoder_wavpack_init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
