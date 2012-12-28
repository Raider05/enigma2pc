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
 */

/*
 * select audio channel plugin for VDR
 */

#define LOG_MODULE "vdr_audio"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/post.h>
#include "combined_vdr.h"



typedef struct vdr_audio_post_plugin_s
{
  post_plugin_t post_plugin;

  xine_event_queue_t *event_queue;
  xine_stream_t      *vdr_stream;

  uint8_t audio_channels;
  int num_channels;

}
vdr_audio_post_plugin_t;


static void vdr_audio_select_audio(vdr_audio_post_plugin_t *this, uint8_t channels)
{
  this->audio_channels = channels;
}


/* plugin class functions */
static post_plugin_t *vdr_audio_open_plugin(post_class_t *class_gen, int inputs,
                                            xine_audio_port_t **audio_target,
                                            xine_video_port_t **video_target);

/* plugin instance functions */
static void           vdr_audio_dispose(post_plugin_t *this_gen);

/* replaced ao_port functions */
static int            vdr_audio_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
                                          uint32_t bits, uint32_t rate, int mode);
static void           vdr_audio_port_put_buffer(xine_audio_port_t *port_gen, audio_buffer_t *buf, xine_stream_t *stream);



void *vdr_audio_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof (post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = vdr_audio_open_plugin;
  class->identifier      = "vdr_audio";
  class->description     = N_("modifies every audio frame as requested by VDR");
  class->dispose         = default_post_class_dispose;

  return class;
}

static post_plugin_t *vdr_audio_open_plugin(post_class_t *class_gen, int inputs,
				      xine_audio_port_t **audio_target,
				      xine_video_port_t **video_target)
{
  vdr_audio_post_plugin_t *this = (vdr_audio_post_plugin_t *)xine_xmalloc(sizeof (vdr_audio_post_plugin_t));
  post_in_t               *input;
  post_out_t              *output;
  post_audio_port_t       *port;
/*
fprintf(stderr, "~~~~~~~~~~ vdr open plugin\n");
*/
  if (!this || !audio_target || !audio_target[ 0 ])
  {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post_plugin, 1, 0);
  this->post_plugin.dispose = vdr_audio_dispose;

  port = _x_post_intercept_audio_port(&this->post_plugin, audio_target[ 0 ], &input, &output);
  port->new_port.open       = vdr_audio_port_open;
  port->new_port.put_buffer = vdr_audio_port_put_buffer;

  this->post_plugin.xine_post.audio_input[ 0 ] = &port->new_port;



  this->audio_channels = 0;

  return &this->post_plugin;
}

static void vdr_audio_dispose(post_plugin_t *this_gen)
{
/*
fprintf(stderr, "~~~~~~~~~~ vdr dispose\n");
*/
  if (_x_post_dispose(this_gen))
  {
    vdr_audio_post_plugin_t *this = (vdr_audio_post_plugin_t *)this_gen;

    if (this->vdr_stream)
      xine_event_dispose_queue(this->event_queue);

    free(this_gen);
  }
}

static int vdr_audio_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
                               uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t       *port = (post_audio_port_t *)port_gen;
  vdr_audio_post_plugin_t *this = (vdr_audio_post_plugin_t *)port->post;

  _x_post_rewire(&this->post_plugin);
  _x_post_inc_usage(port);
/*
fprintf(stderr, "~~~~~~~~~~ vdr port open\n");
*/
  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->num_channels = _x_ao_mode2channels(mode);

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}


static void vdr_audio_port_put_buffer(xine_audio_port_t *port_gen, audio_buffer_t *buf, xine_stream_t *stream)
{
  post_audio_port_t       *port = (post_audio_port_t *)port_gen;
  vdr_audio_post_plugin_t *this = (vdr_audio_post_plugin_t *)port->post;
  xine_event_t *event;
/*
fprintf(stderr, "~~~~~~ vdr_audio\n");
*/
  if (this->vdr_stream
      && !_x_continue_stream_processing(this->vdr_stream))
  {
    this->vdr_stream = 0;

    xine_event_dispose_queue(this->event_queue);
    this->event_queue = 0;

    this->audio_channels = 0;
  }

  if (!this->vdr_stream
      && vdr_is_vdr_stream(stream))
  {
    this->event_queue = xine_event_new_queue(stream);
    if (this->event_queue)
    {
      this->vdr_stream = stream;

      {
        xine_event_t event;

        event.type = XINE_EVENT_VDR_PLUGINSTARTED;
        event.data = 0;
        event.data_length = 1; /* vdr_audio */

        xine_event_send(this->vdr_stream, &event);
      }
    }
  }

  if (this->event_queue)
  {
    while ((event = xine_event_get(this->event_queue)))
    {
      if (event->type == XINE_EVENT_VDR_SELECTAUDIO)
      {
        vdr_select_audio_data_t *data = (vdr_select_audio_data_t *)event->data;

        vdr_audio_select_audio(this, data->channels);
      }

      xine_event_free(event);
    }
  }

  if (this->num_channels == 2
      && this->audio_channels != 0
      && this->audio_channels != 3)
  {
    audio_buffer_t *vdr_buf = port->original_port->get_buffer(port->original_port);
    vdr_buf->num_frames = buf->num_frames;
    vdr_buf->vpts = buf->vpts;
    vdr_buf->frame_header_count = buf->frame_header_count;
    vdr_buf->first_access_unit = buf->first_access_unit;
    /* FIXME: The audio buffer should contain this info.
     *        We should not have to get it from the open call.
     */
    vdr_buf->format.bits = buf->format.bits;
    vdr_buf->format.rate = buf->format.rate;
    vdr_buf->format.mode = buf->format.mode;
    _x_extra_info_merge(vdr_buf->extra_info, buf->extra_info);

    {
      int step = buf->format.bits / 8;
      uint8_t *src = (uint8_t *)buf->mem;
      uint8_t *dst = (uint8_t *)vdr_buf->mem;

      if (this->audio_channels == 2)
        src += step;
/*
      fprintf(stderr, "~~~~~~~~~~ vdr port put buffer: channels: %d, %d\n"
              , this->audio_channels
              , buf->format.bits);
*/
      int i, k;
      for (i = 0; i < buf->num_frames; i++)
      {
        for (k = 0; k < step; k++)
          *dst++ = *src++;

        src -= step;

        for (k = 0; k < step; k++)
          *dst++ = *src++;

        src += step;
      }
    }

    /* pass data to original port */
    port->original_port->put_buffer(port->original_port, vdr_buf, stream);

    /* free data from origial buffer */
    buf->num_frames = 0; /* UNDOCUMENTED, but hey, it works! Force old audio_out buffer free. */
  }

  port->original_port->put_buffer(port->original_port, buf, stream);

  return;
}
