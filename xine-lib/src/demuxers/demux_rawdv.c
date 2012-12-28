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
 * demultiplexer for raw dv streams
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>

#define NTSC_FRAME_SIZE 120000
#define NTSC_FRAME_RATE 29.97
#define PAL_FRAME_SIZE  144000
#define PAL_FRAME_RATE 25

typedef struct {
  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  frame_size;
  int                  bytes_left;

  uint32_t             cur_frame;
  uint32_t             duration;
  uint64_t             pts;
} demux_raw_dv_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_raw_dv_class_t;


static int demux_raw_dv_next (demux_raw_dv_t *this) {
  buf_element_t *buf, *abuf;
  int n;

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;

  if( this->bytes_left <= buf->max_size ) {
    buf->size = this->bytes_left;
    buf->decoder_flags |= BUF_FLAG_FRAME_END;
  } else {
    buf->size = buf->max_size;
  }
  this->bytes_left -= buf->size;

  n = this->input->read (this->input, buf->content, buf->size);

  if (n != buf->size) {
    buf->free_buffer(buf);
    return 0;
  }

  /* TODO: duplicate data and send to audio fifo.
   * however we don't have dvaudio decoder yet.
   */

  buf->pts                    = this->pts;
  buf->extra_info->input_time = this->pts/90;
  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );
  buf->extra_info->frame_number  = this->cur_frame;
  buf->type                   = BUF_VIDEO_DV;

  this->video_fifo->put(this->video_fifo, buf);

  if (this->audio_fifo) {
    abuf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    abuf->content = abuf->mem;
    memcpy( abuf->content, buf->content, buf->size );
    abuf->type   = BUF_AUDIO_DV;
    abuf->pts    = buf->pts;
    abuf->size   = buf->size;
    abuf->decoder_flags = buf->decoder_flags;
    abuf->extra_info->input_time = buf->extra_info->input_time;
    abuf->extra_info->input_normpos = buf->extra_info->input_normpos;
    this->audio_fifo->put (this->audio_fifo, abuf);
  }
  if (!this->bytes_left) {
    this->bytes_left = this->frame_size;
    this->pts += this->duration;
    this->cur_frame++;
  }

  return 1;
}

static int demux_raw_dv_send_chunk (demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  if (!demux_raw_dv_next(this))
    this->status = DEMUX_FINISHED;
  return this->status;
}

static int demux_raw_dv_get_status (demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  return this->status;
}


static void demux_raw_dv_send_headers (demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  buf_element_t *buf, *abuf;
  xine_bmiheader *bih;
  unsigned char *scratch, scratch2[4];
  int i, j;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  _x_demux_control_start(this->stream);

  scratch = (unsigned char *) malloc(NTSC_FRAME_SIZE);
  if (scratch == NULL )
    return;

  if (INPUT_IS_SEEKABLE(this->input)) {
    this->input->seek(this->input, 0, SEEK_SET);
    if( this->input->read (this->input, scratch, NTSC_FRAME_SIZE) != NTSC_FRAME_SIZE )
      return;
    this->input->seek(this->input, 0, SEEK_SET);
  }
  else {
    if( this->input->read (this->input, scratch, NTSC_FRAME_SIZE) != NTSC_FRAME_SIZE )
      return;
    if( !(scratch[3] & 0x80) )
      i = NTSC_FRAME_SIZE;
    else
      i = PAL_FRAME_SIZE;

    i -= NTSC_FRAME_SIZE;
    while (i > 0) {
      if( this->input->read (this->input, scratch2, 4) != 4 )
        return;
      i -= 4;
    }
  }

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;
  buf->type = BUF_VIDEO_DV;
  buf->decoder_flags |= BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                        BUF_FLAG_FRAME_END;

  bih = (xine_bmiheader *)buf->content;

  if( !(scratch[3] & 0x80) ) {
    /* NTSC */
    this->frame_size = NTSC_FRAME_SIZE;
    this->duration = buf->decoder_info[0] = 3003;
    bih->biWidth = 720;
    bih->biHeight = 480;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_BITRATE,
                         NTSC_FRAME_SIZE * NTSC_FRAME_RATE * 8);
  } else {
    /* PAL */
    this->frame_size = PAL_FRAME_SIZE;
    this->duration = buf->decoder_info[0] = 3600;
    bih->biWidth = 720;
    bih->biHeight = 576;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_BITRATE,
                         PAL_FRAME_SIZE * PAL_FRAME_RATE * 8);
  }

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
                     bih->biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
                     bih->biHeight);

  bih->biSize = sizeof(xine_bmiheader);
  bih->biPlanes = 1;
  bih->biBitCount = 24;
  memcpy(&bih->biCompression,"dvsd",4);
  bih->biSizeImage = bih->biWidth*bih->biHeight;

  this->video_fifo->put(this->video_fifo, buf);

  this->pts = 0;
  this->cur_frame = 0;
  this->bytes_left = this->frame_size;

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);

  if (this->audio_fifo) {
    int done = 0;
    abuf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    abuf->content = abuf->mem;

    /* This code GPL from Arne Schirmacher (dvgrab/Kino) */
    /* 10 DIF sequences per NTSC frame */
    for (i = 0; i < 10 && done == 0; ++i) {
      /* 9 audio DIF blocks per sequence */
      for (j = 0; j < 9 && done == 0; ++j) {
        /* calculate address: 150 DIF blocks per sequence, 80 bytes
        per DIF block, audio blocks start at every 16th beginning
        with block 6, block has 3 bytes header, followed by one
        packet. */
        const unsigned char *s = &scratch[i * 150 * 80 + 6 * 80 + j * 16 * 80 + 3];
        /* Pack id 0x50 contains audio metadata */
        if (s[0] == 0x50) {
          /* printf("aaux %d: %2.2x %2.2x %2.2x %2.2x %2.2x\n",
           j, s[0], s[1], s[2], s[3], s[4]);
          */
          int smp, flag;

          done = 1;

          smp = (s[4] >> 3) & 0x07;
          flag = s[3] & 0x20;

          if (flag == 0) {
            switch (smp) {
              case 0:
                abuf->decoder_info[1] = 48000;
                break;
              case 1:
                abuf->decoder_info[1] = 44100;
                break;
              case 2:
                abuf->decoder_info[1] = 32000;
                break;
            }
          } else {
            switch (smp) {
              case 0:
                abuf->decoder_info[1] = 48000;
                break;
              case 1:
                abuf->decoder_info[1] = 44100;
                break;
              case 2:
                abuf->decoder_info[1] = 32000;
                break;
            }
          }
        }
      }
    }
    abuf->type   = BUF_AUDIO_DV;
    abuf->size   = buf->size;
    abuf->decoder_flags = buf->decoder_flags;
    abuf->decoder_info[0] = 0; /* first package, containing wavex */
    abuf->decoder_info[2] = 16; /* Audio bits (ffmpeg upsamples 12 to 16bit) */
    abuf->decoder_info[3] = 2; /* Audio bits (ffmpeg only supports 2 channels) */
    this->audio_fifo->put (this->audio_fifo, abuf);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  }

}

static int demux_raw_dv_seek (demux_plugin_t *this_gen,
				  off_t start_pos, int start_time, int playing) {

  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  if (!INPUT_IS_SEEKABLE(this->input)) {
    this->status = DEMUX_OK;
    return this->status;
  }

  if( !start_pos && start_time ) {
    /* Upcast start_time in case sizeof(off_t) > sizeof(int) */
    start_pos = ((off_t) start_time * 90 / this->duration) * this->frame_size;
  }

  start_pos = start_pos - (start_pos % this->frame_size);
  this->input->seek(this->input, start_pos, SEEK_SET);

  this->cur_frame = start_pos / this->frame_size;
  this->pts = this->cur_frame * this->duration;
  this->bytes_left = this->frame_size;

  _x_demux_flush_engine (this->stream);

  _x_demux_control_newpts (this->stream, this->pts, BUF_FLAG_SEEK);

  this->status = DEMUX_OK;
  return this->status;
}

static int demux_raw_dv_get_stream_length(demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  return (int)((int64_t) this->duration * this->input->get_length (this->input) /
		  (this->frame_size * 90));
}

static uint32_t demux_raw_dv_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_raw_dv_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_raw_dv_t *this;

  this         = calloc(1, sizeof(demux_raw_dv_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_raw_dv_send_headers;
  this->demux_plugin.send_chunk        = demux_raw_dv_send_chunk;
  this->demux_plugin.seek              = demux_raw_dv_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_raw_dv_get_status;
  this->demux_plugin.get_stream_length = demux_raw_dv_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_raw_dv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_raw_dv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[8];

    if (_x_demux_read_header(input, buf, 8) != 8) {
      free (this);
      return NULL;
    }

    /* DIF (DV) movie file */
    if (memcmp(buf, "\x1F\x07\x00", 3) != 0 || !(buf[4] ^ 0x01)) {
      free (this);
      return NULL;
    }
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    free (this);
    return NULL;
  }

  if (!INPUT_IS_SEEKABLE(this->input)) {
    /* "live" DV streams require more prebuffering */
    this->stream->metronom->set_option(this->stream->metronom, METRONOM_PREBUFFER, 90000);
  }

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_raw_dv_class_t     *this;

  this = calloc(1, sizeof(demux_raw_dv_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Raw DV Video stream");
  this->demux_class.identifier      = "raw_dv";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "dv dif";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_raw_dv = {
  1                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "rawdv", XINE_VERSION_CODE, &demux_info_raw_dv, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
