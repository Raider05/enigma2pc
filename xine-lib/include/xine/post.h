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
 * post plugin definitions
 */

#ifndef XINE_POST_H
#define XINE_POST_H

#include <xine.h>
#include <xine/video_out.h>
#include <xine/audio_out.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#ifdef XINE_COMPILE
#  include <xine/plugin_catalog.h>
#endif

#define POST_PLUGIN_IFACE_VERSION 10


typedef struct post_class_s post_class_t;
typedef struct post_plugin_s post_plugin_t;
typedef struct post_in_s post_in_t;
typedef struct post_out_s post_out_t;

struct post_class_s {

  /*
   * open a new instance of this plugin class
   */
  post_plugin_t* (*open_plugin) (post_class_t *this, int inputs,
				 xine_audio_port_t **audio_target,
				 xine_video_port_t **video_target);

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

  void (*dispose) (post_class_t *this);
};

#define default_post_class_dispose (void (*) (post_class_t *this))free

struct post_plugin_s {

  /* public part of the plugin */
  xine_post_t         xine_post;

  /*
   * the connections announced by the plugin
   * the plugin must fill these with xine_post_{in,out}_t on init
   */
  xine_list_t        *input;
  xine_list_t        *output;

  /*
   * close down, free all resources
   */
  void (*dispose) (post_plugin_t *this);

  /* plugins don't have to init the stuff below */

  /*
   * the running ticket
   *
   * the plugin must assure to check for ticket revocation in
   * intervals of finite length; this means that you must release
   * the ticket before any operation that might block;
   * note that all port functions are safe in this respect
   *
   * the running ticket is assigned to you by the engine
   */
  xine_ticket_t      *running_ticket;

  /* this is needed by the engine to decrement the reference counter
   * on disposal of the plugin, but since this is useful, we expose it */
  xine_t             *xine;

  /* used when the user requests a list of all inputs/outputs */
  const char        **input_ids;
  const char        **output_ids;

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

  /* has dispose been called */
  int                 dispose_pending;
};

/* helper function to initialize a post_plugin_t */
void _x_post_init(post_plugin_t *post, int num_audio_inputs, int num_video_inputs) XINE_PROTECTED;

struct post_in_s {

  /* public part of the input */
  xine_post_in_t   xine_in;

  /* backward reference so that you have access to the post plugin */
  post_plugin_t   *post;

  /* you can fill this to your liking */
  void            *user_data;
};

struct post_out_s {

  /* public part of the output */
  xine_post_out_t  xine_out;

  /* backward reference so that you have access to the post plugin */
  post_plugin_t   *post;

  /* you can fill this to your liking */
  void            *user_data;
};


/* Post plugins work by intercepting calls to video or audio ports
 * in the sense of the decorator design pattern. They reuse the
 * functions of a given target port, but add own functionality in
 * front of that port by creating a new port structure and filling in
 * the function pointers with pointers to own functions that
 * would do something and then call the original port function.
 *
 * Much the same is done with video frames which have their own
 * set of functions attached that you might need to decorate.
 */


/* helper structure for intercepting video port calls */
typedef struct post_video_port_s post_video_port_t;
struct post_video_port_s {

  /* the new public port with replaced function pointers */
  xine_video_port_t         new_port;

  /* the original port to call its functions from inside yours */
  xine_video_port_t        *original_port;

  /* if you want to decide yourself, whether a given frame should
   * be intercepted, fill in this function; get_frame() acts as
   * a template method and asks your function; return a boolean;
   * the default is to intercept all frames */
  int (*intercept_frame)(post_video_port_t *self, vo_frame_t *frame);

  /* the new frame function pointers */
  vo_frame_t               *new_frame;

  /* if you want to decide yourself, whether the preprocessing functions
   * should still be routed when draw is intercepted, fill in this
   * function; _x_post_intercept_video_frame() acts as a template method
   * and asks your function; return a boolean; the default is _not_ to
   * route preprocessing functions when draw is intercepted */
  int (*route_preprocessing_procs)(post_video_port_t *self, vo_frame_t *frame);

  /* if you want to decide yourself, whether the overlay manager should
   * be intercepted, fill in this function; get_overlay_manager() acts as
   * a template method and asks your function; return a boolean;
   * the default is _not_ to intercept the overlay manager */
  int (*intercept_ovl)(post_video_port_t *self);

  /* the new public overlay manager with replaced function pointers */
  video_overlay_manager_t  *new_manager;

  /* the original manager to call its functions from inside yours */
  video_overlay_manager_t  *original_manager;

  /* usage counter: how many objects are floating around that need
   * these pointers to exist */
  int                       usage_count;
  pthread_mutex_t           usage_lock;

  /* the stream we are being fed by; NULL means no stream is connected;
   * this may be an anonymous stream */
  xine_stream_t            *stream;

  /* point to a mutex here, if you need some synchronization */
  pthread_mutex_t          *port_lock;
  pthread_mutex_t          *frame_lock;
  pthread_mutex_t          *manager_lock;

  /* backward reference so that you have access to the post plugin
   * when the call only gives you the port */
  post_plugin_t            *post;

  /* you can fill this to your liking */
  void                     *user_data;

#ifdef POST_INTERNAL
  /* some of the above members are to be directly included here, but
   * adding the structures would mean that post_video_port_t becomes
   * depended of the sizes of these structs; solution: we add pointers
   * above and have them point into the memory provided here;
   * note that the overlay manager needs to be first so that we can
   * reconstruct the post_video_port_t* from overlay manager calls */

  /* any change here requires a change in _x_post_ovl_manager_to_port()
   * below! */

  video_overlay_manager_t   manager_storage;
  vo_frame_t                frame_storage;

  /* this is used to keep a linked list of free vo_frame_t's */
  vo_frame_t               *free_frame_slots;
  pthread_mutex_t           free_frames_lock;
#endif
};

/* use this to create a new decorated video port in which
 * port functions will be replaced with own implementations;
 * for convenience, this can also create a related post_in_t and post_out_t */
post_video_port_t *_x_post_intercept_video_port(post_plugin_t *post, xine_video_port_t *port,
						post_in_t **input, post_out_t **output) XINE_PROTECTED;

/* use this to decorate and to undecorate a frame so that its functions
 * can be replaced with own implementations, decoration is usually done in
 * get_frame(), undecoration in frame->free() */
vo_frame_t *_x_post_intercept_video_frame(vo_frame_t *frame, post_video_port_t *port) XINE_PROTECTED;
vo_frame_t *_x_post_restore_video_frame(vo_frame_t *frame, post_video_port_t *port) XINE_PROTECTED;

/* when you want to pass a frame call on to the original issuer of the frame,
 * you need to propagate potential changes up and down the pipe, so the usual
 * procedure for this situation would be:
 *
 *   _x_post_frame_copy_down(frame, frame->next);
 *   frame->next->function(frame->next);
 *   _x_post_frame_copy_up(frame, frame->next);
 */
void _x_post_frame_copy_down(vo_frame_t *from, vo_frame_t *to) XINE_PROTECTED;
void _x_post_frame_copy_up(vo_frame_t *to, vo_frame_t *from) XINE_PROTECTED;

/* when you shortcut a frames usual draw() travel so that it will never reach
 * the draw() function of the original issuer, you still have to do some
 * housekeeping on the frame, before returning control up the pipe */
void _x_post_frame_u_turn(vo_frame_t *frame, xine_stream_t *stream) XINE_PROTECTED;

/* use this to create a new, trivially decorated overlay manager in which
 * port functions can be replaced with own implementations */
void _x_post_intercept_overlay_manager(video_overlay_manager_t *manager, post_video_port_t *port) XINE_PROTECTED;

/* pointer retrieval functions */
static inline post_video_port_t *_x_post_video_frame_to_port(vo_frame_t *frame) {
  return (post_video_port_t *)frame->port;
}

static inline post_video_port_t *_x_post_ovl_manager_to_port(video_overlay_manager_t *manager) {
#ifdef POST_INTERNAL
  return (post_video_port_t *)( (uint8_t *)manager -
    (uint8_t*)&(((post_video_port_t *)NULL)->manager_storage) );
#else
  return (post_video_port_t *)( (uint8_t *)manager - sizeof(post_video_port_t) );
#endif
}


/* helper structure for intercepting audio port calls */
typedef struct post_audio_port_s post_audio_port_t;
struct post_audio_port_s {

  /* the new public port with replaced function pointers */
  xine_audio_port_t  new_port;

  /* the original port to call its functions from inside yours */
  xine_audio_port_t *original_port;

  /* the stream we are being fed by; NULL means no stream is connected;
   * this may be an anonymous stream */
  xine_stream_t     *stream;

  pthread_mutex_t    usage_lock;
  /* usage counter: how many objects are floating around that need
   * these pointers to exist */
  int                usage_count;

  /* some values remembered by (port->open) () */
  uint32_t           bits;
  uint32_t           rate;
  uint32_t           mode;

  /* point to a mutex here, if you need some synchronization */
  pthread_mutex_t   *port_lock;

  /* backward reference so that you have access to the post plugin
   * when the call only gives you the port */
  post_plugin_t     *post;

  /* you can fill this to your liking */
  void              *user_data;
};

/* use this to create a new decorated audio port in which
 * port functions will be replaced with own implementations */
post_audio_port_t *_x_post_intercept_audio_port(post_plugin_t *post, xine_audio_port_t *port,
						post_in_t **input, post_out_t **output) XINE_PROTECTED;


/* this will allow pending rewire operations, calling this at the beginning
 * of decoder-called functions like get_buffer() and open() is a good idea
 * (if you do not intercept get_buffer() or open(), this will be done automatically) */
static inline void _x_post_rewire(post_plugin_t *post) {
  if (post->running_ticket->ticket_revoked)
    post->running_ticket->renew(post->running_ticket, 1);
}

/* with these functions you can switch interruptions like rewiring or engine pausing
 * off for a block of code; use this only when really necessary */
static inline void _x_post_lock(post_plugin_t *post) {
  post->running_ticket->acquire(post->running_ticket, 1);
}
static inline void _x_post_unlock(post_plugin_t *post) {
  post->running_ticket->release(post->running_ticket, 1);
  _x_post_rewire(post);
}

/* the standard disposal operation; returns 1 if the plugin is really
 * disposed and you should free everything you malloc()ed yourself */
int _x_post_dispose(post_plugin_t *post) XINE_PROTECTED;


/* macros to handle usage counter */

/* WARNING!
 * note that _x_post_dec_usage() can call dispose, so be sure to
 * not use any potentially already freed memory after this */

#define _x_post_inc_usage(port)                                    \
do {                                                               \
  pthread_mutex_lock(&(port)->usage_lock);                         \
  (port)->usage_count++;                                           \
  pthread_mutex_unlock(&(port)->usage_lock);                       \
} while(0)

#define _x_post_dec_usage(port)                                    \
do {                                                               \
  pthread_mutex_lock(&(port)->usage_lock);                         \
  (port)->usage_count--;                                           \
  if ((port)->usage_count == 0) {                                  \
    if ((port)->post->dispose_pending) {                           \
      pthread_mutex_unlock(&(port)->usage_lock);                   \
      (port)->post->dispose((port)->post);                         \
    } else                                                         \
      pthread_mutex_unlock(&(port)->usage_lock);                   \
  } else                                                           \
    pthread_mutex_unlock(&(port)->usage_lock);                     \
} while(0)


/* macros to create parameter descriptors */

#define START_PARAM_DESCR( param_t ) \
static param_t temp_s; \
static xine_post_api_parameter_t temp_p[] = {

#define PARAM_ITEM( param_type, var, enumv, min, max, readonly, descr ) \
{ param_type, #var, sizeof(temp_s.var), \
  (char*)&temp_s.var-(char*)&temp_s, enumv, min, max, readonly, descr },

#define END_PARAM_DESCR( name ) \
  { POST_PARAM_TYPE_LAST, NULL, 0, 0, NULL, 0, 0, 1, NULL } \
}; \
static xine_post_api_descr_t name = { \
  sizeof( temp_s ), \
  temp_p \
};

#endif
