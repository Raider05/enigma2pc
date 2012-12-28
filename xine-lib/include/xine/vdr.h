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

#ifndef __VDR_H
#define __VDR_H


#define XINE_VDR_VERSION 901


enum funcs
{
  func_unknown = -1
  , func_nop
  , func_osd_new
  , func_osd_free
  , func_osd_show
  , func_osd_hide
  , func_osd_set_position
  , func_osd_draw_bitmap
  , func_set_color
  , func_clear
  , func_mute
  , func_set_volume
  , func_set_speed
  , func_set_prebuffer
  , func_metronom
  , func_start
  , func_wait
  , func_setup
  , func_grab_image
  , func_get_pts
  , func_flush
  , func_first_frame
  , func_still_frame
  , func_video_size
  , func_set_video_window
  , func_osd_flush
  , func_play_external
  , func_key
  , func_frame_size
  , func_reset_audio
  , func_select_audio
  , func_trick_speed_mode
  , func_get_version
  , func_discontinuity
  , func_query_capabilities
};

enum keys
{
  key_none,
  key_up,
  key_down,
  key_menu,
  key_ok,
  key_back,
  key_left,
  key_right,
  key_red,
  key_green,
  key_yellow,
  key_blue,
  key_0,
  key_1,
  key_2,
  key_3,
  key_4,
  key_5,
  key_6,
  key_7,
  key_8,
  key_9,
  key_play,
  key_pause,
  key_stop,
  key_record,
  key_fast_fwd,
  key_fast_rew,
  key_power,
  key_channel_plus,
  key_channel_minus,
  key_volume_plus,
  key_volume_minus,
  key_mute,
  key_schedule,
  key_channels,
  key_timers,
  key_recordings,
  key_setup,
  key_commands,
  key_user1,
  key_user2,
  key_user3,
  key_user4,
  key_user5,
  key_user6,
  key_user7,
  key_user8,
  key_user9,
  key_audio,
  key_info,
  key_channel_previous,
  key_next,
  key_previous,
  key_subtitles,
  key_user0,
};



typedef struct __attribute__((packed)) data_header_s
{
  uint32_t func:8;
  uint32_t len:24;
}
data_header_t;



typedef data_header_t result_header_t;
typedef data_header_t event_header_t;



typedef struct __attribute__((packed)) data_nop_s
{
  data_header_t header;
}
data_nop_t;



typedef struct __attribute__((packed)) data_osd_new_s
{
  data_header_t header;

  uint8_t  window;
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;
  uint16_t w_ref;
  uint16_t h_ref;
}
data_osd_new_t;



typedef struct __attribute__((packed)) data_osd_free_s
{
  data_header_t header;

  uint8_t window;
}
data_osd_free_t;



typedef struct __attribute__((packed)) data_osd_show_s
{
  data_header_t header;

  uint8_t window;
}
data_osd_show_t;



typedef struct __attribute__((packed)) data_osd_hide_s
{
  data_header_t header;

  uint8_t window;
}
data_osd_hide_t;



typedef struct __attribute__((packed)) data_osd_flush_s
{
  data_header_t header;
}
data_osd_flush_t;



typedef struct __attribute__((packed)) data_play_external_s
{
  data_header_t header;
}
data_play_external_t;



typedef struct __attribute__((packed)) data_osd_set_position_s
{
  data_header_t header;

  uint8_t window;
  int16_t x;
  int16_t y;
}
data_osd_set_position_t;



typedef struct __attribute__((packed)) data_osd_draw_bitmap_s
{
  data_header_t header;

  uint8_t  window;
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;
  uint8_t  argb;
}
data_osd_draw_bitmap_t;



typedef struct __attribute__((packed)) data_set_color_s
{
  data_header_t header;

  uint8_t window;
  uint8_t index;
  uint8_t num;
}
data_set_color_t;



typedef struct __attribute__((packed)) data_flush_s
{
  data_header_t header;

  int32_t ms_timeout;
  uint8_t just_wait;
}
data_flush_t;



typedef struct __attribute__((packed)) result_flush_s
{
  result_header_t header;

  uint8_t timed_out;
}
result_flush_t;



typedef struct __attribute__((packed)) data_clear_s
{
  data_header_t header;

  int32_t n;
  int8_t s;
  uint8_t i;
}
data_clear_t;



typedef struct __attribute__((packed)) data_mute_s
{
  data_header_t header;

  uint8_t mute;
}
data_mute_t;



typedef struct __attribute__((packed)) data_set_volume_s
{
  data_header_t header;

  uint8_t volume;
}
data_set_volume_t;



typedef struct __attribute__((packed)) data_set_speed_s
{
  data_header_t header;

  int32_t speed;
}
data_set_speed_t;



typedef struct __attribute__((packed)) data_set_prebuffer_s
{
  data_header_t header;

  uint32_t prebuffer;
}
data_set_prebuffer_t;



typedef struct __attribute__((packed)) data_metronom_s
{
  data_header_t header;

  int64_t  pts;
  uint32_t flags;
}
data_metronom_t;



typedef struct __attribute__((packed)) data_start_s
{
  data_header_t header;
}
data_start_t;



typedef struct __attribute__((packed)) data_wait_s
{
  data_header_t header;
  uint8_t id;
}
data_wait_t;



typedef struct __attribute__((packed)) result_wait_s
{
  result_header_t header;
}
result_wait_t;



#define XINE_VDR_VOLUME_IGNORE    0
#define XINE_VDR_VOLUME_CHANGE_HW 1
#define XINE_VDR_VOLUME_CHANGE_SW 2

#define XINE_VDR_MUTE_IGNORE   0
#define XINE_VDR_MUTE_EXECUTE  1
#define XINE_VDR_MUTE_SIMULATE 2

typedef struct __attribute__((packed)) data_setup_s
{
  data_header_t header;

  uint8_t osd_unscaled_blending;
  uint8_t volume_mode;
  uint8_t mute_mode;
  uint16_t image4_3_zoom_x;
  uint16_t image4_3_zoom_y;
  uint16_t image16_9_zoom_x;
  uint16_t image16_9_zoom_y;
}
data_setup_t;



typedef struct __attribute__((packed)) data_first_frame_s
{
  data_header_t header;
}
data_first_frame_t;



typedef struct __attribute__((packed)) data_still_frame_s
{
  data_header_t header;
}
data_still_frame_t;



typedef struct __attribute__((packed)) data_set_video_window_s
{
  data_header_t header;

  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
  uint32_t w_ref;
  uint32_t h_ref;
}
data_set_video_window_t;



typedef struct __attribute__((packed)) data_grab_image_s
{
  data_header_t header;
}
data_grab_image_t;



typedef struct __attribute__((packed)) result_grab_image_s
{
  result_header_t header;

  int32_t width;
  int32_t height;
  int32_t ratio;
  int32_t format;
  int32_t interlaced;
  int32_t crop_left;
  int32_t crop_right;
  int32_t crop_top;
  int32_t crop_bottom;
}
result_grab_image_t;



typedef struct __attribute__((packed)) data_get_pts_s
{
  data_header_t header;
  int32_t ms_timeout;
}
data_get_pts_t;



typedef struct __attribute__((packed)) result_get_pts_s
{
  result_header_t header;

  int64_t pts;
  int8_t queued;
}
result_get_pts_t;



typedef struct __attribute__((packed)) data_get_version_s
{
  data_header_t header;
}
data_get_version_t;



typedef struct __attribute__((packed)) result_get_version_s
{
  result_header_t header;

  int32_t version;
}
result_get_version_t;



typedef struct __attribute__((packed)) data_video_size_s
{
  data_header_t header;
}
data_video_size_t;



typedef struct __attribute__((packed)) result_video_size_s
{
  result_header_t header;

  int32_t left;
  int32_t top;
  int32_t width;
  int32_t height;
  int32_t ratio;
  int32_t zoom_x;
  int32_t zoom_y;
}
result_video_size_t;



typedef struct __attribute__((packed)) data_reset_audio_s
{
  data_header_t header;
}
data_reset_audio_t;



typedef struct __attribute__((packed)) event_key_s
{
  event_header_t header;

  uint32_t key;
}
event_key_t;



typedef struct __attribute__((packed)) event_frame_size_s
{
  event_header_t header;

  int32_t left;
  int32_t top;
  int32_t width;
  int32_t height;
  int32_t zoom_x;
  int32_t zoom_y;
}
event_frame_size_t;



typedef struct __attribute__((packed)) event_play_external_s
{
  event_header_t header;

  uint32_t key;
}
event_play_external_t;



typedef struct __attribute__((packed)) data_select_audio_s
{
  data_header_t header;

  uint8_t channels;
}
data_select_audio_t;



typedef struct __attribute__((packed)) data_trick_speed_mode_s
{
  data_header_t header;

  uint8_t on;
}
data_trick_speed_mode_t;



typedef struct __attribute__((packed)) event_discontinuity_s
{
  event_header_t header;

  int32_t type;
}
event_discontinuity_t;



typedef struct __attribute__((packed)) data_query_capabilities_s
{
  data_header_t header;
}
data_query_capabilities_t;



typedef struct __attribute__((packed)) result_query_capabilities_s
{
  result_header_t header;

  uint8_t osd_max_num_windows;
  uint8_t osd_palette_max_depth;
  uint8_t osd_palette_is_shared;
  uint8_t osd_supports_argb_layer;
  uint8_t osd_supports_custom_extent;
}
result_query_capabilities_t;



typedef union __attribute__((packed)) data_union_u
{
  data_header_t             header;
  data_nop_t                nop;
  data_osd_new_t            osd_new;
  data_osd_free_t           osd_free;
  data_osd_show_t           osd_show;
  data_osd_hide_t           osd_hide;
  data_osd_set_position_t   osd_set_position;
  data_osd_draw_bitmap_t    osd_draw_bitmap;
  data_set_color_t          set_color;
  data_flush_t              flush;
  data_clear_t              clear;
  data_mute_t               mute;
  data_set_volume_t         set_volume;
  data_set_speed_t          set_speed;
  data_set_prebuffer_t      set_prebuffer;
  data_metronom_t           metronom;
  data_start_t              start;
  data_wait_t               wait;
  data_setup_t              setup;
  data_grab_image_t         grab_image;
  data_get_pts_t            get_pts;
  data_first_frame_t        first_frame;
  data_still_frame_t        still_frame;
  data_video_size_t         video_size;
  data_set_video_window_t   set_video_window;
  data_osd_flush_t          osd_flush;
  data_play_external_t      play_external;
  data_reset_audio_t        reset_audio;
  data_select_audio_t       select_audio;
  data_trick_speed_mode_t   trick_speed_mode;
  data_get_version_t        get_version;
  data_query_capabilities_t query_capabilities;
}
data_union_t;



typedef union __attribute__((packed)) result_union_u
{
  result_header_t             header;
  result_grab_image_t         grab_image;
  result_get_pts_t            get_pts;
  result_flush_t              flush;
  result_video_size_t         video_size;
  result_get_version_t        get_version;
  result_wait_t               wait;
  result_query_capabilities_t query_capabilities;
}
result_union_t;



typedef union __attribute__((packed)) event_union_u
{
  event_header_t          header;
  event_key_t             key;
  event_frame_size_t      frame_size;
  event_play_external_t   play_external;
  event_discontinuity_t   discontinuity;
}
event_union_t;



#endif /* __VDR_H */

