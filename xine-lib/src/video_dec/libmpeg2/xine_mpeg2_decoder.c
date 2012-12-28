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
 * stuff needed to turn libmpeg2 into a xine decoder plugin
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_MODULE "mpeg2_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include "mpeg2.h"
#include "mpeg2_internal.h"
#include <xine/buffer.h>

typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;


typedef struct mpeg2dec_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2dec_t       mpeg2;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
} mpeg2dec_decoder_t;

static void mpeg2dec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  lprintf ("decode_data, flags=0x%08x ...\n", buf->decoder_flags);

  /* handle aspect hints from xine-dvdnav */
  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_ASPECT) {
      this->mpeg2.force_aspect = buf->decoder_info[2];
      if (buf->decoder_info[3] == 0x1 && buf->decoder_info[2] == 3)
	/* letterboxing is denied, we have to do pan&scan */
	this->mpeg2.force_pan_scan = 1;
      else
	this->mpeg2.force_pan_scan = 0;
    }
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    mpeg2_find_sequence_header (&this->mpeg2, buf->content, buf->content + buf->size);
  } else {

    mpeg2_decode_data (&this->mpeg2, buf->content, buf->content + buf->size,
		       buf->pts);
  }

  lprintf ("decode_data...done\n");
}

static void mpeg2dec_flush (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  lprintf ("flush\n");

  mpeg2_flush (&this->mpeg2);
}

static void mpeg2dec_reset (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  mpeg2_reset (&this->mpeg2);
}

static void mpeg2dec_discontinuity (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  mpeg2_discontinuity (&this->mpeg2);
}

static void mpeg2dec_dispose (video_decoder_t *this_gen) {

  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  lprintf ("close\n");

  mpeg2_close (&this->mpeg2);

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  mpeg2dec_decoder_t *this ;

  this = (mpeg2dec_decoder_t *) calloc(1, sizeof(mpeg2dec_decoder_t));

  this->video_decoder.decode_data         = mpeg2dec_decode_data;
  this->video_decoder.flush               = mpeg2dec_flush;
  this->video_decoder.reset               = mpeg2dec_reset;
  this->video_decoder.discontinuity       = mpeg2dec_discontinuity;
  this->video_decoder.dispose             = mpeg2dec_dispose;
  this->stream                            = stream;
  this->class                             = (mpeg2_class_t *) class_gen;
  this->mpeg2.stream = stream;

  mpeg2_init (&this->mpeg2, stream->video_out);
  (stream->video_out->open) (stream->video_out, stream);
  this->mpeg2.force_aspect = this->mpeg2.force_pan_scan = 0;

  return &this->video_decoder;
}

/*
 * mpeg2 plugin class
 */
static void *init_plugin (xine_t *xine, void *data) {

  mpeg2_class_t *this;

  this = (mpeg2_class_t *) calloc(1, sizeof(mpeg2_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "mpeg2dec";
  this->decoder_class.description     = N_("mpeg2 based video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}
/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static const decoder_info_t dec_info_mpeg2 = {
  supported_types,     /* supported types */
  7                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "mpeg2", XINE_VERSION_CODE, &dec_info_mpeg2, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
