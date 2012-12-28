/*
 * Copyright (C) 2003, 2007 the xine project
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
 * xcbosd.c, use X11 Nonrectangular Window Shape Extension to draw xine OSD
 *
 * Nov 2003 - Miguel Freitas
 * Feb 2007 - ported to xcb by Christoph Pfister
 *
 * based on ideas and code of
 * xosd Copyright (c) 2000 Andre Renaud (andre@ignavus.net)
 *
 * colorkey support by Yann Vernier
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <assert.h>

#include <netinet/in.h>

#include <xcb/shape.h>

#define LOG_MODULE "xcbosd"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include "xcbosd.h"

struct xcbosd
{
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  enum xcbosd_mode mode;

  union {
    struct {
      xcb_window_t window;
      xcb_pixmap_t mask_bitmap;
      xcb_gc_t mask_gc;
      xcb_gc_t mask_gc_back;
      int mapped;
    } shaped;
    struct {
      uint32_t colorkey;
      vo_scale_t *sc;
    } colorkey;
  } u;
  xcb_window_t window;
  unsigned int depth;
  xcb_pixmap_t bitmap;
  xcb_visualid_t visual;
  xcb_colormap_t cmap;

  xcb_gc_t gc;

  int width;
  int height;
  int x;
  int y;
  enum {DRAWN, WIPED, UNDEFINED} clean;
  xine_t *xine;
};


void xcbosd_expose(xcbosd *osd)
{
  assert (osd);

  lprintf("expose (state:%d)\n", osd->clean );

  switch (osd->mode) {
    case XCBOSD_SHAPED:
      xcb_shape_mask(osd->connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		     osd->u.shaped.window, 0, 0, osd->u.shaped.mask_bitmap);
      if( osd->clean==DRAWN ) {

	if( !osd->u.shaped.mapped ) {
	  unsigned int stack_mode = XCB_STACK_MODE_ABOVE;
	  xcb_configure_window(osd->connection, osd->u.shaped.window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);
	  xcb_map_window(osd->connection, osd->u.shaped.window);
	}
	osd->u.shaped.mapped = 1;

	xcb_copy_area(osd->connection, osd->bitmap, osd->u.shaped.window,
		      osd->gc, 0, 0, 0, 0, osd->width, osd->height);
      } else {
	if( osd->u.shaped.mapped )
	  xcb_unmap_window(osd->connection, osd->u.shaped.window);
	osd->u.shaped.mapped = 0;
      }
      break;
    case XCBOSD_COLORKEY:
      if( osd->clean!=UNDEFINED )
	xcb_copy_area(osd->connection, osd->bitmap, osd->window, osd->gc, 0, 0,
		      0, 0, osd->width, osd->height);
  }
}


void xcbosd_resize(xcbosd *osd, int width, int height)
{
  assert (osd);
  assert (width);
  assert (height);

  lprintf("resize old:%dx%d new:%dx%d\n", osd->width, osd->height, width, height );

  osd->width = width;
  osd->height = height;

  xcb_free_pixmap(osd->connection, osd->bitmap);
  switch(osd->mode) {
    case XCBOSD_SHAPED: {
      unsigned int window_config[] = { osd->width, osd->height };
      xcb_configure_window(osd->connection, osd->u.shaped.window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, window_config);
      xcb_free_pixmap(osd->connection, osd->u.shaped.mask_bitmap);
      osd->u.shaped.mask_bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, 1, osd->u.shaped.mask_bitmap, osd->u.shaped.window, osd->width, osd->height);
      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->u.shaped.window, osd->width, osd->height);
      break;
      }
    case XCBOSD_COLORKEY:
      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->window, osd->width, osd->height);
      break;
  }

  osd->clean = UNDEFINED;
  xcbosd_clear(osd);
}

void xcbosd_drawable_changed(xcbosd *osd, xcb_window_t window)
{
  xcb_get_geometry_cookie_t get_geometry_cookie;
  xcb_get_geometry_reply_t *get_geometry_reply;

  assert (osd);

  lprintf("drawable changed\n");

/*
  Do I need to recreate the GC's??

  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/
  xcb_free_pixmap(osd->connection, osd->bitmap);
  xcb_free_colormap(osd->connection, osd->cmap);

  /* we need to call XSync(), because otherwise, calling XDestroyWindow()
     on the parent window could destroy our OSD window twice !! */
  /* XSync (osd->display, False); FIXME don't think that we need that --pfister */

  osd->window = window;

  get_geometry_cookie = xcb_get_geometry(osd->connection, osd->window);
  get_geometry_reply = xcb_get_geometry_reply(osd->connection, get_geometry_cookie, NULL);
  osd->depth = get_geometry_reply->depth;
  osd->width = get_geometry_reply->width;
  osd->height = get_geometry_reply->height;
  free(get_geometry_reply);

  assert(osd->width);
  assert(osd->height);

  switch(osd->mode) {
    case XCBOSD_SHAPED: {
      xcb_free_pixmap(osd->connection, osd->u.shaped.mask_bitmap);
      xcb_destroy_window(osd->connection, osd->u.shaped.window);

      unsigned int window_params[] = { osd->screen->black_pixel, 1, XCB_EVENT_MASK_EXPOSURE };
      osd->u.shaped.window = xcb_generate_id(osd->connection);
      xcb_create_window(osd->connection, XCB_COPY_FROM_PARENT, osd->u.shaped.window,
			osd->window, 0, 0, osd->width, osd->height, 0, XCB_COPY_FROM_PARENT,
			XCB_COPY_FROM_PARENT, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
			window_params);

      osd->u.shaped.mapped = 0;

      osd->u.shaped.mask_bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, 1, osd->u.shaped.mask_bitmap, osd->u.shaped.window, osd->width, osd->height);

      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->u.shaped.window, osd->width, osd->height);

      osd->cmap = xcb_generate_id(osd->connection);
      xcb_create_colormap(osd->connection, XCB_COLORMAP_ALLOC_NONE, osd->cmap, osd->u.shaped.window, osd->visual);
      break;
      }
    case XCBOSD_COLORKEY:
      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->window, osd->width, osd->height);
      osd->cmap = xcb_generate_id(osd->connection);
      xcb_create_colormap(osd->connection, XCB_COLORMAP_ALLOC_NONE, osd->cmap, osd->window, osd->visual);

      break;
  }

  osd->clean = UNDEFINED;
  /* do not xcbosd_clear() here: osd->u.colorkey.sc has not being updated yet */
}

xcbosd *xcbosd_create(xine_t *xine, xcb_connection_t *connection, xcb_screen_t *screen, xcb_window_t window, enum xcbosd_mode mode)
{
  xcbosd *osd;

  xcb_get_geometry_cookie_t get_geometry_cookie;
  xcb_get_geometry_reply_t *get_geometry_reply;

  xcb_void_cookie_t generic_cookie;
  xcb_generic_error_t *generic_error;

  osd = calloc(1, sizeof(xcbosd));
  if (!osd)
    return NULL;

  osd->mode = mode;
  osd->xine = xine;
  osd->connection = connection;
  osd->screen = screen;
  osd->window = window;

  osd->visual = osd->screen->root_visual;

  get_geometry_cookie = xcb_get_geometry(osd->connection, osd->window);
  get_geometry_reply = xcb_get_geometry_reply(osd->connection, get_geometry_cookie, NULL);
  osd->depth = get_geometry_reply->depth;
  osd->width = get_geometry_reply->width;
  osd->height = get_geometry_reply->height;
  free(get_geometry_reply);

  assert(osd->width);
  assert(osd->height);

  switch (mode) {
    case XCBOSD_SHAPED: {
      const xcb_query_extension_reply_t *query_extension_reply = xcb_get_extension_data(osd->connection, &xcb_shape_id);

      if (!query_extension_reply || !query_extension_reply->present) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: XShape extension not available. unscaled overlay disabled.\n"));
	goto error2;
      }

      unsigned int window_params[] = { osd->screen->black_pixel, 1, XCB_EVENT_MASK_EXPOSURE };
      osd->u.shaped.window = xcb_generate_id(osd->connection);
      generic_cookie = xcb_create_window_checked(osd->connection, XCB_COPY_FROM_PARENT, osd->u.shaped.window,
			osd->window, 0, 0, osd->width, osd->height, 0, XCB_COPY_FROM_PARENT,
			XCB_COPY_FROM_PARENT, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
			window_params);
      generic_error = xcb_request_check(osd->connection, generic_cookie);

      if (generic_error != NULL) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating window. unscaled overlay disabled.\n"));
	free(generic_error);
	goto error_window;
      }

      osd->u.shaped.mask_bitmap = xcb_generate_id(osd->connection);
      generic_cookie = xcb_create_pixmap_checked(osd->connection, 1, osd->u.shaped.mask_bitmap, osd->u.shaped.window, osd->width, osd->height);
      generic_error = xcb_request_check(osd->connection, generic_cookie);

      if (generic_error != NULL) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating pixmap. unscaled overlay disabled.\n"));
	free(generic_error);
	goto error_aftermaskbitmap;
      }

      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->u.shaped.window, osd->width, osd->height);
      osd->gc = xcb_generate_id(osd->connection);
      xcb_create_gc(osd->connection, osd->gc, osd->u.shaped.window, 0, NULL);

      osd->u.shaped.mask_gc = xcb_generate_id(osd->connection);
      xcb_create_gc(osd->connection, osd->u.shaped.mask_gc, osd->u.shaped.mask_bitmap, XCB_GC_FOREGROUND, &osd->screen->white_pixel);

      osd->u.shaped.mask_gc_back = xcb_generate_id(osd->connection);
      xcb_create_gc(osd->connection, osd->u.shaped.mask_gc_back, osd->u.shaped.mask_bitmap, XCB_GC_FOREGROUND, &osd->screen->black_pixel);

      osd->u.shaped.mapped = 0;
      osd->cmap = xcb_generate_id(osd->connection);
      xcb_create_colormap(osd->connection, XCB_COLORMAP_ALLOC_NONE, osd->cmap, osd->u.shaped.window, osd->visual);
      break;
      }
    case XCBOSD_COLORKEY:
      osd->bitmap = xcb_generate_id(osd->connection);
      xcb_create_pixmap(osd->connection, osd->depth, osd->bitmap, osd->window, osd->width, osd->height);
      osd->gc = xcb_generate_id(osd->connection);
      xcb_create_gc(osd->connection, osd->gc, osd->window, 0, NULL);
      osd->cmap = xcb_generate_id(osd->connection);
      xcb_create_colormap(osd->connection, XCB_COLORMAP_ALLOC_NONE, osd->cmap, osd->window, osd->visual);
      /* FIXME: the expose event doesn't seem to happen? */
      /*XSelectInput (osd->display, osd->window, ExposureMask);*/
      break;
    default:
      goto error2;
  }

  osd->clean = UNDEFINED;
  xcbosd_expose(osd);

  xprintf(osd->xine, XINE_VERBOSITY_DEBUG,
    _("x11osd: unscaled overlay created (%s mode).\n"),
    (mode==XCBOSD_SHAPED) ? "XShape" : "Colorkey" );

  return osd;

/*
  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/

error_aftermaskbitmap:
  if(mode==XCBOSD_SHAPED)
    xcb_free_pixmap(osd->connection, osd->u.shaped.mask_bitmap);
error_window:
  if(mode==XCBOSD_SHAPED)
    xcb_destroy_window(osd->connection, osd->u.shaped.window);
error2:
  free (osd);
  return NULL;
}

void xcbosd_colorkey(xcbosd *osd, uint32_t colorkey, vo_scale_t *scaling)
{
  assert (osd);
  assert (osd->mode==XCBOSD_COLORKEY);

  osd->u.colorkey.colorkey=colorkey;
  osd->u.colorkey.sc=scaling;
  osd->clean = UNDEFINED;
  xcbosd_clear(osd);
  xcbosd_expose(osd);
}

void xcbosd_destroy(xcbosd *osd)
{

  assert (osd);

  xcb_free_gc(osd->connection, osd->gc);
  xcb_free_pixmap(osd->connection, osd->bitmap);
  xcb_free_colormap(osd->connection, osd->cmap);
  if(osd->mode==XCBOSD_SHAPED) {
    xcb_free_gc(osd->connection, osd->u.shaped.mask_gc);
    xcb_free_gc(osd->connection, osd->u.shaped.mask_gc_back);
    xcb_free_pixmap(osd->connection, osd->u.shaped.mask_bitmap);
    xcb_destroy_window(osd->connection, osd->u.shaped.window);
  }

  free (osd);
}

void xcbosd_clear(xcbosd *osd)
{
  int i;

  lprintf("clear (state:%d)\n", osd->clean );

  if( osd->clean != WIPED )
    switch (osd->mode) {
      case XCBOSD_SHAPED: {
	xcb_rectangle_t rectangle = { 0, 0, osd->width, osd->height };
	xcb_poly_fill_rectangle(osd->connection, osd->u.shaped.mask_bitmap, osd->u.shaped.mask_gc_back, 1, &rectangle);
	break;
	}
      case XCBOSD_COLORKEY:
	xcb_change_gc(osd->connection, osd->gc, XCB_GC_FOREGROUND, &osd->u.colorkey.colorkey);
	if(osd->u.colorkey.sc) {
	  xcb_rectangle_t rectangle = { osd->u.colorkey.sc->output_xoffset, osd->u.colorkey.sc->output_yoffset,
					osd->u.colorkey.sc->output_width, osd->u.colorkey.sc->output_height };
	  xcb_poly_fill_rectangle(osd->connection, osd->bitmap, osd->gc, 1, &rectangle);
	  xcb_change_gc(osd->connection, osd->gc, XCB_GC_FOREGROUND, &osd->screen->black_pixel);

	  xcb_rectangle_t rects[4];
	  int rects_count = 0;

          for( i = 0; i < 4; i++ ) {
            if( osd->u.colorkey.sc->border[i].w && osd->u.colorkey.sc->border[i].h ) {
	      rects[rects_count].x = osd->u.colorkey.sc->border[i].x;
	      rects[rects_count].y = osd->u.colorkey.sc->border[i].y;
	      rects[rects_count].width = osd->u.colorkey.sc->border[i].w;
	      rects[rects_count].height = osd->u.colorkey.sc->border[i].h;
	      rects_count++;
            }

          if (rects_count > 0)
	    xcb_poly_fill_rectangle(osd->connection, osd->bitmap, osd->gc, rects_count, rects);
          }
	} else {
	  xcb_rectangle_t rectangle = { 0, 0, osd->width, osd->height };
	  xcb_poly_fill_rectangle(osd->connection, osd->bitmap, osd->gc, 1, &rectangle);
	}
	break;
    }
  osd->clean = WIPED;
}

#define TRANSPARENT 0xffffffff

#define saturate(n, l, u) ((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))

void xcbosd_blend(xcbosd *osd, vo_overlay_t *overlay)
{
  xcb_alloc_color_cookie_t alloc_color_cookie;
  xcb_alloc_color_reply_t *alloc_color_reply;

  if (osd->clean==UNDEFINED)
    xcbosd_clear(osd);	/* Workaround. Colorkey mode needs sc data before the clear. */

  if (overlay->rle) {
    int i, x, y, len, width;
    int use_clip_palette, max_palette_colour[2];
    uint32_t palette[2][OVL_PALETTE_SIZE];

    max_palette_colour[0] = -1;
    max_palette_colour[1] = -1;

    for (i=0, x=0, y=0; i<overlay->num_rle; i++) {
      len = overlay->rle[i].len;

      while (len > 0) {
        use_clip_palette = 0;
        if (len > overlay->width) {
          width = overlay->width;
          len -= overlay->width;
        }
        else {
          width = len;
          len = 0;
        }
        if ((y >= overlay->hili_top) && (y <= overlay->hili_bottom) && (x <= overlay->hili_right)) {
          if ((x < overlay->hili_left) && (x + width - 1 >= overlay->hili_left)) {
            width -= overlay->hili_left - x;
            len += overlay->hili_left - x;
          }
          else if (x > overlay->hili_left)  {
            use_clip_palette = 1;
            if (x + width - 1 > overlay->hili_right) {
              width -= overlay->hili_right - x;
              len += overlay->hili_right - x;
            }
          }
        }

        if (overlay->rle[i].color > max_palette_colour[use_clip_palette]) {
          int j;
          clut_t *src_clut;
          uint8_t *src_trans;

          if (use_clip_palette) {
            src_clut = (clut_t *)&overlay->hili_color;
            src_trans = (uint8_t *)&overlay->hili_trans;
          }
          else {
            src_clut = (clut_t *)&overlay->color;
            src_trans = (uint8_t *)&overlay->trans;
          }
          for (j=max_palette_colour[use_clip_palette]+1; j<=overlay->rle[i].color; j++) {
            if (src_trans[j]) {
              if (1) {
                int red, green, blue;
                int y, u, v, r, g, b;

                y = saturate(src_clut[j].y, 16, 235);
                u = saturate(src_clut[j].cb, 16, 240);
                v = saturate(src_clut[j].cr, 16, 240);
                y = (9 * y) / 8;
                r = y + (25 * v) / 16 - 218;
                red = (65536 * saturate(r, 0, 255)) / 256;
                g = y + (-13 * v) / 16 + (-25 * u) / 64 + 136;
                green = (65536 * saturate(g, 0, 255)) / 256;
                b = y + 2 * u - 274;
                blue = (65536 * saturate(b, 0, 255)) / 256;

                alloc_color_cookie = xcb_alloc_color(osd->connection, osd->cmap, red, green, blue);
                alloc_color_reply = xcb_alloc_color_reply(osd->connection, alloc_color_cookie, NULL);

                palette[use_clip_palette][j] = alloc_color_reply->pixel;
                free(alloc_color_reply);
              }
              else {
                if (src_clut[j].y > 127) {
                  palette[use_clip_palette][j] = osd->screen->white_pixel;
                }
                else {
                  palette[use_clip_palette][j] = osd->screen->black_pixel;
                }
              }
            }
            else {
              palette[use_clip_palette][j] = TRANSPARENT;
            }
          }
          max_palette_colour[use_clip_palette] = overlay->rle[i].color;
        }

        if(palette[use_clip_palette][overlay->rle[i].color] != TRANSPARENT) {
          xcb_change_gc(osd->connection, osd->gc, XCB_GC_FOREGROUND, &palette[use_clip_palette][overlay->rle[i].color]);
          xcb_rectangle_t rectangle = { overlay->x + x, overlay->y + y, width, 1 };
          xcb_poly_fill_rectangle(osd->connection, osd->bitmap, osd->gc, 1, &rectangle);
	  if(osd->mode==XCBOSD_SHAPED)
            xcb_poly_fill_rectangle(osd->connection, osd->u.shaped.mask_bitmap, osd->u.shaped.mask_gc, 1, &rectangle);
        }

        x += width;
        if (x == overlay->width) {
          x = 0;
          y++;
        }
      }
    }
    osd->clean = DRAWN;
  }
}

