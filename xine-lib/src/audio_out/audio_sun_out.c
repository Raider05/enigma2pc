/*
 * Copyright (C) 2001-2003 the xine project
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
#include <sys/audioio.h>
#if	HAVE_SYS_MIXER_H
#include <sys/mixer.h>
#endif
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#ifdef	__svr4__
#include <stropts.h>
#endif
#include <sys/param.h>

#if (defined(BSD) && BSD >= 199306)
typedef unsigned uint_t;
#endif

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>

#ifdef __svr4__
#define	CS4231_WORKAROUND	1	/* enable workaround for audiocs play.samples bug */
#define	SW_SAMPLE_COUNT		1
#endif


#ifndef	AUDIO_CHANNELS_MONO
#define	AUDIO_CHANNELS_MONO	1
#define	AUDIO_CHANNELS_STEREO	2
#endif
#ifndef	AUDIO_PRECISION_8
#define	AUDIO_PRECISION_8	8
#define	AUDIO_PRECISION_16	16
#endif

#define AO_SUN_IFACE_VERSION 9

#define GAP_TOLERANCE         5000
#define GAP_NONRT_TOLERANCE   AO_MAX_GAP
#define	NOT_REAL_TIME		-1


typedef struct {
  audio_driver_class_t	driver_class;

  xine_t	       *xine;
} sun_class_t;


typedef struct sun_driver_s {

  ao_driver_t    ao_driver;

  xine_t	*xine;

  char		*audio_dev;
  int            audio_fd;
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;
  int		 bytes_per_frame;

#ifdef __svr4__
  uint32_t       frames_in_buffer;     /* number of frames writen to audio hardware   */
#endif

  enum {
      RTSC_UNKNOWN = 0,
      RTSC_ENABLED,
      RTSC_DISABLED
  }		 use_rtsc;

  int		 convert_u8_s8;	       /* Builtin conversion 8-bit UNSIGNED->SIGNED */
  int		 mixer_volume;

#if	CS4231_WORKAROUND
  /*
   * Sun's audiocs driver has problems counting samples when we send
   * sound data chunks with a length that is not a multiple of 1024.
   * As a workaround for this problem, we re-block the audio stream,
   * so that we always send buffers of samples to the driver that have
   * a size of N*1024 bytes;
   */
#define	MIN_WRITE_SIZE	1024

  char		 buffer[MIN_WRITE_SIZE];
  unsigned	 buf_len;
#endif

#ifdef __svr4__
#if	SW_SAMPLE_COUNT
  struct timeval tv0;
  size_t	 sample0;
#endif

  size_t	 last_samplecnt;
#endif
} sun_driver_t;


/*
 * try to figure out, if the soundcard driver provides usable (precise)
 * sample counter information
 */
static int realtime_samplecounter_available(xine_t *xine, char *dev)
{
#ifdef __svr4__
  int fd = -1;
  audio_info_t info;
  int rtsc_ok = RTSC_DISABLED;
  int len;
  void *silence = NULL;
  struct timeval start, end;
  struct timespec delay;
  int usec_delay;
  unsigned last_samplecnt;
  unsigned increment;
  unsigned min_increment;

  len = 44100 * 4 / 4;    /* amount of data for 0.25sec of 44.1khz, stereo,
			   * 16bit.  44kbyte can be sent to all supported
			   * sun audio devices without blocking in the
			   * "write" below.
			   */
  silence = calloc(1, len);
  if (silence == NULL)
    goto error;

  if ((fd = xine_open_cloexec(dev, O_WRONLY|O_NONBLOCK)) < 0)
    goto error;

  /* We wanted non blocking open but now put it back to normal */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  AUDIO_INITINFO(&info);
  info.play.sample_rate = 44100;
  info.play.channels = AUDIO_CHANNELS_STEREO;
  info.play.precision = AUDIO_PRECISION_16;
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.samples = 0;
  if (ioctl(fd, AUDIO_SETINFO, &info)) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "rtsc: SETINFO failed\n");
    goto error;
  }

  if (write(fd, silence, len) != len) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "rtsc: write failed\n");
    goto error;
  }

  if (ioctl(fd, AUDIO_GETINFO, &info)) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "rtsc: GETINFO1, %s\n", strerror(errno));
    goto error;
  }

  last_samplecnt = info.play.samples;
  min_increment = ~0;

  gettimeofday(&start, NULL);
  for (;;) {
    delay.tv_sec = 0;
    delay.tv_nsec = 10000000;
    nanosleep(&delay, NULL);

    gettimeofday(&end, NULL);
    usec_delay = (end.tv_sec - start.tv_sec) * 1000000
	+ end.tv_usec - start.tv_usec;

    /* stop monitoring sample counter after 0.2 seconds */
    if (usec_delay > 200000)
      break;

    if (ioctl(fd, AUDIO_GETINFO, &info)) {
	xprintf(xine, XINE_VERBOSITY_DEBUG, "rtsc: GETINFO2 failed, %s\n", strerror(errno));
	goto error;
    }
    if (info.play.samples < last_samplecnt) {
	xprintf(xine, XINE_VERBOSITY_DEBUG, "rtsc: %u > %u?\n", last_samplecnt, info.play.samples);
	goto error;
    }

    if ((increment = info.play.samples - last_samplecnt) > 0) {
	/* printf("audio_sun_out: sample counter increment: %d\n", increment); */
	if (increment < min_increment) {
	  min_increment = increment;
	  if (min_increment < 2000)
	    break;	/* looks good */
	}
    }
    last_samplecnt = info.play.samples;
  }

  /*
   * For 44.1kkz, stereo, 16-bit format we would send sound data in 16kbytes
   * chunks (== 4096 samples) to the audio device.  If we see a minimum
   * sample counter increment from the soundcard driver of less than
   * 2000 samples,  we assume that the driver provides a useable realtime
   * sample counter in the AUDIO_INFO play.samples field.  Timing based
   * on sample counts should be much more accurate than counting whole
   * 16kbyte chunks.
   */
  if (min_increment < 2000)
    rtsc_ok = RTSC_ENABLED;

  /*
  printf("audio_sun_out: minimum sample counter increment per 10msec interval: %d\n"
	 "\t%susing sample counter based timing code\n",
	 min_increment, rtsc_ok == RTSC_ENABLED ? "" : "not ");
  */


error:
  if (silence != NULL) free(silence);
  if (fd >= 0) {
    /*
     * remove the 0 bytes from the above measurement from the
     * audio driver's STREAMS queue
     */
    ioctl(fd, I_FLUSH, FLUSHW);
    close(fd);
  }

  return rtsc_ok;
#else
  return RTSC_ENABLED;
#endif
}


/*
 * match the requested sample rate |sample_rate| against the
 * sample rates supported by the audio device |dev|.  Return
 * a supported sample rate,  it that sample rate is close to
 * (< 1% difference) the requested rate; return 0 otherwise.
 */
static int
find_close_samplerate_match(int dev, int sample_rate)
{
#if	HAVE_SYS_MIXER_H
    am_sample_rates_t *sr;
    int i, num, err, best_err, best_rate;

    for (num = 16; num < 1024; num *= 2) {
	sr = malloc(AUDIO_MIXER_SAMP_RATES_STRUCT_SIZE(num));
	if (!sr)
	    return 0;
	sr->type = AUDIO_PLAY;
	sr->flags = 0;
	sr->num_samp_rates = num;
	if (ioctl(dev, AUDIO_MIXER_GET_SAMPLE_RATES, sr)) {
	    free(sr);
	    return 0;
	}
	if (sr->num_samp_rates <= num)
	    break;
	free(sr);
    }

    if (sr->flags & MIXER_SR_LIMITS) {
	/*
	 * HW can playback any rate between
	 * sr->samp_rates[0] .. sr->samp_rates[1]
	 */
	free(sr);
	return 0;
    } else {
	/* HW supports fixed sample rates only */

	best_err = 65535;
	best_rate = 0;

	for (i = 0; i < sr->num_samp_rates; i++) {
	    err = abs(sr->samp_rates[i] - sample_rate);
	    if (err == 0) {
		/*
		 * exact supported sample rate match, no need to
		 * retry something else
		 */
		best_rate = 0;
		break;
	    }
	    if (err < best_err) {
		best_err = err;
		best_rate = sr->samp_rates[i];
	    }
	}

	free(sr);

	if (best_rate > 0 && 100*best_err < sample_rate) {
	    /* found a supported sample rate with <1% error? */
	    return best_rate;
	}
	return 0;
    }

#else
    int i, err;
    static const int audiocs_rates[] = {
	5510, 6620, 8000, 9600, 11025, 16000, 18900, 22050,
	27420, 32000, 33075, 37800, 44100, 48000, 0
    };

    for (i = 0; audiocs_rates[i]; i++) {
	err = abs(audiocs_rates[i] - sample_rate);
	if (err == 0) {
	    /*
	     * exact supported sample rate match, no need to
	     * retry something elise
	     */
	    return 0;
	}
	if (100*err < audiocs_rates[i]) {
	    /* <1% error? */
	    return audiocs_rates[i];
	}
    }

    return 0;
#endif
}


/*
 * return the highest sample rate supported by audio device |dev|.
 */
static int
find_highest_samplerate(int dev)
{
#if	HAVE_SYS_MIXER_H
    am_sample_rates_t *sr;
    int i, num, max_rate;

    for (num = 16; num < 1024; num *= 2) {
	sr = malloc(AUDIO_MIXER_SAMP_RATES_STRUCT_SIZE(num));
	if (!sr)
	    return 0;
	sr->type = AUDIO_PLAY;
	sr->flags = 0;
	sr->num_samp_rates = num;
	if (ioctl(dev, AUDIO_MIXER_GET_SAMPLE_RATES, sr)) {
	    free(sr);
	    return 0;
	}
	if (sr->num_samp_rates <= num)
	    break;
	free(sr);
    }

    if (sr->flags & MIXER_SR_LIMITS) {
	/*
	 * HW can playback any rate between
	 * sr->samp_rates[0] .. sr->samp_rates[1]
	 */
	max_rate = sr->samp_rates[1];
    } else {
	/* HW supports fixed sample rates only */
	max_rate = 0;
	for (i = 0; i < sr->num_samp_rates; i++) {
	    if (sr->samp_rates[i] > max_rate)
		max_rate = sr->samp_rates[i];
	}
    }
    free(sr);
    return max_rate;

#else
    return 44100;	/* should be supported even on old ISA SB cards */
#endif
}


/*
 * open the audio device for writing to
 *
 * Implicit assumptions about audio format (bits/rate/mode):
 *
 * bits == 16: We always get 16-bit samples in native endian format,
 *	using signed linear encoding
 *
 * bits ==  8: 8-bit samples use unsigned linear encoding,
 *	other 8-bit formats (uLaw, aLaw, etc) are currently not supported
 *	by xine
 */
static int ao_sun_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t info;
  int pass;
  int ok;

  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_sun_out: ao_sun_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_sun_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd >= 0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    close (this->audio_fd);
  }

  this->mode			= mode;
  this->input_sample_rate	= rate;
#ifdef __svr4__
  this->frames_in_buffer	= 0;
#endif

  /*
   * open audio device
   */

  this->audio_fd = xine_open_cloexec(this->audio_dev, O_WRONLY|O_NONBLOCK);
  if(this->audio_fd < 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_sun_out: opening audio device %s failed: %s\n"), this->audio_dev, strerror(errno));
    return 0;
  }

  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL) & ~O_NONBLOCK);

  /*
   * configure audio device
   */
  for (ok = pass = 0; pass <= 5; pass++) {

      AUDIO_INITINFO(&info);
      info.play.channels = (mode & AO_CAP_MODE_STEREO)
	  ? AUDIO_CHANNELS_STEREO
	  : AUDIO_CHANNELS_MONO;
      info.play.precision = bits;
      info.play.encoding = bits == 8
	  ? AUDIO_ENCODING_LINEAR8
	  : AUDIO_ENCODING_LINEAR;
      info.play.sample_rate = this->input_sample_rate;
      info.play.eof = 0;
      info.play.samples = 0;
#ifndef __svr4__
      info.blocksize = 1024;
#endif

      this->convert_u8_s8 = 0;

      if (pass & 1) {
	  /*
	   * on some sun audio drivers, 8-bit unsigned LINEAR8 encoding is
	   * not supported, but 8-bit signed encoding is.
	   *
	   * Try S8, and if it works, use our own U8->S8 conversion before
	   * sending the samples to the sound driver.
	   */
	  if (info.play.encoding != AUDIO_ENCODING_LINEAR8)
	      continue;
	  info.play.encoding = AUDIO_ENCODING_LINEAR;
	  this->convert_u8_s8 = 1;
      }

      if (pass & 2) {
	  /*
	   * on some sun audio drivers, only certain fixed sample rates are
	   * supported.
	   *
	   * In case the requested sample rate is very close to one of the
	   * supported rates,  use the fixed supported rate instead.
	   *
	   * XXX: assuming the fixed supported rate works, should we
	   * lie with our return value and report the requested input
	   * sample rate, to avoid the software resample code?
	   */
	  if (!(info.play.sample_rate =
		find_close_samplerate_match(this->audio_fd,
					    this->input_sample_rate)))
	      continue;
      }

      if (pass & 4) {
	  /* like "pass & 2", but use the highest supported sample rate */
	  if (!(info.play.sample_rate = find_highest_samplerate(this->audio_fd)))
	      continue;
      }

      if ((ok = ioctl(this->audio_fd, AUDIO_SETINFO, &info) >= 0)) {
	  /* audio format accepted by audio driver */
	  break;
      }

      /*
       * format not supported?
       * retry with different encoding and/or sample rate
       */
  }

  if (!ok) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      "audio_sun_out: Cannot configure audio device for %dhz, %d channel, %d bits: %s\n",
	      rate, info.play.channels, bits, strerror(errno));
      close(this->audio_fd);
      this->audio_fd = -1;
      return 0;
  }

#ifdef __svr4__
  this->last_samplecnt = 0;
#endif

  this->output_sample_rate = info.play.sample_rate;
  this->num_channels = info.play.channels;

  this->bytes_per_frame = 1;
  if (info.play.channels == AUDIO_CHANNELS_STEREO)
    this->bytes_per_frame *= 2;
  if (info.play.precision == 16)
    this->bytes_per_frame *= 2;

#if	CS4231_WORKAROUND
  this->buf_len = 0;
#endif

  /*
  printf ("audio_sun_out: audio rate : %d requested, %d provided by device/sec\n",
	   this->input_sample_rate, this->output_sample_rate);
  */

  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_sun_out: %d channels output\n",this->num_channels);
  return this->output_sample_rate;
}

static int ao_sun_num_channels(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_sun_bytes_per_frame(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_sun_delay(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t info;

#ifdef __svr4__
  if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) == 0 &&
      (this->frames_in_buffer == 0 || info.play.samples > 0)) {

    if (info.play.samples < this->last_samplecnt) {
	xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		"audio_sun_out: broken sound driver, sample counter runs backwards, cur %u < prev %u\n",
		info.play.samples, this->last_samplecnt);
    }
    this->last_samplecnt = info.play.samples;

    if (this->use_rtsc == RTSC_ENABLED)
      return this->frames_in_buffer - info.play.samples;

#if	SW_SAMPLE_COUNT
    /* compute "current sample" based on real time */
    {
      struct timeval tv1;
      size_t cur_sample;
      size_t msec;

      gettimeofday(&tv1, NULL);

      msec = (tv1.tv_sec  - this->tv0.tv_sec)  * 1000
	  +  (tv1.tv_usec - this->tv0.tv_usec) / 1000;

      cur_sample = this->sample0 + this->output_sample_rate * msec / 1000;

      if (info.play.error) {
	AUDIO_INITINFO(&info);
	info.play.error = 0;
	ioctl(this->audio_fd, AUDIO_SETINFO, &info);
      }

      /*
       * more than 0.5 seconds difference between HW sample counter and
       * computed sample counter?  -> re-initialize
       */
      if (abs(cur_sample - info.play.samples) > this->output_sample_rate/2) {
	this->tv0 = tv1;
	this->sample0 = cur_sample = info.play.samples;
      }

      return this->frames_in_buffer - cur_sample;
    }
#endif
  }
#else
  if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) == 0)
    return info.play.seek / this->bytes_per_frame;
#endif
  return NOT_REAL_TIME;
}

static int ao_sun_get_gap_tolerance (ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;

  if (this->use_rtsc == RTSC_ENABLED)
    return GAP_TOLERANCE;
  else
    return GAP_NONRT_TOLERANCE;
}


#if	CS4231_WORKAROUND
/*
 * Sun's audiocs driver has problems counting samples when we send
 * sound data chunks with a length that is not a multiple of 1024.
 * As a workaround for this problem, we re-block the audio stream,
 * so that we always send buffers of samples to the driver that have
 * a size of N*1024 bytes;
 */
static int sun_audio_write(sun_driver_t *this, char *buf, unsigned nbytes)
{
  unsigned total_bytes, remainder;
  int num_written;
  unsigned orig_nbytes = nbytes;

  total_bytes = this->buf_len + nbytes;
  remainder = total_bytes % MIN_WRITE_SIZE;
  if ((total_bytes -= remainder) > 0) {
    struct iovec iov[2];
    int iovcnt = 0;

    if (this->buf_len > 0) {
      iov[iovcnt].iov_base = this->buffer;
      iov[iovcnt].iov_len = this->buf_len;
      iovcnt++;
    }
    iov[iovcnt].iov_base = buf;
    iov[iovcnt].iov_len = total_bytes - this->buf_len;

    this->buf_len = 0;
    buf += iov[iovcnt].iov_len;
    nbytes -= iov[iovcnt].iov_len;

    num_written = writev(this->audio_fd, iov, iovcnt+1);
    if (num_written != total_bytes)
      return -1;
  }

  if (nbytes > 0) {
    memcpy(this->buffer + this->buf_len, buf, nbytes);
    this->buf_len += nbytes;
  }

  return orig_nbytes;
}


static void sun_audio_flush(sun_driver_t *this)
{
  if (this->buf_len > 0) {
    write(this->audio_fd, this->buffer, this->buf_len);
    this->buf_len = 0;
  }
}

#else
static int sun_audio_write(sun_driver_t *this, char *buf, unsigned nbytes)
{
  return write(this->audio_fd, buf, nbytes);
}

static void sun_audio_flush(sun_driver_t *this)
{
}
#endif


 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_sun_write(ao_driver_t *this_gen,
			int16_t* data, uint32_t num_frames)
{
  uint8_t *frame_buffer=(uint8_t *)data;
  sun_driver_t *this = (sun_driver_t *) this_gen;
  int num_written;

  if (this->convert_u8_s8) {
      /*
       * Audio hardware does not support 8-bit unsigned format,
       * only 8-bit signed.  Convert to 8-bit unsigned before sending
       * the data to the audio device.
       */
      uint8_t *p = (void *)frame_buffer;
      int i;

      for (i = num_frames * this->bytes_per_frame; --i >= 0; p++)
	  *p ^= 0x80;
  }
  num_written = sun_audio_write(this, frame_buffer, num_frames * this->bytes_per_frame);
  if (num_written > 0) {
    int buffered_samples;

#ifdef __svr4__
    this->frames_in_buffer += num_written / this->bytes_per_frame;
#endif

    /*
     * Avoid storing too much data in the sound driver's buffers.
     *
     * When we find more than 3 seconds of buffered audio data in the
     * driver's buffer, deliberately block sending of more data, until
     * there is less then 2 seconds of buffered samples.
     *
     * During an active audio playback, this helps when either the
     * xine engine is stopped or a seek operation is performed. In
     * both cases the buffered audio samples need to be flushed from
     * the xine engine and the audio driver anyway.
     */
    if ((buffered_samples = ao_sun_delay(this_gen)) >= 3*this->output_sample_rate) {
      sleep(buffered_samples/this->output_sample_rate - 2);
    }
  }

  return num_written;
}

static void ao_sun_close(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  sun_audio_flush(this);
  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_sun_get_capabilities (ao_driver_t *this_gen) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_sun_exit(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;

  if (this->audio_fd >= 0)
    close(this->audio_fd);

  free (this);
}

/*
 * Get a property of audio driver.
 * return 1 in success, 0 on failure. (and the property value?)
 */
static int ao_sun_get_property (ao_driver_t *this_gen, int property) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) > -1) {
      this->mixer_volume = info.play.gain * 100 / AUDIO_MAX_GAIN;
    }
    return this->mixer_volume;
#ifdef HAVE_AUDIO_INFO_T_OUTPUT_MUTED
  case AO_PROP_MUTE_VOL:
    if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) < 0)
      return 0;
    return info.output_muted;
#endif
  }

  return 0;
}

/*
 * Set a property of audio driver.
 * return value on success, ~value on failure
 */
static int ao_sun_set_property (ao_driver_t *this_gen, int property, int value) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  AUDIO_INITINFO(&info);

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    this->mixer_volume = value;
    info.play.gain = value * AUDIO_MAX_GAIN / 100;
    if (ioctl(this->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#ifdef HAVE_AUDIO_INFO_T_OUTPUT_MUTED
  case AO_PROP_MUTE_VOL:
    info.output_muted = value != 0;
    if (ioctl(this->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#endif
  }

  return ~value;
}

static int ao_sun_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    AUDIO_INITINFO(&info);
    info.play.pause = 1;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);
    break;

  case AO_CTRL_PLAY_RESUME:
    AUDIO_INITINFO(&info);
    info.play.pause = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);
    break;

  case AO_CTRL_FLUSH_BUFFERS:
#ifdef	__svr4__
    /* flush buffered STEAMS data first */
    ioctl(this->audio_fd, I_FLUSH, FLUSHW);

    /*
     * the flush above discarded an unknown amount of data from the
     * audio device.  To get the "*_delay" computation in sync again,
     * reset the audio device's sample counter to 0, after waiting
     * that all samples still active playing on the sound hardware
     * have finished playing.
     */
    AUDIO_INITINFO(&info);
    info.play.pause = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);

    ioctl(this->audio_fd, AUDIO_DRAIN);

    AUDIO_INITINFO(&info);
    info.play.samples = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);

    this->frames_in_buffer = 0;
    this->last_samplecnt = 0;
#endif
#ifdef __NetBSD__
    ioctl(this->audio_fd, AUDIO_FLUSH);
#endif
    break;
  }

  return 0;
}

static ao_driver_t *ao_sun_open_plugin (audio_driver_class_t *class_gen, const void *data) {

  sun_class_t         *class = (sun_class_t *) class_gen;
  config_values_t     *config = class->xine->config;
  sun_driver_t	      *this;
  char                *devname;
  char                *audiodev;
  int                  audio_fd;
  int                  status;
  audio_info_t	       info;

  this = calloc(1, sizeof (sun_driver_t));
  if (!this)
    return NULL;

  this->xine = class->xine;

  audiodev = getenv("AUDIODEV");

  /* This config entry is security critical, is it really necessary? */
  devname = config->register_filename(config,
				    "audio.device.sun_audio_device",
				    audiodev && *audiodev ? audiodev : "/dev/audio",
				    XINE_CONFIG_STRING_IS_DEVICE_NAME,
				    _("Sun audio device name"),
				    _("Specifies the file name for the Sun audio device "
				      "to be used.\nThis setting is security critical, "
				      "because when changed to a different file, xine "
				      "can be used to fill this file with arbitrary content. "
				      "So you should be careful that the value you enter "
				      "really is a proper Sun audio device."),
				    XINE_CONFIG_SECURITY, NULL,
				    NULL);

  /*
   * find best device driver/channel
   */

  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_sun_out: Opening audio device %s...\n", devname);

  /*
   * open the device
   */
  this->audio_dev = devname;
  this->audio_fd = xine_open_cloexec(devname, O_WRONLY|O_NONBLOCK);

  if(this->audio_fd < 0)
  {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_sun_out: opening audio device %s failed: %s\n"), devname, strerror(errno));

    free (this);
    return NULL;
  }

  /*
   * set up driver to reasonable values for capabilities tests
   */

  AUDIO_INITINFO(&info);
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.precision = AUDIO_PRECISION_16;
  info.play.sample_rate = 44100;
  status = ioctl(this->audio_fd, AUDIO_SETINFO, &info);

  if (status < 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_sun_out: audio ioctl on device %s failed: %s\n"), devname, strerror(errno));

    free (this);
    return NULL;
  }

  /*
   * get capabilities
   */

  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_8BITS
		     | AO_CAP_16BITS | AO_CAP_PCM_VOL;
#ifdef __svr4__
  this->capabilities |= AO_CAP_MUTE_VOL;
#endif

  /*
   * get initial mixer volume
   */

  this->mixer_volume = ao_sun_get_property(&this->ao_driver, AO_PROP_MIXER_VOL);

  close (this->audio_fd);
  this->audio_fd = -1;

  this->xine = class->xine;
  this->use_rtsc = realtime_samplecounter_available(this->xine, this->audio_dev);
  this->output_sample_rate = 0;

  this->ao_driver.get_capabilities	= ao_sun_get_capabilities;
  this->ao_driver.get_property		= ao_sun_get_property;
  this->ao_driver.set_property		= ao_sun_set_property;
  this->ao_driver.open			= ao_sun_open;
  this->ao_driver.num_channels		= ao_sun_num_channels;
  this->ao_driver.bytes_per_frame	= ao_sun_bytes_per_frame;
  this->ao_driver.delay			= ao_sun_delay;
  this->ao_driver.write			= ao_sun_write;
  this->ao_driver.close			= ao_sun_close;
  this->ao_driver.exit			= ao_sun_exit;
  this->ao_driver.get_gap_tolerance     = ao_sun_get_gap_tolerance;
  this->ao_driver.control		= ao_sun_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */
static void *ao_sun_init_class (xine_t *xine, void *data) {
  sun_class_t         *this;

  this = calloc(1, sizeof (sun_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = ao_sun_open_plugin;
  this->driver_class.identifier      = "sun";
  this->driver_class.description     = N_("xine audio output plugin using sun-compliant audio devices/drivers");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->xine = xine;

  return this;
}


static const ao_info_t ao_info_sun = {
  10
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_SUN_IFACE_VERSION, "sun", XINE_VERSION_CODE, &ao_info_sun, ao_sun_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
