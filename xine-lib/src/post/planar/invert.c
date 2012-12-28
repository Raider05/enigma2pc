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
 */

/*
 * simple video inverter plugin
 */

#include <xine/xine_internal.h>
#include <xine/post.h>


/* plugin class initialization function */
void *invert_init_plugin(xine_t *xine, void *);

/* plugin class functions */
static post_plugin_t *invert_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           invert_dispose(post_plugin_t *this_gen);

/* frame intercept check */
static int            invert_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            invert_draw(vo_frame_t *frame, xine_stream_t *stream);


void *invert_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = invert_open_plugin;
  class->identifier      = "invert";
  class->description     = N_("inverts the colours of every video frame");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *invert_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_t *this   = calloc(1, sizeof(post_plugin_t));
  post_in_t     *input;
  post_out_t    *output;
  post_video_port_t *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(this, 0, 1);

  port = _x_post_intercept_video_port(this, video_target[0], &input, &output);
  port->intercept_frame = invert_intercept_frame;
  port->new_frame->draw = invert_draw;
  input->xine_in.name   = "video";
  output->xine_out.name = "inverted video";
  this->xine_post.video_input[0] = &port->new_port;

  this->dispose = invert_dispose;

  return this;
}

static void invert_dispose(post_plugin_t *this)
{
  if (_x_post_dispose(this))
    free(this);
}


static int invert_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int invert_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  vo_frame_t *inverted_frame;
  int size, i, skip;

  if (frame->bad_frame) {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
    return skip;
  }

  inverted_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, frame->flags | VO_BOTH_FIELDS);
  _x_post_frame_copy_down(frame, inverted_frame);

  switch (inverted_frame->format) {
  case XINE_IMGFMT_YUY2:
    size = inverted_frame->pitches[0] * inverted_frame->height;
    for (i = 0; i < size; i++)
      inverted_frame->base[0][i] = 0xff - frame->base[0][i];
    break;
  case XINE_IMGFMT_YV12:
    /* Y */
    size = inverted_frame->pitches[0] * inverted_frame->height;
    for (i = 0; i < size; i++)
      inverted_frame->base[0][i] = 0xff - frame->base[0][i];
    /* U */
    size = inverted_frame->pitches[1] * ((inverted_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      inverted_frame->base[1][i] = 0xff - frame->base[1][i];
    /* V */
    size = inverted_frame->pitches[2] * ((inverted_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      inverted_frame->base[2][i] = 0xff - frame->base[2][i];
    break;
  }
  skip = inverted_frame->draw(inverted_frame, stream);
  _x_post_frame_copy_up(frame, inverted_frame);
  inverted_frame->free(inverted_frame);

  return skip;
}
