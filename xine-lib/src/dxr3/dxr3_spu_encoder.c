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

#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#define LOG_MODULE "dxr3_spu_encoder"
/* #define LOG_VERBOSE */
/* #define LOG */

#include "video_out_dxr3.h"

/* We use the following algorithm to reduce the given overlay palette
 * to a spu palette with only four distinct colours:
 *  - create a histogram on the overlay palette
 *  - the color with the maximum histogram value becomes one spu color
 *  - modify the histogram so that the counts for colors very near to the
 *    chosen one are lowered; this is done by multiplying with a penalty
 *    function 1-1/(dist/DIST_COEFF + 1) where dist is the squared spatial
 *    distance between current color and chosen spu color
 *  - continue with the next maximum
 * The used histogram modification function from above looks like that:
 *    ^
 *  1 +              ********
 *    |        ******
 *    |    ****
 *    |  **
 *    | *
 *  0 **--------------------> dist
 */
#define DIST_COEFF 1024.0


/* spu encoder function */
spu_encoder_t *dxr3_spu_encoder_init(void);
void           dxr3_spu_encode(spu_encoder_t *this);

/* helper functions */
static void    convert_palette(spu_encoder_t *this);
static void    create_histogram(spu_encoder_t *this);
static void    generate_clut(spu_encoder_t *this);
static void    map_colors(spu_encoder_t *this);
static void    convert_clut(spu_encoder_t *this);
static void    convert_overlay(spu_encoder_t *this);
static void    write_rle(spu_encoder_t *this, int *offset, int *higher_nibble, int length, int color);
static void    write_byte(spu_encoder_t *this, int *offset, uint8_t byte);
static void    write_nibble(spu_encoder_t *this, int *offset, int *higher_nibble, uint8_t nibble);


spu_encoder_t *dxr3_spu_encoder_init(void)
{
  spu_encoder_t *this;

  this = (spu_encoder_t *)malloc(sizeof(spu_encoder_t));
  this->target        = NULL;
  this->need_reencode = 0;
  this->malloc_size   = 0;
  lprintf("initialized\n");
  return this;
}

void dxr3_spu_encode(spu_encoder_t *this)
{
  if (!this->need_reencode || !this->overlay) return;
  lprintf("overlay for encoding arrived.\n");
  convert_palette(this);
  create_histogram(this);
  generate_clut(this);
  map_colors(this);
  convert_clut(this);
  convert_overlay(this);
  lprintf("overlay encoding completed\n");
}


static void convert_palette(spu_encoder_t *this)
{
  int i, y, cb, cr, r, g, b;

  if (!this->overlay->rgb_clut) {
    for (i = 0; i < OVL_PALETTE_SIZE; i++) {
      y  = (this->overlay->color[i] >> 16) & 0xff;
      cr = (this->overlay->color[i] >>  8) & 0xff;
      cb = (this->overlay->color[i]      ) & 0xff;
      r  = 1.164 * y + 1.596 * (cr - 128);
      g  = 1.164 * y - 0.813 * (cr - 128) - 0.392 * (cb - 128);
      b  = 1.164 * y + 2.017 * (cb - 128);
      if (r < 0) r = 0;
      if (g < 0) g = 0;
      if (b < 0) b = 0;
      if (r > 0xff) r = 0xff;
      if (g > 0xff) g = 0xff;
      if (b > 0xff) b = 0xff;
      this->overlay->color[i] = (r << 16) | (g << 8) | b;
    }
    this->overlay->rgb_clut = 1;
  }
  if (!this->overlay->hili_rgb_clut) {
    for (i = 0; i < OVL_PALETTE_SIZE; i++) {
      y  = (this->overlay->hili_color[i] >> 16) & 0xff;
      cr = (this->overlay->hili_color[i] >>  8) & 0xff;
      cb = (this->overlay->hili_color[i]      ) & 0xff;
      r  = 1.164 * y + 1.596 * (cr - 128);
      g  = 1.164 * y - 0.813 * (cr - 128) - 0.392 * (cb - 128);
      b  = 1.164 * y + 2.017 * (cb - 128);
      if (r < 0) r = 0;
      if (g < 0) g = 0;
      if (b < 0) b = 0;
      if (r > 0xff) r = 0xff;
      if (g > 0xff) g = 0xff;
      if (b > 0xff) b = 0xff;
      this->overlay->hili_color[i] = (r << 16) | (g << 8) | b;
    }
    this->overlay->hili_rgb_clut = 1;
  }
}

static void create_histogram(spu_encoder_t *this)
{
  rle_elem_t *rle;
  int i, x, y, len, part;

  for (i = 0; i < OVL_PALETTE_SIZE; i++)
    this->map[i] = this->clip_map[i] = 0;
  x = y = 0;
  for (i = 0, rle = this->overlay->rle; i < this->overlay->num_rle; i++, rle++) {
    len = rle->len;
    if (y >= this->overlay->hili_top && y < this->overlay->hili_bottom) {
      if (x < this->overlay->hili_left) {
        part = (this->overlay->hili_left - x < len) ? (this->overlay->hili_left - x) : len;
        this->map[rle->color] += part;
        len -= part;
        x += part;
      }
      if (x >= this->overlay->hili_left && x < this->overlay->hili_right) {
        part = (this->overlay->hili_right - x < len) ? (this->overlay->hili_right - x) : len;
        this->clip_map[rle->color] += part;
        len -= part;
        x += part;
      }
    }
    this->map[rle->color] += len;
    x += len;
    if (x >= this->overlay->width) {
      x = 0;
      y++;
    }
  }
#ifdef LOG
  for (i = 0; i < OVL_PALETTE_SIZE; i++)
    if (this->map[i])
      lprintf("histogram: colour #%d 0x%.8x appears %d times\n",
	     i, this->overlay->color[i], this->map[i]);
  for (i = 0; i < OVL_PALETTE_SIZE; i++)
    if (this->clip_map[i])
      lprintf("histogram: clip colour #%d 0x%.8x appears %d times\n",
        i, this->overlay->hili_color[i], this->clip_map[i]);
#endif
}

static void generate_clut(spu_encoder_t *this)
{
  int i, max, spu_color;
  double dist, diff;

  /* find first maximum -> first spu color */
  max = 0;
  for (i = 1; i < OVL_PALETTE_SIZE; i++)
    if (this->map[i] > this->map[max]) max = i;
  this->color[0] = this->overlay->color[max];
  this->trans[0] = this->overlay->trans[max];

  for (spu_color = 1; spu_color < 4; spu_color++) {
    /* modify histogram and find next maximum -> next spu color */
    max = 0;
    for (i = 0; i < OVL_PALETTE_SIZE; i++) {
      /* subtract a correction based on the distance to the last spu color */
      diff  = ((this->overlay->color[i]      ) & 0xff) - ((this->color[spu_color - 1]      ) & 0xff);
      dist  = diff * diff;
      diff  = ((this->overlay->color[i] >>  8) & 0xff) - ((this->color[spu_color - 1] >>  8) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->color[i] >> 16) & 0xff) - ((this->color[spu_color - 1] >> 16) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->trans[i]      )       ) - ((this->trans[spu_color - 1]      )       );
      dist += diff * diff;
      this->map[i] *= 1 - 1.0 / (dist / DIST_COEFF + 1.0);
      if (this->map[i] > this->map[max]) max = i;
    }
    this->color[spu_color] = this->overlay->color[max];
    this->trans[spu_color] = this->overlay->trans[max];
  }
#ifdef LOG
  for (spu_color = 0; spu_color < 4; spu_color++)
    lprintf("spu colour %d: 0x%.8x, trans: %d\n", spu_color,
      this->color[spu_color], this->trans[spu_color]);
#endif

  /* now the same stuff again, this time for the palette of the clipping area */

  /* find first maximum -> first spu color */
  max = 0;
  for (i = 1; i < OVL_PALETTE_SIZE; i++)
    if (this->clip_map[i] > this->clip_map[max]) max = i;
  this->hili_color[0] = this->overlay->hili_color[max];
  this->hili_trans[0] = this->overlay->hili_trans[max];

  for (spu_color = 1; spu_color < 4; spu_color++) {
    /* modify histogram and find next maximum -> next spu color */
    max = 0;
    for (i = 0; i < OVL_PALETTE_SIZE; i++) {
      /* subtract a correction based on the distance to the last spu color */
      diff  = ((this->overlay->hili_color[i]      ) & 0xff) - ((this->hili_color[spu_color - 1]      ) & 0xff);
      dist  = diff * diff;
      diff  = ((this->overlay->hili_color[i] >>  8) & 0xff) - ((this->hili_color[spu_color - 1] >>  8) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->hili_color[i] >> 16) & 0xff) - ((this->hili_color[spu_color - 1] >> 16) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->hili_trans[i]      )       ) - ((this->hili_trans[spu_color - 1]      )       );
      dist += diff * diff;
      this->clip_map[i] *= 1 - 1.0 / (dist / DIST_COEFF + 1.0);
      if (this->clip_map[i] > this->clip_map[max]) max = i;
    }
    this->hili_color[spu_color] = this->overlay->hili_color[max];
    this->hili_trans[spu_color] = this->overlay->hili_trans[max];
  }
#ifdef LOG
  for (spu_color = 0; spu_color < 4; spu_color++)
    lprintf("spu clip colour %d: 0x%.8x, trans: %d\n", spu_color,
      this->hili_color[spu_color], this->hili_trans[spu_color]);
#endif
}

static void map_colors(spu_encoder_t *this)
{
  int i, min, spu_color;
  double dist, diff, min_dist;

  /* for all colors in overlay palette find closest spu color */
  for (i = 0; i < OVL_PALETTE_SIZE; i++) {
    min = 0;
    min_dist = DBL_MAX;
    for (spu_color = 0; spu_color < 4; spu_color++) {
      diff  = ((this->overlay->color[i]      ) & 0xff) - ((this->color[spu_color]      ) & 0xff);
      dist  = diff * diff;
      diff  = ((this->overlay->color[i] >>  8) & 0xff) - ((this->color[spu_color] >>  8) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->color[i] >> 16) & 0xff) - ((this->color[spu_color] >> 16) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->trans[i]      )       ) - ((this->trans[spu_color]      )       );
      dist += diff * diff;
      if (dist < min_dist) {
        min_dist = dist;
	min = spu_color;
      }
    }
    this->map[i] = min;
  }

  /* for all colors in overlay clip palette find closest spu color */
  for (i = 0; i < OVL_PALETTE_SIZE; i++) {
    min = 0;
    min_dist = DBL_MAX;
    for (spu_color = 0; spu_color < 4; spu_color++) {
      diff  = ((this->overlay->hili_color[i]      ) & 0xff) - ((this->hili_color[spu_color]      ) & 0xff);
      dist  = diff * diff;
      diff  = ((this->overlay->hili_color[i] >>  8) & 0xff) - ((this->hili_color[spu_color] >>  8) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->hili_color[i] >> 16) & 0xff) - ((this->hili_color[spu_color] >> 16) & 0xff);
      dist += diff * diff;
      diff  = ((this->overlay->hili_trans[i]      )       ) - ((this->hili_trans[spu_color]      )       );
      dist += diff * diff;
      if (dist < min_dist) {
        min_dist = dist;
	min = spu_color;
      }
    }
    this->clip_map[i] = min;
  }
}

static void convert_clut(spu_encoder_t *this)
{
  int i, r, g, b, y, cb, cr;

  for (i = 0; i < 4; i++) {
    r  = (this->color[i] >> 16) & 0xff;
    g  = (this->color[i] >>  8) & 0xff;
    b  = (this->color[i]      ) & 0xff;
    y  =  0.257 * r + 0.504 * g + 0.098 * b;
    cr =  0.439 * r - 0.368 * g - 0.071 * b + 128;
    cb = -0.148 * r - 0.291 * g + 0.439 * b + 128;
    this->color[i] = (y << 16) | (cr << 8) | cb;
  }
  for (i = 4; i < 16; i++)
    this->color[i] = 0x00008080;

  for (i = 0; i < 4; i++) {
    r  = (this->hili_color[i] >> 16) & 0xff;
    g  = (this->hili_color[i] >>  8) & 0xff;
    b  = (this->hili_color[i]      ) & 0xff;
    y  =  0.257 * r + 0.504 * g + 0.098 * b;
    cr =  0.439 * r - 0.368 * g - 0.071 * b + 128;
    cb = -0.148 * r - 0.291 * g + 0.439 * b + 128;
    this->hili_color[i] = (y << 16) | (cr << 8) | cb;
  }
  for (i = 4; i < 16; i++)
    this->hili_color[i] = 0x00008080;
}

static void convert_overlay(spu_encoder_t *this)
{
  int offset = 0, field_start[2];
  rle_elem_t *rle;
  int field, i, len, part, x, y, higher_nibble = 1;

  /* size will be determined later */
  write_byte(this, &offset, 0x00);
  write_byte(this, &offset, 0x00);

  /* control sequence pointer will be determined later */
  write_byte(this, &offset, 0x00);
  write_byte(this, &offset, 0x00);

  for (field = 0; field < 2; field++) {
    write_byte(this, &offset, 0x00);
    write_byte(this, &offset, 0x00);
    lprintf("encoding field %d\n", field);
    field_start[field] = offset;
    x = y = 0;
    for (i = 0, rle = this->overlay->rle; i < this->overlay->num_rle; i++, rle++) {
      len = rle->len;
      if ((y & 1) == field) {
        if (y >= this->overlay->hili_top && y < this->overlay->hili_bottom) {
          if (x < this->overlay->hili_left) {
            part = (this->overlay->hili_left - x < len) ? (this->overlay->hili_left - x) : len;
	    write_rle(this, &offset, &higher_nibble, part, this->map[rle->color]);
            len -= part;
            x += part;
          }
          if (x >= this->overlay->hili_left && x < this->overlay->hili_right) {
            part = (this->overlay->hili_right - x < len) ? (this->overlay->hili_right - x) : len;
            write_rle(this, &offset, &higher_nibble, part, this->clip_map[rle->color]);
            len -= part;
            x += part;
          }
        }
        write_rle(this, &offset, &higher_nibble, len, this->map[rle->color]);
      }
      x += len;
      if (x >= this->overlay->width) {
        if ((y & 1) == field && !higher_nibble)
	  write_nibble(this, &offset, &higher_nibble, 0);
        x = 0;
        y++;
      }
    }
  }

  /* we should be byte aligned here */
  _x_assert(higher_nibble);

  /* control sequence starts here */
  this->target[2] = offset >> 8;
  this->target[3] = offset & 0xff;
  write_byte(this, &offset, 0x00);
  write_byte(this, &offset, 0x00);
  /* write pointer to end sequence */
  write_byte(this, &offset, this->target[2]);
  write_byte(this, &offset, this->target[3]);
  /* write control sequence */
  write_byte(this, &offset, 0x00);
  /* clut indices */
  write_byte(this, &offset, 0x03);
  write_byte(this, &offset, 0x32);
  write_byte(this, &offset, 0x10);
  /* alpha information */
  write_byte(this, &offset, 0x04);
  write_nibble(this, &offset, &higher_nibble, this->trans[3] & 0xf);
  write_nibble(this, &offset, &higher_nibble, this->trans[2] & 0xf);
  write_nibble(this, &offset, &higher_nibble, this->trans[1] & 0xf);
  write_nibble(this, &offset, &higher_nibble, this->trans[0] & 0xf);
  /* on screen position */
  lprintf("overlay position: x %d, y %d, width %d, height %d\n",
    this->overlay->x, this->overlay->y, this->overlay->width, this->overlay->height);
  write_byte(this, &offset, 0x05);
  write_byte(this, &offset, this->overlay->x >> 4);
  write_nibble(this, &offset, &higher_nibble, this->overlay->x & 0xf);
  write_nibble(this, &offset, &higher_nibble, (this->overlay->x + this->overlay->width - 1) >> 8);
  write_byte(this, &offset, (this->overlay->x + this->overlay->width - 1) & 0xff);
  write_byte(this, &offset, this->overlay->y >> 4);
  write_nibble(this, &offset, &higher_nibble, this->overlay->y & 0xf);
  write_nibble(this, &offset, &higher_nibble, (this->overlay->y + this->overlay->height - 1) >> 8);
  write_byte(this, &offset, (this->overlay->y + this->overlay->height - 1) & 0xff);
  /* field pointers */
  write_byte(this, &offset, 0x06);
  write_byte(this, &offset, field_start[0] >> 8);
  write_byte(this, &offset, field_start[0] & 0xff);
  write_byte(this, &offset, field_start[1] >> 8);
  write_byte(this, &offset, field_start[1] & 0xff);
  /* end marker */
  write_byte(this, &offset, 0xff);
  if (offset & 1)
    write_byte(this, &offset, 0xff);
  /* write size information */
  this->size = offset;
  this->target[0] = offset >> 8;
  this->target[1] = offset & 0xff;
}

static void write_rle(spu_encoder_t *this, int *offset, int *higher_nibble, int length, int color)
{
  if (!length) return;
  length <<= 2;
  while (length > 0x03fc) {
    write_nibble(this, offset, higher_nibble, 0x0);
    write_nibble(this, offset, higher_nibble, 0x3);
    write_nibble(this, offset, higher_nibble, 0xf);
    write_nibble(this, offset, higher_nibble, 0xc | color);
    length -= 0x03fc;
  }
  if ((length & ~0xc) == 0) {
    write_nibble(this, offset, higher_nibble, length | color);
    return;
  }
  if ((length & ~0x3c) == 0) {
    write_nibble(this, offset, higher_nibble, length >> 4);
    write_nibble(this, offset, higher_nibble, (length & 0xc) | color);
    return;
  }
  if ((length & ~0xfc) == 0) {
    write_nibble(this, offset, higher_nibble, 0x0);
    write_nibble(this, offset, higher_nibble, length >> 4);
    write_nibble(this, offset, higher_nibble, (length & 0xc) | color);
    return;
  }
  if ((length & ~0x3fc) == 0) {
    write_nibble(this, offset, higher_nibble, 0x0);
    write_nibble(this, offset, higher_nibble, length >> 8);
    write_nibble(this, offset, higher_nibble, (length >> 4) & 0xf);
    write_nibble(this, offset, higher_nibble, (length & 0xc) | color);
    return;
  }
  _x_abort();
}

static void write_byte(spu_encoder_t *this, int *offset, uint8_t byte)
{
  if (*offset >= this->malloc_size)
    this->target = realloc(this->target, this->malloc_size += 2048);
  this->target[(*offset)++] = byte;
}

static void write_nibble(spu_encoder_t *this, int *offset, int *higher_nibble, uint8_t nibble)
{
  if (*offset >= this->malloc_size)
    this->target = realloc(this->target, this->malloc_size += 2048);
  if (*higher_nibble) {
    this->target[*offset] &= 0x0f;
    this->target[*offset] |= nibble << 4;
    *higher_nibble = 0;
  } else {
    this->target[*offset] &= 0xf0;
    this->target[(*offset)++] |= nibble;
    *higher_nibble = 1;
  }
}
