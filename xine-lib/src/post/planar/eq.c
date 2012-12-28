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
 * mplayer's eq (soft video equalizer)
 * Copyright (C) Richard Felker
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <pthread.h>


#if defined(ARCH_X86) || defined(ARCH_X86_64)
static void process_MMX(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast)
{
	int i;
	int pel;
	int dstep = dstride-w;
	int sstep = sstride-w;
	short brvec[4];
	short contvec[4];

	contrast = ((contrast+100)*256*16)/100;
	brightness = ((brightness+100)*511)/200-128 - contrast/32;

	brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
	contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;

	while (h--) {
		asm volatile (
			"movq (%5), %%mm3 \n\t"
			"movq (%6), %%mm4 \n\t"
			"pxor %%mm0, %%mm0 \n\t"
			"movl %4, %%eax\n\t"
			ASMALIGN(4)
			"1: \n\t"
			"movq (%0), %%mm1 \n\t"
			"movq (%0), %%mm2 \n\t"
			"punpcklbw %%mm0, %%mm1 \n\t"
			"punpckhbw %%mm0, %%mm2 \n\t"
			"psllw $4, %%mm1 \n\t"
			"psllw $4, %%mm2 \n\t"
			"pmulhw %%mm4, %%mm1 \n\t"
			"pmulhw %%mm4, %%mm2 \n\t"
			"paddw %%mm3, %%mm1 \n\t"
			"paddw %%mm3, %%mm2 \n\t"
			"packuswb %%mm2, %%mm1 \n\t"
			"add $8, %0 \n\t"
			"movq %%mm1, (%1) \n\t"
			"add $8, %1 \n\t"
			"decl %%eax \n\t"
			"jnz 1b \n\t"
			: "=r" (src), "=r" (dest)
			: "0" (src), "1" (dest), "r" (w>>3), "r" (brvec), "r" (contvec)
			: "%eax"
		);

		for (i = w&7; i; i--)
		{
			pel = ((*src++* contrast)>>12) + brightness;
			if(pel&768) pel = (-pel)>>31;
			*dest++ = pel;
		}

		src += sstep;
		dest += dstep;
	}
	asm volatile ( "emms \n\t" ::: "memory" );
}
#endif

static void process_C(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast)
{
	int i;
	int pel;
	int dstep = dstride-w;
	int sstep = sstride-w;

	contrast = ((contrast+100)*256*256)/100;
	brightness = ((brightness+100)*511)/200-128 - contrast/512;

	while (h--) {
		for (i = w; i; i--)
		{
			pel = ((*src++* contrast)>>16) + brightness;
			if(pel&768) pel = (-pel)>>31;
			*dest++ = pel;
		}
		src += sstep;
		dest += dstep;
	}
}

static void (*process)(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		       int w, int h, int brightness, int contrast);


/* plugin class initialization function */
void *eq_init_plugin(xine_t *xine, void *);


typedef struct post_plugin_eq_s post_plugin_eq_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct eq_parameters_s {

  int brightness;
  int contrast;

} eq_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( eq_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, brightness, NULL, -100, 100, 0,
            "brightness" )
PARAM_ITEM( POST_PARAM_TYPE_INT, contrast, NULL, -100, 100, 0,
            "contrast" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_eq_s {
  post_plugin_t post;

  /* private data */
  eq_parameters_t    params;
  xine_post_in_t     params_input;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;
  eq_parameters_t *param = (eq_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  memcpy( &this->params, param, sizeof(eq_parameters_t) );

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;
  eq_parameters_t *param = (eq_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(eq_parameters_t) );

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("Software equalizer with interactive controls just like the hardware "
           "equalizer, for cards/drivers that do not support brightness and "
           "contrast controls in hardware.\n"
           "\n"
           "Parameters\n"
           "  brightness\n"
           "  contrast\n"
           "\n"
           "Note: It is possible to use frontend's control window to set "
           "these parameters.\n"
           "\n"
           "* mplayer's eq (C) Richard Felker\n"
           );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *eq_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           eq_dispose(post_plugin_t *this_gen);

/* replaced video_port functions */
static int            eq_get_property(xine_video_port_t *port_gen, int property);
static int            eq_set_property(xine_video_port_t *port_gen, int property, int value);

/* frame intercept check */
static int            eq_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            eq_draw(vo_frame_t *frame, xine_stream_t *stream);


void *eq_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = eq_open_plugin;
  class->identifier      = "eq";
  class->description     = N_("soft video equalizer");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *eq_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_eq_t  *this = calloc(1, sizeof(post_plugin_eq_t));
  post_in_t         *input;
  xine_post_in_t    *input_api;
  post_out_t        *output;
  post_video_port_t *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  process = process_C;
#if defined(ARCH_X86) || defined(ARCH_X86_64)
  if( xine_mm_accel() & MM_ACCEL_X86_MMX )
    process = process_MMX;
#endif

  _x_post_init(&this->post, 0, 1);

  this->params.brightness = 0;
  this->params.contrast = 0;

  pthread_mutex_init (&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->new_port.get_property = eq_get_property;
  port->new_port.set_property = eq_set_property;
  port->intercept_frame       = eq_intercept_frame;
  port->new_frame->draw       = eq_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "eqd video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = eq_dispose;

  return &this->post;
}

static void eq_dispose(post_plugin_t *this_gen)
{
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    pthread_mutex_destroy(&this->lock);
    free(this);
  }
}


static int eq_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  if( property == XINE_PARAM_VO_BRIGHTNESS )
    return 65535 * (this->params.brightness + 100) / 200;
  else if( property == XINE_PARAM_VO_CONTRAST )
    return 65535 * (this->params.contrast + 100) / 200;
  else
    return port->original_port->get_property(port->original_port, property);
}

static int eq_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  if( property == XINE_PARAM_VO_BRIGHTNESS ) {
    pthread_mutex_lock (&this->lock);
    this->params.brightness = (200 * value / 65535) - 100;
    pthread_mutex_unlock (&this->lock);
    return value;
  } else if( property == XINE_PARAM_VO_CONTRAST ) {
    pthread_mutex_lock (&this->lock);
    this->params.contrast = (200 * value / 65535) - 100;
    pthread_mutex_unlock (&this->lock);
    return value;
  } else
    return port->original_port->set_property(port->original_port, property, value);
}


static int eq_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int eq_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int skip;

  if( !frame->bad_frame &&
      ((this->params.brightness != 0) || (this->params.contrast != 0)) ) {

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

    process(out_frame->base[0], out_frame->pitches[0],
            yv12_frame->base[0], yv12_frame->pitches[0],
            frame->width, frame->height,
            this->params.brightness, this->params.contrast);
    xine_fast_memcpy(out_frame->base[1],yv12_frame->base[1],
                     yv12_frame->pitches[1] * frame->height/2);
    xine_fast_memcpy(out_frame->base[2],yv12_frame->base[2],
                     yv12_frame->pitches[2] * frame->height/2);

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
