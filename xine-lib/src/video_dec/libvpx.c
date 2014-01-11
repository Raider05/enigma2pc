/*
 * Copyright (C) 2013 the xine project
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
 * libvpx decoder wrapped by Petri Hintukainen <phintuka@users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

typedef struct {
  video_decoder_class_t decoder_class;
  uint32_t              buffer_type;
} vpx_class_t;

typedef struct vpx_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  vpx_class_t      *class;
  xine_stream_t    *stream;

  int64_t              pts;
  struct vpx_codec_ctx ctx;
  int                  decoder_ok;  /* current decoder status */

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */
  int               frame_flags; /* color matrix and fullrange */

} vpx_decoder_t;

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

static void vpx_handle_header(vpx_decoder_t *this, buf_element_t *buf)
{
  xine_bmiheader *bih;

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  bih = (xine_bmiheader *) buf->content;
  this->width = (bih->biWidth + 1) & ~1;
  this->height = (bih->biHeight + 1) & ~1;

  if (buf->decoder_flags & BUF_FLAG_ASPECT)
    this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
  else
    this->ratio = (double)this->width / (double)this->height;

  free (this->buf);
  this->buf = NULL;
  this->bufsize = 0;
  this->size = 0;

  this->decoder_ok = 1;

  switch (buf->type) {
    case BUF_VIDEO_VP8:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "VP8");
      break;
    case BUF_VIDEO_VP9:
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "VP9");
      break;
  }

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
}

static void vpx_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  vpx_decoder_t *this = (vpx_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    return;
  }

  /* optional demux override (matroska/webm) */
  if (buf->decoder_flags & BUF_FLAG_COLOR_MATRIX) {
    VO_SET_FLAGS_CM (buf->decoder_info[4], this->frame_flags);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    vpx_handle_header(this, buf);
    return;
  }

  if (!this->decoder_ok || buf->decoder_flags & BUF_FLAG_SPECIAL) {
    return;
  }

  /* collect data */
  if (this->size + buf->size > this->bufsize) {
    this->bufsize = this->size + 2 * buf->size;
    this->buf = realloc (this->buf, this->bufsize);
  }
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  /* save pts */
  if (buf->pts > 0) {
    this->pts = buf->pts;
  }

  if (!(buf->decoder_flags & BUF_FLAG_FRAME_END)) {
    return;
  }

  /* decode */

  struct vpx_codec_ctx *ctx = &this->ctx;
  vpx_codec_err_t err;
  vo_frame_t *img;
  int64_t pts, *p_pts;

  p_pts = malloc(sizeof(*p_pts));
  *p_pts = this->pts;
  err = vpx_codec_decode(ctx, this->buf, this->size, p_pts, 0);

  this->size = 0;

  if (err != VPX_CODEC_OK) {
    const char *error  = vpx_codec_error(ctx);
    const char *detail = vpx_codec_error_detail(ctx);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            LOG_MODULE": Failed to decode frame: %s (%s)\n",
            error, detail ? detail : "");
    free(p_pts);
    return;
  }

  const void *iter = NULL;
  struct vpx_image *vpx_img = vpx_codec_get_frame(ctx, &iter);
  if (!vpx_img)
    return;

  p_pts = vpx_img->user_priv;
  vpx_img->user_priv = NULL;
  pts = *p_pts;
  free(p_pts);

  if (vpx_img->fmt != VPX_IMG_FMT_I420) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            LOG_MODULE": Unsupported color space %d\n", vpx_img->fmt);
    return;
  }

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            this->width, this->height,
                                            this->ratio, XINE_IMGFMT_YV12,
                                            this->frame_flags | VO_BOTH_FIELDS);

  yv12_to_yv12(
               /* Y */
               vpx_img->planes[0], vpx_img->stride[0],
               img->base[0], img->pitches[0],
               /* U */
               vpx_img->planes[1], vpx_img->stride[1],
               img->base[1], img->pitches[1],
               /* V */
               vpx_img->planes[2], vpx_img->stride[2],
               img->base[2], img->pitches[2],
               /* width x height */
               this->width, this->height);

  img->pts       = pts;
  img->bad_frame = 0;
  img->progressive_frame = 1;

  img->draw(img, this->stream);
  img->free(img);
}

static void vpx_flush (video_decoder_t *this_gen)
{
}

static void vpx_reset (video_decoder_t *this_gen)
{
  vpx_decoder_t *this = (vpx_decoder_t *) this_gen;

  if (this->decoder_ok) {
    const void *iter = NULL;
    while (1) {
      struct vpx_image *img = vpx_codec_get_frame(&this->ctx, &iter);
      if (!img)
        break;
      free(img->user_priv);
      img->user_priv = NULL;
    }
  }

  this->size = 0;
}

static void vpx_discontinuity (video_decoder_t *this_gen)
{
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vpx_dispose (video_decoder_t *this_gen)
{
  vpx_decoder_t *this = (vpx_decoder_t *) this_gen;

  const void *iter = NULL;
  while (1) {
    struct vpx_image *img = vpx_codec_get_frame(&this->ctx, &iter);
    if (!img)
      break;
    free(img->user_priv);
    img->user_priv = NULL;
  }

  vpx_codec_destroy(&this->ctx);

  free (this->buf);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  vpx_class_t    *cls = (vpx_class_t *)class_gen;
  vpx_decoder_t  *this;

  const struct vpx_codec_iface *iface;
  struct vpx_codec_dec_cfg deccfg = { 0 };
  int vp_version;

  switch (cls->buffer_type) {
    case BUF_VIDEO_VP8:
      iface = &vpx_codec_vp8_dx_algo;
      vp_version = 8;
      break;
#ifdef HAVE_VPX_VP9_DECODER
    case BUF_VIDEO_VP9:
      iface = &vpx_codec_vp9_dx_algo;
      vp_version = 9;
      break;
#endif
    default:
      return NULL;
  }


  this = (vpx_decoder_t *) calloc(1, sizeof(vpx_decoder_t));

  this->video_decoder.decode_data         = vpx_decode_data;
  this->video_decoder.flush               = vpx_flush;
  this->video_decoder.reset               = vpx_reset;
  this->video_decoder.discontinuity       = vpx_discontinuity;
  this->video_decoder.dispose             = vpx_dispose;

  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (vpx_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  /* VP8/9 seems not to transport color matrix and fullrange info.
     So lets at least tell video out to do image size based selection. */
  this->frame_flags   = 0;
  VO_SET_FLAGS_CM (4, this->frame_flags); /* undefined, mpeg range */

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
          LOG_MODULE "VP%d: using libvpx version %s\n",
          vp_version, vpx_codec_version_str());

  if (vpx_codec_dec_init(&this->ctx, iface, &deccfg, 0) != VPX_CODEC_OK) {
    const char *err = vpx_codec_error(&this->ctx);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            LOG_MODULE": Failed to initialize VP%d decoder: %s\n",
            vp_version, err);
    free(this);
    return NULL;
  }

  return &this->video_decoder;
}

static void *init_plugin (xine_t *xine, uint32_t buffer_type, const char *identifier)
{
  vpx_class_t *this;

  this = (vpx_class_t *) calloc(1, sizeof(vpx_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = identifier;
  this->decoder_class.description     = N_("WebM (VP8/VP9) video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  this->buffer_type = buffer_type;

  return this;
}

static void *init_plugin_vp8 (xine_t *xine, void *data)
{
  return init_plugin(xine, BUF_VIDEO_VP8, "libvpx-vp8");
}

#ifdef HAVE_VPX_VP9_DECODER
static void *init_plugin_vp9 (xine_t *xine, void *data)
{
  return init_plugin(xine, BUF_VIDEO_VP9, "libvpx-vp9");
}
#endif

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types_vp8[] = {
  BUF_VIDEO_VP8,
  0
};

#ifdef HAVE_VPX_VP9_DECODER
static const uint32_t video_types_vp9[] = {
  BUF_VIDEO_VP9,
  0
};
#endif

static const decoder_info_t dec_info_video_vp8 = {
  video_types_vp8,     /* supported types */
  1                    /* priority        */
};

#ifdef HAVE_VPX_VP9_DECODER
static const decoder_info_t dec_info_video_vp9 = {
  video_types_vp9,     /* supported types */
  1                    /* priority        */
};
#endif

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "libvpx-vp8", XINE_VERSION_CODE, &dec_info_video_vp8, init_plugin_vp8 },
#ifdef HAVE_VPX_VP9_DECODER
  { PLUGIN_VIDEO_DECODER, 19, "libvpx-vp9", XINE_VERSION_CODE, &dec_info_video_vp9, init_plugin_vp9 },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
