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
 *
 * FILM (CPK) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FILM file format, visit:
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

#define LOG_MODULE "demux_film"
#define LOG_VERBOSE
/*
#define LOG
*/


/* set DEBUG_FILM_LOAD to dump the frame index after the demuxer loads a
 * FILM file */
#define DEBUG_FILM_LOAD 0

/* set DEBUG_FILM_DEMUX to output information about the A/V chunks that the
 * demuxer is dispatching to the engine */
#define DEBUG_FILM_DEMUX 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_games.h"

#define FOURCC_TAG BE_FOURCC
#define FILM_TAG FOURCC_TAG('F', 'I', 'L', 'M')
#define FDSC_TAG FOURCC_TAG('F', 'D', 'S', 'C')
#define STAB_TAG FOURCC_TAG('S', 'T', 'A', 'B')
#define CVID_TAG FOURCC_TAG('c', 'v', 'i', 'd')

typedef struct {
  int audio;  /* audio = 1, video = 0 */
  off_t sample_offset;
  unsigned int sample_size;
  int64_t pts;
  int64_t duration;
  int keyframe;
} film_sample_t;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                data_size;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  char                 version[4];

  /* video information */
  unsigned int         video_codec;
  unsigned int         video_type;
  xine_bmiheader       bih;

  /* audio information */
  unsigned int         audio_type;
  unsigned int         sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned char       *interleave_buffer;

  /* playback information */
  unsigned int         frequency;
  unsigned int         sample_count;
  film_sample_t       *sample_table;
  unsigned int         current_sample;
  unsigned int         last_sample;
  int                  total_time;
} demux_film_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_film_class_t;


/* Open a FILM file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if FILM file was opened successfully. */
static int open_film_file(demux_film_t *film) {

  unsigned char *film_header;
  unsigned int film_header_size;
  unsigned char scratch[16];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned int i, j;
  unsigned int audio_byte_count = 0;
  int64_t largest_pts = 0;
  unsigned int pts;

  /* initialize structure fields */
  film->bih.biWidth = 0;
  film->bih.biHeight = 0;
  film->video_codec = 0;
  film->sample_rate = 0;
  film->audio_bits = 0;
  film->audio_channels = 0;

  /* get the signature, header length and file version */
  if (_x_demux_read_header(film->input, scratch, 16) != 16)
    return 0;

  /* FILM signature correct? */
  if (!_x_is_fourcc(scratch, "FILM"))
    return 0;

  llprintf(DEBUG_FILM_LOAD, "found 'FILM' signature\n");

  /* file is qualified; skip over the header bytes in the stream */
  film->input->seek(film->input, 16, SEEK_SET);

  /* header size = header size - 16-byte FILM signature */
  film_header_size = _X_BE_32(&scratch[4]) - 16;
  film_header = malloc(film_header_size);
  if (!film_header)
    return 0;
  memcpy(film->version, &scratch[8], 4);
  llprintf(DEBUG_FILM_LOAD, "0x%X header bytes, version %c%c%c%c\n",
    film_header_size,
    film->version[0],
    film->version[1],
    film->version[2],
    film->version[3]);

  /* load the rest of the FILM header */
  if (film->input->read(film->input, film_header, film_header_size) !=
    film_header_size) {
    free (film->interleave_buffer);
    free (film->sample_table);
    free (film_header);
    return 0;
  }

  /* get the starting offset */
  film->data_start = film->input->get_current_pos(film->input);
  film->data_size = film->input->get_length(film->input) - film->data_start;

  /* traverse the FILM header */
  i = 0;
  while (i < film_header_size) {
    chunk_type = _X_BE_32(&film_header[i]);
    chunk_size = _X_BE_32(&film_header[i + 4]);

    /* sanity check the chunk size */
    if (i + chunk_size > film_header_size) {
      xine_log(film->stream->xine, XINE_LOG_MSG, _("invalid FILM chunk size\n"));
      free (film->interleave_buffer);
      free (film->sample_table);
      free (film_header);
      return 0;
    }

    switch(chunk_type) {
    case FDSC_TAG:
      llprintf(DEBUG_FILM_LOAD, "parsing FDSC chunk\n");

      /* always fetch the video information */
      film->bih.biWidth = _X_BE_32(&film_header[i + 16]);
      film->bih.biHeight = _X_BE_32(&film_header[i + 12]);
      film->video_codec = *(uint32_t *)&film_header[i + 8];
      film->video_type = _x_fourcc_to_buf_video(*(uint32_t *)&film_header[i + 8]);

      if( !film->video_type )
      {
        film->video_type = BUF_VIDEO_UNKNOWN;
        _x_report_video_fourcc (film->stream->xine, LOG_MODULE,
				*(uint32_t *)&film_header[i + 8]);
      }

      /* fetch the audio information if the chunk size checks out */
      if (chunk_size == 32) {
        film->audio_channels = film_header[21];
        film->audio_bits = film_header[22];
        film->sample_rate = _X_BE_16(&film_header[24]);
      } else {
        /* If the FDSC chunk is not 32 bytes long, this is an early FILM
         * file. Make a few assumptions about the audio parms based on the
         * video codec used in the file. */
        if (film->video_type == BUF_VIDEO_CINEPAK) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 22050;
        } else if (film->video_type == BUF_VIDEO_SEGA) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 16000;
        }
      }
      if (film->sample_rate)
        film->audio_type = BUF_AUDIO_LPCM_BE;
      else
        film->audio_type = 0;

      if (film->video_type)
        llprintf(DEBUG_FILM_LOAD, "video: %dx%d %c%c%c%c\n",
          film->bih.biWidth, film->bih.biHeight,
          film_header[i + 8],
          film_header[i + 9],
          film_header[i + 10],
          film_header[i + 11]);
      else
        llprintf(DEBUG_FILM_LOAD, "no video\n");

      if (film->audio_type)
        llprintf(DEBUG_FILM_LOAD, "audio: %d Hz, %d channels, %d bits PCM\n",
          film->sample_rate,
          film->audio_channels,
          film->audio_bits);
      else
        llprintf(DEBUG_FILM_LOAD, "no audio\n");

      break;

    case STAB_TAG:
      llprintf(DEBUG_FILM_LOAD, "parsing STAB chunk\n");

      /* load the sample table */
      free(film->sample_table);
      film->frequency = _X_BE_32(&film_header[i + 8]);
      film->sample_count = _X_BE_32(&film_header[i + 12]);
      film->sample_table =
        xine_xcalloc(film->sample_count, sizeof(film_sample_t));
      if (!film->sample_table)
        goto film_abort;
      for (j = 0; j < film->sample_count; j++) {

        film->sample_table[j].sample_offset =
          _X_BE_32(&film_header[(i + 16) + j * 16 + 0])
          + film_header_size + 16;
        film->sample_table[j].sample_size =
          _X_BE_32(&film_header[(i + 16) + j * 16 + 4]);
        pts =
          _X_BE_32(&film_header[(i + 16) + j * 16 + 8]);
        film->sample_table[j].duration =
          _X_BE_32(&film_header[(i + 16) + j * 16 + 12]);

        if (pts == 0xFFFFFFFF) {

          film->sample_table[j].audio = 1;
          film->sample_table[j].keyframe = 0;

          /* figure out audio pts */
          film->sample_table[j].pts = audio_byte_count;
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /=
            (film->sample_rate * film->audio_channels * (film->audio_bits / 8));
          audio_byte_count += film->sample_table[j].sample_size;

        } else {

          /* figure out video pts, duration, and keyframe */
          film->sample_table[j].audio = 0;

          /* keyframe if top bit of this field is 0 */
          if (pts & 0x80000000)
            film->sample_table[j].keyframe = 0;
          else
            film->sample_table[j].keyframe = 1;

          /* remove the keyframe bit */
          film->sample_table[j].pts = pts & 0x7FFFFFFF;

          /* compute the pts */
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /= film->frequency;

          /* compute the frame duration */
          film->sample_table[j].duration *= 90000;
          film->sample_table[j].duration /= film->frequency;

        }

        /* use this to calculate the total running time of the file */
        if (film->sample_table[j].pts > largest_pts)
          largest_pts = film->sample_table[j].pts;

        llprintf(DEBUG_FILM_LOAD, "sample %4d @ %8" PRIxMAX ", %8X bytes, %s, pts %" PRId64 ", duration %" PRId64 "%s\n",
          j,
          (intmax_t)film->sample_table[j].sample_offset,
          film->sample_table[j].sample_size,
          (film->sample_table[j].audio) ? "audio" : "video",
          film->sample_table[j].pts,
          film->sample_table[j].duration,
          (film->sample_table[j].keyframe) ? " (keyframe)" : "");
      }

      /*
       * in some files, this chunk length does not account for the 16-byte
       * chunk preamble; watch for it
       */
      if (chunk_size == film->sample_count * 16)
        i += 16;

      /* allocate enough space in the interleave preload buffer for the
       * first chunk (which will be more than enough for successive chunks) */
      if (film->audio_type) {
	free(film->interleave_buffer);
        film->interleave_buffer = calloc(1, film->sample_table[0].sample_size);
        if (!film->interleave_buffer)
          goto film_abort;
      }
      break;

    default:
      xine_log(film->stream->xine, XINE_LOG_MSG, _("unrecognized FILM chunk\n"));
    film_abort:
      free (film->interleave_buffer);
      free (film->sample_table);
      free (film_header);
      return 0;
    }

    i += chunk_size;
  }

  film->total_time = largest_pts / 90;

  free (film_header);

  return 1;
}

static int demux_film_send_chunk(demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int cvid_chunk_size;
  unsigned int i, j, k;
  int fixed_cvid_header;
  unsigned int remaining_sample_bytes;
  int first_buf;
  int interleave_index;

  i = this->current_sample;

  /* if there is an incongruency between last and current sample, it
   * must be time to send a new pts */
  if (this->last_sample + 1 != this->current_sample) {
    /* send new pts */
    _x_demux_control_newpts(this->stream, this->sample_table[i].pts,
      (this->sample_table[i].pts) ? BUF_FLAG_SEEK : 0);
  }

  this->last_sample = this->current_sample;
  this->current_sample++;

  /* check if all the samples have been sent */
  if (i >= this->sample_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if we're only sending audio samples until the next keyframe */
  if ((this->waiting_for_keyframe) &&
      (!this->sample_table[i].audio)) {
    if (this->sample_table[i].keyframe) {
      this->waiting_for_keyframe = 0;
    } else {
      /* move on to the next sample */
      return this->status;
    }
  }

  llprintf(DEBUG_FILM_DEMUX, "dispatching frame...\n");

  if ((!this->sample_table[i].audio) &&
    (this->video_type == BUF_VIDEO_CINEPAK)) {
    /* do a special song and dance when loading CVID data */
    if (this->version[0])
      cvid_chunk_size = this->sample_table[i].sample_size - 2;
    else
      cvid_chunk_size = this->sample_table[i].sample_size - 6;

    /* reset flag */
    fixed_cvid_header = 0;

    remaining_sample_bytes = cvid_chunk_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double)(this->sample_table[i].sample_offset -
                                                         this->data_start) *
                                                 65535 / this->data_size);
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = this->sample_table[i].duration;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (!fixed_cvid_header) {
        if (this->input->read(this->input, buf->content, 10) != 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* skip over the extra non-spec CVID bytes */
        this->input->seek(this->input,
          this->sample_table[i].sample_size - cvid_chunk_size, SEEK_CUR);

        /* load the rest of the chunk */
        if (this->input->read(this->input, buf->content + 10,
          buf->size - 10) != buf->size - 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* adjust the length in the CVID data chunk */
        buf->content[1] = (cvid_chunk_size >> 16) & 0xFF;
        buf->content[2] = (cvid_chunk_size >>  8) & 0xFF;
        buf->content[3] = (cvid_chunk_size >>  0) & 0xFF;

        fixed_cvid_header = 1;
      } else {
        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }
      }

      if (this->sample_table[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      llprintf(DEBUG_FILM_DEMUX, "sending video buf with %" PRId32 " bytes, %" PRId64 " pts, %" PRId32 " duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->video_fifo->put(this->video_fifo, buf);
    }

  } else if (!this->sample_table[i].audio) {

    /* load a non-cvid video chunk */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double)(this->sample_table[i].sample_offset -
                                                         this->data_start) *
                                                 65535 / this->data_size);
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = this->sample_table[i].duration;

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

      if (this->sample_table[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      llprintf(DEBUG_FILM_DEMUX, "sending video buf with %" PRId32 " bytes, %" PRId64 " pts, %" PRId32 " duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->video_fifo->put(this->video_fifo, buf);
    }
  } else if(this->audio_fifo && this->audio_channels == 1) {

    /* load a mono audio sample and packetize it */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    first_buf = 1;
    while (remaining_sample_bytes) {

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double)(this->sample_table[i].sample_offset -
                                                         this->data_start) *
                                                 65535 / this->data_size);

      /* special hack to accomodate linear PCM decoder: only the first
       * buffer gets the real pts */
      if (first_buf) {
        buf->pts = this->sample_table[i].pts;
        first_buf = 0;
      } else
        buf->pts = 0;
      buf->extra_info->input_time = buf->pts / 90;

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

      if (this->video_type == BUF_VIDEO_SEGA) {
        /* if the file uses the SEGA video codec, assume this is
         * sign/magnitude audio */
        for (j = 0; j < buf->size; j++)
          if (buf->content[j] < 0x80)
            buf->content[j] += 0x80;
          else
            buf->content[j] = -(buf->content[j] & 0x7F) + 0x80;
      } else if (this->audio_bits == 8) {
        /* convert 8-bit data from signed -> unsigned */
        for (j = 0; j < buf->size; j++)
          buf->content[j] += 0x80;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      llprintf(DEBUG_FILM_DEMUX, "sending mono audio buf with %" PRId32 " bytes, %" PRId64 " pts, %" PRId32 " duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->audio_fifo->put(this->audio_fifo, buf);

    }
  } else if(this->audio_fifo && this->audio_channels == 2) {

    /* load an entire stereo sample and interleave the channels */

    /* load the whole chunk into the buffer */
    if (this->input->read(this->input, this->interleave_buffer,
      this->sample_table[i].sample_size) != this->sample_table[i].sample_size) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* proceed to de-interleave into individual buffers */
    remaining_sample_bytes = this->sample_table[i].sample_size / 2;
    interleave_index = 0;
    first_buf = 1;
    while (remaining_sample_bytes) {

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double)(this->sample_table[i].sample_offset -
                                                         this->data_start) *
                                                 65535 / this->data_size);

      /* special hack to accomodate linear PCM decoder: only the first
       * buffer gets the real pts */
      if (first_buf) {
        buf->pts = this->sample_table[i].pts;
        first_buf = 0;
      } else
        buf->pts = 0;
      buf->extra_info->input_time = buf->pts / 90;

      if (remaining_sample_bytes > buf->max_size / 2)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes * 2;
      remaining_sample_bytes -= buf->size / 2;

      if (this->audio_bits == 16) {
        for (j = 0, k = interleave_index; j < buf->size; j += 4, k += 2) {
          buf->content[j] =     this->interleave_buffer[k];
          buf->content[j + 1] = this->interleave_buffer[k + 1];
        }
        for (j = 2,
             k = interleave_index + this->sample_table[i].sample_size / 2;
             j < buf->size; j += 4, k += 2) {
          buf->content[j] =     this->interleave_buffer[k];
          buf->content[j + 1] = this->interleave_buffer[k + 1];
        }
        interleave_index += buf->size / 2;
      } else {
        for (j = 0, k = interleave_index; j < buf->size; j += 2, k += 1) {
          buf->content[j] = this->interleave_buffer[k] += 0x80;
        }
        for (j = 1,
             k = interleave_index + this->sample_table[i].sample_size / 2;
             j < buf->size; j += 2, k += 1) {
          buf->content[j] = this->interleave_buffer[k] += 0x80;
        }
        interleave_index += buf->size / 2;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      llprintf(DEBUG_FILM_DEMUX, "sending stereo audio buf with %" PRId32 " bytes, %" PRId64 " pts, %" PRId32 " duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }

  return this->status;
}

static void demux_film_send_headers(demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf;
  int64_t initial_duration = 3000;
  int i;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO,
    (this->video_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
    (this->audio_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC, this->video_codec);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
    this->audio_channels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
    this->sample_rate);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
    this->audio_bits);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->video_type) {
    /* find the first video frame duration */
    for (i = 0; i < this->sample_count; i++)
      if (!this->sample_table[i].audio) {
        initial_duration = this->sample_table[i].duration;
        break;
      }

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = initial_duration;
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }

  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_BE;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_film_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_film_t *this = (demux_film_t *) this_gen;

  int best_index;
  int left, middle, right;
  int found;
  int64_t keyframe_pts;

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  this->waiting_for_keyframe = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine(this->stream);

  if( !playing ) {
    this->waiting_for_keyframe = 0;
    this->last_sample = 0;
  }

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* perform a binary search on the sample table, testing the offset
   * boundaries first */
  if (start_pos) {
    if (start_pos <= 0)
      best_index = 0;
    else if (start_pos >= this->data_size) {
      this->status = DEMUX_FINISHED;
      return this->status;
    } else {
      start_pos += this->data_start;
      left = 0;
      right = this->sample_count - 1;
      found = 0;

      while (!found) {
        middle = (left + right) / 2;
        if ((start_pos >= this->sample_table[middle].sample_offset) &&
            (start_pos <= this->sample_table[middle].sample_offset +
             this->sample_table[middle].sample_size)) {
          found = 1;
        } else if (start_pos < this->sample_table[middle].sample_offset) {
          right = middle;
        } else {
          left = middle;
        }
      }

      best_index = middle;
    }
  } else {
    int64_t pts = 90 * start_time;

    if (pts <= this->sample_table[0].pts)
      best_index = 0;
    else if (pts >= this->sample_table[this->sample_count - 1].pts) {
      this->status = DEMUX_FINISHED;
      return this->status;
    } else {
      left = 0;
      right = this->sample_count - 1;
      do {
        middle = (left + right + 1) / 2;
        if (pts < this->sample_table[middle].pts) {
          right = (middle - 1);
        } else {
          left = middle;
        }
      } while (left < right);

      best_index = left;
    }
  }

  /* search back in the table for the nearest keyframe */
  while (best_index) {
    if (this->sample_table[best_index].keyframe) {
      break;
    }
    best_index--;
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe */
  keyframe_pts = this->sample_table[best_index].pts;
  while (best_index) {
    if ((this->sample_table[best_index].audio) &&
        (this->sample_table[best_index].pts < keyframe_pts)) {
      break;
    }
    best_index--;
  }

  this->current_sample = best_index;

  return this->status;
}

static void demux_film_dispose (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  free(this->sample_table);
  free(this->interleave_buffer);
  free(this);
}

static int demux_film_get_status (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->status;
}

static int demux_film_get_stream_length (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->total_time;
}

static uint32_t demux_film_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_film_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_film_t    *this;

  this         = calloc(1, sizeof(demux_film_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_film_send_headers;
  this->demux_plugin.send_chunk        = demux_film_send_chunk;
  this->demux_plugin.seek              = demux_film_seek;
  this->demux_plugin.dispose           = demux_film_dispose;
  this->demux_plugin.get_status        = demux_film_get_status;
  this->demux_plugin.get_stream_length = demux_film_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_film_get_capabilities;
  this->demux_plugin.get_optional_data = demux_film_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_film_file(this)) {
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

void *demux_film_init_plugin (xine_t *xine, void *data) {
  demux_film_class_t     *this;

  this = calloc(1, sizeof(demux_film_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("FILM (CPK) demux plugin");
  this->demux_class.identifier      = "FILM (CPK)";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "cpk cak film";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
