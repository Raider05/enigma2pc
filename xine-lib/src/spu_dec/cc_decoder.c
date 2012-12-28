/*
 * Copyright (C) 2000-2003 the xine project
 *
 * Copyright (C) Christian Vogler
 *               cvogler@gradient.cis.upenn.edu - December 2001
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
 * stuff needed to provide closed captioning decoding and display
 *
 * Some small bits and pieces of the EIA-608 captioning decoder were
 * adapted from CCDecoder 0.9.1 by Mike Baker. The latest version is
 * available at http://sourceforge.net/projects/ccdecoder/.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>

#include <inttypes.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/xineutils.h>
#include <xine/osd.h>
#include "cc_decoder.h"
#include <xine/osd.h>

/*
#define LOG_DEBUG 3
*/

/* at 29.97 fps, each NTSC frame takes 3003 metronom ticks on the average. */
#define NTSC_FRAME_DURATION 3003

#define CC_ROWS 15
#define CC_COLUMNS 32
#define CC_CHANNELS 2

/* 1 is the caption background color index in the OSD palettes. */
#define CAP_BG_COL 1

/* number of text colors specified by EIA-608 standard */
#define NUM_FG_COL 7

#ifndef WIN32
/* colors specified by the EIA 608 standard */
enum { WHITE, GREEN, BLUE, CYAN, RED, YELLOW, MAGENTA, BLACK, TRANSPARENT };
#else
/* colors specified by the EIA 608 standard */
enum { WHITE, GREEN, BLUE, CYAN, RED, YELLOW, MAGENTA, BLACK };
#endif



/* color mapping to OSD text color indices */
static const int text_colormap[NUM_FG_COL] = {
  OSD_TEXT1, OSD_TEXT2, OSD_TEXT3, OSD_TEXT4, OSD_TEXT5, OSD_TEXT6, OSD_TEXT7
};


/* -------------------- caption text colors -----------------------------*/
/* FIXME: The colors look fine on an XShm display, but they look *terrible*
   with the Xv display on the NVidia driver on a GeForce 3. The colors bleed
   into each other more than I'd expect from the downsampling into YUV
   colorspace.
   At this moment, it looks like a problem in the Xv YUV blending functions.
*/
typedef struct colorinfo_s {
  clut_t bgcol;           /* text background color */
  clut_t bordercol;       /* text border color */
  clut_t textcol;         /* text color */
} colorinfo_t;


static const colorinfo_t cc_text_trans[NUM_FG_COL] = {
  /* white, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80)
  },

  /* green, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x90, 0x22, 0x35)
  },

  /* blue, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x29, 0x6e, 0xff)
  },

  /* cyan, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xaa, 0x10, 0xa6)
  },

  /* red, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x52, 0xf0, 0x5a)
  },

  /* yellow, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xd4, 0x92, 0x10)
  },

  /* magenta, black border, translucid */
  {
    CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x6b, 0xde, 0xca)
  }
};

static const colorinfo_t cc_text_solid[NUM_FG_COL] = {
  /* white, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80)
  },

  /* green, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x90, 0x22, 0x35)
  },

  /* blue, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x29, 0x6e, 0xff)
  },

  /* cyan, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xaa, 0x10, 0xa6)
  },

  /* red, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x52, 0xf0, 0x5a)
  },

  /* yellow, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0xd4, 0x92, 0x10)
  },

  /* magenta, black border, solid */
  {
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
    CLUT_Y_CR_CB_INIT(0x6b, 0xde, 0xca)
  }
};


static const uint8_t cc_text_trans_alpha[TEXT_PALETTE_SIZE] = {
  0, 8, 9, 10, 11, 12, 15, 15, 15, 15, 15
};

static const uint8_t cc_text_solid_alpha[TEXT_PALETTE_SIZE] = {
  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
};


static const colorinfo_t *const cc_text_palettes[NUM_CC_PALETTES] = {
  cc_text_trans,
  cc_text_solid
};

static const uint8_t *const cc_alpha_palettes[NUM_CC_PALETTES] = {
  cc_text_trans_alpha,
  cc_text_solid_alpha
};

/* --------------------- misc. EIA 608 definitions -------------------*/

#define TRANSP_SPACE 0x19   /* code for transparent space, essentially
			       arbitrary */

/* mapping from PAC row code to actual CC row */
static const int  rowdata[] = {10, -1, 0, 1, 2, 3, 11, 12, 13, 14, 4, 5, 6,
			 7, 8, 9};
/* FIXME: do real ™ (U+2122) */
/* Code 182 must be mapped as a musical note ('♪', U+266A) in the caption font */
static const char specialchar[] = {
  174 /* ® */, 176 /* ° */, 189 /* ½ */, 191 /* ¿ */,
  'T' /* ™ */, 162 /* ¢ */, 163 /* £ */, 182 /* ¶ => ♪ */,
  224 /* à */, TRANSP_SPACE,232 /* è */, 226 /* â */,
  234 /* ê */, 238 /* î */, 244 /* ô */, 251 /* û */
};

/**
 * @brief Character translation table
 *
 * EIA 608 codes are not all the same as ASCII
 *
 * The code to produce the characters table would be the following:
 *
 * static void build_char_table(void)
 * {
 *   int i;
 *   // first the normal ASCII codes
 *   for (i = 0; i < 128; i++)
 *     chartbl[i] = (char) i;
 *   // now the special codes
 *   chartbl[0x2a] = 225; // á
 *   chartbl[0x5c] = 233; // é
 *   chartbl[0x5e] = 237; // í
 *   chartbl[0x5f] = 243; // ó
 *   chartbl[0x60] = 250; // ú
 *   chartbl[0x7b] = 231; // ç
 *   chartbl[0x7c] = 247; // ÷
 *   chartbl[0x7d] = 209; // Ñ
 *   chartbl[0x7e] = 241; // ñ
 *   chartbl[0x7f] = 164; // ¤ FIXME: should be a solid block ('█'; U+2588)
 * }
 *
 */
static const int chartbl[128] = {
  '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
  '\x08', '\x09', '\x0a', '\x0b', '\x0c', '\x0d', '\x0e', '\x0f',
  '\x10', '\x11', '\x12', '\x13', '\x14', '\x15', '\x16', '\x17',
  '\x18', '\x19', '\x1a', '\x1b', '\x1c', '\x1d', '\x1e', '\x1f',
  '\x20', '\x21', '\x22', '\x23', '\x24', '\x25', '\x26', '\x27',
  '\x28', '\x29', '\xe1', '\x2b', '\x2c', '\x2d', '\x2e', '\x2f',
  '\x30', '\x31', '\x32', '\x33', '\x34', '\x35', '\x36', '\x37',
  '\x38', '\x39', '\x3a', '\x3b', '\x3c', '\x3d', '\x3e', '\x3f',
  '\x40', '\x41', '\x42', '\x43', '\x44', '\x45', '\x46', '\x47',
  '\x48', '\x49', '\x4a', '\x4b', '\x4c', '\x4d', '\x4e', '\x4f',
  '\x50', '\x51', '\x52', '\x53', '\x54', '\x55', '\x56', '\x57',
  '\x58', '\x59', '\x5a', '\x5b', '\xe9', '\x5d', '\xed', '\xf3',
  '\xfa', '\x61', '\x62', '\x63', '\x64', '\x65', '\x66', '\x67',
  '\x68', '\x69', '\x6a', '\x6b', '\x6c', '\x6d', '\x6e', '\x6f',
  '\x70', '\x71', '\x72', '\x73', '\x74', '\x75', '\x76', '\x77',
  '\x78', '\x79', '\x7a', '\xe7', '\xf7', '\xd1', '\xf1', '\xa4'
};

/**
 * @brief Parity table for packets
 *
 * CC codes use odd parity for error detection, since they originally were
 * transmitted via noisy video signals.
 *
 * The code to produce the parity table would be the following:
 *
 * static int parity(uint8_t byte)
 * {
 *   int i;
 *   int ones = 0;
 *
 *   for (i = 0; i < 7; i++) {
 *     if (byte & (1 << i))
 *       ones++;
 *   }
 *
 *   return ones & 1;
 * }
 *
 * static void build_parity_table(void)
 * {
 *   uint8_t byte;
 *   int parity_v;
 *   for (byte = 0; byte <= 127; byte++) {
 *     parity_v = parity(byte);
 *     // CC uses odd parity (i.e., # of 1's in byte is odd.)
 *     parity_table[byte] = parity_v;
 *     parity_table[byte | 0x80] = !parity_v;
 *   }
 * }
 */
static const int parity_table[256] = {
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
};

/*---------------- decoder data structures -----------------------*/

/* CC renderer */
struct cc_renderer_s {
  int video_width;            /* video dimensions */
  int video_height;

  int x;                      /* coordinates of the captioning area */
  int y;
  int width;
  int height;
  int max_char_height;        /* captioning font properties */
  int max_char_width;

  osd_renderer_t *osd_renderer;   /* active OSD renderer */
  osd_object_t *cap_display;  /* caption display object */
  int displayed;              /* true when caption currently is displayed */

  /* the next variable is a hack: hiding a caption with vpts 0 doesn't seem
     to work if the caption has been registered in the SPU event queue, but
     not yet displayed. So we remember the vpts of the show event, and use
     that as the vpts of the hide event upon an osd free.
  */
/*FIXME: bug in OSD or SPU?*/
  int64_t display_vpts;       /* vpts of currently displayed caption */

  /* this variable is an even worse hack: in some rare cases, the pts
     information on the DVD gets out of sync with the caption information.
     If this happens, the vpts of a hide caption event can actually be
     slightly higher than the vpts of the following show caption event.
     For this reason, we remember the vpts of the hide event and force
     the next show event's vpts  to be at least equal to the hide event's
     vpts.
  */
  int64_t last_hide_vpts;

  /* caption palette and alpha channel */
  uint32_t cc_palette[OVL_PALETTE_SIZE];
  uint8_t cc_trans[OVL_PALETTE_SIZE];

  metronom_t *metronom;       /* the active xine metronom */

  cc_state_t *cc_state;        /* captioning configuration */
};


/* CC attribute */
typedef struct cc_attribute_s {
  uint8_t italic;
  uint8_t underline;
  uint8_t foreground;
  uint8_t background;
} cc_attribute_t;

/* CC character cell */
typedef struct cc_char_cell_s {
  uint8_t c;                   /* character code, not the same as ASCII */
  cc_attribute_t attributes;   /* attributes of this character, if changed */
			       /* here */
  int midrow_attr;             /* true if this cell changes an attribute */
} cc_char_cell_t;

/* a single row in the closed captioning memory */
typedef struct cc_row_s {
  cc_char_cell_t cells[CC_COLUMNS];
  int pos;                   /* position of the cursor */
  int num_chars;             /* how many characters in the row are data */
  int attr_chg;              /* true if midrow attr. change at cursor pos */
  int pac_attr_chg;          /* true if attribute has changed via PAC */
  cc_attribute_t pac_attr;   /* PAC attr. that hasn't been applied yet */
} cc_row_t;

/* closed captioning memory for a single channel */
typedef struct cc_buffer_s {
  cc_row_t rows[CC_ROWS];
  int rowpos;              /* row cursor position */
} cc_buffer_t;

/* captioning memory for all channels */
typedef struct cc_memory_s {
  cc_buffer_t channel[CC_CHANNELS];
  int channel_no;          /* currently active channel */
} cc_memory_t;

/* The closed captioning decoder data structure */
struct cc_decoder_s {
  /* CC decoder buffer  - one onscreen, one offscreen */
  cc_memory_t buffer[2];
  /* onscreen, offscreen buffer ptrs */
  cc_memory_t *on_buf;
  cc_memory_t *off_buf;
  /* which buffer is active for receiving data */
  cc_memory_t **active;

  /* for logging and debugging purposes, captions are assigned increasing */
  /*   unique ids. */
  uint32_t capid;

  /* the last captioning code seen (control codes are often sent twice
     in a row, but should be processed only once) */
  uint32_t lastcode;

  /* The PTS and SCR at which the captioning chunk started */
  int64_t pts;
  /* holds the NTSC frame offset to last known pts/scr */
  uint32_t f_offset;

  /* active OSD renderer */
  osd_renderer_t     *renderer;
  /* true when caption currently is displayed */
  int displayed;

  /* configuration and intrinsics of CC decoder */
  cc_state_t *cc_state;

  metronom_t *metronom;
};


/*---------------- general utility functions ---------------------*/

static void get_font_metrics(osd_renderer_t *renderer,
			     const char *fontname, int font_size,
			     int *maxw, int *maxh)
{
  int c;
  osd_object_t *testc = renderer->new_object(renderer, 640, 480);

  *maxw = 0;
  *maxh = 0;

  renderer->set_font(testc, (char *) fontname, font_size);
  renderer->set_encoding(testc, "iso-8859-1");
  for (c = 32; c < 256; c++) {
    int tw, th;
    const char buf[2] = { c, '\0' };

    renderer->get_text_size(testc, buf, &tw, &th);
    *maxw = MAX(*maxw, tw);
    *maxh = MAX(*maxh, th);
  }
  renderer->free_object(testc);
}


static int good_parity(uint16_t data)
{
  int ret = parity_table[data & 0xff] && parity_table[(data & 0xff00) >> 8];
  if (! ret)
    printf("Bad parity in EIA-608 data (%x)\n", data);
  return ret;
}




static clut_t interpolate_color(clut_t src, clut_t dest, int steps,
				int current_step)
{
  int diff_y = ((int) dest.y) - ((int) src.y);
  int diff_cr = ((int) dest.cr) - ((int) src.cr);
  int diff_cb = ((int) dest.cb) - ((int) src.cb);
  int res_y = ((int) src.y) + (diff_y * current_step / (steps + 1));
  int res_cr = ((int) src.cr) + (diff_cr * current_step / (steps + 1));
  int res_cb = ((int) src.cb) + (diff_cb * current_step / (steps + 1));
#if __SUNPRO_C
  /*
   * Sun's Forte compiler refuses to initialize automatic structure
   * variable with bitfields, so we use explicit assignments for now.
   */
  clut_t res;
  res.y = res_y;
  res.cr = res_cr;
  res.cb = res_cb;
  res.foo = 0;
#else
  clut_t res = CLUT_Y_CR_CB_INIT((uint8_t) res_y, (uint8_t) res_cr,
				 (uint8_t) res_cb);
#endif
  return res;
}

/*----------------- cc_row_t methods --------------------------------*/

static void ccrow_fill_transp(cc_row_t *rowbuf){
  int i;

#ifdef LOG_DEBUG
  printf("cc_decoder: ccrow_fill_transp: Filling in %d transparent spaces.\n",
	 rowbuf->pos - rowbuf->num_chars);
#endif
  for (i = rowbuf->num_chars; i < rowbuf->pos; i++) {
    rowbuf->cells[i].c = TRANSP_SPACE;
    rowbuf->cells[i].midrow_attr = 0;
  }
}


static int ccrow_find_next_text_part(cc_row_t *this, int pos)
{
  while (pos < this->num_chars && this->cells[pos].c == TRANSP_SPACE)
    pos++;
  return pos;
}


static int ccrow_find_end_of_text_part(cc_row_t *this, int pos)
{
  while (pos < this->num_chars && this->cells[pos].c != TRANSP_SPACE)
    pos++;
  return pos;
}


static int ccrow_find_current_attr(cc_row_t *this, int pos)
{
  while (pos > 0 && !this->cells[pos].midrow_attr)
    pos--;
  return pos;
}


static int ccrow_find_next_attr_change(cc_row_t *this, int pos, int lastpos)
{
  pos++;
  while (pos < lastpos && !this->cells[pos].midrow_attr)
    pos++;
  return pos;
}


static void ccrow_set_attributes(cc_renderer_t *renderer, cc_row_t *this,
				 int pos)
{
  const cc_attribute_t *attr = &this->cells[pos].attributes;
  const char *fontname;
  cc_config_t *cap_info = renderer->cc_state->cc_cfg;

  if (attr->italic)
    fontname = cap_info->italic_font;
  else
    fontname = cap_info->font;
  renderer->osd_renderer->set_font(renderer->cap_display, (char *) fontname,
				   cap_info->font_size);
}


static void ccrow_render(cc_renderer_t *renderer, cc_row_t *this, int rownum)
{
  char buf[CC_COLUMNS + 1];
  int base_y;
  int pos = ccrow_find_next_text_part(this, 0);
  cc_config_t *cap_info = renderer->cc_state->cc_cfg;
  osd_renderer_t *osd_renderer = renderer->osd_renderer;

  /* find y coordinate of caption */
  if (cap_info->center) {
    /* find y-center of the desired row; the next line computes */
    /* cap_info->height * (rownum + 0.5) / CC_ROWS */
    /* in integer arithmetic for this purpose. */
    base_y = (renderer->height * rownum * 100 + renderer->height * 50) /
      (CC_ROWS * 100);
  }
  else
    base_y = renderer->height * rownum / CC_ROWS;

  /* break down captions into parts separated by transparent space, and */
  /* center each part individually along the x axis */
  while (pos < this->num_chars) {
    int endpos = ccrow_find_end_of_text_part(this, pos);
    int seg_begin = pos;
    int seg_end;
    int i;
    int text_w = 0, text_h = 0;
    int x, y;
    int seg_w, seg_h;
    int seg_pos[CC_COLUMNS + 1];
    int seg_attr[CC_COLUMNS];
    int cumulative_seg_width[CC_COLUMNS + 1];
    int num_seg = 0;
    int seg;

    /* break down each part into segments bounded by attribute changes and */
    /* find text metrics of the parts */
    seg_pos[0] = seg_begin;
    cumulative_seg_width[0] = 0;
    while (seg_begin < endpos) {
      int attr_pos = ccrow_find_current_attr(this, seg_begin);
      seg_end = ccrow_find_next_attr_change(this, seg_begin, endpos);

      /* compute text size of segment */
      for (i = seg_begin; i < seg_end; i++)
	buf[i - seg_begin] = this->cells[i].c;
      buf[seg_end - seg_begin] = '\0';
      ccrow_set_attributes(renderer, this, attr_pos);
      osd_renderer->get_text_size(renderer->cap_display, buf,
				  &seg_w, &seg_h);

      /* update cumulative segment statistics */
      text_w += seg_w;
      text_h += seg_h;
      seg_pos[num_seg + 1] = seg_end;
      seg_attr[num_seg] = attr_pos;
      cumulative_seg_width[num_seg + 1] = text_w;
      num_seg++;

      seg_begin = seg_end;
    }

    /* compute x coordinate of part */
    if (cap_info->center) {
      int cell_width = renderer->width / CC_COLUMNS;
      x = (renderer->width * (pos + endpos) / 2) / CC_COLUMNS;
      x -= text_w / 2;
      /* clamp x coordinate to nearest character cell */
      x = ((x + cell_width / 2) / CC_COLUMNS) * CC_COLUMNS + cell_width;
      y = base_y - (renderer->max_char_height + 1) / 2;
    }
    else {
      x = renderer->width * pos / CC_COLUMNS;
      y = base_y;
    }

#ifdef LOG_DEBUG
    printf("text_w, text_h = %d, %d\n", text_w, text_h);
    printf("cc from %d to %d; text plotting from %d, %d (basey = %d)\n", pos, endpos, x, y, base_y);
#endif

    /* render text part by rendering each attributed text segment */
    for (seg = 0; seg < num_seg; seg++) {
      int textcol = text_colormap[this->cells[seg_attr[seg]].attributes.foreground];
      int box_x1 = x + cumulative_seg_width[seg];
      int box_x2 = x + cumulative_seg_width[seg + 1];

#ifdef LOG_DEBUG
      printf("ccrow_render: rendering segment %d from %d to %d / %d to %d\n",
	     seg, seg_pos[seg], seg_pos[seg + 1],
	     x + cumulative_seg_width[seg], x + cumulative_seg_width[seg + 1]);
#endif
      /* make caption background a uniform box. Without this line, the */
      /* background is uneven for superscript characters. */
      /* Also pad left & right ends of caption to make it more readable */
/*FIXME: There may be off-by one errors in the rendering - check with Miguel*/
      if (seg == 0)
	box_x1 -= renderer->max_char_width;
      if (seg == num_seg - 1)
	box_x2 += renderer->max_char_width;
      osd_renderer->filled_rect(renderer->cap_display, box_x1, y, box_x2,
				y + renderer->max_char_height,
				textcol + CAP_BG_COL);

      for (i = seg_pos[seg]; i < seg_pos[seg + 1]; i++)
	buf[i - seg_pos[seg]] = this->cells[i].c;
      buf[seg_pos[seg + 1] - seg_pos[seg]] = '\0';
      ccrow_set_attributes(renderer, this, seg_attr[seg]);

      /* text is already mapped from EIA-608 into iso-8859-1 */
      osd_renderer->render_text(renderer->cap_display,
				x + cumulative_seg_width[seg], y, buf,
				textcol);
    }

    pos = ccrow_find_next_text_part(this, endpos);
  }
}


/*----------------- cc_buffer_t methods --------------------------------*/

static int ccbuf_has_displayable(cc_buffer_t *this)
{
  int i;
  for (i = 0; i < CC_ROWS; i++)
    if (this->rows[i].num_chars > 0)
      return 1;

  return 0;
}


static void ccbuf_add_char(cc_buffer_t *this, uint8_t c)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  int pos = rowbuf->pos;
  int left_displayable = (pos > 0) && (pos <= rowbuf->num_chars);

#if LOG_DEBUG > 2
  printf("cc_decoder: ccbuf_add_char: %c @ %d/%d\n", c, this->rowpos, pos);
#endif

  if (pos >= CC_COLUMNS) {
    printf("cc_decoder: ccbuf_add_char: row buffer overflow\n");
    return;
  }

  if (pos > rowbuf->num_chars) {
    /* fill up to indented position with transparent spaces, if necessary */
    ccrow_fill_transp(rowbuf);
  }

  /* midrow PAC attributes are applied only if there is no displayable */
  /* character to the immediate left. This makes the implementation rather */
  /* complicated, but this is what the EIA-608 standard specifies. :-( */
  if (rowbuf->pac_attr_chg && !rowbuf->attr_chg && !left_displayable) {
    rowbuf->attr_chg = 1;
    rowbuf->cells[pos].attributes = rowbuf->pac_attr;
#ifdef LOG_DEBUG
    printf("cc_decoder: ccbuf_add_char: Applying midrow PAC.\n");
#endif
  }

  rowbuf->cells[pos].c = c;
  rowbuf->cells[pos].midrow_attr = rowbuf->attr_chg;
  rowbuf->pos++;

  if (rowbuf->num_chars < rowbuf->pos)
    rowbuf->num_chars = rowbuf->pos;

  rowbuf->attr_chg = 0;
  rowbuf->pac_attr_chg = 0;
}


static void ccbuf_set_cursor(cc_buffer_t *this, int row, int column,
			     int underline, int italics, int color)
{
  cc_row_t *rowbuf = &this->rows[row];
  cc_attribute_t attr;

  attr.italic = italics;
  attr.underline = underline;
  attr.foreground = color;
  attr.background = BLACK;

  rowbuf->pac_attr = attr;
  rowbuf->pac_attr_chg = 1;

  this->rowpos = row;
  rowbuf->pos = column;
  rowbuf->attr_chg = 0;
}


static void ccbuf_apply_attribute(cc_buffer_t *this, cc_attribute_t *attr)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  int pos = rowbuf->pos;

  rowbuf->attr_chg = 1;
  rowbuf->cells[pos].attributes = *attr;
  /* A midrow attribute always counts as a space */
  ccbuf_add_char(this, chartbl[(unsigned int) ' ']);
}


static void ccbuf_tab(cc_buffer_t *this, int tabsize)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  rowbuf->pos += tabsize;
  if (rowbuf->pos > CC_COLUMNS) {
#ifdef LOG_DEBUG
    printf("cc_decoder: ccbuf_tab: row buffer overflow\n");
#endif
    rowbuf->pos = CC_COLUMNS;
    return;
  }
  /* tabs have no effect on pending PAC attribute changes */
}


static void ccbuf_render(cc_renderer_t *renderer, cc_buffer_t *this)
{
  int row;

#ifdef LOG_DEBUG
  printf("cc_decoder: ccbuf_render\n");
#endif

  for (row = 0; row < CC_ROWS; ++row) {
    if (this->rows[row].num_chars > 0)
      ccrow_render(renderer, &this->rows[row], row);
  }
}


/*----------------- cc_memory_t methods --------------------------------*/

static void ccmem_clear(cc_memory_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder.c: ccmem_clear: Clearing CC memory\n");
#endif
  memset(this, 0, sizeof (cc_memory_t));
}


static void ccmem_init(cc_memory_t *this)
{
  ccmem_clear(this);
}


static void ccmem_exit(cc_memory_t *this)
{
/*FIXME: anything to deallocate?*/
}


/*----------------- cc_renderer_t methods -------------------------------*/

static void cc_renderer_build_palette(cc_renderer_t *this)
{
  int i, j;
  const colorinfo_t *cc_text = cc_text_palettes[this->cc_state->cc_cfg->cc_scheme];
  const uint8_t *cc_alpha = cc_alpha_palettes[this->cc_state->cc_cfg->cc_scheme];

  memset(this->cc_palette, 0, sizeof (this->cc_palette));
  memset(this->cc_trans, 0, sizeof (this->cc_trans));
  for (i = 0; i < NUM_FG_COL; i++) {
    /* background color */
    this->cc_palette[i * TEXT_PALETTE_SIZE + 1 + OSD_TEXT1] =
      *(uint32_t *) &cc_text[i].bgcol;
    /* background -> border */
    for (j = 2; j <= 5; j++) {
      clut_t col = interpolate_color(cc_text[i].bgcol,
				     cc_text[i].bordercol, 4, j - 1);
      this->cc_palette[i * TEXT_PALETTE_SIZE + j + OSD_TEXT1] =
	*(uint32_t *) &col;
    }
    /* border color */
    this->cc_palette[i * TEXT_PALETTE_SIZE + 6 + OSD_TEXT1] =
      *(uint32_t *) &cc_text[i].bordercol;
    /* border -> foreground */
    for (j = 7; j <= 9; j++) {
      clut_t col = interpolate_color(cc_text[i].bordercol,
				     cc_text[i].textcol, 3, j - 6);
      this->cc_palette[i * TEXT_PALETTE_SIZE + j + OSD_TEXT1] =
	*(uint32_t *) &col;
    }
    /* foreground color */
    this->cc_palette[i * TEXT_PALETTE_SIZE + 10 + OSD_TEXT1] =
      *(uint32_t *) &cc_text[i].textcol;

    /* alpha values */
    for (j = 0; j <= 10; j++)
      this->cc_trans[i * TEXT_PALETTE_SIZE + j + OSD_TEXT1] = cc_alpha[j];
  }
}


static int64_t cc_renderer_calc_vpts(cc_renderer_t *this, int64_t pts,
				      uint32_t ntsc_frame_offset)
{
  metronom_t *metronom = this->metronom;
  int64_t vpts = metronom->got_spu_packet(metronom, pts);
  return vpts + ntsc_frame_offset * NTSC_FRAME_DURATION;
}


/* returns true if a caption is on display */
static int cc_renderer_on_display(cc_renderer_t *this)
{
  return this->displayed;
}


static void cc_renderer_hide_caption(cc_renderer_t *this, int64_t vpts)
{
  if ( ! this->displayed ) return;

  this->osd_renderer->hide(this->cap_display, vpts);
  this->displayed = 0;
  this->last_hide_vpts = vpts;
}


static void cc_renderer_show_caption(cc_renderer_t *this, cc_buffer_t *buf,
				     int64_t vpts)
{
#ifdef LOG_DEBUG
  printf("spucc: cc_renderer: show\n");
#endif

  if (this->displayed) {
    cc_renderer_hide_caption(this, vpts);
    printf("spucc: cc_renderer: show: OOPS - caption was already displayed!\n");
  }

  this->osd_renderer->clear(this->cap_display);
  ccbuf_render(this, buf);
  this->osd_renderer->set_position(this->cap_display,
				   this->x,
				   this->y);
  vpts = MAX(vpts, this->last_hide_vpts);
  this->osd_renderer->show(this->cap_display, vpts);

  this->displayed = 1;
  this->display_vpts = vpts;
}


static void cc_renderer_free_osd_object(cc_renderer_t *this)
{
  /* hide and free old displayed caption object if necessary */
  if ( ! this->cap_display ) return;

  cc_renderer_hide_caption(this, this->display_vpts);
  this->osd_renderer->free_object(this->cap_display);
  this->cap_display = NULL;
}


static void cc_renderer_adjust_osd_object(cc_renderer_t *this)
{
  cc_renderer_free_osd_object(this);

#ifdef LOG_DEBUG
  printf("spucc: cc_renderer: adjust_osd_object: creating %dx%d OSD object\n",
	 this->width, this->height);
#endif

  /* create display object */
  this->cap_display = this->osd_renderer->new_object(this->osd_renderer,
						     this->width,
						     this->height);
  this->osd_renderer->set_palette(this->cap_display, this->cc_palette,
				  this->cc_trans);
  this->osd_renderer->set_encoding(this->cap_display, "iso-8859-1");
}


cc_renderer_t *cc_renderer_open(osd_renderer_t *osd_renderer,
				metronom_t *metronom, cc_state_t *cc_state,
				int video_width, int video_height)
{
  cc_renderer_t *this = calloc(1, sizeof (cc_renderer_t));

  this->osd_renderer = osd_renderer;
  this->metronom = metronom;
  this->cc_state = cc_state;
  cc_renderer_update_cfg(this, video_width, video_height);
#ifdef LOG_DEBUG
  printf("spucc: cc_renderer: open\n");
#endif
  return this;
}


void cc_renderer_close(cc_renderer_t *this_obj)
{
  cc_renderer_free_osd_object(this_obj);
  free(this_obj);

#ifdef LOG_DEBUG
  printf("spucc: cc_renderer: close\n");
#endif
}


void cc_renderer_update_cfg(cc_renderer_t *this_obj, int video_width,
			    int video_height)
{
  int fontw, fonth;
  int required_w, required_h;

  this_obj->video_width = video_width;
  this_obj->video_height = video_height;

  /* fill in text palette */
  cc_renderer_build_palette(this_obj);

  /* calculate preferred captioning area, as per the EIA-608 standard */
  this_obj->x =  this_obj->video_width * 10 / 100;
  this_obj->y = this_obj->video_height * 10 / 100;
  this_obj->width = this_obj->video_width * 80 / 100;
  this_obj->height = this_obj->video_height * 80 / 100;

  /* find maximum text width and height for normal & italic captioning */
  /* font */
  get_font_metrics(this_obj->osd_renderer, this_obj->cc_state->cc_cfg->font,
		   this_obj->cc_state->cc_cfg->font_size, &fontw, &fonth);
  this_obj->max_char_width = fontw;
  this_obj->max_char_height = fonth;
  get_font_metrics(this_obj->osd_renderer, this_obj->cc_state->cc_cfg->italic_font,
		   this_obj->cc_state->cc_cfg->font_size, &fontw, &fonth);
  this_obj->max_char_width = MAX(fontw, this_obj->max_char_width);
  this_obj->max_char_height = MAX(fonth, this_obj->max_char_height);
#ifdef LOG_DEBUG
  printf("spucc: cc_renderer: update config: max text extents: %d, %d\n",
	 this_obj->max_char_width, this_obj->max_char_height);
#endif

  /* need to adjust captioning area to accommodate font? */
  required_w = CC_COLUMNS * (this_obj->max_char_width + 1);
  required_h = CC_ROWS * (this_obj->max_char_height + 1);
  if (required_w > this_obj->width) {
#ifdef LOG_DEBUG
    printf("spucc: cc_renderer: update config: adjusting cap area width: %d\n",
	   required_w);
#endif
    this_obj->width = required_w;
    this_obj->x = (this_obj->video_width - required_w) / 2;
  }
  if (required_h > this_obj->height) {
#ifdef LOG_DEBUG
    printf("spucc: cc_renderer: update config: adjusting cap area height: %d\n",
	   required_h);
#endif
    this_obj->height = required_h;
    this_obj->y = (this_obj->video_height - required_h) / 2;
  }

  if (required_w <= this_obj->video_width &&
      required_h <= this_obj->video_height) {
    this_obj->cc_state->can_cc = 1;
    cc_renderer_adjust_osd_object(this_obj);
  }
  else {
    this_obj->cc_state->can_cc = 0;
    cc_renderer_free_osd_object(this_obj);
    printf("spucc: required captioning area %dx%d exceeds screen %dx%d!\n"
	   "       Captions disabled. Perhaps you should choose a smaller"
	   " font?\n",
	   required_w, required_h, this_obj->video_width,
	   this_obj->video_height);
  }
}


/*----------------- cc_decoder_t methods --------------------------------*/

static void cc_set_channel(cc_decoder_t *this, int channel)
{
  (*this->active)->channel_no = channel;
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_set_channel: selecting channel %d\n", channel);
#endif
}


static cc_buffer_t *active_ccbuffer(cc_decoder_t *this)
{
  cc_memory_t *mem = *this->active;
  return &mem->channel[mem->channel_no];
}


static int cc_onscreen_displayable(cc_decoder_t *this)
{
  return ccbuf_has_displayable(&this->on_buf->channel[this->on_buf->channel_no]);
}


static void cc_hide_displayed(cc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_hide_displayed\n");
#endif

  if (cc_renderer_on_display(this->cc_state->renderer)) {
    int64_t vpts = cc_renderer_calc_vpts(this->cc_state->renderer, this->pts,
					  this->f_offset);
#ifdef LOG_DEBUG
    printf("cc_decoder: cc_hide_displayed: hiding caption %u at vpts %u\n", this->capid, vpts);
#endif
    cc_renderer_hide_caption(this->cc_state->renderer, vpts);
  }
}


static void cc_show_displayed(cc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_show_displayed\n");
#endif

  if (cc_onscreen_displayable(this)) {
    int64_t vpts = cc_renderer_calc_vpts(this->cc_state->renderer, this->pts,
					  this->f_offset);
#ifdef LOG_DEBUG
    printf("cc_decoder: cc_show_displayed: showing caption %u at vpts %u\n", this->capid, vpts);
#endif
    this->capid++;
    cc_renderer_show_caption(this->cc_state->renderer,
			     &this->on_buf->channel[this->on_buf->channel_no],
			     vpts);
  }
}


static void cc_swap_buffers(cc_decoder_t *this)
{
  cc_memory_t *temp;

  /* hide caption in displayed memory */
  cc_hide_displayed(this);

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_swap_buffers: swapping caption memory\n");
#endif
  temp = this->on_buf;
  this->on_buf = this->off_buf;
  this->off_buf = temp;

  /* show new displayed memory */
  cc_show_displayed(this);
}

static void cc_decode_standard_char(cc_decoder_t *this, uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf = active_ccbuffer(this);
  /* c1 always is a valid character */
  ccbuf_add_char(buf, chartbl[c1]);
  /* c2 might not be a printable character, even if c1 was */
  if (c2 & 0x60)
    ccbuf_add_char(buf, chartbl[c2]);
}


static void cc_decode_PAC(cc_decoder_t *this, int channel,
			  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;
  int row, column = 0;
  int underline, italics = 0, color;

  /* There is one invalid PAC code combination. Ignore it. */
  if (c1 == 0x10 && c2 > 0x5f)
    return;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);

  row = rowdata[((c1 & 0x07) << 1) | ((c2 & 0x20) >> 5)];
  if (c2 & 0x10) {
    column = ((c2 & 0x0e) >> 1) * 4;   /* preamble indentation */
    color = WHITE;                     /* indented lines have white color */
  }
  else if ((c2 & 0x0e) == 0x0e) {
    italics = 1;                       /* italics, they are always white */
    color = WHITE;
  }
  else
    color = (c2 & 0x0e) >> 1;
  underline = c2 & 0x01;

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_PAC: row %d, col %d, ul %d, it %d, clr %d\n",
	 row, column, underline, italics, color);
#endif

  ccbuf_set_cursor(buf, row, column, underline, italics, color);
}


static void cc_decode_ext_attribute(cc_decoder_t *this, int channel,
				    uint8_t c1, uint8_t c2)
{
  cc_set_channel(this, channel);
}


static void cc_decode_special_char(cc_decoder_t *this, int channel,
				   uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_special_char: Mapping %x to %x\n", c2, specialchar[c2 & 0xf]);
#endif
  ccbuf_add_char(buf, specialchar[c2 & 0xf]);
}


static void cc_decode_midrow_attr(cc_decoder_t *this, int channel,
				  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;
  cc_attribute_t attr;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
  if (c2 < 0x2e) {
    attr.italic = 0;
    attr.foreground = (c2 & 0xe) >> 1;
  }
  else {
    attr.italic = 1;
    attr.foreground = WHITE;
  }
  attr.underline = c2 & 0x1;
  attr.background = BLACK;
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_midrow_attr: attribute %x\n", c2);
  printf("cc_decoder: cc_decode_midrow_attr: ul %d, it %d, clr %d\n",
	 attr.underline, attr.italic, attr.foreground);
#endif

  ccbuf_apply_attribute(buf, &attr);
}


static void cc_decode_misc_control_code(cc_decoder_t *this, int channel,
					uint8_t c1, uint8_t c2)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: decode_misc: decoding %x %x\n", c1, c2);
#endif

  cc_set_channel(this, channel);

  switch (c2) {          /* 0x20 <= c2 <= 0x2f */

  case 0x20:             /* RCL */
    break;

  case 0x21:             /* backspace */
#ifdef LOG_DEBUG
    printf("cc_decoder: backspace\n");
#endif
    break;

  case 0x24:             /* DER */
    break;

  case 0x25:             /* RU2 */
    break;

  case 0x26:             /* RU3 */
    break;

  case 0x27:             /* RU4 */
    break;

  case 0x28:             /* FON */
    break;

  case 0x29:             /* RDC */
    break;

  case 0x2a:             /* TR */
    break;

  case 0x2b:             /* RTD */
    break;

  case 0x2c:             /* EDM - erase displayed memory */
    cc_hide_displayed(this);
    ccmem_clear(this->on_buf);
    break;

  case 0x2d:             /* carriage return */
    break;

  case 0x2e:             /* ENM - erase non-displayed memory */
    ccmem_clear(this->off_buf);
    break;

  case 0x2f:             /* EOC - swap displayed and non displayed memory */
    cc_swap_buffers(this);
    break;
  }
}


static void cc_decode_tab(cc_decoder_t *this, int channel,
			  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
  ccbuf_tab(buf, c2 & 0x3);
}


static void cc_decode_EIA608(cc_decoder_t *this, uint16_t data)
{
  uint8_t c1 = data & 0x7f;
  uint8_t c2 = (data >> 8) & 0x7f;

#if LOG_DEBUG >= 3
  printf("decoding %x %x\n", c1, c2);
#endif

  if (c1 & 0x60) {             /* normal character, 0x20 <= c1 <= 0x7f */
    cc_decode_standard_char(this, c1, c2);
  }
  else if (c1 & 0x10) {        /* control code or special character */
                               /* 0x10 <= c1 <= 0x1f */
    int channel = (c1 & 0x08) >> 3;
    c1 &= ~0x08;

    /* control sequences are often repeated. In this case, we should */
    /* evaluate it only once. */
    if (data != this->lastcode) {

      if (c2 & 0x40) {         /* preamble address code: 0x40 <= c2 <= 0x7f */
	cc_decode_PAC(this, channel, c1, c2);
      }
      else {
	switch (c1) {

	case 0x10:             /* extended background attribute code */
	  cc_decode_ext_attribute(this, channel, c1, c2);
	  break;

	case 0x11:             /* attribute or special character */
	  if ((c2 & 0x30) == 0x30) { /* special char: 0x30 <= c2 <= 0x3f  */
	    cc_decode_special_char(this, channel, c1, c2);
	  }
	  else if (c2 & 0x20) {     /* midrow attribute: 0x20 <= c2 <= 0x2f */
	    cc_decode_midrow_attr(this, channel, c1, c2);
	  }
	  break;

	case 0x14:             /* possibly miscellaneous control code */
	  cc_decode_misc_control_code(this, channel, c1, c2);
	  break;

	case 0x17:            /* possibly misc. control code TAB offset */
	                      /* 0x21 <= c2 <= 0x23 */
	  if (c2 >= 0x21 && c2 <= 0x23) {
	    cc_decode_tab(this, channel, c1, c2);
	  }
	  break;
	}
      }
    }
  }

  this->lastcode = data;
}


void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       int64_t pts)
{
  /* The first number may denote a channel number. I don't have the
   * EIA-708 standard, so it is hard to say.
   * From what I could figure out so far, the general format seems to be:
   *
   * repeat
   *
   *   0xfe starts 2 byte sequence of unknown purpose. It might denote
   *        field #2 in line 21 of the VBI. We'll ignore it for the
   *        time being.
   *
   *   0xff starts 2 byte EIA-608 sequence, field #1 in line 21 of the VBI.
   *        Followed by a 3-code triplet that starts either with 0xff or
   *        0xfe. In either case, the following triplet needs to be ignored
   *        for line 21, field 1.
   *
   *   0x00 is padding, followed by 2 more 0x00.
   *
   *   0x01 always seems to appear at the beginning, always seems to
   *        be followed by 0xf8, 8-bit number.
   *        The lower 7 bits of this 8-bit number seem to denote the
   *        number of code triplets that follow.
   *        The most significant bit denotes whether the Line 21 field 1
   *        captioning information is at odd or even triplet offsets from this
   *        beginning triplet. 1 denotes odd offsets, 0 denotes even offsets.
   *
   *        Most captions are encoded with odd offsets, so this is what we
   *        will assume.
   *
   * until end of packet
   */
  uint8_t *current = buffer;
  uint32_t curbytes = 0;
  uint8_t data1, data2;
  uint8_t cc_code;
  int odd_offset = 1;

  this->f_offset = 0;
  this->pts = pts;

#if LOG_DEBUG >= 2
  printf("libspucc: decode_cc: got pts %u\n", pts);
  {
    uint8_t *cur_d = buffer;
    printf("libspucc: decode_cc: codes: ");
    while (cur_d < buffer + buf_len) {
      printf("0x%0x ", *cur_d++);
    }
    printf("\n");
  }
#endif

  while (curbytes < buf_len) {
    int skip = 2;

    cc_code = *current++;
    curbytes++;

    if (buf_len - curbytes < 2) {
#ifdef LOG_DEBUG
      fprintf(stderr, "Not enough data for 2-byte CC encoding\n");
#endif
      break;
    }

    data1 = *current;
    data2 = *(current + 1);

    switch (cc_code) {
    case 0xfe:
      /* expect 2 byte encoding (perhaps CC3, CC4?) */
      /* ignore for time being */
      skip = 2;
      break;

    case 0xff:
      /* expect EIA-608 CC1/CC2 encoding */
      if (good_parity(data1 | (data2 << 8))) {
	cc_decode_EIA608(this, data1 | (data2 << 8));
	this->f_offset++;
      }
      skip = 5;
      break;

    case 0x00:
      /* This seems to be just padding */
      skip = 2;
      break;

    case 0x01:
      odd_offset = data2 & 0x80;
      if (odd_offset)
	skip = 2;
      else
	skip = 5;
      break;

    default:
#ifdef LOG_DEBUG
      fprintf(stderr, "Unknown CC encoding: %x\n", cc_code);
#endif
      skip = 2;
      break;
    }
    current += skip;
    curbytes += skip;
  }
}



cc_decoder_t *cc_decoder_open(cc_state_t *cc_state)
{
  cc_decoder_t *this = calloc(1, sizeof (cc_decoder_t));
  /* configfile stuff */
  this->cc_state = cc_state;

  ccmem_init(&this->buffer[0]);
  ccmem_init(&this->buffer[1]);
  this->on_buf = &this->buffer[0];
  this->off_buf = &this->buffer[1];
  this->active = &this->off_buf;

  this->lastcode = 0;
  this->capid = 0;

  this->pts = this->f_offset = 0;

#ifdef LOG_DEBUG
  printf("spucc: cc_decoder_open\n");
#endif
  return this;
}


void cc_decoder_close(cc_decoder_t *this)
{
  ccmem_exit(&this->buffer[0]);
  ccmem_exit(&this->buffer[1]);

  free(this);

#ifdef LOG_DEBUG
  printf("spucc: cc_decoder_close\n");
#endif
}
