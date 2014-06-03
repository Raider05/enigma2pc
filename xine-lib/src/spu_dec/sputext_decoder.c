/*
 * Copyright (C) 2000-2014 the xine project
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#define LOG_MODULE "libsputext"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/buffer.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/osd.h>

#define SUB_MAX_TEXT  5      /* lines */
#define SUB_BUFSIZE   256    /* chars per line */

/* alignment in SSA codes */
#define ALIGN_LEFT    1
#define ALIGN_CENTER  2
#define ALIGN_RIGHT   3
#define ALIGN_BOTTOM  0
#define ALIGN_TOP     4
#define ALIGN_MIDDLE  8
#define GET_X_ALIGNMENT(a) ((a) & 3)
#define GET_Y_ALIGNMENT(a) ((a) - ((a) & 3))

/* subtitles projection */
/* for subrip file with SSA tags, those values are always correct.*/
/* But for SSA files, those values are the default ones. we have */
/* to use PlayResX and PlayResY defined in [Script Info] section. */
/* not implemented yet... */
#define SPU_PROJECTION_X  384
#define SPU_PROJECTION_Y  288



#define rgb2yuv(R,G,B) ((((((66*R+129*G+25*B+128)>>8)+16)<<8)|(((112*R-94*G-18*B+128)>>8)+128))<<8|(((-38*R-74*G+112*B+128)>>8)+128))

static const uint32_t sub_palette[22]={
/* RED */
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(50,10,10),
  rgb2yuv(120,20,20),
  rgb2yuv(185,50,50),
  rgb2yuv(255,70,70),
/* BLUE */
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,0,0),
  rgb2yuv(0,30,50),
  rgb2yuv(0,90,120),
  rgb2yuv(0,140,185),
  rgb2yuv(0,170,255)
};

static const uint8_t sub_trans[22]={
  0, 0, 3, 6, 8, 10, 12, 14, 15, 15, 15,
  0, 0, 3, 6, 8, 10, 12, 14, 15, 15, 15
};

typedef enum {
  SUBTITLE_SIZE_TINY = 0,
  SUBTITLE_SIZE_SMALL,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,
  SUBTITLE_SIZE_VERY_LARGE,
  SUBTITLE_SIZE_HUGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;

#define FONTNAME_SIZE 100

typedef struct sputext_class_s {
  spu_decoder_class_t class;

  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FONTNAME_SIZE]; /* subtitle font */
#ifdef HAVE_FT2
  char               font_ft[FILENAME_MAX]; /* subtitle font */
  int                use_font_ft;     /* use Freetype */
#endif
  const char        *src_encoding;    /* encoding of subtitle file */
  int                use_unscaled;    /* use unscaled OSD if possible */

  xine_t            *xine;

} sputext_class_t;


/* Convert subtiles coordinates in window coordinates. */
/* (a, b) --> (x + a * dx, y + b * dy) */
typedef struct video2wnd_s {
  int x;
  int y;
  double dx;
  double dy;
} video2wnd_t;

typedef struct sputext_decoder_s {
  spu_decoder_t      spu_decoder;

  sputext_class_t   *class;
  xine_stream_t     *stream;

  int                ogm;
  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  /* below 3 variables are the same from class. use to detect
   * when something changes.
   */
  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FILENAME_MAX]; /* subtitle font */
  const char         *buf_encoding;    /* encoding of subtitle buffer */

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;
  int                started;
  int                finished;

  osd_renderer_t    *renderer;
  osd_object_t      *osd;
  int               current_osd_text;
  uint32_t          spu_palette[OVL_PALETTE_SIZE];
  uint8_t           spu_trans[OVL_PALETTE_SIZE];

  int64_t            img_duration;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
  int                unscaled;          /* use unscaled OSD */

  int                last_y;            /* location of the previous subtitle */
  int                last_lines;        /* number of lines of the previous subtitle */
  video2wnd_t        video2wnd;
} sputext_decoder_t;

static inline char *get_font (sputext_class_t *class)
{
#ifdef HAVE_FT2
  return class->use_font_ft ? class->font_ft : class->font;
#else
  return class->font;
#endif
}

static void update_font_size (sputext_decoder_t *this, int force_update) {
  static const int sizes[SUBTITLE_SIZE_NUM] = { 16, 20, 24, 32, 48, 64 };

  if ((this->subtitle_size != this->class->subtitle_size) ||
      (this->vertical_offset != this->class->vertical_offset) ||
      force_update) {

    this->subtitle_size = this->class->subtitle_size;
    this->vertical_offset = this->class->vertical_offset;
    this->last_lines = 0;

    this->font_size = sizes[this->class->subtitle_size];

    this->line_height = this->font_size + 10;

    /* Create a full-window OSD */
    if( this->osd )
      this->renderer->free_object (this->osd);

    this->osd = this->renderer->new_object (this->renderer,
                                            this->width,
                                            this->height);

    this->renderer->set_font (this->osd, get_font (this->class), this->font_size);

    this->renderer->set_position (this->osd, 0, 0);
  }
}

static void update_output_size (sputext_decoder_t *this) {
  const int unscaled = this->class->use_unscaled &&
    (this->stream->video_out->get_capabilities(this->stream->video_out) &
     VO_CAP_UNSCALED_OVERLAY);

  if( unscaled != this->unscaled ) {
    this->unscaled = unscaled;
    this->width = 0; /* force update */
  }

  /* initialize decoder if needed */
  if( this->unscaled ) {
    if( this->width != this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_WIDTH) ||
        this->height != this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_HEIGHT) ||
        !this->img_duration || !this->osd ) {

      int width = 0, height = 0;

      this->stream->video_out->status(this->stream->video_out, NULL,
                                      &width, &height, &this->img_duration );
      if( width && height ) {

        this->width = this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_WIDTH);
        this->height = this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_HEIGHT);

        if(!this->osd || (this->width && this->height)) {

          /* in unscaled mode, we have to convert subtitle position in window coordinates. */
          /* we have a scale factor because video may be zommed */
          /* and a displacement factor because video may have blacks lines. */
          int output_width, output_height, output_xoffset, output_yoffset;

          output_width = this->stream->video_out->get_property(this->stream->video_out,
                                                               VO_PROP_OUTPUT_WIDTH);
          output_height = this->stream->video_out->get_property(this->stream->video_out,
                                                                VO_PROP_OUTPUT_HEIGHT);
          output_xoffset = this->stream->video_out->get_property(this->stream->video_out,
                                                                 VO_PROP_OUTPUT_XOFFSET);
          output_yoffset = this->stream->video_out->get_property(this->stream->video_out,
                                                                 VO_PROP_OUTPUT_YOFFSET);

          /* driver don't seen to be capable to give us those values */
          /* fallback to a default full-window values */
          if (output_width <= 0 || output_height <= 0) {
            output_width = this->width;
            output_height = this->height;
            output_xoffset = 0;
            output_yoffset = 0;
          }

          this->video2wnd.x = output_xoffset;
          this->video2wnd.y = output_yoffset;
          this->video2wnd.dx = (double)output_width / SPU_PROJECTION_X;
          this->video2wnd.dy = (double)output_height / SPU_PROJECTION_Y;

          this->renderer = this->stream->osd_renderer;
          update_font_size (this, 1);
        }
      }
    }
  } else {
    if( !this->width || !this->height || !this->img_duration || !this->osd ) {

      this->width = 0;
      this->height = 0;

      this->stream->video_out->status(this->stream->video_out, NULL,
                                      &this->width, &this->height, &this->img_duration );

      if(!this->osd || ( this->width && this->height)) {
        this->renderer = this->stream->osd_renderer;

        /* in scaled mode, we have to convert subtitle position in film coordinates. */
        this->video2wnd.x = 0;
        this->video2wnd.y = 0;
        this->video2wnd.dx = (double)this->width / SPU_PROJECTION_X;
        this->video2wnd.dy = (double)this->height / SPU_PROJECTION_Y;

        update_font_size (this, 1);
      }
    }
  }
}

static int parse_utf8_size(const void *buf)
{
  const uint8_t *c = buf;
  if ( c[0]<0x80 )
      return 1;

  if( c[1]==0 )
    return 1;
  if ( (c[0]>=0xC2 && c[0]<=0xDF) && (c[1]>=0x80 && c[1]<=0xBF) )
    return 2;

  if( c[2]==0 )
    return 2;
  else if ( c[0]==0xE0 && (c[1]>=0xA0 && c[1]<=0xBF) && (c[2]>=0x80 && c[1]<=0xBF) )
    return 3;
  else if ( (c[0]>=0xE1 && c[0]<=0xEC) && (c[1]>=0x80 && c[1]<=0xBF) && (c[2]>=0x80 && c[1]<=0xBF) )
    return 3;
  else if ( c[0]==0xED && (c[1]>=0x80 && c[1]<=0x9F) && (c[2]>=0x80 && c[1]<=0xBF) )
    return 3;
  else if ( c[0]==0xEF && (c[1]>=0xA4 && c[1]<=0xBF) && (c[2]>=0x80 && c[1]<=0xBF) )
    return 3;
  else
    return 1;
}

static int ogm_render_line_internal(sputext_decoder_t *this, int x, int y, const char *text, int render)
{
  const size_t length = strlen (text);
  size_t i = 0;

  while (i <= length) {

    if (text[i] == '<') {
      if (!strncasecmp("<b>", text+i, 3)) {
	/* enable Bold color */
	if (render)
	  this->current_osd_text = OSD_TEXT2;
	i=i+3;
	continue;
      } else if (!strncasecmp("</b>", text+i, 4)) {
	/* disable BOLD */
	if (render)
	  this->current_osd_text = OSD_TEXT1;
	i=i+4;
	continue;
      } else if (!strncasecmp("<i>", text+i, 3)) {
	/* enable italics color */
	if (render)
	  this->current_osd_text = OSD_TEXT3;
	i=i+3;
	continue;
      } else if (!strncasecmp("</i>", text+i, 4)) {
	/* disable italics */
	if (render)
	  this->current_osd_text = OSD_TEXT1;
	i=i+4;
	continue;
      } else if (!strncasecmp("<font>", text+i, 6)) {
	/*Do somethink to disable typing
	  fixme - no teststreams*/
	i=i+6;
	continue;
      } else if (!strncasecmp("<font ", text+i, 6)) {
        /* TODO: parse and use attributes. seen: <font color="#xxyyzz"> */
        const char *end = strchr(text + i, '>');
        if (end) {
          i += (end - (text + i)) + 1;
          continue;
        }
      } else if (!strncasecmp("</font>", text+i, 7)) {
	/*Do somethink to enable typing
	  fixme - no teststreams*/
	i=i+7;
	continue;
      }
    }
    if (text[i] == '{') {

      if (!strncmp("{\\", text+i, 2)) {
	int value;

        if (sscanf(text+i, "{\\b%d}", &value) == 1) {
          if (render) {
            if (value)
              this->current_osd_text = OSD_TEXT2;
            else
              this->current_osd_text = OSD_TEXT1;
          }
        } else if (sscanf(text+i, "{\\i%d}", &value) == 1) {
          if (render) {
            if (value)
              this->current_osd_text = OSD_TEXT3;
            else
              this->current_osd_text = OSD_TEXT1;
          }
        }
        char *const end = strstr(text+i+2, "}");
        if (end) {
          i=end-text+1;
          continue;
        }
      }
    }

    char letter[5];
    const char *const encoding = this->buf_encoding ? : this->class->src_encoding;
    const int isutf8 = !strcmp(encoding, "utf-8");
    const size_t shift = isutf8 ? parse_utf8_size (&text[i]) : 1;
    memcpy(letter,&text[i],shift);
    letter[shift]=0;

    if (render)
      this->renderer->render_text(this->osd, x, y, letter, this->current_osd_text);

    int w, dummy;
    this->renderer->get_text_size(this->osd, letter, &w, &dummy);
    x += w;
    i += shift;
  }

  return x;
}

static inline int ogm_get_width(sputext_decoder_t *this, char* text) {
  return ogm_render_line_internal (this, 0, 0, text, 0);
}

static inline void ogm_render_line(sputext_decoder_t *this, int x, int y, char* text) {
  ogm_render_line_internal (this, x, y, text, 1);
}

/* read SSA tags at begening of text. Suported tags are :          */
/* \a   : alignment in SSA code (see #defines)                     */
/* \an  : alignment in 'numpad code'                               */
/* \pos : absolute position of subtitles. Alignment define origin. */
static void read_ssa_tag(sputext_decoder_t *this, const char* text,
                         int* alignment, int* sub_x, int* sub_y, int* max_width) {

  int in_tag = 0;

  (*alignment) = 2;
  (*sub_x) = -1;
  (*sub_y) = -1;

  while (*text) {

    /* wait for tag begin, allow space and tab */
    if (in_tag == 0) {
      if (*text == '{') in_tag = 1;
      else if ((*text != ' ') && (*text != '\t')) break;

    /* parse SSA command */
    } else {
      if (*text == '\\') {
        if (sscanf(text, "\\pos(%d,%d)", sub_x, sub_y) == 2) {
          text += 8; /* just for speed up, 8 is the minimal with */
        }

        if (sscanf(text, "\\a%d", alignment) == 1) {
           text += 2;
        }

        if (sscanf(text, "\\an%d", alignment) == 1) {
          text += 3;
          if ((*alignment) > 6) (*alignment) = (*alignment) - 2;
          else if ((*alignment) > 3) (*alignment) = (*alignment) + 5;
        }
      }

      if (*text == '}') in_tag = 0;
    }

    text++;
  }


  /* check alignment validity */
  if ((*alignment) < 1 || (*alignment) > 11) {
    (*alignment) = 2;
  }

  /* convert to window coordinates */
  if ((*sub_x) >= 0 && (*sub_y) >= 0) {
    (*sub_x) = this->video2wnd.x + this->video2wnd.dx * (*sub_x);
    (*sub_y) = this->video2wnd.y + this->video2wnd.dy * (*sub_y);
  }

  /* check validity, compute max width */
  if ( (*sub_x) < 0 || (*sub_x) >= this->width ||
       (*sub_y) < 0 || (*sub_y) >= this->height  ) {
    (*sub_x) = -1;
    (*sub_y) = -1;
    (*max_width) = this->width;
  } else {
    switch (GET_X_ALIGNMENT(*alignment)) {
    case ALIGN_LEFT:
      (*max_width) = this->width - (*sub_x);
      break;
    case ALIGN_CENTER:
      (*max_width) = this->width;
      break;
    case ALIGN_RIGHT:
      (*max_width) = (*sub_x);
      break;
    }
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
          "libsputext: position : (%d, %d), max width : %d, alignment : %d\n",
          (*sub_x), (*sub_y), (*max_width), (*alignment));
}

static int is_cjk_encoding(const char *enc) {
  /* CJK charset strings defined in iconvdata/gconv-modules of glibc */
  static const char cjk_encoding_strings[][16] = {
    "SJIS",
    "CP932",
    "EUC-KR",
    "UHC",
    "JOHAB",
    "BIG5",
    "BIG5HKSCS",
    "EUC-JP-MS",
    "EUC-JP",
    "EUC-CN",
    "GBBIG5",
    "GBK",
    "GBGBK",
    "EUC-TW",
    "ISO-2022-JP",
    "ISO-2022-JP-2",
    "ISO-2022-JP-3",
    "ISO-2022-KR",
    "ISO-2022-CN",
    "ISO-2022-CN-EXT",
    "GB18030",
    "EUC-JISX0213",
    "SHIFT_JISX0213",
  };

  int pstr;

  /* return 1 if encoding string is one of the CJK(Chinese,Jananese,Korean)
   * character set strings. */
  for (pstr = 0; pstr < sizeof (cjk_encoding_strings) / sizeof (cjk_encoding_strings[0]); pstr++)
    if (strcasecmp (enc, cjk_encoding_strings[pstr]) == 0)
      return 1;

  return 0;
}

static void draw_subtitle(sputext_decoder_t *this, int64_t sub_start, int64_t sub_end ) {

  int y;
  int sub_x, sub_y, max_width = this->width;
  int alignment;

  _x_assert(this->renderer != NULL);
  if ( ! this->renderer )
    return;

  read_ssa_tag(this, this->text[0], &alignment, &sub_x, &sub_y, &max_width);

  update_font_size(this, 0);

  const char *const font = get_font (this->class);
  if( strcmp(this->font, font) ) {
    strncpy(this->font, font, FILENAME_MAX);
    this->font[FILENAME_MAX - 1] = '\0';
    this->renderer->set_font (this->osd, font, this->font_size);
  }

  int font_size = this->font_size;

  const char *const encoding = this->buf_encoding ? : this->class->src_encoding;
  this->renderer->set_encoding(this->osd, encoding);

  int rebuild_all = 0;
  int line;
  for (line = 0; line < this->lines; line++) {
    int line_width = ogm_get_width(this, this->text[line]);

    /* line too long */
    if (line_width > max_width) {
      char *current_cut, *best_cut;
      int a;

      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "libsputext: Line too long: %d > %d, split at max size.\n",
              line_width, max_width);

      /* can't fit with keeping existing lines */
      if (this->lines + 1 > SUB_MAX_TEXT) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "libsputext: Can't fit with keeping existing line, we have to rebuild all the subtitle\n");
        rebuild_all = 1;
        break;
      }

      /* find the longest sequence witch fit */
      line_width = 0;
      current_cut = this->text[line];
      best_cut = NULL;
      while (line_width < max_width) {
        while (*current_cut && *current_cut != ' ') current_cut++;
        if (*current_cut == ' ') {
          *current_cut = 0;
          line_width = ogm_get_width(this, this->text[line]);
          *current_cut = ' ';
          if (line_width < max_width) best_cut = current_cut;
          current_cut++;
        } else {
          break; /* end of line */
        }
      }

      if (best_cut == NULL) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                 "libsputext: Can't wrap line: a word is too long, abort.\n");
        break;
      }

      /* move other lines */
      for (a = this->lines - 1; a > line; a--)
        memcpy(this->text[a + 1], this->text[a], SUB_BUFSIZE);

      /* split current one */
      strncpy(this->text[line + 1], best_cut + 1, SUB_BUFSIZE);
      *best_cut = 0;

      this->lines = this->lines + 1;
    }
  }

  /* regenerate all the lines to find something that better fits */
  if (rebuild_all) {
    char buf[SUB_BUFSIZE * SUB_MAX_TEXT] = { 0, };

    int line;
    for(line = 0; line < this->lines; line++) {
      const size_t len = strlen(buf);
      if (len)
        buf[len] = ' ';

      strncat(buf, this->text[line], SUB_BUFSIZE-len-1);
    }

    char *stream = buf;
    this->lines = 0;

    char *current_cut, *best_cut;
    do {

      if (this->lines + 1 < SUB_MAX_TEXT) {

        /* find the longest sequence witch fit */
        int line_width = 0;
        current_cut = stream;
        best_cut = NULL;
        while (line_width < max_width) {
          while (*current_cut && *current_cut != ' ') current_cut++;
          if (*current_cut == ' ') {
            *current_cut = 0;
            line_width = ogm_get_width(this, stream);
            *current_cut = ' ';
            if (line_width < max_width) best_cut = current_cut;
            current_cut++;
          } else {
            line_width = ogm_get_width(this, stream);
            if (line_width < max_width) best_cut = current_cut;
            break; /* end of line */
          }
        }
      }

      /* line maybe too long, but we have reached last subtitle line */
      else {
        best_cut = current_cut = stream + strlen(stream);
      }

      /* copy current line */
      if (best_cut != NULL) *best_cut = 0;
      strncpy(this->text[this->lines], stream, SUB_BUFSIZE);
      this->lines = this->lines + 1;

      stream = best_cut + 1;

    } while (best_cut != current_cut);

  }


  /* Erase subtitle : use last_y and last_lines saved last turn. */
  if (this->last_lines) {
    this->renderer->filled_rect (this->osd, 0, this->last_y,
                                 this->width - 1, this->last_y + this->last_lines * this->line_height,
                                 0);
  }

  switch (GET_Y_ALIGNMENT(alignment)) {
  case ALIGN_TOP:
    if (sub_y >= 0) y = sub_y;
    else y = 5;
    break;

  case ALIGN_MIDDLE:
    if (sub_y >= 0) y = sub_y - (this->lines * this->line_height) / 2;
    else y = (this->height - this->lines * this->line_height) / 2;
    break;

  case ALIGN_BOTTOM:
  default:
    if (sub_y >= 0) y = sub_y - this->lines * this->line_height;
    else y = this->height - this->lines * this->line_height - this->class->vertical_offset;
    break;
  }
  if (y < 0 || y >= this->height)
    y = this->height - this->line_height * this->lines;

  this->last_lines = this->lines;
  this->last_y = y;


  for (line = 0; line < this->lines; line++) {
    int w, x;

    while(1) {
      w = ogm_get_width( this, this->text[line]);

      switch (GET_X_ALIGNMENT(alignment)) {
      case ALIGN_LEFT:
        if (sub_x >= 0) x = sub_x;
        else x = 5;
        break;

      case ALIGN_RIGHT:
        if (sub_x >= 0) x = sub_x - w;
        else x = max_width - w - 5;
        break;

      case ALIGN_CENTER:
      default:
        if (sub_x >= 0) x = sub_x - w / 2;
        else x = (max_width - w) / 2;
        break;
      }


      if( w > max_width && font_size > 16 ) {
        font_size -= 4;
        this->renderer->set_font (this->osd, get_font (this->class), font_size);
      } else {
        break;
      }
    }

    if( is_cjk_encoding(encoding) ) {
      this->renderer->render_text (this->osd, x, y + line * this->line_height,
                                   this->text[line], OSD_TEXT1);
    } else {
      ogm_render_line(this, x, y + line*this->line_height, this->text[line]);
    }
  }

  if( font_size != this->font_size )
    this->renderer->set_font (this->osd, get_font (this->class), this->font_size);

  if( this->last_subtitle_end && sub_start < this->last_subtitle_end ) {
    sub_start = this->last_subtitle_end;
  }
  this->last_subtitle_end = sub_end;

  this->renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
  this->renderer->get_palette(this->osd, this->spu_palette, this->spu_trans);
  /* append some colors for colored typeface tag */
  memcpy(this->spu_palette+OSD_TEXT2, sub_palette, sizeof(sub_palette));
  memcpy(this->spu_trans+OSD_TEXT2, sub_trans, sizeof(sub_trans));
  this->renderer->set_palette(this->osd, this->spu_palette, this->spu_trans);

  if (this->unscaled)
    this->renderer->show_unscaled (this->osd, sub_start);
  else
    this->renderer->show (this->osd, sub_start);

  this->renderer->hide (this->osd, sub_end);

  lprintf ("scheduling subtitle >%s< at %"PRId64" until %"PRId64", current time is %"PRId64"\n",
	   this->text[0], sub_start, sub_end,
	   this->stream->xine->clock->get_current_time (this->stream->xine->clock));
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  int uses_time;
  int32_t start, end, diff;
  int64_t start_vpts, end_vpts;
  int64_t spu_offset;
  int i;
  uint32_t *val;
  char *str;
  extra_info_t extra_info;
  int master_status, slave_status;
  int vo_discard;

  /* filter unwanted streams */
  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    return;
  }
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((this->stream->spu_channel & 0x1f) != (buf->type & 0x1f))
    return;

  if ( (buf->decoder_flags & BUF_FLAG_SPECIAL) &&
       (buf->decoder_info[1] == BUF_SPECIAL_CHARSET_ENCODING) )
    this->buf_encoding = buf->decoder_info_ptr[2];
  else
    this->buf_encoding = NULL;

  this->current_osd_text = OSD_TEXT1;

  if( (buf->type & 0xFFFF0000) == BUF_SPU_OGM ) {

    this->ogm = 1;
    uses_time = 1;
    val = (uint32_t * )buf->content;
    start = *val++;
    end = *val++;
    str = (char *)val;

    if (!*str) return;
    /* Empty ogm packets (as created by ogmmux) clears out old messages. We already respect the end time. */

    this->lines = 0;

    i = 0;
    while (*str && (this->lines < SUB_MAX_TEXT) && (i < SUB_BUFSIZE)) {
      if (*str == '\r' || *str == '\n') {
        if (i) {
          this->text[ this->lines ][i] = 0;
          this->lines++;
          i = 0;
        }
      } else {
        this->text[ this->lines ][i] = *str;
        if (i < SUB_BUFSIZE-1)
          i++;
      }
      str++;
    }
    if (i == SUB_BUFSIZE)
      i--;

    if (i) {
      this->text[ this->lines ][i] = 0;
      this->lines++;
    }

  } else {

    this->ogm = 0;
    val = (uint32_t * )buf->content;

    this->lines = *val++;
    uses_time = *val++;
    start = *val++;
    end = *val++;
    str = (char *)val;
    for (i = 0; i < this->lines; i++, str += strlen(str) + 1) {
      strncpy( this->text[i], str, SUB_BUFSIZE - 1);
      this->text[i][SUB_BUFSIZE - 1] = '\0';
    }

  }

  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
          "libsputext: decoder data [%s]\n", this->text[0]);
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
          "libsputext: mode %d timing %d->%d\n", uses_time, start, end);

  if( end <= start ) {
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
            "libsputext: discarding subtitle with invalid timing\n");
    return;
  }

  spu_offset = this->stream->master->metronom->get_option (this->stream->master->metronom,
                                                           METRONOM_SPU_OFFSET);
  if( uses_time ) {
    start += (spu_offset / 90);
    end += (spu_offset / 90);
  } else {
    if( this->osd && this->img_duration ) {
      start += spu_offset / this->img_duration;
      end += spu_offset / this->img_duration;
    }
  }

  while( !this->finished ) {

    master_status = xine_get_status (this->stream->master);
    slave_status = xine_get_status (this->stream);
    vo_discard = this->stream->video_out->get_property(this->stream->video_out,
                                                       VO_PROP_DISCARD_FRAMES);

    _x_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );

    lprintf("master: %d slave: %d input_normpos: %d vo_discard: %d\n",
      master_status, slave_status, extra_info.input_normpos, vo_discard);

    if( !this->started && (master_status == XINE_STATUS_PLAY &&
                           slave_status == XINE_STATUS_PLAY &&
                           extra_info.input_normpos) ) {
      lprintf("started\n");

      this->width = this->height = 0;

      update_output_size( this );
      if( this->width && this->height ) {
        this->started = 1;
      }
    }

    if( this->started ) {

      if( master_status != XINE_STATUS_PLAY ||
          slave_status != XINE_STATUS_PLAY ||
          vo_discard ) {
        lprintf("finished\n");

        this->width = this->height = 0;
        this->finished = 1;
        return;
      }

      if( this->osd ) {

        /* try to use frame number mode */
        if( !uses_time && extra_info.frame_number ) {

          diff = end - extra_info.frame_number;

          /* discard old subtitles */
          if( diff < 0 ) {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
                    "libsputext: discarding old subtitle\n");
            return;
          }

          diff = start - extra_info.frame_number;

          start_vpts = extra_info.vpts + diff * this->img_duration;
          end_vpts = start_vpts + (end-start) * this->img_duration;

        } else {

          if( !uses_time ) {
            start = start * this->img_duration / 90;
            end = end * this->img_duration / 90;
            uses_time = 1;
          }

          diff = end - extra_info.input_time;

          /* discard old subtitles */
          if( diff < 0 ) {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
                    "libsputext: discarding old subtitle\n");
            return;
          }

          diff = start - extra_info.input_time;

          start_vpts = extra_info.vpts + diff * 90;
          end_vpts = start_vpts + (end-start) * 90;
        }

        _x_spu_decoder_sleep(this->stream, start_vpts);
        update_output_size( this );
        draw_subtitle(this, start_vpts, end_vpts);

        return;
      }
    }

    if (_x_spu_decoder_sleep(this->stream, 0))
      xine_usec_sleep (50000);
    else
      return;
  }
}


static void spudec_reset (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  lprintf("i guess we just seeked\n");
  this->width = this->height = 0;
  this->started = this->finished = 0;
  this->last_subtitle_end = 0;
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  /* sputext_decoder_t *this = (sputext_decoder_t *) this_gen; */

}

static void spudec_dispose (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_vertical_offset(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->vertical_offset = entry->num_value;
}

static void update_osd_font(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  strncpy(class->font, entry->str_value, FONTNAME_SIZE);
  class->font[FONTNAME_SIZE - 1] = '\0';

  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_font = %s\n", class->font );
}

#ifdef HAVE_FT2
static void update_osd_font_ft(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  strncpy(class->font_ft, entry->str_value, FILENAME_MAX);
  class->font_ft[FILENAME_MAX - 1] = '\0';

  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_font_ft = %s\n", class->font_ft);
}

static void update_osd_use_font_ft(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->use_font_ft = entry->num_value;

  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_use_font_ft = %d\n", class->use_font_ft);
}
#endif

static void update_subtitle_size(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->subtitle_size = entry->num_value;
}

static void update_use_unscaled(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->use_unscaled = entry->num_value;
}

static spu_decoder_t *sputext_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  sputext_class_t *class = (sputext_class_t *)class_gen;
  sputext_decoder_t *this ;

  this = (sputext_decoder_t *) calloc(1, sizeof(sputext_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;
  this->spu_decoder.dispose             = spudec_dispose;

  this->class  = class;
  this->stream = stream;

  return (spu_decoder_t *) this;
}

static void sputext_class_dispose (spu_decoder_class_t *class_gen) {
  sputext_class_t *this = (sputext_class_t *)class_gen;

  this->xine->config->unregister_callback(this->xine->config,
					  "subtitles.separate.src_encoding");
  this->xine->config->unregister_callback(this->xine->config,
					  "subtitles.separate.subtitle_size");
  this->xine->config->unregister_callback(this->xine->config,
					  "subtitles.separate.vertical_offset");
  this->xine->config->unregister_callback(this->xine->config,
					  "subtitles.separate.use_unscaled_osd");
  free (this);
}

static void update_src_encoding(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->src_encoding = entry->str_value;
  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_src_encoding = %s\n", class->src_encoding );
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  static const char *const subtitle_size_strings[] = {
    "tiny", "small", "normal", "large", "very large", "huge", NULL
  };
  sputext_class_t *this ;

  lprintf("init class\n");

  this = (sputext_class_t *) calloc(1, sizeof(sputext_class_t));

  this->class.open_plugin      = sputext_class_open_plugin;
  this->class.identifier       = "sputext";
  this->class.description      = N_("external subtitle decoder plugin");
  this->class.dispose          = sputext_class_dispose;

  this->xine                   = xine;

  this->subtitle_size  = xine->config->register_enum(xine->config,
			      "subtitles.separate.subtitle_size",
			       1,
			       (char **)subtitle_size_strings,
			       _("subtitle size"),
			       _("You can adjust the subtitle size here. The setting will "
			         "be evaluated relative to the window size."),
			       0, update_subtitle_size, this);
  this->vertical_offset  = xine->config->register_num(xine->config,
			      "subtitles.separate.vertical_offset",
			      0,
			      _("subtitle vertical offset"),
			      _("You can adjust the vertical position of the subtitle. "
			        "The setting will be evaluated relative to the window size."),
			      0, update_vertical_offset, this);
  strncpy(this->font, xine->config->register_string(xine->config,
				"subtitles.separate.font",
				"sans",
				_("font for subtitles"),
				_("A font from the xine font directory to be used for the "
				  "subtitle text."),
				10, update_osd_font, this), FONTNAME_SIZE);
  this->font[FONTNAME_SIZE - 1] = '\0';
#ifdef HAVE_FT2
  strncpy(this->font_ft, xine->config->register_filename(xine->config,
				"subtitles.separate.font_freetype",
				"", XINE_CONFIG_STRING_IS_FILENAME,
				_("font for subtitles"),
				_("An outline font file (e.g. a .ttf) to be used for the subtitle text."),
				10, update_osd_font_ft, this), FILENAME_MAX);
  this->font_ft[FILENAME_MAX - 1] = '\0';
  this->use_font_ft = xine->config->register_bool(xine->config,
				"subtitles.separate.font_use_freetype",
				0,
				_("whether to use a freetype font"),
				NULL,
				10, update_osd_use_font_ft, this);
#endif
  this->src_encoding  = xine->config->register_string(xine->config,
				"subtitles.separate.src_encoding",
				xine_guess_spu_encoding(),
				_("encoding of the subtitles"),
				_("The encoding of the subtitle text in the stream. This setting "
				  "is used to render non-ASCII characters correctly. If non-ASCII "
				  "characters are not displayed as you expect, ask the "
				  "creator of the subtitles what encoding was used."),
				10, update_src_encoding, this);
  this->use_unscaled  = xine->config->register_bool(xine->config,
			      "subtitles.separate.use_unscaled_osd",
			       1,
			       _("use unscaled OSD if possible"),
			       _("The unscaled OSD will be rendered independently of the video "
				 "frame and will always be sharp, even if the video is magnified. "
				 "This will look better, but does not work with all graphics "
				 "hardware. The alternative is the scaled OSD, which will become "
				 "blurry, if you enlarge a low resolution video to fullscreen, but "
				 "it works with all graphics cards."),
			       10, update_use_unscaled, this);

  return &this->class;
}


/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_TEXT, BUF_SPU_OGM, 0 };

static const decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

extern void *init_sputext_demux_class (xine_t *xine, void *data);

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER | PLUGIN_MUST_PRELOAD, 17, "sputext", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_DEMUX, 27, "sputext", XINE_VERSION_CODE, NULL, &init_sputext_demux_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
