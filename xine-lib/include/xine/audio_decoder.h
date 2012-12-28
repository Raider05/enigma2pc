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
 * xine audio decoder plugin interface
 */

#ifndef HAVE_AUDIO_DECODER_H
#define HAVE_AUDIO_DECODER_H

#include <xine/os_types.h>
#include <xine/buffer.h>

#ifdef XINE_COMPILE
#  include <xine/plugin_catalog.h>
#endif

#define AUDIO_DECODER_IFACE_VERSION 16

/*
 * generic xine audio decoder plugin interface
 */

typedef struct audio_decoder_class_s audio_decoder_class_t;
typedef struct audio_decoder_s audio_decoder_t;

struct audio_decoder_class_s {

  /*
   * open a new instance of this plugin class
   */
  audio_decoder_t* (*open_plugin) (audio_decoder_class_t *self, xine_stream_t *stream);

  /**
   * @brief short human readable identifier for this plugin class
   */
  const char *identifier;

  /**
   * @brief human readable (verbose = 1 line) description for this plugin class
   *
   * The description is passed to gettext() to internationalise.
   */
  const char *description;

  /**
   * @brief Optional non-standard catalog to use with dgettext() for description.
   */
  const char *text_domain;

  /*
   * free all class-related resources
   */

  void (*dispose) (audio_decoder_class_t *self);
};

#define default_audio_decoder_class_dispose (void (*) (audio_decoder_class_t *this))free

struct audio_decoder_s {

  /*
   * decode data from buf and feed decoded samples to
   * audio output
   */
  void (*decode_data) (audio_decoder_t *self, buf_element_t *buf);

  /*
   * reset decoder after engine flush (prepare for new
   * audio data not related to recently decoded data)
   */
  void (*reset) (audio_decoder_t *self);

  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  void (*discontinuity) (audio_decoder_t *self);

  /*
   * close down, free all resources
   */
  void (*dispose) (audio_decoder_t *self);

  /**
   * @brief Pointer to the loaded plugin node.
   *
   * Used by the plugins loader. It's an opaque type when using the
   * structure outside of xine's build.
   */
#ifdef XINE_COMPILE
  plugin_node_t *node;
#else
  void *node;
#endif
};

#endif
