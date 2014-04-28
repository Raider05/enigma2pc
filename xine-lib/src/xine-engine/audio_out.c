/*
 * Copyright (C) 2000-2014 the xine project
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
 * along with self program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

/**
 * @file
 * @brief xine-lib audio output implementation
 *
 * @date 2001-08-20 First implementation of Audio sync and Audio driver separation.
 *       (c) 2001 James Courtier-Dutton <james@superbug.demon.co.uk>
 * @date 2001-08-22 James imported some useful AC3 sections from the previous
 *       ALSA driver. (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 *
 *
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every
 * audio channel to a full amount of bits.
 * So, it does not mater how many bits per sample, or how many audio channels
 * are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable,
 * rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent
 * in units of audio_frames.
 *
 * Currently, James has tested with OSS: Standard stereo out, SPDIF PCM, SPDIF AC3
 *                                 ALSA: Standard stereo out
 * No testing has been done of ALSA SPDIF AC3 or any 4,5,5.1 channel output.
 * Currently, I don't think resampling functions, as I cannot test it.
 *
 * equalizer based on
 *
 *   PCM time-domain equalizer
 *
 *   Copyright (C) 2002  Felipe Rivera <liebremx at users sourceforge net>
 *
 * heavily modified by guenter bartsch 2003 for use in libxine
 */

#ifndef	__sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif
/* required for FNDELAY decl */
#define _BSD_SOURCE 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>

#define XINE_ENABLE_EXPERIMENTAL_FEATURES
#define XINE_ENGINE_INTERNAL

#define LOG_MODULE "audio_out"
#define LOG_VERBOSE
/*
#define LOG
*/

#define LOG_RESAMPLE_SYNC 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include <xine/resample.h>
#include <xine/metronom.h>


#define NUM_AUDIO_BUFFERS       32
#define AUDIO_BUF_SIZE       32768

#define ZERO_BUF_SIZE         5000

/* By adding gap errors (difference between reported and expected
 * sound card clock) into metronom's vpts_offset we can use its
 * smoothing algorithms to correct sound card clock drifts.
 * obs: previously this error was added to xine scr.
 *
 * audio buf ---> metronom --> audio fifo --> (buf->vpts - hw_vpts)
 *           (vpts_offset + error)                     gap
 *                    <---------- control --------------|
 *
 * Unfortunately audio fifo adds a large delay to our closed loop.
 *
 * The defines below are designed to avoid updating the metronom too fast.
 * - it will only be updated 1 time per second (so it has a chance of
 *   distributing the error for several frames).
 * - it will only be updated 2 times for the whole audio fifo size
 *   length (so the control will wait to see the feedback effect)
 * - each update will be of gap/SYNC_GAP_RATE.
 *
 * Sound card clock correction can only provide smooth playback for
 * errors < 1% nominal rate. For bigger errors (bad streams) audio
 * buffers may be dropped or gaps filled with silence.
 */
#define SYNC_TIME_INVERVAL  (1 * 90000)
#define SYNC_BUF_INTERVAL   NUM_AUDIO_BUFFERS / 2
#define SYNC_GAP_RATE       4

/* Alternative for metronom feedback: fix sound card clock drift
 * by resampling all audio data, so that the sound card keeps in
 * sync with the system clock. This may help, if one uses a DXR3/H+
 * decoder board. Those have their own clock (which serves as xine's
 * master clock) and can only operate at fixed frame rates (if you
 * want smooth playback). Resampling then avoids A/V sync problems,
 * gaps filled with 0-frames and jerky video playback due to different
 * clock speeds of the sound card and DXR3/H+.
 */
#define RESAMPLE_SYNC_WINDOW 50
#define RESAMPLE_MAX_GAP_DIFF 150
#define RESAMPLE_REDUCE_GAP_THRESHOLD 200



typedef struct {
  double   last_factor;
  int      window;
  int      reduce_gap;
  uint64_t window_duration, last_vpts;
  int64_t  recent_gap[8], last_avg_gap;
  int      valid;
} resample_sync_t;

/*
 * equalizer stuff
 */

#define EQ_BANDS    10
#define EQ_CHANNELS  8

#define FP_FRBITS 28

#define EQ_REAL(x) ((int)((x) * (1 << FP_FRBITS)))

typedef struct  {
  int beta;
  int alpha;
  int gamma;
} sIIRCoefficients;

/* Coefficient history for the IIR filter */
typedef struct {
  int x[3]; /* x[n], x[n-1], x[n-2] */
  int y[3]; /* y[n], y[n-1], y[n-2] */
}sXYData;


static const sIIRCoefficients iir_cf[] = {
  /* 31 Hz*/
  { EQ_REAL(9.9691562441e-01), EQ_REAL(1.5421877947e-03), EQ_REAL(1.9968961468e+00) },
  /* 62 Hz*/
  { EQ_REAL(9.9384077546e-01), EQ_REAL(3.0796122698e-03), EQ_REAL(1.9937629855e+00) },
  /* 125 Hz*/
  { EQ_REAL(9.8774277725e-01), EQ_REAL(6.1286113769e-03), EQ_REAL(1.9874275518e+00) },
  /* 250 Hz*/
  { EQ_REAL(9.7522112569e-01), EQ_REAL(1.2389437156e-02), EQ_REAL(1.9739682661e+00) },
  /* 500 Hz*/
  { EQ_REAL(9.5105628526e-01), EQ_REAL(2.4471857368e-02), EQ_REAL(1.9461077269e+00) },
  /* 1k Hz*/
  { EQ_REAL(9.0450844499e-01), EQ_REAL(4.7745777504e-02), EQ_REAL(1.8852109613e+00) },
  /* 2k Hz*/
  { EQ_REAL(8.1778971701e-01), EQ_REAL(9.1105141497e-02), EQ_REAL(1.7444877599e+00) },
  /* 4k Hz*/
  { EQ_REAL(6.6857185264e-01), EQ_REAL(1.6571407368e-01), EQ_REAL(1.4048592171e+00) },
  /* 8k Hz*/
  { EQ_REAL(4.4861333678e-01), EQ_REAL(2.7569333161e-01), EQ_REAL(6.0518718075e-01) },
  /* 16k Hz*/
  { EQ_REAL(2.4201241845e-01), EQ_REAL(3.7899379077e-01), EQ_REAL(-8.0847117831e-01) },
};

typedef struct {

  xine_audio_port_t    ao; /* public part */

  /* private stuff */
  ao_driver_t         *driver;
  pthread_mutex_t      driver_lock;

  uint32_t             driver_open:1;
  uint32_t             audio_loop_running:1;
  uint32_t             audio_thread_created:1;
  uint32_t             grab_only:1; /* => do not start thread, frontend will consume samples */
  uint32_t             do_resample:1;
  uint32_t             do_compress:1;
  uint32_t             do_amp:1;
  uint32_t             amp_mute:1;
  uint32_t             do_equ:1;

  int                  num_driver_actions; /* number of threads, that wish to call
                                            * functions needing driver_lock */
  pthread_mutex_t      driver_action_lock; /* protects num_driver_actions */
  pthread_cond_t       driver_action_cond; /* informs about num_driver_actions-- */

  metronom_clock_t    *clock;
  xine_t              *xine;
  xine_list_t         *streams;
  pthread_mutex_t      streams_lock;

  pthread_t       audio_thread;

  int64_t         audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t         frames_per_kpts;      /* frames per 1024/90000 sec                  */

  int             av_sync_method_conf;
  resample_sync_t resample_sync_info;
  double          resample_sync_factor; /* correct buffer length by this factor
                                         * to sync audio hardware to (dxr3) clock */
  int             resample_sync_method; /* fix sound card clock drift by resampling */

  int             gap_tolerance;

  ao_format_t     input, output;        /* format conversion done at audio_out.c */
  double          frame_rate_factor;
  double          output_frame_excess;  /* used to keep track of 'half' frames */

  int             resample_conf;
  uint32_t        force_rate;           /* force audio output rate to this value if non-zero */
  audio_fifo_t   *free_fifo;
  audio_fifo_t   *out_fifo;
  int64_t         last_audio_vpts;
  pthread_mutex_t current_speed_lock;
  uint32_t        current_speed;        /* the current playback speed */
  int             slow_fast_audio;      /* play audio even on slow/fast speeds */

  int16_t	  last_sample[RESAMPLE_MAX_CHANNELS];
  audio_buffer_t *frame_buf[2];         /* two buffers for "stackable" conversions */
  int16_t        *zero_space;

  int64_t         passthrough_offset;
  int             flush_audio_driver;
  int             discard_buffers;
  pthread_mutex_t flush_audio_driver_lock;
  pthread_cond_t  flush_audio_driver_reached;

  /* some built-in audio filters */

  double          compression_factor;   /* current compression */
  double          compression_factor_max; /* user limit on compression */
  double          amp_factor;

  /* 10-band equalizer */

  int             eq_gain[EQ_BANDS];
  int             eq_preamp;
  int             eq_i;
  int             eq_j;
  int             eq_k;

  sXYData         eq_data_history[EQ_BANDS][EQ_CHANNELS];

  int             last_gap;

} aos_t;

struct audio_fifo_s {
  audio_buffer_t    *first;
  audio_buffer_t    *last;

  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
  pthread_cond_t     empty;

  int                num_buffers;
  int                num_buffers_max;
};

static int ao_get_property (xine_audio_port_t *this_gen, int property);
static int ao_set_property (xine_audio_port_t *this_gen, int property, int value);

static audio_fifo_t *XINE_MALLOC fifo_new (xine_t *xine) {

  audio_fifo_t *fifo;

  fifo = (audio_fifo_t *) calloc(1, sizeof(audio_fifo_t));

  if (!fifo)
    return NULL;

  fifo->first           = NULL;
  fifo->last            = NULL;
  fifo->num_buffers     = 0;
  fifo->num_buffers_max = 0;
  pthread_mutex_init (&fifo->mutex, NULL);
  pthread_cond_init  (&fifo->not_empty, NULL);
  pthread_cond_init  (&fifo->empty, NULL);

  return fifo;
}

static void fifo_append_int (audio_fifo_t *fifo,
			     audio_buffer_t *buf) {

  /* buf->next = NULL; */

  _x_assert(!buf->next);

  if (!fifo->first) {
    fifo->first       = buf;
    fifo->last        = buf;
    fifo->num_buffers = 1;

  } else {

    fifo->last->next = buf;
    fifo->last       = buf;
    fifo->num_buffers++;

  }
  
  if (fifo->num_buffers_max < fifo->num_buffers)
    fifo->num_buffers_max = fifo->num_buffers;

  pthread_cond_signal (&fifo->not_empty);
}

static void fifo_append (audio_fifo_t *fifo,
			 audio_buffer_t *buf) {

  pthread_mutex_lock (&fifo->mutex);
  fifo_append_int (fifo, buf);
  pthread_mutex_unlock (&fifo->mutex);
}

static audio_buffer_t *fifo_peek_int (audio_fifo_t *fifo, int blocking) {
  while (!fifo->first) {
    pthread_cond_signal (&fifo->empty);
    if (blocking)
      pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
    else {
      struct timeval tv;
      struct timespec ts;
      gettimeofday(&tv, NULL);
      ts.tv_sec  = tv.tv_sec + 1;
      ts.tv_nsec = tv.tv_usec * 1000;
      if (pthread_cond_timedwait (&fifo->not_empty, &fifo->mutex, &ts) != 0)
        return NULL;
    }
  }
  return fifo->first;
}

static audio_buffer_t *fifo_remove_int (audio_fifo_t *fifo, int blocking) {
  audio_buffer_t *buf = fifo_peek_int(fifo, blocking);
  if (!buf)
    return NULL;

  fifo->first = buf->next;

  if (!fifo->first) {

    fifo->last = NULL;
    fifo->num_buffers = 0;
    pthread_cond_signal (&fifo->empty);

  } else
    fifo->num_buffers--;

  buf->next = NULL;

  return buf;
}

static audio_buffer_t *fifo_peek (audio_fifo_t *fifo) {

  audio_buffer_t *buf;

  pthread_mutex_lock (&fifo->mutex);
  buf = fifo_peek_int(fifo, 1);
  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

static audio_buffer_t *fifo_remove (audio_fifo_t *fifo) {

  audio_buffer_t *buf;

  pthread_mutex_lock (&fifo->mutex);
  buf = fifo_remove_int(fifo, 1);
  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

static audio_buffer_t *fifo_remove_nonblock (audio_fifo_t *fifo) {

  audio_buffer_t *buf;

  pthread_mutex_lock (&fifo->mutex);
  buf = fifo_remove_int(fifo, 0);
  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

/* This function is currently not needed */
#if 0
static int fifo_num_buffers (audio_fifo_t *fifo) {

  int ret;

  pthread_mutex_lock (&fifo->mutex);
  ret = fifo->num_buffers;
  pthread_mutex_unlock (&fifo->mutex);

  return ret;
}
#endif

static void fifo_wait_empty (audio_fifo_t *fifo) {

  pthread_mutex_lock (&fifo->mutex);
  while (fifo->first) {
    /* i think it's strange to send not_empty signal here (beside the enqueue
     * function), but it should do no harm. [MF] */
    pthread_cond_signal (&fifo->not_empty);
    pthread_cond_wait (&fifo->empty, &fifo->mutex);
  }
  pthread_mutex_unlock (&fifo->mutex);
}


static void write_pause_burst(aos_t *this, uint32_t num_frames) {
  uint16_t sbuf[4096];

  sbuf[0] = 0xf872;
  sbuf[1] = 0x4e1f;
  /* Audio ES Channel empty, wait for DD Decoder or pause */
  sbuf[2] = 0x0003;
  sbuf[3] = 0x0020;
  memset(&sbuf[4], 0, sizeof(sbuf) - 4 * sizeof(uint16_t));
  while (num_frames > 1536) {
    pthread_mutex_lock( &this->driver_lock );
    if(this->driver_open)
      this->driver->write(this->driver, sbuf, 1536);
    pthread_mutex_unlock( &this->driver_lock );
    num_frames -= 1536;
  }
}


static void ao_fill_gap (aos_t *this, int64_t pts_len) {

  int64_t num_frames ;

  num_frames = pts_len * this->frames_per_kpts / 1024;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
           "audio_out: inserting %" PRId64 " 0-frames to fill a gap of %" PRId64 " pts\n", num_frames, pts_len);

  if ((this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5)) {
    write_pause_burst(this,num_frames);
    return;
  }

  while (num_frames > 0 && !this->discard_buffers) {
    if (num_frames > ZERO_BUF_SIZE) {
      pthread_mutex_lock( &this->driver_lock );
      if(this->driver_open)
        this->driver->write(this->driver, this->zero_space, ZERO_BUF_SIZE);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames -= ZERO_BUF_SIZE;
    } else {
      pthread_mutex_lock( &this->driver_lock );
      if(this->driver_open)
        this->driver->write(this->driver, this->zero_space, num_frames);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames = 0;
    }
  }
}

static void ensure_buffer_size (audio_buffer_t *buf, int bytes_per_frame,
                                int frames)
{
  int size = bytes_per_frame * frames;

  if (buf->mem_size < size) {
    buf->mem = realloc( buf->mem, size );
    buf->mem_size = size;
  }
  buf->num_frames = frames;
}

static audio_buffer_t * swap_frame_buffers ( aos_t *this ) {
  audio_buffer_t *tmp;

  tmp = this->frame_buf[1];
  this->frame_buf[1] = this->frame_buf[0];
  this->frame_buf[0] = tmp;
  return this->frame_buf[0];
}

int _x_ao_mode2channels( int mode ) {
  switch( mode ) {
  case AO_CAP_MODE_MONO:
    return 1;
  case AO_CAP_MODE_STEREO:
    return 2;
  case AO_CAP_MODE_4CHANNEL:
    return 4;
  case AO_CAP_MODE_4_1CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    return 6;
  }
  return 0;
}

int _x_ao_channels2mode( int channels ) {

  switch( channels ) {
    case 1:
      return AO_CAP_MODE_MONO;
    case 2:
      return AO_CAP_MODE_STEREO;
    case 3:
    case 4:
      return AO_CAP_MODE_4CHANNEL;
    case 5:
      return AO_CAP_MODE_5CHANNEL;
    case 6:
      return AO_CAP_MODE_5_1CHANNEL;
  }
  return AO_CAP_NOCAP;
}

static void audio_filter_compress (aos_t *this, int16_t *mem, int num_frames) {

  int    i, maxs;
  double f_max;
  int    num_channels;

  num_channels = _x_ao_mode2channels (this->input.mode);
  if (!num_channels)
    return;

  maxs = 0;

  /* measure */

  for (i=0; i<num_frames*num_channels; i++) {
    int16_t sample = abs(mem[i]);
    if (sample>maxs)
      maxs = sample;
  }

  /* calc maximum possible & allowed factor */

  if (maxs>0) {
    f_max = 32767.0 / maxs;
    this->compression_factor = this->compression_factor * 0.999 + f_max * 0.001;
    if (this->compression_factor > f_max)
      this->compression_factor = f_max;

    if (this->compression_factor > this->compression_factor_max)
      this->compression_factor = this->compression_factor_max;
  } else
    f_max = 1.0;

  lprintf ("max=%d f_max=%f compression_factor=%f\n", maxs, f_max, this->compression_factor);

  /* apply it */

  for (i=0; i<num_frames*num_channels; i++) {
    /* 0.98 to avoid overflow */
    mem[i] = mem[i] * 0.98 * this->compression_factor * this->amp_factor;
  }
}

static void audio_filter_amp (aos_t *this, void *buf, int num_frames) {
  double amp_factor;
  int    i;
  const int total_frames = num_frames * _x_ao_mode2channels (this->input.mode);

  if (!total_frames)
    return;

  amp_factor=this->amp_factor;
  if (this->amp_mute || amp_factor == 0) {
    memset (buf, 0, total_frames * (this->input.bits / 8));
    return;
  }

  if (this->input.bits == 8) {
    int16_t test;
    int8_t *mem = (int8_t *) buf;

    for (i=0; i<total_frames; i++) {
      test = mem[i] * amp_factor;
      /* Force limit on amp_factor to prevent clipping */
      if (test < INT8_MIN) {
        this->amp_factor = amp_factor = amp_factor * INT8_MIN / test;
	test=INT8_MIN;
      }
      if (test > INT8_MAX) {
        this->amp_factor = amp_factor = amp_factor * INT8_MIN / test;
	test=INT8_MAX;
      }
      mem[i] = test;
    }
  } else if (this->input.bits == 16) {
    int32_t test;
    int16_t *mem = (int16_t *) buf;

    for (i=0; i<total_frames; i++) {
      test = mem[i] * amp_factor;
      /* Force limit on amp_factor to prevent clipping */
      if (test < INT16_MIN) {
        this->amp_factor = amp_factor = amp_factor * INT16_MIN / test;
	test=INT16_MIN;
      }
      if (test > INT16_MAX) {
        this->amp_factor = amp_factor = amp_factor * INT16_MIN / test;
	test=INT16_MAX;
      }
      mem[i] = test;
    }
  }
}

static void audio_filter_equalize (aos_t *this,
				   int16_t *data, int num_frames) {
  int       index, band, channel;
  int       length;
  int       out[EQ_CHANNELS], scaledpcm[EQ_CHANNELS];
  int64_t l;
  int       num_channels;

  num_channels = _x_ao_mode2channels (this->input.mode);
  if (!num_channels)
    return;

  length = num_frames * num_channels;

  for (index = 0; index < length; index += num_channels) {

    for (channel = 0; channel < num_channels; channel++) {

      /* Convert the PCM sample to a fixed fraction */
      scaledpcm[channel] = ((int)data[index+channel]) << (FP_FRBITS-16-1);

      out[channel] = 0;
      /*  For each band */
      for (band = 0; band < EQ_BANDS; band++) {

	this->eq_data_history[band][channel].x[this->eq_i] = scaledpcm[channel];
	l = (int64_t)iir_cf[band].alpha * (int64_t)(this->eq_data_history[band][channel].x[this->eq_i] - this->eq_data_history[band][channel].x[this->eq_k])
	  + (int64_t)iir_cf[band].gamma * (int64_t)this->eq_data_history[band][channel].y[this->eq_j]
	  - (int64_t)iir_cf[band].beta * (int64_t)this->eq_data_history[band][channel].y[this->eq_k];
	this->eq_data_history[band][channel].y[this->eq_i] = (int)(l >> FP_FRBITS);
	l = (int64_t)this->eq_data_history[band][channel].y[this->eq_i] * (int64_t)this->eq_gain[band];
	out[channel] +=	(int)(l >> FP_FRBITS);
      }

      /*  Volume scaling adjustment by 2^-2 */
      out[channel] += (scaledpcm[channel] >> 2);

      /* Adjust the fixed point fraction value to a PCM sample */
      /* Scale back to a 16bit signed int */
      out[channel] >>= (FP_FRBITS-16);

      /* Limit the output */
      if (out[channel] < -32768)
	data[index+channel] = -32768;
      else if (out[channel] > 32767)
	data[index+channel] = 32767;
      else
	data[index+channel] = out[channel];
    }

    this->eq_i++; this->eq_j++; this->eq_k++;
    if (this->eq_i == 3) this->eq_i = 0;
    else if (this->eq_j == 3) this->eq_j = 0;
    else this->eq_k = 0;
  }

}

static audio_buffer_t* prepare_samples( aos_t *this, audio_buffer_t *buf) {
  double          acc_output_frames;
  int             num_output_frames ;

  /*
   * volume / compressor / equalizer filter
   */

  if (this->amp_factor == 0) {
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  } else if (this->input.bits == 16) {
    if (this->do_equ)
      audio_filter_equalize (this, buf->mem, buf->num_frames);
    if (this->do_compress)
      audio_filter_compress (this, buf->mem, buf->num_frames);
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  } else if (this->input.bits == 8) {
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  }


  /*
   * resample and output audio data
   */

  /* calculate number of output frames (after resampling) */
  acc_output_frames = (double) buf->num_frames * this->frame_rate_factor
    * this->resample_sync_factor + this->output_frame_excess;

  /* Truncate to an integer */
  num_output_frames = acc_output_frames;

  /* Keep track of the amount truncated */
  this->output_frame_excess = acc_output_frames - (double) num_output_frames;
  if ( this->output_frame_excess != 0 &&
       !this->do_resample && !this->resample_sync_method)
    this->output_frame_excess = 0;

  lprintf ("outputting %d frames\n", num_output_frames);

  /* convert 8 bit samples as needed */
  if ( this->input.bits == 8 &&
       (this->resample_sync_method || this->do_resample ||
        this->output.bits != 8 || this->input.mode != this->output.mode) ) {
    int channels = _x_ao_mode2channels(this->input.mode);
    ensure_buffer_size(this->frame_buf[1], 2*channels, buf->num_frames );
    _x_audio_out_resample_8to16((int8_t *)buf->mem, this->frame_buf[1]->mem,
                                channels * buf->num_frames );
    buf = swap_frame_buffers(this);
  }

  /* check if resampling may be skipped */
  if ( (this->resample_sync_method || this->do_resample) &&
       buf->num_frames != num_output_frames ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3), num_output_frames);
      _x_audio_out_resample_mono (this->last_sample, buf->mem, buf->num_frames,
			       this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_STEREO:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*2, num_output_frames);
      _x_audio_out_resample_stereo (this->last_sample, buf->mem, buf->num_frames,
				 this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_4CHANNEL:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*4, num_output_frames);
      _x_audio_out_resample_4channel (this->last_sample, buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*6, num_output_frames);
      _x_audio_out_resample_6channel (this->last_sample, buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      /* pass-through modes: no resampling */
      break;
    }
  } else {
    /* maintain last_sample in case we need it */
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      memcpy (this->last_sample, &buf->mem[buf->num_frames - 1], sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_STEREO:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 2], 2 * sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_4CHANNEL:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 4], 4 * sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 6], 6 * sizeof (this->last_sample[0]));
      break;
    default:;
    }
  }

  /* mode conversion */
  if ( this->input.mode != this->output.mode ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      if( this->output.mode == AO_CAP_MODE_STEREO ) {
	ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*2, buf->num_frames );
	_x_audio_out_resample_monotostereo(buf->mem, this->frame_buf[1]->mem,
					   buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_STEREO:
      if( this->output.mode == AO_CAP_MODE_MONO ) {
	ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3), buf->num_frames );
	_x_audio_out_resample_stereotomono(buf->mem, this->frame_buf[1]->mem,
					   buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_4CHANNEL:
      break;
    case AO_CAP_MODE_5CHANNEL:
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      break;
    }
  }

  /* convert back to 8 bits after resampling */
  if( this->output.bits == 8 &&
        (this->resample_sync_method || this->do_resample ||
         this->input.mode != this->output.mode) ) {
    int channels = _x_ao_mode2channels(this->output.mode);
    ensure_buffer_size(this->frame_buf[1], channels, buf->num_frames );
    _x_audio_out_resample_16to8(buf->mem, (int8_t *)this->frame_buf[1]->mem,
                                channels * buf->num_frames );
    buf = swap_frame_buffers(this);
  }
  return buf;
}


static int resample_rate_adjust(aos_t *this, int64_t gap, audio_buffer_t *buf) {

  /* Calculates the drift factor used to resample the audio data to
   * keep in sync with system (or dxr3) clock.
   *
   * To compensate the sound card drift it is necessary to know, how many audio
   * frames need to be added (or removed) via resampling. This function waits for
   * RESAMPLE_SYNC_WINDOW audio buffers to be sent to the card and keeps track
   * of their total duration in vpts. With the measured gap difference between
   * the reported gap values at the beginning and at the end of this window the
   * required resampling factor is calculated:
   *
   * resample_factor = (duration + gap_difference) / duration
   *
   * This factor is then used in prepare_samples() to resample the audio
   * buffers as needed so we keep in sync with the system (or dxr3) clock.
   */

  resample_sync_t *info = &this->resample_sync_info;
  int64_t avg_gap = 0;
  double factor;
  double diff;
  double duration;
  int i;

  if (abs(gap) > AO_MAX_GAP) {
    /* drop buffers or insert 0-frames in audio out loop */
    info->valid = 0;
    return -1;
  }

  if ( ! info->valid) {
    this->resample_sync_factor = 1.0;
    info->window = 0;
    info->reduce_gap = 0;
    info->last_avg_gap = gap;
    info->last_factor = 0;
    info->window_duration = info->last_vpts = 0;
    info->valid = 1;
  }

  /* calc average gap (to compensate small errors during measurement) */
  for (i = 0; i < 7; i++) info->recent_gap[i] = info->recent_gap[i + 1];
  info->recent_gap[i] = gap;
  for (i = 0; i < 8; i++) avg_gap += info->recent_gap[i];
  avg_gap /= 8;


  /* gap too big? Change sample rate so that gap converges towards 0. */

  if (abs(avg_gap) > RESAMPLE_REDUCE_GAP_THRESHOLD && !info->reduce_gap) {
    info->reduce_gap = 1;
    this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;

    llprintf (LOG_RESAMPLE_SYNC,
              "sample rate adjusted to reduce gap: gap=%" PRId64 "\n", avg_gap);
    return 0;

  } else if (info->reduce_gap && abs(avg_gap) < 50) {
    info->reduce_gap = 0;
    info->valid = 0;
    llprintf (LOG_RESAMPLE_SYNC, "gap successfully reduced\n");
    return 0;

  } else if (info->reduce_gap) {
    /* re-check, because the gap might suddenly change its sign,
     * also slow down, when getting close to zero (-300<gap<300) */
    if (abs(avg_gap) > 300)
      this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;
    else
      this->resample_sync_factor = (avg_gap < 0) ? 0.998 : 1.002;
    return 0;
  }


  if (info->window > RESAMPLE_SYNC_WINDOW) {

    /* adjust drift correction */

    int64_t gap_diff = avg_gap - info->last_avg_gap;

    if (gap_diff < RESAMPLE_MAX_GAP_DIFF) {
#if LOG_RESAMPLE_SYNC
      int num_frames;

      /* if we are already resampling to a different output rate, consider
       * this during calculation */
      num_frames = (this->do_resample) ? (buf->num_frames * this->frame_rate_factor)
        : buf->num_frames;
      printf("audio_out: gap=%5" PRId64 ";  gap_diff=%5" PRId64 ";  frame_diff=%3.0f;  drift_factor=%f\n",
             avg_gap, gap_diff, num_frames * info->window * info->last_factor,
             this->resample_sync_factor);
#endif
      /* we want to add factor * num_frames to each buffer */
      diff = gap_diff;
#if _MSCVER <= 1200
      /* ugly hack needed by old Visual C++ 6.0 */
      duration = (int64_t)info->window_duration;
#else
      duration = info->window_duration;
#endif
      factor = diff / duration + info->last_factor;

      info->last_factor = factor;
      this->resample_sync_factor = 1.0 + factor;

      info->last_avg_gap = avg_gap;
      info->window_duration = 0;
      info->window = 0;
    } else
      info->valid = 0;

  } else {

    /* collect data for next adjustment */
    if (info->window > 0)
      info->window_duration += buf->vpts - info->last_vpts;
    info->last_vpts = buf->vpts;
    info->window++;
  }

  return 0;
}

static int ao_change_settings(aos_t *this, uint32_t bits, uint32_t rate, int mode);

/* Audio output loop: -
 * 1) Check for pause.
 * 2) Make sure audio hardware is in RUNNING state.
 * 3) Get delay
 * 4) Do drop, 0-fill or output samples.
 * 5) Go round loop again.
 */
static void *ao_loop (void *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  int64_t         hw_vpts;
  audio_buffer_t *in_buf, *out_buf;
  int64_t         gap;
  int64_t         delay;
  int64_t         cur_time;
  int64_t         last_sync_time;
  int             bufs_since_sync;
  int             result;

  last_sync_time = bufs_since_sync = 0;
  in_buf = NULL;
  cur_time = -1;

  while ((this->audio_loop_running) ||
	 (!this->audio_loop_running && this->out_fifo->first)) {

    /*
     * get buffer to process for this loop iteration
     */

    if (!in_buf) {
      lprintf ("loop: get buf from fifo\n");
      in_buf = fifo_peek (this->out_fifo);
      bufs_since_sync++;
      lprintf ("got a buffer\n");
    }

    pthread_mutex_lock(&this->flush_audio_driver_lock);
    if (this->flush_audio_driver) {
      this->ao.control(&this->ao, AO_CTRL_FLUSH_BUFFERS, NULL);
      this->flush_audio_driver--;
      pthread_cond_broadcast(&this->flush_audio_driver_reached);
    }

    if (this->discard_buffers) {
      fifo_remove (this->out_fifo);
      if (in_buf->stream)
	_x_refcounter_dec(in_buf->stream->refcounter);
      fifo_append (this->free_fifo, in_buf);
      in_buf = NULL;
      pthread_mutex_unlock(&this->flush_audio_driver_lock);
      continue;
    }
    pthread_mutex_unlock(&this->flush_audio_driver_lock);

    /* Paranoia? */
    {
      int new_speed = this->clock->speed;
      if (new_speed != this->current_speed)
        ao_set_property (&this->ao, AO_PROP_CLOCK_SPEED, new_speed);
    }

    /*
     * wait until user unpauses stream
     * if we are playing at a different speed (without slow_fast_audio flag)
     * we must process/free buffers otherwise the entire engine will stop.
     */

    pthread_mutex_lock(&this->current_speed_lock);
    if ( this->audio_loop_running &&
         (this->current_speed == XINE_SPEED_PAUSE ||
          (this->current_speed != XINE_FINE_SPEED_NORMAL &&
           !this->slow_fast_audio) ) )  {

      if (this->current_speed != XINE_SPEED_PAUSE) {

	cur_time = this->clock->get_current_time (this->clock);
	if (in_buf->vpts < cur_time ) {
	  lprintf ("loop: next fifo\n");
	  fifo_remove (this->out_fifo);
	  if (in_buf->stream)
	    _x_refcounter_dec(in_buf->stream->refcounter);
	  fifo_append (this->free_fifo, in_buf);
	  in_buf = NULL;
	  pthread_mutex_unlock(&this->current_speed_lock);
	  continue;
	}

        if ((in_buf->vpts - cur_time) > 2 * 90000)
	  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		 "audio_out: vpts/clock error, in_buf->vpts=%" PRId64 " cur_time=%" PRId64 "\n",
		 in_buf->vpts, cur_time);
      }

      lprintf ("loop:pause: I feel sleepy (%d buffers).\n", this->out_fifo->num_buffers);
      pthread_mutex_unlock(&this->current_speed_lock);
      xine_usec_sleep (10000);
      lprintf ("loop:pause: I wake up.\n");
      continue;
    }

    /* change driver's settings as needed */
    pthread_mutex_lock( &this->driver_lock );
    if( in_buf && in_buf->num_frames ) {
      if( !this->driver_open ||
         in_buf->format.bits != this->input.bits ||
         in_buf->format.rate != this->input.rate ||
         in_buf->format.mode != this->input.mode ) {
         lprintf("audio format has changed\n");
         if( !in_buf->stream->emergency_brake &&
             ao_change_settings(this,
                                in_buf->format.bits,
                                in_buf->format.rate,
                                in_buf->format.mode) == 0 ) {
             in_buf->stream->emergency_brake = 1;
             _x_message (in_buf->stream, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
         }
      }
    }

    if(this->driver_open) {
      delay = this->driver->delay(this->driver);
      while (delay < 0 && this->audio_loop_running) {
        /* Get the audio card into RUNNING state. */
        ao_fill_gap (this, 10000); /* FIXME, this PTS of 1000 should == period size */
        delay = this->driver->delay(this->driver);
      }
      pthread_mutex_unlock( &this->driver_lock );
    } else {
      xine_stream_t *stream;
      delay = 0;

      pthread_mutex_unlock( &this->driver_lock );

      if (in_buf && in_buf->num_frames) {
	xine_list_iterator_t ite;

	xprintf(this->xine, XINE_VERBOSITY_LOG,
		_("audio_out: delay calculation impossible with an unavailable audio device\n"));

	pthread_mutex_lock(&this->streams_lock);
	for (ite = xine_list_front(this->streams);
	     ite; ite = xine_list_next(this->streams, ite)) {
	  stream = xine_list_get_value (this->streams, ite);
          if( !stream->emergency_brake ) {
            stream->emergency_brake = 1;
            _x_message (stream, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
          }
	}
	pthread_mutex_unlock(&this->streams_lock);
      }
    }

    cur_time = this->clock->get_current_time (this->clock);

    /* we update current_extra_info if either there is no video stream that could do that
     * or if the current_extra_info is getting too much out of date */
    if( in_buf && in_buf->stream && (!in_buf->stream->video_decoder_plugin ||
        (cur_time - in_buf->stream->current_extra_info->vpts) > 30000 )) {

      pthread_mutex_lock( &in_buf->stream->current_extra_info_lock );
      _x_extra_info_merge( in_buf->stream->current_extra_info, in_buf->extra_info );
      pthread_mutex_unlock( &in_buf->stream->current_extra_info_lock );
    }

    /*
     * where, in the timeline is the "end" of the
     * hardware audio buffer at the moment?
     */

    hw_vpts = cur_time;
    lprintf ("current delay is %" PRId64 ", current time is %" PRId64 "\n", delay, cur_time);

    /* External A52 decoder delay correction */
    if ((this->output.mode==AO_CAP_MODE_A52) || (this->output.mode==AO_CAP_MODE_AC5))
      delay += this->passthrough_offset;

    if(this->frames_per_kpts)
      hw_vpts += (delay * 1024) / this->frames_per_kpts;

    /*
     * calculate gap:
     */
    gap = in_buf->vpts - hw_vpts;
    this->last_gap = gap;
    lprintf ("hw_vpts : %" PRId64 " buffer_vpts : %" PRId64 " gap : %" PRId64 "\n",
             hw_vpts, in_buf->vpts, gap);

    if (this->resample_sync_method) {
      /* Correct sound card drift via resampling. If gap is too big to
       * be corrected this way, we use the fallback: drop/insert frames.
       * This function only calculates the drift correction factor. The
       * actual resampling is done by prepare_samples().
       */
      resample_rate_adjust(this, gap, in_buf);
    } else {
      this->resample_sync_factor = 1.0;
    }

    /*
     * output audio data synced to master clock
     */

    if (gap < (-1 * AO_MAX_GAP) || !in_buf->num_frames ) {

      /* drop package */
      lprintf ("loop: drop package, next fifo\n");
      fifo_remove (this->out_fifo);
      if (in_buf->stream)
	_x_refcounter_dec(in_buf->stream->refcounter);
      fifo_append (this->free_fifo, in_buf);

      lprintf ("audio package (vpts = %" PRId64 ", gap = %" PRId64 ") dropped\n",
               in_buf->vpts, gap);
      in_buf = NULL;


      /* for small gaps ( tolerance < abs(gap) < AO_MAX_GAP )
       * feedback them into metronom's vpts_offset (when using
       * metronom feedback for A/V sync)
       */
    } else if ( abs(gap) < AO_MAX_GAP && abs(gap) > this->gap_tolerance &&
                cur_time > (last_sync_time + SYNC_TIME_INVERVAL) &&
                bufs_since_sync >= SYNC_BUF_INTERVAL &&
                !this->resample_sync_method ) {
	xine_list_iterator_t *ite;
        lprintf ("audio_loop: ADJ_VPTS\n");
	pthread_mutex_lock(&this->streams_lock);
	for (ite = xine_list_front(this->streams); ite;
	     ite = xine_list_next(this->streams, ite)) {
	  xine_stream_t *stream = xine_list_get_value(this->streams, ite);
	  if (stream == XINE_ANON_STREAM) continue;
	  stream->metronom->set_option(stream->metronom, METRONOM_ADJ_VPTS_OFFSET,
                                       -gap/SYNC_GAP_RATE );
          last_sync_time = cur_time;
          bufs_since_sync = 0;
	}
	pthread_mutex_unlock(&this->streams_lock);

    } else if ( gap > AO_MAX_GAP ) {
      /* for big gaps output silence */
      ao_fill_gap (this, gap);
    } else {
#if 0
      {
        int count;
        printf("Audio data\n");
        for (count=0;count < 10;count++) {
          printf("%x ",buf->mem[count]);
        }
        printf("\n");
      }
#endif
      out_buf = prepare_samples (this, in_buf);
#if 0
      {
        int count;
        printf("Audio data2\n");
        for (count=0;count < 10;count++) {
          printf("%x ",out_buf->mem[count]);
        }
        printf("\n");
      }
#endif

      lprintf ("loop: writing %d samples to sound device\n", out_buf->num_frames);

      if (this->driver_open) {
        pthread_mutex_lock( &this->driver_lock );
        result = this->driver_open ? this->driver->write (this->driver, out_buf->mem, out_buf->num_frames ) : 0;
        pthread_mutex_unlock( &this->driver_lock );
      } else {
        result = 0;
      }
      fifo_remove (this->out_fifo);

      if( result < 0 ) {
        /* device unplugged. */
        xprintf(this->xine, XINE_VERBOSITY_LOG, _("write to sound card failed. Assuming the device was unplugged.\n"));
        _x_message (in_buf->stream, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);

        pthread_mutex_lock( &this->driver_lock );
        if(this->driver_open) {
          this->driver->close(this->driver);
          this->driver_open = 0;
          this->driver->exit(this->driver);
          this->driver = _x_load_audio_output_plugin (this->xine, "none");
          if (this->driver && !in_buf->stream->emergency_brake &&
              ao_change_settings(this,
                in_buf->format.bits,
                in_buf->format.rate,
                in_buf->format.mode) == 0) {
            in_buf->stream->emergency_brake = 1;
            _x_message (in_buf->stream, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
          }
        }
        pthread_mutex_unlock( &this->driver_lock );
        /* closing the driver will result in XINE_MSG_AUDIO_OUT_UNAVAILABLE to be emitted */
      }

      lprintf ("loop: next buf from fifo\n");
      if (in_buf->stream)
	_x_refcounter_dec(in_buf->stream->refcounter);
      fifo_append (this->free_fifo, in_buf);
      in_buf = NULL;
    }
    pthread_mutex_unlock(&this->current_speed_lock);

    /* Give other threads a chance to use functions which require this->driver_lock to
     * be available. This is needed when using NPTL on Linux (and probably PThreads
     * on Solaris as well). */
    if (this->num_driver_actions > 0) {
      /* calling sched_yield() is not sufficient on multicore systems */
      /* sched_yield(); */
      /* instead wait for the other thread to acquire this->driver_lock */
      pthread_mutex_lock(&this->driver_action_lock);
      if (this->num_driver_actions > 0)
        pthread_cond_wait(&this->driver_action_cond, &this->driver_action_lock);
      pthread_mutex_unlock(&this->driver_action_lock);
    }
  }

  if (in_buf) {
    if (in_buf->stream)
      _x_refcounter_dec(in_buf->stream->refcounter);
    fifo_append (this->free_fifo, in_buf);
  }

  return NULL;
}

/*
 * public a/v processing interface
 */

int xine_get_next_audio_frame (xine_audio_port_t *this_gen,
			       xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *in_buf = NULL, *out_buf;
  xine_stream_t  *stream = NULL;

  lprintf ("get_next_audio_frame\n");

  while (!in_buf || !stream) {
    xine_list_iterator_t ite = xine_list_front (this->streams);

    if (!ite) {
      xine_usec_sleep (5000);
      continue;
    }
    stream = xine_list_get_value(this->streams, ite);

    /* FIXME: ugly, use conditions and locks instead? */

    pthread_mutex_lock (&this->out_fifo->mutex);
    in_buf = this->out_fifo->first;
    if (!in_buf) {
      pthread_mutex_unlock(&this->out_fifo->mutex);
      if (stream != XINE_ANON_STREAM && stream->audio_fifo->fifo_size == 0 &&
	  stream->demux_plugin->get_status(stream->demux_plugin) !=DEMUX_OK)
        /* no further data can be expected here */
        return 0;
      xine_usec_sleep (5000);
      continue;
    }
  }

  in_buf = fifo_remove_int (this->out_fifo, 1);
  pthread_mutex_unlock(&this->out_fifo->mutex);

  out_buf = prepare_samples (this, in_buf);

  if (out_buf != in_buf) {
    if (in_buf->stream)
      _x_refcounter_dec(in_buf->stream->refcounter);
    fifo_append (this->free_fifo, in_buf);
    frame->xine_frame = NULL;
  } else
    frame->xine_frame    = out_buf;

  frame->vpts            = out_buf->vpts;
  frame->num_samples     = out_buf->num_frames;
  frame->sample_rate     = this->input.rate;
  frame->num_channels    = _x_ao_mode2channels (this->input.mode);
  frame->bits_per_sample = this->input.bits;
  frame->pos_stream      = out_buf->extra_info->input_normpos;
  frame->pos_time        = out_buf->extra_info->input_time;
  frame->data            = (uint8_t *) out_buf->mem;

  return 1;
}

void xine_free_audio_frame (xine_audio_port_t *this_gen, xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  buf = (audio_buffer_t *) frame->xine_frame;

  if (buf) {
    if (buf->stream)
      _x_refcounter_dec(buf->stream->refcounter);
    fifo_append (this->free_fifo, buf);
  }
}

static int ao_update_resample_factor(aos_t *this) {

  if( !this->driver_open )
    return 0;

  switch (this->resample_conf) {
  case 1: /* force off */
    this->do_resample = 0;
    break;
  case 2: /* force on */
    this->do_resample = 1;
    break;
  default: /* AUTO */
    if( !this->slow_fast_audio || this->current_speed == XINE_SPEED_PAUSE )
      this->do_resample = this->output.rate != this->input.rate;
    else
      this->do_resample = (this->output.rate*this->current_speed/XINE_FINE_SPEED_NORMAL) != this->input.rate;
  }

  if (this->do_resample)
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
             "will resample audio from %d to %d\n", this->input.rate, this->output.rate);

  if( !this->slow_fast_audio || this->current_speed == XINE_SPEED_PAUSE )
    this->frame_rate_factor = ((double)(this->output.rate)) / ((double)(this->input.rate));
  else
    this->frame_rate_factor = ( XINE_FINE_SPEED_NORMAL / (double)this->current_speed ) * ((double)(this->output.rate)) / ((double)(this->input.rate));
  this->frames_per_kpts   = (this->output.rate * 1024) / 90000;
  this->audio_step        = ((int64_t)90000 * (int64_t)32768) / (int64_t)this->input.rate;

  lprintf ("audio_step %" PRId64 " pts per 32768 frames\n", this->audio_step);
  return this->output.rate;
}

static int ao_change_settings(aos_t *this, uint32_t bits, uint32_t rate, int mode) {
  int output_sample_rate;

  if(this->driver_open && !this->grab_only)
    this->driver->close(this->driver);
  this->driver_open = 0;

  this->input.mode            = mode;
  this->input.rate            = rate;
  this->input.bits            = bits;

  if (!this->grab_only) {
    /* not all drivers/cards support 8 bits */
    if( this->input.bits == 8 &&
	!(this->driver->get_capabilities(this->driver) & AO_CAP_8BITS) ) {
      bits = 16;
      xprintf (this->xine, XINE_VERBOSITY_LOG,
               _("8 bits not supported by driver, converting to 16 bits.\n"));
    }

    /* provide mono->stereo and stereo->mono conversions */
    if( this->input.mode == AO_CAP_MODE_MONO &&
	!(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_MONO) ) {
      mode = AO_CAP_MODE_STEREO;
      xprintf (this->xine, XINE_VERBOSITY_LOG,
               _("mono not supported by driver, converting to stereo.\n"));
    }
    if( this->input.mode == AO_CAP_MODE_STEREO &&
	!(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_STEREO) ) {
      mode = AO_CAP_MODE_MONO;
      xprintf (this->xine, XINE_VERBOSITY_LOG,
               _("stereo not supported by driver, converting to mono.\n"));
    }

    output_sample_rate=(this->driver->open) (this->driver,bits,(this->force_rate ? this->force_rate : rate),mode);
  } else
    output_sample_rate = this->input.rate;

  if ( output_sample_rate == 0) {
    this->driver_open = 0;
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_out: open failed!\n");
    return 0;
  } else {
    this->driver_open = 1;
  }

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "output sample rate %d\n", output_sample_rate);

  this->last_audio_vpts       = 0;
  this->output.mode           = mode;
  this->output.rate           = output_sample_rate;
  this->output.bits           = bits;

  return ao_update_resample_factor(this);
}


static inline void inc_num_driver_actions(aos_t *this) {

  pthread_mutex_lock(&this->driver_action_lock);
  this->num_driver_actions++;
  pthread_mutex_unlock(&this->driver_action_lock);
}


static inline void dec_num_driver_actions(aos_t *this) {

  pthread_mutex_lock(&this->driver_action_lock);
  this->num_driver_actions--;
  /* indicate the change to ao_loop() */
  pthread_cond_broadcast(&this->driver_action_cond);
  pthread_mutex_unlock(&this->driver_action_lock);
}


/*
 * open the audio device for writing to
 */

static int ao_open(xine_audio_port_t *this_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  aos_t *this = (aos_t *) this_gen;
  int channels;

  if( !this->driver_open || bits != this->input.bits || rate != this->input.rate || mode != this->input.mode ) {
    int ret;

    if (this->audio_loop_running) {
      /* make sure there are no more buffers on queue */
      fifo_wait_empty(this->out_fifo);
    }

    if( !stream->emergency_brake ) {
      pthread_mutex_lock( &this->driver_lock );
      ret = ao_change_settings(this, bits, rate, mode);
      pthread_mutex_unlock( &this->driver_lock );

      if( !ret ) {
        stream->emergency_brake = 1;
        _x_message (stream, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
        return 0;
      }
    } else {
      return 0;
    }
  }

  /*
   * set metainfo
   */
  if (stream) {
    channels = _x_ao_mode2channels( mode );
    if( channels == 0 )
      channels = 255; /* unknown */

    _x_stream_info_set(stream, XINE_STREAM_INFO_AUDIO_MODE, mode);
    _x_stream_info_set(stream, XINE_STREAM_INFO_AUDIO_CHANNELS, channels);
    _x_stream_info_set(stream, XINE_STREAM_INFO_AUDIO_BITS, bits);
    _x_stream_info_set(stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, rate);

    stream->metronom->set_audio_rate(stream->metronom, this->audio_step);
  }

  pthread_mutex_lock(&this->streams_lock);
  xine_list_push_back(this->streams, stream);
  pthread_mutex_unlock(&this->streams_lock);

  return this->output.rate;
}

static audio_buffer_t *ao_get_buffer (xine_audio_port_t *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  while (!(buf = fifo_remove_nonblock (this->free_fifo)))
    if (this->xine->port_ticket->ticket_revoked)
      this->xine->port_ticket->renew(this->xine->port_ticket, 1);

  _x_extra_info_reset( buf->extra_info );
  buf->stream = NULL;

  return buf;
}

static void ao_put_buffer (xine_audio_port_t *this_gen,
                           audio_buffer_t *buf, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  int64_t pts;

  if (buf->num_frames == 0) {
    fifo_append (this->free_fifo, buf);
    return;
  }

  /* handle anonymous streams like NULL for easy checking */
  if (stream == XINE_ANON_STREAM) stream = NULL;

  buf->stream = stream;

  pts = buf->vpts;

  if (stream) {
    buf->format.bits = _x_stream_info_get(stream, XINE_STREAM_INFO_AUDIO_BITS);
    buf->format.rate = _x_stream_info_get(stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE);
    buf->format.mode = _x_stream_info_get(stream, XINE_STREAM_INFO_AUDIO_MODE);
    _x_extra_info_merge( buf->extra_info, stream->audio_decoder_extra_info );
    buf->vpts = stream->metronom->got_audio_samples(stream->metronom, pts, buf->num_frames);
  }

  buf->extra_info->vpts = buf->vpts;

  lprintf ("ao_put_buffer, pts=%" PRId64 ", vpts=%" PRId64 ", flushmode=%d\n",
           pts, buf->vpts, this->discard_buffers);

  if (!this->discard_buffers) {
    if (buf->stream)
      _x_refcounter_inc(buf->stream->refcounter);
    fifo_append (this->out_fifo, buf);
  } else
    fifo_append (this->free_fifo, buf);

  this->last_audio_vpts = buf->vpts;

  lprintf ("ao_put_buffer done\n");
}

static void ao_close(xine_audio_port_t *this_gen, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  xine_list_iterator_t ite;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "ao_close\n");

  /* unregister stream */
  pthread_mutex_lock(&this->streams_lock);
  for (ite = xine_list_front(this->streams); ite;
       ite = xine_list_next(this->streams, ite)) {
    xine_stream_t *cur = xine_list_get_value(this->streams, ite);
    if (cur == stream) {
      xine_list_remove(this->streams, ite);
      break;
    }
  }
  ite = xine_list_front(this->streams);
  pthread_mutex_unlock(&this->streams_lock);

  /* close driver if no streams left */
  if (!ite && !this->grab_only && !stream->keep_ao_driver_open) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_out: no streams left, closing driver\n");

    if (this->audio_loop_running) {
      /* make sure there are no more buffers on queue */
      if (this->current_speed == XINE_SPEED_PAUSE ||
          (this->current_speed != XINE_FINE_SPEED_NORMAL && !this->slow_fast_audio)) {
        int discard = ao_get_property(this_gen, AO_PROP_DISCARD_BUFFERS);
        /* discard buffers while waiting, otherwise we'll wait forever */
        ao_set_property(this_gen, AO_PROP_DISCARD_BUFFERS, 1);
        fifo_wait_empty(this->out_fifo);
        ao_set_property(this_gen, AO_PROP_DISCARD_BUFFERS, discard);
      }
      else
        fifo_wait_empty(this->out_fifo);
    }

    pthread_mutex_lock( &this->driver_lock );
    if(this->driver_open)
      this->driver->close(this->driver);
    this->driver_open = 0;
    pthread_mutex_unlock( &this->driver_lock );
  }
}

static void ao_exit(xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  int vol;
  int prop = 0;

  audio_buffer_t *buf, *next;

  if (this->audio_loop_running) {
    void *p;

    this->audio_loop_running = 0;

    buf = fifo_remove(this->free_fifo);
    buf->num_frames = 0;
    buf->stream = NULL;
    fifo_append (this->out_fifo, buf);

    pthread_join (this->audio_thread, &p);
    this->audio_thread_created = 0;
  }

  if (!this->grab_only) {
    pthread_mutex_lock( &this->driver_lock );

    if((this->driver->get_capabilities(this->driver)) & AO_CAP_MIXER_VOL)
      prop = AO_PROP_MIXER_VOL;
    else if((this->driver->get_capabilities(this->driver)) & AO_CAP_PCM_VOL)
      prop = AO_PROP_PCM_VOL;

    vol = this->driver->get_property(this->driver, prop);
    this->xine->config->update_num(this->xine->config, "audio.volume.mixer_volume", vol);
    if(this->driver_open)
      this->driver->close(this->driver);
    this->driver->exit(this->driver);
    pthread_mutex_unlock( &this->driver_lock );
  }

  pthread_mutex_destroy(&this->driver_lock);
  pthread_cond_destroy(&this->driver_action_cond);
  pthread_mutex_destroy(&this->driver_action_lock);
  pthread_mutex_destroy(&this->streams_lock);
  xine_list_delete(this->streams);

  free (this->frame_buf[0]->mem);
  free (this->frame_buf[0]->extra_info);
  free (this->frame_buf[0]);
  free (this->frame_buf[1]->mem);
  free (this->frame_buf[1]->extra_info);
  free (this->frame_buf[1]);
  free (this->zero_space);

  pthread_mutex_destroy(&this->current_speed_lock);
  pthread_mutex_destroy(&this->flush_audio_driver_lock);
  pthread_cond_destroy(&this->flush_audio_driver_reached);

  buf = this->free_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf->extra_info);
    free (buf);

    buf = next;
  }

  buf = this->out_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf->extra_info);
    free (buf);

    buf = next;
  }

  pthread_mutex_destroy(&this->free_fifo->mutex);
  pthread_cond_destroy(&this->free_fifo->empty);
  pthread_cond_destroy(&this->free_fifo->not_empty);

  pthread_mutex_destroy(&this->out_fifo->mutex);
  pthread_cond_destroy(&this->out_fifo->empty);
  pthread_cond_destroy(&this->out_fifo->not_empty);

  free (this->free_fifo);
  free (this->out_fifo);
  free (this);
}

static uint32_t ao_get_capabilities (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  uint32_t result;

  if (this->grab_only) {

    return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO ;
    /* FIXME: make configurable
      | AO_CAP_MODE_4CHANNEL | AO_CAP_MODE_5CHANNEL
      | AO_CAP_MODE_5_1CHANNEL | AO_CAP_8BITS;
    */
  } else {
    inc_num_driver_actions(this);
    pthread_mutex_lock( &this->driver_lock );
    dec_num_driver_actions(this);
    result=this->driver->get_capabilities(this->driver);
    pthread_mutex_unlock( &this->driver_lock );
  }
  return result;
}

static int ao_get_property (xine_audio_port_t *this_gen, int property) {
  aos_t *this = (aos_t *) this_gen;
  int ret;

  switch (property) {
  case AO_PROP_COMPRESSOR:
    ret = this->compression_factor_max*100;
    break;

  case AO_PROP_BUFS_IN_FIFO:
    ret = this->audio_loop_running ? this->out_fifo->num_buffers : -1;
    break;

  case AO_PROP_BUFS_FREE:
    ret = this->audio_loop_running ? this->free_fifo->num_buffers : -1;
    break;

  case AO_PROP_BUFS_TOTAL:
    ret = this->audio_loop_running ? this->free_fifo->num_buffers_max : -1;
    break;

  case AO_PROP_NUM_STREAMS:
    pthread_mutex_lock(&this->streams_lock);
    ret = xine_list_size(this->streams);
    pthread_mutex_unlock(&this->streams_lock);
    break;

  case AO_PROP_AMP:
    ret = this->amp_factor*100;
    break;

  case AO_PROP_AMP_MUTE:
    ret = this->amp_mute;
    break;

  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ:
    ret = (100.0 * this->eq_gain[property - AO_PROP_EQ_30HZ]) / (1 << FP_FRBITS) ;
    break;

  case AO_PROP_DISCARD_BUFFERS:
    ret = this->discard_buffers;
    break;

  case AO_PROP_CLOCK_SPEED:
    ret = this->current_speed;
    break;

  case AO_PROP_DRIVER_DELAY:
    ret = this->last_gap;
    break;

  default:
    inc_num_driver_actions(this);
    pthread_mutex_lock( &this->driver_lock );
    dec_num_driver_actions(this);
    ret = this->driver->get_property(this->driver, property);
    pthread_mutex_unlock( &this->driver_lock );
  }
  return ret;
}

static int ao_set_property (xine_audio_port_t *this_gen, int property, int value) {
  aos_t *this = (aos_t *) this_gen;
  int ret = 0;

  switch (property) {
  case AO_PROP_COMPRESSOR:

    this->compression_factor_max = (double) value / 100.0;

    this->do_compress = (this->compression_factor_max >1.0);

    ret = this->compression_factor_max*100;
    break;

  case AO_PROP_AMP:

    this->amp_factor = (double) value / 100.0;

    this->do_amp = (this->amp_factor != 1.0 || this->amp_mute);

    ret = this->amp_factor*100;
    break;

  case AO_PROP_AMP_MUTE:
    ret = this->amp_mute = value;

    this->do_amp = (this->amp_factor != 1.0 || this->amp_mute);
    break;

  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ:
    {

      int min_gain, max_gain, i;

      this->eq_gain[property - AO_PROP_EQ_30HZ] = EQ_REAL(((float)value / 100.0)) ;

      /* calc pregain, find out if any gain != 0.0 - enable eq if that is the case */
      min_gain = EQ_REAL(0.0);
      max_gain = EQ_REAL(0.0);
      for (i=0; i<EQ_BANDS; i++) {
	if (this->eq_gain[i] < min_gain)
	  min_gain = this->eq_gain[i];
	if (this->eq_gain[i] > max_gain)
	  max_gain = this->eq_gain[i];
      }

      lprintf ("eq min_gain=%d, max_gain=%d\n", min_gain, max_gain);

      this->do_equ = ((min_gain != EQ_REAL(0.0)) || (max_gain != EQ_REAL(0.0)));

      ret = value;
    }
    break;

  case AO_PROP_DISCARD_BUFFERS:
    /* recursive discard buffers setting */
    pthread_mutex_lock(&this->flush_audio_driver_lock);
    if(value)
      this->discard_buffers++;
    else if (this->discard_buffers)
      this->discard_buffers--;
    else
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "ao_set_property: discard_buffers is already zero\n");
    pthread_mutex_unlock(&this->flush_audio_driver_lock);

    ret = this->discard_buffers;

    /* discard buffers here because we have no output thread */
    if (this->grab_only && this->discard_buffers) {
      audio_buffer_t *buf;

      pthread_mutex_lock(&this->out_fifo->mutex);

      while ((buf = this->out_fifo->first)) {
        lprintf ("flushing out frame\n");
        buf = fifo_remove_int (this->out_fifo, 1);
        fifo_append (this->free_fifo, buf);
      }
      pthread_mutex_unlock (&this->out_fifo->mutex);
    }
    break;

  case AO_PROP_CLOSE_DEVICE:
    inc_num_driver_actions(this);
    pthread_mutex_lock( &this->driver_lock );
    dec_num_driver_actions(this);
    if(this->driver_open)
      this->driver->close(this->driver);
    this->driver_open = 0;
    pthread_mutex_unlock( &this->driver_lock );
    break;

  case AO_PROP_CLOCK_SPEED:
    /* something to do? */
    if (value == this->current_speed)
      break;
    /* TJ. pthread mutex implementation on my multicore AMD box is somewhat buggy.
       When fed by a fast single threaded decoder like mad, audio out loop does
       not release current speed lock long enough to wake us up here.
       So tell loop to enter unpause waiting _before_ we wait. */
    this->current_speed = value;
    /*
     * slow motion / fast forward does not play sound, drop buffered
     * samples from the sound driver (check slow_fast_audio flag)
     */
    if (value != XINE_FINE_SPEED_NORMAL && value != XINE_SPEED_PAUSE && !this->slow_fast_audio )
      this->ao.control(&this->ao, AO_CTRL_FLUSH_BUFFERS, NULL);

    if( value == XINE_SPEED_PAUSE ) {
      /* current_speed_lock is here to make sure the ao_loop will pause in a safe place.
       * that is, we cannot pause writing to device, filling gaps etc. */
      pthread_mutex_lock(&this->current_speed_lock);
      this->ao.control(&this->ao, AO_CTRL_PLAY_PAUSE, NULL);
      pthread_mutex_unlock(&this->current_speed_lock);
    } else {
      this->ao.control(&this->ao, AO_CTRL_PLAY_RESUME, NULL);
    }
    if( this->slow_fast_audio )
      ao_update_resample_factor(this);
    break;

  default:
    if (!this->grab_only) {
      /* Let the sound driver lock it's own mixer */
      ret =  this->driver->set_property(this->driver, property, value);
    }
  }

  return ret;
}

static int ao_control (xine_audio_port_t *this_gen, int cmd, ...) {

  aos_t *this = (aos_t *) this_gen;
  va_list args;
  void *arg;
  int rval = 0;

  if (this->grab_only)
    return 0;

  inc_num_driver_actions(this);
  pthread_mutex_lock( &this->driver_lock );
  dec_num_driver_actions(this);
  if(this->driver_open) {
    va_start(args, cmd);
    arg = va_arg(args, void*);
    rval = this->driver->control(this->driver, cmd, arg);
    va_end(args);
  }
  pthread_mutex_unlock( &this->driver_lock );

  return rval;
}

static void ao_flush (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
           "ao_flush (loop running: %d)\n", this->audio_loop_running);

  if( this->audio_loop_running ) {
    pthread_mutex_lock(&this->flush_audio_driver_lock);
    this->discard_buffers++;
    this->flush_audio_driver++;

    /* do not try this in paused mode */
    while( this->flush_audio_driver && this->current_speed != XINE_SPEED_PAUSE) {
      struct timeval  tv;
      struct timespec ts;

      /* release mutex to get a buffer, otherwise a deadlock may happen */
      pthread_mutex_unlock(&this->flush_audio_driver_lock);
      buf = fifo_remove (this->free_fifo);
      pthread_mutex_lock(&this->flush_audio_driver_lock);

      buf->num_frames = 0;
      buf->stream = NULL;
      fifo_append (this->out_fifo, buf);

      /* cond_timedwait was not supposed be needed here, but somehow it may still
       * get stuck when using normal cond_wait. probably the signal is missed when
       * we release the mutex above.
       */
      if (this->flush_audio_driver) {
        gettimeofday(&tv, NULL);
        ts.tv_sec  = tv.tv_sec + 1;
        ts.tv_nsec = tv.tv_usec * 1000;
        pthread_cond_timedwait(&this->flush_audio_driver_reached, &this->flush_audio_driver_lock, &ts);
      }
    }
    this->discard_buffers--;

    pthread_mutex_unlock(&this->flush_audio_driver_lock);
    fifo_wait_empty(this->out_fifo);
  }
}

static int ao_status (xine_audio_port_t *this_gen, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode) {
  aos_t *this = (aos_t *) this_gen;
  xine_stream_t *cur;
  int ret = 0;
  xine_list_iterator_t ite;

  pthread_mutex_lock(&this->streams_lock);
  for (ite = xine_list_front(this->streams); ite;
       ite = xine_list_next(this->streams, ite)) {
    cur = xine_list_get_value(this->streams, ite);
    if (cur == stream || !stream) {
      *bits = this->input.bits;
      *rate = this->input.rate;
      *mode = this->input.mode;
      ret = !!stream; /* return false for a NULL stream, true otherwise */
      break;
    }
  }
  pthread_mutex_unlock(&this->streams_lock);

  return ret;
}

static void ao_update_av_sync_method(void *this_gen, xine_cfg_entry_t *entry) {
  aos_t *this = (aos_t *) this_gen;

  lprintf ("av_sync_method = %d\n", entry->num_value);

  this->av_sync_method_conf = entry->num_value;

  switch (this->av_sync_method_conf) {
  case 0:
    this->resample_sync_method = 0;
    break;
  case 1:
    this->resample_sync_method = 1;
    break;
  default:
    this->resample_sync_method = 0;
    break;
  }
  this->resample_sync_info.valid = 0;
}

xine_audio_port_t *_x_ao_new_port (xine_t *xine, ao_driver_t *driver,
				int grab_only) {

  config_values_t *config = xine->config;
  aos_t           *this;
  int              i, err;
  pthread_attr_t   pth_attrs;
  pthread_mutexattr_t attr;
  static const char *const resample_modes[] = {"auto", "off", "on", NULL};
  static const char *const av_sync_methods[] = {"metronom feedback", "resample", NULL};

  this = calloc(1, sizeof(aos_t)) ;

  this->driver                = driver;
  this->xine                  = xine;
  this->clock                 = xine->clock;
  this->current_speed         = xine->clock->speed;
  this->streams               = xine_list_new();

  /* warning: driver_lock is a recursive mutex. it must NOT be
   * used with neither pthread_cond_wait() or pthread_cond_timedwait()
   */
  pthread_mutexattr_init( &attr );
  pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );

  pthread_mutex_init( &this->streams_lock, NULL );
  pthread_mutex_init( &this->driver_lock, &attr );
  pthread_mutex_init( &this->driver_action_lock, NULL );
  pthread_cond_init( &this->driver_action_cond, NULL );

  this->ao.open                   = ao_open;
  this->ao.get_buffer             = ao_get_buffer;
  this->ao.put_buffer             = ao_put_buffer;
  this->ao.close                  = ao_close;
  this->ao.exit                   = ao_exit;
  this->ao.get_capabilities       = ao_get_capabilities;
  this->ao.get_property           = ao_get_property;
  this->ao.set_property           = ao_set_property;
  this->ao.control                = ao_control;
  this->ao.flush                  = ao_flush;
  this->ao.status                 = ao_status;

  this->num_driver_actions     = 0;
  this->audio_loop_running     = 0;
  this->grab_only              = grab_only;
  this->flush_audio_driver     = 0;
  this->discard_buffers        = 0;
  this->zero_space             = calloc (1, ZERO_BUF_SIZE * 4 * 6); /* MAX as 32bit, 6 channels. */

  pthread_mutex_init( &this->current_speed_lock, NULL );
  pthread_mutex_init( &this->flush_audio_driver_lock, NULL );
  pthread_cond_init( &this->flush_audio_driver_reached, NULL );

  if (!grab_only)
    this->gap_tolerance          = driver->get_gap_tolerance (this->driver);

  this->av_sync_method_conf = config->register_enum(config, "audio.synchronization.av_sync_method", 0,
                                                    (char **)av_sync_methods,
                                                    _("method to sync audio and video"),
						    _("When playing audio and video, there are at least "
						      "two clocks involved: The system clock, to which "
						      "video frames are synchronized and the clock "
						      "in your sound hardware, which determines the "
						      "speed of the audio playback. These clocks are "
						      "never ticking at the same speed except for some "
						      "rare cases where they are physically identical. "
						      "In general, the two clocks will run drift after "
						      "some time, for which xine offers two ways to "
						      "keep audio and video synchronized:\n\n"
						      "metronom feedback\n"
						      "This is the standard method, which applies a "
						      "countereffecting video drift, as soon as the audio "
						      "drift has accumulated over a threshold.\n\n"
						      "resample\n"
						      "For some video hardware, which is limited to a "
						      "fixed frame rate (like the DXR3 or other decoder "
						      "cards) the above does not work, because the video "
						      "cannot drift. Therefore we resample the audio "
						      "stream to make it longer or shorter to compensate "
						      "the audio drift error. This does not work for "
						      "digital passthrough, where audio data is passed to "
						      "an external decoder in digital form."),
                                                    20, ao_update_av_sync_method, this);
  config->update_num(config,"audio.synchronization.av_sync_method",this->av_sync_method_conf);

  this->resample_conf = config->register_enum (config, "audio.synchronization.resample_mode", 0,
					       (char **)resample_modes,
					       _("enable resampling"),
					       _("When the sample rate of the decoded audio does not "
						 "match the capabilities of your sound hardware, an "
						 "adaptation called \"resampling\" is required. Here you "
						 "can select, whether resampling is enabled, disabled or "
						 "used automatically when necessary."),
					       20, NULL, NULL);
  this->force_rate    = config->register_num (config, "audio.synchronization.force_rate", 0,
					      _("always resample to this rate (0 to disable)"),
					      _("Some audio drivers do not correctly announce the "
						"capabilities of the audio hardware. By setting a "
						"value other than zero here, you can force the audio "
						"stream to be resampled to the given rate."),
					      20, NULL, NULL);

  this->passthrough_offset = config->register_num (config,
						   "audio.synchronization.passthrough_offset",
						   0,
						   _("offset for digital passthrough"),
						   _("If you use an external surround decoder and "
						     "audio is ahead or behind video, you can enter "
						     "a fixed offset here to compensate.\nThe unit of "
						     "the value is one PTS tick, which is the 90000th "
						     "part of a second."), 10, NULL, NULL);

  this->slow_fast_audio = config->register_bool (config,
						   "audio.synchronization.slow_fast_audio",
						   0,
						   _("play audio even on slow/fast speeds"),
						   _("If you enable this option, the audio will be "
						     "heard even when playback speed is different "
						     "than 1X. Of course, it will sound distorted "
						     "(lower/higher pitch). If want to experiment "
						     "preserving the pitch you may try the "
						     "'stretch' audio post plugin instead."), 10, NULL, NULL);

  this->compression_factor     = 2.0;
  this->compression_factor_max = 0.0;
  this->do_compress            = 0;
  this->amp_factor             = 1.0;
  this->do_amp                 = 0;
  this->amp_mute               = 0;

  this->do_equ                 = 0;
  this->eq_gain[0]             = 0;
  this->eq_gain[1]             = 0;
  this->eq_gain[2]             = 0;
  this->eq_gain[3]             = 0;
  this->eq_gain[4]             = 0;
  this->eq_gain[5]             = 0;
  this->eq_gain[6]             = 0;
  this->eq_gain[7]             = 0;
  this->eq_gain[8]             = 0;
  this->eq_gain[9]             = 0;
  this->eq_preamp              = EQ_REAL(1.0);
  this->eq_i                   = 0;
  this->eq_j                   = 2;
  this->eq_k                   = 1;

  memset (this->eq_data_history, 0, sizeof(sXYData) * EQ_BANDS * EQ_CHANNELS);

  /*
   * pre-allocate memory for samples
   */

  this->free_fifo        = fifo_new (this->xine);
  this->out_fifo         = fifo_new (this->xine);

  for (i=0; i<NUM_AUDIO_BUFFERS; i++) {

    audio_buffer_t *buf;

    buf = (audio_buffer_t *) calloc(1, sizeof(audio_buffer_t));
    buf->mem = calloc (1, AUDIO_BUF_SIZE);
    buf->mem_size = AUDIO_BUF_SIZE;
    buf->extra_info = malloc(sizeof(extra_info_t));

    fifo_append (this->free_fifo, buf);
  }

  memset (this->last_sample, 0, sizeof (this->last_sample));

  /* buffers used for audio conversions */
  for (i=0; i<2; i++) {

    audio_buffer_t *buf;

    buf = (audio_buffer_t *) calloc(1, sizeof(audio_buffer_t));
    buf->mem = calloc(4, AUDIO_BUF_SIZE);
    buf->mem_size = 4*AUDIO_BUF_SIZE;
    buf->extra_info = malloc(sizeof(extra_info_t));

    this->frame_buf[i] = buf;
  }

  /*
   * Set audio volume to latest used one ?
   */
  if(this->driver){
    int vol;

    vol = config->register_range (config, "audio.volume.mixer_volume",
				  50, 0, 100, _("startup audio volume"),
				  _("The overall audio volume set at xine startup."), 10, NULL, NULL);

    if(config->register_bool (config, "audio.volume.remember_volume", 0,
			      _("restore volume level at startup"),
			      _("If disabled, xine will not modify any mixer settings at startup."),
			      10, NULL, NULL)) {
      int prop = 0;

      if((ao_get_capabilities(&this->ao)) & AO_CAP_MIXER_VOL)
	prop = AO_PROP_MIXER_VOL;
      else if((ao_get_capabilities(&this->ao)) & AO_CAP_PCM_VOL)
	prop = AO_PROP_PCM_VOL;

      ao_set_property(&this->ao, prop, vol);
    }
  }

  if (!this->grab_only) {
    /*
     * start output thread
     */

    this->audio_loop_running = 1;

    pthread_attr_init(&pth_attrs);
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    this->audio_thread_created = 1;
    if ((err = pthread_create (&this->audio_thread,
			       &pth_attrs, ao_loop, this)) != 0) {

      xprintf (this->xine, XINE_VERBOSITY_NONE,
	       "audio_out: can't create thread (%s)\n", strerror(err));
      xprintf (this->xine, XINE_VERBOSITY_LOG,
	       _("audio_out: sorry, this should not happen. please restart xine.\n"));
      _x_abort();

    } else
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_out: thread created\n");

    pthread_attr_destroy(&pth_attrs);
  }

  return &this->ao;
}
