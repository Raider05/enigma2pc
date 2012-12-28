/*
 * Copyright (C) 2007 the xine project
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
 * xine interface to libwavpack by Diego Pettenò <flameeyes@gmail.com>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define LOG_MODULE "decode_wavpack"
#define LOG_VERBOSE

#include <xine/xine_internal.h>
#include <xine/attributes.h>
#include "bswap.h"

#include <wavpack/wavpack.h>
#include "wavpack_combined.h"

typedef struct {
  audio_decoder_class_t   decoder_class;
} wavpack_class_t;

typedef struct {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *stream;

  uint8_t          *buf;
  size_t            buf_size;
  size_t            buf_pos;

  int               sample_rate;
  uint16_t          bits_per_sample:6;
  uint16_t          channels:4;

  uint16_t          output_open:1;

} wavpack_decoder_t;

/* Wrapper functions for Wavpack */
static int32_t xine_buffer_read_bytes(void *const this_gen, void *const data,
				      int32_t bcount) {
  wavpack_decoder_t *const this = (wavpack_decoder_t*)this_gen;

  if ( bcount <= 0 )
    return 0;

  if ( bcount > (this->buf_size - this->buf_pos) )
    bcount = (this->buf_size - this->buf_pos);

  xine_fast_memcpy(data, this->buf + this->buf_pos, bcount);
  this->buf_pos += bcount;

  return bcount;
}

static uint32_t xine_buffer_get_pos(void *const this_gen) {
  wavpack_decoder_t *const this = (wavpack_decoder_t*)this_gen;
  return this->buf_pos;
}

static int xine_buffer_set_pos_rel(void *const this_gen, const int32_t delta,
				  const int mode) {
  wavpack_decoder_t *const this = (wavpack_decoder_t*)this_gen;

  switch(mode) {
  case SEEK_SET:
    if ( delta < 0 || delta > this->buf_size )
      return -1;

    this->buf_pos = delta;
    return 0;
  case SEEK_CUR:
    if ( (this->buf_pos+delta) < 0 || (this->buf_pos+delta) > this->buf_size )
      return -1;

    this->buf_pos += delta;
    return 0;
  case SEEK_END:
    if ( delta < 0 || delta > this->buf_size )
      return -1;

    this->buf_pos = this->buf_size - delta;

    return 0;

  default:
    return -1;
  }

  return -1;
}

static int xine_buffer_set_pos_abs(void *const this_gen, const uint32_t pos) {
  return xine_buffer_set_pos_rel(this_gen, pos, SEEK_SET);
}

static int xine_buffer_push_back_byte(void *const this_gen, const int c) {
  if ( ! xine_buffer_set_pos_rel(this_gen, -1, SEEK_CUR) )
    return EOF;
  return c;
}

static uint32_t xine_buffer_get_length(void *const this_gen) {
  wavpack_decoder_t *const this = (wavpack_decoder_t*)this_gen;
  return this->buf_size;
}

static int xine_buffer_can_seek(void *const this_gen) {
  return 1;
}

static int32_t xine_buffer_write_bytes(__attr_unused void *const id,
				      __attr_unused void *const data,
				      __attr_unused const int32_t bcount) {
  lprintf("xine_buffer_write_bytes: access is read-only.\n");
  return 0;
}

/* Wavpack plugin functions */
static void wavpack_reset (audio_decoder_t *const this_gen)
{
  wavpack_decoder_t *const this = (wavpack_decoder_t *) this_gen;

  this->buf_pos = 0;
}

static void wavpack_discontinuity (audio_decoder_t *const this_gen)
{
  /* wavpack_decoder_t *this = (wavpack_decoder_t *) this_gen; */

  lprintf("Discontinuity!\n");
}

static void wavpack_decode_data (audio_decoder_t *const this_gen, buf_element_t *const buf)
{
    wavpack_decoder_t *const this = (wavpack_decoder_t *) this_gen;

    /* We are getting the stream header, open up the audio
     * device, and collect information about the stream
     */
    if (buf->decoder_flags & BUF_FLAG_STDHEADER)
    {
        int mode = AO_CAP_MODE_MONO;

        this->sample_rate     = buf->decoder_info[1];
	_x_assert(buf->decoder_info[2] <= 32);
        this->bits_per_sample = buf->decoder_info[2];
	_x_assert(buf->decoder_info[3] <= 8);
        this->channels        = buf->decoder_info[3];

	mode = _x_ao_channels2mode(this->channels);

	_x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC,
			 "WavPack");

        if (!this->output_open)
        {
            this->output_open = (this->stream->audio_out->open) (
                                            this->stream->audio_out,
                                            this->stream,
                                            this->bits_per_sample,
                                            this->sample_rate,
                                            mode) ? 1 : 0;
        }
        this->buf_pos = 0;
    } else if (this->output_open) {
      /* This isn't a header frame and we have opened the output device */

      /* What we have buffered so far, and what is coming in
       * is larger than our buffer
       */
      if (this->buf_pos + buf->size > this->buf_size)
        {
	  this->buf_size += 2 * buf->size;
	  this->buf = realloc (this->buf, this->buf_size);
	  lprintf("reallocating buffer to %zd\n", this->buf_size);
        }

      xine_fast_memcpy (&this->buf[this->buf_pos], buf->content, buf->size);
      this->buf_pos += buf->size;

      if ( buf->decoder_flags & BUF_FLAG_FRAME_END ) {
	static WavpackStreamReader wavpack_buffer_reader = {
	  .read_bytes		= xine_buffer_read_bytes,
	  .get_pos		= xine_buffer_get_pos,
	  .set_pos_abs		= xine_buffer_set_pos_abs,
	  .set_pos_rel		= xine_buffer_set_pos_rel,
	  .push_back_byte	= xine_buffer_push_back_byte,
	  .get_length		= xine_buffer_get_length,
	  .can_seek		= xine_buffer_can_seek,
	  .write_bytes		= xine_buffer_write_bytes
	};

	WavpackContext *ctx = NULL;
	/* Current version of wavpack (4.40) does not write more than this */
	char error[256] = { 0, };
	int32_t samples_left; uint32_t samples_total;
	const wvheader_t *header = (const wvheader_t*)this->buf;

	this->buf_pos = 0;

	if ( le2me_32(header->samples_count) == 0 ) return;

	ctx = WavpackOpenFileInputEx(&wavpack_buffer_reader, this, NULL, error, OPEN_STREAMING, 0);
	if ( ! ctx ) {
	  lprintf("unable to open the stream: %s\n", error);
	  this->buf_pos = 0;
	  return;
	}

	samples_left = samples_total = header->samples_count;
	while ( samples_left > 0 ) {
	  uint32_t buf_samples, decoded_count;
	  audio_buffer_t *audio_buffer = this->stream->audio_out->get_buffer(this->stream->audio_out);
	  int32_t *decoded;
	  int i;

	  buf_samples = audio_buffer->mem_size / (this->channels * (this->bits_per_sample/8));
	  if ( buf_samples > samples_left ) buf_samples = samples_left;

	  decoded = alloca(buf_samples * this->channels * sizeof(int32_t));

	  decoded_count = WavpackUnpackSamples(ctx, decoded, buf_samples);
	  if ( decoded_count == 0 && *error ) {
	    lprintf("Error during decode: %s\n", error);
	    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, NULL);
	    break;
	  }

	  if ( decoded_count == 0 ) {
	    lprintf("Finished decoding, but still %d samples left?\n", samples_left);
	    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, NULL);
	    break;
	  }

	  lprintf("Decoded %d samples\n", buf_samples);

	  samples_left -= decoded_count;

	  audio_buffer->num_frames = decoded_count;
	  audio_buffer->vpts = 0; /* TODO: Fix the pts calculation */
	  // audio_buffer->vpts = (buf->pts * (samples_total-samples_left)) / samples_total;
	  lprintf("Audio buffer with pts %"PRId64"\n", audio_buffer->vpts);

	  switch(this->bits_per_sample) {
	  case 8: {
	    int8_t *data8 = (int8_t*)audio_buffer->mem;
	    for(i = 0; i < decoded_count*this->channels; i++)
	      data8[i] = decoded[i];
	    }
	    break;
	  case 16: {
	    int16_t *data16 = (int16_t*)audio_buffer->mem;
	    for(i = 0; i < decoded_count*this->channels; i++)
	      data16[i] = decoded[i];
	    }
	  }

	  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
	}

	WavpackCloseFile(ctx);
	this->buf_pos = 0;
      }
    }
}

static void wavpack_dispose (audio_decoder_t *this_gen) {
    wavpack_decoder_t *this = (wavpack_decoder_t *) this_gen;

    if (this->output_open)
        this->stream->audio_out->close (this->stream->audio_out, this->stream);

    free(this->buf);

    free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {
  wavpack_decoder_t * const this = calloc(1, sizeof (wavpack_decoder_t));

    this->audio_decoder.decode_data         = wavpack_decode_data;
    this->audio_decoder.reset               = wavpack_reset;
    this->audio_decoder.discontinuity       = wavpack_discontinuity;
    this->audio_decoder.dispose             = wavpack_dispose;
    this->stream                            = stream;

    this->buf                               = NULL;
    this->buf_size                          = 0;

    return (audio_decoder_t *) this;
}

/*
 * wavpack plugin class
 */

void *decoder_wavpack_init_plugin (xine_t *xine, void *data) {
    wavpack_class_t *this;

    this = calloc(1, sizeof (wavpack_class_t));

    this->decoder_class.open_plugin     = open_plugin;
    this->decoder_class.identifier      = "wavpackdec";
    this->decoder_class.description     = N_("wavpack audio decoder plugin");
    this->decoder_class.dispose         = default_audio_decoder_class_dispose;

    return this;
}
