/*
 * Copyright (C) 2000-2012 the xine project
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
 * done by Daniel Mack <xine@zonque.org>
 * modified by Rich Wareham <richwareham@users.sourceforge.net>
 *
 * See http://developer.apple.com/technotes/tn2002/tn2091.html
 * and http://developer.apple.com/documentation/MusicAudio/Reference/CoreAudio/index.html
 * for conceptual documentation.
 *
 * The diffuculty here is that CoreAudio is pull-i/o while xine's internal
 * system works on push-i/o basis. So there is need of a buffer inbetween.
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

#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <AudioUnit/AUComponent.h>
#include <AudioUnit/AudioUnitProperties.h>
#include <AudioUnit/AudioUnitParameters.h>
#include <AudioUnit/AudioOutputUnit.h>
#include <CoreServices/CoreServices.h>

#define AO_OUT_COREAUDIO_IFACE_VERSION 9

#define GAP_TOLERANCE        AO_MAX_GAP
#define BUFSIZE              30720
/* Number of seconds to wait for buffered data to arrive/be used
 * before giving up. */
#define BUFFER_TIMEOUT       1

typedef struct coreaudio_driver_s {

  ao_driver_t    ao_driver;

  xine_t        *xine;

  int            capabilities;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  Component      au_component;
  Component      converter_component;

  AudioUnit      au_unit;
  AudioUnit      converter_unit;

  uint8_t        buf[BUFSIZE];
  uint32_t       buf_head;
  uint32_t       last_block_size;
  uint32_t       buffered;

  int            mute;
  Float32        pre_mute_volume;

  pthread_mutex_t mutex;
  pthread_cond_t  buffer_ready_for_reading;
  pthread_cond_t  buffer_ready_for_writing;
} coreaudio_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;

  config_values_t      *config;
  xine_t               *xine;
} coreaudio_class_t;

inline void set_to_future(struct timespec *spec);

inline void set_to_future(struct timespec *spec) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  spec->tv_sec = tv.tv_sec + BUFFER_TIMEOUT;
  spec->tv_nsec = tv.tv_usec * 1000;
}

/* this function is called every time the CoreAudio sytem wants us to
 * supply some data */
static OSStatus ao_coreaudio_render_proc (coreaudio_driver_t *this,
                                          AudioUnitRenderActionFlags *ioActionFlags,
                                          const AudioTimeStamp *inTimeStamp,
                                          unsigned int inBusNumber,
                                          unsigned int inNumberFrames,
                                          AudioBufferList * ioData) {
    int32_t i = 0;
    int32_t buffer_progress = 0;
    int32_t buffer_size = 0;
    int32_t chunk_size = 0;
    int32_t req_size = 0;
    struct timespec future;

    this->buffered = 0;

    while(i < ioData->mNumberBuffers) {
      buffer_size = ioData->mBuffers[i].mDataByteSize;

      pthread_mutex_lock (&this->mutex);
      if(this->buf_head < ((BUFSIZE) >> 2)) {
        set_to_future(&future);
        if(pthread_cond_timedwait
             (&this->buffer_ready_for_reading, &this->mutex, &future) == ETIMEDOUT)
        {
          /* Timed out, give up and fill remainder with silence. */
          while(i < ioData->mNumberBuffers) {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
            i++;
          }
          pthread_mutex_unlock (&this->mutex);
          return noErr;
        }
      }

      if(this->buf_head < buffer_size - buffer_progress) {
        chunk_size = this->buf_head;
      } else {
        chunk_size = buffer_size - buffer_progress;
      }

      xine_fast_memcpy (ioData->mBuffers[i].mData, this->buf, chunk_size);
      if(chunk_size < this->buf_head) {
        memmove(this->buf, &(this->buf[chunk_size]), this->buf_head - chunk_size);
      }
      this->buf_head -= chunk_size;
      buffer_progress += chunk_size;
      this->buffered += chunk_size;
      req_size += chunk_size;

      if(this->buf_head < ((BUFSIZE) >> 2)) {
        pthread_cond_broadcast (&this->buffer_ready_for_writing);
      }

      pthread_mutex_unlock (&this->mutex);

      if(buffer_progress == buffer_size) {
        i++;
        buffer_progress = 0;
      }
    }

    this->last_block_size = req_size;

    return noErr;
}

/*
 * open the audio device for writing to
 */
static int ao_coreaudio_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  unsigned int err;
  /* CoreAudio and AudioUnit related stuff */
  AURenderCallbackStruct input;
  AudioStreamBasicDescription format;
  AudioUnitConnection connection;
  ComponentDescription desc;

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  }

  this->sample_rate = rate;
  this->bits_per_sample = bits;
  this->capabilities = AO_CAP_16BITS | AO_CAP_MODE_STEREO | AO_CAP_MIXER_VOL;
  this->bytes_per_frame = this->num_channels * (bits / 8);
  this->buf_head = 0;
  this->last_block_size = 0;
  this->buffered = 0;
  pthread_mutex_init (&this->mutex, NULL);
  pthread_cond_init (&this->buffer_ready_for_reading, NULL);
  pthread_cond_init (&this->buffer_ready_for_writing, NULL);

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
           "audio_coreaudio_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  /* find an audio output unit */
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  desc.componentFlags = 0;
  desc.componentFlagsMask = 0;

  this->au_component = FindNextComponent (NULL, &desc);

  if (this->au_component == NULL) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: Unable to find a usable audio output unit component\n");
      return 0;
  }

  OpenAComponent (this->au_component, &this->au_unit);

  /* find a converter unit */
  desc.componentType = kAudioUnitType_FormatConverter;
  desc.componentSubType = kAudioUnitSubType_AUConverter;

  this->converter_component = FindNextComponent (NULL, &desc);

  if (this->converter_component == NULL) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: Unable to find a usable audio converter unit component\n");
      return 0;
  }

  OpenAComponent (this->converter_component, &this->converter_unit);

  /* set up the render procedure */
  input.inputProc = (AURenderCallback) ao_coreaudio_render_proc;
  input.inputProcRefCon = this;

  AudioUnitSetProperty (this->converter_unit,
                        kAudioUnitProperty_SetRenderCallback,
                        kAudioUnitScope_Input,
                        0, &input, sizeof(input));

  /* connect the converter unit to the audio output unit */
  connection.sourceAudioUnit = this->converter_unit;
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;
  AudioUnitSetProperty (this->au_unit,
                        kAudioUnitProperty_MakeConnection,
                        kAudioUnitScope_Input, 0,
                        &connection, sizeof(connection));

  /* set up the audio format we want to use */
  format.mSampleRate   = rate;
  format.mFormatID     = kAudioFormatLinearPCM;
  format.mFormatFlags  = kLinearPCMFormatFlagIsSignedInteger
#ifdef WORDS_BIGENDIAN
                       | kLinearPCMFormatFlagIsBigEndian
#endif
                       | kLinearPCMFormatFlagIsPacked;
  format.mBitsPerChannel   = this->bits_per_sample;
  format.mChannelsPerFrame = this->num_channels;
  format.mBytesPerFrame    = this->bytes_per_frame;
  format.mFramesPerPacket  = 1;
  format.mBytesPerPacket   = format.mBytesPerFrame;

  AudioUnitSetProperty (this->converter_unit,
                        kAudioUnitProperty_StreamFormat,
                        kAudioUnitScope_Input,
                        0, &format, sizeof (format));

  /* boarding completed, now initialize and start the units... */
  err = AudioUnitInitialize (this->converter_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioUnitInitialize(converter_unit)\n");
      return 0;
  }

  err = AudioUnitInitialize (this->au_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioUnitInitialize(au_unit)\n");
      return 0;
  }

  err = AudioOutputUnitStart (this->au_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioOutputUnitStart(au_unit)\n");
      return 0;
  }

  return rate;
}


static int ao_coreaudio_num_channels(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
    return this->num_channels;
}

static int ao_coreaudio_bytes_per_frame(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_coreaudio_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_coreaudio_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  int remaining_bytes;

  /* In total we want to write num_frames * this->bytes_per_frame */
  remaining_bytes = num_frames * this->bytes_per_frame;

  while(remaining_bytes > 0) {
    int32_t chunk_size;
    struct timespec future;

    pthread_mutex_lock (&this->mutex);
    if(this->buf_head > ((3 * BUFSIZE)>>2)) {
      set_to_future(&future);
      if(pthread_cond_timedwait
           (&this->buffer_ready_for_writing, &this->mutex, &future) == ETIMEDOUT)
      {
        /* Timed out, give up. */
        pthread_mutex_unlock (&this->mutex);
        return 0;
      }
    }

    /* Write as many bytes as possible from buf_head -> end of buffer */
    if(remaining_bytes > BUFSIZE - this->buf_head) {
      chunk_size = BUFSIZE - this->buf_head;
    } else {
      chunk_size = remaining_bytes;
    }

    xine_fast_memcpy(&(this->buf[this->buf_head]), data, chunk_size);
    this->buf_head += chunk_size;
    remaining_bytes -= chunk_size;

    if(this->buf_head > 0) {
      pthread_cond_broadcast (&this->buffer_ready_for_reading);
    }

    pthread_mutex_unlock (&this->mutex);
  }

  return 1;
}


static int ao_coreaudio_delay (ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return (this->last_block_size + this->buffered + this->buf_head)
          / this->bytes_per_frame;
}

static void ao_coreaudio_close(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  if (this->au_unit) {
      AudioOutputUnitStop (this->au_unit);
      AudioUnitUninitialize (this->au_unit);
      /* contrary to some of Apple's documentation, the function to close a
       * component is called CloseComponent, not CloseAComponent */
      CloseComponent (this->au_unit);
      this->au_unit = 0;
  }

  if (this->converter_unit) {
      AudioUnitUninitialize (this->converter_unit);
      CloseComponent (this->converter_unit);
      this->converter_unit = 0;
  }

  if (this->au_component) {
      this->au_component = NULL;
  }

  if (this->converter_component) {
      this->converter_component = NULL;
  }

  pthread_mutex_destroy (&this->mutex);
  pthread_cond_destroy (&this->buffer_ready_for_reading);
  pthread_cond_destroy (&this->buffer_ready_for_writing);
}

static uint32_t ao_coreaudio_get_capabilities (ao_driver_t *this_gen) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_coreaudio_exit(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  ao_coreaudio_close(this_gen);

  free (this);
}

static int ao_coreaudio_get_property (ao_driver_t *this_gen, int property) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  Float32 val;

  switch(property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
	if(!(this->mute)) {
	  AudioUnitGetParameter (this->au_unit,
                               kHALOutputParam_Volume,
                               kAudioUnitScope_Output,
                               0, &val);
	} else {
	  val = this->pre_mute_volume;
	}
        return (int) (val * 12);
	break;
    case AO_PROP_MUTE_VOL:
	return this->mute;
	break;
  }

  return 0;
}

static int ao_coreaudio_set_property (ao_driver_t *this_gen, int property, int value) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  Float32 val;

  switch(property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
        if(!this->mute) {
          val = value / 12.0;
          AudioUnitSetParameter (this->au_unit,
                          kHALOutputParam_Volume,
                          kAudioUnitScope_Output,
                          0, val, 0);
        }
        return value;
	break;
    case AO_PROP_MUTE_VOL:
	if(value) {
          /* Should mute */
          if(!(this->mute)) {
            AudioUnitGetParameter (this->au_unit,
                            kHALOutputParam_Volume,
                            kAudioUnitScope_Output,
                            0, &(this->pre_mute_volume));

            AudioUnitSetParameter (this->au_unit,
                            kHALOutputParam_Volume,
                            kAudioUnitScope_Output,
                            0, 0, 0);

            this->mute = 1;
          }
        } else {
          /* Should un-mute */
          if(this->mute) {
            AudioUnitSetParameter (this->au_unit,
                            kHALOutputParam_Volume,
                            kAudioUnitScope_Output,
                            0, this->pre_mute_volume, 0);

            this->mute = 0;
          }
	}
	return value;
	break;
  }

  return ~value;
}

static int ao_coreaudio_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
	AudioOutputUnitStop (this->au_unit);
    break;

  case AO_CTRL_PLAY_RESUME:
	AudioOutputUnitStart (this->au_unit);
    break;

  case AO_CTRL_FLUSH_BUFFERS:
        AudioUnitReset (this->au_unit, kAudioUnitScope_Input, 0);
        this->last_block_size = 0;
        this->buf_head = 0;
    break;
  }

  return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen,
                                 const void *data) {

  coreaudio_class_t     *class = (coreaudio_class_t *) class_gen;
  /* config_values_t *config = class->config; */
  coreaudio_driver_t    *this;

  lprintf ("open_plugin called\n");

  this = calloc(1, sizeof (coreaudio_driver_t));
  if (!this)
    return NULL;

  this->xine = class->xine;
  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;

  this->sample_rate  = 0;
  this->mute = 0;
  this->pre_mute_volume = 0;

  this->ao_driver.get_capabilities    = ao_coreaudio_get_capabilities;
  this->ao_driver.get_property        = ao_coreaudio_get_property;
  this->ao_driver.set_property        = ao_coreaudio_set_property;
  this->ao_driver.open                = ao_coreaudio_open;
  this->ao_driver.num_channels        = ao_coreaudio_num_channels;
  this->ao_driver.bytes_per_frame     = ao_coreaudio_bytes_per_frame;
  this->ao_driver.delay               = ao_coreaudio_delay;
  this->ao_driver.write               = ao_coreaudio_write;
  this->ao_driver.close               = ao_coreaudio_close;
  this->ao_driver.exit                = ao_coreaudio_exit;
  this->ao_driver.get_gap_tolerance   = ao_coreaudio_get_gap_tolerance;
  this->ao_driver.control             = ao_coreaudio_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

  coreaudio_class_t        *this;

  lprintf ("init class\n");

  this = calloc(1, sizeof (coreaudio_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "coreaudio";
  this->driver_class.description     = N_("xine output plugin for Coreaudio/Mac OS X");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->config = xine->config;
  this->xine   = xine;

  return this;
}

static const ao_info_t ao_info_coreaudio = {
  1
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_COREAUDIO_IFACE_VERSION, "coreaudio", XINE_VERSION_CODE, &ao_info_coreaudio, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

