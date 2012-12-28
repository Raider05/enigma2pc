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
 * Reference Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 * This is an example/template for the xine visualization post plugin
 * process. It simply paints the screen a solid color and rotates through
 * colors on each iteration.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>

#define FPS 20

#define FOO_WIDTH  320
#define FOO_HEIGHT 240

#define NUMSAMPLES 512

typedef struct post_plugin_fooviz_s post_plugin_fooviz_t;

typedef struct post_class_fooviz_s post_class_fooviz_t;

struct post_class_fooviz_s {
  post_class_t        post_class;

  xine_t             *xine;
};

struct post_plugin_fooviz_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  post_out_t         video_output;

  /* private metronom for syncing the video */
  metronom_t        *metronom;

  double ratio;

  int data_idx;
  short data [2][NUMSAMPLES];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */

  int channels;
  int sample_counter;
  int samples_per_frame;

  /* specific to fooviz */
  unsigned char current_yuv_byte;
};

/**************************************************************************
 * fooviz specific decode functions
 *************************************************************************/


/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

static int fooviz_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_out_t *output = (post_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)output->post;

  if (!data)
    return 0;
  /* register our stream at the new output port */
  old_port->close(old_port, XINE_ANON_STREAM);
  (new_port->open) (new_port, XINE_ANON_STREAM);
  /* reconnect ourselves */
  this->vo_port = new_port;
  return 1;
}

static int fooviz_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->ratio = (double)FOO_WIDTH/(double)FOO_HEIGHT;
  this->channels = _x_ao_mode2channels(mode);
  this->samples_per_frame = rate / FPS;
  this->data_idx = 0;
  this->sample_counter = 0;

  (this->vo_port->open) (this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, stream->metronom);

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}

static void fooviz_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;

  port->stream = NULL;

  this->vo_port->close(this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, NULL);

  port->original_port->close(port->original_port, stream );

  _x_post_dec_usage(port);
}

static void fooviz_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;
  vo_frame_t         *frame;
  int16_t *data;
  int8_t *data8;
  int samples_used = 0;
  int64_t pts = buf->vpts;
  int i, j;

  /* make a copy of buf data for private use */
  if( this->buf.mem_size < buf->mem_size ) {
    this->buf.mem = realloc(this->buf.mem, buf->mem_size);
    this->buf.mem_size = buf->mem_size;
  }
  memcpy(this->buf.mem, buf->mem,
         buf->num_frames*this->channels*((port->bits == 8)?1:2));
  this->buf.num_frames = buf->num_frames;

  /* pass data to original port */
  port->original_port->put_buffer(port->original_port, buf, stream );

  /* we must not use original data anymore, it should have already being moved
   * to the fifo of free audio buffers. just use our private copy instead.
   */
  buf = &this->buf;

  this->sample_counter += buf->num_frames;

  j = (this->channels >= 2) ? 1 : 0;

  do {

    if( port->bits == 8 ) {
      data8 = (int8_t *)buf->mem;
      data8 += samples_used * this->channels;

      /* scale 8 bit data to 16 bits and convert to signed as well */
      for( i = samples_used; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data8 += this->channels ) {
        this->data[0][this->data_idx] = ((int16_t)data8[0] << 8) - 0x8000;
        this->data[1][this->data_idx] = ((int16_t)data8[j] << 8) - 0x8000;
      }
    } else {
      data = buf->mem;
      data += samples_used * this->channels;

      for( i = samples_used; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data += this->channels ) {
        this->data[0][this->data_idx] = data[0];
        this->data[1][this->data_idx] = data[j];
      }
    }

    if( this->sample_counter >= this->samples_per_frame ) {

      samples_used += this->samples_per_frame;

      frame = this->vo_port->get_frame (this->vo_port, FOO_WIDTH, FOO_HEIGHT,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);
      frame->extra_info->invalid = 1;

      /* frame is marked as bad if we don't have enough samples for
       * updating the viz plugin (calculations may be skipped).
       * we must keep the framerate though. */
      if( this->data_idx == NUMSAMPLES ) {
        frame->bad_frame = 0;
        this->data_idx = 0;
      } else {
        frame->bad_frame = 1;
      }
      frame->duration = 90000 * this->samples_per_frame / port->rate;
      frame->pts = pts;
      this->metronom->got_video_frame(this->metronom, frame);

      this->sample_counter -= this->samples_per_frame;

      memset(frame->base[0], this->current_yuv_byte, FOO_WIDTH * FOO_HEIGHT * 2);
      this->current_yuv_byte += 3;

      frame->draw(frame, XINE_ANON_STREAM);
      frame->free(frame);
    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void fooviz_dispose(post_plugin_t *this_gen)
{
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)this_gen;

  if (_x_post_dispose(this_gen)) {

    this->metronom->exit(this->metronom);

    if(this->buf.mem)
      free(this->buf.mem);
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *fooviz_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_class_fooviz_t  *class = (post_class_fooviz_t *)class_gen;
  post_plugin_fooviz_t *this  = calloc(1, sizeof(post_plugin_fooviz_t));
  post_in_t            *input;
  post_out_t           *output;
  post_out_t           *outputv;
  post_audio_port_t    *port;

  if (!this || !video_target || !video_target[0] || !audio_target || !audio_target[0] ) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 1, 0);

  this->metronom = _x_metronom_init(1, 0, class->xine);

  this->vo_port = video_target[0];

  port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
  port->new_port.open       = fooviz_port_open;
  port->new_port.close      = fooviz_port_close;
  port->new_port.put_buffer = fooviz_port_put_buffer;

  outputv                  = &this->video_output;
  outputv->xine_out.name   = "generated video";
  outputv->xine_out.type   = XINE_POST_DATA_VIDEO;
  outputv->xine_out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->xine_out.rewire = fooviz_rewire_video;
  outputv->post            = &this->post;
  xine_list_push_back(this->post.output, outputv);

  this->post.xine_post.audio_input[0] = &port->new_port;

  this->post.dispose = fooviz_dispose;

  return &this->post;
}

/* plugin class initialization function */
static void *fooviz_init_plugin(xine_t *xine, void *data)
{
  post_class_fooviz_t *class = (post_class_fooviz_t *)xine_xmalloc(sizeof(post_class_fooviz_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = fooviz_open_plugin;
  class->post_class.identifier      = "fooviz";
  class->post_class.description     = N_("fooviz");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return class;
}

/* plugin catalog information */
static const post_info_t fooviz_special_info = { XINE_POST_TYPE_AUDIO_VISUALIZATION };

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST, 10, "fooviz", XINE_VERSION_CODE, &fooviz_special_info, &fooviz_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
