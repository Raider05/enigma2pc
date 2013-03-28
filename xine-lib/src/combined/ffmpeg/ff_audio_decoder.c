/*
 * Copyright (C) 2001-2008 the xine project
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
 * xine audio decoder plugin using ffmpeg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#define LOG_MODULE "ffmpeg_audio_dec"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"
#include "ffmpeg_decoder.h"
#include "ffmpeg_compat.h"

#define AUDIOBUFSIZE (64 * 1024)

typedef struct {
  audio_decoder_class_t   decoder_class;

  float                   gain;
} ff_audio_class_t;

typedef struct ff_audio_decoder_s {
  audio_decoder_t   audio_decoder;

  ff_audio_class_t *class;

  xine_stream_t    *stream;

  int               output_open;
  int               audio_channels;
  int               audio_bits;
  int               audio_sample_rate;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  AVCodecContext    *context;
  AVCodec           *codec;

  char              *decode_buffer;
  int               decoder_ok;

  AVCodecParserContext *parser_context;
#if AVAUDIO > 3
  AVFrame          *av_frame;
#endif
} ff_audio_decoder_t;


#include "ff_audio_list.h"

#define malloc16(s) realloc16(NULL,s)
#define free16(p) realloc16(p,0)

static void *realloc16 (void *m, size_t s) {
  unsigned long diff, diff2;
  unsigned char *p = m, *q;
  if (p) {
    diff = p[-1];
    if (s == 0) {
      free (p - diff);
      return (NULL);
    }
    q = realloc (p - diff, s + 16);
    if (!q) return (q);
    diff2 = 16 - ((unsigned long)q & 15);
    if (diff2 != diff) memmove (q + diff2, q + diff, s);
  } else {
    if (s == 0) return (NULL);
    q = malloc (s + 16);
    if (!q) return (q);
    diff2 = 16 - ((unsigned long)q & 15);
  }
  q += diff2;
  q[-1] = diff2;
  return (q);
}


static void ff_audio_ensure_buffer_size(ff_audio_decoder_t *this, int size) {
  if (size > this->bufsize) {
    this->bufsize = size + size / 2;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("ffmpeg_audio_dec: increasing buffer to %d to avoid overflow.\n"),
            this->bufsize);
    this->buf = realloc16 (this->buf, this->bufsize + FF_INPUT_BUFFER_PADDING_SIZE);
  }
}

static void ff_audio_handle_special_buffer(ff_audio_decoder_t *this, buf_element_t *buf) {
  /* prefer plain global headers */
  if (((buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM) && !this->context->extradata)
    || (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG)) {

    free (this->context->extradata);
    this->context->extradata_size = buf->decoder_info[2];
    this->context->extradata = malloc (buf->decoder_info[2] + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy (this->context->extradata, buf->decoder_info_ptr[2], buf->decoder_info[2]);
    memset (this->context->extradata + buf->decoder_info[2], 0, FF_INPUT_BUFFER_PADDING_SIZE);
  }
}

static void ff_audio_init_codec(ff_audio_decoder_t *this, unsigned int codec_type) {
  size_t i;

  this->codec = NULL;

  for(i = 0; i < sizeof(ff_audio_lookup)/sizeof(ff_codec_t); i++)
    if(ff_audio_lookup[i].type == codec_type) {
      pthread_mutex_lock (&ffmpeg_lock);
      this->codec = avcodec_find_decoder(ff_audio_lookup[i].id);
      pthread_mutex_unlock (&ffmpeg_lock);
      _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC,
                       ff_audio_lookup[i].name);
      break;
    }

  if (!this->codec) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_audio_dec: couldn't find ffmpeg decoder for buf type 0x%X\n"),
             codec_type);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
    return;
  }

  /* Try to make the following true */
  this->context->request_sample_fmt = AV_SAMPLE_FMT_S16;

  /* Current ffmpeg audio decoders usually use 16 bits/sample
   * buf->decoder_info[2] can't be used as it doesn't refer to the output
   * bits/sample for some codecs (e.g. MS ADPCM) */
  this->audio_bits = 16;

  this->context->bits_per_sample = this->audio_bits;
  this->context->sample_rate = this->audio_sample_rate;
  this->context->channels    = this->audio_channels;
  this->context->codec_id    = this->codec->id;
  this->context->codec_type  = this->codec->type;
  this->context->codec_tag   = _x_stream_info_get(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC);

  /* Use parser for EAC3, AAC LATM and MPEG.
   * Fixes:
   *  - DVB streams where multiple AAC LATM frames are packed to single PES
   *  - DVB streams where MPEG audio frames do not follow PES packet boundaries
   */
#if AVPARSE > 1
  if (codec_type == BUF_AUDIO_AAC_LATM ||
      codec_type == BUF_AUDIO_EAC3 ||
      codec_type == BUF_AUDIO_MPEG) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "ffmpeg_audio_dec: using parser\n");

    this->parser_context = av_parser_init(this->codec->id);
    if (!this->parser_context) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "ffmpeg_audio_dec: couldn't init parser\n");
    }
  }
#endif
}

static int ff_audio_open_codec(ff_audio_decoder_t *this, unsigned int codec_type) {

  if ( !this->codec ) {
    ff_audio_init_codec(this, codec_type);
  }

  if ( !this->codec ) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_audio_dec: trying to open null codec\n"));
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
    return -1;
  }

  pthread_mutex_lock (&ffmpeg_lock);
  if (avcodec_open (this->context, this->codec) < 0) {
    pthread_mutex_unlock (&ffmpeg_lock);
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_audio_dec: couldn't open decoder\n"));
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
    return -1;
  }
  pthread_mutex_unlock (&ffmpeg_lock);

  this->decoder_ok = 1;

  return 1;
}

static void ff_handle_header_buffer(ff_audio_decoder_t *this, buf_element_t *buf)
{
  unsigned int codec_type = buf->type & (BUF_MAJOR_MASK | BUF_DECODER_MASK);
  xine_waveformatex *audio_header;

  /* accumulate init data */
  ff_audio_ensure_buffer_size(this, this->size + buf->size);
  xine_fast_memcpy(this->buf + this->size, buf->content, buf->size);
  this->size += buf->size;

  if (!(buf->decoder_flags & BUF_FLAG_FRAME_END)) {
    return;
  }

  if(buf->decoder_flags & BUF_FLAG_STDHEADER) {
    this->audio_sample_rate = buf->decoder_info[1];
    this->audio_channels    = buf->decoder_info[3];

    if(this->size) {
      audio_header = (xine_waveformatex *)this->buf;

      this->context->block_align = audio_header->nBlockAlign;
      this->context->bit_rate    = audio_header->nAvgBytesPerSec * 8;

      if(audio_header->cbSize > 0) {
        this->context->extradata = malloc(audio_header->cbSize);
        this->context->extradata_size = audio_header->cbSize;
        memcpy( this->context->extradata,
                (uint8_t *)audio_header + sizeof(xine_waveformatex),
                audio_header->cbSize );
      }
    }
  } else {
    short *ptr;

    switch(codec_type) {
    case BUF_AUDIO_14_4:
      this->audio_sample_rate = 8000;
      this->audio_channels    = 1;

      this->context->block_align = 240;
      break;
    case BUF_AUDIO_28_8:
      this->audio_sample_rate = _X_BE_16(&this->buf[0x30]);
      this->audio_channels    = this->buf[0x37];
      /* this->audio_bits = buf->content[0x35] */

      this->context->block_align = _X_BE_32(&this->buf[0x18]);

      this->context->extradata_size = 5*sizeof(short);
      this->context->extradata      = malloc(this->context->extradata_size);

      ptr = (short *) this->context->extradata;

      ptr[0] = _X_BE_16(&this->buf[0x2C]); /* subpacket size */
      ptr[1] = _X_BE_16(&this->buf[0x28]); /* subpacket height */
      ptr[2] = _X_BE_16(&this->buf[0x16]); /* subpacket flavour */
      ptr[3] = _X_BE_32(&this->buf[0x18]); /* coded frame size */
      ptr[4] = 0;                          /* codec's data length  */

      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "ffmpeg_audio_dec: 28_8 audio channels %d bits %d sample rate %d block align %d\n",
              this->audio_channels, this->audio_bits, this->audio_sample_rate,
              this->context->block_align);
      break;
    case BUF_AUDIO_COOK:
      {
        int version;
        int data_len;
        int extradata;

        version = _X_BE_16 (this->buf+4);
        if (version == 4) {
          this->audio_sample_rate = _X_BE_16 (this->buf+48);
          this->audio_bits = _X_BE_16 (this->buf+52);
          this->audio_channels = _X_BE_16 (this->buf+54);
          data_len = _X_BE_32 (this->buf+67);
          extradata = 71;
        } else {
          this->audio_sample_rate = _X_BE_16 (this->buf+54);
          this->audio_bits = _X_BE_16 (this->buf+58);
          this->audio_channels = _X_BE_16 (this->buf+60);
          data_len = _X_BE_32 (this->buf+74);
          extradata = 78;
        }
        this->context->block_align = _X_BE_16 (this->buf+44);

        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "ffmpeg_audio_dec: cook audio channels %d bits %d sample rate %d block align %d\n",
                this->audio_channels, this->audio_bits, this->audio_sample_rate,
                this->context->block_align);

        if (extradata + data_len > this->size)
          break; /* abort early - extradata length is bad */
        if (extradata > INT_MAX - data_len)
          break;/*integer overflow*/

        this->context->extradata_size = data_len;
        this->context->extradata      = malloc(this->context->extradata_size +
                                               FF_INPUT_BUFFER_PADDING_SIZE);
        xine_fast_memcpy (this->context->extradata, this->buf + extradata,
                          this->context->extradata_size);
        break;
      }

    case BUF_AUDIO_EAC3:
      break;

    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "ffmpeg_audio_dec: unknown header with buf type 0x%X\n", codec_type);
      break;
    }
  }

  ff_audio_init_codec(this, codec_type);

  this->size = 0;
}

static void ff_audio_reset_parser(ff_audio_decoder_t *this)
{
  /* reset parser */
  if (this->parser_context) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "ffmpeg_audio_dec: resetting parser\n");

    pthread_mutex_lock (&ffmpeg_lock);
    av_parser_close(this->parser_context);
    this->parser_context = av_parser_init(this->codec->id);
    pthread_mutex_unlock (&ffmpeg_lock);
  }
}

static void ff_audio_output_close(ff_audio_decoder_t *this)
{
  if (this->output_open) {
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
    this->output_open = 0;
  }

  this->audio_bits = 0;
  this->audio_sample_rate = 0;
  this->audio_channels = 0;
}

static int ff_audio_decode (ff_audio_decoder_t *this,
  int16_t *decode_buffer, int *decode_buffer_size, uint8_t *buf, int size) {
  int consumed;
  int parser_consumed = 0;

#if AVPARSE > 1
  if (this->parser_context) {
    uint8_t *outbuf;
    int      outsize;

    do {
      int ret = av_parser_parse2 (this->parser_context, this->context,
        &outbuf, &outsize, buf, size, 0, 0, 0);
      parser_consumed += ret;
      buf             += ret;
      size            -= ret;
    } while (size > 0 && outsize <= 0);

    /* nothing to decode ? */
    if (outsize <= 0) {
      *decode_buffer_size = 0;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
               "ffmpeg_audio_dec: not enough data to decode\n");
      return parser_consumed;
    }

    /* decode next packet */
    buf  = outbuf;
    size = outsize;
  }
#endif /* AVPARSE > 1 */

#if AVAUDIO > 2
  AVPacket avpkt;
  av_init_packet (&avpkt);
  avpkt.data = buf;
  avpkt.size = size;
  avpkt.flags = AV_PKT_FLAG_KEY;
#  if AVAUDIO > 3
  int got_frame;
  const float gain = this->class->gain;
  if (!this->av_frame)
    this->av_frame = avcodec_alloc_frame ();

  consumed = avcodec_decode_audio4 (this->context, this->av_frame, &got_frame, &avpkt);
  if ((consumed >= 0) && got_frame) {
    int16_t *q = decode_buffer;
    int samples = this->av_frame->nb_samples;
    int channels = this->context->channels;
    int bytes, i, j;
    /* limit buffer */
    if (channels > 12)
      channels = 12;
    if (*decode_buffer_size < samples * channels * 2)
      samples = *decode_buffer_size / (channels * 2);
    bytes = samples * channels * 2;
    *decode_buffer_size = bytes;
    /* convert to packed int16_t. I guess there is something
       in libavfilter but also another dependency... */
    switch (this->context->sample_fmt) {
      case AV_SAMPLE_FMT_U8P:
        if (channels > 1) {
          uint8_t *p[12];
          for (i = 0; i < channels; i++)
            p[i] = (uint8_t *)this->av_frame->extended_data[i];
          for (i = samples; i; i--) {
            for (j = 0; j < channels; j++)
              *q++ = ((uint16_t)(*p[j]++) << 8) ^ 0x8000;
          }
          break;
        }
      case AV_SAMPLE_FMT_U8:
        {
          uint8_t *p = (uint8_t *)this->av_frame->extended_data[0];
          for (i = samples * channels; i; i--)
            *q++ = ((uint16_t)(*p++) << 8) ^ 0x8000;
        }
      break;
      case AV_SAMPLE_FMT_S16P:
        if (channels > 1) {
          int16_t *p[12];
          for (i = 0; i < channels; i++)
            p[i] = (int16_t *)this->av_frame->extended_data[i];
          for (i = samples; i; i--) {
            for (j = 0; j < channels; j++)
              *q++ = *p[j]++;
          }
          break;
        }
      case AV_SAMPLE_FMT_S16:
        xine_fast_memcpy (q, this->av_frame->extended_data[0], bytes);
      break;
      case AV_SAMPLE_FMT_S32P:
        if (channels > 1) {
          int32_t *p[12];
          for (i = 0; i < channels; i++)
            p[i] = (int32_t *)this->av_frame->extended_data[i];
          for (i = samples; i; i--) {
            for (j = 0; j < channels; j++)
              *q++ = *p[j]++ >> 16;
          }
          break;
        }
      case AV_SAMPLE_FMT_S32:
        {
          int32_t *p = (int32_t *)this->av_frame->extended_data[0];
          for (i = samples * channels; i; i--)
            *q++ = *p++ >> 16;
        }
      break;
      case AV_SAMPLE_FMT_FLTP: /* the most popular one */
        if (channels > 1) {
          float *p[12];
          for (i = 0; i < channels; i++)
            p[i] = (float *)this->av_frame->extended_data[i];
          for (i = samples; i; i--) {
            for (j = 0; j < channels; j++) {
              int v = *p[j]++ * gain;
              *q++ = (v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v;
            }
          }
          break;
        }
      case AV_SAMPLE_FMT_FLT:
        {
          float *p = (float *)this->av_frame->extended_data[0];
          for (i = samples * channels; i; i--) {
            int v = *p++ * gain;
            *q++ = (v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v;
          }
        }
      break;
      default: ;
    }
  } else *decode_buffer_size = 0;
#  else
  consumed = avcodec_decode_audio3 (this->context, decode_buffer, decode_buffer_size, &avpkt);
#  endif
#else
  consumed = avcodec_decode_audio2 (this->context, decode_buffer, decode_buffer_size, buf, size);
#endif

  if (consumed < 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "ffmpeg_audio_dec: error decompressing audio frame (%d)\n", consumed);
  } else if (parser_consumed && consumed != size) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "ffmpeg_audio_dec: decoder didn't consume all data\n");
  }

  return parser_consumed ? parser_consumed : consumed;
}

static void ff_audio_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  ff_audio_decoder_t *this = (ff_audio_decoder_t *) this_gen;
  int bytes_consumed;
  int decode_buffer_size;
  int offset;
  int out;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;
  unsigned int codec_type = buf->type & (BUF_MAJOR_MASK | BUF_DECODER_MASK);

  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    ff_audio_handle_special_buffer(this, buf);
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    ff_handle_header_buffer(this, buf);
    return;

  } else {

    if( !this->decoder_ok ) {
      if (ff_audio_open_codec(this, codec_type) < 0) {
	return;
      }
    }

    if( buf->decoder_flags & BUF_FLAG_PREVIEW )
      return;

    ff_audio_ensure_buffer_size(this, this->size + buf->size);
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;

    if (this->parser_context || buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */

      offset = 0;

      /* pad input data */
      memset(&this->buf[this->size], 0, FF_INPUT_BUFFER_PADDING_SIZE);

      while (this->size>=0) {
        decode_buffer_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;

	bytes_consumed =
          ff_audio_decode(this,
                          (int16_t *)this->decode_buffer, &decode_buffer_size,
                          &this->buf[offset], this->size);

        if (bytes_consumed<0) {
          this->size=0;
          return;
        } else if (bytes_consumed == 0 && decode_buffer_size == 0) {
          if (offset)
            memmove(this->buf, &this->buf[offset], this->size);
          return;
        }

        if (this->audio_bits        != this->context->bits_per_sample ||
            this->audio_sample_rate != this->context->sample_rate ||
            this->audio_channels    != this->context->channels) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  _("ffmpeg_audio_dec: codec parameters changed\n"));
          /* close if it was open, and always trigger 1 new open attempt below */
          ff_audio_output_close(this);
        }

	if (!this->output_open) {
	  if (!this->audio_bits || !this->audio_sample_rate || !this->audio_channels) {
	    this->audio_bits = this->context->bits_per_sample;
	    this->audio_sample_rate = this->context->sample_rate;
	    this->audio_channels = this->context->channels;
	  }
	  if (!this->audio_bits || !this->audio_sample_rate || !this->audio_channels) {
	    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		    _("ffmpeg_audio_dec: cannot read codec parameters from packet\n"));
	    /* try to decode next packet. */
	    /* there shouldn't be any output yet */
	    decode_buffer_size = 0;
	    /* pts applies only to first audio packet */
	    buf->pts = 0;
	  } else {
	    this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
								 this->stream, this->audio_bits, this->audio_sample_rate,
								 _x_ao_channels2mode(this->audio_channels));
	    if (!this->output_open) {
	      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		      "ffmpeg_audio_dec: error opening audio output\n");
	      this->size = 0;
	      return;
	    }
	  }
	}

        /* dispatch the decoded audio */
        out = 0;
        while (out < decode_buffer_size) {
          int stream_status = xine_get_status(this->stream);

	  if (stream_status == XINE_STATUS_QUIT || stream_status == XINE_STATUS_STOP) {
	    this->size = 0;
            return;
	  }

          audio_buffer =
            this->stream->audio_out->get_buffer (this->stream->audio_out);
          if (audio_buffer->mem_size == 0) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                     "ffmpeg_audio_dec: Help! Allocated audio buffer with nothing in it!\n");
            return;
          }

          /* fill up this buffer */
#if AVAUDIO < 4
          if (codec_type == BUF_AUDIO_WMAPRO) {
            /* the above codecs output float samples, not 16-bit integers */
            int bytes_per_sample = sizeof(float);
            if (((decode_buffer_size - out) * 2 / bytes_per_sample) > audio_buffer->mem_size)
              bytes_to_send = audio_buffer->mem_size * bytes_per_sample / 2;
            else
              bytes_to_send = decode_buffer_size - out;

            int16_t *int_buffer = calloc(1, bytes_to_send * 2 / bytes_per_sample);
            int i;
            for (i = 0; i < (bytes_to_send / bytes_per_sample); i++) {
              float *float_sample = (float *)&this->decode_buffer[i * bytes_per_sample + out];
              int_buffer[i] = (int16_t)lrintf(*float_sample * 32768.);
            }

            out += bytes_to_send;
            bytes_to_send = bytes_to_send * 2 / bytes_per_sample;
            xine_fast_memcpy(audio_buffer->mem, int_buffer, bytes_to_send);
            free(int_buffer);
          } else
#endif
          {
            if ((decode_buffer_size - out) > audio_buffer->mem_size)
              bytes_to_send = audio_buffer->mem_size;
            else
              bytes_to_send = decode_buffer_size - out;

            xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[out], bytes_to_send);
            out += bytes_to_send;
          }

          /* byte count / 2 (bytes / sample) / channels */
          audio_buffer->num_frames = bytes_to_send / 2 / this->audio_channels;

          audio_buffer->vpts = buf->pts;

          buf->pts = 0;  /* only first buffer gets the real pts */
          this->stream->audio_out->put_buffer (this->stream->audio_out,
            audio_buffer, this->stream);
        }

        this->size -= bytes_consumed;
        offset += bytes_consumed;
      }

      /* reset internal accumulation buffer */
      this->size = 0;
    }
  }
}

static void ff_audio_reset (audio_decoder_t *this_gen) {
  ff_audio_decoder_t *this = (ff_audio_decoder_t *) this_gen;

  this->size = 0;

  /* try to reset the wma decoder */
  if( this->decoder_ok ) {
#if AVAUDIO > 3
    avcodec_free_frame (&this->av_frame);
#endif
    pthread_mutex_lock (&ffmpeg_lock);
    avcodec_close (this->context);
    if (avcodec_open (this->context, this->codec) < 0)
      this->decoder_ok = 0;
    pthread_mutex_unlock (&ffmpeg_lock);
  }

  ff_audio_reset_parser(this);
}

static void ff_audio_discontinuity (audio_decoder_t *this_gen) {

  ff_audio_decoder_t *this = (ff_audio_decoder_t *) this_gen;

  this->size = 0;

  ff_audio_reset_parser(this);
}

static void ff_audio_dispose (audio_decoder_t *this_gen) {

  ff_audio_decoder_t *this = (ff_audio_decoder_t *) this_gen;

  if (this->parser_context) {
    pthread_mutex_lock (&ffmpeg_lock);
    av_parser_close(this->parser_context);
    this->parser_context = NULL;
    pthread_mutex_unlock (&ffmpeg_lock);
  }

  if( this->context && this->decoder_ok ) {
#if AVAUDIO > 3
    avcodec_free_frame (&this->av_frame);
#endif
    pthread_mutex_lock (&ffmpeg_lock);
    avcodec_close (this->context);
    pthread_mutex_unlock (&ffmpeg_lock);
  }

  ff_audio_output_close(this);

  free16 (this->buf);
  free16 (this->decode_buffer);

  if(this->context && this->context->extradata)
    free(this->context->extradata);

  if(this->context)
    av_free(this->context);

  free (this_gen);
}

static audio_decoder_t *ff_audio_open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  ff_audio_decoder_t *this ;

  this = calloc(1, sizeof (ff_audio_decoder_t));

  this->class = (ff_audio_class_t *)class_gen;

  this->audio_decoder.decode_data         = ff_audio_decode_data;
  this->audio_decoder.reset               = ff_audio_reset;
  this->audio_decoder.discontinuity       = ff_audio_discontinuity;
  this->audio_decoder.dispose             = ff_audio_dispose;

  this->output_open = 0;
  this->audio_channels = 0;
  this->stream = stream;
  this->buf = NULL;
  this->size = 0;
  this->bufsize = 0;
  this->decoder_ok = 0;

  ff_audio_ensure_buffer_size(this, AUDIOBUFSIZE);

  this->context = avcodec_alloc_context();
  this->decode_buffer = malloc16 (AVCODEC_MAX_AUDIO_FRAME_SIZE);
#if AVAUDIO > 3
  this->av_frame = NULL;
#endif
  return &this->audio_decoder;
}

static void ff_gain_cb (void *user_data, xine_cfg_entry_t *entry) {
  ff_audio_class_t *class = (ff_audio_class_t *)user_data;

  class->gain = (float)0x7fff * powf ((float)10, (float)entry->num_value / (float)20);
}

void *init_audio_plugin (xine_t *xine, void *data) {

  ff_audio_class_t *this ;

  this = calloc(1, sizeof (ff_audio_class_t));

  this->decoder_class.open_plugin     = ff_audio_open_plugin;
  this->decoder_class.identifier      = "ffmpeg audio";
  this->decoder_class.description     = N_("ffmpeg based audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  pthread_once( &once_control, init_once_routine );

  this->gain = (float)0x7fff * powf ((float)10, (float)
    xine->config->register_num (xine->config,
      "audio.processing.ffmpeg_gain_dB", -3,
      _("FFmpeg audio gain (dB)"),
      _("Some AAC and WMA tracks are encoded too loud and thus play distorted.\n"
        "This cannot be fixed by volume control, but by this setting."),
      10, ff_gain_cb, this)
    / (float)20);

  return this;
}

decoder_info_t dec_info_ffmpeg_audio = {
  supported_audio_types,   /* supported types */
  7                        /* priority        */
};
