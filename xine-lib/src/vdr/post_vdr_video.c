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
 * frame scaler plugin for VDR
 */

#define LOG_MODULE "vdr_video"
/*
#define LOG
#define LOG_VERBOSE
*/

#include <xine/xine_internal.h>
#include <xine/post.h>
#include "combined_vdr.h"



typedef struct vdr_video_post_plugin_s
{
  post_plugin_t post_plugin;

  xine_event_queue_t *event_queue;
  xine_stream_t      *vdr_stream;

  int8_t trick_speed_mode;
  int8_t enabled;

  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int32_t w_ref;
  int32_t h_ref;

  int32_t old_frame_left;
  int32_t old_frame_top;
  int32_t old_frame_width;
  int32_t old_frame_height;
  double  old_frame_ratio;

}
vdr_video_post_plugin_t;


static void vdr_video_set_video_window(vdr_video_post_plugin_t *this, int32_t x, int32_t y, int32_t w, int32_t h, int32_t w_ref, int32_t h_ref)
{
  this->enabled = 0;

  this->x     = x;
  this->y     = y;
  this->w     = w;
  this->h     = h;
  this->w_ref = w_ref;
  this->h_ref = h_ref;

  if (w != w_ref || h != h_ref)
    this->enabled = 1;
}


/* plugin class functions */
static post_plugin_t *vdr_video_open_plugin(post_class_t *class_gen, int inputs,
                                            xine_audio_port_t **audio_target,
                                            xine_video_port_t **video_target);

/* plugin instance functions */
static void           vdr_video_dispose(post_plugin_t *this_gen);

/* route preprocessing functions check */
static int            vdr_video_route_preprocessing_procs(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            vdr_video_draw(vo_frame_t *frame, xine_stream_t *stream);


void *vdr_video_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof (post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = vdr_video_open_plugin;
  class->identifier      = "vdr";
  class->description     = N_("modifies every video frame as requested by VDR");
  class->dispose         = default_post_class_dispose;

  return class;
}

static post_plugin_t *vdr_video_open_plugin(post_class_t *class_gen, int inputs,
                                            xine_audio_port_t **audio_target,
                                            xine_video_port_t **video_target)
{
  vdr_video_post_plugin_t *this = (vdr_video_post_plugin_t *)xine_xmalloc(sizeof (vdr_video_post_plugin_t));
  post_in_t               *input;
  post_out_t              *output;
  post_video_port_t       *port;

  if (!this || !video_target || !video_target[ 0 ])
  {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post_plugin, 0, 1);
  this->post_plugin.dispose = vdr_video_dispose;

  port = _x_post_intercept_video_port(&this->post_plugin, video_target[ 0 ], &input, &output);
  port->route_preprocessing_procs = vdr_video_route_preprocessing_procs;
  port->new_frame->draw           = vdr_video_draw;
  this->post_plugin.xine_post.video_input[ 0 ] = &port->new_port;

  this->enabled          = 0;
  this->vdr_stream       = 0;
  this->event_queue      = 0;
  this->old_frame_left   = 0;
  this->old_frame_top    = 0;
  this->old_frame_width  = 0;
  this->old_frame_height = 0;
  this->old_frame_ratio  = 0;
  this->trick_speed_mode = 0;

  return &this->post_plugin;
}

static void vdr_video_dispose(post_plugin_t *this_gen)
{
  if (_x_post_dispose(this_gen))
  {
    vdr_video_post_plugin_t *this = (vdr_video_post_plugin_t *)this_gen;

    if (this->vdr_stream)
    {
      xine_event_t event;
      vdr_frame_size_changed_data_t event_data;

      event_data.x = 0;
      event_data.y = 0;
      event_data.w = 0;
      event_data.h = 0;

      event.type        = XINE_EVENT_VDR_FRAMESIZECHANGED;
      event.data        = &event_data;
      event.data_length = sizeof (event_data);

      xine_event_send(this->vdr_stream, &event);

      xine_event_dispose_queue(this->event_queue);
    }

    free(this_gen);
  }
}

static int vdr_video_route_preprocessing_procs(post_video_port_t *port, vo_frame_t *frame)
{
  vdr_video_post_plugin_t *this = (vdr_video_post_plugin_t *)port->post;
  return !this->enabled
    || (frame->format != XINE_IMGFMT_YUY2
      && frame->format != XINE_IMGFMT_YV12);
}


static inline void vdr_video_scale(uint8_t *src, uint8_t *dst, int y_inc, int x_inc, int w_dst, int h_dst, int x, int y, int w, int h, int w_ref, int h_ref, int init)
{
  int x0 = x * w_dst / w_ref;
  int y0 = y * h_dst / h_ref;

  int x1 = ((x + w) * w_dst - 1 + w_ref) / w_ref;
  int y1 = ((y + h) * h_dst - 1 + h_ref) / h_ref;

  int dx = x1 - x0;
  int dy = y1 - y0;

  int yy, xx;

  int dy2    = dy + dy;
  int h_dst2 = h_dst + h_dst;
  int y_eps  = h_dst - dy2;

  int dx2    = dx + dx;
  int w_dst2 = w_dst + w_dst;
  int x_eps0 = w_dst - dx2;

  for (yy = 0; yy < y0; yy++)
  {
    uint8_t *dst0 = dst;

    for (xx = 0; xx < w_dst; xx++)
    {
      *dst0 = init;
      dst0 += x_inc;
    }

    dst += y_inc;
  }

  for (yy = y0; yy < y1; yy++)
  {
    uint8_t *dst0 = dst;
    uint8_t *src0 = src;

    int x_eps = x_eps0;

    for (xx = 0; xx < x0; xx++)
    {
      *dst0 = init;
      dst0 += x_inc;
    }

    for (xx = x0; xx < x1; xx++)
    {
      *dst0 = *src0;
      dst0 += x_inc;

      x_eps += w_dst2;
      while (x_eps >= 0)
      {
        src0  += x_inc;
        x_eps -= dx2;
      }
    }

    for (xx = x1; xx < w_dst; xx++)
    {
      *dst0 = init;
      dst0 += x_inc;
    }

    dst += y_inc;

    y_eps += h_dst2;
    while (y_eps >= 0)
    {
      src   += y_inc;
      y_eps -= dy2;
    }
  }

  for (yy = y1; yy < h_dst; yy++)
  {
    uint8_t *dst0 = dst;

    for (xx = 0; xx < w_dst; xx++)
    {
      *dst0 = init;
      dst0 += x_inc;
    }

    dst += y_inc;
  }
}

static void vdr_video_scale_YUY2(vdr_video_post_plugin_t *this, vo_frame_t *src, vo_frame_t *dst)
{
  int w = dst->width  - dst->crop_left - dst->crop_right;
  int h = dst->height - dst->crop_top  - dst->crop_bottom;
  int offset;

  if (w < 0)
    w = 0;

  if (h < 0)
    h = 0;

  offset = dst->pitches[ 0 ] * dst->crop_top + 2 *   dst->crop_left;
  vdr_video_scale(&src->base[ 0 ][ 0 ] + offset, &dst->base[ 0 ][ 0 ] + offset, dst->pitches[ 0 ], 2,  w         , h, this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x00);
  offset = dst->pitches[ 0 ] * dst->crop_top + 4 * ((dst->crop_left + 1) / 2);
  vdr_video_scale(&src->base[ 0 ][ 1 ] + offset, &dst->base[ 0 ][ 1 ] + offset, dst->pitches[ 0 ], 4, (w + 1) / 2, h, this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x80);
  offset = dst->pitches[ 0 ] * dst->crop_top + 4 * ((dst->crop_left + 1) / 2);
  vdr_video_scale(&src->base[ 0 ][ 3 ] + offset, &dst->base[ 0 ][ 3 ] + offset, dst->pitches[ 0 ], 4, (w + 1) / 2, h, this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x80);
}

static void vdr_video_scale_YV12(vdr_video_post_plugin_t *this, vo_frame_t *src, vo_frame_t *dst)
{
  int w = dst->width  - dst->crop_left - dst->crop_right;
  int h = dst->height - dst->crop_top  - dst->crop_bottom;
  int offset;

  if (w < 0)
    w = 0;

  if (h < 0)
    h = 0;

  offset = dst->pitches[ 0 ] *   dst->crop_top           + 1 *   dst->crop_left;
  vdr_video_scale(&src->base[ 0 ][ 0 ] + offset, &dst->base[ 0 ][ 0 ] + offset, dst->pitches[ 0 ], 1,  w         ,  h         , this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x00);
  offset = dst->pitches[ 1 ] * ((dst->crop_top + 1) / 2) + 1 * ((dst->crop_left + 1) / 2);
  vdr_video_scale(&src->base[ 1 ][ 0 ] + offset, &dst->base[ 1 ][ 0 ] + offset, dst->pitches[ 1 ], 1, (w + 1) / 2, (h + 1) / 2, this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x80);
  offset = dst->pitches[ 2 ] * ((dst->crop_top + 1) / 2) + 1 * ((dst->crop_left + 1) / 2);
  vdr_video_scale(&src->base[ 2 ][ 0 ] + offset, &dst->base[ 2 ][ 0 ] + offset, dst->pitches[ 2 ], 1, (w + 1) / 2, (h + 1) / 2, this->x, this->y, this->w, this->h, this->w_ref, this->h_ref, 0x80);
}


static int vdr_video_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t       *port = (post_video_port_t *)frame->port;
  vdr_video_post_plugin_t *this = (vdr_video_post_plugin_t *)port->post;
  vo_frame_t *vdr_frame;
  xine_event_t *event;
  int skip;

  if (this->vdr_stream
      && !_x_continue_stream_processing(this->vdr_stream))
  {
    this->vdr_stream = 0;

    xine_event_dispose_queue(this->event_queue);
    this->event_queue = 0;

    this->old_frame_left   = 0;
    this->old_frame_top    = 0;
    this->old_frame_width  = 0;
    this->old_frame_height = 0;
    this->old_frame_ratio  = 0;
  }

  if (!this->vdr_stream
      && vdr_is_vdr_stream(stream))
  {
    this->event_queue = xine_event_new_queue(stream);
    if (this->event_queue)
    {
      this->vdr_stream = stream;

      {
        xine_event_t event;

        event.type = XINE_EVENT_VDR_PLUGINSTARTED;
        event.data = 0;
        event.data_length = 0; /* vdr_video */

        xine_event_send(this->vdr_stream, &event);
      }
    }
  }

  if (this->event_queue)
  {
    while ((event = xine_event_get(this->event_queue)))
    {
      if (event->type == XINE_EVENT_VDR_SETVIDEOWINDOW)
      {
        vdr_set_video_window_data_t *data = (vdr_set_video_window_data_t *)event->data;

        vdr_video_set_video_window(this, data->x, data->y, data->w, data->h, data->w_ref, data->h_ref);
      }
      else if (event->type == XINE_EVENT_VDR_TRICKSPEEDMODE)
      {
/*
        fprintf(stderr, "###############################: %p, %d\n", event->data, event->data_length);
        this->trick_speed_mode = (0 != event->data_length);
*/
      }

      xine_event_free(event);
    }
  }

  {
    int32_t frame_left   = frame->crop_left;
    int32_t frame_width  = frame->width - frame->crop_left - frame->crop_right;
    int32_t frame_top    = frame->crop_top;
    int32_t frame_height = frame->height - frame->crop_top - frame->crop_bottom;
    double  frame_ratio  = frame->ratio;

    if (frame_left < 0)
      frame_left = 0;
    if (frame_width > frame->width)
      frame_width = frame->width;
    if (frame_top < 0)
      frame_top = 0;
    if (frame_height > frame->height)
      frame_height = frame->height;

    if (this->vdr_stream
        && frame_width != 0
        && frame_height != 0
        && (this->old_frame_left    != frame_left
          || this->old_frame_top    != frame_top
          || this->old_frame_width  != frame_width
          || this->old_frame_height != frame_height
          || this->old_frame_ratio  != frame_ratio))
    {
      xine_event_t event;
      vdr_frame_size_changed_data_t event_data;

      event_data.x = frame_left;
      event_data.y = frame_top;
      event_data.w = frame_width;
      event_data.h = frame_height;
      event_data.r = frame_ratio;

      xprintf(this->vdr_stream->xine, XINE_VERBOSITY_LOG,
            _(LOG_MODULE ": osd: (%d, %d)-(%d, %d)@%lg\n"), frame_left, frame_top, frame_width, frame_height, frame_ratio);

      event.type        = XINE_EVENT_VDR_FRAMESIZECHANGED;
      event.data        = &event_data;
      event.data_length = sizeof (event_data);

      xine_event_send(this->vdr_stream, &event);

      this->old_frame_left   = frame_left;
      this->old_frame_top    = frame_top;
      this->old_frame_width  = frame_width;
      this->old_frame_height = frame_height;
      this->old_frame_ratio  = frame_ratio;
    }
  }
/*
  fprintf(stderr, "~~~~~~~~~~~~ trickspeedmode: %d\n", this->trick_speed_mode);

  if (this->vdr_stream
      && this->trick_speed_mode)
  {
    frame->pts = 0;
    frame->next->pts = 0;
  }
*/
#if defined(LOG) && defined(LOG_VERBOSE)
  {
    int a = 0, b = 0, c = 0, d = 0;
    if (stream)
      _x_query_buffer_usage(stream, &a, &b, &c, &d);
    lprintf("buffer usage: %3d, %2d, %2d, %2d, %p\n", a, b, c, d, stream);
  }
#endif

  if (!this->enabled
      || frame->bad_frame
      || (frame->format != XINE_IMGFMT_YUY2
          && frame->format != XINE_IMGFMT_YV12)
      || frame->proc_frame
      || frame->proc_slice)
  {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
    return skip;
  }

  vdr_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, frame->flags | VO_BOTH_FIELDS);

  _x_post_frame_copy_down(frame, vdr_frame);

  switch (vdr_frame->format)
  {
  case XINE_IMGFMT_YUY2:
    vdr_video_scale_YUY2(this, frame, vdr_frame);
    break;

  case XINE_IMGFMT_YV12:
    vdr_video_scale_YV12(this, frame, vdr_frame);
    break;
  }

  skip = vdr_frame->draw(vdr_frame, stream);
  _x_post_frame_copy_up(frame, vdr_frame);
  vdr_frame->free(vdr_frame);

  return skip;
}
