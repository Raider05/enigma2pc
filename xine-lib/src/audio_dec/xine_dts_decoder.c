/*
 * Copyright (C) 2000-2007 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * @brief DTS decoder for xine
 *
 * @author Joachim Koenig (2001-09-04)
 * @author James Courtier-Dutton (2001-12-09)
 */

#ifndef __sun
/* required for swab() */
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
#include <assert.h>

#define LOG_MODULE "libdts"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>

#include "bswap.h"

#include <dts.h>

#define MAX_AC5_FRAME 4096

typedef struct {
  audio_decoder_class_t   decoder_class;
} dts_class_t;

typedef struct {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;
  audio_decoder_class_t *class;

  dts_state_t     *dts_state;
  int64_t          pts;

  int              audio_caps;
  int              sync_state;
  int              ac5_length, ac5_pcm_length, frame_todo;
  uint32_t         syncdword;
  uint8_t          frame_buffer[MAX_AC5_FRAME];
  uint8_t         *frame_ptr;

  int              output_open;

  int              bypass_mode;
  int              dts_flags;
  int              dts_sample_rate;
  int              dts_bit_rate;
  int              dts_flags_map[11]; /* Convert from stream dts_flags to the dts_flags we want from the dts downmixer */
  int              ao_flags_map[11];  /* Convert from the xine AO_CAP's to dts_flags. */
  int              have_lfe;


} dts_decoder_t;

static void dts_reset (audio_decoder_t *const this_gen) {
}

static void dts_discontinuity (audio_decoder_t *const this_gen) {
}

/**
 * @brief Convert a array of floating point samples into 16-bit signed integer samples
 * @param f Floating point samples array (origin)
 * @param s16 16-bit signed integer samples array (destination)
 * @param num_channels Number of channels present in the stream
 *
 * @todo This same work is being done in many decoders to adapt the output of
 *       the decoder to what the audio output can actually use, this should be
 *       done by the audio_output loop, not by the decoders.
 * @note This is subtly different from the function with the same name in xine_musepack_decoder.c
 */
static inline void float_to_int (const float *const _f, int16_t *const s16, const int num_channels) {
  const int endidx = 256 * num_channels;
  int i, j;

  for (i = 0, j = 0; j < endidx; i++, j += num_channels) {
    const float f = _f[i] * 32767;
    if (f > INT16_MAX)
      s16[j] = INT16_MAX;
    else if (f < INT16_MIN)
      s16[j] = INT16_MIN;
    else
      s16[j] = f;
    /* printf("samples[%d] = %f, %d\n", i, _f[i], s16[num_channels*i]); */
  }
}

static inline void mute_channel (int16_t *const s16, const int num_channels) {
  const int endidx = 256 * num_channels;
  int i;

  for (i = 0; i < endidx; i += num_channels)
    s16[i] = 0;
}

static void dts_decode_frame (dts_decoder_t *this, const int64_t pts) {

  audio_buffer_t *audio_buffer;
  uint32_t  ac5_spdif_type=0;
  int output_mode = AO_CAP_MODE_STEREO;
  uint8_t        *data_out;
  uint8_t        *const data_in = this->frame_buffer;

  lprintf("decode_frame\n");
  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
  audio_buffer->vpts       = pts;

    if(this->bypass_mode) {
      /* SPDIF digital output */
      if (!this->output_open) {
        this->output_open = ((this->stream->audio_out->open) (this->stream->audio_out, this->stream,
                                                            16, this->dts_sample_rate,
                                                            AO_CAP_MODE_AC5));
      }

      if (!this->output_open)
        return;

      data_out=(uint8_t *) audio_buffer->mem;
      if (this->ac5_length > 8191) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: ac5_length too long\n");
        this->ac5_pcm_length = 0;
      }

      switch (this->ac5_pcm_length) {
      case 512:
        ac5_spdif_type = 0x0b; /* DTS-1 (512-sample bursts) */
        break;
      case 1024:
        ac5_spdif_type = 0x0c; /* DTS-1 (1024-sample bursts) */
        break;
      case 2048:
        ac5_spdif_type = 0x0d; /* DTS-1 (2048-sample bursts) */
        break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"libdts: DTS %i-sample bursts not supported\n", this->ac5_pcm_length);
        return;
      }

#ifdef LOG_DEBUG
      {
        int i;
        printf("libdts: DTS frame type=%d\n",data_in[4] >> 7);
        printf("libdts: DTS deficit frame count=%d\n",(data_in[4] & 0x7f) >> 2);
        printf("libdts: DTS AC5 PCM samples=%d\n",ac5_pcm_samples);
        printf("libdts: DTS AC5 length=%d\n",this->ac5_length);
        printf("libdts: DTS AC5 bitrate=%d\n",((data_in[8] & 0x03) << 4) | (data_in[8] >> 4));
        printf("libdts: DTS AC5 spdif type=%d\n", ac5_spdif_type);

        printf("libdts: ");
        for(i=2000;i<2048;i++) {
          printf("%02x ",data_in[i]);
        }
        printf("\n");
      }
#endif

      lprintf("length=%d pts=%"PRId64"\n",this->ac5_pcm_length,audio_buffer->vpts);

      audio_buffer->num_frames = this->ac5_pcm_length;

      // Checking if AC5 data plus IEC958 header will fit into frames samples data
      if ( this->ac5_length + 8 <= this->ac5_pcm_length * 2 * 2 ) {
        data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
        data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
        data_out[4] = ac5_spdif_type;		/* DTS data          */
        data_out[5] = 0;		                /* Unknown */
        data_out[6] = (this->ac5_length << 3) & 0xff;   /* ac5_length * 8   */
        data_out[7] = ((this->ac5_length ) >> 5) & 0xff;

        if( this->ac5_pcm_length ) {
          if( this->ac5_pcm_length % 2) {
            swab(data_in, &data_out[8], this->ac5_length );
          } else {
            swab(data_in, &data_out[8], this->ac5_length + 1);
          }
        }
      // Transmit it without header otherwise, receivers will autodetect DTS
      } else {
        lprintf("AC5 data is too large (%i > %i), sending without IEC958 header\n",
                this->ac5_length + 8, this->ac5_pcm_length * 2 * 2);
        memcpy(data_out, data_in, this->ac5_length);
      }
    } else {
      /* Software decode */
      int       i, dts_output_flags;
      int16_t  *const int_samples = audio_buffer->mem;
      int       number_of_dts_blocks;

      level_t   level = 1.0;
      sample_t *samples;

      dts_output_flags = this->dts_flags_map[this->dts_flags & DTS_CHANNEL_MASK];

      if(dts_frame(this->dts_state, data_in, &dts_output_flags, &level, 0)) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: dts_frame error\n");
        return;
      }

      this->have_lfe = dts_output_flags & DTS_LFE;
      if (this->have_lfe)
        if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {
          output_mode = AO_CAP_MODE_5_1CHANNEL;
        } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {
          output_mode = AO_CAP_MODE_4_1CHANNEL;
        } else {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: WHAT DO I DO!!!\n");
          output_mode = this->ao_flags_map[dts_output_flags & DTS_CHANNEL_MASK];
        }
      else
        output_mode = this->ao_flags_map[dts_output_flags & DTS_CHANNEL_MASK];

      if (!this->output_open) {
        this->output_open = (this->stream->audio_out->open) (this->stream->audio_out, this->stream,
                                                           16, this->dts_sample_rate,
                                                           output_mode);
      }

      if (!this->output_open)
        return;
      number_of_dts_blocks = dts_blocks_num (this->dts_state);
      audio_buffer->num_frames = 256*number_of_dts_blocks;
      for(i = 0; i < number_of_dts_blocks; i++) {
        if(dts_block(this->dts_state)) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "libdts: dts_block error on audio channel %d\n", i);
          audio_buffer->num_frames = 0;
          break;
        }

        samples = dts_samples(this->dts_state);
        switch (output_mode) {
        case AO_CAP_MODE_MONO:
          float_to_int (&samples[0], int_samples+(i*256), 1);
          break;
        case AO_CAP_MODE_STEREO:
          /* Tested, working. */
          float_to_int (&samples[0*256], int_samples+(i*256*2), 2);   /*  L */
          float_to_int (&samples[1*256], int_samples+(i*256*2)+1, 2); /*  R */
          break;
        case AO_CAP_MODE_4CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*4),   4); /*  L */
          float_to_int (&samples[1*256], int_samples+(i*256*4)+1, 4); /*  R */
          float_to_int (&samples[2*256], int_samples+(i*256*4)+2, 4); /* RL */
          float_to_int (&samples[3*256], int_samples+(i*256*4)+3, 4); /* RR */
          break;
        case AO_CAP_MODE_4_1CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+3, 6); /*  RR */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+5, 6); /* LFE */
          mute_channel ( int_samples+(i*256*6)+4, 6); /* C */
          break;
        case AO_CAP_MODE_5CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*6)+4, 6); /*   C */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /*  RR */
          mute_channel ( int_samples+(i*256*6)+5, 6); /* LFE */
          break;
        case AO_CAP_MODE_5_1CHANNEL:
          float_to_int (&samples[0*256], int_samples+(i*256*6)+4, 6); /*   C */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /*  RR */
          float_to_int (&samples[5*256], int_samples+(i*256*6)+5, 6); /* LFE */ /* Not working yet */
          break;
        default:
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: help - unsupported mode %08x\n", output_mode);
        }
      }
    }

    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);


}

static void dts_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dts_decoder_t  *const this = (dts_decoder_t *) this_gen;
  uint8_t        *current = (uint8_t *)buf->content;
  uint8_t        *sync_start=current + 1;
  uint8_t        *const end = buf->content + buf->size;

  lprintf("decode_data\n");

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;
  if (buf->decoder_flags & BUF_FLAG_STDHEADER)
    return;

  lprintf ("processing...state %d\n", this->sync_state);

  while (current < end) {
    switch (this->sync_state) {
    case 0:  /* Looking for sync header */
	  this->syncdword = (this->syncdword << 8) | *current++;
/*
          if ((this->syncdword == 0xff1f00e8) ||
              (this->syncdword == 0x1fffe800) ||
              (this->syncdword == 0xfe7f0180) ||
              (this->syncdword == 0x7ffe8001) ) {
*/

          if ((this->syncdword == 0x7ffe8001) || (this->syncdword == 0xff1f00e8)) {
	    const uint32_t be_syncdword = be2me_32(this->syncdword);

            lprintf ("sync found: syncdword=0x%x\n", this->syncdword);

	    memcpy(this->frame_buffer, &be_syncdword, sizeof(be_syncdword));

	    this->sync_state = 1;
	    this->frame_ptr = this->frame_buffer+4;
            this->pts = buf->pts;
	  }
          break;

    case 1:  /* Looking for enough bytes for sync_info. */
          sync_start = current - 1;
	  *this->frame_ptr++ = *current++;
          if ((this->frame_ptr - this->frame_buffer) > 19) {
	    const int old_dts_flags       = this->dts_flags;
	    const int old_dts_sample_rate = this->dts_sample_rate;
	    const int old_dts_bit_rate    = this->dts_bit_rate;

	    this->ac5_length = dts_syncinfo (this->dts_state, this->frame_buffer,
					       &this->dts_flags,
					       &this->dts_sample_rate,
					       &this->dts_bit_rate, &(this->ac5_pcm_length));
	    lprintf("ac5_length=%d\n",this->ac5_length);
	    lprintf("dts_sample_rate=%d\n",this->dts_sample_rate);

            if ( (this->ac5_length < 80) || (this->ac5_length > MAX_AC5_FRAME) ) { /* Invalid dts ac5_pcm_length */
	      this->syncdword = 0;
	      current = sync_start;
	      this->sync_state = 0;
	      break;
	    }

            lprintf("Frame length = %d\n",this->ac5_pcm_length);

	    this->frame_todo = this->ac5_length - 20;
	    this->sync_state = 2;
	    if (!_x_meta_info_get(this->stream, XINE_META_INFO_AUDIOCODEC) ||
	        old_dts_flags       != this->dts_flags ||
                old_dts_sample_rate != this->dts_sample_rate ||
		old_dts_bit_rate    != this->dts_bit_rate) {

              switch (this->dts_flags & DTS_CHANNEL_MASK) {
                case DTS_3F2R:
                  if (this->dts_flags & DTS_LFE)
                    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 5.1");
                  else
                    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 5.0");
                  break;
                case DTS_3F1R:
                case DTS_2F2R:
                  if (this->dts_flags & DTS_LFE)
                    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 4.1");
                  else
                    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 4.0");
                  break;
                case DTS_2F1R:
                case DTS_3F:
                  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 3.0");
                  break;
                case DTS_STEREO:
                  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 2.0 (stereo)");
                  break;
                case DTS_MONO:
                  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS 1.0");
                  break;
                default:
                  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS");
                  break;
              }

              _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, this->dts_bit_rate);
              _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, this->dts_sample_rate);
            }
          }
          break;

    case 2:  /* Filling frame_buffer with sync_info bytes */
	  *this->frame_ptr++ = *current++;
	  this->frame_todo--;
	  if (this->frame_todo < 1) {
	    this->sync_state = 3;
          } else break;

    case 3:  /* Ready for decode */
#if 0
          dtsdec_decode_frame (this, this->pts_list[0]);
#else
          dts_decode_frame (this, this->pts);
#endif
    case 4:  /* Clear up ready for next frame */
          this->pts = 0;
	  this->syncdword = 0;
	  this->sync_state = 0;
          break;
    default: /* No come here */
          break;
    }
  }
}

static void dts_dispose (audio_decoder_t *this_gen) {
  dts_decoder_t *const this = (dts_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  free (this);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {
  dts_decoder_t *this ;

  lprintf("open_plugin\n");

  this = calloc(1, sizeof (dts_decoder_t));

  this->audio_decoder.decode_data         = dts_decode_data;
  this->audio_decoder.reset               = dts_reset;
  this->audio_decoder.discontinuity       = dts_discontinuity;
  this->audio_decoder.dispose             = dts_dispose;

  this->dts_state = dts_init(0);
  this->audio_caps        = stream->audio_out->get_capabilities(stream->audio_out);
  if(this->audio_caps & AO_CAP_MODE_AC5)
    this->bypass_mode = 1;
  else {
    this->bypass_mode = 0;
    /* FIXME: Leave "DOLBY pro logic" downmix out for now. */
    this->dts_flags_map[DTS_MONO]   = DTS_MONO;
    this->dts_flags_map[DTS_STEREO] = DTS_STEREO;
    this->dts_flags_map[DTS_3F]     = DTS_STEREO;
    this->dts_flags_map[DTS_2F1R]   = DTS_STEREO;
    this->dts_flags_map[DTS_3F1R]   = DTS_STEREO;
    this->dts_flags_map[DTS_2F2R]   = DTS_STEREO;
    this->dts_flags_map[DTS_3F2R]   = DTS_STEREO;

    this->ao_flags_map[DTS_MONO]    = AO_CAP_MODE_MONO;
    this->ao_flags_map[DTS_STEREO]  = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F]      = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_2F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_STEREO;

    /* find best mode */
    if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_3F2R | DTS_LFE;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_5CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_3F2R;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_2F2R | DTS_LFE;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_4CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_2F2R;

      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_4CHANNEL;

      /* else if (this->audio_caps & AO_CAP_MODE_STEREO)
         defaults are ok */
    } else if (!(this->audio_caps & AO_CAP_MODE_STEREO)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, _("HELP! a mono-only audio driver?!\n"));

      this->dts_flags_map[DTS_MONO]   = DTS_MONO;
      this->dts_flags_map[DTS_STEREO] = DTS_MONO;
      this->dts_flags_map[DTS_3F]     = DTS_MONO;
      this->dts_flags_map[DTS_2F1R]   = DTS_MONO;
      this->dts_flags_map[DTS_3F1R]   = DTS_MONO;
      this->dts_flags_map[DTS_2F2R]   = DTS_MONO;
      this->dts_flags_map[DTS_3F2R]   = DTS_MONO;

      this->ao_flags_map[DTS_MONO]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_STEREO]  = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F]      = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_2F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_MONO;
    }
  }
  this->stream        = stream;
  this->class         = class_gen;
  this->output_open   = 0;

  return &this->audio_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {
  dts_class_t *this ;

  lprintf("init_plugin\n");

  this = calloc(1, sizeof (dts_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "DTS";
  this->decoder_class.description     = N_("DTS passthru audio format decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_DTS, 0
 };

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "dts", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
