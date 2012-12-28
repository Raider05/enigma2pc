/*
 * Copyright (C) 2000-2006 the xine project
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
 *
 * John McCutchan
 * FLAC demuxer (http://flac.sf.net)
 *
 * TODO: Skip id3v2 tags.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>

#include <FLAC/stream_decoder.h>

#if !defined FLAC_API_VERSION_CURRENT || FLAC_API_VERSION_CURRENT < 8
#include <FLAC/seekable_stream_decoder.h>
#define LEGACY_FLAC
#else
#undef LEGACY_FLAC
#endif

#define LOG_MODULE "demux_flac"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#ifndef LEGACY_FLAC
# define FLAC__SeekableStreamDecoder FLAC__StreamDecoder
#endif

/* FLAC Demuxer plugin */
typedef struct demux_flac_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int status;

  int seek_flag;

  off_t data_start;
  off_t data_size;

  /* FLAC Stuff */
  FLAC__SeekableStreamDecoder *flac_decoder;

  uint64_t total_samples;
  uint64_t bits_per_sample;
  uint64_t channels;
  uint64_t sample_rate;
  uint64_t length_in_msec;
} demux_flac_t ;


/* FLAC Demuxer class */
typedef struct demux_flac_class_s {
  demux_class_t     demux_class;

  xine_t           *xine;
  config_values_t  *config;

} demux_flac_class_t;

/* FLAC Callbacks */
static
#ifdef LEGACY_FLAC
FLAC__SeekableStreamDecoderReadStatus
flac_read_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__byte buffer[],
                    unsigned *bytes,
                    void *client_data)
#else
FLAC__StreamDecoderReadStatus
flac_read_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__byte buffer[],
                    size_t *bytes,
                    void *client_data)
#endif
{
    demux_flac_t *this    = (demux_flac_t *)client_data;
    input_plugin_t *input = this->input;
    off_t offset = *bytes;

    lprintf("flac_read_callback\n");

    /* This should only be called when flac is reading the metadata
     * of the flac stream.
     */

    offset = input->read (input, buffer, offset);

    lprintf("Read %lld / %u bytes into buffer\n", offset, *bytes);

    /* This is the way to detect EOF with xine input plugins */
    if ( offset <= 0 && *bytes != 0 )
    {
      *bytes = offset;
      lprintf("Marking EOF\n");

      this->status = DEMUX_FINISHED;
#ifdef LEGACY_FLAC
      return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_ERROR;
#else
      return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
#endif
    }
    else
    {
      *bytes = offset;
      lprintf("Read was perfect\n");

#ifdef LEGACY_FLAC
      return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK;
#else
      return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
#endif
    }
}

static
#ifdef LEGACY_FLAC
FLAC__SeekableStreamDecoderSeekStatus
#else
FLAC__StreamDecoderSeekStatus
#endif
flac_seek_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__uint64 absolute_byte_offset,
                    void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

    lprintf("flac_seek_callback\n");

    offset = input->seek (input, absolute_byte_offset, SEEK_SET);

    if (offset == -1)
#ifdef LEGACY_FLAC
        return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_ERROR;
#else
        return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
#endif
	else
#ifdef LEGACY_FLAC
        return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
#else
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
#endif
}

static
#ifdef LEGACY_FLAC
FLAC__SeekableStreamDecoderTellStatus
#else
FLAC__StreamDecoderTellStatus
#endif
flac_tell_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__uint64 *absolute_byte_offset,
                    void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

    lprintf("flac_tell_callback\n");

    offset = input->get_current_pos (input);

    *absolute_byte_offset = offset;

#ifdef LEGACY_FLAC
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
#else
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
#endif
}

static
#ifdef LEGACY_FLAC
FLAC__SeekableStreamDecoderLengthStatus
#else
FLAC__StreamDecoderLengthStatus
#endif
flac_length_callback (const FLAC__SeekableStreamDecoder *decoder,
                     FLAC__uint64 *stream_length,
                     void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

    lprintf("flac_length_callback\n");

    offset = input->get_length (input);

    /* FIXME, can flac handle -1 as offset ? */
#ifdef LEGACY_FLAC
    return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
#else
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
#endif
}

static FLAC__bool
flac_eof_callback (const FLAC__SeekableStreamDecoder *decoder,
                    void *client_data)
{
    demux_flac_t *this = (demux_flac_t *)client_data;

    lprintf("flac_eof_callback\n");

    if (this->status == DEMUX_FINISHED)
    {
      lprintf("flac_eof_callback: True!\n");

      return true;
    }
    else
    {
      lprintf("flac_eof_callback: False!\n");

      return false;
    }
}

static FLAC__StreamDecoderWriteStatus
flac_write_callback (const FLAC__SeekableStreamDecoder *decoder,
                     const FLAC__Frame *frame,
                     const FLAC__int32 * const buffer[],
                     void *client_data)
{
    /* This should never be called, all we use flac for in this demuxer
     * is seeking. We do the decoding in the decoder
     */

  lprintf("Error: Write callback was called!\n");

  return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
}

static void
flac_metadata_callback (const FLAC__SeekableStreamDecoder *decoder,
                        const FLAC__StreamMetadata *metadata,
                        void *client_data)
{
    demux_flac_t *this = (demux_flac_t *)client_data;

    lprintf("IN: Metadata callback\n");

    /* This should be called when we first look at a flac stream,
     * We get information about the stream here.
     */
     if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
       lprintf("Got METADATA!\n");

       this->total_samples   = metadata->data.stream_info.total_samples;
       this->bits_per_sample = metadata->data.stream_info.bits_per_sample;
       this->channels        = metadata->data.stream_info.channels;
       this->sample_rate     = metadata->data.stream_info.sample_rate;
       this->length_in_msec  = (this->total_samples * 1000) /
                                this->sample_rate;
     }
     return;
}

static void
flac_error_callback (const FLAC__SeekableStreamDecoder *decoder,
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data)
{
    demux_flac_t *this = (demux_flac_t *)client_data;
    /* This will be called if there is an error when flac is seeking
     * in the stream.
     */

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_flac: flac_error_callback\n");

    if (status == FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_flac: Decoder lost synchronization.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER)
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_flac: Decoder encounted a corrupted frame header.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH)
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_flac: Frame's data did not match the CRC in the footer.\n");
    else
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_flac: unknown error.\n");

    this->status = DEMUX_FINISHED;

    return;
}

/* FLAC Demuxer plugin */
static int
demux_flac_send_chunk (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;
    buf_element_t *buf = NULL;
    off_t current_file_pos, file_size = 0;
    int64_t current_pts;
    unsigned int remaining_sample_bytes = 0;

    remaining_sample_bytes = 2048;

    current_file_pos = this->input->get_current_pos (this->input)
                        - this->data_start;
    if( (this->data_size - this->data_start) > 0 )
        file_size = (this->data_size - this->data_start);

    current_pts = current_file_pos;
    current_pts *= this->length_in_msec * 90;
    if( file_size )
        current_pts /= file_size;

    if (this->seek_flag) {
#ifdef USE_ESTIMATED_PTS
        _x_demux_control_newpts (this->stream, current_pts, BUF_FLAG_SEEK);
#else
        _x_demux_control_newpts (this->stream, 0, BUF_FLAG_SEEK);
#endif
        this->seek_flag = 0;
    }


    while (remaining_sample_bytes)
    {
        if(!this->audio_fifo) {
          this->status = DEMUX_FINISHED;
          break;
        }

        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_FLAC;
        if( file_size )
          buf->extra_info->input_normpos = (int) ((double)current_file_pos * 65535 / file_size);
        buf->extra_info->input_time   = current_pts / 90;
#ifdef USE_ESTIMATED_PTS
        buf->pts = current_pts;
#else
        buf->pts = 0;
#endif

        if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
        else
            buf->size = remaining_sample_bytes;

        remaining_sample_bytes -= buf->size;

        if (this->input->read (this->input,buf->content,buf->size)!=buf->size) {
	  lprintf("buf->size != input->read()\n");

	  buf->free_buffer (buf);
	  this->status = DEMUX_FINISHED;
	  break;
        }

        /*
        if (!remaining_sample_bytes)
        {
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
        }*/

        this->audio_fifo->put (this->audio_fifo, buf);
    }

    return this->status;
}

static void
demux_flac_send_headers (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    buf_element_t *buf;

    lprintf("demux_flac_send_headers\n");

    this->video_fifo = this->stream->video_fifo;
    this->audio_fifo = this->stream->audio_fifo;

    this->status = DEMUX_OK;

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, this->channels);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, this->sample_rate);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, this->bits_per_sample);

    _x_demux_control_start (this->stream);

    if (this->audio_fifo) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_FLAC;
        buf->decoder_flags   = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
        buf->decoder_info[0] = 0;
        buf->decoder_info[1] = this->sample_rate;
        buf->decoder_info[2] = this->bits_per_sample;
        buf->decoder_info[3] = this->channels;
        buf->size = 0;
        this->audio_fifo->put (this->audio_fifo, buf);
    }
}

static void
demux_flac_dispose (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    lprintf("demux_flac_dispose\n");

    if (this->flac_decoder)
#ifdef LEGACY_FLAC
        FLAC__seekable_stream_decoder_delete (this->flac_decoder);
#else
	FLAC__stream_decoder_delete (this->flac_decoder);
#endif

    free(this);
    return;
}

static int
demux_flac_get_status (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    lprintf("demux_flac_get_status\n");

    return this->status;
}


static int
demux_flac_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    lprintf("demux_flac_seek\n");

    start_pos = (off_t) ( (double) start_pos / 65535 *
                this->input->get_length (this->input) );

    if (!start_pos && start_time) {
        double distance = (double)start_time;

        if (this->length_in_msec != 0)
        {
            distance /= (double)this->length_in_msec;
        }
        start_pos = (uint64_t)(distance * (this->data_size - this->data_start));
    }

    if (start_pos || !start_time) {

        start_pos += this->data_start;
        this->input->seek (this->input, start_pos, SEEK_SET);
        lprintf ("Seek to position: %lld\n", start_pos);

    } else {

        double distance = (double)start_time;
        uint64_t target_sample;
        FLAC__bool s = false;

        if (this->length_in_msec != 0)
        {
            distance /= (double)this->length_in_msec;
        }
        target_sample = (uint64_t)(distance * this->total_samples);

#ifdef LEGACY_FLAC
        s = FLAC__seekable_stream_decoder_seek_absolute (this->flac_decoder,
                                                         target_sample);
#else
        s = FLAC__stream_decoder_seek_absolute (this->flac_decoder,
                                                         target_sample);
#endif

        if (s) {
	  lprintf ("Seek to: %d successfull!\n", start_time);
        } else
            this->status = DEMUX_FINISHED;
    }

    _x_demux_flush_engine (this->stream);
    this->seek_flag = 1;

    return this->status;
}

static int
demux_flac_get_stream_length (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    lprintf("demux_flac_get_stream_length\n");

    if (this->flac_decoder)
        return this->length_in_msec;
    else
        return 0;
}

static uint32_t
demux_flac_get_capabilities (demux_plugin_t *this_gen) {
  lprintf("demux_flac_get_capabilities\n");

  return DEMUX_CAP_NOCAP;
}

static int
demux_flac_get_optional_data (demux_plugin_t *this_gen, void *data, int dtype) {
  lprintf("demux_flac_get_optional_data\n");

  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *
open_plugin (demux_class_t *class_gen,
             xine_stream_t *stream,
             input_plugin_t *input) {
    demux_flac_t *this;

    lprintf("open_plugin\n");

    switch (stream->content_detection_method) {
        case METHOD_BY_CONTENT:
        {
          uint8_t      buf[MAX_PREVIEW_SIZE];
          int          len;

          /*
           * try to get a preview of the data
           */
          len = input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
          if (len == INPUT_OPTIONAL_UNSUPPORTED) {

            if (input->get_capabilities (input) & INPUT_CAP_SEEKABLE) {

              input->seek (input, 0, SEEK_SET);
              if ( (len=input->read (input, buf, 1024)) <= 0)
                return NULL;
              input->seek (input, 0, SEEK_SET);

            } else
              return NULL;
          }

          /* FIXME: Skip id3v2 tag */
          /* Look for fLaC tag at the beginning of file */
          if ( (buf[0] != 'f') || (buf[1] != 'L') ||
               (buf[2] != 'a') || (buf[3] != 'C') )
            return NULL;
        }
        break;
        case METHOD_BY_MRL:
        case METHOD_EXPLICIT:
        break;
        default:
            return NULL;
        break;
    }

    /*
    * if we reach this point, the input has been accepted.
    */

    this         = calloc(1, sizeof (demux_flac_t));
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

    this->seek_flag = 0;


    /* Get a new FLAC decoder and hook up callbacks */
#ifdef LEGACY_FLAC
    this->flac_decoder = FLAC__seekable_stream_decoder_new();
    lprintf("this->flac_decoder: %p\n", this->flac_decoder);

    FLAC__seekable_stream_decoder_set_md5_checking  (this->flac_decoder, false);
    FLAC__seekable_stream_decoder_set_read_callback (this->flac_decoder,
                                                     flac_read_callback);
    FLAC__seekable_stream_decoder_set_seek_callback (this->flac_decoder,
                                                     flac_seek_callback);
    FLAC__seekable_stream_decoder_set_tell_callback (this->flac_decoder,
                                                     flac_tell_callback);
    FLAC__seekable_stream_decoder_set_length_callback (this->flac_decoder,
                                                     flac_length_callback);
    FLAC__seekable_stream_decoder_set_eof_callback  (this->flac_decoder,
                                                     flac_eof_callback);
    FLAC__seekable_stream_decoder_set_metadata_callback (this->flac_decoder,
                                                     flac_metadata_callback);
    FLAC__seekable_stream_decoder_set_write_callback (this->flac_decoder,
                                                     flac_write_callback);
    FLAC__seekable_stream_decoder_set_error_callback (this->flac_decoder,
                                                     flac_error_callback);
    FLAC__seekable_stream_decoder_set_client_data    (this->flac_decoder,
                                                     this);

    FLAC__seekable_stream_decoder_init (this->flac_decoder);
#else
    this->flac_decoder = FLAC__stream_decoder_new();
    lprintf("this->flac_decoder: %p\n", this->flac_decoder);

    if ( ! this->flac_decoder ) {
      free(this);
      return NULL;
    }

    FLAC__stream_decoder_set_md5_checking  (this->flac_decoder, false);

    if ( FLAC__stream_decoder_init_stream(this->flac_decoder,
					  flac_read_callback,
					  flac_seek_callback,
					  flac_tell_callback,
					  flac_length_callback,
					  flac_eof_callback,
					  flac_write_callback,
					  flac_metadata_callback,
					  flac_error_callback,
					  this
					  ) != FLAC__STREAM_DECODER_INIT_STATUS_OK ) {
#ifdef LEGACY_FLAC
      FLAC__seekable_stream_decoder_delete (this->flac_decoder);
#else
      FLAC__stream_decoder_delete (this->flac_decoder);
#endif
      free(this);
      return NULL;
    }
#endif

    /* Get some stream info */
    this->data_size  = this->input->get_length (this->input);
    this->data_start = this->input->get_current_pos (this->input);

    /* This will cause FLAC to give us the rest of the information on
     * this flac stream
     */
    this->status = DEMUX_OK;
#ifdef LEGACY_FLAC
    FLAC__seekable_stream_decoder_process_until_end_of_metadata (this->flac_decoder);
#else
    FLAC__stream_decoder_process_until_end_of_metadata (this->flac_decoder);
#endif

    lprintf("Processed file until end of metadata: %s\n",
	    this->status == DEMUX_OK ? "success" : "failure");

    if (this->status != DEMUX_OK) {
#ifdef LEGACY_FLAC
        FLAC__seekable_stream_decoder_delete (this->flac_decoder);
#else
	FLAC__stream_decoder_delete (this->flac_decoder);
#endif
        free (this);
        return NULL;
    }

    return &this->demux_plugin;
}


/* FLAC Demuxer class */
void *
demux_flac_init_class (xine_t *xine, void *data) {

    demux_flac_class_t     *this;

    lprintf("demux_flac_init_class\n");

    this         = calloc(1, sizeof (demux_flac_class_t));
    this->config = xine->config;
    this->xine   = xine;

    this->demux_class.open_plugin     = open_plugin;
    this->demux_class.description     = N_("FLAC demux plugin");
    this->demux_class.identifier      = "FLAC";
    this->demux_class.mimetypes       = "application/x-flac: flac: FLAC Audio;"
                                        "application/flac: flac: FLAC Audio;";
    this->demux_class.extensions      = "flac";
    this->demux_class.dispose         = default_demux_class_dispose;

    return this;
}
