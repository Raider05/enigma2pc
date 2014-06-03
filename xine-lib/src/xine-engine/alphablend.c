/*
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * Copyright (C) 2000  Thomas Mirlacher
 *               2002-2013 the xine project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 *
 *------------------------------------------------------------
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
#define LOG_BLEND_YUV
#define LOG_BLEND_RGB16
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/alphablend.h>
#include "bswap.h"


#define BLEND_COLOR(dst, src, mask, o) ((((((src&mask)-(dst&mask))*(o*0x111+1))>>12)+(dst&mask))&mask)

#define BLEND_BYTE(dst, src, o) (((((src)-(dst))*(o*0x1111+1))>>16)+(dst))

static void mem_blend8(uint8_t *mem, uint8_t val, uint8_t o, size_t sz)
{
  uint8_t *limit = mem + sz;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, val, o);
    mem++;
  }
}

static void mem_blend16(uint16_t *mem, uint16_t clr, uint8_t o, int len) {
  uint16_t *limit = mem + len;
  while (mem < limit) {
    *mem =
     BLEND_COLOR(*mem, clr, 0xf800, o) |
     BLEND_COLOR(*mem, clr, 0x07e0, o) |
     BLEND_COLOR(*mem, clr, 0x001f, o);
    mem++;
  }
}

static void mem_blend24(uint8_t *mem, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t o, int len) {
  uint8_t *limit = mem + len*3;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, r, o);
    mem++;
    *mem = BLEND_BYTE(*mem, g, o);
    mem++;
    *mem = BLEND_BYTE(*mem, b, o);
    mem++;
  }
}

static void mem_blend32(uint8_t *mem, const uint8_t *src, uint8_t o, int len) {
  uint8_t *limit = mem + len*4;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, src[0], o);
    mem++;
    *mem = BLEND_BYTE(*mem, src[1], o);
    mem++;
    *mem = BLEND_BYTE(*mem, src[2], o);
    mem++;
    *mem = BLEND_BYTE(*mem, src[3], o);
    mem++;
  }
}

/*
 * Some macros for fixed point arithmetic.
 *
 * The blend_rgb* routines perform rle image scaling using
 * scale factors that are expressed as integers scaled with
 * a factor of 2**16.
 *
 * INT_TO_SCALED()/SCALED_TO_INT() converts from integer
 * to scaled fixed point and back.
 */
#define	SCALE_SHIFT	  16
#define	SCALE_FACTOR	  (1<<SCALE_SHIFT)
#define	INT_TO_SCALED(i)  ((i)  << SCALE_SHIFT)
#define	SCALED_TO_INT(sc) ((sc) >> SCALE_SHIFT)


static rle_elem_t *
rle_img_advance_line(rle_elem_t *rle, rle_elem_t *rle_limit, int w)
{
  int x;

  for (x = 0; x < w && rle < rle_limit; ) {
    x += rle->len;
    rle++;
  }
  return rle;
}

/*
 * heck, this function is overly complicated and currently buggy.
 * if James would like to revive it (implementing proper clipping -
 * not to confuse with button highlight) i would have no objections,
 * but for now i will be using an alternate version based on rgb24. [MF]
 *
 * obs: this function has about 420 lines. other blend_rgb16 has 165.
 */
#if JAMES_BLEND_RGB16_FUNCTION
void blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl,
                  int img_width, int img_height,
                  int dst_width, int dst_height,
                  alphablend_t *extra_data)
{
  uint8_t *trans;
  clut_t *clut;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_start = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;     /* scaled 2**SCALE_SHIFT */
  int clip_right, clip_left, clip_top;
  int hili_right, hili_left;
  uint16_t *img_pix;
  int rlelen;
  int rle_this_bite;
  int rle_remainder;
  int zone_state=0;
  uint8_t clr_next,clr;
  uint16_t o;
  double img_offset;
  int stripe_height;
/*
 * Let zone_state keep state.
 * 0 = Starting.
 * 1 = Above button.
 * 2 = Left of button.
 * 3 = Inside of button.
 * 4 = Right of button.
 * 5 = Below button.
 * 6 = Finished.
 *
 * Each time round the loop, update the state.
 * We can do this easily and cheaply(fewer IF statements per cycle) as we are testing rle end position anyway.
 * Possible optimization is to ensure that rle never overlaps from outside to inside a button.
 * Possible optimization is to pre-scale the RLE overlay, so that no scaling is needed here.
 */

#ifdef LOG_BLEND_RGB16
  printf("blend_rgb16: img_height=%i, dst_height=%i\n", img_height, dst_height);
  printf("blend_rgb16: img_width=%i, dst_width=%i\n", img_width, dst_width);
  if (img_width & 1) { printf("blend_rgb16s: odd\n");}
  else { printf("blend_rgb16s: even\n");}

#endif
/* stripe_height is used in yuv2rgb scaling, so use the same scale factor here for overlays. */
  stripe_height = 16 * img_height / dst_height;
/*  dy_step = INT_TO_SCALED(dst_height) / img_height; */
  dy_step = INT_TO_SCALED(16) / stripe_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;
#ifdef LOG_BLEND_RGB16
  printf("blend_rgb16: dy_step=%i, x_scale=%i\n", dy_step, x_scale);
#endif
  if (img_width & 1) img_width++;
  img_offset = ( ( (y_off * img_height) / dst_height) * img_width)
             + ( (x_off * img_width) / dst_width);
#ifdef LOG_BLEND_RGB16
  printf("blend_rgb16: x=%i, y=%i, w=%i, h=%i, img_offset=%lf\n", img_overl->x, img_overl->y,
    img_overl->width,
    img_overl->height,
    img_offset);
#endif
  img_pix = (uint16_t *) img + (int)img_offset;
/*
      + (y_off * img_height / dst_height) * img_width
      + (x_off * img_width / dst_width);
*/

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  rlelen = rle_remainder = rle_this_bite = 0;
  rle_remainder = rlelen = rle->len;
  clr_next = rle->color;
  rle++;
  y = dy = 0;
  x = x1_scaled = x2_scaled = 0;

#ifdef LOG_BLEND_RGB16
  printf("blend_rgb16 started\n");
#endif

  while (zone_state != 6) {
    clr = clr_next;
    switch (zone_state) {
    case 0:  /* Starting */
      /* FIXME: Get libspudec to set hili_top to -1 if no button */
      if (img_overl->hili_top < 0) {
#ifdef LOG_BLEND_RGB16
        printf("blend_rgb16: No button highlight area\n");
#endif

        zone_state = 7;
        break;
      }
#ifdef LOG_BLEND_RGB16
      printf("blend_rgb16: Button highlight area found. (%d,%d) .. (%d,%d)\n",
        img_overl->hili_left,
        img_overl->hili_top,
        img_overl->hili_right,
        img_overl->hili_bottom);
#endif
      if (y < img_overl->hili_top) {
        zone_state = 1;
        break;
      } else if (y >= img_overl->hili_bottom) {
        zone_state = 5;
        break;
      } else if (x < hili_left) {
        zone_state = 2;
        break;
      } else if (x >= hili_right) {
        zone_state = 4;
        break;
      } else {
        zone_state = 3;
        break;
      }
      break;
    case 1:  /* Above highlight area */
      clut = (clut_t*) img_overl->color;
      trans = img_overl->trans;
      o   = trans[clr];
      rle_this_bite = rle_remainder;
      rle_remainder = 0;
      rlelen -= rle_this_bite;
      /*printf("(x,y) = (%03i,%03i), clr=%03x, len=%03i, zone=%i\n", x, y, clr, rle_this_bite, zone_state); */
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;

        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
      }
      rle_remainder = rlelen = rle->len;
      clr_next = rle->color;
      rle++;
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      if (y >= img_overl->hili_top) {
        zone_state = 2;
#ifdef LOG_BLEND_RGB16
        printf("blend_rgb16: Button highlight top reached. y=%i, top=%i\n",
                y, img_overl->hili_top);
#endif
        if (x >= hili_left) {
          zone_state = 3;
          if (x >= hili_right) {
            zone_state = 4;
          }
        }
      }
      break;
    case 2:  /* Left of button */
      clut = (clut_t*) img_overl->color;
      trans = img_overl->trans;
      o   = trans[clr];
      if (x + rle_remainder <= hili_left) {
        rle_this_bite = rle_remainder;
        rle_remainder = rlelen = rle->len;
        clr_next = rle->color;
        rle++;
      } else {
        rle_this_bite = hili_left - x;
        rle_remainder -= rle_this_bite;
        zone_state = 3;
      }
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;
        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
        if (y >= img_overl->hili_bottom) {
          zone_state = 5;
          break;
        }
      }
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      break;
    case 3:  /* In button */
      clut = (clut_t*) img_overl->hili_color;
      trans = img_overl->hili_trans;
      o   = trans[clr];
      if (x + rle_remainder <= hili_right) {
        rle_this_bite = rle_remainder;
        rle_remainder = rlelen = rle->len;
        clr_next = rle->color;
        rle++;
      } else {
        rle_this_bite = hili_right - x;
        rle_remainder -= rle_this_bite;
        zone_state = 4;
      }
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;
        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
        if (y >= img_overl->hili_bottom) {
          zone_state = 5;
          break;
        }
      }
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      break;
    case 4:  /* Right of button */
      clut = (clut_t*) img_overl->color;
      trans = img_overl->trans;
      o   = trans[clr];
      if (x + rle_remainder <= src_width) {
        rle_this_bite = rle_remainder;
        rle_remainder = rlelen = rle->len;
        clr_next = rle->color;
        rle++;
      } else {
        rle_this_bite = src_width - x;
        rle_remainder -= rle_this_bite;
        zone_state = 2;
      }
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;
        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
        if (y >= img_overl->hili_bottom) {
          zone_state = 5;
          break;
        }
      }
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      break;
    case 5:  /* Below button */
      clut = (clut_t*) img_overl->color;
      trans = img_overl->trans;
      o   = trans[clr];
      rle_this_bite = rle_remainder;
      rle_remainder = 0;
      rlelen -= rle_this_bite;
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;
        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
      }
      rle_remainder = rlelen = rle->len;
      clr_next = rle->color;
      rle++;
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      break;
    case 6:  /* Finished */
      _x_abort();

    case 7:  /* No button */
      clut = (clut_t*) img_overl->color;
      trans = img_overl->trans;
      o   = trans[clr];
      rle_this_bite = rle_remainder;
      rle_remainder = 0;
      rlelen -= rle_this_bite;
      if (o) {
        x1_scaled = SCALED_TO_INT( x * x_scale );
        x2_scaled = SCALED_TO_INT( (x + rle_this_bite) * x_scale);
        mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }
      x += rle_this_bite;
      if (x >= src_width ) {
        x -= src_width;
        img_pix += img_width;
        dy += dy_step;
        if (dy >= INT_TO_SCALED(1)) {
          dy -= INT_TO_SCALED(1);
          ++y;
          while (dy >= INT_TO_SCALED(1)) {
            rle = rle_img_advance_line(rle, rle_limit, src_width);
            dy -= INT_TO_SCALED(1);
            ++y;
          }
          rle_start = rle;
        } else {
          rle = rle_start;          /* y-scaling, reuse the last rle encoded line */
        }
      }
      rle_remainder = rlelen = rle->len;
      clr_next = rle->color;
      rle++;
      if (rle >= rle_limit) {
        zone_state = 6;
      }
      break;
    default:
      ;
    }
  }
#ifdef LOG_BLEND_RGB16
  printf("blend_rgb16 ended\n");
#endif

}
#endif

void _x_blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data)
{
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;
  uint8_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = img + 2 * (  (y_off * img_height / dst_height) * img_width
		       + (x_off * img_width  / dst_width));

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  for (dy = y = 0; y < src_height && rle < rle_limit; ) {
    int mask = !(y < img_overl->hili_top || y >= img_overl->hili_bottom);
    rle_elem_t *rle_start = rle;

    int rlelen = 0;
    uint8_t clr = 0;

    for (x = x1_scaled = 0; x < src_width;) {
      int rle_bite;
      uint32_t *colors;
      uint8_t *trans;
      uint16_t o;
      int clipped = (y < clip_top);

      /* take next element from rle list everytime an element is finished */
      if (rlelen <= 0) {
        if (rle >= rle_limit)
          break;

        rlelen = rle->len;
        clr = rle->color;
        rle++;
      }

      if (!mask) {
        /* above or below highlight area */

        rle_bite = rlelen;
        /* choose palette for surrounding area */
        colors = img_overl->color;
        trans = img_overl->trans;
      } else {
        /* treat cases where highlight border is inside rle->len pixels */
        if ( x < hili_left ) {
          /* starts left */
          if( x + rlelen > hili_left ) {
            /* ends not left */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_left - x;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          } else {
            /* ends left */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          }
          if( x < clip_left )
            clipped = 1;
        } else if( x + rlelen > hili_right ) {
          /* ends right */
          if( x < hili_right ) {
            /* starts not right */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_right - x;
            /* we're in the center area so choose highlight palette */
            colors = img_overl->hili_color;
            trans = img_overl->hili_trans;
          } else {
            /* starts right */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;

            if( x + rle_bite >= clip_right )
              clipped = 1;
          }
        } else {
          /* starts not left and ends not right */

          rle_bite = rlelen;
          /* we're in the center area so choose highlight palette */
          colors = img_overl->hili_color;
          trans = img_overl->hili_trans;
        }
      }

      x2_scaled = SCALED_TO_INT((x + rle_bite) * x_scale);

      o = trans[clr];
      if (o && !clipped) {
        mem_blend16((uint16_t *) (img_pix + x1_scaled*2),
                    *((uint16_t *)&colors[clr]),
                    o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle_bite;
      rlelen -= rle_bite;
    }

    img_pix += img_width * 2;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
  }
}

void _x_blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data)
{
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;
  uint8_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = img + 3 * (  (y_off * img_height / dst_height) * img_width
		       + (x_off * img_width  / dst_width));

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  for (dy = y = 0; y < src_height && rle < rle_limit; ) {
    int mask = !(y < img_overl->hili_top || y >= img_overl->hili_bottom);
    rle_elem_t *rle_start = rle;

    int rlelen = 0;
    uint8_t clr = 0;

    for (x = x1_scaled = 0; x < src_width;) {
      int rle_bite;
      uint32_t *colors;
      uint8_t *trans;
      uint16_t o;
      int clipped = (y < clip_top);

      /* take next element from rle list everytime an element is finished */
      if (rlelen <= 0) {
        if (rle >= rle_limit)
          break;

        rlelen = rle->len;
        clr = rle->color;
        rle++;
      }

      if (!mask) {
        /* above or below highlight area */

        rle_bite = rlelen;
        /* choose palette for surrounding area */
        colors = img_overl->color;
        trans = img_overl->trans;
      } else {
        /* treat cases where highlight border is inside rle->len pixels */
        if ( x < hili_left ) {
          /* starts left */
          if( x + rlelen > hili_left ) {
            /* ends not left */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_left - x;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          } else {
            /* ends left */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          }
          if( x < clip_left )
            clipped = 1;
        } else if( x + rlelen > hili_right ) {
          /* ends right */
          if( x < hili_right ) {
            /* starts not right */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_right - x;
            /* we're in the center area so choose highlight palette */
            colors = img_overl->hili_color;
            trans = img_overl->hili_trans;
          } else {
            /* starts right */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;

            if( x + rle_bite >= clip_right )
              clipped = 1;
          }
        } else {
          /* starts not left and ends not right */

          rle_bite = rlelen;
          /* we're in the center area so choose highlight palette */
          colors = img_overl->hili_color;
          trans = img_overl->hili_trans;
        }
      }

      x2_scaled = SCALED_TO_INT((x + rle_bite) * x_scale);

      o = trans[clr];
      if (o && !clipped) {
        union {
          uint32_t u32;
          clut_t   c;
        } color = {colors[clr]};

        mem_blend24(img_pix + x1_scaled*3,
                    color.c.cb, color.c.cr, color.c.y,
                    o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle_bite;
      rlelen -= rle_bite;
    }

    img_pix += img_width * 3;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
  }
}

void _x_blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height,
                  alphablend_t *extra_data)
{
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;
  uint8_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = img + 4 * (  (y_off * img_height / dst_height) * img_width
		       + (x_off * img_width / dst_width));

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  for (y = dy = 0; y < src_height && rle < rle_limit; ) {
    int mask = !(y < img_overl->hili_top || y >= img_overl->hili_bottom);
    rle_elem_t *rle_start = rle;

    int rlelen = 0;
    uint8_t clr = 0;

    for (x = x1_scaled = 0; x < src_width;) {
      int rle_bite;
      uint32_t *colors;
      uint8_t *trans;
      uint16_t o;
      int clipped = (y < clip_top);

      /* take next element from rle list everytime an element is finished */
      if (rlelen <= 0) {
        if (rle >= rle_limit)
          break;

        rlelen = rle->len;
        clr = rle->color;
        rle++;
      }

      if (!mask) {
        /* above or below highlight area */

        rle_bite = rlelen;
        /* choose palette for surrounding area */
        colors = img_overl->color;
        trans = img_overl->trans;
      } else {
        /* treat cases where highlight border is inside rle->len pixels */
        if ( x < hili_left ) {
          /* starts left */
          if( x + rlelen > hili_left ) {
            /* ends not left */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_left - x;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          } else {
            /* ends left */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;
          }
          if( x < clip_left )
            clipped = 1;
        } else if( x + rlelen > hili_right ) {
          /* ends right */
          if( x < hili_right ) {
            /* starts not right */

            /* choose the largest "bite" up to palette change */
            rle_bite = hili_right - x;
            /* we're in the center area so choose highlight palette */
            colors = img_overl->hili_color;
            trans = img_overl->hili_trans;
          } else {
            /* starts right */

            rle_bite = rlelen;
            /* choose palette for surrounding area */
            colors = img_overl->color;
            trans = img_overl->trans;

            if( x + rle_bite >= clip_right )
              clipped = 1;
          }
        } else {
          /* starts not left and ends not right */

          rle_bite = rlelen;
          /* we're in the center area so choose highlight palette */
          colors = img_overl->hili_color;
          trans = img_overl->hili_trans;
        }
      }

      x2_scaled = SCALED_TO_INT((x + rle_bite) * x_scale);

      o = trans[clr];
      if (o && !clipped) {
        mem_blend32(img_pix + x1_scaled*4, (uint8_t *)&colors[clr], o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle_bite;
      rlelen -= rle_bite;
    }

    img_pix += img_width * 4;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
  }
}

static void blend_yuv_exact(uint8_t *dst_cr, uint8_t *dst_cb, int src_width,
                            uint8_t *(*blend_yuv_data)[ 3 ][ 2 ])
{
  int x;

  for (x = 0; x < src_width; x += 2) {
    /* get opacity of the 4 pixels that share chroma */
    int o00 = (*blend_yuv_data)[ 0 ][ 0 ][ x + 0 ];
    int o01 = (*blend_yuv_data)[ 0 ][ 0 ][ x + 1 ];
    int o = o00 + o01;
    int o10 = (*blend_yuv_data)[ 0 ][ 1 ][ x + 0 ];
    o += o10;
    int o11 = (*blend_yuv_data)[ 0 ][ 1 ][ x + 1 ];
    o += o11;

    /* are there any pixels a little bit opaque? */
    if (o) {
      /* get the chroma components of the 4 pixels */
      int cr00 = (*blend_yuv_data)[ 1 ][ 0 ][ x + 0 ];
      int cr01 = (*blend_yuv_data)[ 1 ][ 0 ][ x + 1 ];
      int cr10 = (*blend_yuv_data)[ 1 ][ 1 ][ x + 0 ];
      int cr11 = (*blend_yuv_data)[ 1 ][ 1 ][ x + 1 ];

      int cb00 = (*blend_yuv_data)[ 2 ][ 0 ][ x + 0 ];
      int cb01 = (*blend_yuv_data)[ 2 ][ 0 ][ x + 1 ];
      int cb10 = (*blend_yuv_data)[ 2 ][ 1 ][ x + 0 ];
      int cb11 = (*blend_yuv_data)[ 2 ][ 1 ][ x + 1 ];

      /* are all pixels completely opaque? */
      if (o >= 4*0xf) {
        /* set the output chroma to the average of the four pixels */
        *dst_cr = (cr00 + cr01 + cr10 + cr11) / 4;
        *dst_cb = (cb00 + cb01 + cb10 + cb11) / 4;
      } else {
        /* calculate transparency of background over the four pixels */
        int t4 = 4*0xf - o;

        /* blend the output chroma to the average of the four pixels */
        /* for explanation of the used equation, see blend_yuy2_exact() */
        *dst_cr = ((*dst_cr * t4 + cr00 * o00 + cr01 * o01 + cr10 * o10 + cr11 * o11) * (0x1111+1)) >> 18;
        *dst_cb = ((*dst_cb * t4 + cb00 * o00 + cb01 * o01 + cb10 * o10 + cb11 * o11) * (0x1111+1)) >> 18;
      }
    }

    /* next chroma sample */
    dst_cr++;
    dst_cb++;
  }
}

static uint8_t *(*blend_yuv_grow_extra_data(alphablend_t *extra_data, int osd_width))[ 3 ][ 2 ]
{
  struct XINE_PACKED header_s {
    int id;
    int max_width;
    uint8_t *data[ 3 ][ 2 ];
  } *header = (struct header_s *)extra_data->buffer;

  /* align buffers to 16 bytes */
  size_t header_size = (sizeof(*header) + 15) & (~15);
  size_t alloc_width = (osd_width + 15) & (~15);
  size_t needed_buffer_size = 16 + header_size + alloc_width * sizeof (uint8_t[ 3 ][ 2 ]);

  if (extra_data->buffer_size < needed_buffer_size) {

    _x_freep(&extra_data->buffer);
    header = calloc(1, needed_buffer_size);
    if (!header) {
      extra_data->buffer_size = 0;
      return 0;
    }
    extra_data->buffer_size = needed_buffer_size;
    extra_data->buffer = header;
    header->max_width = 0;
  }

  if (header->id != ME_FOURCC('y', 'u', 'v', 0) || header->max_width < osd_width) {
    header->id = ME_FOURCC('y', 'u', 'v', 0);
    header->max_width = osd_width;

    header->data[ 0 ][ 0 ] = ((uint8_t *)extra_data->buffer) + header_size;
    header->data[ 0 ][ 1 ] = header->data[ 0 ][ 0 ] + alloc_width;
    header->data[ 1 ][ 0 ] = header->data[ 0 ][ 1 ] + alloc_width;
    header->data[ 1 ][ 1 ] = header->data[ 1 ][ 0 ] + alloc_width;
    header->data[ 2 ][ 0 ] = header->data[ 1 ][ 1 ] + alloc_width;
    header->data[ 2 ][ 1 ] = header->data[ 2 ][ 0 ] + alloc_width;
  }

  return &(header->data);
}

void _x_blend_yuv (uint8_t *dst_base[3], vo_overlay_t * img_overl,
                int dst_width, int dst_height, int dst_pitches[3],
                alphablend_t *extra_data)
{
  int enable_exact_blending = !extra_data->disable_exact_blending;
  uint32_t *my_clut;
  uint8_t *my_trans;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x_odd = x_off & 1;
  int y_odd = y_off & 1;
  int ymask,xmask;
  int rle_this_bite;
  int rle_remainder;
  int rlelen;
  int x, y;
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;
  uint8_t clr=0;

  int any_line_buffered = 0;
  int exact_blend_width = ((src_width <= (dst_width - x_off)) ? src_width : (dst_width - x_off));
  int exact_blend_width_m2 = (x_odd + exact_blend_width + 1) & ~1; /* make it a (larger) multiple of 2 */
  uint8_t *(*blend_yuv_data)[ 3 ][ 2 ] = 0;

  uint8_t *dst_y = dst_base[0] + dst_pitches[0] * y_off + x_off;
  uint8_t *dst_cr = dst_base[2] + (y_off / 2) * dst_pitches[1] + (x_off / 2);
  uint8_t *dst_cb = dst_base[1] + (y_off / 2) * dst_pitches[2] + (x_off / 2);
#ifdef LOG_BLEND_YUV
  printf("overlay_blend started x=%d, y=%d, w=%d h=%d\n",img_overl->x,img_overl->y,img_overl->width,img_overl->height);
#endif
  my_clut = img_overl->hili_color;
  my_trans = img_overl->hili_trans;

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  if (src_height <= 0)
    return;

  if (enable_exact_blending) {
    if (exact_blend_width <= 0)
      return;

    blend_yuv_data = blend_yuv_grow_extra_data(extra_data, exact_blend_width_m2);
    if (!blend_yuv_data)
      return;

    /* make linebuffer transparent */
    memset(&(*blend_yuv_data)[ 0 ][ 0 ][ 0 ], 0, exact_blend_width_m2);
    memset(&(*blend_yuv_data)[ 0 ][ 1 ][ 0 ], 0, exact_blend_width_m2);
  }

  rlelen=rle_remainder=0;
  for (y = 0; y < src_height; y++) {
    if (rle >= rle_limit) {
#ifdef LOG_BLEND_YUV
      printf("y-rle_limit\n");
#endif
      break;
    }

    ymask = ((y < img_overl->hili_top) || (y >= img_overl->hili_bottom));
    xmask = 0;
#ifdef LOG_BLEND_YUV
    printf("X started ymask=%d y=%d src_height=%d\n",ymask, y, src_height);
#endif

    for (x = 0; x < src_width;) {
      uint16_t o;
      int clipped = (y < clip_top);

      if (rle >= rle_limit) {
#ifdef LOG_BLEND_YUV
        printf("x-rle_limit\n");
#endif
        break;
      }

#ifdef LOG_BLEND_YUV
      printf("1:rle_len=%d, remainder=%d, x=%d\n",rlelen, rle_remainder, x);
#endif

      if ((rlelen < 0) || (rle_remainder < 0)) {
#ifdef LOG_BLEND_YUV
        printf("alphablend: major bug in blend_yuv < 0\n");
#endif
      }
      if (rlelen == 0) {
        rle_remainder = rlelen = rle->len;
        clr = rle->color;
        rle++;
      }
      if (rle_remainder == 0) {
        rle_remainder = rlelen;
      }
      if ((rle_remainder + x) > src_width) {
        /* Do something for long rlelengths */
        rle_remainder = src_width - x;
      }
#ifdef LOG_BLEND_YUV
      printf("2:rle_len=%d, remainder=%d, x=%d\n",rlelen, rle_remainder, x);
#endif

      if (ymask == 0) {
        if (x < hili_left) {
          /* Starts outside highlight area */
          if ((x + rle_remainder) > hili_left ) {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight left %d, ending inside\n", hili_left);
#endif
            /* Cutting needed, starts outside, ends inside */
            rle_this_bite = (hili_left - x);
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
            xmask = 0;
          } else {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight left %d, ending outside\n", hili_left);
#endif
          /* no cutting needed, starts outside, ends outside */
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
            xmask = 0;
          }
          if( x < clip_left )
            clipped = 1;
        } else if (x < hili_right) {
          /* Starts inside highlight area */
          if ((x + rle_remainder) > hili_right ) {
#ifdef LOG_BLEND_YUV
            printf("Inside highlight right %d, ending outside\n", hili_right);
#endif
            /* Cutting needed, starts inside, ends outside */
            rle_this_bite = (hili_right - x);
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->hili_color;
            my_trans = img_overl->hili_trans;
            xmask++;
          } else {
#ifdef LOG_BLEND_YUV
            printf("Inside highlight right %d, ending inside\n", hili_right);
#endif
          /* no cutting needed, starts inside, ends inside */
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->hili_color;
            my_trans = img_overl->hili_trans;
            xmask++;
          }
        } else if (x >= hili_right) {
          /* Starts outside highlight area, ends outside highlight area */
          if ((x + rle_remainder ) > src_width ) {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight right %d, ending eol\n", hili_right);
#endif
            /* Cutting needed, starts outside, ends at right edge */
            /* It should never reach here due to the earlier test of src_width */
            rle_this_bite = (src_width - x );
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
            xmask = 0;
          } else {
          /* no cutting needed, starts outside, ends outside */
#ifdef LOG_BLEND_YUV
            printf("Outside highlight right %d, ending outside\n", hili_right);
#endif
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
            xmask = 0;
          }
          if( x + rle_this_bite >= clip_right )
            clipped = 1;
        }
      } else {
        /* Outside highlight are due to y */
        /* no cutting needed, starts outside, ends outside */
        rle_this_bite = rle_remainder;
        rle_remainder = 0;
        rlelen -= rle_this_bite;
        my_clut = img_overl->color;
        my_trans = img_overl->trans;
        xmask = 0;
      }
      o   = my_trans[clr];
#ifdef LOG_BLEND_YUV
      printf("Trans=%d clr=%d xmask=%d my_clut[clr]=%d\n",o, clr, xmask, my_clut[clr].y);
#endif

      if (x < (dst_width - x_off)) {
        /* clip against right edge of destination area */
        if ((x + rle_this_bite) > (dst_width - x_off)) {
          int toClip = (x + rle_this_bite) - (dst_width - x_off);

          rle_this_bite -= toClip;
          rle_remainder += toClip;
          rlelen += toClip;
        }

        if (enable_exact_blending) {
          /* remember opacity of current line */
          memset(&(*blend_yuv_data)[ 0 ][ (y + y_odd) & 1 ][ x + x_odd ], o, rle_this_bite);
          any_line_buffered |= ((y + y_odd) & 1) ? 2 : 1;
        }

        if (o && !clipped) {
          union {
            uint32_t u32;
            clut_t   c;
          } color = {my_clut[clr]};

          if(o >= 15) {
            memset(dst_y + x, color.c.y, rle_this_bite);
            if (!enable_exact_blending) {
              if ((y + y_odd) & 1) {
                memset(dst_cr + ((x + x_odd) >> 1), color.c.cr, (rle_this_bite+1) >> 1);
                memset(dst_cb + ((x + x_odd) >> 1), color.c.cb, (rle_this_bite+1) >> 1);
              }
            }
          } else {
            mem_blend8(dst_y + x, color.c.y, o, rle_this_bite);
            if (!enable_exact_blending) {
              if ((y + y_odd) & 1) {
                /* Blending cr and cb should use a different function, with pre -128 to each sample */
                mem_blend8(dst_cr + ((x + x_odd) >> 1), color.c.cr, o, (rle_this_bite+1) >> 1);
                mem_blend8(dst_cb + ((x + x_odd) >> 1), color.c.cb, o, (rle_this_bite+1) >> 1);
              }
            }
          }

          if (enable_exact_blending) {
            /* remember chroma of current line */
            memset(&(*blend_yuv_data)[ 1 ][ (y + y_odd) & 1 ][ x + x_odd ], color.c.cr, rle_this_bite);
            memset(&(*blend_yuv_data)[ 2 ][ (y + y_odd) & 1 ][ x + x_odd ], color.c.cb, rle_this_bite);
          }
        }
      }
#ifdef LOG_BLEND_YUV
      printf("rle_this_bite=%d, remainder=%d, x=%d\n",rle_this_bite, rle_remainder, x);
#endif
      x += rle_this_bite;
    }

    if ((y + y_odd) & 1) {
      if (enable_exact_blending) {
        /* blend buffered lines */
        if (any_line_buffered) {
          if (!(any_line_buffered & 2)) {
            /* make second line transparent */
            memset(&(*blend_yuv_data)[ 0 ][ 1 ][ 0 ], 0, exact_blend_width_m2);
          }

          blend_yuv_exact(dst_cr, dst_cb, exact_blend_width, blend_yuv_data);

          any_line_buffered = 0;
        }
      }

      dst_cr += dst_pitches[2];
      dst_cb += dst_pitches[1];
    }

    dst_y += dst_pitches[0];
  }

  if (enable_exact_blending) {
    /* blend buffered lines */
    if (any_line_buffered) {
      if (!(any_line_buffered & 2)) {
        /* make second line transparent */
        memset(&(*blend_yuv_data)[ 0 ][ 1 ][ 0 ], 0, exact_blend_width_m2);
      }

      blend_yuv_exact(dst_cr, dst_cb, exact_blend_width, blend_yuv_data);
    }
  }

#ifdef LOG_BLEND_YUV
  printf("overlay_blend ended\n");
#endif
}

static void blend_yuy2_exact(uint8_t *dst_cr, uint8_t *dst_cb, int src_width,
                             uint8_t *(*blend_yuy2_data)[ 3 ])
{
  int x;

  for (x = 0; x < src_width; x += 2) {
    /* get opacity of the 2 pixels that share chroma */
    int o0 = (*blend_yuy2_data)[ 0 ][ x + 0 ];
    int o1 = (*blend_yuy2_data)[ 0 ][ x + 1 ];
    int o = o0 + o1;

    /* are there any pixels a little bit opaque? */
    if (o) {
      /* get the chroma components of the 2 pixels */
      int cr0 = (*blend_yuy2_data)[ 1 ][ x + 0 ];
      int cr1 = (*blend_yuy2_data)[ 1 ][ x + 1 ];

      int cb0 = (*blend_yuy2_data)[ 2 ][ x + 0 ];
      int cb1 = (*blend_yuy2_data)[ 2 ][ x + 1 ];

      /* are all pixels completely opaque? */
      if (o >= 2*0xf) {
        /* set the output chroma to the average of the two pixels */
        *dst_cr = (cr0 + cr1) / 2;
        *dst_cb = (cb0 + cb1) / 2;
      } else {
        /* calculate transparency of background over the two pixels */
        int t2 = 2*0xf - o;

	/*
	 * No need to adjust chroma values with +/- 128:
	 *   *dst_cb
	 *   = 128 + ((*dst_cb-128) * t2 + (cb0-128) * o0 + (cb1-128) * o1) / (2 * 0xf);
	 *   = 128 + (*dst_cb * t2 + cb0 * o0 + cb1 * o1 + (t2*(-128) - 128*o0 - 128*o1)) / (2 * 0xf);
	 *   = 128 + (*dst_cb * t2 + cb0 * o0 + cb1 * o1 + ((2*0xf-o0-o1)*(-128) - 128*o0 - 128*o1)) / (2 * 0xf);
	 *   = 128 + (*dst_cb * t2 + cb0 * o0 + cb1 * o1 + (2*0xf*(-128))) / (2 * 0xf);
	 *   = 128 + (*dst_cb * t2 + cb0 * o0 + cb1 * o1) / (2 * 0xf) - 128;
	 *   =       (*dst_cb * t2 + cb0 * o0 + cb1 * o1) / (2 * 0xf);
	 *
	 * Convert slow divisions to multiplication and shift:
	 *     X/0xf
	 *   = X * (1/0xf)
	 *   = X * (0x1111/0x1111) * (1/0xf)
	 *   = X * 0x1111/0xffff
	 *   =(almost) X * 0x1112/0x10000
	 *   = (X * 0x1112) >> 16
	 *
	 * The tricky point is 0x1111/0xffff --> 0x1112/0x10000.
	 * All calculations are done using integers and X is in
	 * range of [0 ... 0xff*0xf*4]. This results in error of
	 *     X*0x1112/0x10000 - X/0xf
	 *   = X*(0x1112/0x10000 - 1/0xf)
	 *   = X*(0x0.1112 - 0x0.111111...)
	 *   = X*0.0000eeeeee....
	 *   = [0 ... 0.37c803fc...]    when X in [0...3bc4]
	 * As the error is less than 1 and always positive, whole error
	 * "disappears" during truncation (>>16). Rounding to exact results is
	 * guaranteed by selecting 0x1112 instead of more accurate 0x1111
	 * (with 0x1111 error=X*(-0.00001111...)). With 0x1112 error is
	 * always positive, but still less than one.
	 * So, one can forget the "=(almost)" as it is really "=" when source
	 * operands are within 0...0xff (U,V) and 0...0xf (A).
	 *
	 * 1/0x10000 (= >>16) was originally selected because of MMX pmullhw
	 * instruction; it makes possible to do whole calculation in MMX using
	 * uint16's (pmullhw is (X*Y)>>16).
	 *
	 * Here X/(2*0xf) = X/0xf/2 = ((X*0x1112)>>16)>>1 = (X*0x1112)>>17
	 */

        /* blend the output chroma to the average of the two pixels */
        /* *dst_cr = 128 + ((*dst_cr-128) * t2 + (cr0-128) * o0 + (cr1-128) * o1) / (2 * 0xf); */
        *dst_cr = ((*dst_cr * t2 + cr0 * o0 + cr1 * o1) * (0x1111+1)) >> 17;
        *dst_cb = ((*dst_cb * t2 + cb0 * o0 + cb1 * o1) * (0x1111+1)) >> 17;
      }
    }

    /* next chroma sample */
    dst_cr += 4;
    dst_cb += 4;
  }
}

static uint8_t *(*blend_yuy2_grow_extra_data(alphablend_t *extra_data, int osd_width))[ 3 ]
{
  struct XINE_PACKED header_s {
    int id;
    int max_width;
    uint8_t *data[ 3 ];
  } *header = (struct header_s *)extra_data->buffer;

  /* align buffers to 16 bytes */
  size_t header_size = (sizeof(*header) + 15) & (~15);
  size_t alloc_width = (osd_width + 15) & (~15);
  size_t needed_buffer_size = 16 + header_size + alloc_width * sizeof (uint8_t[ 3 ]);

  if (extra_data->buffer_size < needed_buffer_size) {

    _x_freep(&extra_data->buffer);
    header = calloc(1, needed_buffer_size);
    if (!header) {
      extra_data->buffer_size = 0;
      return 0;
    }
    extra_data->buffer_size = needed_buffer_size;
    extra_data->buffer = header;
    header->max_width = 0;
  }

  if (header->id != ME_FOURCC('y', 'u', 'y', '2') || header->max_width < osd_width) {
    header->id = ME_FOURCC('y', 'u', 'y', '2');
    header->max_width = osd_width;

    header->data[ 0 ] = ((uint8_t *)extra_data->buffer) + header_size;
    header->data[ 1 ] = header->data[ 0 ] + alloc_width;
    header->data[ 2 ] = header->data[ 1 ] + alloc_width;
  }

  return &(header->data);
}

void _x_blend_yuy2 (uint8_t * dst_img, vo_overlay_t * img_overl,
                 int dst_width, int dst_height, int dst_pitch,
                 alphablend_t *extra_data)
{
  int enable_exact_blending = !extra_data->disable_exact_blending;
  uint32_t *my_clut;
  uint8_t *my_trans;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x + extra_data->offset_x;
  int y_off = img_overl->y + extra_data->offset_y;
  int x_odd = x_off & 1;
  int ymask;
  int rle_this_bite;
  int rle_remainder;
  int rlelen;
  int x, y;
  int l = 0;
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;

  union {
    uint32_t value;
    uint8_t  b[4];
    uint16_t h[2];
  } yuy2;

  uint8_t clr = 0;

  int any_line_buffered = 0;
  int exact_blend_width = ((src_width <= (dst_width - x_off)) ? src_width : (dst_width - x_off));
  int exact_blend_width_m2 = (x_odd + exact_blend_width + 1) & ~1; /* make it a (larger) multiple of 2 */
  uint8_t *(*blend_yuy2_data)[ 3 ] = 0;

  uint8_t *dst_y = dst_img + dst_pitch * y_off + 2 * x_off;
  uint8_t *dst;

  my_clut = img_overl->hili_color;
  my_trans = img_overl->hili_trans;

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  if (src_height <= 0)
    return;

  if (enable_exact_blending) {
    if (exact_blend_width <= 0)
      return;

    blend_yuy2_data = blend_yuy2_grow_extra_data(extra_data, exact_blend_width_m2);
    if (!blend_yuy2_data)
      return;

    /* make linebuffer transparent */
    memset(&(*blend_yuy2_data)[ 0 ][ 0 ], 0, exact_blend_width_m2);
  }

  rlelen=rle_remainder=0;
  for (y = 0; y < src_height; y++) {
    if (rle >= rle_limit)
      break;

    ymask = ((y < img_overl->hili_top) || (y >= img_overl->hili_bottom));

    dst = dst_y;
    for (x = 0; x < src_width;) {
      uint16_t o;
      int clipped = (y < clip_top);

      if (rle >= rle_limit)
        break;

      if ((rlelen < 0) || (rle_remainder < 0)) {
#ifdef LOG_BLEND_YUV
        printf("alphablend: major bug in blend_yuv < 0\n");
#endif
      }
      if (rlelen == 0) {
        rle_remainder = rlelen = rle->len;
        clr = rle->color;
        rle++;
      }
      if (rle_remainder == 0) {
        rle_remainder = rlelen;
      }
      if ((rle_remainder + x) > src_width) {
        /* Do something for long rlelengths */
        rle_remainder = src_width - x;
      }
#ifdef LOG_BLEND_YUV
      printf("2:rle_len=%d, remainder=%d, x=%d\n",rlelen, rle_remainder, x);
#endif

      if (ymask == 0) {
        if (x < hili_left) {
          /* Starts outside highlight area */
          if ((x + rle_remainder) > hili_left ) {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight left %d, ending inside\n", hili_left);
#endif
            /* Cutting needed, starts outside, ends inside */
            rle_this_bite = (hili_left - x);
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
          } else {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight left %d, ending outside\n", hili_left);
#endif
          /* no cutting needed, starts outside, ends outside */
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
          }
          if( x < clip_left )
            clipped = 1;
        } else if (x < hili_right) {
          /* Starts inside highlight area */
          if ((x + rle_remainder) > hili_right ) {
#ifdef LOG_BLEND_YUV
            printf("Inside highlight right %d, ending outside\n", hili_right);
#endif
            /* Cutting needed, starts inside, ends outside */
            rle_this_bite = (hili_right - x);
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->hili_color;
            my_trans = img_overl->hili_trans;
          } else {
#ifdef LOG_BLEND_YUV
            printf("Inside highlight right %d, ending inside\n", hili_right);
#endif
          /* no cutting needed, starts inside, ends inside */
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->hili_color;
            my_trans = img_overl->hili_trans;
          }
        } else if (x >= hili_right) {
          /* Starts outside highlight area, ends outsite highlight area */
          if ((x + rle_remainder ) > src_width ) {
#ifdef LOG_BLEND_YUV
            printf("Outside highlight right %d, ending eol\n", hili_right);
#endif
            /* Cutting needed, starts outside, ends at right edge */
            /* It should never reach here due to the earlier test of src_width */
            rle_this_bite = (src_width - x );
            rle_remainder -= rle_this_bite;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
          } else {
          /* no cutting needed, starts outside, ends outside */
#ifdef LOG_BLEND_YUV
            printf("Outside highlight right %d, ending outside\n", hili_right);
#endif
            rle_this_bite = rle_remainder;
            rle_remainder = 0;
            rlelen -= rle_this_bite;
            my_clut = img_overl->color;
            my_trans = img_overl->trans;
          }
          if( x + rle_this_bite >= clip_right )
            clipped = 1;
        }
      } else {
        /* Outside highlight are due to y */
        /* no cutting needed, starts outside, ends outside */
        rle_this_bite = rle_remainder;
        rle_remainder = 0;
        rlelen -= rle_this_bite;
        my_clut = img_overl->color;
        my_trans = img_overl->trans;
      }
      o   = my_trans[clr];

      if (x < (dst_width - x_off)) {
        /* clip against right edge of destination area */
        if ((x + rle_this_bite) > (dst_width - x_off)) {
          int toClip = (x + rle_this_bite) - (dst_width - x_off);

          rle_this_bite -= toClip;
          rle_remainder += toClip;
          rlelen += toClip;
        }

        if (enable_exact_blending) {
          /* remember opacity of current line */
          memset(&(*blend_yuy2_data)[ 0 ][ x + x_odd ], o, rle_this_bite);
          any_line_buffered = 1;
        }

        if (o && !clipped) {
          union {
            uint32_t u32;
            clut_t   c;
          } color = {my_clut[clr]};

          if (!enable_exact_blending) {
            l = rle_this_bite>>1;
            if( !((x_odd+x) & 1) ) {
              yuy2.b[0] = color.c.y;
              yuy2.b[1] = color.c.cb;
              yuy2.b[2] = color.c.y;
              yuy2.b[3] = color.c.cr;
            } else {
              yuy2.b[0] = color.c.y;
              yuy2.b[1] = color.c.cr;
              yuy2.b[2] = color.c.y;
              yuy2.b[3] = color.c.cb;
            }
          }
	  if (o >= 15) {
            if (!enable_exact_blending) {
              while(l--) {
                *(uint16_t *)dst = yuy2.h[0];
                dst += 2;
                *(uint16_t *)dst = yuy2.h[1];
                dst += 2;
              }
              if(rle_this_bite & 1) {
                *(uint16_t *)dst = yuy2.h[0];
                dst += 2;
              }
            } else {
              l = rle_this_bite;
              while (l--) {
                *dst = color.c.y;
                dst += 2;
              }
            }
          } else {
            if (!enable_exact_blending) {
              if( l ) {
                mem_blend32(dst, &yuy2.b[0], o, l);
                dst += 4*l;
              }

              if(rle_this_bite & 1) {
                *dst = BLEND_BYTE(*dst, yuy2.b[0], o);
                dst++;
                *dst = BLEND_BYTE(*dst, yuy2.b[1], o);
                dst++;
              }
            } else {
              l = rle_this_bite;
              while (l--) {
                *dst = BLEND_BYTE(*dst, color.c.y, o);
                dst += 2;
              }
            }
          }

          if (enable_exact_blending) {
            /* remember chroma of current line */
            memset(&(*blend_yuy2_data)[ 1 ][ x + x_odd ], color.c.cr, rle_this_bite);
            memset(&(*blend_yuy2_data)[ 2 ][ x + x_odd ], color.c.cb, rle_this_bite);
          }
        } else {
          dst += rle_this_bite*2;
        }
      }

      x += rle_this_bite;
    }

    if (enable_exact_blending) {
      /* blend buffered line */
      if (any_line_buffered) {
        blend_yuy2_exact(dst_y - x_odd * 2 + 3, dst_y - x_odd * 2 + 1, exact_blend_width, blend_yuy2_data);

        any_line_buffered = 0;
      }
    }

    dst_y += dst_pitch;
  }
}

void _x_clear_xx44_palette(xx44_palette_t *p)
{
  register int i;
  register uint32_t *cluts = p->cluts;
  register int *ids = p->lookup_cache;

  i= p->size;
  while(i--)
    *cluts++ = 0;
  i = 2*OVL_PALETTE_SIZE;
  while(i--)
    *ids++ = -1;
  p->max_used=1;
}

void _x_init_xx44_palette(xx44_palette_t *p, unsigned num_entries)
{
  p->size = (num_entries > XX44_PALETTE_SIZE) ? XX44_PALETTE_SIZE : num_entries;
}

void _x_dispose_xx44_palette(xx44_palette_t *p)
{
}

static void colorToPalette(const uint32_t *icolor, unsigned char *palette_p,
			   unsigned num_xvmc_components, const char *xvmc_components)
{
  const clut_t *color = (const clut_t *) icolor;
  unsigned int i;
  for (i=0; i<num_xvmc_components; ++i) {
    switch(xvmc_components[i]) {
    case 'V': *palette_p = color->cr; break;
    case 'U': *palette_p = color->cb; break;
    case 'Y':
    default:  *palette_p = color->y; break;
    }
    palette_p++;
  }
}


void _x_xx44_to_xvmc_palette(const xx44_palette_t *p,unsigned char *xvmc_palette,
			  unsigned first_xx44_entry, unsigned num_xx44_entries,
			  unsigned num_xvmc_components, const char *xvmc_components)
{
  register unsigned int i;
  register const uint32_t *cluts = p->cluts + first_xx44_entry;

  for (i=0; i<num_xx44_entries; ++i) {
    if ((cluts - p->cluts) < p->size) {
      colorToPalette(cluts++, xvmc_palette, num_xvmc_components, xvmc_components);
      xvmc_palette += num_xvmc_components;
    }
  }
}

static int xx44_paletteIndex(xx44_palette_t *p, int color, uint32_t clut)
{

  register unsigned int i;
  register uint32_t *cluts = p->cluts;
  register int tmp;

  if ((tmp = p->lookup_cache[color]) >= 0)
    if (cluts[tmp] == clut) return tmp;

  for (i=0; i<p->max_used; ++i) {
    if (*cluts++ == clut) return p->lookup_cache[color] = i;
  }

  if (p->max_used == p->size -1) {
    printf("video_out: Warning! Out of xx44 palette colours!\n");
    return 1;
  }
  p->cluts[p->max_used] = clut;
  return p->lookup_cache[color] = p->max_used++;
}

static void memblend_xx44(uint8_t *mem,uint8_t val, register size_t size, uint8_t mask)
{
  register uint8_t
    masked_val;

  if (0 == (masked_val = val & mask)) return;

  while(size--) {
    if ((*mem & mask) <= masked_val ) *mem = val;
    mem++;
  }
}

void _x_blend_xx44 (uint8_t *dst_img, vo_overlay_t *img_overl,
		int dst_width, int dst_height, int dst_pitch,
                alphablend_t *extra_data,
		xx44_palette_t *palette,int ia44)
{
  int src_width, src_height;
  rle_elem_t *rle, *rle_limit;
  int mask;
  int x_off, y_off;
  int x, y;
  uint8_t norm_pixel,hili_pixel;
  uint8_t *dst_y;
  uint8_t *dst;
  uint8_t alphamask = (ia44) ? 0x0F : 0xF0;
  int hili_right, hili_left;
  int clip_right, clip_left, clip_top;

  if (!img_overl)
    return;

  src_width  = img_overl->width;
  src_height = img_overl->height;
  rle        = img_overl->rle;
  rle_limit  = rle + img_overl->num_rle;
  x_off = img_overl->x + extra_data->offset_x;
  y_off = img_overl->y + extra_data->offset_y;

  dst_y = dst_img + dst_pitch*y_off + x_off;

  /* checks to avoid drawing overlay outside the destination buffer */
  if( (x_off + src_width) <= dst_width )
    clip_right = src_width;
  else
    clip_right = dst_width - x_off;

  if( x_off >= 0 )
    clip_left = 0;
  else
    clip_left = -x_off;

  if( y_off >= 0 )
    clip_top = 0;
  else
    clip_top = -y_off;

  if( (src_height + y_off) > dst_height )
    src_height = dst_height - y_off;

  /* make highlight area fit into clip area */
  if( img_overl->hili_right <= clip_right )
    hili_right = img_overl->hili_right;
  else
    hili_right = clip_right;

  if( img_overl->hili_left >= clip_left )
    hili_left = img_overl->hili_left;
  else
    hili_left = clip_left;

  for (y = 0; y < src_height; y++) {

    mask = !(y < img_overl->hili_top || y >= img_overl->hili_bottom);
    dst = dst_y;

    for (x = 0; x < src_width;) {
      int len = (x + rle->len > clip_right) ? clip_right - x : rle->len;
      int clipped = (y < clip_top);

      if (len > 0) {
	norm_pixel = (uint8_t)((xx44_paletteIndex(palette,rle->color,
					     img_overl->color[rle->color]) << 4) |
			       (img_overl->trans[rle->color] & 0x0F));
	hili_pixel = (uint8_t)((xx44_paletteIndex(palette,rle->color+OVL_PALETTE_SIZE,
					     img_overl->hili_color[rle->color]) << 4) |
			       (img_overl->hili_trans[rle->color] & 0x0F));
	if (!ia44) {
	  norm_pixel = ((norm_pixel & 0x0F) << 4) | ((norm_pixel & 0xF0) >> 4);
	  hili_pixel = ((hili_pixel & 0x0F) << 4) | ((hili_pixel & 0xF0) >> 4);
	}
	if (mask) {
	  if (x < hili_left) {
	    if (x < clip_left)
	      clipped = 1;

	    if (x + len <= hili_left) {
	      if(!clipped)
	        memblend_xx44(dst,norm_pixel,len, alphamask);
	      dst += len;
	    } else {
	      if(!clipped)
	        memblend_xx44(dst,norm_pixel,hili_left -x, alphamask);
	      dst += hili_left - x;
	      len -= hili_left - x;
	      if (len <= hili_right - hili_left) {
	        if(!clipped)
		  memblend_xx44(dst,hili_pixel,len, alphamask);
		dst += len;
	      } else {
	        if(!clipped)
		  memblend_xx44(dst,hili_pixel, hili_right - hili_left,
			        alphamask);
		dst += hili_right - hili_left;
		len -= hili_right - hili_left;
	        if(!clipped)
		  memblend_xx44(dst,norm_pixel,len, alphamask);
		dst += len;
	      }
	    }
	  } else if (x < hili_right) {
	    if (len <= hili_right - x) {
	      if(!clipped)
	        memblend_xx44(dst,hili_pixel,len, alphamask);
	      dst += len;
	    } else {
	      if(!clipped)
	        memblend_xx44(dst,hili_pixel,hili_right - x,alphamask);
	      if (len > clip_right - x)
	        clipped = 1;
	      dst += hili_right - x;
	      len -= hili_right - x;
	      if(!clipped)
	        memblend_xx44(dst,norm_pixel,len, alphamask);
	      dst += len;
	    }
	  } else {
	    if (x > clip_right)
	      clipped = 1;
	    if(!clipped)
	      memblend_xx44(dst,norm_pixel,len, alphamask);
	    dst += len;
	  }
	} else {
	  if(!clipped)
	    memblend_xx44(dst,norm_pixel,len, alphamask);
	  dst += len;
	}
      }
      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }
    if (rle >= rle_limit) break;
    dst_y += dst_pitch;
  }
}

static void alphablend_disable_exact_osd_alpha_blending_changed(void *user_data, xine_cfg_entry_t *entry)
{
  alphablend_t *extra_data = (alphablend_t *)user_data;

  extra_data->disable_exact_blending = entry->num_value;
}

void _x_alphablend_init(alphablend_t *extra_data, xine_t *xine)
{
  config_values_t *config = xine->config;

  extra_data->buffer = 0;
  extra_data->buffer_size = 0;
  extra_data->offset_x = 0;
  extra_data->offset_y = 0;

  extra_data->disable_exact_blending =
    config->register_bool(config, "video.output.disable_exact_alphablend", 0,
      _("disable exact alpha blending of overlays"),
      _("If you experience a performance impact when an On Screen Display or other "
        "overlays like DVD subtitles are active, then you might want to enable this option.\n"
        "The result is that alpha blending of overlays is less accurate than before, "
        "but the CPU usage will be decreased as well."),
      10, alphablend_disable_exact_osd_alpha_blending_changed, extra_data);
}

void _x_alphablend_free(alphablend_t *extra_data)
{
  _x_freep(&extra_data->buffer);

  extra_data->buffer_size = 0;
}

#define saturate(v) if (v & ~255) v = (~((uint32_t)v)) >> 24

void _x_clut_yuv2rgb(uint32_t *clut, int num_items, int color_matrix)
{
  uint32_t *end = clut + num_items;
  if (end <= clut) return;

  switch (color_matrix >> 1) {

    case 8:
      while (clut < end) {
        union {
          uint32_t u32;
          clut_t   c;
        } tmp = { *clut };
        int32_t y, u, v, r, g, b;
        y = tmp.c.y;
        u = tmp.c.cb;
        v = tmp.c.cr;
        /* Green Orange -- does this ever happen? */
        r = y - u + v;
        saturate (r);
        g = y + u     - 128;
        saturate (g);
        b = y - u - v + 256;
        saturate (b);
        /* see clut_to_argb () */
        tmp.c.cb = b;
        tmp.c.cr = g;
        tmp.c.y  = r;
        *clut++  = tmp.u32;
      }
      break;

    case 1:
    case 7:
      while (clut < end) {
        union {
          uint32_t u32;
          clut_t   c;
        } tmp = { *clut };
        int32_t y, u, v, r, g, b;
        y = tmp.c.y;
        u = tmp.c.cb;
        v = tmp.c.cr;
        /* ITU 709 (HD), mpeg range. */
        y *= 76304;
        r = (y              + 117473 * v - 16224640) >> 16;
        saturate (r);
        g = (y  - 13972 * u  - 34918 * v  + 5069824) >> 16;
        saturate (g);
        b = (y + 138425 * u              - 18906496) >> 16;
        saturate (b);
        /* see clut_to_argb () */
        tmp.c.cb = b;
        tmp.c.cr = g;
        tmp.c.y  = r;
        *clut++  = tmp.u32;
      }
      break;

    default:
      while (clut < end) {
        union {
          uint32_t u32;
          clut_t   c;
        } tmp = { *clut };
        int32_t y, u, v, r, g, b;
        y = tmp.c.y;
        u = tmp.c.cb;
        v = tmp.c.cr;
        /* ITU 601 (SD), mpeg range. */
        y *= 76304;
        r = (y              + 104582 * v - 14574592) >> 16;
        saturate (r);
        g = (y  - 25664 * u  - 53268 * v  + 8849664) >> 16;
        saturate (g);
        b = (y + 132186 * u              - 18107904) >> 16;
        saturate (b);
        /* see clut_to_argb () */
        tmp.c.cb = b;
        tmp.c.cr = g;
        tmp.c.y  = r;
        *clut++  = tmp.u32;
      }
      break;
  }
}
