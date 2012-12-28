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
 * mplayer's unsharp
 * Copyright (C) 2002 Rémi Guyomarch <rguyom@pobox.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <pthread.h>

/*===========================================================================*/

#define MIN_MATRIX_SIZE 3
#define MAX_MATRIX_SIZE 63

typedef struct FilterParam {
    int msizeX, msizeY;
    double amount;
    uint32_t *SC[MAX_MATRIX_SIZE-1];
} FilterParam;

struct vf_priv_s {
    FilterParam lumaParam;
    FilterParam chromaParam;
    int width, height;
};


/*===========================================================================*/

/* This code is based on :

An Efficient algorithm for Gaussian blur using finite-state machines
Frederick M. Waltz and John W. V. Miller

SPIE Conf. on Machine Vision Systems for Inspection and Metrology VII
Originally published Boston, Nov 98

*/

static void unsharp( uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int width, int height, FilterParam *fp ) {

    uint32_t **SC = fp->SC;
    uint32_t SR[MAX_MATRIX_SIZE-1], Tmp1, Tmp2;
    uint8_t* src2 = src;

    int32_t res;
    int x, y, z;
    int amount = fp->amount * 65536.0;
    int stepsX = fp->msizeX/2;
    int stepsY = fp->msizeY/2;
    int scalebits = (stepsX+stepsY)*2;
    int32_t halfscale = 1 << ((stepsX+stepsY)*2-1);

    if( !fp->amount ) {
	if( src == dst )
	    return;
	if( dstStride == srcStride )
	    xine_fast_memcpy( dst, src, srcStride*height );
	else
	    for( y=0; y<height; y++, dst+=dstStride, src+=srcStride )
		xine_fast_memcpy( dst, src, width );
	return;
    }

    for( y=0; y<2*stepsY; y++ )
	memset( SC[y], 0, sizeof(SC[y][0]) * (width+2*stepsX) );

    for( y=-stepsY; y<height+stepsY; y++ ) {
	if( y < height ) src2 = src;
	memset( SR, 0, sizeof(SR[0]) * (2*stepsX-1) );
	for( x=-stepsX; x<width+stepsX; x++ ) {
	    Tmp1 = x<=0 ? src2[0] : x>=width ? src2[width-1] : src2[x];
	    for( z=0; z<stepsX*2; z+=2 ) {
		Tmp2 = SR[z+0] + Tmp1; SR[z+0] = Tmp1;
		Tmp1 = SR[z+1] + Tmp2; SR[z+1] = Tmp2;
	    }
	    for( z=0; z<stepsY*2; z+=2 ) {
		Tmp2 = SC[z+0][x+stepsX] + Tmp1; SC[z+0][x+stepsX] = Tmp1;
		Tmp1 = SC[z+1][x+stepsX] + Tmp2; SC[z+1][x+stepsX] = Tmp2;
	    }
	    if( x>=stepsX && y>=stepsY ) {
		uint8_t* srx = src - stepsY*srcStride + x - stepsX;
		uint8_t* dsx = dst - stepsY*dstStride + x - stepsX;

		res = (int32_t)*srx + ( ( ( (int32_t)*srx - (int32_t)((Tmp1+halfscale) >> scalebits) ) * amount ) >> 16 );
		*dsx = res>255 ? 255 : res<0 ? 0 : (uint8_t)res;
	    }
	}
	if( y >= 0 ) {
	    dst += dstStride;
	    src += srcStride;
	}
    }
}


/* plugin class initialization function */
void *unsharp_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_unsharp_s post_plugin_unsharp_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct unsharp_parameters_s {

  int luma_matrix_width;
  int luma_matrix_height;
  double luma_amount;

  int chroma_matrix_width;
  int chroma_matrix_height;
  double chroma_amount;

} unsharp_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( unsharp_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_matrix_width, NULL, 3, 11, 0,
            "width of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_matrix_height, NULL, 3, 11, 0,
            "height of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, luma_amount, NULL, -2, 2, 0,
            "relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_matrix_width, NULL, 3, 11, 0,
            "width of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_matrix_height, NULL, 3, 11, 0,
            "height of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, chroma_amount, NULL, -2, 2, 0,
            "relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_unsharp_s {
  post_plugin_t post;

  /* private data */
  unsharp_parameters_t params;
  xine_post_in_t       params_input;
  struct vf_priv_s     priv;

  pthread_mutex_t      lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;
  unsharp_parameters_t *param = (unsharp_parameters_t *)param_gen;
  FilterParam *fp;

  pthread_mutex_lock (&this->lock);

  if( &this->params != param )
    memcpy( &this->params, param, sizeof(unsharp_parameters_t) );

  fp = &this->priv.lumaParam;
  fp->msizeX = 1 | MIN( MAX( param->luma_matrix_width, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->msizeY = 1 | MIN( MAX( param->luma_matrix_height, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->amount = param->luma_amount;

  fp = &this->priv.chromaParam;
  fp->msizeX = 1 | MIN( MAX( param->chroma_matrix_width, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->msizeY = 1 | MIN( MAX( param->chroma_matrix_height, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->amount = param->chroma_amount;

  this->priv.width = this->priv.height = 0;

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;
  unsharp_parameters_t *param = (unsharp_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(unsharp_parameters_t) );

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  /* use real name "Rémi Guyomarch" in translations */
  return _("Unsharp mask / gaussian blur\n"
           "It is possible to set the width and height of the matrix, "
           "odd sized in both directions (min = 3x3, max = 13x11 or 11x13, "
           "usually something between 3x3 and 7x7) and the relative amount "
           "of sharpness/blur to add to the image (a sane range should be "
           "-1.5 - 1.5).\n"
           "\n"
           "Parameters\n"
           "\n"
           "  Luma_matrix_width: Width of the matrix (must be odd)\n"
           "\n"
           "  Luma_matrix_height: Height of the matrix (must be odd)\n"
           "\n"
           "  Luma_amount: Relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)\n"
           "\n"
           "  Chroma_matrix_width: Width of the matrix (must be odd)\n"
           "\n"
           "  Chroma_matrix_height: Height of the matrix (must be odd)\n"
           "\n"
           "  Chroma_amount: Relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)\n"
           "\n"
           "\n"
           "* mplayer's unsharp (C) 2002 Remi Guyomarch\n"
         );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *unsharp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           unsharp_dispose(post_plugin_t *this_gen);

/* frame intercept check */
static int            unsharp_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            unsharp_draw(vo_frame_t *frame, xine_stream_t *stream);


void *unsharp_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = unsharp_open_plugin;
  class->identifier      = "unsharp";
  class->description     = N_("unsharp mask & gaussian blur");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *unsharp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_unsharp_t *this = calloc(1, sizeof(post_plugin_unsharp_t));
  post_in_t             *input;
  xine_post_in_t        *input_api;
  post_out_t            *output;
  post_video_port_t     *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->params.luma_matrix_width = 5;
  this->params.luma_matrix_height = 5;
  this->params.luma_amount = 0.0;

  this->params.chroma_matrix_width = 3;
  this->params.chroma_matrix_height = 3;
  this->params.chroma_amount = 0.0;

  pthread_mutex_init (&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->intercept_frame = unsharp_intercept_frame;
  port->new_frame->draw = unsharp_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "unsharped video";

  this->post.xine_post.video_input[0] = &port->new_port;

  set_parameters ((xine_post_t *)this, &this->params);

  this->post.dispose = unsharp_dispose;

  return &this->post;
}

static void unsharp_free_SC(post_plugin_unsharp_t *this)
{
  int i;

  for( i = 0; i < MAX_MATRIX_SIZE-1; i++ ) {
    if( this->priv.lumaParam.SC[i] ) {
      free( this->priv.lumaParam.SC[i] );
      this->priv.lumaParam.SC[i] = NULL;
    }
  }

  for( i = 0; i < MAX_MATRIX_SIZE-1; i++ ) {
    if( this->priv.chromaParam.SC[i] ) {
      free( this->priv.chromaParam.SC[i] );
      this->priv.chromaParam.SC[i] = NULL;
    }
  }
}


static void unsharp_dispose(post_plugin_t *this_gen)
{
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    unsharp_free_SC(this);
    pthread_mutex_destroy(&this->lock);
    free(this);
  }
}


static int unsharp_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int unsharp_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int skip;

  if( !frame->bad_frame &&
      (this->priv.lumaParam.amount || this->priv.chromaParam.amount) ) {


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

    if( frame->width != this->priv.width || frame->height != this->priv.height ) {
       int z, stepsX, stepsY;
       FilterParam *fp;

       this->priv.width = frame->width;
       this->priv.height = frame->height;

       unsharp_free_SC(this);

       fp = &this->priv.lumaParam;
       stepsX = fp->msizeX/2;
       stepsY = fp->msizeY/2;
       for( z=0; z<2*stepsY; z++ )
         fp->SC[z] = malloc( sizeof(*(fp->SC[z])) * (frame->width+2*stepsX) );

       fp = &this->priv.chromaParam;
       stepsX = fp->msizeX/2;
       stepsY = fp->msizeY/2;
       for( z=0; z<2*stepsY; z++ )
         fp->SC[z] = malloc( sizeof(*(fp->SC[z])) * (frame->width+2*stepsX) );
    }

    unsharp( out_frame->base[0], yv12_frame->base[0], out_frame->pitches[0], yv12_frame->pitches[0], yv12_frame->width,   yv12_frame->height,   &this->priv.lumaParam );
    unsharp( out_frame->base[1], yv12_frame->base[1], out_frame->pitches[1], yv12_frame->pitches[1], yv12_frame->width/2, yv12_frame->height/2, &this->priv.chromaParam );
    unsharp( out_frame->base[2], yv12_frame->base[2], out_frame->pitches[2], yv12_frame->pitches[2], yv12_frame->width/2, yv12_frame->height/2, &this->priv.chromaParam );

    pthread_mutex_unlock (&this->lock);

    skip = out_frame->draw(out_frame, stream);

    _x_post_frame_copy_up(frame, out_frame);

    out_frame->free(out_frame);
    yv12_frame->free(yv12_frame);

  } else {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
  }

  return skip;
}
