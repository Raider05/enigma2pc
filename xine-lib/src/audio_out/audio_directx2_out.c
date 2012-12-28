/*
 * Copyright (C) 2004-2005 the xine project
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
 *
 * xine audio output plugin using DirectX
 *
 * Implementation:
 *   - this version contains service thread which starts and stops playback
 *     according to the data availability
 *   - it uses the ring buffer offered by DirectSound API
 *   - formula for volume level is deduced according to authors ears :-)
 *
 * Hacker notes:
 *   - always lock the mutex before calling audio_* functions
 *
 * Authors:
 *   - Frantisek Dvorak <valtri@atlas.cz>
 *     - Original version with slotted ring buffer
 *   - Matthias Ringald <mringwal@inf.ethz.ch>
 *     - non-slotted simpler version for ring buffer handling
 *
 * Inspiration:
 *   - mplayer for workarounding -lguid idea
 *   - DirectX 7 documentation
 *
 * License:
 *  - dual GPL/LGPL (LGPL for non xine-specific part)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <dsound.h>


#define LOG_MODULE "audio_directx2_out"
#define LOG_VERBOSE
/*
 #define LOG
 */

#include <xine/xine_internal.h>
#include <xine/audio_out.h>


#define AO_OUT_DIRECTX2_IFACE_VERSION 9

/*
 * If GAP_TOLERANCE is lower than AO_MAX_GAP, xine will
 * try to smooth playback without skipping frames or
 * inserting silence.
 */
#define GAP_TOLERANCE        (AO_MAX_GAP/3)

/*
 * buffer size in miliseconds
 * (one second takes 11-192 KB)
 */
#define BUFFER_MS 1000

/*
 * buffer below this threshold is considered a buffer underrun
 */
#define BUFFER_MIN_MS 200

/*
 * base power factor for volume remapping
 */
#define FACTOR 60.0

/*
 * buffer handler status
 */
#define STATUS_START 0
#define STATUS_WAIT 1
#define STATUS_RUNNING 2


#define PRIdword "lu"
#define PRIsizet "u"


typedef struct {
  audio_driver_class_t driver_class;
  xine_t *xine;
} dx2_class_t;


typedef struct {
  ao_driver_t ao_driver;
  dx2_class_t *class;

  LPDIRECTSOUND ds;                /* DirectSound device */
  LPDIRECTSOUNDBUFFER dsbuffer;    /* DirectSound buffer */

  size_t buffer_size;              /* size of the buffer */
  size_t write_pos;                /* positition in ring buffer for writing*/

  int status;                      /* current status of the driver */
  int paused;                      /* paused mode */
  int finished;                    /* driver finished */
  int failed;                      /* don't open modal dialog again */

  uint32_t bits;
  uint32_t rate;
  uint32_t frame_size;
  uint32_t capabilities;
  int channels;
  int volume;
  int muted;

  pthread_t buffer_service;        /* service thread for operating with DSB */
  pthread_cond_t data_cond;        /* signals on data */
  pthread_mutex_t data_mutex;      /* data lock */
} dx2_driver_t;


/*****************************************************************************
 * DirectDraw GUIDs.
 * Defining them here allows us to get rid of the dxguid library during
 * the linking stage.
 *****************************************************************************/
static const GUID xine_IID_IDirectSoundNotify = {
0xB0210783, 0x89CD, 0x11D0, {0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16}
};
#ifdef IID_IDirectSoundNotify
#  undef IID_IDirectSoundNotify
#endif
#define IID_IDirectSoundNotify xine_IID_IDirectSoundNotify



/* popup a dialog with error */
static void XINE_FORMAT_PRINTF(1, 2)
error_message(const char *fmt, ...) {
  char message[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap);

  MessageBox(0, message, _("Error"), MB_ICONERROR | MB_OK | MB_APPLMODAL);
}


/* description of given error */
static char *dsound_strerror(HRESULT err) {
  switch(err) {
    case DS_OK: return _("success");
#ifdef DSERR_ACCESSDENIED
    case DSERR_ACCESSDENIED: return _("access denied");
#endif
    case DSERR_ALLOCATED: return _("resource is already in use");
    case DSERR_ALREADYINITIALIZED: return _("object was already initialized");
    case DSERR_BADFORMAT: return _("specified wave format is not supported");
    case DSERR_BUFFERLOST: return _("memory buffer has been lost and must be restored");
    case DSERR_CONTROLUNAVAIL: return _("requested buffer control is not available");
    case DSERR_GENERIC: return _("undetermined error inside DirectSound subsystem");
#ifdef DSERR_HWUNAVAIL
    case DSERR_HWUNAVAIL: return _("DirectSound hardware device is unavailable");
#endif
    case DSERR_INVALIDCALL: return _("function is not valid for the current state of the object");
    case DSERR_INVALIDPARAM: return _("invalid parameter was passed");
    case DSERR_NOAGGREGATION: return _("object doesn't support aggregation");
    case DSERR_NODRIVER: return _("no sound driver available for use");
    case DSERR_NOINTERFACE: return _("requested COM interface not available");
    case DSERR_OTHERAPPHASPRIO: return _("another application has a higher priority level");
    case DSERR_OUTOFMEMORY: return _("insufficient memory");
    case DSERR_PRIOLEVELNEEDED: return _("low priority level for this function");
    case DSERR_UNINITIALIZED: return _("DirectSound wasn't initialized");
    case DSERR_UNSUPPORTED: return _("function is not supported");
    default: return _("unknown error");
  }
}


/* create direct sound object */
static LPDIRECTSOUND dsound_create() {
  LPDIRECTSOUND ds;

  if (DirectSoundCreate(NULL, &ds, NULL) != DS_OK) {
    error_message(_("Unable to create direct sound object."));
    return NULL;
  }

  if (IDirectSound_SetCooperativeLevel(ds, GetDesktopWindow(), DSSCL_PRIORITY) != DS_OK) {
    IDirectSound_Release(ds);
    error_message(_("Could not set direct sound cooperative level."));
    return NULL;
  }

  return ds;
}


/* destroy direct sound object */
static void dsound_destroy(LPDIRECTSOUND ds) {
  IDirectSound_Release(ds);
}


/* fill out wave format header */
static void dsound_fill_wfx(WAVEFORMATEX *wfx, uint32_t bits, uint32_t rate, int channels, size_t frame_size) {
  memset(wfx, 0, sizeof(wfx));
  wfx->wFormatTag = WAVE_FORMAT_PCM;
  wfx->nChannels = channels;
  wfx->nSamplesPerSec = rate;
  wfx->wBitsPerSample = (WORD)bits;
  wfx->nBlockAlign = frame_size;
  wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
}


/* fill out buffer description structure */
static void dsound_fill_desc(DSBUFFERDESC *desc, DWORD flags, DWORD buffer_size, WAVEFORMATEX *wfx) {
  memset(desc, 0, sizeof(DSBUFFERDESC));
  desc->dwSize = sizeof(DSBUFFERDESC);
  desc->dwFlags = flags;
  desc->dwBufferBytes = buffer_size;
  desc->lpwfxFormat = wfx;
}


/* send exit signal to the audio thread */
static void audio_thread_exit(dx2_driver_t *this) {
  this->finished = 1;
  pthread_cond_signal(&this->data_cond);
}


/* check for error, log it and pop up dialog */
static void audio_error(dx2_driver_t *this, HRESULT err, char *msg) {
  xine_log(this->class->xine, XINE_LOG_MSG, LOG_MODULE ": %s: %s\n", msg, dsound_strerror(err));
  if (!this->failed) {
    error_message("%s: %s", msg, dsound_strerror(err));
    this->failed = 1;
  }
  if (this->status != STATUS_START) audio_thread_exit(this);
}


/* create direct sound buffer */
static int audio_create_buffers(dx2_driver_t *this) {
  DSBUFFERDESC desc;
  WAVEFORMATEX wfx;
  DWORD flags;
  HRESULT err;
  size_t buffer_size;

  buffer_size = this->rate * BUFFER_MS / 1000 * this->frame_size;
  if (buffer_size > DSBSIZE_MAX) buffer_size = DSBSIZE_MAX;
  if (buffer_size < DSBSIZE_MIN) buffer_size = DSBSIZE_MIN;
  this->buffer_size = buffer_size;

  flags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;
  dsound_fill_wfx(&wfx, this->bits, this->rate, this->channels, this->frame_size);
  dsound_fill_desc(&desc, flags, this->buffer_size, &wfx);

  if ((err = IDirectSound_CreateSoundBuffer(this->ds, &desc, &this->dsbuffer, NULL)) != DS_OK) {
    audio_error(this, err, _("Unable to create secondary direct sound buffer"));
    return 0;
  }

  lprintf("created direct sound buffer, size = %u\n", this->buffer_size);
  return 1;
}


/* destroy the sound buffer */
static void audio_destroy_buffers(dx2_driver_t *this) {
  IDirectSoundBuffer_Release(this->dsbuffer);
}


/* start playback */
static int audio_play(dx2_driver_t *this) {
  HRESULT err;

  if ((err = IDirectSoundBuffer_Play(this->dsbuffer, 0, 0, DSBPLAY_LOOPING)) != DS_OK) {
    audio_error(this, err, _("Couldn't play sound buffer"));
    return 0;
  }
  return 1;
}


/* stop playback */
static int audio_stop(dx2_driver_t *this) {
  HRESULT err;

  if ((err = IDirectSoundBuffer_Stop(this->dsbuffer)) != DS_OK) {
    audio_error(this, err, _("Couldn't stop sound buffer"));
    return 0;
  }
  return 1;
}


/* get current playback position in the ring buffer */
static int audio_tell(dx2_driver_t *this, size_t *pos) {
  DWORD err;
  DWORD play_pos;

  if ((err = IDirectSoundBuffer_GetCurrentPosition(this->dsbuffer, &play_pos, NULL)) != DS_OK) {
    audio_error(this, err, _("Can't get buffer position"));
    return 0;
  }
  *pos = play_pos;

  return 1;
}


/* set playback position in the ring buffer */
static int audio_seek(dx2_driver_t *this, size_t pos) {
  DWORD err;

  if ((err = IDirectSoundBuffer_SetCurrentPosition(this->dsbuffer, pos)) != DS_OK) {
    audio_error(this, err, _("Can't set buffer position"));
    return 0;
  }

  return 1;
}


/* flush audio buffers */
static int audio_flush(dx2_driver_t *this) {
  this->status = STATUS_WAIT;
  this->write_pos = 0;
  return audio_seek(this, 0);
}


/*
 * set the volume
 *
 * DirecSound can only lower the volume by software way.
 * Unit is dB, value is always negative or zero.
 */
static int audio_set_volume(dx2_driver_t *this, int volume) {
  HRESULT err;
  LONG value;

  value = DSBVOLUME_MIN * (pow(FACTOR, 1 - volume / 100.0) - 1) / (FACTOR - 1);
  if (value < DSBVOLUME_MIN) value = DSBVOLUME_MIN;
  else if (value > DSBVOLUME_MAX) value = DSBVOLUME_MAX;
  lprintf("Setting sound to %d%% (%ld dB)\n", volume, value);
  if ((err = IDirectSoundBuffer_SetVolume(this->dsbuffer, value) != DS_OK)) {
    audio_error(this, err, _("Can't set sound volume"));
    return 0;
  }

  return 1;
}


/* add given data into the ring buffer */
static int audio_fill(dx2_driver_t *this, char *data, size_t size) {
  DWORD size1, size2;
  void *ptr1, *ptr2;
  HRESULT err;

  /* lock a part of the buffer, begin position on free space */
  err = IDirectSoundBuffer_Lock(this->dsbuffer, this->write_pos, size, &ptr1, &size1, &ptr2, &size2, 0);
  /* try to restore the buffer, if necessary */
  if (err == DSERR_BUFFERLOST) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": buffer lost, trying to restore\n"));
    IDirectSoundBuffer_Restore(this->dsbuffer);
  err = IDirectSoundBuffer_Lock(this->dsbuffer, this->write_pos, size, &ptr1, &size1, &ptr2, &size2, 0);  }
  if (err != DS_OK) {
    audio_error(this, err, _("Couldn't lock direct sound buffer"));
    return 0;
  }

  _x_assert(size == size1 + size2);
  if (ptr1 && size1) xine_fast_memcpy(ptr1, data, size1);
  if (ptr2 && size2) xine_fast_memcpy(ptr2, data + size1, size2);

  // this->read_size += size;
  this->write_pos = (this->write_pos + size ) % this->buffer_size;
  lprintf("size %u, write_pos %u\n", size, this->write_pos);

  if ((err = IDirectSoundBuffer_Unlock(this->dsbuffer, ptr1, size1, ptr2, size2)) != DS_OK) {
    audio_error(this, err, _("Couldn't unlock direct sound buffer"));
    return 0;
  }

  return 1;
}


/* transform given mode the number of channels */
static int mode2channels(uint32_t mode) {
  int channels;

  switch(mode) {
    case AO_CAP_MODE_MONO:
      channels = 1;
      break;

    case AO_CAP_MODE_STEREO:
      channels = 2;
      break;

    case AO_CAP_MODE_4CHANNEL:
      channels = 4;
      break;

    case AO_CAP_MODE_5CHANNEL:
      channels = 5;
      break;

    case AO_CAP_MODE_5_1CHANNEL:
      channels = 6;
      break;

    default:
      return 0;
  }

  return channels;
}


/* test the capability on given buffer */
static int test_capability(LPDIRECTSOUNDBUFFER buffer, uint32_t bits, uint32_t rate, int mode) {
  WAVEFORMATEX wfx;
  int channels;

  channels = mode2channels(mode);
  if (!channels) return 0;

  dsound_fill_wfx(&wfx, bits, rate, channels, (bits >> 3) * channels);
  if (IDirectSoundBuffer_SetFormat(buffer, &wfx) != DS_OK) {
    lprintf("mode %d, bits %" PRIu32 " not supported\n", mode, bits);
    return 0;
  }

  lprintf("mode %d, bits %" PRIu32 " supported\n", mode, bits);

  return 1;
}


/*
 * test capabilities of driver before opening
 *
 * Passed only 8 bit and 16 bit with mono or stereo.
 */
static int test_capabilities(dx2_driver_t *this) {
  struct {
    uint32_t bits;
    uint32_t rate;
    uint32_t mode;
    uint32_t caps;
  } tests[] = {
    {8, 44100, AO_CAP_MODE_MONO, AO_CAP_8BITS | AO_CAP_MODE_MONO},
    {8, 44100, AO_CAP_MODE_STEREO, AO_CAP_8BITS | AO_CAP_MODE_STEREO},
    {16, 44100, AO_CAP_MODE_MONO, AO_CAP_16BITS | AO_CAP_MODE_MONO},
    {16, 44100, AO_CAP_MODE_STEREO, AO_CAP_16BITS | AO_CAP_MODE_STEREO},
    {16, 44100, AO_CAP_MODE_4CHANNEL, AO_CAP_16BITS | AO_CAP_MODE_4CHANNEL},
    {16, 44100, AO_CAP_MODE_5CHANNEL, AO_CAP_16BITS | AO_CAP_MODE_5CHANNEL},
    {16, 44100, AO_CAP_MODE_5_1CHANNEL, AO_CAP_16BITS | AO_CAP_MODE_5_1CHANNEL},
    {24, 44100, AO_CAP_MODE_STEREO, AO_CAP_24BITS | AO_CAP_MODE_STEREO},
    {32, 44100, AO_CAP_MODE_STEREO, AO_CAP_FLOAT32 | AO_CAP_MODE_STEREO},
    {0, 0, 0, 0},
  };
  LPDIRECTSOUNDBUFFER buffer;
  DSBUFFERDESC desc;
  int i;

  /* create temporary primary sound buffer */
  dsound_fill_desc(&desc, DSBCAPS_PRIMARYBUFFER, 0, NULL);
  if (IDirectSound_CreateSoundBuffer(this->ds, &desc, &buffer, NULL) != DS_OK) {
    error_message(_("Unable to create primary direct sound buffer."));
    return 0;
  }

  /* test capabilities */
  this->capabilities = 0;
  i = 0;
  while (tests[i].bits) {
    if (test_capability(buffer, tests[i].bits, tests[i].rate, tests[i].mode)) this->capabilities |= tests[i].caps;
    i++;
  }
  lprintf("result capabilities: 0x08%" PRIX32 "\n", this->capabilities);

  IDirectSoundBuffer_Release(buffer);
  return 1;
}


/* size of free space in the ring buffer */
static size_t buffer_free_size(dx2_driver_t *this) {

  int ret;
  size_t play_pos;
	size_t free_space;

  // get current play pos
	ret = audio_tell(this, &play_pos);
	if (!ret)
		return 0;

	// calc free space (-1)
	free_space = (this->buffer_size + play_pos - this->write_pos - 1) % this->buffer_size;

	return free_space;
}


/* size of occupied space in the ring buffer */
static size_t buffer_occupied_size(dx2_driver_t *this) {
  int ret;
  size_t play_pos;
	size_t used_space;

  // get current play pos
	ret = audio_tell(this, &play_pos);
	if (!ret) return 0;

	// calc used space
	used_space = (this->buffer_size + this->write_pos - play_pos) % this->buffer_size;

	return used_space;
}


/* service thread working with direct sound buffer */
static void *buffer_service(void *data) {
  dx2_driver_t *this = (dx2_driver_t *)data;
  size_t buffer_min;
  size_t data_in_buffer;

  /* prepare empty buffer */
  audio_flush(this);

  /* prepare min buffer fill */
  buffer_min = BUFFER_MIN_MS * this->rate / 1000 * this->frame_size;

  /* we live! */
  pthread_mutex_lock(&this->data_mutex);
  pthread_cond_signal(&this->data_cond);
  pthread_mutex_unlock(&this->data_mutex);

  while (!this->finished) {

    pthread_mutex_lock(&this->data_mutex);
    switch( this->status){

			case STATUS_WAIT:

        // pre: stop/buffer flushed
        lprintf("no data, sleeping...\n");
        pthread_cond_wait(&this->data_cond, &this->data_mutex);
        lprintf("woke up (write_pos=%d,free=%" PRIsizet")\n", this->write_pos, buffer_free_size(this));
        if (this->finished) goto finished;
        if (!audio_seek(this, 0)) goto fail;
        if (!this->paused) {
					if (!audio_play(this)) goto fail;
        }
        this->status = STATUS_RUNNING;
        pthread_mutex_unlock(&this->data_mutex);
				break;

      case STATUS_RUNNING:

        // check for buffer underrun
        data_in_buffer =  buffer_occupied_size(this);
        if ( data_in_buffer < buffer_min){
          xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": play cursor overran (data %u, min %u), flushing buffers\n"),
                   data_in_buffer, buffer_min);
          if (!audio_stop(this)) goto fail;
          if (!audio_flush(this)) goto fail;
        }
        pthread_mutex_unlock(&this->data_mutex);

        // just wait BUFFER_MIN_MS before next check
        xine_usec_sleep(BUFFER_MIN_MS * 1000);
				break;
    }
  }
  return NULL;

fail:
  this->finished = 1;
finished:
  pthread_mutex_unlock(&this->data_mutex);
  return NULL;
}


/* ---- driver functions ---- */

static uint32_t ao_dx2_get_capabilities(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  return this->capabilities;
}


static int ao_dx2_get_property(ao_driver_t *this_gen, int property) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  switch(property) {

    case AO_PROP_MIXER_VOL:
    case AO_PROP_PCM_VOL:
      return this->volume;

    case AO_PROP_MUTE_VOL:
      return this->muted;

    default:
      return 0;

  }
}


static int ao_dx2_set_property(ao_driver_t *this_gen, int property, int value) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  switch(property) {

    case AO_PROP_MIXER_VOL:
    case AO_PROP_PCM_VOL:
      lprintf("set volume to %d\n", value);
      pthread_mutex_lock(&this->data_mutex);
      if (!this->muted) {
        if (this->dsbuffer && !audio_set_volume(this, value)) return ~value;
      }
      this->volume = value;
      pthread_mutex_unlock(&this->data_mutex);
      break;

    case AO_PROP_MUTE_VOL:
      pthread_mutex_lock(&this->data_mutex);
      if (this->dsbuffer && !audio_set_volume(this, value ? 0 : this->volume)) return ~value;
      this->muted = value;
      pthread_mutex_unlock(&this->data_mutex);
      break;

    default:
      return ~value;

  }

  return value;
}


static int ao_dx2_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  lprintf("bits=%" PRIu32 ", rate=%" PRIu32 ", mode=%d\n", bits, rate, mode);

  if (rate < DSBFREQUENCY_MIN) rate = DSBFREQUENCY_MIN;
  if (rate > DSBFREQUENCY_MAX) rate = DSBFREQUENCY_MAX;

  this->bits = bits;
  this->rate = rate;
  if ((this->channels = mode2channels(mode)) == 0) return 0;
  this->frame_size = (this->bits >> 3) * this->channels;

  this->paused = 0;
  this->finished = 0;
  this->status = STATUS_START;

  if (!audio_create_buffers(this)) return 0;
  if (!audio_set_volume(this, this->volume)) goto fail_buffers;

  if (pthread_cond_init(&this->data_cond, NULL) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't create pthread condition: %s\n"), strerror(errno));
    goto fail_buffers;
  }
  if (pthread_mutex_init(&this->data_mutex, NULL) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't create pthread mutex: %s\n"), strerror(errno));
    goto fail_cond;
  }

  /* creating the service thread and waiting for its signal */
  pthread_mutex_lock(&this->data_mutex);
  if (pthread_create(&this->buffer_service, NULL, buffer_service, this) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't create buffer pthread: %s\n"), strerror(errno));
    goto fail_mutex;
  }
  pthread_cond_wait(&this->data_cond, &this->data_mutex);
  pthread_mutex_unlock(&this->data_mutex);

  return rate;

fail_mutex:
  pthread_mutex_unlock(&this->data_mutex);
  pthread_mutex_destroy(&this->data_mutex);
fail_cond:
  pthread_cond_destroy(&this->data_cond);
fail_buffers:
  audio_destroy_buffers(this);
  return 0;
}


static int ao_dx2_num_channels(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  lprintf("channels=%d\n", this->channels);

  return this->channels;
}


static int ao_dx2_bytes_per_frame(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  lprintf("frame_size=%d\n", this->frame_size);

  return this->frame_size;
}


static int ao_dx2_delay(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;
  int frames = 0;
  int ret;
  size_t play_pos;

  pthread_mutex_lock(&this->data_mutex);

  if (this->status != STATUS_RUNNING){
    frames = this->write_pos / this->frame_size;
  } else {
    ret = audio_tell(this, &play_pos);
    if (ret){
      frames = buffer_occupied_size(this) / this->frame_size;
    }
  }

  pthread_mutex_unlock(&this->data_mutex);

#ifdef LOG
  if ((rand() % 10) == 0)
    lprintf("frames=%d, play_pos=%" PRIdword ", write_pos=%u\n", frames, play_pos, this->write_pos);
#endif

  return frames;
}


static int ao_dx2_write(ao_driver_t *this_gen, int16_t* audio_data, uint32_t num_samples) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;
  size_t input_size;    /* used size of input data */
  size_t free_size;     /* size of the free space in the ring buffer */
  size_t size;          /* current block size */
  size_t read_pos;      /* position in the input */

  input_size = this->frame_size * num_samples;
  read_pos = 0;

  while (input_size && !this->finished) {
    pthread_mutex_lock(&this->data_mutex);
    while (((free_size = buffer_free_size(this)) == 0) && !this->finished) {
      lprintf("buffer full, waiting\n");
      pthread_mutex_unlock(&this->data_mutex);
      xine_usec_sleep(1000 * BUFFER_MS / 10);
      pthread_mutex_lock(&this->data_mutex);
    }
    if (free_size >= input_size) size = input_size;
    else size = free_size;

    if (!audio_fill(this, ((char *)audio_data) + read_pos, size)) {
      audio_thread_exit(this);
      pthread_mutex_unlock(&this->data_mutex);
      return 0;
    }
    pthread_mutex_unlock(&this->data_mutex);
    read_pos += size;
    input_size -= size;
  }

  /* signal, if are waiting and need wake up */
  if ((this->status == STATUS_WAIT) && (buffer_occupied_size(this) > BUFFER_MIN_MS * this->rate / 1000 * this->frame_size)) {
    lprintf("buffer ready, waking up\n");
    pthread_cond_signal(&this->data_cond);
  }

  return 1;
}


static void ao_dx2_close(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  lprintf("close plugin\n");

  pthread_mutex_lock(&this->data_mutex);
  audio_thread_exit(this);
  pthread_mutex_unlock(&this->data_mutex);
  if (pthread_join(this->buffer_service, NULL) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't destroy buffer pthread: %s\n"), strerror(errno));
    return;
  }
  lprintf("pthread joined\n");
  pthread_mutex_unlock(&this->data_mutex);

  if (pthread_cond_destroy(&this->data_cond) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't destroy pthread condition: %s\n"), strerror(errno));
  }
  if (pthread_mutex_destroy(&this->data_mutex) != 0) {
    xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": can't destroy pthread mutex: %s\n"), strerror(errno));
  }
  audio_destroy_buffers(this);
}


static void ao_dx2_exit(ao_driver_t *this_gen) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  lprintf("exit instance\n");

  dsound_destroy(this->ds);
  free(this);
}


static int ao_dx2_get_gap_tolerance(ao_driver_t *this_gen) {
  return GAP_TOLERANCE;
}


static int ao_dx2_control(ao_driver_t *this_gen, int cmd, ...) {
  dx2_driver_t *this = (dx2_driver_t *)this_gen;

  switch(cmd) {

    case AO_CTRL_PLAY_PAUSE:
      lprintf("control pause\n");
      pthread_mutex_lock(&this->data_mutex);
      if (!this->paused) {
        audio_stop(this);
        this->paused = 1;
      }
      pthread_mutex_unlock(&this->data_mutex);
      break;

    case AO_CTRL_PLAY_RESUME:
      lprintf("control resume\n");
      pthread_mutex_lock(&this->data_mutex);
      if (this->paused) {
        if (this->status != STATUS_WAIT) audio_play(this);
        this->paused = 0;
      }
      pthread_mutex_unlock(&this->data_mutex);
      break;

    case AO_CTRL_FLUSH_BUFFERS:
      lprintf("control flush\n");
      pthread_mutex_lock(&this->data_mutex);
      audio_stop(this);
      audio_flush(this);
      pthread_mutex_unlock(&this->data_mutex);
      break;

    default:
      xine_log(this->class->xine, XINE_LOG_MSG, _(LOG_MODULE ": unknown control command %d\n"), cmd);

  }

  return 0;
}


/* ---- class functions ---- */

static ao_driver_t *open_plugin(audio_driver_class_t *class_gen, const void *data) {
  dx2_class_t *class = (dx2_class_t *)class_gen;
  dx2_driver_t *this;

  lprintf("open plugin called\n");

  this = calloc(1, sizeof(dx2_driver_t));
  if (!this)
    return NULL;

  this->class = class;

  this->ao_driver.get_capabilities    = ao_dx2_get_capabilities;
  this->ao_driver.get_property        = ao_dx2_get_property;
  this->ao_driver.set_property        = ao_dx2_set_property;
  this->ao_driver.open                = ao_dx2_open;
  this->ao_driver.num_channels        = ao_dx2_num_channels;
  this->ao_driver.bytes_per_frame     = ao_dx2_bytes_per_frame;
  this->ao_driver.delay               = ao_dx2_delay;
  this->ao_driver.write               = ao_dx2_write;
  this->ao_driver.close               = ao_dx2_close;
  this->ao_driver.exit                = ao_dx2_exit;
  this->ao_driver.get_gap_tolerance   = ao_dx2_get_gap_tolerance;
  this->ao_driver.control	      = ao_dx2_control;

  this->volume = 100;
  this->muted = 0;

  this->failed = 0;

  if ((this->ds = dsound_create()) == NULL) {
    free(this);
    return NULL;
  }
  test_capabilities(this);

  return (ao_driver_t *)this;
}

static void *init_class(xine_t *xine, void *data) {
  dx2_class_t *this;

  lprintf("init class\n");

  this = calloc(1, sizeof(dx2_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "directx2";
  this->driver_class.description     = N_("second xine audio output plugin using directx");
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  this->xine = xine;

  return this;
}


static const ao_info_t ao_info_directx2 = {
  10
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  { PLUGIN_AUDIO_OUT, AO_OUT_DIRECTX2_IFACE_VERSION, "directx2", XINE_VERSION_CODE, &ao_info_directx2, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
