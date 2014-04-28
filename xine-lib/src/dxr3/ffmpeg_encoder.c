/*
 * Copyright (C) 2000-2014 the xine project
 *
 * This file is part of xine, a unix video player.
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

/* mpeg encoders for the dxr3 video out plugin. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

#define LOG_MODULE "dxr3_mpeg_encoder"
/* #define LOG_VERBOSE */
/* #define LOG */

#include "video_out_dxr3.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <avcodec.h>
#else
#  include <libavcodec/avcodec.h>
#  include <libavutil/mem.h>
#endif

#include "../combined/ffmpeg/ffmpeg_compat.h"

#if AVENCVIDEO == 1
/* buffer size for encoded mpeg1 stream; will hold one intra frame
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024
#endif


/* functions required by encoder api */
static int lavc_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int lavc_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int lavc_on_unneeded(dxr3_driver_t *drv);

/*encoder structure*/
typedef struct lavc_data_s {
  encoder_data_t     encoder_data;
  AVCodecContext     *context;         /* handle for encoding */
  int                width, height;    /* width and height of the video frame */
#if AVENCVIDEO == 1
  uint8_t            *ffmpeg_buffer;   /* lavc buffer */
#endif
  AVFrame            *picture;         /* picture to be encoded */
  uint8_t            *out[3];          /* aligned buffer for YV12 data */
  uint8_t            *buf;     /* base address of YV12 buffer */
} lavc_data_t;


static int dxr3_lavc_close(dxr3_driver_t *drv) {
  drv->enc->on_unneeded(drv);
  free(drv->enc);
  drv->enc = NULL;

  return 1;
}

int dxr3_lavc_init(dxr3_driver_t *drv, plugin_node_t *plugin)
{
  lavc_data_t* this;
  avcodec_init();

  avcodec_register_all();
  lprintf("lavc init , version %x\n", avcodec_version());
  this = calloc(1, sizeof(lavc_data_t));
  if (!this) return 0;

  this->encoder_data.type             = ENC_LAVC;
  this->encoder_data.on_update_format = lavc_on_update_format;
  this->encoder_data.on_frame_copy    = NULL;
  this->encoder_data.on_display_frame = lavc_on_display_frame;
  this->encoder_data.on_unneeded      = lavc_on_unneeded;
  this->context                       = 0;

  drv->enc = &this->encoder_data;
  drv->enc->on_close = dxr3_lavc_close;
  return 1;
}

/* helper function */
static int lavc_prepare_frame(lavc_data_t *this, dxr3_driver_t *drv, dxr3_frame_t *frame);

static int lavc_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  lavc_data_t *this = (lavc_data_t *)drv->enc;
  AVCodec *codec;
  unsigned char use_quantizer;

  if (this->context) {
    avcodec_close(this->context);
    free(this->context);
    free(this->picture);
    this->context = NULL;
    this->picture = NULL;
  }

  /* if YUY2 and dimensions changed, we need to re-allocate the
   * internal YV12 buffer */
  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    int image_size = frame->vo_frame.pitches[0] * frame->oheight;

    this->out[0] = this->buf = av_mallocz(image_size * 3/2);
    this->out[1] = this->out[0] + image_size;
    this->out[2] = this->out[1] + image_size/4;

    /* fill with black (yuv 16,128,128) */
    memset(this->out[0], 16, image_size);
    memset(this->out[1], 128, image_size/4);
    memset(this->out[2], 128, image_size/4);
    lprintf("Using YUY2->YV12 conversion\n");
  }

  /* resolution must be a multiple of two */
  if ((frame->vo_frame.pitches[0] % 2 != 0) || (frame->oheight % 2 != 0)) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
      "dxr3_mpeg_encoder: lavc only handles video dimensions which are multiples of 2\n");
    return 0;
  }

  /* get mpeg codec handle */
  codec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
  if (!codec) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
      "dxr3_mpeg_encoder: lavc MPEG1 codec not found\n");
    return 0;
  }
  lprintf("lavc MPEG1 encoder found.\n");

  this->width  = frame->vo_frame.pitches[0];
  this->height = frame->oheight;

  this->context = avcodec_alloc_context();
  if (!this->context) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
      "dxr3_mpeg_encoder: Couldn't start the ffmpeg library\n");
    return 0;
  }
  this->picture = avcodec_alloc_frame();
  if (!this->picture) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
      "dxr3_mpeg_encoder: Couldn't allocate ffmpeg frame\n");
    return 0;
  }

  /* mpeg1 encoder only support YUV420P */
  this->context->pix_fmt = PIX_FMT_YUVJ420P;

  /* put sample parameters */
  this->context->bit_rate = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.encoding.lavc_bitrate", 10000, 1000, 20000,
    _("libavcodec mpeg output bitrate (kbit/s)"),
    _("The bitrate the libavcodec mpeg encoder should use for DXR3's encoding mode. "
      "Higher values will increase quality and CPU usage.\n"
      "This setting is only considered, when constant quality mode is disabled."), 10, NULL, NULL);
    this->context->bit_rate *= 1000; /* config in kbit/s, libavcodec wants bit/s */

  use_quantizer = drv->class->xine->config->register_bool(drv->class->xine->config,
    "dxr3.encoding.lavc_quantizer", 1,
    _("constant quality mode"),
    _("When enabled, libavcodec will use a constant quality mode by dynamically "
      "compressing the images based on their complexity. When disabled, libavcodec "
      "will use constant bitrate mode."), 10, NULL, NULL);

  if (use_quantizer) {
    this->context->qmin = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.encoding.lavc_qmin", 1, 1, 10,
    _("minimum compression"),
    _("The minimum compression to apply to an image in constant quality mode."),
    10, NULL, NULL);

    this->context->qmax = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.encoding.lavc_qmax", 2, 1, 20,
    _("maximum quantizer"),
    _("The maximum compression to apply to an image in constant quality mode."),
    10, NULL, NULL);
  }

  lprintf("lavc -> bitrate %d  \n", this->context->bit_rate);

  this->context->width  = frame->vo_frame.pitches[0];
  this->context->height = frame->oheight;

  this->context->gop_size = 0; /*intra frames only */
  this->context->me_method = ME_ZERO; /*motion estimation type*/

  this->context->time_base.den = 90000;
  if (frame->vo_frame.duration > 90000 / 24)
    this->context->time_base.num = 90000 / 24;
  else if (frame->vo_frame.duration < 90000 / 60)
    this->context->time_base.num = 90000 / 60;
  else
    this->context->time_base.num = frame->vo_frame.duration;
  /* ffmpeg can complain about illegal framerates, but since this seems no
   * problem for the DXR3, we just tell ffmpeg to be more lax with */
  this->context->strict_std_compliance = -1;

  /* open avcodec */
  if (avcodec_open(this->context, codec) < 0) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG, "dxr3_mpeg_encoder: could not open codec\n");
    return 0;
  }
  lprintf("dxr3_mpeg_encoder: lavc MPEG1 codec opened.\n");

#if AVENCVIDEO == 1
  if (!this->ffmpeg_buffer)
    this->ffmpeg_buffer = (unsigned char *)malloc(DEFAULT_BUFFER_SIZE); /* why allocate more than needed ?! */
  if (!this->ffmpeg_buffer) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
      "dxr3_mpeg_encoder: Couldn't allocate temp buffer for mpeg data\n");
    return 0;
  }
#endif

  return 1;
}

static int lavc_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
#if AVENCVIDEO == 1
  int size;
#else /* 2 */
  AVPacket pkt = { 0 };
  int ret, got_output;
#endif
  lavc_data_t* this = (lavc_data_t *)drv->enc;
  ssize_t written;

  if (frame->vo_frame.bad_frame) return 1;
    /* ignore old frames */
  if ((frame->vo_frame.pitches[0] != this->context->width) || (frame->oheight != this->context->height)) {
	frame->vo_frame.free(&frame->vo_frame);
    lprintf("LAVC ignoring frame !!!\n");
    return 1;
  }

  /* prepare frame for conversion, handles YUY2 -> YV12 conversion when necessary */
  lavc_prepare_frame(this, drv, frame);

  /* do the encoding */
#if AVENCVIDEO == 1
  size = avcodec_encode_video(this->context, this->ffmpeg_buffer, DEFAULT_BUFFER_SIZE, this->picture);
#else /* 2 */
  ret = avcodec_encode_video2(this->context, &pkt, this->picture, &got_output);
#endif

  frame->vo_frame.free(&frame->vo_frame);

#if AVENCVIDEO == 1
  if (size < 0)
#else /* 2 */
  if (ret < 0)
#endif
  {
      xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
        "dxr3_mpeg_encoder: encoding failed\n");
      return 0;
  }
#if AVENCVIDEO == 2
  else if (!got_output)
      return 1;
#endif

#if AVENCVIDEO == 1
  written = write(drv->fd_video, this->ffmpeg_buffer, size);
#else
  written = write(drv->fd_video, pkt.data, pkt.size);
#endif

  if (written < 0) {
#if AVENCVIDEO == 2
      av_packet_unref(&pkt);
#endif
      xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
        "dxr3_mpeg_encoder: video device write failed (%s)\n", strerror(errno));
      return 0;
    }
#if AVENCVIDEO == 1
  if (written != size)
      xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
        "dxr3_mpeg_encoder: Could only write %zd of %d mpeg bytes.\n", written, size);
#else /* 2 */
  if (written != pkt.size)
      xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
        "dxr3_mpeg_encoder: Could only write %zd of %d mpeg bytes.\n", written, pkt.size);
  av_packet_unref(&pkt);
#endif
  return 1;
}

static int lavc_on_unneeded(dxr3_driver_t *drv)
{
  lavc_data_t *this = (lavc_data_t *)drv->enc;
  lprintf("flushing buffers\n");
  if (this->context) {
    avcodec_close(this->context);
    free(this->context);
    free(this->picture);
    this->context = NULL;
    this->picture = NULL;
  }
  return 1;
}

static int lavc_prepare_frame(lavc_data_t *this, dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  int i, j, w2;
  uint8_t *yuy2;

  if (frame->vo_frame.bad_frame) return 1;

  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    /* need YUY2->YV12 conversion */
    if (!(this->out[0] && this->out[1] && this->out[2]) ) {
      lprintf("Internal YV12 buffer not created.\n");
      return 0;
    }
    this->picture->data[0] = this->out[0] +  frame->vo_frame.pitches[0]      *  drv->top_bar;		/* y */
    this->picture->data[1] = this->out[1] + (frame->vo_frame.pitches[0] / 2) * (drv->top_bar / 2);	/* u */
    this->picture->data[2] = this->out[2] + (frame->vo_frame.pitches[0] / 2) * (drv->top_bar / 2);	/* v */
    yuy2 = frame->vo_frame.base[0];
    w2 = frame->vo_frame.pitches[0] / 2;
    for (i = 0; i < frame->vo_frame.height; i += 2) {
      for (j = 0; j < w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(this->picture->data[0]++) = *(yuy2++);
        *(this->picture->data[1]++) = *(yuy2++);
        *(this->picture->data[0]++) = *(yuy2++);
        *(this->picture->data[2]++) = *(yuy2++);
      }
      /* down sampling */
      for (j = 0; j < w2; j++) {
        /* skip every second line for U and V */
        *(this->picture->data[0]++) = *(yuy2++);
        yuy2++;
        *(this->picture->data[0]++) = *(yuy2++);
        yuy2++;
      }
    }
    /* reset for encoder */
    this->picture->data[0] = this->out[0];
    this->picture->data[1] = this->out[1];
    this->picture->data[2] = this->out[2];
  }
  else { /* YV12 **/
	this->picture->data[0] = frame->real_base[0];
    this->picture->data[1] = frame->real_base[1];
    this->picture->data[2] = frame->real_base[2];
  }
  this->picture->linesize[0] = this->context->width;
  this->picture->linesize[1] = this->context->width / 2;
  this->picture->linesize[2] = this->context->width / 2;
  return 1;
}
