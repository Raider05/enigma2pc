/*
 * Copyright (C) 2000-2013 the xine project
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
 * advanced video deinterlacer plugin
 * Jun/2003 by Miguel Freitas
 *
 * heavily based on tvtime.sf.net by Billy Biggs
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <xine/xine_buffer.h>
#include <pthread.h>

#include "tvtime.h"
#include "speedy.h"
#include "deinterlace.h"
#include "plugins/plugins.h"

/* plugin class initialization function */
static void *deinterlace_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
static const post_info_t deinterlace_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST | PLUGIN_MUST_PRELOAD, 10, "tvtime", XINE_VERSION_CODE, &deinterlace_special_info, &deinterlace_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


typedef struct post_plugin_deinterlace_s post_plugin_deinterlace_t;

#define MAX_NUM_METHODS 30
static const char *enum_methods[MAX_NUM_METHODS];
static const char *const enum_pulldown[] = { "none", "vektor", NULL };
static const char *const enum_framerate[] = { "full", "half_top", "half_bottom", NULL };

static void *help_string;

/*
 * this is the struct used by "parameters api"
 */
typedef struct deinterlace_parameters_s {

  int method;
  int enabled;
  int pulldown;
  int pulldown_error_wait;
  int framerate_mode;
  int judder_correction;
  int use_progressive_frame_flag;
  int chroma_filter;
  int cheap_mode;

} deinterlace_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( deinterlace_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, method, enum_methods, 0, 0, 0,
            "deinterlace method" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, enabled, NULL, 0, 1, 0,
            "enable/disable" )
PARAM_ITEM( POST_PARAM_TYPE_INT, pulldown, enum_pulldown, 0, 0, 0,
            "pulldown algorithm" )
PARAM_ITEM( POST_PARAM_TYPE_INT, pulldown_error_wait, NULL, 0, 0, 0,
            "number of frames of telecine pattern sync required before mode change" )
PARAM_ITEM( POST_PARAM_TYPE_INT, framerate_mode, enum_framerate, 0, 0, 0,
            "framerate output mode" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, judder_correction, NULL, 0, 1, 0,
            "make frames evenly spaced for film mode (24 fps)" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, use_progressive_frame_flag, NULL, 0, 1, 0,
            "disable deinterlacing when progressive_frame flag is set" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, chroma_filter, NULL, 0, 1, 0,
            "apply chroma filter after deinterlacing" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, cheap_mode, NULL, 0, 1, 0,
            "skip image format conversion - cheaper but not 100% correct" )
END_PARAM_DESCR( param_descr )


#define NUM_RECENT_FRAMES  2
#define FPS_24_DURATION    3754
#define FRAMES_TO_SYNC     20

/* plugin structure */
struct post_plugin_deinterlace_s {
  post_plugin_t      post;
  xine_post_in_t     parameter_input;

  /* private data */
  int                cur_method;
  int                enabled;
  int                pulldown;
  int                framerate_mode;
  int                judder_correction;
  int                use_progressive_frame_flag;
  int                chroma_filter;
  int                cheap_mode;
  tvtime_t          *tvtime;
  int                tvtime_changed;
  int                tvtime_last_filmmode;
  int                vo_deinterlace_enabled;

  int                framecounter;
  uint8_t            rff_pattern;

  vo_frame_t        *recent_frame[NUM_RECENT_FRAMES];

  pthread_mutex_t    lock;
};


typedef struct post_class_deinterlace_s {
  post_class_t class;
  deinterlace_parameters_t init_param;
} post_class_deinterlace_t;

static void _flush_frames(post_plugin_deinterlace_t *this)
{
  int i;

  for( i = 0; i < NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frame[i] ) {
      this->recent_frame[i]->free(this->recent_frame[i]);
      this->recent_frame[i] = NULL;
    }
  }
  this->tvtime_changed++;
}

static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  if( this->enabled != param->enabled ||
      this->cheap_mode != param->cheap_mode )
    _flush_frames(this);

  this->cur_method = param->method;

  this->enabled = param->enabled;

  this->pulldown = param->pulldown;
  this->tvtime->pulldown_error_wait = param->pulldown_error_wait;
  this->framerate_mode = param->framerate_mode;
  this->judder_correction = param->judder_correction;
  this->use_progressive_frame_flag = param->use_progressive_frame_flag;
  this->chroma_filter = param->chroma_filter;
  this->cheap_mode = param->cheap_mode;

  this->tvtime_changed++;

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;

  param->method = this->cur_method;
  param->enabled = this->enabled;
  param->pulldown = this->pulldown;
  param->pulldown_error_wait = this->tvtime->pulldown_error_wait;
  param->framerate_mode = this->framerate_mode;
  param->judder_correction = this->judder_correction;
  param->use_progressive_frame_flag = this->use_progressive_frame_flag;
  param->chroma_filter = this->chroma_filter;
  param->cheap_mode = this->cheap_mode;

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_static_help (void) {
  return _("Advanced tvtime/deinterlacer plugin with pulldown detection\n"
           "This plugin aims to provide deinterlacing mechanisms comparable "
           "to high quality progressive DVD players and so called "
           "line-doublers, for use with computer monitors, projectors and "
           "other progressive display devices.\n"
           "\n"
           "Parameters\n"
           "\n"
           "  Method: Select deinterlacing method/algorithm to use, see below for "
           "explanation of each method.\n"
           "\n"
           "  Enabled: Enable/disable the plugin.\n"
           "\n"
           "  Pulldown_error_wait: Ensures that the telecine pattern has been "
           "locked for this many frames before changing to filmmode.\n"
           "\n"
           "  Pulldown: Choose the 2-3 pulldown detection algorithm. 24 FPS films "
           "that have being converted to NTSC can be detected and intelligently "
           "reconstructed to their original (non-interlaced) frames.\n"
           "\n"
           "  Framerate_mode: Selecting 'full' will deinterlace every field "
           "to an unique frame for television quality and beyond. This feature will "
           "effetively double the frame rate, improving smoothness. Note, however, "
           "that full 59.94 FPS is not possible with plain 2.4 Linux kernel (that "
           "use a timer interrupt frequency of 100Hz). Newer RedHat and 2.6 kernels "
           "use higher HZ settings (512 and 1000, respectively) and should work fine.\n"
           "\n"
           "  Judder_correction: Once 2-3 pulldown is enabled and a film material "
           "is detected, it is possible to reduce the frame rate to original rate "
           "used (24 FPS). This will make the frames evenly spaced in time, "
           "matching the speed they were shot and eliminating the judder effect.\n"
           "\n"
           "  Use_progressive_frame_flag: Well mastered MPEG2 streams uses a flag "
           "to indicate progressive material. This setting control whether we trust "
           "this flag or not (some rare and buggy mpeg2 streams set it wrong).\n"
           "\n"
           "  Chroma_filter: DVD/MPEG2 use an interlaced image format that has "
           "a very poor vertical chroma resolution. Upsampling the chroma for purposes "
           "of deinterlacing may cause some artifacts to occur (eg. colour stripes). Use "
           "this option to blur the chroma vertically after deinterlacing to remove "
           "the artifacts. Warning: cpu intensive.\n"
           "\n"
           "  Cheap_mode: This will skip the expensive YV12->YUY2 image conversion, "
           "tricking tvtime/dscaler routines like if they were still handling YUY2 "
           "images. Of course, this is not correct, not all pixels will be evaluated "
           "by the algorithms to decide the regions to deinterlace and chroma will be "
           "processed separately. Nevertheless, it allows people with not so fast "
           "systems to try deinterlace algorithms, in a tradeoff between quality "
           "and cpu usage.\n"
           "\n"
           "* Uses several algorithms from tvtime and dscaler projects.\n"
           "Deinterlacing methods: (Not all methods are available for all platforms)\n"
           "\n"
           );
}

static char * get_help (void) {
  return (char *)help_string;
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *deinterlace_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static void           deinterlace_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           deinterlace_dispose(post_plugin_t *this_gen);

/* replaced video_port functions */
static int            deinterlace_get_property(xine_video_port_t *port_gen, int property);
static int            deinterlace_set_property(xine_video_port_t *port_gen, int property, int value);
static void           deinterlace_flush(xine_video_port_t *port_gen);
static void           deinterlace_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static void           deinterlace_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* frame intercept check */
static int            deinterlace_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            deinterlace_draw(vo_frame_t *frame, xine_stream_t *stream);


static void *deinterlace_init_plugin(xine_t *xine, void *data)
{
  post_class_deinterlace_t *class = calloc(1, sizeof(post_class_deinterlace_t));
  uint32_t config_flags = xine_mm_accel();
  int i;

  if (!class)
    return NULL;

  class->class.open_plugin     = deinterlace_open_plugin;
  class->class.identifier      = "tvtime";
  class->class.description     = N_("advanced deinterlacer plugin with pulldown detection");
  class->class.dispose         = deinterlace_class_dispose;


  setup_speedy_calls(xine_mm_accel(),0);

  register_deinterlace_method( linear_get_method() );
  register_deinterlace_method( linearblend_get_method() );
  register_deinterlace_method( greedy_get_method() );
  register_deinterlace_method( greedy2frame_get_method() );
  register_deinterlace_method( weave_get_method() );
  register_deinterlace_method( double_get_method() );
  register_deinterlace_method( vfir_get_method() );
  register_deinterlace_method( scalerbob_get_method() );
  register_deinterlace_method( dscaler_greedyh_get_method() );
  register_deinterlace_method( dscaler_tomsmocomp_get_method() );

  filter_deinterlace_methods( config_flags, 5 /*fieldsavailable*/ );
  if( !get_num_deinterlace_methods() ) {
      xprintf(xine, XINE_VERBOSITY_LOG,
	      _("tvtime: No deinterlacing methods available, exiting.\n"));
      return NULL;
  }

  help_string = xine_buffer_init(1024);
  xine_buffer_strcat( help_string, get_static_help() );

  enum_methods[0] = "use_vo_driver";
  for(i = 0; i < get_num_deinterlace_methods(); i++ ) {
    deinterlace_method_t *method;

    method = get_deinterlace_method(i);

    enum_methods[i+1] = method->short_name;
    xine_buffer_strcat( help_string, "[" );
    xine_buffer_strcat( help_string, method->short_name );
    xine_buffer_strcat( help_string, "] " );
    xine_buffer_strcat( help_string, method->name );
    xine_buffer_strcat( help_string, ":\n" );
    if (method->description)
      xine_buffer_strcat( help_string, method->description );
    xine_buffer_strcat( help_string, "\n---\n" );
  }
  enum_methods[i+1] = NULL;


  /* Some default values */
  class->init_param.method                     = 1; /* First (plugin) method available */
  class->init_param.enabled                    = 1;
  class->init_param.pulldown                   = 1; /* vektor */
  class->init_param.pulldown_error_wait        = 60; /* about one second */
  class->init_param.framerate_mode             = 0; /* full */
  class->init_param.judder_correction          = 1;
  class->init_param.use_progressive_frame_flag = 1;
  class->init_param.chroma_filter              = 0;
  class->init_param.cheap_mode                 = 0;

  return &class->class;
}


static post_plugin_t *deinterlace_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_deinterlace_t *this = calloc(1, sizeof(post_plugin_deinterlace_t));
  post_in_t                 *input;
  xine_post_in_t            *input_api;
  post_out_t                *output;
  post_class_deinterlace_t  *class = (post_class_deinterlace_t *)class_gen;
  post_video_port_t *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  this->tvtime = tvtime_new_context();
  this->tvtime_changed++;
  this->tvtime_last_filmmode = 0;

  pthread_mutex_init (&this->lock, NULL);

  set_parameters (&this->post.xine_post, &class->init_param);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  /* replace with our own get_frame function */
  port->new_port.open         = deinterlace_open;
  port->new_port.close        = deinterlace_close;
  port->new_port.get_property = deinterlace_get_property;
  port->new_port.set_property = deinterlace_set_property;
  port->new_port.flush        = deinterlace_flush;
  port->intercept_frame       = deinterlace_intercept_frame;
  port->new_frame->draw       = deinterlace_draw;

  input_api       = &this->parameter_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "deinterlaced video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = deinterlace_dispose;

  return &this->post;
}

static void deinterlace_class_dispose(post_class_t *class_gen)
{
  xine_buffer_free(help_string);
  free(class_gen);
}


static void deinterlace_dispose(post_plugin_t *this_gen)
{
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    _flush_frames(this);
    pthread_mutex_destroy(&this->lock);
    free(this->tvtime);
    free(this);
  }
}


static int deinterlace_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  if( property == XINE_PARAM_VO_DEINTERLACE && this->cur_method )
    return this->enabled;
  else
    return port->original_port->get_property(port->original_port, property);
}

static int deinterlace_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  if( property == XINE_PARAM_VO_DEINTERLACE ) {
    pthread_mutex_lock (&this->lock);

    if( this->enabled != value )
      _flush_frames(this);

    this->enabled = value;

    pthread_mutex_unlock (&this->lock);

    this->vo_deinterlace_enabled = this->enabled && (!this->cur_method);

    port->original_port->set_property(port->original_port,
                                      XINE_PARAM_VO_DEINTERLACE,
                                      this->vo_deinterlace_enabled);

    return this->enabled;
  } else
    return port->original_port->set_property(port->original_port, property, value);
}

static void deinterlace_flush(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;

  _flush_frames(this);

  port->original_port->flush(port->original_port);
}

static void deinterlace_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);
  port->stream = stream;
  (port->original_port->open) (port->original_port, stream);
  this->vo_deinterlace_enabled = !this->cur_method;
  port->original_port->set_property(port->original_port,
                                    XINE_PARAM_VO_DEINTERLACE,
                                    this->vo_deinterlace_enabled);
}

static void deinterlace_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;

  port->stream = NULL;
  _flush_frames(this);
  port->original_port->set_property(port->original_port,
                                    XINE_PARAM_VO_DEINTERLACE,
                                    0);
  port->original_port->close(port->original_port, stream);
  _x_post_dec_usage(port);
}


static int deinterlace_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  int vo_deinterlace_enabled = 0;

  vo_deinterlace_enabled = ( frame->format != XINE_IMGFMT_YV12 &&
                             frame->format != XINE_IMGFMT_YUY2 &&
                             this->enabled );

  if( this->cur_method &&
      this->vo_deinterlace_enabled != vo_deinterlace_enabled ) {
    this->vo_deinterlace_enabled = vo_deinterlace_enabled;
    port->original_port->set_property(port->original_port,
                                      XINE_PARAM_VO_DEINTERLACE,
                                      this->vo_deinterlace_enabled);
  }

  return (this->enabled && this->cur_method &&
      (frame->flags & VO_INTERLACED_FLAG) &&
      (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2) );
}


static void apply_chroma_filter( uint8_t *data, int stride, int width, int height )
{
  int i;

  /* ok, using linearblend inplace is a bit weird: the result of a scanline
   * interpolation will affect the next scanline. this might not be a problem
   * at all, we just want a kind of filter here.
   */
  for( i = 0; i < height; i++, data += stride ) {
    vfilter_chroma_332_packed422_scanline( data, width,
                                           data,
                                           (i) ? (data - stride) : data,
                                           (i < height-1) ? (data + stride) : data );
  }
}

/* Build the output frame from the specified field. */
static int deinterlace_build_output_field(
             post_plugin_deinterlace_t *this, post_video_port_t *port,
             xine_stream_t *stream,
             vo_frame_t *frame, vo_frame_t *yuy2_frame,
             int bottom_field, int second_field,
             int64_t pts, int64_t duration, int skip)
{
  vo_frame_t *deinterlaced_frame;
  int scaler = 1;
  int force24fps;

  force24fps = this->judder_correction && !this->cheap_mode &&
               ( this->pulldown == PULLDOWN_VEKTOR && this->tvtime->filmmode );

  if( this->tvtime->curmethod->doscalerbob ) {
    scaler = 2;
  }

  pthread_mutex_unlock (&this->lock);
  deinterlaced_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height / scaler, frame->ratio, yuy2_frame->format,
    frame->flags | VO_BOTH_FIELDS);
  pthread_mutex_lock (&this->lock);

  deinterlaced_frame->crop_left   = frame->crop_left;
  deinterlaced_frame->crop_right  = frame->crop_right;
  deinterlaced_frame->crop_top    = frame->crop_top;
  deinterlaced_frame->crop_bottom = frame->crop_bottom;

  _x_extra_info_merge(deinterlaced_frame->extra_info, frame->extra_info);

  if( skip > 0 && !this->pulldown ) {
    deinterlaced_frame->bad_frame = 1;
  } else {
    if( this->tvtime->curmethod->doscalerbob ) {
      if( yuy2_frame->format == XINE_IMGFMT_YUY2 ) {
        deinterlaced_frame->bad_frame = !tvtime_build_copied_field(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0], bottom_field,
                           frame->width, frame->height,
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0] );
      } else {
        deinterlaced_frame->bad_frame = !tvtime_build_copied_field(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0], bottom_field,
                           frame->width/2, frame->height,
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0] );
        deinterlaced_frame->bad_frame += !tvtime_build_copied_field(this->tvtime,
                           deinterlaced_frame->base[1],
                           yuy2_frame->base[1], bottom_field,
                           frame->width/4, frame->height/2,
                           yuy2_frame->pitches[1], deinterlaced_frame->pitches[1] );
        deinterlaced_frame->bad_frame += !tvtime_build_copied_field(this->tvtime,
                           deinterlaced_frame->base[2],
                           yuy2_frame->base[2], bottom_field,
                           frame->width/4, frame->height/2,
                           yuy2_frame->pitches[2], deinterlaced_frame->pitches[2] );
      }
    } else {
      if( yuy2_frame->format == XINE_IMGFMT_YUY2 ) {
        deinterlaced_frame->bad_frame = !tvtime_build_deinterlaced_frame(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0],
                           (this->recent_frame[0])?this->recent_frame[0]->base[0]:yuy2_frame->base[0],
                           (this->recent_frame[1])?this->recent_frame[1]->base[0]:yuy2_frame->base[0],
                           bottom_field, second_field, frame->width, frame->height,
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0]);
      } else {
        deinterlaced_frame->bad_frame = !tvtime_build_deinterlaced_frame(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0],
                           (this->recent_frame[0])?this->recent_frame[0]->base[0]:yuy2_frame->base[0],
                           (this->recent_frame[1])?this->recent_frame[1]->base[0]:yuy2_frame->base[0],
                           bottom_field, second_field, frame->width/2, frame->height,
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0]);
        deinterlaced_frame->bad_frame += !tvtime_build_deinterlaced_frame(this->tvtime,
                           deinterlaced_frame->base[1],
                           yuy2_frame->base[1],
                           (this->recent_frame[0])?this->recent_frame[0]->base[1]:yuy2_frame->base[1],
                           (this->recent_frame[1])?this->recent_frame[1]->base[1]:yuy2_frame->base[1],
                           bottom_field, second_field, frame->width/4, frame->height/2,
                           yuy2_frame->pitches[1], deinterlaced_frame->pitches[1]);
        deinterlaced_frame->bad_frame += !tvtime_build_deinterlaced_frame(this->tvtime,
                           deinterlaced_frame->base[2],
                           yuy2_frame->base[2],
                           (this->recent_frame[0])?this->recent_frame[0]->base[2]:yuy2_frame->base[2],
                           (this->recent_frame[1])?this->recent_frame[1]->base[2]:yuy2_frame->base[2],
                           bottom_field, second_field, frame->width/4, frame->height/2,
                           yuy2_frame->pitches[2], deinterlaced_frame->pitches[2]);
      }
    }
  }

  pthread_mutex_unlock (&this->lock);
  if( force24fps ) {
    if( !deinterlaced_frame->bad_frame ) {
      this->framecounter++;
      if( pts && this->framecounter > FRAMES_TO_SYNC ) {
        deinterlaced_frame->pts = pts;
        this->framecounter = 0;
      } else
        deinterlaced_frame->pts = 0;
      deinterlaced_frame->duration = FPS_24_DURATION;
      if( this->chroma_filter && !this->cheap_mode )
        apply_chroma_filter( deinterlaced_frame->base[0], deinterlaced_frame->pitches[0],
                             frame->width, frame->height / scaler );
      skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
    } else {
      skip = 0;
    }
  } else {
    deinterlaced_frame->pts = pts;
    deinterlaced_frame->duration = duration;
    if( this->chroma_filter && !this->cheap_mode && !deinterlaced_frame->bad_frame )
      apply_chroma_filter( deinterlaced_frame->base[0], deinterlaced_frame->pitches[0],
                           frame->width, frame->height / scaler );
    skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
  }

  /* _x_post_frame_copy_up(frame, deinterlaced_frame); */
  deinterlaced_frame->free(deinterlaced_frame);
  pthread_mutex_lock (&this->lock);

  return skip;
}

static int deinterlace_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  vo_frame_t *orig_frame;
  vo_frame_t *yuy2_frame;
  int i, skip = 0, progressive = 0;
  int fields[2] = {0, 0};
  int framerate_mode;

  orig_frame = frame;
  _x_post_frame_copy_down(frame, frame->next);
  frame = frame->next;

  /* update tvtime context and method */
  pthread_mutex_lock (&this->lock);
  if( this->tvtime_changed ) {
    tvtime_reset_context(this->tvtime);

    if( this->cur_method )
      this->tvtime->curmethod = get_deinterlace_method( this->cur_method-1 );
    else
      this->tvtime->curmethod = NULL;

    port->original_port->set_property(port->original_port,
                                XINE_PARAM_VO_DEINTERLACE,
                                !this->cur_method);

    this->tvtime_changed = 0;
  }
  if( this->tvtime_last_filmmode != this->tvtime->filmmode ) {
    xine_event_t event;
    event.type = XINE_EVENT_POST_TVTIME_FILMMODE_CHANGE;
    event.stream = stream;
    event.data = (void *)&this->tvtime->filmmode;
    event.data_length = sizeof(this->tvtime->filmmode);
    xine_event_send(stream, &event);
    this->tvtime_last_filmmode = this->tvtime->filmmode;
  }
  pthread_mutex_unlock (&this->lock);

  lprintf("frame flags pf: %d rff: %d tff: %d duration: %d\n",
           frame->progressive_frame, frame->repeat_first_field,
           frame->top_field_first, frame->duration);

  /* detect special rff patterns */
  this->rff_pattern = this->rff_pattern << 1;
  this->rff_pattern |= !!frame->repeat_first_field;

  if( ((this->rff_pattern & 0xff) == 0xaa ||
      (this->rff_pattern & 0xff) == 0x55) ) {
    /*
     * special case for ntsc 3:2 pulldown (called flags or soft pulldown).
     * we know all frames are indeed progressive.
     */
    progressive = 1;
  }

  /* using frame->progressive_frame may help displaying still menus.
   * however, it is known that some rare material set it wrong.
   *
   * we also assume that repeat_first_field is progressive (it doesn't
   * make much sense to display interlaced fields out of order)
   */
  if( this->use_progressive_frame_flag &&
      (frame->repeat_first_field || frame->progressive_frame) ) {
    progressive = 1;
  }

  if( !frame->bad_frame &&
      (frame->flags & VO_INTERLACED_FLAG) &&
      this->tvtime->curmethod ) {

    frame->flags &= ~VO_INTERLACED_FLAG;

    /* convert to YUY2 if needed */
    if( frame->format == XINE_IMGFMT_YV12 && !this->cheap_mode ) {

      yuy2_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YUY2, frame->flags | VO_BOTH_FIELDS);
      _x_post_frame_copy_down(frame, yuy2_frame);

      /* the logic for deciding upsampling to use comes from:
       * http://www.hometheaterhifi.com/volume_8_2/dvd-benchmark-special-report-chroma-bug-4-2001.html
       */
      yv12_to_yuy2(frame->base[0], frame->pitches[0],
                   frame->base[1], frame->pitches[1],
                   frame->base[2], frame->pitches[2],
                   yuy2_frame->base[0], yuy2_frame->pitches[0],
                   frame->width, frame->height,
                   frame->progressive_frame || progressive );

    } else {
      yuy2_frame = frame;
      yuy2_frame->lock(yuy2_frame);
    }


    pthread_mutex_lock (&this->lock);
    /* check if frame format changed */
    for(i = 0; i < NUM_RECENT_FRAMES; i++ ) {
      if( this->recent_frame[i] &&
          (this->recent_frame[i]->width != frame->width ||
           this->recent_frame[i]->height != frame->height ||
           this->recent_frame[i]->format != yuy2_frame->format ) ) {
        this->recent_frame[i]->free(this->recent_frame[i]);
        this->recent_frame[i] = NULL;
      }
    }

    if( !this->cheap_mode ) {
      framerate_mode = this->framerate_mode;
      this->tvtime->pulldown_alg = this->pulldown;
    } else {
      framerate_mode = FRAMERATE_HALF_TFF;
      this->tvtime->pulldown_alg = PULLDOWN_NONE;
    }

    if( framerate_mode == FRAMERATE_FULL ) {
      int top_field_first = frame->top_field_first;

      /* if i understood mpeg2 specs correctly, top_field_first
       * shall be zero for field pictures and the output order
       * is the same that the fields are decoded.
       * frame->flags allow us to find the first decoded field.
       *
       * note: frame->field() is called later to switch decoded
       *       field but frame->flags do not change.
       */
      if ( (frame->flags & VO_BOTH_FIELDS) != VO_BOTH_FIELDS ) {
        top_field_first = (frame->flags & VO_TOP_FIELD) ? 1 : 0;
      }

      if ( top_field_first ) {
        fields[0] = 0;
        fields[1] = 1;
      } else {
        fields[0] = 1;
        fields[1] = 0;
      }
    } else if ( framerate_mode == FRAMERATE_HALF_TFF ) {
      fields[0] = 0;
    } else if ( framerate_mode == FRAMERATE_HALF_BFF ) {
      fields[0] = 1;
    }


    if( progressive ) {

      /* If the previous field was interlaced and this one is progressive
       * we need to run a deinterlace on the first field of this frame
       * in order to let output for the previous frames last field be
       * generated. This is only necessary for the deinterlacers that
       * delay output by one field.  This is signaled by the delaysfield
       * flag in the deinterlace method structure. The previous frames
       * duration is used in the calculation because the generated frame
       * represents the second half of the previous frame.
       */
      if (this->recent_frame[0] && !this->recent_frame[0]->progressive_frame &&
          this->tvtime->curmethod->delaysfield)
      {
	skip = deinterlace_build_output_field(
          this, port, stream,
          frame, yuy2_frame,
          fields[0], 0,
	  0,
	  (framerate_mode == FRAMERATE_FULL) ? this->recent_frame[0]->duration/2 : this->recent_frame[0]->duration,
	  0);
      }
      pthread_mutex_unlock (&this->lock);
      skip = yuy2_frame->draw(yuy2_frame, stream);
      pthread_mutex_lock (&this->lock);
      _x_post_frame_copy_up(frame, yuy2_frame);

    } else {


      /* If the previous field was progressive and we are using a
       * filter that delays it's output by one field then we need
       * to skip the first field's output. Otherwise the effective
       * display duration of the previous frame will be extended
       * by 1/2 of this frames duration when output is generated
       * using the last field of the progressive frame. */

      /* Build the output from the first field. */
      if ( !(this->recent_frame[0] && this->recent_frame[0]->progressive_frame &&
             this->tvtime->curmethod->delaysfield) ) {
        skip = deinterlace_build_output_field(
          this, port, stream,
          frame, yuy2_frame,
          fields[0], 0,
          frame->pts,
          (framerate_mode == FRAMERATE_FULL) ? frame->duration/2 : frame->duration,
          0);
      }

      if( framerate_mode == FRAMERATE_FULL ) {

        /* Build the output from the second field. */
        skip = deinterlace_build_output_field(
          this, port, stream,
          frame, yuy2_frame,
          fields[1], 1,
          0,
          frame->duration/2,
          skip);
      }
    }

    /* don't drop frames when pulldown mode is enabled. otherwise
     * pulldown detection fails (yo-yo effect has also been seen)
     */
    if( this->pulldown )
      skip = 0;

    /* store back progressive flag for frame history */
    yuy2_frame->progressive_frame = progressive;

    /* keep track of recent frames */
    i = NUM_RECENT_FRAMES-1;
    if( this->recent_frame[i] )
      this->recent_frame[i]->free(this->recent_frame[i]);
    for( ; i ; i-- )
      this->recent_frame[i] = this->recent_frame[i-1];
    if (port->stream)
      this->recent_frame[0] = yuy2_frame;
    else {
      /* do not keep this frame when no stream is connected to us,
       * otherwise, this frame might never get freed */
      yuy2_frame->free(yuy2_frame);
      this->recent_frame[0] = NULL;
    }

    pthread_mutex_unlock (&this->lock);

  } else {
    skip = frame->draw(frame, stream);
  }

  _x_post_frame_copy_up(orig_frame, frame);

  return skip;
}
