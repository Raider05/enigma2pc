/*
 * Copyright (C) 2000-2008 the xine project
 *
 * Copyright (C) Christian Vogler
 *               cvogler@gradient.cis.upenn.edu - December 2001
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
 * stuff needed to provide closed captioning decoding and display
 *
 * Some small bits and pieces of the EIA-608 captioning decoder were
 * adapted from CCDecoder 0.9.1 by Mike Baker. The latest version is
 * available at http://sourceforge.net/projects/ccdecoder/.
 */

typedef struct cc_decoder_s cc_decoder_t;
typedef struct cc_renderer_s cc_renderer_t;

#define NUM_CC_PALETTES 2
#define CC_FONT_MAX 256

typedef struct cc_config_s {
  int cc_enabled;             /* true if closed captions are enabled */
  char font[CC_FONT_MAX];     /* standard captioning font & size */
  int font_size;
  char italic_font[CC_FONT_MAX];   /* italic captioning font & size */
  int center;                 /* true if captions should be centered */
                              /* according to text width */
  int cc_scheme;              /* which captioning scheme to use */

  int config_version;         /* the decoder should be updated when this is increased */
} cc_config_t;

typedef struct spucc_class_s {
  spu_decoder_class_t spu_class;
  cc_config_t         cc_cfg;
} spucc_class_t;

typedef struct cc_state_s {
  cc_config_t *cc_cfg;
  /* the following variables are not controlled by configuration files; they */
  /* are intrinsic to the properties of the configuration options and the */
  /* currently played video */
  int            can_cc;      /* true if captions can be displayed */
                              /* (e.g., font fits on screen) */
  cc_renderer_t *renderer;    /* closed captioning renderer */
} cc_state_t;

cc_decoder_t *cc_decoder_open(cc_state_t *cc_state);
void cc_decoder_close(cc_decoder_t *this_obj);

void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       int64_t pts);

/* Instantiates a new closed captioning renderer. */
cc_renderer_t *cc_renderer_open(osd_renderer_t *osd_renderer,
				metronom_t *metronom, cc_state_t *cc_state,
				int video_width, int video_height);

/* Destroys a closed captioning renderer. */
void cc_renderer_close(cc_renderer_t *this_obj);

/* Updates the renderer configuration variables */
void cc_renderer_update_cfg(cc_renderer_t *this_obj, int video_width,
			    int video_height);

