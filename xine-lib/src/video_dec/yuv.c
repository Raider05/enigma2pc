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
 * YUV "Decoder" by Mike Melanson (melanson@pcisys.net)
 * Actually, this decoder just reorganizes chunks of raw YUV data in such
 * a way that xine can display them.
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

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} yuv_class_t;

typedef struct yuv_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  yuv_class_t      *class;
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

  int               progressive;
  int               top_field_first;
  int               color_matrix;

} yuv_decoder_t;

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void yuv_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;
  xine_bmiheader *bih;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    (this->stream->video_out->open) (this->stream->video_out, this->stream);

    bih = (xine_bmiheader *) buf->content;
    this->width = bih->biWidth;
    this->height = bih->biHeight;

    if (buf->decoder_flags & BUF_FLAG_ASPECT)
      this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
    else
      this->ratio = (double)this->width / (double)this->height;

    this->progressive = buf->decoder_info[3];
    this->top_field_first = buf->decoder_info[4];

    this->color_matrix = 4; /* undefined, mpeg range */

    free (this->buf);
    this->buf = NULL;

    this->bufsize = 0;
    this->size = 0;

    this->decoder_ok = 1;

    /* load the stream/meta info */
    switch (buf->type) {

      case BUF_VIDEO_YUY2:
        this->width = (this->width + 1) & ~1;
        this->bufsize = this->width * this->height * 2;
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw YUY2");
        break;

      case BUF_VIDEO_YV12:
        this->width = (this->width + 1) & ~1;
        this->height = (this->height + 1) & ~1;
        this->bufsize = this->width * this->height * 3 / 2;
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw YV12");
        break;

      case BUF_VIDEO_YVU9:
        this->width = (this->width + 3) & ~3;
        this->height = (this->height + 3) & ~3;
        this->bufsize = this->width * this->height * 9 / 8;
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw YVU9");
        break;

      case BUF_VIDEO_GREY:
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Greyscale YUV");
        break;

      case BUF_VIDEO_I420:
        this->width = (this->width + 1) & ~1;
        this->height = (this->height + 1) & ~1;
        this->bufsize = this->width * this->height * 3 / 2;
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw I420");
        break;

    }

    this->buf = malloc(this->bufsize);

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);

    return;
  } else if (this->decoder_ok && !(buf->decoder_flags & BUF_FLAG_SPECIAL)) {
    uint8_t *src;

    /* if buffer contains an entire frame then there's no need to copy it
     * into our internal buffer */
    if ((buf->decoder_flags & BUF_FLAG_FRAME_START) &&
        (buf->decoder_flags & BUF_FLAG_FRAME_END))
      src = buf->content;
    else {
      if (this->size + buf->size > this->bufsize) {
        this->bufsize = this->size + 2 * buf->size;
        this->buf = realloc (this->buf, this->bufsize);
      }

      xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

      this->size += buf->size;

      src = this->buf;
    }

    if (buf->decoder_flags & BUF_FLAG_COLOR_MATRIX)
      this->color_matrix = buf->decoder_info[4];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      if (buf->type == BUF_VIDEO_YUY2) {

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

        yuy2_to_yuy2(
         /* src */
          src, this->width*2,
         /* dst */
          img->base[0], img->pitches[0],
         /* width x height */
          this->width, this->height);

      } else if (buf->type == BUF_VIDEO_YV12) {

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

        yv12_to_yv12(
         /* Y */
          src, this->width,
          img->base[0], img->pitches[0],
         /* U */
          src + (this->width * this->height * 5/4), this->width/2,
          img->base[1], img->pitches[1],
         /* V */
          src + (this->width * this->height), this->width/2,
          img->base[2], img->pitches[2],
         /* width x height */
          this->width, this->height);

      } else if (buf->type == BUF_VIDEO_I420) {

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

        yv12_to_yv12(
         /* Y */
          src, this->width,
          img->base[0], img->pitches[0],
         /* U */
          src + (this->width * this->height), this->width/2,
          img->base[1], img->pitches[1],
         /* V */
          src + (this->width * this->height * 5/4), this->width/2,
          img->base[2], img->pitches[2],
         /* width x height */
          this->width, this->height);

      } else if (buf->type == BUF_VIDEO_YVU9) {

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);


        yuv9_to_yv12(
         /* Y */
          src,
          this->width,
          img->base[0],
          img->pitches[0],
         /* U */
          src + (this->width * this->height),
          this->width / 4,
          img->base[1],
          img->pitches[1],
         /* V */
          src + (this->width * this->height) +
            (this->width * this->height / 16),
          this->width / 4,
          img->base[2],
          img->pitches[2],
         /* width x height */
          this->width,
          this->height);

      } else if (buf->type == BUF_VIDEO_GREY) {

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

        xine_fast_memcpy(img->base[0], src, this->width * this->height);
        memset( img->base[1], 0x80, this->width * this->height / 4 );
        memset( img->base[2], 0x80, this->width * this->height / 4 );

      } else {

        /* just allocate something to avoid compiler warnings */
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                          this->width, this->height,
                                          this->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

      }

      VO_SET_FLAGS_CM (this->color_matrix, img->flags);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

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
static void yuv_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void yuv_reset (video_decoder_t *this_gen) {
  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;

  this->size = 0;
}

static void yuv_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void yuv_dispose (video_decoder_t *this_gen) {
  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;

  free (this->buf);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  yuv_decoder_t  *this ;

  this = (yuv_decoder_t *) calloc(1, sizeof(yuv_decoder_t));

  this->video_decoder.decode_data         = yuv_decode_data;
  this->video_decoder.flush               = yuv_flush;
  this->video_decoder.reset               = yuv_reset;
  this->video_decoder.discontinuity       = yuv_discontinuity;
  this->video_decoder.dispose             = yuv_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (yuv_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  yuv_class_t *this;

  this = (yuv_class_t *) calloc(1, sizeof(yuv_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "YUV";
  this->decoder_class.description     = N_("Raw YUV video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types[] = {
  BUF_VIDEO_YUY2,
  BUF_VIDEO_YV12,
  BUF_VIDEO_YVU9,
  BUF_VIDEO_GREY,
  BUF_VIDEO_I420,
  0
 };

static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "yuv", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
