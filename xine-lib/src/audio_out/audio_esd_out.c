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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <esd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <inttypes.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include <xine/metronom.h>

#define AO_OUT_ESD_IFACE_VERSION 9

#define	REBLOCK		      1	    /* reblock output to ESD_BUF_SIZE blks */
#define GAP_TOLERANCE         5000

typedef struct esd_driver_s {

  ao_driver_t      ao_driver;

  xine_t          *xine;

  int              audio_fd;
  int              capabilities;
  int              mode;

  char            *pname; /* Player name id for esd daemon */

  int32_t          output_sample_rate, input_sample_rate;
  int32_t          output_sample_k_rate;
  double           sample_rate_factor;
  uint32_t         num_channels;
  uint32_t	   bytes_per_frame;
  uint32_t         bytes_in_buffer;      /* number of bytes writen to esd */

  int              gap_tolerance, latency;
  int		   server_sample_rate;

  struct timeval   start_time;

  struct {
    int            source_id;
    int            volume;
    int            mute;
  } mixer;

#if	REBLOCK
  /*
   * Temporary sample buffer used to reblock the sample output stream
   * to writes using buffer sizes of n*ESD_BUF_SIZE bytes.
   *
   * The reblocking avoids a bug with esd 0.2.18 servers and reduces
   * cpu load with newer versions of the esd server.
   *
   * The esd 0.2.18 version zero fills "partial"/"incomplete" blocks.
   * esd 0.2.28+ has fixed this problem, by performing a busy polling
   * loop reading from a nonblocking socket to get the remainder of
   * the partial block.  This is wasting a lot of cpu cycles.
   */
  char		   reblock_buf[ESD_BUF_SIZE];
  int		   reblock_rem;
#endif

} esd_driver_t;

typedef struct {
  audio_driver_class_t driver_class;
  xine_t          *xine;
} esd_class_t;


/*
 * connect to esd
 */
static int ao_esd_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_format_t     format;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   "audio_esd_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd>=0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    esd_close (this->audio_fd);
  }

  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->output_sample_rate     = rate;
  this->bytes_in_buffer        = 0;
  this->start_time.tv_sec      = 0;

  /*
   * open stream to ESD server
   */

  format = ESD_STREAM | ESD_PLAY | ESD_BITS16;
  switch (mode) {
  case AO_CAP_MODE_MONO:
    format |= ESD_MONO;
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    format |= ESD_STEREO;
    this->num_channels = 2;
    break;
  }
  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: %d channels output\n",this->num_channels);

  this->bytes_per_frame=(bits*this->num_channels)/8;

#if ESD_RESAMPLE
  /* esd resamples (only for sample rates < the esd server's sample rate) */
  if (this->output_sample_rate > this->server_sample_rate)
    this->output_sample_rate = this->server_sample_rate;
#else
  /* use xine's resample code */
  this->output_sample_rate = this->server_sample_rate;
#endif
  this->output_sample_k_rate = this->output_sample_rate / 1000;

  this->audio_fd = esd_play_stream(format, this->output_sample_rate, NULL, this->pname);
  if (this->audio_fd < 0) {
    char *server = getenv("ESPEAKER");
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("audio_esd_out: connecting to ESD server %s: %s\n"),
	    server ? server : "<default>", strerror(errno));
    return 0;
  }

  return this->output_sample_rate;
}

static int ao_esd_num_channels(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_esd_bytes_per_frame(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->bytes_per_frame;
}


static int ao_esd_delay(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  int           bytes_left;
  int           frames;
  struct        timeval tv;

  if (this->start_time.tv_sec == 0)
    return 0;

  gettimeofday(&tv, NULL);

  frames  = (tv.tv_usec - this->start_time.tv_usec)
    * this->output_sample_k_rate / 1000;
  frames += (tv.tv_sec - this->start_time.tv_sec)
    * this->output_sample_rate;

  frames -= this->latency;
  if (frames < 0)
      frames = 0;

  /* calc delay */

  bytes_left = this->bytes_in_buffer - frames * this->bytes_per_frame;

  if (bytes_left<=0) /* buffer ran dry */
    bytes_left = 0;
  return bytes_left / this->bytes_per_frame;
}

static int ao_esd_write(ao_driver_t *this_gen,
			int16_t* frame_buffer, uint32_t num_frames)
{

  esd_driver_t  *this = (esd_driver_t *) this_gen;
  int            simulated_bytes_in_buffer, frames ;
  struct timeval tv;

  if (this->audio_fd<0)
    return 1;

  if (this->start_time.tv_sec == 0)
    gettimeofday(&this->start_time, NULL);

  /* check if simulated buffer ran dry */

  gettimeofday(&tv, NULL);

  frames  = (tv.tv_usec - this->start_time.tv_usec)
    * this->output_sample_k_rate / 1000;
  frames += (tv.tv_sec - this->start_time.tv_sec)
    * this->output_sample_rate;

  frames -= this->latency;
  if (frames < 0)
      frames = 0;

  /* calc delay */

  simulated_bytes_in_buffer = frames * this->bytes_per_frame;

  if (this->bytes_in_buffer < simulated_bytes_in_buffer)
    this->bytes_in_buffer = simulated_bytes_in_buffer;

#if REBLOCK
  {
    struct iovec iov[2];
    int iovcnt;
    int num_bytes;
    int nwritten;
    int rem;

    if (this->reblock_rem + num_frames*this->bytes_per_frame < ESD_BUF_SIZE) {
	/*
	 * the stuff in the temporary reblocking buffer plus the new
	 * samples still do not give a complete ESD_BUF_SIZE block.
	 * just save the new samples in the reblocking buffer for later.
	 */
	memcpy(this->reblock_buf + this->reblock_rem,
	       frame_buffer,
	       num_frames * this->bytes_per_frame);
	this->reblock_rem += num_frames * this->bytes_per_frame;
	return 1;
    }

    /* OK, we have at least one complete ESD_BUF_SIZE block */

    iovcnt = 0;
    num_bytes = 0;
    if (this->reblock_rem > 0) {
	/* send any saved samples from the reblocking buffer first */
	iov[iovcnt].iov_base = this->reblock_buf;
	iov[iovcnt].iov_len = this->reblock_rem;
	iovcnt++;
	num_bytes += this->reblock_rem;
	this->reblock_rem = 0;
    }
    rem = (num_bytes + num_frames * this->bytes_per_frame) % ESD_BUF_SIZE;
    if (num_frames * this->bytes_per_frame > rem) {
	/*
	 * add samples from caller, so that the total number of bytes is
	 * a multiple of ESD_BUF_SIZE
	 */
	iov[iovcnt].iov_base = frame_buffer;
	iov[iovcnt].iov_len = num_frames * this->bytes_per_frame - rem;
	num_bytes += num_frames * this->bytes_per_frame - rem;
	iovcnt++;
    }

    nwritten = writev(this->audio_fd, iov, iovcnt);
    if (nwritten != num_bytes) {
	if (nwritten < 0)
	  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: writev failed: %s\n", strerror(errno));
	else
	  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: warning, incomplete write: %d\n", nwritten);
    }
    if (nwritten > 0)
	this->bytes_in_buffer += nwritten;

    if (rem > 0) {
	/* save the remaining bytes for the next ao_esd_write() */
	memcpy(this->reblock_buf,
	       (char*)frame_buffer + iov[iovcnt-1].iov_len, rem);
	this->reblock_rem = rem;
    }
  }
#else
  this->bytes_in_buffer += num_frames * this->bytes_per_frame;

  write(this->audio_fd, frame_buffer, num_frames * this->bytes_per_frame);
#endif
  return 1;
}

static void ao_esd_close(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_esd_get_capabilities (ao_driver_t *this_gen) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->capabilities;
}

static int ao_esd_get_gap_tolerance (ao_driver_t *this_gen) {
  /* esd_driver_t *this = (esd_driver_t *) this_gen; */
  return GAP_TOLERANCE;
}

static void ao_esd_exit(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;

  if (this->audio_fd != -1)
    esd_close(this->audio_fd);

  free(this->pname);

  free (this);
}

static int ao_esd_get_property (ao_driver_t *this_gen, int property) {
  esd_driver_t      *this = (esd_driver_t *) this_gen;
  int                mixer_fd;
  esd_player_info_t *esd_pi;
  esd_info_t        *esd_i;

  switch(property) {
  case AO_PROP_MIXER_VOL:

    if((mixer_fd = esd_open_sound(NULL)) >= 0) {
      if((esd_i = esd_get_all_info(mixer_fd)) != NULL) {
	for(esd_pi = esd_i->player_list; esd_pi != NULL; esd_pi = esd_pi->next) {
	  if(!strcmp(this->pname, esd_pi->name)) {

	    this->mixer.source_id = esd_pi->source_id;

	    if(!this->mixer.mute)
	      this->mixer.volume  = (((esd_pi->left_vol_scale * 100)  / 256) +
				     ((esd_pi->right_vol_scale * 100) / 256)) >> 1;

	  }
	}
	esd_free_all_info(esd_i);
      }
      esd_close(mixer_fd);
    }

    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    return this->mixer.mute;
    break;
  }

  return 0;
}

static int ao_esd_set_property (ao_driver_t *this_gen, int property, int value) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  int           mixer_fd;

  switch(property) {
  case AO_PROP_MIXER_VOL:

    if(!this->mixer.mute) {

      /* need this to get source_id */
      (void) ao_esd_get_property(&this->ao_driver, AO_PROP_MIXER_VOL);

      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = (value * 256) / 100;

	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);

	if(!this->mixer.mute)
	  this->mixer.volume = value;

	esd_close(mixer_fd);
      }
    }
    else
      this->mixer.volume = value;

    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL: {
    int mute = (value) ? 1 : 0;

    /* need this to get source_id */
    (void) ao_esd_get_property(&this->ao_driver, AO_PROP_MIXER_VOL);

    if(mute) {
      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = 0;

	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);
	esd_close(mixer_fd);
      }
    }
    else {
      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = (this->mixer.volume * 256) / 100;

	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);
	esd_close(mixer_fd);
      }
    }

    this->mixer.mute = mute;

    return value;
  }
  break;
  }

  return ~value;
}

static int ao_esd_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  /* esd_driver_t *this = (esd_driver_t *) this_gen; */


  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    break;

  case AO_CTRL_PLAY_RESUME:
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    break;
  }

  return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen,
				 const void *data) {

  esd_class_t       *class = (esd_class_t *) class_gen;
  config_values_t   *config = class->xine->config;
  esd_driver_t      *this;
  int                audio_fd;
  int		     err;
  esd_server_info_t *esd_svinfo;
  int		     server_sample_rate;
  sigset_t           vo_mask, vo_mask_orig;

  /*
   * open stream to ESD server
   *
   * esd_open_sound needs a working SIGALRM for detecting a failed
   * attempt to autostart the esd daemon;  esd notifies the process that
   * attempts the esd daemon autostart with a SIGALRM (SIGUSR1) signal
   * about a failure to open the audio device (successful daemon startup).
   *
   * Temporarily release the blocked SIGALRM, while esd_open_sound is active.
   * (Otherwise xine hangs in esd_open_sound on a machine without sound)
   */

  sigemptyset(&vo_mask);
  sigaddset(&vo_mask, SIGALRM);
  if (sigprocmask(SIG_UNBLOCK, &vo_mask, &vo_mask_orig))
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: cannot unblock SIGALRM: %s\n", strerror(errno));

  xprintf(class->xine, XINE_VERBOSITY_LOG, _("audio_esd_out: connecting to esd server...\n"));
  audio_fd = esd_open_sound(NULL);
  err = errno;

  if (sigprocmask(SIG_SETMASK, &vo_mask_orig, NULL))
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "audio_esd_out: cannot block SIGALRM: %s\n", strerror(errno));

  if(audio_fd < 0) {
    char *server = getenv("ESPEAKER");

    /* print a message so the user knows why ESD failed */
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("audio_esd_out: can't connect to %s ESD server: %s\n"),
	    server ? server : "<default>", strerror(err));

    return NULL;
  }

  esd_svinfo = esd_get_server_info(audio_fd);
  if (esd_svinfo) {
      server_sample_rate = esd_svinfo->rate;
      esd_free_server_info(esd_svinfo);
  } else
      server_sample_rate = 44100;

  esd_close(audio_fd);

  this                     = calloc(1, sizeof (esd_driver_t));
  if (!this)
    return NULL;
  this->xine               = class->xine;
  this->pname              = strdup("xine esd audio output plugin");
  if (!this->pname) {
    free (this);
    return NULL;
  }
  this->output_sample_rate = 0;
  this->server_sample_rate = server_sample_rate;
  this->audio_fd           = -1;
  this->capabilities       = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_MIXER_VOL | AO_CAP_MUTE_VOL;
  this->latency            = config->register_range (config, "audio.device.esd_latency", 0,
						     -30000, 90000,
						     _("esd audio output latency (adjust a/v sync)"),
						     _("If you experience audio being not in sync "
						       "with the video, you can enter a fixed offset "
						       "here to compensate.\nThe unit of the value "
						       "is one PTS tick, which is the 90000th part "
						       "of a second."),
						     10, NULL, NULL);

  this->ao_driver.get_capabilities    = ao_esd_get_capabilities;
  this->ao_driver.get_property        = ao_esd_get_property;
  this->ao_driver.set_property        = ao_esd_set_property;
  this->ao_driver.open                = ao_esd_open;
  this->ao_driver.num_channels        = ao_esd_num_channels;
  this->ao_driver.bytes_per_frame     = ao_esd_bytes_per_frame;
  this->ao_driver.get_gap_tolerance   = ao_esd_get_gap_tolerance;
  this->ao_driver.delay               = ao_esd_delay;
  this->ao_driver.write		      = ao_esd_write;
  this->ao_driver.close               = ao_esd_close;
  this->ao_driver.exit                = ao_esd_exit;
  this->ao_driver.control	      = ao_esd_ctrl;

  return &(this->ao_driver);
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

  esd_class_t        *this;

  this = calloc(1, sizeof (esd_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "esd";
  this->driver_class.description     = N_("xine audio output plugin using esound");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->xine = xine;

  return this;
}

static const ao_info_t ao_info_esd = {
  4
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_ESD_IFACE_VERSION, "esd", XINE_VERSION_CODE, &ao_info_esd, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
