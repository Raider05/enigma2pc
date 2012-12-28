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
 * simple video mosaico plugin
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define LOG_MODULE "mosaico"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/post.h>

/* FIXME: This plugin needs to handle overlays as well. */

/* plugin class initialization function */
static void *mosaico_init_plugin(xine_t *xine, void *);

/* plugin catalog information */
static const post_info_t mosaico_special_info = { XINE_POST_TYPE_VIDEO_COMPOSE };

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST, 10, "mosaico", XINE_VERSION_CODE, &mosaico_special_info, &mosaico_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

typedef struct mosaico_parameters_s {
  unsigned int  pip_num;
  unsigned int  x, y, w, h;
} mosaico_parameters_t;

START_PARAM_DESCR(mosaico_parameters_t)
PARAM_ITEM(POST_PARAM_TYPE_INT, pip_num, NULL, 1, INT_MAX, 1,
  "which picture slots settings are being edited")
PARAM_ITEM(POST_PARAM_TYPE_INT, x, NULL, 0, INT_MAX, 50,
  "x coordinate of the pasted picture")
PARAM_ITEM(POST_PARAM_TYPE_INT, y, NULL, 0, INT_MAX, 50,
  "y coordinate of the pasted picture")
PARAM_ITEM(POST_PARAM_TYPE_INT, w, NULL, 0, INT_MAX, 150,
  "width of the pasted picture")
PARAM_ITEM(POST_PARAM_TYPE_INT, h, NULL, 0, INT_MAX, 150,
  "height of the pasted picture")
END_PARAM_DESCR(mosaico_param_descr)

typedef struct post_class_mosaico_s post_class_mosaico_t;
typedef struct post_mosaico_s post_mosaico_t;

struct post_class_mosaico_s {
  post_class_t    class;
  xine_t         *xine;
};

/* plugin structures */
typedef struct mosaico_pip_s mosaico_pip_t;
struct mosaico_pip_s {
  unsigned int  x, y, w, h;
  vo_frame_t   *frame;
  char         *input_name;
};

struct post_mosaico_s {
  post_plugin_t    post;
  xine_post_in_t   parameter_input;

  mosaico_pip_t   *pip;
  int64_t          vpts_limit;
  pthread_cond_t   vpts_limit_changed;
  int64_t          skip_vpts;
  int              skip;
  pthread_mutex_t  mutex;
  unsigned int     pip_count;
};

/* plugin class functions */
static post_plugin_t *mosaico_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           mosaico_dispose(post_plugin_t *this_gen);

/* parameter functions */
static xine_post_api_descr_t *mosaico_get_param_descr(void);
static int            mosaico_set_parameters(xine_post_t *this_gen, void *param_gen);
static int            mosaico_get_parameters(xine_post_t *this_gen, void *param_gen);
static char          *mosaico_get_help(void);

/* replaced video port functions */
static void           mosaico_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* frame intercept check */
static int            mosaico_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            mosaico_draw_background(vo_frame_t *frame, xine_stream_t *stream);
static int            mosaico_draw(vo_frame_t *frame, xine_stream_t *stream);


static void *mosaico_init_plugin(xine_t *xine, void *data)
{
  post_class_mosaico_t *this = calloc(1, sizeof(post_class_mosaico_t));

  if (!this)
    return NULL;

  this->class.open_plugin     = mosaico_open_plugin;
  this->class.identifier      = "mosaico";
  this->class.description     = N_("Mosaico is a picture in picture (pip) post plugin");
  this->class.dispose         = default_post_class_dispose;
  this->xine                  = xine;

  return &this->class;
}

static post_plugin_t *mosaico_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_mosaico_t       *this = calloc(1, sizeof(post_mosaico_t));
  post_in_t            *input;
  xine_post_in_t       *input_api;
  post_out_t           *output;
  post_video_port_t    *port;
  static xine_post_api_t post_api =
    { mosaico_set_parameters, mosaico_get_parameters, mosaico_get_param_descr, mosaico_get_help };
  int i;

  lprintf("mosaico open\n");

  if (inputs < 2 || !this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, inputs);

  this->pip       = (mosaico_pip_t *)calloc((inputs - 1), sizeof(mosaico_pip_t));
  this->pip_count = inputs - 1;

  pthread_cond_init(&this->vpts_limit_changed, NULL);
  pthread_mutex_init(&this->mutex, NULL);

  /* the port for the background video */
  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->intercept_frame = mosaico_intercept_frame;
  port->new_frame->draw = mosaico_draw_background;
  port->port_lock       = &this->mutex;
  port->frame_lock      = &this->mutex;
  input->xine_in.name   = "video in 0";
  this->post.xine_post.video_input[0] = &port->new_port;

  for (i = 0; i < inputs - 1; i++) {
    this->pip[i].x = 50;
    this->pip[i].y = 50;
    this->pip[i].w = 150;
    this->pip[i].h = 150;
    this->pip[i].input_name = _x_asprintf("video in %d", i+1);

    port = _x_post_intercept_video_port(&this->post, video_target[0], &input, NULL);
    port->new_port.close  = mosaico_close;
    port->intercept_frame = mosaico_intercept_frame;
    port->new_frame->draw = mosaico_draw;
    port->port_lock       = &this->mutex;
    port->frame_lock      = &this->mutex;
    input->xine_in.name   = this->pip[i].input_name;
    this->post.xine_post.video_input[i+1] = &port->new_port;
  }

  input_api       = &this->parameter_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  this->post.dispose = mosaico_dispose;

  return &this->post;
}

static void mosaico_dispose(post_plugin_t *this_gen)
{
  post_mosaico_t *this = (post_mosaico_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    int i;
    for (i = 0; i < this->pip_count; i++)
      free(this->pip[i].input_name);
    free(this->pip);
    pthread_cond_destroy(&this->vpts_limit_changed);
    pthread_mutex_destroy(&this->mutex);
    free(this);
  }
}


static xine_post_api_descr_t *mosaico_get_param_descr(void)
{
  return &mosaico_param_descr;
}

static int mosaico_set_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_mosaico_t *this = (post_mosaico_t *)this_gen;
  mosaico_parameters_t *param = (mosaico_parameters_t *)param_gen;

  if (param->pip_num > this->pip_count) return 0;
  this->pip[param->pip_num - 1].x = param->x;
  this->pip[param->pip_num - 1].y = param->y;
  this->pip[param->pip_num - 1].w = param->w;
  this->pip[param->pip_num - 1].h = param->h;
  return 1;
}

static int mosaico_get_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_mosaico_t *this = (post_mosaico_t *)this_gen;
  mosaico_parameters_t *param = (mosaico_parameters_t *)param_gen;

  if (param->pip_num > this->pip_count || param->pip_num < 1)
    param->pip_num = 1;
  param->x = this->pip[param->pip_num - 1].x;
  param->y = this->pip[param->pip_num - 1].y;
  param->w = this->pip[param->pip_num - 1].w;
  param->h = this->pip[param->pip_num - 1].h;
  return 1;
}

static char *mosaico_get_help(void)
{
  return _("Mosaico does simple picture in picture effects.\n"
	   "\n"
	   "Parameters\n"
	   "  pip_num: the number of the picture slot the following settings apply to\n"
	   "  x: the x coordinate of the left upper corner of the picture\n"
	   "  y: the y coordinate of the left upper corner of the picture\n"
	   "  w: the width of the picture\n"
	   "  h: the height of the picture\n");
}


static void mosaico_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_mosaico_t *this = (post_mosaico_t *)port->post;
  vo_frame_t *free_frame;
  int pip_num;

  for (pip_num = 0; pip_num < this->pip_count; pip_num++)
    if (this->post.xine_post.video_input[pip_num+1] == port_gen) break;

  pthread_mutex_lock(&this->mutex);
  free_frame = this->pip[pip_num].frame;
  this->pip[pip_num].frame = NULL;
  port->original_port->close(port->original_port, port->stream);
  pthread_mutex_unlock(&this->mutex);

  if (free_frame)
    free_frame->free(free_frame);
  port->stream = NULL;
  _x_post_dec_usage(port);
}


static int mosaico_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  /* TODO: only YV12 supported */
  return (frame->format == XINE_IMGFMT_YV12);
}


static void frame_copy_content(vo_frame_t *to, vo_frame_t *from)
{
  int size;

  switch (from->format) {
  case XINE_IMGFMT_YUY2:
    /* TODO: implement conversion to YV12 or implement support to paste
     * frames of different types together */
    break;

  case XINE_IMGFMT_YV12:
    /* Y */
    size = to->pitches[0] * to->height;
    xine_fast_memcpy(to->base[0], from->base[0], size);

    /* U */
    size = to->pitches[1] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[1], from->base[1], size);

    /* V */
    size = to->pitches[2] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[2], from->base[2], size);
  }
}

static void frame_paste(post_mosaico_t *this, vo_frame_t *background, int pip_num)
{
  unsigned long target_width, target_height;
  unsigned long source_width, source_height;
  unsigned long background_width;
  unsigned long scale_x, scale_y;
  const int shift_x = 3, shift_y = 3;
  unsigned long pos_x, pos_y, pos;
  unsigned long target_offset, source_offset;
  unsigned long i, j;

  if (!this->pip[pip_num].frame) return;

  target_width  = this->pip[pip_num].w;
  target_height = this->pip[pip_num].h;
  background_width = background->width;
  source_width = this->pip[pip_num].frame->width;
  source_height = this->pip[pip_num].frame->height;
  scale_x = (source_width << shift_x) / target_width;
  scale_y = (source_height << shift_y) / target_height;
  pos_x = this->pip[pip_num].x;
  pos_y = this->pip[pip_num].y;
  pos = pos_y * background_width + pos_x;

  switch (this->pip[pip_num].frame->format) {
  case XINE_IMGFMT_YUY2:
    /* TODO: implement YUY2 */
    break;

  case XINE_IMGFMT_YV12:
    /* Y */
    target_offset = 0;
    for (j = 0; j < target_height; j++, target_offset += (background_width - target_width))
      for (i = 0; i < target_width; i++, target_offset++) {
	source_offset = ((i * scale_x) >> shift_x) + (((j * scale_y) >> shift_y) * source_width);
	background->base[0][pos + target_offset] = this->pip[pip_num].frame->base[0][source_offset];
      }

    background_width = (background_width + 1) / 2;
    source_width = (source_width + 1) / 2;
    pos_x = (pos_x + 1) / 2;
    pos_y = (pos_y + 1) / 2;
    pos = pos_y * background_width + pos_x;
    target_width = (target_width + 1) / 2;
    target_height = (target_height + 1) / 2;

    /* U */
    target_offset = 0;
    for (j = 0; j < target_height; j++, target_offset += (background_width - target_width))
      for (i = 0; i < target_width; i++, target_offset++) {
	source_offset = ((i * scale_x) >> shift_x) + (((j * scale_y) >> shift_y) * source_width);
	background->base[1][pos + target_offset] = this->pip[pip_num].frame->base[1][source_offset];
      }

    /* V */
    target_offset = 0;
    for (j = 0; j < target_height; j++, target_offset += (background_width - target_width))
      for (i = 0; i < target_width; i++, target_offset++) {
	source_offset = ((i * scale_x) >> shift_x) + (((j * scale_y) >> shift_y) * source_width);
	background->base[2][pos + target_offset] = this->pip[pip_num].frame->base[2][source_offset];
      }

    break;
  }
}

static int mosaico_draw_background(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_mosaico_t *this = (post_mosaico_t *)port->post;
  vo_frame_t *background;
  int pip_num, skip;

  pthread_mutex_lock(&this->mutex);

  if (frame->bad_frame) {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);

    this->vpts_limit = frame->vpts + frame->duration;
    if (skip) {
      this->skip      = skip;
      this->skip_vpts = frame->vpts;
    } else
      this->skip      = 0;

    pthread_mutex_unlock(&this->mutex);
    pthread_cond_broadcast(&this->vpts_limit_changed);

    return skip;
  }

  background = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, frame->flags | VO_BOTH_FIELDS);
  _x_post_frame_copy_down(frame, background);
  frame_copy_content(background, frame);

  for (pip_num = 0; pip_num < this->pip_count; pip_num++)
    frame_paste(this, background, pip_num);

  skip = background->draw(background, stream);
  _x_post_frame_copy_up(frame, background);
  this->vpts_limit = background->vpts + background->duration;
  background->free(background);

  if (skip) {
    this->skip      = skip;
    this->skip_vpts = frame->vpts;
  } else
    this->skip      = 0;

  pthread_mutex_unlock(&this->mutex);
  pthread_cond_broadcast(&this->vpts_limit_changed);

  return skip;
}

static int mosaico_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_mosaico_t *this = (post_mosaico_t *)port->post;
  vo_frame_t *free_frame;
  int pip_num, skip;

  for (pip_num = 0; pip_num < this->pip_count; pip_num++)
    if (this->post.xine_post.video_input[pip_num+1] == frame->port) break;
  _x_assert(pip_num < this->pip_count);

  frame->lock(frame);

  pthread_mutex_lock(&this->mutex);

  /* the original output will never see this frame again */
  _x_post_frame_u_turn(frame, stream);
  while (frame->vpts > this->vpts_limit || !this->vpts_limit)
    /* we are too early */
    pthread_cond_wait(&this->vpts_limit_changed, &this->mutex);
  free_frame = this->pip[pip_num].frame;
  if (port->stream)
    this->pip[pip_num].frame = frame;

  if (this->skip && frame->vpts <= this->skip_vpts)
    skip = this->skip;
  else
    skip = 0;

  pthread_mutex_unlock(&this->mutex);

  if (free_frame)
    free_frame->free(free_frame);
  if (!port->stream)
    /* do not keep this frame when no stream is connected to us,
     * otherwise, this frame might never get freed */
    frame->free(frame);

  return skip;
}
