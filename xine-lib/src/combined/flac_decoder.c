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
 * John McCutchan 2003
 * FLAC Decoder (http://flac.sf.net)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <FLAC/stream_decoder.h>

#if !defined FLAC_API_VERSION_CURRENT || FLAC_API_VERSION_CURRENT < 8
#include <FLAC/seekable_stream_decoder.h>
#define LEGACY_FLAC
#else
#undef LEGACY_FLAC
#endif

#define LOG_MODULE "flac_decoder"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>

typedef struct {
  audio_decoder_class_t   decoder_class;
} flac_class_t;

typedef struct flac_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  xine_stream_t    *stream;

  FLAC__StreamDecoder *flac_decoder;

  unsigned char *buf;
  size_t         buf_size;
  size_t         buf_pos;
  size_t         min_size;

  int            output_open;

} flac_decoder_t;

/*
 * FLAC callback functions
 */

#ifdef LEGACY_FLAC
static FLAC__StreamDecoderReadStatus
flac_read_callback (const FLAC__StreamDecoder *decoder,
                    FLAC__byte buffer[],
                    unsigned *bytes,
                    void *client_data)
#else
static FLAC__StreamDecoderReadStatus
flac_read_callback (const FLAC__StreamDecoder *decoder,
                    FLAC__byte buffer[],
                    size_t *bytes,
                    void *client_data)
#endif
{
    flac_decoder_t *this = (flac_decoder_t *)client_data;
    size_t number_of_bytes_to_copy;

    lprintf("flac_read_callback: %zd\n", (size_t)*bytes);

    if (this->buf_pos > *bytes)
        number_of_bytes_to_copy = *bytes;
    else
        number_of_bytes_to_copy = this->buf_pos;

    lprintf("number_of_bytes_to_copy: %zd\n", number_of_bytes_to_copy);

    *bytes = number_of_bytes_to_copy;

    xine_fast_memcpy (buffer, this->buf, number_of_bytes_to_copy);

    this->buf_pos -= number_of_bytes_to_copy;
    memmove(this->buf, &this->buf[number_of_bytes_to_copy], this->buf_pos );

    if(number_of_bytes_to_copy)
      return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    else
      return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderWriteStatus
flac_write_callback (const FLAC__StreamDecoder *decoder,
                     const FLAC__Frame *frame,
                     const FLAC__int32 *const buffer[],
                     void *client_data)
{
    flac_decoder_t *this = (flac_decoder_t *)client_data;
    audio_buffer_t *audio_buffer = NULL;
    int samples_left = frame->header.blocksize;
    int bytes_per_sample = (frame->header.bits_per_sample <= 8) ? 1 : 2;
    int buf_samples;
    int8_t *data8;
    int16_t *data16;
    int i,j;

    lprintf("flac_write_callback\n");

    while( samples_left ) {

      audio_buffer = this->stream->audio_out->get_buffer(this->stream->audio_out);

      if( audio_buffer->mem_size < samples_left * frame->header.channels * bytes_per_sample )
        buf_samples = audio_buffer->mem_size / (frame->header.channels * bytes_per_sample);
      else
        buf_samples = samples_left;

      switch (frame->header.bits_per_sample) {
      case 8:
        data8 = (int8_t *)audio_buffer->mem;

        for( j=0; j < buf_samples; j++ )
          for( i=0; i < frame->header.channels; i++ )
            *data8++ = buffer[i][j];
        break;

      case 16:
        data16 = (int16_t *)audio_buffer->mem;

        for( j=0; j < buf_samples; j++ )
          for( i=0; i < frame->header.channels; i++ )
            *data16++ = buffer[i][j];
        break;

      case 24:
        data16 = (int16_t *)audio_buffer->mem;

        for( j=0; j < buf_samples; j++ )
          for( i=0; i < frame->header.channels; i++ )
            *data16++ = buffer[i][j] >> 8;
        break;

      }

      audio_buffer->num_frames = buf_samples;
      audio_buffer->vpts = this->pts;
      this->pts = 0;
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      samples_left -= buf_samples;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

#ifdef LEGACY_FLAC
static void
flac_metadata_callback (const FLAC__StreamDecoder *decoder,
                        const FLAC__StreamMetadata *metadata,
                        void *client_data)
{
  /* flac_decoder_t *this = (flac_decoder_t *)client_data; */

  lprintf("Metadata callback called!\n");

#ifdef LOG
  if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    printf("libflac: min_blocksize = %d\n", metadata->data.stream_info.min_blocksize);
    printf("libflac: max_blocksize = %d\n", metadata->data.stream_info.max_blocksize);
    printf("libflac: min_framesize = %d\n", metadata->data.stream_info.min_framesize);
    printf("libflac: max_framesize = %d\n", metadata->data.stream_info.max_framesize);
    /* does not work well:
       this->min_size = 2 * metadata->data.stream_info.max_blocksize; */
  }
#endif

  return;
}
#endif

static void
flac_error_callback (const FLAC__StreamDecoder *decoder,
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data)
{
  /* This will be called if there is an error in the flac stream */
  lprintf("flac_error_callback\n");

#ifdef LOG
  if (status == FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
    printf("libflac: Decoder lost synchronization.\n");
  else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER)
    printf("libflac: Decoder encounted a corrupted frame header.\n");
  else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH)
    printf("libflac: Frame's data did not match the CRC in the footer.\n");
  else
    printf("libflac: unknown error.\n");
#endif

    return;
}


/*
 * FLAC plugin decoder
 */

static void
flac_reset (audio_decoder_t *this_gen)
{
  flac_decoder_t *this = (flac_decoder_t *) this_gen;

  this->buf_pos = 0;

  if( FLAC__stream_decoder_get_state(this->flac_decoder) !=
                FLAC__STREAM_DECODER_SEARCH_FOR_METADATA )
    FLAC__stream_decoder_flush (this->flac_decoder);
}

static void
flac_discontinuity (audio_decoder_t *this_gen)
{
  flac_decoder_t *this = (flac_decoder_t *) this_gen;

  this->pts = 0;

  lprintf("Discontinuity!\n");
}

static void
flac_decode_data (audio_decoder_t *this_gen, buf_element_t *buf)
{
    flac_decoder_t *this = (flac_decoder_t *) this_gen;
    int ret = 1;

    /* We are getting the stream header, open up the audio
     * device, and collect information about the stream
     */
    if (buf->decoder_flags & BUF_FLAG_STDHEADER)
    {
        const int sample_rate     = buf->decoder_info[1];
        const int bits_per_sample = buf->decoder_info[2];
        const int channels        = buf->decoder_info[3];
        const int mode            = _x_ao_channels2mode(channels);

        if (!this->output_open)
        {
            const int bits = bits_per_sample;
            this->output_open = (this->stream->audio_out->open) (
                                            this->stream->audio_out,
                                            this->stream,
                                            bits > 16 ? 16 : bits,
                                            sample_rate,
                                            mode);
        }
        this->buf_pos = 0;
    } else if (this->output_open)
    {
        /* This isn't a header frame and we have opened the output device */


        /* What we have buffered so far, and what is coming in
         * is larger than our buffer
         */
        if (this->buf_pos + buf->size > this->buf_size)
        {
            this->buf_size += 2 * buf->size;
            this->buf = realloc (this->buf, this->buf_size);
            lprintf("reallocating buffer to %d\n", this->buf_size);
        }

        xine_fast_memcpy (&this->buf[this->buf_pos], buf->content, buf->size);
        this->buf_pos += buf->size;

        if (buf->pts)
          this->pts = buf->pts;

        /* We have enough to decode a frame */
        while( ret && this->buf_pos > this->min_size ) {

            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(this->flac_decoder);

            if( state ==  FLAC__STREAM_DECODER_SEARCH_FOR_METADATA ) {
              lprintf("process_until_end_of_metadata\n");
              ret = FLAC__stream_decoder_process_until_end_of_metadata (this->flac_decoder);
            } else if ( state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC ||
                        state == FLAC__STREAM_DECODER_READ_FRAME ) {
              lprintf("process_single\n");
              ret = FLAC__stream_decoder_process_single (this->flac_decoder);
            } else {
              lprintf("aborted.\n");
              FLAC__stream_decoder_flush (this->flac_decoder);
              break;
            }
        }
    } else
        return;


}

static void
flac_dispose (audio_decoder_t *this_gen) {
    flac_decoder_t *this = (flac_decoder_t *) this_gen;

    FLAC__stream_decoder_finish (this->flac_decoder);

    FLAC__stream_decoder_delete (this->flac_decoder);

    if (this->output_open)
        this->stream->audio_out->close (this->stream->audio_out, this->stream);

    if (this->buf)
      free(this->buf);

    free (this_gen);
}

static audio_decoder_t *
open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {
    flac_decoder_t *this ;

    this = calloc(1, sizeof (flac_decoder_t));

    this->audio_decoder.decode_data         = flac_decode_data;
    this->audio_decoder.reset               = flac_reset;
    this->audio_decoder.discontinuity       = flac_discontinuity;
    this->audio_decoder.dispose             = flac_dispose;
    this->stream                            = stream;

    this->output_open     = 0;
    this->buf      = NULL;
    this->buf_size = 0;
    this->min_size = 65536;
    this->pts      = 0;

    this->flac_decoder = FLAC__stream_decoder_new();

#ifdef LEGACY_FLAC
    FLAC__stream_decoder_set_read_callback     (this->flac_decoder,
                                                flac_read_callback);
    FLAC__stream_decoder_set_write_callback    (this->flac_decoder,
                                                flac_write_callback);
    FLAC__stream_decoder_set_metadata_callback (this->flac_decoder,
                                                flac_metadata_callback);
    FLAC__stream_decoder_set_error_callback    (this->flac_decoder,
                                                flac_error_callback);

    FLAC__stream_decoder_set_client_data (this->flac_decoder, this);

    if (FLAC__stream_decoder_init (this->flac_decoder) != FLAC__STREAM_DECODER_SEARCH_FOR_METADATA) {
	    free (this);
	    return NULL;
    }
#else
    if ( FLAC__stream_decoder_init_stream (this->flac_decoder,
					   flac_read_callback,
					   NULL, /* seek */
					   NULL, /* tell */
					   NULL, /* length */
					   NULL, /* eof */
					   flac_write_callback,
					   NULL, /* metadata */
					   flac_error_callback,
					   this
					   ) != FLAC__STREAM_DECODER_INIT_STATUS_OK ) {
	    free (this);
	    return NULL;
    }
#endif

    return (audio_decoder_t *) this;
}

/*
 * flac plugin class
 */
static void *
init_plugin (xine_t *xine, void *data) {
    flac_class_t *this;

    this = calloc(1, sizeof (flac_class_t));

    this->decoder_class.open_plugin     = open_plugin;
    this->decoder_class.identifier      = "flacdec";
    this->decoder_class.description     = N_("flac audio decoder plugin");
    this->decoder_class.dispose         = default_audio_decoder_class_dispose;


    return this;
}

void *demux_flac_init_class (xine_t *xine, void *data);

static const uint32_t audio_types[] = {
  BUF_AUDIO_FLAC, 0
 };

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  8                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "flac", XINE_VERSION_CODE, NULL, demux_flac_init_class },
  { PLUGIN_AUDIO_DECODER, 16, "flacdec", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
