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
 * config file management
 */

#ifndef HAVE_CONFIGFILE_H
#define HAVE_CONFIGFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

#include <xine.h>

#define CONFIG_FILE_VERSION 2

/**
 * config entries above this experience
 * level must never be changed from MRL
 */
#define XINE_CONFIG_SECURITY 30


typedef struct cfg_entry_s cfg_entry_t;
typedef struct config_values_s config_values_t;

struct cfg_entry_s {
  cfg_entry_t     *next;
  config_values_t *config;

  char            *key;
  int              type;

  /** user experience level */
  int              exp_level;

  /** type unknown */
  char            *unknown_value;

  /** type string */
  char            *str_value;
  char            *str_default;

  /** common to range, enum, num, bool: */
  int              num_value;
  int              num_default;

  /** type range specific: */
  int              range_min;
  int              range_max;

  /** type enum specific: */
  char           **enum_values;

  /** help info for the user */
  char            *description;
  char            *help;

  /** callback function and data for live changeable values */
  xine_config_cb_t callback;
  void            *callback_data;
};

struct config_values_s {

  /*
   * register config values
   *
   * these functions return the current value of the
   * registered item, i.e. the default value if it was
   * not found in the config file or the current value
   * from the config file otherwise
   */

  char* (*register_string) (config_values_t *self,
			    const char *key,
			    const char *def_value,
			    const char *description,
			    const char *help,
			    int exp_level,
			    xine_config_cb_t changed_cb,
			    void *cb_data);

  char* (*register_filename) (config_values_t *self,
			      const char *key,
			      const char *def_value,
			      int req_type,
			      const char *description,
			      const char *help,
			      int exp_level,
			      xine_config_cb_t changed_cb,
			      void *cb_data);

  int (*register_range) (config_values_t *self,
			 const char *key,
			 int def_value,
			 int min, int max,
			 const char *description,
			 const char *help,
			 int exp_level,
			 xine_config_cb_t changed_cb,
			 void *cb_data);

  int (*register_enum) (config_values_t *self,
			const char *key,
			int def_value,
			char **values,
			const char *description,
			const char *help,
			int exp_level,
			xine_config_cb_t changed_cb,
			void *cb_data);

  int (*register_num) (config_values_t *self,
		       const char *key,
		       int def_value,
		       const char *description,
		       const char *help,
		       int exp_level,
		       xine_config_cb_t changed_cb,
		       void *cb_data);

  int (*register_bool) (config_values_t *self,
			const char *key,
			int def_value,
			const char *description,
			const char *help,
			int exp_level,
			xine_config_cb_t changed_cb,
			void *cb_data);

  void (*register_entry) (config_values_t *self, cfg_entry_t* entry);

  /** convenience function to update range, enum, num and bool values */
  void (*update_num) (config_values_t *self, const char *key, int value);

  /** convenience function to update string values */
  void (*update_string) (config_values_t *self, const char *key, const char *value);

  /** small utility function for enum handling */
  int (*parse_enum) (const char *str, const char **values);

  /**
   * @brief lookup config entries
   *
   * remember to call the changed_cb if it exists
   * and you changed the value of this item
   */

  cfg_entry_t* (*lookup_entry) (config_values_t *self, const char *key);

  /**
   * unregister entry callback function
   */
  void (*unregister_callback) (config_values_t *self, const char *key);

  /**
   * dispose of all config entries in memory
   */
  void (*dispose) (config_values_t *self);

  /**
   * callback called when a new config entry is registered
   */
  void (*set_new_entry_callback) (config_values_t *self, xine_config_cb_t new_entry_cb, void *cb_data);

  /**
   * unregister the callback
   */
  void (*unset_new_entry_callback) (config_values_t *self);

  /**
   * serialize a config entry.
   * return a base64 null terminated string.
   */
  char* (*get_serialized_entry) (config_values_t *self, const char *key);

  /**
   * deserialize a config entry.
   * value is a base 64 encoded string
   * return the key of the serialized entry
   */
  char* (*register_serialized_entry) (config_values_t *self, const char *value);

  /**
   * config values are stored here:
   */
  cfg_entry_t         *first, *last, *cur;

  /**
   * new entry callback
   */
  xine_config_cb_t    new_entry_cb;
  void                *new_entry_cbdata;

  /**
   * mutex for modification to the config
   */
  pthread_mutex_t      config_lock;

  /**
   * current config file's version number
   */
  int current_version;
};

/**
 * @brief allocate and init a new xine config object
 * @internal
 */
config_values_t *_x_config_init (void) XINE_MALLOC;

/**
 * @brief interpret stream_setup part of mrls for config value changes
 * @internal
 */

int _x_config_change_opt(config_values_t *config, const char *opt);


#ifdef __cplusplus
}
#endif

#endif

