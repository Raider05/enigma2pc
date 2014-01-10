/*
 * Copyright (C) 2001-2014 the xine project
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
 * xine decoder plugin using ffmpeg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>

#include "ffmpeg_decoder.h"
#include "ffmpeg_compat.h"

#ifdef HAVE_AVFORMAT
#include <libavformat/avformat.h> // av_register_all()
#endif

/*
 * common initialisation
 */

pthread_once_t once_control = PTHREAD_ONCE_INIT;
pthread_mutex_t ffmpeg_lock;

void init_once_routine(void) {
  pthread_mutex_init(&ffmpeg_lock, NULL);
  avcodec_init();
  avcodec_register_all();

#ifdef HAVE_AVFORMAT
  av_register_all();
  avformat_network_init();
#endif
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "ffmpegvideo", XINE_VERSION_CODE, &dec_info_ffmpeg_video, init_video_plugin },
  { PLUGIN_VIDEO_DECODER, 19, "ffmpeg-wmv8", XINE_VERSION_CODE, &dec_info_ffmpeg_wmv8, init_video_plugin },
  { PLUGIN_VIDEO_DECODER, 19, "ffmpeg-wmv9", XINE_VERSION_CODE, &dec_info_ffmpeg_wmv9, init_video_plugin },
  { PLUGIN_AUDIO_DECODER, 16, "ffmpegaudio", XINE_VERSION_CODE, &dec_info_ffmpeg_audio, init_audio_plugin },
#ifdef HAVE_AVFORMAT
  { PLUGIN_INPUT,         18, INPUT_AVIO_ID,     XINE_VERSION_CODE, &input_info_avio,     init_avio_input_plugin },
  { PLUGIN_INPUT,         18, DEMUX_AVFORMAT_ID, XINE_VERSION_CODE, &input_info_avformat, init_avformat_input_plugin },
  { PLUGIN_DEMUX,         27, DEMUX_AVFORMAT_ID, XINE_VERSION_CODE, &demux_info_avformat, init_avformat_demux_plugin },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
