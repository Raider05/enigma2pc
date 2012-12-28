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
 *
 * This file contains plugin entries for several demuxers used in games
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/demux.h>

#include "group_games.h"

/*
 * exported plugin catalog entries
 */

static const demuxer_info_t demux_info_eawve = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_idcin = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_ipmovie = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_vqa = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_wc3movie = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_roq = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_str = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_film = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_smjpeg = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_fourxm = {
  10                       /* priority */
};

static const demuxer_info_t demux_info_vmd = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "wve",      XINE_VERSION_CODE, &demux_info_eawve,    demux_eawve_init_plugin},
  { PLUGIN_DEMUX, 27, "idcin",    XINE_VERSION_CODE, &demux_info_idcin,    demux_idcin_init_plugin },
  { PLUGIN_DEMUX, 27, "ipmovie",  XINE_VERSION_CODE, &demux_info_ipmovie,  demux_ipmovie_init_plugin },
  { PLUGIN_DEMUX, 27, "vqa",      XINE_VERSION_CODE, &demux_info_vqa,      demux_vqa_init_plugin },
  { PLUGIN_DEMUX, 27, "wc3movie", XINE_VERSION_CODE, &demux_info_wc3movie, demux_wc3movie_init_plugin },
  { PLUGIN_DEMUX, 27, "roq",      XINE_VERSION_CODE, &demux_info_roq,      demux_roq_init_plugin },
  { PLUGIN_DEMUX, 27, "str",      XINE_VERSION_CODE, &demux_info_str,      demux_str_init_plugin },
  { PLUGIN_DEMUX, 27, "film",     XINE_VERSION_CODE, &demux_info_film,     demux_film_init_plugin },
  { PLUGIN_DEMUX, 27, "smjpeg",   XINE_VERSION_CODE, &demux_info_smjpeg,   demux_smjpeg_init_plugin },
  { PLUGIN_DEMUX, 27, "fourxm",   XINE_VERSION_CODE, &demux_info_fourxm,   demux_fourxm_init_plugin },
  { PLUGIN_DEMUX, 27, "vmd",      XINE_VERSION_CODE, &demux_info_vmd,      demux_vmd_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
