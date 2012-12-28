/*
 * Copyright (C) 2003 the xine project
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
 * x11osd.c, use X11 Nonrectangular Window Shape Extension to draw xine OSD
 *
 * Nov 2003 - Miguel Freitas
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/Xatom.h>

#include <xine/xineutils.h>
#include "xine_mmx.h"

#define LOG_MODULE "x11osd"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include "x11osd.h"

struct x11osd
{
  Display *display;
  int screen;
  enum x11osd_mode mode;

  union {
    struct {
      Window window;
      Pixmap mask_bitmap;
      GC mask_gc;
      GC mask_gc_back;
      int mapped;
    } shaped;
    struct {
      uint32_t colorkey;
      vo_scale_t *sc;
    } colorkey;
  } u;
  Window window;
  unsigned int depth;
  Pixmap bitmap;
  Visual *visual;
  Colormap cmap;
  XImage *argb_img;
  void (*scale_func) (uint32_t* src, uint32_t* dst, int width, int step);
  int scale_mmx;

  GC gc;

  int width;
  int height;
  int x;
  int y;
  enum {DRAWN, WIPED, UNDEFINED} clean;
  xine_t *xine;
};


void
x11osd_expose (x11osd * osd)
{
  assert (osd);

  lprintf("expose (state:%d)\n", osd->clean );
  
  // copy argb data to bitmap
  if (osd->argb_img && osd->argb_img->data)
  {
     XPutImage(osd->display, osd->bitmap, osd->gc, osd->argb_img, 0, 0, 0, 0, osd->width, osd->height);
  }

  switch (osd->mode) {
    case X11OSD_SHAPED:
      XShapeCombineMask (osd->display, osd->u.shaped.window, ShapeBounding, 0, 0,
			 osd->u.shaped.mask_bitmap, ShapeSet);
      if( osd->clean==DRAWN ) {

	if( !osd->u.shaped.mapped )
	  XMapRaised (osd->display, osd->u.shaped.window);
	osd->u.shaped.mapped = 1;

	XCopyArea (osd->display, osd->bitmap, osd->u.shaped.window, osd->gc, 0, 0,
		   osd->width, osd->height, 0, 0);
      } else {
	if( osd->u.shaped.mapped )
	  XUnmapWindow (osd->display, osd->u.shaped.window);
	osd->u.shaped.mapped = 0;
      }
      break;
    case X11OSD_COLORKEY:
      if( osd->clean!=UNDEFINED )
	XCopyArea (osd->display, osd->bitmap, osd->window, osd->gc, 0, 0,
		   osd->width, osd->height, 0, 0);
  }
}


void
x11osd_resize (x11osd * osd, int width, int height)
{
  assert (osd);
  assert (width);
  assert (height);

  lprintf("resize old:%dx%d new:%dx%d\n", osd->width, osd->height, width, height );

  osd->width = width;
  osd->height = height;

  XFreePixmap (osd->display, osd->bitmap);
  switch(osd->mode) {
    case X11OSD_SHAPED:
      XResizeWindow (osd->display, osd->u.shaped.window, osd->width, osd->height);
      XFreePixmap (osd->display, osd->u.shaped.mask_bitmap);
      osd->u.shaped.mask_bitmap =
	XCreatePixmap (osd->display, osd->u.shaped.window, osd->width, osd->height,
		       1);
      osd->bitmap =
	XCreatePixmap (osd->display, osd->u.shaped.window,
		       osd->width, osd->height, osd->depth);
      break;
    case X11OSD_COLORKEY:
      osd->bitmap =
	XCreatePixmap (osd->display, osd->window,
		       osd->width, osd->height, osd->depth);
      break;
  }
  
  // resize argb_img data
  XDestroyImage(osd->argb_img);
  osd->argb_img = XCreateImage(osd->display, osd->visual, 24, ZPixmap, 0, NULL, osd->width, osd->height, 32, 0);
  osd->argb_img->data = calloc(osd->width * osd->height, sizeof(uint32_t));

  osd->clean = UNDEFINED;
  x11osd_clear(osd);
}

void
x11osd_drawable_changed (x11osd * osd, Window window)
{
  XSetWindowAttributes  attr;
  XWindowAttributes getattr;

  assert (osd);

  lprintf("drawable changed\n");

/*
  Do I need to recreate the GC's??

  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/
  XFreePixmap (osd->display, osd->bitmap);
  XFreeColormap (osd->display, osd->cmap);

  /* we need to call XSync(), because otherwise, calling XDestroyWindow()
     on the parent window could destroy our OSD window twice !! */
  XSync (osd->display, False);

  osd->window = window;

  XGetWindowAttributes (osd->display, osd->window, &getattr);
  osd->width = getattr.width;
  osd->height = getattr.height;

  assert(osd->width);
  assert(osd->height);

  switch(osd->mode) {
    case X11OSD_SHAPED:
      XFreePixmap (osd->display, osd->u.shaped.mask_bitmap);
      XDestroyWindow (osd->display, osd->u.shaped.window);

      attr.override_redirect = True;
      attr.background_pixel  = BlackPixel (osd->display, osd->screen);
      osd->u.shaped.window = XCreateWindow(osd->display, osd->window,
                              0, 0, osd->width, osd->height, 0,
                              CopyFromParent, CopyFromParent, CopyFromParent,
                              CWBackPixel | CWOverrideRedirect, &attr);

      XSelectInput (osd->display, osd->u.shaped.window, ExposureMask);
      osd->u.shaped.mapped = 0;

      osd->u.shaped.mask_bitmap = XCreatePixmap (osd->display, osd->u.shaped.window,
						 osd->width, osd->height, 1);

      osd->bitmap = XCreatePixmap (osd->display, osd->u.shaped.window, osd->width,
				   osd->height, osd->depth);

      osd->cmap = XCreateColormap(osd->display, osd->u.shaped.window,
				  osd->visual, AllocNone);
      break;
    case X11OSD_COLORKEY:
      osd->bitmap = XCreatePixmap (osd->display, osd->window, osd->width,
				   osd->height, osd->depth);
      osd->cmap = XCreateColormap(osd->display, osd->window,
                              osd->visual, AllocNone);

      break;
  }

  // resize argb_img data
  XDestroyImage(osd->argb_img);
  osd->argb_img = XCreateImage(osd->display, osd->visual, 24, ZPixmap, 0, NULL, osd->width, osd->height, 32, 0);
  osd->argb_img->data = calloc(osd->width * osd->height, sizeof(uint32_t));

  osd->clean = UNDEFINED;
  /* do not x11osd_clear() here: osd->u.colorkey.sc has not being updated yet */
}

static int x11_error = False ;

static int x11_error_handler(Display *dpy, XErrorEvent *error)
{
  x11_error = True;
  return 0;
}

x11osd *
x11osd_create (xine_t *xine, Display *display, int screen, Window window, enum x11osd_mode mode)
{
  x11osd *osd;
  int event_basep, error_basep;
  XErrorHandler   old_handler = NULL;
  XSetWindowAttributes  attr;
  XWindowAttributes getattr;

  osd = calloc(1, sizeof(x11osd));
  if (!osd)
    return NULL;

  osd->mode = mode;
  osd->xine = xine;
  osd->display = display;
  osd->screen = screen;
  osd->window = window;

  x11_error = False;
  old_handler = XSetErrorHandler(x11_error_handler);

  osd->visual = DefaultVisual (osd->display, osd->screen);
  osd->depth = DefaultDepth (osd->display, osd->screen);

  XGetWindowAttributes (osd->display, osd->window, &getattr);
  osd->width = getattr.width;
  osd->height = getattr.height;

  assert(osd->width);
  assert(osd->height);

  // create image for argb overlay
  osd->argb_img = XCreateImage(osd->display, osd->visual, 24, ZPixmap, 0, NULL, osd->width, osd->height, 32, 0);
  osd->argb_img->data = calloc(osd->width * osd->height, sizeof(uint32_t));
  
  // scale function
#if defined(ARCH_X86) || defined(ARCH_X86_64)
  uint32_t mm = xine_mm_accel();
  if ((osd->scale_func == NULL) && ((mm & MM_ACCEL_X86_MMX) || (mm & MM_ACCEL_X86_MMXEXT)))
  {
    osd->scale_func= x11osd_scale_line_mmx;
    osd->scale_mmx= 1;
    printf("X11OSD: MMX OSD scaling active\n");
  }
#endif
  if (osd->scale_func == NULL)
  {
    osd->scale_func= x11osd_scale_line;
    osd->scale_mmx= 0;
  }  

  switch (mode) {
    case X11OSD_SHAPED:
      if (!XShapeQueryExtension (osd->display, &event_basep, &error_basep)) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: XShape extension not available. unscaled overlay disabled.\n"));
	goto error2;
      }

      attr.override_redirect = True;
      attr.background_pixel  = BlackPixel (osd->display, osd->screen);
      osd->u.shaped.window = XCreateWindow(osd->display, osd->window,
                              0, 0, osd->width, osd->height, 0,
                              CopyFromParent, CopyFromParent, CopyFromParent,
                              CWBackPixel | CWOverrideRedirect, &attr);

      XSync(osd->display, False);
      if( x11_error ) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating window. unscaled overlay disabled.\n"));
	goto error_window;
      }

      osd->u.shaped.mask_bitmap = XCreatePixmap (osd->display, osd->u.shaped.window, osd->width,
                   osd->height, 1);
      XSync(osd->display, False);
      if( x11_error ) {
	xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating pixmap. unscaled overlay disabled.\n"));
	goto error_aftermaskbitmap;
      }

      osd->bitmap = XCreatePixmap (osd->display, osd->u.shaped.window, osd->width,
                   osd->height, osd->depth);
      osd->gc = XCreateGC (osd->display, osd->u.shaped.window, 0, NULL);

      osd->u.shaped.mask_gc = XCreateGC (osd->display, osd->u.shaped.mask_bitmap, 0, NULL);
      XSetForeground (osd->display, osd->u.shaped.mask_gc,
		  WhitePixel (osd->display, osd->screen));
      XSetBackground (osd->display, osd->u.shaped.mask_gc,
		  BlackPixel (osd->display, osd->screen));


      osd->u.shaped.mask_gc_back = XCreateGC (osd->display, osd->u.shaped.mask_bitmap, 0, NULL);
      XSetForeground (osd->display, osd->u.shaped.mask_gc_back,
		  BlackPixel (osd->display, osd->screen));
      XSetBackground (osd->display, osd->u.shaped.mask_gc_back,
		  WhitePixel (osd->display, osd->screen));

      XSelectInput (osd->display, osd->u.shaped.window, ExposureMask);
      osd->u.shaped.mapped = 0;
      osd->cmap = XCreateColormap(osd->display, osd->u.shaped.window,
                              osd->visual, AllocNone);
      break;
    case X11OSD_COLORKEY:
      osd->bitmap = XCreatePixmap (osd->display, osd->window, osd->width,
                   osd->height, osd->depth);
      osd->gc = XCreateGC (osd->display, osd->window, 0, NULL);
      osd->cmap = XCreateColormap(osd->display, osd->window,
                              osd->visual, AllocNone);
      /* FIXME: the expose event doesn't seem to happen? */
      /*XSelectInput (osd->display, osd->window, ExposureMask);*/
      break;
    default:
      goto error2;
  }

  XSync(osd->display, False);
  if( x11_error ) {
    xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating pixmap. unscaled overlay disabled.\n"));
    goto error_pixmap;
  }

  osd->clean = UNDEFINED;
  x11osd_expose(osd);

  XSetErrorHandler(old_handler);

  xprintf(osd->xine, XINE_VERBOSITY_DEBUG,
    _("x11osd: unscaled overlay created (%s mode).\n"),
    (mode==X11OSD_SHAPED) ? "XShape" : "Colorkey" );

  return osd;

/*
  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/

error_pixmap:
  XFreePixmap (osd->display, osd->bitmap);
error_aftermaskbitmap:
  if(mode==X11OSD_SHAPED)
    XFreePixmap (osd->display, osd->u.shaped.mask_bitmap);
error_window:
  if(mode==X11OSD_SHAPED)
    XDestroyWindow (osd->display, osd->u.shaped.window);
  XSetErrorHandler(old_handler);
error2:
  free (osd);
  return NULL;
}

void x11osd_colorkey(x11osd * osd, uint32_t colorkey, vo_scale_t *sc)
{
  assert (osd);
  assert (osd->mode==X11OSD_COLORKEY);

  osd->u.colorkey.colorkey=colorkey;
  osd->u.colorkey.sc=sc;
  osd->clean = UNDEFINED;
  x11osd_clear(osd);
  x11osd_expose(osd);
}

void
x11osd_destroy (x11osd * osd)
{

  assert (osd);

  XFreeGC (osd->display, osd->gc);
  XFreePixmap (osd->display, osd->bitmap);
  XFreeColormap (osd->display, osd->cmap);
  if(osd->mode==X11OSD_SHAPED) {
    XFreeGC (osd->display, osd->u.shaped.mask_gc);
    XFreeGC (osd->display, osd->u.shaped.mask_gc_back);
    XFreePixmap (osd->display, osd->u.shaped.mask_bitmap);
    XDestroyWindow (osd->display, osd->u.shaped.window);
  }
  XDestroyImage(osd->argb_img);
  free (osd);
}

void x11osd_clear(x11osd *osd)
{
  int i;

  lprintf("clear (state:%d)\n", osd->clean );

  if( osd->clean != WIPED )
    switch (osd->mode) {
      case X11OSD_SHAPED:
	XFillRectangle (osd->display, osd->u.shaped.mask_bitmap, osd->u.shaped.mask_gc_back,
			0, 0, osd->width, osd->height);
	break;
      case X11OSD_COLORKEY:
	XSetForeground(osd->display, osd->gc, osd->u.colorkey.colorkey);
	if(osd->u.colorkey.sc) {
	  XFillRectangle (osd->display, osd->bitmap, osd->gc,
			  osd->u.colorkey.sc->output_xoffset,
			  osd->u.colorkey.sc->output_yoffset,
			  osd->u.colorkey.sc->output_width,
			  osd->u.colorkey.sc->output_height);
	  XSetForeground(osd->display, osd->gc, BlackPixel(osd->display, osd->screen));
          for( i = 0; i < 4; i++ ) {
            if( osd->u.colorkey.sc->border[i].w && osd->u.colorkey.sc->border[i].h ) {
              XFillRectangle(osd->display, osd->bitmap, osd->gc,
                            osd->u.colorkey.sc->border[i].x, osd->u.colorkey.sc->border[i].y,
                            osd->u.colorkey.sc->border[i].w, osd->u.colorkey.sc->border[i].h);
            }
          }
	} else
	  XFillRectangle (osd->display, osd->bitmap, osd->gc, 0, 0, osd->width, osd->height);
	break;
    }
  osd->clean = WIPED;
}

#define X11OSD_TRANSPARENT 0xffffffff

#define saturate(n, l, u) ((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))

void x11osd_blend(x11osd *osd, vo_overlay_t *overlay)
{
  if (osd->clean==UNDEFINED)
    x11osd_clear(osd);	/* Workaround. Colorkey mode needs sc data before the clear. */

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
                XColor xcolor;
                int y, u, v, r, g, b;

                y = saturate(src_clut[j].y, 16, 235);
                u = saturate(src_clut[j].cb, 16, 240);
                v = saturate(src_clut[j].cr, 16, 240);
                y = (9 * y) / 8;
                r = y + (25 * v) / 16 - 218;
                xcolor.red = (65536 * saturate(r, 0, 255)) / 256;
                g = y + (-13 * v) / 16 + (-25 * u) / 64 + 136;
                xcolor.green = (65536 * saturate(g, 0, 255)) / 256;
                b = y + 2 * u - 274;
                xcolor.blue = (65536 * saturate(b, 0, 255)) / 256;

                xcolor.flags = DoRed | DoBlue | DoGreen;

                XAllocColor(osd->display, osd->cmap, &xcolor);

                palette[use_clip_palette][j] = xcolor.pixel;
              }
              else {
                if (src_clut[j].y > 127) {
                  palette[use_clip_palette][j] = WhitePixel(osd->display, osd->screen);
                }
                else {
                  palette[use_clip_palette][j] = BlackPixel(osd->display, osd->screen);
                }
              }
            }
            else {
              palette[use_clip_palette][j] = X11OSD_TRANSPARENT;
            }
          }
          max_palette_colour[use_clip_palette] = overlay->rle[i].color;
        }

        if(palette[use_clip_palette][overlay->rle[i].color] != X11OSD_TRANSPARENT) {
          XSetForeground(osd->display, osd->gc, palette[use_clip_palette][overlay->rle[i].color]);
          XFillRectangle(osd->display, osd->bitmap, osd->gc, overlay->x + x, overlay->y + y, width, 1);
	  if(osd->mode==X11OSD_SHAPED)
	    XFillRectangle(osd->display, osd->u.shaped.mask_bitmap, osd->u.shaped.mask_gc, overlay->x + x, overlay->y + y, width, 1);
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
  else if (overlay->argb_layer && overlay->argb_layer->buffer)
  {
    osd->argb_img->data = realloc(osd->argb_img->data, osd->width * osd->height * sizeof(uint32_t));
    
    x11osd_scale_argb32_image(osd, (uint32_t*)overlay->argb_layer->buffer, (uint32_t*)osd->argb_img->data, overlay->extent_width, overlay->extent_height, osd->width, osd->height);
	  
	uint32_t bx, by;
	uint32_t w, h;
	if(osd->mode==X11OSD_SHAPED) // fill bitmask / if bit is set, the corresponding pixel is drawn to screen
	{
	  w= osd->width - overlay->x;
	  h= osd->height - overlay->y;
	  for (by= 0; by < h; by++)
		for (bx= 0; bx < w; bx++)
		  if ((((uint32_t*)osd->argb_img->data)[bx+by*w] >> 24) != 0 )
		    XDrawPoint(osd->display, osd->u.shaped.mask_bitmap, osd->u.shaped.mask_gc, bx, by);
	}    

	osd->clean = DRAWN;
  }
}

// adapted algorithm from yuv2rgb.c to scale rgb images
void x11osd_scale_argb32_image(x11osd *osd, uint32_t* src, uint32_t* dst, int src_width, int src_height, int dst_width, int dst_height)
{
	int step_dx = src_width * 32768 / dst_width;
	int step_dy = src_height * 32768 / dst_height;
	int height, dy= 0;
		
	if (src_width == dst_width && src_height == dst_height)
	{
	  xine_fast_memcpy (dst, src, dst_width*dst_height*4);
	  return;
	}
	
	for (height = 0;; ) 
	{
	  osd->scale_func(src, dst, dst_width, step_dx);  // scale_line with or without mmx
	    
      dy += step_dy;
      dst += dst_width;

      while (--dst_height > 0 && dy < 32768)    // copy scaled line (only enlarging)
      {
        xine_fast_memcpy (dst, dst-(dst_width), dst_width*4); // copy last line

	    dy += step_dy;
	    dst += dst_width;
      }
      if (dst_height <= 0)
	    break;

      do 
      { // skip at least one line (possibly more if scale factor < 1)
        dy -= 32768;
        src += src_width;  

        height++;
      } while( dy>=32768);
    }	
    if (osd->scale_mmx) emms();   // empties the MMX state
}

// adapted algorithm from yuv2rgb.c to scale single argb line
void x11osd_scale_line(uint32_t* src, uint32_t* dst, int width, int step)
{
  uint32_t p1;
  uint32_t p2;
  int dx;
  
  p1 = *src++;
  p2 = *src++;
  dx = 0;
  
  if (step < 32768) 
  {
    while (width) 
    {
      *dst = ((((p1 >> 24) * (32768-dx)) + ((p2 >> 24) * dx))>>15) << 24                   // interpolate A: (A1*(32768-dx)+A2*dx) / 32768
           | (((((p1 >> 16) & 0xFF) * (32768-dx)) + (((p2 >> 16) & 0xFF) * dx))>>15) << 16 // interpolate R
           | (((((p1 >>  8) & 0xFF) * (32768-dx)) + (((p2 >>  8) & 0xFF) * dx))>>15) <<  8 // interpolate G
           | (((p1 & 0xFF) * (32768-dx)) + ((p2 & 0xFF) * dx))>>15;                        // interpolate B
      
      dx += step;
      if (dx > 32768) 
      {
	    dx -= 32768;
	    p1 = p2;
	    p2 = *src++;
      }

      dst ++;
      width --;
    }
  } else if (step <= 65536) 
  {
    while (width) 
    {
      *dst = ((((p1 >> 24) * (32768-dx)) + ((p2 >> 24) * dx))>>15) << 24
           | (((((p1 >> 16) & 0xFF) * (32768-dx)) + (((p2 >> 16) & 0xFF) * dx))>>15) << 16
           | (((((p1 >>  8) & 0xFF) * (32768-dx)) + (((p2 >>  8) & 0xFF) * dx))>>15) <<  8
           | (((p1 & 0xFF) * (32768-dx)) + ((p2 & 0xFF) * dx))>>15;

      dx += step;
      if (dx > 65536) 
      {
	    dx -= 65536;
	    p1 = *src++;
	    p2 = *src++;
      } else 
      {
	    dx -= 32768;
	    p1 = p2;
	    p2 = *src++;
      }

      dst ++;
      width --;
    }
  } else 
  {
    while (width) 
    {
      int offs;

      *dst = ((((p1 >> 24) * (32768-dx)) + ((p2 >> 24) * dx))>>15) << 24
           | (((((p1 >> 16) & 0xFF) * (32768-dx)) + (((p2 >> 16) & 0xFF) * dx))>>15) << 16
           | (((((p1 >>  8) & 0xFF) * (32768-dx)) + (((p2 >>  8) & 0xFF) * dx))>>15) <<  8
           | (((p1 & 0xFF) * (32768-dx)) + ((p2 & 0xFF) * dx))>>15;

      dx += step;
      offs=((dx-1)>>15);
      dx-=offs<<15;
      src+=offs-2;
      p1=*src++;
      p2=*src++;
      dst ++;
      width --;
    }
  }
};

// adapted algorithm from yuv2rgb.c to scale single argb line
void x11osd_scale_line_mmx(uint32_t* src, uint32_t* dst, int width, int step)
{
  uint32_t p1;
  uint32_t p2;
  int dx;
  int dx2; 
  
  p1 = *src++;
  p2 = *src++;
  dx = 0;
  
  if (step < 32768) 
  {
    while (width) 
    {
	  dx2 = dx * 2;
	  if (dx2==65536) 
	    dx2-= 1;
       
      /*  
       *  MMX ARGB interpolation between p1(a1,r1,g1,b1) and p2(a2,r2,g2,b2)
       *  a= (a1* (32768-dx) + a2 * dx) / 32768 -> a = a1 - a1*dx/32768 + a2*dx/32768
       *  r= (r1* (32768-dx) + r2 * dx) / 32768 -> r = r1 - r1*dx/32768 + r2*dx/32768
       *  ...
       *  Doing multiplication of a,r,g,b with dx in one step
       *  Division by 32768 is not necessary in mmx because you can get only the high 16 bits of a multiplication
      */
      movd_m2r(p1,mm0);          // mm0 = p1;  
      movq_r2r(mm0,mm1);         // mm1 = mm0;  copy because result of unpack overides mm1 and we need p1 later again
      pxor_r2r(mm2,mm2);         // mm2 = 0;
      punpcklbw_r2r(mm2,mm1);    // mm1 = unpacked p1 (00 AA 00 RR 00 GG 00 BB)
      movd_m2r(dx2,mm3);         // mm3 = dx2;   from here i call dx2 = 0xd1d2
      movd_m2r(dx2,mm4);         // mm4 = dx2;
      punpcklwd_r2r(mm3,mm4);    // mm4 = 00 00 00 00 d1 d2 d1 d2;
      movq_r2r(mm4,mm3);         // mm3 = mm4;
      punpcklwd_r2r(mm3,mm4);    // mm4 = d1 d2 d1 d2 d1 d2 d1 d2;  4 times dx2
      movq_r2r(mm4,mm5);         // mm5 = mm4; copy
      pmulhuw_r2r(mm4,mm1);      // mm1 = mm4 * mm1 = dx2 * p1 = dx * p1 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm1);     // mm1 = packed mm1
      movd_m2r(p2,mm6);          // mm6 = p2;
      punpcklbw_r2r(mm2,mm6);    // mm6 = unpacked p2 (00 AA 00 RR 00 GG 00 BB)
      pmulhuw_r2r(mm5,mm6);      // mm6 = mm5 * mm6 = dx2 * p2 = dx * p2 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm6);     // mm6 = packed mm6
      psubusb_r2r(mm1,mm0);      // mm0 = mm0 - mm1 = p1 - (p1*dx/32768)
      paddusb_r2r(mm0,mm6);      // mm5 = mm0 + mm5 = p1 - (p1*dx/32768) + (p2*dx/32768)
      
      movd_r2m(mm6, *dst);
      
      dx += step;
      if (dx > 32768) 
      {
	    dx -= 32768;
	    p1 = p2;
	    p2 = *src++;
      }

      dst ++;
      width --;
    }
  } else if (step <= 65536) 
  {
    while (width) 
    {
	  dx2 = dx * 2;
	  if (dx2==65536) 
	    dx2-= 1;
	      
      movd_m2r(p1,mm0);          // mm0 = p1;  
      movq_r2r(mm0,mm1);         // mm1 = mm0;  copy because result of unpack overides mm1 and we need p1 later again
      pxor_r2r(mm2,mm2);         // mm2 = 0;
      punpcklbw_r2r(mm2,mm1);    // mm1 = unpacked p1 (00 AA 00 RR 00 GG 00 BB)
      movd_m2r(dx2,mm3);         // mm3 = dx2;   from here i call dx2 = 0xd1d2
      movd_m2r(dx2,mm4);         // mm4 = dx2;
      punpcklwd_r2r(mm3,mm4);    // mm4 = 00 00 00 00 d1 d2 d1 d2;
      movq_r2r(mm4,mm3);         // mm3 = mm4;
      punpcklwd_r2r(mm3,mm4);    // mm4 = d1 d2 d1 d2 d1 d2 d1 d2;  4 times dx2
      movq_r2r(mm4,mm5);         // mm5 = mm4; copy
      pmulhuw_r2r(mm4,mm1);      // mm1 = mm4 * mm1 = dx2 * p1 = dx * p1 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm1);     // mm1 = packed mm1
      movd_m2r(p2,mm6);          // mm6 = p2;
      punpcklbw_r2r(mm2,mm6);    // mm6 = unpacked p2 (00 AA 00 RR 00 GG 00 BB)
      pmulhuw_r2r(mm5,mm6);      // mm6 = mm5 * mm6 = dx2 * p2 = dx * p2 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm6);     // mm6 = packed mm6
      psubusb_r2r(mm1,mm0);      // mm0 = mm0 - mm1 = p1 - (p1*dx/32768)
      paddusb_r2r(mm0,mm6);      // mm5 = mm0 + mm5 = p1 - (p1*dx/32768) + (p2*dx/32768)
      
      movd_r2m(mm6, *dst);

      dx += step;
      if (dx > 65536) 
      {
	    dx -= 65536;
	    p1 = *src++;
	    p2 = *src++;
      } else 
      {
	    dx -= 32768;
	    p1 = p2;
	    p2 = *src++;
      }

      dst ++;
      width --;
    }
  } else 
  {
    while (width) 
    {
      int offs;
      
      dx2 = dx * 2;
	  if (dx2==65536) 
	    dx2-= 1;
      
      movd_m2r(p1,mm0);          // mm0 = p1;  
      movq_r2r(mm0,mm1);         // mm1 = mm0;  copy because result of unpack overides mm1 and we need p1 later again
      pxor_r2r(mm2,mm2);         // mm2 = 0;
      punpcklbw_r2r(mm2,mm1);    // mm1 = unpacked p1 (00 AA 00 RR 00 GG 00 BB)
      movd_m2r(dx2,mm3);         // mm3 = dx2;   from here i call dx2 = 0xd1d2
      movd_m2r(dx2,mm4);         // mm4 = dx2;
      punpcklwd_r2r(mm3,mm4);    // mm4 = 00 00 00 00 d1 d2 d1 d2;
      movq_r2r(mm4,mm3);         // mm3 = mm4;
      punpcklwd_r2r(mm3,mm4);    // mm4 = d1 d2 d1 d2 d1 d2 d1 d2;  4 times dx2
      movq_r2r(mm4,mm5);         // mm5 = mm4; copy
      pmulhuw_r2r(mm4,mm1);      // mm1 = mm4 * mm1 = dx2 * p1 = dx * p1 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm1);     // mm1 = packed mm1
      movd_m2r(p2,mm6);          // mm6 = p2;
      punpcklbw_r2r(mm2,mm6);    // mm6 = unpacked p2 (00 AA 00 RR 00 GG 00 BB)
      pmulhuw_r2r(mm5,mm6);      // mm6 = mm5 * mm6 = dx2 * p2 = dx * p2 / 32768 // get from multiplication only high 16 bits
      packuswb_r2r(mm2,mm6);     // mm6 = packed mm6
      psubusb_r2r(mm1,mm0);      // mm0 = mm0 - mm1 = p1 - (p1*dx/32768)
      paddusb_r2r(mm0,mm6);      // mm5 = mm0 + mm5 = p1 - (p1*dx/32768) + (p2*dx/32768)
      
      movd_r2m(mm6, *dst);

      dx += step;
      offs=((dx-1)>>15);
      dx-=offs<<15;
      src+=offs-2;
      p1=*src++;
      p2=*src++;
      dst ++;
      width --;
    }
  }
};
