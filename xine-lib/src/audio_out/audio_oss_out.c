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
 * 20-8-2001 First implementation of Audio sync and Audio driver separation.
 * Copyright (C) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 *
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every audio channel
 * to a full amount of bits. So, it does not matter how many bits per sample,
 * or how many audio channels are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable,
 * rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent in units of audio_frames.
 */

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
#include <sys/ioctl.h>
#include <inttypes.h>

#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#endif
#ifdef HAVE_MACHINE_SOUNDCARD_H
# include <sys/soundcard.h>
#endif
#ifdef HAVE_SOUNDCARD_H
# include <soundcard.h>
#endif

#define LOG_MODULE "audio_oss_out"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/audio_out.h>

#include <sys/time.h>

#include "speakers.h"

#ifndef SNDCTL_DSP_SETFMT
/* Deprecated OSS API */
#define SNDCTL_DSP_SETFMT SOUND_PCM_SETFMT
#endif

#ifndef SNDCTL_DSP_SPEED
/* Deprecated OSS API */
#define SNDCTL_DSP_SPEED SOUND_PCM_WRITE_RATE
#endif

#ifndef AFMT_S16_NE
# ifdef WORDS_BIGENDIAN
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#ifndef AFMT_AC3
#       define AFMT_AC3         0x00000400
#endif

#define AO_OUT_OSS_IFACE_VERSION 9

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/* bufsize must be a multiple of 3 and 5 for 5.0 and 5.1 channel playback! */
#define ZERO_BUF_SIZE        15360

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000

#define OSS_SYNC_AUTO_DETECT  0
#define OSS_SYNC_GETODELAY    1
#define OSS_SYNC_GETOPTR      2
#define OSS_SYNC_SOFTSYNC     3
#define OSS_SYNC_PROBEBUFFER  4

typedef struct oss_driver_s {

  ao_driver_t      ao_driver;
  char             audio_dev[30];
  int              audio_fd;
  int              capabilities;
  int              mode;

  config_values_t *config;

  int32_t          output_sample_rate, input_sample_rate;
  int32_t          output_sample_k_rate;
  uint32_t         num_channels;
  uint32_t	   bits_per_sample;
  uint32_t	   bytes_per_frame;
  uint32_t         bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  uint32_t         last_getoptr;

  int              audio_started;
  int              sync_method;
  int              latency;
  int              buffer_size;

  struct {
    int            fd;
    int            prop;
    int            volume;
    int            mute;
  } mixer;

  struct timeval   start_time;

  xine_t          *xine;
} oss_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;

  config_values_t      *config;
  xine_t               *xine;
} oss_class_t;

/*
 * open the audio device for writing to
 */
static int ao_oss_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int tmp;

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  "audio_oss_out: ao_open rate=%d, mode=%d, dev=%s\n", rate, mode, this->audio_dev);

  if ( (mode & this->capabilities) == 0 ) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd > -1) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) ) {
      return this->output_sample_rate;
    }

    close (this->audio_fd);
  }

  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  this->last_getoptr           = 0;
  this->audio_started          = 0;

  /*
   * open audio device
   */

  this->audio_fd = xine_open_cloexec(this->audio_dev, O_WRONLY|O_NONBLOCK);
  if (this->audio_fd < 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: Opening audio device %s: %s\n"), this->audio_dev, strerror(errno));
    return 0;
  }

  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL)&~O_NONBLOCK);

  /*
   * configure audio device
   * In A52 mode, skip all other SNDCTL commands
   */
  if(!(mode & (AO_CAP_MODE_A52 | AO_CAP_MODE_AC5))) {
    tmp = (mode & AO_CAP_MODE_STEREO) ? 1 : 0;
    ioctl(this->audio_fd,SNDCTL_DSP_STEREO,&tmp);

    tmp = bits;
    ioctl(this->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

    tmp = this->input_sample_rate;
    if (ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {

      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("audio_oss_out: warning: sampling rate %d Hz not supported, trying 44100 Hz\n"),
	      this->input_sample_rate);

      tmp = 44100;
      if (ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: error: 44100 Hz sampling rate not supported\n");
        return 0;
      }
    }
    this->output_sample_rate = tmp;
    this->output_sample_k_rate = this->output_sample_rate / 1000;
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: audio rate : %d requested, %d provided by device\n"),
	    this->input_sample_rate, this->output_sample_rate);
  }
  /*
   * set number of channels / a52 passthrough
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
    tmp = 1;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_STEREO:
    tmp = 2;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_4CHANNEL:
    tmp = 4;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_5CHANNEL:
    tmp = 5;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    tmp = 6;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    tmp = AFMT_AC3;
    this->num_channels = 2; /* FIXME: is this correct ? */
    this->output_sample_rate = this->input_sample_rate;
    this->output_sample_k_rate = this->output_sample_rate / 1000;
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: AO_CAP_MODE_A52\n");
    break;
  }

  xprintf(this->xine, XINE_VERBOSITY_LOG, "audio_oss_out: %d channels output\n", this->num_channels);
  this->bytes_per_frame=(this->bits_per_sample*this->num_channels)/8;

  /*
   * set format
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
  case AO_CAP_MODE_STEREO:
  case AO_CAP_MODE_4CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    if (bits==8)
      tmp = AFMT_U8;
    else
      tmp = AFMT_S16_NE;
    if (ioctl(this->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0
	|| (tmp!=AFMT_S16_NE && tmp!=AFMT_U8)) {
      if (bits==8) {
	xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: SNDCTL_DSP_SETFMT failed for AFMT_U8.\n");
        if (tmp != AFMT_U8)
          xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: ioctl succeeded but set format to 0x%x.\n",tmp);
        else
          xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: The AFMT_U8 ioctl failed.\n");
        return 0;
      } else {
	xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: SNDCTL_DSP_SETFMT failed for AFMT_S16_NE.\n");
        if (tmp != AFMT_S16_NE)
          xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: ioctl succeeded but set format to 0x%x.\n",tmp);
        else
          xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: The AFMT_S16_NE ioctl failed.\n");
        return 0;
      }
    }
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    tmp = bits;
    ioctl(this->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

    tmp = this->input_sample_rate;
    ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp);
    tmp = 2;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    tmp = AFMT_AC3;
    if (ioctl(this->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0 || tmp != AFMT_AC3) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "audio_oss_out: AC3 SNDCTL_DSP_SETFMT failed. %d. Using alternative.\n",tmp);
      tmp = AFMT_S16_LE;
      ioctl(this->audio_fd, SNDCTL_DSP_SETFMT, &tmp);
    }
    break;
  }



  /*
   * audio buffer size handling
   */

  /* WARNING: let's hope for good defaults here...
     tmp=0 ;
     fsize = AUDIO_FRAGMENT_SIZE;
     while (fsize>0) {
     fsize /=2;
     tmp++;
     }
     tmp--;

     tmp = (AUDIO_NUM_FRAGMENTS << 16) | tmp ;

     printf ("audio_oss_out: audio buffer fragment info : %x\n",tmp);

     ioctl(this->audio_fd,SNDCTL_DSP_SETFRAGMENT,&tmp);
  */

  return this->output_sample_rate;
}

static int ao_oss_num_channels(ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_oss_bytes_per_frame(ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  return this->bytes_per_frame;
}

static int ao_oss_get_gap_tolerance (ao_driver_t *this_gen){

  /* oss_driver_t *this = (oss_driver_t *) this_gen; */

  return GAP_TOLERANCE;
}

static int ao_oss_delay(ao_driver_t *this_gen) {

  count_info    info;
  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           bytes_left = 0;
  int           frames;
  struct        timeval tv;

  switch (this->sync_method) {
  case OSS_SYNC_PROBEBUFFER:
    if( this->bytes_in_buffer < this->buffer_size )
      bytes_left = this->bytes_in_buffer;
    else
      bytes_left = this->buffer_size;
    break;

  case OSS_SYNC_SOFTSYNC:
    /* use system real-time clock to get pseudo audio frame position */

    xine_monotonic_clock(&tv, NULL);

    frames  = (tv.tv_usec - this->start_time.tv_usec)
                  * this->output_sample_k_rate / 1000;
    frames += (tv.tv_sec - this->start_time.tv_sec)
                  * this->output_sample_rate;

    frames -= this->latency * this->output_sample_k_rate;

    /* calc delay */

    bytes_left = this->bytes_in_buffer - frames * this->bytes_per_frame;

    if (bytes_left<=0) /* buffer ran dry */
      bytes_left = 0;
    break;
  case OSS_SYNC_GETODELAY:
#ifdef SNDCTL_DSP_GETODELAY
    if (ioctl (this->audio_fd, SNDCTL_DSP_GETODELAY, &bytes_left)) {
      perror ("audio_oss_out: DSP_GETODELAY ioctl():");
    }
    if (bytes_left<0)
      bytes_left = 0;

    lprintf ("%d bytes left\n", bytes_left);

    break;
#endif
  case OSS_SYNC_GETOPTR:
    if (ioctl (this->audio_fd, SNDCTL_DSP_GETOPTR, &info)) {
      perror ("audio_oss_out: SNDCTL_DSP_GETOPTR failed:");
    }

    lprintf ("%d bytes output\n", info.bytes);

    if (this->bytes_in_buffer < info.bytes) {
      this->bytes_in_buffer -= this->last_getoptr; /* GETOPTR wrapped */
    }

    bytes_left = this->bytes_in_buffer - info.bytes; /* calc delay */

    if (bytes_left<=0) { /* buffer ran dry */
      bytes_left = 0;
      this->bytes_in_buffer = info.bytes;
    }
    this->last_getoptr = info.bytes;
    break;
  }

  return bytes_left / this->bytes_per_frame;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_oss_write(ao_driver_t *this_gen,
			int16_t* frame_buffer, uint32_t num_frames) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int n;

  lprintf ("ao_oss_write %d frames\n", num_frames);

  if (this->sync_method == OSS_SYNC_SOFTSYNC) {
    int            simulated_bytes_in_buffer, frames ;
    struct timeval tv;
    /* check if simulated buffer ran dry */

    xine_monotonic_clock(&tv, NULL);

    frames  = (tv.tv_usec - this->start_time.tv_usec)
                  * this->output_sample_k_rate / 1000;
    frames += (tv.tv_sec - this->start_time.tv_sec)
                  * this->output_sample_rate;

    /* calc delay */

    simulated_bytes_in_buffer = frames * this->bytes_per_frame;

    if (this->bytes_in_buffer < simulated_bytes_in_buffer)
      this->bytes_in_buffer = simulated_bytes_in_buffer;
  }

  this->bytes_in_buffer += num_frames * this->bytes_per_frame;

  n = write(this->audio_fd, frame_buffer, num_frames * this->bytes_per_frame);

  lprintf ("ao_oss_write done\n");

  return (n >= 0 ? n : 0);
}

static void ao_oss_close(ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_oss_get_capabilities (ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  return this->capabilities;
}

static void ao_oss_exit(ao_driver_t *this_gen) {

  oss_driver_t    *this   = (oss_driver_t *) this_gen;

  if (this->mixer.fd != -1)
    close(this->mixer.fd);
  if (this->audio_fd != -1)
    close(this->audio_fd);

  free (this);
}

static int ao_oss_get_property (ao_driver_t *this_gen, int property) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           audio_devs;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    if(!this->mixer.mute) {

      if(this->mixer.fd != -1) {
	IOCTL_REQUEST_TYPE cmd = 0;
	int v;

	ioctl(this->mixer.fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);

	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_READ_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_READ_VOLUME;
	else
	  return -1;

	ioctl(this->mixer.fd, cmd, &v);
	this->mixer.volume = (((v & 0xFF00) >> 8) + (v & 0x00FF)) / 2;
      } else
	return -1;
    }
    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    return this->mixer.mute;
    break;
  }

  return 0;
}

static int ao_oss_set_property (ao_driver_t *this_gen, int property, int value) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           audio_devs;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    if(!this->mixer.mute) {

      if(this->mixer.fd != -1) {
	IOCTL_REQUEST_TYPE cmd = 0;
	int v;

	ioctl(this->mixer.fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);

	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_WRITE_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_WRITE_VOLUME;
	else
	  return -1;

	v = (value << 8) | value;
	ioctl(this->mixer.fd, cmd, &v);
	this->mixer.volume = value;
      } else
	return -1;
    } else
      this->mixer.volume = value;

    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    this->mixer.mute = (value) ? 1 : 0;

    if(this->mixer.mute) {

      if(this->mixer.fd != -1) {
	IOCTL_REQUEST_TYPE cmd = 0;
	int v = 0;

	ioctl(this->mixer.fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);

	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_WRITE_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_WRITE_VOLUME;
	else
	  return -1;

	ioctl(this->mixer.fd, cmd, &v);
      } else
	return -1;
    } else
      (void) ao_oss_set_property(&this->ao_driver, this->mixer.prop, this->mixer.volume);

    return value;
    break;
  }

  return -1;
}

static int ao_oss_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  oss_driver_t *this = (oss_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    lprintf ("AO_CTRL_PLAY_PAUSE\n");

    if (this->sync_method != OSS_SYNC_SOFTSYNC)
      ioctl(this->audio_fd, SNDCTL_DSP_RESET, NULL);

    /* close/reopen if RESET causes problems */
    if (this->sync_method == OSS_SYNC_GETOPTR) {
      ao_oss_close(this_gen);
      ao_oss_open(this_gen, this->bits_per_sample, this->input_sample_rate, this->mode);
    }
    break;

  case AO_CTRL_PLAY_RESUME:
    lprintf ("AO_CTRL_PLAY_RESUME\n");
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    lprintf ("AO_CTRL_FLUSH_BUFFERS\n");
    if (this->sync_method != OSS_SYNC_SOFTSYNC)
      ioctl(this->audio_fd, SNDCTL_DSP_RESET, NULL);

    if (this->sync_method == OSS_SYNC_GETOPTR) {
      ao_oss_close(this_gen);
      ao_oss_open(this_gen, this->bits_per_sample, this->input_sample_rate, this->mode);
    }

    lprintf ("AO_CTRL_FLUSH_BUFFERS done\n");

    break;
  }

  return 0;
}

/* Probe /dev/dsp,       /dev/dsp1,       /dev/dsp2 ...
 *       /dev/sound/dsp, /dev/sound/dsp1, /dev/sound/dsp2, ...
 * If one is found, the name of the winner is placed in this->audio_dev
 * and the function returns the audio rate.
 * If not, the function returns 0.
 */
static int probe_audio_devices(oss_driver_t *this) {
  static const char *const base_names[2] = {"/dev/dsp", "/dev/sound/dsp"};
  int base_num, i;
  int audio_fd, rate;
  int best_rate;
  char devname[30];

  strcpy(this->audio_dev, "auto");

  best_rate = 0;
  for(base_num = 0; base_num < 2; ++base_num) {
    for(i = -1; i < 16; i++) {
      if (i == -1) strcpy(devname, base_names[base_num]);
      else sprintf(devname, "%s%d", base_names[base_num], i);

      /* Test the device */
      audio_fd = open(devname, O_WRONLY|O_NONBLOCK);
      if (audio_fd >= 0) {

	/* test bitrate capability */
	rate = 48000;
	ioctl(audio_fd, SNDCTL_DSP_SPEED, &rate);
	if (rate > best_rate) {
	  strcpy(this->audio_dev, devname); /* Better, keep this one */
	  best_rate = rate;
	}

	close (audio_fd);
      }
    }
  }
  return best_rate; /* Will be zero if we did not find one */
}

static void oss_speaker_arrangement_cb (void *user_data,
                                  xine_cfg_entry_t *entry);

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {

  oss_class_t     *class = (oss_class_t *) class_gen;
  config_values_t *config = class->config;
  oss_driver_t    *this;
  int              caps;
  int              audio_fd;
  int              num_channels, status, arg;
  static const char * const sync_methods[] = {"auto", "getodelay", "getoptr", "softsync", "probebuffer", NULL};
  static const char * const devname_opts[] = {"auto", "/dev/dsp", "/dev/sound/dsp", NULL};
  int devname_val, devname_num;

  AUDIO_DEVICE_SPEAKER_ARRANGEMENT_TYPES;
  int speakers;


  this = calloc(1, sizeof (oss_driver_t));
  if (!this)
    return NULL;

  /*
   * find best device driver/channel
   */

  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: Opening audio device...\n");

  /* devname_val is offset used to select auto, /dev/dsp, or /dev/sound/dsp */
  devname_val = config->register_enum (config, "audio.device.oss_device_name", 0,
				       (char **)devname_opts,
				       _("OSS audio device name"),
				       _("Specifies the base part of the audio device name, "
					 "to which the OSS device number is appended to get the "
					 "full device name.\nSelect \"auto\" if you want xine to "
					 "auto detect the corret setting."),
					10, NULL, NULL);
  /* devname_num is the N in '/dev[/sound]/dsp[N]'. Set to -1 for nothing */
  devname_num = config->register_num(config, "audio.device.oss_device_number", -1,
				     _("OSS audio device number, -1 for none"),
				     _("The full audio device name is created by concatenating the "
				       "OSS device name and the audio device number.\n"
				       "If you do not need a number because you are happy with "
				       "your system's default audio device, set this to -1.\n"
				       "The range of this value is -1 or 0-15. This setting is "
				       "ignored, when the OSS audio device name is set to \"auto\"."),
				     10, NULL, NULL);
  if (devname_val == 0) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: audio.device.oss_device_name = auto, probing devs\n"));
    if ( ! probe_audio_devices(this)) {  /* Returns zero on fail */
      xprintf(class->xine, XINE_VERBOSITY_LOG,
	      _("audio_oss_out: Auto probe for audio device failed\n"));
      free (this);
      return NULL;
    }
  }
  else {
    /* Create the device name /dev[/sound]/dsp[0-15] */
    if (devname_num < 0) strcpy(this->audio_dev, devname_opts[devname_val]);
    else sprintf(this->audio_dev, "%s%d", devname_opts[devname_val], devname_num);
  }

  /*
   * open that device
   */

  xprintf(class->xine, XINE_VERBOSITY_LOG,
	  _("audio_oss_out: using device >%s<\n"), this->audio_dev);

  audio_fd = xine_open_cloexec(this->audio_dev, O_WRONLY|O_NONBLOCK);

  if (audio_fd < 0) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: opening audio device %s failed:\n%s\n"), this->audio_dev, strerror(errno));

    free (this);
    return NULL;

  }
  /*
   * set up driver to reasonable values for capabilities tests
   */

  arg = AFMT_S16_NE;
  status = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &arg);
  arg = 44100;
  status = ioctl(audio_fd, SNDCTL_DSP_SPEED, &arg);

  /*
   * find out which sync method to use
   */

  this->sync_method = config->register_enum (config, "audio.oss_sync_method", OSS_SYNC_AUTO_DETECT,
					     (char **)sync_methods,
					     _("a/v sync method to use by OSS"),
					     _("xine can use different methods to keep audio and video "
					       "synchronized. Which setting works best depends on the "
					       "OSS driver and sound hardware you are using. Try the "
					       "various methods, if you experience sync problems.\n\n"
					       "The meaning of the values is as follows:\n\n"
					       "auto\n"
					       "xine attempts to automatically detect the optimal setting\n\n"
					       "getodelay\n"
					       "uses the SNDCTL_DSP_GETODELAY ioctl to achieve true a/v "
					       "sync even if the driver claims not to support realtime "
					       "playback\n\n"
					       "getoptr\n"
					       "uses the SNDCTL_DSP_GETOPTR ioctl to achieve true a/v "
					       "sync even if the driver supports the preferred "
					       "SNDCTL_DSP_GETODELAY ioctl\n\n"
					       "softsync\n"
					       "uses software synchronization with the system clock; audio "
					       "and video can get severely out of sync if the system clock "
					       "speed does not precisely match your sound card's playback "
					       "speed\n\n"
					       "probebuffer\n"
					       "probes the sound card buffer size on initialization to "
					       "calculate the latency for a/v sync; try this if your "
					       "system does not support any of the realtime ioctls and "
					       "you experience sync errors after long playback"),
					     20, NULL, NULL);

  if (this->sync_method == OSS_SYNC_AUTO_DETECT) {

    count_info info;

    /*
     * check if SNDCTL_DSP_GETODELAY works. if so, using it is preferred.
     */

#ifdef SNDCTL_DSP_GETODELAY
    if (ioctl(audio_fd, SNDCTL_DSP_GETODELAY, &info) != -1) {
      xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: using SNDCTL_DSP_GETODELAY\n");
      this->sync_method = OSS_SYNC_GETODELAY;
    } else
#endif
    if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &info) != -1) {
      xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: using SNDCTL_DSP_GETOPTR\n");
      this->sync_method = OSS_SYNC_GETOPTR;
    } else {
      this->sync_method = OSS_SYNC_SOFTSYNC;
    }
  }

  if (this->sync_method == OSS_SYNC_SOFTSYNC) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: Audio driver realtime sync disabled...\n"
	      "audio_oss_out: ...will use system real-time clock for soft-sync instead\n"
	      "audio_oss_out: ...there may be audio/video synchronization issues\n"));
    xine_monotonic_clock(&this->start_time, NULL);

    this->latency = config->register_range (config, "audio.oss_latency", 0,
					    -3000, 3000,
					    _("OSS audio output latency (adjust a/v sync)"),
					    _("If you experience audio being not in sync "
					      "with the video, you can enter a fixed offset "
					      "here to compensate.\nThe unit of the value "
					      "is one PTS tick, which is the 90000th part "
					      "of a second."),
					    20, NULL, NULL);
  }

  if (this->sync_method == OSS_SYNC_PROBEBUFFER) {
    char *buf;
    int c;

    this->buffer_size = 0;

    if( (buf=calloc(1, 1024)) != NULL ) {
      do {
        c = write(audio_fd,buf,1024);
        if( c != -1 )
          this->buffer_size += c;
      } while( c == 1024 );

      free(buf);
    }
    close(audio_fd);
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("audio_oss_out: Audio driver realtime sync disabled...\n"
	      "audio_oss_out: ...probing output buffer size: %d bytes\naudio_oss_out: ...there may be audio/video synchronization issues\n"), this->buffer_size);

    audio_fd = xine_open_cloexec(this->audio_dev, O_WRONLY|O_NONBLOCK);

    if(audio_fd < 0)
    {
      xprintf(class->xine, XINE_VERBOSITY_LOG,
	      _("audio_oss_out: opening audio device %s failed:\n%s\n"), this->audio_dev, strerror(errno));

      free (this);
      return NULL;
    }
  }

  this->capabilities = 0;

  arg = AFMT_U8;
  if( ioctl(audio_fd, SNDCTL_DSP_SETFMT, &arg) != -1  && arg == AFMT_U8)
    this->capabilities |= AO_CAP_8BITS;

  /* switch back to 16bits, because some soundcards otherwise do not report all their capabilities */
  arg = AFMT_S16_NE;
  if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &arg) == -1 || arg != AFMT_S16_NE) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_oss_out: switching the soundcard to 16 bits mode failed\n");
    free(this);
    close(audio_fd);
    return NULL;
  }

  /* for usability reasons, keep this in sync with audio_alsa_out.c */
  speakers = config->register_enum(config, "audio.output.speaker_arrangement", STEREO,
                                   (char **)speaker_arrangement,
                                   AUDIO_DEVICE_SPEAKER_ARRANGEMENT_HELP,
                                   0, oss_speaker_arrangement_cb, this);


  char *logmsg = strdup (_("audio_oss_out: supported modes are"));
  num_channels = 1;
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels);
  if ( (status != -1) && (num_channels==1) ) {
    this->capabilities |= AO_CAP_MODE_MONO;
    xine_strcat_realloc (&logmsg, _(" mono"));
  }
  num_channels = 2;
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels);
  if ( (status != -1) && (num_channels==2) ) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    xine_strcat_realloc (&logmsg, _(" stereo"));
  }
  num_channels = 4;
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels);
  if ( (status != -1) && (num_channels==4) )  {
    if  ( speakers == SURROUND4 ) {
      this->capabilities |= AO_CAP_MODE_4CHANNEL;
      xine_strcat_realloc (&logmsg, _(" 4-channel"));
    }
    else
      xine_strcat_realloc (&logmsg, _(" (4-channel not enabled in xine config)"));
  }
  num_channels = 5;
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels);
  if ( (status != -1) && (num_channels==5) ) {
    if  ( speakers == SURROUND5 ) {
      this->capabilities |= AO_CAP_MODE_5CHANNEL;
      xine_strcat_realloc (&logmsg, _(" 5-channel"));
    }
    else
      xine_strcat_realloc (&logmsg, _(" (5-channel not enabled in xine config)"));
  }
  num_channels = 6;
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels);
  if ( (status != -1) && (num_channels==6) ) {
    if  ( speakers == SURROUND51 ) {
      this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
      xine_strcat_realloc (&logmsg, _(" 5.1-channel"));
    }
    else
      xine_strcat_realloc (&logmsg, _(" (5.1-channel not enabled in xine config)"));
  }

  ioctl(audio_fd,SNDCTL_DSP_GETFMTS,&caps);

  /* one would normally check for (caps & AFMT_AC3) before asking about passthrough,
   * but some buggy OSS drivers do not report this properly, so we ask anyway */
  if  ( speakers == A52_PASSTHRU ) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
    xine_strcat_realloc (&logmsg, _(" a/52 pass-through"));
  }
  else
    xine_strcat_realloc (&logmsg, _(" (a/52 pass-through not enabled in xine config)"));

  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "%s\n", logmsg);
  free (logmsg);

  /*
   * mixer initialisation.
   */
  {
    char mixer_name[32];
    char mixer_dev[32];
    int mixer_num;
    int audio_devs;
    char *parse;

    mixer_num = config->register_num(config, "audio.device.oss_mixer_number", -1,
				     _("OSS audio mixer number, -1 for none"),
				     _("The full mixer device name is created by taking the "
				       "OSS device name, replacing \"dsp\" with \"mixer\" and "
				       "adding the mixer number.\n"
				       "If you do not need a number because you are happy with "
				       "your system's default mixer device, set this to -1.\n"
				       "The range of this value is -1 or 0-15. This setting is "
				       "ignored, when the OSS audio device name is set to \"auto\"."),
				     10, NULL, NULL);

    /* get the mixer device name from the audio device name by replacing "dsp" with "mixer" */
    strcpy(mixer_name, this->audio_dev);
    if ((parse = strstr(mixer_name, "dsp"))) {
      parse[0] = '\0';
      parse += 3;
      if (devname_val == 0)
	snprintf(mixer_dev, sizeof(mixer_dev), "%smixer%s", mixer_name, parse);
      else if (mixer_num == -1)
	snprintf(mixer_dev, sizeof(mixer_dev), "%smixer", mixer_name);
      else
	snprintf(mixer_dev, sizeof(mixer_dev), "%smixer%d", mixer_name, mixer_num);
    }

    this->mixer.fd = xine_open_cloexec(mixer_dev, O_RDONLY);

    if(this->mixer.fd != -1) {

      ioctl(this->mixer.fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);

      if(audio_devs & SOUND_MASK_PCM) {
	this->capabilities |= AO_CAP_PCM_VOL;
	this->mixer.prop = AO_PROP_PCM_VOL;
      }
      else if(audio_devs & SOUND_MASK_VOLUME) {
	this->capabilities |= AO_CAP_MIXER_VOL;
	this->mixer.prop = AO_PROP_MIXER_VOL;
      }

      /*
       * This is obsolete in Linux kernel OSS
       * implementation, so this will certainly doesn't work.
       * So we just simulate the mute stuff
       */
      /*
	if(audio_devs & SOUND_MASK_MUTE)
	this->capabilities |= AO_CAP_MUTE_VOL;
      */
      this->capabilities |= AO_CAP_MUTE_VOL;

    } else
      xprintf (class->xine, XINE_VERBOSITY_LOG,
	       _("audio_oss_out: open() mixer %s failed: %s\n"), mixer_dev, strerror(errno));

    this->mixer.mute = 0;
    this->mixer.volume = ao_oss_get_property (&this->ao_driver, this->mixer.prop);

  }
  close (audio_fd);

  this->output_sample_rate    = 0;
  this->output_sample_k_rate  = 0;
  this->audio_fd              = -1;
  this->xine                  = class->xine;

  this->config                        = config;
  this->ao_driver.get_capabilities    = ao_oss_get_capabilities;
  this->ao_driver.get_property        = ao_oss_get_property;
  this->ao_driver.set_property        = ao_oss_set_property;
  this->ao_driver.open                = ao_oss_open;
  this->ao_driver.num_channels        = ao_oss_num_channels;
  this->ao_driver.bytes_per_frame     = ao_oss_bytes_per_frame;
  this->ao_driver.delay               = ao_oss_delay;
  this->ao_driver.write		      = ao_oss_write;
  this->ao_driver.close               = ao_oss_close;
  this->ao_driver.exit                = ao_oss_exit;
  this->ao_driver.get_gap_tolerance   = ao_oss_get_gap_tolerance;
  this->ao_driver.control	      = ao_oss_ctrl;

  return &this->ao_driver;
}

static void oss_speaker_arrangement_cb (void *user_data,
                                  xine_cfg_entry_t *entry) {
  oss_driver_t *this = (oss_driver_t *) user_data;
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

  oss_class_t        *this;

  this = calloc(1, sizeof (oss_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "oss";
  this->driver_class.description     = N_("xine audio output plugin using oss-compliant audio devices/drivers");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->config = xine->config;
  this->xine   = xine;

  return this;
}

static const ao_info_t ao_info_oss = {
  9 /* less than alsa so xine will use alsa's native interface by default */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_OSS_IFACE_VERSION, "oss", XINE_VERSION_CODE, &ao_info_oss, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
