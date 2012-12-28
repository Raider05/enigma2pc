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
 * closed caption spu decoder. receive data by events.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xine/buffer.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "cc_decoder.h"

/*
#define LOG_DEBUG 1
*/

static const char *const cc_schemes[NUM_CC_PALETTES + 1] = {
  "White/Gray/Translucent",
  "White/Black/Solid",
  NULL
};

typedef struct spucc_decoder_s {
  spu_decoder_t      spu_decoder;

  xine_stream_t     *stream;

  /* closed captioning decoder state */
  cc_decoder_t *ccdec;
  /* true if ccdec has been initialized */
  int cc_open;

  /* closed captioning decoder configuration and intrinsics */
  cc_state_t cc_state;
  /* this is to detect configuration changes */
  int        config_version;

  /* video dimensions captured in frame change events */
  int video_width;
  int video_height;

  /* events will be sent here */
  xine_event_queue_t *queue;

} spucc_decoder_t;


/*------------------- general utility functions ----------------------------*/

static void copy_str(char *d, const char *s, size_t maxbytes)
{
  strncpy(d, s, maxbytes - 1);
  d[maxbytes - 1] = '\0';
}


/*------------------- private methods --------------------------------------*/

static void spucc_update_intrinsics(spucc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("spucc: update_intrinsics\n");
#endif

  if (this->cc_open)
    cc_renderer_update_cfg(this->cc_state.renderer, this->video_width,
			   this->video_height);
}

static void spucc_do_close(spucc_decoder_t *this)
{
  if (this->cc_open) {
#ifdef LOG_DEBUG
    printf("spucc: close\n");
#endif
    cc_decoder_close(this->ccdec);
    cc_renderer_close(this->cc_state.renderer);
    this->cc_open = 0;
  }
}

static void spucc_do_init (spucc_decoder_t *this)
{
  if (! this->cc_open) {
#ifdef LOG_DEBUG
    printf("spucc: init\n");
#endif
    /* initialize caption renderer */
    this->cc_state.renderer = cc_renderer_open(this->stream->osd_renderer,
					       this->stream->metronom,
					       &this->cc_state,
					       this->video_width,
					       this->video_height);
    spucc_update_intrinsics(this);
    /* initialize CC decoder */
    this->ccdec = cc_decoder_open(&this->cc_state);
    this->cc_open = 1;
  }
}


/*----------------- configuration listeners --------------------------------*/

static void spucc_cfg_enable_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_class_t *this = (spucc_class_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;

  cc_cfg->cc_enabled = value->num_value;
#ifdef LOG_DEBUG
  printf("spucc: closed captions are now %s.\n", cc_cfg->cc_enabled?
	 "enabled" : "disabled");
#endif
  cc_cfg->config_version++;
}


static void spucc_cfg_scheme_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_class_t *this = (spucc_class_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;

  cc_cfg->cc_scheme = value->num_value;
#ifdef LOG_DEBUG
  printf("spucc: closed captioning scheme is now %s.\n",
	 cc_schemes[cc_cfg->cc_scheme]);
#endif
  cc_cfg->config_version++;
}


static void spucc_font_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_class_t *this = (spucc_class_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;
  char *font;

  if (strcmp(value->key, "subtitles.closedcaption.font") == 0)
    font = cc_cfg->font;
  else
    font = cc_cfg->italic_font;

  copy_str(font, value->str_value, CC_FONT_MAX);
#ifdef LOG_DEBUG
  printf("spucc: changing %s to font %s\n", value->key, font);
#endif
  cc_cfg->config_version++;
}


static void spucc_num_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_class_t *this = (spucc_class_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;
  int *num;

  if (strcmp(value->key, "subtitles.closedcaption.font_size") == 0)
    num = &cc_cfg->font_size;
  else
    num = &cc_cfg->center;

  *num = value->num_value;
#ifdef LOG_DEBUG
  printf("spucc: changing %s to %d\n", value->key, *num);
#endif
  cc_cfg->config_version++;
}


static void spucc_register_cfg_vars(spucc_class_t *this,
				    config_values_t *xine_cfg) {
  cc_config_t *cc_vars = &this->cc_cfg;

  cc_vars->cc_enabled = xine_cfg->register_bool(xine_cfg,
						"subtitles.closedcaption.enabled", 0,
						_("display closed captions in MPEG-2 streams"),
						_("Closed Captions are subtitles mostly meant "
						  "to help the hearing impaired."),
						0, spucc_cfg_enable_change, this);

  cc_vars->cc_scheme = xine_cfg->register_enum(xine_cfg,
					       "subtitles.closedcaption.scheme", 0,
					       (char **)cc_schemes,
					       _("closed-captioning foreground/background scheme"),
					       _("Choose your favourite rendering of the closed "
					         "captions."),
					       10, spucc_cfg_scheme_change, this);

  copy_str(cc_vars->font,
	   xine_cfg->register_string(xine_cfg, "subtitles.closedcaption.font", "cc",
				     _("standard closed captioning font"),
				     _("Choose the font for standard closed captions text."),
				     20, spucc_font_change, this),
	   CC_FONT_MAX);

  copy_str(cc_vars->italic_font,
	   xine_cfg->register_string(xine_cfg, "subtitles.closedcaption.italic_font", "cci",
				     _("italic closed captioning font"),
				     _("Choose the font for italic closed captions text."),
				     20, spucc_font_change, this),
	   CC_FONT_MAX);

  cc_vars->font_size = xine_cfg->register_num(xine_cfg, "subtitles.closedcaption.font_size",
					      24,
					      _("closed captioning font size"),
					      _("Choose the font size for closed captions text."),
					      10, spucc_num_change, this);

  cc_vars->center = xine_cfg->register_bool(xine_cfg, "subtitles.closedcaption.center", 1,
					    _("center-adjust closed captions"),
					    _("When enabled, closed captions will be positioned "
					      "by the center of the individual lines."),
					    20, spucc_num_change, this);
}


/* called when the video frame size changes */
static void spucc_notify_frame_change(spucc_decoder_t *this,
				      int width, int height) {
#ifdef LOG_DEBUG
  printf("spucc: new frame size: %dx%d\n", width, height);
#endif

  this->video_width = width;
  this->video_height = height;
  spucc_update_intrinsics(this);
}


/*------------------- implementation of spudec interface -------------------*/

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  xine_event_t *event;

  while ((event = xine_event_get(this->queue))) {
    switch (event->type) {
    case XINE_EVENT_FRAME_FORMAT_CHANGE:
      {
        xine_format_change_data_t *frame_change =
          (xine_format_change_data_t *)event->data;

        spucc_notify_frame_change(this, frame_change->width,
                                 frame_change->height);
      }
      break;
    }
    xine_event_free(event);
  }

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
  } else {

    if (this->cc_state.cc_cfg->config_version > this->config_version) {
      spucc_update_intrinsics(this);
      if (!this->cc_state.cc_cfg->cc_enabled)
	spucc_do_close(this);
      this->config_version = this->cc_state.cc_cfg->config_version;
    }

    if (this->cc_state.cc_cfg->cc_enabled) {
      if( !this->cc_open )
	spucc_do_init (this);
      if(this->cc_state.can_cc) {
	decode_cc(this->ccdec, buf->content, buf->size,
		  buf->pts);
      }
    }
  }
}

static void spudec_reset (spu_decoder_t *this_gen) {
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;

  spucc_do_close(this);
  xine_event_dispose_queue(this->queue);
  free (this);
}


static spu_decoder_t *spudec_open_plugin (spu_decoder_class_t *class, xine_stream_t *stream) {

  spucc_decoder_t *this ;

  this = (spucc_decoder_t *) calloc(1, sizeof(spucc_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;

  this->stream                          = stream;
  this->queue                           = xine_event_new_queue(stream);
  this->cc_state.cc_cfg                 = &((spucc_class_t *)class)->cc_cfg;
  this->config_version = 0;
  this->cc_open = 0;

  return &this->spu_decoder;
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  spucc_class_t *this ;

  this = (spucc_class_t *) calloc(1, sizeof(spucc_class_t));

  this->spu_class.open_plugin      = spudec_open_plugin;
  this->spu_class.identifier       = "spucc";
  this->spu_class.description      = N_("closed caption decoder plugin");
  this->spu_class.dispose          = default_spu_decoder_class_dispose;

  spucc_register_cfg_vars(this, xine->config);
  this->cc_cfg.config_version = 0;

  return &this->spu_class;
}

/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_CC, 0 };

static const decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER, 17, "spucc", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
