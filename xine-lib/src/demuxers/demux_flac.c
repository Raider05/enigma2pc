/*
 * Copyright (C) 2000-2007 the xine project
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
 * FLAC File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FLAC file format, visit:
 *   http://flac.sourceforge.net/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#define LOG_MODULE "demux_flac"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_audio.h"

#include "id3.h"
#include "flacutils.h"

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  sample_rate;
  int                  bits_per_sample;
  int                  channels;
  int64_t              total_samples;
  off_t                data_start;
  off_t                data_size;

  flac_seekpoint_t    *seekpoints;
  int                  seekpoint_count;

  unsigned char        streaminfo[sizeof(xine_waveformatex) + FLAC_STREAMINFO_SIZE];
} demux_flac_t;

typedef struct {
  demux_class_t     demux_class;
} demux_flac_class_t;

/* Open a flac file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if flac file was opened successfully. */
static int open_flac_file(demux_flac_t *flac) {

  uint32_t signature;
  unsigned char preamble[10];
  unsigned int block_length;
  unsigned char buffer[FLAC_SEEKPOINT_SIZE];
  unsigned char *streaminfo = flac->streaminfo + sizeof(xine_waveformatex);
  int i;

  flac->seekpoints = NULL;

  /* fetch the file signature, 4 bytes will read both the fLaC
   * signature and the */
  if (_x_demux_read_header(flac->input, &signature, 4) != 4)
    return 0;

  flac->input->seek(flac->input, 4, SEEK_SET);

  /* Unfortunately some FLAC files have an ID3 flag prefixed on them
   * before the actual FLAC headers... these are barely legal, but
   * users use them and want them working, so check and skip the ID3
   * tag if present.
   */
  if ( id3v2_istag(signature) ) {
    id3v2_parse_tag(flac->input, flac->stream, signature);

    if ( flac->input->read(flac->input, &signature, 4) != 4 )
      return 0;
  }

  /* validate signature */
  if ( signature != ME_FOURCC('f', 'L', 'a', 'C') )
      return 0;

  /* loop through the metadata blocks; use a do-while construct since there
   * will always be 1 metadata block */
  do {

    if (flac->input->read(flac->input, preamble, FLAC_SIGNATURE_SIZE) !=
        FLAC_SIGNATURE_SIZE)
      return 0;

    block_length = (preamble[1] << 16) |
                   (preamble[2] <<  8) |
                   (preamble[3] <<  0);

    switch (preamble[0] & 0x7F) {

    /* STREAMINFO */
    case 0:
      lprintf ("STREAMINFO metadata\n");
      if (block_length != FLAC_STREAMINFO_SIZE) {
        lprintf ("expected STREAMINFO chunk of %d bytes\n",
          FLAC_STREAMINFO_SIZE);
        return 0;
      }
      if (flac->input->read(flac->input,
        flac->streaminfo + sizeof(xine_waveformatex),
        FLAC_STREAMINFO_SIZE) != FLAC_STREAMINFO_SIZE)
        return 0;
      flac->sample_rate = _X_BE_32(&streaminfo[10]);
      flac->channels = ((flac->sample_rate >> 9) & 0x07) + 1;
      flac->bits_per_sample = ((flac->sample_rate >> 4) & 0x1F) + 1;
      flac->sample_rate >>= 12;
      flac->total_samples = _X_BE_64(&streaminfo[10]) & UINT64_C(0x0FFFFFFFFF);  /* 36 bits */
      lprintf ("%d Hz, %d bits, %d channels, %"PRId64" total samples\n",
        flac->sample_rate, flac->bits_per_sample,
        flac->channels, flac->total_samples);
      break;

    /* PADDING */
    case 1:
      lprintf ("PADDING metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* APPLICATION */
    case 2:
      lprintf ("APPLICATION metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* SEEKTABLE */
    case 3:
      lprintf ("SEEKTABLE metadata, %d bytes\n", block_length);
      flac->seekpoint_count = block_length / FLAC_SEEKPOINT_SIZE;
      flac->seekpoints = xine_xcalloc(flac->seekpoint_count,
				      sizeof(flac_seekpoint_t));
      if (flac->seekpoint_count && !flac->seekpoints)
        return 0;
      for (i = 0; i < flac->seekpoint_count; i++) {
        if (flac->input->read(flac->input, buffer, FLAC_SEEKPOINT_SIZE) != FLAC_SEEKPOINT_SIZE)
          return 0;
        flac->seekpoints[i].sample_number = _X_BE_64(&buffer[0]);
        lprintf (" %d: sample %"PRId64", ", i, flac->seekpoints[i].sample_number);
        flac->seekpoints[i].offset = _X_BE_64(&buffer[8]);
        flac->seekpoints[i].size = _X_BE_16(&buffer[16]);
        lprintf ("@ 0x%"PRIX64", size = %d bytes, ",
          flac->seekpoints[i].offset, flac->seekpoints[i].size);
        flac->seekpoints[i].pts = flac->seekpoints[i].sample_number;
        flac->seekpoints[i].pts *= 90000;
        flac->seekpoints[i].pts /= flac->sample_rate;
        lprintf ("pts = %"PRId64"\n", flac->seekpoints[i].pts);
      }
      break;

    /* VORBIS_COMMENT
     *
     * For a description of the format please have a look at
     * http://www.xiph.org/vorbis/doc/v-comment.html */
    case 4:
      lprintf ("VORBIS_COMMENT metadata\n");
      {
        char comments[block_length + 1]; /* last byte for NUL termination */
        char *ptr = comments;
        uint32_t length, user_comment_list_length, cn;
        char *comment;
        char c;

        if (flac->input->read(flac->input, comments, block_length) == block_length) {
          int tracknumber = -1;
          int tracktotal = -1;

          length = _X_LE_32(ptr);
          ptr += 4 + length;
          if (length > block_length - 8)
            return 0; /* bad length or too little left in the buffer */

          user_comment_list_length = _X_LE_32(ptr);
          ptr += 4;

          cn = 0;
          for (; cn < user_comment_list_length; cn++) {
            if (ptr > comments + block_length - 4)
              return 0; /* too little left in the buffer */

            length = _X_LE_32(ptr);
            ptr += 4;
            if (length >= block_length || ptr + length > comments + block_length)
              return 0; /* bad length */

            comment = (char*) ptr;
            c = comment[length];
            comment[length] = 0; /* NUL termination */

            lprintf ("comment[%02d] = %s\n", cn, comment);

            if ((strncasecmp ("TITLE=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_TITLE, comment + 6);
            } else if ((strncasecmp ("ARTIST=", comment, 7) == 0)
                && (length - 7 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_ARTIST, comment + 7);
            } else if ((strncasecmp ("COMPOSER=", comment, 9) == 0)
                && (length - 9 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_COMPOSER, comment + 9);
            } else if ((strncasecmp ("ALBUM=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_ALBUM, comment + 6);
            } else if ((strncasecmp ("DATE=", comment, 5) == 0)
                && (length - 5 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_YEAR, comment + 5);
            } else if ((strncasecmp ("GENRE=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_GENRE, comment + 6);
            } else if ((strncasecmp ("Comment=", comment, 8) == 0)
                && (length - 8 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_COMMENT, comment + 8);
            } else if ((strncasecmp ("TRACKNUMBER=", comment, 12) == 0)
                && (length - 12 > 0)) {
              tracknumber = atoi (comment + 12);
            } else if ((strncasecmp ("TRACKTOTAL=", comment, 11) == 0)
                && (length - 11 > 0)) {
              tracktotal = atoi (comment + 11);
            }
            comment[length] = c;

            ptr += length;
          }

          if ((tracknumber > 0) && (tracktotal > 0)) {
            char tn[24];
            snprintf (tn, 24, "%02d/%02d", tracknumber, tracktotal);
            _x_meta_info_set(flac->stream, XINE_META_INFO_TRACK_NUMBER, tn);
          }
          else if (tracknumber > 0) {
            char tn[16];
            snprintf (tn, 16, "%02d", tracknumber);
            _x_meta_info_set(flac->stream, XINE_META_INFO_TRACK_NUMBER, tn);
          }
        }
      }
      break;

    /* CUESHEET */
    case 5:
      lprintf ("CUESHEET metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* 6-127 are presently reserved */
    default:
      lprintf ("unknown metadata chunk: %d\n", preamble[0] & 0x7F);
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    }

  } while ((preamble[0] & 0x80) == 0);

  flac->data_start = flac->input->get_current_pos(flac->input);
  flac->data_size = flac->input->get_length(flac->input) - flac->data_start;

  /* now at the beginning of the audio, adjust the seekpoint offsets */
  for (i = 0; i < flac->seekpoint_count; i++) {
    flac->seekpoints[i].offset += flac->data_start;
  }

  return 1;
}

static int demux_flac_send_chunk(demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  buf_element_t *buf = NULL;
  int64_t input_time_guess;

  /* just send a buffer-sized chunk; let the decoder figure out the
   * boundaries and let the engine figure out the pts */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_FLAC;
  if( this->data_size )
    buf->extra_info->input_normpos = (int) ( (double) (this->input->get_current_pos(this->input) -
                                     this->data_start) * 65535 / this->data_size );
  buf->pts = 0;
  buf->size = buf->max_size;

  /*
   * Estimate the input_time field based on file position:
   *
   *   current_pos     input time
   *   -----------  =  ----------
   *    total_pos      total time
   *
   *  total time = total samples / sample rate * 1000
   */

  /* do this one step at a time to make sure all the numbers stay safe */
  input_time_guess = this->total_samples;
  input_time_guess /= this->sample_rate;
  input_time_guess *= 1000;
  input_time_guess *= buf->extra_info->input_normpos;
  input_time_guess /= 65535;
  buf->extra_info->input_time = input_time_guess;

  if (this->input->read(this->input, buf->content, buf->size) !=
    buf->size) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  buf->decoder_flags |= BUF_FLAG_FRAME_END;
  this->audio_fifo->put(this->audio_fifo, buf);

  return this->status;
}

static void demux_flac_send_headers(demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  buf_element_t *buf;
  xine_waveformatex wave;
  int bits;

  this->audio_fifo  = this->stream->audio_fifo;

  /* send start buffers */
  _x_demux_control_start(this->stream);

  if ( ! this->audio_fifo )
  {
    this->status = DEMUX_FINISHED;
    return;
  }

  /* lie about 24bps */
  bits = this->bits_per_sample > 16 ? 16 : this->bits_per_sample;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_FLAC;
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = this->sample_rate;
  buf->decoder_info[2] = bits;
  buf->decoder_info[3] = this->channels;
  /* copy the faux WAV header */
  buf->size = sizeof(xine_waveformatex) + FLAC_STREAMINFO_SIZE;
  memcpy(buf->content, this->streaminfo, buf->size);
  /* forge a WAV header with the proper length */
  wave.cbSize = FLAC_STREAMINFO_SIZE;
  memcpy(buf->content, &wave, sizeof(xine_waveformatex));
  this->audio_fifo->put (this->audio_fifo, buf);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                       this->channels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       this->sample_rate);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                       bits);

  this->status = DEMUX_OK;
}

static int demux_flac_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time, int playing) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  int seekpoint_index = 0;
  int64_t start_pts;
  unsigned char buf[4];

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  /* if thread is not running, initialize demuxer */
  if( !playing && !start_pos) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  } else {

    if (this->seekpoints == NULL && !start_pos) {
      /* cannot seek if there is no seekpoints */
      this->status = DEMUX_OK;
      return this->status;
    }

    /* Don't use seekpoints if start_pos != 0. This allows smooth seeking */
    if (start_pos) {
      /* offset-based seek */
      this->status = DEMUX_OK;
      start_pos += this->data_start;
      this->input->seek(this->input, start_pos, SEEK_SET);
      while(1){ /* here we try to find something that resembles a frame header */

	if (this->input->read(this->input, buf, 2) != 2){
	  this->status = DEMUX_FINISHED; /* we sought past the end of stream ? */
	  break;
	}

	if (buf[0] == 0xff && buf[1] == 0xf8)
	  break; /* this might be the frame header... or it may be not. We pass it to the decoder
		  * to decide, but this way we reduce the number of warnings */
	start_pos +=2;
      }

      _x_demux_flush_engine(this->stream);
      this->input->seek(this->input, start_pos, SEEK_SET);
      _x_demux_control_newpts(this->stream, 0, BUF_FLAG_SEEK);
      return this->status;

    } else {
      /* do a lazy, linear seek based on the assumption that there are not
       * that many seek points; time-based seek */
      start_pts = start_time;
      start_pts *= 90;
      if (start_pts < this->seekpoints[0].pts)
        seekpoint_index = 0;
      else {
        for (seekpoint_index = 0; seekpoint_index < this->seekpoint_count - 1;
          seekpoint_index++) {
          if (start_pts < this->seekpoints[seekpoint_index + 1].pts) {
            break;
          }
        }
      }
    }

    _x_demux_flush_engine(this->stream);
    this->input->seek(this->input, this->seekpoints[seekpoint_index].offset,
      SEEK_SET);
    _x_demux_control_newpts(this->stream,
      this->seekpoints[seekpoint_index].pts, BUF_FLAG_SEEK);
  }

  return this->status;
}

static void demux_flac_dispose (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;

  free(this->seekpoints);
  free(this);
}

static int demux_flac_get_status (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;

  return this->status;
}

static int demux_flac_get_stream_length (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  int64_t length = this->total_samples;

  length *= 1000;
  length /= this->sample_rate;

  return length;
}

static uint32_t demux_flac_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_flac_get_optional_data(demux_plugin_t *this_gen,
                                        void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_flac_t    *this;

  /* this should change eventually... */
  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_flac_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_flac_send_headers;
  this->demux_plugin.send_chunk        = demux_flac_send_chunk;
  this->demux_plugin.seek              = demux_flac_seek;
  this->demux_plugin.dispose           = demux_flac_dispose;
  this->demux_plugin.get_status        = demux_flac_get_status;
  this->demux_plugin.get_stream_length = demux_flac_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_flac_get_capabilities;
  this->demux_plugin.get_optional_data = demux_flac_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_flac_file(this)) {
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

void *demux_flac_init_plugin (xine_t *xine, void *data) {
  demux_flac_class_t     *this;

  this = calloc(1, sizeof(demux_flac_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Free Lossless Audio Codec (flac) demux plugin");
  this->demux_class.identifier      = "FLAC";
  this->demux_class.mimetypes       =
    "audio/x-flac: flac: FLAC Audio;"
    "audio/flac: flac: FLAC Audio;";
  this->demux_class.extensions      = "flac";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
