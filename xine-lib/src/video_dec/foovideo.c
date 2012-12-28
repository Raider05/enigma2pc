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
 * foovideo.c: This is a reference video decoder for the xine multimedia
 * player. It really works too! It will output frames of packed YUY2 data
 * where each byte in the map is the same value, which is 3 larger than the
 * value from the last frame. This creates a slowly rotating solid color
 * frame when the frames are played in succession.
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
} foovideo_class_t;

typedef struct foovideo_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  foovideo_class_t *class;
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

  /* these are variables exclusive to the foo video decoder */
  unsigned char     current_yuv_byte;

} foovideo_decoder_t;

/**************************************************************************
 * foovideo specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void foovideo_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;
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

    free(this->buf);

    bih = (xine_bmiheader *) buf->content;
    this->width = bih->biWidth;
    this->height = bih->biHeight;
    this->ratio = (double)this->width/(double)this->height;

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    /* take this opportunity to load the stream/meta info */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "foovideo");

    /* do anything else relating to initializing this decoder */
    this->current_yuv_byte = 0;

    this->decoder_ok = 1;

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
                                        this->ratio,
                                        XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      memset(img->base[0], this->current_yuv_byte,
        this->width * this->height * 2);
      this->current_yuv_byte += 3;

      img->draw(img, this->stream);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system.
 */
static void foovideo_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void foovideo_reset (video_decoder_t *this_gen) {
  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * The decoder should forget any stored pts values here.
 */
static void foovideo_discontinuity (video_decoder_t *this_gen) {
  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void foovideo_dispose (video_decoder_t *this_gen) {

  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

  free (this->buf);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  foovideo_decoder_t  *this ;

  this = (foovideo_decoder_t *) calloc(1, sizeof(foovideo_decoder_t));

  this->video_decoder.decode_data         = foovideo_decode_data;
  this->video_decoder.flush               = foovideo_flush;
  this->video_decoder.reset               = foovideo_reset;
  this->video_decoder.discontinuity       = foovideo_discontinuity;
  this->video_decoder.dispose             = foovideo_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (foovideo_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

/*
 * This function frees the video decoder class and any other memory that was
 * allocated.
 */
static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  foovideo_class_t *this;

  this = (foovideo_class_t *) calloc(1, sizeof(foovideo_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "foovideo";
  this->decoder_class.description     = N_("foovideo: reference xine video decoder plugin");
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
static const uint32_t video_types[] = {
  /* BUF_VIDEO_FOOVIDEO, */
  BUF_VIDEO_VQA,
  BUF_VIDEO_SORENSON_V3,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  5                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER, 19, "foovideo", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
