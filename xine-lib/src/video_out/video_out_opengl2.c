/*
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 * Copyright (C) 2012-2014 the xine project
 * Copyright (C) 2012 Christophe Thommeret <hftom@free.fr>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * video_out_opengl2.c, a video output plugin using opengl 2.0
 *
 *
 */

/* #define LOG */
#define LOG_MODULE "video_out_opengl2"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <xine.h>
#include <xine/video_out.h>
#include <xine/vo_scale.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>



typedef int (*GLXSWAPINTERVALSGI) ( int );

typedef struct {
  vo_frame_t         vo_frame;
  int                width, height, format, flags;
  double             ratio;
} opengl2_frame_t;



typedef struct {
  int       ovl_w, ovl_h;
  int       ovl_x, ovl_y;
  
  GLuint    tex;
  int       tex_w, tex_h;
  
  int       unscaled;
  int       vid_scale;

  int       extent_width;
  int       extent_height;
} opengl2_overlay_t;



typedef struct {
  int    compiled;
  GLuint shader;
  GLuint program;
} opengl2_program_t;



typedef struct {
  GLuint y, u, v;
  GLuint yuv;
  int width;
  int height;
} opengl2_yuvtex_t;



typedef struct {

  vo_driver_t        vo_driver;
  vo_scale_t         sc;

  Display           *display;
  int                screen;
  Drawable           drawable;
  GLXContext         context;

  int                texture_float;
  opengl2_program_t  yuv420_program;
  opengl2_program_t  yuv422_program;
  opengl2_yuvtex_t   yuvtex;
  GLuint             videoPBO;
  GLuint             overlayPBO;
  GLuint             fbo;
  GLuint             videoTex, videoTex2;
  GLXSWAPINTERVALSGI mglXSwapInterval;

  int                ovl_changed;
  int                ovl_vid_scale;
  int                num_ovls;
  opengl2_overlay_t  overlays[XINE_VORAW_MAX_OVL];

  opengl2_program_t  sharpness_program;
  float              csc_matrix[3 * 4];
  int                color_standard;
  int                update_csc;
  int                saturation;
  int                contrast;
  int                brightness;
  int                hue;
  int                sharpness;

  opengl2_program_t  bicubic_pass1_program;
  opengl2_program_t  bicubic_pass2_program;
  GLuint             bicubic_lut_texture;
  GLuint             bicubic_pass1_texture;
  int                bicubic_pass1_texture_width;
  int                bicubic_pass1_texture_height;
  GLuint             bicubic_fbo;
  int                scale_bicubic;
  
  pthread_mutex_t    drawable_lock;
  uint32_t           display_width;
  uint32_t           display_height;

  config_values_t   *config;

  uint32_t          capabilities;
  xine_t            *xine;

  int		            zoom_x;
  int		            zoom_y;

  int                cm_state;
} opengl2_driver_t;

/* import common color matrix stuff */
#define CM_HAVE_YCGCO_SUPPORT 1
#define CM_DRIVER_T opengl2_driver_t
#include "color_matrix.c"


typedef struct {
  video_driver_class_t driver_class;
  GLXContext           ctx;
  xine_t              *xine;
} opengl2_class_t;



static const char *bicubic_pass1_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex, lut;\n"
"uniform float spline;\n"
"void main() {\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    vec2 TexCoord = vec2( floor( coord.x - 0.5 ) + 0.5, coord.y );\n"
"    vec4 wlut = texture2DRect( lut, vec2( ( coord.x - TexCoord.x ) * 1000.0, spline ) );\n"
"    vec4 sum  = texture2DRect( tex, TexCoord + vec2( -1.0, 0.0) ) * wlut[0];\n"
"         sum += texture2DRect( tex, TexCoord )                    * wlut[1];\n"
"         sum += texture2DRect( tex, TexCoord + vec2(  1.0, 0.0) ) * wlut[2];\n"
"         sum += texture2DRect( tex, TexCoord + vec2(  2.0, 0.0) ) * wlut[3];\n"
"    gl_FragColor = sum;\n"
"}\n";



static const char *bicubic_pass2_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex, lut;\n"
"uniform float spline;\n"
"void main() {\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    vec2 TexCoord = vec2( coord.x, floor( coord.y - 0.5 ) + 0.5 );\n"
"    vec4 wlut = texture2DRect( lut, vec2( ( coord.y - TexCoord.y ) * 1000.0, spline ) );\n"
"    vec4 sum  = texture2DRect( tex, TexCoord + vec2( 0.0, -1.0 ) ) * wlut[0];\n"
"         sum += texture2DRect( tex, TexCoord )                     * wlut[1];\n"
"         sum += texture2DRect( tex, TexCoord + vec2( 0.0,  1.0 ) ) * wlut[2];\n"
"         sum += texture2DRect( tex, TexCoord + vec2( 0.0,  2.0 ) ) * wlut[3];\n"
"    gl_FragColor = sum;\n"
"}\n";



#define PI 3.14159265359
#define LUTWIDTH 1000
#define N_SPLINES 2
#define CATMULLROM_SPLINE   0
#define COS_SPLINE          1

static float compute_cos_spline( float x )
{
    if ( x < 0.0 )
        x = -x;
    return 0.5 * cos( PI * x / 2.0 ) + 0.5;
}

static float compute_catmullrom_spline( float x )
{
    if ( x < 0.0 )
        x = -x;
    if ( x < 1.0 )
        return ((9.0 * (x * x * x)) - (15.0 * (x * x)) + 6.0) / 6.0;
    if ( x <= 2.0 )
        return ((-3.0 * (x * x * x)) + (15.0 * (x * x)) - (24.0 * x) + 12.0) / 6.0;
    return 0.0;
}

static int create_lut_texture( opengl2_driver_t *that )
{
  int i = 0;
  float *lut = calloc( sizeof(float) * LUTWIDTH * 4 * N_SPLINES, 1 );
  if ( !lut )
    return 0;

  while ( i < LUTWIDTH ) {
    float t, v1, v2, v3, v4, coefsum;
    t = (float)i / (float)LUTWIDTH;

    v1 = compute_catmullrom_spline( t + 1.0 ); coefsum  = v1;
    v2 = compute_catmullrom_spline( t );       coefsum += v2;
    v3 = compute_catmullrom_spline( t - 1.0 ); coefsum += v3;
    v4 = compute_catmullrom_spline( t - 2.0 ); coefsum += v4;
    lut[i * 4]       = v1 / coefsum;
    lut[(i * 4) + 1] = v2 / coefsum;
    lut[(i * 4) + 2] = v3 / coefsum;
    lut[(i * 4) + 3] = v4 / coefsum;

    lut[(i * 4) + (LUTWIDTH * 4)] = compute_cos_spline( t + 1.0 );
    lut[(i * 4) + (LUTWIDTH * 4) + 1] = compute_cos_spline( t );
    lut[(i * 4) + (LUTWIDTH * 4) + 2] = compute_cos_spline( t - 1.0 );
    lut[(i * 4) + (LUTWIDTH * 4) + 3] = compute_cos_spline( t - 2.0 );

    ++i;
  }

  that->bicubic_lut_texture = 0;
  glGenTextures( 1, &that->bicubic_lut_texture );
  if ( !that->bicubic_lut_texture ) {
    free( lut );
    return 0;
  }

  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA32F, LUTWIDTH, N_SPLINES, 0, GL_RGBA, GL_FLOAT, lut );
  free( lut );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
  return 1;
}



static const char *blur_sharpen_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;\n"
"uniform float value;\n"
"void main() {\n"
"  vec2 pos = gl_TexCoord[0].xy;\n"
"  vec4 c1;\n"
"  float K;\n"
"  if ( value < 0.0 )\n"
"    K = value / 8.0;\n"
"  else\n"
"    K = value / 4.0;\n"
"  c1 = texture2DRect( tex, pos ) * (1.0 + (8.0 * K));\n"
"  c1 -= texture2DRect( tex, pos + vec2( 1.0, 0.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( -1.0, 0.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( 0.0, 1.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( 0.0, -1.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( 1.0, 1.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( -1.0, 1.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( -1.0, -1.0 ) ) * K;\n"
"  c1 -= texture2DRect( tex, pos + vec2( 1.0, -1.0 ) ) * K;\n"
"  gl_FragColor = c1 ;\n"
"}\n";



static const char *yuv420_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect texY, texU, texV;\n"
"uniform vec4 r_coefs, g_coefs, b_coefs;\n"
"void main(void) {\n"
"    vec3 rgb;\n"
"    vec4 yuv;\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    yuv.r = texture2DRect(texY, vec2(coord.x, coord.y)).r;\n"
"    yuv.g = texture2DRect(texU, vec2(coord.x/2.0, coord.y/2.0)).r;\n"
"    yuv.b = texture2DRect(texV, vec2(coord.x/2.0, coord.y/2.0)).r;\n"
"    yuv.a = 1.0;\n"
"    rgb.r = dot( yuv, r_coefs );\n"
"    rgb.g = dot( yuv, g_coefs );\n"
"    rgb.b = dot( yuv, b_coefs );\n"
"    gl_FragColor = vec4(rgb, 1.0);\n"
"}\n";



static const char *yuv422_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect texYUV;\n"
"uniform vec4 r_coefs, g_coefs, b_coefs;\n"
"void main(void) {\n"
"    vec3 rgb;\n"
"    vec4 yuv;\n"
"    vec4 coord = gl_TexCoord[0].xyxx;\n"
"    coord.z -= step(1.0, mod(coord.x, 2.0));\n"
"    coord.w = coord.z + 1.0;\n"
"    yuv.r = texture2DRect(texYUV, coord.xy).r;\n"
"    yuv.g = texture2DRect(texYUV, coord.zy).a;\n"
"    yuv.b = texture2DRect(texYUV, coord.wy).a;\n"
"    yuv.a = 1.0;\n"
"    rgb.r = dot( yuv, r_coefs );\n"
"    rgb.g = dot( yuv, g_coefs );\n"
"    rgb.b = dot( yuv, b_coefs );\n"
"    gl_FragColor = vec4(rgb, 1.0);\n"
"}\n";



static void load_csc_matrix( GLuint prog, float *cf )
{
    glUniform4f( glGetUniformLocationARB( prog, "r_coefs" ), cf[0], cf[1], cf[2], cf[3] );
    glUniform4f( glGetUniformLocationARB( prog, "g_coefs" ), cf[4], cf[5], cf[6], cf[7] );
    glUniform4f( glGetUniformLocationARB( prog, "b_coefs" ), cf[8], cf[9], cf[10], cf[11] );
}



static int opengl2_build_program( opengl2_program_t *prog, const char **source, const char *name )
{
  fprintf(stderr, "vo_opengl2 : compiling shader %s\n", name);
  if ( !(prog->shader = glCreateShader( GL_FRAGMENT_SHADER )) )
    return 0;
  if ( !(prog->program = glCreateProgram()) )
    return 0;

  glShaderSource( prog->shader, 1, source, NULL );
  glCompileShader( prog->shader );

  GLint length;
  GLchar *log;
  glGetShaderiv( prog->shader, GL_INFO_LOG_LENGTH, &length );
  log = (GLchar*)malloc( length );
  if ( !log )
    return 0;

  glGetShaderInfoLog( prog->shader, length, &length, log );
  if ( length ) {
    fprintf( stderr, "Shader %s Compilation Log:\n", name );
    fprintf( stderr, "%s", log );
  }
  free( log );

  glAttachShader( prog->program, prog->shader );
  glLinkProgram( prog->program );
  glGetProgramiv( prog->program, GL_INFO_LOG_LENGTH, &length );
  log = (GLchar*)malloc( length );
  if ( !log )
    return 0;

  glGetProgramInfoLog( prog->program, length, &length, log );
  if ( length ) {
    fprintf( stderr, "Linking Log:\n" );
    fprintf( stderr, "%s", log );
  }
  free( log );

  prog->compiled = 1;

  return 1;
}



static void opengl2_delete_program( opengl2_program_t *prog )
{
  glDeleteProgram( prog->program );
  glDeleteShader( prog->shader );
}



static int opengl2_check_textures_size( opengl2_driver_t *this_gen, int w, int h )
{
  opengl2_driver_t *this = this_gen;
  opengl2_yuvtex_t *ytex = &this->yuvtex;

  w = (w + 15) & ~15;
  if ( (w == ytex->width) && (h == ytex->height) )
    return 1;

  if ( ytex->y )
    glDeleteTextures( 1, &ytex->y );
  if ( ytex->u )
    glDeleteTextures( 1, &ytex->u );
  if ( ytex->v )
    glDeleteTextures( 1, &ytex->v );
  if ( ytex->yuv )
    glDeleteTextures( 1, &ytex->yuv );
  if ( this->videoTex )
    glDeleteTextures( 1, &this->videoTex );
  if ( this->videoTex2 )
    glDeleteTextures( 1, &this->videoTex2 );

  if ( !this->videoPBO ) {
    glGenBuffers( 1, &this->videoPBO );
    if ( !this->videoPBO )
      return 0;
  }

  if ( !this->fbo ) {
    glGenFramebuffers( 1, &this->fbo );
    if ( !this->fbo )
      return 0;
  }

  glGenTextures( 1, &this->videoTex );
  if ( !this->videoTex )
    return 0;

  glGenTextures( 1, &this->videoTex2 );
  if ( !this->videoTex2 )
    return 0;

  glGenTextures( 1, &ytex->y );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, ytex->y );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glGenTextures( 1, &ytex->u );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, ytex->u );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, w/2, h/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glGenTextures( 1, &ytex->v );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, ytex->v );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, w/2, h/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glGenTextures( 1, &ytex->yuv );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, ytex->yuv );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA, w, h, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

  ytex->width = w;
  ytex->height = h;

  glGenTextures( 1, &this->videoTex );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, this->videoTex );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

  glGenTextures( 1, &this->videoTex2 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, this->videoTex2 );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

  glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, this->videoPBO );
  glBufferData( GL_PIXEL_UNPACK_BUFFER_ARB, w * h * 2, NULL, GL_STREAM_DRAW );
  glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );

  glBindFramebuffer( GL_FRAMEBUFFER, this->fbo );
  glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, this->videoTex, 0 );
  glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_RECTANGLE_ARB, this->videoTex2, 0 );
  glBindFramebuffer( GL_FRAMEBUFFER, 0 );

  return 1;
}


static void opengl2_upload_overlay(opengl2_driver_t *this, opengl2_overlay_t *o, vo_overlay_t *overlay)
{
  if ( o->tex && ((o->tex_w != o->ovl_w) || (o->tex_h != o->ovl_h)) ) {
    glDeleteTextures( 1, &o->tex );
    o->tex = 0;
  }

  if ( !o->tex ) {
    glGenTextures( 1, &o->tex );
    o->tex_w = o->ovl_w;
    o->tex_h = o->ovl_h;
  }

  if ( overlay->rle && !this->overlayPBO ) {
    glGenBuffers( 1, &this->overlayPBO );
    if ( !this->overlayPBO ) {
      fprintf(stderr, "*** overlay PBO failed\n");
      return;
    }
  }

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

  if (overlay->argb_layer) {
    pthread_mutex_lock(&overlay->argb_layer->mutex); /* buffer can be changed or freed while unlocked */

    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, o->tex_w, o->tex_h, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                  overlay->argb_layer->buffer );

    pthread_mutex_unlock(&overlay->argb_layer->mutex);

  } else {
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, this->overlayPBO );
    glBufferData( GL_PIXEL_UNPACK_BUFFER_ARB, o->tex_w * o->tex_h * 4, NULL, GL_STREAM_DRAW );

    void *rgba = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    _x_overlay_to_argb32(overlay, rgba, o->tex_w, "RGBA");

    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, o->tex_w, o->tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );
  }

  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
}

static void opengl2_overlay_begin (vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  //fprintf(stderr, "opengl2_overlay_begin\n");
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  if ( changed ) {
    this->ovl_changed = 1;

    if ( !glXMakeCurrent( this->display, this->drawable, this->context ) ) {
      xprintf( this->xine, XINE_VERBOSITY_LOG, "video_out_opengl2: display unavailable for rendering\n" );
      return;
    }
  }
}


static void opengl2_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  if ( !this->ovl_changed || this->ovl_changed>XINE_VORAW_MAX_OVL )
    return;

  if ( overlay->width<=0 || overlay->height<=0 )
    return;

  opengl2_overlay_t *ovl = &this->overlays[this->ovl_changed-1];

  ovl->ovl_w = overlay->width;
  ovl->ovl_h = overlay->height;
  ovl->ovl_x = overlay->x;
  ovl->ovl_y = overlay->y;
  ovl->unscaled = overlay->unscaled;
  ovl->extent_width = overlay->extent_width;
  ovl->extent_height = overlay->extent_height;
  if ( overlay->extent_width == -1 )
    ovl->vid_scale = 1;
  else
    ovl->vid_scale = 0;

  if (overlay->rle) {
    if (!overlay->rgb_clut || !overlay->hili_rgb_clut) {
      _x_overlay_clut_yuv2rgb(overlay, this->color_standard);
    }
  }

  if (overlay->argb_layer || overlay->rle) {
    opengl2_upload_overlay(this, ovl, overlay);
    ++this->ovl_changed;
  }
}


static void opengl2_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img)
{
  //fprintf(stderr, "opengl2_overlay_end\n");
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;
  unsigned i;

  if ( !this->ovl_changed )
    return;

  this->num_ovls = this->ovl_changed - 1;

  /* free unused textures and buffers */
  for ( i = this->num_ovls; i < XINE_VORAW_MAX_OVL && this->overlays[i].tex; ++i ) {
    this->overlays[i].ovl_w = 0;
    this->overlays[i].ovl_h = 0;
    glDeleteTextures( 1, &this->overlays[i].tex );
    this->overlays[i].tex = 0;
  }

  glXMakeCurrent( this->display, None, NULL );
}




static void opengl2_frame_proc_slice( vo_frame_t *vo_img, uint8_t **src )
{
  vo_img->proc_called = 1;
}



static void opengl2_frame_field( vo_frame_t *vo_img, int which_field )
{
}



static void opengl2_frame_dispose( vo_frame_t *vo_img )
{
  opengl2_frame_t  *frame = (opengl2_frame_t *) vo_img ;

  av_free (frame->vo_frame.base[0]);
  free (frame);
}



static vo_frame_t *opengl2_alloc_frame( vo_driver_t *this_gen )
{
  opengl2_frame_t  *frame;

  frame = (opengl2_frame_t *) calloc(1, sizeof(opengl2_frame_t));

  if (!frame)
    return NULL;

  frame->vo_frame.base[0] = frame->vo_frame.base[1] = frame->vo_frame.base[2] = NULL;
  frame->width = frame->height = frame->format = frame->flags = 0;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_slice = opengl2_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = opengl2_frame_field;
  frame->vo_frame.dispose    = opengl2_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  return (vo_frame_t *) frame;
}



static void opengl2_update_frame_format( vo_driver_t *this_gen, vo_frame_t *frame_gen,
      uint32_t width, uint32_t height, double ratio, int format, int flags )
{
  opengl2_frame_t *frame = (opengl2_frame_t *) frame_gen;

  /* Check frame size and format and reallocate if necessary */
  if ( (frame->width != width) || (frame->height != height) || (frame->format != format) || (frame->flags  != flags) ) {

    /* (re-) allocate render space */
    av_freep (&frame->vo_frame.base[0]);
    frame->vo_frame.base[1] = NULL;
    frame->vo_frame.base[2] = NULL;

    if (format == XINE_IMGFMT_YV12) {
      int w = (width + 15) & ~15;
      int ysize = w * height;
      int uvsize = (w >> 1) * ((height + 1) >> 1);
      frame->vo_frame.pitches[0] = w;
      frame->vo_frame.pitches[1] = w >> 1;
      frame->vo_frame.pitches[2] = w >> 1;
      frame->vo_frame.base[0] = av_malloc (ysize + 2 * uvsize);
      if (!frame->vo_frame.base[0]) {
        frame->width = 0;
        frame->vo_frame.width = 0; /* tell vo_get_frame () to retry later */
        return;
      }
      memset (frame->vo_frame.base[0], 0, ysize);
      frame->vo_frame.base[1] = frame->vo_frame.base[0] + ysize;
      memset (frame->vo_frame.base[1], 128, 2 * uvsize);
      frame->vo_frame.base[2] = frame->vo_frame.base[1] + uvsize;
    } else if (format == XINE_IMGFMT_YUY2){
      frame->vo_frame.pitches[0] = ((width + 15) & ~15) << 1;
      frame->vo_frame.base[0] = av_malloc (frame->vo_frame.pitches[0] * height);
      if (frame->vo_frame.base[0]) {
        const union {uint8_t bytes[4]; uint32_t word;} black = {{0, 128, 0, 128}};
        uint32_t *q = (uint32_t *)frame->vo_frame.base[0];
        int i;
        for (i = frame->vo_frame.pitches[0] * height / 4; i > 0; i--)
          *q++ = black.word;
      } else {
        frame->width = 0;
        frame->vo_frame.width = 0; /* tell vo_get_frame () to retry later */
        return;
      }
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->flags = flags;
  }

  frame->ratio = ratio;
}



static int opengl2_redraw_needed( vo_driver_t *this_gen )
{
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  _x_vo_scale_compute_ideal_size( &this->sc );
  if ( _x_vo_scale_redraw_needed( &this->sc ) ) {
    _x_vo_scale_compute_output_size( &this->sc );
    return 1;
  }
  return this->update_csc;
}



static void opengl2_update_csc_matrix (opengl2_driver_t *that, opengl2_frame_t *frame) {
  int color_standard;

  color_standard = cm_from_frame (&frame->vo_frame);

  if ( that->update_csc || that->color_standard != color_standard ) {
    float hue = (float)that->hue * 3.14159265359 / 128.0;
    float saturation = (float)that->saturation / 128.0;
    float contrast = (float)that->contrast / 128.0;
    float brightness = that->brightness;
    float uvcos = saturation * cos( hue );
    float uvsin = saturation * sin( hue );
    int i;

    if ((color_standard >> 1) == 8) {
      /* YCgCo. This is really quite simple. */
      uvsin *= contrast;
      uvcos *= contrast;
      /* csc_matrix[rgb][yuv1] */
      that->csc_matrix[1] = -1.0 * uvcos - 1.0 * uvsin;
      that->csc_matrix[2] =  1.0 * uvcos - 1.0 * uvsin;
      that->csc_matrix[5] =  1.0 * uvcos;
      that->csc_matrix[6] =                1.0 * uvsin;
      that->csc_matrix[9] = -1.0 * uvcos + 1.0 * uvsin;
      that->csc_matrix[10] = -1.0 * uvcos - 1.0 * uvsin;
      for (i = 0; i < 12; i += 4) {
        that->csc_matrix[i] = contrast;
        that->csc_matrix[i + 3] = (brightness * contrast
          - 128.0 * (that->csc_matrix[i + 1] + that->csc_matrix[i + 2])) / 255.0;
      }
    } else {
      /* YCbCr */
      float kb, kr;
      float vr, vg, ug, ub;
      float ygain, yoffset;

      switch (color_standard >> 1) {
        case 1:  kb = 0.0722; kr = 0.2126; break; /* ITU-R 709 */
        case 4:  kb = 0.1100; kr = 0.3000; break; /* FCC */
        case 7:  kb = 0.0870; kr = 0.2120; break; /* SMPTE 240 */
        default: kb = 0.1140; kr = 0.2990;        /* ITU-R 601 */
      }
      vr = 2.0 * (1.0 - kr);
      vg = -2.0 * kr * (1.0 - kr) / (1.0 - kb - kr);
      ug = -2.0 * kb * (1.0 - kb) / (1.0 - kb - kr);
      ub = 2.0 * (1.0 - kb);

      if (color_standard & 1) {
        /* fullrange mode */
        yoffset = brightness;
        ygain = contrast;
        uvcos *= contrast * 255.0 / 254.0;
        uvsin *= contrast * 255.0 / 254.0;
      } else {
        /* mpeg range */
        yoffset = brightness - 16.0;
        ygain = contrast * 255.0 / 219.0;
        uvcos *= contrast * 255.0 / 224.0;
        uvsin *= contrast * 255.0 / 224.0;
      }

      /* csc_matrix[rgb][yuv1] */
      that->csc_matrix[1] = -uvsin * vr;
      that->csc_matrix[2] = uvcos * vr;
      that->csc_matrix[5] = uvcos * ug - uvsin * vg;
      that->csc_matrix[6] = uvcos * vg + uvsin * ug;
      that->csc_matrix[9] = uvcos * ub;
      that->csc_matrix[10] = uvsin * ub;
      for (i = 0; i < 12; i += 4) {
        that->csc_matrix[i] = ygain;
        that->csc_matrix[i + 3] = (yoffset * ygain
          - 128.0 * (that->csc_matrix[i + 1] + that->csc_matrix[i + 2])) / 255.0;
      }
    }

    that->color_standard = color_standard;
    that->update_csc = 0;

    xprintf (that->xine, XINE_VERBOSITY_LOG,"video_out_opengl2: b %d c %d s %d h %d [%s]\n",
      that->brightness, that->contrast, that->saturation, that->hue, cm_names[color_standard]);
  }
}



static void opengl2_update_overlays( opengl2_driver_t *that )
{
  int i, vid_scale=0, cancel_vid_scale=0;
  opengl2_overlay_t *o;

  if ( that->ovl_changed ) {
    that->ovl_vid_scale = 0;
    for ( i=0; i<that->num_ovls; ++i ) {
      o = &that->overlays[i];
      
      /* handle DVB subs scaling, e.g. 720x576->1920x1080 */
      if ( o->vid_scale ) {
        vid_scale = 1;
        if ( o->ovl_w > 720 || o->ovl_h > 576 )
          cancel_vid_scale = 1;
      }
    }
  }

  if ( that->ovl_changed && vid_scale && !cancel_vid_scale )
    that->ovl_vid_scale = 1;

  that->ovl_changed = 0;
}



static void opengl2_draw_scaled_overlays( opengl2_driver_t *that, opengl2_frame_t *frame )
{
  int i, ox, oy, ow, oh;
  opengl2_overlay_t *o;

  glEnable( GL_BLEND );
  for ( i=0; i<that->num_ovls; ++i ) {
    o = &that->overlays[i];
    if ( o->unscaled )
      continue;
    /* scaled overlays with known extent:
       draw overlay over scaled video frame -> more sharpness in overlay */
    if (o->extent_width > 0 && o->extent_height > 0)
      continue;
    ox = o->ovl_x; oy = o->ovl_y;
    ow = o->ovl_w; oh = o->ovl_h;
    if (o->extent_width > 0 && o->extent_height > 0) {
      float fx = frame->width / (float)o->extent_width, fy = frame->height / (float)o->extent_height;
      ox *= fx; oy *= fy;
      ow *= fx; oh *= fy;
    } else if ( o->vid_scale && that->ovl_vid_scale ) {
      float fx = frame->width / 720.0, fy = frame->height / 576.0;
      ox *= fx; oy *= fy;
      ow *= fx; oh *= fy;
    }
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

    glBegin( GL_QUADS );
      glTexCoord2f( 0, 0 );                  glVertex3f( ox, oy, 0.);
      glTexCoord2f( 0, o->tex_h );           glVertex3f( ox, oy + oh, 0.);
      glTexCoord2f( o->tex_w, o->tex_h );    glVertex3f( ox + ow, oy + oh, 0.);
      glTexCoord2f( o->tex_w, 0 );           glVertex3f( ox + ow, oy, 0.);
    glEnd();
  }
  glDisable( GL_BLEND );
}



static void opengl2_draw_unscaled_overlays( opengl2_driver_t *that )
{
  int i, ox, oy, ow, oh;
  opengl2_overlay_t *o;
  
  glEnable( GL_BLEND );
  for ( i=0; i<that->num_ovls; ++i ) {
    o = &that->overlays[i];
    if ( !o->unscaled && (o->extent_width <= 0 || o->extent_height <= 0))
      continue;
    ox = o->ovl_x; oy = o->ovl_y;
    ow = o->ovl_w; oh = o->ovl_h;
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

    if (!o->unscaled) {
      float fx = that->sc.gui_width / (float)o->extent_width, fy = that->sc.gui_height / (float)o->extent_height;
      /* scale to output */
      ox = fx * ox; oy = fy * oy;
      ow = fx * ow; oh = fy * oh;
    }

    glBegin( GL_QUADS );
      glTexCoord2f( 0, 0 );                  glVertex3f( ox, oy, 0.);
      glTexCoord2f( 0, o->tex_h );           glVertex3f( ox, oy + oh, 0.);
      glTexCoord2f( o->tex_w, o->tex_h );    glVertex3f( ox + ow, oy + oh, 0.);
      glTexCoord2f( o->tex_w, 0 );           glVertex3f( ox + ow, oy, 0.);
    glEnd();
  }
  glDisable( GL_BLEND );
}



static GLuint opengl2_swap_textures( opengl2_driver_t *that, GLuint current_dest )
{
  if ( current_dest == that->videoTex ) {
    glDrawBuffer( GL_COLOR_ATTACHMENT1 );
    return that->videoTex2;
  }

  glDrawBuffer( GL_COLOR_ATTACHMENT0 );
  return that->videoTex;
}



static GLuint opengl2_sharpness( opengl2_driver_t *that, opengl2_frame_t *frame, GLuint video_texture )
{
  GLuint ret = video_texture;
  
  if ( !that->sharpness_program.compiled ) {
    if ( !opengl2_build_program( &that->sharpness_program, &blur_sharpen_frag, "blur_sharpen_frag" ) )
      return ret;
  }

  ret = opengl2_swap_textures( that, video_texture );
  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );

  float value = that->sharpness / 100.0 * frame->width / 1920.0;

  glUseProgram( that->sharpness_program.program );
  glUniform1i( glGetUniformLocationARB( that->sharpness_program.program, "tex" ), 0 );
  glUniform1f( glGetUniformLocationARB( that->sharpness_program.program, "value" ), value );
  //fprintf(stderr, "vo_opengl2 : sharpness = %f\n", value);

  glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 );                           glVertex3f( 0, 0, 0.);
    glTexCoord2f( 0, frame->height );               glVertex3f( 0, frame->height, 0.);
    glTexCoord2f( frame->width, frame->height );    glVertex3f( frame->width, frame->height, 0.);
    glTexCoord2f( frame->width, 0 );                glVertex3f( frame->width, 0, 0.);
  glEnd();

  glUseProgram( 0 );

  return ret;
}



static int opengl2_draw_video_bicubic( opengl2_driver_t *that, int guiw, int guih, GLfloat u, GLfloat v, GLfloat u1, GLfloat v1,
    GLfloat x, GLfloat y, GLfloat x1, GLfloat y1, GLuint video_texture )
{
  if ( !that->bicubic_lut_texture ) {
    if ( !create_lut_texture( that ) )
      return 0;
  }

  if ( !that->bicubic_pass1_program.compiled && !opengl2_build_program( &that->bicubic_pass1_program, &bicubic_pass1_frag, "bicubic_pass1_frag" ) )
    return 0;
  if ( !that->bicubic_pass2_program.compiled && !opengl2_build_program( &that->bicubic_pass2_program, &bicubic_pass2_frag, "bicubic_pass2_frag" ) )
    return 0;
  if ( !that->bicubic_fbo ) {
    glGenFramebuffers( 1, &that->bicubic_fbo );
    if ( !that->bicubic_fbo )
      return 0;
  }
  if ( (that->bicubic_pass1_texture_width != (x1 - x)) || (that->bicubic_pass1_texture_height != (v1 - v) ) ) {
    if ( that->bicubic_pass1_texture )
      glDeleteTextures( 1, &that->bicubic_pass1_texture );
    glGenTextures( 1, &that->bicubic_pass1_texture );
    if ( !that->bicubic_pass1_texture )
      return 0;
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, x1 - x, v1 - v, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
    glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
    that->bicubic_pass1_texture_width = x1 - x;
    that->bicubic_pass1_texture_height = v1 - v;
  }
  glBindFramebuffer( GL_FRAMEBUFFER, that->bicubic_fbo );
  glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture, 0 );

  glViewport( 0, 0, x1 - x, v1 - v );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, x1 - x, 0.0, v1 - v, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glActiveTexture( GL_TEXTURE1 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glUseProgram( that->bicubic_pass1_program.program );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass1_program.program, "tex" ), 0 );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass1_program.program, "lut" ), 1 );
  glUniform1f( glGetUniformLocationARB( that->bicubic_pass1_program.program, "spline" ), CATMULLROM_SPLINE );

  glBegin( GL_QUADS );
    glTexCoord2f( u, v );    glVertex3f( 0.0, 0.0, 0.);
    glTexCoord2f( u, v1 );   glVertex3f( 0.0, v1 - v, 0.);
    glTexCoord2f( u1, v1 );  glVertex3f( x1 - x, v1 - v, 0.);
    glTexCoord2f( u1, v );   glVertex3f( x1 - x, 0.0, 0.);
  glEnd();

  glActiveTexture( GL_TEXTURE0 );
  glBindFramebuffer( GL_FRAMEBUFFER, 0 );

  glViewport( 0, 0, guiw, guih );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, guiw, guih, 0.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture );
  glActiveTexture( GL_TEXTURE1 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glUseProgram( that->bicubic_pass2_program.program );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass2_program.program, "tex" ), 0 );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass2_program.program, "lut" ), 1 );
  glUniform1f( glGetUniformLocationARB( that->bicubic_pass2_program.program, "spline" ), CATMULLROM_SPLINE );

  glBegin( GL_QUADS );
    glTexCoord2f( 0.0, 0.0 );        glVertex3f( x, y, 0.);
    glTexCoord2f( 0.0, v1 - v );     glVertex3f( x, y1, 0.);
    glTexCoord2f( x1 - x, v1 - v );  glVertex3f( x1, y1, 0.);
    glTexCoord2f( x1 - x, 0.0 );     glVertex3f( x1, y, 0.);
  glEnd();

  glUseProgram( 0 );

  return 1;
}



static int opengl2_draw_video_cubic_x( opengl2_driver_t *that, int guiw, int guih, GLfloat u, GLfloat v, GLfloat u1, GLfloat v1,
    GLfloat x, GLfloat y, GLfloat x1, GLfloat y1, GLuint video_texture )
{
  if ( !that->bicubic_lut_texture ) {
    if ( !create_lut_texture( that ) )
      return 0;
  }

  if ( !that->bicubic_pass1_program.compiled && !opengl2_build_program( &that->bicubic_pass1_program, &bicubic_pass1_frag, "bicubic_pass1_frag" ) )
    return 0;

  glViewport( 0, 0, guiw, guih );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, guiw, guih, 0.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glActiveTexture( GL_TEXTURE1 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glUseProgram( that->bicubic_pass1_program.program );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass1_program.program, "tex" ), 0 );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass1_program.program, "lut" ), 1 );
  glUniform1f( glGetUniformLocationARB( that->bicubic_pass1_program.program, "spline" ), CATMULLROM_SPLINE );

  glBegin( GL_QUADS );
    glTexCoord2f( u, v );     glVertex3f( x, y, 0.);
    glTexCoord2f( u, v1 );    glVertex3f( x, y1, 0.);
    glTexCoord2f( u1, v1 );   glVertex3f( x1, y1, 0.);
    glTexCoord2f( u1, v );    glVertex3f( x1, y, 0.);
  glEnd();

  glUseProgram( 0 );

  return 1;
}



static int opengl2_draw_video_cubic_y( opengl2_driver_t *that, int guiw, int guih, GLfloat u, GLfloat v, GLfloat u1, GLfloat v1,
    GLfloat x, GLfloat y, GLfloat x1, GLfloat y1, GLuint video_texture )
{
  if ( !that->bicubic_lut_texture ) {
    if ( !create_lut_texture( that ) )
      return 0;
  }

  if ( !that->bicubic_pass2_program.compiled && !opengl2_build_program( &that->bicubic_pass2_program, &bicubic_pass2_frag, "bicubic_pass2_frag" ) )
    return 0;

  glViewport( 0, 0, guiw, guih );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, guiw, guih, 0.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glActiveTexture( GL_TEXTURE1 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glUseProgram( that->bicubic_pass2_program.program );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass2_program.program, "tex" ), 0 );
  glUniform1i( glGetUniformLocationARB( that->bicubic_pass2_program.program, "lut" ), 1 );
  glUniform1f( glGetUniformLocationARB( that->bicubic_pass2_program.program, "spline" ), CATMULLROM_SPLINE );

  glBegin( GL_QUADS );
    glTexCoord2f( u, v );     glVertex3f( x, y, 0.);
    glTexCoord2f( u, v1 );    glVertex3f( x, y1, 0.);
    glTexCoord2f( u1, v1 );   glVertex3f( x1, y1, 0.);
    glTexCoord2f( u1, v );    glVertex3f( x1, y, 0.);
  glEnd();

  glUseProgram( 0 );

  return 1;
}



static int opengl2_draw_video_simple( opengl2_driver_t *that, int guiw, int guih, GLfloat u, GLfloat v, GLfloat u1, GLfloat v1,
    GLfloat x, GLfloat y, GLfloat x1, GLfloat y1, GLuint video_texture )
{
  glViewport( 0, 0, guiw, guih );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, guiw, guih, 0.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

  glBegin( GL_QUADS );
    glTexCoord2f( u, v );     glVertex3f( x, y, 0.);
    glTexCoord2f( u, v1 );    glVertex3f( x, y1, 0.);
    glTexCoord2f( u1, v1 );   glVertex3f( x1, y1, 0.);
    glTexCoord2f( u1, v );    glVertex3f( x1, y, 0.);
  glEnd();

  return 1;
}



static void opengl2_draw_video_bilinear( opengl2_driver_t *that, int guiw, int guih, GLfloat u, GLfloat v, GLfloat u1, GLfloat v1,
    GLfloat x, GLfloat y, GLfloat x1, GLfloat y1, GLuint video_texture )
{
  glViewport( 0, 0, guiw, guih );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, guiw, guih, 0.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

  glBegin( GL_QUADS );
    glTexCoord2f( u, v );     glVertex3f( x, y, 0.);
    glTexCoord2f( u, v1 );    glVertex3f( x, y1, 0.);
    glTexCoord2f( u1, v1 );   glVertex3f( x1, y1, 0.);
    glTexCoord2f( u1, v );    glVertex3f( x1, y, 0.);
  glEnd();
}



static void opengl2_draw( opengl2_driver_t *that, opengl2_frame_t *frame )
{
  if ( !glXMakeCurrent( that->display, that->drawable, that->context ) ) {
    xprintf( that->xine, XINE_VERBOSITY_LOG, "video_out_opengl2: display unavailable for rendering\n" );
    return;
  }

  if ( !opengl2_check_textures_size( that, frame->width, frame->height ) ) {
    glXMakeCurrent( that->display, None, NULL );
    return;
  }

  opengl2_update_csc_matrix( that, frame );

  glBindFramebuffer( GL_FRAMEBUFFER, that->fbo );

  if ( frame->format == XINE_IMGFMT_YV12 ) {
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->yuvtex.y );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, that->videoPBO );
    void *mem = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    xine_fast_memcpy (mem, frame->vo_frame.base[0], frame->vo_frame.pitches[0] * frame->height);
    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frame->vo_frame.pitches[0], frame->height, GL_LUMINANCE, GL_UNSIGNED_BYTE, 0 );
    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->yuvtex.u );
    mem = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    xine_fast_memcpy (mem, frame->vo_frame.base[1], frame->vo_frame.pitches[1] * frame->height / 2);
    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frame->vo_frame.pitches[1], frame->height/2, GL_LUMINANCE, GL_UNSIGNED_BYTE, 0 );
    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->yuvtex.v );
    mem = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    xine_fast_memcpy (mem, frame->vo_frame.base[2], frame->vo_frame.pitches[2] * frame->height / 2);
    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frame->vo_frame.pitches[2], frame->height/2, GL_LUMINANCE, GL_UNSIGNED_BYTE, 0 );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );

    glUseProgram( that->yuv420_program.program );
    glUniform1i( glGetUniformLocationARB( that->yuv420_program.program, "texY" ), 0 );
    glUniform1i( glGetUniformLocationARB( that->yuv420_program.program, "texU" ), 1 );
    glUniform1i( glGetUniformLocationARB( that->yuv420_program.program, "texV" ), 2 );
    load_csc_matrix( that->yuv420_program.program, that->csc_matrix );
  }
  else if ( frame->format == XINE_IMGFMT_YUY2 ) {
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->yuvtex.yuv );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, that->videoPBO );
    void *mem = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    xine_fast_memcpy (mem, frame->vo_frame.base[0], frame->vo_frame.pitches[0] * frame->height);
    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frame->vo_frame.pitches[0] / 2, frame->height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 0 );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );

    glUseProgram( that->yuv422_program.program );
    glUniform1i( glGetUniformLocationARB( that->yuv422_program.program, "texYUV" ), 0 );
    load_csc_matrix( that->yuv422_program.program, that->csc_matrix );
  }
  else {
    /* unknown format */
    fprintf(stderr, "vo_opengl2: unknown image format\n" );
  }

  glViewport( 0, 0, frame->width, frame->height );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, frame->width, 0.0, frame->height, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  GLuint video_texture = that->videoTex;
  glDrawBuffer( GL_COLOR_ATTACHMENT0 );

  glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 );                           glVertex3f( 0, 0, 0.);
    glTexCoord2f( 0, frame->height );               glVertex3f( 0, frame->height, 0.);
    glTexCoord2f( frame->width, frame->height );    glVertex3f( frame->width, frame->height, 0.);
    glTexCoord2f( frame->width, 0 );                glVertex3f( frame->width, 0, 0.);
  glEnd();

  glUseProgram( 0 );

  // post-processing
  if ( that->sharpness != 0 )
    video_texture = opengl2_sharpness( that, frame, video_texture );

  // draw scaled overlays
  opengl2_update_overlays( that );
  opengl2_draw_scaled_overlays( that, frame );

  glBindFramebuffer( GL_FRAMEBUFFER, 0 );

  // draw on screen
  GLfloat u, u1, v, v1, x, x1, y, y1;

  u = that->sc.displayed_xoffset;
  v = that->sc.displayed_yoffset;
  u1 = that->sc.displayed_width + that->sc.displayed_xoffset;
  v1 = that->sc.displayed_height + that->sc.displayed_yoffset;
  x = that->sc.output_xoffset;
  y = that->sc.output_yoffset;
  x1 = that->sc.output_xoffset + that->sc.output_width;
  y1 = that->sc.output_yoffset + that->sc.output_height;

  int res = 0;

  if ( that->scale_bicubic ) {
    if ( that->sc.displayed_width != that->sc.output_width ) {
      if ( that->sc.displayed_height != that->sc.output_height )
        res = opengl2_draw_video_bicubic( that, that->sc.gui_width, that->sc.gui_height, u, v, u1, v1, x, y, x1, y1, video_texture );
      else
        res = opengl2_draw_video_cubic_x( that, that->sc.gui_width, that->sc.gui_height, u, v, u1, v1, x, y, x1, y1, video_texture );
    } else {
      if ( that->sc.displayed_height != that->sc.output_height )
        res = opengl2_draw_video_cubic_y( that, that->sc.gui_width, that->sc.gui_height, u, v, u1, v1, x, y, x1, y1, video_texture );
      else
        res = opengl2_draw_video_simple( that, that->sc.gui_width, that->sc.gui_height, u, v, u1, v1, x, y, x1, y1, video_texture );
    }
  }
  if (!res)
    opengl2_draw_video_bilinear( that, that->sc.gui_width, that->sc.gui_height, u, v, u1, v1, x, y, x1, y1, video_texture );

  // draw unscaled overlays
  opengl2_draw_unscaled_overlays( that );

  //if ( that->mglXSwapInterval )
    //that->mglXSwapInterval( 1 );

  glXSwapBuffers( that->display, that->drawable );

  glXMakeCurrent( that->display, None, NULL );
}



static void opengl2_display_frame( vo_driver_t *this_gen, vo_frame_t *frame_gen )
{
  opengl2_driver_t  *this  = (opengl2_driver_t *) this_gen;
  opengl2_frame_t   *frame = (opengl2_frame_t *) frame_gen;

  if ( (frame->width != this->sc.delivered_width) ||
                  (frame->height != this->sc.delivered_height) ||
                  (frame->ratio != this->sc.delivered_ratio) ||
                  (frame->vo_frame.crop_left != this->sc.crop_left) ||
                  (frame->vo_frame.crop_right != this->sc.crop_right) ||
                  (frame->vo_frame.crop_top != this->sc.crop_top) ||
                  (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  this->sc.delivered_height = frame->height;
  this->sc.delivered_width  = frame->width;
  this->sc.delivered_ratio  = frame->ratio;
  this->sc.crop_left        = frame->vo_frame.crop_left;
  this->sc.crop_right       = frame->vo_frame.crop_right;
  this->sc.crop_top         = frame->vo_frame.crop_top;
  this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

  opengl2_redraw_needed( this_gen );

  XLockDisplay (this->display);
  pthread_mutex_lock(&this->drawable_lock); /* protect drawable from being changed */
  opengl2_draw( this, frame );
  pthread_mutex_unlock(&this->drawable_lock); /* allow changing drawable again */
  XUnlockDisplay (this->display);

  frame->vo_frame.free( &frame->vo_frame );
}



static int opengl2_get_property( vo_driver_t *this_gen, int property )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  switch (property) {
    case VO_PROP_MAX_NUM_FRAMES:
      return 15;
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    case VO_PROP_OUTPUT_WIDTH:
      return this->sc.output_width;
    case VO_PROP_OUTPUT_HEIGHT:
      return this->sc.output_height;
    case VO_PROP_OUTPUT_XOFFSET:
      return this->sc.output_xoffset;
    case VO_PROP_OUTPUT_YOFFSET:
      return this->sc.output_yoffset;
    case VO_PROP_HUE:
      return this->hue;
    case VO_PROP_SATURATION:
      return this->saturation;
    case VO_PROP_CONTRAST:
      return this->contrast;
    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    case VO_PROP_SHARPNESS:
      return this->sharpness;
    case VO_PROP_ZOOM_X:
      return this->zoom_x;
    case VO_PROP_ZOOM_Y:
      return this->zoom_y;
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;
  }

  return -1;
}



static int opengl2_set_property( vo_driver_t *this_gen, int property, int value )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  //fprintf(stderr,"opengl2_set_property: property=%d, value=%d\n", property, value );

  switch (property) {
    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_x = value;
        this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;     //trigger re-calc of output size 
      }
      break;
    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_y = value;
        this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    // trigger re-calc of output size
      }
      break;
    case VO_PROP_ASPECT_RATIO:
      if ( value>=XINE_VO_ASPECT_NUM_RATIOS )
        value = XINE_VO_ASPECT_AUTO;
      this->sc.user_ratio = value;
      this->sc.force_redraw = 1;    // trigger re-calc of output size
      break;
    case VO_PROP_HUE: this->hue = value; this->update_csc = 1; break;
    case VO_PROP_SATURATION: this->saturation = value; this->update_csc = 1; break;
    case VO_PROP_CONTRAST: this->contrast = value; this->update_csc = 1; break;
    case VO_PROP_BRIGHTNESS: this->brightness = value; this->update_csc = 1; break;
    case VO_PROP_SHARPNESS: this->sharpness = value; break;
  }

  return value;
}



static void opengl2_get_property_min_max( vo_driver_t *this_gen, int property, int *min, int *max ) 
{
  switch ( property ) {
    case VO_PROP_HUE:
      *max = 127; *min = -128; break;
    case VO_PROP_SATURATION:
      *max = 255; *min = 0; break;
    case VO_PROP_CONTRAST:
      *max = 255; *min = 0; break;
    case VO_PROP_BRIGHTNESS:
      *max = 127; *min = -128; break;
    case VO_PROP_SHARPNESS:
      *max = 100; *min = -100; break;
    default:
      *max = 0; *min = 0;
  }
}



static int opengl2_gui_data_exchange( vo_driver_t *this_gen, int data_type, void *data )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
    case XINE_GUI_SEND_COMPLETION_EVENT:
      break;
#endif

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      pthread_mutex_lock(&this->drawable_lock); /* wait for other thread which is currently displaying */
      this->drawable = (Drawable)data;
      pthread_mutex_unlock(&this->drawable_lock);
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
      break;
    }

    default:
      return -1;
  }

  return 0;
}



static uint32_t opengl2_get_capabilities( vo_driver_t *this_gen )
{
  opengl2_driver_t *this = (opengl2_driver_t *) this_gen;

  return this->capabilities;
}



static void opengl2_set_bicubic( void *this_gen, xine_cfg_entry_t *entry )
{
  opengl2_driver_t  *this  = (opengl2_driver_t *) this_gen;

  this->scale_bicubic = entry->num_value;
  fprintf(stderr, "vo_opengl2: scale_bicubic=%d\n", this->scale_bicubic );
}



static void opengl2_dispose( vo_driver_t *this_gen )
{
  opengl2_driver_t *this = (opengl2_driver_t *) this_gen;

  cm_close (this);

  pthread_mutex_destroy(&this->drawable_lock);

  glXMakeCurrent( this->display, this->drawable, this->context );
  
  opengl2_delete_program( &this->yuv420_program );
  opengl2_delete_program( &this->yuv422_program );

  if ( this->sharpness_program.compiled )
    opengl2_delete_program( &this->sharpness_program );

  if ( this->bicubic_pass1_program.compiled )
    opengl2_delete_program( &this->bicubic_pass1_program );
  if ( this->bicubic_pass2_program.compiled )
    opengl2_delete_program( &this->bicubic_pass2_program );
  if ( this->bicubic_lut_texture )
    glDeleteTextures( 1, &this->bicubic_lut_texture );
  if ( this->bicubic_pass1_texture )
    glDeleteTextures( 1, &this->bicubic_pass1_texture );
  if ( this->bicubic_fbo )
    glDeleteFramebuffers( 1, &this->bicubic_fbo );

  if ( this->yuvtex.y )
    glDeleteTextures( 1, &this->yuvtex.y );
  if ( this->yuvtex.u )
    glDeleteTextures( 1, &this->yuvtex.u );
  if ( this->yuvtex.v )
    glDeleteTextures( 1, &this->yuvtex.v );
  if ( this->yuvtex.yuv )
    glDeleteTextures( 1, &this->yuvtex.yuv );
  if ( this->videoTex )
    glDeleteTextures( 1, &this->videoTex );
  if ( this->videoTex2 )
    glDeleteTextures( 1, &this->videoTex2 );
  if ( this->fbo )
    glDeleteFramebuffers( 1, &this->fbo );
  if ( this->videoPBO )
    glDeleteBuffers( 1, &this->videoPBO );
  if (this->overlayPBO)
    glDeleteBuffers( 1, &this->overlayPBO );

  int i;
  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    glDeleteTextures( 1, &this->overlays[i].tex );
  }
  
  glXMakeCurrent( this->display, None, NULL );

  glXDestroyContext( this->display, this->context );

  free (this);
}



static vo_driver_t *opengl2_open_plugin( video_driver_class_t *class_gen, const void *visual_gen )
{
  opengl2_class_t     *class   = (opengl2_class_t *) class_gen;
  x11_visual_t        *visual  = (x11_visual_t *) visual_gen;
  opengl2_driver_t    *this;
  config_values_t     *config  = class->xine->config;

  this = (opengl2_driver_t *) calloc(1, sizeof(opengl2_driver_t));

  if (!this)
    return NULL;

  this->display       = visual->display;
  this->screen        = visual->screen;
  this->drawable      = visual->d;
  this->context        = class->ctx;
  pthread_mutex_init(&this->drawable_lock, 0);

  _x_vo_scale_init(&this->sc, 1, 0, config);
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  this->zoom_x = 100;
  this->zoom_y = 100;

  this->xine   = class->xine;
  this->config = config;

  this->vo_driver.get_capabilities     = opengl2_get_capabilities;
  this->vo_driver.alloc_frame          = opengl2_alloc_frame;
  this->vo_driver.update_frame_format  = opengl2_update_frame_format;
  this->vo_driver.overlay_begin        = opengl2_overlay_begin;
  this->vo_driver.overlay_blend        = opengl2_overlay_blend;
  this->vo_driver.overlay_end          = opengl2_overlay_end;
  this->vo_driver.display_frame        = opengl2_display_frame;
  this->vo_driver.get_property         = opengl2_get_property;
  this->vo_driver.set_property         = opengl2_set_property;
  this->vo_driver.get_property_min_max = opengl2_get_property_min_max;
  this->vo_driver.gui_data_exchange    = opengl2_gui_data_exchange;
  this->vo_driver.dispose              = opengl2_dispose;
  this->vo_driver.redraw_needed        = opengl2_redraw_needed;

  if ( !glXMakeCurrent( this->display, this->drawable, this->context ) ) {
    fprintf(stderr, "vo_opengl2: MakeCurrent failed\n");
    return NULL;
  }

  glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
  glClearDepth( 1.0f );
  glDepthFunc( GL_LEQUAL );
  glDisable( GL_DEPTH_TEST );
  glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
  glDisable( GL_BLEND );
  glShadeModel( GL_SMOOTH );
  glEnable( GL_TEXTURE_RECTANGLE_ARB );
  glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );

  const char *extensions = glGetString( GL_EXTENSIONS );
  if ( strstr( extensions, "ARB_texture_float" ) )
    this->texture_float = 1;
  else
    this->texture_float = 0;

#define INITWIDTH  720
#define INITHEIGHT 576

  this->yuvtex.y = this->yuvtex.u = this->yuvtex.v = this->yuvtex.yuv = 0;
  this->yuvtex.width = this->yuvtex.height = 0;
  this->fbo = this->videoPBO = this->videoTex = this->videoTex2 = 0;
  if ( !opengl2_check_textures_size( this, INITWIDTH, INITHEIGHT ) )
    return 0;

  if ( !opengl2_build_program( &this->yuv420_program, &yuv420_frag, "yuv420_frag" ) )
    return NULL;
  if ( !opengl2_build_program( &this->yuv422_program, &yuv422_frag, "yuv422_frag" ) )
    return NULL;

  this->mglXSwapInterval = (GLXSWAPINTERVALSGI)glXGetProcAddressARB( (const GLubyte*)"glXSwapIntervalSGI" );

  glXMakeCurrent( this->display, None, NULL );

  this->capabilities = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP | VO_CAP_UNSCALED_OVERLAY | VO_CAP_CUSTOM_EXTENT_OVERLAY | VO_CAP_ARGB_LAYER_OVERLAY;// | VO_CAP_VIDEO_WINDOW_OVERLAY;

  this->capabilities |= VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE;

  this->capabilities |= VO_CAP_HUE;
  this->capabilities |= VO_CAP_SATURATION;
  this->capabilities |= VO_CAP_CONTRAST;
  this->capabilities |= VO_CAP_BRIGHTNESS;

  this->update_csc = 1;
  this->color_standard = 10;
  this->hue = 0;
  this->saturation = 128;
  this->contrast = 128;
  this->brightness = 0;
  this->sharpness = 0;
  this->sharpness_program.compiled = 0;

  cm_init (this);

  this->bicubic_pass1_program.compiled = 0;
  this->bicubic_pass2_program.compiled = 0;
  this->bicubic_lut_texture = 0;
  this->bicubic_pass1_texture = 0;
  this->bicubic_pass1_texture_width = 0;
  this->bicubic_pass1_texture_height = 0;
  this->bicubic_fbo = 0;

  int i;
  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    this->overlays[i].ovl_w = this->overlays[i].ovl_h = 0;
    this->overlays[i].ovl_x = this->overlays[i].ovl_y = 0;
    this->overlays[i].unscaled = 0;
    this->overlays[i].tex = 0;
    this->overlays[i].tex_w = this->overlays[i].tex_h = 0;
  }
  this->ovl_changed = 0;
  this->num_ovls = 0;

  if ( this->texture_float ) {
    this->scale_bicubic = config->register_bool( config, "video.output.opengl2_bicubic_scaling", 0,
      _("opengl2: use a bicubic algo to scale the video"),
      _("Set to true if you want bicubic scaling.\n\n"),
      10, opengl2_set_bicubic, this );
  }
  else
    this->scale_bicubic = 0;

  fprintf(stderr, "vo_opengl2: initialized.\n");

  return &this->vo_driver;
}



static int opengl2_check_platform( opengl2_class_t *this_gen, x11_visual_t *vis )
{
  int attribs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 16, None };

  Window        root;
  XVisualInfo  *visinfo;
  GLXContext    ctx;
  int           ret = 1;

  if ( !vis || !vis->display || !( root = RootWindow( vis->display, vis->screen ) ) )
    return 0;
  
  if ( !( visinfo = glXChooseVisual( vis->display, vis->screen, attribs ) ) )
    return 0;
  
  if ( !( ctx = glXCreateContext( vis->display, visinfo, NULL, GL_TRUE  ) ) )
    return 0;
  
  if ( glXMakeCurrent( vis->display, root, ctx ) ) {
    if ( !glXIsDirect( vis->display, ctx ) )
      ret = 0;
    const char *extensions = glGetString( GL_EXTENSIONS );
    if ( !strstr( extensions, "ARB_texture_rectangle" ) )
      ret = 0;
    if ( !strstr( extensions, "ARB_texture_non_power_of_two" ) )
      ret = 0;
    if ( !strstr( extensions, "ARB_pixel_buffer_object" ) )
      ret = 0;
    if ( !strstr( extensions, "ARB_framebuffer_object" ) )
      ret = 0;
    if ( !strstr( extensions, "ARB_fragment_shader" ) )
      ret = 0;
    if ( !strstr( extensions, "ARB_vertex_shader" ) )
      ret = 0;
    glXMakeCurrent( vis->display, None, NULL );
  }
  else
    ret = 0;

  if ( !ret )
    glXDestroyContext( vis->display, ctx );
  else
    this_gen->ctx = ctx;    

  return ret;
}

/*
 * class functions
 */

static void *opengl2_init_class( xine_t *xine, void *visual_gen )
{
  opengl2_class_t *this = (opengl2_class_t *) calloc(1, sizeof(opengl2_class_t));

  if ( !opengl2_check_platform( this, (x11_visual_t *)visual_gen ) )
    return NULL;

  this->driver_class.open_plugin     = opengl2_open_plugin;
  this->driver_class.identifier      = "opengl2";
  this->driver_class.description     = N_("xine video output plugin using opengl 2.0");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;

  return this;
}



static const vo_info_t vo_info_opengl2 = {
  7,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "opengl2", XINE_VERSION_CODE, &vo_info_opengl2, opengl2_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
