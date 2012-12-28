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
 * 4X Technologies (.4xm) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the 4xm file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_4xm"
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

#define FOURCC_TAG LE_FOURCC
#define  RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define _4XMV_TAG FOURCC_TAG('4', 'X', 'M', 'V')
#define  LIST_TAG FOURCC_TAG('L', 'I', 'S', 'T')
#define  HEAD_TAG FOURCC_TAG('H', 'E', 'A', 'D')
#define  TRK__TAG FOURCC_TAG('T', 'R', 'K', '_')
#define  MOVI_TAG FOURCC_TAG('M', 'O', 'V', 'I')
#define  VTRK_TAG FOURCC_TAG('V', 'T', 'R', 'K')
#define  STRK_TAG FOURCC_TAG('S', 'T', 'R', 'K')
#define  std__TAG FOURCC_TAG('s', 't', 'd', '_')
#define  name_TAG FOURCC_TAG('n', 'a', 'm', 'e')
#define  vtrk_TAG FOURCC_TAG('v', 't', 'r', 'k')
#define  strk_TAG FOURCC_TAG('s', 't', 'r', 'k')
#define  ifrm_TAG FOURCC_TAG('i', 'f', 'r', 'm')
#define  pfrm_TAG FOURCC_TAG('p', 'f', 'r', 'm')
#define  cfrm_TAG FOURCC_TAG('c', 'f', 'r', 'm')
#define  snd__TAG FOURCC_TAG('s', 'n', 'd', '_')

#define vtrk_SIZE 0x44
#define strk_SIZE 0x28

typedef struct AudioTrack {
  unsigned int audio_type;
  int sample_rate;
  int bits;
  int channels;
} audio_track_t;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned int         filesize;

  xine_bmiheader       bih;

  unsigned int         track_count;
  audio_track_t       *tracks;

  int64_t              video_pts;
  int64_t              video_pts_inc;
  int64_t              duration_in_ms;

} demux_fourxm_t;

typedef struct {
  demux_class_t     demux_class;
} demux_fourxm_class_t;

static float get_le_float(unsigned char *buffer)
{
  float f;
  unsigned char *float_buffer = (unsigned char *)&f;

#ifdef WORDS_BIGENDIAN
  float_buffer[0] = buffer[3];
  float_buffer[1] = buffer[2];
  float_buffer[2] = buffer[1];
  float_buffer[3] = buffer[0];
#else
  float_buffer[0] = buffer[0];
  float_buffer[1] = buffer[1];
  float_buffer[2] = buffer[2];
  float_buffer[3] = buffer[3];
#endif

  return f;
}

/* Open a 4xm file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if 4xm file was opened successfully. */
static int open_fourxm_file(demux_fourxm_t *fourxm) {
  unsigned char preview[12];

  /* the file signature will be in the first 12 bytes */
  if (_x_demux_read_header(fourxm->input, preview, 12) != 12)
    return 0;

  /* check for the signature tags */
  if (!_x_is_fourcc(&preview[0], "RIFF") ||
      !_x_is_fourcc(&preview[8], "4XMV"))
    return 0;

  /* file is qualified; skip over the header bytes in the stream */
  fourxm->input->seek(fourxm->input, 12, SEEK_SET);

  /* fetch the LIST-HEAD header */
  if (fourxm->input->read(fourxm->input, preview, 12) != 12)
    return 0;
  if (!_x_is_fourcc(&preview[0], "LIST") ||
      !_x_is_fourcc(&preview[8], "HEAD") )
    return 0;

  /* read the whole header */
  const uint32_t header_size = _X_LE_32(&preview[4]) - 4;
  uint8_t *const header = malloc(header_size);
  if (!header || fourxm->input->read(fourxm->input, header, header_size) != header_size) {
    free(header);
    return 0;
  }

  fourxm->bih.biWidth = 0;
  fourxm->bih.biHeight = 0;
  fourxm->track_count = 0;
  fourxm->tracks = NULL;
  fourxm->video_pts_inc = 0;

  /* take the lazy approach and search for any and all vtrk and strk chunks */
  int i;
  for (i = 0; i < header_size - 8; i++) {
    const uint32_t fourcc_tag = _X_LE_32(&header[i]);
    const uint32_t size = _X_LE_32(&header[i + 4]);

    if (fourcc_tag == std__TAG) {
      const float fps = get_le_float(&header[i + 12]);
      fourxm->video_pts_inc = (int64_t)(90000.0 / fps);
    } else if (fourcc_tag == vtrk_TAG) {
      /* check that there is enough data */
      if (size != vtrk_SIZE) {
        free(header);
        return 0;
      }
      const uint32_t total_frames = _X_LE_32(&header[i + 24]);
      fourxm->duration_in_ms = total_frames;
      fourxm->duration_in_ms *= fourxm->video_pts_inc;
      fourxm->duration_in_ms /= 90000;
      fourxm->duration_in_ms *= 1000;
      fourxm->bih.biWidth = _X_LE_32(&header[i + 36]);
      fourxm->bih.biHeight = _X_LE_32(&header[i + 40]);
      i += 8 + size;
    } else if (fourcc_tag == strk_TAG) {
      /* check that there is enough data */
      if (size != strk_SIZE) {
        free(header);
        return 0;
      }
      const uint32_t current_track = _X_LE_32(&header[i + 8]);
      if (current_track >= fourxm->track_count) {
        fourxm->track_count = current_track + 1;
        if (!fourxm->track_count || fourxm->track_count >= UINT_MAX / sizeof(audio_track_t)) {
          free(header);
          return 0;
        }
        fourxm->tracks = realloc(fourxm->tracks,
          fourxm->track_count * sizeof(audio_track_t));
        if (!fourxm->tracks) {
          free(header);
          return 0;
        }
      }

      fourxm->tracks[current_track].channels = _X_LE_32(&header[i + 36]);
      fourxm->tracks[current_track].sample_rate = _X_LE_32(&header[i + 40]);
      fourxm->tracks[current_track].bits = _X_LE_32(&header[i + 44]);
      const uint32_t audio_type = _X_LE_32(&header[i + 12]);
      if (audio_type == 0)
          fourxm->tracks[current_track].audio_type = BUF_AUDIO_LPCM_LE;
      else if (audio_type == 1)
          fourxm->tracks[current_track].audio_type = BUF_AUDIO_4X_ADPCM;
      fourxm->tracks[current_track].audio_type += (current_track & 0x0000FFFF);
      i += 8 + size;
    }
  }

  fourxm->filesize = fourxm->input->get_length(fourxm->input);

  /* this will get bumped to 0 on the first iteration */
  fourxm->video_pts = -fourxm->video_pts_inc;

  /* skip the data body LIST header */
  fourxm->input->seek(fourxm->input, 12, SEEK_CUR);

  free(header);

  return 1;
}

static int demux_fourxm_send_chunk(demux_plugin_t *this_gen) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int remaining_bytes;
  unsigned int current_track;

  /* read the next header */
  uint8_t header[8];
  if (this->input->read(this->input, header, 8) != 8) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  const uint32_t fourcc_tag = _X_LE_32(&header[0]);
  const uint32_t size = _X_LE_32(&header[4]);

  switch (fourcc_tag) {

  case ifrm_TAG:
  case pfrm_TAG:
  case cfrm_TAG:
    /* send the 8-byte chunk header first */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_4XM;
    if( this->filesize )
      buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                       65535 / this->filesize );
    buf->extra_info->input_time = this->video_pts / 90;
    buf->pts = this->video_pts;
    buf->size = 8;
    memcpy(buf->content, header, 8);
    if (fourcc_tag == ifrm_TAG)
      buf->decoder_flags |= BUF_FLAG_KEYFRAME;
    this->video_fifo->put(this->video_fifo, buf);

    remaining_bytes = size;
    while (remaining_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_4XM;
      if( this->filesize )
        buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->filesize );
      buf->extra_info->input_time = this->video_pts / 90;
      buf->pts = this->video_pts;

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      if (fourcc_tag == ifrm_TAG)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;

      this->video_fifo->put(this->video_fifo, buf);
    }

    break;

  case snd__TAG:
    /* fetch the track number and audio chunk size */
    if (this->input->read(this->input, header, 8) != 8) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    current_track = _X_LE_32(&header[0]);
//    size = _X_LE_32(&header[4]);

    if (current_track >= this->track_count) {
      lprintf ("bad audio track number (%d >= %d)\n",
               current_track, this->track_count);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    remaining_bytes = size - 8;
    while (remaining_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->tracks[current_track].audio_type;
      if( this->filesize )
        buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->filesize );
      /* let the engine sort it out */
      buf->extra_info->input_time = 0;
      buf->pts = 0;

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
        this->audio_fifo->put(this->audio_fifo, buf);
      }
    break;

  case LIST_TAG:
    /* skip LIST header */
    this->input->seek(this->input, 4, SEEK_CUR);

    /* take this opportunity to bump the video pts */
    this->video_pts += this->video_pts_inc;
    break;

  default:
    lprintf("bad chunk: %c%c%c%c (%02X%02X%02X%02X)\n",
      header[0], header[1], header[2], header[3],
      header[0], header[1], header[2], header[3]);
    this->status = DEMUX_FINISHED;
    break;

  }

  return this->status;
}

static void demux_fourxm_send_headers(demux_plugin_t *this_gen) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
                       (this->track_count > 0) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
                       this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
                       this->bih.biHeight);
  if (this->track_count > 0) {
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                         this->tracks[0].channels);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                         this->tracks[0].sample_rate);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                         this->tracks[0].bits);
  }

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = this->video_pts_inc;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = BUF_VIDEO_4XM;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->track_count > 0) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->tracks[0].audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->tracks[0].sample_rate;
    buf->decoder_info[2] = this->tracks[0].bits;
    buf->decoder_info[3] = this->tracks[0].channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_fourxm_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time, int playing) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_fourxm_get_status (demux_plugin_t *this_gen) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  return this->status;
}

static int demux_fourxm_get_stream_length (demux_plugin_t *this_gen) {

  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  return this->duration_in_ms;
}

static uint32_t demux_fourxm_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_fourxm_get_optional_data(demux_plugin_t *this_gen,
                                        void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_fourxm_t    *this;

  this         = calloc(1, sizeof(demux_fourxm_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_fourxm_send_headers;
  this->demux_plugin.send_chunk        = demux_fourxm_send_chunk;
  this->demux_plugin.seek              = demux_fourxm_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_fourxm_get_status;
  this->demux_plugin.get_stream_length = demux_fourxm_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_fourxm_get_capabilities;
  this->demux_plugin.get_optional_data = demux_fourxm_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_fourxm_file(this)) {
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

void *demux_fourxm_init_plugin (xine_t *xine, void *data) {
  demux_fourxm_class_t     *this;

  this = calloc(1, sizeof(demux_fourxm_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("4X Technologies (4xm) demux plugin");
  this->demux_class.identifier      = "4X Technologies";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "4xm";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
