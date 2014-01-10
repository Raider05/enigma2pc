/*
 * Copyright (C) 2001-2012 the xine project
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
 *
 * xine-fontconv.c
 *
 * converts ttf fonts to xine osd fonts
 *
 * compile:
 *   gcc -o xine-fontconv xine-fontconv.c `freetype-config --cflags --libs` -lz
 *
 * usage:
 *   xine-fontconv font.ttf fontname [encoding1 [encoding2 [...]]]
 *
 * begin                : Sat Dec 1 2001
 * copyright            : (C) 2001 by Miguel Freitas
 * unicode stuff        : (C) 2003 by Frantisek Dvorak
 */

#include <sys/param.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <zlib.h>
#include <iconv.h>

#ifndef OLD_FREETYPE2
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#else                   /* freetype 2.0.1 */
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#endif

/* we want UCS-2 encoding in the machine endian */
#if BYTE_ORDER == BIG_ENDIAN
#  define UCS2_ENCODING "UCS-2BE"
#else
#  define UCS2_ENCODING "UCS-2LE"
#endif

#ifndef MAX
#  define MAX (a, b) ((a) > (b)? (a) : (b))
#endif

#define f266ToInt(x)            (((x)+32)>>6)   /* round fractional fixed point */
                                                /* coordinates are in 26.6 pixels (i.e. 1/64th of pixels)*/
#define f266CeilToInt(x)        (((x)+63)>>6)   /* ceiling */
#define f266FloorToInt(x)       ((x)>>6)        /* floor */

/*
#define LOG 1
*/

/* xine stuff */
typedef struct osd_fontchar_s osd_fontchar_t;
typedef struct osd_font_s     osd_font_t;
typedef unsigned short        uint16_t;
typedef unsigned char         uint8_t;

struct osd_fontchar_s {
  uint16_t code;
  uint16_t width;
  uint16_t height;
  uint8_t *bmp;
};

struct osd_font_s {
  char             name[40];
  uint16_t         version;
  uint16_t         size;
  uint16_t         num_fontchars;
  osd_fontchar_t  *fontchar;
  osd_font_t      *next;
};


/* list */

typedef struct item_s item_t;

struct item_s {
  uint16_t code;
  item_t *next;
};


osd_fontchar_t fontchar;
osd_font_t     font;


static int gzwrite_i16(gzFile fp, uint16_t number) {
  return gzputc(fp, number & 0xFF) != -1 &&
         gzputc(fp, (number >> 8) & 0xFF) != -1;
}


/* search the item with 'code' in the sorted list */
item_t *list_search(item_t *list, uint16_t code, item_t **parent) {
  item_t *item;

  /* searching */
  item = list;
  while(item && item->code < code) {
    list = item;
    item = item->next;
  }

  /* parent (or future parent) */
  if (parent) *parent = list == item ? NULL : list;

  if (item && item->code == code) return item;
  else return NULL;
}

/* add new number into sorted list, returns if code is there already */
int list_insert(item_t **list, uint16_t code) {
  item_t *item, *parent;

  if ((item = list_search(*list, code, &parent)) == NULL) {
  /* insert new item */
    if ((item = malloc(sizeof(item_t))) == NULL) {
      printf("Insufficient memory\n");
      abort();
    }
    item->code = code;
    if (parent) {
      item->next = parent->next;
      parent->next = item;
    } else {
      item->next = *list;
      *list = item;
    }
    return 0;
  } else {
  /* item is there already */
    return 1;
  }
}

/* free the list */
void list_free(item_t *list) {
  item_t *item;

  while(list != NULL) {
    item = list;
    list = list->next;
    free(item);
  }
}

/*
 * generate sorted list with unicodes in all given pages,
 * returns number of unicodes in the list
 */
uint16_t generate_unicodes_list(item_t **list, char **pages, int number) {
  int page;
  uint16_t codes_count = 0;  /* unicode counter */
  int32_t z;                 /* index in the codepage */
  iconv_t cd;                /* iconv conversion descriptor */

  *list = NULL;

  /* process all given codepages */
  for (page = 0; page < number; page++) {
    /* prepare encoding */
    if ((cd = iconv_open(UCS2_ENCODING, pages[page])) == (iconv_t)-1) {
      printf("Unsupported encoding \"%s\"\n", pages[page]);
      continue;
    }
    printf("Used encoding \"%s\"\n", pages[page]);

    /* add new unicodes into list */
    for (z = 32; z < 0xFFFF; z++) {
      uint16_t unicode;
      char *inbuf = (char *)&z;
      char *outbuf = (char *)&unicode;
      size_t inbytesleft = z <= 0xFF ? 1 : 2;
      size_t outbytesleft = 2;
      size_t count;

      /* get unicode value from index 'z' in this codepage 'pages[i]' */
      count = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
      if (count == (size_t)-1 || inbytesleft != 0) {
      /* unused index 'z' in this codepage */
        continue;
      }

      if (!list_insert(list, unicode)) codes_count++;
    }
    iconv_close(cd);
  }

  return codes_count;
}

#ifdef LOG
void print_bitmap (FT_Bitmap *bitmap) {

  int x,y;

  for( y = 0; y < bitmap->rows; y++ ) {
    for( x = 0; x < bitmap->width; x++ ) {
      if( bitmap->buffer[y*bitmap->width+x] > 1 )
        printf("%02x ", bitmap->buffer[y*bitmap->width+x] );
      else
        printf("   ");
    }
    printf("\n");
  }
}
#endif

FT_Bitmap *create_bitmap (int width, int height) {
  FT_Bitmap * bitmap;

#ifdef LOG
  printf("Bitmap char %d %d\n",width,height);
#endif
  bitmap = malloc( sizeof( FT_Bitmap ) );
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->buffer = malloc(width*height);
  memset( bitmap->buffer, 0, width*height );

  return bitmap;
}

void destroy_bitmap (FT_Bitmap * bitmap) {
  free(bitmap->buffer);
  free(bitmap);
}


/*
   This function is called to blend a slightly deslocated
   version of the bitmap. This will produce the border effect.
   Note that the displacement may be smaller than 1 pixel
   as the bitmap is generated in freetype 1/64 units.
   This border is antialiased to the background.
*/
void add_border_bitmap( FT_Bitmap *dst, FT_Bitmap *src, int left, int top )
{
  int x,y;
  int x1, y1;
  int dstpos, srcpos;

  for( y = 0; y < src->rows; y++ ) {
    for( x = 0; x < src->width; x++ ) {
      srcpos = y * src->width + x;

      x1 = x + left;
      if( x1 < 0 || x1 >= dst->width )
        continue;

      y1 = y + top;
      if( y1 < 0 || y1 >= dst->rows )
        continue;

      dstpos = y1 * dst->width + x1;
      src->buffer[srcpos] /= 51;
      if( src->buffer[srcpos] > dst->buffer[dstpos] )
        dst->buffer[dstpos] = src->buffer[srcpos];
    }
  }
}

/*
   Blend the final version of bitmap (the foreground color) over the
   already generated border. It will be antialiased to the border.

   Final palette will be:

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
void add_final_bitmap( FT_Bitmap *dst, FT_Bitmap *src, int left, int top )
{
  int x,y;
  int x1, y1;
  int dstpos, srcpos;

  for( y = 0; y < src->rows; y++ ) {
    for( x = 0; x < src->width; x++ ) {
      srcpos = y * src->width + x;

      x1 = x + left;
      if( x1 < 0 || x1 >= dst->width )
        continue;

      y1 = y + top;
      if( y1 < 0 || y1 >= dst->rows )
        continue;

      dstpos = y1 * dst->width + x1;
      src->buffer[srcpos] /= 52;
      if( src->buffer[srcpos] )
        dst->buffer[dstpos] = src->buffer[srcpos] + 5;
    }
  }

  for( y = 0; y < dst->rows; y++ ) {
    for( x = 0; x < dst->width; x++ ) {
      dstpos = y * dst->width + x;
      dst->buffer[dstpos]++;
    }
  }

}


void render_font (FT_Face face, char *fontname, int size, int thickness,
                  item_t *unicodes) {

  char                filename[1024];
  FT_Bitmap          *out_bitmap;
  gzFile              fp;
  int                 error;
  int                 error_counter;
  int                 glyph_index;
  FT_Glyph            glyph;
  FT_BitmapGlyph      glyph_bitmap;
  FT_Vector           origin;
  int                 max_bearing_y = 0;
  int                 i;
  int                 converted;
  item_t             *item, *error_unicodes;

  static int border_pos[9][2] = {
    {-1,0},{1,0},{0,-1},{0,1},
    {-1,-1},{1,-1},{-1,1},{1,1}, {0,0}
  };


  /*
   * generate filename, open file
   */

  snprintf (filename, sizeof(filename), "%s-%d.xinefont.gz", fontname, size);

  fp = gzopen(filename,"w");

  if (!fp) {
    printf ("error opening output file %s\n", filename);
    return;
  }

  /*
   * set up font
   */

  strncpy(font.name, fontname, sizeof(font.name));
  font.name[sizeof(font.name) - 1] = '\0';
  /* changes from version 1 to version 2:
   *   'code' in characters is defined as little endian 16-bit unicode
   *   characters are sorted by 'code'
   */
  font.version       = 2;
  font.num_fontchars = 0;
  font.size          = size;

  error = FT_Set_Pixel_Sizes( face,     /* handle to face object */
                              0,        /* pixel_width           */
                              size );   /* pixel_height          */

  if (error) {
    printf("error setting size\n");
    return;
  }

  if( !thickness )
    thickness = size * 64 / 30;

  /*
   * calc max bearing y.
   * this is needed to align all bitmaps by the upper position.
   */

  error_counter = 0;
  error_unicodes = NULL;
  for (item = unicodes; item; item = item->next) {
    glyph_index = FT_Get_Char_Index( face, item->code);

    if (!glyph_index) {
      error_counter++;
      list_insert(&error_unicodes, item->code);
      continue;
    }

    error = FT_Load_Glyph (face,               /* handle to face object */
                           glyph_index,        /* glyph index           */
                           FT_LOAD_DEFAULT );  /* load flags            */

    if (error) {
      error_counter++;
      list_insert(&error_unicodes, item->code);
      continue;
    }

#ifdef LOG
    printf("bearing_y %ld\n",face->glyph->metrics.horiBearingY);
#endif

    if( (face->glyph->metrics.horiBearingY >> 6) > max_bearing_y )
      max_bearing_y = (face->glyph->metrics.horiBearingY >> 6);
    font.num_fontchars++;
  }

  printf("max_bearing_y: %d\n", max_bearing_y + f266CeilToInt(thickness));

  gzwrite(fp, font.name, sizeof(font.name));
  gzwrite_i16(fp, font.version);
  gzwrite_i16(fp, font.size);
  gzwrite_i16(fp, font.num_fontchars);

  for (item = unicodes; item; item = item->next) {
    converted = 0;

    for( i=0; i < 9; i++ ) {

      glyph_index = FT_Get_Char_Index( face, item->code);


      if (glyph_index) {

        error = FT_Load_Glyph( face,          /* handle to face object */
                               glyph_index,   /* glyph index           */
                               FT_LOAD_DEFAULT );  /* load flags */

        if (!error) {
          error = FT_Get_Glyph( face->glyph, &glyph );

          if( i == 0 ) {
            out_bitmap = create_bitmap( f266CeilToInt(thickness + MAX(face->glyph->metrics.horiAdvance, face->glyph->metrics.width + face->glyph->metrics.horiBearingX)),
                                        f266CeilToInt((max_bearing_y<<6) - face->glyph->metrics.horiBearingY +
                                        face->glyph->metrics.height + thickness) );
          }

          origin.x = thickness + border_pos[i][0]*thickness;
          origin.y = thickness + border_pos[i][1]*thickness;

          error = FT_Glyph_Transform(glyph, NULL, &origin) ||
                  FT_Glyph_To_Bitmap( &glyph, ft_render_mode_normal, NULL, 1);

          if (error) {
            printf("error generating bitmap [U+%04X]\n", item->code);
            destroy_bitmap(out_bitmap);
            return;
          }

          glyph_bitmap = (FT_BitmapGlyph)glyph;

          if( i < 8 )
            add_border_bitmap( out_bitmap, &glyph_bitmap->bitmap, glyph_bitmap->left,
                               max_bearing_y - glyph_bitmap->top );
          else
            add_final_bitmap( out_bitmap, &glyph_bitmap->bitmap, glyph_bitmap->left,
                              max_bearing_y - glyph_bitmap->top );
          converted = 1;

          FT_Done_Glyph( glyph );
	}
      }
    }

    if( converted ) {
#ifdef LOG
      printf("[U+%04X] bitmap width: %d height: %d\n", item->code, out_bitmap->width, out_bitmap->rows );
      /*
      print_bitmap(out_bitmap);
      */
#endif
      fontchar.code = item->code;
      fontchar.width = out_bitmap->width;
      fontchar.height = out_bitmap->rows;

      gzwrite_i16 (fp, fontchar.code);
      gzwrite_i16 (fp, fontchar.width);
      gzwrite_i16 (fp, fontchar.height);
      gzwrite (fp, out_bitmap->buffer, out_bitmap->width*out_bitmap->rows);

      destroy_bitmap(out_bitmap);
    }
  }
  gzclose(fp);

  if (error_counter) {
    printf("error: %d characters couldn't read: ", error_counter);
    /* this unicodes wasn't readed from .ttf font */
    item = error_unicodes;
    while(item) {
      printf("U+%04X ", item->code);
      item = item->next;
    }
    printf("\n");
    list_free(error_unicodes);
  }
  printf ("generated %s (%d characters)\n", filename, font.num_fontchars);
}


int main(int argc, char *argv[]) {

  int          error;
  int          len;
  FT_Library   library;
  FT_Face      face;
  int          thickness = 0;
  char        *encoding = "iso-8859-1";
  item_t      *unicodes = NULL;  /* unicode list */
#ifdef LOG
  item_t      *item;
#endif
  uint16_t     count;

  /*
   * command line parsing
   */

  if (argc < 3) {
    printf ("usage: %s font.ttf fontname [encoding1 [encoding2 [...]]]\n", argv[0]);
    exit (1);
  }

  len = strlen (argv[1]);

  if (strncasecmp (&argv[1][len-4],".ttf",3)) {
    printf ("font name must have .ttf suffix (is %s)\n", &argv[1][len-4]);
    exit (1);
  }

  error = FT_Init_FreeType( &library );
  if( error ) {
    printf("error initializing freetype\n");
    return 1;
  }

  error = FT_New_Face( library,
                       argv[1],
                       0,
                       &face );
  if (error) {
    printf("error loading font\n");
    return 1;
  }
  error = FT_Select_Charmap( face, ft_encoding_unicode);
  if (error) {
    printf("error selecting unicode charmap\n");
    return 1;
  }

  if (argc == 3) {
    count = generate_unicodes_list(&unicodes, &encoding, 1);
  } else {
    count = generate_unicodes_list(&unicodes, argv + 3, argc - 3);
  }
#ifdef LOG
  printf("Prepared %d unicode values: ", count);
  for (item = unicodes; item; item = item->next) printf("U+%04X ", item->code);
  printf("\n");
#else
  printf("Prepared %d unicode values.\n", count);
#endif

  if (count) {
    render_font (face, argv[2], 16, thickness, unicodes);
    render_font (face, argv[2], 20, thickness, unicodes);
    render_font (face, argv[2], 24, thickness, unicodes);
    render_font (face, argv[2], 32, thickness, unicodes);
    render_font (face, argv[2], 48, thickness, unicodes);
    render_font (face, argv[2], 64, thickness, unicodes);
  } else
    printf("No font generated\n");

  list_free(unicodes);

  FT_Done_Face(face);

  FT_Done_FreeType(library);

  /*
   * some rgb -> yuv conversion,
   * can be used to calc new palettes
   */
  /*
  {
    float f;
    for (f=1.0; f<6.0; f+=1.0) {

      float R=f*40.0;
      float G=f*40.0;
      float B=f*42.0;
      float Y, Cb, Cr;

      Y  =  0.29900 * R + 0.58700 * G + 0.11400 * B ;
      Cb = -0.16874 * R - 0.33126 * G + 0.50000 * B  + 128.0;
      Cr =  0.50000 * R - 0.41869 * G - 0.08131 * B  + 128.0;

      printf ("CLUT_Y_CR_CB_INIT(0x%x, 0x%x, 0x%x),\n", (int) Y, (int) Cr, (int) Cb);
    }
  }
  */
  return 0;
}
