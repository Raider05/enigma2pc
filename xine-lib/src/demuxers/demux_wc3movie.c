/*
 * Copyright (C) 2000-2012 the xine project
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
 * File Demuxer for Wing Commander III MVE movie files
 *   by Mike Melanson (melanson@pcisys.net)
 * For more information on the MVE file format, visit:
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

/********** logging **********/
#define LOG_MODULE "demux_wc3movie"
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

#define FOURCC_TAG BE_FOURCC
#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define MOVE_TAG FOURCC_TAG('M', 'O', 'V', 'E')
#define PC_TAG   FOURCC_TAG('_', 'P', 'C', '_')
#define SOND_TAG FOURCC_TAG('S', 'O', 'N', 'D')
#define PALT_TAG FOURCC_TAG('P', 'A', 'L', 'T')
#define INDX_TAG FOURCC_TAG('I', 'N', 'D', 'X')
#define BNAM_TAG FOURCC_TAG('B', 'N', 'A', 'M')
#define SIZE_TAG FOURCC_TAG('S', 'I', 'Z', 'E')
#define BRCH_TAG FOURCC_TAG('B', 'R', 'C', 'H')
#define SHOT_TAG FOURCC_TAG('S', 'H', 'O', 'T')
#define VGA_TAG  FOURCC_TAG('V', 'G', 'A', ' ')
#define AUDI_TAG FOURCC_TAG('A', 'U', 'D', 'I')
#define TEXT_TAG FOURCC_TAG('T', 'E', 'X', 'T')

#define PALETTE_SIZE 256
#define PALETTE_CHUNK_SIZE (PALETTE_SIZE * 3)
#define WC3_FRAMERATE 15
#define WC3_PTS_INC (90000 / 15)
#define WC3_USUAL_WIDTH 320
#define WC3_USUAL_HEIGHT 165
#define WC3_HEADER_SIZE 16
#define PREAMBLE_SIZE 8

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  xine_bmiheader       bih;
  xine_waveformatex    wave;

  palette_entry_t     *palettes;
  unsigned int         number_of_shots;
  unsigned int         current_shot;
  off_t               *shot_offsets;
  int                  seek_flag;   /* this is set when a seek occurs */

  off_t                data_start;
  off_t                data_size;

  int64_t              video_pts;
} demux_mve_t;

typedef struct {
  demux_class_t     demux_class;
} demux_mve_class_t;

/* bizarre palette lookup table */
static const unsigned char wc3_pal_lookup[] = {
0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0E, 0x10, 0x12, 0x13, 0x15, 0x16,
0x18, 0x19, 0x1A,
0x1C, 0x1D, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2A, 0x2C,
0x2D, 0x2E, 0x2F,
0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3F,
0x40, 0x41, 0x42,
0x43, 0x44, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
0x51, 0x52, 0x53,
0x54, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
0x62, 0x63, 0x64,
0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
0x72, 0x73, 0x74,
0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7D, 0x7E, 0x7F, 0x80,
0x81, 0x82, 0x83,
0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8D, 0x8E, 0x8F,
0x90, 0x91, 0x92,
0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E,
0x9F, 0xA0, 0xA1,
0xA2, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAA, 0xAB, 0xAC,
0xAD, 0xAE, 0xAF,
0xB0, 0xB1, 0xB2, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xB9, 0xBA,
0xBB, 0xBC, 0xBD,
0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC5, 0xC6, 0xC7, 0xC8,
0xC9, 0xCA, 0xCB,
0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD5,
0xD6, 0xD7, 0xD8,
0xD9, 0xDA, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
0xE4, 0xE4, 0xE5,
0xE6, 0xE7, 0xE8, 0xE9, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xED, 0xEE, 0xEF, 0xF0,
0xF1, 0xF1, 0xF2,
0xF3, 0xF4, 0xF5, 0xF6, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFC, 0xFD,
0xFD, 0xFD, 0xFD
};

static int demux_mve_send_chunk(demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  buf_element_t *buf = NULL;
  int64_t audio_pts = 0;
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_tag;
  unsigned int chunk_size;
  off_t current_file_pos;
  unsigned int palette_number;

  /* compensate for the initial data in the file */
  current_file_pos = this->input->get_current_pos(this->input) -
    this->data_start;

  if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
    PREAMBLE_SIZE)
    this->status = DEMUX_FINISHED;
  else {
    chunk_tag = _X_BE_32(&preamble[0]);
    /* round up to the nearest even size */
    chunk_size = (_X_BE_32(&preamble[4]) + 1) & (~1);

    if (chunk_tag == BRCH_TAG) {
      /* empty chunk; do nothing */
    } else if (chunk_tag == SHOT_TAG) {
      if (this->seek_flag) {
        /* reset pts */
        this->video_pts = 0;
        _x_demux_control_newpts(this->stream, 0, BUF_FLAG_SEEK);
        this->seek_flag = 0;
      } else {
        /* record the offset of the SHOT chunk */
        if (this->current_shot < this->number_of_shots) {
	  this->shot_offsets[this->current_shot] =
            this->input->get_current_pos(this->input) - PREAMBLE_SIZE;
        }
      }
      this->current_shot++;

      /* this is the start of a new shot; send a new palette */
      if (this->input->read(this->input, preamble, 4) != 4) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }
      palette_number = _X_LE_32(&preamble[0]);

      if (palette_number >= this->number_of_shots) {
        xine_log(this->stream->xine, XINE_LOG_MSG,
		 _("demux_wc3movie: SHOT chunk referenced invalid palette (%d >= %d)\n"),
          palette_number, this->number_of_shots);
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = PALETTE_SIZE;
      buf->decoder_info_ptr[2] = &this->palettes[PALETTE_SIZE * palette_number];
      buf->size = 0;
      buf->type = BUF_VIDEO_WC3;
      this->video_fifo->put (this->video_fifo, buf);

    } else if (chunk_tag == AUDI_TAG) {
      if( this->audio_fifo ) {
        audio_pts = this->video_pts - WC3_PTS_INC;

        while (chunk_size) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          buf->type = BUF_AUDIO_LPCM_LE;
          if( this->data_size )
            buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->data_size);
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
        }
      }else{
        this->input->seek(this->input, chunk_size, SEEK_CUR);
      }
    } else if (chunk_tag == VGA_TAG) {
      while (chunk_size) {
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->type = BUF_VIDEO_WC3;
        if( this->data_size )
          buf->extra_info->input_normpos = (int)( (double) current_file_pos * 65535 / this->data_size);
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
      this->video_pts += WC3_PTS_INC;
    } else if (chunk_tag == TEXT_TAG) {
      /*text_pts = this->video_pts - WC3_PTS_INC;*/

      /* unhandled thus far */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    } else {
      /* report an unknown chunk and skip it */
      lprintf("encountered unknown chunk: %c%c%c%c\n",
        (chunk_tag >> 24) & 0xFF,
        (chunk_tag >> 16) & 0xFF,
        (chunk_tag >>  8) & 0xFF,
        (chunk_tag >>  0) & 0xFF);
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  }

  return this->status;
}

static void demux_mve_send_headers(demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  /* this is not strictly correct-- some WC3 MVE files do not contain
   * audio, but I'm too lazy to check if that is the case */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
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
  buf->decoder_info[0] = WC3_PTS_INC;  /* initial video_step */
  buf->content = (void *)&this->bih;
  buf->size = sizeof(this->bih);
  buf->type = BUF_VIDEO_WC3;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo) {
    this->wave.wFormatTag = 1;
    this->wave.nChannels = 1;
    this->wave.nSamplesPerSec = 22050;
    this->wave.wBitsPerSample = 16;
    this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
    this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_LE;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave.nSamplesPerSec;
    buf->decoder_info[2] = this->wave.wBitsPerSample;
    buf->decoder_info[3] = this->wave.nChannels;
    buf->content = (void *)&this->wave;
    buf->size = sizeof(this->wave);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

/* returns 1 if the MVE file was opened successfully, 0 otherwise */
static int open_mve_file(demux_mve_t *this) {

  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_tag;
  unsigned int chunk_size;
  unsigned char disk_palette[PALETTE_CHUNK_SIZE];
  int i, j;
  unsigned char r, g, b;
  int temp;
  unsigned char header[WC3_HEADER_SIZE];
  void *title;

  if (_x_demux_read_header(this->input, header, WC3_HEADER_SIZE) != WC3_HEADER_SIZE)
    return 0;

  if ( !_x_is_fourcc(&header[0], "FORM") ||
       !_x_is_fourcc(&header[8], "MOVE") ||
       !_x_is_fourcc(&header[12], "_PC_") )
    return 0;

  /* file is qualified */

  this->bih.biSize = sizeof(xine_bmiheader);
  /* these are the frame dimensions unless others are found */
  this->bih.biWidth = WC3_USUAL_WIDTH;
  this->bih.biHeight = WC3_USUAL_HEIGHT;

  /* load the number of palettes, the only interesting piece of information
   * in the _PC_ chunk; take it for granted that it will always appear at
   * position 0x1C */
  this->input->seek(this->input, 0x1C, SEEK_SET);
  if (this->input->read(this->input, preamble, 4) != 4)
    return 0;
  this->number_of_shots = _X_LE_32(&preamble[0]);

  /* allocate space for the shot offset index and set offsets to 0 */
  this->shot_offsets = xine_xcalloc(this->number_of_shots, sizeof(off_t));
  this->current_shot = 0;

  /* skip the SOND chunk */
  this->input->seek(this->input, 12, SEEK_CUR);

  /* load the palette chunks */
  this->palettes = xine_xcalloc(this->number_of_shots, PALETTE_SIZE *
				sizeof(palette_entry_t));

  if (!this->shot_offsets || !this->palettes) {
    free (this->shot_offsets);
    return 0;
  }

  for (i = 0; i < this->number_of_shots; i++) {
    /* make sure there was a valid palette chunk preamble */
    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      free (this->palettes);
      free (this->shot_offsets);
      return 0;
    }

    if ( !_x_is_fourcc(&preamble[0], "PALT") ||
	 (_X_BE_32(&preamble[4]) != PALETTE_CHUNK_SIZE)) {
      xine_log(this->stream->xine, XINE_LOG_MSG,
	       _("demux_wc3movie: There was a problem while loading palette chunks\n"));
      free (this->palettes);
      free (this->shot_offsets);
      return 0;
    }

    /* load the palette chunk */
    if (this->input->read(this->input, disk_palette, PALETTE_CHUNK_SIZE) !=
      PALETTE_CHUNK_SIZE) {
      free (this->palettes);
      free (this->shot_offsets);
      return 0;
    }

    /* convert and store the palette */
    for (j = 0; j < PALETTE_SIZE; j++) {
      r = disk_palette[j * 3 + 0];
      g = disk_palette[j * 3 + 1];
      b = disk_palette[j * 3 + 2];
      /* rotate each component left by 2 */
      temp = r << 2; r = (temp & 0xff) | (temp >> 8);
      r = wc3_pal_lookup[r];
      temp = g << 2; g = (temp & 0xff) | (temp >> 8);
      g = wc3_pal_lookup[g];
      temp = b << 2; b = (temp & 0xff) | (temp >> 8);
      b = wc3_pal_lookup[b];
      this->palettes[i * 256 + j].r = r;
      this->palettes[i * 256 + j].g = g;
      this->palettes[i * 256 + j].b = b;
    }
  }

  /* after the palette chunks comes any number of chunks such as INDX,
   * BNAM, SIZE and perhaps others; traverse chunks until first BRCH
   * chunk is found */
  chunk_tag = 0;
  title = NULL;
  while (chunk_tag != BRCH_TAG) {

    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      free (title);
      free (this->palettes);
      free (this->shot_offsets);
      return 0;
    }

    chunk_tag = _X_BE_32(&preamble[0]);
    /* round up to the nearest even size */
    chunk_size = (_X_BE_32(&preamble[4]) + 1) & (~1);

    switch (chunk_tag) {

      case BRCH_TAG:
        /* time to start demuxing */
        break;

      case BNAM_TAG:
        /* load the name into the stream attributes */
        free (title);
        title = malloc (chunk_size);
        if (!title || this->input->read(this->input, title, chunk_size) != chunk_size) {
          free (title);
          free (this->palettes);
          free (this->shot_offsets);
          return 0;
        }
        break;

      case SIZE_TAG:
        /* override the default width and height */
        /* reuse the preamble bytes */
        if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
          PREAMBLE_SIZE) {
          free (title);
          free (this->palettes);
          free (this->shot_offsets);
          return 0;
        }
        this->bih.biWidth = _X_BE_32(&preamble[0]);
        this->bih.biHeight = _X_BE_32(&preamble[4]);
        break;

      case INDX_TAG:
        /* index is not useful for this demuxer */
        this->input->seek(this->input, chunk_size, SEEK_CUR);
        break;

      default:
        /* report an unknown chunk and skip it */
        lprintf("encountered unknown chunk: %c%c%c%c\n",
          (chunk_tag >> 24) & 0xFF,
          (chunk_tag >> 16) & 0xFF,
          (chunk_tag >>  8) & 0xFF,
          (chunk_tag >>  0) & 0xFF);
        this->input->seek(this->input, chunk_size, SEEK_CUR);
        break;
    }
  }

  /* note the data start offset */
  this->data_start = this->input->get_current_pos(this->input);
  this->data_size  = this->input->get_length(this->input) - this->data_start;
  this->video_pts  = 0;

  _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, title);

  return 1;
}

static int demux_mve_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  /*
   * MVE files are comprised of a series of SHOTs. A SHOT begins when the
   * camera angle changes. The first frame of a SHOT is effectively a
   * keyframe so it is safe to seek to the start of a SHOT. A/V sync is
   * not a concern since each video or audio chunk represents exactly
   * 1/15 sec.
   *
   * When a seek is requested, traverse the list of SHOT offsets and find
   * the best match. If not enough SHOT boundaries have been crossed while
   * demuxing the file, traverse the file until enough SHOTs are found.
   */

  demux_mve_t *this = (demux_mve_t *) this_gen;
  int i;
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_tag;
  unsigned int chunk_size;
  int new_shot = -1;

  start_time /= 1000;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  this->status = DEMUX_OK;
  _x_demux_flush_engine(this->stream);
  this->seek_flag = 1;

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* make sure the first shot has been recorded */
  if (this->shot_offsets[0] == 0) {

    while (1) {

      if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
        PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      chunk_tag = _X_BE_32(&preamble[0]);
      /* round up to the nearest even size */
      chunk_size = (_X_BE_32(&preamble[4]) + 1) & (~1);

      if (chunk_tag == SHOT_TAG) {
        this->shot_offsets[0] =
          this->input->get_current_pos(this->input) - PREAMBLE_SIZE;
        /* skip the four SHOT data bytes (palette index) */
        this->input->seek(this->input, 4, SEEK_CUR);
        break;  /* get out of the infinite while loop */
      } else {
        this->input->seek(this->input, chunk_size, SEEK_CUR);
      }
    }
  }

  /* compensate for data at start of file */
  start_pos += this->data_start;
  for (i = 0; i < this->number_of_shots - 1; i++) {

    /* if the next shot offset has not been recorded, traverse through the
     * file until it is found */
    if (this->shot_offsets[i + 1] == 0) {
      off_t current_pos;

      /* be sure to be just after the last known shot_offset */
      current_pos = this->input->get_current_pos(this->input);
      if (current_pos < this->shot_offsets[i]) {
	this->input->seek(this->input,
			  this->shot_offsets[i] + PREAMBLE_SIZE + 4,
			  SEEK_SET);
      }

      while (1) {

        if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
          PREAMBLE_SIZE) {
          this->status = DEMUX_FINISHED;
          return this->status;
        }

        chunk_tag = _X_BE_32(&preamble[0]);
        /* round up to the nearest even size */
        chunk_size = (_X_BE_32(&preamble[4]) + 1) & (~1);

        if (chunk_tag == SHOT_TAG) {
          this->shot_offsets[i + 1] =
            this->input->get_current_pos(this->input) - PREAMBLE_SIZE;
          /* skip the four SHOT data bytes (palette index) */
          this->input->seek(this->input, 4, SEEK_CUR);
          break;  /* get out of the infinite while loop */
        } else {
          this->input->seek(this->input, chunk_size, SEEK_CUR);
        }
      }
    }

    /* check if the seek-to offset falls in between this shot offset and
     * the next one */
    if ((start_pos >= this->shot_offsets[i]) &&
        (start_pos <  this->shot_offsets[i + 1])) {

      new_shot = i;
      break;
    }
  }

  /* if no new shot was found in the loop, the new shot must be the last
   * shot */
  if (new_shot == -1)
    new_shot = this->number_of_shots - 1;
  this->current_shot = new_shot;

  /* reposition the stream at new shot */
  this->input->seek(this->input, this->shot_offsets[new_shot], SEEK_SET);

  return this->status;
}

static void demux_mve_dispose (demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  free(this->palettes);
  free(this->shot_offsets);
  free(this);
}

static int demux_mve_get_status (demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  return this->status;
}

static int demux_mve_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_mve_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mve_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_mve_t    *this;

  this         = calloc(1, sizeof(demux_mve_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mve_send_headers;
  this->demux_plugin.send_chunk        = demux_mve_send_chunk;
  this->demux_plugin.seek              = demux_mve_seek;
  this->demux_plugin.dispose           = demux_mve_dispose;
  this->demux_plugin.get_status        = demux_mve_get_status;
  this->demux_plugin.get_stream_length = demux_mve_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mve_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mve_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_mve_file(this)) {
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

void *demux_wc3movie_init_plugin (xine_t *xine, void *data) {
  demux_mve_class_t     *this;

  this = calloc(1, sizeof(demux_mve_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Wing Commander III Movie (MVE) demux plugin");
  this->demux_class.identifier      = "WC3 Movie";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "mve";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
