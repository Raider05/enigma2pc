/*
 * Copyright (C) 2006 the xine project
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
 * True Audio demuxer by Diego Pettenò <flameeyes@gentoo.org>
 * Inspired by tta libavformat demuxer by Alex Beregszaszi
 *
 * Seek + time support added by Kelvie Wong <kelvie@ieee.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LOG_MODULE "demux_tta"
#define LOG_VERBOSE

// This is from the TTA spec, the length (in seconds) of a frame
// http://www.true-audio.com/TTA_Lossless_Audio_Codec_-_Format_Description
#define FRAME_TIME 1.04489795918367346939

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"
#include "group_audio.h"
#include <xine/attributes.h>

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;

  uint32_t            *seektable;
  uint32_t             totalframes;
  uint32_t             currentframe;

  off_t                datastart;

  int                  status;

  union {
    struct tta_header {
      uint32_t signature; /* TTA1 */
      uint16_t flags;     /* Skipped */
      uint16_t channels;
      uint16_t bits_per_sample;
      uint32_t samplerate;
      uint32_t data_length; /* Number of samples */
      uint32_t crc32;
    } XINE_PACKED tta;
    uint8_t buffer[22]; /* This is the size of the header */
  } header;
} demux_tta_t;

typedef struct {
  demux_class_t     demux_class;
} demux_tta_class_t;

static int open_tta_file(demux_tta_t *this) {
  uint32_t peek;
  uint32_t framelen;

  if (_x_demux_read_header(this->input, &peek, 4) != 4)
      return 0;

  if ( !_x_is_fourcc(&peek, "TTA1") )
    return 0;

  if ( this->input->read(this->input, this->header.buffer, sizeof(this->header)) != sizeof(this->header) )
    return 0;

  framelen = (uint32_t)(FRAME_TIME * le2me_32(this->header.tta.samplerate));
  this->totalframes = le2me_32(this->header.tta.data_length) / framelen + ((le2me_32(this->header.tta.data_length) % framelen) ? 1 : 0);
  this->currentframe = 0;

  if(this->totalframes >= UINT_MAX/sizeof(uint32_t)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, _("demux_tta: total frames count too high\n"));
    return 0;
  }

  this->seektable = xine_xcalloc(this->totalframes, sizeof(uint32_t));
  this->input->read(this->input, (uint8_t*)this->seektable, sizeof(uint32_t)*this->totalframes);

  /* Skip the CRC32 */
  this->input->seek(this->input, 4, SEEK_CUR);

  /* Store the offset after the header for seeking */
  this->datastart = this->input->get_current_pos(this->input);

  return 1;
}

static int demux_tta_send_chunk(demux_plugin_t *this_gen) {
  demux_tta_t *this = (demux_tta_t *) this_gen;
  uint32_t bytes_to_read;

  if ( this->currentframe >= this->totalframes ) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  bytes_to_read = le2me_32(this->seektable[this->currentframe]);

  _x_demux_read_send_data(this->audio_fifo,
                          this->input,
                          bytes_to_read,
                          (int64_t)(FRAME_TIME * this->currentframe * 90000),
                          BUF_AUDIO_TTA,
                          /*decoder_flags*/ 0,
                          (int) ((double) this->currentframe * 65535.0 / this->totalframes),
                          (int)(FRAME_TIME * this->currentframe * 1000),
                          (int)(le2me_32(this->header.tta.data_length) * 1000.0 /
                                le2me_32(this->header.tta.samplerate)),
                          this->currentframe);

  this->currentframe++;

  return this->status;
}

static void demux_tta_send_headers(demux_plugin_t *this_gen) {
  demux_tta_t *this = (demux_tta_t *) this_gen;
  buf_element_t *buf;
  xine_waveformatex wave;
  uint32_t total_size = sizeof(xine_waveformatex) + sizeof(this->header) +
    sizeof(uint32_t)*this->totalframes;
  unsigned char *header;

  header = malloc(total_size);

  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
		     le2me_16(this->header.tta.channels));
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
		     le2me_32(this->header.tta.samplerate));
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
		     le2me_16(this->header.tta.bits_per_sample));

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* create header */
  wave.cbSize = total_size - sizeof(xine_waveformatex);

  memcpy(header, &wave, sizeof(wave));
  memcpy(header+sizeof(xine_waveformatex), this->header.buffer, sizeof(this->header));
  memcpy(header+sizeof(xine_waveformatex)+sizeof(this->header), this->seektable, sizeof(uint32_t)*this->totalframes);

  /* send init info to decoders */
  if (this->audio_fifo) {
    uint32_t bytes_left = total_size;

    /* We are sending the seektable as well, and this may be larger than
       buf->max_size */
    while (bytes_left) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER;
      buf->type = BUF_AUDIO_TTA;

      /* Copy min(bytes_left, max_size) bytes */
      buf->size = bytes_left < buf->max_size ? bytes_left : buf->max_size;
      memcpy(buf->content, header+(total_size-bytes_left), buf->size);

      bytes_left -= buf->size;

      /* The decoder information only needs the decoder information on the last
         buffer element. */
      if (!bytes_left) {
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
        buf->decoder_info[0] = 0;
        buf->decoder_info[1] = le2me_32(this->header.tta.samplerate);
        buf->decoder_info[2] = le2me_16(this->header.tta.bits_per_sample);
        buf->decoder_info[3] = le2me_16(this->header.tta.channels);
      }
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }
  free(header);
}

static int demux_tta_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_tta_t *this = (demux_tta_t *) this_gen;
  uint32_t start_frame;
  uint32_t frame_index;
  int64_t pts;
  off_t start_off = this->datastart;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

  } else {

    /* Get the starting frame */
    if( start_pos ) {
        pts = start_pos * le2me_32(this->header.tta.data_length) * 1000.0 / le2me_32(this->header.tta.samplerate) * 90 / 65535;
      start_frame = start_pos * this->totalframes / 65535;

    } else {
      pts = start_time * 90;
      start_frame = (uint32_t)((double)start_time/ 1000.0 / FRAME_TIME);
    }

    /* Now we find the offset */
    for( frame_index = 0; frame_index < start_frame; frame_index++ )
        start_off += le2me_32(this->seektable[frame_index]);

    /* Let's seek!  We store the current frame internally, so let's update that
     * as well */
    _x_demux_flush_engine(this->stream);
    this->input->seek(this->input, start_off, SEEK_SET);
    this->currentframe = start_frame;
    _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static void demux_tta_dispose (demux_plugin_t *this_gen) {
  demux_tta_t *this = (demux_tta_t *) this_gen;

  free(this->seektable);
  free(this);
}

static int demux_tta_get_status (demux_plugin_t *this_gen) {
  demux_tta_t *this = (demux_tta_t *) this_gen;

  return this->status;
}

static int demux_tta_get_stream_length (demux_plugin_t *this_gen) {
  demux_tta_t *this = (demux_tta_t *) this_gen;
  return le2me_32(this->header.tta.data_length) * 1000.0 / le2me_32(this->header.tta.samplerate); /* milliseconds */
}

static uint32_t demux_tta_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_tta_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_tta_t    *this;

  this         = calloc(1, sizeof(demux_tta_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_tta_send_headers;
  this->demux_plugin.send_chunk        = demux_tta_send_chunk;
  this->demux_plugin.seek              = demux_tta_seek;
  this->demux_plugin.dispose           = demux_tta_dispose;
  this->demux_plugin.get_status        = demux_tta_get_status;
  this->demux_plugin.get_stream_length = demux_tta_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_tta_get_capabilities;
  this->demux_plugin.get_optional_data = demux_tta_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  this->seektable = NULL;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:
    if (!open_tta_file(this)) {
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

void *demux_tta_init_plugin (xine_t *xine, void *data) {
  demux_tta_class_t     *this;

  this = calloc(1, sizeof(demux_tta_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("True Audio demux plugin");
  this->demux_class.identifier      = "True Audio";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "tta";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
