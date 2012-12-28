/*
 * Copyright (C) 2000-2008 the xine project
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
 * Volume normalization audio filter for xine.  Ported by Jason Tackaberry
 * from MPlayer's af_volnorm, which is copyright 2004 by Alex Beregszaszi
 * & Pierre Lombard.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <math.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>
#include "dsp.h"

#include "audio_filters.h"


// Methods:
// 1: uses a 1 value memory and coefficients new=a*old+b*cur (with a+b=1)
// 2: uses several samples to smooth the variations (standard weighted mean
//    on past samples)

// Size of the memory array
// FIXME: should depend on the frequency of the data (should be a few seconds)
#define NSAMPLES 128

// If summing all the mem[].len is lower than MIN_SAMPLE_SIZE bytes, then we
// choose to ignore the computed value as it's not significant enough
// FIXME: should depend on the frequency of the data (0.5s maybe)
#define MIN_SAMPLE_SIZE 32000

// mul is the value by which the samples are scaled
// and has to be in [MUL_MIN, MUL_MAX]
#define MUL_INIT 1.0
#define MUL_MIN 0.1
#define MUL_MAX 5.0
// "Ideal" level
#define MID_S16 (SHRT_MAX * 0.25)
#define MID_FLOAT (INT_MAX * 0.25)

// Silence level
// FIXME: should be relative to the level of the samples
#define SIL_S16 (SHRT_MAX * 0.01)
#define SIL_FLOAT (INT_MAX * 0.01) // FIXME

// smooth must be in ]0.0, 1.0[
#define SMOOTH_MUL 0.06
#define SMOOTH_LASTAVG 0.06

#define clamp(a,min,max) (((a)>(max))?(max):(((a)<(min))?(min):(a)))

typedef struct post_plugin_volnorm_s post_plugin_volnorm_t;

typedef struct post_class_volnorm_s post_class_volnorm_t;

struct post_class_volnorm_s {
    post_class_t        post_class;

    xine_t             *xine;
};


typedef struct volnorm_parameters_s {
    int method;
} volnorm_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( volnorm_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, method, NULL, 0, 2, 0, "Normalization method" )
END_PARAM_DESCR( param_descr )

struct post_plugin_volnorm_s {
    post_plugin_t  post;

    /* private data */
    pthread_mutex_t    lock;
    xine_post_in_t params_input;

    // From mplayer af_volnorm
    int method;
    float mul;
    // method 1
    float lastavg; // history value of the filter
    // method 2
    int idx;
    struct {
        float avg; // average level of the sample
        int len; // sample size (weight)
    } mem[NSAMPLES];

};

/**************************************************************************
 * volnorm parameters functions
 *************************************************************************/
static int set_parameters (xine_post_t *this_gen, void *param_gen)
{
    post_plugin_volnorm_t *this = (post_plugin_volnorm_t *)this_gen;
    volnorm_parameters_t *param = (volnorm_parameters_t *)param_gen;

    pthread_mutex_lock (&this->lock);
    this->method = param->method;
    pthread_mutex_unlock (&this->lock);

    return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen)
{
    post_plugin_volnorm_t *this = (post_plugin_volnorm_t *)this_gen;
    volnorm_parameters_t *param = (volnorm_parameters_t *)param_gen;

    pthread_mutex_lock (&this->lock);
    param->method = this->method;
    pthread_mutex_unlock (&this->lock);

    return 1;
}

static xine_post_api_descr_t * get_param_descr (void)
{
    return &param_descr;
}

static char * get_help (void)
{
    return _("Normalizes audio by maximizing the volume without distorting "
             "the sound.\n"
             "\n"
             "Parameters:\n"
             "  method: 1: use a single sample to smooth the variations via "
             "the standard weighted mean over past samples (default); 2: use "
             "several samples to smooth the variations via the standard "
             "weighted mean over past samples.\n"
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

static int volnorm_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
                             uint32_t bits, uint32_t rate, int mode)
{
    post_audio_port_t  *port = (post_audio_port_t *)port_gen;
    post_plugin_volnorm_t *this = (post_plugin_volnorm_t *)port->post;

    _x_post_rewire(&this->post);
    _x_post_inc_usage(port);

    port->stream = stream;
    port->bits = bits;
    port->rate = rate;
    port->mode = mode;

    return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}

static void volnorm_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

    post_audio_port_t  *port = (post_audio_port_t *)port_gen;

    port->stream = NULL;
    port->original_port->close(port->original_port, stream );
    _x_post_dec_usage(port);
}

static void method1_int16(post_plugin_volnorm_t *this, audio_buffer_t *buf)
{
  register int i = 0;
  int16_t *data = (int16_t*)buf->mem;   // Audio data
  int len = buf->mem_size / 2;       // Number of samples
  float curavg = 0.0, newavg, neededmul;
  int tmp;

  for (i = 0; i < len; i++)
  {
    tmp = data[i];
    curavg += tmp * tmp;
  }
  curavg = sqrt(curavg / (float) len);

  // Evaluate an adequate 'mul' coefficient based on previous state, current
  // samples level, etc

  if (curavg > SIL_S16)
  {
    neededmul = MID_S16 / (curavg * this->mul);
    this->mul = (1.0 - SMOOTH_MUL) * this->mul + SMOOTH_MUL * neededmul;

    // clamp the mul coefficient
    this->mul = clamp(this->mul, MUL_MIN, MUL_MAX);
  }

  // Scale & clamp the samples
  for (i = 0; i < len; i++)
  {
    tmp = this->mul * data[i];
    tmp = clamp(tmp, SHRT_MIN, SHRT_MAX);
    data[i] = tmp;
  }

  // Evaulation of newavg (not 100% accurate because of values clamping)
  newavg = this->mul * curavg;

  // Stores computed values for future smoothing
  this->lastavg = (1.0 - SMOOTH_LASTAVG) * this->lastavg + SMOOTH_LASTAVG * newavg;
}

static void method1_float(post_plugin_volnorm_t *this, audio_buffer_t *buf)
{
  register int i = 0;
  float *data = (float*)buf->mem;   // Audio data
  int len = buf->mem_size / 4;       // Number of samples
  float curavg = 0.0, newavg, neededmul, tmp;

  for (i = 0; i < len; i++)
  {
    tmp = data[i];
    curavg += tmp * tmp;
  }
  curavg = sqrt(curavg / (float) len);

  // Evaluate an adequate 'mul' coefficient based on previous state, current
  // samples level, etc

  if (curavg > SIL_FLOAT) // FIXME
  {
    neededmul = MID_FLOAT / (curavg * this->mul);
    this->mul = (1.0 - SMOOTH_MUL) * this->mul + SMOOTH_MUL * neededmul;

    // clamp the mul coefficient
    this->mul = clamp(this->mul, MUL_MIN, MUL_MAX);
  }

  // Scale & clamp the samples
  for (i = 0; i < len; i++)
    data[i] *= this->mul;

  // Evaulation of newavg (not 100% accurate because of values clamping)
  newavg = this->mul * curavg;

  // Stores computed values for future smoothing
  this->lastavg = (1.0 - SMOOTH_LASTAVG) * this->lastavg + SMOOTH_LASTAVG * newavg;
}

static void method2_int16(post_plugin_volnorm_t *this, audio_buffer_t *buf)
{
  register int i = 0;
  int16_t *data = (int16_t*)buf->mem;   // Audio data
  int len = buf->mem_size / 2;       // Number of samples
  float curavg = 0.0, newavg, avg = 0.0;
  int tmp, totallen = 0;

  for (i = 0; i < len; i++)
  {
    tmp = data[i];
    curavg += tmp * tmp;
  }
  curavg = sqrt(curavg / (float) len);

  // Evaluate an adequate 'mul' coefficient based on previous state, current
  // samples level, etc
  for (i = 0; i < NSAMPLES; i++)
  {
    avg += this->mem[i].avg * (float)this->mem[i].len;
    totallen += this->mem[i].len;
  }

  if (totallen > MIN_SAMPLE_SIZE)
  {
    avg /= (float)totallen;
    if (avg >= SIL_S16)
    {
    this->mul = MID_S16 / avg;
    this->mul = clamp(this->mul, MUL_MIN, MUL_MAX);
    }
  }

  // Scale & clamp the samples
  for (i = 0; i < len; i++)
  {
    tmp = this->mul * data[i];
    tmp = clamp(tmp, SHRT_MIN, SHRT_MAX);
    data[i] = tmp;
  }

  // Evaulation of newavg (not 100% accurate because of values clamping)
  newavg = this->mul * curavg;

  // Stores computed values for future smoothing
  this->mem[this->idx].len = len;
  this->mem[this->idx].avg = newavg;
  this->idx = (this->idx + 1) % NSAMPLES;
}

static void method2_float(post_plugin_volnorm_t *this, audio_buffer_t *buf)
{
  register int i = 0;
  float *data = (float*)buf->mem;   // Audio data
  int len = buf->mem_size / 4;       // Number of samples
  float curavg = 0.0, newavg, avg = 0.0, tmp;
  int totallen = 0;

  for (i = 0; i < len; i++)
  {
    tmp = data[i];
    curavg += tmp * tmp;
  }
  curavg = sqrt(curavg / (float) len);

  // Evaluate an adequate 'mul' coefficient based on previous state, current
  // samples level, etc
  for (i = 0; i < NSAMPLES; i++)
  {
    avg += this->mem[i].avg * (float)this->mem[i].len;
    totallen += this->mem[i].len;
  }

  if (totallen > MIN_SAMPLE_SIZE)
  {
    avg /= (float)totallen;
    if (avg >= SIL_FLOAT)
    {
    this->mul = MID_FLOAT / avg;
    this->mul = clamp(this->mul, MUL_MIN, MUL_MAX);
    }
  }

  // Scale & clamp the samples
  for (i = 0; i < len; i++)
    data[i] *= this->mul;

  // Evaulation of newavg (not 100% accurate because of values clamping)
  newavg = this->mul * curavg;

  // Stores computed values for future smoothing
  this->mem[this->idx].len = len;
  this->mem[this->idx].avg = newavg;
  this->idx = (this->idx + 1) % NSAMPLES;
}


static void volnorm_port_put_buffer (xine_audio_port_t *port_gen,
                                     audio_buffer_t *buf, xine_stream_t *stream)
{

    post_audio_port_t  *port = (post_audio_port_t *)port_gen;
    post_plugin_volnorm_t *this = (post_plugin_volnorm_t *)port->post;

    if (this->method == 1) {
        if (buf->format.bits == 16)
            method1_int16(this, buf);
        else if (buf->format.bits == 32)
            method1_float(this, buf);
    } else {
        if (buf->format.bits == 16)
            method2_int16(this, buf);
        else if (buf->format.bits == 32)
            method2_float(this, buf);
    }
    port->original_port->put_buffer(port->original_port, buf, stream );

    return;
}

static void volnorm_dispose(post_plugin_t *this_gen)
{
    post_plugin_volnorm_t *this = (post_plugin_volnorm_t *)this_gen;

    if (_x_post_dispose(this_gen)) {
        pthread_mutex_destroy(&this->lock);
        free(this);
    }
}

/* plugin class functions */
static post_plugin_t *volnorm_open_plugin(post_class_t *class_gen, int inputs,
                                          xine_audio_port_t **audio_target,
                                          xine_video_port_t **video_target)
{
    post_plugin_volnorm_t *this  = calloc(1, sizeof(post_plugin_volnorm_t));
    post_in_t             *input;
    post_out_t            *output;
    xine_post_in_t        *input_api;
    post_audio_port_t     *port;

    if (!this || !audio_target || !audio_target[0] ) {
        free(this);
        return NULL;
    }

    _x_post_init(&this->post, 1, 0);
    pthread_mutex_init (&this->lock, NULL);

    this->method = 1;
    this->mul = MUL_INIT;
    this->lastavg = MID_S16;
    this->idx = 0;
    memset(this->mem, 0, sizeof(this->mem));

    port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
    port->new_port.open       = volnorm_port_open;
    port->new_port.close      = volnorm_port_close;
    port->new_port.put_buffer = volnorm_port_put_buffer;

    input_api       = &this->params_input;
    input_api->name = "parameters";
    input_api->type = XINE_POST_DATA_PARAMETERS;
    input_api->data = &post_api;
    xine_list_push_back(this->post.input, input_api);

    this->post.xine_post.audio_input[0] = &port->new_port;

    this->post.dispose = volnorm_dispose;

    return &this->post;
}

/* plugin class initialization function */
void *volnorm_init_plugin(xine_t *xine, void *data)
{
    post_class_volnorm_t *class = (post_class_volnorm_t *)xine_xmalloc(sizeof(post_class_volnorm_t));

    if (!class)
        return NULL;

    class->post_class.open_plugin     = volnorm_open_plugin;
    class->post_class.identifier      = "volnorm";
    class->post_class.description     = N_("Normalize volume");
    class->post_class.dispose         = default_post_class_dispose;

    class->xine                       = xine;

    return class;
}
