/*
 * Copyright (C) 2001-2013 the xine project
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

#define MAX_CHANNELS 6

typedef struct {
  audio_decoder_class_t   decoder_class;

  float                   gain;
} ff_audio_class_t;

typedef struct ff_audio_decoder_s {
  audio_decoder_t   audio_decoder;

  ff_audio_class_t *class;

  xine_stream_t    *stream;

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

  /* decoder settings */
  int               ff_channels;
  int               ff_bits;
  int               ff_sample_rate;
  int64_t           ff_map;

  /* channel mixer settings */
  /* map[ao_channel] = ff_channel */
  int8_t            map[MAX_CHANNELS];
  int8_t            left[4], right[4];
  /* how many left[] / right[] entries are in use */
  int               front_mixes;
  /* volume adjustment */
  int               downmix_shift;

  /* audio out settings */
  int               output_open;
  int               ao_channels;
  int               new_mode;
  int               ao_mode;
  int               ao_caps;

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

#if AVAUDIO < 4
  /* Try to make the following true */
  this->context->request_sample_fmt = AV_SAMPLE_FMT_S16;
  /* For lavc v54+, we have our channel mixer that wants default float samples to fix
    oversaturation via audio gain. */
#endif

  /* Current ffmpeg audio decoders usually use 16 bits/sample
   * buf->decoder_info[2] can't be used as it doesn't refer to the output
   * bits/sample for some codecs (e.g. MS ADPCM) */
  this->ff_bits = 16;

  this->context->bits_per_sample = this->ff_bits;
  this->context->sample_rate = this->ff_sample_rate;
  this->context->channels    = this->ff_channels;
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
    this->ff_sample_rate = buf->decoder_info[1];
    this->ff_channels    = buf->decoder_info[3];

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
    switch (codec_type) {

      case BUF_AUDIO_14_4:
        this->ff_sample_rate = 8000;
        this->ff_channels    = 1;

        this->context->block_align = 240;
        break;

      case BUF_AUDIO_28_8:
        {
          uint16_t *ptr;

          this->ff_sample_rate = _X_BE_16 (&this->buf[0x30]);
          this->ff_channels    = this->buf[0x37];
          /* this->ff_bits = buf->content[0x35] */

          this->context->block_align = _X_BE_32 (&this->buf[0x18]);

          this->context->extradata_size = 5 * sizeof (uint16_t);
          this->context->extradata      = malloc (this->context->extradata_size);

          ptr = (uint16_t *)this->context->extradata;

          ptr[0] = _X_BE_16 (&this->buf[0x2C]); /* subpacket size */
          ptr[1] = _X_BE_16 (&this->buf[0x28]); /* subpacket height */
          ptr[2] = _X_BE_16 (&this->buf[0x16]); /* subpacket flavour */
          ptr[3] = _X_BE_32 (&this->buf[0x18]); /* coded frame size */
          ptr[4] = 0;                           /* codec's data length */

          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "ffmpeg_audio_dec: 28_8 audio channels %d bits %d sample rate %d block align %d\n",
            this->ff_channels, this->ff_bits, this->ff_sample_rate, this->context->block_align);
          break;
        }

      case BUF_AUDIO_COOK:
      case BUF_AUDIO_ATRK:
        {
          int version, subpacket_size = 0, coded_frame_size = 0, intl = 0;
          int data_len;
          uint8_t *p, *e;
          p = this->buf;
          e = p + this->size;
          if (p + 6 > e) break;
          version = p[5];
          if (version == 3) {
            this->ff_sample_rate = 8000;
            this->ff_bits = 16;
            this->ff_channels = 1;
            data_len = 0;
          } else if (version == 4) {
            if (p + 73 > e) break;
            coded_frame_size = _X_BE_32 (p + 24);
            subpacket_size = _X_BE_16 (p + 44);
            this->ff_sample_rate = _X_BE_16 (p + 48);
            this->ff_bits = _X_BE_16 (p + 52);
            this->ff_channels = _X_BE_16 (p + 54);
            if (p[56] != 4) break;
            intl = 57;
            if (p[61] != 4) break;
            data_len = _X_BE_32 (p + 69);
            p += 73;
          } else {
            if (p + 78 > e) break;
            coded_frame_size = _X_BE_32 (p + 24);
            subpacket_size = _X_BE_16 (p + 44);
            this->ff_sample_rate = _X_BE_16 (p + 54);
            this->ff_bits = _X_BE_16 (p + 58);
            this->ff_channels = _X_BE_16 (p + 60);
            intl = 62;
            data_len = _X_BE_32 (p + 74);
            p += 78;
          }
          this->context->block_align = intl && !memcmp (this->buf + intl, "genr", 4) ?
            subpacket_size : coded_frame_size;
          if (p + data_len > e) break;
          if (p > e - data_len) break;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "ffmpeg_audio_dec: %s audio channels %d bits %d sample rate %d block align %d\n",
            codec_type == BUF_AUDIO_COOK ? "cook" : "atrac 3",
            this->ff_channels, this->ff_bits, this->ff_sample_rate,
            this->context->block_align);
          if (!data_len) break;
          e = malloc (data_len + FF_INPUT_BUFFER_PADDING_SIZE);
          if (!e) break;
          xine_fast_memcpy (e, p, data_len);
          memset (e + data_len, 0, FF_INPUT_BUFFER_PADDING_SIZE);
          this->context->extradata = e;
          this->context->extradata_size = data_len;
          break;
        }

      case BUF_AUDIO_EAC3:
        break;

      default:
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
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

  this->ff_sample_rate = 0;
  this->ao_mode = 0;
}

static void ff_map_channels (ff_audio_decoder_t *this) {
  uint64_t ff_map;
  int caps = this->stream->audio_out->get_capabilities (this->stream->audio_out);

  /* safety kludge for very old libavcodec */
#ifdef AV_CH_FRONT_LEFT
  ff_map = this->context->channel_layout;
  if (!ff_map) /* wma2 bug */
#endif
    ff_map = (1 << this->context->channels) - 1;

  if ((caps != this->ao_caps) || (ff_map != this->ff_map)) {
    int i, j;
    /* ff: see names[] below; xine: L R RL RR C LFE */
    const int8_t base_map[] = {0, 1, 4, 5, 2, 3, -1, -1, -1, 2, 3};
    int8_t name_map[MAX_CHANNELS];
    const int modes[] = {
      AO_CAP_MODE_MONO, AO_CAP_MODE_STEREO,
      AO_CAP_MODE_4CHANNEL, AO_CAP_MODE_4_1CHANNEL,
      AO_CAP_MODE_5CHANNEL, AO_CAP_MODE_5_1CHANNEL
    };
    const int num_modes = sizeof (modes) / sizeof (modes[0]);
    const int8_t mode_channels[]   = {1, 2, 4, 6, 6, 6};
    const int8_t wishlist[] = {
      0, 1, 2, 3, 4, 5, /* mono */
      1, 2, 3, 4, 5, 0, /* stereo */
      5, 4, 3, 2, 1, 0, /* center + lfe */
      4, 5, 2, 3, 1, 0, /* center */
      3, 5, 2, 4, 1, 0, /* lfe */
      2, 3, 4, 5, 1, 0  /* 4.0 */
    };
    const int8_t *tries;

    this->ao_caps     = caps;
    this->ff_map      = ff_map;
    this->ff_channels = this->context->channels;

    /* silence out */
    for (i = 0; i < MAX_CHANNELS; i++)
      this->map[i] = -1;
    for (i = 0; i < 4; i++)
      this->left[i] = this->right[i] = -1;

    /* set up raw map and ao mode wishlist */
    if (this->ff_channels == 1) { /* mono */
      name_map[0] = 2;
      this->left[0] = this->right[0] = 0;
      tries = wishlist + 0 * num_modes;
    } else if (this->ff_channels == 2) { /* stereo */
      name_map[0] = 0;
      name_map[1] = 1;
      this->left[0] = 0;
      this->right[0] = 1;
      tries = wishlist + 1 * num_modes;
    } else {
      for (i = j = 0; i < sizeof (base_map) / sizeof (base_map[0]); i++) {
        if ((ff_map >> i) & 1) {
          int8_t target = base_map[i];
          if ((target >= 0) && (this->map[target] < 0))
            this->map[target] = j;
          name_map[j] = i; /* for debug output below */
          j++;
        }
      }
      this->left[0]  = this->map[0] < 0 ? 0 : this->map[0];
      this->map[0]   = -1;
      this->right[0] = this->map[1] < 0 ? 1 : this->map[1];
      this->map[1]   = -1;
      tries = wishlist
        + (2 + (this->map[4] < 0 ? 2 : 0) + (this->map[5] < 0 ? 1 : 0)) * num_modes;
    }
    this->front_mixes = 1;

    /* find ao mode */
    for (i = 0; i < num_modes; i++) if (caps & modes[tries[i]]) break;
    i = i == num_modes ? 1 : tries[i];
    this->new_mode = modes[i];
    this->ao_channels = mode_channels[i];

    /* mix center to front */
    if ((this->map[4] >= 0) && !((0x30 >> i) & 1)) {
      this->left[this->front_mixes]    = this->map[4];
      this->right[this->front_mixes++] = this->map[4];
      this->map[4] = -1;
    }
    /* mix lfe to front */
    if ((this->map[5] >= 0) && !((0x28 >> i) & 1)) {
      this->left[this->front_mixes]    = this->map[5];
      this->right[this->front_mixes++] = this->map[5];
      this->map[5] = -1;
    }
    /* mix surround to front */
    if ((this->map[2] >= 0) && (this->map[3] >= 0) && !((0x3c >> i) & 1)) {
      this->left[this->front_mixes]    = this->map[2];
      this->right[this->front_mixes++] = this->map[3];
      this->map[2] = -1;
      this->map[3] = -1;
    }

    this->downmix_shift = this->front_mixes > 1 ? 1 : 0;
    /* this will be on the safe side but usually too soft?? */
#if 0
    if (this->front_mixes > 2)
      this->downmix_shift = 2;
#endif

    if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG) {
      const int8_t *names[] = {
        "left", "right", "center", "bass",
        "rear left", "rear right",
        "half left", "half right",
        "rear center",
        "side left", "side right"
      };
      int8_t buf[200];
      int p = sprintf (buf, "ff_audio_dec: channel layout: ");
      int8_t *indx = this->left;
      for (i = 0; i < 2; i++) {
        buf[p++] = '[';
        for (j = 0; j < this->front_mixes; j++)
          p += sprintf (buf + p, "%s%s", names[name_map[indx[j]]], (j < this->front_mixes - 1) ? " + " : "");
        buf[p++] = ']';
        buf[p++] = ' ';
        indx = this->right;
      }
      for (i = 2; i < this->ao_channels; i++)
        p += sprintf (buf + p, "[%s] ",
          ((this->map[i] < 0) || (this->map[i] > 5)) ? (const int8_t *)"-" : names[name_map[this->map[i]]]);
      buf[p++] = '\n';
      fwrite (buf, 1, p, stdout);
    }
  }
}

#define CLIP_16(v) ((v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v)

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
  float gain = this->class->gain;
  if (!this->av_frame)
    this->av_frame = avcodec_alloc_frame ();

  consumed = avcodec_decode_audio4 (this->context, this->av_frame, &got_frame, &avpkt);
  if ((consumed >= 0) && got_frame) {
    /* setup may have altered while decoding */
    ff_map_channels (this);

    int16_t *q = decode_buffer;
    int samples = this->av_frame->nb_samples;
    int channels = this->ao_channels;
    int bytes, i, j, shift = this->downmix_shift;
    /* limit buffer */
    if (*decode_buffer_size < samples * channels * 2)
      samples = *decode_buffer_size / (channels * 2);
    bytes = samples * channels * 2;
    *decode_buffer_size = bytes;
    /* TJ. convert to packed int16_t while respecting the user's speaker arrangement.
       I tried to speed up and not to pull in libswresample. */
    for (i = 2; i < channels; i++) if (this->map[i] < 0) {
      /* clear if there is an upmix mute channel */
      memset (q, 0, bytes);
      break;
    }
    /* For mono output, downmix to stereo first */
    if ((channels == 1) && (this->ff_channels > 1))
      channels = 2;
    gain /= (float)(1 << shift);
    switch (this->context->sample_fmt) {
#define MIX_AUDIO(stype,planar,idx,num,dindx) {\
    stype *p1, *p2, *p3, *p4;\
    int i, sstep;\
    int8_t *x = idx;\
    int16_t *dptr = (int16_t *)decode_buffer + dindx;\
    if (planar) {\
      p1 = (stype *)this->av_frame->extended_data[x[0]];\
      sstep = 1;\
    } else {\
      p1 = (stype *)this->av_frame->extended_data[0] + x[0];\
      sstep = this->ff_channels;\
    }\
    if (num == 1) {\
      if (p1) for (i = 0; i < samples; i++) {\
        int32_t v = MIX_FIX(*p1);\
        p1       += sstep;\
        v       >>= shift;\
        *dptr     = (v);\
        dptr     += channels;\
      }\
    } else {\
      if (planar)\
        p2 = (stype *)this->av_frame->extended_data[x[1]];\
      else\
        p2 = (stype *)this->av_frame->extended_data[0] + x[1];\
      if (num == 2) {\
        if (p1 && p2) for (i = 0; i < samples; i++) {\
          int32_t v = MIX_FIX(*p1);\
          p1       += sstep;\
          v        += MIX_FIX(*p2);\
          p2       += sstep;\
          v       >>= shift;\
          *dptr     = CLIP_16(v);\
          dptr     += channels;\
        }\
      } else {\
        if (planar)\
          p3 = (stype *)this->av_frame->extended_data[x[2]];\
        else\
          p3 = (stype *)this->av_frame->extended_data[0] + x[2];\
        if (num == 3) {\
          if (p1 && p2 && p3) for (i = 0; i < samples; i++) {\
            int32_t v = MIX_FIX(*p1);\
            p1       += sstep;\
            v        += MIX_FIX(*p2);\
            p2       += sstep;\
            v        += MIX_FIX(*p3);\
            p3       += sstep;\
            v       >>= shift;\
            *dptr     = CLIP_16(v);\
            dptr     += channels;\
          }\
        } else {\
          if (planar)\
            p4 = (stype *)this->av_frame->extended_data[x[3]];\
          else\
            p4 = (stype *)this->av_frame->extended_data[0] + x[3];\
          if (p1 && p2 && p3 && p4) for (i = 0; i < samples; i++) {\
            int32_t v = MIX_FIX(*p1);\
            p1       += sstep;\
            v        += MIX_FIX(*p2);\
            p2       += sstep;\
            v        += MIX_FIX(*p3);\
            p3       += sstep;\
            v        += MIX_FIX(*p4);\
            p4       += sstep;\
            v       >>= shift;\
            *dptr     = CLIP_16(v);\
            dptr     += channels;\
          }\
        }\
      }\
    }\
  }
#define MIX_FIX(v) (((int16_t)(v)<<8)^0x8000)
      case AV_SAMPLE_FMT_U8P:
        MIX_AUDIO (uint8_t, 1, this->left,  this->front_mixes, 0);
        MIX_AUDIO (uint8_t, 1, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (uint8_t, 1, this->map + j, 1, j);
      break;
      case AV_SAMPLE_FMT_U8:
        MIX_AUDIO (uint8_t, 0, this->left,  this->front_mixes, 0);
        MIX_AUDIO (uint8_t, 0, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (uint8_t, 0, this->map + j, 1, j);
      break;
#undef MIX_FIX
#define MIX_FIX(v) (v)
      case AV_SAMPLE_FMT_S16P:
        MIX_AUDIO (int16_t, 1, this->left,  this->front_mixes, 0);
        MIX_AUDIO (int16_t, 1, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (int16_t, 1, this->map + j, 1, j);
      break;
      case AV_SAMPLE_FMT_S16:
        MIX_AUDIO (int16_t, 0, this->left,  this->front_mixes, 0);
        MIX_AUDIO (int16_t, 0, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (int16_t, 0, this->map + j, 1, j);
      break;
#undef MIX_FIX
#define MIX_FIX(v) ((v)>>16)
      case AV_SAMPLE_FMT_S32P:
        MIX_AUDIO (int32_t, 1, this->left,  this->front_mixes, 0);
        MIX_AUDIO (int32_t, 1, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (int32_t, 1, this->map + j, 1, j);
      break;
      case AV_SAMPLE_FMT_S32:
        MIX_AUDIO (int32_t, 0, this->left,  this->front_mixes, 0);
        MIX_AUDIO (int32_t, 0, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (int32_t, 0, this->map + j, 1, j);
      break;
#undef MIX_FIX
#undef MIX_AUDIO
#define MIX_AUDIO(stype,planar,idx,num,dindx) {\
    stype *p1, *p2, *p3, *p4;\
    int i, sstep;\
    int8_t *x = idx;\
    int16_t *dptr = (int16_t *)decode_buffer + dindx;\
    if (planar) {\
      p1 = (stype *)this->av_frame->extended_data[x[0]];\
      sstep = 1;\
    } else {\
      p1 = (stype *)this->av_frame->extended_data[0] + x[0];\
      sstep = this->ff_channels;\
    }\
    if (num == 1) {\
      if (p1) for (i = 0; i < samples; i++) {\
        int32_t v = (*p1) * gain;\
        p1       += sstep;\
        *dptr     = CLIP_16(v);\
        dptr     += channels;\
      }\
    } else {\
      if (planar)\
        p2 = (stype *)this->av_frame->extended_data[x[1]];\
      else\
        p2 = (stype *)this->av_frame->extended_data[0] + x[1];\
      if (num == 2) {\
        if (p1 && p2) for (i = 0; i < samples; i++) {\
          int32_t v = (*p1 + *p2) * gain;\
          p1       += sstep;\
          p2       += sstep;\
          *dptr     = CLIP_16(v);\
          dptr     += channels;\
        }\
      } else {\
        if (planar)\
          p3 = (stype *)this->av_frame->extended_data[x[2]];\
        else\
          p3 = (stype *)this->av_frame->extended_data[0] + x[2];\
        if (num == 3) {\
          if (p1 && p2 && p3) for (i = 0; i < samples; i++) {\
            int32_t v = (*p1 + *p2 + *p3) * gain;\
            p1       += sstep;\
            p2       += sstep;\
            p3       += sstep;\
            *dptr     = CLIP_16(v);\
            dptr     += channels;\
          }\
        } else {\
          if (planar)\
            p4 = (stype *)this->av_frame->extended_data[x[3]];\
          else\
            p4 = (stype *)this->av_frame->extended_data[0] + x[3];\
          if (p1 && p2 && p3 && p4) for (i = 0; i < samples; i++) {\
            int32_t v = (*p1 + *p2 + *p3 + *p4) * gain;\
            p1       += sstep;\
            p2       += sstep;\
            p3       += sstep;\
            p4       += sstep;\
            *dptr     = CLIP_16(v);\
            dptr     += channels;\
          }\
        }\
      }\
    }\
  }
      case AV_SAMPLE_FMT_FLTP: /* the most popular one */
        MIX_AUDIO (float, 1, this->left,  this->front_mixes, 0);
        MIX_AUDIO (float, 1, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (float, 1, this->map + j, 1, j);
      break;
      case AV_SAMPLE_FMT_FLT:
        MIX_AUDIO (float, 0, this->left,  this->front_mixes, 0);
        MIX_AUDIO (float, 0, this->right, this->front_mixes, 1);
        for (j = 0; j < channels; j++) if (this->map[j] >= 0)
          MIX_AUDIO (float, 0, this->map + j, 1, j);
      break;
      default: ;
    }
    if (channels > this->ao_channels) {
      /* final mono downmix */
      int16_t *p = decode_buffer;
      q = p;
      for (i = samples; i; i--) {
        int v = *p++;
        v += *p++;
        *q++ = v >> 1;
      }
      *decode_buffer_size = samples * 2;
    }
  } else *decode_buffer_size = 0;
#  else
  consumed = avcodec_decode_audio3 (this->context, decode_buffer, decode_buffer_size, &avpkt);
  ff_map_channels (this);
#  endif
#else
  consumed = avcodec_decode_audio2 (this->context, decode_buffer, decode_buffer_size, buf, size);
  ff_map_channels (this);
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

        bytes_consumed = ff_audio_decode (this, (int16_t *)this->decode_buffer, &decode_buffer_size,
          &this->buf[offset], this->size);

        if (bytes_consumed < 0)
          break;

        offset     += bytes_consumed;
        this->size -= bytes_consumed;

        if (decode_buffer_size == 0) {
          if (bytes_consumed)
            continue;
          if (offset)
            memmove(this->buf, &this->buf[offset], this->size);
          return;
        }

        if (this->ff_sample_rate != this->context->sample_rate ||
            this->ao_mode        != this->new_mode) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  _("ffmpeg_audio_dec: codec parameters changed\n"));
          /* close if it was open, and always trigger 1 new open attempt below */
          ff_audio_output_close(this);
        }

	if (!this->output_open) {
	  if (!this->ff_sample_rate || !this->ao_mode) {
	    this->ff_sample_rate = this->context->sample_rate;
	    this->ao_mode        = this->new_mode;
	  }
	  if (!this->ff_sample_rate || !this->new_mode) {
	    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		    _("ffmpeg_audio_dec: cannot read codec parameters from packet\n"));
	    /* try to decode next packet. */
	    /* there shouldn't be any output yet */
	    decode_buffer_size = 0;
	    /* pts applies only to first audio packet */
	    buf->pts = 0;
	  } else {
	    this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
								 this->stream, 16, this->ff_sample_rate,
								 this->ao_mode);
	    if (!this->output_open) {
	      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		      "ffmpeg_audio_dec: error opening audio output\n");
	      this->size = 0;
	      return;
	    }
	  }
	}

#if AVAUDIO < 4
        /* Old style postprocessing */
        if (codec_type == BUF_AUDIO_WMAPRO) {
          /* the above codecs output float samples, not 16-bit integers */
          int samples = decode_buffer_size / sizeof(float);
          float gain  = this->class->gain;
          float *p    = (float *)this->decode_buffer;
          int16_t *q  = (int16_t *)this->decode_buffer;
          int i;
          for (i = samples; i; i--) {
            int v = *p++ * gain;
            *q++ = CLIP_16 (v);
          }
          decode_buffer_size = samples * 2;
        }

        if ((this->ao_channels != this->ff_channels) || (this->ao_channels > 2)) {
          /* Channel reordering and/or mixing */
          int samples     = decode_buffer_size / (this->ff_channels * 2);
          int channels    = this->ao_channels;
          int ff_channels = this->ff_channels;
          int16_t *p      = (int16_t *)this->decode_buffer;
          int16_t *q      = p;
          int shift       = this->downmix_shift, i, j;
          /* downmix mono output to stereo first */
          if ((channels == 1) && (ff_channels > 1))
            channels = 2;
          /* move to end of buf for in-place editing */
          p += AVCODEC_MAX_AUDIO_FRAME_SIZE - decode_buffer_size;
          if (p >= q + decode_buffer_size)
            xine_fast_memcpy (p, q, decode_buffer_size);
          else
            memmove (p, q, decode_buffer_size);
          /* not very optimized but it only hits when playing multichannel audio through
             old ffmpeg - and its still better than previous code there */
          if (this->front_mixes < 2) {
            /* just reorder and maybe upmix */
            for (i = samples; i; i--) {
              q[0] = p[0];
              q[1] = p[this->right[0]];
              for (j = 2; j < channels; j++)
                q[j] = this->map[j] < 0 ? 0 : p[this->map[j]];
              p += ff_channels;
              q += channels;
            }
          } else {
            /* downmix */
            for (i = samples; i; i--) {
              int left  = p[0];
              int right = p[this->right[0]];
              for (j = 1; j < this->front_mixes; j++) {
                left  += p[this->left[j]];
                right += p[this->right[j]];
              }
              left  >>= shift;
              q[0]    = CLIP_16 (left);
              right >>= shift;
              q[1]    = CLIP_16 (right);
              for (j = 2; j < channels; j++)
                q[j] = this->map[j] < 0 ? 0 : p[this->map[j]] >> shift;
              p += ff_channels;
              q += channels;
            }
          }
          /* final mono downmix */
          if (channels > this->ao_channels) {
            p = (int16_t *)this->decode_buffer;
            q = p;
            for (i = samples; i; i--) {
              int v = *p++;
              v += *p++;
              *q++ = v >> 1;
            }
          }
          decode_buffer_size = samples * this->ao_channels * 2;
        }
#endif

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
          if ((decode_buffer_size - out) > audio_buffer->mem_size)
            bytes_to_send = audio_buffer->mem_size;
          else
            bytes_to_send = decode_buffer_size - out;

          xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[out], bytes_to_send);
          out += bytes_to_send;

          /* byte count / 2 (bytes / sample) / channels */
          audio_buffer->num_frames = bytes_to_send / 2 / this->ao_channels;

          audio_buffer->vpts = buf->pts;

          buf->pts = 0;  /* only first buffer gets the real pts */
          this->stream->audio_out->put_buffer (this->stream->audio_out,
            audio_buffer, this->stream);
        }
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
  this->ff_channels = 0;
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

const decoder_info_t dec_info_ffmpeg_audio = {
  supported_audio_types,   /* supported types */
  7                        /* priority        */
};
