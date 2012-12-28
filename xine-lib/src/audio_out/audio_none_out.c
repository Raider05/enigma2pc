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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>

#define AO_OUT_NONE_IFACE_VERSION 9

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

#define GAP_TOLERANCE        AO_MAX_GAP

typedef struct none_driver_s {

  ao_driver_t    ao_driver;

  xine_t        *xine;

  int            capabilities;
  int            mode;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  uint32_t       latency;

} none_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;

  config_values_t      *config;
  xine_t               *xine;
} none_class_t;

/*
 * open the audio device for writing to
 */
static int ao_none_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  none_driver_t *this = (none_driver_t *) this_gen;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   "audio_none_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  this->mode                   = mode;
  this->sample_rate            = rate;
  this->bits_per_sample        = bits;

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  }

  return this->sample_rate;
}


static int ao_none_num_channels(ao_driver_t *this_gen)
{
  none_driver_t *this = (none_driver_t *) this_gen;
    return this->num_channels;
}

static int ao_none_bytes_per_frame(ao_driver_t *this_gen)
{
  none_driver_t *this = (none_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_none_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_none_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  none_driver_t *this = (none_driver_t *) this_gen;

  /* take some time to pretend we are doing something.
   * avoids burning cpu.
   */
  if( (1000 * num_frames / this->sample_rate) > 10 )
    xine_usec_sleep ((1000 * num_frames / this->sample_rate)*1000/2);

  return 1;
}


static int ao_none_delay (ao_driver_t *this_gen)
{
  return 0;
}

static void ao_none_close(ao_driver_t *this_gen)
{
}

static uint32_t ao_none_get_capabilities (ao_driver_t *this_gen) {
  none_driver_t *this = (none_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_none_exit(ao_driver_t *this_gen)
{
  none_driver_t *this = (none_driver_t *) this_gen;

  ao_none_close(this_gen);

  free (this);
}

static int ao_none_get_property (ao_driver_t *this_gen, int property) {

  return 0;
}

static int ao_none_set_property (ao_driver_t *this_gen, int property, int value) {

  return ~value;
}

static int ao_none_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  /*none_driver_t *this = (none_driver_t *) this_gen;*/

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

  none_class_t     *class = (none_class_t *) class_gen;
  /* config_values_t *config = class->config; */
  none_driver_t    *this;

  lprintf ("open_plugin called\n");

  this = calloc(1, sizeof (none_driver_t));
  if (!this)
    return NULL;

  this->xine = class->xine;
  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;

  this->sample_rate  = 0;

  this->ao_driver.get_capabilities    = ao_none_get_capabilities;
  this->ao_driver.get_property        = ao_none_get_property;
  this->ao_driver.set_property        = ao_none_set_property;
  this->ao_driver.open                = ao_none_open;
  this->ao_driver.num_channels        = ao_none_num_channels;
  this->ao_driver.bytes_per_frame     = ao_none_bytes_per_frame;
  this->ao_driver.delay               = ao_none_delay;
  this->ao_driver.write               = ao_none_write;
  this->ao_driver.close               = ao_none_close;
  this->ao_driver.exit                = ao_none_exit;
  this->ao_driver.get_gap_tolerance   = ao_none_get_gap_tolerance;
  this->ao_driver.control	      = ao_none_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

  none_class_t        *this;

  lprintf ("init class\n");

  this = calloc(1, sizeof (none_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "none";
  this->driver_class.description     = N_("xine dummy audio output plugin");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->config = xine->config;
  this->xine   = xine;

  return this;
}

static const ao_info_t ao_info_none = {
  -1 /* do not auto probe this one */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_NONE_IFACE_VERSION, "none", XINE_VERSION_CODE, &ao_info_none, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

