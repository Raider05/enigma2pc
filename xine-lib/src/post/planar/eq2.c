/*
 * Copyright (C) 2000-2008 the xine project
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
 * mplayer's eq2 (soft video equalizer)
 * Software equalizer (brightness, contrast, gamma, saturation)
 *
 * Hampa Hug <hampa@hampa.ch> (original LUT gamma/contrast/brightness filter)
 * Daniel Moreno <comac@comac.darktech.org> (saturation, R/G/B gamma support)
 * Richard Felker (original MMX contrast/brightness code (vf_eq.c))
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <math.h>
#include <pthread.h>


/* Per channel parameters */
typedef struct eq2_param_t {
  unsigned char lut[256];
  int           lut_clean;

  void (*adjust) (struct eq2_param_t *par, unsigned char *dst, unsigned char *src,
    unsigned w, unsigned h, unsigned dstride, unsigned sstride);

  double        c;
  double        b;
  double        g;
} eq2_param_t;

typedef struct vf_priv_s {
  eq2_param_t param[3];

  double        contrast;
  double        brightness;
  double        saturation;

  double        gamma;
  double        rgamma;
  double        ggamma;
  double        bgamma;

  unsigned      buf_w[3];
  unsigned      buf_h[3];
  unsigned char *buf[3];
} vf_eq2_t;

static
void create_lut (eq2_param_t *par)
{
  unsigned i;
  double   g, v;

  g = par->g;

  if ((g < 0.001) || (g > 1000.0)) {
    g = 1.0;
  }

  g = 1.0 / g;

  for (i = 0; i < 256; i++) {
    v = (double) i / 255.0;
    v = par->c * (v - 0.5) + 0.5 + par->b;

    if (v <= 0.0) {
      par->lut[i] = 0;
    }
    else {
      v = pow (v, g);

      if (v >= 1.0) {
        par->lut[i] = 255;
      }
      else {
        par->lut[i] = (unsigned char) (256.0 * v);
      }
    }
  }

  par->lut_clean = 1;
}


#if defined(ARCH_X86) || defined(ARCH_X86_64)
static
void affine_1d_MMX (eq2_param_t *par, unsigned char *dst, unsigned char *src,
  unsigned w, unsigned h, unsigned dstride, unsigned sstride)
{
  unsigned i;
  int      contrast, brightness;
  unsigned dstep, sstep;
  int      pel;
  short    brvec[4];
  short    contvec[4];

  contrast = (int) (par->c * 256 * 16);
  brightness = ((int) (100.0 * par->b + 100.0) * 511) / 200 - 128 - contrast / 32;

  brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
  contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;

  sstep = sstride - w;
  dstep = dstride - w;

  while (h-- > 0) {
    asm volatile (
      "movq (%5), %%mm3 \n\t"
      "movq (%6), %%mm4 \n\t"
      "pxor %%mm0, %%mm0 \n\t"
      "movl %4, %%eax\n\t"
      ASMALIGN(4)
      "1: \n\t"
      "movq (%0), %%mm1 \n\t"
      "movq (%0), %%mm2 \n\t"
      "punpcklbw %%mm0, %%mm1 \n\t"
      "punpckhbw %%mm0, %%mm2 \n\t"
      "psllw $4, %%mm1 \n\t"
      "psllw $4, %%mm2 \n\t"
      "pmulhw %%mm4, %%mm1 \n\t"
      "pmulhw %%mm4, %%mm2 \n\t"
      "paddw %%mm3, %%mm1 \n\t"
      "paddw %%mm3, %%mm2 \n\t"
      "packuswb %%mm2, %%mm1 \n\t"
      "add $8, %0 \n\t"
      "movq %%mm1, (%1) \n\t"
      "add $8, %1 \n\t"
      "decl %%eax \n\t"
      "jnz 1b \n\t"
      : "=r" (src), "=r" (dst)
      : "0" (src), "1" (dst), "r" (w >> 3), "r" (brvec), "r" (contvec)
      : "%eax"
    );

    for (i = w & 7; i > 0; i--) {
      pel = ((*src++ * contrast) >> 12) + brightness;
      if (pel & 768) {
        pel = (-pel) >> 31;
      }
      *dst++ = pel;
    }

    src += sstep;
    dst += dstep;
  }

  asm volatile ( "emms \n\t" ::: "memory" );
}
#endif

static
void apply_lut (eq2_param_t *par, unsigned char *dst, unsigned char *src,
  unsigned w, unsigned h, unsigned dstride, unsigned sstride)
{
  unsigned      i, j;
  unsigned char *lut;

  if (!par->lut_clean) {
    create_lut (par);
  }

  lut = par->lut;

  for (j = 0; j < h; j++) {
    for (i = 0; i < w; i++) {
      dst[i] = lut[src[i]];
    }

    src += sstride;
    dst += dstride;
  }
}

static
void check_values (eq2_param_t *par)
{
  /* yuck! floating point comparisons... */

  if ((par->c == 1.0) && (par->b == 0.0) && (par->g == 1.0)) {
    par->adjust = NULL;
  }
#if defined(ARCH_X86) || defined(ARCH_X86_64)
  else if (par->g == 1.0 && (xine_mm_accel() & MM_ACCEL_X86_MMX) ) {
    par->adjust = &affine_1d_MMX;
  }
#endif
  else {
    par->adjust = &apply_lut;
  }
}


static
void set_contrast (vf_eq2_t *eq2, double c)
{
  eq2->contrast = c;
  eq2->param[0].c = c;
  eq2->param[0].lut_clean = 0;
  check_values (&eq2->param[0]);
}

static
void set_brightness (vf_eq2_t *eq2, double b)
{
  eq2->brightness = b;
  eq2->param[0].b = b;
  eq2->param[0].lut_clean = 0;
  check_values (&eq2->param[0]);
}

static
void set_gamma (vf_eq2_t *eq2, double g)
{
  eq2->gamma = g;

  eq2->param[0].g = eq2->gamma * eq2->ggamma;
  eq2->param[1].g = sqrt (eq2->bgamma / eq2->ggamma);
  eq2->param[2].g = sqrt (eq2->rgamma / eq2->ggamma);

  eq2->param[0].lut_clean = 0;
  eq2->param[1].lut_clean = 0;
  eq2->param[2].lut_clean = 0;

  check_values (&eq2->param[0]);
  check_values (&eq2->param[1]);
  check_values (&eq2->param[2]);
}

static
void set_saturation (vf_eq2_t *eq2, double s)
{
  eq2->saturation = s;

  eq2->param[1].c = s;
  eq2->param[2].c = s;

  eq2->param[1].lut_clean = 0;
  eq2->param[2].lut_clean = 0;

  check_values (&eq2->param[1]);
  check_values (&eq2->param[2]);
}


/* plugin class initialization function */
void *eq2_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_eq2_s post_plugin_eq2_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct eq2_parameters_s {

  double gamma;
  double contrast;
  double brightness;
  double saturation;

  double rgamma;
  double ggamma;
  double bgamma;

} eq2_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( eq2_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, gamma, NULL, 0, 5, 0,
            "gamma" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, brightness, NULL, -1, 1, 0,
            "brightness" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, contrast, NULL, 0, 2, 0,
            "contrast" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, saturation, NULL, 0, 2, 0,
            "saturation" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, rgamma, NULL, 0, 5, 0,
            "rgamma" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, ggamma, NULL, 0, 5, 0,
            "ggamma" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, bgamma, NULL, 0, 5, 0,
            "bgamma" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_eq2_s {
  post_plugin_t post;

  /* private data */
  eq2_parameters_t   params;
  xine_post_in_t     params_input;

  vf_eq2_t           eq2;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)this_gen;
  eq2_parameters_t *param = (eq2_parameters_t *)param_gen;
  vf_eq2_t *eq2 = &this->eq2;

  pthread_mutex_lock (&this->lock);

  if( &this->params != param )
    memcpy( &this->params, param, sizeof(eq2_parameters_t) );

  eq2->rgamma = param->rgamma;
  eq2->ggamma = param->ggamma;
  eq2->bgamma = param->bgamma;

  set_gamma (eq2, param->gamma);
  set_contrast (eq2, param->contrast);
  set_brightness (eq2, param->brightness);
  set_saturation (eq2, param->saturation);

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)this_gen;
  eq2_parameters_t *param = (eq2_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(eq2_parameters_t) );

  return 1;
}

static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static char * get_help (void) {
  return _("Alternative software equalizer that uses lookup tables (very slow), "
           "allowing gamma correction in addition to simple brightness, "
           "contrast and saturation adjustment.\n"
           "Note that it uses the same MMX optimized code as 'eq' if all "
           "gamma values are 1.0.\n"
           "\n"
           "Parameters\n"
           "  gamma\n"
           "  brightness\n"
           "  contrast\n"
           "  saturation\n"
           "  rgamma (gamma for the red component)\n"
           "  ggamma (gamma for the green component)\n"
           "  bgamma (gamma for the blue component)\n"
           "\n"
           "Value ranges are 0.1 - 10 for gammas, -2 - 2 for contrast "
           "(negative values result in a negative image), -1 - 1 for "
           "brightness and 0 - 3 for saturation.\n"
           "\n"
           "* mplayer's eq2 (C) Hampa Hug, Daniel Moreno, Richard Felker\n"
           );
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
  get_help,
};


/* plugin class functions */
static post_plugin_t *eq2_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);

/* plugin instance functions */
static void           eq2_dispose(post_plugin_t *this_gen);

/* replaced video_port functions */
static int            eq2_get_property(xine_video_port_t *port_gen, int property);
static int            eq2_set_property(xine_video_port_t *port_gen, int property, int value);

/* frame intercept check */
static int            eq2_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            eq2_draw(vo_frame_t *frame, xine_stream_t *stream);


void *eq2_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

  if (!class)
    return NULL;

  class->open_plugin     = eq2_open_plugin;
  class->identifier      = "eq2";
  class->description     = N_("Software video equalizer");
  class->dispose         = default_post_class_dispose;

  return class;
}


static post_plugin_t *eq2_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_eq2_t *this = calloc(1, sizeof(post_plugin_eq2_t));
  post_in_t         *input;
  xine_post_in_t    *input_api;
  post_out_t        *output;
  post_video_port_t *port;

  if (!this || !video_target || !video_target[0]) {
    free(this);
    return NULL;
  }

  _x_post_init(&this->post, 0, 1);

  memset(&this->eq2, 0, sizeof(this->eq2));

  this->eq2.gamma = this->params.gamma = 1.0;
  this->eq2.contrast = this->params.contrast = 1.0;
  this->eq2.brightness = this->params.brightness = 0.0;
  this->eq2.saturation = this->params.saturation = 1.0;
  this->eq2.rgamma = this->params.rgamma = 1.0;
  this->eq2.ggamma = this->params.ggamma = 1.0;
  this->eq2.bgamma = this->params.bgamma = 1.0;

  pthread_mutex_init(&this->lock, NULL);

  port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
  port->new_port.get_property = eq2_get_property;
  port->new_port.set_property = eq2_set_property;
  port->intercept_frame       = eq2_intercept_frame;
  port->new_frame->draw       = eq2_draw;

  input_api       = &this->params_input;
  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;
  xine_list_push_back(this->post.input, input_api);

  input->xine_in.name     = "video";
  output->xine_out.name   = "eqd video";

  this->post.xine_post.video_input[0] = &port->new_port;

  this->post.dispose = eq2_dispose;

  set_parameters ((xine_post_t *)this, &this->params);

  return &this->post;
}

static void eq2_dispose(post_plugin_t *this_gen)
{
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    pthread_mutex_destroy(&this->lock);
    free(this);
  }
}


static int eq2_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)port->post;

  if( property == XINE_PARAM_VO_BRIGHTNESS )
    return 65535 * (this->params.brightness + 1.0) / 2.0;
  else if( property == XINE_PARAM_VO_CONTRAST )
    return 65535 * (this->params.contrast) / 2.0;
  else if( property == XINE_PARAM_VO_SATURATION )
    return 65535 * (this->params.saturation) / 2.0;
  else
    return port->original_port->get_property(port->original_port, property);
}

static int eq2_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)port->post;

  if( property == XINE_PARAM_VO_BRIGHTNESS ) {
    this->params.brightness = (2.0 * value / 65535) - 1.0;
    set_parameters ((xine_post_t *)this, &this->params);
    return value;
  } else if( property == XINE_PARAM_VO_CONTRAST ) {
    this->params.contrast = (2.0 * value / 65535);
    set_parameters ((xine_post_t *)this, &this->params);
    return value;
  } else if( property == XINE_PARAM_VO_SATURATION ) {
    this->params.saturation = (2.0 * value / 65535);
    set_parameters ((xine_post_t *)this, &this->params);
    return value;
  } else
    return port->original_port->set_property(port->original_port, property, value);
}


static int eq2_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
  return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int eq2_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_eq2_t *this = (post_plugin_eq2_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  vf_eq2_t   *eq2 = &this->eq2;
  int skip;
  int i;

  if( !frame->bad_frame &&
      (eq2->param[0].adjust || eq2->param[1].adjust || eq2->param[2].adjust) ) {

    /* convert to YV12 if needed */
    if( frame->format != XINE_IMGFMT_YV12 ) {

      yv12_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);

      _x_post_frame_copy_down(frame, yv12_frame);

      yuy2_to_yv12(frame->base[0], frame->pitches[0],
                   yv12_frame->base[0], yv12_frame->pitches[0],
                   yv12_frame->base[1], yv12_frame->pitches[1],
                   yv12_frame->base[2], yv12_frame->pitches[2],
                   frame->width, frame->height);

    } else {
      yv12_frame = frame;
      yv12_frame->lock(yv12_frame);
    }

    out_frame = port->original_port->get_frame(port->original_port,
      frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);

    _x_post_frame_copy_down(frame, out_frame);

    pthread_mutex_lock (&this->lock);

    for (i = 0; i < 3; i++) {
      int height, width;
      height = (i==0) ? frame->height : frame->height/2;
      width = (i==0) ? frame->width : frame->width/2;
      if (eq2->param[i].adjust != NULL) {
        eq2->param[i].adjust (&eq2->param[i], out_frame->base[i], yv12_frame->base[i],
          width, height, out_frame->pitches[i], yv12_frame->pitches[i]);
      }
      else {
        xine_fast_memcpy(out_frame->base[i],yv12_frame->base[i],
                         yv12_frame->pitches[i] * height);
      }
    }

    pthread_mutex_unlock (&this->lock);

    skip = out_frame->draw(out_frame, stream);

    _x_post_frame_copy_up(frame, out_frame);

    out_frame->free(out_frame);
    yv12_frame->free(yv12_frame);

  } else {
    _x_post_frame_copy_down(frame, frame->next);
    skip = frame->next->draw(frame->next, stream);
    _x_post_frame_copy_up(frame, frame->next);
  }

  return skip;
}
