/*
 * Copyright (C) 2003 by Dirk Meyer
 *
 * This file is part of xine, a unix video player.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * Modified for xineliboutput by Petri Hintukainen, 2006
 *
 */

#ifndef POST_HH
#define POST_HH

#include <xine.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/plugin_catalog.h>

typedef struct {
  xine_post_t    *post;
  char           *name;
  char           *args;
  int            enable; /* 0 - disabled, 1 - enabled, 2 - can't disable */
} post_element_t;


typedef struct post_plugins_s post_plugins_t;

struct post_plugins_s {

  /* frontend data */
  char *static_post_plugins;   /* post plugins from command line; always on */
  xine_stream_t *video_source; /* stream to take video from */
  xine_stream_t *audio_source; /* stream to take audio from */
  xine_stream_t *pip_stream;   /* pip stream */

  /* xine */
  xine_t            *xine;
  xine_video_port_t *video_port;
  xine_audio_port_t *audio_port;

  /* post.c internal use */
  int post_audio_elements_num;
  int post_video_elements_num;
  int post_vis_elements_num;
  int post_pip_elements_num;

  post_element_t **post_audio_elements;
  post_element_t **post_video_elements;
  post_element_t **post_vis_elements;   /* supports only one */
  post_element_t **post_pip_elements;   /* supports only one and two input */

  int post_audio_enable;
  int post_video_enable;
  int post_vis_enable;
  int post_pip_enable;
};


void vpplugin_rewire_posts(post_plugins_t *fe);
void applugin_rewire_posts(post_plugins_t *fe);

/* load and config post plugin(s) */
/* post == "plugin:arg1=val1,arg2=val2;plugin2..." */
void vpplugin_parse_and_store_post(post_plugins_t *fe, const char *post);
void applugin_parse_and_store_post(post_plugins_t *fe, const char *post);

/* enable (and load if not loaded), but don't rewire */
/* result indicates only unwiring condition, not enable result */
/* -> if result <> 0, something was enabled and post chain is unwired */
int  vpplugin_enable_post(post_plugins_t *fe, const char *name, int *found);
int  applugin_enable_post(post_plugins_t *fe, const char *name, int *found);

/* disable (and unwire if found), but don't unload */
/* result indicates only unwiring condition, not disable result */
int  vpplugin_disable_post(post_plugins_t *fe, const char *name);
int  applugin_disable_post(post_plugins_t *fe, const char *name);

/* unload (and unwire) plugin(s) */
/* result indicates only unwiring condition, not unload result */
int  vpplugin_unload_post(post_plugins_t *fe, const char *name);
int  applugin_unload_post(post_plugins_t *fe, const char *name);

#endif

/* end of post.h */
