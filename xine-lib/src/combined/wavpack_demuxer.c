/*
 * Copyright (C) 2006-2007 the xine project
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

#define LOG_MODULE "demux_wavpack"
#define LOG_VERBOSE
#define LOG

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include "bswap.h"
#include <xine/attributes.h>

#include <wavpack/wavpack.h>
#include "wavpack_combined.h"

typedef struct {
  demux_plugin_t demux_plugin;

  xine_stream_t *stream;
  fifo_buffer_t *audio_fifo;
  input_plugin_t *input;
  int status;

  uint32_t current_sample;
  uint32_t samples;
  uint32_t samplerate;
  uint16_t bits_per_sample:6;
  uint16_t channels:4;
} demux_wv_t;

typedef struct {
  demux_class_t demux_class;
} demux_wv_class_t;

static int32_t xine_input_read_bytes(void *const this_gen, void *const data,
				     const int32_t bcount) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return this->read(this, data, bcount);
}

static uint32_t xine_input_get_pos(void *const this_gen) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return this->get_current_pos(this);
}

static int xine_input_set_pos_abs(void *const this_gen, const uint32_t pos) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return this->seek(this, pos, SEEK_SET);
}

static int xine_input_set_pos_rel(void *const this_gen, const int32_t delta,
				  const int mode) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return this->seek(this, delta, mode);
}

static int xine_input_push_back_byte(void *const this_gen, const int c) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  if ( this->seek(this, -1, SEEK_CUR) ) {
    return c;
  } else {
    lprintf("xine_input_push_back_byte: unable to seek.\n");
    return EOF;
  }
}

static uint32_t xine_input_get_length(void *const this_gen) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return this->get_length(this);
}

static int xine_input_can_seek(void *const this_gen) {
  input_plugin_t *const this = (input_plugin_t*)this_gen;
  return INPUT_IS_SEEKABLE(this);
}

static int32_t xine_input_write_bytes(__attr_unused void *const id,
				      __attr_unused void *const data,
				      __attr_unused const int32_t bcount) {
  lprintf("xine_input_write_bytes: acces is read-only.\n");
  return 0;
}

static WavpackStreamReader wavpack_input_reader = {
  .read_bytes		= xine_input_read_bytes,
  .get_pos		= xine_input_get_pos,
  .set_pos_abs		= xine_input_set_pos_abs,
  .set_pos_rel		= xine_input_set_pos_rel,
  .push_back_byte	= xine_input_push_back_byte,
  .get_length		= xine_input_get_length,
  .can_seek		= xine_input_can_seek,
  .write_bytes		= xine_input_write_bytes
};

static int open_wv_file(demux_wv_t *const this) {
  WavpackContext *ctx = NULL;
  char error[256]; /* Current version of wavpack (4.31) does not write more than this */
  wvheader_t header;
  uint32_t tmp;

  /* Right now we don't support non-seekable streams */
  if (! INPUT_IS_SEEKABLE(this->input) ) {
    lprintf("open_wv_file: non-seekable inputs aren't supported yet.\n");
    return 0;
  }

  /* Read the file header */
  if (_x_demux_read_header(this->input, (uint8_t*)(&header), sizeof(wvheader_t)) != sizeof(wvheader_t))
    return 0;

  /* Validate header, we currently support only Wavpack 4 */
  if ( header.idcode != wvpk_signature || (le2me_16(header.wv_version) >> 8) != 4 )
    return 0;

  /* Rewind */
  this->input->seek(this->input, 0, SEEK_SET);

  ctx = WavpackOpenFileInputEx(&wavpack_input_reader, this->input, NULL, error, 0, 0);
  if ( ! ctx ) {
    lprintf("xine_open_wavpack_input: unable to open the stream: %s\n", error);
    return 0;
  }

  this->current_sample = 0;
  this->samples = WavpackGetNumSamples(ctx);
  lprintf("number of samples: %u\n", this->samples);
  this->samplerate = WavpackGetSampleRate(ctx);
  lprintf("samplerate: %u Hz\n", this->samplerate);

  tmp = WavpackGetBitsPerSample(ctx); _x_assert(tmp <= 32);
  lprintf("bits_per_sample: %u\n", tmp);
  this->bits_per_sample = tmp;

  tmp = WavpackGetNumChannels(ctx); _x_assert(tmp <= 8);
  lprintf("channels: %u\n", tmp);
  this->channels = tmp;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC,
		     wvpk_signature);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
		     this->channels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
		     this->samplerate);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
		     this->bits_per_sample);

  WavpackCloseFile(ctx);
  this->input->seek(this->input, SEEK_SET, 0);

  return 1;
}

static int demux_wv_send_chunk(demux_plugin_t *const this_gen) {
  demux_wv_t *const this = (demux_wv_t *) this_gen;
  uint32_t bytes_to_read; uint8_t header_sent = 0;
  wvheader_t header;

  lprintf("new frame\n");

  /* Check if we've finished */
  if (this->current_sample >= this->samples) {
    lprintf("all frames read\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  lprintf("current sample: %u\n", this->current_sample);

  do {
    if ( this->input->read(this->input, (uint8_t*)(&header), sizeof(wvheader_t)) != sizeof(wvheader_t) ) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* The size of the block is «of course» minus 8, and
       it also includes the size of the header.
    */
    bytes_to_read = le2me_32(header.block_size) + 8 - sizeof(wvheader_t);

    lprintf("demux_wavpack: going to read %u bytes.\n", bytes_to_read);

    while(bytes_to_read) {
      off_t bytes_read = 0, bytes_to_read_now, offset = 0;
      buf_element_t *buf = NULL;
      int64_t input_time_guess;

      /* Get a buffer */
      buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
      buf->type = BUF_AUDIO_WAVPACK;
      buf->decoder_flags = 0;

      /* Set normalised position */
      buf->extra_info->input_normpos =
	(int) ((double) this->input->get_current_pos(this->input) * 65535 /
	       this->input->get_length(this->input));

      buf->pts = (((this->current_sample) / this->samplerate))*90000;
      lprintf("Sending buffer with PTS %"PRId64"\n", buf->pts);

      /* Set time */
      input_time_guess = this->samples;
      input_time_guess /= this->samplerate;
      input_time_guess *= 1000;
      input_time_guess *= buf->extra_info->input_normpos;
      input_time_guess /= 65535;
      buf->extra_info->input_time = input_time_guess;

      if ( ! header_sent )
	offset = sizeof(wvheader_t);

      bytes_to_read_now = ( bytes_to_read+offset > buf->max_size ) ? buf->max_size-offset : bytes_to_read;
      if ( ! header_sent ) {
	header_sent = 1;
	xine_fast_memcpy(buf->content, &header, sizeof(wvheader_t));
      }

      bytes_read = this->input->read(this->input, &buf->content[offset], bytes_to_read_now);

      buf->size = offset + bytes_read;

      bytes_to_read -= bytes_read;

      if ( bytes_to_read <= 0 && (le2me_32(header.flags) & FINAL_BLOCK) == FINAL_BLOCK)
	buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->audio_fifo->put(this->audio_fifo, buf);
    }
  } while ( (le2me_32(header.flags) & FINAL_BLOCK) != FINAL_BLOCK );

  this->current_sample += le2me_32(header.samples_count);

  return this->status;
}

static void demux_wv_send_headers(demux_plugin_t *const this_gen) {
  demux_wv_t *const this = (demux_wv_t *) this_gen;
  buf_element_t *buf;

  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* Send start buffers */
  _x_demux_control_start(this->stream);

  /* Send header to decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    buf->type            = BUF_AUDIO_WAVPACK;
    buf->decoder_flags   = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = this->input->get_length(this->input);
    buf->decoder_info[1] = this->samplerate;
    buf->decoder_info[2] = this->bits_per_sample;
    buf->decoder_info[3] = this->channels;

    buf->size = 0;

    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_wv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_wv_t *const this = (demux_wv_t *) this_gen;

  /* If thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

  }

  return this->status;
}

static int demux_wv_get_status (demux_plugin_t *const this_gen) {
  const demux_wv_t *const this = (const demux_wv_t *) this_gen;

  return this->status;
}

static int demux_wv_get_stream_length (demux_plugin_t *const this_gen) {
  const demux_wv_t *const this = (demux_wv_t *) this_gen;

  return (this->samples*1000) / this->samplerate;
}

static uint32_t demux_wv_get_capabilities(demux_plugin_t *const this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_wv_get_optional_data(demux_plugin_t *const this_gen,
                                      void *data, const int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *const class_gen,
				    xine_stream_t *const stream,
				    input_plugin_t *const input) {
  demux_wv_t *const this = calloc(1, sizeof (demux_wv_t));
  this->stream = stream;
  this->input = input;

  this->demux_plugin.send_headers      = demux_wv_send_headers;
  this->demux_plugin.send_chunk        = demux_wv_send_chunk;
  this->demux_plugin.seek              = demux_wv_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_wv_get_status;
  this->demux_plugin.get_stream_length = demux_wv_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_wv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_wv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_wv_file(this)) {
      free (this);
      return NULL;
    }

  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

void *demux_wv_init_plugin (xine_t *const xine, void *const data) {
  demux_wv_class_t *const this = calloc(1, sizeof (demux_wv_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Wavpack demux plugin");
  this->demux_class.identifier      = "Wavpack";
  this->demux_class.mimetypes       = "audio/x-wavpack: wv,wvp: WavPack audio;";
  this->demux_class.extensions      = "wv wvp";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
