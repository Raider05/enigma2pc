/*
 * Copyright (C) 2000-2003 the xine project
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

/* mpeg encoders for the dxr3 video out plugin.
 * supports the libfame and librte mpeg encoder libraries.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_LIBRTE
#  include <unistd.h>
#  include <rte.h>
#endif
#ifdef HAVE_LIBFAME
#  include <fame.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#define LOG_MODULE "dxr3_mpeg_encoder"
/* #define LOG_VERBOSE */
/* #define LOG */

#include <xine/xineutils.h>
#include "video_out_dxr3.h"

/* buffer size for encoded mpeg1 stream; will hold one intra frame
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024


#ifdef HAVE_LIBRTE
/* initialization function */
int         dxr3_rte_init(dxr3_driver_t *drv);

/* functions required by encoder api */
static int  rte_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int  rte_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int  rte_on_unneeded(dxr3_driver_t *drv);
static int  rte_on_close(dxr3_driver_t *drv);

/* helper function */
static void mp1e_callback(rte_context *context, void *data, ssize_t size,
                          void *user_data);

/* encoder structure */
typedef struct rte_data_s {
  encoder_data_t  encoder_data;
  rte_context    *context;       /* handle for encoding */
  int             width, height;
  void           *rte_ptr;       /* buffer maintened by librte */
  double          rte_bitrate;   /* mpeg out bitrate, default 2.3e6 bits/s */
} rte_data_t;
#endif

#ifdef HAVE_LIBFAME
/* initialization function */
int        dxr3_fame_init(dxr3_driver_t *drv);

/* functions required by encoder api */
static int fame_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int fame_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int fame_on_unneeded(dxr3_driver_t *drv);
static int fame_on_close(dxr3_driver_t *drv);

/* encoder structure */
typedef struct {
  encoder_data_t     encoder_data;
  fame_context_t    *context; /* needed for fame calls */
  fame_parameters_t  fp;
  fame_yuv_t         yuv;
  char              *buffer;  /* temporary buffer for mpeg data */
                              /* temporary buffer for YUY2->YV12 conversion */
  uint8_t           *out[3];  /* aligned buffer for YV12 data */
  uint8_t           *buf;     /* base address of YV12 buffer */
} fame_data_t;

/* helper function */
static int fame_prepare_frame(fame_data_t *this, dxr3_driver_t *drv,
                              dxr3_frame_t *frame);
#endif

#ifdef HAVE_LIBRTE
int dxr3_rte_init(dxr3_driver_t *drv)
{
  rte_data_t* this;

  if (!rte_init()) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG, _("dxr3_mpeg_encoder: failed to init librte\n"));
    return 0;
  }

  this = calloc(1, sizeof(rte_data_t));
  if (!this) return 0;

  this->encoder_data.type             = ENC_RTE;
  this->encoder_data.on_update_format = rte_on_update_format;
  this->encoder_data.on_frame_copy    = NULL;
  this->encoder_data.on_display_frame = rte_on_display_frame;
  this->encoder_data.on_unneeded       = rte_on_unneeded;
  this->encoder_data.on_close         = rte_on_close;
  this->context                       = 0;

  drv->enc = &this->encoder_data;
  return 1;
}

static int rte_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  rte_data_t *this = (rte_data_t *)drv->enc;
  rte_context *context;
  rte_codec *codec;
  double fps;

  if (this->context) { /* already running */
    lprintf("closing current encoding context.\n");
    rte_stop(this->context);
    rte_context_destroy(this->context);
    this->context = 0;
  }

  if ((frame->vo_frame.pitches[0] % 16 != 0) || (frame->oheight % 16 != 0)) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_mpeg_encoder: rte only handles video dimensions which are multiples of 16\n"));
    return 0;
  }

  this->width = frame->vo_frame.pitches[0];
  this->height = frame->oheight;

  /* create new rte context */
  this->context = rte_context_new(this->width, this->height, "mp1e", drv);
  if (!this->context) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG, _("dxr3_mpeg_encoder: failed to get rte context.\n"));
    return 0;
  }
  context = this->context; /* shortcut */
#if LOG_ENC
  rte_set_verbosity(context, 2);
#endif

  /* get mpeg codec handle */
  codec = rte_codec_set(context, RTE_STREAM_VIDEO, 0, "mpeg1_video");
  if (!codec) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG, _("dxr3_mpeg_encoder: could not create codec.\n"));
    rte_context_destroy(context);
    this->context = 0;
    return 0;
  }

  this->rte_bitrate = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.encoding.rte_bitrate", 10000, 1000, 20000,
    _("rte mpeg output bitrate (kbit/s)"),
    _("The bitrate the mpeg encoder library librte should use for DXR3's encoding mode. "
      "Higher values will increase quality and CPU usage."), 10, NULL, NULL);
  this->rte_bitrate *= 1000; /* config in kbit/s, rte wants bit/s */

  /* FIXME: this needs to be replaced with a codec option call.
   * However, there seems to be none for the colour format!
   * So we'll use the deprecated set_video_parameters instead.
   * Alternative is to manually set context->video_format (RTE_YU... )
   * and context->video_bytes (= width * height * bytes/pixel)
   */
  rte_set_video_parameters(context,
    (frame->vo_frame.format == XINE_IMGFMT_YV12 ? RTE_YUV420 : RTE_YUYV),
    context->width, context->height,
    context->video_rate, context->output_video_bits,
    context->gop_sequence);

  /* Now set a whole bunch of codec options
   * If I understand correctly, virtual_frame_rate is the frame rate
   * of the source (can be anything), while coded_frame_rate must be
   * one of the mpeg1 alloweds
   */
  fps = 90000.0 / frame->vo_frame.duration;
  if (!rte_option_set(codec, "virtual_frame_rate", fps))
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: WARNING: rte_option_set failed; virtual_frame_rate = %g.\n", fps);
  if (!rte_option_set(codec, "coded_frame_rate", fps))
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: WARNING: rte_option_set failed; coded_frame_rate = %g.\n", fps);
  if (!rte_option_set(codec, "bit_rate", (int)this->rte_bitrate))
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: WARNING: rte_option_set failed; bit_rate = %d.\n", (int)this->rte_bitrate);
  if (!rte_option_set(codec, "gop_sequence", "I"))
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: WARNING: rte_option_set failed; gop_sequence = \"I\".\n");
  /* just to be sure, disable motion comp (not needed in I frames) */
  if (!rte_option_set(codec, "motion_compensation", 0))
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: WARNING: rte_option_set failed; motion_compensation = 0.\n");

  rte_set_input(context, RTE_VIDEO, RTE_PUSH, FALSE, NULL, NULL, NULL);
  rte_set_output(context, mp1e_callback, NULL, NULL);

  if (!rte_init_context(context)) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_mpeg_encoder: cannot init the context: %s\n"), context->error);
    rte_context_destroy(context);
    this->context = 0;
    return 0;
  }
  /* do the sync'ing and start encoding */
  if (!rte_start_encoding(context)) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_mpeg_encoder: cannot start encoding: %s\n"), context->error);
    rte_context_destroy(context);
    this->context = 0;
    return 0;
  }
  this->rte_ptr = rte_push_video_data(context, NULL, 0);
  if (!this->rte_ptr) {
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: failed to get encoder buffer pointer.\n");
    return 0;
  }

  return 1;
}

static int rte_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  int size;
  rte_data_t* this = (rte_data_t *)drv->enc;

  if ((this->width == frame->vo_frame.pitches[0]) && (this->height == frame->oheight)) {
    /* This frame belongs to current context. */
    size = frame->vo_frame.pitches[0] * frame->oheight;
    if (frame->vo_frame.format == XINE_IMGFMT_YV12)
      xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size * 3/2);
    else
      xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size * 2);
    this->rte_ptr = rte_push_video_data(this->context, this->rte_ptr,
      frame->vo_frame.vpts / 90000.0);
  }
  frame->vo_frame.free(&frame->vo_frame);
  return 1;
}

static int rte_on_unneeded(dxr3_driver_t *drv)
{
  rte_data_t *this = (rte_data_t *)drv->enc;

  if (this->context) {
    rte_stop(this->context);
    rte_context_destroy(this->context);
    this->context = 0;
  }
  return 1;
}

static int rte_on_close(dxr3_driver_t *drv)
{
  rte_on_unneeded(drv);
  free(drv->enc);
  drv->enc = 0;
  return 1;
}


static void mp1e_callback(rte_context *context, void *data, ssize_t size, void *user_data)
{
  dxr3_driver_t *drv = (dxr3_driver_t *)user_data;
  char tmpstr[128];
  ssize_t written;

  written = write(drv->fd_video, data, size);
  if (written < 0) {
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: video device write failed (%s)\n", strerror(errno));
    return;
  }
  if (written != size)
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: Could only write %d of %d mpeg bytes.\n", written, size);
}
#endif


#ifdef HAVE_LIBFAME
int dxr3_fame_init(dxr3_driver_t *drv)
{
  fame_data_t *this;

  this = calloc(1, sizeof(fame_data_t));
  if (!this) return 0;

  this->encoder_data.type             = ENC_FAME;
  this->encoder_data.on_update_format = fame_on_update_format;
  this->encoder_data.on_frame_copy    = NULL;
  this->encoder_data.on_display_frame = fame_on_display_frame;
  this->encoder_data.on_unneeded      = fame_on_unneeded;
  this->encoder_data.on_close         = fame_on_close;
  this->context                       = 0;

  drv->enc = &this->encoder_data;
  return 1;
}

static int fame_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  fame_data_t *this = (fame_data_t *)drv->enc;
  fame_parameters_t init_fp = FAME_PARAMETERS_INITIALIZER;
  double fps;

  av_freep(&this->buf);
  this->out[0] = this->out[1] = this->out[2] = 0;

  /* if YUY2 and dimensions changed, we need to re-allocate the
   * internal YV12 buffer */
  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    int image_size = frame->vo_frame.width * frame->oheight;

    this->out[0] = this->buf = av_mallocz(image_size * 3/2);
    this->out[1] = this->out[0] + image_size;
    this->out[2] = this->out[1] + image_size/4;

    /* fill with black (yuv 16,128,128) */
    memset(this->out[0], 16, image_size);
    memset(this->out[1], 128, image_size/4);
    memset(this->out[2], 128, image_size/4);
    lprintf("Using YUY2->YV12 conversion\n");
  }

  if (this->context) {
    lprintf("closing current encoding context.\n");
    fame_close(this->context);
    this->context = 0;
  }

  this->context = fame_open();
  if (!this->context) {
    xprintf(drv->class->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_mpeg_encoder: Couldn't start the FAME library\n"));
    return 0;
  }

  if (!this->buffer)
    this->buffer = (unsigned char *)malloc(DEFAULT_BUFFER_SIZE);
  if (!this->buffer) {
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: Couldn't allocate temp buffer for mpeg data\n");
    return 0;
  }

  this->fp = init_fp;
  this->fp.quality = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.encoding.fame_quality", 90, 10, 100,
    _("fame mpeg encoding quality"),
    _("The encoding quality of the libfame mpeg encoder library. "
      "Lower is faster but gives noticeable artifacts. Higher is better but slower."),
    10, NULL,NULL);
  /* the really interesting bit is the quantizer scale. The formula
   * below is copied from libfame's sources (could be changed in the
   * future) */
  lprintf("quality %d -> quant scale = %d\n", this->fp.quality,
    1 + (30 * (100 - this->fp.quality) + 50) / 100);
  this->fp.width   = frame->vo_frame.width;
  this->fp.height  = frame->oheight;
  this->fp.profile = "mpeg1";
  this->fp.coding  = "I";
#if LOG_ENC
  this->fp.verbose = 1;
#else
  this->fp.verbose = 0;
#endif

  /* start guessing the framerate */
  fps = 90000.0 / frame->vo_frame.duration;
  if (fps < 23.988) { /* NTSC-FILM */
    lprintf("setting mpeg output framerate to NTSC-FILM (23.976 Hz)\n");
    this->fp.frame_rate_num = 24000;
    this->fp.frame_rate_den = 1001;
  } else if (fps < 24.5) { /* FILM */
    lprintf("setting mpeg output framerate to FILM (24 Hz)\n");
    this->fp.frame_rate_num = 24;
    this->fp.frame_rate_den = 1;
  } else if (fps < 27.485) { /* PAL */
    lprintf("setting mpeg output framerate to PAL (25 Hz)\n");
    this->fp.frame_rate_num = 25;
    this->fp.frame_rate_den = 1;
  } else { /* NTSC */
    lprintf("setting mpeg output framerate to NTSC (29.97 Hz)\n");
    this->fp.frame_rate_num = 30000;
    this->fp.frame_rate_den = 1001;
  }

  fame_init (this->context, &this->fp, this->buffer, DEFAULT_BUFFER_SIZE);

  return 1;
}

static int fame_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  fame_data_t *this = (fame_data_t *)drv->enc;
  char tmpstr[128];
  ssize_t written;
  int size;

  if ((frame->vo_frame.width != this->fp.width) || (frame->oheight != this->fp.height)) {
    /* probably an old frame for a previous context. ignore it */
    frame->vo_frame.free(&frame->vo_frame);
    return 1;
  }

  fame_prepare_frame(this, drv, frame);
#ifdef HAVE_NEW_LIBFAME
  fame_start_frame(this->context, &this->yuv, NULL);
  size = fame_encode_slice(this->context);
  fame_end_frame(this->context, NULL);
#else
  size = fame_encode_frame(this->context, &this->yuv, NULL);
#endif

  frame->vo_frame.free(&frame->vo_frame);

  written = write(drv->fd_video, this->buffer, size);
  if (written < 0) {
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: video device write failed (%s)\n",
      strerror(errno));
    return 0;
  }
  if (written != size)
    xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_mpeg_encoder: Could only write %d of %d mpeg bytes.\n",
      written, size);
  return 1;
}

static int fame_on_unneeded(dxr3_driver_t *drv)
{
  fame_data_t *this = (fame_data_t *)drv->enc;

  if (this->context) {
    fame_close(this->context);
    this->context = 0;
  }
  return 1;
}

static int fame_on_close(dxr3_driver_t *drv)
{
  fame_on_unneeded(drv);
  free(drv->enc);
  drv->enc = 0;
  return 1;
}


static int fame_prepare_frame(fame_data_t *this, dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  int i, j, w2;
  uint8_t *y, *u, *v, *yuy2;

  if (frame->vo_frame.bad_frame) return 1;

  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    /* need YUY2->YV12 conversion */
    if (!(this->out[0] && this->out[1] && this->out[2]) ) {
      xprintf(drv->class->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_mpeg_encoder: Internal YV12 buffer not created.\n");
      return 0;
    }
    y = this->out[0] +  frame->vo_frame.width      *  drv->top_bar;
    u = this->out[1] + (frame->vo_frame.width / 2) * (drv->top_bar / 2);
    v = this->out[2] + (frame->vo_frame.width / 2) * (drv->top_bar / 2);
    yuy2 = frame->vo_frame.base[0];
    w2 = frame->vo_frame.width / 2;
    for (i = 0; i < frame->vo_frame.height; i += 2) {
      for (j = 0; j < w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(y++) = *(yuy2++);
        *(u++) = *(yuy2++);
        *(y++) = *(yuy2++);
        *(v++) = *(yuy2++);
      }
      /* down sampling */
      for (j = 0; j < w2; j++) {
        /* skip every second line for U and V */
        *(y++) = *(yuy2++);
        yuy2++;
        *(y++) = *(yuy2++);
        yuy2++;
      }
    }
    /* reset for encoder */
    y = this->out[0];
    u = this->out[1];
    v = this->out[2];
  }
  else { /* YV12 */
    y = frame->real_base[0];
    u = frame->real_base[1];
    v = frame->real_base[2];
  }

  this->yuv.y = y;
  this->yuv.u = u;
  this->yuv.v = v;
  return 1;
}
#endif
