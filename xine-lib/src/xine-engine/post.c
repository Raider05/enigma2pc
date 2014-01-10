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
 */

/*
 * some helper functions for post plugins
 */

#define POST_INTERNAL
#define XINE_ENGINE_INTERNAL

#include <xine/post.h>
#include <stdarg.h>


void _x_post_init(post_plugin_t *post, int num_audio_inputs, int num_video_inputs) {
  post->input  = xine_list_new();
  post->output = xine_list_new();
  post->xine_post.audio_input = calloc(num_audio_inputs + 1, sizeof(xine_audio_port_t *));
  post->xine_post.video_input = calloc(num_video_inputs + 1, sizeof(xine_video_port_t *));
}


/* dummy intercept functions that just pass the call on to the original port */
static uint32_t post_video_get_capabilities(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  uint32_t caps;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  caps = port->original_port->get_capabilities(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return caps;
}

static void post_video_open(xine_video_port_t *port_gen, xine_stream_t *stream) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  _x_post_rewire(port->post);
  _x_post_inc_usage(port);
  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  (port->original_port->open) (port->original_port, stream);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  port->stream = stream;
}

static vo_frame_t *post_video_get_frame(xine_video_port_t *port_gen, uint32_t width,
    uint32_t height, double ratio, int format, int flags) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t *frame;

  _x_post_rewire(port->post);
  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio, format, flags);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);

  if (frame && (!port->intercept_frame || port->intercept_frame(port, frame))) {
    _x_post_inc_usage(port);
    if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
    frame = _x_post_intercept_video_frame(frame, port);
    if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
  }

  return frame;
}

static vo_frame_t *post_video_get_last_frame(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t *frame;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  frame = port->original_port->get_last_frame(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return frame;
}

static xine_grab_video_frame_t *post_video_new_grab_video_frame(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  xine_grab_video_frame_t *frame;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  frame = port->original_port->new_grab_video_frame(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return frame;
}

static void post_video_enable_ovl(xine_video_port_t *port_gen, int ovl_enable) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->enable_ovl(port->original_port, ovl_enable);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static void post_video_close(xine_video_port_t *port_gen, xine_stream_t *stream) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->close(port->original_port, stream);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  port->stream = NULL;
  _x_post_dec_usage(port);
}

static void post_video_exit(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->exit(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static video_overlay_manager_t *post_video_get_overlay_manager(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  video_overlay_manager_t *manager;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  manager = port->original_port->get_overlay_manager(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);

  if (port->intercept_ovl && port->intercept_ovl(port)) {
    if (manager && !port->original_manager)
      /* this is the first access to overlay manager */
      _x_post_intercept_overlay_manager(manager, port);
    else
      /* the original port might have changed */
      port->original_manager = manager;
    return port->new_manager;
  } else
    return manager;
}

static void post_video_flush(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->flush(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static void post_video_trigger_drawing(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->trigger_drawing(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static int post_video_status(xine_video_port_t *port_gen, xine_stream_t *stream,
                             int *width, int *height, int64_t *img_duration) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  int status;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  status = port->original_port->status(port->original_port, stream, width, height, img_duration);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return status;
}

static int post_video_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  int prop;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  prop = port->original_port->get_property(port->original_port, property);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return prop;
}

static int post_video_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  int val;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  val = port->original_port->set_property(port->original_port, property, value);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return val;
}


static int post_video_rewire(xine_post_out_t *output_gen, void *data) {
  post_out_t        *output     = (post_out_t *)output_gen;
  xine_video_port_t *new_port   = (xine_video_port_t *)data;
  post_video_port_t *input_port = (post_video_port_t *)output->user_data;
  post_plugin_t     *this       = output->post;
  int64_t img_duration;
  int width, height;

  if (!new_port)
    return 0;

  this->running_ticket->lock_port_rewiring(this->running_ticket, -1);
  this->running_ticket->revoke(this->running_ticket, 1);

  if (input_port->original_port->status(input_port->original_port, input_port->stream,
      &width, &height, &img_duration)) {
    (new_port->open) (new_port, input_port->stream);
    input_port->original_port->close(input_port->original_port, input_port->stream);
  }
  input_port->original_port = new_port;

  this->running_ticket->issue(this->running_ticket, 1);
  this->running_ticket->unlock_port_rewiring(this->running_ticket);

  return 1;
}


post_video_port_t *_x_post_intercept_video_port(post_plugin_t *post, xine_video_port_t *original,
						post_in_t **input, post_out_t **output) {
  post_video_port_t *port = calloc(1, sizeof(post_video_port_t));

  if (!port)
    return NULL;

  port->new_port.get_capabilities    = post_video_get_capabilities;
  port->new_port.open                = post_video_open;
  port->new_port.get_frame           = post_video_get_frame;
  port->new_port.get_last_frame      = post_video_get_last_frame;
  port->new_port.new_grab_video_frame = post_video_new_grab_video_frame;
  port->new_port.enable_ovl          = post_video_enable_ovl;
  port->new_port.close               = post_video_close;
  port->new_port.exit                = post_video_exit;
  port->new_port.get_overlay_manager = post_video_get_overlay_manager;
  port->new_port.flush               = post_video_flush;
  port->new_port.trigger_drawing     = post_video_trigger_drawing;
  port->new_port.status              = post_video_status;
  port->new_port.get_property        = post_video_get_property;
  port->new_port.set_property        = post_video_set_property;
  port->new_port.driver              = original->driver;

  port->original_port                = original;
  port->new_frame                    = &port->frame_storage;
  port->new_manager                  = &port->manager_storage;
  port->post                         = post;

  pthread_mutex_init(&port->usage_lock, NULL);
  pthread_mutex_init(&port->free_frames_lock, NULL);

  if (input) {
    *input = calloc(1, sizeof(post_in_t));
    if (!*input) return port;
    (*input)->xine_in.name = "video in";
    (*input)->xine_in.type = XINE_POST_DATA_VIDEO;
    (*input)->xine_in.data = &port->new_port;
    (*input)->post = post;
    xine_list_push_back(post->input, *input);
  }

  if (output) {
    *output = calloc(1, sizeof(post_out_t));
    if (!*output) return port;
    (*output)->xine_out.name = "video out";
    (*output)->xine_out.type = XINE_POST_DATA_VIDEO;
    (*output)->xine_out.data = &port->original_port;
    (*output)->xine_out.rewire = post_video_rewire;
    (*output)->post = post;
    (*output)->user_data = port;
    xine_list_push_back(post->output, *output);
  }

  return port;
}


/* dummy intercept functions for frames */
static void post_frame_free(vo_frame_t *vo_img) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  if (--vo_img->lock_counter == 0) {
    /* this frame is free */
    vo_img = _x_post_restore_video_frame(vo_img, port);
    vo_img->free(vo_img);
    if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
    _x_post_dec_usage(port);
  } else if (vo_img->next) {
    /* this frame is still in use */
    _x_post_frame_copy_down(vo_img, vo_img->next);
    vo_img->next->free(vo_img->next);
    _x_post_frame_copy_up(vo_img, vo_img->next);
    if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
  }
}

static void post_frame_proc_slice(vo_frame_t *vo_img, uint8_t **src) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  _x_post_frame_copy_down(vo_img, vo_img->next);
  vo_img->next->proc_slice(vo_img->next, src);
  _x_post_frame_copy_up(vo_img, vo_img->next);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
}

static void post_frame_proc_frame(vo_frame_t *vo_img) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  _x_post_frame_copy_down(vo_img, vo_img->next);
  vo_img->next->proc_frame(vo_img->next);
  _x_post_frame_copy_up(vo_img, vo_img->next);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
}

static void post_frame_field(vo_frame_t *vo_img, int which_field) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  _x_post_frame_copy_down(vo_img, vo_img->next);
  vo_img->next->field(vo_img->next, which_field);
  _x_post_frame_copy_up(vo_img, vo_img->next);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
}

static int post_frame_draw(vo_frame_t *vo_img, xine_stream_t *stream) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);
  int skip;

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  _x_post_frame_copy_down(vo_img, vo_img->next);
  skip = vo_img->next->draw(vo_img->next, stream);
  _x_post_frame_copy_up(vo_img, vo_img->next);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
  return skip;
}

static void post_frame_lock(vo_frame_t *vo_img) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  _x_post_frame_copy_down(vo_img, vo_img->next);
  vo_img->lock_counter++;
  vo_img->next->lock(vo_img->next);
  _x_post_frame_copy_up(vo_img, vo_img->next);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
}

static void post_frame_dispose(vo_frame_t *vo_img) {
  post_video_port_t *port = _x_post_video_frame_to_port(vo_img);

  if (port->frame_lock) pthread_mutex_lock(port->frame_lock);
  vo_img = _x_post_restore_video_frame(vo_img, port);
  vo_img->dispose(vo_img);
  if (port->frame_lock) pthread_mutex_unlock(port->frame_lock);
  _x_post_dec_usage(port);
}


vo_frame_t *_x_post_intercept_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  vo_frame_t *new_frame;

  /* get a free frame slot */
  pthread_mutex_lock(&port->free_frames_lock);
  if (port->free_frame_slots) {
    new_frame = port->free_frame_slots;
    port->free_frame_slots = new_frame->next;
  } else {
    new_frame = calloc(1, sizeof(vo_frame_t));
  }
  pthread_mutex_unlock(&port->free_frames_lock);

  /* make a copy and attach the original */
  xine_fast_memcpy(new_frame, frame, sizeof(vo_frame_t));
  new_frame->next = frame;

  if (new_frame->stream)
    _x_refcounter_inc(new_frame->stream->refcounter);

  /* modify the frame with the intercept functions */
  new_frame->port             = &port->new_port;
  new_frame->proc_frame       =
    port->new_frame->proc_frame       ? port->new_frame->proc_frame       : NULL;
  new_frame->proc_slice       =
    port->new_frame->proc_slice       ? port->new_frame->proc_slice       : NULL;
  new_frame->field            =
    port->new_frame->field            ? port->new_frame->field            : post_frame_field;
  new_frame->draw             =
    port->new_frame->draw             ? port->new_frame->draw             : post_frame_draw;
  new_frame->lock             =
    port->new_frame->lock             ? port->new_frame->lock             : post_frame_lock;
  new_frame->free             =
    port->new_frame->free             ? port->new_frame->free             : post_frame_free;
  new_frame->dispose          =
    port->new_frame->dispose          ? port->new_frame->dispose          : post_frame_dispose;

  if (!port->new_frame->draw || (port->route_preprocessing_procs && port->route_preprocessing_procs(port, frame))) {
    /* draw will most likely modify the frame, so the decoder
     * should only request preprocessing when there is no new draw
     * but route_preprocessing_procs() can override this decision */
    if (frame->proc_frame       && !new_frame->proc_frame)
      new_frame->proc_frame       = post_frame_proc_frame;
    if (frame->proc_slice       && !new_frame->proc_slice)
      new_frame->proc_slice       = post_frame_proc_slice;
  }

  return new_frame;
}

vo_frame_t *_x_post_restore_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  /* the first attched context is the original frame */
  vo_frame_t *original = frame->next;

  /* propagate any changes */
  _x_post_frame_copy_down(frame, original);

  if (frame->stream)
    _x_refcounter_dec(frame->stream->refcounter);

  /* put the now free slot into the free frames list */
  pthread_mutex_lock(&port->free_frames_lock);
  frame->next = port->free_frame_slots;
  port->free_frame_slots = frame;
  pthread_mutex_unlock(&port->free_frames_lock);

  return original;
}

void _x_post_frame_copy_down(vo_frame_t *from, vo_frame_t *to) {
  /* propagate changes downwards (from decoders to video out) */
  if (from->stream)
    _x_refcounter_inc(from->stream->refcounter);
  if (to->stream)
    _x_refcounter_dec(to->stream->refcounter);

  to->pts                 = from->pts;
  to->bad_frame           = from->bad_frame;
  to->duration            = from->duration;
  to->top_field_first     = from->top_field_first;
  to->repeat_first_field  = from->repeat_first_field;
  to->progressive_frame   = from->progressive_frame;
  to->picture_coding_type = from->picture_coding_type;
  to->drawn               = from->drawn;
  to->stream              = from->stream;
  to->crop_left           = from->crop_left;
  to->crop_right          = from->crop_right;
  to->crop_top            = from->crop_top;
  to->crop_bottom         = from->crop_bottom;
  to->ratio               = from->ratio;

  if (to->extra_info != from->extra_info)
    _x_extra_info_merge(to->extra_info, from->extra_info);
}

void _x_post_frame_copy_up(vo_frame_t *to, vo_frame_t *from) {
  /* propagate changes upwards (from video out to decoders) */
  if (from->stream)
    _x_refcounter_inc(from->stream->refcounter);
  if (to->stream)
    _x_refcounter_dec(to->stream->refcounter);

  to->vpts     = from->vpts;
  to->duration = from->duration;
  to->stream   = from->stream;

  if (to->extra_info != from->extra_info)
    _x_extra_info_merge(to->extra_info, from->extra_info);
}

void _x_post_frame_u_turn(vo_frame_t *frame, xine_stream_t *stream) {
  /* frame's travel will end here => do the housekeeping */
  if (stream)
    _x_refcounter_inc(stream->refcounter);
  if (frame->stream)
    _x_refcounter_dec(frame->stream->refcounter);

  frame->stream = stream;
  if (stream) {
    _x_extra_info_merge(frame->extra_info, stream->video_decoder_extra_info);
    stream->metronom->got_video_frame(stream->metronom, frame);
  }
}


/* dummy intercept functions that just pass the call on to the original overlay manager */
static void post_overlay_init(video_overlay_manager_t *ovl_gen) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  port->original_manager->init(port->original_manager);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
}

static void post_overlay_dispose(video_overlay_manager_t *ovl_gen) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  port->original_manager->dispose(port->original_manager);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
}

static int32_t post_overlay_get_handle(video_overlay_manager_t *ovl_gen, int object_type) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);
  int32_t handle;

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  handle = port->original_manager->get_handle(port->original_manager, object_type);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
  return handle;
}

static void post_overlay_free_handle(video_overlay_manager_t *ovl_gen, int32_t handle) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  port->original_manager->free_handle(port->original_manager, handle);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
}

static int32_t post_overlay_add_event(video_overlay_manager_t *ovl_gen, void *event) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);
  int32_t result;

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  result = port->original_manager->add_event(port->original_manager, event);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
  return result;
}

static void post_overlay_flush_events(video_overlay_manager_t *ovl_gen) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  port->original_manager->flush_events(port->original_manager);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
}

static int post_overlay_redraw_needed(video_overlay_manager_t *ovl_gen, int64_t vpts) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);
  int redraw;

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  redraw = port->original_manager->redraw_needed(port->original_manager, vpts);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
  return redraw;
}

static void post_overlay_multiple_overlay_blend(video_overlay_manager_t *ovl_gen, int64_t vpts,
	      vo_driver_t *output, vo_frame_t *vo_img, int enabled) {
  post_video_port_t *port = _x_post_ovl_manager_to_port(ovl_gen);

  if (port->manager_lock) pthread_mutex_lock(port->manager_lock);
  port->original_manager->multiple_overlay_blend(port->original_manager, vpts, output, vo_img, enabled);
  if (port->manager_lock) pthread_mutex_unlock(port->manager_lock);
}


void _x_post_intercept_overlay_manager(video_overlay_manager_t *original, post_video_port_t *port) {
  if (!port->new_manager->init)
    port->new_manager->init                   = post_overlay_init;
  if (!port->new_manager->dispose)
    port->new_manager->dispose                = post_overlay_dispose;
  if (!port->new_manager->get_handle)
    port->new_manager->get_handle             = post_overlay_get_handle;
  if (!port->new_manager->free_handle)
    port->new_manager->free_handle            = post_overlay_free_handle;
  if (!port->new_manager->add_event)
    port->new_manager->add_event              = post_overlay_add_event;
  if (!port->new_manager->flush_events)
    port->new_manager->flush_events           = post_overlay_flush_events;
  if (!port->new_manager->redraw_needed)
    port->new_manager->redraw_needed          = post_overlay_redraw_needed;
  if (!port->new_manager->multiple_overlay_blend)
    port->new_manager->multiple_overlay_blend = post_overlay_multiple_overlay_blend;

  port->original_manager                      = original;
}


/* dummy intercept functions that just pass the call on to the original port */
static uint32_t post_audio_get_capabilities(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  uint32_t caps;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  caps = port->original_port->get_capabilities(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return caps;
}

static int post_audio_get_property(xine_audio_port_t *port_gen, int property) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  int prop;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  prop = port->original_port->get_property(port->original_port, property);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return prop;
}

static int post_audio_set_property(xine_audio_port_t *port_gen, int property, int value) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  int val;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  val = port->original_port->set_property(port->original_port, property, value);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return val;
}

static int post_audio_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
	       uint32_t bits, uint32_t rate, int mode) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  int result;

  _x_post_rewire(port->post);
  _x_post_inc_usage(port);
  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  result = (port->original_port->open) (port->original_port, stream, bits, rate, mode);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  port->stream = stream;
  port->bits   = bits;
  port->rate   = rate;
  port->mode   = mode;
  return result;
}

static audio_buffer_t *post_audio_get_buffer(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  audio_buffer_t *buf;

  _x_post_rewire(port->post);
  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  buf = port->original_port->get_buffer(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return buf;
}

static void post_audio_put_buffer(xine_audio_port_t *port_gen, audio_buffer_t *buf,
                                  xine_stream_t *stream) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->put_buffer(port->original_port, buf, stream);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static void post_audio_close(xine_audio_port_t *port_gen, xine_stream_t *stream) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->close(port->original_port, stream);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  port->stream = NULL;
  _x_post_dec_usage(port);
}

static void post_audio_exit(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->exit(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static int post_audio_control(xine_audio_port_t *port_gen, int cmd, ...) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  va_list args;
  void *arg;
  int rval;

  va_start(args, cmd);
  arg = va_arg(args, void*);
  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  rval = port->original_port->control(port->original_port, cmd, arg);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  va_end(args);

  return rval;
}

static void post_audio_flush(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  port->original_port->flush(port->original_port);
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
}

static int post_audio_status(xine_audio_port_t *port_gen, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  int result;

  if (port->port_lock) pthread_mutex_lock(port->port_lock);
  result = port->original_port->status(port->original_port, stream, bits, rate, mode);
  *bits = port->bits;
  *rate = port->rate;
  *mode = port->mode;
  if (port->port_lock) pthread_mutex_unlock(port->port_lock);
  return result;
}


static int post_audio_rewire(xine_post_out_t *output_gen, void *data) {
  post_out_t        *output     = (post_out_t *)output_gen;
  xine_audio_port_t *new_port   = (xine_audio_port_t *)data;
  post_audio_port_t *input_port = (post_audio_port_t *)output->user_data;
  post_plugin_t     *this       = output->post;
  uint32_t bits, rate;
  int mode;

  if (!new_port)
    return 0;

  this->running_ticket->lock_port_rewiring(this->running_ticket, -1);
  this->running_ticket->revoke(this->running_ticket, 1);

  if (input_port->original_port->status(input_port->original_port, input_port->stream,
      &bits, &rate, &mode)) {
    (new_port->open) (new_port, input_port->stream, bits, rate, mode);
    input_port->original_port->close(input_port->original_port, input_port->stream);
  }
  input_port->original_port = new_port;

  this->running_ticket->issue(this->running_ticket, 1);
  this->running_ticket->unlock_port_rewiring(this->running_ticket);

  return 1;
}

post_audio_port_t *_x_post_intercept_audio_port(post_plugin_t *post, xine_audio_port_t *original,
						post_in_t **input, post_out_t **output) {
  post_audio_port_t *port = calloc(1, sizeof(post_audio_port_t));

  if (!port)
    return NULL;

  port->new_port.open             = post_audio_open;
  port->new_port.get_buffer       = post_audio_get_buffer;
  port->new_port.put_buffer       = post_audio_put_buffer;
  port->new_port.close            = post_audio_close;
  port->new_port.exit             = post_audio_exit;
  port->new_port.get_capabilities = post_audio_get_capabilities;
  port->new_port.get_property     = post_audio_get_property;
  port->new_port.set_property     = post_audio_set_property;
  port->new_port.control          = post_audio_control;
  port->new_port.flush            = post_audio_flush;
  port->new_port.status           = post_audio_status;

  port->original_port             = original;
  port->post                      = post;

  pthread_mutex_init(&port->usage_lock, NULL);

  if (input) {
    *input = calloc(1, sizeof(post_in_t));
    if (!*input) return port;
    (*input)->xine_in.name = "audio in";
    (*input)->xine_in.type = XINE_POST_DATA_AUDIO;
    (*input)->xine_in.data = &port->new_port;
    (*input)->post = post;
    xine_list_push_back(post->input, *input);
  }

  if (output) {
    *output = calloc(1, sizeof(post_out_t));
    if (!*output) return port;
    (*output)->xine_out.name = "audio out";
    (*output)->xine_out.type = XINE_POST_DATA_AUDIO;
    (*output)->xine_out.data = &port->original_port;
    (*output)->xine_out.rewire = post_audio_rewire;
    (*output)->post = post;
    (*output)->user_data = port;
    xine_list_push_back(post->output, *output);
  }

  return port;
}


int _x_post_dispose(post_plugin_t *this) {
  int i, in_use = 0;

  /* acquire all usage locks */
  for (i = 0; this->xine_post.audio_input[i]; i++) {
    post_audio_port_t *port = (post_audio_port_t *)this->xine_post.audio_input[i];
    pthread_mutex_lock(&port->usage_lock);
  }
  for (i = 0; this->xine_post.video_input[i]; i++) {
    post_video_port_t *port = (post_video_port_t *)this->xine_post.video_input[i];
    pthread_mutex_lock(&port->usage_lock);
  }

  /* we can set this witout harm, because it is always checked with
   * usage lock held */
  this->dispose_pending = 1;

  /* check counters */
  for (i = 0; this->xine_post.audio_input[i]; i++) {
    post_audio_port_t *port = (post_audio_port_t *)this->xine_post.audio_input[i];
    if (port->usage_count > 0) {
      in_use = 1;
      break;
    }
  }
  for (i = 0; this->xine_post.video_input[i]; i++) {
    post_video_port_t *port = (post_video_port_t *)this->xine_post.video_input[i];
    if (port->usage_count > 0) {
      in_use = 1;
      break;
    }
  }

  /* free the locks */
  for (i = 0; this->xine_post.audio_input[i]; i++) {
    post_audio_port_t *port = (post_audio_port_t *)this->xine_post.audio_input[i];
    pthread_mutex_unlock(&port->usage_lock);
  }
  for (i = 0; this->xine_post.video_input[i]; i++) {
    post_video_port_t *port = (post_video_port_t *)this->xine_post.video_input[i];
    pthread_mutex_unlock(&port->usage_lock);
  }

  if (!in_use) {
    xine_post_in_t  *input;
    xine_post_out_t *output;
    xine_list_iterator_t ite;

    /* we can really dispose it */

    free(this->xine_post.audio_input);
    free(this->xine_post.video_input);
    /* these were allocated in the plugin loader */
    free(this->input_ids);
    free(this->output_ids);

    for (ite = xine_list_front(this->input); ite;
         ite = xine_list_next(this->input, ite)) {
      input = xine_list_get_value(this->input, ite);
      switch (input->type) {
      case XINE_POST_DATA_VIDEO:
	{
	  post_video_port_t *port = (post_video_port_t *)input->data;
	  vo_frame_t *first, *second;

	  pthread_mutex_destroy(&port->usage_lock);
	  pthread_mutex_destroy(&port->free_frames_lock);

	  second = NULL;
	  for (first = port->free_frame_slots; first;
	      second = first, first = first->next)
	    free(second);
	  free(second);

	  free(port);
	  free(input);
	}
	break;
      case XINE_POST_DATA_AUDIO:
	{
	  post_audio_port_t *port = (post_audio_port_t *)input->data;

	  pthread_mutex_destroy(&port->usage_lock);

	  free(port);
	  free(input);
	}
	break;
      }
    }
    for (ite = xine_list_front(this->output); ite;
         ite = xine_list_next(this->output, ite)) {
      output = xine_list_get_value(this->output, ite);
      switch (output->type) {
      case XINE_POST_DATA_VIDEO:
	if (output->rewire == post_video_rewire)
	  /* we allocated it, we free it */
	  free(output);
	break;
      case XINE_POST_DATA_AUDIO:
	if (output->rewire == post_audio_rewire)
	  /* we allocated it, we free it */
	  free(output);
	break;
      }
    }

    xine_list_delete(this->input);
    xine_list_delete(this->output);

    /* since the plugin loader does not know, when the plugin gets disposed,
     * we have to handle the reference counter here */
    pthread_mutex_lock(&this->xine->plugin_catalog->lock);
    this->node->ref--;
    pthread_mutex_unlock(&this->xine->plugin_catalog->lock);

    return 1;
  }

  return 0;
}
