/*
 * Copyright (C) 2005 the xine project
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
 * dv audio decoder based on patch by Dan Dennedy <dan@dennedy.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

#define LOG_MODULE "dvaudio"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#include "ff_dvdata.h" /* This is not installed by FFmpeg, its usage has to be cleared up */

#define AUDIOBUFSIZE 128*1024
#define MAXFRAMESIZE 131072


typedef struct {
  audio_decoder_class_t   decoder_class;
} dvaudio_class_t;

typedef struct dvaudio_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *stream;

  int               output_open;
  int               audio_channels;
  int               audio_bits;
  int               audio_sample_rate;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  char             *decode_buffer;
  int               decoder_ok;

} dvaudio_decoder_t;

/*
 * This is the dumbest implementation of all -- it simply looks at
 * a fixed offset and if pack isn't there -- fails. We might want
 * to have a fallback mechanism for complete search of missing packs.
 */
static const uint8_t* dv_extract_pack(uint8_t* frame, enum dv_pack_type t)
{
    int offs;

    switch (t) {
    case dv_audio_source:
          offs = (80*6 + 80*16*3 + 3);
	  break;
    case dv_audio_control:
          offs = (80*6 + 80*16*4 + 3);
	  break;
    case dv_video_control:
          offs = (80*5 + 48 + 5);
          break;
    default:
          return NULL;
    }

    return (frame[offs] == t ? &frame[offs] : NULL);
}

static inline uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;

    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
	result = sample;
    } else if (shift < 0x8) {
        shift--;
	result = (sample - (256 * shift)) << shift;
    } else {
	shift = 0xe - shift;
	result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

/*
 * There's a couple of assumptions being made here:
 * 1. By default we silence erroneous (0x8000/16bit 0x800/12bit) audio samples.
 *    We can pass them upwards when ffmpeg will be ready to deal with them.
 * 2. We don't do software emphasis.
 * 3. Audio is always returned as 16bit linear samples: 12bit nonlinear samples
 *    are converted into 16bit linear ones.
 */
static int dv_extract_audio(uint8_t* frame, uint8_t* pcm, uint8_t* pcm2)
{
    int size, i, j, d, of, smpls, freq, quant, half_ch;
    uint16_t lc, rc;
    const DVprofile* sys;
    const uint8_t* as_pack;

    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack)    /* No audio ? */
	return 0;

    sys = dv_frame_profile(frame);
    smpls = as_pack[1] & 0x3f; /* samples in this frame - min. samples */
    freq = (as_pack[4] >> 3) & 0x07; /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    quant = as_pack[4] & 0x07; /* 0 - 16bit linear, 1 - 12bit nonlinear */

    if (quant > 1)
	return -1; /* Unsupported quantization */

    size = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
    half_ch = sys->difseg_size/2;

    /* for each DIF segment */
    for (i = 0; i < sys->difseg_size; i++) {
       frame += 6 * 80; /* skip DIF segment header */
       if (quant == 1 && i == half_ch) {
           if (!pcm2)
               break;
           else
               pcm = pcm2;
       }

       for (j = 0; j < 9; j++) {
          for (d = 8; d < 80; d += 2) {
	     if (quant == 0) {  /* 16bit quantization */
		 of = sys->audio_shuffle[i][j] + (d - 8)/2 * sys->audio_stride;
                 if (of*2 >= size)
		     continue;

#ifdef WORDS_BIGENDIAN
		 pcm[of*2] = frame[d];
		 pcm[of*2+1] = frame[d+1];
#else
		 pcm[of*2] = frame[d+1];
		 pcm[of*2+1] = frame[d];
#endif
		 if (pcm[of*2+1] == 0x80 && pcm[of*2] == 0x00)
		     pcm[of*2+1] = 0;
	      } else {           /* 12bit quantization */
		 lc = ((uint16_t)frame[d] << 4) |
		      ((uint16_t)frame[d+2] >> 4);
		 rc = ((uint16_t)frame[d+1] << 4) |
	              ((uint16_t)frame[d+2] & 0x0f);
		 lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
		 rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

		 of = sys->audio_shuffle[i%half_ch][j] + (d - 8)/3 * sys->audio_stride;
                 if (of*2 >= size)
		     continue;

#ifdef WORDS_BIGENDIAN
		 pcm[of*2] = lc >> 8;
		 pcm[of*2+1] = lc & 0xff;
#else
		 pcm[of*2] = lc & 0xff;
		 pcm[of*2+1] = lc >> 8;
#endif
		 of = sys->audio_shuffle[i%half_ch+half_ch][j] +
		      (d - 8)/3 * sys->audio_stride;
#ifdef WORDS_BIGENDIAN
		 pcm[of*2] = rc >> 8;
		 pcm[of*2+1] = rc & 0xff;
#else
		 pcm[of*2] = rc & 0xff;
		 pcm[of*2+1] = rc >> 8;
#endif
		 ++d;
	      }
	  }

	  frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
        }
    }

    return size;
}

static void dvaudio_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dvaudio_decoder_t *this = (dvaudio_decoder_t *) this_gen;
  int bytes_consumed;
  int decode_buffer_size;
  int offset;
  int out;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    this->buf = calloc(1, AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;
    this->decode_buffer = calloc(1, MAXFRAMESIZE);

    this->audio_sample_rate = buf->decoder_info[1];
    this->audio_bits = buf->decoder_info[2];
    this->audio_channels = buf->decoder_info[3];

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DV Audio");

    this->decoder_ok = 1;

    return;
  }

  if (this->decoder_ok && !(buf->decoder_flags & (BUF_FLAG_HEADER|BUF_FLAG_SPECIAL))) {

    if (!this->output_open) {
      this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
        this->stream, this->audio_bits, this->audio_sample_rate,
	_x_ao_channels2mode(this->audio_channels));
    }

    /* if the audio still isn't open, bail */
    if (!this->output_open)
      return;

    if( this->size + buf->size > this->bufsize ) {
      this->bufsize = this->size + 2 * buf->size;
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("dvaudio: increasing buffer to %d to avoid overflow.\n"),
              this->bufsize);
      this->buf = realloc( this->buf, this->bufsize );
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) { /* time to decode a frame */

      offset = 0;
      while (this->size>0) {
        decode_buffer_size = dv_extract_audio(&this->buf[offset], this->decode_buffer, NULL);

        if (decode_buffer_size > -1)
          bytes_consumed = dv_frame_profile(&this->buf[offset])->frame_size;
        else
          bytes_consumed = decode_buffer_size;

        /* dispatch the decoded audio */
        out = 0;
        while (out < decode_buffer_size) {
          audio_buffer =
            this->stream->audio_out->get_buffer (this->stream->audio_out);
          if (audio_buffer->mem_size == 0) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                     "dvaudio: Help! Allocated audio buffer with nothing in it!\n");
            return;
          }

          if ((decode_buffer_size - out) > audio_buffer->mem_size)
            bytes_to_send = audio_buffer->mem_size;
          else
            bytes_to_send = decode_buffer_size - out;

          /* fill up this buffer */
          xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[out],
            bytes_to_send);
          /* byte count / 2 (bytes / sample) / channels */
          audio_buffer->num_frames = bytes_to_send / 2 / this->audio_channels;

          audio_buffer->vpts = buf->pts;
          buf->pts = 0;  /* only first buffer gets the real pts */
          this->stream->audio_out->put_buffer (this->stream->audio_out,
            audio_buffer, this->stream);

          out += bytes_to_send;
        }

        this->size -= bytes_consumed;
        offset += bytes_consumed;
      }

      /* reset internal accumulation buffer */
      this->size = 0;
    }
  }
}

static void dvaudio_reset (audio_decoder_t *this_gen) {
  dvaudio_decoder_t *this = (dvaudio_decoder_t *) this_gen;

  this->size = 0;
}

static void dvaudio_discontinuity (audio_decoder_t *this_gen) {
}

static void dvaudio_dispose (audio_decoder_t *this_gen) {

  dvaudio_decoder_t *this = (dvaudio_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  free(this->buf);
  free(this->decode_buffer);

  free (this_gen);
}

static audio_decoder_t *dvaudio_open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  dvaudio_decoder_t *this ;

  this = calloc(1, sizeof (dvaudio_decoder_t));

  this->audio_decoder.decode_data    = dvaudio_decode_data;
  this->audio_decoder.reset          = dvaudio_reset;
  this->audio_decoder.discontinuity  = dvaudio_discontinuity;
  this->audio_decoder.dispose        = dvaudio_dispose;

  this->output_open = 0;
  this->audio_channels = 0;
  this->stream = stream;
  this->buf = NULL;
  this->size = 0;
  this->decoder_ok = 0;

  return &this->audio_decoder;
}

static void *init_dvaudio_plugin (xine_t *xine, void *data) {

  dvaudio_class_t *this ;

  this = calloc(1, sizeof (dvaudio_class_t));

  this->decoder_class.open_plugin     = dvaudio_open_plugin;
  this->decoder_class.identifier      = "dv audio";
  this->decoder_class.description     = N_("dv audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static uint32_t supported_audio_types[] = {
  BUF_AUDIO_DV,
  0
};

static const decoder_info_t dec_info_dvaudio = {
  supported_audio_types,   /* supported types */
  5                        /* priority        */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "dvaudio", XINE_VERSION_CODE, &dec_info_dvaudio, init_dvaudio_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
