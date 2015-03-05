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
 */

#ifndef __COMBINED_ENIGMA_H
#define __COMBINED_ENIGMA_H



typedef struct enigma_set_video_window_data_s {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int32_t w_ref;
  int32_t h_ref;

} enigma_set_video_window_data_t;



typedef struct enigma_frame_size_changed_data_s {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  double r;

} enigma_frame_size_changed_data_t;



typedef struct enigma_select_audio_data_s {
  uint8_t channels;

} enigma_select_audio_data_t;



inline static int enigma_is_stream(xine_stream_t *stream)
{
  if (!stream
      || !stream->input_plugin
      || !stream->input_plugin->input_class)
  {
    return 0;
  }

  if (stream->input_plugin->input_class->identifier &&
      0 == strcmp(stream->input_plugin->input_class->identifier, "ENIGMA"))
    return 1;

  return 0;
}


/* plugin class initialization function */
void *init_class(xine_t *xine, void *data);
void *enigma_video_init_plugin(xine_t *xine, void *data);
void *enigma_audio_init_plugin(xine_t *xine, void *data);



#endif /* __COMBINED_ENIGMA_H */

