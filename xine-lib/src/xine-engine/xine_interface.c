/*
 * Copyright (C) 2000-2004 the xine project
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
 * convenience/abstraction layer, functions to implement
 * libxine's public interface
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#if defined (__linux__) || defined (__GLIBC__)
#include <endian.h>
#elif defined (__FreeBSD__)
#include <machine/endian.h>
#endif

#define XINE_ENGINE_INTERNAL

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/video_out.h>
#include <xine/demux.h>
#include <xine/post.h>

/*
 * version information / checking
 */

const char *xine_get_version_string(void) {
  return VERSION
#ifndef NDEBUG
    "[DEBUG]"
#endif
    ;
}

void xine_get_version (int *major, int *minor, int *sub) {
  *major = XINE_MAJOR;
  *minor = XINE_MINOR;
  *sub   = XINE_SUB;
}

int xine_check_version(int major, int minor, int sub) {

  if((XINE_MAJOR > major) ||
     ((XINE_MAJOR == major) && (XINE_MINOR > minor)) ||
     ((XINE_MAJOR == major) && (XINE_MINOR == minor) && (XINE_SUB >= sub)))
    return 1;

  return 0;
}

/*
 * public config object access functions
 */

const char* xine_config_register_string (xine_t *self,
					 const char *key,
					 const char *def_value,
					 const char *description,
					 const char *help,
					 int   exp_level,
					 xine_config_cb_t changed_cb,
					 void *cb_data) {

  return self->config->register_string (self->config,
					key,
					def_value,
					description,
					help,
					exp_level,
					changed_cb,
					cb_data);

}

const char* xine_config_register_filename (xine_t *self,
					   const char *key,
					   const char *def_value,
					   int req_type,
					   const char *description,
					   const char *help,
					   int   exp_level,
					   xine_config_cb_t changed_cb,
					   void *cb_data) {

  return self->config->register_filename (self->config,
					  key, def_value, req_type,
					  description, help, exp_level,
					  changed_cb, cb_data);
}

int xine_config_register_range (xine_t *self,
				const char *key,
				int def_value,
				int min, int max,
				const char *description,
				const char *help,
				int   exp_level,
				xine_config_cb_t changed_cb,
				void *cb_data) {
  return self->config->register_range (self->config,
				       key, def_value, min, max,
				       description, help, exp_level,
				       changed_cb, cb_data);
}


int xine_config_register_enum (xine_t *self,
			       const char *key,
			       int def_value,
			       char **values,
			       const char *description,
			       const char *help,
			       int   exp_level,
			       xine_config_cb_t changed_cb,
			       void *cb_data) {
  return self->config->register_enum (self->config,
				      key, def_value, values,
				      description, help, exp_level,
				      changed_cb, cb_data);
}


int xine_config_register_num (xine_t *self,
			      const char *key,
			      int def_value,
			      const char *description,
			      const char *help,
			      int   exp_level,
			      xine_config_cb_t changed_cb,
			      void *cb_data) {
  return self->config->register_num (self->config,
				     key, def_value,
				     description, help, exp_level,
				     changed_cb, cb_data);
}


int xine_config_register_bool (xine_t *self,
			       const char *key,
			       int def_value,
			       const char *description,
			       const char *help,
			       int   exp_level,
			       xine_config_cb_t changed_cb,
			       void *cb_data) {
  return self->config->register_bool (self->config,
				      key, def_value,
				      description, help, exp_level,
				      changed_cb, cb_data);
}


/*
 * helper function:
 *
 * copy current config entry data to user-provided memory
 * and return status
 */

static int config_get_current_entry (xine_t *this, xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  if (!config->cur)
    return 0;

  entry->key            = config->cur->key;
  entry->type           = config->cur->type;
  entry->str_value      = config->cur->str_value;
  entry->str_default    = config->cur->str_default;
  entry->num_value      = config->cur->num_value;
  entry->num_default    = config->cur->num_default;
  entry->range_min      = config->cur->range_min;
  entry->range_max      = config->cur->range_max;
  entry->enum_values    = config->cur->enum_values;

  entry->description    = config->cur->description;
  entry->help           = config->cur->help;
  entry->callback       = config->cur->callback;
  entry->callback_data  = config->cur->callback_data;
  entry->exp_level      = config->cur->exp_level;

  return 1;
}

/*
 * get first config item
 */
int  xine_config_get_first_entry (xine_t *this, xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  pthread_mutex_lock(&config->config_lock);
  config->cur = config->first;

  /* do not hand out unclaimed entries */
  while (config->cur && config->cur->type == XINE_CONFIG_TYPE_UNKNOWN)
    config->cur = config->cur->next;
  result = config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
}


/*
 * get next config item (iterate through the items)
 * this will return NULL when called after returning the last item
 */
int xine_config_get_next_entry (xine_t *this, xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  pthread_mutex_lock(&config->config_lock);

  if (!config->cur) {
    pthread_mutex_unlock(&config->config_lock);
    return (xine_config_get_first_entry(this, entry));
  }

  /* do not hand out unclaimed entries */
  do {
    config->cur = config->cur->next;
  } while (config->cur && config->cur->type == XINE_CONFIG_TYPE_UNKNOWN);
  result = config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
}


/*
 * search for a config entry by key
 */

int xine_config_lookup_entry (xine_t *this, const char *key,
			      xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  config->cur = config->lookup_entry (config, key);
  pthread_mutex_lock(&config->config_lock);
  /* do not hand out unclaimed entries */
  if (config->cur && config->cur->type == XINE_CONFIG_TYPE_UNKNOWN)
    config->cur = NULL;
  result = config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
}


/*
 * update a config entry (which was returned from lookup_entry() )
 */
void xine_config_update_entry (xine_t *this, const xine_cfg_entry_t *entry) {

  switch (entry->type) {
  case XINE_CONFIG_TYPE_RANGE:
  case XINE_CONFIG_TYPE_ENUM:
  case XINE_CONFIG_TYPE_NUM:
  case XINE_CONFIG_TYPE_BOOL:
    this->config->update_num (this->config, entry->key, entry->num_value);
    break;

  case XINE_CONFIG_TYPE_STRING:
    this->config->update_string (this->config, entry->key, entry->str_value);
    break;

  default:
    xprintf (this, XINE_VERBOSITY_DEBUG,
	     "xine_interface: error, unknown config entry type %d\n", entry->type);
    _x_abort();
  }
}


void xine_config_reset (xine_t *this) {

  config_values_t *config = this->config;
  cfg_entry_t *entry;

  pthread_mutex_lock(&config->config_lock);
  config->cur = NULL;

  entry = config->first;
  while (entry) {
    cfg_entry_t *next;
    next = entry->next;
    free (entry);
    entry = next;
  }

  config->first = NULL;
  config->last = NULL;
  pthread_mutex_unlock(&config->config_lock);
}

int xine_port_send_gui_data (xine_video_port_t *vo,
			   int type, void *data) {

  return vo->driver->gui_data_exchange (vo->driver,
						  type, data);
}

static void send_audio_amp_event_internal(xine_stream_t *stream)
{
  xine_event_t            event;
  xine_audio_level_data_t data;

  data.left
    = data.right
    = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP);
  data.mute
    = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP_MUTE);

  event.type        = XINE_EVENT_AUDIO_AMP_LEVEL;
  event.data        = &data;
  event.data_length = sizeof (data);

  xine_event_send(stream, &event);
}

void xine_set_param (xine_stream_t *stream, int param, int value) {
  /* Avoid crashing */
  if ( ! stream ) {
    lprintf ("xine_interface: xine_set_param called with NULL stream.\n");
    return;
  }

  switch (param) {
  case XINE_PARAM_SPEED:
    pthread_mutex_lock (&stream->frontend_lock);
    _x_set_speed (stream, value);
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_FINE_SPEED:
    pthread_mutex_lock (&stream->frontend_lock);
    _x_set_fine_speed (stream, value);
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_AV_OFFSET:
    stream->metronom->set_option (stream->metronom, METRONOM_AV_OFFSET, value);
    break;

  case XINE_PARAM_SPU_OFFSET:
    stream->metronom->set_option (stream->metronom, METRONOM_SPU_OFFSET, value);
    break;

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    pthread_mutex_lock (&stream->frontend_lock);
    if (value < -2)
      value = -2;
    stream->audio_channel_user = value;
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_SPU_CHANNEL:
    _x_select_spu_channel (stream, value);
    break;

  case XINE_PARAM_VIDEO_CHANNEL:
    pthread_mutex_lock (&stream->frontend_lock);
    if (value<0)
      value = 0;
    stream->video_channel = value;
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_AUDIO_VOLUME:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_MIXER_VOL, value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_MUTE:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_MUTE_VOL, value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_COMPR_LEVEL:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_COMPRESSOR, value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_AMP_LEVEL:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out) {
      int old_value = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP);
      if (old_value != stream->audio_out->set_property (stream->audio_out, AO_PROP_AMP, value))
        send_audio_amp_event_internal(stream);
    }
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_AMP_MUTE:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out) {
      int old_value = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP_MUTE);
      if (old_value != stream->audio_out->set_property (stream->audio_out, AO_PROP_AMP_MUTE, value))
        send_audio_amp_event_internal(stream);
    }
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_CLOSE_DEVICE:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_CLOSE_DEVICE, value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_EQ_30HZ:
  case XINE_PARAM_EQ_60HZ:
  case XINE_PARAM_EQ_125HZ:
  case XINE_PARAM_EQ_250HZ:
  case XINE_PARAM_EQ_500HZ:
  case XINE_PARAM_EQ_1000HZ:
  case XINE_PARAM_EQ_2000HZ:
  case XINE_PARAM_EQ_4000HZ:
  case XINE_PARAM_EQ_8000HZ:
  case XINE_PARAM_EQ_16000HZ:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out,
				       param - XINE_PARAM_EQ_30HZ + AO_PROP_EQ_30HZ,
				       value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_VERBOSITY:
    stream->xine->verbosity = value;
    break;

  case XINE_PARAM_VO_SHARPNESS:
  case XINE_PARAM_VO_NOISE_REDUCTION:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_GAMMA:
  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_TVMODE:
  case XINE_PARAM_VO_CROP_LEFT:
  case XINE_PARAM_VO_CROP_RIGHT:
  case XINE_PARAM_VO_CROP_TOP:
  case XINE_PARAM_VO_CROP_BOTTOM:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    stream->video_out->set_property(stream->video_out, param, value);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_IGNORE_VIDEO:
    _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_VIDEO, value);
    break;

  case XINE_PARAM_IGNORE_AUDIO:
    _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_AUDIO, value);
    break;

  case XINE_PARAM_IGNORE_SPU:
    _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_SPU, value);
    break;

  case XINE_PARAM_METRONOM_PREBUFFER:
    stream->metronom->set_option(stream->metronom, METRONOM_PREBUFFER, value);
    break;

  case XINE_PARAM_BROADCASTER_PORT:
    if( !stream->broadcaster && value ) {
      stream->broadcaster = _x_init_broadcaster(stream, value);
    } else if ( stream->broadcaster && !value ) {
      _x_close_broadcaster(stream->broadcaster);
      stream->broadcaster = NULL;
    }
    break;

  case XINE_PARAM_EARLY_FINISHED_EVENT:
    stream->early_finish_event = !!value;
    break;

  case XINE_PARAM_DELAY_FINISHED_EVENT:
    stream->delay_finish_event = value;
    break;

  case XINE_PARAM_GAPLESS_SWITCH:
    stream->gapless_switch = !!value;
    if( stream->gapless_switch && !stream->early_finish_event ) {
      xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "frontend possibly buggy: gapless_switch without early_finish_event\n");
    }
    break;

  default:
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG,
	     "xine_interface: unknown or deprecated stream param %d set\n", param);
  }
}

int xine_get_param (xine_stream_t *stream, int param) {
  int ret;

  switch (param) {
  case XINE_PARAM_SPEED:
    ret = _x_get_speed(stream);
    break;

  case XINE_PARAM_FINE_SPEED:
    ret = _x_get_fine_speed(stream);
    break;

  case XINE_PARAM_AV_OFFSET:
    ret = stream->metronom->get_option (stream->metronom, METRONOM_AV_OFFSET);
    break;

  case XINE_PARAM_SPU_OFFSET:
    ret = stream->metronom->get_option (stream->metronom, METRONOM_SPU_OFFSET);
    break;

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    ret = stream->audio_channel_user;
    break;

  case XINE_PARAM_SPU_CHANNEL:
    ret = stream->spu_channel_user;
    break;

  case XINE_PARAM_VIDEO_CHANNEL:
    ret = stream->video_channel;
    break;

  case XINE_PARAM_AUDIO_VOLUME:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret = stream->audio_out->get_property (stream->audio_out, AO_PROP_MIXER_VOL);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_MUTE:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret = stream->audio_out->get_property (stream->audio_out, AO_PROP_MUTE_VOL);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_COMPR_LEVEL:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret = stream->audio_out->get_property (stream->audio_out, AO_PROP_COMPRESSOR);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_AMP_LEVEL:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_AUDIO_AMP_MUTE:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret = stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP_MUTE);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_EQ_30HZ:
  case XINE_PARAM_EQ_60HZ:
  case XINE_PARAM_EQ_125HZ:
  case XINE_PARAM_EQ_250HZ:
  case XINE_PARAM_EQ_500HZ:
  case XINE_PARAM_EQ_1000HZ:
  case XINE_PARAM_EQ_2000HZ:
  case XINE_PARAM_EQ_4000HZ:
  case XINE_PARAM_EQ_8000HZ:
  case XINE_PARAM_EQ_16000HZ:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    if (!stream->audio_out)
      ret = -1;
    else
      ret=  stream->audio_out->get_property (stream->audio_out,
					     param - XINE_PARAM_EQ_30HZ + AO_PROP_EQ_30HZ);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_VERBOSITY:
    ret = stream->xine->verbosity;
    break;

  case XINE_PARAM_VO_SHARPNESS:
  case XINE_PARAM_VO_NOISE_REDUCTION:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_GAMMA:
  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_TVMODE:
  case XINE_PARAM_VO_WINDOW_WIDTH:
  case XINE_PARAM_VO_WINDOW_HEIGHT:
  case XINE_PARAM_VO_CROP_LEFT:
  case XINE_PARAM_VO_CROP_RIGHT:
  case XINE_PARAM_VO_CROP_TOP:
  case XINE_PARAM_VO_CROP_BOTTOM:
    stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
    ret = stream->video_out->get_property(stream->video_out, param);
    stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
    break;

  case XINE_PARAM_IGNORE_VIDEO:
    ret = _x_stream_info_get_public(stream, XINE_STREAM_INFO_IGNORE_VIDEO);
    break;

  case XINE_PARAM_IGNORE_AUDIO:
    ret = _x_stream_info_get_public(stream, XINE_STREAM_INFO_IGNORE_AUDIO);
    break;

  case XINE_PARAM_IGNORE_SPU:
    ret = _x_stream_info_get_public(stream, XINE_STREAM_INFO_IGNORE_SPU);
    break;

  case XINE_PARAM_METRONOM_PREBUFFER:
    ret = stream->metronom->get_option(stream->metronom, METRONOM_PREBUFFER);
    break;

  case XINE_PARAM_BROADCASTER_PORT:
    if( stream->broadcaster )
      ret = _x_get_broadcaster_port(stream->broadcaster);
    else
      ret = 0;
    break;

  case XINE_PARAM_EARLY_FINISHED_EVENT:
    ret = stream->early_finish_event;
    break;

  case XINE_PARAM_DELAY_FINISHED_EVENT:
    ret = stream->delay_finish_event;
    break;

  case XINE_PARAM_GAPLESS_SWITCH:
    ret = stream->gapless_switch;
    break;

  default:
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG,
	     "xine_interface: unknown or deprecated stream param %d requested\n", param);
    ret = 0;
  }

  return ret;
}

uint32_t xine_get_stream_info (xine_stream_t *stream, int info) {

  switch (info) {

  case XINE_STREAM_INFO_SEEKABLE:
    if (stream->input_plugin)
      return stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_SEEKABLE;
    return 0;

  case XINE_STREAM_INFO_HAS_CHAPTERS:
    if (stream->demux_plugin)
      if (stream->demux_plugin->get_capabilities (stream->demux_plugin) & DEMUX_CAP_CHAPTERS)
        return 1;
    if (stream->input_plugin)
      if (stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_CHAPTERS)
        return 1;
    return 0;

  case XINE_STREAM_INFO_BITRATE:
  case XINE_STREAM_INFO_VIDEO_WIDTH:
  case XINE_STREAM_INFO_VIDEO_HEIGHT:
  case XINE_STREAM_INFO_VIDEO_RATIO:
  case XINE_STREAM_INFO_VIDEO_CHANNELS:
  case XINE_STREAM_INFO_VIDEO_STREAMS:
  case XINE_STREAM_INFO_VIDEO_BITRATE:
  case XINE_STREAM_INFO_VIDEO_FOURCC:
  case XINE_STREAM_INFO_VIDEO_HANDLED:
  case XINE_STREAM_INFO_FRAME_DURATION:
  case XINE_STREAM_INFO_AUDIO_CHANNELS:
  case XINE_STREAM_INFO_AUDIO_BITS:
  case XINE_STREAM_INFO_AUDIO_SAMPLERATE:
  case XINE_STREAM_INFO_AUDIO_BITRATE:
  case XINE_STREAM_INFO_AUDIO_FOURCC:
  case XINE_STREAM_INFO_AUDIO_HANDLED:
  case XINE_STREAM_INFO_HAS_AUDIO:
  case XINE_STREAM_INFO_HAS_VIDEO:
  case XINE_STREAM_INFO_IGNORE_VIDEO:
  case XINE_STREAM_INFO_IGNORE_AUDIO:
  case XINE_STREAM_INFO_IGNORE_SPU:
  case XINE_STREAM_INFO_VIDEO_HAS_STILL:
  case XINE_STREAM_INFO_SKIPPED_FRAMES:
  case XINE_STREAM_INFO_DISCARDED_FRAMES:
  case XINE_STREAM_INFO_VIDEO_AFD:
  case XINE_STREAM_INFO_DVD_TITLE_NUMBER:
  case XINE_STREAM_INFO_DVD_TITLE_COUNT:
  case XINE_STREAM_INFO_DVD_CHAPTER_NUMBER:
  case XINE_STREAM_INFO_DVD_CHAPTER_COUNT:
  case XINE_STREAM_INFO_DVD_ANGLE_NUMBER:
  case XINE_STREAM_INFO_DVD_ANGLE_COUNT:
    return _x_stream_info_get_public(stream, info);

  case XINE_STREAM_INFO_MAX_AUDIO_CHANNEL:
    return stream->audio_track_map_entries;

  case XINE_STREAM_INFO_MAX_SPU_CHANNEL:
    return stream->spu_track_map_entries;

  default:
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG,
	     "xine_interface: unknown or deprecated stream info %d requested\n", info);
  }
  return 0;
}

const char *xine_get_meta_info (xine_stream_t *stream, int info) {
  return _x_meta_info_get_public(stream, info);
}

xine_osd_t *xine_osd_new(xine_stream_t *stream, int x, int y, int width, int height) {
  xine_osd_t *this = (xine_osd_t *)stream->osd_renderer->new_object(stream->osd_renderer, width, height);
  this->osd.renderer->set_position(&this->osd, x, y);
  this->osd.renderer->set_encoding(&this->osd, "");
  return this;
}

uint32_t xine_osd_get_capabilities(xine_osd_t *this) {
  return this->osd.renderer->get_capabilities(&this->osd);
}

void xine_osd_draw_point(xine_osd_t *this, int x, int y, int color) {
  this->osd.renderer->point(&this->osd, x, y, color);
}

void xine_osd_draw_line(xine_osd_t *this, int x1, int y1, int x2, int y2, int color) {
  this->osd.renderer->line(&this->osd, x1, y1, x2, y2, color);
}

void xine_osd_draw_rect(xine_osd_t *this, int x1, int y1, int x2, int y2, int color, int filled) {
  if (filled) {
    this->osd.renderer->filled_rect(&this->osd, x1, y1, x2, y2, color);
  } else {
    this->osd.renderer->line(&this->osd, x1, y1, x2, y1, color);
    this->osd.renderer->line(&this->osd, x2, y1, x2, y2, color);
    this->osd.renderer->line(&this->osd, x2, y2, x1, y2, color);
    this->osd.renderer->line(&this->osd, x1, y2, x1, y1, color);
  }
}

void xine_osd_draw_text(xine_osd_t *this, int x1, int y1, const char *text, int color_base) {
  this->osd.renderer->render_text(&this->osd, x1, y1, text, color_base);
}

void xine_osd_get_text_size(xine_osd_t *this, const char *text, int *width, int *height) {
  this->osd.renderer->get_text_size(&this->osd, text, width, height);
}

int xine_osd_set_font(xine_osd_t *this, const char *fontname, int size) {
  return this->osd.renderer->set_font(&this->osd, fontname, size);
}

void xine_osd_set_encoding(xine_osd_t *this, const char *encoding) {
  this->osd.renderer->set_encoding(&this->osd, encoding);
}

void xine_osd_set_position(xine_osd_t *this, int x, int y) {
  this->osd.renderer->set_position(&this->osd, x, y);
}

void xine_osd_show(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->show(&this->osd, vpts);
}

void xine_osd_show_unscaled(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->show_unscaled(&this->osd, vpts);
}

void xine_osd_show_scaled(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->show_scaled(&this->osd, vpts);
}

void xine_osd_hide(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->hide(&this->osd, vpts);
}

void xine_osd_clear(xine_osd_t *this) {
  this->osd.renderer->clear(&this->osd);
}

void xine_osd_free(xine_osd_t *this) {
  this->osd.renderer->free_object(&this->osd);
}

void xine_osd_set_palette(xine_osd_t *this, const uint32_t *const color, const uint8_t *const trans) {
  this->osd.renderer->set_palette(&this->osd, color, trans);
}

void xine_osd_set_text_palette(xine_osd_t *this, int palette_number, int color_base) {
  this->osd.renderer->set_text_palette(&this->osd, palette_number, color_base);
}

void xine_osd_get_palette(xine_osd_t *this, uint32_t *color, uint8_t *trans) {
  this->osd.renderer->get_palette(&this->osd, color, trans);
}

void xine_osd_draw_bitmap(xine_osd_t *this, uint8_t *bitmap,
			    int x1, int y1, int width, int height,
			    uint8_t *palette_map) {
  this->osd.renderer->draw_bitmap(&this->osd, bitmap, x1, y1, width, height, palette_map);
}

void xine_osd_set_argb_buffer(xine_osd_t *this, uint32_t *argb_buffer,
    int dirty_x, int dirty_y, int dirty_width, int dirty_height) {
  this->osd.renderer->set_argb_buffer(&this->osd, argb_buffer, dirty_x, dirty_y, dirty_width, dirty_height);
}

void xine_osd_set_extent(xine_osd_t *this, int extent_width, int extent_height) {
  this->osd.renderer->set_extent(&this->osd, extent_width, extent_height);
}

void xine_osd_set_video_window(xine_osd_t *this, int window_x, int window_y, int window_width, int window_height) {
  this->osd.renderer->set_video_window(&this->osd, window_x, window_y, window_width, window_height);
}


const char *const *xine_post_list_inputs(xine_post_t *this_gen) {
  post_plugin_t *this = (post_plugin_t *)this_gen;
  return this->input_ids;
}

const char *const *xine_post_list_outputs(xine_post_t *this_gen) {
  post_plugin_t *this = (post_plugin_t *)this_gen;
  return this->output_ids;
}

xine_post_in_t *xine_post_input(xine_post_t *this_gen, const char *name) {
  post_plugin_t  *this = (post_plugin_t *)this_gen;
  xine_list_iterator_t ite;

  ite = xine_list_front(this->input);
  while (ite) {
    xine_post_in_t *input = xine_list_get_value(this->input, ite);
    if (strcmp(input->name, name) == 0)
      return input;
    ite = xine_list_next(this->input, ite);
  }
  return NULL;
}

xine_post_out_t *xine_post_output(xine_post_t *this_gen, const char *name) {
  post_plugin_t   *this = (post_plugin_t *)this_gen;
  xine_list_iterator_t ite;

  ite = xine_list_front(this->output);
  while (ite) {
    xine_post_out_t *output = xine_list_get_value(this->output, ite);
    if (strcmp(output->name, name) == 0)
      return output;
    ite = xine_list_next(this->output, ite);
  }
  return NULL;
}

int xine_post_wire(xine_post_out_t *source, xine_post_in_t *target) {
  if (source && source->rewire) {
    if (target) {
      if (source->type == target->type)
        return source->rewire(source, target->data);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

int xine_post_wire_video_port(xine_post_out_t *source, xine_video_port_t *vo) {
  if (source && source->rewire) {
    if (vo) {
      if (source->type == XINE_POST_DATA_VIDEO)
        return source->rewire(source, vo);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

int xine_post_wire_audio_port(xine_post_out_t *source, xine_audio_port_t *ao) {
  if (source && source->rewire) {
    if (ao) {
      if (source->type == XINE_POST_DATA_AUDIO)
        return source->rewire(source, ao);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

xine_post_out_t * xine_get_video_source(xine_stream_t *stream) {
  return &stream->video_source;
}

xine_post_out_t * xine_get_audio_source(xine_stream_t *stream) {
  return &stream->audio_source;
}

/* report error/message to UI. may be provided with several
 * string parameters. last parameter must be NULL.
 */
int _x_message(xine_stream_t *stream, int type, ...) {
  xine_ui_message_data_t *data;
  xine_event_t            event;
  const char              *explanation;
  size_t                  size;
  int                     n;
  va_list                 ap;
  char                   *s, *params;
  char                   *args[1025];
  static const char *const std_explanation[] = {
    "",
    N_("Warning:"),
    N_("Unknown host:"),
    N_("Unknown device:"),
    N_("Network unreachable"),
    N_("Connection refused:"),
    N_("File not found:"),
    N_("Read error from:"),
    N_("Error loading library:"),
    N_("Encrypted media stream detected"),
    N_("Security message:"),
    N_("Audio device unavailable"),
    N_("Permission error"),
    N_("File is empty:"),
  };

  if (!stream) return 0;

  if( type >= 0 && (size_t)type < sizeof(std_explanation)/
                           sizeof(std_explanation[0]) ) {
    explanation = _(std_explanation[type]);
    size = strlen(explanation)+1;
  } else {
    explanation = NULL;
    size = 0;
  }

  n = 0;
  va_start(ap, type);
  while(((s = va_arg(ap, char *)) != NULL) && (n < 1024)) {
    size += strlen(s) + 1;
    args[n] = s;
    n++;
  }
  va_end(ap);

  args[n] = NULL;

  size += sizeof(xine_ui_message_data_t) + 1;
  data = calloc(1, size );

  strcpy(data->compatibility.str, "Upgrade your frontend to see the error messages");
  data->type           = type;
  data->num_parameters = n;

  if( explanation ) {
    strcpy(data->messages, explanation);
    data->explanation = data->messages - (char *)data;
    params = data->messages + strlen(explanation) + 1;
  } else {
    data->explanation = 0;
    params            = data->messages;
  }

  data->parameters = params - (char *)data;

  n       = 0;
  *params = '\0';

  while(args[n]) {
    strcpy(params, args[n]);
    params += strlen(args[n]) + 1;
    n++;
  }

  *params = '\0';

  event.type        = XINE_EVENT_UI_MESSAGE;
  event.stream      = stream;
  event.data_length = size;
  event.data        = data;
  xine_event_send(stream, &event);

  free(data);

  return 1;
}

int64_t xine_get_current_vpts(xine_stream_t *stream) {
  return stream->xine->clock->get_current_time(stream->xine->clock);
}
