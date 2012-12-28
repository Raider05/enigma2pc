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
 * (ogg/)speex audio decoder plugin (libspeex wrapper) for xine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "speex_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/
#define LOG_BUFFERS 0

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>

#include <ogg/ogg.h>

#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_callbacks.h>
#include <speex/speex_stereo.h>

#define MAX_FRAME_SIZE 2000

typedef struct {
  audio_decoder_class_t   decoder_class;
} speex_class_t;

typedef struct speex_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  /* speex stuff */
  void             *st;
  int               frame_size;
  int               rate;
  int               nframes;
  int               channels;
  SpeexBits         bits;
  SpeexStereoState  stereo;
  int               expect_metadata;

  int               header_count;

  xine_stream_t    *stream;

} speex_decoder_t;


static void speex_reset (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

  speex_bits_init (&this->bits);
}

static void speex_discontinuity (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

  this->pts=0;
}

/* Known speex comment keys from ogg123 sources*/
static const struct {
  char key[16];         /* includes the '=' for programming convenience */
  int   xine_metainfo_index;
} speex_comment_keys[] = {
  {"ARTIST=", XINE_META_INFO_ARTIST},
  {"ALBUM=", XINE_META_INFO_ALBUM},
  {"TITLE=", XINE_META_INFO_TITLE},
  {"GENRE=", XINE_META_INFO_GENRE},
  {"DESCRIPTION=", XINE_META_INFO_COMMENT},
  {"DATE=", XINE_META_INFO_YEAR}
};

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
                            (buf[base]&0xff))

static
void read_metadata (speex_decoder_t *this, char * comments, int length)
{
  char * c = comments;
  int len, i, nb_fields;
  char * end;

  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "speex");

  if (length < 8) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: invalid/corrupted comments\n");
    return;
  }

  end = c+length;
  len = readint (c, 0);
  c += 4;

  if (c+len > end) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: invalid/corrupted comments\n");
    return;
  }

#ifdef LOG
  /* Encoder */
  printf ("libspeex: ");
  fwrite (c, 1, len, stdout);
  printf ("\n");
#endif

  c += len;

  if (c+4 > end) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: invalid/corrupted comments\n");
    return;
  }

  nb_fields = readint (c, 0);
  c += 4;

  for (i = 0; i < nb_fields; i++) {
    if (c+4 > end) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: invalid/corrupted comments\n");
      return;
    }

    len = readint (c, 0);
    c += 4;
    if (c+len > end) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: invalid/corrupted comments\n");
      return;
    }

#ifdef LOG
    printf ("libspeex: ");
    fwrite (c, 1, len, stdout);
    printf ("\n");
#endif

    for (i = 0; i < (sizeof(speex_comment_keys)/sizeof(speex_comment_keys[0])); i++) {
      size_t keylen = strlen(speex_comment_keys[i].key);

      if ( !strncasecmp (speex_comment_keys[i].key, c,
			 keylen) ) {
	char meta_info[(len - keylen) + 1];

	lprintf ("known metadata %d %d\n",
		 i, speex_comment_keys[i].xine_metainfo_index);

	strncpy(meta_info, &c[keylen], len-keylen);
	_x_meta_info_set_utf8(this->stream, speex_comment_keys[i].xine_metainfo_index, meta_info);
      }
    }

    c += len;
  }
}

static void speex_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;
  char *const buf_content = (char*)buf->content;

  llprintf (LOG_BUFFERS, "decode buf=%8p content=%8p flags=%08x\n",
	    buf, buf->content, buf->decoder_flags);

  if ( (buf->decoder_flags & BUF_FLAG_HEADER) &&
       !(buf->decoder_flags & BUF_FLAG_STDHEADER) ) {
    lprintf ("preview buffer, %d headers to go\n", this->header_count);

    if (this->header_count) {

      if (!this->st) {
	SpeexMode * spx_mode;
	SpeexHeader * spx_header;
	unsigned int modeID;
	int bitrate;

	speex_bits_init (&this->bits);

	spx_header = speex_packet_to_header (buf_content, buf->size);

	if (!spx_header) {
	  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: could not read Speex header\n");
	  return;
	}

	modeID = (unsigned int)spx_header->mode;
	if (modeID >= SPEEX_NB_MODES) {
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": invalid mode ID %u\n", modeID);
	  return;
	}

	spx_mode = (SpeexMode *) speex_mode_list[modeID];

	if (spx_mode->bitstream_version != spx_header->mode_bitstream_version) {
	  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: incompatible Speex mode bitstream version\n");
	  return;
	}

	this->st = speex_decoder_init (spx_mode);
	if (!this->st) {
	  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: decoder initialization failed\n");
	  return;
	}

	this->rate = spx_header->rate;
	speex_decoder_ctl (this->st, SPEEX_SET_SAMPLING_RATE, &this->rate);
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
	  this->rate);

	this->channels = spx_header->nb_channels;
	if (this->channels == 2) {
	  SpeexCallback callback;

	  callback.callback_id = SPEEX_INBAND_STEREO;
	  callback.func = speex_std_stereo_request_handler;
	  callback.data = &this->stereo;
	  speex_decoder_ctl (this->st, SPEEX_SET_HANDLER, &callback);
	}

	this->nframes = spx_header->frames_per_packet;
	if (!this->nframes) this->nframes = 1;

	speex_decoder_ctl (this->st, SPEEX_GET_FRAME_SIZE, &this->frame_size);

	speex_decoder_ctl (this->st, SPEEX_GET_BITRATE, &bitrate);
	if (bitrate <= 1) bitrate = 16000; /* assume 16 kbit */
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, bitrate);

	this->header_count += spx_header->extra_headers;
	this->expect_metadata = 1;

	free (spx_header);
      } else if (this->expect_metadata) {
	read_metadata (this, buf_content, buf->size);
      }

      this->header_count--;

      if (!this->header_count) {
        int mode = _x_ao_channels2mode(this->channels);

	if (!this->output_open) {
	  this->output_open =
	    (this->stream->audio_out->open) (this->stream->audio_out,
					  this->stream,
					  16,
					  this->rate,
					  mode);
            lprintf ("this->output_open after attempt is %d\n", this->output_open);
	}
      }
    }

  } else if (this->output_open) {
    int j;

    audio_buffer_t *audio_buffer;

    audio_buffer =
      this->stream->audio_out->get_buffer (this->stream->audio_out);

    speex_bits_read_from (&this->bits, buf_content, buf->size);

    for (j = 0; j < this->nframes; j++) {
      int ret;
      int bitrate;

      ret = speex_decode_int (this->st, &this->bits, audio_buffer->mem);

      if (ret==-1)
	break;
      if (ret==-2) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: Decoding error, corrupted stream?\n");
	break;
      }
      if (speex_bits_remaining(&this->bits)<0) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libspeex: Decoding overflow, corrupted stream?\n");
	break;
      }

      if (this->channels == 2) {
	speex_decode_stereo_int (audio_buffer->mem, this->frame_size, &this->stereo);
      }

      speex_decoder_ctl (this->st, SPEEX_GET_BITRATE, &bitrate);
      if (bitrate <= 1) bitrate = 16000; /* assume 16 kbit */
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, bitrate);

      audio_buffer->vpts       = this->pts;
      this->pts=0;
      audio_buffer->num_frames = this->frame_size;

      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      buf->pts=0;

    }
  }
  else {
    llprintf (LOG_BUFFERS, "output not open\n");
  }
}

static void speex_dispose (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

  if (this->st) {
    speex_decoder_destroy (this->st);
  }
  speex_bits_destroy (&this->bits);

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen,
				     xine_stream_t *stream) {

  speex_decoder_t *this ;
  static SpeexStereoState init_stereo = SPEEX_STEREO_STATE_INIT;

  this = (speex_decoder_t *) calloc(1, sizeof(speex_decoder_t));

  this->audio_decoder.decode_data         = speex_decode_data;
  this->audio_decoder.reset               = speex_reset;
  this->audio_decoder.discontinuity       = speex_discontinuity;
  this->audio_decoder.dispose             = speex_dispose;
  this->stream                            = stream;

  this->output_open     = 0;
  this->header_count    = 1;
  this->expect_metadata = 0;

  this->st = NULL;

  this->channels = 1;

  memcpy (&this->stereo, &init_stereo, sizeof (SpeexStereoState));

  return (audio_decoder_t *) this;
}

/*
 * speex plugin class
 */

void *speex_init_plugin (xine_t *xine, void *data) {

  speex_class_t *this;

  this = (speex_class_t *) calloc(1, sizeof(speex_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "speex";
  this->decoder_class.description     = N_("Speex audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_SPEEX, 0
 };

const decoder_info_t dec_info_speex = {
  audio_types,         /* supported types */
  5                    /* priority        */
};
