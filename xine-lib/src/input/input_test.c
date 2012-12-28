/*
 * Copyright (C) 2012 the xine project
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

/* Torsten Jager <t.jager@gmx.de>
   The idea is: present a virtual directory filled with images.
   Create on demand in memory what the user actually tries to access. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define LOG_MODULE "input_test"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>
#include <xine/video_out.h>

/* describe tests here */
static const char * const test_names[] = {
  "test://",
  "test://color_circle.bmp",
  "test://rgb_levels.bmp",
  "test://saturation_levels.bmp",
  "test://uv_square.bmp",
  "test://y_resolution.bmp",
  "test://color_circle.y4m",
  "test://rgb_levels.y4m",
  "test://saturation_levels.y4m",
  "test://uv_square.y4m",
  "test://y_resolution.y4m",
  "test://rgb_levels_fullrange.y4m",
  NULL
};
static const char test_type[]          = {2, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 2};
static const char test_is_yuv[]        = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1};
static const char test_is_mpeg_range[] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0};

#define TEST_FILES ((sizeof (test_names) / sizeof (char *)) - 1)

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
  xine_mrl_t       *mrls[TEST_FILES], m[TEST_FILES];
} test_input_class_t;

typedef struct {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;

  unsigned char    *buf, *bmp_head, *y4m_head, *y4m_frame;
  off_t             filesize, filepos, headsize, framesize;
  int               bufsize, index;
} test_input_plugin_t;

/* TJ. the generator code - actually a cut down version of my "testvideo" project */
/* It also reminisces good old Amiga coding style - strictly integer math, no     */
/* unnecessary memory allocations ...                                             */

static void put32le (unsigned int v, unsigned char *p) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

/* square root */
static unsigned int isqr (unsigned int v) {
  unsigned int a, b = v, c = 0, e = 0;
  if (v == 0) return (0);
  while (b) {b >>= 2; c++;}
  a = 1 << (c - 1);
  c = 1 << c;
  while (a + 1 < c) {
    b = (a + c) >> 1;
    e = b * b;
    if (e <= v) a = b; else c = b;
  }
  return (a + (c * c - v < v - e ? 1 : 0));
}

/* arcus tangens 0..24*255 */
static int iatan (int x, int y) {
  int a, b, v = 0;
  if ((x == 0) && (y == 0)) return (0);
  /* mirror to first half quadrant */
  if (y < 0) {v = -24 * 255; y = -y;}
  if (x < 0) {v = -12 * 255 - v; x = -x;}
  if (x < y) {v = -6 * 255 - v; a = x; b = y;} else {a = y; b = x;}
  /* cubic interpolation within 32 bit */
  v += (1027072 * a / b - 718 * a * a / b * 255 / b
    - 237 * a * a / b * a / b * 255 / b) >> 10;
  return (v < 0 ? -v : v);
}

/* absolute angle difference 0..180*255 */
static int adiff (int a, int b) {
  int d = a > b ? a - b : b - a;
  return (d < 12 * 255 ? d : 24 * 255 - d);
}

/* gimmick #2588: the XINE logo ;-) */
static void render_parallelogram (unsigned char *buf, int buf_width, int buf_height, unsigned int gray,
  int x, int y, int width, int height, int slant, int sc) {
  int i, o;
  int pitch = (3 * buf_width + 3) & ~3;
  if (height < 2) return;
  /* slant compensation */
  if (sc) {
    i = (width * slant + height / 2) / height;
    width = isqr (width * width + i * i);
  }
  /* 3 bytes per pixel */
  width *= 3;
  /* OK now render */
  height--;
  for (i = 0; i <= height; i++) {
    o = (buf_height - 1 - y - i) * pitch + 3 * (x + (slant * i + height / 2) / height);
    memset (buf + o, gray, width);
  }
}

static void render_turn (unsigned char *buf, int buf_width, int buf_height, unsigned int gray,
  int x, int y, int size, int quad) {
  int cx = quad & 1 ? 0 : size;
  int cy = quad & 2 ? 0 : size;
  int i, j, d, e;
  int _min = size * size, _max = 4 * _min;
  unsigned char *p;
  int pitch = (3 * buf_width + 3) & ~3;
  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      d = 2 * (i - cy) + 1;
      d *= d;
      e = 2 * (j - cx) + 1;
      e *= e;
      d += e;
      if ((d < _min) || (d > _max)) continue;
      p = buf + (buf_height - 1 - y - i) * pitch + 3 * (x + j);
      *p++ = gray;
      *p++ = gray;
      *p = gray;
    }
  }
}

static void render_xine_logo (unsigned char *buf, int buf_width, int buf_height, unsigned int gray) {
  int height = buf_height / 30 >= 10 ? buf_height / 30 : 10;
  int width = height / 4;
  int top = buf_height - 2 * height;
  int left = buf_width - 4 * height - 3 * width;
  /* safety */
  if ((top < 0) || (left < 0)) return;
  /* X */
  render_parallelogram (buf, buf_width, buf_height, gray,
    left, top, width, height, height - width, 1);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + height - width, top, width, height, width - height, 1);
  left += height + width / 2;
  /* I */
  render_parallelogram (buf, buf_width, buf_height, gray,
    left, top, width, height, 0, 0);
  left += 3 * width / 2;
  /* N */
  render_parallelogram (buf, buf_width, buf_height, gray,
    left, top, width, height, 0, 0);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + width, top, height - 3 * width, width, 0, 0);
  render_turn (buf, buf_width, buf_height, gray,
    left + height - 2 * width, top, 2 * width, 1);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + height - width, top + 2 * width, width, height - 2 * width, 0, 0);
  left += height + width / 2;
  /* E */
  render_turn (buf, buf_width, buf_height, gray,
    left, top, 2 * width, 0);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + 2 * width, top, height - 2 * width, width, 0, 0);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left, top + 2 * width, width, height - 4 * width, 0, 0);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + width, top + (height - width) / 2, height - width, width, 0, 0);
  render_turn (buf, buf_width, buf_height, gray,
    left, top + height - 2 * width, 2 * width, 2);
  render_parallelogram (buf, buf_width, buf_height, gray,
    left + 2 * width, top + height - width, height - 2  * width, width, 0, 0);
}

static int test_make (test_input_plugin_t * this) {
  int width, height, x, y, cx, cy, d, r, dx, dy, a, red, green, blue;
  int type, yuv, mpeg;
  int pad, pitch;
  int angle = 0, hdtv = 0, gray = 0;
  unsigned char *p;

  /* mode */
  type = test_type[this->index];
  yuv  = test_is_yuv[this->index];
  mpeg = test_is_mpeg_range[this->index];

  /* dimensions */
  width = 320;
  if (this->stream && this->stream->video_out) {
    x = this->stream->video_out->get_property (this->stream->video_out,
      VO_PROP_WINDOW_WIDTH);
    if (x > width) width = x;
  }
  if (width > 1920) width = 1920;
  width &= ~1;
  height = width * 9 / 16;
  height &= ~1;

  /* BMP rows must be n * 4 bytes long */
  pitch = (width * 3 + 3) & ~3;
  pad = pitch - width * 3;

  /* (re)allocate buffer */
  a = 54 + pitch * height;
  if (yuv) {
    if (height >= 720) hdtv = 1;
    a += 88 + width * height * 3 / 2;
  }
  if (this->buf && (a != this->bufsize)) {
    free (this->buf);
    this->buf = NULL;
  }
  if (!this->buf) {
    this->buf = malloc (a);
    if (!this->buf) return (1);
    this->bufsize = a;
  }

  /* make file heads */
  p = this->buf;
  this->bmp_head = p;
  this->filesize = 54 + pitch * height;
  if (yuv) {
    p += 54 + pitch * height;
    this->y4m_head = p;
    /* use inofficial extension to announce color matrix here ;-) */
    this->headsize = sprintf (p,
      "YUV4MPEG2 W%d H%d F25:1 Ip A0:0 C420mpeg2 XYSCSS=420MPEG2 XXINE_CM=%d\n",
      width, height, (hdtv ? 2 : 10) | !mpeg);
    p += 82;
    this->y4m_frame = p;
    memcpy (p, "FRAME\n", 6);
    this->framesize = 6 + width * height * 3 / 2;
    this->filesize = this->headsize + 10 * 25 * this->framesize;
  }
  this->filepos = 0;
  p = this->bmp_head;
  memset (p, 0, 54);
  p[0] = 'B';
  p[1] = 'M';
  put32le (54 + pitch * height, p + 2); /* file size */
  put32le (54, p + 10); /* header size */
  put32le (40, p + 14); /* ?? */
  put32le (width, p + 18);
  put32le (height, p + 22);
  p[26] = 1; /* ?? */
  p[28] = 24; /* depth */
  put32le (pitch * height, p + 34); /* bitmap size */
  put32le (2835, p + 38); /* ?? */
  put32le (2835, p + 42); /* ?? */
  p += 54;

  /* generate RGB image */
  switch (type) {

    case 1:
      /* color circle test */
      cx = width >> 1;
      cy = height >> 1;
      r = width < height ? (width * 98) / 100 : (height * 98) / 100;
      for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
          dx = ((x - cx) << 1) + 1;
          dy = ((y - cy) << 1) + 1;
          d = isqr (dx * dx + dy * dy);
          if (d > r) red = green = blue = 128; else {
            a = (iatan (dx, dy) + angle) % (24 * 255);
            red = 8 * 255 - adiff (a, 0);
            red = red < 0 ? 0 : (red > 4 * 255 ? 4 * 255 : red);
            red = red * d / (4 * r);
            green = 8 * 255 - adiff (a, 8 * 255);
            green = green < 0 ? 0 : (green > 4 * 255 ? 4 * 255 : green);
            green = green * d / (4 * r);
            blue = 8 * 255 - adiff (a, 16 * 255);
            blue = blue < 0 ? 0 : (blue > 4 * 255 ? 4 * 255 : blue);
            blue = blue * d / (4 * r);
          }
          *p++ = blue;
          *p++ = green;
          *p++ = red;
        }
        for (x = pad; x; x--) *p++ = 0;
      }
    break;

    case 2:
      /* sweep bars */
      dx = (((width + 9) / 18) + 1) & ~1;
      dy = (((height + 10) / 20) + 1) & ~1;
      cx = (width / 2 - 8 * dx) & ~1;
      cy = (height / 2 - 8 * dy) & ~1;
      /* bottom gray */
      d = cy * pitch;
      memset (p, 127, d);
      p += d;
      /* color bars */
      for (y = 0; y < 16; y++) {
        /* make 1 line */
        unsigned char *q = p;
        for (x = 0; x < width; x++) {
          d = x - cx;
          if ((d < 0) || (d >= 16 * dx)) red = green = blue = 127;
          else {
            a = (y + 1) & 2 ? 17 * (15 - d / dx) : 255 - 16 * d / dx;
            red = y & 4 ? a : 0;
            green = y & 8 ? a : 0;
            blue = y & 2 ? a : 0;
          }
          *p++ = blue;
          *p++ = green;
          *p++ = red;
        }
        for (x = pad; x; x--) *p++ = 0;
        /* duplicate it further */
        for (d = 1; d < dy; d++) {
          memcpy (p, q, pitch);
          p += pitch;
        }
      }
      /* top gray */
      memset (p, 127, (height - cy - 16 * dy) * pitch);
    break;

    case 3: {
      /* sweep bars, saturation */
      int g[] = {0, 29, 76, 105, 150, 179, 226, 255, 0, 18, 54, 73, 182, 201, 237, 255};
      dx = (((width + 9) / 18) + 1) & ~1;
      dy = (((height + 10) / 20) + 1) & ~1;
      cx = (width / 2 - 8 * dx) & ~1;
      cy = (height / 2 - 8 * dy) & ~1;
      /* bottom gray */
      d = cy * pitch;
      memset (p, 127, d);
      p += d;
      /* color bars */
      for (y = 0; y < 16; y++) {
        /* make 1 line */
        unsigned char *q = p;
        for (x = 0; x < width; x++) {
          d = x - cx;
          if ((d < 0) || (d >= 16 * dx)) red = green = blue = 127;
          else {
            a = (y + 1) & 2 ? 17 * (15 - d / dx) : 255 - 16 * d / dx;
            r = (255 - a) * g[y / 2 + 8 * hdtv];
            red = ((y & 4 ? 255 : 0) * a + r) / 255;
            green = ((y & 8 ? 255 : 0) * a + r) / 255;
            blue = ((y & 2 ? 255 : 0) * a + r) / 255;
          }
          *p++ = blue;
          *p++ = green;
          *p++ = red;
        }
        for (x = pad; x; x--) *p++ = 0;
        /* duplicate it further */
        for (d = 1; d < dy; d++) {
          memcpy (p, q, pitch);
          p += pitch;
        }
      }
      /* top gray */
      memset (p, 127, (height - cy - 16 * dy) * pitch);
    } break;

    case 4: {
      /* UV square */
      int m1 = hdtv ?  51603 :  45941;
      int m2 = hdtv ?  -6138 : -11277;
      int m3 = hdtv ? -15339 : -23401;
      int m4 = hdtv ?  60804 :  58065;
      r = width < height ? width : height;
      r = (49 << 9) * r / 100;
      for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
          int min, max, u, v;
          u = (x << 1) - width + 1;
          v = (y << 1) - height + 1;
          min = max = red = m1 * v;
          green = m2 * u + m3 * v;
          if (green < min) min = green; else if (green > max) max = green;
          blue = m4 * u;
          if (blue < min) min = blue; else if (blue > max) max = blue;
          d = (256 * r + (r >> 1)) + min - max - 1;
          if (d < 0) red = green = blue = 127;
          else {
            if (gray == 255) min -= d - (r >> 1);
            else min -= d / 255 * gray - (r >> 1);
            red = (red - min) / r;
            green = (green - min) / r;
            blue = (blue - min) / r;
          }
          *p++ = blue;
          *p++ = green;
          *p++ = red;
        }
        for (x = pad; x; x--) *p++ = 0;
      }
    } break;

    case 5:
      /* resolution pattern */
      dx = (width / 10);
      dy = (height / 7);
      for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
          if ((x < dx) || (x >= 9 * dx)) red = 127;
          else {
            d = x / dx;
            switch (y / dy) {
              case 1: red = ((x / d) ^ (y / d)) & 1 ? 0 : 255; break;
              case 3: red = (x / d) & 1 ? 0 : 255; break;
              case 5: red = (y / d) & 1 ? 0 : 255; break;
              default: red = 127;
            }
          }
          *p++ = red;
          *p++ = red;
          *p++ = red;
        }
        for (x = pad; x; x--) *p++ = 0;
      }
    break;
  }

  /* add logo */
  render_xine_logo (this->bmp_head + 54, width, height, 150);

  /* convert to YUV */
  if (yuv) {
    int fb, fr, yb, yr, yg, yo, ubvr, vb, ur, ug, vg;
    int i, _yb[256], _yr[256], _yg[256];
    int _ubvr[1024], _vb[1024], _ur[1024], _ug[1024], _vg[1024];
    unsigned char *p2, *q, *q2;
    #define SSHIFT 17
    #define SFACTOR (1 << SSHIFT)
    fb = hdtv ? 722 : 1140;
    fr = hdtv ? 2126 : 2990;
    if (mpeg) {
      yg = (SFACTOR * 219 + 127) / 255;
      yo = SFACTOR * 16 + SFACTOR / 2;
      ubvr = (SFACTOR * 112 + 127) / 255;
    } else {
      yg = SFACTOR;
      yo = SFACTOR / 2;
      ubvr = (SFACTOR * 127 + 127) / 255;
    }
    yb = (yg * fb + 5000) / 10000;
    yr = (yg * fr + 5000) / 10000;
    yg -= yb + yr;
    for (i = 0; i < 256; i++) {
      _yb[i] = yb * i;
      _yr[i] = yr * i;
      _yg[i] = yg * i + yo;
    }
    ur = (ubvr * fr + fb / 2 - 5000) / (fb - 10000);
    ug = -ur - ubvr;
    vb = (ubvr * fb + fr / 2 - 5000) / (fr - 10000);
    vg = -vb - ubvr;
    for (i = 0; i < 1024; i++) {
      _ubvr[i] = ubvr * i + 4 * (SFACTOR * 128 + SFACTOR / 2);
      _ur[i] = ur * i;
      _ug[i] = ug * i;
      _vb[i] = vb * i;
      _vg[i] = vg * i;
    }
    q = this->y4m_frame + 6;
    for (y = height - 1; y >= 0; y--) {
      p = this->bmp_head + 54 + y * pitch;
      for (x = width; x; x--) {
        *q++ = (_yb[p[0]] + _yg[p[1]] + _yr[p[2]]) >> SSHIFT;
        p += 3;
      }
    }
    q2 = q + width * height / 4;
    for (y = height - 2; y >= 0; y -= 2) {
      p = this->bmp_head + 54 + y * pitch;
      p2 = p + pitch;
      for (x = width / 2; x; x--) {
        blue  = (unsigned int)*p++ + *p2++;
        green = (unsigned int)*p++ + *p2++;
        red   = (unsigned int)*p++ + *p2++;
        blue  += (unsigned int)*p++ + *p2++;
        green += (unsigned int)*p++ + *p2++;
        red   += (unsigned int)*p++ + *p2++;
        *q++  = (_ubvr[blue] + _ug[green] + _ur[red]) >> (SSHIFT + 2);
        *q2++ = (_ubvr[red] + _vg[green] + _vb[blue]) >> (SSHIFT + 2);
      }
    }
  }

  /* add readable title */
  {
    char *test_titles[5] = {
      _("Color Circle"),
      _("RGB Levels"),
      _("Saturation Levels"),
      _("UV Square"),
      _("Luminance Resolution")
    };
    char *temp;

    temp = _x_asprintf ("%s [%s%s]", test_titles[type - 1],
      yuv ? (mpeg ? "" : "full swing ") : "",
      yuv ? (hdtv ? "ITU-R 709 YUV" : " ITU-R 601 YUV") : "RGB");
    _x_meta_info_set (this->stream, XINE_META_INFO_TITLE, temp);
    free(temp);
  }

  return (1);
}

/* instance functions */

static uint32_t test_plugin_get_capabilities (input_plugin_t *this_gen) {
  return INPUT_CAP_SEEKABLE;
}

static off_t test_plugin_read (input_plugin_t *this_gen, void *buf, off_t len) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  if (!this->buf || (len < 0) || !buf) return -1;
  if (len > this->filesize - this->filepos) len = this->filesize - this->filepos;

  if (test_is_yuv[this->index]) {
    char *p = this->y4m_frame, *q = buf;
    off_t l = len, d;
    d = this->headsize - this->filepos;
    if (d > 0) {
      xine_fast_memcpy (q, this->y4m_head + this->filepos, d);
      q += d;
      this->filepos += d;
      l -= d;
      d = this->framesize;
    } else {
      d = (this->filepos - this->headsize) % this->framesize;
      p += d;
      d = this->framesize - d;
    }
    while (l > 0) {
      if (d > l) d = l;
      xine_fast_memcpy (q, p, d);
      p = this->y4m_frame;
      q += d;
      this->filepos += d;
      l -= d;
      d = this->framesize;
    }
  } else {
    xine_fast_memcpy (buf, this->bmp_head + this->filepos, len);
    this->filepos += len;
  }
  return len;
}

static buf_element_t *test_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
  off_t todo) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;
  buf_element_t *buf;

  if (!this->buf || (todo < 0)) return NULL;

  buf = fifo->buffer_pool_alloc (fifo);
  if (todo > buf->max_size) todo = buf->max_size;
  buf->type = BUF_DEMUX_BLOCK;
  test_plugin_read (this_gen, buf->content, todo);

  return buf;
}

static off_t test_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;
  off_t newpos = offset;

  switch (origin) {
    case SEEK_SET: break;
    case SEEK_CUR: newpos += this->filepos; break;
    case SEEK_END: newpos += this->filesize; break;
    default: newpos = -1;
  }

  if ((newpos < 0) || (newpos > this->filesize)) {
    errno = EINVAL;
    return (off_t)-1;
  }

  this->filepos = newpos;
  return newpos;
}

static off_t test_plugin_get_current_pos (input_plugin_t *this_gen) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  return this->filepos;
}

static off_t test_plugin_get_length (input_plugin_t *this_gen) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  return this->filesize;
}

static uint32_t test_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static const char *test_plugin_get_mrl (input_plugin_t *this_gen) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  return test_names[this->index];
}

static int test_plugin_get_optional_data (input_plugin_t *this_gen, void *data,
  int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void test_plugin_dispose (input_plugin_t *this_gen ) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  if (this->buf) free (this->buf);
  free (this);
}

static int test_plugin_open (input_plugin_t *this_gen ) {
  test_input_plugin_t *this = (test_input_plugin_t *) this_gen;

  return test_make (this);
}

static input_plugin_t *test_class_get_instance (input_class_t *cls_gen,
  xine_stream_t *stream, const char *data) {
  test_input_plugin_t *this;
  int i;

  for (i = 0; i < TEST_FILES; i++) {
    if (!strcasecmp (data, test_names[i])) break;
  }
  if (i == TEST_FILES) return NULL;

  this = (test_input_plugin_t *) calloc(1, sizeof (test_input_plugin_t));
  this->stream = stream;
  this->index = i;

  this->input_plugin.open               = test_plugin_open;
  this->input_plugin.get_capabilities   = test_plugin_get_capabilities;
  this->input_plugin.read               = test_plugin_read;
  this->input_plugin.read_block         = test_plugin_read_block;
  this->input_plugin.seek               = test_plugin_seek;
  this->input_plugin.get_current_pos    = test_plugin_get_current_pos;
  this->input_plugin.get_length         = test_plugin_get_length;
  this->input_plugin.get_blocksize      = test_plugin_get_blocksize;
  this->input_plugin.get_mrl            = test_plugin_get_mrl;
  this->input_plugin.get_optional_data  = test_plugin_get_optional_data;
  this->input_plugin.dispose            = test_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  return &this->input_plugin;
}


/*
 * plugin class functions
 */

static const char * const * test_get_autoplay_list (input_class_t *this_gen, int *num_files) {
  if (num_files) *num_files = TEST_FILES - 1;
  return test_names + 1;
}

static xine_mrl_t **test_class_get_dir (input_class_t *this_gen, const char *filename,
  int *nFiles) {
  test_input_class_t *this = (test_input_class_t *) this_gen;
  int i;
  xine_mrl_t *m;

  if (!this->mrls[0]) {
    for (i = 0; i < TEST_FILES - 1; i++) {
      m = &this->m[i];
      this->mrls[i] = m;

      m->origin = (char *)test_names[0];
      m->mrl = (char *)test_names[i + 1];
      m->link = NULL;
      m->type = mrl_file | mrl_file_normal;
      m->size = 54 + 1024 * 576 * 3; /* for true size, call test_plugin_get_length () */
    }

    this->mrls[i] = NULL;
  }

  if (nFiles) *nFiles = TEST_FILES - 1;

  return this->mrls;
}

static void test_class_dispose (input_class_t *this_gen) {
  free (this_gen);
}

static void *init_plugin (xine_t *xine, void *data) {
  test_input_class_t *this;

  this = (test_input_class_t *) calloc(1, sizeof (test_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = test_class_get_instance;
  this->input_class.identifier         = "test";
  this->input_class.description        = N_("test card input plugin");
  this->input_class.get_dir            = test_class_get_dir;
  this->input_class.get_autoplay_list  = test_get_autoplay_list;
  this->input_class.dispose            = test_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

static input_info_t input_info_test = {
  110                       /* priority */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "TEST", XINE_VERSION_CODE, &input_info_test, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
