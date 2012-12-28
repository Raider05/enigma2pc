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
 * FftGraph Visualization Post Plugin For xine
 *   by Thibaut Mattern (tmattern@noos.fr)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <math.h>

#include <assert.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>
#include "bswap.h"
#include "visualizations.h"
#include "fft.h"

#define FPS                      20

#define FFTGRAPH_WIDTH          512
#define FFTGRAPH_HEIGHT         256

#define FFT_BITS                 11
#define NUMSAMPLES  (1 << FFT_BITS)

#define MAXCHANNELS               6

typedef struct post_plugin_fftgraph_s post_plugin_fftgraph_t;

typedef struct post_class_fftgraph_s post_class_fftgraph_t;

struct post_class_fftgraph_s {
  post_class_t        post_class;

  xine_t             *xine;
};

struct post_plugin_fftgraph_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  post_out_t         video_output;

  /* private metronom for syncing the video */
  metronom_t        *metronom;

  double ratio;

  int data_idx;
  complex_t wave[MAXCHANNELS][NUMSAMPLES];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */

  int channels;
  int sample_counter;
  int samples_per_frame;

  fft_t *fft;
  uint32_t map[FFTGRAPH_HEIGHT][FFTGRAPH_WIDTH / 2];
  int cur_line;
  int lines_per_channel;

  uint32_t yuy2_colors[8192];
};

/*
 * fade function
 */
static void fade(int r1, int g1, int b1,
		 int r2, int g2, int b2,
		 uint32_t *yuy2_colors, int steps) {
  int i, r, g, b, y, u, v;

  for (i = 0; i < steps; i++) {
    r = r1 + (r2 - r1) * i / steps;
    g = g1 + (g2 - g1) * i / steps;
    b = b1 + (b2 - b1) * i / steps;

    y = COMPUTE_Y(r, g, b);
    u = COMPUTE_U(r, g, b);
    v = COMPUTE_V(r, g, b);

    *(yuy2_colors + i) = be2me_32((y << 24) |
				  (u << 16) |
				  (y << 8) |
				  v);

  }
}

static void draw_fftgraph(post_plugin_fftgraph_t *this, vo_frame_t *frame) {

  int i, c, y;
  int map_ptr;
  int amp_int;
  float amp_float;
  uint32_t yuy2_white;
  int line, line_min, line_max;

  yuy2_white = be2me_32((0xFF << 24) |
			(0x80 << 16) |
			(0xFF << 8) |
			0x80);

  for (c = 0; c < this->channels; c++){
    /* perform FFT for channel data */
    fft_window(this->fft, this->wave[c]);
    fft_scale(this->wave[c], this->fft->bits);
    fft_compute(this->fft, this->wave[c]);

    /* plot the FFT points for the channel */
    line = this->cur_line + c * this->lines_per_channel;

    for (i = 0; i < FFTGRAPH_WIDTH / 2; i++) {
      amp_float = fft_amp(i, this->wave[c], this->fft->bits);
      amp_int = (int)(amp_float);
      if (amp_int > 8191)
        amp_int = 8191;
      if (amp_int < 0)
        amp_int = 0;

      this->map[line][i] = this->yuy2_colors[amp_int];
    }
  }

  this->cur_line = (this->cur_line + 1) % this->lines_per_channel;

  /* scrolling map */
  map_ptr = 0;

  for(c = 0; c < this->channels; c++) {
    line = this->cur_line + c * this->lines_per_channel;
    line_min = c * this->lines_per_channel;
    line_max = (c + 1) * this->lines_per_channel;

    for(y = line; y < line_max; y++) {
      xine_fast_memcpy(((uint32_t *)frame->base[0]) + map_ptr,
		       this->map[y],
		       FFTGRAPH_WIDTH * 2);
      map_ptr += (FFTGRAPH_WIDTH / 2);
    }

    for(y = line_min; y < line; y++) {
      xine_fast_memcpy(((uint32_t *)frame->base[0]) + map_ptr,
		       this->map[y],
		       FFTGRAPH_WIDTH * 2);
      map_ptr += (FFTGRAPH_WIDTH / 2);
    }
  }

  /* top line */
  for (map_ptr = 0; map_ptr < FFTGRAPH_WIDTH / 2; map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;

  /* lines under each channel */
  for (c = 0; c < this->channels; c++){
    for (i = 0, map_ptr = ((FFTGRAPH_HEIGHT * (c+1) / this->channels -1 ) * FFTGRAPH_WIDTH) / 2;
       i < FFTGRAPH_WIDTH / 2; i++, map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;
  }

}

/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

static int fftgraph_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_out_t *output = (post_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_fftgraph_t *this = (post_plugin_fftgraph_t *)output->post;

  if (!data)
    return 0;
  /* register our stream at the new output port */
  old_port->close(old_port, XINE_ANON_STREAM);
  (new_port->open) (new_port, XINE_ANON_STREAM);
  /* reconnect ourselves */
  this->vo_port = new_port;
  return 1;
}

static int fftgraph_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftgraph_t *this = (post_plugin_fftgraph_t *)port->post;
  int i,j;
  uint32_t *color_ptr;
  uint32_t last_color, yuy2_black;

  /* printf("fftgraph_port_open, port_gen=%p, stream=%p, this=%p\n", port_gen, stream, this); */

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->ratio = (double)FFTGRAPH_WIDTH / (double)FFTGRAPH_HEIGHT;

  this->channels = _x_ao_mode2channels(mode);
  if( this->channels > MAXCHANNELS )
    this->channels = MAXCHANNELS;
  this->lines_per_channel = FFTGRAPH_HEIGHT / this->channels;
  this->samples_per_frame = rate / FPS;
  this->data_idx = 0;
  this->sample_counter = 0;

  (this->vo_port->open) (this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, stream->metronom);

  this->fft = fft_new(FFT_BITS);

  this->cur_line = 0;

  /* compute colors */
  color_ptr = this->yuy2_colors;
  /* black -> red */
  fade(0, 0, 0,
       128, 0, 0,
       color_ptr, 128);
  color_ptr += 128;

  /* red -> blue */
  fade(128, 0, 0,
       40, 0, 160,
       color_ptr, 256);
  color_ptr += 256;

  /* blue -> green */
  fade(40, 0, 160,
       40, 160, 70,
       color_ptr, 1024);
  color_ptr += 1024;

  /* green -> white */
  fade(40, 160, 70,
       255, 255, 255,
       color_ptr, 2048);
  color_ptr += 2048;

  last_color = *(color_ptr - 1);

  /* white */
  for (i = 0; i < 8192 - 128 - 256 - 1024 - 2048; i++) {
    *color_ptr = last_color;
    color_ptr++;
  }

  /* clear the map */
  yuy2_black = be2me_32((0x00 << 24) |
			(0x80 << 16) |
			(0x00 << 8) |
			0x80);
  for (i = 0; i < FFTGRAPH_HEIGHT; i++) {
    for (j = 0; j < (FFTGRAPH_WIDTH / 2); j++) {
      this->map[i][j] = yuy2_black;
    }
  }

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}

static void fftgraph_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftgraph_t *this = (post_plugin_fftgraph_t *)port->post;

  port->stream = NULL;

  fft_dispose(this->fft);
  this->fft = NULL;

  this->vo_port->close(this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, NULL);

  port->original_port->close(port->original_port, stream );

  _x_post_dec_usage(port);
}

static void fftgraph_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftgraph_t *this = (post_plugin_fftgraph_t *)port->post;
  vo_frame_t         *frame;
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
           i++, this->data_idx++, data8 += this->channels ) {
        for( c = 0; c < this->channels; c++){
          this->wave[c][this->data_idx].re = (double)(data8[c] << 8) - 0x8000;
          this->wave[c][this->data_idx].im = 0;
        }
      }
    } else {
      data = buf->mem;
      data += samples_used * this->channels;

      for( i = samples_used; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data += this->channels ) {
        for( c = 0; c < this->channels; c++){
          this->wave[c][this->data_idx].re = (double)data[c];
          this->wave[c][this->data_idx].im = 0;
        }
      }
    }

    if( this->sample_counter >= this->samples_per_frame ) {

      samples_used += this->samples_per_frame;

      frame = this->vo_port->get_frame (this->vo_port, FFTGRAPH_WIDTH, FFTGRAPH_HEIGHT,
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

      if( this->fft )
        draw_fftgraph(this, frame);
      else
        frame->bad_frame = 1;

      frame->draw(frame, XINE_ANON_STREAM);
      frame->free(frame);
    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void fftgraph_dispose(post_plugin_t *this_gen)
{
  post_plugin_fftgraph_t *this = (post_plugin_fftgraph_t *)this_gen;

  if (_x_post_dispose(this_gen)) {

    this->metronom->exit(this->metronom);

    if(this->buf.mem)
      free(this->buf.mem);
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *fftgraph_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_class_fftgraph_t  *class = (post_class_fftgraph_t *)class_gen;
  post_plugin_fftgraph_t *this  = calloc(1, sizeof(post_plugin_fftgraph_t));
  post_in_t              *input;
  post_out_t             *output;
  post_out_t             *outputv;
  post_audio_port_t      *port;

  if (!this || !video_target || !video_target[0] || !audio_target || !audio_target[0] ) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 1, 0);

  this->metronom = _x_metronom_init(1, 0, class->xine);

  this->vo_port = video_target[0];

  port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
  port->new_port.open       = fftgraph_port_open;
  port->new_port.close      = fftgraph_port_close;
  port->new_port.put_buffer = fftgraph_port_put_buffer;

  outputv                  = &this->video_output;
  outputv->xine_out.name   = "generated video";
  outputv->xine_out.type   = XINE_POST_DATA_VIDEO;
  outputv->xine_out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->xine_out.rewire = fftgraph_rewire_video;
  outputv->post            = &this->post;
  xine_list_push_back(this->post.output, outputv);

  this->post.xine_post.audio_input[0] = &port->new_port;

  this->post.dispose = fftgraph_dispose;

  return &this->post;
}

/* plugin class initialization function */
void *fftgraph_init_plugin(xine_t *xine, void *data)
{
  post_class_fftgraph_t *class = (post_class_fftgraph_t *)xine_xmalloc(sizeof(post_class_fftgraph_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = fftgraph_open_plugin;
  class->post_class.identifier      = "fftgraph";
  class->post_class.description     = N_("fftgraph Visualization Post Plugin");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return class;
}
