/*
 * Copyright (C) 2000-2003 the xine project
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
 * Raw RGB "Decoder" by Mike Melanson (melanson@pcisys.net)
 * Actually, this decoder just converts a raw RGB image to a YUY2 map
 * suitable for display under xine.
 *
 * This decoder deals with raw RGB data from Microsoft and Quicktime files.
 * Data from a MS file can be 32-, 24-, 16-, or 8-bit. The latter can also
 * be grayscale, depending on whether a palette is present. Data from a QT
 * file can be 32-, 24-, 16-, 8-, 4-, 2-, or 1-bit. Any resolutions <= 8
 * can also be greyscale depending on what the QT file specifies.
 *
 * One more catch: Raw RGB from a Microsoft file is upside down. This is
 * indicated by a negative height parameter.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_MODULE "rgb"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"

typedef struct {
  video_decoder_class_t   decoder_class;
} rgb_class_t;

typedef struct rgb_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  rgb_class_t      *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */
  int               bytes_per_pixel;
  int               bit_depth;
  int               upside_down;

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;

} rgb_decoder_t;

static void rgb_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;
  xine_bmiheader *bih;
  palette_entry_t *palette;
  int i;
  int pixel_ptr, row_ptr;
  int palette_index;
  int buf_ptr;
  unsigned int packed_pixel;
  unsigned char r, g, b;
  int pixels_left;
  unsigned char pixel_byte = 0;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
    (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info_ptr[2];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    (this->stream->video_out->open) (this->stream->video_out, this->stream);

    bih = (xine_bmiheader *) buf->content;
    this->width = (bih->biWidth + 3) & ~0x03;
    this->height = (bih->biHeight + 3) & ~0x03;
    if (this->height < 0) {
      this->upside_down = 1;
      this->height = -this->height;
    } else {
      this->upside_down = 0;
    }
    this->ratio = (double)this->width/(double)this->height;

    this->bit_depth = bih->biBitCount;
    if (this->bit_depth > 32)
      this->bit_depth &= 0x1F;
    /* round this number up in case of 15 */
    lprintf("width = %d, height = %d, bit_depth = %d\n", this->width, this->height, this->bit_depth);

    this->bytes_per_pixel = (this->bit_depth + 1) / 8;

    free (this->buf);

    /* minimal buffer size */
    this->bufsize = this->width * this->height * this->bytes_per_pixel;
    this->buf = calloc(1, this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw RGB");

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;


      /* iterate through each row */
      buf_ptr = 0;

      if (this->upside_down) {
        for (row_ptr = this->yuv_planes.row_width * (this->yuv_planes.row_count - 1);
          row_ptr >= 0; row_ptr -= this->yuv_planes.row_width) {
          for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {

            if (this->bytes_per_pixel == 1) {

              palette_index = this->buf[buf_ptr++];

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 0];
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 1];
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 2];

            } else if (this->bytes_per_pixel == 2) {

              /* ABGR1555 format, little-endian order */
              packed_pixel = _X_LE_16(&this->buf[buf_ptr]);
              buf_ptr += 2;
              UNPACK_BGR15(packed_pixel, r, g, b);

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                COMPUTE_Y(r, g, b);
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                COMPUTE_U(r, g, b);
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                COMPUTE_V(r, g, b);

            } else {

              /* BGR24 or BGRA32 */
              b = this->buf[buf_ptr++];
              g = this->buf[buf_ptr++];
              r = this->buf[buf_ptr++];

              /* the next line takes care of 'A' in the 32-bit case */
              buf_ptr += this->bytes_per_pixel - 3;

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                COMPUTE_Y(r, g, b);
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                COMPUTE_U(r, g, b);
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                COMPUTE_V(r, g, b);

            }
          }
        }
      } else {

        for (row_ptr = 0; row_ptr < this->yuv_planes.row_width * this->yuv_planes.row_count; row_ptr += this->yuv_planes.row_width) {
          pixels_left = 0;
          for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {

            if (this->bit_depth == 1) {

              if (pixels_left == 0) {
                pixels_left = 8;
                pixel_byte = *this->buf++;
              }

              if (pixel_byte & 0x80) {
                this->yuv_planes.y[row_ptr + pixel_ptr] =
                  this->yuv_palette[1 * 4 + 0];
                this->yuv_planes.u[row_ptr + pixel_ptr] =
                  this->yuv_palette[1 * 4 + 1];
                this->yuv_planes.v[row_ptr + pixel_ptr] =
                  this->yuv_palette[1 * 4 + 2];
              } else {
                this->yuv_planes.y[row_ptr + pixel_ptr] =
                  this->yuv_palette[0 * 4 + 0];
                this->yuv_planes.u[row_ptr + pixel_ptr] =
                  this->yuv_palette[0 * 4 + 1];
                this->yuv_planes.v[row_ptr + pixel_ptr] =
                  this->yuv_palette[0 * 4 + 2];
              }
              pixels_left--;
              pixel_byte <<= 1;

            } else if (this->bit_depth == 2) {

              if (pixels_left == 0) {
                pixels_left = 4;
                pixel_byte = *this->buf++;
              }

              palette_index = (pixel_byte & 0xC0) >> 6;
              this->yuv_planes.y[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 0];
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 1];
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 2];

              pixels_left--;
              pixel_byte <<= 2;

            } else if (this->bit_depth == 4) {

              if (pixels_left == 0) {
                pixels_left = 2;
                pixel_byte = *this->buf++;
              }

              palette_index = (pixel_byte & 0xF0) >> 4;
              this->yuv_planes.y[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 0];
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 1];
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 2];

              pixels_left--;
              pixel_byte <<= 4;

            } else if (this->bytes_per_pixel == 1) {

              palette_index = this->buf[buf_ptr++];

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 0];
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 1];
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                this->yuv_palette[palette_index * 4 + 2];

            } else if (this->bytes_per_pixel == 2) {

              /* ARGB1555 format, big-endian order */
              packed_pixel = _X_BE_16(&this->buf[buf_ptr]);
              buf_ptr += 2;
              UNPACK_RGB15(packed_pixel, r, g, b);

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                COMPUTE_Y(r, g, b);
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                COMPUTE_U(r, g, b);
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                COMPUTE_V(r, g, b);

            } else {

              /* RGB24 or ARGB32; the next line takes care of 'A' in the
               * 32-bit case */
              buf_ptr += this->bytes_per_pixel - 3;

              r = this->buf[buf_ptr++];
              g = this->buf[buf_ptr++];
              b = this->buf[buf_ptr++];

              this->yuv_planes.y[row_ptr + pixel_ptr] =
                COMPUTE_Y(r, g, b);
              this->yuv_planes.u[row_ptr + pixel_ptr] =
                COMPUTE_U(r, g, b);
              this->yuv_planes.v[row_ptr + pixel_ptr] =
                COMPUTE_V(r, g, b);

            }
          }
        }
      }

      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      img->draw(img, this->stream);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void rgb_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void rgb_reset (video_decoder_t *this_gen) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  this->size = 0;
}

static void rgb_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void rgb_dispose (video_decoder_t *this_gen) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  free (this->buf);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  rgb_decoder_t  *this ;

  this = (rgb_decoder_t *) calloc(1, sizeof(rgb_decoder_t));

  this->video_decoder.decode_data         = rgb_decode_data;
  this->video_decoder.flush               = rgb_flush;
  this->video_decoder.reset               = rgb_reset;
  this->video_decoder.discontinuity       = rgb_discontinuity;
  this->video_decoder.dispose             = rgb_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (rgb_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  rgb_class_t *this;

  this = (rgb_class_t *) calloc(1, sizeof(rgb_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "RGB";
  this->decoder_class.description     = N_("Raw RGB video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types[] = {
  BUF_VIDEO_RGB,
  0
 };

static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "rgb", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
