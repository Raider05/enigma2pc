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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Credits go
 * - for the SPDIF A/52 sync part
 * - frame size calculation added (16-08-2001)
 * (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * for initial ALSA 0.9.x support.
 *     adding MONO/STEREO/4CHANNEL/5CHANNEL/5.1CHANNEL analogue support.
 * (c) 2001 James Courtier-Dutton <James@superbug.demon.co.uk>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <asoundlib.h>

#include <sys/ioctl.h>
#include <inttypes.h>
#include <pthread.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/audio_out.h>

#include "speakers.h"

/*
#define ALSA_LOG
#define ALSA_LOG_BUFFERS
*/
/*
#define LOG_DEBUG
*/

#define AO_OUT_ALSA_IFACE_VERSION 9

#define BUFFER_TIME               1000*1000
#define GAP_TOLERANCE             5000

#define MIXER_MASK_LEFT           0x0001
#define MIXER_MASK_RIGHT          0x0002
#define MIXER_MASK_MUTE           0x0004
#define MIXER_MASK_STEREO         0x0008
#define MIXER_HAS_MUTE_SWITCH     0x0010

typedef struct {
  audio_driver_class_t driver_class;

  xine_t          *xine;
} alsa_class_t;

typedef struct alsa_driver_s {

  ao_driver_t        ao_driver;

  alsa_class_t      *class;

  snd_pcm_t         *audio_fd;
  int                capabilities;
  int                open_mode;
  int		     has_pause_resume;
  int		     is_paused;

  int32_t            output_sample_rate, input_sample_rate;
  double             sample_rate_factor;
  uint32_t           num_channels;
  uint32_t           bits_per_sample;
  uint32_t           bytes_per_frame;
  uint32_t           bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  snd_pcm_uframes_t  buffer_size;
  int32_t            mmap;

  struct {
    pthread_t          thread;
    pthread_mutex_t    mutex;
    char              *name;
    snd_mixer_t       *handle;
    snd_mixer_elem_t  *elem;
    long               min;
    long               max;
    long               left_vol;
    long               right_vol;
    int                mute;
    int                running;
  } mixer;
} alsa_driver_t;

static snd_output_t *jcd_out;

/*
 * Get and convert volume to percent value
 */
static int ao_alsa_get_percent_from_volume(long val, long min, long max) {
  int range = max - min;
  return (range == 0) ? 0 : ((val - min) * 100.0 / range + .5);
}

/* Stolen from alsa-lib */
static int my_snd_mixer_wait(snd_mixer_t *mixer, int timeout) {
  struct pollfd  spfds[16];
  struct pollfd *pfds = spfds;
  int            err, count;

  count = snd_mixer_poll_descriptors(mixer, pfds, sizeof(spfds) / sizeof(spfds[0]));

  if (count < 0)
    return count;

  if ((unsigned int) count > sizeof(spfds) / sizeof(spfds[0])) {
    pfds = calloc(count, sizeof(*pfds));

    if (!pfds)
      return -ENOMEM;

    err = snd_mixer_poll_descriptors(mixer, pfds, (unsigned int) count);
    assert(err == count);
  }

  err = poll(pfds, (unsigned int) count, timeout);

  if (err < 0)
    return -errno;

  return err;
}

/*
 * Wait (non blocking) till a mixer event happen
 */
static void *ao_alsa_handle_event_thread(void *data) {
  alsa_driver_t  *this = (alsa_driver_t *) data;

  do {

    if(my_snd_mixer_wait(this->mixer.handle, 333) > 0) {
      int err, mute = 0, swl = 0, swr = 0;
      long right_vol, left_vol;
      int old_mute;

      pthread_mutex_lock(&this->mixer.mutex);

      old_mute = (this->mixer.mute & MIXER_MASK_MUTE) ? 1 : 0;

      if((err = snd_mixer_handle_events(this->mixer.handle)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_handle_events(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	continue;
      }

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &left_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	continue;
      }

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &right_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	continue;
      }

      if(this->mixer.mute & MIXER_HAS_MUTE_SWITCH) {

	if(this->mixer.mute & MIXER_MASK_STEREO) {
	  snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);
	  mute = (swl) ? 0 : 1;
	}
	else {

	  if (this->mixer.mute & MIXER_MASK_LEFT)
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);

	  if ((SND_MIXER_SCHN_FRONT_RIGHT != SND_MIXER_SCHN_UNKNOWN) && (this->mixer.mute & MIXER_MASK_RIGHT))
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &swr);

	  mute = (swl || swr) ? 0 : 1;
	}
      }

      if((this->mixer.right_vol != right_vol) || (this->mixer.left_vol != left_vol) || (old_mute != mute)) {
	xine_event_t              event;
	xine_audio_level_data_t   data;
	xine_stream_t            *stream;
	xine_list_iterator_t      ite;

	this->mixer.right_vol = right_vol;
	this->mixer.left_vol  = left_vol;
	if(mute)
	  this->mixer.mute |= MIXER_MASK_MUTE;
	else
	  this->mixer.mute &= ~MIXER_MASK_MUTE;

	data.right = ao_alsa_get_percent_from_volume(this->mixer.right_vol,
						     this->mixer.min, this->mixer.max);
	data.left  = ao_alsa_get_percent_from_volume(this->mixer.left_vol,
						     this->mixer.min, this->mixer.max);
	data.mute  = (this->mixer.mute & MIXER_MASK_MUTE) ? 1 : 0;

	event.type        = XINE_EVENT_AUDIO_LEVEL;
	event.data        = &data;
	event.data_length = sizeof(data);

	pthread_mutex_lock(&this->class->xine->streams_lock);
	for(ite = xine_list_front(this->class->xine->streams);
	    ite; ite = xine_list_next(this->class->xine->streams, ite)) {
	  stream = xine_list_get_value(this->class->xine->streams, ite);
	  event.stream = stream;
	  xine_event_send(stream, &event);
	}
	pthread_mutex_unlock(&this->class->xine->streams_lock);
      }

      pthread_mutex_unlock(&this->mixer.mutex);
    }

  } while(this->mixer.running);

  pthread_exit(NULL);
}

/*
 * Convert percent value to volume and set
 */
static long ao_alsa_get_volume_from_percent(int val, long min, long max) {
  int range = max - min;
  return (range == 0) ? min : (val * range / 100.0 + min + .5);
}

/*
 * Error callback, we need to control this,
 * error message should be printed only in DEBUG mode.
 * XINE_FORMAT_PRINTF(5, 6) is true but useless here,
 * as alsa delivers "fmt" at runtime only.
 */
static void error_callback(const char *file, int line,
			   const char *function, int err, const char *fmt, ...) {
#ifdef DEBUG
  va_list   args;
  char     *buf = NULL;

  va_start(args, fmt);
  vasprintf(&buf, fmt, args);
  va_end(args);
  printf("%s: %s() %s.\n", file, function, buf);
  free(buf);
#endif
}

/*
 * open the audio device for writing to
 */
static int ao_alsa_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode) {
  alsa_driver_t        *this = (alsa_driver_t *) this_gen;
  config_values_t *config = this->class->xine->config;
  char                 *pcm_device;
  snd_pcm_stream_t      direction = SND_PCM_STREAM_PLAYBACK;
  snd_pcm_hw_params_t  *params;
  snd_pcm_sw_params_t  *swparams;
  snd_pcm_access_mask_t *mask;
  snd_pcm_uframes_t     period_size;
  snd_pcm_uframes_t     period_size_min;
  snd_pcm_uframes_t     period_size_max;
  snd_pcm_uframes_t     buffer_size_min;
  snd_pcm_uframes_t     buffer_size_max;
  snd_pcm_format_t      format;
#if 0
  uint32_t              periods;
#endif
  uint32_t              buffer_time=BUFFER_TIME;
  snd_pcm_uframes_t     buffer_time_to_size;
  int                   err, dir;
  int                 open_mode=1; /* NONBLOCK */
  /* int                   open_mode=0;  BLOCK */
  struct timeval start_time;
  struct timeval end_time;

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_sw_params_alloca(&swparams);
  err = snd_output_stdio_attach(&jcd_out, stdout, 0);

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    pcm_device = config->lookup_entry(config, "audio.device.alsa_default_device")->str_value;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    pcm_device = config->lookup_entry(config, "audio.device.alsa_front_device")->str_value;
    break;
  case AO_CAP_MODE_4CHANNEL:
    this->num_channels = 4;
    pcm_device = config->lookup_entry(config, "audio.device.alsa_surround40_device")->str_value;
    break;
  case AO_CAP_MODE_4_1CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    this->num_channels = 6;
    pcm_device = config->lookup_entry(config, "audio.device.alsa_surround51_device")->str_value;
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    this->num_channels = 2;
    pcm_device = config->lookup_entry(config, "audio.device.alsa_passthrough_device")->str_value;
    break;
  default:
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: ALSA Driver does not support the requested mode: 0x%X\n",mode);
    return 0;
  }

#ifdef ALSA_LOG
  printf("audio_alsa_out: Audio Device name = %s\n",pcm_device);
  printf("audio_alsa_out: Number of channels = %d\n",this->num_channels);
#endif

  if (this->audio_fd) {
    xine_log (this->class->xine, XINE_LOG_MSG, _("audio_alsa_out:Already open...WHY!"));
    snd_pcm_close (this->audio_fd);
    this->audio_fd = NULL;
  }

  this->open_mode              = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  /*
   * open audio device
   * When switching to surround, dmix blocks the device some time, so we just keep trying for 0.8sec.
   */
  gettimeofday(&start_time, NULL);
  do {
    err = snd_pcm_open(&this->audio_fd, pcm_device, direction, open_mode);
    gettimeofday(&end_time, NULL);
    if( err == -EBUSY ) {
      if( (double)end_time.tv_sec + 1E-6*end_time.tv_usec
          - (double)start_time.tv_sec - 1E-6*start_time.tv_usec > 0.8)
        break;
      else
        usleep(10000);
    }
  } while( err == -EBUSY );

  if(err <0 ) {
    xprintf (this->class->xine, XINE_VERBOSITY_LOG,
	     _("audio_alsa_out: snd_pcm_open() of %s failed: %s\n"), pcm_device, snd_strerror(err));
    xprintf (this->class->xine, XINE_VERBOSITY_LOG,
	     _("audio_alsa_out: >>> check if another program already uses PCM <<<\n"));
    return 0;
  }
  /* printf ("audio_alsa_out: snd_pcm_open() opened %s\n", pcm_device); */
  /* We wanted non blocking open but now put it back to normal */
  //snd_pcm_nonblock(this->audio_fd, 0);
  snd_pcm_nonblock(this->audio_fd, 1);
  /*
   * configure audio device
   */
  err = snd_pcm_hw_params_any(this->audio_fd, params);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_LOG,
	     _("audio_alsa_out: broken configuration for this PCM: no configurations available: %s\n"),
	     snd_strerror(err));
    goto close;
  }
  /* set interleaved access */
  if (this->mmap != 0) {
    mask = alloca(snd_pcm_access_mask_sizeof());
    snd_pcm_access_mask_none(mask);
    snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
    snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
    snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_COMPLEX);
    err = snd_pcm_hw_params_set_access_mask(this->audio_fd, params, mask);
    if (err < 0) {
      xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	       "audio_alsa_out: mmap not available, falling back to compatiblity mode\n");
      this->mmap=0;
      err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
    }
  } else {
    err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  }

  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: access type not available: %s\n", snd_strerror(err));
    goto close;
  }
  /* set the sample format ([SU]{8,16,24,FLOAT}) */
  /* ALSA automatically appends _LE or _BE depending on the CPU */
  switch (bits>>3) {
  case 1:
    format = SND_PCM_FORMAT_U8;
    break;
  case 2:
    format = SND_PCM_FORMAT_S16;
    break;
  case 3:
#ifdef WORDS_BIGENDIAN
    format = SND_PCM_FORMAT_S24_3BE; /* 24 bit samples taking 3 bytes. */
#else
    format = SND_PCM_FORMAT_S24_3LE;
#endif
    break;
  case 4:
    format = SND_PCM_FORMAT_FLOAT;
    break;
  default:
    format = SND_PCM_FORMAT_S16;
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: pcm format bits=%d unknown. failed: %s\n", bits, snd_strerror(err));
    break;
  }
  err = snd_pcm_hw_params_set_format(this->audio_fd, params, format );
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: sample format non available: %s\n", snd_strerror(err));
    goto close;
  }
  /* set the number of channels */
  err = snd_pcm_hw_params_set_channels(this->audio_fd, params, this->num_channels);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Cannot set number of channels to %d (err=%d:%s)\n",
	     this->num_channels, err, snd_strerror(err));
    goto close;
  }
#if 0
  /* Restrict a configuration space to contain only real hardware rates */
  err = snd_pcm_hw_params_set_rate_resample(this->audio_fd, params, 0);
#endif
  /* set the stream rate [Hz] */
  dir=0;
  err = snd_pcm_hw_params_set_rate_near(this->audio_fd, params, &rate, &dir);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: rate not available: %s\n", snd_strerror(err));
    goto close;
  }
  this->output_sample_rate = (uint32_t)rate;
  if (this->input_sample_rate != this->output_sample_rate) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: audio rate : %d requested, %d provided by device/sec\n",
	     this->input_sample_rate, this->output_sample_rate);
  }
  buffer_time_to_size = ( (uint64_t)buffer_time * rate) / 1000000;
  err = snd_pcm_hw_params_get_buffer_size_min(params, &buffer_size_min);
  err = snd_pcm_hw_params_get_buffer_size_max(params, &buffer_size_max);
  dir=0;
  err = snd_pcm_hw_params_get_period_size_min(params, &period_size_min,&dir);
  dir=0;
  err = snd_pcm_hw_params_get_period_size_max(params, &period_size_max,&dir);
#ifdef ALSA_LOG_BUFFERS
  printf("Buffer size range from %lu to %lu\n",buffer_size_min, buffer_size_max);
  printf("Period size range from %lu to %lu\n",period_size_min, period_size_max);
  printf("Buffer time size %lu\n",buffer_time_to_size);
#endif
  this->buffer_size = buffer_time_to_size;
  if (buffer_size_max < this->buffer_size) this->buffer_size = buffer_size_max;
  if (buffer_size_min > this->buffer_size) this->buffer_size = buffer_size_min;
  period_size=this->buffer_size/8;
  this->buffer_size = period_size*8;
#ifdef ALSA_LOG_BUFFERS
  printf("To choose buffer_size = %ld\n",this->buffer_size);
  printf("To choose period_size = %ld\n",period_size);
#endif

#if 0
  /* Set period to buffer size ratios at 8 periods to 1 buffer */
  dir=-1;
  periods=8;
  err = snd_pcm_hw_params_set_periods_near(this->audio_fd, params, &periods ,&dir);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: unable to set any periods: %s\n", snd_strerror(err));
    goto close;
  }
  /* set the ring-buffer time [us] (large enough for x us|y samples ...) */
  dir=0;
  err = snd_pcm_hw_params_set_buffer_time_near(this->audio_fd, params, &buffer_time, &dir);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: buffer time not available: %s\n", snd_strerror(err));
    goto close;
  }
#endif
#if 1
  /* set the period time [us] (interrupt every x us|y samples ...) */
  dir=0;
  err = snd_pcm_hw_params_set_period_size_near(this->audio_fd, params, &period_size, &dir);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: period time not available: %s\n", snd_strerror(err));
    goto close;
  }
#endif
  dir=0;
  err = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);

  dir=0;
  err = snd_pcm_hw_params_set_buffer_size_near(this->audio_fd, params, &this->buffer_size);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: buffer time not available: %s\n", snd_strerror(err));
    goto close;
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &(this->buffer_size));
#ifdef ALSA_LOG_BUFFERS
  printf("was set period_size = %ld\n",period_size);
  printf("was set buffer_size = %ld\n",this->buffer_size);
#endif
  if (2*period_size > this->buffer_size) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: buffer to small, could not use\n");
    goto close;
  }

  /* write the parameters to device */
  err = snd_pcm_hw_params(this->audio_fd, params);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: pcm hw_params failed: %s\n", snd_strerror(err));
    goto close;
  }
  /* Check for pause/resume support */
  this->has_pause_resume = ( snd_pcm_hw_params_can_pause (params)
			    && snd_pcm_hw_params_can_resume (params) );
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	  "audio_alsa_out:open pause_resume=%d\n", this->has_pause_resume);
  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->bytes_per_frame = snd_pcm_frames_to_bytes (this->audio_fd, 1);
  /*
   * audio buffer size handling
   */
  /* Copy current parameters into swparams */
  err = snd_pcm_sw_params_current(this->audio_fd, swparams);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to determine current swparams: %s\n", snd_strerror(err));
    goto close;
  }

#if defined(SND_LIB_VERSION) && SND_LIB_VERSION >= 0x010016
  /* snd_pcm_sw_params_set_xfer_align() is deprecated, alignment is always 1 */
#else
  /* align all transfers to 1 sample */
  err = snd_pcm_sw_params_set_xfer_align(this->audio_fd, swparams, 1);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to set transfer alignment: %s\n", snd_strerror(err));
    goto close;
  }
#endif

  /* allow the transfer when at least period_size samples can be processed */
  err = snd_pcm_sw_params_set_avail_min(this->audio_fd, swparams, period_size);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to set available min: %s\n", snd_strerror(err));
    goto close;
  }
  /* start the transfer when the buffer contains at least period_size samples */
  err = snd_pcm_sw_params_set_start_threshold(this->audio_fd, swparams, period_size);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to set start threshold: %s\n", snd_strerror(err));
    goto close;
  }

  /* never stop the transfer, even on xruns */
  err = snd_pcm_sw_params_set_stop_threshold(this->audio_fd, swparams, this->buffer_size);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to set stop threshold: %s\n", snd_strerror(err));
    goto close;
  }

  /* Install swparams into current parameters */
  err = snd_pcm_sw_params(this->audio_fd, swparams);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: Unable to set swparams: %s\n", snd_strerror(err));
    goto close;
  }
#ifdef ALSA_LOG
  snd_pcm_dump_setup(this->audio_fd, jcd_out);
  snd_pcm_sw_params_dump(swparams, jcd_out);
#endif

  return this->output_sample_rate;

close:
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  return 0;
}

/*
 * Return the number of audio channels
 */
static int ao_alsa_num_channels(ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->num_channels;
}

/*
 * Return the number of bytes per frame
 */
static int ao_alsa_bytes_per_frame(ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->bytes_per_frame;
}

/*
 * Return gap tolerance (in pts)
 */
static int ao_alsa_get_gap_tolerance (ao_driver_t *this_gen) {
  return GAP_TOLERANCE;
}

/*
 * Return the delay. is frames measured by looking at pending samples
 */
/* FIXME: delay returns invalid data if status is not RUNNING.
 * e.g When there is an XRUN or we are in PREPARED mode.
 */
static int ao_alsa_delay (ao_driver_t *this_gen)  {
  snd_pcm_sframes_t delay = 0;
  int err = 0;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
#ifdef LOG_DEBUG
  struct timeval now;
  printf("audio_alsa_out:delay:ENTERED\n");
#endif
  err = snd_pcm_delay( this->audio_fd, &delay );

#ifdef LOG_DEBUG
  printf("audio_alsa_out:delay:delay all=%ld err=%d\n",delay, err);
  gettimeofday(&now, 0);
  printf("audio_alsa_out:delay: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:delay:FINISHED\n");
#endif

  /*
   * try to recover from errors and recalculate delay
   */
  if(err) {
#ifdef LOG_DEBUG
    printf("gap audio_alsa_out:delay: recovery\n");
#endif
    err = snd_pcm_recover( this->audio_fd, err, 1 );
    err = snd_pcm_delay( this->audio_fd, &delay );
  }

  /*
   * if we have a negative delay try to forward within the buffer
   */
  if(!err && (delay < 0)) {
#ifdef LOG_DEBUG
    printf("gap audio_alsa_out:delay: forwarding frames: %d\n", (int)-delay);
#endif
    err = snd_pcm_forward( this->audio_fd, -delay );
    if(err >= 0) {
      err = snd_pcm_delay( this->audio_fd, &delay );
    }
  }

  /*
   * on error or (still) negative delays ensure delay 
   * is not negative
   */
  if (err || (delay < 0))
    delay = 0;

  return delay;
}

#if 0
/*
 * Handle over/under-run
 */
static void xrun(alsa_driver_t *this)
{
  /* snd_pcm_status_t *status; */
  int res;

  /*
     snd_pcm_status_alloca(&status);
     if ((res = snd_pcm_status(this->audio_fd, status))<0) {
       printf ("audio_alsa_out: status error: %s\n", snd_strerror(res));
       return;
     }
     snd_pcm_status_dump(status, jcd_out);
  */
  if (snd_pcm_state(this->audio_fd) == SND_PCM_STATE_XRUN) {
    /*
      struct timeval now, diff, tstamp;
      gettimeofday(&now, 0);
      snd_pcm_status_get_trigger_tstamp(status, &tstamp);
      timersub(&now, &tstamp, &diff);
      printf ("audio_alsa_out: xrun!!! (at least %.3f ms long)\n", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
    */
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG, "audio_alsa_out: XRUN!!!\n");
    if ((res = snd_pcm_prepare(this->audio_fd))<0) {
      xprintf (this->class->xine, XINE_VERBOSITY_DEBUG, "audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
      return;
    }
    return;         /* ok, data should be accepted again */
  }
}
#endif

/*
 * resume from suspend
 */
static int resume(snd_pcm_t *pcm)
{
  int res;
  while ((res = snd_pcm_resume(pcm)) == -EAGAIN)
    sleep(1);
  if (! res)
    return 0;
  return snd_pcm_prepare(pcm);
}

/*
 * Write audio data to output buffer (blocking using snd_pcm_wait)
 */
static int ao_alsa_write(ao_driver_t *this_gen, int16_t *data, uint32_t count) {
  snd_pcm_sframes_t result;
  snd_pcm_status_t *pcm_stat;
  snd_pcm_state_t    state;
#ifdef LOG_DEBUG
  struct timeval now;
#endif
  int wait_result;
  int res;
  uint8_t *buffer=(uint8_t *)data;
  snd_pcm_uframes_t number_of_frames = (snd_pcm_uframes_t) count;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;

#ifdef LOG_DEBUG
  printf("audio_alsa_out:write:ENTERED\n");
  gettimeofday(&now, 0);
  printf("audio_alsa_out:write: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:write:count=%u\n",count);
#endif
  snd_pcm_status_alloca(&pcm_stat);
  state = snd_pcm_state(this->audio_fd);
  if (state == SND_PCM_STATE_SUSPENDED) {
    res = resume(this->audio_fd);
    if (res < 0)
      return 0;
    state = snd_pcm_state(this->audio_fd);
  } else if (state == SND_PCM_STATE_DISCONNECTED) {
    /* the device is gone. audio_out.c handles it if we return something < 0 */
    return -1;
  }
  if (state == SND_PCM_STATE_XRUN) {
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:XRUN before\n");
    snd_pcm_status(this->audio_fd, pcm_stat);
    snd_pcm_status_dump(pcm_stat, jcd_out);
#endif
    if ((res = snd_pcm_prepare(this->audio_fd))<0) {
      return 0;
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
      _x_abort();
    }
    state = snd_pcm_state(this->audio_fd);
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:XRUN after\n");
#endif
  }
  if ( (state != SND_PCM_STATE_PREPARED) &&
       (state != SND_PCM_STATE_RUNNING) &&
       (state != SND_PCM_STATE_DRAINING) ) {
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "audio_alsa_out:write:BAD STATE, state = %d\n",state);
  }

  while( number_of_frames > 0) {
    if ( (state == SND_PCM_STATE_RUNNING) ) {
#ifdef LOG_DEBUG
      printf("audio_alsa_out:write:loop:waiting for Godot\n");
#endif
      snd_pcm_status(this->audio_fd, pcm_stat);
      if ( snd_pcm_status_get_avail(pcm_stat) < number_of_frames) {
        wait_result = snd_pcm_wait(this->audio_fd, 1000);
#ifdef LOG_DEBUG
        printf("audio_alsa_out:write:loop:wait_result=%d\n",wait_result);
#endif
        if (wait_result <= 0) return 0;
      }
    }
    if (this->mmap != 0) {
      result = snd_pcm_mmap_writei(this->audio_fd, buffer, number_of_frames);
    } else {
      result = snd_pcm_writei(this->audio_fd, buffer, number_of_frames);
    }

    if (result < 0) {
#ifdef LOG_DEBUG
      printf("audio_alsa_out:write:result=%ld:%s\n",result, snd_strerror(result));
#endif
      state = snd_pcm_state(this->audio_fd);
      if (state == SND_PCM_STATE_SUSPENDED) {
	res = resume(this->audio_fd);
	if (res < 0)
	  return 0;
	continue;
      }
      if (state == SND_PCM_STATE_DISCONNECTED) {
        /* the device is gone. audio_out.c handles it if we return something < 0 */
        return -1;
      } else if ( (state != SND_PCM_STATE_PREPARED) &&
           (state != SND_PCM_STATE_RUNNING) &&
           (state != SND_PCM_STATE_DRAINING) ) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out:write:BAD STATE2, state = %d, going to try XRUN\n",state);
        if ((res = snd_pcm_prepare(this->audio_fd))<0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
	  _x_abort();
        }
      }
    }
    if (result > 0) {
      number_of_frames -= result;
      buffer += result * this->bytes_per_frame;
    }
  }
#if 0
  if ( (state == SND_PCM_STATE_RUNNING) ) {
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:loop:waiting for Godot2\n");
#endif
    wait_result = snd_pcm_wait(this->audio_fd, 1000000);
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:loop:wait_result=%d\n",wait_result);
#endif
    if (wait_result < 0) return 0;
  }
#endif
#ifdef LOG_DEBUG
  gettimeofday(&now, 0);
  printf("audio_alsa_out:write: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:write:FINISHED\n");
#endif
  return 1; /* audio samples were processed ok */
}

/*
 * This is called when the decoder no longer uses the audio
 */
static void ao_alsa_close(ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;

  if(this->audio_fd) {
    snd_pcm_nonblock(this->audio_fd, 0);
    snd_pcm_drain(this->audio_fd);
    snd_pcm_close(this->audio_fd);
  }
  this->audio_fd = NULL;
  this->has_pause_resume = 0; /* This is set at open time */
}

/*
 * Find out what output modes + capatilities are supported
 */
static uint32_t ao_alsa_get_capabilities (ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->capabilities;
}

/*
 * Shut down audio output driver plugin and free all resources allocated
 */
static void ao_alsa_exit(ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;

  /*
   * Destroy the mixer thread and cleanup the mixer, so that
   * any child processes (such as xscreensaver) cannot inherit
   * the mixer's handle and keep it open.
   * By rejoining the mixer thread, we remove a race condition
   * between closing the handle and spawning the child process
   * (i.e. xscreensaver).
   */

  if(this->mixer.handle && this->mixer.thread != 0) {
    this->mixer.running = 0;
    pthread_join(this->mixer.thread, NULL);
    snd_mixer_close(this->mixer.handle);
    this->mixer.handle=0;
  }
  pthread_mutex_destroy(&this->mixer.mutex);

  if (this->audio_fd) snd_pcm_close(this->audio_fd);
  this->audio_fd=NULL;
  free (this);
}

/*
 * Get a property of audio driver
 */
static int ao_alsa_get_property (ao_driver_t *this_gen, int property) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int err;

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    if(this->mixer.elem) {
      int vol;

      pthread_mutex_lock(&this->mixer.mutex);

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    &this->mixer.left_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	goto done;
      }

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    &this->mixer.right_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	goto done;
      }

    done:
      vol = (((ao_alsa_get_percent_from_volume(this->mixer.left_vol, this->mixer.min, this->mixer.max)) +
	      (ao_alsa_get_percent_from_volume(this->mixer.right_vol, this->mixer.min, this->mixer.max))) /2);
      pthread_mutex_unlock(&this->mixer.mutex);

      return vol;
    }
    break;

  case AO_PROP_MUTE_VOL:
    {
      int mute;

      pthread_mutex_lock(&this->mixer.mutex);
      mute = ((this->mixer.mute & MIXER_HAS_MUTE_SWITCH) && (this->mixer.mute & MIXER_MASK_MUTE)) ? 1 : 0;
      pthread_mutex_unlock(&this->mixer.mutex);

      return mute;
    }
    break;
  }

  return 0;
}

/*
 * Set a property of audio driver
 */
static int ao_alsa_set_property (ao_driver_t *this_gen, int property, int value) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int err;

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    if(this->mixer.elem) {

      pthread_mutex_lock(&this->mixer.mutex);

      this->mixer.left_vol = this->mixer.right_vol = ao_alsa_get_volume_from_percent(value, this->mixer.min, this->mixer.max);

      if((err = snd_mixer_selem_set_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    this->mixer.left_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	return ~value;
      }

      if((err = snd_mixer_selem_set_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    this->mixer.right_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	return ~value;
      }
      pthread_mutex_unlock(&this->mixer.mutex);
      return value;
    }
    break;

  case AO_PROP_MUTE_VOL:
    if(this->mixer.elem) {

      if(this->mixer.mute & MIXER_HAS_MUTE_SWITCH) {
	int swl = 0, swr = 0;
	int old_mute;

	pthread_mutex_lock(&this->mixer.mutex);

	old_mute = this->mixer.mute;
	if(value)
	  this->mixer.mute |= MIXER_MASK_MUTE;
	else
	  this->mixer.mute &= ~MIXER_MASK_MUTE;

	if ((this->mixer.mute & MIXER_MASK_MUTE) != (old_mute & MIXER_MASK_MUTE)) {
	  if(this->mixer.mute & MIXER_MASK_STEREO) {
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);
	    snd_mixer_selem_set_playback_switch_all(this->mixer.elem, !swl);
	  }
	  else {
	    if (this->mixer.mute & MIXER_MASK_LEFT) {
	      snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);
	      snd_mixer_selem_set_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, !swl);
	    }
	    if (SND_MIXER_SCHN_FRONT_RIGHT != SND_MIXER_SCHN_UNKNOWN && (this->mixer.mute & MIXER_MASK_RIGHT)) {
	      snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &swr);
	      snd_mixer_selem_set_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, !swr);
	    }
	  }
	}

	pthread_mutex_unlock(&this->mixer.mutex);
      }
      return value;
    }
    return ~value;
    break;
  }

  return ~value;
}

/*
 * Misc control operations
 */
static int ao_alsa_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int err;

  /* Alsa 0.9.x pause and resume is not stable enough at the moment.
   * Use snd_pcm_drop and restart instead.
   */
  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    if (this->audio_fd) {
      if (this->has_pause_resume) {
        if ((err=snd_pcm_pause(this->audio_fd, 1)) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: Pause call failed. (err=%d:%s)\n",err, snd_strerror(err));
          this->has_pause_resume = 0;
          ao_alsa_ctrl(this_gen, AO_CTRL_PLAY_PAUSE, NULL);
        } else {
          this->is_paused = 1;
	}
      } else {
        if ((err=snd_pcm_reset(this->audio_fd)) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: Reset call failed. (err=%d:%s)\n",err, snd_strerror(err));
        }
        if ((err=snd_pcm_drain(this->audio_fd)) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: Drain call failed. (err=%d:%s)\n",err, snd_strerror(err));
        }
        if ((err=snd_pcm_prepare(this->audio_fd)) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: Prepare call failed. (err=%d:%s)\n",err, snd_strerror(err));
        }
      }
    }
    break;

  case AO_CTRL_PLAY_RESUME:
    if (this->audio_fd) {
      if (this->has_pause_resume && this->is_paused) {
        if ((err=snd_pcm_pause(this->audio_fd, 0)) < 0) {
          if (err == -77) {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		    "audio_alsa_out: Warning: How am I supposed to RESUME, if I am not PAUSED. "
		    "audio_out.c, please don't call me!\n");
            break;
          }
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		  "audio_alsa_out: Resume call failed. (err=%d:%s)\n",err, snd_strerror(err));
          this->has_pause_resume = 0;
        } else {
          this->is_paused = 0;
	}
      }
    }
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    if (this->audio_fd) {
      if ((err=snd_pcm_drop(this->audio_fd)) < 0) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: Drop call failed. (err=%d:%s)\n",err, snd_strerror(err));
      }
      if ((err=snd_pcm_prepare(this->audio_fd)) < 0) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: Prepare call failed. (err=%d:%s)\n",err, snd_strerror(err));
      }
    }
    break;
  }

  return 0;
}

/*
 * Initialize mixer
 */
static void ao_alsa_mixer_init(ao_driver_t *this_gen) {
  alsa_driver_t        *this = (alsa_driver_t *) this_gen;
  config_values_t      *config = this->class->xine->config;
  char                 *pcm_device;
  snd_ctl_card_info_t  *hw_info;
  snd_ctl_t            *ctl_handle;
  int                   err;
  void                 *mixer_sid;
  snd_mixer_elem_t     *elem;
  int                   mixer_n_selems = 0;
  snd_mixer_selem_id_t *sid;
  int                   loop = 0;
  int                   found;
  int                   swl = 0, swr = 0, send_events;

  this->mixer.elem = 0;
  snd_ctl_card_info_alloca(&hw_info);
  pcm_device = config->lookup_entry(config, "audio.device.alsa_default_device")->str_value;
  if ((err = snd_ctl_open (&ctl_handle, pcm_device, 0)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG, "audio_alsa_out: snd_ctl_open(): %s\n", snd_strerror(err));
    return;
  }

  if ((err = snd_ctl_card_info (ctl_handle, hw_info)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: snd_ctl_card_info(): %s\n", snd_strerror(err));
    snd_ctl_close(ctl_handle);
    return;
  }

  snd_ctl_close (ctl_handle);

  /*
   * Open mixer device
   */
  if ((err = snd_mixer_open (&this->mixer.handle, 0)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: snd_mixer_open(): %s\n", snd_strerror(err));
    this->mixer.handle=0;
    return;
  }

  if ((err = snd_mixer_attach (this->mixer.handle, pcm_device)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: snd_mixer_attach(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    this->mixer.handle=0;
    return;
  }

  if ((err = snd_mixer_selem_register (this->mixer.handle, NULL, NULL)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: snd_mixer_selem_register(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    this->mixer.handle=0;
    return;
  }

  if ((err = snd_mixer_load (this->mixer.handle)) < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: snd_mixer_load(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    this->mixer.handle=0;
    return;
  }

  mixer_sid = alloca(snd_mixer_selem_id_sizeof() * snd_mixer_get_count(this->mixer.handle));
  if (mixer_sid == NULL) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: alloca() failed: %s\n", strerror(errno));
    snd_mixer_close(this->mixer.handle);
    this->mixer.handle=0;
    return;
  }

 again:

  found = 0;
  mixer_n_selems = 0;
  for (elem = snd_mixer_first_elem(this->mixer.handle); elem; elem = snd_mixer_elem_next(elem)) {
    sid = (snd_mixer_selem_id_t *)(((char *)mixer_sid) + snd_mixer_selem_id_sizeof() * mixer_n_selems);

    if ((snd_mixer_elem_get_type(elem) != SND_MIXER_ELEM_SIMPLE) ||
        !snd_mixer_selem_is_active(elem))
      continue;

    snd_mixer_selem_get_id(elem, sid);
    mixer_n_selems++;

    if(!strcmp((snd_mixer_selem_get_name(elem)), this->mixer.name)) {
      /* printf("found %s\n", snd_mixer_selem_get_name(elem)); */

      this->mixer.elem = elem;

      snd_mixer_selem_get_playback_volume_range(this->mixer.elem,
						&this->mixer.min, &this->mixer.max);
      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    &this->mixer.left_vol)) < 0) {
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	this->mixer.elem = NULL;
	continue;
      }

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    &this->mixer.right_vol)) < 0) {
	xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
		 "audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	this->mixer.elem = NULL;
	continue;
      }

      /* Channels mute */
      this->mixer.mute = 0;
      if(snd_mixer_selem_has_playback_switch(this->mixer.elem)) {
	this->mixer.mute |= MIXER_HAS_MUTE_SWITCH;

	if (snd_mixer_selem_has_playback_switch_joined(this->mixer.elem)) {
	  this->mixer.mute |= MIXER_MASK_STEREO;
	  snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);
	}
	else {
	  this->mixer.mute |= MIXER_MASK_LEFT;
	  snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &swl);

	  if (SND_MIXER_SCHN_FRONT_RIGHT != SND_MIXER_SCHN_UNKNOWN) {
	    this->mixer.mute |= MIXER_MASK_RIGHT;
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &swr);
	  }

	  if(!swl || !swr)
	    this->mixer.mute |= MIXER_MASK_MUTE;
	}

	this->capabilities |= AO_CAP_MUTE_VOL;
      }

      found++;

      goto mixer_found;
    }
  }

  if(loop)
    goto mixer_found; /* Yes, untrue but... ;-) */

  if(!strcmp(this->mixer.name, "PCM")) {
    config->update_string(config, "audio.device.alsa_mixer_name", "Master");
    loop++;
  }
  else {
    config->update_string(config, "audio.device.alsa_mixer_name", "PCM");
  }

  this->mixer.name = config->lookup_entry(config, "audio.device.alsa_mixer_name")->str_value;

  goto again;

 mixer_found:

  /*
   * Ugly: yes[*]  no[ ]
   */
  if(found) {
    if(!strcmp(this->mixer.name, "Master"))
      this->capabilities |= AO_CAP_MIXER_VOL;
    else
      this->capabilities |= AO_CAP_PCM_VOL;
  } else {
    if (this->mixer.handle) {
      snd_mixer_close(this->mixer.handle);
      this->mixer.handle=0;
    }
    return;
  }

  /* Create a thread which wait/handle mixer events */
  send_events = config->register_bool(config, "audio.alsa_hw_mixer", 1,
				      _("notify changes to the hardware mixer"),
				      _("When the hardware mixer changes, your application will receive "
				        "a notification so that it can update its graphical representation "
				        "of the mixer settings on the fly."),
				      10, NULL, NULL);

  if (send_events && found) {
    pthread_attr_t       pth_attrs;
    struct sched_param   pth_params;

    this->mixer.running = 1;

    pthread_attr_init(&pth_attrs);

    pthread_attr_getschedparam(&pth_attrs, &pth_params);
    pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
    pthread_attr_setschedparam(&pth_attrs, &pth_params);
    pthread_create(&this->mixer.thread, &pth_attrs, ao_alsa_handle_event_thread, (void *) this);
    pthread_attr_destroy(&pth_attrs);
  } else {
    this->mixer.thread = 0;
  }
}

static void alsa_speaker_arrangement_cb (void *user_data,
                                  xine_cfg_entry_t *entry);

/*
 * Initialize plugin
 */

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {

  alsa_class_t        *class = (alsa_class_t *) class_gen;
  config_values_t     *config = class->xine->config;
  alsa_driver_t       *this;
  int                  err;
  char                *pcm_device;
  snd_pcm_hw_params_t *params;

  AUDIO_DEVICE_SPEAKER_ARRANGEMENT_TYPES;
  int speakers;

  this = calloc(1, sizeof (alsa_driver_t));
  if (!this)
    return NULL;

  this->class = class;

  err = snd_lib_error_set_handler(error_callback);
  if(err < 0)
    xine_log(this->class->xine, XINE_LOG_MSG, _("snd_lib_error_set_handler() failed: %d"), err);

  snd_pcm_hw_params_alloca(&params);

  this->mmap = config->register_bool (config,
                               "audio.device.alsa_mmap_enable",
                               0,
                               _("sound card can do mmap"),
                               _("Enable this, if your sound card and alsa driver "
                                 "support memory mapped IO.\nYou can try enabling it "
                                 "and check, if everything works. If it does, this "
                                 "will increase performance."),
                               10, NULL,
                               NULL);
  pcm_device = config->register_string(config,
				       "audio.device.alsa_default_device",
				       "default",
				       _("device used for mono output"),
				       _("xine will use this alsa device to output "
				         "mono sound.\nSee the alsa documentation "
				         "for information on alsa devices."),
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.device.alsa_front_device",
				       "plug:front:default",
				       _("device used for stereo output"),
				       _("xine will use this alsa device to output "
				         "stereo sound.\nSee the alsa documentation "
				         "for information on alsa devices."),
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.device.alsa_surround40_device",
				       "plug:surround40:0",
				       _("device used for 4-channel output"),
				       _("xine will use this alsa device to output "
				         "4 channel (4.0) surround sound.\nSee the "
				         "alsa documentation for information on alsa "
				         "devices."),
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.device.alsa_surround51_device",
				       "plug:surround51:0",
				       _("device used for 5.1-channel output"),
				       _("xine will use this alsa device to output "
				         "5 channel plus LFE (5.1) surround sound.\n"
				         "See the alsa documentation for information "
				         "on alsa devices."),
                                       10,  NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.device.alsa_passthrough_device",
				       "iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
				       _("device used for 5.1-channel output"),
				       _("xine will use this alsa device to output "
				         "undecoded digital surround sound. This can "
				         "be used be external surround decoders.\nSee the "
				         "alsa documentation for information on alsa "
				         "devices."),
				       10, NULL,
				       NULL);

  /* Use the default device to open first */
  pcm_device = config->lookup_entry(config, "audio.device.alsa_default_device")->str_value;

  /*
   * find best device driver/channel
   */
  /*
   * open that device
   */
  err=snd_pcm_open(&this->audio_fd, pcm_device, SND_PCM_STREAM_PLAYBACK, 1); /* NON-BLOCK mode */
  if(err <0 ) {
    xine_log (this->class->xine, XINE_LOG_MSG,
          _("snd_pcm_open() failed:%d:%s\n"), err, snd_strerror(err));
    xine_log (this->class->xine, XINE_LOG_MSG,
          _(">>> Check if another program already uses PCM <<<\n"));
    free(this);
    return NULL;
  }

  /*
   * configure audio device
   */
  err = snd_pcm_hw_params_any(this->audio_fd, params);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: broken configuration for this PCM: no configurations available\n");
    snd_pcm_close(this->audio_fd);
    free(this);
    return NULL;
  }
  err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    xprintf (this->class->xine, XINE_VERBOSITY_DEBUG,
	     "audio_alsa_out: access type not available");
    snd_pcm_close(this->audio_fd);
    free(this);
    return NULL;
  }

  this->capabilities = 0;

  /* for usability reasons, keep this in sync with audio_oss_out.c */
  speakers = config->register_enum(config, "audio.output.speaker_arrangement", STEREO,
                                   (char **)speaker_arrangement,
                                   AUDIO_DEVICE_SPEAKER_ARRANGEMENT_HELP,
                                   0, alsa_speaker_arrangement_cb, this);

  char *logmsg = strdup (_("audio_alsa_out : supported modes are"));

  if (!(snd_pcm_hw_params_test_format(this->audio_fd, params, SND_PCM_FORMAT_U8))) {
    this->capabilities |= AO_CAP_8BITS;
    xine_strcat_realloc (&logmsg, _(" 8bit"));
  }
  /* ALSA automatically appends _LE or _BE depending on the CPU */
  if (!(snd_pcm_hw_params_test_format(this->audio_fd, params, SND_PCM_FORMAT_S16))) {
    this->capabilities |= AO_CAP_16BITS;
    xine_strcat_realloc (&logmsg, _(" 16bit"));
  }
  if (!(snd_pcm_hw_params_test_format(this->audio_fd, params, SND_PCM_FORMAT_S24))) {
    this->capabilities |= AO_CAP_24BITS;
    xine_strcat_realloc (&logmsg, _(" 24bit"));
  }
  if (!(snd_pcm_hw_params_test_format(this->audio_fd, params, SND_PCM_FORMAT_FLOAT))) {
    this->capabilities |= AO_CAP_FLOAT32;
    xine_strcat_realloc (&logmsg, _(" 32bit"));
  }
  if (0 == (this->capabilities & (AO_CAP_FLOAT32 | AO_CAP_24BITS | AO_CAP_16BITS | AO_CAP_8BITS))) {
    xprintf(class->xine, XINE_VERBOSITY_LOG, "%s\n", logmsg);
    free (logmsg);
    xprintf (class->xine, XINE_VERBOSITY_DEBUG,
             "audio_alsa_out: no supported PCM format found\n");
    snd_pcm_close(this->audio_fd);
    free(this);
    return NULL;
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 1))) {
    this->capabilities |= AO_CAP_MODE_MONO;
    xine_strcat_realloc (&logmsg, _(" mono"));
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 2))) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    xine_strcat_realloc (&logmsg, _(" stereo"));
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 4)) &&
     ( speakers == SURROUND4 )) {
    this->capabilities |= AO_CAP_MODE_4CHANNEL;
    xine_strcat_realloc (&logmsg, _(" 4-channel"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (4-channel not enabled in xine config)"));

  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 6)) &&
     ( speakers == SURROUND41 )) {
    this->capabilities |= AO_CAP_MODE_4_1CHANNEL;
    xine_strcat_realloc (&logmsg, _(" 4.1-channel"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (4.1-channel not enabled in xine config)"));

  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 6)) &&
     ( speakers == SURROUND5 )) {
    this->capabilities |= AO_CAP_MODE_5CHANNEL;
    xine_strcat_realloc (&logmsg, _(" 5-channel"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (5-channel not enabled in xine config)"));

  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 6)) &&
     ( speakers >= SURROUND51 )) {
    this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
    xine_strcat_realloc (&logmsg, _(" 5.1-channel"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (5.1-channel not enabled in xine config)"));

  this->has_pause_resume = 0; /* This is checked at open time instead */
  this->is_paused = 0;

  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;

  /* Fallback to "default" if device "front" does not exist */
  /* Needed for some very basic sound cards. */
  pcm_device = config->lookup_entry(config, "audio.device.alsa_front_device")->str_value;
  err=snd_pcm_open(&this->audio_fd, pcm_device, SND_PCM_STREAM_PLAYBACK, 1); /* NON-BLOCK mode */
  if(err < 0) {
    config->update_string(config, "audio.device.alsa_front_device", "default");
  } else {
    snd_pcm_close (this->audio_fd);
    this->audio_fd=NULL;
  }

  this->output_sample_rate = 0;
  if ( speakers == A52_PASSTHRU ) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
    xine_strcat_realloc (&logmsg, _(" a/52 and DTS pass-through"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (a/52 and DTS pass-through not enabled in xine config)"));

  xprintf(class->xine, XINE_VERBOSITY_LOG, "%s\n", logmsg);
  free (logmsg);

  /* printf("audio_alsa_out: capabilities 0x%X\n",this->capabilities); */

  this->mixer.name = config->register_string(config,
                                             "audio.device.alsa_mixer_name",
                                             "PCM",
                                             _("alsa mixer device"),
                                             _("xine will use this alsa mixer device to change "
                                               "the volume.\nSee the alsa documentation for "
                                               "information on alsa devices."),
                                             10, NULL,
                                             NULL);
  if (!this->mixer.name) {
    if (this->audio_fd)
      snd_pcm_close (this->audio_fd);
    free(this);
    return NULL;
  }

  pthread_mutex_init(&this->mixer.mutex, NULL);
  ao_alsa_mixer_init(&this->ao_driver);

  this->ao_driver.get_capabilities    = ao_alsa_get_capabilities;
  this->ao_driver.get_property        = ao_alsa_get_property;
  this->ao_driver.set_property        = ao_alsa_set_property;
  this->ao_driver.open                = ao_alsa_open;
  this->ao_driver.num_channels        = ao_alsa_num_channels;
  this->ao_driver.bytes_per_frame     = ao_alsa_bytes_per_frame;
  this->ao_driver.delay               = ao_alsa_delay;
  this->ao_driver.write		      = ao_alsa_write;
  this->ao_driver.close               = ao_alsa_close;
  this->ao_driver.exit                = ao_alsa_exit;
  this->ao_driver.get_gap_tolerance   = ao_alsa_get_gap_tolerance;
  this->ao_driver.control	      = ao_alsa_ctrl;

  return &this->ao_driver;
}

static void alsa_speaker_arrangement_cb (void *user_data,
                                  xine_cfg_entry_t *entry) {
  alsa_driver_t *this = (alsa_driver_t *) user_data;
  int32_t value = entry->num_value;
  if (value == A52_PASSTHRU) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
  } else {
    this->capabilities &= ~AO_CAP_MODE_A52;
    this->capabilities &= ~AO_CAP_MODE_AC5;
  }
  if (value == SURROUND4) {
    this->capabilities |= AO_CAP_MODE_4CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_4CHANNEL;
  }
  if (value == SURROUND41) {
    this->capabilities |= AO_CAP_MODE_4_1CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_4_1CHANNEL;
  }
  if (value == SURROUND5) {
    this->capabilities |= AO_CAP_MODE_5CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_5CHANNEL;
  }
  if (value >= SURROUND51) {
    this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_5_1CHANNEL;
  }
}


/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

  alsa_class_t        *this;

  this = calloc(1, sizeof (alsa_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "alsa";
  this->driver_class.description     = N_("xine audio output plugin using alsa-compliant audio devices/drivers");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

/*  this->config = xine->config; */
  this->xine = xine;
  return this;
 }

static ao_info_t ao_info_alsa = {
  10
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_ALSA_IFACE_VERSION, "alsa", XINE_VERSION_CODE, &ao_info_alsa, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

