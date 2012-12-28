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
 * CIN File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Id CIN file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * CIN is a somewhat quirky and ill-defined format. Here are some notes
 * for anyone trying to understand the technical details of this format:
 *
 * The format has no definite file signature. This is problematic for a
 * general-purpose media player that wants to automatically detect file
 * types. However, a CIN file does start with 5 32-bit numbers that
 * specify audio and video parameters. This demuxer gets around the lack
 * of file signature by performing sanity checks on those parameters.
 * Probabalistically, this is a reasonable solution since the number of
 * valid combinations of the 5 parameters is a very small subset of the
 * total 160-bit number space.
 *
 * Refer to the function demux_idcin_open() for the precise A/V parameters
 * that this demuxer allows.
 *
 * Next, each audio and video frame has a duration of 1/14 sec. If the
 * audio sample rate is a multiple of the common frequency 22050 Hz it will
 * divide evenly by 14. However, if the sample rate is 11025 Hz:
 *   11025 (samples/sec) / 14 (frames/sec) = 787.5 (samples/frame)
 * The way the CIN stores audio in this case is by storing 787 sample
 * frames in the first audio frame and 788 sample frames in the second
 * audio frame. Therefore, the total number of bytes in an audio frame
 * is given as:
 *   audio frame #0: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #1: 788 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #2: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #3: 788 * (bytes/sample) * (# channels) bytes in frame
 *
 * Finally, not all Id CIN creation tools agree on the resolution of the
 * color palette, apparently. Some creation tools specify red, green, and
 * blue palette components in terms of 6-bit VGA color DAC values which
 * range from 0..63. Other tools specify the RGB components as full 8-bit
 * values that range from 0..255. Since there are no markers in the file to
 * differentiate between the two variants, this demuxer uses the following
 * heuristic:
 *   - load the 768 palette bytes from disk
 *   - assume that they will need to be shifted left by 2 bits to
 *     transform them from 6-bit values to 8-bit values
 *   - scan through all 768 palette bytes
 *     - if any bytes exceed 63, do not shift the bytes at all before
 *       transmitting them to the video decoder
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_idcin"
#define LOG_VERBOSE

/* define LOG to output information about the A/V chunks that the
 * demuxer is dispatching to the engine */
/* #define LOG */

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_games.h"

#define IDCIN_HEADER_SIZE 20
#define HUFFMAN_TABLE_SIZE 65536
#define IDCIN_FRAME_PTS_INC  (90000 / 14)
#define PALETTE_SIZE 256

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                filesize;
  xine_bmiheader       bih;
  unsigned char        huffman_table[HUFFMAN_TABLE_SIZE];
  xine_waveformatex    wave;

  int                  audio_chunk_size1;
  int                  audio_chunk_size2;
  int                  current_audio_chunk;

  uint64_t             pts_counter;
} demux_idcin_t;

typedef struct {
  demux_class_t     demux_class;
} demux_idcin_class_t;

static int demux_idcin_send_chunk(demux_plugin_t *this_gen) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int command;
  unsigned char preamble[8];
  unsigned char disk_palette[PALETTE_SIZE * 3];
  palette_entry_t palette[PALETTE_SIZE];
  int i;
  int remaining_sample_bytes;
  int scale_bits;

  /* figure out what the next data is */
  if (this->input->read(this->input, (unsigned char *)&command, 4) != 4) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  command = le2me_32(command);
  lprintf("command %d: ", command);
  if (command == 2) {
    lprintf("demux finished\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    if (command == 1) {
      lprintf("load palette\n");

      /* load a 768-byte palette and pass it to the demuxer */
      if (this->input->read(this->input, disk_palette, PALETTE_SIZE * 3) !=
        PALETTE_SIZE * 3) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      /* scan the palette to figure out if it's 6- or 8-bit;
       * assume 6-bit palette until a value > 63 is seen */
      scale_bits = 2;
      for (i = 0; i < PALETTE_SIZE * 3; i++)
        if (disk_palette[i] > 63) {
          scale_bits = 0;
          break;
        }

      /* convert palette to internal structure */
      for (i = 0; i < PALETTE_SIZE; i++) {
        /* these are VGA color DAC values, which means they only range
         * from 0..63; adjust as appropriate */
        palette[i].r = disk_palette[i * 3 + 0] << scale_bits;
        palette[i].g = disk_palette[i * 3 + 1] << scale_bits;
        palette[i].b = disk_palette[i * 3 + 2] << scale_bits;
      }

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = PALETTE_SIZE;
      buf->decoder_info_ptr[2] = &palette;
      buf->size = 0;
      buf->type = BUF_VIDEO_IDCIN;
      this->video_fifo->put (this->video_fifo, buf);
    } else
      lprintf("load video and audio\n");
  }

  /* load the video frame */
  if (this->input->read(this->input, preamble, 8) != 8) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  remaining_sample_bytes = _X_LE_32(&preamble[0]) - 4;

  lprintf("dispatching %d video bytes\n", remaining_sample_bytes);
  while (remaining_sample_bytes) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_IDCIN;
    if( this->filesize )
      buf->extra_info->input_normpos = (int)( (double) (this->input->get_current_pos (this->input) -
                                                        IDCIN_HEADER_SIZE - HUFFMAN_TABLE_SIZE) *
                                     65535 / this->filesize );
    buf->extra_info->input_time = this->pts_counter / 90;
    buf->pts = this->pts_counter;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    /* all frames are intra-coded */
    buf->decoder_flags |= BUF_FLAG_KEYFRAME;
    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    lprintf("sending video buf with %d bytes, %"PRId64" pts\n", buf->size, buf->pts);
    this->video_fifo->put(this->video_fifo, buf);
  }

  /* load the audio frame */
  if (this->audio_fifo && this->wave.nSamplesPerSec) {

    if (this->current_audio_chunk == 1) {
      remaining_sample_bytes = this->audio_chunk_size1;
      this->current_audio_chunk = 2;
    } else {
      remaining_sample_bytes = this->audio_chunk_size2;
      this->current_audio_chunk = 1;
    }

    lprintf("dispatching %d audio bytes\n", remaining_sample_bytes);
    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_LE;
      if( this->filesize )
        buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                         65535 / this->filesize );
      buf->extra_info->input_time = this->pts_counter / 90;
      buf->pts = this->pts_counter;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      lprintf("sending audio buf with %d bytes, %"PRId64" pts\n", buf->size, buf->pts);
      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }
  this->pts_counter += IDCIN_FRAME_PTS_INC;

  return this->status;
}

/* returns 1 if the CIN file was opened successfully, 0 otherwise */
static int open_idcin_file(demux_idcin_t *this) {
  unsigned char header[IDCIN_HEADER_SIZE];
  xine_bmiheader *bih = &this->bih;

  if (_x_demux_read_header(this->input, header, IDCIN_HEADER_SIZE) != IDCIN_HEADER_SIZE)
    return 0;

  /*
   * This is what you could call a "probabilistic" file check: Id CIN
   * files don't have a definite file signature. In lieu of such a marker,
   * perform sanity checks on the 5 header fields:
   *  width, height: greater than 0, less than or equal to 1024
   * audio sample rate: greater than or equal to 8000, less than or
   *  equal to 48000, or 0 for no audio
   * audio sample width (bytes/sample): 0 for no audio, or 1 or 2
   * audio channels: 0 for no audio, or 1 or 2
   */

  /* check the width */
  bih->biWidth = _X_LE_32(&header[0]);
  if ((bih->biWidth == 0) || (bih->biWidth > 1024))
    return 0;

  /* check the height */
  bih->biHeight = _X_LE_32(&header[4]);
  if ((bih->biHeight == 0) || (bih->biHeight > 1024))
    return 0;

  /* check the audio sample rate */
  this->wave.nSamplesPerSec = _X_LE_32(&header[8]);
  if ((this->wave.nSamplesPerSec != 0) &&
     ((this->wave.nSamplesPerSec < 8000) || (this->wave.nSamplesPerSec > 48000)))
    return 0;

  /* check the audio bytes/sample */
  this->wave.wBitsPerSample = _X_LE_32(&header[12]) * 8;
  if (this->wave.wBitsPerSample > 16)
    return 0;

  /* check the audio channels */
  this->wave.nChannels = _X_LE_32(&header[16]);
  if (this->wave.nChannels > 2)
    return 0;

  /* if execution got this far, qualify it as a valid Id CIN file
   * and continue loading */
  lprintf("%dx%d video, %d Hz, %d channels, %d bit PCM audio\n",
    bih->biWidth, bih->biHeight,
    this->wave.nSamplesPerSec,
    this->wave.nChannels,
    this->wave.wBitsPerSample);

  /* file is qualified; skip over the signature bytes in the stream */
  this->input->seek(this->input, IDCIN_HEADER_SIZE, SEEK_SET);

  /* read the Huffman table */
  if (this->input->read(this->input, this->huffman_table, HUFFMAN_TABLE_SIZE) !=
    HUFFMAN_TABLE_SIZE)
    return 0;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
    (this->wave.nChannels) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
    bih->biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
    bih->biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
    this->wave.nChannels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
    this->wave.nSamplesPerSec);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
    this->wave.wBitsPerSample);

  this->filesize = this->input->get_length(this->input) -
    IDCIN_HEADER_SIZE - HUFFMAN_TABLE_SIZE;

  return 1;
}

static void demux_idcin_send_headers(demux_plugin_t *this_gen) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  buf_element_t *buf;
  uint32_t i;
  int size;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  this->bih.biSize = sizeof(xine_bmiheader) + HUFFMAN_TABLE_SIZE;
  size = this->bih.biSize;

  i = 0;
  do {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_info[0] = IDCIN_FRAME_PTS_INC;  /* initial video_step */
    if (size > buf->max_size) {
      buf->size = buf->max_size;
      buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|
                           BUF_FLAG_FRAMERATE;
    } else {
      buf->size = size;
      buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|
                           BUF_FLAG_FRAMERATE|BUF_FLAG_FRAME_END;
    }

    if (i == 0) {
      memcpy(buf->content, &this->bih, sizeof(xine_bmiheader));
      memcpy(buf->content + sizeof(xine_bmiheader), this->huffman_table, buf->size - sizeof(xine_bmiheader));
    } else {
      memcpy(buf->content, this->huffman_table + i - sizeof(xine_bmiheader), buf->size);
    }

    buf->type = BUF_VIDEO_IDCIN;
    this->video_fifo->put (this->video_fifo, buf);

    size -= buf->size;
    i += buf->size;
  } while (size);

  if (this->audio_fifo && this->wave.nChannels) {

    /* initialize the chunk sizes */
    if (this->wave.nSamplesPerSec % 14 != 0) {
      this->audio_chunk_size1 = (this->wave.nSamplesPerSec / 14) *
        this->wave.wBitsPerSample / 8 * this->wave.nChannels;
      this->audio_chunk_size2 = (this->wave.nSamplesPerSec / 14 + 1) *
        this->wave.wBitsPerSample / 8 * this->wave.nChannels;
    } else {
      this->audio_chunk_size1 = this->audio_chunk_size2 =
        (this->wave.nSamplesPerSec / 14) * this->wave.wBitsPerSample / 8 *
        this->wave.nChannels;
    }
    lprintf("audio_chunk_size[1,2] = %d, %d\n",
            this->audio_chunk_size1, this->audio_chunk_size2);

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_LE;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave.nSamplesPerSec;
    buf->decoder_info[2] = this->wave.wBitsPerSample;
    buf->decoder_info[3] = this->wave.nChannels;
    buf->size = sizeof(this->wave);
    memcpy(buf->content, &this->wave, buf->size);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_idcin_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* reposition stream past the Huffman tables */
    this->input->seek(this->input, IDCIN_HEADER_SIZE + HUFFMAN_TABLE_SIZE,
      SEEK_SET);

    this->pts_counter = 0;
    this->current_audio_chunk = 1;
  }

  return this->status;
}

static int demux_idcin_get_status (demux_plugin_t *this_gen) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  return this->status;
}

static int demux_idcin_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_idcin_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_idcin_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_idcin_t  *this;

  this         = calloc(1, sizeof(demux_idcin_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_idcin_send_headers;
  this->demux_plugin.send_chunk        = demux_idcin_send_chunk;
  this->demux_plugin.seek              = demux_idcin_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_idcin_get_status;
  this->demux_plugin.get_stream_length = demux_idcin_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_idcin_get_capabilities;
  this->demux_plugin.get_optional_data = demux_idcin_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_idcin_file(this)) {
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

void *demux_idcin_init_plugin (xine_t *xine, void *data) {
  demux_idcin_class_t     *this;

  this         = calloc(1, sizeof(demux_idcin_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Id Quake II Cinematic file demux plugin");
  this->demux_class.identifier      = "Id CIN";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "cin";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
