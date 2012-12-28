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
 * GSM 6.10 Audio Decoder
 * This decoder is based on the GSM 6.10 codec library found at:
 *   http://kbs.cs.tu-berlin.de/~jutta/toast.html
 * Additionally, here is an article regarding the software that appeared
 * in Dr. Dobbs Journal:
 *   http://www.ddj.com/documents/s=1012/ddj9412b/9412b.htm
 *
 * This is the notice that comes with the software:
 * --------------------------------------------------------------------
 * Copyright 1992, 1993, 1994 by Jutta Degener and Carsten Bormann,
 * Technische Universitaet Berlin
 *
 * Any use of this software is permitted provided that this notice is not
 * removed and that neither the authors nor the Technische Universitaet Berlin
 * are deemed to have made any representations as to the suitability of this
 * software for any purpose nor are held responsible for any defects of
 * this software.  THERE IS ABSOLUTELY NO WARRANTY FOR THIS SOFTWARE.
 *
 * As a matter of courtesy, the authors request to be informed about uses
 * this software has found, about bugs in this software, and about any
 * improvements that may be of general interest.
 *
 * Berlin, 28.11.1994
 * Jutta Degener
 * Carsten Bormann
 * --------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"

#include "private.h"
#include "gsm.h"

#define AUDIOBUFSIZE 128*1024

#define GSM610_SAMPLE_SIZE 16
#define GSM610_BLOCK_SIZE 160

typedef struct {
  audio_decoder_class_t   decoder_class;
} gsm610_class_t;

typedef struct gsm610_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *stream;

  unsigned int      buf_type;
  int               output_open;
  int               sample_rate;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  gsm               gsm_state;

} gsm610_decoder_t;

/**************************************************************************
 * xine audio plugin functions
 *************************************************************************/

static void gsm610_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  gsm610_decoder_t *this = (gsm610_decoder_t *) this_gen;
  audio_buffer_t *audio_buffer;
  int in_ptr;

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    this->sample_rate = buf->decoder_info[1];

    this->buf = calloc(1, AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    /* stream/meta info */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "GSM 6.10");

    return;
  }

  if (!this->output_open) {

    this->gsm_state = gsm_create();
    this->buf_type = buf->type;

    this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
      this->stream, GSM610_SAMPLE_SIZE, this->sample_rate, AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, bail */
  if (!this->output_open)
    return;

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "gsm610: increasing source buffer to %d to avoid overflow.\n", this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */
    int16_t decode_buffer[GSM610_BLOCK_SIZE];

    /* handle the Microsoft variant of GSM data */
    if (this->buf_type == BUF_AUDIO_MSGSM) {

      this->gsm_state->wav_fmt = 1;

      /* the data should line up on a 65-byte boundary */
      if ((buf->size % 65) != 0) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "gsm610: received MS GSM block that does not line up\n");
        this->size = 0;
        return;
      }

      in_ptr = 0;
      while (this->size) {
        gsm_decode(this->gsm_state, &this->buf[in_ptr], decode_buffer);
        if ((in_ptr % 65) == 0) {
          in_ptr += 33;
          this->size -= 33;
        } else {
          in_ptr += 32;
          this->size -= 32;
        }

        /* dispatch the decoded audio; assume that the audio buffer will
         * always contain at least 160 samples */
        audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

        xine_fast_memcpy(audio_buffer->mem, decode_buffer,
          GSM610_BLOCK_SIZE * 2);
        audio_buffer->num_frames = GSM610_BLOCK_SIZE;

        audio_buffer->vpts = buf->pts;
        buf->pts = 0;  /* only first buffer gets the real pts */
        this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
      }
    } else {

      /* handle the other variant, which consists of 33-byte blocks */
      this->gsm_state->wav_fmt = 0;

      /* the data should line up on a 33-byte boundary */
      if ((buf->size % 33) != 0) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "gsm610: received GSM block that does not line up\n");
        this->size = 0;
        return;
      }

      in_ptr = 0;
      while (this->size) {
        gsm_decode(this->gsm_state, &this->buf[in_ptr], decode_buffer);
        in_ptr += 33;
        this->size -= 33;

        /* dispatch the decoded audio; assume that the audio buffer will
         * always contain at least 160 samples */
        audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

        xine_fast_memcpy(audio_buffer->mem, decode_buffer,
          GSM610_BLOCK_SIZE * 2);
        audio_buffer->num_frames = GSM610_BLOCK_SIZE;

        audio_buffer->vpts = buf->pts;
        buf->pts = 0;  /* only first buffer gets the real pts */
        this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
      }
    }
  }
}

static void gsm610_reset (audio_decoder_t *this_gen) {
}

static void gsm610_discontinuity (audio_decoder_t *this_gen) {
}

static void gsm610_dispose (audio_decoder_t *this_gen) {

  gsm610_decoder_t *this = (gsm610_decoder_t *) this_gen;

  if (this->gsm_state)
    gsm_destroy(this->gsm_state);

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  if (this->buf)
    free(this->buf);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  gsm610_decoder_t *this ;

  this = (gsm610_decoder_t *) calloc(1, sizeof(gsm610_decoder_t));

  this->audio_decoder.decode_data         = gsm610_decode_data;
  this->audio_decoder.reset               = gsm610_reset;
  this->audio_decoder.discontinuity       = gsm610_discontinuity;
  this->audio_decoder.dispose             = gsm610_dispose;

  this->output_open = 0;
  this->sample_rate = 0;
  this->stream = stream;
  this->buf = NULL;
  this->size = 0;

  return &this->audio_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  gsm610_class_t *this ;

  this = (gsm610_class_t *) calloc(1, sizeof(gsm610_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "GSM 6.10";
  this->decoder_class.description     = N_("GSM 6.10 audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_MSGSM,
  BUF_AUDIO_GSM610,
  0
};

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  9                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "gsm610", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
