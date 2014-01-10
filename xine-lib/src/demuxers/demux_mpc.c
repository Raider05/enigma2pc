/*
 * Copyright (C) 2005-2012 the xine project
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
 * Musepack demuxer by James Stembridge <jstembridge@gmail.com>
 *
 * TODO:
 *   ID3 tag reading
 *   APE tag reading
 *   Seeking??
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_mpc"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"
#include "group_audio.h"
#include "id3.h"

/* Note that the header is actually 25 bytes long, so we'd only read 28
 * (because of byte swapping we have to round up to nearest multiple of 4)
 * if it weren't for libmusepack reading 32 bytes when it parses the header */
#define HEADER_SIZE 32

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned char        header[HEADER_SIZE];
  unsigned int         frames;
  double               samplerate;
  unsigned int         length;

  unsigned int         current_frame;
  unsigned int         next_frame_bits;
} demux_mpc_t;

typedef struct {
  demux_class_t     demux_class;
} demux_mpc_class_t;


/* Open a musepack file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if the musepack file was opened successfully. */
static int open_mpc_file(demux_mpc_t *this) {
  unsigned int first_frame_size;
  unsigned int id3v2_size = 0;

  /* Read the file header */
  if (_x_demux_read_header(this->input, this->header, HEADER_SIZE) != HEADER_SIZE)
      return 0;

  /* TODO: non-seeking version */
  if (INPUT_IS_SEEKABLE(this->input)) {
    /* Check for id3v2 tag */
    if (id3v2_istag(_X_BE_32(this->header))) {

      lprintf("found id3v2 header\n");

      /* Read tag size */

      id3v2_size = _X_BE_32_synchsafe(&this->header[6]) + 10;

      /* Add footer size if one is present */
      if (this->header[5] & 0x10)
        id3v2_size += 10;

      lprintf("id3v2 size: %u\n", id3v2_size);

      /* Seek past tag */
      if (this->input->seek(this->input, id3v2_size, SEEK_SET) < 0)
        return 0;

      /* Read musepack header */
      if (this->input->read(this->input, this->header, HEADER_SIZE) != HEADER_SIZE)
        return 0;
    }
  }

  /* Validate signature - We only support SV 7.x at the moment */
  if ( memcmp(this->header, "MP+", 3) != 0 ||
      ((this->header[3]&0x0f) != 0x07))
    return 0;

  /* Get frame count */
  this->current_frame = 0;
  this->frames = _X_LE_32(&this->header[4]);
  lprintf("number of frames: %u\n", this->frames);

  /* Get sample rate */
  switch ((_X_LE_32(&this->header[8]) >> 16) & 0x3) {
    case 0:
      this->samplerate = 44.1;
      break;
    case 1:
      this->samplerate = 48.0;
      break;
    case 2:
      this->samplerate = 37.8;
      break;
    case 3:
      this->samplerate = 32.0;
      break;
    default:
      break;
  }
  lprintf("samplerate: %f kHz\n", this->samplerate);

  /* Calculate stream length */
  this->length = (int) ((double) this->frames * 1152 / this->samplerate);
  lprintf("stream length: %d ms\n", this->length);

  /* Calculate the number of bits of the first frame that are still be sent */
  first_frame_size = (_X_LE_32(&this->header[24]) >> 4) & 0xFFFFF;
  this->next_frame_bits =  first_frame_size - 4;
  lprintf("first frame size: %u\n", first_frame_size);

  /* Move input to start of data (to nearest multiple of 4) */
  this->input->seek(this->input, 28+id3v2_size, SEEK_SET);

  /* Set stream info */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, _X_ME_32(this->header));

  return 1;
}

static int demux_mpc_send_chunk(demux_plugin_t *this_gen) {
  demux_mpc_t *this = (demux_mpc_t *) this_gen;
  unsigned int bits_to_read, bytes_to_read, extra_bits_read, next_frame_size;
  off_t bytes_read;

  buf_element_t *buf = NULL;

  /* Check if we've finished */
  if (this->current_frame++ == this->frames) {
    lprintf("all frames read\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  lprintf("current frame: %u\n", this->current_frame);

  /* Get a buffer */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_MPC;
  buf->pts = 0;
  buf->extra_info->total_time = this->length;

  /* Set normalised position */
  buf->extra_info->input_normpos =
    (int) ((double) this->input->get_current_pos(this->input) * 65535 /
           this->input->get_length(this->input));

  /* Set time based on there being 1152 audio frames per frame */
  buf->extra_info->input_time =
    (int) ((double) this->current_frame * 1152 / this->samplerate);

  /* Calculate the number of bits that need to be read to finish reading
   * the current frame and read the size of the next frame. This number
   * has to be rounded up to the nearest 4 bytes on account of the
   * byte swapping used */
  bits_to_read = (this->next_frame_bits+20+31) & ~31;
  bytes_to_read = bits_to_read / 8;

  /* Check we'll be able to read directly into the buffer */
  if (bytes_to_read > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("demux_mpc: frame too big for buffer"));
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* Read data */
  bytes_read = this->input->read(this->input, buf->content, bytes_to_read);
  if(bytes_read <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  } else
    buf->size = bytes_read;

  /* Read the size of the next frame */
  if (this->current_frame < this->frames) {
    /* The number of bits of the next frame we've read */
    extra_bits_read = bits_to_read - (this->next_frame_bits+20);

    if(extra_bits_read <= 12)
      next_frame_size = (_X_LE_32(&buf->content[bytes_to_read-4]) >> extra_bits_read) & 0xFFFFF;
    else
      next_frame_size = ((_X_LE_32(&buf->content[bytes_to_read-8]) << (32-extra_bits_read)) |
                        (_X_LE_32(&buf->content[bytes_to_read-4]) >> extra_bits_read)) & 0xFFFFF;

    lprintf("next frame size: %u\n", next_frame_size);

    /* The number of bits of the next frame still to read */
    this->next_frame_bits = next_frame_size - extra_bits_read;
  }

  /* Each buffer contains at least one frame */
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put(this->audio_fifo, buf);

  return this->status;
}

static void demux_mpc_send_headers(demux_plugin_t *this_gen) {
  demux_mpc_t *this = (demux_mpc_t *) this_gen;
  buf_element_t *buf;

  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* Send start buffers */
  _x_demux_control_start(this->stream);

  /* Send header to decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    buf->type            = BUF_AUDIO_MPC;
    buf->decoder_flags   = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = this->input->get_length(this->input);
    buf->decoder_info[1] = 0;
    buf->decoder_info[2] = 0;
    buf->decoder_info[3] = 0;

    /* Copy the header */
    buf->size = HEADER_SIZE;
    memcpy(buf->content, this->header, buf->size);

    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_mpc_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_mpc_t *this = (demux_mpc_t *) this_gen;

  /* If thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_mpc_get_status (demux_plugin_t *this_gen) {
  demux_mpc_t *this = (demux_mpc_t *) this_gen;

  return this->status;
}

static int demux_mpc_get_stream_length (demux_plugin_t *this_gen) {
//  demux_mpc_t *this = (demux_mpc_t *) this_gen;

  return 0;
}

static uint32_t demux_mpc_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpc_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_mpc_t    *this;

  this         = calloc(1, sizeof(demux_mpc_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpc_send_headers;
  this->demux_plugin.send_chunk        = demux_mpc_send_chunk;
  this->demux_plugin.seek              = demux_mpc_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_mpc_get_status;
  this->demux_plugin.get_stream_length = demux_mpc_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpc_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpc_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_mpc_file(this)) {
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

void *demux_mpc_init_plugin (xine_t *xine, void *data) {
  demux_mpc_class_t     *this;

  this = calloc(1, sizeof(demux_mpc_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Musepack demux plugin");
  this->demux_class.identifier      = "Musepack";
  this->demux_class.mimetypes       =
         "audio/musepack: mpc, mp+, mpp: Musepack audio;"
         "audio/x-musepack: mpc, mp+, mpp: Musepack audio;";
  this->demux_class.extensions      = "mpc mp+ mpp";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
