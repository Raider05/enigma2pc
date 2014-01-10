/*
 * Copyright (C) 2000-2012 the xine project
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
 */

/**
 * @file
 * @author James Courtier-Dutton <james@superbug.demon.co.uk>
 *
 * @date 2001-08-31 Added LPCM rate sensing
 */

#ifndef __sun
#define _XOPEN_SOURCE 500
#endif
/* avoid compiler warnings */
#define _BSD_SOURCE 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> /* htons */
#include <netinet/in.h> /* ntohs */

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>

#ifdef WIN32
#include <winsock.h>
/*#include <Winsock2.h>*/ /* htons */
#endif

typedef struct {
  audio_decoder_class_t   decoder_class;
} lpcm_class_t;

typedef struct lpcm_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t   *stream;

  uint32_t         rate;
  uint32_t         bits_per_sample;
  uint32_t         number_of_channels;
  uint32_t         ao_cap_mode;

  int              output_open;
  int		   cpu_be;	/* TRUE, if we're a Big endian CPU */

  int64_t          pts;

  uint8_t         *buf;
  size_t           buffered_bytes;
  size_t           buf_size;

} lpcm_decoder_t;

static void lpcm_reset (audio_decoder_t *this_gen) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;

  free (this->buf);
  this->buf = NULL;
}

static void lpcm_discontinuity (audio_decoder_t *this_gen) {

  lpcm_reset(this_gen);
}

static void lpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;
  int16_t        *sample_buffer=(int16_t *)buf->content;
  int             buf_size = buf->size;
  int             stream_be;
  audio_buffer_t *audio_buffer;
  int             format_changed = 0;
  int             special_dvd_audio = 0;

  /* Drop preview data */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* get config byte from mpeg2 stream */
  if ( (buf->decoder_flags & BUF_FLAG_SPECIAL) &&
        buf->decoder_info[1] == BUF_SPECIAL_LPCM_CONFIG ) {
    unsigned int bits_per_sample = 16;
    unsigned int sample_rate = 0;
    unsigned int num_channels;

    lprintf("lpcm_decoder: config data 0x%x\n", buf->decoder_info[2]);

    /* BluRay PCM header is 4 bytes */
    if (buf->decoder_info[2] & 0xffffff00) {
      static const uint8_t channels[16] = {0, 1, 0, 2, 3, 3, 4, 4, 5, 6, 7, 8, 0, 0, 0, 0};

      num_channels = channels[(buf->decoder_info[2] >> (16+4)) & 0x0f];
      switch ((buf->decoder_info[2] >> (24+6)) & 0x03) {
        case 1:  bits_per_sample = 16; break;
        case 2:  /*bits_per_sample = 20; break;*/
                 /* fall thru. Samples are 0-padded to 24 bits, and
                  * converted later to 16 bits by dropping 8 lowest bits.
                  * this needs to be changed if audio out some day accepts 24bit samples.
                  */
        case 3:  bits_per_sample = 24; break;
        default: bits_per_sample =  0; break;
      }
      switch ((buf->decoder_info[2] >> 16) & 0x0f) {
        case 1:  sample_rate =  48000; break;
        case 4:  sample_rate =  96000; break;
        case 5:  sample_rate = 192000; break;
        default: sample_rate =      0; break;
      }

      if (!num_channels || !sample_rate || !bits_per_sample)
        xine_log (this->stream->xine, XINE_LOG_MSG,
                  "lpcm_decoder: unsupported BluRay PCM format: 0x%08x\n", buf->decoder_info[2]);

      if (this->buffered_bytes)
        xine_log (this->stream->xine, XINE_LOG_MSG, "lpcm_decoder: %zd bytes lost !\n", this->buffered_bytes);

      if (!this->buf) {
        this->buffered_bytes = 0;
        this->buf_size       = 8128;
        this->buf            = malloc(this->buf_size);
      }

    } else {

      /* MPEG2/DVD PCM header is one byte */
      num_channels = (buf->decoder_info[2] & 0x7) + 1;
      switch ((buf->decoder_info[2]>>4) & 3) {
        case 0: sample_rate = 48000; break;
        case 1: sample_rate = 96000; break;
        case 2: sample_rate = 44100; break;
        case 3: sample_rate = 32000; break;
      }
      switch ((buf->decoder_info[2]>>6) & 3) {
        case 0: bits_per_sample = 16; break;
        case 1: bits_per_sample = 20; break;
        case 2: bits_per_sample = 24; special_dvd_audio = 1; break;
      }
    }

    if( this->bits_per_sample != bits_per_sample ||
        this->number_of_channels != num_channels ||
        this->rate != sample_rate ||
        !this->output_open ) {
      this->bits_per_sample = bits_per_sample;
      this->number_of_channels = num_channels;
      this->rate = sample_rate;
      format_changed++;

      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "lpcm_decoder: format changed to %d channels, %d bits per sample, %d Hz, %d kbit/s\n",
              num_channels, bits_per_sample, sample_rate, (num_channels * sample_rate * bits_per_sample)/1024);
    }
  }

  if( buf->decoder_flags & BUF_FLAG_STDHEADER ) {
    this->rate=buf->decoder_info[1];
    this->bits_per_sample=buf->decoder_info[2];
    this->number_of_channels=buf->decoder_info[3];
    format_changed++;
  }

  /*
   * (re-)open output device
   */
  if ( format_changed ) {
    if (this->output_open)
        this->stream->audio_out->close (this->stream->audio_out, this->stream);

    this->ao_cap_mode=_x_ao_channels2mode(this->number_of_channels);

    /* force 24-bit samples into 16 bits for now */
    if (this->bits_per_sample == 24)
      this->output_open = (this->stream->audio_out->open) (this->stream->audio_out, this->stream,
                                               16,
                                               this->rate,
                                               this->ao_cap_mode) ;
    else
      this->output_open = (this->stream->audio_out->open) (this->stream->audio_out, this->stream,
                                               this->bits_per_sample,
                                               this->rate,
                                               this->ao_cap_mode) ;

    /* stream/meta info */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Linear PCM");
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE,
        this->bits_per_sample * this->rate * this->number_of_channels);
  }

  if (!this->output_open || (buf->decoder_flags & BUF_FLAG_HEADER) )
    return;

  if (buf->pts && !this->pts)
    this->pts = buf->pts;

  /* data accumulation */
  if (this->buf) {
    int frame_end = buf->decoder_flags & BUF_FLAG_FRAME_END;
    if (this->buffered_bytes || !frame_end) {
      if (this->buf_size < this->buffered_bytes + buf->size) {
        this->buf_size *= 2;
        this->buf = realloc(this->buf, this->buf_size);
      }

      memcpy(this->buf + this->buffered_bytes, buf->content, buf->size);
      this->buffered_bytes += buf->size;

      if (!frame_end)
        return;

      sample_buffer = (int16_t*)this->buf;
      buf_size = this->buffered_bytes;
      this->buffered_bytes = 0;
    }
  }

  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

  /* Swap LPCM samples into native byte order, if necessary */
  buf->type &= 0xffff0000;
  stream_be = ( buf->type == BUF_AUDIO_LPCM_BE );

  if( this->bits_per_sample == 16 ){
    if (stream_be != this->cpu_be)
      swab (sample_buffer, audio_buffer->mem, buf_size);
    else
      memcpy (audio_buffer->mem, sample_buffer, buf_size);
  }
  else if( this->bits_per_sample == 20 ) {
    uint8_t *s = (uint8_t *)sample_buffer;
    uint8_t *d = (uint8_t *)audio_buffer->mem;
    int n = buf_size;

    if (stream_be != this->cpu_be) {
      while( n >= 0 ) {
        swab( s, d, 8 );
        s += 10;
        d += 8;
        n -= 10;
      }
    } else {
      while( n >= 0 ) {
        memcpy( d, s, 8 );
        s += 10;
        d += 8;
        n -= 10;
      }
    }
  } else if( this->bits_per_sample == 24 ) {
    uint8_t *s = (uint8_t *)sample_buffer;
    uint8_t *d = (uint8_t *)audio_buffer->mem;
    int n = buf_size;

    if ( stream_be ) {
      if (special_dvd_audio)
        while (n >= 12) {
          if ( stream_be == this->cpu_be ) {
            *d++ = s[0];
            *d++ = s[1];
            *d++ = s[2];
            *d++ = s[3];
            *d++ = s[4];
            *d++ = s[5];
            *d++ = s[6];
            *d++ = s[7];
          } else {
            *d++ = s[1];
            *d++ = s[0];
            *d++ = s[3];
            *d++ = s[2];
            *d++ = s[5];
            *d++ = s[4];
            *d++ = s[7];
            *d++ = s[6];
          }
          s += 12;
          n -= 12;
        }
      else
        while (n >= 3) {
          if ( stream_be == this->cpu_be ) {
            *d++ = s[0];
            *d++ = s[1];
          } else {
            *d++ = s[1];
            *d++ = s[0];
          }
          s += 3;
          n -= 3;
        }
    } else {
      while (n >= 3) {
        if ( stream_be == this->cpu_be ) {
          *d++ = s[1];
          *d++ = s[2];
        } else {
          *d++ = s[2];
          *d++ = s[1];
        }
        s += 3;
        n -= 3;
      }
    }

    if ( (d - (uint8_t*)audio_buffer->mem)/2*3 < buf_size )
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "lpcm_decoder: lost %i bytes of %i in the buffer\n", (int)(buf_size - (d - (uint8_t*)audio_buffer->mem)/2*3), buf_size);

  } else {
    memcpy (audio_buffer->mem, sample_buffer, buf_size);
  }

  audio_buffer->vpts       = this->pts;
  audio_buffer->num_frames = (((buf_size*8)/this->number_of_channels)/this->bits_per_sample);

  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

  this->pts = 0;
}

static void lpcm_dispose (audio_decoder_t *this_gen) {
  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  free (this->buf);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  lpcm_decoder_t *this ;

  this = (lpcm_decoder_t *) calloc(1, sizeof(lpcm_decoder_t));

  this->audio_decoder.decode_data         = lpcm_decode_data;
  this->audio_decoder.reset               = lpcm_reset;
  this->audio_decoder.discontinuity       = lpcm_discontinuity;
  this->audio_decoder.dispose             = lpcm_dispose;

  this->output_open   = 0;
  this->rate          = 0;
  this->bits_per_sample=0;
  this->number_of_channels=0;
  this->ao_cap_mode=0;
  this->stream = stream;

  this->cpu_be        = ( htons(1) == 1 );

  return &this->audio_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  lpcm_class_t *this ;

  this = (lpcm_class_t *) calloc(1, sizeof(lpcm_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "Linear PCM";
  this->decoder_class.description     = N_("Linear PCM audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_LPCM_BE, BUF_AUDIO_LPCM_LE, 0
};

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "pcm", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
