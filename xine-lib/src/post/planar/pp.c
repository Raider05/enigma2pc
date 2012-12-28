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
 *
 * plugin for ffmpeg libpostprocess
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <config.h>

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <postprocess.h>
#else
#  include <libpostproc/postprocess.h>
#endif
#include <pthread.h>

#if LIBPOSTPROC_VERSION_MAJOR < 52
#  define pp_context	pp_context_t
#  define pp_mode	pp_mode_t
#  define PP_PARAMETERS_T
#endif

#define PP_STRING_SIZE 256 /* size of pp mode string (including all options) */

/* plugin class initialization function */
void *pp_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_pp_s post_plugin_pp_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct pp_parameters_s {

  int quality;
  char mode[PP_STRING_SIZE];

} pp_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( pp_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, quality, NULL, 0, PP_QUALITY_MAX, 0,
            "postprocessing quality" )
PARAM_ITEM( POST_PARAM_TYPE_CHAR, mode, NULL, 0, 0, 0,
            "mode string (overwrites all other options except quality)" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_pp_s {
  post_plugin_t post;

  /* private data */
  int                frame_width;
  int                frame_height;

  pp_parameters_t    params;
  xine_post_in_t     params_input;

  /* libpostproc specific stuff */
  int                pp_flags;
  pp_context        *our_context;
  pp_mode           *our_mode;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
#ifdef PP_PARAMETERS_T
  post_plugin_pp_t *this = (post_plugin_pp_t *)this_gen;
  pp_parameters_t *param = (pp_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  memcpy( &this->params, param, sizeof(pp_parameters_t) );

  pthread_mutex_unlock (&this->lock);
#endif
  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
#ifdef PP_PARAMETERS_T
  post_plugin_pp_t *this = (post_plugin_pp_t *)this_gen;
  pp_parameters_t *param = (pp_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(pp_parameters_t) );
#endif
  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  static char *help = NULL;

  if( !help ) {
    help = _x_asprintf("%s%s%s",
		       _("FFmpeg libpostprocess plugin.\n"
			 "\n"
			 "Parameters\n"
			 "\n"),
		       pp_help,
		       _("\n"
			 "* libpostprocess (C) Michael Niedermayer\n")
		       );
  }

  return help;
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *pp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           pp_dispose(post_plugin_t *this_gen);

/* frame intercept check */
static int            pp_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            pp_draw(vo_frame_t *frame, xine_stream_t *stream);


void *pp_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = pp_open_plugin;
  class->identifier      = "pp";
  class->description     = N_("plugin for ffmpeg libpostprocess");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *pp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_pp_t  *this = calloc(1, sizeof(post_plugin_pp_t));
  post_in_t         *input;
  xine_post_in_t    *input_api;
  post_out_t        *output;
  post_video_port_t *port;
  uint32_t           cpu_caps;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->params.quality = 3;
  strcpy(this->params.mode, "de");

  /* Detect what cpu accel we have */
  cpu_caps = xine_mm_accel();
  this->pp_flags = PP_FORMAT_420;
  if(cpu_caps & MM_ACCEL_X86_MMX)
    this->pp_flags |= PP_CPU_CAPS_MMX;
  if(cpu_caps & MM_ACCEL_X86_MMXEXT)
    this->pp_flags |= PP_CPU_CAPS_MMX2;
  if(cpu_caps & MM_ACCEL_X86_3DNOW)
    this->pp_flags |= PP_CPU_CAPS_3DNOW;

  this->our_mode = NULL;
  this->our_context = NULL;

  pthread_mutex_init (&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->intercept_frame = pp_intercept_frame;
  port->new_frame->draw = pp_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "pped video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = pp_dispose;

  return &this->post;
}

static void pp_dispose(post_plugin_t *this_gen)
{
  post_plugin_pp_t *this = (post_plugin_pp_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    if(this->our_mode) {
      pp_free_mode(this->our_mode);
      this->our_mode = NULL;
    }
    if(this->our_context) {
      pp_free_context(this->our_context);
      this->our_context = NULL;
    }
    free(this);
  }
}


static int pp_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int pp_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_pp_t *this = (post_plugin_pp_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int skip;
  int pp_flags;

  if( !frame->bad_frame ) {

    /* convert to YV12 if needed */
    if( frame->format != XINE_IMGFMT_YV12 ) {

      yv12_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);

      _x_post_frame_copy_down(frame, yv12_frame);

      yuy2_to_yv12(frame->base[0], frame->pitches[0],
                   yv12_frame->base[0], yv12_frame->pitches[0],
                   yv12_frame->base[1], yv12_frame->pitches[1],
                   yv12_frame->base[2], yv12_frame->pitches[2],
                   frame->width, frame->height);

    } else {
      yv12_frame = frame;
      yv12_frame->lock(yv12_frame);
    }

    out_frame = port->original_port->get_frame(port->original_port,
      frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);

    _x_post_frame_copy_down(frame, out_frame);

    pthread_mutex_lock (&this->lock);

    if( !this->our_context ||
        this->frame_width != yv12_frame->width ||
        this->frame_height != yv12_frame->height ) {

      this->frame_width = yv12_frame->width;
      this->frame_height = yv12_frame->height;
      pp_flags = this->pp_flags;

      if(this->our_context)
        pp_free_context(this->our_context);

      this->our_context = pp_get_context(frame->width, frame->height, pp_flags);

      if(this->our_mode) {
        pp_free_mode(this->our_mode);
        this->our_mode = NULL;
      }
    }

    if(!this->our_mode)
      this->our_mode = pp_get_mode_by_name_and_quality(this->params.mode,
                                                      this->params.quality);

    if(this->our_mode)
      pp_postprocess(yv12_frame->base, yv12_frame->pitches,
                     out_frame->base, out_frame->pitches,
                     (frame->width+7)&(~7), frame->height,
                     NULL, 0,
                     this->our_mode, this->our_context,
                     0 /*this->av_frame->pict_type*/);

    pthread_mutex_unlock (&this->lock);

    if(this->our_mode) {
      skip = out_frame->draw(out_frame, stream);
      _x_post_frame_copy_up(frame, out_frame);
    } else {
      _x_post_frame_copy_down(frame, frame->next);
      skip = frame->next->draw(frame->next, stream);
      _x_post_frame_copy_up(frame, frame->next);
    }

    out_frame->free(out_frame);
    yv12_frame->free(yv12_frame);

  } else {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
  }

  return skip;
}
