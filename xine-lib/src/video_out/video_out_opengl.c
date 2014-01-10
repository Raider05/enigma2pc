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
 *
 * video_out_opengl.c, OpenGL based interface for xine
 *
 * Written by Matthias Hopf <mat@mshopf.de>,
 * based on the xshm and xv video output plugins.
 */

/* #define LOG */
#define LOG_MODULE "video_out_opengl"


#define BYTES_PER_PIXEL      4
#define NUM_FRAMES_BACKLOG   4	/* Allow thread some time to render frames */

#define SECONDS_PER_CYCLE    60	/* Animation parameters */
#define CYCLE_FACTOR1        3
#define CYCLE_FACTOR2        5


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

/* defines for debugging extensions only */
/* #define GL_GLEXT_LEGACY */
#include <GL/gl.h>
/* #undef  GL_GLEXT_LEGACY */
#ifdef HAVE_GLU
#  include <GL/glu.h>
#endif

/*
 * *Sigh*
 * glext.h really makes a lot of trouble, so we are providing our own.
 * It has been created from the original one from
 * http://oss.sgi.com/projects/ogl-sample/registry/ with
 * perl -ne 's/\bGL_\B/MYGL_/g;s/\bgl\B/mygl/g;s/\b__gl\B/__mygl/g;s/\bPFNGL\B/PFNMYGL/g;print' glext.h >myglext.h
 */
#define GLchar MYGLChar
#define GLintptr MYGLintptr
#define GLsizeiptr MYGLsizeiptr
#define GLintptrARB MYGLintptrARB
#define GLsizeiptrARB MYGLsizeiptrARB
#define GLcharARB MYGLcharARB
#define GLhandleARB MYGLhandleARB
#define GLhalfARB MYGLhalfARB
#define GLhalfNV MYGLhalfNV
#include "myglext.h"

#if defined (_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif /* NOMINMAX */
#  include <windows.h>
#elif defined (__APPLE__)
#  include <GL/glx.h>
#  include <mach-o/dyld.h>
#else
#  include <GL/glx.h>
#  include <dlfcn.h>
#endif

#include "xine.h"
#include <xine/video_out.h>

#include <xine/xine_internal.h>
#include "yuv2rgb.h"
#include <xine/xineutils.h>
#include "x11osd.h"


#ifdef LOG
#  ifdef HAVE_GLU
#    define CHECKERR(a) do { int i = glGetError (); if (i != GL_NO_ERROR) fprintf (stderr, "   *** %s: 0x%x = %s\n", a, i, gluErrorString (i)); } while (0)
#  else
#    define CHECKERR(a) do { int i = glGetError (); if (i != GL_NO_ERROR) fprintf (stderr, "   *** %s: 0x%x\n", a, i); } while (0)
#  endif
#else
#  define CHECKERR(a) ((void)0)
#endif


#if (BYTES_PER_PIXEL != 4)
/* currently nothing else is supported */
#   error "BYTES_PER_PIXEL bad"
#endif
/* TODO: haven't checked bigendian for a long time... */
#ifdef WORDS_BIGENDIAN
#  define RGB_TEXTURE_FORMAT GL_RGBA
#  define YUV_FORMAT         MODE_32_BGR
#  define YUV_SWAP_MODE      1
#else
/* TODO: Use GL_BGRA extension check for dynamically changing this */
#if 1
/* Might be faster on ix86, but yuv2rgb often buggy, needs extension */
#  define RGB_TEXTURE_FORMAT GL_BGRA
#  define YUV_FORMAT         MODE_32_RGB
#else
/* Slower on ix86 and overlays use wrong pixel order, but needs no extension */
#  define RGB_TEXTURE_FORMAT GL_RGBA
#  define YUV_FORMAT         MODE_32_BGR
#endif
#  define YUV_SWAP_MODE      0
#endif

#define MY_PI                3.1415926
#define MY_2PI               6.2831853

typedef struct opengl_argb_layer_s {
  pthread_mutex_t  mutex;
  uint32_t        *buffer;
  /* dirty area */
  int width;
  int height;
  int changed;
} opengl_argb_layer_t;

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;

  uint8_t           *rgb, *rgb_dst;

  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */

} opengl_frame_t;


/* RENDER_DRAW to RENDER_SETUP are asynchronous actions, but later actions
 * imply former actions -> only check '>' on update */
/* RENDER_CREATE and later are synchronous actions and override async ones */
enum render_e { RENDER_NONE=0, RENDER_DRAW, RENDER_CLEAN, RENDER_SETUP,
		RENDER_CREATE, RENDER_VISUAL, RENDER_RELEASE, RENDER_EXIT };

typedef struct {

  vo_driver_t        vo_driver;
  vo_scale_t         sc;
  alphablend_t       alphablend_extra_data;

  /* X11 related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;

  /* Render thread */
  pthread_t          render_thread;
  enum render_e      render_action;
  int                render_frame_changed;
  pthread_mutex_t    render_action_mutex;
  pthread_cond_t     render_action_cond;
  pthread_cond_t     render_return_cond;
  int                last_width, last_height;

  /* Render parameters */
  int                render_fun_id;
  int                render_min_fps;
  int                render_double_buffer;
  int                gui_width, gui_height;

  /* OpenGL state */
  GLXContext         context;
  XVisualInfo       *vinfo;
  GLuint             fprog;
  int                tex_width, tex_height; /* independend of frame */
  /* OpenGL capabilities */
  const GLubyte     *gl_exts;	/* extension string - NULL if uninitialized */
  int                has_bgra;
  int                has_texobj;            /* TODO: use */
  int                has_fragprog;
  int                has_pixbufobj;
  /* OpenGL extensions */
  PFNMYGLBINDPROGRAMARBPROC          glBindProgramARB;
  PFNMYGLGENPROGRAMSARBPROC          glGenProgramsARB;
  PFNMYGLPROGRAMSTRINGARBPROC        glProgramStringARB;
  PFNMYGLPROGRAMENVPARAMETER4FARBPROC glProgramEnvParameter4fARB;
  PFNMYGLGENTEXTURESEXTPROC          glGenTexturesEXT;
  PFNMYGLBINDTEXTUREEXTPROC          glBindTextureEXT;

  int                brightness;
  int                contrast;
  int                saturation;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  /* color matrix switching */
  int                cm_yuv2rgb, cm_fragprog, cm_state;

  /* Frame state */
  opengl_frame_t    *frame[NUM_FRAMES_BACKLOG];
  
  /* Overlay */
  x11osd            *xoverlay;
  opengl_argb_layer_t argb_layer;
  int                ovl_changed;
  int                last_ovl_width, last_ovl_height;
  int                tex_ovl_width, tex_ovl_height; /* independend of frame */
  int                video_window_width, video_window_height, video_window_x, video_window_y;

  config_values_t   *config;
  xine_t            *xine;
} opengl_driver_t;

typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
} opengl_class_t;

typedef void *(*thread_run_t)(void *);


typedef struct {
    /* Name of render backend */
    const char * const name;
    /* Finally display current image (needed for Redraw) */
    void (*display)(opengl_driver_t *, opengl_frame_t *);
    /* Upload new image; Returns 0 if failed */
    int (*image)(opengl_driver_t *, opengl_frame_t *);
    /* Setup; Returns 0 if failed */
    int (*setup)(opengl_driver_t *);
    /* Flag: needs output converted to rgb (is able to do YUV otherwise) */
    int needsrgb;
    /* Default action: what to do if there's no new image
     * typically either RENDER_NONE or RENDER_DRAW (for animated render's) */
    enum render_e defaction;
    /* Fallback: change to following render backend if this one doesn't work */
    int fallback;
    /* Upload new overlay image; Returns 0 if failed */
    int (*ovl_image)(opengl_driver_t *, opengl_frame_t *);
    /* Display current overlay */
    void (*ovl_display)(opengl_driver_t *, opengl_frame_t *);
} opengl_render_t;


/* import common color matrix stuff */
#define CM_DRIVER_T opengl_driver_t
#include "color_matrix.c"

extern const int32_t Inverse_Table_6_9[8][4]; /* from yuv2rgb.c */

/*
 * Render functions
 */

/* Static 2d texture based display */
static void render_tex2d (opengl_driver_t *this, opengl_frame_t *frame) {
  int             x1, x2, y1, y2;
  float           tx, ty;

  /* Calc texture/rectangle coords */
  if (this->video_window_width && this->video_window_height) // video is displayed in a small window
  {
    x1 = this->video_window_x;
    y1 = this->video_window_y;
    x2 = x1 + this->video_window_width;
    y2 = y1 + this->video_window_height;
  }
  else
  {
    x1 = this->sc.output_xoffset;
    y1 = this->sc.output_yoffset;
    x2 = x1 + this->sc.output_width;
    y2 = y1 + this->sc.output_height;
  }
  
  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;
  /* Draw quad */
  glBegin (GL_QUADS);
  glTexCoord2f (tx, ty);   glVertex2i (x2, y2);
  glTexCoord2f (0,  ty);   glVertex2i (x1, y2);
  glTexCoord2f (0,  0);    glVertex2i (x1, y1);
  glTexCoord2f (tx, 0);    glVertex2i (x2, y1);
  glEnd ();
}

/* Static Overlay display */
static void render_overlay (opengl_driver_t *this, opengl_frame_t *frame) {
  int             x1, x2, y1, y2;
  float           tx, ty;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  if (this->tex_ovl_width == 0 && this->tex_ovl_height == 0) // Image_Pipeline renderer is active (no texture support)
  {
    glPixelZoom   (((float)this->gui_width)    / this->argb_layer.width,
	       	 - ((float)this->gui_height)   / this->argb_layer.height);
    glRasterPos2i (0, 0);
    glDrawPixels  (this->argb_layer.width, this->argb_layer.height, GL_BGRA,
		             GL_UNSIGNED_BYTE, this->argb_layer.buffer);
  }
  else
  {
    if (this->glBindTextureEXT) // bind overlay texture
      this->glBindTextureEXT (GL_TEXTURE_2D, 1000);

    if (this->fprog != -1)  // 2D_Tex_Fragprog is active which uses a pixelshader to make yuv2rgb conversion
	                    // -> disable it because texture is already argb
      glDisable(MYGL_FRAGMENT_PROGRAM_ARB);
		    
    /* Calc texture/rectangle coords */
    x1 = 0;
    y1 = 0;
    x2 = this->gui_width;
    y2 = this->gui_height;
    tx = (float) this->argb_layer.width  / this->tex_ovl_width;
    ty = (float) this->argb_layer.height / this->tex_ovl_height;

    /* Draw quad */
    glBegin (GL_QUADS);
    glTexCoord2f (tx, ty);   glVertex2i (x2, y2);
    glTexCoord2f (0,  ty);   glVertex2i (x1, y2);
    glTexCoord2f (0,  0);    glVertex2i (x1, y1);
    glTexCoord2f (tx, 0);    glVertex2i (x2, y1);
    glEnd ();

    if (this->fprog != -1)  // enable pixelshader for next normal video frame
      glEnable(MYGL_FRAGMENT_PROGRAM_ARB);

    if (this->glBindTextureEXT) // unbind overlay texture  
      this->glBindTextureEXT (GL_TEXTURE_2D, 0);
  }
  glDisable(GL_BLEND);
}

/* Static 2d texture tiled based display */
static void render_tex2dtiled (opengl_driver_t *this, opengl_frame_t *frame) {
  int    tex_w, tex_h, frame_w, frame_h;
  int    i, j, nx, ny;
  float  x1, x2, y1, y2, txa, txb, tya, tyb, xa, xb, xn, ya, yb, yn;

  tex_w   = this->tex_width;
  tex_h   = this->tex_height;
  frame_w = frame->width;
  frame_h = frame->height;
  /* Calc texture/rectangle coords */
  if (this->video_window_width && this->video_window_height) // video is displayed in a small window
  {
    x1 = this->video_window_x;
    y1 = this->video_window_y;
    x2 = x1 + this->video_window_width;
    y2 = y1 + this->video_window_height;
  }
  else
  {
    x1 = this->sc.output_xoffset;
    y1 = this->sc.output_yoffset;
    x2 = x1 + this->sc.output_width;
    y2 = y1 + this->sc.output_height;
  }
  txa = 1.0 / tex_w;
  tya = 1.0 / tex_h;
  txb = (float) frame_w / (tex_w-2);	/* temporary: total */
  tyb = (float) frame_h / (tex_h-2);
  xn = this->sc.output_width  / txb;
  yn = this->sc.output_height / tyb;
  nx = txb;
  ny = tyb;

  /* Draw quads */
  for (i = 0, ya = y1; i <= ny; i++, ya += yn) {
    for (j = 0, xa = x1; j <= nx; j++, xa += xn) {
      if (this->glBindTextureEXT)
        this->glBindTextureEXT (GL_TEXTURE_2D, i*(nx+1)+j+1);
      txb = (float) (j == nx ? frame_w - j*(tex_w-2)+1 : (tex_w-1)) / tex_w;
      tyb = (float) (i == ny ? frame_h - i*(tex_h-2)+1 : (tex_h-1)) / tex_h;
      xb  = (j == nx ? x2 : xa + xn);
      yb  = (i == ny ? y2 : ya + yn);
      glBegin (GL_QUADS);
      glTexCoord2f (txb, tyb);   glVertex2f (xb, yb);
      glTexCoord2f (txa, tyb);   glVertex2f (xa, yb);
      glTexCoord2f (txa, tya);   glVertex2f (xa, ya);
      glTexCoord2f (txb, tya);   glVertex2f (xb, ya);
      glEnd ();
    }
  }
}

/* Static image pipline based display */
static void render_draw (opengl_driver_t *this, opengl_frame_t *frame) {
	
  if (this->video_window_width && this->video_window_height) // video is displayed in a small window
  {
    glPixelZoom(((float)this->video_window_width)  / frame->width,
              - ((float)this->video_window_height) / frame->height);
    glRasterPos2i(this->video_window_x, this->video_window_y);
    glDrawPixels (frame->width, frame->height, RGB_TEXTURE_FORMAT,
                  GL_UNSIGNED_BYTE, frame->rgb);
  }
  else
  {
    glPixelZoom(((float)this->sc.output_width)  / frame->width,
              - ((float)this->sc.output_height) / frame->height);
    glRasterPos2i(this->sc.output_xoffset, this->sc.output_yoffset);
    glDrawPixels (frame->width, frame->height, RGB_TEXTURE_FORMAT,
                  GL_UNSIGNED_BYTE, frame->rgb);
  }
}

/* Animated spinning cylinder */
#define CYL_TESSELATION    128
#define CYL_WIDTH          2.5
#define CYL_HEIGHT         3.0
static void render_cyl (opengl_driver_t *this, opengl_frame_t *frame) {
  int             i;
  float           off;
  float           tx, ty;
  struct timeval  curtime;

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  /* Calc timing + texture coords */
  gettimeofday (&curtime, NULL);
  off = ((curtime.tv_sec % SECONDS_PER_CYCLE) + curtime.tv_usec * 1e-6)
    * (360.0 / SECONDS_PER_CYCLE);
  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;

  /* Spin it */
  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef   (0, 0, -10);
  glRotatef      (off * CYCLE_FACTOR1, 1, 0, 0);
  glRotatef      (off,                 0, 0, 1);
  glRotatef      (off * CYCLE_FACTOR2, 0, 1, 0);

  /* Note that this is not aspect ratio corrected */
  glBegin (GL_QUADS);
  for (i = 0; i < CYL_TESSELATION; i++) {
    float x1 = CYL_WIDTH * sin (i     * MY_2PI / CYL_TESSELATION);
    float x2 = CYL_WIDTH * sin ((i+1) * MY_2PI / CYL_TESSELATION);
    float z1 = CYL_WIDTH * cos (i     * MY_2PI / CYL_TESSELATION);
    float z2 = CYL_WIDTH * cos ((i+1) * MY_2PI / CYL_TESSELATION);
    float tx1 = tx * i / CYL_TESSELATION;
    float tx2 = tx * (i+1) / CYL_TESSELATION;
    glTexCoord2f (tx1, 0);     glVertex3f (x1, CYL_HEIGHT, z1);
    glTexCoord2f (tx2, 0);     glVertex3f (x2, CYL_HEIGHT, z2);
    glTexCoord2f (tx2, ty);    glVertex3f (x2, -CYL_HEIGHT, z2);
    glTexCoord2f (tx1, ty);    glVertex3f (x1, -CYL_HEIGHT, z1);
  }
  glEnd ();
}

/* Animated spinning environment mapped torus */
#define DIST_FACTOR   16.568542	/* 2 * (sqrt(2)-1) * 20 */
static void render_env_tor (opengl_driver_t *this, opengl_frame_t *frame) {
  float           off;
  float           x1, y1, x2, y2, tx, ty;
  struct timeval  curtime;

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  /* Calc timing + texture coords */
  gettimeofday (&curtime, NULL);
  off = ((curtime.tv_sec % SECONDS_PER_CYCLE) + curtime.tv_usec * 1e-6)
    * (360.0 / SECONDS_PER_CYCLE);
  /* Fovy is angle in y direction */
  x1 = (this->sc.output_xoffset - this->gui_width/2.0)
    * DIST_FACTOR / this->gui_height;
  x2 = (this->sc.output_xoffset+this->sc.output_width - this->gui_width/2.0)
    * DIST_FACTOR / this->gui_height;
  y1 = (this->sc.output_yoffset - this->gui_height/2.0)
    * DIST_FACTOR / this->gui_height;
  y2 = (this->sc.output_yoffset+this->sc.output_height - this->gui_height/2.0)
    * DIST_FACTOR / this->gui_height;

  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;

  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();

  /* Draw background, Y swapped */
  glMatrixMode   (GL_TEXTURE);
  glPushMatrix   ();
  glLoadIdentity ();
  glDepthFunc    (GL_ALWAYS);
  glDepthMask    (GL_FALSE);

  glBegin        (GL_QUADS);
  glColor3f      (1, 1, 1);
  glTexCoord2f   (tx, 0);     glVertex3f   (x2, y2, -20);
  glTexCoord2f   (0,  0);     glVertex3f   (x1, y2, -20);
  glTexCoord2f   (0,  ty);    glVertex3f   (x1, y1, -20);
  glTexCoord2f   (tx, ty);    glVertex3f   (x2, y1, -20);
  glEnd          ();

  glPopMatrix    ();
  glDepthFunc    (GL_LEQUAL);
  glDepthMask    (GL_TRUE);

  /* Spin it */
  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef   (0, 0, -10);
  glRotatef      (off * CYCLE_FACTOR1, 1, 0, 0);
  glRotatef      (off,                 0, 0, 1);
  glRotatef      (off * CYCLE_FACTOR2, 0, 1, 0);
  glEnable       (GL_TEXTURE_GEN_S);
  glEnable       (GL_TEXTURE_GEN_T);
  glColor3f      (1, 0.8, 0.6);
  glCallList     (1);
  glDisable      (GL_TEXTURE_GEN_S);
  glDisable      (GL_TEXTURE_GEN_T);
}

/*
 * Image setup functions
 */
/* returns 0: allocation failure  1: texture updated  2: texture kept */
static int render_help_image_tex (opengl_driver_t *this, int new_w, int new_h,
				  GLint glformat, GLint texformat) {
  int tex_w, tex_h, err;

  /* check necessary texture size and allocate */
  if (new_w != this->last_width ||
      new_h != this->last_height ||
      ! this->tex_width || ! this->tex_height) {
    tex_w = tex_h = 16;
    while (tex_w < new_w)
      tex_w <<= 1;
    while (tex_h < new_h)
      tex_h <<= 1;

    if (tex_w != this->tex_width || tex_h != this->tex_height) {
      char *tmp = calloc (tex_w * tex_h, 4); /* 4 enough until RGBA */
      if (this->glBindTextureEXT)
        this->glBindTextureEXT (GL_TEXTURE_2D, 0);
      glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexImage2D (GL_TEXTURE_2D, 0, glformat, tex_w, tex_h,
		      0, texformat, GL_UNSIGNED_BYTE, tmp);
      err = glGetError ();
      free (tmp);
      if (err)
	return 0;
      this->tex_width  = tex_w;
      this->tex_height = tex_h;
      lprintf ("* new texsize: %dx%d\n", tex_w, tex_h);
    }
    this->last_width  = new_w;
    this->last_height = new_h;
    return 1;
  }
  return 2;
}

/* holds/allocates extra texture for overlay */
/* returns 0: allocation failure  1: texture updated  2: texture kept */
static int render_help_overlay_image_tex(opengl_driver_t *this, int new_w, int new_h,
				                         GLint glformat, GLint texformat) {
  int tex_w, tex_h, err;

  /* check necessary texture size and allocate */
  if (new_w != this->last_ovl_width ||
      new_h != this->last_ovl_height ||
      ! this->tex_ovl_width || ! this->tex_ovl_height) {
    tex_w = tex_h = 16;
    while (tex_w < new_w)
      tex_w <<= 1;
    while (tex_h < new_h)
      tex_h <<= 1;

    if (tex_w != this->tex_ovl_width || tex_h != this->tex_ovl_height) {
      char *tmp = calloc (tex_w * tex_h, 4); /* 4 enough until RGBA */
      if (this->glBindTextureEXT)  // xine code binds without call glGenTextures -> seems to me not correct
        this->glBindTextureEXT (GL_TEXTURE_2D, 1000);  // bind 1000 to avoid collision with tiledtex textures / don't want to rewrite everything ...
      glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexImage2D (GL_TEXTURE_2D, 0, glformat, tex_w, tex_h,
		      0, texformat, GL_UNSIGNED_BYTE, tmp);
      err = glGetError ();
      free (tmp);
      if (err)
	    return 0;
      this->tex_ovl_width  = tex_w;
      this->tex_ovl_height = tex_h;
      lprintf ("* new texsize: %dx%d\n", tex_w, tex_h);
    }
    this->last_ovl_width  = new_w;
    this->last_ovl_height = new_h;
    return 1;
  }
  return 2;										 
}

/* returns 0: allocation failure  1: textures updated  2: textures kept */
static int render_help_image_tiledtex (opengl_driver_t *this,
				       int new_w, int new_h,
				       GLint glformat, GLint texformat) {
  int tex_w, tex_h, err, i, num;

  /* check necessary texture size and allocate */
  if (new_w != this->last_width ||
      new_h != this->last_height ||
      ! this->tex_width || ! this->tex_height) {
    tex_w = tex_h = 16;
    while (tex_w < new_w)
      tex_w <<= 1;
    while (tex_h < new_h)
      tex_h <<= 1;

    if (tex_w != this->tex_width || tex_h != this->tex_height) {
      char *tmp = calloc (tex_w * tex_h, 4); /* 4 enough until RGBA */
      if (this->glBindTextureEXT)
        this->glBindTextureEXT (GL_TEXTURE_2D, 1);
      /* allocate and figure out maximum texture size */
      do {
        glTexImage2D (GL_TEXTURE_2D, 0, glformat, tex_w, tex_h,
		      0, texformat, GL_UNSIGNED_BYTE, tmp);
	err = glGetError ();
	if (err) {
	  if (tex_w > tex_h)
	    tex_w >>= 1;
	  else
	    tex_h >>= 1;
          if (tex_w < 64 && tex_h < 64)
	    break;
	}
      } while (err);
      /* tiles have to overlap by one pixel in each direction
       * -> (tex_w-2) x (tex_h-2) */
      num = (new_w / (tex_w-2) + 1) * (new_h / (tex_h-2) + 1);
      if (! this->has_texobj && num > 1)
	err = 1;
      if (! err) {
        for (i = 1; i <= num; i++) {
          if (this->glBindTextureEXT)
            this->glBindTextureEXT (GL_TEXTURE_2D, i);
          glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexImage2D (GL_TEXTURE_2D, 0, glformat, tex_w, tex_h,
		        0, texformat, GL_UNSIGNED_BYTE, tmp);
        }
      }
      free (tmp);
      if (err)
	return 0;
      this->tex_width  = tex_w;
      this->tex_height = tex_h;
      lprintf ("* new texsize: %dx%d on %d tiles\n", tex_w, tex_h, num);
    }
    this->last_width  = new_w;
    this->last_height = new_h;
    return 1;
  }
  return 2;
}

static int render_image_nop (opengl_driver_t *this, opengl_frame_t *frame) {
  return 1;
}

static int render_image_tex (opengl_driver_t *this, opengl_frame_t *frame) {
  int ret;

  ret = render_help_image_tex (this, frame->width, frame->height,
			       GL_RGB, RGB_TEXTURE_FORMAT);
  if (! ret)
    return 0;

  /* TODO: asynchronous texture upload (ARB_pixel_buffer_object) */
  /* texture data is already not destroyed immedeately after this call */
  /* Load texture */
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
		   RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
		   frame->rgb);
  return 1;
}

static int render_overlay_image_tex (opengl_driver_t *this, opengl_frame_t *frame) {
  int ret;
  
  // use own texture
  ret = render_help_overlay_image_tex (this, this->argb_layer.width, this->argb_layer.height,
                                       4, GL_BGRA);

  if (! ret)
    return 0;

  if (this->glBindTextureEXT)
    this->glBindTextureEXT (GL_TEXTURE_2D, 1000); 
  glTexSubImage2D (GL_TEXTURE_2D, 0, 4, 0, this->argb_layer.width, this->argb_layer.height,
                   GL_BGRA, GL_UNSIGNED_BYTE,
                   this->argb_layer.buffer);
  return 1;
}

static int render_image_tiledtex (opengl_driver_t *this, opengl_frame_t *frame) {
  int ret;
  int frame_w, frame_h, tex_w, tex_h, i, j, nx, ny;

  ret = render_help_image_tiledtex (this, frame->width, frame->height,
			            GL_RGB, RGB_TEXTURE_FORMAT);
  if (! ret)
    return 0;

  frame_w = frame->width;
  frame_h = frame->height;
  tex_w   = this->tex_width;
  tex_h   = this->tex_height;
  nx = frame_w / (tex_w-2);
  ny = frame_h / (tex_h-2);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, frame_w);
  for (i = 0; i <= ny; i++) {
    for (j = 0; j <= nx; j++) {
      if (this->glBindTextureEXT)
        this->glBindTextureEXT (GL_TEXTURE_2D, i*(nx+1)+j+1);
      /* TODO: asynchronous texture upload (ARB_pixel_buffer_object) */
      /* gets a bit ugly in order not to address data above frame->rgb */
      glTexSubImage2D (GL_TEXTURE_2D, 0, (j==0), (i==0),
                       j == nx ? frame_w-j*(tex_w-2)+(j!=0) : tex_w-(j==0),
                       i == ny ? frame_h-i*(tex_h-2)+(i!=0) : tex_h-(i==0),
		       RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
		       &frame->rgb[4*(frame_w*((tex_h-2)*i-(i!=0))+(tex_w-2)*j-(j!=0))]);
    }
  }
  glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  CHECKERR ("texsubimage");
  return 1;
}

/* YUV texture layout                   .llllll.
 *                                      .llllll.
 * lum size 6x4 ->                      .llllll.
 *                                      .llllll.
 * empty w/ 0   (box filter y)          .......
 * empty w/ 0.5 (box filter u+v)        /////////
 *                                      /uuu/vvv/
 * u, v size 3x2 each ->                /uuu/vvv/
 *                                      ///////// */
/* TODO: use non-power-of-2 textures */
/* TODO: don't calculate texcoords in fragprog, but get interpolated coords */
static int render_image_fp_yuv (opengl_driver_t *this, opengl_frame_t *frame) {
  int w2 = frame->width/2, h2 = frame->height/2;
  int i, ret;

  if (! this->has_fragprog)
    return 0;
  if (frame->format != XINE_IMGFMT_YV12) {
      fprintf (stderr, "Fragment program only supported for YV12 data\n");
      return 0;
  }

  ret = render_help_image_tex (this, w2 + frame->vo_frame.pitches[2] + 3, frame->height + h2 + 3,
			       GL_LUMINANCE, GL_LUMINANCE);
  if (! ret)
    return 0;
  if (ret == 1) {
    char *tmp = calloc (this->tex_width * this->tex_height, 1);
    for (i = 0; i < frame->width+3; i++) {
      tmp[this->tex_width*(frame->height+1)+i] = 128;
      tmp[this->tex_width*(frame->height+h2+2)+i] = 128;
    }
    for (i = 0; i < h2; i++) {
      tmp[this->tex_width*(frame->height+2+i)] = 128;
      tmp[this->tex_width*(frame->height+2+i)+w2+1] = 128;
      tmp[this->tex_width*(frame->height+2+i)+2*w2+2] = 128;
    }
    glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, this->tex_width, this->tex_height,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, tmp);
    CHECKERR ("clean-texsubimage");
    free (tmp);
    this->glProgramEnvParameter4fARB (MYGL_FRAGMENT_PROGRAM_ARB, 0,
				      1.0                     /this->tex_width,
				      (float)(frame->height+2)/this->tex_height,
				      (float)(w2+2)           /this->tex_width,
				      0);
  }
  if (w2 & 7)
    for (i = 0; i < h2; i++) {
      frame->vo_frame.base[1][i*frame->vo_frame.pitches[1]+w2] = 128;
      frame->vo_frame.base[2][i*frame->vo_frame.pitches[2]+w2] = 128;
    }
  /* Load texture */
  CHECKERR ("pre-texsubimage");
  glTexSubImage2D (GL_TEXTURE_2D, 0,  1, 0,  frame->vo_frame.pitches[0], frame->height,
		   GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->vo_frame.base[0]);
  glTexSubImage2D (GL_TEXTURE_2D, 0,  1, frame->height+2,  frame->vo_frame.pitches[1], h2,
		   GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->vo_frame.base[1]);
  glTexSubImage2D (GL_TEXTURE_2D, 0,  w2+2, frame->height+2,  frame->vo_frame.pitches[2], h2,
		   GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->vo_frame.base[2]);
  CHECKERR ("texsubimage");
  return 1;
}

static int render_image_envtex (opengl_driver_t *this, opengl_frame_t *frame) {
  static float mTex[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  int ret;
  /* update texture matrix if frame size changed */
  if (frame->width != this->last_width ||
      frame->height != this->last_height ||
      ! this->tex_width || ! this->tex_height) {
    ret = render_image_tex (this, frame);
    /* Texture matrix has to skale/shift tex origin + swap y coords */
    mTex[0]  =   1.0 * frame->width  / this->tex_width;
    mTex[5]  =  -1.0 * frame->height / this->tex_height;
    mTex[12] = (-2.0 * mTex[0]) / mTex[0];
    mTex[13] =  -mTex[5];
    glMatrixMode  (GL_TEXTURE);
    glLoadMatrixf (mTex);
  } else {
    ret = render_image_tex (this, frame);
  }
  return ret;
}


/*
 * Render setup functions
 */
static int render_help_verify_ext (opengl_driver_t *this, const char *ext) {
  int ret = 0;
  const size_t l = strlen (ext);
  const char *e;
  for (e = (char *) this->gl_exts; e && *e; e = strchr (e, ' ')) {
    while (isspace (*e))
      e++;
    if (strncmp (e, ext, l) == 0 && (e[l] == 0 || e[l] == ' ')) {
      ret = 1;
      break;
    }
  }
  xprintf (this->xine, XINE_VERBOSITY_LOG,
	   "video_out_opengl: extension %s: %s\n", ext,
	   ret ? "OK" : "missing");
  return ret;
}

/* Return the address of a linked function */
static void *getdladdr (const GLubyte *_funcName) {
  const char *funcName = (const char *) _funcName;

#if defined(_WIN32)
  return NULL;

#elif defined(__APPLE__)
  char *temp;
  temp = _x_asprintf("_%s", funcName);
  void *res = NULL;
  if (NSIsSymbolNameDefined (temp)) {
    NSSymbol symbol = NSLookupAndBindSymbol (temp);
    res = NSAddressOfSymbol (symbol);
  }
  free (temp);
  return res;

#elif defined (__sun) || defined (__sgi)
   static void *handle = NULL;
   if (!handle) {
     handle = dlopen (NULL, RTLD_LAZY);
   }
   return dlsym (handle, funcName);

#else /* all other Un*xes */
  return dlsym (0, funcName);

#endif
}

/* Return the address of the specified OpenGL extension function */
static void *getaddr (const char *funcName) {

#if defined(_WIN32)
  return (void*) wglGetProcAddress ((const GLubyte *) funcName);

#else
  void * (*MYgetProcAddress) (const GLubyte *);
  void *res;

  /* Try to get address of extension via glXGetProcAddress[ARB], if that
   * fails try to get the address of a linked function */
  MYgetProcAddress = getdladdr ((const GLubyte *) "glXGetProcAddress");
  if (! MYgetProcAddress)
    MYgetProcAddress = getdladdr ((const GLubyte *) "glXGetProcAddressARB");
  if (! MYgetProcAddress)
    MYgetProcAddress = getdladdr;

  res = MYgetProcAddress ((const GLubyte *) funcName);
  if (! res)
    fprintf (stderr, "Cannot find address for OpenGL extension function '%s',\n"
	     "which should be available according to extension specs.\n",
	     funcName);
  return res;

#endif
}

static void render_help_check_exts (opengl_driver_t *this) {
  static int num_tests = 0;

  if (this->gl_exts)
    return;

  this->gl_exts  = glGetString (GL_EXTENSIONS);
  if (! this->gl_exts) {
    if (++num_tests > 10) {
      fprintf (stderr, "video_out_opengl: Cannot find OpenGL extensions (tried multiple times).\n");
      this->gl_exts = (const GLubyte *) "";
    }
  } else
    num_tests = 0;
  if (! this->gl_exts)
    xprintf (this->xine, XINE_VERBOSITY_NONE, "video_out_opengl: No extensions found - assuming bad visual, testing later.\n");

  this->has_bgra = render_help_verify_ext (this, "GL_EXT_bgra");
  if (! this->has_bgra && RGB_TEXTURE_FORMAT == GL_BGRA && this->gl_exts)
    fprintf (stderr, "video_out_opengl: compiled for BGRA output, but missing extension.\n");
  if ( (this->has_texobj   = render_help_verify_ext (this, "GL_EXT_texture_object")) ) {
    this->glGenTexturesEXT   = getaddr ("glGenTexturesEXT"); /* TODO: use for alloc */
    this->glBindTextureEXT   = getaddr ("glBindTextureEXT");
    if (! this->glGenTexturesEXT || ! this->glBindTextureEXT)
      this->has_texobj = 0;
  }
  if ( (this->has_fragprog = render_help_verify_ext (this, "GL_ARB_fragment_program")) ) {
    this->glBindProgramARB   = getaddr ("glBindProgramARB");
    this->glGenProgramsARB   = getaddr ("glGenProgramsARB");
    this->glProgramStringARB = getaddr ("glProgramStringARB");
    this->glProgramEnvParameter4fARB = getaddr ("glProgramEnvParameter4fARB");
    if (! this->glBindProgramARB   || ! this->glGenProgramsARB ||
	! this->glProgramStringARB || ! this->glProgramEnvParameter4fARB)
      this->has_fragprog = 0;
  }
  this->has_pixbufobj = render_help_verify_ext (this, "GL_ARB_pixel_buffer_object");
}

static int render_help_setup_tex (opengl_driver_t *this) {
  CHECKERR ("pre-tex_setup");
  glEnable        (GL_TEXTURE_2D);
  glTexEnvi       (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,   GL_REPLACE);
  glMatrixMode    (GL_TEXTURE);
  glLoadIdentity  ();
  CHECKERR ("post-tex_setup");
  return 1;
}

#ifndef HAVE_GLU
#define gluPerspective myGluPerspective
static void myGluPerspective (GLdouble fovy, GLdouble aspect,
		       GLdouble zNear, GLdouble zFar) {
  double ymax = zNear * tan(fovy * M_PI / 360.0);
  double ymin = -ymax;
  glFrustum (ymin * aspect, ymax * aspect, ymin, ymax, zNear, zFar);
}
#endif


static int render_setup_2d (opengl_driver_t *this) {
  render_help_check_exts (this);
  CHECKERR ("pre-viewport");
  if (this->gui_width > 0 && this->gui_height > 0)
    glViewport   (0, 0, this->gui_width, this->gui_height);
  glDepthRange (-1, 1);
  glClearColor (0, 0, 0, 0);
  glColor3f    (1, 1, 1);
  glClearDepth (1);
  CHECKERR ("pre-frustum_setup");
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho      (0, this->gui_width, this->gui_height, 0, -1, 1);
  CHECKERR ("post-frustum_setup");
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glDisable    (GL_BLEND);
  glDisable    (GL_DEPTH_TEST);
  glDepthMask  (GL_FALSE);
  glDisable    (GL_CULL_FACE);
  glShadeModel (GL_FLAT);
  glDisable    (GL_TEXTURE_2D);
  CHECKERR ("post-en/disable");
  glHint       (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
  glDisable    (MYGL_FRAGMENT_PROGRAM_ARB);
  glGetError   ();
  return 1;
}

static int render_setup_tex2d (opengl_driver_t *this) {
  int ret;
  ret  = render_setup_2d (this);
  ret &= render_help_setup_tex (this);
  return ret;
}
static int render_setup_3d (opengl_driver_t *this) {
  render_help_check_exts (this);
  if (this->gui_width > 0 && this->gui_height > 0) {
    CHECKERR ("pre-3dfrustum_setup");
    glViewport   (0, 0, this->gui_width, this->gui_height);
    glDepthRange (0, 1);
    glClearColor (0, 0, 0, 0);
    glClearDepth (1.0f);
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    gluPerspective  (45.0f,
		     (GLfloat)(this->gui_width) / (GLfloat)(this->gui_height),
		     1.0f, 1000.0f);
  }
  glDisable    (GL_BLEND);
  glEnable     (GL_DEPTH_TEST);
  glDepthFunc  (GL_LEQUAL);
  glDepthMask  (GL_TRUE);
  glDisable    (GL_CULL_FACE);
  glShadeModel (GL_FLAT);
  glDisable    (GL_TEXTURE_2D);
  glHint       (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  CHECKERR ("post-3dfrustum_setup");
  glDisable    (MYGL_FRAGMENT_PROGRAM_ARB);
  glGetError   ();
  return 1;
}

static int render_setup_cyl (opengl_driver_t *this) {
  int ret;
  ret  = render_setup_3d       (this);
  ret &= render_help_setup_tex (this);
  glClearColor  (0, .2, .3, 0);
  return ret;
}

#define TOR_TESSELATION_B  128
#define TOR_TESSELATION_S  64
#define TOR_RADIUS_B       2.5
#define TOR_RADIUS_S       1.0

static int render_setup_torus (opengl_driver_t *this) {
  int i, j, k;
  int ret;

  ret  = render_setup_3d       (this);
  ret &= render_help_setup_tex (this);

  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,   GL_MODULATE);
  glTexGeni (GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
  glTexGeni (GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

  /* create display list */
  glNewList (1, GL_COMPILE);
  for (i = 0; i < TOR_TESSELATION_B; i++) {
    glBegin (GL_QUAD_STRIP);
    for (j = 0; j <= TOR_TESSELATION_S; j++) {
      float phi = MY_2PI * j / TOR_TESSELATION_S;
      for (k = 0; k <= 1; k++) {
        float theta = MY_2PI * (i + k) / TOR_TESSELATION_B;
	float nx    = TOR_RADIUS_S * cos(phi) * cos(theta);
	float ny    = TOR_RADIUS_S * cos(phi) * sin(theta);
	float nz    = TOR_RADIUS_S * sin(phi);
	float nnorm = 1.0 / sqrt (nx*nx + ny*ny + nz*nz);
	float x     = (TOR_RADIUS_B + TOR_RADIUS_S * cos(phi)) * cos(theta);
	float y     = (TOR_RADIUS_B + TOR_RADIUS_S * cos(phi)) * sin(theta);
	float z     = TOR_RADIUS_S * sin(phi);
        glNormal3f (nx * nnorm, ny * nnorm, nz * nnorm);
        glVertex3f (x, y, z);
      }
    }
    glEnd   ();
  }
  glEndList ();
  return ret;
}

static int render_setup_fp_yuv (opengl_driver_t *this) {
  GLint errorpos;
  int ret;

  int i = (this->cm_fragprog >> 1) & 7;
  int vr = Inverse_Table_6_9[i][0];
  int ug = Inverse_Table_6_9[i][2];
  int vg = Inverse_Table_6_9[i][3];
  int ub = Inverse_Table_6_9[i][1];
  int ygain, yoffset;
  /* TV set behaviour: contrast affects colour difference as well */
  int saturation = (this->saturation * this->contrast + 64) / 128;

  static char fragprog_yuv[512];
  const char *s = "";

  /* full range mode */
  if (this->cm_fragprog & 1) {
    ygain = (1000 * this->contrast + 64) / 128;
    yoffset = this->brightness * ygain / 255;
    /* be careful to stay within 32 bit */
    vr = (saturation * (112 / 4) * vr + 127 * 16) / (127 * 128 / 4);
    ug = (saturation * (112 / 4) * ug + 127 * 16) / (127 * 128 / 4);
    vg = (saturation * (112 / 4) * vg + 127 * 16) / (127 * 128 / 4);
    ub = (saturation * (112 / 4) * ub + 127 * 16) / (127 * 128 / 4);
  } else {
    ygain = (1000 * 255 * this->contrast + 219 * 64) / (219 * 128);
    yoffset = (this->brightness - 16) * ygain / 255;
    vr = (saturation * vr + 64) / 128;
    ug = (saturation * ug + 64) / 128;
    vg = (saturation * vg + 64) / 128;
    ub = (saturation * ub + 64) / 128;
  }

  vr = 1000 * vr / 65536;
  ug = 1000 * ug / 65536;
  vg = 1000 * vg / 65536;
  ub = 1000 * ub / 65536;

  if (yoffset < 0) {
    s = "-";
    yoffset = -yoffset;
  }

  sprintf (fragprog_yuv,
    "!!ARBfp1.0\n"
    "ATTRIB tex = fragment.texcoord[0];"
    "PARAM  off = program.env[0];"
    "TEMP u, v;"
    "TEMP res, tmp;"
    "ADD u, tex, off.xwww;"
    "TEX res, u, texture[0], 2D;"
    "MUL v, tex, .5;"
    "ADD u, v, off.xyww;"
    "ADD v, v, off.zyww;"
    "TEX tmp.x, u, texture[0], 2D;"
    "MAD res, res, %d.%03d, %s%d.%03d;"
    "TEX tmp.y, v, texture[0], 2D;"
    "SUB tmp, tmp, { .5, .5 };"
    "MAD res, { 0, -%d.%03d, %d.%03d }, tmp.xxxw, res;"
    "MAD result.color, { %d.%03d, -%d.%03d, 0 }, tmp.yyyw, res;"
    "END",
    /* nasty: "%.3f" may use comma as decimal point... */
    ygain / 1000, ygain % 1000,
    s, yoffset / 1000, yoffset % 1000,
    ug / 1000, ug % 1000,
    ub / 1000, ub % 1000,
    vr / 1000, vr % 1000,
    vg / 1000, vg % 1000);

  ret = render_setup_tex2d (this);
  if (! this->has_fragprog)
    return 0;

  xprintf (this->xine, XINE_VERBOSITY_LOG,
    "video_out_open_opengl_fragprog: b %d c %d s %d [%s]\n",
    this->brightness, this->contrast, this->saturation, cm_names[this->cm_fragprog]);

  if (this->fprog == (GLuint)-1)
    this->glGenProgramsARB (1, &this->fprog);
  this->glBindProgramARB   (MYGL_FRAGMENT_PROGRAM_ARB, this->fprog);
  this->glProgramStringARB (MYGL_FRAGMENT_PROGRAM_ARB,
			    MYGL_PROGRAM_FORMAT_ASCII_ARB,
			    strlen (fragprog_yuv), fragprog_yuv);
  glGetIntegerv             (MYGL_PROGRAM_ERROR_POSITION_ARB, &errorpos);
  if (errorpos != -1)
    xprintf (this->xine, XINE_VERBOSITY_NONE,
	     "video_out_opengl: fragprog_yuv errorpos %d beginning with '%.20s'. Ask a wizard.\n",
	     errorpos, fragprog_yuv+errorpos);

  glEnable (MYGL_FRAGMENT_PROGRAM_ARB);
  CHECKERR ("fragprog");
  return ret;
}

/*
 * List of render backends
 */
/* name, display, image,  setup, needsrgb, defaction, fallback, ovl_image, ovl_display */
static const opengl_render_t opengl_rb[] = {
    {   "2D_Tex_Fragprog",  render_tex2d, render_image_fp_yuv,
	render_setup_fp_yuv, 0, RENDER_NONE, 1, render_overlay_image_tex, render_overlay },
    {   "2D_Tex",           render_tex2d, render_image_tex,
	render_setup_tex2d,  1, RENDER_NONE, 2, render_overlay_image_tex, render_overlay },
    {   "2D_Tex_Tiled",     render_tex2dtiled, render_image_tiledtex,
	render_setup_tex2d,  1, RENDER_NONE, 3, render_overlay_image_tex, render_overlay },
    {   "Image_Pipeline",   render_draw, render_image_nop,
	render_setup_2d,     1, RENDER_NONE, -1, render_image_nop, render_overlay },
    {   "Cylinder",         render_cyl, render_image_tex,
	render_setup_cyl,    1, RENDER_DRAW, 1, render_image_nop, render_image_nop },
    {   "Env_Mapped_Torus", render_env_tor, render_image_envtex,
	render_setup_torus,  1, RENDER_DRAW, 1, render_image_nop, render_image_nop }
} ;


/*
 * GFX state management
 */
static void render_gfx_vinfo (opengl_driver_t *this) {
  static int glxAttrib[] = {
    GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
    GLX_DEPTH_SIZE, 1, None, None
  } ;
  if (this->render_double_buffer)
    glxAttrib[9] = GLX_DOUBLEBUFFER;
  else
    glxAttrib[9] = None;
  this->vinfo = glXChooseVisual (this->display, this->screen, glxAttrib);
  CHECKERR ("choosevisual");
}

/*
 * Render thread
 */
static void *render_run (opengl_driver_t *this) {
  int              action, changed;
  int              ret;
  opengl_frame_t  *frame;
  struct timeval   curtime;
  struct timespec  timeout;
  const opengl_render_t *render;

  lprintf ("* render thread created\n");
  while (1) {

    /* Wait for render action */
    pthread_mutex_lock (&this->render_action_mutex);
    if (! this->render_action) {
      this->render_action = opengl_rb[this->render_fun_id].defaction;
      if (this->render_action) {
	/* we have to animate even static images */
	gettimeofday (&curtime, NULL);
	timeout.tv_nsec = 1000 * curtime.tv_usec + 1e9L / this->render_min_fps;
	timeout.tv_sec  = curtime.tv_sec;
	if (timeout.tv_nsec > 1e9L) {
	  timeout.tv_nsec -= 1e9L;
	  timeout.tv_sec  += 1;
	}
	pthread_cond_timedwait (&this->render_action_cond,
				&this->render_action_mutex, &timeout);
      } else {
	pthread_cond_wait (&this->render_action_cond,
			   &this->render_action_mutex);
      }
    }
    action  = this->render_action;
    changed = this->render_frame_changed;
    render  = &opengl_rb[this->render_fun_id];
    /* frame may be updated/deleted outside mutex, but still atomically */
    /* we do not (yet) care to check frames for validity - this is a race.. */
    /* but we do not delete/change frames for at least 4 frames after update */
    frame  = this->frame[0];

    lprintf ("* render action: %d   frame %d   changed %d   drawable %lx  context %lx\n", action, frame ? frame->vo_frame.id : -1, changed, this->drawable, (unsigned long) this->context);
    switch (action) {

    case RENDER_NONE:
      pthread_mutex_unlock (&this->render_action_mutex);
      break;

    case RENDER_DRAW:
      this->render_action = RENDER_NONE;
      this->render_frame_changed = 0;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context && frame) {
	/* update fragprog if color matrix changed */
	if (this->render_fun_id == 0) {
	  int cm = cm_from_frame ((vo_frame_t *)frame);
	  if (cm != this->cm_fragprog) {
	    this->cm_fragprog = cm;
	    this->render_action = RENDER_SETUP;
	    break;
	  }
	}
	XLockDisplay (this->display);
	CHECKERR ("pre-render");
	ret = 1;
	if (changed)
	  if (this->argb_layer.changed) // clean window after every overlay change - do it twice because of double buffering
	  {
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      if (this->argb_layer.changed == 1) 
        this->argb_layer.changed++;
      else this->argb_layer.changed = 0;
    }
	  ret = (render->image) (this, frame);
    (render->display) (this, frame);
    // display overlay
    pthread_mutex_lock (&this->argb_layer.mutex);
    if (this->argb_layer.buffer)
    {
      ret = (render->ovl_image) (this, frame);
      (render->ovl_display) (this, frame);
    }
    pthread_mutex_unlock (&this->argb_layer.mutex);
    if (this->render_double_buffer)
      glXSwapBuffers(this->display, this->drawable);
    else
      glFlush ();
	/* Note: no glFinish() - work concurrently to the graphics pipe */
	CHECKERR ("post-render");
	XUnlockDisplay (this->display);
	if (! ret) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
	           "video_out_opengl: rendering '%s' failed, switching to fallback\n",
	           render->name);
	  if (render->fallback != -1 && this->gl_exts)
	    this->config->update_num (this->config, "video.output.opengl_renderer", render->fallback);
	}
      }
      break;

    case RENDER_CLEAN:
      this->render_action = RENDER_DRAW;
      this->render_frame_changed = 0;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context && frame) {
	XLockDisplay (this->display);
	CHECKERR ("pre-clean");
	ret = 1;
	if (changed)
	  ret = (render->image) (this, frame);
	if (this->render_double_buffer) {
	  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
		   | GL_STENCIL_BUFFER_BIT);
	  (render->display) (this, frame);
	  glXSwapBuffers(this->display, this->drawable);
	}
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
		 | GL_STENCIL_BUFFER_BIT);
	glFlush ();
	CHECKERR ("post-clean");
	XUnlockDisplay (this->display);
	if (! ret) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
	           "video_out_opengl: rendering '%s' failed, switching to fallback\n",
	           render->name);
	  if (render->fallback != -1 && this->gl_exts)
	    this->config->update_num (this->config, "video.output.opengl_renderer", render->fallback);
	}
      }
      break;

    case RENDER_SETUP:
      this->render_action = RENDER_CLEAN;
      this->render_frame_changed = 1;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context) {
	XLockDisplay (this->display);
	xprintf (this->xine, XINE_VERBOSITY_NONE,
	         "video_out_opengl: setup of '%s'\n", render->name);
	if (! (render->setup) (this)) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
	           "video_out_opengl: setup of '%s' failed, switching to fallback\n",
	           render->name);
	  if (render->fallback != -1 && this->gl_exts)
	    this->config->update_num (this->config, "video.output.opengl_renderer", render->fallback);
	}
	XUnlockDisplay (this->display);
	this->tex_width = this->tex_height = 0;
	this->tex_ovl_width = this->tex_ovl_height = 0;
      }
      break;

    case RENDER_CREATE:
      this->render_action = RENDER_SETUP;
      this->gl_exts       = NULL;
      _x_assert (this->vinfo);
      _x_assert (! this->context);
      XLockDisplay (this->display);
      glXMakeCurrent    (this->display, None, NULL);
      this->context = glXCreateContext (this->display, this->vinfo, NULL, True);
      if (this->context) {
        glXMakeCurrent (this->display, this->drawable, this->context);
        CHECKERR ("create+makecurrent");
      }
      XUnlockDisplay (this->display);
      pthread_cond_signal  (&this->render_return_cond);
      pthread_mutex_unlock (&this->render_action_mutex);
      break;

    case RENDER_VISUAL:
      this->render_action = RENDER_NONE;
      XLockDisplay (this->display);
      render_gfx_vinfo (this);
      XUnlockDisplay (this->display);
      if (this->vinfo == NULL)
	xprintf (this->xine, XINE_VERBOSITY_NONE,
		 "video_out_opengl: no OpenGL support available (glXChooseVisual)\n");
      else
        lprintf ("* visual %p id %lx depth %d\n", this->vinfo->visual,
		 this->vinfo->visualid, this->vinfo->depth);
      pthread_cond_signal  (&this->render_return_cond);
      pthread_mutex_unlock (&this->render_action_mutex);
      break;

    case RENDER_RELEASE:
      this->render_action = RENDER_NONE;
      if (this->context) {
	XLockDisplay (this->display);
	glXMakeCurrent    (this->display, None, NULL);
	glXDestroyContext (this->display, this->context);
	CHECKERR ("release");
	XUnlockDisplay (this->display);
	this->context = NULL;
      }
      pthread_cond_signal  (&this->render_return_cond);
      pthread_mutex_unlock (&this->render_action_mutex);
      break;

    case RENDER_EXIT:
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context) {
	XLockDisplay (this->display);
	glXMakeCurrent    (this->display, None, NULL);
	glXDestroyContext (this->display, this->context);
	CHECKERR ("exit");
	XUnlockDisplay (this->display);
      }
      pthread_exit      (NULL);
      break;

    default:
      this->render_action = RENDER_NONE;
      pthread_mutex_unlock (&this->render_action_mutex);
      _x_assert (!action);		/* unknown action */
    }
    lprintf ("* render action: %d   frame %d   done\n", action,
	     frame ? frame->vo_frame.id : -1);
  }
  /* NOTREACHED */
  return NULL;
}


/*
 * and now, the driver functions
 */

static uint32_t opengl_get_capabilities (vo_driver_t *this_gen) {
/*   opengl_driver_t *this = (opengl_driver_t *) this_gen; */
  uint32_t capabilities = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS
    | VO_CAP_CONTRAST | VO_CAP_SATURATION | VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE;

  /* TODO: somehow performance goes down during the first few frames */
/*   if (this->xoverlay) */
/*     capabilities |= VO_CAP_UNSCALED_OVERLAY; */

  return capabilities;
}

static void opengl_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
  opengl_driver_t *this = (opengl_driver_t *) vo_img->driver;
  int cm;

  vo_img->proc_called = 1;
  if (! frame->rgb_dst)
      return;

/*   lprintf ("%p: frame_copy src %p=%p to %p\n", frame, src[0], frame->chunk[0], frame->rgb_dst); */

  if( frame->vo_frame.crop_left || frame->vo_frame.crop_top ||
      frame->vo_frame.crop_right || frame->vo_frame.crop_bottom )
  {
    /* TODO: opengl *could* support this?!? */
    /* cropping will be performed by video_out.c */
    return;
  }

  cm = cm_from_frame (vo_img);
  if (cm != this->cm_yuv2rgb) {
    this->cm_yuv2rgb = cm;
    this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
      this->brightness, this->contrast, this->saturation, cm);
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "video_out_opengl: b %d c %d s %d [%s]\n",
      this->brightness, this->contrast, this->saturation, cm_names[cm]);
  }

  if (frame->format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				 src[0], src[1], src[2]);
  else
    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				  src[0]);

/*   lprintf ("frame_copy...done\n"); */
}

static void opengl_frame_field (vo_frame_t *vo_img, int which_field) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
  opengl_driver_t *this = (opengl_driver_t *) vo_img->driver;

/*   lprintf ("%p: frame_field rgb %p which_field %x\n", frame, frame->rgb, which_field); */

  if (! opengl_rb[this->render_fun_id].needsrgb) {
    frame->rgb_dst = NULL;
    return;
  }

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb + frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  }

  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
/*   lprintf ("frame_field...done\n"); */
}

static void opengl_frame_dispose (vo_frame_t *vo_img) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);
  av_free (frame->rgb);
  free (frame);
}


static vo_frame_t *opengl_alloc_frame (vo_driver_t *this_gen) {
  opengl_frame_t  *frame;
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  frame = (opengl_frame_t *) calloc(1, sizeof(opengl_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_slice = opengl_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = opengl_frame_field;
  frame->vo_frame.dispose    = opengl_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  /*
   * colorspace converter for this frame
   */
  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

  return (vo_frame_t *) frame;
}

static void opengl_compute_ideal_size (opengl_driver_t *this) {
  _x_vo_scale_compute_ideal_size( &this->sc );
}

static void opengl_compute_rgb_size (opengl_driver_t *this) {
  _x_vo_scale_compute_output_size( &this->sc );
}

static void opengl_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      double ratio, int format, int flags) {
  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;
  int     g_width, g_height;
  double  g_pixel_aspect;

  /* Check output size to signal render thread output size changes */
  this->sc.dest_size_cb (this->sc.user_data, width, height,
			 this->sc.video_pixel_aspect, &g_width, &g_height,
			 &g_pixel_aspect);
/*   lprintf ("update_frame_format %dx%d output %dx%d\n", width, height, g_width, g_height); */

  if (g_width != this->gui_width || g_height != this->gui_height) {
      this->gui_width  = g_width;
      this->gui_height = g_height;
      pthread_mutex_lock (&this->render_action_mutex);
      if (this->render_action <= RENDER_SETUP) {
	  this->render_action = RENDER_SETUP;
	  pthread_cond_signal  (&this->render_action_cond);
      }
      pthread_mutex_unlock (&this->render_action_mutex);
  }

  /* Check frame size and format and reallocate if necessary */
  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)
      || (frame->flags  != flags)) {
/*     lprintf ("updating frame to %d x %d (ratio=%g, format=%08x)\n", width, height, ratio, format); */

    flags &= VO_BOTH_FIELDS;

    XLockDisplay (this->display);

    /* (re-) allocate render space */
    av_freep(&frame->vo_frame.base[0]);
    av_freep(&frame->vo_frame.base[1]);
    av_freep(&frame->vo_frame.base[2]);
    av_freep(&frame->rgb);

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz(frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz(frame->vo_frame.pitches[2] * ((height+1)/2));
    } else {
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz(frame->vo_frame.pitches[0] * height);
    }
    frame->rgb = av_mallocz(BYTES_PER_PIXEL*width*height);

    /* set up colorspace converter */
    switch (flags) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 2*frame->vo_frame.pitches[0],
				 2*frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width * 2);
      break;
    case VO_BOTH_FIELDS:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 frame->vo_frame.pitches[0],
				 frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width);
      break;
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;

    XUnlockDisplay (this->display);

    opengl_frame_field ((vo_frame_t *)frame, flags);
  }

  frame->ratio = ratio;
/*   lprintf ("done...update_frame_format\n"); */
}


static void opengl_overlay_clut_yuv2rgb(opengl_driver_t  *this, vo_overlay_t *overlay,
				      opengl_frame_t *frame) {
  int     i;
  clut_t* clut = (clut_t*) overlay->color;

  if (!overlay->rgb_clut) {
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, clut[i].y,
						  clut[i].cb, clut[i].cr);
    }
    overlay->rgb_clut++;
  }
  if (!overlay->hili_rgb_clut) {
    clut = (clut_t*) overlay->hili_color;
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun(frame->yuv2rgb, clut[i].y,
						 clut[i].cb, clut[i].cr);
    }
    overlay->hili_rgb_clut++;
  }
}

static void opengl_overlay_begin (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, int changed) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;

  this->ovl_changed += changed;

  if (this->ovl_changed && this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_clear(this->xoverlay);
    XUnlockDisplay (this->display);
  }

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void opengl_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;

  if (this->ovl_changed && this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_expose(this->xoverlay);
    XUnlockDisplay (this->display);
  }

  this->ovl_changed = 0;
}

static void opengl_overlay_blend (vo_driver_t *this_gen,
				vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

  if (overlay->width <= 0 || overlay->height <= 0 || (!overlay->rle && (!overlay->argb_layer || !overlay->argb_layer->buffer)))
    return;

  /* Alpha Blend here */
  if (overlay->rle) {
    if (overlay->unscaled) {
      if (this->ovl_changed && this->xoverlay) {
        XLockDisplay (this->display);
        x11osd_blend (this->xoverlay, overlay);
        XUnlockDisplay (this->display);
      }
    } else {

      if (!frame->rgb_dst) {
        if (frame->format == XINE_IMGFMT_YV12) {
          _x_blend_yuv(frame->vo_frame.base, overlay,
                       frame->width, frame->height, frame->vo_frame.pitches,
                       &this->alphablend_extra_data);
        } else {
          _x_blend_yuy2(frame->vo_frame.base[0], overlay,
                        frame->width, frame->height, frame->vo_frame.pitches[0],
                        &this->alphablend_extra_data);
        }
        return;
      }

      if (!overlay->rgb_clut || !overlay->hili_rgb_clut)
        opengl_overlay_clut_yuv2rgb (this, overlay, frame);

#     if BYTES_PER_PIXEL == 3
      _x_blend_rgb24 ((uint8_t *)frame->rgb, overlay,
		   frame->width, frame->height,
		   frame->width, frame->height,
                   &this->alphablend_extra_data);
#     elif BYTES_PER_PIXEL == 4
      _x_blend_rgb32 ((uint8_t *)frame->rgb, overlay,
		   frame->width, frame->height,
		   frame->width, frame->height,
                   &this->alphablend_extra_data);
#     else
#       error "bad BYTES_PER_PIXEL"
#     endif
    }
  }
  else if (overlay && overlay->argb_layer && overlay->argb_layer->buffer && this->ovl_changed)
  { 
    // copy argb_buffer because it gets invalid after overlay_end and rendering is after overlay_end
    pthread_mutex_lock (&this->argb_layer.mutex);
    if (this->argb_layer.buffer)
      free(this->argb_layer.buffer);
    this->argb_layer.buffer = calloc(overlay->extent_width * overlay->extent_height, sizeof(uint32_t));
    if (this->argb_layer.buffer == NULL)
    {
      printf("Fatal error(opengl_overlay_blend): No memory\n");
      return;
    }
    this->argb_layer.width  = overlay->extent_width;
    this->argb_layer.height = overlay->extent_height;
    this->argb_layer.changed= 1;
    xine_fast_memcpy(this->argb_layer.buffer, overlay->argb_layer->buffer, overlay->extent_width * overlay->extent_height * sizeof(uint32_t));
    pthread_mutex_unlock (&this->argb_layer.mutex);
    this->video_window_width  = overlay->video_window_width;
    this->video_window_height = overlay->video_window_height;
    this->video_window_x      = overlay->video_window_x;
    this->video_window_y      = overlay->video_window_y;
  }
}

static int opengl_redraw_needed (vo_driver_t *this_gen) {
  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  int             ret = 0;

/*   lprintf ("redraw_needed\n"); */
  if (this->frame[0]) {
    this->sc.delivered_height   = this->frame[0]->height;
    this->sc.delivered_width    = this->frame[0]->width;
    this->sc.delivered_ratio    = this->frame[0]->ratio;

    this->sc.crop_left        = this->frame[0]->vo_frame.crop_left;
    this->sc.crop_right       = this->frame[0]->vo_frame.crop_right;
    this->sc.crop_top         = this->frame[0]->vo_frame.crop_top;
    this->sc.crop_bottom      = this->frame[0]->vo_frame.crop_bottom;

    opengl_compute_ideal_size(this);

    if( _x_vo_scale_redraw_needed( &this->sc ) ) {
      opengl_compute_rgb_size(this);
      pthread_mutex_lock (&this->render_action_mutex);
      if (this->render_action <= RENDER_CLEAN) {
	  this->render_action = RENDER_CLEAN;
	  pthread_cond_signal  (&this->render_action_cond);
      }
      pthread_mutex_unlock (&this->render_action_mutex);
      ret = 1;
    }
  }
  else
    ret = 1;

/*   lprintf ("done...redraw_needed: %d\n", ret); */
  return ret;
}

static void opengl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;
  int i;

/*   lprintf ("about to draw frame (%d) %d x %d...\n", frame->vo_frame.id, frame->width, frame->height); */

/*   lprintf ("video_out_opengl: freeing frame %d\n", this->frame[NUM_FRAMES_BACKLOG-1] ? this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame.id : -1); */
  if (this->frame[NUM_FRAMES_BACKLOG-1])
    this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame.free (&this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame);
  for (i = NUM_FRAMES_BACKLOG-1; i > 0; i--)
    this->frame[i] = this->frame[i-1];
  this->frame[0] = frame;
  this->render_frame_changed = 1;
/*   lprintf ("video_out_opengl: cur_frame updated to %d\n", frame->vo_frame.id); */

  /*
   * let's see if this frame is different in size / aspect
   * ratio from the previous one
   */
  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio) ) {
/*     lprintf("frame format changed\n"); */
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  opengl_redraw_needed (this_gen);

  pthread_mutex_lock (&this->render_action_mutex);
  if (this->render_action <= RENDER_DRAW) {
      this->render_action = RENDER_DRAW;
      pthread_cond_signal  (&this->render_action_cond);
  }
  pthread_mutex_unlock (&this->render_action_mutex);

/*   lprintf ("display frame done\n"); */
}

static int opengl_get_property (vo_driver_t *this_gen, int property) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    return this->sc.user_ratio;
  case VO_PROP_MAX_NUM_FRAMES:
    return 15;
  case VO_PROP_BRIGHTNESS:
    return this->brightness;
  case VO_PROP_CONTRAST:
    return this->contrast;
  case VO_PROP_SATURATION:
    return this->saturation;
  case VO_PROP_WINDOW_WIDTH:
    return this->sc.gui_width;
  case VO_PROP_WINDOW_HEIGHT:
    return this->sc.gui_height;
  default:
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_opengl: tried to get unsupported property %d\n", property);
  }

  return 0;
}

static int opengl_set_property (vo_driver_t *this_gen,
			      int property, int value) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    if (value>=XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;
    this->sc.user_ratio = value;
    opengl_compute_ideal_size (this);
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */

    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_opengl: aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name_table[value]);
    break;
  case VO_PROP_BRIGHTNESS:
    this->brightness = value;
    this->cm_yuv2rgb = 0;
    this->cm_fragprog = 0;
    this->sc.force_redraw = 1;
    break;
  case VO_PROP_CONTRAST:
    this->contrast = value;
    this->cm_yuv2rgb = 0;
    this->cm_fragprog = 0;
    this->sc.force_redraw = 1;
    break;
  case VO_PROP_SATURATION:
    this->saturation = value;
    this->cm_yuv2rgb = 0;
    this->cm_fragprog = 0;
    this->sc.force_redraw = 1;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     "video_out_opengl: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void opengl_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  /* opengl_driver_t *this = (opengl_driver_t *) this_gen;  */

  switch (property) {
  case VO_PROP_BRIGHTNESS:
    *min = -128;    *max = 127;     break;
  case VO_PROP_CONTRAST:
    *min = 0;       *max = 255;     break;
  case VO_PROP_SATURATION:
    *min = 0;       *max = 255;     break;
  default:
    *min = 0;       *max = 0;
  }
}

static int opengl_gui_data_exchange (vo_driver_t *this_gen,
				   int data_type, void *data) {
  opengl_driver_t   *this = (opengl_driver_t *) this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT:

    /*     lprintf ("expose event\n"); */
    if (this->frame[0]) {
      XExposeEvent * xev = (XExposeEvent *) data;

      if (xev && xev->count == 0) {
	pthread_mutex_lock (&this->render_action_mutex);
	if (this->render_action <= RENDER_CLEAN) {
	    this->render_action = RENDER_CLEAN;
	    pthread_cond_signal  (&this->render_action_cond);
	}
	pthread_mutex_unlock (&this->render_action_mutex);
	XLockDisplay (this->display);
        if(this->xoverlay)
          x11osd_expose(this->xoverlay);
	XSync(this->display, False);
	XUnlockDisplay (this->display);
      }
    }
    break;

  case XINE_GUI_SEND_SELECT_VISUAL:

    pthread_mutex_lock   (&this->render_action_mutex);
    this->render_action = RENDER_VISUAL;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    pthread_mutex_unlock (&this->render_action_mutex);
    *(XVisualInfo**)data = this->vinfo;
    break;

  case XINE_GUI_SEND_WILL_DESTROY_DRAWABLE:

    pthread_mutex_lock   (&this->render_action_mutex);
    this->render_action = RENDER_RELEASE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    pthread_mutex_unlock (&this->render_action_mutex);
    break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:

    pthread_mutex_lock   (&this->render_action_mutex);
    this->render_action = RENDER_RELEASE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    this->drawable      = (Drawable) data;
    this->render_action = RENDER_CREATE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    pthread_mutex_unlock (&this->render_action_mutex);
    if (! this->context)
      xprintf (this->xine, XINE_VERBOSITY_NONE,
	       "video_out_opengl: cannot create OpenGL capable visual.\n"
	       "   plugin will not work.\n");
    XLockDisplay (this->display);
    if(this->xoverlay)
      x11osd_drawable_changed(this->xoverlay, this->drawable);
    this->ovl_changed = 1;
    XUnlockDisplay (this->display);
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:

    if (this->frame[0]) {
      x11_rectangle_t *rect = data;
      int              x1, y1, x2, y2;

      _x_vo_scale_translate_gui2video(&this->sc,
			       rect->x, rect->y,
			       &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc,
			       rect->x + rect->w, rect->y + rect->h,
			       &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
    break;

  default:
    return -1;
  }

  return 0;
}

static void opengl_dispose (vo_driver_t *this_gen) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;
  int i;

  pthread_mutex_lock    (&this->render_action_mutex);
  this->render_action = RENDER_EXIT;
  pthread_cond_signal   (&this->render_action_cond);
  pthread_mutex_unlock  (&this->render_action_mutex);
  pthread_join          (this->render_thread, NULL);
  pthread_mutex_destroy (&this->render_action_mutex);
  pthread_cond_destroy  (&this->render_action_cond);
  pthread_cond_destroy  (&this->render_return_cond);

  for (i = 0; i < NUM_FRAMES_BACKLOG; i++)
    if (this->frame[i])
      this->frame[i]->vo_frame.dispose (&this->frame[i]->vo_frame);

  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  cm_close (this);

  if (this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_destroy (this->xoverlay);
    XUnlockDisplay (this->display);
  }

  pthread_mutex_lock (&this->argb_layer.mutex);
  if (this->argb_layer.buffer)
	free(this->argb_layer.buffer);
  pthread_mutex_unlock (&this->argb_layer.mutex);

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

static void opengl_cb_render_fun (void *this_gen, xine_cfg_entry_t *entry) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;
  pthread_mutex_lock (&this->render_action_mutex);
  this->render_fun_id = entry->num_value;
  if (this->render_action <= RENDER_SETUP) {
    this->render_action = RENDER_SETUP;
    pthread_cond_signal  (&this->render_action_cond);
  }
  pthread_mutex_unlock (&this->render_action_mutex);
}

static void opengl_cb_default (void *val_gen, xine_cfg_entry_t *entry) {
  int *val = (int *) val_gen;
  *val = entry->num_value;
}

static vo_driver_t *opengl_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  opengl_class_t       *class   = (opengl_class_t *) class_gen;
  config_values_t      *config  = class->xine->config;
  x11_visual_t         *visual  = (x11_visual_t *) visual_gen;
  opengl_driver_t      *this;
  const char          **render_fun_names;
  int                   i;

  this = (opengl_driver_t *) calloc(1, sizeof(opengl_driver_t));

  if (!this)
    return NULL;

  this->display		    = visual->display;
  this->screen		    = visual->screen;

  _x_vo_scale_init (&this->sc, 0, 0, config);
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  _x_alphablend_init (&this->alphablend_extra_data, class->xine);

  this->drawable	    = visual->d;
  this->gui_width  = this->gui_height  = -1;
  this->last_width = this->last_height = -1;
  this->fprog = -1;

  this->xoverlay                = NULL;
  this->argb_layer.buffer       = NULL;
  this->argb_layer.width        = 0;
  this->argb_layer.height       = 0;
  this->argb_layer.changed      = 0;
  this->ovl_changed             = 0;
  this->last_ovl_width = this->last_ovl_height = -1;
  this->video_window_width      = 0;
  this->video_window_height     = 0;
  this->video_window_x          = 0;
  this->video_window_y          = 0;
  
  this->xine                    = class->xine;
  this->config                  = config;

  this->vo_driver.get_capabilities     = opengl_get_capabilities;
  this->vo_driver.alloc_frame          = opengl_alloc_frame;
  this->vo_driver.update_frame_format  = opengl_update_frame_format;
  this->vo_driver.overlay_begin        = opengl_overlay_begin;
  this->vo_driver.overlay_blend        = opengl_overlay_blend;
  this->vo_driver.overlay_end          = opengl_overlay_end;
  this->vo_driver.display_frame        = opengl_display_frame;
  this->vo_driver.get_property         = opengl_get_property;
  this->vo_driver.set_property         = opengl_set_property;
  this->vo_driver.get_property_min_max = opengl_get_property_min_max;
  this->vo_driver.gui_data_exchange    = opengl_gui_data_exchange;
  this->vo_driver.dispose              = opengl_dispose;
  this->vo_driver.redraw_needed        = opengl_redraw_needed;

  this->brightness = 0;
  this->contrast   = 128;
  this->saturation = 128;

  cm_init (this);

  this->yuv2rgb_factory = yuv2rgb_factory_init (YUV_FORMAT, YUV_SWAP_MODE, NULL);

  XLockDisplay (this->display);
  this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                  this->drawable, X11OSD_SHAPED);
  XUnlockDisplay (this->display);

  render_fun_names = calloc((sizeof(opengl_rb)/sizeof(opengl_render_t)+1), sizeof(const char*));
  for (i = 0; i < sizeof (opengl_rb) / sizeof (opengl_render_t); i++)
    render_fun_names[i] = opengl_rb[i].name;
  render_fun_names[i] = NULL;
  this->render_fun_id = config->register_enum (config, "video.output.opengl_renderer",
					       0, render_fun_names,
					       _("OpenGL renderer"),
					       _("The OpenGL plugin provides several render modules:\n\n"
						 "2D_Tex_Fragprog\n"
						 "This module downloads the images as YUV 2D textures and renders a textured slice\n"
						 "using fragment programs for reconstructing RGB.\n"
						 "This is the best and fastest method on modern graphics cards.\n\n"
						 "2D_Tex\n"
						 "This module downloads the images as 2D textures and renders a textured slice.\n"
						 "2D_Tex_Tiled\n"
						 "This module downloads the images as multiple 2D textures and renders a textured\n"
						 "slice. Thus this works with smaller maximum texture sizes as well.\n"
						 "Image_Pipeline\n"
						 "This module uses glDraw() to render the images.\n"
						 "Only accelerated on few drivers.\n"
						 "Does not interpolate on scaling.\n\n"
						 "Cylinder\n"
						 "Shows images on a rotating cylinder. Nice effect :)\n\n"
						 "Environment_Mapped_Torus\n"
						 "Show images reflected in a spinning torus. Way cool =)"),
					       10, opengl_cb_render_fun, this);
  this->render_min_fps = config->register_range (config,
						 "video.output.opengl_min_fps",
						 20, 1, 120,
						 _("OpenGL minimum framerate"),
						 _("Minimum framerate for animated render routines.\n"
						   "Ignored for static render routines.\n"),
						 20, opengl_cb_default,
						 &this->render_min_fps);
  this->render_double_buffer = config->register_bool (config, "video.device.opengl_double_buffer", 1,
						      _("enable double buffering"),
						      _("For OpenGL double buffering does not only remove tearing artifacts,\n"
							"it also reduces flickering a lot.\n"
							"It should not have any performance impact."),
						      20, NULL, NULL);

  pthread_mutex_init (&this->render_action_mutex, NULL);
  pthread_cond_init  (&this->render_action_cond, NULL);
  pthread_cond_init  (&this->render_return_cond, NULL);
  pthread_create (&this->render_thread, NULL, (thread_run_t) render_run, this);

  /* Check for OpenGL capable visual */
  pthread_mutex_lock   (&this->render_action_mutex);
  this->render_action = RENDER_VISUAL;
  pthread_cond_signal  (&this->render_action_cond);
  pthread_cond_wait    (&this->render_return_cond,
			&this->render_action_mutex);
  if (this->vinfo) {
    /* Create context if possible w/o drawable change */
    this->render_action = RENDER_CREATE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
  }
  pthread_mutex_unlock (&this->render_action_mutex);

  if (! this->vinfo) {
    /* no OpenGL capable visual available */
    opengl_dispose (&this->vo_driver);
    return NULL;
  }
  if (! this->context)
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "video_out_opengl: default visual not OpenGL capable\n"
	     "   plugin will only work with clients supporting XINE_GUI_SEND_SELECT_VISUAL.\n");

  return &this->vo_driver;
}

/*
 * class functions
 */

static int opengl_verify_direct (x11_visual_t *vis) {
  int attribs[] = {
    GLX_RGBA,
    GLX_RED_SIZE, 1,
    GLX_GREEN_SIZE, 1,
    GLX_BLUE_SIZE, 1,
    None
  };
  Window        root, win;
  XVisualInfo  *visinfo;
  GLXContext    ctx;
  XSetWindowAttributes xattr;
  int           ret = 0;

  if (!vis || !vis->display ||
      ! (root = RootWindow (vis->display, vis->screen))) {
      fprintf (stderr, "[videoout_opengl]: Don't have a root window to verify\n");
      return 0;
  }
  if (! (visinfo = glXChooseVisual (vis->display, vis->screen, attribs)))
      return 0;
  if (! (ctx = glXCreateContext (vis->display, visinfo, NULL, 1)))
      return 0;
  memset (&xattr, 0, sizeof (xattr));
  xattr.colormap = XCreateColormap(vis->display, root, visinfo->visual, AllocNone);
  xattr.event_mask = StructureNotifyMask | ExposureMask;
  if ( (win = XCreateWindow (vis->display, root, 0, 0, 1, 1, 0, visinfo->depth,
			     InputOutput, visinfo->visual,
			     CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
			     &xattr))) {
      if (glXMakeCurrent (vis->display, win, ctx)) {
	  const char *renderer = (const char *) glGetString(GL_RENDERER);
	  if (glXIsDirect (vis->display, ctx) &&
	      ! strstr (renderer, "Software") &&
	      ! strstr (renderer, "Indirect"))
	      ret = 1;
	  glXMakeCurrent (vis->display, None, NULL);
      }
      XDestroyWindow (vis->display, win);
  }
  glXDestroyContext (vis->display, ctx);
  XFreeColormap     (vis->display, xattr.colormap);

  return ret;
}

static void *opengl_init_class (xine_t *xine, void *visual_gen) {

  opengl_class_t *this;

  xprintf (xine, XINE_VERBOSITY_LOG,
	   "video_out_opengl: Testing for hardware accelerated direct rendering visual\n");
  if (! opengl_verify_direct ((x11_visual_t *)visual_gen)) {
      xprintf (xine, XINE_VERBOSITY_LOG,
	       "video_out_opengl: Didn't find any\n");
      return NULL;
  }

  this = (opengl_class_t *) calloc (1, sizeof(opengl_class_t));

  this->driver_class.open_plugin     = opengl_open_plugin;
  this->driver_class.identifier      = "opengl";
  this->driver_class.description     = N_("xine video output plugin using the OpenGL 3D graphics API");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;

  return this;
}


static const vo_info_t vo_info_opengl = {
  7,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "opengl", XINE_VERSION_CODE, &vo_info_opengl, opengl_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
