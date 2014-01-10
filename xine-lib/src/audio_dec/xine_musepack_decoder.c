/*
 * Copyright (C) 2005-2013 the xine project
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
 * @brief xine interface to libmusepack/libmpcdec
 * @author James Stembridge <jstembridge@gmail.com>
 *
 * @todo Add support for 32-bit float samples.
 * @todo Add support for seeking.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_MODULE "mpc_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#ifdef HAVE_MPCDEC_MPCDEC_H
# include <mpcdec/mpcdec.h>
#elif defined(HAVE_MPC_MPCDEC_H)
# include <mpc/mpcdec.h>
#else
# include "mpcdec/mpcdec.h"
#endif

#define MPC_DECODER_MEMSIZE  65536
#define MPC_DECODER_MEMSIZE2 (MPC_DECODER_MEMSIZE/2)

#define INIT_BUFSIZE (MPC_DECODER_MEMSIZE*2)

typedef struct {
  audio_decoder_class_t   decoder_class;
} mpc_class_t;

typedef struct mpc_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;

  int              sample_rate;       /* audio sample rate */
  int              bits_per_sample;   /* bits/sample, usually 8 or 16 */
  int              channels;          /* 1 or 2, usually */

  int              output_open;       /* flag to indicate audio is ready */

  unsigned char   *buf;              /* data accumulation buffer */
  unsigned int     buf_max;          /* maximum size of buf */
  unsigned int     read;             /* size of accum. data already read */
  unsigned int     size;             /* size of accumulated data in buf */

  mpc_reader       reader;
  mpc_streaminfo   streaminfo;
#ifndef HAVE_MPC_MPCDEC_H
  mpc_decoder      decoder;
#else
  mpc_demux       *decoder;
#endif

  int              decoder_ok;
  unsigned int     current_frame;

  int32_t          file_size;

} mpc_decoder_t;


/**************************************************************************
 * musepack specific functions
 *************************************************************************/

/* Reads size bytes of data into buffer at ptr. */
#ifndef HAVE_MPC_MPCDEC_H
static int32_t mpc_reader_read(void *const data, void *const ptr, int size) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data;
#else
static int32_t mpc_reader_read(mpc_reader *data, void *ptr, int32_t size) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data->data;
#endif

  lprintf("mpc_reader_read: size=%d\n", size);

  /* Don't try to read more data than we have */
  if (size > (this->size - this->read))
    size = this->size - this->read;

  /* Copy the data */
  xine_fast_memcpy(ptr, &this->buf[this->read], size);

  /* Update our position in the data buffer */
  this->read += size;

  return size;
}

/* Seeks to byte position offset. */
#ifndef HAVE_MPC_MPCDEC_H
static mpc_bool_t mpc_reader_seek(void *const data, const int32_t offset) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data;
#else
static mpc_bool_t mpc_reader_seek(mpc_reader *data, int32_t offset) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data->data;
#endif

  lprintf("mpc_reader_seek: offset=%d\n", offset);

  /* seek is only called when reading the header so we can assume
   * that the buffer starts at the start of the file */
  this->read = offset;

#ifndef HAVE_MPC_MPCDEC_H
  return TRUE;
#else
  return MPC_TRUE;
#endif
}

/* Returns the current byte offset in the stream. */
#ifndef HAVE_MPC_MPCDEC_H
static int32_t mpc_reader_tell(void *const data) {
#else
static int32_t mpc_reader_tell(mpc_reader *const data) {
#endif
  lprintf("mpc_reader_tell\n");

  /* Tell isn't used so just return 0 */
  return 0;
}

/* Returns the total length of the source stream, in bytes. */
#ifndef HAVE_MPC_MPCDEC_H
static int32_t mpc_reader_get_size(void *const data) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data;
#else
static int32_t mpc_reader_get_size(mpc_reader *const data) {
  mpc_decoder_t *const this = (mpc_decoder_t *) data->data;
#endif

  lprintf("mpc_reader_get_size\n");

  return this->file_size;
}

/* True if the stream is a seekable stream. */
#ifndef HAVE_MPC_MPCDEC_H
static mpc_bool_t mpc_reader_canseek(void *data) {
  lprintf("mpc_reader_canseek\n");

  return TRUE;
#else
static mpc_bool_t mpc_reader_canseek(mpc_reader *data) {

  lprintf("mpc_reader_canseek\n");
  return MPC_TRUE;
#endif
}

/**
 * @brief Convert a array of floating point samples into 16-bit signed integer samples
 * @param f Floating point samples array (origin)
 * @param s16 16-bit signed integer samples array (destination)
 * @param samples Number of samples to convert
 *
 * @todo This same work is being done in many decoders to adapt the output of
 *       the decoder to what the audio output can actually use, this should be
 *       done by the audio_output loop, not by the decoders.
 */
static inline void float_to_int(const float *const _f, int16_t *const s16, const int samples) {
  int i;
  for (i = 0; i < samples; i++) {
    const float f = _f[i] * 32767;
    if (f > INT16_MAX)
      s16[i] = INT16_MAX;
    else if (f < INT16_MIN)
      s16[i] = INT16_MIN;
    else
      s16[i] = f;
    /* printf("samples[%d] = %f, %d\n", i, _f[i], s16[num_channels*i]); */
  }
}

/* Decode a musepack frame */
static int mpc_decode_frame (mpc_decoder_t *this) {
  float buffer[MPC_DECODER_BUFFER_LENGTH];
  uint32_t frames;
#ifdef HAVE_MPC_MPCDEC_H
  mpc_frame_info frame;
#endif

  lprintf("mpd_decode_frame\n");

#ifndef HAVE_MPC_MPCDEC_H
  frames = mpc_decoder_decode(&this->decoder, buffer, 0, 0);
#else
  frame.buffer = buffer;
  mpc_demux_decode(this->decoder, &frame);
  frames = frame.samples;
#endif

  if (frames > 0) {
    audio_buffer_t *audio_buffer;
    int16_t  *int_samples;

    lprintf("got %d samples\n", frames);

    /* Get audio buffer */
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    audio_buffer->vpts = 0;
    audio_buffer->num_frames = frames;

    /* Convert samples */
    int_samples = (int16_t *) audio_buffer->mem;
    float_to_int(buffer, int_samples, frames*this->channels);

    /* Output converted samples */
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
  }

  return frames;
}

/**************************************************************************
 * xine audio plugin functions
 *************************************************************************/

static void mpc_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {
  mpc_decoder_t *this = (mpc_decoder_t *) this_gen;
  int err;

  lprintf("mpc_decode_data\n");

  if (!_x_stream_info_get(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED))
    return;

  /* We don't handle special buffers */
  if (buf->decoder_flags & BUF_FLAG_SPECIAL)
    return;

  /* Read header */
  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    lprintf("header\n");

    /* File size is in decoder_info[0] */
    this->file_size = buf->decoder_info[0];

    /* Initialise the data accumulation buffer */
    this->buf     = calloc(1, INIT_BUFSIZE);
    this->buf_max = INIT_BUFSIZE;
    this->read    = 0;
    this->size    = 0;

    /* Initialise the reader */
    this->reader.read     = mpc_reader_read;
    this->reader.seek     = mpc_reader_seek;
    this->reader.tell     = mpc_reader_tell;
    this->reader.get_size = mpc_reader_get_size;
    this->reader.canseek  = mpc_reader_canseek;
    this->reader.data     = this;

    /* Copy header to buffer */
    xine_fast_memcpy(this->buf, buf->content, buf->size);
    this->size = buf->size;

#ifdef HAVE_MPC_MPCDEC_H
    this->decoder = mpc_demux_init(&this->reader);
    if (!this->decoder) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("libmusepack: mpc_demux_init failed.\n"));
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
      return;
    }
    mpc_demux_get_info(this->decoder, &this->streaminfo);
#else
    /* Initialise and read stream info */
    mpc_streaminfo_init(&this->streaminfo);

    if ((err = mpc_streaminfo_read(&this->streaminfo, &this->reader))) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("libmusepack: mpc_streaminfo_read failed: %d\n"), err);

      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
      return;
    }
#endif

    this->sample_rate     = this->streaminfo.sample_freq;
    this->channels        = this->streaminfo.channels;
    this->bits_per_sample = 16;

    /* After the header the demuxer starts sending data from an offset
     * of 28 bytes */
    this->size = 28;

    /* We need to keep track of the current frame so we now when we've
     * reached the end of the stream */
    this->current_frame = 0;

    /* Setup the decoder */
#ifndef HAVE_MPC_MPCDEC_H
    mpc_decoder_setup(&this->decoder, &this->reader);
#endif
    this->decoder_ok = 0;

    /* Take this opportunity to initialize stream/meta information */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
                          "Musepack (libmusepack)");
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE,
                       (int) this->streaminfo.average_bitrate);

    return;
  }

  lprintf("data: %u size=%u read=%u\n", buf->size, this->size, this->read);

  /* if the audio output is not open yet, open the audio output */
  if (!this->output_open) {
    this->output_open = (this->stream->audio_out->open) (
      this->stream->audio_out,
      this->stream,
      this->bits_per_sample,
      this->sample_rate,
      _x_ao_channels2mode(this->channels));

    /* if the audio still isn't open, do not go any further with the decode */
    if (!this->output_open)
      return;
  }

  /* If we run out of space in our internal buffer we discard what's
   * already been read */
  if (((this->size + buf->size) > this->buf_max) && this->read) {
    lprintf("discarding read data\n");
    this->size -= this->read;
    memmove(this->buf, &this->buf[this->read], this->size);
    this->read = 0;
  }

  /* If there still isn't space we have to increase the size of the
   * internal buffer */
  if ((this->size + buf->size) > this->buf_max) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "libmusepack: increasing internal buffer size\n");
    this->buf_max += 2*buf->size;
    this->buf = realloc(this->buf, this->buf_max);
  }

  /* Copy data */
  xine_fast_memcpy(&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  /* Time to decode */
  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {
    /* Increment frame count */
#ifndef HAVE_MPC_MPCDEC_H
    if (this->current_frame++ == this->streaminfo.frames) {
#else
    if (this->current_frame++ == this->streaminfo.samples) {
#endif
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("libmusepack: data after last frame ignored\n"));
      return;
    }

    if (!this->decoder_ok) {
      /* We require MPC_DECODER_MEMSIZE bytes to initialise the decoder */
      if ((this->size - this->read) >= MPC_DECODER_MEMSIZE) {
        lprintf("initialise");

#ifndef HAVE_MPC_MPCDEC_H
        if (!mpc_decoder_initialize(&this->decoder, &this->streaminfo)) {
#else
        if (!this->decoder) {
#endif
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  _("libmusepack: mpc_decoder_initialise failed\n"));

          _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
          return;
        }

        this->decoder_ok = 1;
      } else {
        /* Not enough data yet */
        return;
      }
    }

    /* mpc_decoder_decode may cause a read of MPC_DECODER_MEMSIZE/2 bytes so
     * make sure we have enough data available */
    if ((this->size - this->read) >= MPC_DECODER_MEMSIZE2) {
      lprintf("decoding\n");

      if ((err = mpc_decode_frame(this)) < 0) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                _("libmusepack: mpc_decoder_decode failed: %d\n"), err);

        _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
        return;
      }
    }

    /* If we are at the end of the stream we decode the remaining frames as we
     * know we'll have enough data */
#ifndef HAVE_MPC_MPCDEC_H
    if (this->current_frame == this->streaminfo.frames) {
#else
    if (this->current_frame == this->streaminfo.samples) {
#endif
      lprintf("flushing buffers\n");

      do {
        if ((err = mpc_decode_frame(this)) < 0) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  _("libmusepack: mpc_decoder_decode failed: %d\n"), err);
        }
      } while (err > 0);

      lprintf("buffers flushed\n");
    }
  }
}

static void mpc_reset (audio_decoder_t *this_gen) {
  mpc_decoder_t *this = (mpc_decoder_t *) this_gen;

  this->size = 0;
  this->read = 0;
}

static void mpc_discontinuity (audio_decoder_t *this_gen) {
  /* mpc_decoder_t *this = (mpc_decoder_t *) this_gen; */
}

static void mpc_dispose (audio_decoder_t *this_gen) {

  mpc_decoder_t *this = (mpc_decoder_t *) this_gen;

  /* close the audio output */
  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  /* free anything that was allocated during operation */
  free(this->buf);
#ifdef HAVE_MPC_MPCDEC_H
  if (this->decoder)
    mpc_demux_exit(this->decoder);
#endif

  free(this);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  mpc_decoder_t *this ;

  this = (mpc_decoder_t *) calloc(1, sizeof(mpc_decoder_t));

  /* connect the member functions */
  this->audio_decoder.decode_data         = mpc_decode_data;
  this->audio_decoder.reset               = mpc_reset;
  this->audio_decoder.discontinuity       = mpc_discontinuity;
  this->audio_decoder.dispose             = mpc_dispose;

  /* connect the stream */
  this->stream = stream;

  /* audio output is not open at the start */
  this->output_open = 0;

  /* no buffer yet */
  this->buf = NULL;

  /* initialize the basic audio parameters */
  this->channels = 0;
  this->sample_rate = 0;
  this->bits_per_sample = 0;

  /* return the newly-initialized audio decoder */
  return &this->audio_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  mpc_class_t *this ;

  this = (mpc_class_t *) calloc(1, sizeof(mpc_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "mpc";
  this->decoder_class.description     = N_("mpc: musepack audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_MPC,
  0
};

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API version, "name", version, special_info, init_function }, */
  { PLUGIN_AUDIO_DECODER, 16, "mpc", XINE_VERSION_CODE, &dec_info_audio, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

