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
 * Time stretch by a given factor, optionally preserving pitch
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>
#include "dsp.h"
#include <xine/resample.h>

#include "audio_filters.h"

#define AUDIO_FRAGMENT  120/1000  /* ms of audio */

#define CLIP_INT16(s) ((s) < INT16_MIN) ? INT16_MIN : \
                      (((s) > INT16_MAX) ? INT16_MAX : (s))

/*
 * ***************************************************
 * stretchable unix System Clock Reference
 * use stretch factor to calculate speed
 * ***************************************************
 */

struct stretchscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  int64_t          cur_pts;
  int              xine_speed;
  double           speed_factor;
  double           *stretch_factor;

  pthread_mutex_t  lock;

};

typedef struct stretchscr_s stretchscr_t;

static int stretchscr_get_priority (scr_plugin_t *scr) {
  return 10; /* high priority */
}

/* Only call this when already mutex locked */
static void stretchscr_set_pivot (stretchscr_t *this) {

  struct   timeval tv;
  int64_t  pts;
  double   pts_calc;

  xine_monotonic_clock(&tv, NULL);
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
  pts = this->cur_pts + pts_calc;

/* This next part introduces a one off inaccuracy
 * to the scr due to rounding tv to pts.
 */
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts=pts;

  return ;
}

static int stretchscr_set_speed (scr_plugin_t *scr, int speed) {
  stretchscr_t *this = (stretchscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  stretchscr_set_pivot( this );
  this->xine_speed   = speed;
  this->speed_factor = (double) speed * 90000.0 / XINE_FINE_SPEED_NORMAL /
                       (*this->stretch_factor);

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void stretchscr_adjust (scr_plugin_t *scr, int64_t vpts) {
  stretchscr_t *this = (stretchscr_t*) scr;
  struct   timeval tv;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts = vpts;

  pthread_mutex_unlock (&this->lock);
}

static void stretchscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  stretchscr_t *this = (stretchscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);

  stretchscr_set_speed (&this->scr, XINE_FINE_SPEED_NORMAL);
}

static int64_t stretchscr_get_current (scr_plugin_t *scr) {
  stretchscr_t *this = (stretchscr_t*) scr;

  struct   timeval tv;
  int64_t pts;
  double   pts_calc;
  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);

  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;

  pts = this->cur_pts + pts_calc;

  pthread_mutex_unlock (&this->lock);

  return pts;
}

static void stretchscr_exit (scr_plugin_t *scr) {
  stretchscr_t *this = (stretchscr_t*) scr;

  pthread_mutex_destroy (&this->lock);
  free(this);
}

static stretchscr_t *XINE_MALLOC stretchscr_init (double *stretch_factor) {
  stretchscr_t *this;

  this = calloc(1, sizeof(stretchscr_t));

  this->scr.interface_version = 3;
  this->scr.get_priority      = stretchscr_get_priority;
  this->scr.set_fine_speed    = stretchscr_set_speed;
  this->scr.adjust            = stretchscr_adjust;
  this->scr.start             = stretchscr_start;
  this->scr.get_current       = stretchscr_get_current;
  this->scr.exit              = stretchscr_exit;

  pthread_mutex_init (&this->lock, NULL);

  this->stretch_factor = stretch_factor;
  stretchscr_set_speed (&this->scr, XINE_SPEED_PAUSE);

  return this;
}

/*****************************************************/


typedef struct post_plugin_stretch_s post_plugin_stretch_t;

typedef struct post_class_stretch_s post_class_stretch_t;

struct post_class_stretch_s {
  post_class_t        post_class;

  xine_t             *xine;
};


typedef struct stretch_parameters_s {
  int preserve_pitch;
  double factor;
} stretch_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( stretch_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, preserve_pitch, NULL, 0, 1, 0,
            "Preserve pitch" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, factor, NULL, 0.5, 1.5, 0,
            "Time stretch factor (<1.0 shorten duration)" )
END_PARAM_DESCR( param_descr )

/* plugin structure */
struct post_plugin_stretch_s {
  post_plugin_t        post;

  stretchscr_t*        scr;

  /* private data */
  stretch_parameters_t params;
  xine_post_in_t       params_input;
  int                  params_changed;

  int                  channels;
  int                  bytes_per_frame;

  int16_t             *audiofrag;         /* audio fragment to work on */
  int16_t             *outfrag;           /* processed audio fragment  */
  _ftype_t            *w;
  int                  frames_per_frag;
  int                  frames_per_outfrag;
  int                  num_frames;        /* current # of frames on audiofrag */

  int16_t             last_sample[RESAMPLE_MAX_CHANNELS];

  int64_t              pts;               /* pts for audiofrag */

  pthread_mutex_t      lock;
};

/**************************************************************************
 * stretch parameters functions
 *************************************************************************/
static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)this_gen;
  stretch_parameters_t *param = (stretch_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);
  memcpy( &this->params, param, sizeof(stretch_parameters_t) );
  this->params_changed = 1;
  pthread_mutex_unlock (&this->lock);

  return 1;
}
static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)this_gen;
  stretch_parameters_t *param = (stretch_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);
  memcpy( param, &this->params, sizeof(stretch_parameters_t) );
  pthread_mutex_unlock (&this->lock);

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("This filter will perform a time stretch, playing the "
           "stream faster or slower by a factor. Pitch is optionally "
           "preserved, so it is possible, for example, to use it to "
           "watch a movie in less time than it was originally shot.\n"
           );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};



/**************************************************************************
 * xine audio post plugin functions
 *************************************************************************/

static int stretch_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)port->post;
  int64_t time;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  /* register our own scr provider */
  time = port->stream->xine->clock->get_current_time(port->stream->xine->clock);
  this->scr = stretchscr_init(&this->params.factor);
  this->scr->scr.start(&this->scr->scr, time);
  port->stream->xine->clock->register_scr(port->stream->xine->clock, &this->scr->scr);

  /* force updating on stretch_port_put_buffer */
  this->params_changed = 1;

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode);
}

static void stretch_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)port->post;

  if (this->scr) {
    port->stream->xine->clock->unregister_scr(port->stream->xine->clock, &this->scr->scr);
    this->scr->scr.exit(&this->scr->scr);
  }

  if(this->audiofrag) {
    free(this->audiofrag);
    this->audiofrag = NULL;
  }

  if(this->outfrag) {
    free(this->outfrag);
    this->outfrag = NULL;
  }

  if(this->w) {
    free(this->w);
    this->w = NULL;
  }

  port->stream = NULL;

  port->original_port->close(port->original_port, stream );

  _x_post_dec_usage(port);
}

static void stretch_process_fragment( post_audio_port_t *port,
  xine_stream_t *stream, extra_info_t *extra_info )
{
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)port->post;

  audio_buffer_t  *outbuf;
  int16_t         *data_out = this->outfrag;
  int num_frames_in = this->num_frames;
  int num_frames_out = this->num_frames * this->frames_per_outfrag /
                         this->frames_per_frag;

  if( !this->params.preserve_pitch ) {
     if( this->channels == 2 )
       _x_audio_out_resample_stereo(this->last_sample, this->audiofrag, num_frames_in,
                                    this->outfrag, num_frames_out);
     else if( this->channels == 1 )
        _x_audio_out_resample_mono(this->last_sample, this->audiofrag, num_frames_in,
                                   this->outfrag, num_frames_out);
  } else {
     if (this->channels == 2)
       memcpy (this->last_sample, &this->audiofrag[(num_frames_in - 1) * 2], 2 * sizeof (this->last_sample[0]));
     else if (this->channels == 1)
       memcpy (this->last_sample, &this->audiofrag[num_frames_in - 1], sizeof (this->last_sample[0]));
     if( num_frames_in > num_frames_out )
     {
       /*
        * time compressing strategy
        *
        * input chunk has two halves, A and B.
        * output chunk is composed as follow:
        * - some frames copied directly from A
        * - some frames copied from A merged with frames from B
        *   weighted by an increasing factor (0 -> 1.0)
        * - frames from A weighted by a decreasing factor (1.0 -> 0)
        *   merged with frames copied from B
        * - some frames copied directly from B
        */

       int merge_frames = num_frames_in - num_frames_out;
       int copy_frames;
       int16_t *src = this->audiofrag;
       int16_t *dst = this->outfrag;
       int i, j;

       if( merge_frames > num_frames_out )
         merge_frames = num_frames_out;
       copy_frames = num_frames_out - merge_frames;

       memcpy(dst, src, copy_frames/2 * this->bytes_per_frame);
       dst += copy_frames/2 * this->channels;
       src += copy_frames/2 * this->channels;

       for( i = 0; i < merge_frames/2; i++ )
       {
         for( j = 0; j < this->channels; j++, src++, dst++ ) {

           int32_t  s = (int32_t) ((_ftype_t) src[0] +
                                    src[merge_frames * this->channels] * this->w[i]);
           *dst = CLIP_INT16(s);
         }
       }

       for( ; i < merge_frames; i++ )
       {
         for( j = 0; j < this->channels; j++, src++, dst++ ) {

           int32_t  s = (int32_t) ((_ftype_t) src[0] * this->w[i] +
                                    src[merge_frames * this->channels]);
           *dst = CLIP_INT16(s);
         }
       }

       src += merge_frames * this->channels;

       memcpy(dst, src, (copy_frames - copy_frames/2) *
                        this->bytes_per_frame);

     } else {
       /*
        * time expansion strategy
        *
        * output chunk is composed of two versions of the
        * input chunk:
        * - first part copied directly from input, and then
        *   merged with the second (delayed) part using a
        *   decreasing factor (1.0 -> 0)
        * - the delayed version of the input is merged with
        *   an increasing factor (0 -> 1.0) and then (when
        *   factor reaches 1.0) just copied until the end.
        */

       int merge_frames = num_frames_out - num_frames_in;
       int copy_frames = num_frames_out - merge_frames;
       int16_t *src1 = this->audiofrag;
       int16_t *src2;
       int16_t *dst = this->outfrag;
       int i, j;

       memcpy(dst, src1, copy_frames/2 * this->bytes_per_frame);
       dst += copy_frames/2 * this->channels;
       src1 += copy_frames/2 * this->channels;
       src2 = src1 - merge_frames * this->channels;

       for( i = 0; i < merge_frames/2; i++ )
       {
         for( j = 0; j < this->channels; j++, src1++, src2++, dst++ ) {

           int32_t  s = (int32_t) ((_ftype_t) *src1 +
                                    *src2 * this->w[i]);
           *dst = CLIP_INT16(s);
         }
       }

       for( ; i < merge_frames; i++ )
       {
         for( j = 0; j < this->channels; j++, src1++, src2++, dst++ ) {

           int32_t  s = (int32_t) ((_ftype_t) *src1 * this->w[i] +
                                    *src2);
           *dst = CLIP_INT16(s);
         }
       }

       memcpy(dst, src2, (copy_frames - copy_frames/2) *
                         this->bytes_per_frame);

    }
  }

  /* copy processed fragment into multiple audio buffers, if needed */
  while( num_frames_out ) {
    outbuf = port->original_port->get_buffer(port->original_port);

    outbuf->num_frames = outbuf->mem_size / this->bytes_per_frame;
    if( outbuf->num_frames > num_frames_out )
      outbuf->num_frames = num_frames_out;

    memcpy( outbuf->mem, data_out,
            outbuf->num_frames * this->bytes_per_frame );
    num_frames_out -= outbuf->num_frames;
    data_out = (uint16_t *)((uint8_t *)data_out + outbuf->num_frames * this->bytes_per_frame);

    outbuf->vpts        = this->pts;
    this->pts           = 0;
    outbuf->stream      = stream;
    outbuf->format.bits = port->bits;
    outbuf->format.rate = port->rate;
    outbuf->format.mode = port->mode;

    _x_extra_info_merge( outbuf->extra_info, extra_info );

    port->original_port->put_buffer(port->original_port, outbuf, stream );
  }

  this->num_frames = 0;
}

static void stretch_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)port->post;
  int16_t               *data_in;

  pthread_mutex_lock (&this->lock);


  if( this->params_changed ) {
    int64_t audio_step;

    if( this->num_frames && this->audiofrag && this->outfrag ) {
      /* output whatever we have before changing parameters */
      stretch_process_fragment( port, stream, buf->extra_info );
    }

    this->channels = _x_ao_mode2channels(port->mode);
    this->bytes_per_frame = port->bits / 8 * this->channels;

    audio_step = ((int64_t)90000 * (int64_t)32768) / (int64_t)port->rate;
    audio_step = (int64_t) ((double)audio_step / this->params.factor);
    stream->metronom->set_audio_rate(stream->metronom, audio_step);

    stretchscr_set_speed(&this->scr->scr, this->scr->xine_speed);

    if(this->audiofrag) {
      free(this->audiofrag);
      this->audiofrag = NULL;
    }

    if(this->outfrag) {
      free(this->outfrag);
      this->outfrag = NULL;
    }

    if(this->w) {
      free(this->w);
      this->w = NULL;
    }

    this->frames_per_frag = port->rate * AUDIO_FRAGMENT;
    this->frames_per_outfrag = (int) ((double)this->params.factor * this->frames_per_frag);

    if( this->frames_per_frag != this->frames_per_outfrag ) {
      int wsize;

      this->audiofrag = malloc( this->frames_per_frag * this->bytes_per_frame );
      this->outfrag = malloc( this->frames_per_outfrag * this->bytes_per_frame );

      if( this->frames_per_frag > this->frames_per_outfrag )
        wsize = this->frames_per_frag - this->frames_per_outfrag;
      else
        wsize = this->frames_per_outfrag - this->frames_per_frag;

      this->w = (_ftype_t*) malloc( wsize * sizeof(_ftype_t) );
      triang(wsize, this->w);
    }

    this->num_frames = 0;
    this->pts = 0;

    this->params_changed = 0;
  }

  pthread_mutex_unlock (&this->lock);

  /* just pass data through if we have nothing to do */
  if( this->frames_per_frag == this->frames_per_outfrag ||
      /* FIXME: we only handle 1 or 2 channels, 16 bits for now */
      (this->channels != 1 && this->channels != 2) ||
      port->bits != 16 ) {

    port->original_port->put_buffer(port->original_port, buf, stream );

    return;
  }

  /* update pts for our current audio fragment */
  if( buf->vpts )
    this->pts = buf->vpts - (this->num_frames * 90000 / port->rate);

  data_in = buf->mem;
  while( buf->num_frames ) {
    int frames_to_copy = this->frames_per_frag - this->num_frames;

    if( frames_to_copy > buf->num_frames )
      frames_to_copy = buf->num_frames;

    /* copy up to one fragment from input buf to our buffer */
    memcpy( (uint8_t *)this->audiofrag + this->num_frames * this->bytes_per_frame,
            data_in, frames_to_copy * this->bytes_per_frame );

    data_in = (uint16_t *)((uint8_t *)data_in + frames_to_copy * this->bytes_per_frame);
    this->num_frames += frames_to_copy;
    buf->num_frames -= frames_to_copy;

    /* check if we have a complete audio fragment to process */
    if( this->num_frames == this->frames_per_frag ) {
      stretch_process_fragment( port, stream, buf->extra_info );
    }
  }

  buf->num_frames=0; /* UNDOCUMENTED, but hey, it works! Force old audio_out buffer free. */
  port->original_port->put_buffer(port->original_port, buf, stream );

  return;
}

static void stretch_dispose(post_plugin_t *this_gen)
{
  post_plugin_stretch_t *this = (post_plugin_stretch_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *stretch_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_stretch_t *this  = calloc(1, sizeof(post_plugin_stretch_t));
  post_in_t            *input;
  post_out_t           *output;
  xine_post_in_t       *input_api;
  post_audio_port_t    *port;
  stretch_parameters_t  init_params;

  if (!this || !audio_target || !audio_target[0] ) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 1, 0);

  init_params.preserve_pitch = 1;
  init_params.factor = 0.80;

  pthread_mutex_init (&this->lock, NULL);

  set_parameters (&this->post.xine_post, &init_params);

  port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
  port->new_port.open       = stretch_port_open;
  port->new_port.close      = stretch_port_close;
  port->new_port.put_buffer = stretch_port_put_buffer;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  this->post.xine_post.audio_input[0] = &port->new_port;

  this->post.dispose = stretch_dispose;

  return &this->post;
}

/* plugin class initialization function */
void *stretch_init_plugin(xine_t *xine, void *data)
{
  post_class_stretch_t *class = (post_class_stretch_t *)xine_xmalloc(sizeof(post_class_stretch_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = stretch_open_plugin;
  class->post_class.identifier      = "stretch";
  class->post_class.description     = N_("Time stretch by a given factor, optionally preserving pitch");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return class;
}
