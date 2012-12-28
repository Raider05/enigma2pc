/*
 * Copyright (C) 2004 the xine project
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
 * Sierra Video and Music Data (.vmd) File Demuxer
 *   by Mike Melanson (melanson@pcisys.net)
 * For more information on the VMD file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Note that the only way that this demuxer validates by content is by
 * checking the first 2 bytes, which are 0x2E 0x03 in a Sierra VMD file.
 * There is a 1/65536 chance of a false positive using this method.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_vmd"
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

#define VMD_HEADER_SIZE 0x330
#define BYTES_PER_FRAME_RECORD 16

typedef struct {
  int is_audio_frame;
  off_t frame_offset;
  unsigned int frame_size;
  int64_t pts;
  int keyframe;
  unsigned char frame_record[BYTES_PER_FRAME_RECORD];
} vmd_frame_t;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                data_size;

  xine_bmiheader       bih;
  unsigned char        vmd_header[VMD_HEADER_SIZE];
  xine_waveformatex    wave;

  unsigned int         audio_frames;
  unsigned int         iteration;

  unsigned int         frame_count;
  vmd_frame_t         *frame_table;
  unsigned int         current_frame;

  int64_t              video_pts_inc;
  int64_t              total_pts;

} demux_vmd_t;

typedef struct {
  demux_class_t     demux_class;
} demux_vmd_class_t;

/* returns 1 if the VMD file was opened successfully, 0 otherwise */
static int open_vmd_file(demux_vmd_t *this) {

  unsigned char *vmd_header = this->vmd_header;
  off_t toc_offset;
  unsigned char *raw_frame_table;
  unsigned int raw_frame_table_size;
  unsigned char *current_frame_record;
  off_t current_offset;
  int i;
  unsigned int total_frames;
  int64_t current_video_pts = 0;

  if (_x_demux_read_header(this->input, vmd_header, VMD_HEADER_SIZE) !=
    VMD_HEADER_SIZE)
    return 0;

  if (_X_LE_16(&vmd_header[0]) != VMD_HEADER_SIZE - 2)
    return 0;

  /* file is minimally qualified at this point, proceed to load */

  /* get the actual filesize */
  if ( !(this->data_size = this->input->get_length(this->input)) )
    this->data_size = 1;

  this->bih.biSize = sizeof(xine_bmiheader) + VMD_HEADER_SIZE;
  this->bih.biWidth = _X_LE_16(&vmd_header[12]);
  this->bih.biHeight = _X_LE_16(&vmd_header[14]);
  this->wave.nSamplesPerSec = _X_LE_16(&vmd_header[804]);
  this->wave.nChannels =  (vmd_header[811] & 0x80) ? 2 : 1;
  this->wave.nBlockAlign = _X_LE_16(&vmd_header[806]);
  if (this->wave.nBlockAlign & 0x8000) {
      this->wave.nBlockAlign -= 0x8000;
      this->wave.wBitsPerSample = 16;
  } else {
      this->wave.wBitsPerSample = 8;
  }

  /* decide on a framerate */
  if (this->wave.nSamplesPerSec) {
    this->video_pts_inc = 90000;
    this->video_pts_inc *= this->wave.nBlockAlign;
    this->video_pts_inc /= this->wave.nSamplesPerSec;
  } else {
    this->video_pts_inc = 90000 / 10;
  }

  /* skip over the offset table and load the table of contents; don't
   * care about the offset table since demuxer will calculate those
   * independently */
  toc_offset = _X_LE_32(&vmd_header[812]);
  this->frame_count = _X_LE_16(&vmd_header[6]);
  this->input->seek(this->input, toc_offset + this->frame_count * 6, SEEK_SET);

  /* while we have the toal number of blocks, calculate the total running
   * time */
  this->total_pts = this->frame_count;
  this->total_pts *= this->video_pts_inc;
  this->total_pts /= 90;

  /* 2 frames for every block reported on disk */
  this->frame_count *= 2;

  raw_frame_table_size = this->frame_count * BYTES_PER_FRAME_RECORD;
  raw_frame_table = xine_xmalloc(raw_frame_table_size);
  if (this->input->read(this->input, raw_frame_table, raw_frame_table_size) !=
    raw_frame_table_size) {
    free(raw_frame_table);
    return 0;
  }

  this->frame_table = calloc(this->frame_count, sizeof(vmd_frame_t));

  current_offset = this->data_start = _X_LE_32(&vmd_header[20]);
  this->data_size = toc_offset - this->data_start;
  current_frame_record = raw_frame_table;
  total_frames = this->frame_count;
  i = 0;
  while (total_frames--) {
    /* if the frame size is 0, do not count the frame and bring the
     * total frame count down */
    this->frame_table[i].frame_size = _X_LE_32(&current_frame_record[2]);

    /* this logic is present so that 0-length audio chunks are not
     * accounted */
    if (!this->frame_table[i].frame_size) {
      this->frame_count--;  /* one less frame to count */
      current_frame_record += BYTES_PER_FRAME_RECORD;
      continue;
    }

    if (current_frame_record[0] == 0x02) {
      this->frame_table[i].is_audio_frame = 0;
      this->frame_table[i].pts = current_video_pts;
      current_video_pts += this->video_pts_inc;
    } else {
      this->frame_table[i].is_audio_frame = 1;
      this->frame_table[i].pts = 0;
    }
    this->frame_table[i].frame_offset = current_offset;
    current_offset += this->frame_table[i].frame_size;
    memcpy(this->frame_table[i].frame_record, current_frame_record,
      BYTES_PER_FRAME_RECORD);

    current_frame_record += BYTES_PER_FRAME_RECORD;
    i++;
  }

  free(raw_frame_table);
  this->current_frame = 0;

  return 1;
}

static int demux_vmd_send_chunk(demux_plugin_t *this_gen) {

  demux_vmd_t *this = (demux_vmd_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int remaining_bytes;
  vmd_frame_t *frame;

  if (this->current_frame >= this->frame_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  frame = &this->frame_table[this->current_frame];
  /* position the stream (will probably be there already) */
  this->input->seek(this->input, frame->frame_offset, SEEK_SET);
  remaining_bytes = frame->frame_size;

  if (!frame->is_audio_frame) {

    /* send off the frame record first in its own buffer */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_VMD;
    if( this->data_size )
      buf->extra_info->input_normpos = (int)( (double) (frame->frame_offset - this->data_start) *
                                              65535 / this->data_size);
    memcpy(buf->content, frame->frame_record, BYTES_PER_FRAME_RECORD);
    buf->size = BYTES_PER_FRAME_RECORD;
    buf->pts = frame->pts;
    buf->extra_info->input_time = buf->pts / 90;
    this->video_fifo->put(this->video_fifo, buf);

    while (remaining_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_VMD;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double) (frame->frame_offset - this->data_start) *
                                              65535 / this->data_size);

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      buf->pts = frame->pts;
      buf->extra_info->input_time = buf->pts / 90;
      this->video_fifo->put(this->video_fifo, buf);
    }

  } else if (frame->is_audio_frame && this->audio_fifo) {

#if 0
    /* send off the frame record first in its own buffer */
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_VMD;
    if( this->data_size )
      buf->extra_info->input_normpos = (int)( (double) (frame->frame_offset - this->data_start) *
                                              65535 / this->data_size);
    memcpy(buf->content, frame->frame_record, BYTES_PER_FRAME_RECORD);
    buf->size = BYTES_PER_FRAME_RECORD;
    buf->pts = 0;  /* let the engine sort out the audio pts */
    buf->extra_info->input_time = 0;
    this->audio_fifo->put(this->audio_fifo, buf);

    while (remaining_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_VMD;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double) (frame->frame_offset - this->data_start) *
                                              65535 / this->data_size);

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      buf->pts = 0;  /* let the engine sort out the audio pts */
      buf->extra_info->input_time = 0;
      this->audio_fifo->put(this->audio_fifo, buf);
    }
#endif
  }

  this->current_frame++;

  return this->status;
}

static void demux_vmd_send_headers(demux_plugin_t *this_gen) {
  demux_vmd_t *this = (demux_vmd_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
                       (this->wave.nSamplesPerSec) ? 1 : 0);
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
  buf->decoder_info[0] = this->video_pts_inc;  /* initial duration */
  memcpy(buf->content, &this->bih, sizeof(xine_bmiheader));
  memcpy(buf->content + sizeof(xine_bmiheader), this->vmd_header, VMD_HEADER_SIZE);
  buf->size = sizeof(xine_bmiheader) + VMD_HEADER_SIZE;
  buf->type = BUF_VIDEO_VMD;
  this->video_fifo->put (this->video_fifo, buf);

#if 0
  if (this->audio_fifo && this->wave.nSamplesPerSec) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_VMD;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave.nSamplesPerSec;
    buf->decoder_info[2] = this->wave.wBitsPerSample;
    buf->decoder_info[3] = this->wave.nChannels;
    this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
    this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;
    memcpy(buf->content, &this->wave, sizeof(this->wave));
    buf->size = sizeof(this->wave);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
#endif
}

static int demux_vmd_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_vmd_t *this = (demux_vmd_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {
    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_vmd_get_status (demux_plugin_t *this_gen) {
  demux_vmd_t *this = (demux_vmd_t *) this_gen;

  return this->status;
}

static int demux_vmd_get_stream_length (demux_plugin_t *this_gen) {
  demux_vmd_t *this = (demux_vmd_t *) this_gen;

  return this->total_pts;
}

static uint32_t demux_vmd_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_vmd_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_vmd_t    *this;

  this         = calloc(1, sizeof(demux_vmd_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_vmd_send_headers;
  this->demux_plugin.send_chunk        = demux_vmd_send_chunk;
  this->demux_plugin.seek              = demux_vmd_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_vmd_get_status;
  this->demux_plugin.get_stream_length = demux_vmd_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_vmd_get_capabilities;
  this->demux_plugin.get_optional_data = demux_vmd_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_vmd_file(this)) {
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

void *demux_vmd_init_plugin (xine_t *xine, void *data) {
  demux_vmd_class_t     *this;

  this = calloc(1, sizeof(demux_vmd_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Sierra VMD file demux plugin");
  this->demux_class.identifier      = "VMD";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "vmd";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
