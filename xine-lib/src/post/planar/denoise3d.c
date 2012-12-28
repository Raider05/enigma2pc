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
 * mplayer's denoise3d
 * Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <math.h>
#include <pthread.h>

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0
#define MAX_LINE_WIDTH 2048


/* plugin class initialization function */
void *denoise3d_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_denoise3d_s post_plugin_denoise3d_t;


/*
 * this is the struct used by "parameters api"
 */
typedef struct denoise3d_parameters_s {

  double luma;
  double chroma;
  double time;

} denoise3d_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( denoise3d_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, luma, NULL, 0, 10, 0,
            "spatial luma strength" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, chroma, NULL, 0, 10, 0,
            "spatial chroma strength" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, time, NULL, 0, 10, 0,
            "temporal strength" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_denoise3d_s {
  post_plugin_t post;

  /* private data */
  denoise3d_parameters_t params;
  xine_post_in_t         params_input;

  int                    Coefs[4][512];
  unsigned char          Line[MAX_LINE_WIDTH];
  vo_frame_t            *prev_frame;

  pthread_mutex_t        lock;
};

#define ABS(A) ( (A) > 0 ? (A) : -(A) )

static void PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0);

    for (i = -256; i <= 255; i++)
    {
        Simil = 1.0 - ABS(i) / 255.0;
        Ct[256+i] = pow(Simil, Gamma) * 65536;
    }
}

static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;
  denoise3d_parameters_t *param = (denoise3d_parameters_t *)param_gen;
  double ChromTmp;

  pthread_mutex_lock (&this->lock);

  if( &this->params != param )
    memcpy( &this->params, param, sizeof(denoise3d_parameters_t) );

  ChromTmp = this->params.time * this->params.chroma / this->params.luma;

  PrecalcCoefs(this->Coefs[0], this->params.luma);
  PrecalcCoefs(this->Coefs[1], this->params.time);
  PrecalcCoefs(this->Coefs[2], this->params.chroma);
  PrecalcCoefs(this->Coefs[3], ChromTmp);

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;
  denoise3d_parameters_t *param = (denoise3d_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(denoise3d_parameters_t) );

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("This filter aims to reduce image noise producing smooth images and "
           "making still images really still (This should enhance compressibility.). "
           "It can be given from 0 to 3 parameters.  If you omit a parameter, "
           "a reasonable value will be inferred.\n"
           "\n"
           "Parameters\n"
           "  Luma: Spatial luma strength (default = 4)\n"
           "  Chroma: Spatial chroma strength (default = 3)\n"
           "  Time: Temporal strength (default = 6)\n"
           "\n"
           "* mplayer's denoise3d (C) 2003 Daniel Moreno\n"
           );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *denoise3d_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           denoise3d_dispose(post_plugin_t *this_gen);

/* replaced video_port functios */
static void           denoise3d_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* frame intercept check */
static int            denoise3d_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            denoise3d_draw(vo_frame_t *frame, xine_stream_t *stream);


void *denoise3d_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = denoise3d_open_plugin;
  class->identifier      = "denoise3d";
  class->description     = N_("3D Denoiser (variable lowpass filter)");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *denoise3d_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_denoise3d_t *this = calloc(1, sizeof(post_plugin_denoise3d_t));
  post_in_t               *input;
  xine_post_in_t          *input_api;
  post_out_t              *output;
  post_video_port_t       *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->params.luma = PARAM1_DEFAULT;
  this->params.chroma = PARAM2_DEFAULT;
  this->params.time = PARAM3_DEFAULT;
  this->prev_frame = NULL;

  pthread_mutex_init(&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->new_port.close  = denoise3d_close;
  port->intercept_frame = denoise3d_intercept_frame;
  port->new_frame->draw = denoise3d_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "denoise3d video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = denoise3d_dispose;

  set_parameters ((xine_post_t *)this, &this->params);

  return &this->post;
}

static void denoise3d_dispose(post_plugin_t *this_gen)
{
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    pthread_mutex_destroy(&this->lock);
    free(this);
  }
}


static void denoise3d_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)port->post;

  if(this->prev_frame) {
    this->prev_frame->free(this->prev_frame);
    this->prev_frame = NULL;
  }

  port->original_port->close(port->original_port, stream);
  port->stream = NULL;
  _x_post_dec_usage(port);
}


static int denoise3d_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


#define LowPass(Prev, Curr, Coef) (((Prev)*Coef[Prev - Curr] + (Curr)*(65536-(Coef[Prev - Curr]))) / 65536)

static void deNoise(unsigned char *Frame,
                    unsigned char *FramePrev,
                    unsigned char *FrameDest,
                    unsigned char *LineAnt,
                    int W, int H, int sStride, int pStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, pLineOffs = 0, dLineOffs = 0;
    unsigned char PixelAnt;

    /* First pixel has no left nor top neightbour. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0];
    FrameDest[0] = LowPass(FramePrev[0], LineAnt[0], Temporal);

    /* Fist line has no top neightbour. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++)
    {
        PixelAnt = LowPass(PixelAnt, Frame[X], Horizontal);
        LineAnt[X] = PixelAnt;
        FrameDest[X] = LowPass(FramePrev[X], LineAnt[X], Temporal);
    }

    for (Y = 1; Y < H; Y++)
    {
	sLineOffs += sStride, pLineOffs += pStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs];
        LineAnt[0] = LowPass(LineAnt[0], PixelAnt, Vertical);
        FrameDest[dLineOffs] = LowPass(FramePrev[pLineOffs], LineAnt[0], Temporal);

        for (X = 1; X < W; X++)
        {
            /* The rest are normal */
            PixelAnt = LowPass(PixelAnt, Frame[sLineOffs+X], Horizontal);
            LineAnt[X] = LowPass(LineAnt[X], PixelAnt, Vertical);
            FrameDest[dLineOffs+X] = LowPass(FramePrev[pLineOffs+X], LineAnt[X], Temporal);
        }
    }
}


static int denoise3d_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *prev_frame;
  vo_frame_t *yv12_frame;
  int cw, ch;
  int skip;

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

    cw = yv12_frame->width/2;
    ch = yv12_frame->height/2;
    prev_frame = (this->prev_frame) ? this->prev_frame : yv12_frame;

    deNoise(yv12_frame->base[0], prev_frame->base[0], out_frame->base[0],
            this->Line, yv12_frame->width, yv12_frame->height,
            yv12_frame->pitches[0], prev_frame->pitches[0], out_frame->pitches[0],
            this->Coefs[0] + 256,
            this->Coefs[0] + 256,
            this->Coefs[1] + 256);
    deNoise(yv12_frame->base[1], prev_frame->base[1], out_frame->base[1],
            this->Line, cw, ch,
            yv12_frame->pitches[1], prev_frame->pitches[1], out_frame->pitches[1],
            this->Coefs[2] + 256,
            this->Coefs[2] + 256,
            this->Coefs[3] + 256);
    deNoise(yv12_frame->base[2], prev_frame->base[2], out_frame->base[2],
            this->Line, cw, ch,
            yv12_frame->pitches[2], prev_frame->pitches[2], out_frame->pitches[2],
            this->Coefs[2] + 256,
            this->Coefs[2] + 256,
            this->Coefs[3] + 256);

    pthread_mutex_unlock (&this->lock);

    skip = out_frame->draw(out_frame, stream);

    _x_post_frame_copy_up(frame, out_frame);

    out_frame->free(out_frame);

    if(this->prev_frame)
      this->prev_frame->free(this->prev_frame);
    if(port->stream)
      this->prev_frame = yv12_frame;
    else
      /* do not keep this frame when no stream is connected to us,
       * otherwise, this frame might never get freed */
      yv12_frame->free(yv12_frame);

  } else {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
  }

  return skip;
}
