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
 * Upmix audio filter for xine.
 *   (c) 2004 James Courtier-Dutton (James@superbug.demon.co.uk)
 * This is an up-mix audio filter post plugin.
 * It simply converts Mono into Stereo.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#define LOG_MODULE "upmix_mono"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xineutils.h>
#include <xine/post.h>

#include "audio_filters.h"

typedef struct upmix_mono_parameters_s {
  int channel;
} upmix_mono_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( upmix_mono_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, channel, NULL, -1, 5, 0,
            "Select channel to upmix (duplicate) to stereo" )
END_PARAM_DESCR( param_descr )


typedef struct post_plugin_upmix_mono_s post_plugin_upmix_mono_t;

typedef struct post_class_upmix_mono_s post_class_upmix_mono_t;

struct post_class_upmix_mono_s {
  post_class_t        post_class;

  xine_t             *xine;
};

struct post_plugin_upmix_mono_s {
  post_plugin_t  post;

  /* private data */
  int            channels;

  upmix_mono_parameters_t params;
  xine_post_in_t       params_input;
  int                  params_changed;

  pthread_mutex_t      lock;

};

/**************************************************************************
 * upmix_mono parameters functions
 *************************************************************************/
static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_upmix_mono_t *this = (post_plugin_upmix_mono_t *)this_gen;
  upmix_mono_parameters_t *param = (upmix_mono_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);
  memcpy( &this->params, param, sizeof(upmix_mono_parameters_t) );
  this->params_changed = 1;
  pthread_mutex_unlock (&this->lock);

  return 1;
}
static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_upmix_mono_t *this = (post_plugin_upmix_mono_t *)this_gen;
  upmix_mono_parameters_t *param = (upmix_mono_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);
  memcpy( param, &this->params, sizeof(upmix_mono_parameters_t) );
  pthread_mutex_unlock (&this->lock);

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("This filter will upmix a mono stream to stereo, by "
           "duplicating channels. Alternatively, one may use this "
           "plugin to listen just one channel of a given stream.\n"
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

static int upmix_mono_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t        *port = (post_audio_port_t *)port_gen;
  post_plugin_upmix_mono_t *this = (post_plugin_upmix_mono_t *)port->post;
  uint32_t capabilities;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->channels = _x_ao_mode2channels(mode);
  capabilities = port->original_port->get_capabilities(port->original_port);

  if (this->channels == 1 && (capabilities & AO_CAP_MODE_STEREO)) {
    xprintf(stream->xine, XINE_VERBOSITY_LOG,
            _(LOG_MODULE ": upmixing Mono to Stereo.\n"));
    mode = AO_CAP_MODE_STEREO;
  } else {
    if ( this->channels != 1)
      xprintf(stream->xine, XINE_VERBOSITY_LOG,
              ngettext(LOG_MODULE ": upmixing a single channel from original %d channel stream.\n",
                       LOG_MODULE ": upmixing a single channel from original %d channels stream.\n",
                       this->channels), this->channels);
    else {
      xprintf(stream->xine, XINE_VERBOSITY_LOG,
              _(LOG_MODULE ": audio device not capable of AO_CAP_MODE_STEREO.\n"));
      this->channels = 0;
    }
  }

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode);
}

static void upmix_mono_port_put_buffer(xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t        *port = (post_audio_port_t *)port_gen;
  post_plugin_upmix_mono_t *this = (post_plugin_upmix_mono_t *)port->post;

  pthread_mutex_lock (&this->lock);

  if (this->channels == 1)
  {
    audio_buffer_t *buf0 = port->original_port->get_buffer(port->original_port);
    audio_buffer_t *buf1 = port->original_port->get_buffer(port->original_port);
    buf0->num_frames = buf->num_frames / 2;
    buf1->num_frames = buf->num_frames - (buf->num_frames / 2);
    buf0->vpts = buf->vpts;
    buf1->vpts = 0;
    buf0->frame_header_count = buf->frame_header_count;
    buf1->frame_header_count = buf->frame_header_count;
    buf0->first_access_unit = buf->first_access_unit;
    buf1->first_access_unit = buf->first_access_unit;
    /* FIXME: The audio buffer should contain this info.
     *        We should not have to get it from the open call.
     */
    buf0->format.bits = buf->format.bits;
    buf1->format.bits = buf->format.bits;
    buf0->format.rate = buf->format.rate;
    buf1->format.rate = buf->format.rate;
    buf0->format.mode = AO_CAP_MODE_STEREO;
    buf1->format.mode = AO_CAP_MODE_STEREO;
    _x_extra_info_merge(buf0->extra_info, buf->extra_info);
    _x_extra_info_merge(buf1->extra_info, buf->extra_info);

    {
      const size_t step = buf->format.bits / 8;
      uint8_t *src  = (uint8_t *)buf->mem;
      uint8_t *dst0 = (uint8_t *)buf0->mem;
      uint8_t *dst1 = (uint8_t *)buf1->mem;

      int i;
      for (i = 0; i < buf->num_frames / 2; i++)
      {
	memcpy(dst0, src, step);
	dst0 += step;

	memcpy(dst0, src, step);
	dst0 += step;

	src += step;
      }

      for (i = buf->num_frames / 2; i < buf->num_frames; i++)
      {
	memcpy(dst1, src, step);
	dst1 += step;

	memcpy(dst1, src, step);
	dst1 += step;

	src += step;
      }
    }

    /* pass data to original port */
    port->original_port->put_buffer(port->original_port, buf0, stream);
    port->original_port->put_buffer(port->original_port, buf1, stream);

    /* free data from origial buffer */
    buf->num_frames = 0; /* UNDOCUMENTED, but hey, it works! Force old audio_out buffer free. */
  }
  else if (this->channels && this->params.channel >= 0)
  {
    audio_buffer_t *buf0 = port->original_port->get_buffer(port->original_port);
    buf0->num_frames = buf->num_frames;
    buf0->vpts = buf->vpts;
    buf0->frame_header_count = buf->frame_header_count;
    buf0->first_access_unit = buf->first_access_unit;
    /* FIXME: The audio buffer should contain this info.
     *        We should not have to get it from the open call.
     */
    buf0->format.bits = buf->format.bits;
    buf0->format.rate = buf->format.rate;
    buf0->format.mode = AO_CAP_MODE_STEREO;
    _x_extra_info_merge(buf0->extra_info, buf->extra_info);

    {
      const size_t step = buf->format.bits / 8;
      uint8_t *src  = (uint8_t *)buf->mem;
      uint8_t *dst0 = (uint8_t *)buf0->mem;
      int cur_channel = this->params.channel;
      int i, j;

      if( cur_channel >= this->channels )
        cur_channel = this->channels-1;

      src += cur_channel * step;

      for (i = 0; i < buf->num_frames; i++)
      {
        for (j = 0; j < this->channels; j++ )
        {
	  memcpy(dst0, src, step);
	  dst0 += step;
        }
        src += this->channels * step;
      }
    }

    /* pass data to original port */
    port->original_port->put_buffer(port->original_port, buf0, stream);

    /* free data from origial buffer */
    buf->num_frames = 0; /* UNDOCUMENTED, but hey, it works! Force old audio_out buffer free. */
  }

  pthread_mutex_unlock (&this->lock);

  port->original_port->put_buffer(port->original_port, buf, stream);

  return;
}

static void upmix_mono_dispose(post_plugin_t *this_gen)
{
  post_plugin_upmix_mono_t *this = (post_plugin_upmix_mono_t *)this_gen;

  if (_x_post_dispose(this_gen))
    free(this);
}

/* plugin class functions */
static post_plugin_t *upmix_mono_open_plugin(post_class_t *class_gen, int inputs,
                                             xine_audio_port_t **audio_target,
                                             xine_video_port_t **video_target)
{
  post_plugin_upmix_mono_t *this = calloc(1, sizeof(post_plugin_upmix_mono_t));
  post_in_t                *input;
  post_out_t               *output;
  xine_post_in_t       *input_api;
  post_audio_port_t        *port;
  upmix_mono_parameters_t  init_params;

  if (!this || !audio_target || !audio_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 1, 0);

  init_params.channel = -1;

  pthread_mutex_init (&this->lock, NULL);

  set_parameters (&this->post.xine_post, &init_params);

  port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
  port->new_port.open       = upmix_mono_port_open;
  port->new_port.put_buffer = upmix_mono_port_put_buffer;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  this->post.xine_post.audio_input[0] = &port->new_port;
  this->post.dispose = upmix_mono_dispose;

  return &this->post;
}

/* plugin class initialization function */
void *upmix_mono_init_plugin(xine_t *xine, void *data)
{
  post_class_upmix_mono_t *class = (post_class_upmix_mono_t *)xine_xmalloc(sizeof(post_class_upmix_mono_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = upmix_mono_open_plugin;
  class->post_class.identifier      = "upmix_mono";
  class->post_class.description     = N_("converts Mono into Stereo");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return class;
}

