/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*- */

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
 * ao plugin for PulseAudio:
 * http://0pointer.de/lennart/projects/pulsaudio/
 *
 * Diego Petteno, Lennart Poettering
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
#include <pthread.h>

#include <pulse/pulseaudio.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include "bswap.h"

#define GAP_TOLERANCE        AO_MAX_GAP

typedef struct {
  audio_driver_class_t  driver_class;
  xine_t                      *xine;
} pulse_class_t;

typedef struct pulse_driver_s {
  ao_driver_t       ao_driver;
  xine_t           *xine;

  pulse_class_t    *pa_class;

  char             *host;    /*< The host to connect to */
  char             *sink;    /*< The sink to connect to */

  pa_threaded_mainloop *mainloop;  /*< Main event loop object */
  pa_context *context;             /*< Pulseaudio connection context */
  pa_stream  *stream;              /*< Pulseaudio playback stream object */

  pa_volume_t       swvolume;
  int muted;
  pa_cvolume        cvolume;

  int               capabilities;
  int               mode;

  uint32_t          sample_rate;
  uint32_t          num_channels;
  uint32_t          bits_per_sample;
  uint32_t          bytes_per_frame;

  int               volume_bool;

} pulse_driver_t;


/**
 * @brief Callback function called when the state of the context is changed
 * @param c Context which changed status
 * @param this_gen pulse_class_t pointer for the PulseAudio output class
 */
static void __xine_pa_context_state_callback(pa_context *c, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;

  switch (pa_context_get_state(c)) {

    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal(this->mainloop, 0);
      break;

    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

/**
 * @brief Callback function called when the state of the stream is changed
 * @param s Stream that changed status
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_stream_state_callback(pa_stream *s, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;

  switch (pa_stream_get_state(s)) {

    case PA_STREAM_READY:
    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
      pa_threaded_mainloop_signal(this->mainloop, 0);
      break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      break;
  }
}

/**
 * @brief Callback function called when PA asks for more audio data.
 * @param s Stream on which data is requested
 * @param nbytes the number of bytes PA requested
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_stream_request_callback(pa_stream *s, size_t nbytes, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when PA notifies about something
 * @param s Stream on which the notification happened
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_stream_notify_callback(pa_stream *s, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when PA completed an operation
 * @param ctx Context which operation has succeeded
 * @param nbytes the number of bytes PA requested
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_stream_success_callback(pa_stream *s, int success, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;

  if (!success)
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: stream operation failed: %s\n", pa_strerror(pa_context_errno(this->context)));

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when PA completed an operation
 * @param c Context on which operation has succeeded
 * @param nbytes the number of bytes PA requested
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_context_success_callback(pa_context *c, int success, void *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t*) this_gen;

  if (!success)
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: context operation failed: %s\n", pa_strerror(pa_context_errno(this->context)));

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when the information on the
 *        context's sink is retrieved.
 * @param ctx Context which operation has succeeded
 * @param info Structure containing the sink's information
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 *
 * This function saves the volume field of the passed structure to the
 * @c cvolume variable of the output instance and send an update volume
 * event to the frontend.
 */
static void __xine_pa_sink_info_callback(pa_context *c, const pa_sink_input_info *info,
                                         int is_last, void *userdata) {

  pulse_driver_t *const this = (pulse_driver_t *) userdata;

  if (is_last < 0) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Failed to get sink input info: %s\n",
             pa_strerror(pa_context_errno(this->context)));
    return;
  }

  if (!info)
      return;

  this->cvolume = info->volume;
  this->swvolume = pa_cvolume_avg(&info->volume);
#if PA_PROTOCOL_VERSION >= 11
  /* PulseAudio 0.9.7 and newer */
  this->muted = info->mute;
#else
  this->muted = pa_cvolume_is_muted (&this->cvolume);
#endif

  /* send update volume event to frontend */

  xine_event_t              event;
  xine_audio_level_data_t   data;
  xine_stream_t            *stream;
  xine_list_iterator_t      ite;

  data.right        = data.left = (int) (pa_sw_volume_to_linear(this->swvolume)*100);

  data.mute         = this->muted;

  event.type        = XINE_EVENT_AUDIO_LEVEL;
  event.data        = &data;
  event.data_length = sizeof(data);

  pthread_mutex_lock(&this->xine->streams_lock);
  for(ite = xine_list_front(this->xine->streams); ite; ite =
    xine_list_next(this->xine->streams, ite)) {
    stream = xine_list_get_value(this->xine->streams, ite);
    event.stream = stream;
    xine_event_send(stream, &event);
  }
  pthread_mutex_unlock(&this->xine->streams_lock);
}

/**
 * @brief Callback function called when the state of the daemon changes
 * @param c Context in which the state of the daemon changes
 * @param t Subscription event type
 * @param idx Index of the sink
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_context_subscribe_callback(pa_context *c,
    pa_subscription_event_type_t t, uint32_t idx, void *this_gen)
{
  pulse_driver_t * this = (pulse_driver_t*) this_gen;
  int index;

  if (this->stream == NULL)
    return;

  index = pa_stream_get_index(this->stream);

  if (index != idx)
    return;

  if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE)
    return;

  pa_operation *operation = pa_context_get_sink_input_info(
      this->context, index, __xine_pa_sink_info_callback, this);

  if (operation == NULL) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to get sink info: %s\n", pa_strerror(pa_context_errno (this->context)));
    return;
  }

  pa_operation_unref(operation);
}

static int connect_context(pulse_driver_t *this) {

  if (this->context && (pa_context_get_state(this->context) == PA_CONTEXT_FAILED ||
                        pa_context_get_state(this->context) == PA_CONTEXT_TERMINATED)) {
    pa_context_unref(this->context);
    this->context = NULL;
  }

  if (!this->context) {
    char fn[XINE_PATH_MAX];
    const char *p;

    if (pa_get_binary_name(fn, sizeof(fn)))
      p = pa_path_get_filename(fn);
    else
      p = "Xine";

    this->context = pa_context_new(pa_threaded_mainloop_get_api(this->mainloop), p);
    _x_assert(this->context);

    pa_context_set_state_callback(this->context, __xine_pa_context_state_callback, this);

    /* set subscribe callback (for volume change information) */

    pa_context_set_subscribe_callback(this->context, __xine_pa_context_subscribe_callback, this);
  }

  if (pa_context_get_state(this->context) == PA_CONTEXT_UNCONNECTED) {

    if (pa_context_connect(this->context, this->host, 0, NULL) < 0) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to connect context object %s\n", pa_strerror(pa_context_errno(this->context)));
      return -1;
    }
  }

  for (;;) {
    pa_context_state_t state = pa_context_get_state(this->context);

    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to connect context object: %s\n", pa_strerror(pa_context_errno(this->context)));
      return -1;
    }

    if (state == PA_CONTEXT_READY)
      break;

    pa_threaded_mainloop_wait(this->mainloop);
  }

  /* subscribe to sink input events (for volume change information) */

  pa_operation *operation = pa_context_subscribe(this->context,
    PA_SUBSCRIPTION_MASK_SINK_INPUT,
    __xine_pa_context_success_callback, this);

  if (operation == NULL) {
     xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to enable event notification: %s\n", pa_strerror(pa_context_errno(this->context)));
     return -1;
  }

  return 0;
}

/*
 * open the audio device for writing to
 */
static int ao_pulse_open(ao_driver_t *this_gen,
                         uint32_t bits, uint32_t rate, int mode)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  pa_sample_spec ss;
  pa_channel_map cm;

#if PA_CHECK_VERSION(1,0,0)
  pa_encoding_t encoding = PA_ENCODING_INVALID;
#endif

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
           "audio_pulse_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: unsupported mode %08x\n", mode);
    return 0;
  }

  pa_threaded_mainloop_lock(this->mainloop);

  if (this->stream) {

    if (mode == this->mode && rate == this->sample_rate &&
        bits == this->bits_per_sample) {

      pa_threaded_mainloop_unlock(this->mainloop);
      return this->sample_rate;
    }

    pa_stream_disconnect(this->stream);
    pa_stream_unref(this->stream);
    this->stream = NULL;
  }

  this->mode                   = mode;
  this->sample_rate            = rate;
  this->bits_per_sample        = bits;
  this->num_channels           = _x_ao_mode2channels( mode );
  this->bytes_per_frame        = (this->bits_per_sample*this->num_channels)/8;

  ss.rate = rate;
  ss.channels = this->num_channels;
  switch (bits) {
    case 8:
      ss.format = PA_SAMPLE_U8;
      break;
    case 16:
      ss.format = PA_SAMPLE_S16NE;
      break;
    case 32:
      ss.format = PA_SAMPLE_FLOAT32NE;
      break;
    default:
      _x_assert(!"Should not be reached");
  }

#if PA_CHECK_VERSION(1,0,0)
  if (mode == AO_CAP_MODE_A52 || mode == AO_CAP_MODE_AC5) {
    this->num_channels = 2;
    this->bytes_per_frame = (this->bits_per_sample*this->num_channels)/8;
    ss.channels = 2;
    encoding = PA_ENCODING_AC3_IEC61937;
  }
#endif

  if (!pa_sample_spec_valid(&ss)) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Invalid sample spec\n");
    goto fail;
  }

  cm.channels = ss.channels;

  switch (mode) {
    case AO_CAP_MODE_MONO:
      cm.map[0] = PA_CHANNEL_POSITION_MONO;
      _x_assert(cm.channels == 1);
      break;

    case AO_CAP_MODE_STEREO:
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      cm.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
      cm.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
      _x_assert(cm.channels == 2);
      break;

    case AO_CAP_MODE_4CHANNEL:
      cm.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
      cm.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
      cm.map[2] = PA_CHANNEL_POSITION_REAR_LEFT;
      cm.map[3] = PA_CHANNEL_POSITION_REAR_RIGHT;
      _x_assert(cm.channels == 4);
      break;

    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
      cm.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
      cm.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
      cm.map[2] = PA_CHANNEL_POSITION_REAR_LEFT;
      cm.map[3] = PA_CHANNEL_POSITION_REAR_RIGHT;
      cm.map[4] = PA_CHANNEL_POSITION_FRONT_CENTER;
      cm.map[5] = PA_CHANNEL_POSITION_LFE;
      cm.channels = 6;
      break;
    default:
      _x_assert(!"Should not be reached");
  }

  if (!pa_channel_map_valid(&cm)) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Invalid channel map\n");
    goto fail;
  }

  if (connect_context(this) < 0)
    goto fail;

#if PA_CHECK_VERSION(1,0,0)
  pa_format_info *formatv[2];
  unsigned formatc = 0;

  /* Use digital pass-through if enabled */
  if (encoding != PA_ENCODING_INVALID) {
    formatv[formatc] = pa_format_info_new();
    formatv[formatc]->encoding = encoding;
    pa_format_info_set_rate(formatv[formatc], ss.rate);
    pa_format_info_set_channels(formatv[formatc], ss.channels);
    pa_format_info_set_channel_map(formatv[formatc], &cm);
    formatc++;
  }

  /* Fallback to PCM */
  formatv[formatc] = pa_format_info_new();
  formatv[formatc]->encoding = PA_ENCODING_PCM;
  pa_format_info_set_sample_format(formatv[formatc], ss.format);
  pa_format_info_set_rate(formatv[formatc], ss.rate);
  pa_format_info_set_channels(formatv[formatc], ss.channels);
  pa_format_info_set_channel_map(formatv[formatc], &cm);
  formatc++;

  pa_proplist *proplist = pa_proplist_new();
  if (proplist != NULL)
    pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, "video");

  _x_assert(!this->stream);
  this->stream = pa_stream_new_extended(this->context, "Audio Stream", formatv, formatc, proplist);
  _x_assert(this->stream);

  if (proplist != NULL)
    pa_proplist_free(proplist);

  unsigned i = 0;
  for (i = 0; i < formatc; i++)
    pa_format_info_free(formatv[i]);
#else
  _x_assert(!this->stream);
  this->stream = pa_stream_new(this->context, "Audio Stream", &ss, &cm);
  _x_assert(this->stream);
#endif

  pa_stream_set_state_callback(this->stream, __xine_pa_stream_state_callback, this);
  pa_stream_set_write_callback(this->stream, __xine_pa_stream_request_callback, this);
  pa_stream_set_latency_update_callback(this->stream, __xine_pa_stream_notify_callback, this);

  pa_stream_connect_playback(this->stream, this->sink, NULL,
                             PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_AUTO_TIMING_UPDATE,
                             NULL, NULL);

  for (;;) {
    pa_context_state_t cstate = pa_context_get_state(this->context);
    pa_stream_state_t sstate = pa_stream_get_state(this->stream);

    if (cstate == PA_CONTEXT_FAILED || cstate == PA_CONTEXT_TERMINATED ||
        sstate == PA_STREAM_FAILED || sstate == PA_STREAM_TERMINATED) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to connect context object: %s\n", pa_strerror(pa_context_errno(this->context)));
      goto fail;
    }

    if (sstate == PA_STREAM_READY)
      break;

    pa_threaded_mainloop_wait(this->mainloop);
  }

#if PA_CHECK_VERSION(1,0,0)
  if (encoding != PA_ENCODING_INVALID) {
    const pa_format_info *info = pa_stream_get_format_info(this->stream);

    _x_assert(info);
    if (pa_format_info_is_pcm (info)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "digital pass-through not available\n");
    } else {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "digital pass-through enabled\n");
    }
  }
#endif

  pa_threaded_mainloop_unlock(this->mainloop);

  /* Now we must handle a problem: at init time, xine might have tried to set the default volume value
   * This won't work with pulseaudio, because, at that time, pulseaudio doesn't have a stream.
   * As a workaround, we re-do the volume thingie here */

  config_values_t *cfg;
  cfg = this->xine->config;
  
  cfg_entry_t *entry;

  if (this->volume_bool) {
    this->volume_bool = 0;
    
    if (this->num_channels)
      pa_cvolume_reset(&this->cvolume, this->num_channels);

    entry = cfg->lookup_entry (cfg, "audio.volume.remember_volume");

    if (entry && entry->num_value) {
      entry = cfg->lookup_entry (cfg, "audio.volume.mixer_volume");
      if (entry) {
	this->ao_driver.set_property(&this->ao_driver, AO_PROP_MIXER_VOL, entry->num_value);
      }
    }
  }

  /* get pa sink input information to trigger a update volume event in the frontend */

  pa_operation *operation = pa_context_get_sink_input_info(
      this->context, pa_stream_get_index(this->stream),
      __xine_pa_sink_info_callback, this);

  if (operation == NULL) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to get sink info: %s\n", pa_strerror(pa_context_errno (this->context)));
    goto fail;
  }

  pa_operation_unref(operation);

  return this->sample_rate;

 fail:

  pa_threaded_mainloop_unlock(this->mainloop);
  this_gen->close(this_gen);
  return 0;
}


static int ao_pulse_num_channels(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_pulse_bytes_per_frame(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_pulse_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_pulse_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  size_t size = num_frames * this->bytes_per_frame;
  int ret = -1;
  size_t done = 0;

  pa_threaded_mainloop_lock(this->mainloop);

  while (size > 0) {
    size_t l;

    for (;;) {

      if (!this->stream ||
          !this->context ||
          pa_context_get_state(this->context) != PA_CONTEXT_READY ||
          pa_stream_get_state(this->stream) != PA_STREAM_READY)
        goto finish;

      if ((l = pa_stream_writable_size(this->stream)) == (size_t) -1)
        goto finish;

      if (l > 0)
        break;

      pa_threaded_mainloop_wait(this->mainloop);
    }

    if (l > size)
      l = size;

    pa_stream_write(this->stream, data, l, NULL, 0, PA_SEEK_RELATIVE);
    data = (int16_t *) ((uint8_t*) data + l);
    size -= l;
    done += l;
  }

  ret = done;

finish:

  pa_threaded_mainloop_unlock(this->mainloop);

/*   fprintf(stderr, "write-out\n"); */

  return ret;

}

static int ao_pulse_delay (ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  int ret = 0;

/*   fprintf(stderr, "delay-in\n"); */

  pa_threaded_mainloop_lock(this->mainloop);

  for (;;) {
    pa_usec_t latency = 0;

    if (!this->stream ||
        !this->context ||
        pa_context_get_state(this->context) != PA_CONTEXT_READY ||
        pa_stream_get_state(this->stream) != PA_STREAM_READY)
      goto finish;

    if (pa_stream_get_latency(this->stream, &latency, NULL) >= 0) {
      ret = (int) ((latency * this->sample_rate) / 1000000);
      goto finish;
    }

    if (pa_context_errno(this->context) != PA_ERR_NODATA) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: failed to query latency: %s\n", pa_strerror(pa_context_errno(this->context)));
      goto finish;
    }

    pa_threaded_mainloop_wait(this->mainloop);
  }

finish:

  pa_threaded_mainloop_unlock(this->mainloop);

  return ret;
}

static void ao_pulse_close(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  pa_threaded_mainloop_lock(this->mainloop);

  if (this->stream) {
    pa_stream_disconnect(this->stream);
    pa_stream_unref(this->stream);
    this->stream = NULL;
  }

  pa_threaded_mainloop_unlock(this->mainloop);
}

static uint32_t ao_pulse_get_capabilities (ao_driver_t *this_gen) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  return this->capabilities;
}

static void ao_pulse_exit(ao_driver_t *this_gen) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  ao_pulse_close(this_gen);

  pa_threaded_mainloop_lock(this->mainloop);

  if (this->context) {
    pa_context_disconnect(this->context);
    pa_context_unref(this->context);
  }

  pa_threaded_mainloop_unlock(this->mainloop);

  pa_threaded_mainloop_free(this->mainloop);

  free(this->host);
  free(this->sink);
  free(this);
}

static int wait_for_operation(pulse_driver_t *this, pa_operation *o) {

  for (;;) {

    if (!this->stream ||
        !this->context ||
        pa_context_get_state(this->context) != PA_CONTEXT_READY ||
        pa_stream_get_state(this->stream) != PA_STREAM_READY)
      return -1;

    if (pa_operation_get_state(o) != PA_OPERATION_RUNNING)
      return 0;

    pa_threaded_mainloop_wait(this->mainloop);
  }
}

static int ao_pulse_get_property (ao_driver_t *this_gen, int property) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  int result = 0;
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock(this->mainloop);

  if (!this->stream ||
      !this->context ||
      pa_context_get_state(this->context) != PA_CONTEXT_READY ||
      pa_stream_get_state(this->stream) != PA_STREAM_READY) {
    pa_threaded_mainloop_unlock(this->mainloop);
    return 0;
  }

  switch(property) {

    case AO_PROP_MUTE_VOL:
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:

      o = pa_context_get_sink_input_info(this->context, pa_stream_get_index(this->stream),
                                         __xine_pa_sink_info_callback, this);

      break;
  }

  if (o) {
    wait_for_operation(this, o);
    pa_operation_unref(o);
  }

  switch(property) {

    case AO_PROP_MUTE_VOL:
      result = this->muted;
      break;

    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
      result = (int) (pa_sw_volume_to_linear(this->swvolume)*100);
      break;
  }

  pa_threaded_mainloop_unlock(this->mainloop);

  return result;
}

static int ao_pulse_set_property (ao_driver_t *this_gen, int property, int value) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  int result = ~value;
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock(this->mainloop);

  if (!this->stream ||
      !this->context ||
      pa_context_get_state(this->context) != PA_CONTEXT_READY ||
      pa_stream_get_state(this->stream) != PA_STREAM_READY) {
    pa_threaded_mainloop_unlock(this->mainloop);
    return 0;
  }

  switch(property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:

      this->swvolume = pa_sw_volume_from_linear((double)value/100.0);
      pa_cvolume_set(&this->cvolume, pa_stream_get_sample_spec(this->stream)->channels, this->swvolume);

      o = pa_context_set_sink_input_volume(this->context, pa_stream_get_index(this->stream),
                                           &this->cvolume, __xine_pa_context_success_callback, this);

      result = value;
      break;

    case AO_PROP_MUTE_VOL:

      this->muted = value;

#if PA_PROTOCOL_VERSION >= 11
      /* PulseAudio 0.9.7 and newer */
      o = pa_context_set_sink_input_mute(this->context, pa_stream_get_index(this->stream),
                                           value, __xine_pa_context_success_callback, this);
#else
      /* Get the current volume, so we can restore it properly. */
      o = pa_context_get_sink_input_info(this->context, pa_stream_get_index(this->stream),
                                         __xine_pa_sink_info_callback, this);

      if (o) {
        wait_for_operation(this, o);
        pa_operation_unref(o);
      }

      if ( value )
        pa_cvolume_mute(&this->cvolume, pa_stream_get_sample_spec(this->stream)->channels);
      else
        pa_cvolume_set(&this->cvolume, pa_stream_get_sample_spec(this->stream)->channels, this->swvolume);

      o = pa_context_set_sink_input_volume(this->context, pa_stream_get_index(this->stream),
                                           &this->cvolume, __xine_pa_context_success_callback, this);
#endif
      result = value;
  }

  if (o) {
    wait_for_operation(this, o);
    pa_operation_unref(o);
  }

  pa_threaded_mainloop_unlock(this->mainloop);

  return result;
}

static int ao_pulse_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock(this->mainloop);

  if (!this->stream ||
      !this->context ||
      pa_context_get_state(this->context) != PA_CONTEXT_READY ||
      pa_stream_get_state(this->stream) != PA_STREAM_READY) {
    pa_threaded_mainloop_unlock(this->mainloop);
    return 0;
  }

  switch (cmd) {

    case AO_CTRL_FLUSH_BUFFERS:

      o = pa_stream_flush(this->stream, __xine_pa_stream_success_callback, this);
      break;

    case AO_CTRL_PLAY_RESUME:
    case AO_CTRL_PLAY_PAUSE:

      o = pa_stream_cork(this->stream, cmd == AO_CTRL_PLAY_PAUSE, __xine_pa_stream_success_callback, this);
      break;
  }

  if (o) {
    wait_for_operation(this, o);
    pa_operation_unref(o);
  }

  pa_threaded_mainloop_unlock(this->mainloop);

  return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {
  pulse_class_t   *class = (pulse_class_t *) class_gen;
  pulse_driver_t  *this;
  const char* device;
  int r;
#if PA_CHECK_VERSION(1,0,0)
  int a52_passthru;
#endif

  lprintf ("audio_pulse_out: open_plugin called\n");

  this = calloc(1, sizeof (pulse_driver_t));
  if (!this)
    return NULL;

  this->xine = class->xine;
  this->host = NULL;
  this->sink = NULL;
  this->context = NULL;
  this->mainloop = NULL;

  device = class->xine->config->register_string(class->xine->config,
                                         "audio.pulseaudio_device",
                                         "",
                                         _("device used for pulseaudio"),
                                         _("use 'server[:sink]' for setting the "
                                           "pulseaudio sink device."),
                                         10, NULL,
                                         NULL);

#if PA_CHECK_VERSION(1,0,0)
  a52_passthru = class->xine->config->register_bool(class->xine->config,
                               "audio.device.pulseaudio_a52_pass_through",
                               0,
                               _("use A/52 pass through"),
                               _("Enable this, if your want to use digital audio "
                                 "pass through with pulseaudio.\nYou need to connect a digital "
                                 "surround decoder capable of decoding the formats you want "
                                 "to play to your sound card's digital output."),
                               10, NULL,
                               NULL);
#endif

  if (device && *device) {
    char *sep = strrchr(device, ':');
    if ( sep ) {
      if (!(this->host = strndup(device, sep-device))) {
        free(this);
        return NULL;
      }

      if (!(this->sink = strdup(sep+1))) {
        free(this->host);
        free(this);
        return NULL;
      }
    } else {

      if (!(this->host = strdup(device))) {
        free(this);
        return NULL;
      }
    }
  }

  this->mainloop = pa_threaded_mainloop_new();
  _x_assert(this->mainloop);
  pa_threaded_mainloop_start(this->mainloop);

  /*
   * set capabilities
   */
  this->capabilities =
    AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_MODE_4CHANNEL |
    AO_CAP_MODE_4_1CHANNEL | AO_CAP_MODE_5CHANNEL | AO_CAP_MODE_5_1CHANNEL |
    AO_CAP_MIXER_VOL | AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL |
    AO_CAP_8BITS | AO_CAP_16BITS | AO_CAP_FLOAT32;

#if PA_CHECK_VERSION(1,0,0)
  if (a52_passthru) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
  }
#endif

  this->sample_rate  = 0;

  this->ao_driver.get_capabilities    = ao_pulse_get_capabilities;
  this->ao_driver.get_property        = ao_pulse_get_property;
  this->ao_driver.set_property        = ao_pulse_set_property;
  this->ao_driver.open                = ao_pulse_open;
  this->ao_driver.num_channels        = ao_pulse_num_channels;
  this->ao_driver.bytes_per_frame     = ao_pulse_bytes_per_frame;
  this->ao_driver.delay               = ao_pulse_delay;
  this->ao_driver.write               = ao_pulse_write;
  this->ao_driver.close               = ao_pulse_close;
  this->ao_driver.exit                = ao_pulse_exit;
  this->ao_driver.get_gap_tolerance   = ao_pulse_get_gap_tolerance;
  this->ao_driver.control             = ao_pulse_ctrl;

  xprintf (class->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: host %s sink %s\n",
           this->host ? this->host : "(null)", this->sink ? this->sink : "(null)");

  this->pa_class = class;

  pa_threaded_mainloop_lock(this->mainloop);
  r = connect_context(this);
  pa_threaded_mainloop_unlock(this->mainloop);

  if (r < 0) {
    ao_pulse_exit((ao_driver_t *) this);
    return NULL;
  }

  this->volume_bool = 1;

  return &this->ao_driver;
}

/*
 * class functions
 */

static void dispose_class (audio_driver_class_t *this_gen) {

  pulse_class_t *this = (pulse_class_t *) this_gen;

  free(this);
}

static void *init_class (xine_t *xine, void *data) {

  pulse_class_t        *this;

  lprintf ("audio_pulse_out: init class\n");

  this = calloc(1, sizeof (pulse_class_t));
  if (!this)
    return NULL;

  this->xine = xine;
  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.dispose         = dispose_class;
  this->driver_class.identifier	     = "pulseaudio";
  this->driver_class.description     = N_("xine audio output plugin using pulseaudio sound server");

  return this;
}

static const ao_info_t ao_info_pulse = {
  12
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, 9, "pulseaudio", XINE_VERSION_CODE, &ao_info_pulse, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
