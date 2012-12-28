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
 * stuff needed to turn libmad into a xine decoder plugin
 */

#include <stdlib.h>
#include <string.h>
#include "config.h"

#ifdef HAVE_MAD_H
#include <mad.h>
#endif

#define LOG_MODULE "mad_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#ifdef HAVE_MAD_H
# include <mad.h>
#else
# include "frame.h"
# include "synth.h"
#endif

#define INPUT_BUF_SIZE  16384

/* According to Rob Leslie (libmad author) :
 * The absolute theoretical maximum frame size is 2881 bytes: MPEG 2.5 Layer II,
 * 8000 Hz @ 160 kbps, with a padding slot. (Such a frame is unlikely, but it was
 * a useful exercise to compute all possible frame sizes.) Add to this an 8 byte
 * MAD_BUFFER_GUARD, and the minimum buffer size you should be streaming to
 * libmad in the general case is 2889 bytes.

 * Theoretical frame sizes for Layer III range from 24 to 1441 bytes, but there
 * is a "soft" limit imposed by the standard of 960 bytes. Nonetheless MAD can
 * decode frames of any size as long as they fit entirely in the buffer you pass,
 * not including the MAD_BUFFER_GUARD bytes.
 */
#define MAD_MIN_SIZE 2889

typedef struct {
  audio_decoder_class_t   decoder_class;
} mad_class_t;

typedef struct mad_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *xstream;

  int64_t           pts;

  struct mad_synth  synth;
  struct mad_stream stream;
  struct mad_frame  frame;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  uint8_t           buffer[INPUT_BUF_SIZE];
  int               bytes_in_buffer;
  int               preview_mode;
  int               start_padding;
  int               end_padding;
  int               needs_more_data;

} mad_decoder_t;

static void mad_reset (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  mad_synth_finish (&this->synth);
  mad_frame_finish (&this->frame);
  mad_stream_finish(&this->stream);

  this->pts = 0;
  this->bytes_in_buffer = 0;
  this->preview_mode = 0;
  this->start_padding = 0;
  this->end_padding = 0;
  this->needs_more_data = 0;

  mad_synth_init  (&this->synth);
  mad_stream_init (&this->stream);
  this->stream.options = MAD_OPTION_IGNORECRC;
  mad_frame_init  (&this->frame);
}


static void mad_discontinuity (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  this->pts = 0;
}

/* utility to scale and round samples to 16 bits */

static inline
signed int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

/*
static int head_check(mad_decoder_t *this) {

  if( (this->header & 0xffe00000) != 0xffe00000)
    return 0;
  if(!((this->header>>17)&3))
    return 0;
  if( ((this->header>>12)&0xf) == 0xf)
    return 0;
  if( ((this->header>>10)&0x3) == 0x3 )
    return 0;
  return 1;
}
*/

static void mad_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;
  int bytes_in_buffer_at_pts;

  lprintf ("decode data, size: %d, decoder_flags: %d\n", buf->size, buf->decoder_flags);

  if (buf->size>(INPUT_BUF_SIZE-this->bytes_in_buffer)) {
    xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG,
	     "libmad: ALERT input buffer too small (%d bytes, %d avail)!\n",
	     buf->size, INPUT_BUF_SIZE-this->bytes_in_buffer);
    buf->size = INPUT_BUF_SIZE-this->bytes_in_buffer;
  }

  if ((buf->decoder_flags & BUF_FLAG_HEADER) == 0) {

    /* reset decoder on leaving preview mode */
    if ((buf->decoder_flags & BUF_FLAG_PREVIEW) == 0) {
      if (this->preview_mode) {
	mad_reset (this_gen);
      }
    } else {
      this->preview_mode = 1;
    }

    bytes_in_buffer_at_pts = this->bytes_in_buffer;

    xine_fast_memcpy (&this->buffer[this->bytes_in_buffer],
                        buf->content, buf->size);
    this->bytes_in_buffer += buf->size;

    /*
    printf ("libmad: decode data - doing it\n");
    */

    mad_stream_buffer (&this->stream, this->buffer,
		       this->bytes_in_buffer);

    if (this->bytes_in_buffer < MAD_MIN_SIZE && buf->pts == 0)
      return;

    if (!this->needs_more_data) {
      this->pts = buf->pts;
      if (buf->decoder_flags & BUF_FLAG_AUDIO_PADDING) {
        this->start_padding = buf->decoder_info[1];
        this->end_padding = buf->decoder_info[2];
      } else {
        this->start_padding = 0;
        this->end_padding = 0;
      }
    }

    while (1) {

      if (mad_frame_decode (&this->frame, &this->stream) != 0) {

	if (this->stream.next_frame) {
	  int num_bytes =
	    this->buffer + this->bytes_in_buffer - this->stream.next_frame;

	  /* printf("libmad: MAD_ERROR_BUFLEN\n"); */

	  memmove(this->buffer, this->stream.next_frame, num_bytes);
	  this->bytes_in_buffer = num_bytes;
	}

	switch (this->stream.error) {

	case MAD_ERROR_BUFLEN:
	  /* libmad wants more data */
	  this->needs_more_data = 1;
	  return;

	default:
	  lprintf ("error 0x%04X, mad_stream_buffer %d bytes\n", this->stream.error, this->bytes_in_buffer);
	  mad_stream_buffer (&this->stream, this->buffer,
			     this->bytes_in_buffer);
	}

      } else {
	int mode = (this->frame.header.mode == MAD_MODE_SINGLE_CHANNEL) ? AO_CAP_MODE_MONO : AO_CAP_MODE_STEREO;

	if (!this->output_open
	    || (this->output_sampling_rate != this->frame.header.samplerate)
	    || (this->output_mode != mode)) {

	  lprintf ("audio sample rate %d mode %08x\n", this->frame.header.samplerate, mode);

          /* the mpeg audio demuxer can set audio bitrate */
          if (! _x_stream_info_get(this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE)) {
            _x_stream_info_set(this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE,
                               this->frame.header.bitrate);
          }

          /* the mpeg audio demuxer can set this meta info */
          if (! _x_meta_info_get(this->xstream, XINE_META_INFO_AUDIOCODEC)) {
            switch (this->frame.header.layer) {
            case MAD_LAYER_I:
              _x_meta_info_set_utf8(this->xstream, XINE_META_INFO_AUDIOCODEC,
                "MPEG audio layer 1 (lib: MAD)");
              break;
            case MAD_LAYER_II:
              _x_meta_info_set_utf8(this->xstream, XINE_META_INFO_AUDIOCODEC,
                "MPEG audio layer 2 (lib: MAD)");
              break;
            case MAD_LAYER_III:
              _x_meta_info_set_utf8(this->xstream, XINE_META_INFO_AUDIOCODEC,
                "MPEG audio layer 3 (lib: MAD)");
              break;
            default:
              _x_meta_info_set_utf8(this->xstream, XINE_META_INFO_AUDIOCODEC,
                "MPEG audio (lib: MAD)");
            }
          }

	  if (this->output_open) {
	    this->xstream->audio_out->close (this->xstream->audio_out, this->xstream);
	    this->output_open = 0;
          }
          if (!this->output_open) {
	    this->output_open = (this->xstream->audio_out->open) (this->xstream->audio_out,
				   this->xstream, 16,
				   this->frame.header.samplerate,
			           mode) ;
          }
          if (!this->output_open) {
            return;
          }
	  this->output_sampling_rate = this->frame.header.samplerate;
	  this->output_mode = mode;
	}

	mad_synth_frame (&this->synth, &this->frame);

	if ( (buf->decoder_flags & BUF_FLAG_PREVIEW) == 0 ) {

	  unsigned int         nchannels, nsamples;
	  mad_fixed_t const   *left_ch, *right_ch;
	  struct mad_pcm      *pcm = &this->synth.pcm;
	  audio_buffer_t      *audio_buffer;
	  uint16_t            *output;
          int                  bitrate;
          int                  pts_offset;

	  audio_buffer = this->xstream->audio_out->get_buffer (this->xstream->audio_out);
	  output = audio_buffer->mem;

	  nchannels = pcm->channels;
	  nsamples  = pcm->length;
	  left_ch   = pcm->samples[0];
	  right_ch  = pcm->samples[1];

	  /* padding */
	  if (this->start_padding || this->end_padding) {
	    /* check padding validity */
	    if (nsamples < (this->start_padding + this->end_padding)) {
	      lprintf("invalid padding data");
	      this->start_padding = 0;
	      this->end_padding = 0;
	    }
	    lprintf("nsamples=%d, start_padding=%d, end_padding=%d\n",
	            nsamples, this->start_padding, this->end_padding);
	    nsamples -= this->start_padding + this->end_padding;
	    left_ch  += this->start_padding;
	    right_ch += this->start_padding;
	  }
	  audio_buffer->num_frames = nsamples;
	  audio_buffer->vpts       = this->pts;

	  while (nsamples--) {
	    /* output sample(s) in 16-bit signed little-endian PCM */

	    *output++ = scale(*left_ch++);

	    if (nchannels == 2)
	      *output++ = scale(*right_ch++);

	  }

	  audio_buffer->num_frames = pcm->length;

          /* pts computing */
          if (this->frame.header.bitrate > 0) {
            bitrate = this->frame.header.bitrate;
          } else {
            bitrate = _x_stream_info_get(this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE);
            lprintf("offset %d bps\n", bitrate);
          }
          audio_buffer->vpts = buf->pts;
          if (audio_buffer->vpts && (bitrate > 0)) {
            pts_offset = (bytes_in_buffer_at_pts * 8 * 90) / (bitrate / 1000);
            lprintf("pts: %"PRId64", offset: %d pts, %d bytes\n", buf->pts, pts_offset, bytes_in_buffer_at_pts);
            if (audio_buffer->vpts < pts_offset)
              pts_offset = audio_buffer->vpts;
            audio_buffer->vpts -= pts_offset;
          }

	  this->xstream->audio_out->put_buffer (this->xstream->audio_out, audio_buffer, this->xstream);

	  this->pts = buf->pts;
	  buf->pts = 0;
	  if (buf->decoder_flags & BUF_FLAG_AUDIO_PADDING) {
	    this->start_padding = buf->decoder_info[1];
	    this->end_padding = buf->decoder_info[2];
	    buf->decoder_info[1] = 0;
	    buf->decoder_info[2] = 0;
	  } else {
	    this->start_padding = 0;
	    this->end_padding = 0;
	  }
	}
	lprintf ("decode worked\n");
      }
    }
  }
}

static void mad_dispose (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  mad_synth_finish (&this->synth);
  mad_frame_finish (&this->frame);
  mad_stream_finish(&this->stream);

  if (this->output_open) {
    this->xstream->audio_out->close (this->xstream->audio_out, this->xstream);
    this->output_open = 0;
  }

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  mad_decoder_t *this ;

  this = (mad_decoder_t *) calloc(1, sizeof(mad_decoder_t));

  this->audio_decoder.decode_data         = mad_decode_data;
  this->audio_decoder.reset               = mad_reset;
  this->audio_decoder.discontinuity       = mad_discontinuity;
  this->audio_decoder.dispose             = mad_dispose;

  this->output_open     = 0;
  this->bytes_in_buffer = 0;
  this->preview_mode    = 0;

  this->xstream         = stream;

  mad_synth_init  (&this->synth);
  mad_stream_init (&this->stream);
  mad_frame_init  (&this->frame);

  this->stream.options = MAD_OPTION_IGNORECRC;

  lprintf ("init\n");

  return &this->audio_decoder;
}

/*
 * mad plugin class
 */
static void *init_plugin (xine_t *xine, void *data) {

  mad_class_t *this;

  this = (mad_class_t *) calloc(1, sizeof(mad_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "mad";
  this->decoder_class.description     = N_("libmad based mpeg audio layer 1/2/3 decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_MPEG, 0
};

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  8                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "mad", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
