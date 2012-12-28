/*
 * Copyright (C) 2000-2008 the xine project
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

/**
 * @file
 * @brief Declaration of internal, private functions for xine-lib.
 *
 * @internal These functions should not be used by neither plugins nor
 * frontends.
 */

#ifndef XINE_PRIVATE_H__
#define XINE_PRIVATE_H__

#ifndef XINE_LIBRARY_COMPILE
# error xine_private.h is for libxine private use only!
#endif

#include <config.h>
#include <xine/xine_internal.h>

#if SUPPORT_ATTRIBUTE_VISIBILITY_INTERNAL
# define INTERNAL __attribute__((visibility("internal")))
#elif SUPPORT_ATTRIBUTE_VISIBILITY_DEFAULT
# define INTERNAL __attribute__((__visibility__("default")))
#else
# define INTERNAL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup load_plugins Plugins loading
 * @brief Functions related with plugins loading.
 */

/**
 * @ingroup load_plugins
 * @brief Load plugins into catalog
 * @param this xine instance
 *
 * All input and demux plugins will be fully loaded and initialized.
 * Decoder plugins are loaded on demand. Video/audio output plugins
 * have special load/probe functions
 */
void _x_scan_plugins (xine_t *this) INTERNAL;

/**
 * @ingroup load_plugins
 * @brief Dispose (shutdown) all currently loaded plugins
 * @param this xine instance
 */
void _x_dispose_plugins (xine_t *this) INTERNAL;

///@{
/**
 * @defgroup
 * @brief find and instantiate input and demux plugins
 */
input_plugin_t *_x_find_input_plugin (xine_stream_t *stream, const char *mrl) INTERNAL;
demux_plugin_t *_x_find_demux_plugin (xine_stream_t *stream, input_plugin_t *input) INTERNAL;
demux_plugin_t *_x_find_demux_plugin_by_name (xine_stream_t *stream, const char *name, input_plugin_t *input) INTERNAL;
demux_plugin_t *_x_find_demux_plugin_last_probe(xine_stream_t *stream, const char *last_demux_name, input_plugin_t *input) INTERNAL;
input_plugin_t *_x_rip_plugin_get_instance (xine_stream_t *stream, const char *filename) INTERNAL;
input_plugin_t *_x_cache_plugin_get_instance (xine_stream_t *stream) INTERNAL;
void _x_free_input_plugin (xine_stream_t *stream, input_plugin_t *input) INTERNAL;
void _x_free_demux_plugin (xine_stream_t *stream, demux_plugin_t *demux) INTERNAL;
///@}

///@{
/**
 * @defgroup
 * @brief  create decoder fifos and threads
*/

int _x_video_decoder_init           (xine_stream_t *stream) INTERNAL;
void _x_video_decoder_shutdown      (xine_stream_t *stream) INTERNAL;

int _x_audio_decoder_init           (xine_stream_t *stream) INTERNAL;
void _x_audio_decoder_shutdown      (xine_stream_t *stream) INTERNAL;
///@}

/**
 * @brief Benchmark available memcpy methods
 */
void xine_probe_fast_memcpy(xine_t *xine) INTERNAL;

/**
 * @brief Make file descriptors and sockets uninheritable
 */
int _x_set_file_close_on_exec(int fd) INTERNAL;

int _x_set_socket_close_on_exec(int s) INTERNAL;

#ifdef __cplusplus
}
#endif

#endif
