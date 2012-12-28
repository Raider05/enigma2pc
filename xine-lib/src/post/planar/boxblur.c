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
 * mplayer's boxblur
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <pthread.h>

/* plugin class initialization function */
void *boxblur_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_boxblur_s post_plugin_boxblur_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct boxblur_parameters_s {

  int luma_radius;
  int luma_power;
  int chroma_radius;
  int chroma_power;

} boxblur_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( boxblur_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_radius, NULL, 0, 10, 0,
            "radius of luma blur" )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_power, NULL, 0, 10, 0,
            "power of luma blur" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_radius, NULL, -1, 10, 0,
            "radius of chroma blur (-1 = same as luma)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_power, NULL, -1, 10, 0,
            "power of chroma blur (-1 = same as luma)" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_boxblur_s {
  post_plugin_t post;

  /* private data */
  boxblur_parameters_t params;
  xine_post_in_t       params_input;

  pthread_mutex_t      lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;
  boxblur_parameters_t *param = (boxblur_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  memcpy( &this->params, param, sizeof(boxblur_parameters_t) );

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;
  boxblur_parameters_t *param = (boxblur_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(boxblur_parameters_t) );

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("Box blur does a simple blurring of the image.\n"
           "\n"
           "Parameters\n"
           "  Radius: size of the filter\n"
           "  Power: how often the filter should be applied\n"
           "\n"
           "* mplayer's boxblur (C) 2002 Michael Niedermayer\n"
         );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *boxblur_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           boxblur_dispose(post_plugin_t *this_gen);

/* frame intercept check */
static int            boxblur_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            boxblur_draw(vo_frame_t *frame, xine_stream_t *stream);


void *boxblur_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = boxblur_open_plugin;
  class->identifier      = "boxblur";
  class->description     = N_("box blur filter from mplayer");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *boxblur_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_boxblur_t *this = calloc(1, sizeof(post_plugin_boxblur_t));
  post_in_t             *input;
  xine_post_in_t        *input_api;
  post_out_t            *output;
  post_video_port_t     *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->params.luma_radius = 2;
  this->params.luma_power = 1;
  this->params.chroma_radius = -1;
  this->params.chroma_power = -1;

  pthread_mutex_init(&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->intercept_frame = boxblur_intercept_frame;
  port->new_frame->draw = boxblur_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "boxblured video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = boxblur_dispose;

  return &this->post;
}

static void boxblur_dispose(post_plugin_t *this_gen)
{
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    pthread_mutex_destroy(&this->lock);
    free(this);
  }
}


static int boxblur_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static inline void blur(uint8_t *dst, uint8_t *src, int w, int radius, int dstStep, int srcStep){
	int x;
	const int length= radius*2 + 1;
	const int inv= ((1<<16) + length/2)/length;

	int sum= 0;

	for(x=0; x<radius; x++){
		sum+= src[x*srcStep]<<1;
	}
	sum+= src[radius*srcStep];

	for(x=0; x<=radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(radius-x)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w-radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w; x++){
		sum+= src[(2*w-radius-x-1)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}
}

static inline void blur2(uint8_t *dst, uint8_t *src, int w, int radius, int power, int dstStep, int srcStep){
	uint8_t temp[2][4096];
	uint8_t *a= temp[0], *b=temp[1];

	if(radius){
		blur(a, src, w, radius, 1, srcStep);
		for(; power>2; power--){
			uint8_t *c;
			blur(b, a, w, radius, 1, 1);
			c=a; a=b; b=c;
		}
		if(power>1)
			blur(dst, a, w, radius, dstStep, 1);
		else{
			int i;
			for(i=0; i<w; i++)
				dst[i*dstStep]= a[i];
		}
	}else{
		int i;
		for(i=0; i<w; i++)
			dst[i*dstStep]= src[i*srcStep];
	}
}

static void hBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
	int y;

	if(radius==0 && dst==src) return;

	for(y=0; y<h; y++){
		blur2(dst + y*dstStride, src + y*srcStride, w, radius, power, 1, 1);
	}
}

static void vBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
	int x;

	if(radius==0 && dst==src) return;

	for(x=0; x<w; x++){
		blur2(dst + x, src + x, h, radius, power, dstStride, srcStride);
	}
}


static int boxblur_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int chroma_radius, chroma_power;
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

    chroma_radius = (this->params.chroma_radius != -1) ? this->params.chroma_radius :
                                                         this->params.luma_radius;
    chroma_power = (this->params.chroma_power != -1) ? this->params.chroma_power :
                                                       this->params.luma_power;
    cw = yv12_frame->width/2;
    ch = yv12_frame->height/2;

    hBlur(out_frame->base[0], yv12_frame->base[0], yv12_frame->width, yv12_frame->height,
          out_frame->pitches[0], yv12_frame->pitches[0], this->params.luma_radius, this->params.luma_power);
    hBlur(out_frame->base[1], yv12_frame->base[1], cw,ch,
          out_frame->pitches[1], yv12_frame->pitches[1], chroma_radius, chroma_power);
    hBlur(out_frame->base[2], yv12_frame->base[2], cw,ch,
          out_frame->pitches[2], yv12_frame->pitches[2], chroma_radius, chroma_power);

    vBlur(out_frame->base[0], out_frame->base[0], yv12_frame->width, yv12_frame->height,
          out_frame->pitches[0], out_frame->pitches[0], this->params.luma_radius, this->params.luma_power);
    vBlur(out_frame->base[1], out_frame->base[1], cw,ch,
          out_frame->pitches[1], out_frame->pitches[1], chroma_radius, chroma_power);
    vBlur(out_frame->base[2], out_frame->base[2], cw,ch,
          out_frame->pitches[2], out_frame->pitches[2], chroma_radius, chroma_power);

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
