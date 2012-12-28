/*
 * Copyright (C) 2003 the xine project
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
 * $Id:
 *
 * expand video filter by James Stembridge 24/05/2003
 *            improved by Michael Roitzsch
 *            centre_crop_out_mode by Reinhard Nissl
 *
 * based on invert.c
 *
 */

#include <xine/xine_internal.h>
#include <xine/post.h>

/* The expand trick explained:
 *
 * The expand plugin is meant to take frames of arbitrary aspect ratio and
 * converts them to 4:3 aspect by adding black bars on the top and bottom
 * of the frame. This allows us to shift overlays down into the black area
 * so they don't cover the image.
 *
 * How do we do that? The naive approach would be to intercept the frame's
 * draw() function and simply copy the frame's content into a larger one.
 * This is quite CPU intensive because of the huge memcpy()s involved.
 *
 * Therefore the better idea is to trick the decoder into rendering the
 * image into a frame with pre-attached black borders. This is the way:
 *  - when the decoder asks for a new frame, we allocate an enlarged
 *    frame from the original port and prepare it with black borders
 *  - we modify this frame's base pointers so that the decoder will only see
 *    the area between the black bars
 *  - this frame is given to the decoder, which paints its image inside
 *  - when the decoder draws the frame, the post plugin architecture
 *    will automatically restore the old pointers
 * This way, the decoder (or any other post plugin up the tree) will only
 * see the frame area between the black bars and by that modify the
 * enlarged version directly. No need for later copying.
 *
 * When centre_crop_out_mode is enabled, the plugin will detect the black
 * bars to the left and right of the image and will then set up cropping
 * to efficiently remove the black border around the 4:3 image, which the
 * plugin would produce otherwise for this case.
 */


/* plugin class initialization function */
void *expand_init_plugin(xine_t *xine, void *);

/* plugin structures */
typedef struct expand_parameters_s {
  int enable_automatic_shift;
  int overlay_y_offset;
  double aspect;
  int centre_cut_out_mode;
} expand_parameters_t;

START_PARAM_DESCR(expand_parameters_t)
PARAM_ITEM(POST_PARAM_TYPE_BOOL, enable_automatic_shift, NULL, 0, 1, 0,
  "enable automatic overlay shifting")
PARAM_ITEM(POST_PARAM_TYPE_INT, overlay_y_offset, NULL, -500, 500, 0,
  "manually shift the overlay vertically")
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, aspect, NULL, 1.0, 3.5, 0,
  "target aspect ratio")
PARAM_ITEM(POST_PARAM_TYPE_BOOL, centre_cut_out_mode, NULL, 0, 1, 0,
  "cut out centred 4:3 image contained in 16:9 frame")
END_PARAM_DESCR(expand_param_descr)

typedef struct post_expand_s {
  post_plugin_t            post;

  xine_post_in_t           parameter_input;

  int                      enable_automatic_shift;
  int                      overlay_y_offset;
  double                   aspect;
  int                      top_bar_height;
  int                      centre_cut_out_mode;
  int                      cropping_active;
} post_expand_t;

/* plugin class functions */
static post_plugin_t *expand_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           expand_dispose(post_plugin_t *this_gen);

/* parameter functions */
static xine_post_api_descr_t *expand_get_param_descr(void);
static int            expand_set_parameters(xine_post_t *this_gen, void *param_gen);
static int            expand_get_parameters(xine_post_t *this_gen, void *param_gen);
static char          *expand_get_help (void);

/* replaced video port functions */
static vo_frame_t    *expand_get_frame(xine_video_port_t *port_gen, uint32_t width,
				       uint32_t height, double ratio,
				       int format, int flags);

/* replaced vo_frame functions */
static int            expand_draw(vo_frame_t *frame, xine_stream_t *stream);

/* overlay manager intercept check */
static int            expand_intercept_ovl(post_video_port_t *port);

/* replaced overlay manager functions */
static int32_t        expand_overlay_add_event(video_overlay_manager_t *this_gen, void *event);


void *expand_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = expand_open_plugin;
  class->identifier      = "expand";
  class->description     = N_("add black borders to top and bottom of video to expand it to 4:3 aspect ratio");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *expand_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_expand_t     *this        = calloc(1, sizeof(post_expand_t));
  post_in_t         *input;
  xine_post_in_t    *input_param;
  post_out_t        *output;
  post_video_port_t *port;
  static xine_post_api_t post_api =
    { expand_set_parameters, expand_get_parameters, expand_get_param_descr, expand_get_help };

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->enable_automatic_shift = 0;
  this->overlay_y_offset       = 0;
  this->aspect                 = 4.0 / 3.0;
  this->centre_cut_out_mode    = 0;
  this->cropping_active        = 0;

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->new_port.get_frame     = expand_get_frame;
  port->new_frame->draw        = expand_draw;
  port->intercept_ovl          = expand_intercept_ovl;
  port->new_manager->add_event = expand_overlay_add_event;

  input_param       = &this->parameter_input;
  input_param->name = "parameters";
  input_param->type = XINE_POST_DATA_PARAMETERS;
  input_param->data = &post_api;
  xine_list_push_back(this->post.input, input_param);

  input->xine_in.name   = "video";
  output->xine_out.name = "expanded video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = expand_dispose;

  return &this->post;
}

static void expand_dispose(post_plugin_t *this_gen)
{
  post_expand_t     *this = (post_expand_t *)this_gen;

  if (_x_post_dispose(this_gen))
    free(this);
}


static xine_post_api_descr_t *expand_get_param_descr(void)
{
  return &expand_param_descr;
}

static int expand_set_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_expand_t *this = (post_expand_t *)this_gen;
  expand_parameters_t *param = (expand_parameters_t *)param_gen;

  this->enable_automatic_shift = param->enable_automatic_shift;
  this->overlay_y_offset       = param->overlay_y_offset;
  this->aspect                 = param->aspect;
  this->centre_cut_out_mode    = param->centre_cut_out_mode;

  return 1;
}

static int expand_get_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_expand_t *this = (post_expand_t *)this_gen;
  expand_parameters_t *param = (expand_parameters_t *)param_gen;

  param->enable_automatic_shift = this->enable_automatic_shift;
  param->overlay_y_offset       = this->overlay_y_offset;
  param->aspect                 = this->aspect;
  param->centre_cut_out_mode    = this->centre_cut_out_mode;

  return 1;
}

static char *expand_get_help(void) {
  return _("The expand plugin is meant to take frames of arbitrary aspect ratio and "
           "converts them to a different aspect (4:3 by default) by adding black bars "
           "on the top and bottom of the frame. This allows us to shift overlays "
           "down into the black area so they don't cover the image.\n"
           "\n"
           "Parameters (FIXME: better help)\n"
           "  Enable_automatic_shift: Enable automatic overlay shifting\n"
           "  Overlay_y_offset: Manually shift the overlay vertically\n"
           "  aspect: The target aspect ratio (default 4:3)\n"
           "  Centre_cut_out_mode: extracts 4:3 image contained in 16:9 frame\n"
           "\n"
         );
}


static int is_pixel_black(vo_frame_t *frame, int x, int y)
{
  int Y = 0x00, Cr = 0x00, Cb = 0x00;

  if (x < 0)              x = 0;
  if (x >= frame->width)  x = frame->width - 1;
  if (y < 0)              y = 0;
  if (y >= frame->height) y = frame->height - 1;

  switch (frame->format)
  {
  case XINE_IMGFMT_YV12:
    Y  = *(frame->base[ 0 ] + frame->pitches[ 0 ] * y     + x);
    Cr = *(frame->base[ 1 ] + frame->pitches[ 1 ] * y / 2 + x / 2);
    Cb = *(frame->base[ 2 ] + frame->pitches[ 2 ] * y / 2 + x / 2);
    break;

  case XINE_IMGFMT_YUY2:
    Y  = *(frame->base[ 0 ] + frame->pitches[ 0 ] * y + x * 2 + 0);
    x &= ~1;
    Cr = *(frame->base[ 0 ] + frame->pitches[ 0 ] * y + x * 2 + 1);
    Cb = *(frame->base[ 0 ] + frame->pitches[ 0 ] * y + x * 2 + 3);
    break;
  }

  return (Y == 0x10 && Cr == 0x80 && Cb == 0x80);
}


static int expand_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_expand_t     *this = (post_expand_t *)port->post;
  int                skip;

  if (this->centre_cut_out_mode && !frame->bad_frame)
  {
    /* expected area of inner 4:3 image */
    int centre_width = frame->width * (9 * 4) / (16 * 3);
    int centre_left  = (frame->width - centre_width ) / 2;

    /* centre point for detecting a black frame */
    int centre_x = frame->width  / 2;
    int centre_y = frame->height / 2;

    /* ignore a black frame as it could lead to wrong results */
    if (!is_pixel_black(frame, centre_x, centre_y))
    {
      /* coordinates for testing black border near the centre area */
      int test_left  = centre_left - 16;
      int test_right = centre_left + 16 + centre_width;

      /* enable cropping when these pixels are black */
      this->cropping_active = is_pixel_black(frame, test_left, centre_y)
        && is_pixel_black(frame, test_right, centre_y);
    }

    /* crop frame */
    if (this->centre_cut_out_mode && this->cropping_active) {
      frame->crop_left  += centre_left;
      frame->crop_right += centre_left;

      /* get_frame() allocated an extra high frame */
      frame->crop_top    += (frame->next->height - frame->height) / 2;
      frame->crop_bottom += (frame->next->height - frame->height) / 2;
    }
  }

  frame->ratio = this->aspect;
  _x_post_frame_copy_down(frame, frame->next);
  skip = frame->next->draw(frame->next, stream);
  _x_post_frame_copy_up(frame, frame->next);

  return skip;
}


static vo_frame_t *expand_get_frame(xine_video_port_t *port_gen, uint32_t width,
				    uint32_t height, double ratio,
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_t     *this = (post_expand_t *)port->post;
  vo_frame_t        *frame;
  uint32_t           new_height, top_bar_height;
  int                i, end;

  _x_post_rewire(&this->post);

  if (ratio <= 0.0) ratio = (double)width / (double)height;

  /* Calculate height of expanded frame */
  new_height = (double)height * ratio / this->aspect;
  new_height = (new_height + 1) & ~1;
  top_bar_height = (new_height - height) / 2;
  top_bar_height = (top_bar_height + 1) & ~1;

  this->top_bar_height = top_bar_height;

  if (new_height > height &&
      (format == XINE_IMGFMT_YV12 || format == XINE_IMGFMT_YUY2)) {
    frame = port->original_port->get_frame(port->original_port,
      width, new_height, this->aspect, format, flags);

    _x_post_inc_usage(port);
    frame = _x_post_intercept_video_frame(frame, port);

    /* paint black bars in the top and bottom of the frame and hide these
     * from the decoders by modifying the pointers to and
     * the size of the drawing area */
    frame->height = height;
    frame->ratio  = ratio;
    switch (format) {
    case XINE_IMGFMT_YV12:
      /* paint top bar */
      memset(frame->base[0],   0, frame->pitches[0] * top_bar_height    );
      memset(frame->base[1], 128, frame->pitches[1] * top_bar_height / 2);
      memset(frame->base[2], 128, frame->pitches[2] * top_bar_height / 2);
      /* paint bottom bar */
      memset(frame->base[0] + frame->pitches[0] * (top_bar_height + height)    ,   0,
        frame->pitches[0] * (new_height - top_bar_height - height)    );
      memset(frame->base[1] + frame->pitches[1] * (top_bar_height + height) / 2, 128,
        frame->pitches[1] * (new_height - top_bar_height - height) / 2);
      memset(frame->base[2] + frame->pitches[2] * (top_bar_height + height) / 2, 128,
        frame->pitches[2] * (new_height - top_bar_height - height) / 2);
      /* modify drawing area */
      frame->base[0] += frame->pitches[0] * top_bar_height;
      frame->base[1] += frame->pitches[1] * top_bar_height / 2;
      frame->base[2] += frame->pitches[2] * top_bar_height / 2;
      break;
    case XINE_IMGFMT_YUY2:
      /* paint top bar */
      end = frame->pitches[0] * top_bar_height;
      for (i = 0; i < end; i += 2) {
	frame->base[0][i]   = 0;
	frame->base[0][i+1] = 128;
      }
      /* paint bottom bar */
      end = frame->pitches[0] * new_height;
      for (i = frame->pitches[0] * (top_bar_height + height); i < end; i += 2) {
	frame->base[0][i]   = 0;
	frame->base[0][i+1] = 128;
      }
      /* modify drawing area */
      frame->base[0] += frame->pitches[0] * top_bar_height;
    }
  } else {
    frame = port->original_port->get_frame(port->original_port,
      width, height, ratio, format, flags);
    /* no need to intercept this one, we are not going to do anything with it */
  }

  return frame;
}


static int expand_intercept_ovl(post_video_port_t *port)
{
  post_expand_t         *this = (post_expand_t *)port->post;

  if (this->centre_cut_out_mode && this->cropping_active) return 0;

  /* we always intercept overlay manager */
  return 1;
}


static int32_t expand_overlay_add_event(video_overlay_manager_t *this_gen, void *event_gen)
{
  video_overlay_event_t *event = (video_overlay_event_t *)event_gen;
  post_video_port_t     *port = _x_post_ovl_manager_to_port(this_gen);
  post_expand_t         *this = (post_expand_t *)port->post;

  if (event->event_type == OVERLAY_EVENT_SHOW) {
    switch (event->object.object_type) {
    case 0:
      /* regular subtitle */
      if (this->enable_automatic_shift)
	event->object.overlay->y += 2 * this->top_bar_height;
      else
	event->object.overlay->y += this->overlay_y_offset;
      break;
    case 1:
      /* menu overlay */
      event->object.overlay->y += this->top_bar_height;
    }
  }

  return port->original_manager->add_event(port->original_manager, event_gen);
}
