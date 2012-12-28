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
 * VQA File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the VQA file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Quick technical note: VQA files begin with a header that includes a
 * frame index. This ought to be useful for seeking within a VQA file.
 * However, seeking is infeasible due to the audio encoding: Each audio
 * block needs information from the previous audio block in order to be
 * decoded, thus making random seeking difficult.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_games.h"

#define FOURCC_TAG BE_FOURCC
#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define WVQA_TAG FOURCC_TAG('W', 'V', 'Q', 'A')
#define VQHD_TAG FOURCC_TAG('V', 'Q', 'H', 'D')
#define FINF_TAG FOURCC_TAG('F', 'I', 'N', 'F')
#define SND0_TAG FOURCC_TAG('S', 'N', 'D', '0')
#define SND2_TAG FOURCC_TAG('S', 'N', 'D', '2')
#define VQFR_TAG FOURCC_TAG('V', 'Q', 'F', 'R')

#define VQA_HEADER_SIZE 0x2A
#define VQA_FRAMERATE 15
#define VQA_PTS_INC (90000 / VQA_FRAMERATE)
#define VQA_PREAMBLE_SIZE 8

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                filesize;

  xine_bmiheader       bih;
  unsigned char        vqa_header[VQA_HEADER_SIZE];
  xine_waveformatex    wave;

  int64_t              video_pts;
  unsigned int         audio_frames;
  unsigned int         iteration;
} demux_vqa_t;

typedef struct {
  demux_class_t     demux_class;
} demux_vqa_class_t;

/* returns 1 if the VQA file was opened successfully, 0 otherwise */
static int open_vqa_file(demux_vqa_t *this) {
  unsigned char scratch[12];
  unsigned int chunk_size;

  if (_x_demux_read_header(this->input, scratch, 12) != 12)
    return 0;

  /* check for the VQA signatures */
  if (!_x_is_fourcc(&scratch[0], "FORM") ||
      !_x_is_fourcc(&scratch[8], "WVQA") )
    return 0;

  /* file is qualified; skip to the start of the VQA header */
  this->input->seek(this->input, 20, SEEK_SET);

  /* get the actual filesize */
  if ( !(this->filesize = this->input->get_length(this->input)) )
    this->filesize = 1;

  /* load the VQA header */
  if (this->input->read(this->input, this->vqa_header, VQA_HEADER_SIZE) !=
    VQA_HEADER_SIZE)
    return 0;

  this->bih.biSize = sizeof(xine_bmiheader) + VQA_HEADER_SIZE;
  this->bih.biWidth = _X_LE_16(&this->vqa_header[6]);
  this->bih.biHeight = _X_LE_16(&this->vqa_header[8]);
  this->wave.nSamplesPerSec = _X_LE_16(&this->vqa_header[24]);
  this->wave.nChannels = this->vqa_header[26];
  this->wave.wBitsPerSample = 16;

  /* skip the FINF chunk */
  if (this->input->read(this->input, scratch, VQA_PREAMBLE_SIZE) !=
    VQA_PREAMBLE_SIZE)
    return 0;
  chunk_size = _X_BE_32(&scratch[4]);
  this->input->seek(this->input, chunk_size, SEEK_CUR);

  this->video_pts = this->audio_frames = 0;
  this->iteration = 0;

  return 1;
}

static int demux_vqa_send_chunk(demux_plugin_t *this_gen) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned char preamble[VQA_PREAMBLE_SIZE];
  unsigned int chunk_size;
  off_t current_file_pos;
  int skip_byte;
  int64_t audio_pts;

  /* load and dispatch the audio portion of the frame */
  if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
    VQA_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  current_file_pos = this->input->get_current_pos(this->input);
  chunk_size = _X_BE_32(&preamble[4]);
  skip_byte = chunk_size & 0x1;
  audio_pts = this->audio_frames;
  audio_pts *= 90000;
  audio_pts /= this->wave.nSamplesPerSec;
  this->audio_frames += (chunk_size * 2 / this->wave.nChannels);

  while (chunk_size) {
    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_VQA_IMA;
      if( this->filesize )
         buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->filesize);
      buf->extra_info->input_time = audio_pts / 90;
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

      this->audio_fifo->put (this->audio_fifo, buf);
    }else{
      this->input->seek(this->input, chunk_size, SEEK_CUR);
      chunk_size = 0;
    }
  }
  /* stay on 16-bit alignment */
  if (skip_byte)
    this->input->seek(this->input, 1, SEEK_CUR);

  /* load and dispatch the video portion of the frame but only if this
   * is not frame #0 */
  if (this->iteration > 0) {
    if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
      VQA_PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    current_file_pos = this->input->get_current_pos(this->input);
    chunk_size = _X_BE_32(&preamble[4]);
    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_VQA;
      if( this->filesize )
         buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->filesize);
      buf->extra_info->input_time = this->video_pts / 90;
      buf->pts = this->video_pts;

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

      this->video_fifo->put (this->video_fifo, buf);
    }
    this->video_pts += VQA_PTS_INC;
  }

  this->iteration++;

  return this->status;
}

static void demux_vqa_send_headers(demux_plugin_t *this_gen) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;
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
                       this->wave.nSamplesPerSec);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                       this->wave.wBitsPerSample);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = VQA_PTS_INC;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(xine_bmiheader));
  memcpy(buf->content + sizeof(xine_bmiheader), this->vqa_header, VQA_HEADER_SIZE);
  buf->size = sizeof(xine_bmiheader) + VQA_HEADER_SIZE;
  buf->type = BUF_VIDEO_VQA;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->wave.nChannels) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_VQA_IMA;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave.nSamplesPerSec;
    buf->decoder_info[2] = 16;  /* bits/samples */
    buf->decoder_info[3] = 1;   /* channels */
    this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
    this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;
    memcpy(buf->content, &this->wave, sizeof(this->wave));
    buf->size = sizeof(this->wave);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_vqa_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time, int playing) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {
    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_vqa_get_status (demux_plugin_t *this_gen) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  return this->status;
}

static int demux_vqa_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_vqa_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_vqa_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_vqa_t    *this;

  this         = calloc(1, sizeof(demux_vqa_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_vqa_send_headers;
  this->demux_plugin.send_chunk        = demux_vqa_send_chunk;
  this->demux_plugin.seek              = demux_vqa_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_vqa_get_status;
  this->demux_plugin.get_stream_length = demux_vqa_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_vqa_get_capabilities;
  this->demux_plugin.get_optional_data = demux_vqa_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_vqa_file(this)) {
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

void *demux_vqa_init_plugin (xine_t *xine, void *data) {
  demux_vqa_class_t     *this;

  this = calloc(1, sizeof(demux_vqa_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Westwood Studios VQA file demux plugin");
  this->demux_class.identifier      = "VQA";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "vqa";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
