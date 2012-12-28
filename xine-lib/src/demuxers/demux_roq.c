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
 * RoQ File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the RoQ file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_roq"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_games.h"

#define RoQ_CHUNK_PREAMBLE_SIZE 8
#define RoQ_AUDIO_SAMPLE_RATE 22050

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned int         frame_pts_inc;

  xine_bmiheader       bih;
  xine_waveformatex    wave;

  int64_t              video_pts_counter;
  unsigned int         audio_byte_count;

} demux_roq_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_roq_class_t;

/* returns 1 if the RoQ file was opened successfully, 0 otherwise */
static int open_roq_file(demux_roq_t *this) {
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  int i;
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned int fps;

  if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) !=
      RoQ_CHUNK_PREAMBLE_SIZE)
    return 0;

  /* check for the RoQ magic numbers */
  static const uint8_t RoQ_MAGIC_STRING[] =
    { 0x10, 0x84, 0xFF, 0xFF, 0xFF, 0xFF };
  if( memcmp(preamble, RoQ_MAGIC_STRING, sizeof(RoQ_MAGIC_STRING)) != 0 )
    return 0;

  this->bih.biSize = sizeof(xine_bmiheader);
  this->bih.biWidth = this->bih.biHeight = 0;
  this->wave.nChannels = 0;  /* assume no audio at first */

  /*
   * RoQ files enjoy a constant framerate; pts calculation:
   *
   *   xine pts     frame #
   *   --------  =  -------  =>  xine pts = 90000 * frame # / fps
   *    90000         fps
   *
   * therefore, the frame pts increment is 90000 / fps
   */
  fps = _X_LE_16(&preamble[6]);
  this->frame_pts_inc = 90000 / fps;

  /* iterate through the first 2 seconds worth of chunks searching for
   * the RoQ_INFO chunk and an audio chunk */
  i = fps * 2;
  while (i-- > 0) {
    /* if this read fails, then maybe it's just a really small RoQ file
     * (even less than 2 seconds) */
    if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) !=
      RoQ_CHUNK_PREAMBLE_SIZE)
      break;
    chunk_type = _X_LE_16(&preamble[0]);
    chunk_size = _X_LE_32(&preamble[2]);

    if (chunk_type == RoQ_INFO) {
      /* fetch the width and height; reuse the preamble bytes */
      if (this->input->read(this->input, preamble, 8) != 8)
        break;

      this->bih.biWidth = _X_LE_16(&preamble[0]);
      this->bih.biHeight = _X_LE_16(&preamble[2]);

      /* if an audio chunk was already found, search is done */
      if (this->wave.nChannels)
        break;

      /* prep the size for a seek */
      chunk_size -= 8;
    } else {
      /* if it was an audio chunk and the info chunk has already been
       * found (as indicated by width and height) then break */
      if (chunk_type == RoQ_SOUND_MONO) {
        this->wave.nChannels = 1;
        if (this->bih.biWidth && this->bih.biHeight)
          break;
      } else if (chunk_type == RoQ_SOUND_STEREO) {
        this->wave.nChannels = 2;
        if (this->bih.biWidth && this->bih.biHeight)
          break;
      }
    }

    /* skip the rest of the chunk */
    this->input->seek(this->input, chunk_size, SEEK_CUR);
  }

  /* after all is said and done, if there is a width and a height,
   * regard it as being a valid file and reset to the first chunk */
  if (this->bih.biWidth && this->bih.biHeight) {
    this->input->seek(this->input, 8, SEEK_SET);
  } else {
    return 0;
  }

  this->video_pts_counter = this->audio_byte_count = 0;

  return 1;
}

static int demux_roq_send_chunk(demux_plugin_t *this_gen) {
  demux_roq_t *this = (demux_roq_t *) this_gen;

  buf_element_t *buf = NULL;
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  int64_t audio_pts;
  off_t current_file_pos;

  /* fetch the next preamble */
  if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) !=
    RoQ_CHUNK_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  chunk_type = _X_LE_16(&preamble[0]);
  chunk_size = _X_LE_32(&preamble[2]);

  /* if the chunk is an audio chunk, route it to the audio fifo */
  if ((chunk_type == RoQ_SOUND_MONO) || (chunk_type == RoQ_SOUND_STEREO)) {

    if( this->audio_fifo ) {

      /* do this calculation carefully because I can't trust the
       * 64-bit numerical manipulation */
      audio_pts = this->audio_byte_count;
      audio_pts *= 90000;
      audio_pts /= (RoQ_AUDIO_SAMPLE_RATE * this->wave.nChannels);
      this->audio_byte_count += chunk_size - 8;  /* do not count the preamble */

      current_file_pos = this->input->get_current_pos(this->input);

      /* send out the preamble */
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_ROQ;
      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos = (int)( (double) (current_file_pos - RoQ_CHUNK_PREAMBLE_SIZE) *
                                     65535 / this->input->get_length (this->input) );
      buf->pts = 0;
      buf->size = RoQ_CHUNK_PREAMBLE_SIZE;
      memcpy(buf->content, preamble, RoQ_CHUNK_PREAMBLE_SIZE);
      this->audio_fifo->put(this->audio_fifo, buf);

      /* packetize the audio */
      while (chunk_size) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_ROQ;
        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos = (int)( (double) current_file_pos *
                                     65535 / this->input->get_length (this->input) );
        buf->pts = audio_pts;

        if (chunk_size > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = chunk_size;
        chunk_size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        if (!chunk_size)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;
        this->audio_fifo->put(this->audio_fifo, buf);
      }
    } else {
      /* no audio -> skip chunk */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  } else if (chunk_type == RoQ_INFO) {
    /* skip 8 bytes */
    this->input->seek(this->input, chunk_size, SEEK_CUR);
  } else if ((chunk_type == RoQ_QUAD_CODEBOOK) ||
    (chunk_type == RoQ_QUAD_VQ)) {

    current_file_pos = this->input->get_current_pos(this->input);

    /* send out the preamble */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_ROQ;
    if( this->input->get_length (this->input) )
      buf->extra_info->input_normpos = (int)( (double) (current_file_pos - RoQ_CHUNK_PREAMBLE_SIZE) *
                                     65535 / this->input->get_length (this->input) );
    buf->pts = this->video_pts_counter;
    buf->size = RoQ_CHUNK_PREAMBLE_SIZE;
    memcpy(buf->content, preamble, RoQ_CHUNK_PREAMBLE_SIZE);
    this->video_fifo->put(this->video_fifo, buf);

    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_VIDEO_ROQ;
      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos = (int)( (double) current_file_pos *
                                     65535 / this->input->get_length (this->input) );
      buf->pts = this->video_pts_counter;

      if (chunk_size > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = chunk_size;
      chunk_size -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      /* only indicate end of video frame if this is a VQ chunk */
      if (!chunk_size && (chunk_type == RoQ_QUAD_VQ))
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->video_fifo->put(this->video_fifo, buf);
    }

    /* only advance pts counter on VQ frames */
    if (chunk_type == RoQ_QUAD_VQ)
      this->video_pts_counter += this->frame_pts_inc;
  } else {
    lprintf("encountered bad chunk type: %d\n", chunk_type);
  }

  return this->status;
}

static void demux_roq_send_headers(demux_plugin_t *this_gen) {
  demux_roq_t *this = (demux_roq_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
                       (this->wave.nChannels) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
                       this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
                       this->bih.biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                       this->wave.nChannels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       RoQ_AUDIO_SAMPLE_RATE);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, 16);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = this->frame_pts_inc;  /* initial video_step */
  buf->size = sizeof(xine_bmiheader);
  memcpy(buf->content, &this->bih, buf->size);
  buf->type = BUF_VIDEO_ROQ;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->wave.nChannels) {
    this->wave.nSamplesPerSec = RoQ_AUDIO_SAMPLE_RATE;
    this->wave.wBitsPerSample = 16;
    this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
    this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_ROQ;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = RoQ_AUDIO_SAMPLE_RATE;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = this->wave.nChannels;
    buf->size = sizeof(this->wave);
    memcpy(buf->content, &this->wave, buf->size);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_roq_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time, int playing) {

  demux_roq_t *this = (demux_roq_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* start after the signature chunk */
    this->input->seek(this->input, RoQ_CHUNK_PREAMBLE_SIZE, SEEK_SET);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_roq_get_status (demux_plugin_t *this_gen) {
  demux_roq_t *this = (demux_roq_t *) this_gen;

  return this->status;
}

static int demux_roq_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_roq_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_roq_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_roq_t    *this;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_roq_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_roq_send_headers;
  this->demux_plugin.send_chunk        = demux_roq_send_chunk;
  this->demux_plugin.seek              = demux_roq_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_roq_get_status;
  this->demux_plugin.get_stream_length = demux_roq_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_roq_get_capabilities;
  this->demux_plugin.get_optional_data = demux_roq_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_roq_file(this)) {
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

void *demux_roq_init_plugin (xine_t *xine, void *data) {
  demux_roq_class_t     *this;

  this  = calloc(1, sizeof(demux_roq_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Id RoQ file demux plugin");
  this->demux_class.identifier      = "RoQ";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "roq";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
