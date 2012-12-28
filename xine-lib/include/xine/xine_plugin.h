/*
 * Copyright (C) 2000-2003 the xine project
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
 * generic plugin definitions
 */

#ifndef XINE_PLUGIN_H
#define XINE_PLUGIN_H

#define PLUGIN_NONE	      0
#define PLUGIN_INPUT	      1
#define PLUGIN_DEMUX	      2
#define PLUGIN_AUDIO_DECODER  3
#define PLUGIN_VIDEO_DECODER  4
#define PLUGIN_SPU_DECODER    5
#define PLUGIN_AUDIO_OUT      6
#define PLUGIN_VIDEO_OUT      7
#define PLUGIN_POST           8

#define PLUGIN_TYPE_MAX       PLUGIN_POST

/* this flag may be or'ed with type in order to force preloading the plugin.
 * very useful to register config items on xine initialization.
 */
#define PLUGIN_MUST_PRELOAD   (1 << 7)

/* this flag may be or'ed with type to prevent the plugin loader from unloading
 * the plugin
 */
#define PLUGIN_NO_UNLOAD      (1 << 6)

#define PLUGIN_TYPE_MASK      ((1 << 6) - 1)

typedef struct {
  uint8_t                  type;                    /* one of the PLUGIN_* constants above     */
  uint8_t                  API;                     /* API version supported by this plugin    */
  const char              *id;                      /* a name that identifies this plugin      */
  uint32_t                 version;                 /* version number, increased every release */
  const void              *special_info;            /* plugin-type specific, see structs below */
  void                  *(*init)(xine_t *, void *); /* init the plugin class            */
} plugin_info_t;


/* special_info for a video output plugin */
typedef struct {
  int                      priority;                /* priority of this plugin for auto-probing  */
  int                      visual_type;             /* visual type supported by this plugin      */
} vo_info_t;

/* special info for a audio output plugin */
typedef struct {
  int                      priority;
} ao_info_t;

/* special_info for a decoder plugin */
typedef struct {
  const uint32_t          *supported_types;         /* streamtypes this decoder can handle       */
  int                      priority;
} decoder_info_t;

/* special info for a post plugin */
typedef struct {
  uint32_t                 type;                    /* type of the post plugin, use one of XINE_POST_TYPE_* */
} post_info_t;

/* special info for a demuxer plugin */
typedef struct {
  int                      priority;
} demuxer_info_t;

/* special info for an input plugin */
typedef struct {
  int                      priority;
} input_info_t;


/* register a list of statically linked plugins
 * info is a list of plugin_info_t terminated by PLUGIN_NONE
 * example:
 *   plugin_info_t acme_plugin_info[] = {
 *     { PLUGIN_VIDEO_OUT, 21, "acme", XINE_VERSION_CODE, &vo_info_acme,
 *   init_class_acme },
 *     { PLUGIN_NONE, 0, "", 0, NULL, NULL }
 *   };
 *
 */
void xine_register_plugins(xine_t *self, plugin_info_t *info) XINE_PROTECTED;

#endif
