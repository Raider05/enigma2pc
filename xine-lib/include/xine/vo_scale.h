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
 * vo_scale.h
 *
 * keeps video scaling information
 */

#ifndef HAVE_VO_SCALE_H
#define HAVE_VO_SCALE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xine/configfile.h>

typedef struct {
  int x, y;
  int w, h;
} vo_scale_rect_t;

struct vo_scale_s {

  /* true if driver supports frame zooming */
  int                support_zoom;

  /* forces direct mapping between frame pixels and screen pixels */
  int                scaling_disabled;

  /* size / aspect ratio calculations */

  /*
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   * units: frame pixels
   */
  int                delivered_width;
  int                delivered_height;
  double             delivered_ratio;

  /*
   * required cropping:
   * units: frame pixels
   */
  int                crop_left;
  int                crop_right;
  int                crop_top;
  int                crop_bottom;

  /*
   * displayed part of delivered images,
   * taking zoom into account
   * units: frame pixels
   */
  int                displayed_xoffset;
  int                displayed_yoffset;
  int                displayed_width;
  int                displayed_height;
  double             zoom_factor_x, zoom_factor_y;

  /*
   * user's aspect selection
   */
  int                user_ratio;

  /*
   * "gui" size / offset:
   * what gui told us about where to display the video
   * units: screen pixels
   */
  int                gui_x, gui_y;
  int                gui_width, gui_height;
  int                gui_win_x, gui_win_y;

  /* */
  int                force_redraw;

  /*
   * video + display pixel aspect
   * One pixel of height 1 has this width
   * This may be corrected by the driver in order to fit the video seamlessly
   */
  double             gui_pixel_aspect;
  double             video_pixel_aspect;

  /*
   * "output" size:
   *
   * this is finally the ideal size "fitted" into the
   * gui size while maintaining the aspect ratio
   * units: screen pixels
   */
  int                output_width;
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;


  /* gui callbacks */

  void              *user_data;
  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
                           double video_pixel_aspect,
			   int *dest_x, int *dest_y,
			   int *dest_width, int *dest_height,
                           double *dest_pixel_aspect,
			   int *win_x, int *win_y);

  void (*dest_size_cb) (void *user_data,
			int video_width, int video_height,
                        double video_pixel_aspect,
			int *dest_width, int *dest_height,
                        double *dest_pixel_aspect);

  /* borders */
  vo_scale_rect_t     border[4];

  /*
   * border ratios to determine image position in the
   * viewport; these are set by user config
   */
  double             output_horizontal_position;
  double             output_vertical_position;

};

typedef struct vo_scale_s vo_scale_t;


/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

void _x_vo_scale_compute_ideal_size (vo_scale_t *self) XINE_PROTECTED;


/*
 * make ideal width/height "fit" into the gui
 */

void _x_vo_scale_compute_output_size (vo_scale_t *self) XINE_PROTECTED;

/*
 * return true if a redraw is needed due resizing, zooming,
 * aspect ratio changing, etc.
 */

int _x_vo_scale_redraw_needed (vo_scale_t *self) XINE_PROTECTED;

/*
 *
 */

void _x_vo_scale_translate_gui2video(vo_scale_t *self,
				     int x, int y,
				     int *vid_x, int *vid_y) XINE_PROTECTED;

/*
 * Returns description of a given ratio code
 */

extern const char _x_vo_scale_aspect_ratio_name_table[][8] XINE_PROTECTED;

/*
 * initialize rescaling struct
 */

void _x_vo_scale_init(vo_scale_t *self, int support_zoom,
		      int scaling_disabled, config_values_t *config ) XINE_PROTECTED;

#ifdef __cplusplus
}
#endif

#endif
