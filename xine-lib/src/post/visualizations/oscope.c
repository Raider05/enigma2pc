/*
 * Copyright (C) 2000-2003 the xine project
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
 * Basic Oscilloscope Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>
#include "visualizations.h"

#define FPS 20

#define NUMSAMPLES 512
#define MAXCHANNELS  6

#define OSCOPE_WIDTH  NUMSAMPLES
#define OSCOPE_HEIGHT 256

typedef struct post_plugin_oscope_s post_plugin_oscope_t;

typedef struct post_class_oscope_s post_class_oscope_t;

struct post_class_oscope_s {
  post_class_t        post_class;

  xine_t             *xine;
};

struct post_plugin_oscope_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  post_out_t         video_output;

  /* private metronom for syncing the video */
  metronom_t        *metronom;

  double ratio;

  int data_idx;
  short data [MAXCHANNELS][NUMSAMPLES];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */

  int channels;
  int sample_counter;
  int samples_per_frame;

  unsigned char u_current;
  unsigned char v_current;
  int u_direction;
  int v_direction;

  yuv_planes_t yuv;
};

/**************************************************************************
 * oscope specific decode functions
 *************************************************************************/

static void draw_oscope_dots(post_plugin_oscope_t *this) {

  int i, c;
  int pixel_ptr;
  int c_delta;

  memset(this->yuv.y, 0x00, OSCOPE_WIDTH * OSCOPE_HEIGHT);
  memset(this->yuv.u, 0x90, OSCOPE_WIDTH * OSCOPE_HEIGHT);
  memset(this->yuv.v, 0x80, OSCOPE_WIDTH * OSCOPE_HEIGHT);

  /* get a random delta between 1..6 */
  c_delta = (rand() % 6) + 1;
  /* apply it to the current U value */
  if (this->u_direction) {
    if (this->u_current + c_delta > 255) {
      this->u_current = 255;
      this->u_direction = 0;
    } else
      this->u_current += c_delta;
  } else {
    if (this->u_current - c_delta < 0) {
      this->u_current = 0;
      this->u_direction = 1;
    } else
      this->u_current -= c_delta;
  }

  /* get a random delta between 1..3 */
  c_delta = (rand() % 3) + 1;
  /* apply it to the current V value */
  if (this->v_direction) {
    if (this->v_current + c_delta > 255) {
      this->v_current = 255;
      this->v_direction = 0;
    } else
      this->v_current += c_delta;
  } else {
    if (this->v_current - c_delta < 0) {
      this->v_current = 0;
      this->v_direction = 1;
    } else
      this->v_current -= c_delta;
  }

  for( c = 0; c < this->channels; c++){

    /* draw channel scope */
    for (i = 0; i < NUMSAMPLES; i++) {
      pixel_ptr =
        ((OSCOPE_HEIGHT * (c * 2 + 1) / (2*this->channels) ) + (this->data[c][i] >> 9)) * OSCOPE_WIDTH + i;
      this->yuv.y[pixel_ptr] = 0xFF;
      this->yuv.u[pixel_ptr] = this->u_current;
      this->yuv.v[pixel_ptr] = this->v_current;
    }

  }

  /* top line */
  for (i = 0, pixel_ptr = 0; i < OSCOPE_WIDTH; i++, pixel_ptr++)
      this->yuv.y[pixel_ptr] = 0xFF;

  /* lines under each channel */
  for ( c = 0; c < this->channels; c++)
    for (i = 0, pixel_ptr = (OSCOPE_HEIGHT * (c+1) / this->channels - 1) * OSCOPE_WIDTH;
       i < OSCOPE_WIDTH; i++, pixel_ptr++)
      this->yuv.y[pixel_ptr] = 0xFF;

}

/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

static int oscope_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_out_t *output = (post_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)output->post;

  if (!data)
    return 0;
  old_port->close(old_port, XINE_ANON_STREAM);
  (new_port->open) (new_port, XINE_ANON_STREAM);
  /* reconnect ourselves */
  this->vo_port = new_port;
  return 1;
}

static int oscope_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->ratio = (double)OSCOPE_WIDTH/(double)OSCOPE_HEIGHT;

  this->channels = _x_ao_mode2channels(mode);
  if( this->channels > MAXCHANNELS )
    this->channels = MAXCHANNELS;
  this->samples_per_frame = rate / FPS;
  this->data_idx = 0;
  this->sample_counter = 0;
  init_yuv_planes(&this->yuv, OSCOPE_WIDTH, OSCOPE_HEIGHT);

  (this->vo_port->open) (this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, stream->metronom);

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}

static void oscope_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;

  port->stream = NULL;

  this->vo_port->close(this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, NULL);

  port->original_port->close(port->original_port, stream );

  _x_post_dec_usage(port);
}

static void oscope_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t    *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;
  vo_frame_t           *frame;
  int16_t *data;
  int8_t *data8;
  int samples_used = 0;
  int64_t pts = buf->vpts;
  int i, c;

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

  do {

    if( port->bits == 8 ) {
      data8 = (int8_t *)buf->mem;
      data8 += samples_used * this->channels;

      /* scale 8 bit data to 16 bits and convert to signed as well */
      for( i = samples_used; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data8 += this->channels )
        for( c = 0; c < this->channels; c++)
          this->data[c][this->data_idx] = ((int16_t)data8[c] << 8) - 0x8000;
    } else {
      data = buf->mem;
      data += samples_used * this->channels;

      for( i = samples_used; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data += this->channels )
        for( c = 0; c < this->channels; c++)
          this->data[c][this->data_idx] = data[c];
    }

    if( this->sample_counter >= this->samples_per_frame ) {

      samples_used += this->samples_per_frame;

      frame = this->vo_port->get_frame (this->vo_port, OSCOPE_WIDTH, OSCOPE_HEIGHT,
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

      draw_oscope_dots(this);
      yuv444_to_yuy2(&this->yuv, frame->base[0], frame->pitches[0]);

      frame->draw(frame, XINE_ANON_STREAM);
      frame->free(frame);

    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void oscope_dispose(post_plugin_t *this_gen)
{
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)this_gen;

  if (_x_post_dispose(this_gen)) {

    this->metronom->exit(this->metronom);

    if(this->buf.mem)
      free(this->buf.mem);
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *oscope_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_class_oscope_t  *class = (post_class_oscope_t *)class_gen;
  post_plugin_oscope_t *this  = calloc(1, sizeof(post_plugin_oscope_t));
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
  port->new_port.open       = oscope_port_open;
  port->new_port.close      = oscope_port_close;
  port->new_port.put_buffer = oscope_port_put_buffer;

  outputv                  = &this->video_output;
  outputv->xine_out.name   = "generated video";
  outputv->xine_out.type   = XINE_POST_DATA_VIDEO;
  outputv->xine_out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->xine_out.rewire = oscope_rewire_video;
  outputv->post            = &this->post;
  xine_list_push_back(this->post.output, outputv);

  this->post.xine_post.audio_input[0] = &port->new_port;

  this->post.dispose = oscope_dispose;

  return &this->post;
}

/* plugin class initialization function */
void *oscope_init_plugin(xine_t *xine, void *data)
{
  post_class_oscope_t *class = (post_class_oscope_t *)xine_xmalloc(sizeof(post_class_oscope_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = oscope_open_plugin;
  class->post_class.identifier      = "Oscilloscope";
  class->post_class.description     = N_("Oscilloscope");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return &class->post_class;
}
