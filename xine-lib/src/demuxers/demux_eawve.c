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
 */

/*
 * demux_eawve.c, Demuxer plugin for Electronic Arts' WVE file format
 *
 * written and currently maintained by Robin Kay <komadori@myrealbox.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_MODULE "demux_eawve"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "bswap.h"
#include <xine/demux.h>
#include "group_games.h"

#define FOURCC_TAG BE_FOURCC

typedef struct {
  demux_plugin_t   demux_plugin;

  xine_stream_t   *stream;
  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;
  input_plugin_t  *input;
  int              status;

  int              thread_running;

  int              num_channels;
  int              compression_type;
  int              num_samples;
  int              sample_counter;
} demux_eawve_t;

typedef struct {
  demux_class_t     demux_class;
} demux_eawve_class_t;

typedef struct {
  uint32_t id;
  uint32_t size;
} chunk_header_t;

/*
 * Read an arbitary number of byte into a word
 */

static uint32_t read_arbitary(input_plugin_t *input){
  uint8_t size;

  if (input->read(input, (void*)&size, 1) != 1) {
    return 0;
  }

  uint32_t word = 0;
  int i;
  for (i=0;i<size;i++) {
    uint8_t byte;
    if (input->read(input, (void*)&byte, 1) != 1) {
      return 0;
    }
    word <<= 8;
    word |= byte;
  }

  return word;
}

/*
 * Process WVE file header
 * Returns 1 if the WVE file is valid and successfully opened, 0 otherwise
 */

static int process_header(demux_eawve_t *this){
  uint8_t header[12];

  if (this->input->get_current_pos(this->input) != 0)
    this->input->seek(this->input, 0, SEEK_SET);

  if (this->input->read(this->input, header, sizeof(header)) != sizeof(header))
    return 0;

  if (!_x_is_fourcc(&header[0], "SCHl"))
    return 0;

  if (!_x_is_fourcc(&header[8], "PT\0\0")) {
    lprintf("PT header missing\n");
    return 0;
  }

  const uint32_t size = _X_LE_32(&header[4]);

  int inHeader = 1;
  while (inHeader) {
    int inSubheader;
    uint8_t byte;
    if (this->input->read(this->input, (void*)&byte, 1) != 1) {
      return 0;
    }

    switch (byte) {
      case 0xFD:
        lprintf("entered audio subheader\n");
        inSubheader = 1;
        while (inSubheader) {
          uint8_t subbyte;
          if (this->input->read(this->input, (void*)&subbyte, 1) != 1) {
            return 0;
          }

          switch (subbyte) {
            case 0x82:
              this->num_channels = read_arbitary(this->input);
              lprintf("num_channels (element 0x82) set to 0x%08x\n", this->num_channels);
            break;
            case 0x83:
              this->compression_type = read_arbitary(this->input);
              lprintf("compression_type (element 0x83) set to 0x%08x\n", this->compression_type);
            break;
            case 0x85:
              this->num_samples = read_arbitary(this->input);
              lprintf("num_samples (element 0x85) set to 0x%08x\n", this->num_samples);
            break;
            default:
              lprintf("element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(this->input));
            break;
            case 0x8A:
              lprintf("element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(this->input));
              lprintf("exited audio subheader\n");
              inSubheader = 0;
            break;
          }
        }
      break;
      default:
        lprintf("header element 0x%02x set to 0x%08x\n", byte, read_arbitary(this->input));
      break;
      case 0xFF:
        lprintf("end of header block reached\n");
        inHeader = 0;
      break;
    }
  }

  if ((this->num_channels != 2) || (this->compression_type != 7)) {
    lprintf("unsupported stream type\n");
    return 0;
  }

  if (this->input->seek(this->input, size - this->input->get_current_pos(this->input), SEEK_CUR) < 0) {
    return 0;
  }

  return 1;
}

/*
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 * All the following functions are defined by the xine demuxer API
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 */

static int demux_eawve_send_chunk(demux_eawve_t *this){
  chunk_header_t header;

  if (this->input->read(this->input, (void*)&header, sizeof(chunk_header_t)) != sizeof(chunk_header_t)) {
    lprintf("read error\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  header.id = be2me_32(header.id);
  header.size = le2me_32(header.size) - 8;

  switch (header.id) {
    case FOURCC_TAG('S', 'C', 'D', 'l'): {
      int first_segment = 1;

      while (header.size > 0) {
        buf_element_t *buf;

        buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
        buf->type = BUF_AUDIO_EA_ADPCM;
        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                           65535 / this->input->get_length (this->input) );
        buf->extra_info->input_time = (int)((int64_t)this->sample_counter * 1000 / 22050);
        buf->pts = this->sample_counter;
        buf->pts *= 90000;
        buf->pts /= 22050;

        if (header.size > buf->max_size) {
          buf->size = buf->max_size;
        }
        else {
          buf->size = header.size;
        }
        header.size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
          lprintf("read error\n");
          this->status = DEMUX_FINISHED;
          buf->free_buffer(buf);
          break;
        }

        if (first_segment) {
          buf->decoder_flags |= BUF_FLAG_FRAME_START;
          this->sample_counter += _X_LE_32(buf->content);
          first_segment = 0;
        }

        if (header.size == 0) {
          buf->decoder_flags |= BUF_FLAG_FRAME_END;
        }

        this->audio_fifo->put(this->audio_fifo, buf);
      }
    }
    break;

    case FOURCC_TAG('S', 'C', 'E', 'l'): {
      this->status = DEMUX_FINISHED;
    }
    break;

    default: {
      if (this->input->seek(this->input, header.size, SEEK_CUR) < 0) {
        lprintf("read error\n");
        this->status = DEMUX_FINISHED;
      }
    }
    break;
  }

  return this->status;
}

static void demux_eawve_send_headers(demux_plugin_t *this_gen){
  demux_eawve_t *this = (demux_eawve_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, 2);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, 22050);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, 16);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf_element_t *buf;

    buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
    buf->type = BUF_AUDIO_EA_ADPCM;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 22050;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = 2;
    this->audio_fifo->put(this->audio_fifo, buf);
  }
}

static int demux_eawve_seek(demux_eawve_t *this, off_t start_pos, int start_time, int playing){

  if (!this->thread_running) {
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
    this->sample_counter = 0;

    this->thread_running = 1;
  }

  return this->status;
}

static int demux_eawve_get_status(demux_eawve_t *this){
  return this->status;
}

static int demux_eawve_get_stream_length(demux_eawve_t *this){
  return (int)((int64_t)this->num_samples * 1000 / 22050);
}

static uint32_t demux_eawve_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_eawve_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t* open_plugin(demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input){
  demux_eawve_t    *this;

  if (!INPUT_IS_SEEKABLE(input))
    return NULL;

  this         = calloc(1, sizeof(demux_eawve_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = (void*)demux_eawve_send_headers;
  this->demux_plugin.send_chunk        = (void*)demux_eawve_send_chunk;
  this->demux_plugin.seek              = (void*)demux_eawve_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = (void*)demux_eawve_get_status;
  this->demux_plugin.get_stream_length = (void*)demux_eawve_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_eawve_get_capabilities;
  this->demux_plugin.get_optional_data = demux_eawve_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!process_header(this)) {
      free(this);
      return NULL;
    }

  break;

  default:
    free(this);
    return NULL;
  }

  return &this->demux_plugin;
}

void *demux_eawve_init_plugin(xine_t *xine, void *data) {
  demux_eawve_class_t     *this;

  this = calloc(1, sizeof(demux_eawve_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Electronics Arts WVE format demux plugin");
  this->demux_class.identifier      = "EA WVE";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "wve";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
