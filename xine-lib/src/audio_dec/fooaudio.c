/*
 * Copyright (C) 2000-2001 the xine project
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
 * fooaudio.c: This is a reference audio decoder for the xine multimedia
 * player. It really works too! It will output a continuous sine wave in
 * place of the data it should actually send.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"

/* math.h required for fooaudio sine wave generation */
#include <math.h>

#define AUDIOBUFSIZE 128*1024

typedef struct {
  audio_decoder_class_t   decoder_class;
} fooaudio_class_t;

typedef struct fooaudio_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;

  int              sample_rate;       /* audio sample rate */
  int              bits_per_sample;   /* bits/sample, usually 8 or 16 */
  int              channels;          /* 1 or 2, usually */

  int              output_open;       /* flag to indicate audio is ready */

  unsigned char   *buf;               /* data accumulation buffer */
  int              bufsize;           /* maximum size of buf */
  int              size;              /* size of accumulated data in buf */

  /* fooaudio-specific variables */
  int64_t           last_pts;
  unsigned int      iteration;

} fooaudio_decoder_t;

/**************************************************************************
 * fooaudio specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine audio plugin functions
 *************************************************************************/

static void fooaudio_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  fooaudio_decoder_t *this = (fooaudio_decoder_t *) this_gen;
  audio_buffer_t *audio_buffer;
  int i;
  int64_t samples_to_generate;
  int samples_to_send;

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {

    /* When the engine sends a BUF_FLAG_HEADER flag, it is time to initialize
     * the decoder. The buffer element type has 4 decoder_info fields,
     * 0..3. Field 1 is the sample rate. Field 2 is the bits/sample. Field
     * 3 is the number of channels. */
    this->sample_rate = buf->decoder_info[1];
    this->bits_per_sample = buf->decoder_info[2];
    this->channels = buf->decoder_info[3];

    /* initialize the data accumulation buffer */
    this->buf = calloc(1, AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    /* take this opportunity to initialize stream/meta information */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "fooaudio");

    /* perform any other required initialization */
    this->last_pts = -1;
    this->iteration = 0;

    return;
  }

  /* if the audio output is not open yet, open the audio output */
#warning: Audio output is hardcoded to mono 16-bit PCM
  if (!this->output_open) {
    this->output_open = (this->stream->audio_out->open) (
      this->stream->audio_out,
      this->stream,
/*      this->bits_per_sample, */
      16,
      this->sample_rate,
/*      _x_ao_channels2mode(this->channels));*/
      AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, do not go any further with the decode */
  if (!this->output_open)
    return;

  /* accumulate the data passed through the buffer element type; increase
   * the accumulator buffer size as necessary */
  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "fooaudio: increasing source buffer to %d to avoid overflow.\n", this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  /* When a buffer element type has the BUF_FLAG_FRAME_END flag set, it is
   * time to decode the data in the buffer. */
  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

    /* This is where the real meat of the audio decoder is implemented.
     * The general strategy is to decode the data in the accumulation buffer
     * into raw PCM data and then dispatch the PCM to the engine in smaller
     * buffers. What follows in the inside of this scope is the meat of
     * this particular audio decoder. */

    /* Operation of the fooaudio decoder:
     * This decoder generates a continuous sine pattern based on the pts
     * values sent by the xine engine. Two pts values are needed to know
     * how long to make the audio. Thus, If this is the first frame or
     * a seek has occurred (indicated by this->last_pts = -1),
     * log the pts but do not create any audio.
     *
     * When a valid pts delta is generated, create n audio samples, where
     * n is given as:
     *
     *       n          pts delta
     *  -----------  =  ---------  =>  n = (pts delta * sample rate) / 90000
     *  sample rate       90000
     *
     */

    if (this->last_pts != -1) {

      /* no real reason to set this variable to 0 first; I just wanted the
       * novelty of using all 4 basic arithmetic ops in a row (+ - * /) */
      samples_to_generate = 0;
      samples_to_generate += buf->pts;
      samples_to_generate -= this->last_pts;
      samples_to_generate *= this->sample_rate;
      samples_to_generate /= 90000;

      /* save the pts now since it will likely be trashed later */
      this->last_pts = buf->pts;

      while (samples_to_generate) {

        /* get an audio buffer */
        audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
        if (audio_buffer->mem_size == 0) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		   "fooaudio: Help! Allocated audio buffer with nothing in it!\n");
          return;
        }

        /* samples_to_generate is a sample count; mem_size is a byte count */
        if (samples_to_generate > audio_buffer->mem_size / 2)
          samples_to_send = audio_buffer->mem_size / 2;
        else
          samples_to_send = samples_to_generate;
        samples_to_generate -= samples_to_send;

#define WAVE_HZ 300
        /* fill up the samples in the buffer */
        for (i = 0; i < samples_to_send; i++)
          audio_buffer->mem[i] =
            (short)(sin(2 * M_PI * this->iteration++ / WAVE_HZ) * 32767);

        /* final prep for audio buffer dispatch */
        audio_buffer->num_frames = samples_to_send;
        audio_buffer->vpts = buf->pts;
        buf->pts = 0;  /* only first buffer gets the real pts */
        this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      }
    } else {
      /* log the pts for the next time */
      this->last_pts = buf->pts;
    }

    /* reset data accumulation buffer */
    this->size = 0;
  }
}

/* This function resets the state of the audio decoder. This usually
 * entails resetting the data accumulation buffer. */
static void fooaudio_reset (audio_decoder_t *this_gen) {

  fooaudio_decoder_t *this = (fooaudio_decoder_t *) this_gen;

  this->size = 0;

  /* this is specific to fooaudio */
  this->last_pts = -1;
}

/* This function resets the last pts value of the audio decoder. */
static void fooaudio_discontinuity (audio_decoder_t *this_gen) {

  fooaudio_decoder_t *this = (fooaudio_decoder_t *) this_gen;

  /* this is specific to fooaudio */
  this->last_pts = -1;
}

/* This function closes the audio output and frees the private audio decoder
 * structure. */
static void fooaudio_dispose (audio_decoder_t *this_gen) {

  fooaudio_decoder_t *this = (fooaudio_decoder_t *) this_gen;

  /* close the audio output */
  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  /* free anything that was allocated during operation */
  free(this->buf);
  free(this);
}

/* This function allocates, initializes, and returns a private audio
 * decoder structure. */
static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  fooaudio_decoder_t *this ;

  this = (fooaudio_decoder_t *) calloc(1, sizeof(fooaudio_decoder_t));

  /* connect the member functions */
  this->audio_decoder.decode_data         = fooaudio_decode_data;
  this->audio_decoder.reset               = fooaudio_reset;
  this->audio_decoder.discontinuity       = fooaudio_discontinuity;
  this->audio_decoder.dispose             = fooaudio_dispose;

  /* connect the stream */
  this->stream = stream;

  /* audio output is not open at the start */
  this->output_open = 0;

  /* initialize the basic audio parameters */
  this->channels = 0;
  this->sample_rate = 0;
  this->bits_per_sample = 0;

  /* initialize the data accumulation buffer */
  this->buf = NULL;
  this->bufsize = 0;
  this->size = 0;

  /* return the newly-initialized audio decoder */
  return &this->audio_decoder;
}

/* This function frees the audio decoder class and any other memory that was
 * allocated. */
static void dispose_class (audio_decoder_class_t *this_gen) {

  fooaudio_class_t *this = (fooaudio_class_t *)this_gen;

  free (this);
}

/* This function allocates a private audio decoder class and initializes
 * the class's member functions. */
static void *init_plugin (xine_t *xine, void *data) {

  fooaudio_class_t *this ;

  this = (fooaudio_class_t *) xine_malloc (sizeof (fooaudio_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "fooaudio";
  this->decoder_class.description     = N_("fooaudio: reference xine audio decoder plugin");
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* This is a list of all of the internal xine audio buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0. */
static const uint32_t audio_types[] = {
  /* BUF_AUDIO_FOO, */
  0
};

/* This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n). */
static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

/* The plugin catalog entry. This is the only information that this plugin
 * will export to the public. */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API version, "name", version, special_info, init_function }, */
  { PLUGIN_AUDIO_DECODER, 16, "fooaudio", XINE_VERSION_CODE, &dec_info_audio, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

