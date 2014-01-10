/*
 * Copyright (C) 2000-2013 the xine project
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
 * config object (was: file) management - implementation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xine/configfile.h>
#include "bswap.h"
#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <base64.h>
#else
#  include <libavutil/base64.h>
#endif

#define LOG_MODULE "configfile"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xineutils.h>
#include <xine/xine_internal.h>

static const xine_config_entry_translation_t *config_entry_translation_user = NULL;
static const xine_config_entry_translation_t config_entry_translation[] = {
  { "audio.a52_pass_through",			"" },
  { "audio.alsa_a52_device",			"audio.device.alsa_passthrough_device" },
  { "audio.alsa_default_device",		"audio.device.alsa_default_device" },
  { "audio.alsa_front_device",			"audio.device.alsa_front_device" },
  { "audio.alsa_mixer_name",			"audio.device.alsa_mixer_name" },
  { "audio.alsa_mmap_enable",			"audio.device.alsa_mmap_enable" },
  { "audio.alsa_surround40_device",		"audio.device.alsa_surround40_device" },
  { "audio.alsa_surround51_device",		"audio.device.alsa_surround51_device" },
  { "audio.av_sync_method",			"audio.synchronization.av_sync_method" },
  { "audio.directx_device",			"" },
  { "audio.esd_latency",			"audio.device.esd_latency" },
  { "audio.five_channel",			"" },
  { "audio.five_lfe_channel",			"" },
  { "audio.force_rate",				"audio.synchronization.force_rate" },
  { "audio.four_channel",			"" },
  { "audio.four_lfe_channel",			"" },
  { "audio.irixal_gap_tolerance",		"audio.device.irixal_gap_tolerance" },
  { "audio.mixer_name",				"" },
  { "audio.mixer_number",			"audio.device.oss_mixer_number" },
  { "audio.mixer_volume",			"audio.volume.mixer_volume" },
  { "audio.num_buffers",			"engine.buffers.audio_num_buffers" },
  { "audio.oss_device_name",			"audio.device.oss_device_name" },
  { "audio.oss_device_num",			"" },
  { "audio.oss_device_number",			"audio.device.oss_device_number" },
  { "audio.oss_pass_through_bug",		"" },
  { "audio.passthrough_offset",			"audio.synchronization.passthrough_offset" },
  { "audio.remember_volume",			"audio.volume.remember_volume" },
  { "audio.resample_mode",			"audio.synchronization.resample_mode" },
  { "audio.speaker_arrangement",		"audio.output.speaker_arrangement" },
  { "audio.sun_audio_device",			"audio.device.sun_audio_device" },
  { "codec.a52_dynrng",				"audio.a52.dynamic_range" },
  { "codec.a52_level",				"audio.a52.level" },
  { "codec.a52_surround_downmix",		"audio.a52.surround_downmix" },
  { "codec.ffmpeg_pp_quality",			"video.processing.ffmpeg_pp_quality" },
  { "codec.real_codecs_path",			"decoder.external.real_codecs_path" },
  { "codec.win32_path",				"decoder.external.win32_codecs_path" },
  { "dxr3.alt_play_mode",			"dxr3.playback.alt_play_mode" },
  { "dxr3.color_interval",			"dxr3.output.keycolor_interval" },
  { "dxr3.correct_durations",			"dxr3.playback.correct_durations" },
  { "dxr3.devicename",				"" },
  { "dxr3.enc_add_bars",			"dxr3.encoding.add_bars" },
  { "dxr3.enc_alt_play_mode",			"dxr3.encoding.alt_play_mode" },
  { "dxr3.enc_swap_fields",			"dxr3.encoding.swap_fields" },
  { "dxr3.encoder",				"dxr3.encoding.encoder" },
  { "dxr3.fame_quality",			"dxr3.encoding.fame_quality" },
  { "dxr3.keycolor",				"dxr3.output.keycolor" },
  { "dxr3.lavc_bitrate",			"dxr3.encoding.lavc_bitrate" },
  { "dxr3.lavc_qmax",				"dxr3.encoding.lavc_qmax" },
  { "dxr3.lavc_qmin",				"dxr3.encoding.lavc_qmin" },
  { "dxr3.lavc_quantizer",			"dxr3.encoding.lavc_quantizer" },
  { "dxr3.preferred_tvmode",			"dxr3.output.tvmode" },
  { "dxr3.rte_bitrate",				"dxr3.encoding.rte_bitrate" },
  { "dxr3.shrink_overlay_area",			"dxr3.output.shrink_overlay_area" },
  { "dxr3.sync_every_frame",			"dxr3.playback.sync_every_frame" },
  { "dxr3.videoout_mode",			"dxr3.output.mode" },
  { "input.cdda_cddb_cachedir",			"media.audio_cd.cddb_cachedir" },
  { "input.cdda_cddb_port",			"media.audio_cd.cddb_port" },
  { "input.cdda_cddb_server",			"media.audio_cd.cddb_server" },
  { "input.cdda_device",			"media.audio_cd.device" },
  { "input.cdda_use_cddb",			"media.audio_cd.use_cddb" },
  { "input.css_cache_path",			"media.dvd.css_cache_path" },
  { "input.css_decryption_method",		"media.dvd.css_decryption_method" },
  { "input.drive_slowdown",			"media.audio_cd.drive_slowdown" },
  { "input.dvb_last_channel_enable",		"media.dvb.remember_channel" },
  { "input.dvb_last_channel_watched",		"media.dvb.last_channel" },
  { "input.dvbdisplaychan",			"media.dvb.display_channel" },
  { "input.dvbzoom",				"media.dvb.zoom" },
  { "input.dvb_adapternum",			"media.dvb.adapter"},
  { "input.dvd_device",				"media.dvd.device" },
  { "input.dvd_language",			"media.dvd.language" },
  { "input.dvd_raw_device",			"media.dvd.raw_device" },
  { "input.dvd_region",				"media.dvd.region" },
  { "input.dvd_seek_behaviour",			"media.dvd.seek_behaviour" },
  { "input.dvd_skip_behaviour",			"media.dvd.skip_behaviour" },
  { "input.dvd_use_readahead",			"media.dvd.readahead" },
  { "input.file_hidden_files",			"media.files.show_hidden_files" },
  { "input.file_origin_path",			"media.files.origin_path" },
  { "input.http_no_proxy",			"media.network.http_no_proxy" },
  { "input.http_proxy_host",			"media.network.http_proxy_host" },
  { "input.http_proxy_password",		"media.network.http_proxy_password" },
  { "input.http_proxy_port",			"media.network.http_proxy_port" },
  { "input.http_proxy_user",			"media.network.http_proxy_user" },
  { "input.mms_network_bandwidth",		"media.network.bandwidth" },
  { "input.mms_protocol",			"media.network.mms_protocol" },
  { "input.pvr_device",				"media.wintv_pvr.device" },
  { "input.v4l_radio_device_path",		"media.video4linux.radio_device" },
  { "input.v4l_video_device_path",		"media.video4linux.video_device" },
  { "input.vcd_device",				"media.vcd.device" },
  { "misc.cc_center",				"subtitles.closedcaption.center" },
  { "misc.cc_enabled",				"subtitles.closedcaption.enabled" },
  { "misc.cc_font",				"subtitles.closedcaption.font" },
  { "misc.cc_font_size",			"subtitles.closedcaption.font_size" },
  { "misc.cc_italic_font",			"subtitles.closedcaption.italic_font" },
  { "misc.cc_scheme",				"subtitles.closedcaption.scheme" },
  { "misc.demux_strategy",			"engine.demux.strategy" },
  { "misc.memcpy_method",			"engine.performance.memcpy_method" },
  { "misc.osd_text_palette",			"ui.osd.text_palette" },
  { "misc.save_dir",				"media.capture.save_dir" },
  { "misc.spu_font",				"subtitles.separate.font" },
  { "misc.spu_src_encoding",			"subtitles.separate.src_encoding" },
  { "misc.spu_subtitle_size",			"subtitles.separate.subtitle_size" },
  { "misc.spu_use_unscaled_osd",		"subtitles.separate.use_unscaled_osd" },
  { "misc.spu_vertical_offset",			"subtitles.separate.vertical_offset" },
  { "misc.sub_timeout",				"subtitles.separate.timeout" },
  { "post.goom_csc_method",			"effects.goom.csc_method" },
  { "post.goom_fps",				"effects.goom.fps" },
  { "post.goom_height",				"effects.goom.height" },
  { "post.goom_width",				"effects.goom.width" },
  { "vcd.autoadvance",				"media.vcd.autoadvance" },
  { "vcd.autoplay",				"media.vcd.autoplay" },
  { "vcd.comment_format",			"media.vcd.comment_format" },
  { "vcd.debug",				"media.vcd.debug" },
  { "vcd.default_device",			"media.vcd.device" },
  { "vcd.length_reporting",			"media.vcd.length_reporting" },
  { "vcd.show_rejected",			"media.vcd.show_rejected" },
  { "vcd.title_format",				"media.vcd.title_format" },
  { "video.XV_DOUBLE_BUFFER",			"video.device.xv_double_buffer" },
  { "video.XV_FILTER",				"video.device.xv_filter" },
  { "video.deinterlace_method",			"video.output.xv_deinterlace_method" },
  { "video.disable_exact_osd_alpha_blending",	"video.output.disable_exact_alphablend" },
  { "video.disable_scaling",			"video.output.disable_scaling" },
  { "video.fb_device",				"video.device.fb_device" },
  { "video.fb_gamma",				"video.output.fb_gamma" },
  { "video.horizontal_position",		"video.output.horizontal_position" },
  { "video.num_buffers",			"engine.buffers.video_num_buffers" },
  { "video.opengl_double_buffer",		"video.device.opengl_double_buffer" },
  { "video.opengl_gamma",			"video.output.opengl_gamma" },
  { "video.opengl_min_fps",			"video.output.opengl_min_fps" },
  { "video.opengl_renderer",			"video.output.opengl_renderer" },
  { "video.pgx32_device",			"video.device.pgx32_device" },
  { "video.pgx64_brightness",			"video.output.pgx64_brightness" },
  { "video.pgx64_chromakey_en",			"video.device.pgx64_chromakey_en" },
  { "video.pgx64_colour_key",			"video.device.pgx64_colour_key" },
  { "video.pgx64_device",			"" },
  { "video.pgx64_multibuf_en",			"video.device.pgx64_multibuf_en" },
  { "video.pgx64_overlay_mode",			"" },
  { "video.pgx64_saturation",			"video.output.pgx64_saturation" },
  { "video.sdl_hw_accel",			"video.device.sdl_hw_accel" },
  { "video.unichrome_cpu_save",			"video.device.unichrome_cpu_save" },
  { "video.vertical_position",			"video.output.vertical_position" },
  { "video.vidix_blue_intensity",		"video.output.vidix_blue_intensity" },
  { "video.vidix_colour_key_blue",		"video.device.vidix_colour_key_blue" },
  { "video.vidix_colour_key_green",		"video.device.vidix_colour_key_green" },
  { "video.vidix_colour_key_red",		"video.device.vidix_colour_key_red" },
  { "video.vidix_green_intensity",		"video.output.vidix_green_intensity" },
  { "video.vidix_red_intensity",		"video.output.vidix_red_intensity" },
  { "video.vidix_use_double_buffer",		"video.device.vidix_double_buffer" },
  { "video.vidixfb_device",			"video.device.vidixfb_device" },
  { "video.warn_discarded_threshold",		"engine.performance.warn_discarded_threshold" },
  { "video.warn_skipped_threshold",		"engine.performance.warn_skipped_threshold" },
  { "video.xshm_gamma",				"video.output.xshm_gamma" },
  { "video.xv_autopaint_colorkey",		"video.device.xv_autopaint_colorkey" },
  { "video.xv_colorkey",			"video.device.xv_colorkey" },
  { "video.xv_pitch_alignment",			"video.device.xv_pitch_alignment" },
  { "video.xvmc_more_frames",			"video.device.xvmc_more_frames" },
  { "video.xvmc_nvidia_color_fix",		"video.device.xvmc_nvidia_color_fix" },
  {}
};


static int config_section_enum(const char *sect) {
  static const char *const known_section[] = {
    "gui",
    "ui",
    "audio",
    "video",
    "dxr3",
    "input",
    "media",
    "codec",
    "decoder",
    "subtitles",
    "post",
    "effects",
    "engine",
    "misc",
    NULL
  };
  int i = 0;

  while (known_section[i])
    if (strcmp(sect, known_section[i++]) == 0)
      return i;
  return i + 1;
}

static void config_key_split(const char *key, char **base, char **section, char **subsect, char **name) {
  char *parse;

  *base = strdup(key);
  if ((parse = strchr(*base, '.'))) {
    *section = *base;
    *parse   = '\0';
    parse++;
    if ((*name = strchr(parse, '.'))) {
      *subsect = parse;
      **name   = '\0';
      (*name)++;
    } else {
      *subsect = NULL;
      *name    = parse;
    }
  } else {
    *section = NULL;
    *subsect = NULL;
    *name    = parse;
  }
}

static void config_insert(config_values_t *this, cfg_entry_t *new_entry) {
  cfg_entry_t *cur, *prev;
  char *new_base, *new_section, *new_subsect, *new_name;
  char *cur_base, *cur_section, *cur_subsect, *cur_name;

  /* extract parts of the new key */
  config_key_split(new_entry->key, &new_base, &new_section, &new_subsect, &new_name);

  /* search right position */
  cur_base = NULL;
  for (cur = this->first, prev = NULL; cur; prev = cur, cur = cur->next) {
    /* extract parts of the cur key */
    if (cur_base)
      free(cur_base);
    config_key_split(cur->key, &cur_base, &cur_section, &cur_subsect, &cur_name);

    /* sort by section name */
    if (!new_section &&  cur_section) break;
    if ( new_section && !cur_section) continue;
    if ( new_section &&  cur_section) {
      int new_sec_num = config_section_enum(new_section);
      int cur_sec_num = config_section_enum(cur_section);
      int cmp         = strcmp(new_section, cur_section);
      if (new_sec_num < cur_sec_num) break;
      if (new_sec_num > cur_sec_num) continue;
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }
    /* sort by subsection name */
    if (!new_subsect &&  cur_subsect) break;
    if ( new_subsect && !cur_subsect) continue;
    if ( new_subsect &&  cur_subsect) {
      int cmp = strcmp(new_subsect, cur_subsect);
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }
    /* sort by experience level */
    if (new_entry->exp_level < cur->exp_level) break;
    if (new_entry->exp_level > cur->exp_level) continue;
    /* sort by entry name */
    if (!new_name &&  cur_name) break;
    if ( new_name && !cur_name) continue;
    if ( new_name &&  cur_name) {
      int cmp = strcmp(new_name, cur_name);
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }

    break;
  }
  if (new_base)
    free(new_base);
  if (cur_base)
    free(cur_base);

  new_entry->next = cur;
  if (!cur)
    this->last = new_entry;
  if (prev)
    prev->next = new_entry;
  else
    this->first = new_entry;
}

static cfg_entry_t *XINE_MALLOC config_add (config_values_t *this, const char *key, int exp_level) {

  cfg_entry_t *entry;

  entry = calloc (1, sizeof (cfg_entry_t));
  entry->config        = this;
  entry->key           = strdup(key);
  entry->type          = XINE_CONFIG_TYPE_UNKNOWN;
  entry->unknown_value = NULL;
  entry->str_value     = NULL;
  entry->exp_level     = exp_level;

  config_insert(this, entry);

  lprintf ("add entry key=%s\n", key);

  return entry;
}

static void config_remove(config_values_t *this, cfg_entry_t *entry, cfg_entry_t *prev) {
  if (!entry->next)
    this->last = prev;
  if (!prev)
    this->first = entry->next;
  else
    prev->next = entry->next;
}

static const char *config_xlate_internal (const char *key, const xine_config_entry_translation_t *trans)
{
  --trans;
  while ((++trans)->old_name)
    if (trans->new_name[0] && strcmp(key, trans->old_name) == 0)
      return trans->new_name;
  return NULL;
}

static const char *config_translate_key (const char *key, char **tmp) {
  /* Returns translated key or, if no translation found, NULL.
   * Translated key may be in a static buffer allocated within this function.
   * NOT re-entrant; assumes that config_lock is held.
   */
  unsigned trans;
  const char *newkey = NULL;

  /* first, special-case the decoder entries (so that new ones can be added
   * without requiring modification of the translation table)
   */
  *tmp = NULL;
  if (!strncmp (key, "decoder.", 8) &&
      !strcmp (key + (trans = strlen (key)) - 9, "_priority")) {
    *tmp = _x_asprintf ("engine.decoder_priorities.%.*s", trans - 17, key + 8);
    return *tmp;
  }

  /* search the translation table... */
  newkey = config_xlate_internal (key, config_entry_translation);
  if (!newkey && config_entry_translation_user)
    newkey = config_xlate_internal (key, config_entry_translation_user);

  return newkey;
}

static void config_lookup_entry_int (config_values_t *this, const char *key,
				       cfg_entry_t **entry, cfg_entry_t **prev) {

  int trans;
  char *tmp = NULL;

  /* try twice at most (second time with translation from old key name) */
  for (trans = 2; trans; --trans) {
    *entry = this->first;
    *prev  = NULL;

    while (*entry && strcmp((*entry)->key, key)) {
      *prev  = *entry;
      *entry = (*entry)->next;
    }

    if (*entry) {
      free(tmp);
      return;
    }

    /* we did not find a match, maybe this is an old config entry name
     * trying to translate */
    key = config_translate_key(key, &tmp);
    if (!key) {
      free(tmp);
      return;
    }
  }
}


/*
 * external interface
 */

static cfg_entry_t *config_lookup_entry(config_values_t *this, const char *key) {
  cfg_entry_t *entry, *prev;

  pthread_mutex_lock(&this->config_lock);
  config_lookup_entry_int(this, key, &entry, &prev);
  pthread_mutex_unlock(&this->config_lock);

  return entry;
}

static void config_reset_value(cfg_entry_t *entry) {

  if (entry->str_value) {
    free (entry->str_value);
    entry->str_value = NULL;
  }
  if (entry->str_default) {
    free (entry->str_default);
    entry->str_default = NULL;
  }
  if (entry->description) {
    free (entry->description);
    entry->description = NULL;
  }
  if (entry->help) {
    free (entry->help);
    entry->help = NULL;
  }
  if (entry->enum_values) {
    char **value;

    value = entry->enum_values;
    while (*value) {
      free (*value);
      value++;
    }
    free (entry->enum_values);
    entry->enum_values = NULL;
  }
  entry->num_value = 0;
}

static void config_shallow_copy(xine_cfg_entry_t *dest, cfg_entry_t *src);

static cfg_entry_t *config_register_key (config_values_t *this,
					 const char *key,
					 int exp_level,
					 xine_config_cb_t changed_cb,
					 void *cb_data) {
  cfg_entry_t *entry, *prev;

  _x_assert(this);
  _x_assert(key);

  lprintf ("registering %s\n", key);
  config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    /* new entry */
    entry = config_add (this, key, exp_level);
  } else {
    if (entry->exp_level != exp_level) {
      config_remove(this, entry, prev);
      entry->exp_level = exp_level;
      config_insert(this, entry);
    }
  }

  /* override callback */
  if (changed_cb) {
    if (entry->callback && (entry->callback != changed_cb)) {
      lprintf("overriding callback\n");
    }
    entry->callback = changed_cb;
    entry->callback_data = cb_data;
  }

  /* we created a new entry, call the callback */
  if (this->new_entry_cb) {
    xine_cfg_entry_t cb_entry;

    config_shallow_copy(&cb_entry, entry);
    this->new_entry_cb(this->new_entry_cbdata, &cb_entry);
  }

  return entry;
}

static cfg_entry_t *config_register_string_internal (config_values_t *this,
						     const char *key,
						     const char *def_value,
						     int num_value,
						     const char *description,
						     const char *help,
						     int exp_level,
						     xine_config_cb_t changed_cb,
						     void *cb_data) {
  cfg_entry_t *entry;

  _x_assert(this);
  _x_assert(key);
  _x_assert(def_value);

  pthread_mutex_lock(&this->config_lock);

  entry = config_register_key(this, key, exp_level, changed_cb, cb_data);

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf("config entry already registered: %s\n", key);
    pthread_mutex_unlock(&this->config_lock);
    return entry;
  }

  config_reset_value(entry);

  /* set string */
  entry->type = XINE_CONFIG_TYPE_STRING;

  if (entry->unknown_value)
    entry->str_value = strdup(entry->unknown_value);
  else
    entry->str_value = strdup(def_value);

  entry->num_value = num_value;

  /* fill out rest of struct */
  entry->str_default    = strdup(def_value);
  entry->description    = (description) ? strdup(description) : NULL;
  entry->help           = (help) ? strdup(help) : NULL;

  pthread_mutex_unlock(&this->config_lock);
  return entry;
}

static char *config_register_string (config_values_t *this,
				     const char *key,
				     const char *def_value,
				     const char *description,
				     const char *help,
				     int exp_level,
				     xine_config_cb_t changed_cb,
				     void *cb_data) {
  return config_register_string_internal (this, key, def_value, 0, description,
					  help, exp_level, changed_cb, cb_data)->str_value;
}

static char *config_register_filename (config_values_t *this,
				       const char *key,
				       const char *def_value,
				       int req_type,
				       const char *description,
				       const char *help,
				       int exp_level,
				       xine_config_cb_t changed_cb,
				       void *cb_data) {
  return config_register_string_internal (this, key, def_value, req_type, description,
					  help, exp_level, changed_cb, cb_data)->str_value;
}

static int config_register_num (config_values_t *this,
				const char *key,
				int def_value,
				const char *description,
				const char *help,
				int exp_level,
				xine_config_cb_t changed_cb,
				void *cb_data) {

  cfg_entry_t *entry;
  _x_assert(this);
  _x_assert(key);

  pthread_mutex_lock(&this->config_lock);

  entry = config_register_key(this, key, exp_level, changed_cb, cb_data);

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf("config entry already registered: %s\n", key);
    pthread_mutex_unlock(&this->config_lock);
    return entry->num_value;
  }

  config_reset_value(entry);
  entry->type = XINE_CONFIG_TYPE_NUM;

  if (entry->unknown_value)
    sscanf (entry->unknown_value, "%d", &entry->num_value);
  else
    entry->num_value = def_value;

  /* fill out rest of struct */
  entry->num_default    = def_value;
  entry->description    = (description) ? strdup(description) : NULL;
  entry->help           = (help) ? strdup(help) : NULL;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int config_register_bool (config_values_t *this,
				 const char *key,
				 int def_value,
				 const char *description,
				 const char *help,
				 int exp_level,
				 xine_config_cb_t changed_cb,
				 void *cb_data) {

  cfg_entry_t *entry;
  _x_assert(this);
  _x_assert(key);

  pthread_mutex_lock(&this->config_lock);

  entry = config_register_key(this, key, exp_level, changed_cb, cb_data);

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf("config entry already registered: %s\n", key);
    pthread_mutex_unlock(&this->config_lock);
    return entry->num_value;
  }

  config_reset_value(entry);
  entry->type = XINE_CONFIG_TYPE_BOOL;

  if (entry->unknown_value)
    sscanf (entry->unknown_value, "%d", &entry->num_value);
  else
    entry->num_value = def_value;

  /* fill out rest of struct */
  entry->num_default    = def_value;
  entry->description    = (description) ? strdup(description) : NULL;
  entry->help           = (help) ? strdup(help) : NULL;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int config_register_range (config_values_t *this,
				  const char *key,
				  int def_value,
				  int min, int max,
				  const char *description,
				  const char *help,
				  int exp_level,
				  xine_config_cb_t changed_cb,
				  void *cb_data) {

  cfg_entry_t *entry;
  _x_assert(this);
  _x_assert(key);

  pthread_mutex_lock(&this->config_lock);

  entry = config_register_key(this, key, exp_level, changed_cb, cb_data);

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf("config entry already registered: %s\n", key);
    pthread_mutex_unlock(&this->config_lock);
    return entry->num_value;
  }

  config_reset_value(entry);
  entry->type = XINE_CONFIG_TYPE_RANGE;

  if (entry->unknown_value)
    sscanf (entry->unknown_value, "%d", &entry->num_value);
  else
    entry->num_value = def_value;

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->range_min     = min;
  entry->range_max     = max;
  entry->description    = (description) ? strdup(description) : NULL;
  entry->help           = (help) ? strdup(help) : NULL;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int config_parse_enum (const char *str, const char **values) {

  const char **value;
  int    i;


  value = values;
  i = 0;

  while (*value) {

    lprintf ("parse enum, >%s< ?= >%s<\n", *value, str);

    if (!strcmp (*value, str))
      return i;

    value++;
    i++;
  }

  lprintf ("warning, >%s< is not a valid enum here, using 0\n", str);

  return 0;
}

static int config_register_enum (config_values_t *this,
				 const char *key,
				 int def_value,
				 char **values,
				 const char *description,
				 const char *help,
				 int exp_level,
				 xine_config_cb_t changed_cb,
				 void *cb_data) {

  cfg_entry_t *entry;
  const char **value_src;
  char **value_dest;
  int value_count;


  _x_assert(this);
  _x_assert(key);
  _x_assert(values);

  pthread_mutex_lock(&this->config_lock);

  entry = config_register_key(this, key, exp_level, changed_cb, cb_data);

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf("config entry already registered: %s\n", key);
    pthread_mutex_unlock(&this->config_lock);
    return entry->num_value;
  }

  config_reset_value(entry);
  entry->type = XINE_CONFIG_TYPE_ENUM;

  if (entry->unknown_value)
    entry->num_value = config_parse_enum (entry->unknown_value, (const char **)values);
  else
    entry->num_value = def_value;

  /* fill out rest of struct */
  entry->num_default = def_value;

  /* allocate and copy the enum values */
  value_src = (const char **)values;
  value_count = 0;
  while (*value_src) {
    value_src++;
    value_count++;
  }
  entry->enum_values = malloc (sizeof(char*) * (value_count + 1));
  value_src = (const char **)values;
  value_dest = entry->enum_values;
  while (*value_src) {
    *value_dest = strdup(*value_src);
    value_src++;
    value_dest++;
  }
  *value_dest = NULL;

  entry->description   = (description) ? strdup(description) : NULL;
  entry->help          = (help) ? strdup(help) : NULL;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static void config_shallow_copy(xine_cfg_entry_t *dest, cfg_entry_t *src)
{
  dest->key           = src->key;
  dest->type          = src->type;
  dest->unknown_value = src->unknown_value;
  dest->str_value     = src->str_value;
  dest->str_default   = src->str_default;
  dest->num_value     = src->num_value;
  dest->num_default   = src->num_default;
  dest->range_min     = src->range_min;
  dest->range_max     = src->range_max;
  dest->enum_values   = src->enum_values;
  dest->description   = src->description;
  dest->help          = src->help;
  dest->exp_level     = src->exp_level;
  dest->callback      = src->callback;
  dest->callback_data = src->callback_data;
}

static void config_update_num (config_values_t *this,
			       const char *key, int value) {

  cfg_entry_t *entry;

  entry = this->lookup_entry (this, key);

  lprintf ("updating %s to %d\n", key, value);

  if (!entry) {

    lprintf ("WARNING! tried to update unknown key %s (to %d)\n", key, value);

    return;

  }

  if ((entry->type == XINE_CONFIG_TYPE_UNKNOWN)
      || (entry->type == XINE_CONFIG_TYPE_STRING)) {
    printf ("configfile: error - tried to update non-num type %d (key %s, value %d)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  entry->num_value = value;

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;

    config_shallow_copy(&cb_entry, entry);

    /* it is safe to enter the callback from within a locked context
     * because we use a recursive mutex.
     */
    entry->callback (entry->callback_data, &cb_entry);
  }

  pthread_mutex_unlock(&this->config_lock);
}

static void config_update_string (config_values_t *this,
				  const char *key,
				  const char *value) {

  cfg_entry_t *entry;
  char *str_free = NULL;

  lprintf ("updating %s to %s\n", key, value);

  entry = this->lookup_entry (this, key);

  if (!entry) {

    printf ("configfile: error - tried to update unknown key %s (to %s)\n",
	    key, value);
    return;

  }

  /* if an enum is updated with a string, we convert the string to
   * its index and use update number */
  if (entry->type == XINE_CONFIG_TYPE_ENUM) {
    config_update_num(this, key, config_parse_enum(value, (const char **)entry->enum_values));
    return;
  }

  if (entry->type != XINE_CONFIG_TYPE_STRING) {
    printf ("configfile: error - tried to update non-string type %d (key %s, value %s)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  if (value != entry->str_value) {
    str_free = entry->str_value;
    entry->str_value = strdup(value);
  }

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;

    config_shallow_copy(&cb_entry, entry);

    /* it is safe to enter the callback from within a locked context
     * because we use a recursive mutex.
     */
    entry->callback (entry->callback_data, &cb_entry);
  }

  if (str_free)
    free(str_free);
  pthread_mutex_unlock(&this->config_lock);
}

/*
 * front end config translation handling
 */
void xine_config_set_translation_user (const xine_config_entry_translation_t *xlate)
{
  config_entry_translation_user = xlate;
}

/*
 * load/save config data from/to afile (e.g. $HOME/.xine/config)
 */
void xine_config_load (xine_t *xine, const char *filename) {

  config_values_t *this = xine->config;
  FILE *f_config;

  lprintf ("reading from file '%s'\n", filename);

  f_config = fopen (filename, "r");

  if (f_config) {

    char line[1024];
    char *value;

    while (fgets (line, 1023, f_config)) {
      line[strlen(line)-1]= (char) 0; /* eliminate lf */

      if (line[0] == '#')
	continue;

      if (line[0] == '.') {
	if (strncmp(line, ".version:", 9) == 0) {
	  sscanf(line + 9, "%d", &this->current_version);
	  if (this->current_version > CONFIG_FILE_VERSION)
	    xine_log(xine, XINE_LOG_MSG,
		     _("The current config file has been modified by a newer version of xine."));
	}
	continue;
      }

      if ((value = strchr (line, ':'))) {

	cfg_entry_t *entry;

	*value = (char) 0;
	value++;

	if (!(entry = config_lookup_entry(this, line))) {
	  const char *key = line;
	  char *tmp = NULL;
	  pthread_mutex_lock(&this->config_lock);
	  if (this->current_version < CONFIG_FILE_VERSION) {
	    /* old config file -> let's see if we have to rename this one */
	    key = config_translate_key(key, &tmp);
	    if (!key)
	      key = line; /* no translation? fall back on untranslated key */
	  }
	  entry = config_add (this, key, 50);
	  entry->unknown_value = strdup(value);
	  free(tmp);
	  pthread_mutex_unlock(&this->config_lock);
	} else {
          switch (entry->type) {
          case XINE_CONFIG_TYPE_RANGE:
          case XINE_CONFIG_TYPE_NUM:
          case XINE_CONFIG_TYPE_BOOL:
            config_update_num (this, entry->key, atoi(value));
            break;
          case XINE_CONFIG_TYPE_ENUM:
          case XINE_CONFIG_TYPE_STRING:
            config_update_string (this, entry->key, value);
            break;
          case XINE_CONFIG_TYPE_UNKNOWN:
	    pthread_mutex_lock(&this->config_lock);
	    free(entry->unknown_value);
	    entry->unknown_value = strdup(value);
	    pthread_mutex_unlock(&this->config_lock);
	    break;
          default:
            printf ("xine_interface: error, unknown config entry type %d\n", entry->type);
            _x_abort();
          }
	}
      }
    }

    fclose (f_config);
    xine_log(xine, XINE_LOG_MSG,
	     _("Loaded configuration from file '%s'\n"), filename);

  }
  else if (errno != ENOENT)
    xine_log(xine, XINE_LOG_MSG,
	     _("Failed to load configuration from file '%s': %s\n"), filename, strerror (errno));
}

void xine_config_save (xine_t *xine, const char *filename) {

  config_values_t *this = xine->config;
  char             temp[XINE_PATH_MAX];
  int              backup = 0;
  struct stat      backup_stat, config_stat;
  FILE            *f_config, *f_backup;

  snprintf(temp, XINE_PATH_MAX, "%s~", filename);
  unlink (temp);

  if (stat(temp, &backup_stat) != 0) {

    lprintf("backing up configfile to %s\n", temp);

    f_backup = fopen(temp, "wb");
    f_config = fopen(filename, "rb");

    if (f_config && f_backup && (stat(filename, &config_stat) == 0)) {
      char    *buf = NULL;
      size_t   rlen;

      buf = (char *) malloc(config_stat.st_size + 1);
      if((rlen = fread(buf, 1, config_stat.st_size, f_config)) && ((off_t)rlen == config_stat.st_size)) {
	if (rlen != fwrite(buf, 1, rlen, f_backup)) {
	  lprintf("backing up configfile to %s failed\n", temp);
	}
      }
      free(buf);

      fclose(f_config);
      fclose(f_backup);

      if (stat(temp, &backup_stat) == 0 && config_stat.st_size == backup_stat.st_size)
	backup = 1;
      else
	unlink(temp);

    }
    else {

      if (f_config)
        fclose(f_config);
      else
	backup = 1;

      if (f_backup)
        fclose(f_backup);

    }
  }

  if (!backup && (stat(filename, &config_stat) == 0)) {
    xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: backing up configfile to %s failed\n"), temp);
    xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: your configuration will not be saved\n"));
    return;
  }

  lprintf ("writing config file to %s\n", filename);

  f_config = fopen(filename, "w");

  if (f_config) {

    cfg_entry_t *entry;

    fprintf (f_config, "#\n# xine config file\n#\n");
    fprintf (f_config, ".version:%d\n\n", CONFIG_FILE_VERSION);
    fprintf (f_config, "# Entries which are still set to their default values are commented out.\n");
    fprintf (f_config, "# Remove the \'#\' at the beginning of the line, if you want to change them.\n\n");

    pthread_mutex_lock(&this->config_lock);
    entry = this->first;

    while (entry) {

      if (!entry->key[0])
        /* deleted key */
        goto next;

      lprintf ("saving key '%s'\n", entry->key);

      if (entry->description)
	fprintf (f_config, "# %s\n", entry->description);

      switch (entry->type) {
      case XINE_CONFIG_TYPE_UNKNOWN:

/*#if 0*/
	/* discard unclaimed values */
	fprintf (f_config, "%s:%s\n",
		 entry->key, entry->unknown_value);
	fprintf (f_config, "\n");
/*#endif*/

	break;
      case XINE_CONFIG_TYPE_RANGE:
	fprintf (f_config, "# [%d..%d], default: %d\n",
		 entry->range_min, entry->range_max, entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_STRING:
	fprintf (f_config, "# string, default: %s\n",
		 entry->str_default);
	if (strcmp(entry->str_value, entry->str_default) == 0) fprintf (f_config, "#");
	fprintf (f_config, "%s:%s\n", entry->key, entry->str_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_ENUM: {
	char **value;

	fprintf (f_config, "# {");
	value = entry->enum_values;
	while (*value) {
	  fprintf (f_config, " %s ", *value);
	  value++;
	}

	fprintf (f_config, "}, default: %d\n",
		 entry->num_default);

	if (entry->enum_values[entry->num_value] != NULL) {
	  if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	  fprintf (f_config, "%s:", entry->key);
	  fprintf (f_config, "%s\n", entry->enum_values[entry->num_value]);
	}

	fprintf (f_config, "\n");
	break;
      }
      case XINE_CONFIG_TYPE_NUM:
	fprintf (f_config, "# numeric, default: %d\n",
		 entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_BOOL:
	fprintf (f_config, "# bool, default: %d\n",
		 entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      }

      next:
      entry = entry->next;
    }
    pthread_mutex_unlock(&this->config_lock);

    if (fclose(f_config) != 0) {
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: writing configuration to %s failed\n"), filename);
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: removing possibly broken config file %s\n"), filename);
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: you should check the backup file %s\n"), temp);
      /* writing config failed -> remove file, it might be broken ... */
      unlink(filename);
      /* ... but keep the backup */
      backup = 0;
    }
  }

  if (backup)
    unlink(temp);
}

static void config_dispose (config_values_t *this) {

  cfg_entry_t *entry, *last;

  pthread_mutex_lock(&this->config_lock);
  entry = this->first;

  lprintf ("dispose\n");

  while (entry) {
    last = entry;
    entry = entry->next;

    if (last->key)
      free (last->key);
    if (last->unknown_value)
      free (last->unknown_value);

    config_reset_value(last);

    free (last);
  }
  pthread_mutex_unlock(&this->config_lock);

  pthread_mutex_destroy(&this->config_lock);
  free (this);
}


static void config_unregister_cb (config_values_t *this, const char *key) {

  cfg_entry_t *entry;

  _x_assert(key);
  _x_assert(this);

  entry = config_lookup_entry (this, key);
  if (entry) {
    pthread_mutex_lock(&this->config_lock);
    entry->callback = NULL;
    entry->callback_data = NULL;
    pthread_mutex_unlock(&this->config_lock);
  }
}

static void config_set_new_entry_callback (config_values_t *this, xine_config_cb_t new_entry_cb, void* cbdata) {
  pthread_mutex_lock(&this->config_lock);
  this->new_entry_cb = new_entry_cb;
  this->new_entry_cbdata = cbdata;
  pthread_mutex_unlock(&this->config_lock);
}

static void config_unset_new_entry_callback (config_values_t *this) {
  pthread_mutex_lock(&this->config_lock);
  this->new_entry_cb = NULL;
  this->new_entry_cbdata = NULL;
  pthread_mutex_unlock(&this->config_lock);
}

static int put_int(uint8_t *buffer, int pos, int value) {
  int32_t value_int32 = (int32_t)value;

  buffer[pos] = value_int32 & 0xFF;
  buffer[pos + 1] = (value_int32 >> 8) & 0xFF;
  buffer[pos + 2] = (value_int32 >> 16) & 0xFF;
  buffer[pos + 3] = (value_int32 >> 24) & 0xFF;

  return 4;
}

static int put_string(uint8_t *buffer, int pos, const char *value, int value_len) {
  pos += put_int(buffer, pos, value_len);
  memcpy(&buffer[pos], value, value_len);

  return 4 + value_len;
}

static char* config_get_serialized_entry (config_values_t *this, const char *key) {
  char *output = NULL;
  cfg_entry_t *entry, *prev;

  pthread_mutex_lock(&this->config_lock);
  config_lookup_entry_int(this, key, &entry, &prev);

  if (entry) {
    /* now serialize this stuff
      fields to serialize :
          int              type;
          int              range_min;
          int              range_max;
          int              exp_level;
          int              num_default;
          int              num_value;
          char            *key;
          char            *str_default;
          char            *description;
          char            *help;
          char           **enum_values;
    */

    int key_len = 0;
    int str_default_len = 0;
    int description_len = 0;
    int help_len = 0;
    unsigned long total_len;
    int value_count;
    int value_len[10];
    int pos = 0;
    int i;

    if (entry->key)
      key_len = strlen(entry->key);
    if (entry->str_default)
      str_default_len = strlen(entry->str_default);
    if (entry->description)
      description_len = strlen(entry->description);
    if (entry->help)
      help_len = strlen(entry->help);

    /* integers */
    /* value: 4 bytes */
    total_len = 6 * sizeof(int32_t);

    /* strings (size + char buffer)
     * length: 4 bytes
     * buffer: length bytes
     */
    total_len += sizeof(int32_t) + key_len;
    total_len += sizeof(int32_t) + str_default_len;
    total_len += sizeof(int32_t) + description_len;
    total_len += sizeof(int32_t) + help_len;

    /* enum values...
     * value count: 4 bytes
     * for each value:
     *   length: 4 bytes
     *   buffer: length bytes
     */
    value_count = 0;
    total_len += sizeof(int32_t);  /* value count */

    char **cur_value = entry->enum_values;
    if (cur_value) {
      while (*cur_value && (value_count < (sizeof(value_len) / sizeof(int) ))) {
        value_len[value_count] = strlen(*cur_value);
        total_len += sizeof(int32_t) + value_len[value_count];
        value_count++;
        cur_value++;
      }
    }

    /* Now we have the length needed to serialize the entry and the length of each string */
    uint8_t *buffer = malloc (total_len);
    if (!buffer) return NULL;

    /* Let's go */

    /* the integers */
    pos += put_int(buffer, pos, entry->type);
    pos += put_int(buffer, pos, entry->range_min);
    pos += put_int(buffer, pos, entry->range_max);
    pos += put_int(buffer, pos, entry->exp_level);
    pos += put_int(buffer, pos, entry->num_default);
    pos += put_int(buffer, pos, entry->num_value);

    /* the strings */
    pos += put_string(buffer, pos, entry->key, key_len);
    pos += put_string(buffer, pos, entry->str_default, str_default_len);
    pos += put_string(buffer, pos, entry->description, description_len);
    pos += put_string(buffer, pos, entry->help, help_len);

    /* the enum stuff */
    pos += put_int(buffer, pos, value_count);
    cur_value = entry->enum_values;

    for (i = 0; i < value_count; i++) {
      pos += put_string(buffer, pos, *cur_value, value_len[i]);
      cur_value++;
    }

    /* and now the output encoding */
    /* We're going to encode total_len bytes in base64
     * libavutil's base64 encoding functions want the size to
     * be at least len * 4 / 3 + 12, so let's use that!
     */
    output = malloc(total_len * 4 / 3 + 12);
    av_base64_encode(output, total_len * 4 / 3 + 12, buffer, total_len);

    free(buffer);
  }
  pthread_mutex_unlock(&this->config_lock);

  return output;

}

static int get_int(uint8_t *buffer, int buffer_size, int pos, int *value) {
  int32_t value_int32;

  if ((pos + sizeof(int32_t)) > buffer_size)
    return 0;

  value_int32 = _X_LE_32(&buffer[pos]);
  *value = (int)value_int32;
  return sizeof(int32_t);
}

static int get_string(uint8_t *buffer, int buffer_size, int pos, char **value) {
  int len;
  int bytes = get_int(buffer, buffer_size, pos, &len);
  *value = NULL;

  if (!bytes || (len < 0) || (len > 1024*64))
    return 0;

  char *str = malloc(len + 1);
  pos += bytes;
  memcpy(str, &buffer[pos], len);
  str[len] = 0;

  *value = str;
  return bytes + len;
}

static char* config_register_serialized_entry (config_values_t *this, const char *value) {
  /*
      fields serialized :
          int              type;
          int              range_min;
          int              range_max;
          int              exp_level;
          int              num_default;
          int              num_value;
          char            *key;
          char            *str_default;
          char            *description;
          char            *help;
          char           **enum_values;
  */
  int    type;
  int    range_min;
  int    range_max;
  int    exp_level;
  int    num_default;
  int    num_value;
  char  *key = NULL;
  char  *str_default = NULL;
  char  *description = NULL;
  char  *help = NULL;
  char **enum_values = NULL;

  int    bytes;
  int    pos;
  void  *output = NULL;
  size_t output_len;
  int    value_count = 0;
  int    i;

  output_len = strlen(value) * 3 / 4 + 1;
  output = malloc(output_len);
  av_base64_decode(output, value, output_len);

  pos = 0;
  pos += bytes = get_int(output, output_len, pos, &type);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &range_min);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &range_max);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &exp_level);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &num_default);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &num_value);
  if (!bytes) goto exit;

  pos += bytes = get_string(output, output_len, pos, &key);
  if (!bytes) goto exit;

  pos += bytes = get_string(output, output_len, pos, &str_default);
  if (!bytes) goto exit;

  pos += bytes = get_string(output, output_len, pos, &description);
  if (!bytes) goto exit;

  pos += bytes = get_string(output, output_len, pos, &help);
  if (!bytes) goto exit;

  pos += bytes = get_int(output, output_len, pos, &value_count);
  if (!bytes) goto exit;
  if ((value_count < 0) || (value_count > 256)) goto exit;

  enum_values = calloc (value_count + 1, sizeof(void*));
  for (i = 0; i < value_count; i++) {
    pos += bytes = get_string(output, output_len, pos, &enum_values[i]);
    if (!bytes) goto exit;
  }
  enum_values[value_count] = NULL;

#ifdef LOG
  printf("config entry deserialization:\n");
  printf("  key        : %s\n", key);
  printf("  type       : %d\n", type);
  printf("  exp_level  : %d\n", exp_level);
  printf("  num_default: %d\n", num_default);
  printf("  num_value  : %d\n", num_value);
  printf("  str_default: %s\n", str_default);
  printf("  range_min  : %d\n", range_min);
  printf("  range_max  : %d\n", range_max);
  printf("  description: %s\n", description);
  printf("  help       : %s\n", help);
  printf("  enum       : %d values\n", value_count);

  for (i = 0; i < value_count; i++) {
    printf("    enum[%2d]: %s\n", i, enum_values[i]);
  }
  printf("\n");
#endif

  switch (type) {
  case XINE_CONFIG_TYPE_STRING:
    switch (num_value) {
      case 0:
        this->register_string(this, key, str_default, description, help, exp_level, NULL, NULL);
		break;
      default:
        this->register_filename(this, key, str_default, num_value, description, help, exp_level, NULL, NULL);
		break;
    }
    break;
  case XINE_CONFIG_TYPE_RANGE:
    this->register_range(this, key, num_default, range_min, range_max, description, help, exp_level, NULL, NULL);
    break;
  case XINE_CONFIG_TYPE_ENUM:
    this->register_enum(this, key, num_default, enum_values, description, help, exp_level, NULL, NULL);
    break;
  case XINE_CONFIG_TYPE_NUM:
    this->register_num(this, key, num_default, description, help, exp_level, NULL, NULL);
    break;
  case XINE_CONFIG_TYPE_BOOL:
    this->register_bool(this, key, num_default, description, help, exp_level, NULL, NULL);
    break;
  case XINE_CONFIG_TYPE_UNKNOWN:
    break;
  }

exit:
  /* cleanup */
  free(str_default);
  free(description);
  free(help);
  free(output);

  if (enum_values) {
    for (i = 0; i < value_count; i++) {
      free(enum_values[i]);
    }
    free(enum_values);
  }

  return key;
}

config_values_t *_x_config_init (void) {

#ifdef HAVE_IRIXAL
  volatile /* is this a (old, 2.91.66) irix gcc bug?!? */
#endif
  config_values_t *this;
  pthread_mutexattr_t attr;

  if (!(this = calloc(1, sizeof(config_values_t)))) {

    printf ("configfile: could not allocate config object\n");
    _x_abort();
  }

  this->first = NULL;
  this->last  = NULL;
  this->current_version = 0;

  /* warning: config_lock is a recursive mutex. it must NOT be
   * used with neither pthread_cond_wait() or pthread_cond_timedwait()
   */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&this->config_lock, &attr);

  this->register_string           = config_register_string;
  this->register_filename         = config_register_filename;
  this->register_range            = config_register_range;
  this->register_enum             = config_register_enum;
  this->register_num              = config_register_num;
  this->register_bool             = config_register_bool;
  this->register_serialized_entry = config_register_serialized_entry;
  this->update_num                = config_update_num;
  this->update_string             = config_update_string;
  this->parse_enum                = config_parse_enum;
  this->lookup_entry              = config_lookup_entry;
  this->unregister_callback       = config_unregister_cb;
  this->dispose                   = config_dispose;
  this->set_new_entry_callback    = config_set_new_entry_callback;
  this->unset_new_entry_callback  = config_unset_new_entry_callback;
  this->get_serialized_entry      = config_get_serialized_entry;

  return this;
}

int _x_config_change_opt(config_values_t *config, const char *opt) {
  cfg_entry_t *entry;
  int          handled = 0;
  char        *key, *value;

  /* If the configuration is missing, return now rather than trying
   * to dereference it and then check it. */
  if ( ! config || ! opt ) return -1;

  if ((entry = config->lookup_entry(config, "misc.implicit_config")) &&
      entry->type == XINE_CONFIG_TYPE_BOOL) {
    if (!entry->num_value)
      /* changing config entries implicitly is denied */
      return -1;
  } else
    /* someone messed with the config entry */
    return -1;

  key = strdup(opt);
  if ( !key || *key == '\0' ) return 0;

  value = strrchr(key, ':');
  if ( !value || *value == '\0' ) {
    free(key);
    return 0;
  }

  *value++ = '\0';

  entry = config->lookup_entry(config, key);
  if ( ! entry ) {
    free(key);
    return -1;
  }

  if(entry->exp_level >= XINE_CONFIG_SECURITY) {
    printf(_("configfile: entry '%s' mustn't be modified from MRL\n"), key);
    free(key);
    return -1;
  }

  switch(entry->type) {
  case XINE_CONFIG_TYPE_STRING:
    config->update_string(config, key, value);
    handled = 1;
    break;

  case XINE_CONFIG_TYPE_RANGE:
  case XINE_CONFIG_TYPE_ENUM:
  case XINE_CONFIG_TYPE_NUM:
  case XINE_CONFIG_TYPE_BOOL:
    config->update_num(config, key, (atoi(value)));
    handled = 1;
    break;

  case XINE_CONFIG_TYPE_UNKNOWN:
    entry->unknown_value = strdup(value);
    handled = 1;
    break;
  }

  free(key);
  return handled;
}

