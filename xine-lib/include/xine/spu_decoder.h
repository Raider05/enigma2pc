/*
 * spu_decoder_api.h
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * This file is part of xine, a unix video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation,
 *
 */

#ifndef HAVE_SPU_API_H
#define HAVE_SPU_API_H

#include <xine/os_types.h>
#include <xine/buffer.h>

#ifdef XINE_COMPILE
#  include <xine/plugin_catalog.h>
#endif

#define SPU_DECODER_IFACE_VERSION 17

/*
 * generic xine spu decoder plugin interface
 */

typedef struct spu_decoder_class_s spu_decoder_class_t;
typedef struct spu_decoder_s spu_decoder_t;

struct spu_decoder_class_s {

  /*
   * open a new instance of this plugin class
   */
  spu_decoder_t* (*open_plugin) (spu_decoder_class_t *self, xine_stream_t *stream);

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
  void (*dispose) (spu_decoder_class_t *self);
};

#define default_spu_decoder_class_dispose (void (*) (spu_decoder_class_t *this))free

struct spu_decoder_s {

  /*
   * decode data from buf and feed the overlay to overlay manager
   */
  void (*decode_data) (spu_decoder_t *self, buf_element_t *buf);

  /*
   * reset decoder after engine flush (prepare for new
   * SPU data not related to recently decoded data)
   */
  void (*reset) (spu_decoder_t *self);

  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  void (*discontinuity) (spu_decoder_t *self);

  /*
   * close down, free all resources
   */
  void (*dispose) (spu_decoder_t *self);

  /*
   * When the SPU decoder also handles data used in user interaction,
   * you can query the related information here. The typical example
   * for this is DVD NAV packets which are handled by the SPU decoder
   * and can be received readily parsed from here.
   * The caller and the decoder must agree on the structure which is
   * passed here.
   * This function pointer may be NULL, if the plugin does not have
   * such functionality.
   */
  int  (*get_interact_info) (spu_decoder_t *self, void *data);

  /*
   * When the SPU decoder also handles menu overlays for user inter-
   * action, you can set a menu button here. The typical example for
   * this is DVD menus.
   * This function pointer may be NULL, if the plugin does not have
   * such functionality.
   */
  void (*set_button) (spu_decoder_t *this_gen, int32_t button, int32_t mode);

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


/* SPU decoders differ from video and audio decoders in one significant
 * way: unlike audio and video, SPU streams are not continuous;
 * this results in another difference, programmers have to consider:
 * while both audio and video decoders are automatically blocked in
 * their get_buffer()/get_frame() methods when the output cannot take
 * any more data, this does not work for SPU, because it could take
 * minutes before the next free slot becomes available and we must not
 * block the decoder thread for that long;
 * therefore, we provide a convenience function for SPU decoders which
 * implements a wait until a timestamp sufficiently close to the VPTS
 * of the next SPU is reached, but the waiting will end before that,
 * if some outside condition requires us to release the decoder thread
 * to other tasks;
 * if this functions returns with 1, noone needs the decoder thread and
 * you may continue waiting; if it returns 0, finish whatever you are
 * doing and return;
 * the usual pattern for SPU decoders is this:
 *
 * do {
 *   spu = prepare_spu();
 *   int thread_vacant = _x_spu_decoder_sleep(this->stream, spu->vpts);
 *   int success = process_spu(spu);
 * } while (!success && thread_vacant);
 */
int _x_spu_decoder_sleep(xine_stream_t *, int64_t next_spu_vpts) XINE_PROTECTED;

#endif /* HAVE_SPUDEC_H */
