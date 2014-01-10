/*
 * Copyright (C) 2000-2012 the xine project
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include <sys/types.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <errno.h>

#ifdef HAVE_ICONV
#  include <iconv.h>
#endif

#include <basedir.h>

#define LOG_MODULE "osd"
#define LOG_VERBOSE
/*
#define LOG
*/

#define XINE_ENGINE_INTERNAL

#include <xine/xine_internal.h>
#include "xine-engine/bswap.h"
#include <xine/xineutils.h>
#include <xine/video_out.h>
#include <xine/osd.h>

#ifdef HAVE_FT2
#include <ft2build.h>
#include FT_FREETYPE_H
# ifdef HAVE_FONTCONFIG
#  include <fontconfig/fontconfig.h>
# endif
#endif

#define FONT_VERSION  2

#define BINARY_SEARCH 1

/* unicode value of alias character,
 * used if conversion fails
 */
#define ALIAS_CHARACTER_CONV '#'

/* unicode value of alias character,
 * used if character isn't in the font
 */
#define ALIAS_CHARACTER_FONT '_'

/* we want UCS-2 encoding in the machine endian */
#ifdef WORDS_BIGENDIAN
#  define UCS2_ENCODING "UCS-2BE"
#else
#  define UCS2_ENCODING "UCS-2LE"
#endif

#if (FREETYPE_MAJOR > 2) || \
    (FREETYPE_MAJOR == 2 && FREETYPE_MINOR > 1) || \
    (FREETYPE_MAJOR == 2 && FREETYPE_MINOR == 1 && FREETYPE_PATCH >= 3)
#  define KERNING_DEFAULT FT_KERNING_DEFAULT
#else
#  define KERNING_DEFAULT ft_kerning_default
#endif

#ifdef ENABLE_ANTIALIASING
#  define FT_LOAD_FLAGS   FT_LOAD_DEFAULT
#else
#  define FT_LOAD_FLAGS  (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING)
#endif

/* This text descriptions are used for config screen */
static const char *const textpalettes_str[NUMBER_OF_TEXT_PALETTES+1] = {
  "white-black-transparent",
  "white-none-transparent",
  "white-none-translucid",
  "yellow-black-transparent",
  NULL};

/*
   Palette entries as used by osd fonts:

   0: not used by font, always transparent
   1: font background, usually transparent, may be used to implement
      translucid boxes where the font will be printed.
   2-5: transition between background and border (usually only alpha
        value changes).
   6: font border. if the font is to be displayed without border this
      will probably be adjusted to font background or near.
   7-9: transition between border and foreground
   10: font color (foreground)
*/

/*
    The palettes below were made by hand, ie, i just throw
    values that seemed to do the transitions i wanted.
    This can surelly be improved a lot. [Miguel]
*/

static const clut_t textpalettes_color[NUMBER_OF_TEXT_PALETTES][TEXT_PALETTE_SIZE] = {
/* white, black border, transparent */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x00, 0x00), /*0*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*1*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*2*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*3*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*4*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*5*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*6*/
    CLUT_Y_CR_CB_INIT(0x40, 0x80, 0x80), /*7*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*8*/
    CLUT_Y_CR_CB_INIT(0xc0, 0x80, 0x80), /*9*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*10*/
  },
  /* white, no border, transparent */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x00, 0x00), /*0*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*1*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*2*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*3*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*4*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*5*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*6*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*7*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*8*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*9*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*10*/
  },
  /* white, no border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x00, 0x00), /*0*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*1*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*2*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*3*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*4*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*5*/
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80), /*6*/
    CLUT_Y_CR_CB_INIT(0xa0, 0x80, 0x80), /*7*/
    CLUT_Y_CR_CB_INIT(0xc0, 0x80, 0x80), /*8*/
    CLUT_Y_CR_CB_INIT(0xe0, 0x80, 0x80), /*9*/
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80), /*10*/
  },
  /* yellow, black border, transparent */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x00, 0x00), /*0*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*1*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*2*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*3*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*4*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*5*/
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80), /*6*/
    CLUT_Y_CR_CB_INIT(0x40, 0x84, 0x60), /*7*/
    CLUT_Y_CR_CB_INIT(0x70, 0x88, 0x40), /*8*/
    CLUT_Y_CR_CB_INIT(0xb0, 0x8a, 0x20), /*9*/
    CLUT_Y_CR_CB_INIT(0xff, 0x90, 0x00), /*10*/
  },
};

static const uint8_t textpalettes_trans[NUMBER_OF_TEXT_PALETTES][TEXT_PALETTE_SIZE] = {
  {0, 0, 3, 6, 8, 10, 12, 14, 15, 15, 15 },
  {0, 0, 0, 0, 0, 0, 2, 6, 9, 12, 15 },
  {0, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 },
  {0, 0, 3, 6, 8, 10, 12, 14, 15, 15, 15 },
};

typedef struct osd_fontchar_s {
  uint8_t *bmp;
  uint16_t code;
  uint16_t width;
  uint16_t height;
} osd_fontchar_t;

struct osd_font_s {
  char             name[40];
  char            *filename;
  osd_fontchar_t  *fontchar;
  osd_font_t      *next;
  uint16_t         version;
  uint16_t         size;
  uint16_t         num_fontchars;
  uint16_t         loaded;
};

#ifdef HAVE_FT2
struct osd_ft2context_s {
  FT_Library library;
  FT_Face    face;
  int        size;
};

static void osd_free_ft2 (osd_object_t *osd)
{
  if( osd->ft2 ) {
    if ( osd->ft2->face )
      FT_Done_Face (osd->ft2->face);
    if ( osd->ft2->library )
      FT_Done_FreeType(osd->ft2->library);
    free( osd->ft2 );
    osd->ft2 = NULL;
  }
}
#else
static inline void osd_free_ft2 (osd_object_t *osd __attr_unused) {}
#endif

/*
 * open a new osd object. this will allocated an empty (all zero) drawing
 * area where graphic primitives may be used.
 * It is ok to specify big width and height values. The render will keep
 * track of the smallest changed area to not generate too big overlays.
 * A default palette is initialized (i sugest keeping color 0 as transparent
 * for the sake of simplicity)
 */

static osd_object_t *XINE_MALLOC osd_new_object (osd_renderer_t *this, int width, int height) {

  osd_object_t *osd;

  pthread_mutex_lock (&this->osd_mutex);

  osd = calloc(1, sizeof(osd_object_t));
  osd->renderer = this;
  osd->next = this->osds;
  this->osds = osd;

  osd->video_window_x = 0;
  osd->video_window_y = 0;
  osd->video_window_width = 0;
  osd->video_window_height = 0;
  osd->extent_width = 0;
  osd->extent_height = 0;
  osd->width = width;
  osd->height = height;
  osd->area = calloc(width, height);
  osd->area_touched = 0;

  osd->x1 = width;
  osd->y1 = height;
  osd->x2 = 0;
  osd->y2 = 0;

  memcpy(osd->color, textpalettes_color[0], sizeof(textpalettes_color[0]));
  memcpy(osd->trans, textpalettes_trans[0], sizeof(textpalettes_trans[0]));

  osd->handle = -1;

#ifdef HAVE_ICONV
  osd->cd       = (iconv_t)-1;
  osd->encoding = NULL;
#endif

  pthread_mutex_unlock (&this->osd_mutex);

  lprintf("osd=%p size: %dx%d\n", osd, width, height);

  return osd;
}

/*
 * osd extent must be set to achive video resolution independent osds
 * both sizes must be > 0 to take effect. otherwise, video resolution
 * will still be used. the extent defines the reference coordinate
 * system which is matched to the video output area.
 */
static void osd_set_extent (osd_object_t *osd, int extent_width, int extent_height) {

  osd->extent_width  = extent_width;
  osd->extent_height = extent_height;
}

/*
 * osd video window defines an area withing osd extent where the
 * video shall be scaled to while an osd is displayed on screen.
 * both width and height must be > 0 to take effect.
 */
static void osd_set_video_window (osd_object_t *osd, int window_x, int window_y, int window_width, int window_height) {

  osd->video_window_x      = window_x;
  osd->video_window_y      = window_y;
  osd->video_window_width  = window_width;
  osd->video_window_height = window_height;
}

static argb_layer_t *argb_layer_create() {

  argb_layer_t *argb_layer = (argb_layer_t *)calloc(1, sizeof (argb_layer_t));

  pthread_mutex_init(&argb_layer->mutex, NULL);
  return argb_layer;
}

static void argb_layer_destroy(argb_layer_t *argb_layer) {

  pthread_mutex_destroy(&argb_layer->mutex);
  free(argb_layer);
}

void set_argb_layer_ptr(argb_layer_t **dst, argb_layer_t *src) {

  if (src) {
    pthread_mutex_lock(&src->mutex);
    ++src->ref_count;
    pthread_mutex_unlock(&src->mutex);
  }

  if (*dst) {
    int free_argb_layer;

    pthread_mutex_lock(&(*dst)->mutex);
    free_argb_layer = (0 == --(*dst)->ref_count);
    pthread_mutex_unlock(&(*dst)->mutex);

    if (free_argb_layer)
      argb_layer_destroy(*dst);
  }
  
  *dst = src;
}


/*
#define DEBUG_RLE
*/

static int _osd_hide (osd_object_t *osd, int64_t vpts);

/*
 * send the osd to be displayed at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int _osd_show (osd_object_t *osd, int64_t vpts, int unscaled ) {

  osd_renderer_t *this = osd->renderer;
  video_overlay_manager_t *ovl_manager;
  rle_elem_t rle, *rle_p=0;
  int x, y;
  uint8_t *c;

  lprintf("osd=%p vpts=%"PRId64"\n", osd, vpts);

  this->stream->xine->port_ticket->acquire(this->stream->xine->port_ticket, 1);

  ovl_manager = this->stream->video_out->get_overlay_manager(this->stream->video_out);

  if( osd->handle < 0 ) {
    if( (osd->handle = ovl_manager->get_handle(ovl_manager, 0)) == -1 ) {
      this->stream->xine->port_ticket->release(this->stream->xine->port_ticket, 1);
      return 0;
    }
  }

  pthread_mutex_lock (&this->osd_mutex);

  /* clip update area to allowed range */
  if(osd->x1 > osd->width)
    osd->x1 = osd->width;
  if(osd->x2 > osd->width)
    osd->x2 = osd->width;
  if(osd->y1 > osd->height)
    osd->y1 = osd->height;
  if(osd->y2 > osd->height)
    osd->y2 = osd->height;
  if(osd->x1 < 0) osd->x1 = 0;
  if(osd->x2 < 0) osd->x2 = 0;
  if(osd->y1 < 0) osd->y1 = 0;
  if(osd->y2 < 0) osd->y2 = 0;

#ifdef DEBUG_RLE
  lprintf("osd_show %p rle starts\n", osd);
#endif

  /* check if osd is valid (something drawn on it) */
  if( osd->x2 > osd->x1 && osd->y2 > osd->y1 ) {

    this->event.object.handle = osd->handle;

    memset( this->event.object.overlay, 0, sizeof(*this->event.object.overlay) );

    set_argb_layer_ptr(&this->event.object.overlay->argb_layer, osd->argb_layer);

    this->event.object.overlay->unscaled = unscaled;
    this->event.object.overlay->x = osd->display_x + osd->x1;
    this->event.object.overlay->y = osd->display_y + osd->y1;
    this->event.object.overlay->width = osd->x2 - osd->x1;
    this->event.object.overlay->height = osd->y2 - osd->y1;

    this->event.object.overlay->video_window_x      = osd->video_window_x;
    this->event.object.overlay->video_window_y      = osd->video_window_y;
    this->event.object.overlay->video_window_width  = osd->video_window_width;
    this->event.object.overlay->video_window_height = osd->video_window_height;

    this->event.object.overlay->extent_width  = osd->extent_width;
    this->event.object.overlay->extent_height = osd->extent_height;

    this->event.object.overlay->hili_top    = 0;
    this->event.object.overlay->hili_bottom = this->event.object.overlay->height;
    this->event.object.overlay->hili_left   = 0;
    this->event.object.overlay->hili_right  = this->event.object.overlay->width;

    /* there will be at least that many rle objects (one for each row) */
    this->event.object.overlay->num_rle = 0;
    if (!osd->area_touched) {
      /* avoid rle encoding when only argb_layer is modified */
      this->event.object.overlay->data_size = 0;
      rle_p = this->event.object.overlay->rle = NULL;
    } else {
      /* We will never need more rle objects than columns in any row
         Rely on lazy page allocation to avoid us actually taking up
         this much RAM */
      this->event.object.overlay->data_size = osd->width * osd->height;
      rle_p = this->event.object.overlay->rle =
         malloc(this->event.object.overlay->data_size * sizeof(rle_elem_t) );

      for( y = osd->y1; y < osd->y2; y++ ) {
#ifdef DEBUG_RLE
        lprintf("osd_show %p y = %d: ", osd, y);
#endif
        c = osd->area + y * osd->width + osd->x1;

        /* initialize a rle object with the first pixel's color */
        rle.len = 1;
        rle.color = *c++;

        /* loop over the remaining pixels in the row */
        for( x = osd->x1 + rle.len; x < osd->x2; x++, c++ ) {
          if( rle.color != *c ) {
#ifdef DEBUG_RLE
            lprintf("(%d, %d), ", rle.len, rle.color);
#endif
            *rle_p++ = rle;
            this->event.object.overlay->num_rle++;

            rle.color = *c;
            rle.len = 1;
          } else {
            rle.len++;
          }
        }
#ifdef DEBUG_RLE
        lprintf("(%d, %d)\n", rle.len, rle.color);
#endif
        *rle_p++ = rle;
        this->event.object.overlay->num_rle++;
      }
#ifdef DEBUG_RLE
      lprintf("osd_show %p rle ends\n", osd);
#endif
      lprintf("num_rle = %d\n", this->event.object.overlay->num_rle);

      memcpy(this->event.object.overlay->hili_color, osd->color, sizeof(osd->color));
      memcpy(this->event.object.overlay->hili_trans, osd->trans, sizeof(osd->trans));
      memcpy(this->event.object.overlay->color, osd->color, sizeof(osd->color));
      memcpy(this->event.object.overlay->trans, osd->trans, sizeof(osd->trans));
    }

    this->event.event_type = OVERLAY_EVENT_SHOW;
    this->event.vpts = vpts;
    ovl_manager->add_event(ovl_manager, (void *)&this->event);

    set_argb_layer_ptr(&this->event.object.overlay->argb_layer, NULL);
  } else {
    /* osd empty - hide it */
    _osd_hide(osd, vpts);
  }
  pthread_mutex_unlock (&this->osd_mutex);

  this->stream->xine->port_ticket->release(this->stream->xine->port_ticket, 1);

  return 1;
}

/* normal OSD show
 * overlay is blended and scaled together with the stream.
 */
static int osd_show_scaled (osd_object_t *osd, int64_t vpts) {
  return _osd_show(osd, vpts, 0);
}

/* unscaled OSD show
 * overlay is blended at output (screen) resolution.
 */
static int osd_show_unscaled (osd_object_t *osd, int64_t vpts) {
  return _osd_show(osd, vpts, 1);
}

static int osd_show_gui_scaled (osd_object_t *osd, int64_t vpts) {
  return _osd_show(osd, vpts, 2);
}

/*
 * send event to hide osd at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int _osd_hide (osd_object_t *osd, int64_t vpts) {

  osd_renderer_t *this = osd->renderer;
  video_overlay_manager_t *ovl_manager;

  lprintf("osd=%p vpts=%"PRId64"\n",osd, vpts);

  if( osd->handle < 0 )
    return 0;

  this->event.object.handle = osd->handle;

  /* not really needed this, but good pratice to clean it up */
  memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );

  this->event.event_type = OVERLAY_EVENT_HIDE;
  this->event.vpts = vpts;

  ovl_manager = this->stream->video_out->get_overlay_manager(this->stream->video_out);
  ovl_manager->add_event(ovl_manager, (void *)&this->event);

  return 1;
}

static int osd_hide (osd_object_t *osd, int64_t vpts) {

  osd_renderer_t *this = osd->renderer;
  int ret;

  this->stream->xine->port_ticket->acquire(this->stream->xine->port_ticket, 1);

  pthread_mutex_lock (&this->osd_mutex);

  ret = _osd_hide(osd, vpts);

  pthread_mutex_unlock (&this->osd_mutex);

  this->stream->xine->port_ticket->release(this->stream->xine->port_ticket, 1);

  return ret;
}


/*
 * clear an osd object, so that it can be used for rendering a new image
 */

static void osd_clear (osd_object_t *osd) {
  lprintf("osd=%p\n",osd);

  if (osd->area_touched) {
    osd->area_touched = 0;
    memset(osd->area, 0, osd->width * osd->height);
  }

  osd->x1 = osd->width;
  osd->y1 = osd->height;
  osd->x2 = 0;
  osd->y2 = 0;

  if (osd->argb_layer) {
    pthread_mutex_lock(&osd->argb_layer->mutex);
    osd->argb_layer->x1 = osd->x1;
    osd->argb_layer->y1 = osd->y1;
    osd->argb_layer->x2 = osd->x2;
    osd->argb_layer->y2 = osd->y2;
    pthread_mutex_unlock(&osd->argb_layer->mutex);
  }
}

/*
 * Draw a point.
 */

static void osd_point (osd_object_t *osd, int x, int y, int color) {
  uint8_t *c;

  lprintf("osd=%p (%d x %d)\n", osd, x, y);

  if (x < 0 || x >= osd->width)
    return;
  if (y < 0 || y >= osd->height)
    return;

  /* update clipping area */
  osd->x1 = MIN(osd->x1, x);
  osd->x2 = MAX(osd->x2, (x + 1));
  osd->y1 = MIN(osd->y1, y);
  osd->y2 = MAX(osd->y2, (y + 1));
  osd->area_touched = 1;

  c = osd->area + y * osd->width + x;
  *c = color;
}

/*
 * Bresenham line implementation on osd object
 */

static void osd_line (osd_object_t *osd,
		      int x1, int y1, int x2, int y2, int color) {

  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;
  int swap_x = 0;
  int swap_y = 0;

  lprintf("osd=%p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );

  /* sort line */
  if (x2 < x1) {
    t  = x1;
    x1 = x2;
    x2 = t;
    swap_x = 1;
  }
  if (y2 < y1) {
    t  = y1;
    y1 = y2;
    y2 = t;
    swap_y = 1;
  }

  /* clip line */
  if (x1 < 0) {
    y1 = y1 + (y2-y1) * -x1 / (x2-x1);
    x1 = 0;
  }
  if (y1 < 0) {
    x1 = x1 + (x2-x1) * -y1 / (y2-y1);
    y1 = 0;
  }
  if (x2 > osd->width) {
    y2 = y1 + (y2-y1) * (osd->width-x1) / (x2-x1);
    x2 = osd->width;
  }
  if (y2 > osd->height) {
    x2 = x1 + (x2-x1) * (osd->height-y1) / (y2-y1);
    y2 = osd->height;
  }

  if (x1 >= osd->width || y1 >= osd->height)
    return;

  /* update clipping area */
  osd->x1 = MIN( osd->x1, x1 );
  osd->x2 = MAX( osd->x2, x2 );
  osd->y1 = MIN( osd->y1, y1 );
  osd->y2 = MAX( osd->y2, y2 );
  osd->area_touched = 1;

  dx = x2 - x1;
  dy = y2 - y1;

  /* unsort line */
  if (swap_x) {
    t  = x1;
    x1 = x2;
    x2 = t;
  }
  if (swap_y) {
    t  = y1;
    y1 = y2;
    y2 = t;
  }

  if( dx>=dy ) {
    if( x1>x2 )
    {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( y2 > y1 ) inc = 1; else inc = -1;

    inc1 = 2*dy;
    d = inc1 - dx;
    inc2 = 2*(dy-dx);

    c = osd->area + y1 * osd->width + x1;

    while(x1<x2)
    {
      *c++ = color;

      x1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        y1+=inc;
        d+=inc2;
        c = osd->area + y1 * osd->width + x1;
      }
    }
  } else {
    if( y1>y2 ) {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( x2 > x1 ) inc = 1; else inc = -1;

    inc1 = 2*dx;
    d = inc1-dy;
    inc2 = 2*(dx-dy);

    c = osd->area + y1 * osd->width + x1;

    while(y1<y2) {
      *c = color;

      c += osd->width;
      y1++;
      if( d<0 ) {
	d+=inc1;
      } else {
	x1+=inc;
	d+=inc2;
	c = osd->area + y1 * osd->width + x1;
      }
    }
  }
}


/*
 * filled retangle
 */

static void osd_filled_rect (osd_object_t *osd,
			     int x1, int y1, int x2, int y2, int color) {

  int x, y, dx, dy;

  lprintf("osd=%p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );

  /* sort rectangle */
  x  = MIN( x1, x2 );
  dx = MAX( x1, x2 );
  y  = MIN( y1, y2 );
  dy = MAX( y1, y2 );

  /* clip rectangle */
  if (x >= osd->width || y >= osd->height)
    return;

  if (x < 0) {
    dx += x;
    x = 0;
  }
  if (y < 0) {
    dy += y;
    y = 0;
  }

  dx = MIN( dx, osd->width );
  dy = MIN( dy, osd->height );

  /* update clipping area */
  osd->x1 = MIN( osd->x1, x );
  osd->x2 = MAX( osd->x2, dx );
  osd->y1 = MIN( osd->y1, y );
  osd->y2 = MAX( osd->y2, dy );
  osd->area_touched = 1;

  dx -= x;
  dy -= y;

  for( ; dy--; y++ ) {
    memset(osd->area + y * osd->width + x,color,dx);
  }
}

/*
 * set palette (color and transparency)
 */

static void osd_set_palette(osd_object_t *osd, const uint32_t *color, const uint8_t *trans ) {

  memcpy(osd->color, color, sizeof(osd->color));
  memcpy(osd->trans, trans, sizeof(osd->trans));
}

/*
 * set on existing text palette
 * (-1 to set user specified palette)
 */

static void osd_set_text_palette(osd_object_t *osd, int palette_number,
				 int color_base) {

  if( palette_number < 0 )
    palette_number = osd->renderer->textpalette;

  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  memcpy(&osd->color[color_base], textpalettes_color[palette_number],
	 sizeof(textpalettes_color[palette_number]));
  memcpy(&osd->trans[color_base], textpalettes_trans[palette_number],
	 sizeof(textpalettes_trans[palette_number]));
}


/*
 * get palette (color and transparency)
 */

static void osd_get_palette (osd_object_t *osd, uint32_t *color, uint8_t *trans) {

  memcpy(color, osd->color, sizeof(osd->color));
  memcpy(trans, osd->trans, sizeof(osd->trans));
}

/*
 * set position were overlay will be blended
 */

static void osd_set_position (osd_object_t *osd, int x, int y) {

  if( x < 0 || x > 0x10000 )
    x = 0;
  if( y < 0 || y > 0x10000 )
    y = 0;
  osd->display_x = x;
  osd->display_y = y;
}

static uint16_t gzread_i16(gzFile fp) {
  uint16_t ret;
  ret = gzgetc(fp);
  ret |= (gzgetc(fp)<<8);
  return ret;
}

/*
   load bitmap font into osd engine
*/

static int osd_renderer_load_font(osd_renderer_t *this, char *filename) {

  gzFile       fp;
  osd_font_t  *font = NULL;
  int          i, ret = 0;

  lprintf("name=%s\n", filename );

  /* load quick & dirt font format */
  /* fixme: check for all read errors... */
  if( (fp = gzopen(filename,"rb")) != NULL ) {

    font = calloc(1, sizeof(osd_font_t));

    gzread(fp, font->name, sizeof(font->name) );
    font->version = gzread_i16(fp);

    if( font->version == FONT_VERSION ) {

      font->size = gzread_i16(fp);
      font->num_fontchars = gzread_i16(fp);
      font->loaded = 1;

      font->fontchar = malloc( sizeof(osd_fontchar_t) * font->num_fontchars );

      lprintf("font '%s' chars=%d\n", font->name, font->num_fontchars);

      /* load all characters */
      for( i = 0; i < font->num_fontchars; i++ ) {
        font->fontchar[i].code = gzread_i16(fp);
        font->fontchar[i].width = gzread_i16(fp);
        font->fontchar[i].height = gzread_i16(fp);
        font->fontchar[i].bmp = malloc(font->fontchar[i].width*font->fontchar[i].height);
        if( gzread(fp, font->fontchar[i].bmp,
              font->fontchar[i].width*font->fontchar[i].height) <= 0 )
          break;
      }

      /* check if all expected characters were loaded */
      if( i == font->num_fontchars ) {
        osd_font_t *known_font;
        ret = 1;

        lprintf("font '%s' loaded\n",font->name);

        /* check if font is already known to us */
        known_font = this->fonts;
        while( known_font ) {
          if( !strcasecmp(known_font->name,font->name) &&
               known_font->size == font->size )
            break;
          known_font = known_font->next;
        }

        if( !known_font ) {

          /* new font, add it to list */
          font->filename = strdup(filename);
          font->next = this->fonts;
          this->fonts = font;

        } else {

          if( !known_font->loaded ) {
            /* the font was preloaded before.
             * add loaded characters to the existing entry.
             */
            known_font->version = font->version;
            known_font->num_fontchars = font->num_fontchars;
            known_font->loaded = 1;
            known_font->fontchar = font->fontchar;
            free(font);
          } else {
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		    _("font '%s-%d' already loaded, weird.\n"), font->name, font->size);
            while( --i >= 0 ) {
              free(font->fontchar[i].bmp);
            }
            free(font->fontchar);
            free(font);
          }

        }
      } else {

        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		_("font '%s' loading failed (%d < %d)\n") ,font->name, i, font->num_fontchars);

        while( --i >= 0 ) {
          free(font->fontchar[i].bmp);
        }
        free(font->fontchar);
        free(font);
      }
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("wrong version for font '%s'. expected %d found %d.\n"), font->name, font->version, FONT_VERSION);
      free(font);
    }
    gzclose(fp);
  }

  return ret;
}

/*
 * unload font
 */
static int osd_renderer_unload_font(osd_renderer_t *this, char *fontname ) {

  osd_font_t *font, *last;
  osd_object_t *osd;
  int i, ret = 0;

  lprintf("font '%s'\n", fontname);

  pthread_mutex_lock (&this->osd_mutex);

  osd = this->osds;
  while( osd ) {
    if( !strcasecmp(osd->font->name, fontname) )
      osd->font = NULL;
    osd = osd->next;
  }

  last = NULL;
  font = this->fonts;
  while( font ) {
    if ( !strcasecmp(font->name,fontname) ) {

      free( font->filename );

      if( font->loaded ) {
        for( i = 0; i < font->num_fontchars; i++ ) {
          free( font->fontchar[i].bmp );
        }
        free( font->fontchar );
      }

      if( last )
        last->next = font->next;
      else
        this->fonts = font->next;

      free( font );
      ret = 1;
      break;
    }

    last = font;
    font = font->next;
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}

#ifdef HAVE_FT2

# ifdef HAVE_FONTCONFIG
/**
 * @brief Look up a font name using FontConfig library
 * @param osd The OSD object to load the font for.
 * @param fontname Name of the font to look up.
 * @param size Size of the font to look for.
 *
 * @return If the lookup was done correctly, a non-zero value is returned.
 */
static int osd_lookup_fontconfig( osd_object_t *osd, const char *const fontname, const int size ) {
  FcPattern *pat = NULL, *match = NULL;
  FcFontSet *fs = FcFontSetCreate();
  FcResult result;

  pat = FcPatternBuild(NULL, FC_FAMILY, FcTypeString, fontname, FC_SIZE, FcTypeDouble, (double)size, NULL);
  FcConfigSubstitute(NULL, pat, FcMatchPattern);
  FcDefaultSubstitute(pat);

  match = FcFontMatch(NULL, pat, &result);
  FcPatternDestroy(pat);

  if ( ! match ) {
    FcFontSetDestroy(fs);
    xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	    _("osd: error matching font %s with FontConfig"), fontname);
    return 0;
  }
  FcFontSetAdd(fs, match);

  if ( fs->nfont != 0 ) {
    FcChar8 *filename = NULL;
    FcPatternGetString(fs->fonts[0], FC_FILE, 0, &filename);
    if ( ! FT_New_Face(osd->ft2->library, (const char*)filename, 0, &osd->ft2->face) ) {
      FcFontSetDestroy(fs);
      return 1;
    }

    xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	    _("osd: error loading font %s with FontConfig"), fontname);
    return 0;
  } else {
    xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	    _("osd: error looking up font %s with FontConfig"), fontname);
    return 0;
  }
}
# endif /* HAVE_FONTCONFIG */

/**
 * @brief Look up a font file using XDG data directories.
 * @param osd The OSD object to load the font for.
 * @param fontname Name (absolute or relative) of the font to look up.
 *
 * @return If the lookup was done correctly, a non-zero value is returned.
 *
 * @see XDG Base Directory specification:
 *      http://standards.freedesktop.org/basedir-spec/latest/index.html
 */
static int osd_lookup_xdg( osd_object_t *osd, const char *const fontname ) {
  const char *const *data_dirs = xdgSearchableDataDirectories(&osd->renderer->stream->xine->basedir_handle);

  /* try load font from current directory or from an absolute path */
  if ( FT_New_Face(osd->ft2->library, fontname, 0, &osd->ft2->face) == FT_Err_Ok )
    return 1;

  if ( data_dirs )
    while( (*data_dirs) && *(*data_dirs) ) {
      FT_Error fte = FT_Err_Ok;
      char *fontpath = NULL;
      fontpath = _x_asprintf("%s/"PACKAGE"/fonts/%s", *data_dirs, fontname);

      fte = FT_New_Face(osd->ft2->library, fontpath, 0, &osd->ft2->face);

      free(fontpath);

      if ( fte == FT_Err_Ok )
	return 1;

      data_dirs++;
    }

  xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	  _("osd: error loading font %s with in XDG data directories.\n"), fontname);
  return 0;
}

static int osd_set_font_freetype2( osd_object_t *osd, const char *fontname, int size ) {
  if (!osd->ft2) {
    osd->ft2 = calloc(1, sizeof(osd_ft2context_t));
    if(FT_Init_FreeType( &osd->ft2->library )) {
      xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	      _("osd: cannot initialize ft2 library\n"));
      free(osd->ft2);
      osd->ft2 = NULL;
      return 0;
    }
  }

  if (osd->ft2->face) {
      FT_Done_Face (osd->ft2->face);
      osd->ft2->face = NULL;
  }

  do { /* while 0 */
#ifdef HAVE_FONTCONFIG
    if ( osd_lookup_fontconfig(osd, fontname, size) )
      break;
#endif
    if ( osd_lookup_xdg(osd, fontname) )
      break;

    osd_free_ft2 (osd);
    return 0;
  } while(0);

  if (FT_Set_Pixel_Sizes(osd->ft2->face, 0, size)) {
    xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	    _("osd: error setting font size (no scalable font?)\n"));
    osd_free_ft2 (osd);
    return 0;
  }

  osd->ft2->size = size;
  return 1;
}
#endif

/*
  set the font of osd object
*/

static int osd_set_font( osd_object_t *osd, const char *fontname, int size) {
  int ret = 1;

  lprintf("osd=%p font '%s'\n", osd, fontname);

  pthread_mutex_lock (&osd->renderer->osd_mutex);

#ifdef HAVE_FT2
  if ( ! osd_set_font_freetype2(osd, fontname, size) )
#endif
    { /* If the FreeType2 loading failed */
      osd_font_t *font;
      int best = 0;
      osd->font = NULL;
      ret = 0;

      font = osd->renderer->fonts;
      while( font ) {

	if( !strcasecmp(font->name, fontname) && (size>=font->size)
	    && (best<font->size)) {
	  ret = 1;
	  osd->font = font;
	  best = font->size;
	  lprintf ("best: font->name=%s, size=%d\n", font->name, font->size);
	}
	font = font->next;
      }

      if( ret ) {
	/* load font if needed */
	if( !osd->font->loaded )
	  ret = osd_renderer_load_font(osd->renderer, osd->font->filename);
	if(!ret)
	  osd->font = NULL;
      }
    }

  pthread_mutex_unlock (&osd->renderer->osd_mutex);
  return ret;
}


/*
 * search the character in the sorted array,
 *
 * returns ALIAS_CHARACTER_FONT if character 'code' isn't found,
 * returns 'n' on error
 */
static int osd_search(osd_fontchar_t *array, size_t n, uint16_t code) {
#ifdef BINARY_SEARCH
  size_t i, left, right;

  if (!n) return 0;

  left = 0;
  right = n - 1;
  while (left < right) {
    i = (left + right) >> 1;
    if (code <= array[i].code) right = i;
    else left = i + 1;
  }

  if (array[right].code == code)
    return right;
  else
    return ALIAS_CHARACTER_FONT < n ? ALIAS_CHARACTER_FONT : n;
#else
  size_t i;

  for( i = 0; i < n; i++ ) {
    if( font->fontchar[i].code == unicode )
      break;
  }

  if (i < n)
    return i;
  else
    return ALIAS_CHARACTER_FONT < n ? ALIAS_CHARACTER_FONT : n;
#endif
}


#ifdef HAVE_ICONV
/*
 * get next unicode value
 */
static uint16_t osd_iconv_getunicode(xine_t *xine,
				     iconv_t cd, const char *encoding, ICONV_CONST char **inbuf,
				     size_t *inbytesleft) {
  uint16_t unicode;
  char *outbuf = (char*)&unicode;
  size_t outbytesleft = 2;
  size_t count;

  if (cd != (iconv_t)-1) {
    /* get unicode value from iconv */
    count = iconv(cd, inbuf, inbytesleft, &outbuf, &outbytesleft);
    if (count == (size_t)-1 && errno != E2BIG) {
      /* unknown character or character wider than 16 bits, try skip one byte */
      xprintf(xine, XINE_VERBOSITY_LOG,
	      _("osd: unknown sequence starting with byte 0x%02X in encoding \"%s\", skipping\n"),
	      (*inbuf)[0] & 0xFF, encoding);
      if (*inbytesleft) {
        (*inbytesleft)--;
        (*inbuf)++;
      }
      return ALIAS_CHARACTER_CONV;
    }
  } else {
    /* direct mapping without iconv */
    unicode = (unsigned char)(*inbuf)[0];
    (*inbuf)++;
    (*inbytesleft)--;
  }

  return unicode;
}
#endif


/*
 * free iconv encoding
 */
static void osd_free_encoding(osd_object_t *osd) {
#ifdef HAVE_ICONV
  if (osd->cd != (iconv_t)-1) {
    iconv_close(osd->cd);
    osd->cd = (iconv_t)-1;
  }
  if (osd->encoding) {
    free(osd->encoding);
    osd->encoding = NULL;
  }
#endif
}


/*
 * set encoding of text
 *
 * NULL ... no conversion (iso-8859-1)
 * ""   ... locale encoding
 */
static int osd_set_encoding (osd_object_t *osd, const char *encoding) {
#ifdef HAVE_ICONV
  char *enc;

  osd_free_encoding(osd);

  lprintf("osd=%p, encoding=%s\n", osd, encoding ? (encoding[0] ? encoding : "locale") : "no conversion");
  /* no conversion, use latin1 */
  if (!encoding) return 1;
  /* get encoding from system */
  if (!encoding[0]) {
    if ((enc = xine_get_system_encoding()) == NULL) {
      xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	      _("osd: can't find out current locale character set\n"));
      return 0;
    }
    lprintf("locale encoding='%s'\n", enc);
  } else
    enc = strdup(encoding);

  /* prepare conversion to UCS-2 */
  if ((osd->cd = iconv_open(UCS2_ENCODING, enc)) == (iconv_t)-1) {
    xprintf(osd->renderer->stream->xine, XINE_VERBOSITY_LOG,
	    _("osd: unsupported conversion %s -> %s, no conversion performed\n"), enc, UCS2_ENCODING);
    free(enc);
    return 0;
  }

  osd->encoding = enc;
  return 1;
#else
  return encoding == NULL;
#endif /* HAVE_ICONV */
}


#define FONT_OVERLAP 1/10  /* overlap between consecutive characters */

/*
 * render text in current encoding on x,y position
 *  no \n yet
 */
static int osd_render_text (osd_object_t *osd, int x1, int y1,
                            const char *text, int color_base) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i, y;
  uint8_t *dst, *src;
  const char *inbuf;
  uint16_t unicode;
  size_t inbytesleft;

#ifdef HAVE_FT2
  FT_UInt previous = 0;
  FT_Bool use_kerning = osd->ft2 && FT_HAS_KERNING(osd->ft2->face);
  int first = 1;
#endif

  lprintf("osd=%p (%d,%d) \"%s\"\n", osd, x1, y1, text);

  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  pthread_mutex_lock (&this->osd_mutex);

  {
    int proceed = 0;

    if ((font = osd->font)) proceed = 1;
#ifdef HAVE_FT2
    if (osd->ft2) proceed = 1;
#endif

    if (proceed == 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: font isn't defined\n"));
      pthread_mutex_unlock(&this->osd_mutex);
      return 0;
    }
  }

  if( x1 < osd->x1 ) osd->x1 = x1;
  if( y1 < osd->y1 ) osd->y1 = y1;
  osd->area_touched = 1;

  inbuf = text;
  inbytesleft = strlen(text);

  while( inbytesleft ) {
#ifdef HAVE_ICONV
    unicode = osd_iconv_getunicode(this->stream->xine, osd->cd, osd->encoding,
                                   (ICONV_CONST char **)&inbuf, &inbytesleft);
#else
    unicode = inbuf[0];
    inbuf++;
    inbytesleft--;
#endif


#ifdef HAVE_FT2
    if (osd->ft2) {

      FT_GlyphSlot slot = osd->ft2->face->glyph;

      i = FT_Get_Char_Index( osd->ft2->face, unicode );

      /* add kerning relative to the previous letter */
      if (use_kerning && previous && i) {
        FT_Vector delta;
        FT_Get_Kerning(osd->ft2->face, previous, i, KERNING_DEFAULT, &delta);
        x1 += delta.x / 64;
      }
      previous = i;

      if (FT_Load_Glyph(osd->ft2->face, i, FT_LOAD_FLAGS)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: error loading glyph\n"));
        continue;
      }

      if (slot->format != ft_glyph_format_bitmap) {
        if (FT_Render_Glyph(slot, ft_render_mode_normal))
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: error in rendering glyph\n"));
      }

      /* if the first letter has a bearing not on the basepoint shift, the
       * whole output to be sure that we are inside the bounding box
       */
      if (first) x1 -= slot->bitmap_left;
      first = 0;

      /* we shift the whole glyph down by it's ascender so that the specified
       * coordinate is the top left corner which is much more practical than
       * the baseline as the user normally has no idea where the baseline is
       */
      dst = osd->area + (y1 + osd->ft2->face->size->metrics.ascender/64 - slot->bitmap_top) * osd->width;
      src = (uint8_t*) slot->bitmap.buffer;

      for (y = 0; y < slot->bitmap.rows; y++) {
        uint8_t *s = src;
        uint8_t *d = dst + x1 + slot->bitmap_left;

        if (d >= osd->area + osd->width*osd->height)
          break;

        if (dst > osd->area)
          while (s < src + slot->bitmap.width) {
            if ((d >= dst) && (d < dst + osd->width) && *s)
              *d = (uint8_t)(*s/25) + (uint8_t) color_base;

            d++;
            s++;
          }

        src += slot->bitmap.pitch;
        dst += osd->width;

      }

      x1 += slot->advance.x / 64;
      if( x1 > osd->x2 ) osd->x2 = x1;
      if( y1 + osd->ft2->face->size->metrics.height/64 > osd->y2 ) osd->y2 = y1 + osd->ft2->face->size->metrics.height/64;

    } else {

#endif

      i = osd_search(font->fontchar, font->num_fontchars, unicode);

      lprintf("font '%s' [%d, U+%04X == U+%04X] %dx%d -> %d,%d\n", font->name, i,
             unicode, font->fontchar[i].code, font->fontchar[i].width,
             font->fontchar[i].height, x1, y1);

      if ( i != font->num_fontchars ) {
        dst = osd->area + y1 * osd->width;
        src = font->fontchar[i].bmp;

        for( y = 0; y < font->fontchar[i].height; y++ ) {
          uint8_t *s = src;
          uint8_t *d = dst + x1;

          if (d >= osd->area + osd->width*osd->height)
            break;

          if (dst >= osd->area)
            while (s < src + font->fontchar[i].width) {
              if((d >= dst) && (d < dst + osd->width) && (*s > 1)) /* skip drawing transparency */
                *d = *s + (uint8_t) color_base;

              d++;
              s++;
            }
          src += font->fontchar[i].width;
          dst += osd->width;
        }
        x1 += font->fontchar[i].width - (font->fontchar[i].width * FONT_OVERLAP);

        if( x1 > osd->x2 ) osd->x2 = x1;
        if( y1 + font->fontchar[i].height > osd->y2 )
          osd->y2 = y1 + font->fontchar[i].height;
      }

#ifdef HAVE_FT2
    } /* !(osd->ft2) */
#endif

  }

  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

/*
  get width and height of how text will be renderized
*/
static int osd_get_text_size(osd_object_t *osd, const char *text, int *width, int *height) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i;
  const char *inbuf;
  uint16_t unicode;
  size_t inbytesleft;

#ifdef HAVE_FT2
  /* not all free type fonts provide kerning */
  FT_Bool use_kerning = osd->ft2 && FT_HAS_KERNING(osd->ft2->face);
  FT_UInt previous = 0;
  int first_glyph = 1;
#endif

  lprintf("osd=%p \"%s\"\n", osd, text);

  pthread_mutex_lock (&this->osd_mutex);

  {
    int proceed = 0;

    if ((font = osd->font)) proceed = 1;
#ifdef HAVE_FT2
    if (osd->ft2) proceed = 1;
#endif

    if (proceed == 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: font isn't defined\n"));
      pthread_mutex_unlock(&this->osd_mutex);
      return 0;
    }
  }

  *width = 0;
  *height = 0;

  inbuf = text;
  inbytesleft = strlen(text);

  while( inbytesleft ) {
#ifdef HAVE_ICONV
    unicode = osd_iconv_getunicode(this->stream->xine, osd->cd, osd->encoding,
                                   (ICONV_CONST char **)&inbuf, &inbytesleft);
#else
    unicode = inbuf[0];
    inbuf++;
    inbytesleft--;
#endif

#ifdef HAVE_FT2
    if (osd->ft2) {
      FT_GlyphSlot  slot = osd->ft2->face->glyph;

      i = FT_Get_Char_Index( osd->ft2->face, unicode);

      /* kerning add the relative to the previous letter */
      if (use_kerning && previous && i) {
        FT_Vector delta;
        FT_Get_Kerning(osd->ft2->face, previous, i, KERNING_DEFAULT, &delta);
        *width += delta.x / 64;
      }
      previous = i;

      if (FT_Load_Glyph(osd->ft2->face, i, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: error loading glyph %i\n"), i);
        text++;
        continue;
      }

      if (slot->format != ft_glyph_format_bitmap) {
        if (FT_Render_Glyph(osd->ft2->face->glyph, ft_render_mode_normal))
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("osd: error in rendering\n"));
      }
      /* left shows the left edge relative to the base point. A positive value means the
       * letter is shifted right, so we need to subtract the value from the width
       */
      if (first_glyph) *width -= slot->bitmap_left;
      first_glyph = 0;
      *width += slot->advance.x / 64;
      text++;
    } else {
#endif
      i = osd_search(font->fontchar, font->num_fontchars, unicode);

      if ( i != font->num_fontchars ) {
        if( font->fontchar[i].height > *height )
          *height = font->fontchar[i].height;
        *width += font->fontchar[i].width - (font->fontchar[i].width * FONT_OVERLAP);
      }
#ifdef HAVE_FT2
    } /* !(osd->ft2) */
#endif
  }

#ifdef HAVE_FT2
  if (osd->ft2) {
    /* if we have a true type font we need to do some corrections for the last
     * letter. As this one is still in the gylph slot we can still work with
     * it. For the last letter be must not use advance and width but the real
     * width of the bitmap. We're right from the base point so we subtract the
     * advance value that was added in the for-loop and add the width. We have
     * to also add the left bearing because the letter might be shifted left or
     * right and then the right edge is also shifted
     */
    if (osd->ft2->face->glyph->bitmap.width)
        *width -= osd->ft2->face->glyph->advance.x / 64;
    *width += osd->ft2->face->glyph->bitmap.width;
    *width += osd->ft2->face->glyph->bitmap_left;
    *height = osd->ft2->face->size->metrics.height / 64;
  }
#endif

  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

static void osd_preload_fonts (osd_renderer_t *this, char *path) {
  DIR   *dir;
  char  *s, *p;

  lprintf ("path='%s'\n", path);

  dir = opendir (path);

  if (dir) {
    struct dirent  *entry;

    while ((entry = readdir (dir)) != NULL) {
      int  len;

      len = strlen (entry->d_name);

      if ( (len > 12) && !strncmp (&entry->d_name[len-12], ".xinefont.gz", 12)) {

        s = strdup(entry->d_name);
        p = strchr(s, '-');

        if( p ) {
	  osd_font_t  *font;

          *p++ = '\0';
          font = calloc(1, sizeof(osd_font_t) );

          strncpy(font->name, s, sizeof(font->name));
          font->size = atoi(p);

          lprintf("font '%s' size %d is preloaded\n",
                  font->name, font->size);

          font->filename = _x_asprintf ("%s/%s", path, entry->d_name);

          font->next = this->fonts;
          this->fonts = font;
        }
        free(s);
      }
    }

    closedir (dir);
  }
}

/*
 * free osd object
 */

static void osd_free_object (osd_object_t *osd_to_close) {

  osd_renderer_t *this = osd_to_close->renderer;
  video_overlay_manager_t *ovl_manager;
  osd_object_t *osd, *last;

  if( osd_to_close->handle >= 0 ) {
    osd_hide(osd_to_close,0);

    this->event.object.handle = osd_to_close->handle;

    /* not really needed this, but good pratice to clean it up */
    memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
    this->event.event_type = OVERLAY_EVENT_FREE_HANDLE;
    this->event.vpts = 0;

    this->stream->xine->port_ticket->acquire(this->stream->xine->port_ticket, 1);
    ovl_manager = this->stream->video_out->get_overlay_manager(this->stream->video_out);
    ovl_manager->add_event(ovl_manager, (void *)&this->event);
    this->stream->xine->port_ticket->release(this->stream->xine->port_ticket, 1);

    osd_to_close->handle = -1; /* handle will be freed */
  }

  if (osd_to_close->argb_layer) {
    /* clear argb buffer pointer so that buffer may be freed safely after returning */
    this->set_argb_buffer(osd_to_close, NULL, 0, 0, 0, 0);
    set_argb_layer_ptr(&osd_to_close->argb_layer, NULL);
  }

  pthread_mutex_lock (&this->osd_mutex);

  last = NULL;
  osd = this->osds;
  while( osd ) {
    if ( osd == osd_to_close ) {
      free( osd->area );

      osd_free_ft2 (osd);
      osd_free_encoding(osd);

      if( last )
        last->next = osd->next;
      else
        this->osds = osd->next;

      free( osd );
      break;
    }
    last = osd;
    osd = osd->next;
  }
  pthread_mutex_unlock (&this->osd_mutex);
}

static void osd_renderer_close (osd_renderer_t *this) {

  while( this->osds )
    osd_free_object ( this->osds );

  while( this->fonts )
    osd_renderer_unload_font( this, this->fonts->name );

  pthread_mutex_destroy (&this->osd_mutex);

  free(this->event.object.overlay);
  free(this);
}


static void update_text_palette(void *this_gen, xine_cfg_entry_t *entry)
{
  osd_renderer_t *this = (osd_renderer_t *)this_gen;

  this->textpalette = entry->num_value;
  lprintf("palette will be '%s'\n", textpalettes_str[this->textpalette] );
}

static void osd_draw_bitmap(osd_object_t *osd, uint8_t *bitmap,
			    int x1, int y1, int width, int height,
			    uint8_t *palette_map)
{
  int y, x;

  lprintf("osd=%p at (%d,%d) %dx%d\n",osd, x1,y1, width,height );

  /* update clipping area */
  osd->x1 = MIN( osd->x1, x1 );
  osd->x2 = MAX( osd->x2, x1+width );
  osd->y1 = MIN( osd->y1, y1 );
  osd->y2 = MAX( osd->y2, y1+height );
  osd->area_touched = 1;

  for( y=0; y<height; y++ ) {
    if ( palette_map ) {
      int src_offset = y * width;
      int dst_offset = (y1+y) * osd->width + x1;
      /* Slow copy with palette translation, the map describes how to
         convert color indexes in the source bitmap to indexes in the
         osd palette */
      for ( x=0; x<width; x++ ) {
	osd->area[dst_offset+x] = palette_map[bitmap[src_offset+x]];
      }
    } else {
      /* Fast copy with direct mapping */
      memcpy(osd->area + (y1+y) * osd->width + x1, bitmap + y * width, width);
    }
  }
}

static void osd_set_argb_buffer(osd_object_t *osd, uint32_t *argb_buffer,
    int dirty_x, int dirty_y, int dirty_width, int dirty_height)
{
  if (!osd->argb_layer)
    set_argb_layer_ptr(&osd->argb_layer, argb_layer_create());

  if (osd->argb_layer->buffer != argb_buffer) {
    dirty_x = 0;
    dirty_y = 0;
    dirty_width = osd->width;
    dirty_height = osd->height;
  }

  /* keep osd_object clipping behavior */
  osd->x1 = MIN( osd->x1, dirty_x );
  osd->x2 = MAX( osd->x2, dirty_x + dirty_width );
  osd->y1 = MIN( osd->y1, dirty_y );
  osd->y2 = MAX( osd->y2, dirty_y + dirty_height );

  pthread_mutex_lock(&osd->argb_layer->mutex);

  /* argb layer update area accumulation */
  osd->argb_layer->x1 = MIN( osd->argb_layer->x1, dirty_x );
  osd->argb_layer->x2 = MAX( osd->argb_layer->x2, dirty_x + dirty_width );
  osd->argb_layer->y1 = MIN( osd->argb_layer->y1, dirty_y );
  osd->argb_layer->y2 = MAX( osd->argb_layer->y2, dirty_y + dirty_height );

  osd->argb_layer->buffer = argb_buffer;

  pthread_mutex_unlock(&osd->argb_layer->mutex);
}

static uint32_t osd_get_capabilities (osd_object_t *osd) {

  osd_renderer_t *this = osd->renderer;
  uint32_t capabilities = 0;
  uint32_t vo_capabilities;

#ifdef HAVE_FT2
  capabilities |= XINE_OSD_CAP_FREETYPE2;
#endif

  this->stream->xine->port_ticket->acquire(this->stream->xine->port_ticket, 1);
  vo_capabilities = this->stream->video_out->get_capabilities(this->stream->video_out);
  this->stream->xine->port_ticket->release(this->stream->xine->port_ticket, 1);

  if (vo_capabilities & VO_CAP_UNSCALED_OVERLAY)
    capabilities |= XINE_OSD_CAP_UNSCALED;

  if (vo_capabilities & VO_CAP_CUSTOM_EXTENT_OVERLAY)
    capabilities |= XINE_OSD_CAP_CUSTOM_EXTENT;

  if (vo_capabilities & VO_CAP_ARGB_LAYER_OVERLAY)
    capabilities |= XINE_OSD_CAP_ARGB_LAYER;

  if (vo_capabilities & VO_CAP_VIDEO_WINDOW_OVERLAY)
    capabilities |= XINE_OSD_CAP_VIDEO_WINDOW;

  return capabilities;
}


/*
 * initialize the osd rendering engine
 */

osd_renderer_t *_x_osd_renderer_init( xine_stream_t *stream ) {

  osd_renderer_t *this;

  this = calloc(1, sizeof(osd_renderer_t));
  this->stream = stream;
  this->event.object.overlay = calloc(1, sizeof(vo_overlay_t));

  pthread_mutex_init (&this->osd_mutex, NULL);

  /*
   * load available fonts
   */
  {
    const char *const *data_dirs = xdgSearchableDataDirectories(&stream->xine->basedir_handle);
    if ( data_dirs )
      while( (*data_dirs) && *(*data_dirs) ) {
	/* sizeof("") takes care of the final NUL byte */
	char *fontpath = xine_xmalloc( strlen(*data_dirs) + sizeof("/"PACKAGE"/fonts/") );
	strcpy(fontpath, *data_dirs);
	strcat(fontpath, "/"PACKAGE"/fonts/");

	osd_preload_fonts(this, fontpath);

	free(fontpath);

	data_dirs++;
      }
  }

  this->textpalette = this->stream->xine->config->register_enum (this->stream->xine->config,
                                             "ui.osd.text_palette", 0,
                                             textpalettes_str,
                                             _("palette (foreground-border-background) to use for subtitles and OSD"),
                                             _("The palette for on-screen-display and some subtitle formats that do "
					       "not specify any colouring themselves. The palettes are listed in the "
					       "form: foreground-border-background."),
                                             10, update_text_palette, this);

  /*
   * set up function pointer
   */

  this->new_object         = osd_new_object;
  this->free_object        = osd_free_object;
  this->show               = osd_show_scaled;
  this->hide               = osd_hide;
  this->set_palette        = osd_set_palette;
  this->set_text_palette   = osd_set_text_palette;
  this->get_palette        = osd_get_palette;
  this->set_position       = osd_set_position;
  this->set_font           = osd_set_font;
  this->clear              = osd_clear;
  this->point              = osd_point;
  this->line               = osd_line;
  this->filled_rect        = osd_filled_rect;
  this->set_encoding       = osd_set_encoding;
  this->render_text        = osd_render_text;
  this->get_text_size      = osd_get_text_size;
  this->close              = osd_renderer_close;
  this->draw_bitmap        = osd_draw_bitmap;
  this->set_argb_buffer    = osd_set_argb_buffer;
  this->show_unscaled      = osd_show_unscaled;
  this->show_scaled        = osd_show_gui_scaled;
  this->get_capabilities   = osd_get_capabilities;
  this->set_extent         = osd_set_extent;
  this->set_video_window   = osd_set_video_window;

  return this;
}
