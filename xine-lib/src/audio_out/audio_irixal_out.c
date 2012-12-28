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

#warning DISABLED: FIXME
#if 0

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#include <dmedia/audio.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/audio_out.h>

//#ifndef AFMT_S16_NE
//# if defined(sparc) || defined(__sparc__) || defined(PPC)
///* Big endian machines */
//#  define AFMT_S16_NE AFMT_S16_BE
//# else
//#  define AFMT_S16_NE AFMT_S16_LE
//# endif
//#endif

#define AO_IRIXAL_IFACE_VERSION 4

#define DEFAULT_GAP_TOLERANCE         5000

typedef struct irixal_driver_s {

  ao_driver_t   ao_driver;

  ALport	port;

  int           capabilities;
  int		open_mode;
  int		gap_tolerance;

  int32_t       output_sample_rate, input_sample_rate;
  uint32_t      num_channels;
  uint32_t      bits_per_sample;
  uint32_t      bytes_per_frame;
  stamp_t       frames_in_buffer;      /* number of frames writen to audio hardware   */

} irixal_driver_t;

//  static snd_output_t *jcd_out;
/*
 * open the audio device for writing to
 */
static int ao_irixal_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  irixal_driver_t      *this = (irixal_driver_t *) this_gen;
  int		resource;
  ALconfig	config;
  ALpv		parvalue;

  /*
   * Init config for audio port
   */
  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  /* not tested so far (missing an Onyx with multichannel output...) */
  case AO_CAP_MODE_4CHANNEL:
    this->num_channels = 4;
    break;
#if 0
/* unsupported so far */
  case AO_CAP_MODE_5CHANNEL:
    this->num_channels = 5;
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    this->num_channels = 6;
    break;
  case AO_CAP_MODE_A52:
    this->num_channels = 2;
    break;
#endif
  default:
    xlerror ("irixal Driver does not support the requested mode: 0x%x",mode);
    return 0;
  }

  if (! (config = alNewConfig ()))
  {
    xlerror ("cannot get new config: %s", strerror (oserror()));
    return 0;
  }
  if ( (alSetChannels (config, this->num_channels)) == -1)
  {
    xlerror ("cannot set to %d channels: %s", this->num_channels, strerror (oserror()));
    alFreeConfig (config);
    return 0;
  }

  switch (bits) {
    case 8:
      if ( (alSetWidth (config, AL_SAMPLE_8)) == -1)
      {
        xlerror ("cannot set 8bit mode: %s", strerror (oserror()));
        alFreeConfig (config);
        return 0;
      }
      break;
    case 16:
      /* Default format is 16bit PCM */
      break;
    default:
      xlerror ("irixal Driver does not support %dbit audio", bits);
      alFreeConfig (config);
      return 0;
  }

  printf("audio_irixal_out: channels=%d, bits=%d\n", this->num_channels, bits);

  /*
   * Try to open audio port
   */
  if (! (this->port = alOpenPort ("xine", "w", config))) {
    xlerror ("irixal Driver does not support the audio configuration");
    alFreeConfig (config);
    return 0;
  }
  alFreeConfig (config);
  resource = alGetResource (this->port);
  this->open_mode              = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  /* FIXME: Can use an irixal function here ?!? */
  this->bytes_per_frame        = (this->bits_per_sample*this->num_channels) / 8;
  this->frames_in_buffer       = 0;


  /* TODO: not yet settable (see alParams (3dm)): AL_INTERFACE, AL_CLOCK_GEN */
  /*
   * Try to adapt sample rate of audio port
   */
  parvalue.param = AL_MASTER_CLOCK;
  parvalue.value.i = AL_CRYSTAL_MCLK_TYPE;
  if (alSetParams (resource, &parvalue, 1) == -1)
    printf ("audio_irixal: FYI: cannot set audio master clock to crystal based clock\n");

  parvalue.param = AL_RATE;
  parvalue.value.ll = alIntToFixed (rate);
  if (alSetParams (resource, &parvalue, 1) == -1)
    printf ("audio_irixal: FYI: cannot set sample rate, using software resampling\n");
  if (alGetParams (resource, &parvalue, 1) == -1)
  {
    xlerror ("cannot ask for current sample rate, assuming everything worked...");
    this->output_sample_rate = this->input_sample_rate;
  }
  else
    this->output_sample_rate = alFixedToInt (parvalue.value.ll);

  if (this->input_sample_rate != this->output_sample_rate)
    printf ("audio_irixal: FYI: sample_rate in %d, out %d\n",
             this->input_sample_rate, this->output_sample_rate);

  return this->output_sample_rate;
}

static int ao_irixal_num_channels(ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_irixal_bytes_per_frame(ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_irixal_get_gap_tolerance (ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  return this->gap_tolerance;
}

static int ao_irixal_delay (ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  stamp_t stamp, time;
  int frames_left;

  if (alGetFrameTime (this->port, &stamp, &time) == -1)
    xlerror ("alGetFrameNumber failed");
  frames_left = this->frames_in_buffer - stamp;
  if (frames_left <= 0) /* buffer ran dry */
    frames_left = 0;

  return frames_left;
}

static int ao_irixal_write(ao_driver_t *this_gen,int16_t *data, uint32_t num_frames)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  stamp_t stamp;

  /* Grmbf. IRIX audio does not tell us, wenn we run dry.
   * We have to detect this ourself. */
  /* get absolute number of samples played so far
   * note: this counts up when run dry as well... */
  if (alGetFrameNumber (this->port, &stamp) == -1)
    xlerror ("alGetFrameNumber failed");
  if (this->frames_in_buffer < stamp) /* dry run */
  {
    if (this->frames_in_buffer > 0)
      printf ("audio_irixal: audio buffer dry run detected, buffer %llu should be > %llu!\n",
              this->frames_in_buffer, stamp);
    this->frames_in_buffer = stamp;
  }
  /* FIXME: what to do when the call would block?
   * We have to write things out anyway...
   * alGetFillable() would tell us, whether space was available */
  alWriteFrames (this->port, data, num_frames);
  this->frames_in_buffer += num_frames;

  return num_frames;
}

static void ao_irixal_close(ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  if (this->port)
    alClosePort (this->port);
  this->port = NULL;
}

static uint32_t ao_irixal_get_capabilities (ao_driver_t *this_gen) {
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_irixal_exit(ao_driver_t *this_gen)
{
  irixal_driver_t *this = (irixal_driver_t *) this_gen;
  ao_irixal_close (this_gen);
  free (this);
}

static int ao_irixal_get_property (ao_driver_t *this, int property) {
  /* FIXME: implement some properties */
  return 0;
}

/*
 *
 */
static int ao_irixal_set_property (ao_driver_t *this, int property, int value) {

  /* FIXME: Implement property support */
  return ~value;
}

/*
 *
 */
static int ao_irixal_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  irixal_driver_t *this = (irixal_driver_t *) this_gen;

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

static void *init_audio_out_plugin (config_values_t *config)
{
  irixal_driver_t *this;
  ALvalue values [32];
  ALpv    parvalue;
  char	  name[32];
  int	  i, numvalues;
  int	  useresource = -1;

  printf ("audio_irixal: init...\n");

  /* Check available outputs */
  /* TODO: this is verbose information only right now, output is not selectable */
  if ( (numvalues = alQueryValues (AL_SYSTEM, AL_DEFAULT_OUTPUT, values, 32, NULL, 0)) > 0)
  {
    useresource = values [0].i;
    for (i = 0; i < numvalues; i++)
    {
      parvalue.param = AL_NAME;
      parvalue.value.ptr = name;
      parvalue.sizeIn = 32;
      if (alGetParams (values [i].i, &parvalue, 1) != -1)
	printf ("  available Output: %s\n", name);
    }
  }
  if (useresource == -1)
  {
    xlerror ("cannot find output resource");
    return NULL;
  }

#if 0
  /* TODO */
  device = config->lookup_str(config,"irixal_default_device", "default");
#endif

  /* allocate struct */
  this = (irixal_driver_t *) calloc (sizeof (irixal_driver_t), 1);
  if (!this)
    return NULL;

  /* get capabilities */
  if ( (numvalues = alQueryValues (useresource, AL_CHANNELS, values, 32, NULL, 0)) > 0)
  {
    for (i = 0; i < numvalues; i++)
    {
      switch (values[i].i) {
	case 1:
	  this->capabilities |= AO_CAP_MODE_MONO;
	  break;
	case 2:
	  this->capabilities |= AO_CAP_MODE_STEREO;
	  break;
        /* not tested so far (missing an Onyx with multichannel output...) */
	case 4:
	  this->capabilities |= AO_CAP_MODE_4CHANNEL;
	  break;
#if 0
/* unsupported so far */
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
  case AO_CAP_MODE_A52:
#endif
	default:
	  printf ("  unsupported %d channel config available on system\n", values[i].i);
      }
    }
  }

  printf ("  capabilities 0x%X\n",this->capabilities);

  /* TODO: anything can change during runtime... move check to the right location */
  this->gap_tolerance = config->register_range (config, "audio.device.irixal_gap_tolerance",
					        DEFAULT_GAP_TOLERANCE, 0, 90000,
						_("irixal audio output maximum gap length"),
						_("You can specify the maximum offset between audio "
						  "and video xine will tolerate before trying to "
						  "resync them.\nThe unit of this value is one PTS tick, "
						  "which is the 90000th part of a second."),
						30, NULL, NULL);

  this->ao_driver.get_capabilities    = ao_irixal_get_capabilities;
  this->ao_driver.get_property        = ao_irixal_get_property;
  this->ao_driver.set_property        = ao_irixal_set_property;
  this->ao_driver.open                = ao_irixal_open;
  this->ao_driver.num_channels        = ao_irixal_num_channels;
  this->ao_driver.bytes_per_frame     = ao_irixal_bytes_per_frame;
  this->ao_driver.delay               = ao_irixal_delay;
  this->ao_driver.write		      = ao_irixal_write;
  this->ao_driver.close               = ao_irixal_close;
  this->ao_driver.exit                = ao_irixal_exit;
  this->ao_driver.get_gap_tolerance   = ao_irixal_get_gap_tolerance;
  this->ao_driver.control	      = ao_irixal_ctrl;

  return this;
}

static const ao_info_t ao_info_irixal = {
  "xine audio output plugin using IRIX libaudio",
  10
};

ao_info_t *get_audio_out_plugin_info()
{
  ao_info_irixal.description = _("xine audio output plugin using IRIX libaudio");
  return &ao_info_irixal;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_IRIXAL_IFACE_VERSION, "irixal", XINE_VERSION_CODE, &ao_info_irixal, init_audio_out_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


#endif
