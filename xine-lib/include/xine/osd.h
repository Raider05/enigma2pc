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
 * OSD stuff (text and graphic primitives)
 */

#ifndef HAVE_OSD_H
#define HAVE_OSD_H

#ifdef HAVE_ICONV
#  include <iconv.h>
#endif

#include <xine/video_overlay.h>

typedef struct osd_object_s osd_object_t;
typedef struct osd_renderer_s osd_renderer_t;
typedef struct osd_font_s osd_font_t;
typedef struct osd_ft2context_s osd_ft2context_t;

struct osd_object_s {
  osd_object_t *next;
  osd_renderer_t *renderer;

  int width, height;    /* work area dimentions */
  uint8_t *area;        /* work area */
  int area_touched;     /* work area was used for painting */
  int display_x,display_y;  /* where to display it in screen */

  /* video output area within osd extent */
  int video_window_x, video_window_y;
  int video_window_width, video_window_height;

  /* extent of reference coordinate system */
  int extent_width, extent_height;

  /* clipping box inside work area */
  int x1, y1;
  int x2, y2;

  uint32_t color[OVL_PALETTE_SIZE];	/* color lookup table  */
  uint8_t trans[OVL_PALETTE_SIZE];	/* mixer key table */

#ifdef HAVE_ICONV
  iconv_t cd;                           /* iconv handle of encoding */
  char *encoding;                       /* name of encoding */
#endif

  osd_font_t *font;
  osd_ft2context_t *ft2;


  /* this holds an optional ARGB overlay, which
   * is only be used by supported video_out modules.
   * right now this is only vdpau */
  argb_layer_t *argb_layer;

  int32_t handle;
};

/* this one is public */
struct xine_osd_s {
  osd_object_t osd;
};

struct osd_renderer_s {

  xine_stream_t              *stream;

  /*
   * open a new osd object. this will allocated an empty (all zero) drawing
   * area where graphic primitives may be used.
   * It is ok to specify big width and height values. The render will keep
   * track of the smallest changed area to not generate too big overlays.
   * A default palette is initialized (i sugest keeping color 0 as transparent
   * for the sake of simplicity)
   */
  osd_object_t* (*new_object) (osd_renderer_t *self, int width, int height);

  /*
   * free osd object
   */
  void (*free_object) (osd_object_t *osd_to_close);


  /*
   * send the osd to be displayed at given pts (0=now)
   * the object is not changed. there may be subsequent drawing  on it.
   */
  int (*show) (osd_object_t *osd, int64_t vpts );

  /*
   * send event to hide osd at given pts (0=now)
   * the object is not changed. there may be subsequent drawing  on it.
   */
  int (*hide) (osd_object_t *osd, int64_t vpts );

  /*
   * draw point.
   */
  void (*point) (osd_object_t *osd, int x, int y, int color);

       /*
   * Bresenham line implementation on osd object
   */
  void (*line) (osd_object_t *osd,
		int x1, int y1, int x2, int y2, int color );

  /*
   * filled rectangle
   */
  void (*filled_rect) (osd_object_t *osd,
		       int x1, int y1, int x2, int y2, int color );

  /*
   * set palette (color and transparency)
   */
  void (*set_palette) (osd_object_t *osd, const uint32_t *color, const uint8_t *trans );

  /*
   * set on existing text palette
   * (-1 to set used specified palette)
   *
   * color_base specifies the first color index to use for this text
   * palette. The OSD palette is then modified starting at this
   * color index, up to the size of the text palette.
   *
   * Use OSD_TEXT1, OSD_TEXT2, ... for some preasssigned color indices.
   */
  void (*set_text_palette) (osd_object_t *osd, int palette_number,
			    int color_base );

  /*
   * get palette (color and transparency)
   */
  void (*get_palette) (osd_object_t *osd, uint32_t *color,
		       uint8_t *trans);

  /*
   * set position were overlay will be blended
   */
  void (*set_position) (osd_object_t *osd, int x, int y);

  /*
   * set the font of osd object
   */

  int (*set_font) (osd_object_t *osd, const char *fontname, int size);

  /*
   * set encoding of text
   *
   * NULL ... no conversion (iso-8859-1)
   * ""   ... locale encoding
   */
  int (*set_encoding) (osd_object_t *osd, const char *encoding);

  /*
   * render text in current encoding on x,y position
   * no \n yet
   *
   * The text is assigned the colors starting at the index specified by
   * color_base up to the size of the text palette.
   *
   * Use OSD_TEXT1, OSD_TEXT2, ... for some preasssigned color indices.
   */
  int (*render_text) (osd_object_t *osd, int x1, int y1,
		      const char *text, int color_base);

  /*
   * get width and height of how text will be renderized
   */
  int (*get_text_size) (osd_object_t *osd, const char *text,
			int *width, int *height);

  /*
   * close osd rendering engine
   * loaded fonts are unloaded
   * osd objects are closed
   */
  void (*close) (osd_renderer_t *self);

  /*
   * clear an osd object (empty drawing area)
   */
  void (*clear) (osd_object_t *osd );

  /*
   * paste a bitmap with optional palette mapping
   */
  void (*draw_bitmap) (osd_object_t *osd, uint8_t *bitmap,
		       int x1, int y1, int width, int height,
		       uint8_t *palette_map);

  /*
   * send the osd to be displayed (unscaled) at given pts (0=now)
   * the object is not changed. there may be subsequent drawing  on it.
   * overlay is blended at output (screen) resolution.
   */
  int (*show_unscaled) (osd_object_t *osd, int64_t vpts );



  int (*show_scaled) (osd_object_t *osd, int64_t vpts );

  /*
   * see xine.h for defined XINE_OSD_CAP_ values.
   */
  uint32_t (*get_capabilities) (osd_object_t *osd);

  /*
   * define extent of reference coordinate system for video
   * resolution independent osds. both sizes must be > 0 to
   * take effect. otherwise, video resolution will be used.
   */
  void (*set_extent) (osd_object_t *osd, int extent_width, int extent_height);

  /*
   * set an argb buffer to be blended into video
   * the buffer must exactly match the osd dimensions
   * and stay valid while the osd is on screen. pass
   * a NULL pointer to safely remove the buffer from
   * the osd layer. only the dirty area  will be
   * updated on screen. for convinience the whole
   * osd object will be considered dirty when setting
   * a different buffer pointer.
   * see also XINE_OSD_CAP_ARGB_LAYER
   */
  void (*set_argb_buffer) (osd_object_t *osd, uint32_t *argb_buffer,
                           int dirty_x, int dirty_y, int dirty_width, int dirty_height);

  /*
   * osd video window defines an area withing osd extent where the
   * video shall be scaled to while an osd is displayed on screen.
   * both width and height must be > 0 to take effect.
   */
  void (*set_video_window) (osd_object_t *osd,
                            int window_x, int window_y, int window_width, int window_height);

  /* private stuff */

  pthread_mutex_t             osd_mutex;
  video_overlay_event_t       event;
  osd_object_t               *osds;          /* instances of osd */
  osd_font_t                 *fonts;         /* loaded fonts */
  int                        textpalette;    /* default textpalette */

};

/*
 *   initialize the osd rendering engine
 */
osd_renderer_t *_x_osd_renderer_init( xine_stream_t *stream ) XINE_MALLOC;


/*
 * The size of a text palette
 */

#define TEXT_PALETTE_SIZE 11

/*
 * Preassigned color indices for rendering text
 * (more can be added, not exceeding OVL_PALETTE_SIZE)
 */

#define OSD_TEXT1 (0 * TEXT_PALETTE_SIZE)
#define OSD_TEXT2 (1 * TEXT_PALETTE_SIZE)
#define OSD_TEXT3 (2 * TEXT_PALETTE_SIZE)
#define OSD_TEXT4 (3 * TEXT_PALETTE_SIZE)
#define OSD_TEXT5 (4 * TEXT_PALETTE_SIZE)
#define OSD_TEXT6 (5 * TEXT_PALETTE_SIZE)
#define OSD_TEXT7 (6 * TEXT_PALETTE_SIZE)
#define OSD_TEXT8 (7 * TEXT_PALETTE_SIZE)
#define OSD_TEXT9 (8 * TEXT_PALETTE_SIZE)
#define OSD_TEXT10 (9 * TEXT_PALETTE_SIZE)

/*
 * Defined palettes for rendering osd text
 * (more can be added later)
 */

#define NUMBER_OF_TEXT_PALETTES 4
#define TEXTPALETTE_WHITE_BLACK_TRANSPARENT    0
#define TEXTPALETTE_WHITE_NONE_TRANSPARENT     1
#define TEXTPALETTE_WHITE_NONE_TRANSLUCID      2
#define TEXTPALETTE_YELLOW_BLACK_TRANSPARENT   3

#endif

