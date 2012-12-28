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
 * xine video decoder plugin interface
 */

#ifndef HAVE_VIDEO_DECODER_H
#define HAVE_VIDEO_DECODER_H

#include <xine/os_types.h>
#include <xine/buffer.h>

#ifdef XINE_COMPILE
#  include <xine/plugin_catalog.h>
#endif

#define VIDEO_DECODER_IFACE_VERSION 19


/*
 * generic xine video decoder plugin interface
 */

typedef struct video_decoder_class_s video_decoder_class_t;
typedef struct video_decoder_s video_decoder_t;

struct video_decoder_class_s {

  /*
   * open a new instance of this plugin class
   */
  video_decoder_t* (*open_plugin) (video_decoder_class_t *self, xine_stream_t *stream);

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
  void (*dispose) (video_decoder_class_t *self);
};

#define default_video_decoder_class_dispose (void (*) (video_decoder_class_t *this))free

struct video_decoder_s {

  /*
   * decode data from buf and feed decoded frames to
   * video output
   */
  void (*decode_data) (video_decoder_t *self, buf_element_t *buf);

  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
  void (*reset) (video_decoder_t *self);

  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  void (*discontinuity) (video_decoder_t *self);

  /*
   * flush out any frames that are still stored in the decoder
   */
  void (*flush) (video_decoder_t *self);

  /*
   * close down, free all resources
   */
  void (*dispose) (video_decoder_t *self);

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
