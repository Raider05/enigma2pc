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
 * Fast Fourier Transform Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 *
 * FFT code by Steve Haehnichen, originally licensed under GPL v1
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <math.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/post.h>
#include "bswap.h"
#include "visualizations.h"
#include "fft.h"

#define FPS 20

#define FFT_WIDTH   512
#define FFT_HEIGHT  256

#define NUMSAMPLES  512
#define MAXCHANNELS   6

#define FFT_BITS      9

typedef struct post_plugin_fftscope_s post_plugin_fftscope_t;

typedef struct post_class_fftscope_s post_class_fftscope_t;

struct post_class_fftscope_s {
  post_class_t post_class;

  xine_t      *xine;
};

struct post_plugin_fftscope_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  post_out_t         video_output;

  /* private metronom for syncing the video */
  metronom_t        *metronom;

  double ratio;

  int data_idx;
  complex_t wave[MAXCHANNELS][NUMSAMPLES];
  int amp_max[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_y[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_u[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_v[MAXCHANNELS][NUMSAMPLES / 2];
  int     amp_age[MAXCHANNELS][NUMSAMPLES / 2];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */

  int channels;
  int sample_counter;
  int samples_per_frame;

  unsigned char u_current;
  unsigned char v_current;
  int u_direction;
  int v_direction;
  fft_t *fft;
};


/*
 *  Fade out a YUV pixel
 */
static void fade_out_yuv(uint8_t *y, uint8_t *u, uint8_t *v, float factor) {

  *y = (uint8_t)(factor * (*y - 16)) + 16;
  *u = (uint8_t)(factor * (*u - 128)) + 128;
  *v = (uint8_t)(factor * (*v - 128)) + 128;
}


static void draw_fftscope(post_plugin_fftscope_t *this, vo_frame_t *frame) {

  int i, j, c;
  int map_ptr, map_ptr_bkp;
  int amp_int, amp_max, x;
  float amp_float;
  uint32_t yuy2_pair, yuy2_pair_max, yuy2_white;
  int c_delta;

  /* clear the YUY2 map */
  for (i = 0; i < FFT_WIDTH * FFT_HEIGHT / 2; i++)
    ((uint32_t *)frame->base[0])[i] = be2me_32(0x00900080);

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

  yuy2_pair = be2me_32(
    (0x7F << 24) |
    (this->u_current << 16) |
    (0x7F << 8) |
    this->v_current);

  yuy2_white = be2me_32(
    (0xFF << 24) |
    (0x80 << 16) |
    (0xFF << 8) |
    0x80);

  for (c = 0; c < this->channels; c++){
    /* perform FFT for channel data */
    fft_window(this->fft, this->wave[c]);
    fft_scale(this->wave[c], this->fft->bits);
    fft_compute(this->fft, this->wave[c]);

    /* plot the FFT points for the channel */
    for (i = 0; i < NUMSAMPLES / 2; i++) {

      map_ptr = ((FFT_HEIGHT * (c+1) / this->channels -1 ) * FFT_WIDTH + i * 2) / 2;
      map_ptr_bkp = map_ptr;
      amp_float = fft_amp(i, this->wave[c], FFT_BITS);
      if (amp_float == 0)
        amp_int = 0;
      else
        amp_int = (int)((60/this->channels) * log10(amp_float));
      if (amp_int > 255/this->channels)
        amp_int = 255/this->channels;
      if (amp_int < 0)
        amp_int = 0;

      for (j = 0; j < amp_int; j++, map_ptr -= FFT_WIDTH / 2)
        ((uint32_t *)frame->base[0])[map_ptr] = yuy2_pair;

      /* amp max */
      yuy2_pair_max = be2me_32(
        (this->amp_max_y[c][i] << 24) |
        (this->amp_max_u[c][i] << 16) |
        (this->amp_max_y[c][i] << 8) |
        this->amp_max_v[c][i]);

      /* gravity */
      this->amp_age[c][i]++;
      if (this->amp_age[c][i] < 10) {
        amp_max = this->amp_max[c][i];
      } else {
        x = this->amp_age[c][i] - 10;
        amp_max = this->amp_max[c][i] - x * x;
      }

      /* new peak ? */
      if (amp_int > amp_max) {
        this->amp_max[c][i] = amp_int;
        this->amp_age[c][i] = 0;
        this->amp_max_y[c][i] = 0x7f;
        this->amp_max_u[c][i] = this->u_current;
        this->amp_max_v[c][i] = this->v_current;
        fade_out_yuv(&this->amp_max_y[c][i], &this->amp_max_u[c][i],
          &this->amp_max_v[c][i], 0.5);
        amp_max = amp_int;
      } else {
        fade_out_yuv(&this->amp_max_y[c][i], &this->amp_max_u[c][i],
          &this->amp_max_v[c][i], 0.95);
      }

      /* draw peaks */
      for (j = amp_int; j < (amp_max - 1); j++, map_ptr -= FFT_WIDTH / 2)
        ((uint32_t *)frame->base[0])[map_ptr] = yuy2_pair_max;

      /* top */
      ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;

      /* persistence of top */
      if (this->amp_age[c][i] >= 10) {
        x = this->amp_age[c][i] - 10;
        x = 0x5f - x;
        if (x < 0x10) x = 0x10;
        ((uint32_t *)frame->base[0])[map_ptr_bkp -
          this->amp_max[c][i] * (FFT_WIDTH / 2)] =
            be2me_32((x << 24) | (0x80 << 16) | (x << 8) | 0x80);
      }
    }
  }

  /* top line */
  for (map_ptr = 0; map_ptr < FFT_WIDTH / 2; map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;

  /* lines under each channel */
  for (c = 0; c < this->channels; c++){
    for (i = 0, map_ptr = ((FFT_HEIGHT * (c+1) / this->channels -1 ) * FFT_WIDTH) / 2;
       i < FFT_WIDTH / 2; i++, map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;
  }

}

/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

static int fftscope_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_out_t *output = (post_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)output->post;

  if (!data)
    return 0;
  /* register our stream at the new output port */
  old_port->close(old_port, XINE_ANON_STREAM);
  (new_port->open) (new_port, XINE_ANON_STREAM);
  /* reconnect ourselves */
  this->vo_port = new_port;
  return 1;
}

static int fftscope_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;
  int c, i;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);

  port->stream = stream;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;

  this->ratio = (double)FFT_WIDTH/(double)FFT_HEIGHT;

  this->channels = _x_ao_mode2channels(mode);
  if( this->channels > MAXCHANNELS )
    this->channels = MAXCHANNELS;
  this->samples_per_frame = rate / FPS;
  this->data_idx = 0;
  this->sample_counter = 0;
  this->fft = fft_new(FFT_BITS);

  (this->vo_port->open) (this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, stream->metronom);

  for (c = 0; c < this->channels; c++) {
    for (i = 0; i < (NUMSAMPLES / 2); i++) {
      this->amp_max[c][i]   = 0;
      this->amp_max_y[c][i] = 0;
      this->amp_max_u[c][i] = 0;
      this->amp_max_v[c][i] = 0;
      this->amp_age[c][i]   = 0;
    }
  }

  return (port->original_port->open) (port->original_port, stream, bits, rate, mode );
}

static void fftscope_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;

  port->stream = NULL;

  fft_dispose(this->fft);
  this->fft = NULL;

  this->vo_port->close(this->vo_port, XINE_ANON_STREAM);
  this->metronom->set_master(this->metronom, NULL);

  port->original_port->close(port->original_port, stream );

  _x_post_dec_usage(port);
}

static void fftscope_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t      *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;
  vo_frame_t             *frame;
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

      frame = this->vo_port->get_frame (this->vo_port, FFT_WIDTH, FFT_HEIGHT,
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
        draw_fftscope(this, frame);
      else
        frame->bad_frame = 1;

      frame->draw(frame, XINE_ANON_STREAM);
      frame->free(frame);
    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void fftscope_dispose(post_plugin_t *this_gen)
{
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)this_gen;

  if (_x_post_dispose(this_gen)) {

    this->metronom->exit(this->metronom);

    if(this->buf.mem)
      free(this->buf.mem);
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *fftscope_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_fftscope_t *this  = calloc(1, sizeof(post_plugin_fftscope_t));
  post_class_fftscope_t  *class = (post_class_fftscope_t *)class_gen;
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
  port->new_port.open       = fftscope_port_open;
  port->new_port.close      = fftscope_port_close;
  port->new_port.put_buffer = fftscope_port_put_buffer;

  outputv                  = &this->video_output;
  outputv->xine_out.name   = "generated video";
  outputv->xine_out.type   = XINE_POST_DATA_VIDEO;
  outputv->xine_out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->xine_out.rewire = fftscope_rewire_video;
  outputv->post            = &this->post;
  xine_list_push_back(this->post.output, outputv);

  this->post.xine_post.audio_input[0] = &port->new_port;

  this->post.dispose = fftscope_dispose;

  return &this->post;
}

/* plugin class initialization function */
void *fftscope_init_plugin(xine_t *xine, void *data)
{
  post_class_fftscope_t *class = (post_class_fftscope_t *)xine_xmalloc(sizeof(post_class_fftscope_t));

  if (!class)
    return NULL;

  class->post_class.open_plugin     = fftscope_open_plugin;
  class->post_class.identifier      = "FFT Scope";
  class->post_class.description     = N_("FFT Scope");
  class->post_class.dispose         = default_post_class_dispose;

  class->xine                       = xine;

  return &class->post_class;
}
