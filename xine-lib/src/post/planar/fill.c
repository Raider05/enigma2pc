/*
 * Copyright (C) 2005 the xine project
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
 * fill video filter by James Stembridge (jstembridge@gmail.com)
 *
 * based on invert.c
 */

#include <xine/xine_internal.h>
#include <xine/post.h>

/* plugin class initialization function */
void *fill_init_plugin(xine_t *xine, void *);

/* plugin class functions */
static post_plugin_t *fill_open_plugin(post_class_t *class_gen, int inputs,
                                       xine_audio_port_t **audio_target,
                                       xine_video_port_t **video_target);

/* plugin instance functions */
static void           fill_dispose(post_plugin_t *this_gen);

/* replaced video port functions */
static vo_frame_t    *fill_get_frame(xine_video_port_t *port_gen, uint32_t width,
                                     uint32_t height, double ratio,
                                     int format, int flags);
static int            fill_draw(vo_frame_t *frame, xine_stream_t *stream);


void *fill_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = fill_open_plugin;
  class->identifier      = "fill";
  class->description     = N_("crops left and right of video to fill 4:3 aspect ratio");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *fill_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_t     *this        = calloc(1, sizeof(post_plugin_t));
  post_in_t         *input;
  post_out_t        *output;
  post_video_port_t *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(this, 0, 1);

  port = _x_post_intercept_video_port(this, video_target[0], &input, &output);
  port->new_port.get_frame = fill_get_frame;
  port->new_frame->draw    = fill_draw;

  input->xine_in.name     = "video";
  output->xine_out.name   = "cropped video";

  this->xine_post.video_input[0] = &port->new_port;

  this->dispose = fill_dispose;

  return this;
}

static void fill_dispose(post_plugin_t *this)
{
  if (_x_post_dispose(this))
    free(this);
}


static vo_frame_t *fill_get_frame(xine_video_port_t *port_gen, uint32_t width,
                                  uint32_t height, double ratio,
                                  int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_t     *this = port->post;
  vo_frame_t        *frame;

  _x_post_rewire(this);

  if (ratio <= 0.0) ratio = (double)width / (double)height;

  if ((ratio > 4.0/3.0) &&
      (format == XINE_IMGFMT_YV12 || format == XINE_IMGFMT_YUY2)) {
    frame = port->original_port->get_frame(port->original_port,
      width, height, 4.0/3.0, format, flags);

    _x_post_inc_usage(port);
    frame = _x_post_intercept_video_frame(frame, port);

    frame->ratio = ratio;
  } else {
    frame = port->original_port->get_frame(port->original_port,
      width, height, ratio, format, flags);
    /* no need to intercept this one, we are not going to do anything with it */
  }

  return frame;
}


static int fill_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  int skip, new_width;

  new_width = (4.0*frame->width) / (3.0*frame->ratio);

  frame->crop_left += (frame->width - new_width) / 2;
  frame->crop_right += (frame->width + 1 - new_width) / 2;
  frame->ratio = 4.0/3.0;

  _x_post_frame_copy_down(frame, frame->next);
  skip = frame->next->draw(frame->next, stream);
  _x_post_frame_copy_up(frame, frame->next);
  return skip;
}
