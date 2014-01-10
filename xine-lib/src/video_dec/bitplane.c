/*
 * Copyright (C) 2004-2012 the xine project
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
 * Bitplane "Decoder" by Manfred Tremmel (Manfred.Tremmel@iiv.de)
 * Converts Amiga typical bitplane pictures to a YUV2 map
 * suitable for display under xine. It's based on the rgb-decoder
 * and the development documentation from the Amiga Developer CD
 *
 * Supported formats:
 * - uncompressed and byterun1 compressed ILBM data
 * - IFF ANIM compression methods OPT 5, 7 (long and short) and
 *   8 (long and short)
 * - untested (found no testfiles) IFF-ANIM OPT 3, 4 and 6
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"

#include "demuxers/iff.h"

#define IFF_REPLACE_BYTE_SIMPLE(ptr, old_data, new_data, colorindexx ) { \
  register uint8_t  *index_ptr = ptr; \
  register uint8_t  colorindex = colorindexx; \
  *index_ptr    -= ((old_data & 0x80) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x80) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x40) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x40) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x20) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x20) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x10) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x10) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x08) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x08) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x04) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x04) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x02) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x02) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x01) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x01) ? colorindex : 0); \
  old_data       = new_data; \
}

#define IFF_REPLACE_BYTE(ptr, yuvy, yuvu, yuvv, yuv_palette, old_data, new_data, colorindexx ) { \
  register uint8_t  *index_ptr = ptr; \
  register uint8_t  colorindex = colorindexx; \
  register uint8_t  *yuv_y = yuvy; \
  register uint8_t  *yuv_u = yuvu; \
  register uint8_t  *yuv_v = yuvv; \
  *index_ptr    -= ((old_data & 0x80) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x80) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x40) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x40) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x20) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x20) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x10) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x10) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x08) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x08) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x04) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x04) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x02) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x02) ? colorindex : 0); \
  yuv_index      = *index_ptr++ * 4; \
  *yuv_y++       = yuv_palette[yuv_index++]; \
  *yuv_u++       = yuv_palette[yuv_index++]; \
  *yuv_v++       = yuv_palette[yuv_index]; \
  *index_ptr    -= ((old_data & 0x01) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x01) ? colorindex : 0); \
  yuv_index      = *index_ptr * 4; \
  *yuv_y         = yuv_palette[yuv_index++]; \
  *yuv_u         = yuv_palette[yuv_index++]; \
  *yuv_v         = yuv_palette[yuv_index]; \
  old_data       = new_data; \
}

#define IFF_REPLACE_SHORT_SIMPLE(ptr_s, old_data_s, new_data_s, colorindexx_s ) { \
  uint8_t  *xindex_ptr = (uint8_t *)ptr_s; \
  uint8_t  *xold_data  = (uint8_t *)old_data_s; \
  uint8_t  *xnew_data  = (uint8_t *)new_data_s; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_s ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_s ); \
}

#define IFF_REPLACE_SHORT(ptr_s, yuvy_s, yuvu_s, yuvv_s, yuv_palette_s, old_data_s, new_data_s, colorindexx_s ) { \
  uint8_t  *xindex_ptr = (uint8_t *)ptr_s; \
  uint8_t  *xold_data  = (uint8_t *)old_data_s; \
  uint8_t  *xnew_data  = (uint8_t *)new_data_s; \
  uint8_t  *xyuv_y = yuvy_s; \
  uint8_t  *xyuv_u = yuvu_s; \
  uint8_t  *xyuv_v = yuvv_s; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_s, *xold_data, *xnew_data, colorindexx_s ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  xyuv_y     += 8; \
  xyuv_u     += 8; \
  xyuv_v     += 8; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_s, *xold_data, *xnew_data, colorindexx_s ); \
}

#define IFF_REPLACE_LONG_SIMPLE(ptr_l, old_data_l, new_data_l, colorindexx_l ) { \
  uint8_t  *xindex_ptr = (uint8_t *)ptr_l; \
  uint8_t  *xold_data  = (uint8_t *)old_data_l; \
  uint8_t  *xnew_data  = (uint8_t *)new_data_l; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  IFF_REPLACE_BYTE_SIMPLE(xindex_ptr, *xold_data, *xnew_data, colorindexx_l ); \
}

#define IFF_REPLACE_LONG(ptr_l, yuvy_l, yuvu_l, yuvv_l, yuv_palette_l, old_data_l, new_data_l, colorindexx_l ) { \
  uint8_t  *xindex_ptr = (uint8_t *)ptr_l; \
  uint8_t  *xold_data  = (uint8_t *)old_data_l; \
  uint8_t  *xnew_data  = (uint8_t *)new_data_l; \
  uint8_t  *xyuv_y = yuvy_l; \
  uint8_t  *xyuv_u = yuvu_l; \
  uint8_t  *xyuv_v = yuvv_l; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_l, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  xyuv_y     += 8; \
  xyuv_u     += 8; \
  xyuv_v     += 8; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_l, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  xyuv_y     += 8; \
  xyuv_u     += 8; \
  xyuv_v     += 8; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_l, *xold_data, *xnew_data, colorindexx_l ); \
  xindex_ptr += 8; \
  xold_data++; \
  xnew_data++; \
  xyuv_y     += 8; \
  xyuv_u     += 8; \
  xyuv_v     += 8; \
  IFF_REPLACE_BYTE(xindex_ptr, xyuv_y, xyuv_u, xyuv_v, yuv_palette_l, *xold_data, *xnew_data, colorindexx_l ); \
}

typedef struct {
  video_decoder_class_t   decoder_class;
} bitplane_class_t;

typedef struct bitplane_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  bitplane_class_t *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object    */
  uint64_t          video_step;  /* frame duration in pts units   */
  int               decoder_ok;  /* current decoder status        */
  int               skipframes;  /* 0 = draw picture, 1 = skip it */
  int               framenumber;

  unsigned char    *buf;         /* the accumulated buffer data   */
  int               bufsize;     /* the maximum size of buf       */
  int               size;        /* the current size of buf       */
  int               size_uk;     /* size of unkompressed bitplane */

  int               width;       /* the width of a video frame    */
  int               height;      /* the height of a video frame   */
  int               num_pixel;   /* number pixel                  */
  double            ratio;       /* the width to height ratio     */
  int               bytes_per_pixel;
  int               num_bitplanes;
  int               camg_mode;
  int               is_ham;

  unsigned char     yuv_palette[256 * 4];
  unsigned char     rgb_palette[256 * 4];
  yuv_planes_t      yuv_planes;
  yuv_planes_t      yuv_planes_hist;

  uint8_t          *buf_uk;      /* uncompressed buffer                */
  uint8_t          *buf_uk_hist; /* uncompressed buffer historic       */
  uint8_t          *index_buf;   /* index buffer (for indexed pics)    */
  uint8_t          *index_buf_hist;/* index buffer historic            */

} bitplane_decoder_t;

/* create a new buffer and decde a byterun1 decoded buffer into it */
static uint8_t *bitplane_decode_byterun1 (uint8_t *compressed,
  int size_compressed,
  int size_uncompressed) {

  /* BytRun1 decompression */
  int pixel_ptr                         = 0;
  int i                                 = 0;
  int j                                 = 0;

  uint8_t *uncompressed                 = calloc(1, size_uncompressed );

  while ( i < size_compressed &&
          pixel_ptr < size_uncompressed ) {
    if( compressed[i] <= 127 ) {
      j = compressed[i++];
      if( (i+j) > size_compressed ) {
	free(uncompressed);
        return NULL;
      }
      for( ; (j >= 0) && (pixel_ptr < size_uncompressed); j-- ) {
        uncompressed[pixel_ptr++] = compressed[i++];
      }
    } else if ( compressed[i] > 128 ) {
      j = 256 - compressed[i++];
      if( i >= size_compressed ) {
	free(uncompressed);
        return NULL;
      }
      for( ; (j >= 0) && (pixel_ptr < size_uncompressed); j-- ) {
        uncompressed[pixel_ptr++] = compressed[i];
      }
      i++;
    }
  }
  return uncompressed;
}

/* create a new buffer with "normal" index or rgb numbers out of a bitplane */
static void bitplane_decode_bitplane (uint8_t *bitplane_buffer,
  uint8_t *index_buf,
  int width,
  int height,
  int num_bitplanes,
  int bytes_per_pixel ) {

  int rowsize                           = width / 8;
  int pixel_ptr                         = 0;
  int row_ptr                           = 0;
  int palette_index                     = 0;
  int i                                 = 0;
  int j                                 = 0;
  int row_i                             = 0;
  int row_j                             = 0;
  int palette_offset                    = 0;
  int palette_index_rowsize             = 0;
  uint8_t color                         = 0;
  uint8_t data                          = 0;
  int bytes_per_pixel_8                 = bytes_per_pixel * 8;
  int rowsize_num_bitplanes             = rowsize * num_bitplanes;
  int width_bytes_per_pixel             = width * bytes_per_pixel;

  for (i = 0; i < (height * width_bytes_per_pixel); index_buf[i++] = 0);

  /* decode Bitplanes to RGB/Index Numbers */
  for (row_ptr = 0; row_ptr < height; row_ptr++) {

    row_i                               = row_ptr * width_bytes_per_pixel;
    row_j                               = row_ptr * rowsize_num_bitplanes;

    for (palette_index = 0; palette_index < num_bitplanes; palette_index++) {

      palette_offset                    = ((palette_index > 15) ? 2 : (palette_index > 7) ? 1 : 0);
      color                             = bitplainoffeset[palette_index];
      palette_index_rowsize             = palette_index * rowsize;

      for (pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        i                               = row_i +
                                          (pixel_ptr * bytes_per_pixel_8) +
                                          palette_offset;
        j                               = row_j + palette_index_rowsize + pixel_ptr;

        data                            = bitplane_buffer[j];

        index_buf[i]                   += ((data & 0x80) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x40) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x20) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x10) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x08) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x04) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x02) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x01) ? color : 0);
      }
    }
  }
}

/* create Buffer decode HAM6 and HAM8 to YUV color */
static void bitplane_decode_ham (uint8_t *ham_buffer,
  yuv_planes_t *yuv_planes,
  int width,
  int height,
  int num_bitplanes,
  int bytes_per_pixel,
  unsigned char *rgb_palette ) {

  uint8_t *ham_buffer_work              = ham_buffer;
  uint8_t *ham_buffer_end               = &ham_buffer[(width * height)];
  uint8_t *yuv_ptr_y                    = yuv_planes->y;
  uint8_t *yuv_ptr_u                    = yuv_planes->u;
  uint8_t *yuv_ptr_v                    = yuv_planes->v;
  int i                                 = 0;
  int j                                 = 0;
  uint8_t r                             = 0;
  uint8_t g                             = 0;
  uint8_t b                             = 0;
  /* position of special HAM-Bits differs in HAM6 and HAM8, detect them */
  int hambits                           = num_bitplanes > 6 ? 6 : 4;
        /* the other bits contain the real data, dreate a mask out of it */
  int maskbits                          = 8 - hambits;
  int mask                              = ( 1 << hambits ) - 1;

  for(; ham_buffer_work < ham_buffer_end; j = *ham_buffer_work++) {
    i                                   = (j & mask);
    switch ( j >> hambits ) {
      case HAMBITS_CMAP:
        /* Take colors from palette */
        r                               = rgb_palette[i * 4 + 0];
        g                               = rgb_palette[i * 4 + 1];
        b                               = rgb_palette[i * 4 + 2];
        break;
      case HAMBITS_BLUE:
        /* keep red and green and modify blue */
        b                               = i << maskbits;
        b                              |= b >> hambits;
        break;
      case HAMBITS_RED:
        /* keep green and blue and modify red */
        r                               = i << maskbits;
        r                              |= r >> hambits;
        break;
      case HAMBITS_GREEN:
        /* keep red and blue and modify green */
        g                               = i << maskbits;
        g                              |= g >> hambits;
        break;
      default:
        break;
    }
    *yuv_ptr_y++                        = COMPUTE_Y(r, g, b);
    *yuv_ptr_u++                        = COMPUTE_U(r, g, b);
    *yuv_ptr_v++                        = COMPUTE_V(r, g, b);
  }
}

/* decoding method 3 */
static void bitplane_sdelta_opt_3 (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 16;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t palette_index                = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint16_t *ptr                         = NULL;
  uint16_t *planeptr                    = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&this->buf_uk[(rowsize_all_planes * 2 * this->height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&this->buf[this->size]);
  uint16_t *rowworkptr                  = NULL;
  int16_t s                             = 0;
  int16_t size                          = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t yuv_index                    = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = (uint16_t *)(&this->buf_uk[(palette_index * rowsize * 2)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    data                                = (uint16_t *)(&this->buf[_X_BE_32(&deltadata[palette_index])]);
    if( data != (uint16_t *)this->buf ) {
      /* This 8 Pointers are followd by another 8                    */
      ptr                               = (uint16_t *)(&this->buf[_X_BE_32(&deltadata[(palette_index+8)])]);

      /* in this case, I think big/little endian is not important ;-) */
      while( *data !=  0xFFFF) {
        row_ptr                         = 0;
        size                            = _X_BE_16(data);
        data++;
        if( size >= 0 ) {
          rowworkptr                    = planeptr + size;
          pixel_ptr_bit                 = size * 16;
          if( this->is_ham ) {
            IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[pixel_ptr_bit],
                               rowworkptr, data, bitplainoffeset[palette_index] );
          } else {
            IFF_REPLACE_SHORT( &this->index_buf[pixel_ptr_bit],
                               &this->yuv_planes.y[pixel_ptr_bit], &this->yuv_planes.u[pixel_ptr_bit],
                               &this->yuv_planes.v[pixel_ptr_bit], this->yuv_palette,
                               rowworkptr, data, bitplainoffeset[palette_index] );
          }
          data++;
        } else {
          size                          = 0 - size + 2;
          rowworkptr                    = planeptr + size;
          pixel_ptr_bit                 = size * 16;
          s                             = _X_BE_16(data);
          data++;
          while( s--) {
            if( this->is_ham ) {
              IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[pixel_ptr_bit],
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            } else {
              IFF_REPLACE_SHORT( &this->index_buf[pixel_ptr_bit],
                                 &this->yuv_planes.y[pixel_ptr_bit], &this->yuv_planes.u[pixel_ptr_bit],
                                 &this->yuv_planes.v[pixel_ptr_bit], this->yuv_palette,
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            }
            rowworkptr++;
            data++;
          }
        }




        size                            = _X_BE_16(ptr);
        ptr++;
        if (size < 0) {
          for (s = size; s < 0; s++) {
            if (data > data_end || rowworkptr > picture_end)
              return;
            yuv_index                   = ((row_ptr * this->width) + pixel_ptr_bit);
            if( this->is_ham ) {
              IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            } else {
              IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                 &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                 &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            }
            rowworkptr                 += rowsize_all_planes;
            row_ptr++;
          }
          data++;
        }
        else {
          for (s = 0; s < size; s++) {
            if (data > data_end || rowworkptr > picture_end)
              return;
            yuv_index                   = ((row_ptr * this->width) + pixel_ptr_bit);
            if( this->is_ham ) {
              IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            } else {
              IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                 &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                 &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            }
            data++;
            rowworkptr                 += rowsize_all_planes;
            row_ptr++;
          }
        }
      }
    }
  }
}

/* decoding method 4 */
static void bitplane_set_dlta_short (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 16;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t palette_index                = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint16_t *ptr                         = NULL;
  uint16_t *planeptr                    = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&this->buf_uk[(rowsize_all_planes * 2 * this->height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&this->buf[this->size]);
  uint16_t *rowworkptr                  = NULL;
  int16_t s                             = 0;
  int16_t size                          = 0;
  uint16_t pixel_ptr                    = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t yuv_index                    = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = (uint16_t *)(&this->buf_uk[(palette_index * rowsize * 2)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    data                                = (uint16_t *)(&this->buf[_X_BE_32(&deltadata[palette_index])]);
    if( data != (uint16_t *)this->buf ) {
      /* This 8 Pointers are followd by another 8                    */
      ptr                               = (uint16_t *)(&this->buf[_X_BE_32(&deltadata[(palette_index+8)])]);

      /* in this case, I think big/little endian is not important ;-) */
      while( *ptr !=  0xFFFF) {
        pixel_ptr                       = _X_BE_16(ptr);
        pixel_ptr_bit                   = pixel_ptr * 16;
        row_ptr                         = 0;
        rowworkptr                      = planeptr + pixel_ptr;
        ptr++;
        size                            = _X_BE_16(ptr);
        ptr++;
        if (size < 0) {
          for (s = size; s < 0; s++) {
            if (data > data_end || rowworkptr > picture_end)
              return;
            yuv_index                   = ((row_ptr * this->width) + pixel_ptr_bit);
            if( this->is_ham ) {
              IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            } else {
              IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                 &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                 &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            }
            rowworkptr                 += rowsize_all_planes;
            row_ptr++;
          }
          data++;
        } else {
          for (s = 0; s < size; s++) {
            if (data > data_end || rowworkptr > picture_end)
              return;
            yuv_index                   = ((row_ptr * this->width) + pixel_ptr_bit);
            if( this->is_ham ) {
              IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            } else {
              IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                 &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                 &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                 rowworkptr, data, bitplainoffeset[palette_index] );
            }
            data++;
            rowworkptr                   += rowsize_all_planes;
            row_ptr++;
          }
        }
      }
    }
  }
}

/* decoding method 5 */
static void bitplane_dlta_5 (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 8;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t yuv_index                    = 0;
  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint8_t  *planeptr                    = NULL;
  uint8_t  *rowworkptr                  = NULL;
  uint8_t  *picture_end                 = this->buf_uk + (rowsize_all_planes * this->height);
  uint8_t  *data                        = NULL;
  uint8_t  *data_end                    = this->buf + this->size;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = &this->buf_uk[(palette_index * rowsize)];
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = _X_BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = this->buf + delta_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        pixel_ptr_bit                   = pixel_ptr * 8;
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *data++; op_count; op_count--) {
          op                            = *data++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              yuv_index                 = ((row_ptr * this->width) + pixel_ptr_bit);
              if( this->is_ham ) {
                IFF_REPLACE_BYTE_SIMPLE(&this->index_buf[yuv_index],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
              } else {
                IFF_REPLACE_BYTE( &this->index_buf[yuv_index],
                                  &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                  &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
              }
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *data++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                yuv_index               = ((row_ptr * this->width) + pixel_ptr_bit);
                if( this->is_ham ) {
                  IFF_REPLACE_BYTE_SIMPLE(&this->index_buf[yuv_index],
                                    *rowworkptr, *data, bitplainoffeset[palette_index] );
                } else {
                  IFF_REPLACE_BYTE( &this->index_buf[yuv_index],
                                    &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                    &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                    *rowworkptr, *data, bitplainoffeset[palette_index] );
                }
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 7 (short version) */
static void bitplane_dlta_7_short (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 16;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t yuv_index                    = 0;
  uint32_t opcode_offset                = 0;
  uint32_t data_offset                  = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint8_t  *planeptr                    = NULL;
  uint16_t *rowworkptr                  = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&this->buf_uk[(rowsize_all_planes * 2 * this->height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&this->buf[this->size]);
  uint8_t  *op_ptr                      = NULL;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = &this->buf_uk[(palette_index * rowsize * 2)];
    /* find opcode and data offset (up to 8 pointers, one for every bitplane */
    opcode_offset                       = _X_BE_32(&deltadata[palette_index]);
    data_offset                         = _X_BE_32(&deltadata[palette_index + 8]);

    if (opcode_offset > 0 && data_offset > 0) {
      data                              = (uint16_t *)(&this->buf[data_offset]);
      op_ptr                            = this->buf + opcode_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = (uint16_t *)(&planeptr[pixel_ptr * 2]);
        pixel_ptr_bit                   = pixel_ptr * 16;
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *op_ptr++; op_count; op_count--) {
          op                            = *op_ptr++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              yuv_index                 = ((row_ptr * this->width) + pixel_ptr_bit);
              if( this->is_ham ) {
                IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              } else {
                IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                   &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                   &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              }
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *op_ptr++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                yuv_index               = ((row_ptr * this->width) + pixel_ptr_bit);
                if( this->is_ham ) {
                  IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                } else {
                  IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                     &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                     &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                }
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 7 (long version) */
static void bitplane_dlta_7_long  (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 32;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t yuv_index                    = 0;
  uint32_t opcode_offset                = 0;
  uint32_t data_offset                  = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint8_t  *planeptr                    = NULL;
  uint32_t *rowworkptr                  = NULL;
  uint32_t *picture_end                 = (uint32_t *)(&this->buf_uk[(rowsize_all_planes * 4 * this->height)]);
  uint32_t *data                        = NULL;
  uint32_t *data_end                    = (uint32_t *)(&this->buf[this->size]);
  uint8_t  *op_ptr                      = NULL;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {
    planeptr                            = &this->buf_uk[(palette_index * rowsize * 4)];
    /* find opcode and data offset (up to 8 pointers, one for every bitplane */
    opcode_offset                       = _X_BE_32(&deltadata[palette_index]);
    data_offset                         = _X_BE_32(&deltadata[palette_index + 8]);

    if (opcode_offset > 0 && data_offset > 0) {
      data                              = (uint32_t *)(&this->buf[data_offset]);
      op_ptr                            = this->buf + opcode_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = (uint32_t *)(&planeptr[pixel_ptr * 4]);
        pixel_ptr_bit                   = pixel_ptr * 32;
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *op_ptr++; op_count; op_count--) {
          op                            = *op_ptr++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                return;
              yuv_index                 = ((row_ptr * this->width) + pixel_ptr_bit);
              if( this->is_ham ) {
                IFF_REPLACE_LONG_SIMPLE(&this->index_buf[yuv_index],
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              } else {
                IFF_REPLACE_LONG( &this->index_buf[yuv_index],
                                   &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                   &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              }
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *op_ptr++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                  return;
                yuv_index               = ((row_ptr * this->width) + pixel_ptr_bit);
                if( this->is_ham ) {
                  IFF_REPLACE_LONG_SIMPLE(&this->index_buf[yuv_index],
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                } else {
                  IFF_REPLACE_LONG( &this->index_buf[yuv_index],
                                    &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                    &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                    rowworkptr, data, bitplainoffeset[palette_index] );
                }
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
             /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 8 short */
static void bitplane_dlta_8_short (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 16;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t yuv_index                    = 0;
  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint16_t *planeptr                    = NULL;
  uint16_t *rowworkptr                  = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&this->buf_uk[(rowsize_all_planes * 2 * this->height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&this->buf[this->size]);
  uint16_t op_count                     = 0;
  uint16_t op                           = 0;
  uint16_t count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = (uint16_t *)(&this->buf_uk[(palette_index * rowsize * 2)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = _X_BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = (uint16_t *)(&this->buf[delta_offset]);
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        pixel_ptr_bit                   = pixel_ptr * 16;
        row_ptr                         = 0;
        /* execute ops */
        op_count = _X_BE_16(data);
        data++;
        for( ; op_count; op_count--) {
          op                            = _X_BE_16(data);
          data++;
          if (op & 0x8000) {
            /* Uniq ops */
            count                       = op & 0x7fff; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              yuv_index                 = ((row_ptr * this->width) + pixel_ptr_bit);
              if( this->is_ham ) {
                IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              } else {
                IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                   &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                   &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                   rowworkptr, data, bitplainoffeset[palette_index] );
              }
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = _X_BE_16(data);
              data++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                yuv_index               = ((row_ptr * this->width) + pixel_ptr_bit);
                if( this->is_ham ) {
                  IFF_REPLACE_SHORT_SIMPLE(&this->index_buf[yuv_index],
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                } else {
                  IFF_REPLACE_SHORT( &this->index_buf[yuv_index],
                                     &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                     &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                }
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 8 long */
static void bitplane_dlta_8_long (bitplane_decoder_t *this) {

  uint32_t rowsize                      = this->width / 32;
  uint32_t rowsize_all_planes           = rowsize * this->num_bitplanes;

  uint32_t yuv_index                    = 0;
  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t pixel_ptr_bit                = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)this->buf;
  uint32_t *planeptr                    = NULL;
  uint32_t *rowworkptr                  = NULL;
  uint32_t *picture_end                 = (uint32_t *)(&this->buf_uk[(rowsize_all_planes * 4 * this->height)]);
  uint32_t *data                        = NULL;
  uint32_t *data_end                    = (uint32_t *)(&this->buf[this->size]);
  uint32_t op_count                     = 0;
  uint32_t op                           = 0;
  uint32_t count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {

    planeptr                            = (uint32_t *)(&this->buf_uk[(palette_index * rowsize * 4)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = _X_BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = (uint32_t *)(&this->buf[delta_offset]);
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        pixel_ptr_bit                   = pixel_ptr * 32;
        row_ptr                         = 0;
        /* execute ops */
        op_count                        = _X_BE_32(data);
        data++;
        for( ; op_count; op_count--) {
          op                            = _X_BE_32(data);
          data++;
          if (op & 0x80000000) {
            /* Uniq ops */
            count                       = op & 0x7fffffff; /* get count */
            while(count--) {
              if (data <= data_end || rowworkptr <= picture_end) {
                yuv_index               = ((row_ptr * this->width) + pixel_ptr_bit);
                if( this->is_ham ) {
                  IFF_REPLACE_LONG_SIMPLE(&this->index_buf[yuv_index],
                                     rowworkptr, data, bitplainoffeset[palette_index] );
                } else {
                  IFF_REPLACE_LONG( &this->index_buf[((row_ptr * this->width) + pixel_ptr_bit)],
                                    &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                    &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                    rowworkptr, data, bitplainoffeset[palette_index] );
                }
              }
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = _X_BE_32(data);
              data++;
              while(count--) {
                if (data <= data_end && rowworkptr <= picture_end) {
                  yuv_index             = ((row_ptr * this->width) + pixel_ptr_bit);
                  if( this->is_ham ) {
                    IFF_REPLACE_LONG_SIMPLE(&this->index_buf[yuv_index],
                                       rowworkptr, data, bitplainoffeset[palette_index] );
                  } else {
                    IFF_REPLACE_LONG( &this->index_buf[yuv_index],
                                      &this->yuv_planes.y[yuv_index], &this->yuv_planes.u[yuv_index],
                                      &this->yuv_planes.v[yuv_index], this->yuv_palette,
                                      rowworkptr, data, bitplainoffeset[palette_index] );
                  }
                }
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
/*  bitplane_decode_bitplane(this->buf_uk, this->index_buf, this->width, this->height, this->num_bitplanes, 1);*/
}

static void bitplane_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;
  xine_bmiheader *bih                   = 0;
  palette_entry_t *palette              = 0;
  AnimHeader *anhd                      = NULL;
  int i                                 = 0;
  int j                                 = 0;
  unsigned char r                       = 0;
  unsigned char g                       = 0;
  unsigned char b                       = 0;
  uint8_t *buf_exchange                 = NULL;

  vo_frame_t *img                       = 0; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette                             = (palette_entry_t *)buf->decoder_info_ptr[2];

    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0]      =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1]      =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2]      =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
      this->rgb_palette[i * 4 + 0]      = palette[i].r;
      this->rgb_palette[i * 4 + 1]      = palette[i].g;
      this->rgb_palette[i * 4 + 2]      = palette[i].b;
    }

    /* EHB Pictures not allways contain all 64 colors, sometimes only    */
    /* the first 32 are included and sometimes all 64 colors are provide,*/
    /* but second 32 are only stupid dirt, so recalculate them           */
    if (((this->num_bitplanes  == 6) &&
         (buf->decoder_info[2] == 32)) ||
        (this->camg_mode & CAMG_EHB)) {
      for (i = 32; i < 64; i++) {
        this->rgb_palette[i * 4 + 0]    = palette[(i-32)].r / 2;
        this->rgb_palette[i * 4 + 1]    = palette[(i-32)].g / 2;
        this->rgb_palette[i * 4 + 2]    = palette[(i-32)].b / 2;
        this->yuv_palette[i * 4 + 0]    =
           COMPUTE_Y(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
        this->yuv_palette[i * 4 + 1]    =
           COMPUTE_U(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
        this->yuv_palette[i * 4 + 2]    =
           COMPUTE_V(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
       }
    }

    return;
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    (this->stream->video_out->open) (this->stream->video_out, this->stream);

    bih                                 = (xine_bmiheader *) buf->content;
    this->width                         = (bih->biWidth + 15) & ~0x0f;
    this->height                        = bih->biHeight;
    this->num_pixel                     = this->width * this->height;
    this->ratio                         = (double)this->width/(double)this->height;
    this->video_step                    = buf->decoder_info[1];
    /* Palette based Formates use up to 8 Bit per pixel, always use 8 Bit if less */
    this->bytes_per_pixel               = (bih->biBitCount + 1) / 8;
    if( this->bytes_per_pixel < 1 )
      this->bytes_per_pixel             = 1;

    /* New Buffer for indexes (palette based formats) */
    this->index_buf                     = calloc( this->num_pixel, this->bytes_per_pixel );
    this->index_buf_hist                = calloc( this->num_pixel, this->bytes_per_pixel );

    this->num_bitplanes                 = bih->biPlanes;
    this->camg_mode                     = bih->biCompression;
    if( this->camg_mode & CAMG_HAM )
      this->is_ham                      = 1;
    else
      this->is_ham                      = 0;

    if( buf->decoder_info[2]           != buf->decoder_info[3] &&
        buf->decoder_info[3]            > 0 ) {
      this->ratio                      *= buf->decoder_info[2];
      this->ratio                      /= buf->decoder_info[3];
    }

    if( (bih->biCompression & CAMG_HIRES) &&
        !(bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 16) > (buf->decoder_info[3] * 10) )
        this->ratio                    /= 2.0;
    }

    if( !(bih->biCompression & CAMG_HIRES) &&
        (bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 10) < (buf->decoder_info[3] * 16) )
        this->ratio                    *= 2.0;
    }

    free (this->buf);
    this->bufsize                       = VIDEOBUFSIZE;
    this->buf                           = calloc(1, this->bufsize);
    this->size                          = 0;
    this->framenumber                   = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);
    init_yuv_planes(&this->yuv_planes_hist, this->width, this->height);

    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->decoder_ok                    = 1;

    /* load the stream/meta info */
    switch( buf->type ) {
      case BUF_VIDEO_BITPLANE:
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Uncompressed bitplane");
        break;
      case BUF_VIDEO_BITPLANE_BR1:
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "ByteRun1 bitplane");
        break;
      default:
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Unknown bitplane");
        break;
    }

    return;
  } else if (this->decoder_ok) {

    this->skipframes                    = 0;
    this->framenumber++;
    if (this->size + buf->size > this->bufsize) {
      this->bufsize                     = this->size + 2 * buf->size;
      this->buf                         = realloc (this->buf, this->bufsize);
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size                         += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);

      img->duration                     = this->video_step;
      img->pts                          = buf->pts;
      img->bad_frame                    = 0;
      anhd                              = (AnimHeader *)(buf->decoder_info_ptr[0]);

      if( (this->buf_uk    == NULL) ||
          (anhd            == NULL) ||
          (anhd->operation == IFF_ANHD_ILBM) ) {

        /* iterate through each row */
        this->size_uk                   = (((this->num_pixel) / 8) * this->num_bitplanes);

        if( this->buf_uk_hist != NULL )
          xine_fast_memcpy (this->buf_uk_hist, this->buf_uk, this->size_uk);
        switch( buf->type ) {
          case BUF_VIDEO_BITPLANE:
            /* uncompressed Buffer, set decoded_buf pointer direct to input stream */
            if( this->buf_uk == NULL )
              this->buf_uk              = malloc(this->size);
            xine_fast_memcpy (this->buf_uk, this->buf, this->size);
            break;
          case BUF_VIDEO_BITPLANE_BR1:
            /* create Buffer for decompressed bitmap */
            this->buf_uk                = bitplane_decode_byterun1(
                                                   this->buf,          /* compressed buffer         */
                                                   this->size,         /* size of compressed data   */
                                                   this->size_uk );    /* size of uncompressed data */

            if( this->buf_uk == NULL ) {
              xine_log(this->stream->xine, XINE_LOG_MSG,
                       _("bitplane: error doing ByteRun1 decompression\n"));
              _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
              return;
            }
            /* set pointer to decompressed Buffer */
            break;
          default:
            break;
        }
        bitplane_decode_bitplane(     this->buf_uk,              /* bitplane buffer         */
                                      this->index_buf,           /* index buffer            */
                                      this->width,               /* width                   */
                                      this->height,              /* hight                   */
                                      this->num_bitplanes,       /* number bitplanes        */
                                      this->bytes_per_pixel);    /* used Bytes per pixel    */

        if ((this->bytes_per_pixel == 1) &&
            (this->is_ham == 0) ) {
          buf_exchange                  = this->index_buf;
          for (i = 0; i < (this->height * this->width); i++) {
            j                           = *buf_exchange++ * 4;
            this->yuv_planes.y[i]       = this->yuv_palette[j++];
            this->yuv_planes.u[i]       = this->yuv_palette[j++];
            this->yuv_planes.v[i]       = this->yuv_palette[j];
          }
        }
        if( this->buf_uk_hist == NULL ) {
          this->buf_uk_hist             = malloc(this->size_uk);
          xine_fast_memcpy (this->buf_uk_hist, this->buf_uk, this->size_uk);
          xine_fast_memcpy (this->index_buf_hist, this->index_buf,
                            (this->num_pixel * this->bytes_per_pixel));
          xine_fast_memcpy (this->yuv_planes_hist.y, this->yuv_planes.y, (this->num_pixel));
          xine_fast_memcpy (this->yuv_planes_hist.u, this->yuv_planes.u, (this->num_pixel));
          xine_fast_memcpy (this->yuv_planes_hist.v, this->yuv_planes.v, (this->num_pixel));
        }
      } else {
        /* when no start-picture is given, create a empty one */
        if( this->buf_uk_hist == NULL ) {
          this->size_uk                 = (((this->num_pixel) / 8) * this->num_bitplanes);
          this->buf_uk                  = calloc(this->num_bitplanes, ((this->num_pixel) / 8));
          this->buf_uk_hist             = calloc(this->num_bitplanes, ((this->num_pixel) / 8));
        }
        if( this->index_buf == NULL ) {
          this->index_buf               = calloc( this->num_pixel, this->bytes_per_pixel );
          this->index_buf_hist          = calloc( this->num_pixel, this->bytes_per_pixel );
        }

        switch( anhd->operation ) {
          /* also known as IFF-ANIM OPT1 (never seen in real world) */
          case IFF_ANHD_XOR:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 1 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          /* also known as IFF-ANIM OPT2 (never seen in real world) */
          case IFF_ANHD_LDELTA:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 2 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          /* also known as IFF-ANIM OPT3 */
          case IFF_ANHD_SDELTA:
            _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT3");
            bitplane_sdelta_opt_3 ( this );
            return;
            break;
          /* also known as IFF-ANIM OPT4 (never seen in real world) */
          case IFF_ANHD_SLDELTA:
            _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT4 (SLDELTA)");
            bitplane_set_dlta_short ( this );
            break;
          /* also known as IFF-ANIM OPT5 */
          case IFF_ANHD_BVDELTA:
            _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT5 (BVDELTA)");
            bitplane_dlta_5(this);
            break;
          /* IFF-ANIM OPT6 is exactly the same as OPT5, but for stereo-displays */
          /* first picture is on the left display, second on the right, third on */
          /* the left, forth on right, ... Only display left picture on mono display*/
          case IFF_ANHD_STEREOO5:
            _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT6 (BVDELTA STEREO)");
            bitplane_dlta_5(this);
            if( this->framenumber % 2   == 0 )
              this->skipframes          = 1;
            return;
            break;
          case IFF_ANHD_OPT7:
            if(anhd->bits == 0) {
              _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT7 (SHORT)");
              bitplane_dlta_7_short(this);
            } else {
              _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT7 (LONG)");
              bitplane_dlta_7_long(this);
            }
            break;
          case IFF_ANHD_OPT8:
            if(anhd->bits == 0) {
              _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT8 (SHORT)");
              bitplane_dlta_8_short(this);
            } else {
              _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT8 (LONG)");
              bitplane_dlta_8_long(this);
            }
            break;
          case IFF_ANHD_ASCIIJ:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim ASCIIJ is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          default:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: This anim-type is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
        }
        /* change old bitmap buffer (which now is the new one) with new buffer */
        buf_exchange                    = this->buf_uk;
        this->buf_uk                    = this->buf_uk_hist;
        this->buf_uk_hist               = buf_exchange;
        /* do the same with the index buffer */
        buf_exchange                    = this->index_buf;
        this->index_buf                 = this->index_buf_hist;
        this->index_buf_hist            = buf_exchange;
        /* and also with yuv buffer */
        buf_exchange                    = this->yuv_planes.y;
        this->yuv_planes.y              = this->yuv_planes_hist.y;
        this->yuv_planes_hist.y         = buf_exchange;
        buf_exchange                    = this->yuv_planes.u;
        this->yuv_planes.u              = this->yuv_planes_hist.u;
        this->yuv_planes_hist.u         = buf_exchange;
        buf_exchange                    = this->yuv_planes.v;
        this->yuv_planes.v              = this->yuv_planes_hist.v;
        this->yuv_planes_hist.v         = buf_exchange;
      }

      if( this->skipframes == 0 ) {
        switch (this->bytes_per_pixel) {
          case 1:
            /* HAM-pictrues need special handling */
            if( this->is_ham ) {
              /* Decode HAM-Pictures to YUV */
              bitplane_decode_ham( this->index_buf,          /* HAM-bitplane buffer     */
                                   &(this->yuv_planes),      /* YUV buffer              */
                                   this->width,              /* width                   */
                                   this->height,             /* hight                   */
                                   this->num_bitplanes,      /* number bitplanes        */
                                   this->bytes_per_pixel,    /* used Bytes per pixel    */
                                   this->rgb_palette);       /* Palette (RGB)           */
            }
            break;
          case 3:
            buf_exchange                = this->index_buf;
            for (i = 0; i < (this->height * this->width); i++) {
              r                         = *buf_exchange++;
              g                         = *buf_exchange++;
              b                         = *buf_exchange++;

              this->yuv_planes.y[i]     = COMPUTE_Y(r, g, b);
              this->yuv_planes.u[i]     = COMPUTE_U(r, g, b);
              this->yuv_planes.v[i]     = COMPUTE_V(r, g, b);
            }
            break;
          default:
            break;
        }

        yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

        img->draw(img, this->stream);
      }
      img->free(img);

      this->size                        = 0;
      if ( buf->decoder_info[1] > 90000 )
        xine_usec_sleep(buf->decoder_info[1]);
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void bitplane_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void bitplane_reset (video_decoder_t *this_gen) {
  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;

  this->size                            = 0;
}

static void bitplane_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void bitplane_dispose (video_decoder_t *this_gen) {
  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;

  free (this->buf);
  free (this->buf_uk);
  free (this->buf_uk_hist);
  free (this->index_buf);
  free (this->index_buf_hist);
  free (this->index_buf);

  if (this->decoder_ok) {
    this->decoder_ok                    = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  bitplane_decoder_t  *this             = (bitplane_decoder_t *) calloc(1, sizeof(bitplane_decoder_t));

  this->video_decoder.decode_data       = bitplane_decode_data;
  this->video_decoder.flush             = bitplane_flush;
  this->video_decoder.reset             = bitplane_reset;
  this->video_decoder.discontinuity     = bitplane_discontinuity;
  this->video_decoder.dispose           = bitplane_dispose;
  this->size                            = 0;

  this->stream                          = stream;
  this->class                           = (bitplane_class_t *) class_gen;

  this->decoder_ok                      = 0;
  this->buf                             = NULL;
  this->buf_uk                          = NULL;
  this->index_buf                       = NULL;
  this->index_buf                         = NULL;

  return &this->video_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  bitplane_class_t *this                = (bitplane_class_t *) calloc(1, sizeof(bitplane_class_t));

  this->decoder_class.open_plugin       = open_plugin;
  this->decoder_class.identifier        = "bitplane";
  this->decoder_class.description       = N_("Raw bitplane video decoder plugin");
  this->decoder_class.dispose           = default_video_decoder_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types[] = {
  BUF_VIDEO_BITPLANE,
  BUF_VIDEO_BITPLANE_BR1,
  0
};

static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "bitplane", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
