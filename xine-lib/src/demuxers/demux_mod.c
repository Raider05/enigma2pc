/*
 * Copyright (C) 2000-2004 the xine project
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
 * MOD File "demuxer" by Paul Eggleton (bluelightning@bluelightning.org)
 * This is really just a loader for Amiga MOD (and similar) music files
 * which reads an entire MOD file and passes it over to the ModPlug library
 * for playback.
 *
 * This file was based on demux_nsf.c by Mike Melanson.
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
#define LOG_MODULE "demux_mod"
/* #define LOG_VERBOSE */
/* #define LOG */

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "modplug.h"
#include "bswap.h"

#define MOD_SAMPLERATE 44100
#define MOD_BITS 16
#define MOD_CHANNELS 2

#define OUT_BYTES_PER_SECOND (MOD_SAMPLERATE * MOD_CHANNELS * (MOD_BITS >> 3))

#define BLOCK_SIZE 4096


typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  char                *title;
  char                *artist;
  char                *copyright;
  size_t               filesize;

  char                *buffer;

  int64_t              current_pts;

  ModPlug_Settings     settings;
  ModPlugFile         *mpfile;
  int                  mod_length;
  int                  seek_flag;  /* this is set when a seek just occurred */

} demux_mod_t;

typedef struct {
  demux_class_t     demux_class;
} demux_mod_class_t;

#define FOURCC_32(a, b, c, d) (d + (c<<8) + (b<<16) + (a<<24))

/**
 * @brief Probes if the given file can be demuxed using modplug or not
 * @retval 0 The file is not a valid modplug file (or the probe isn't complete yet)
 * @retval 1 The file has been identified as a valid modplug file
 * @todo Just Protracker files are detected right now.
 */
static int probe_mod_file(demux_mod_t *this) {
  /* We need the value present at offset 1080, of size 4 */
  union {
    uint8_t buffer[1080+4]; /* The raw buffer */
    uint32_t values[(1080+4)/sizeof(uint32_t)];
  } header;

  if (_x_demux_read_header(this->input, header.buffer, 1080+4) != 1080+4)
      return 0;

  /* Magic numbers taken from GNU file's magic description */
  switch( _X_ABE_32(header.values + (1080/sizeof(uint32_t))) ) {
  case FOURCC_32('M', '.', 'K', '.'): /* 4-channel Protracker module sound data */
  case FOURCC_32('M', '!', 'K', '!'): /* 4-channel Protracker module sound data */
  case FOURCC_32('F', 'L', 'T', '4'): /* 4-channel Startracker module sound data */
  case FOURCC_32('F', 'L', 'T', '8'): /* 8-channel Startracker module sound data */
  case FOURCC_32('4', 'C', 'H', 'N'): /* 4-channel Fasttracker module sound data */
  case FOURCC_32('6', 'C', 'H', 'N'): /* 6-channel Fasttracker module sound data */
  case FOURCC_32('8', 'C', 'H', 'N'): /* 8-channel Fasttracker module sound data */
  case FOURCC_32('C', 'D', '8', '1'): /* 8-channel Octalyser module sound data */
  case FOURCC_32('O', 'K', 'T', 'A'): /* 8-channel Oktalyzer module sound data */
  case FOURCC_32('1', '6', 'C', 'N'): /* 16-channel Taketracker module sound data */
  case FOURCC_32('3', '2', 'C', 'N'): /* 32-channel Taketracker module sound data */
    return 1;
  }

  /* ScreamTracker 2 */
  if (!memcmp (header.buffer + 20, "!Scream!", 7))
    return 1;

  /* ScreamTracker 3 */
  if (_X_ABE_32(header.values + 0x2C / sizeof (uint32_t)) == FOURCC_32('S', 'C', 'R', 'M'))
    return 1;

  return 0;
}

/* returns 1 if the MOD file was opened successfully, 0 otherwise */
static int open_mod_file(demux_mod_t *this) {
  int total_read;
  off_t input_length;

  /* Get size and create buffer */
  input_length = this->input->get_length(this->input);
  /* Avoid potential issues with signed variables and e.g. read() returning -1 */
  if (input_length > 0x7FFFFFFF || input_length < 0) {
    xine_log(this->stream->xine, XINE_LOG_PLUGIN, "modplug - size overflow\n");
    return 0;
  }
  this->filesize = input_length;
  this->buffer = (char *)malloc(this->filesize);
  if(!this->buffer) {
    xine_log(this->stream->xine, XINE_LOG_PLUGIN, "modplug - allocation failure\n");
    return 0;
  }

  /* Seek to beginning */
  this->input->seek(this->input, 0, SEEK_SET);

  /* Read data */
  total_read = this->input->read(this->input, this->buffer, this->filesize);

  if(total_read != this->filesize) {
    xine_log(this->stream->xine, XINE_LOG_PLUGIN, "modplug - filesize error\n");
    free(this->buffer);
    return 0;
  }

  /* Set up modplug engine */
  ModPlug_GetSettings(&this->settings);
  this->settings.mResamplingMode = MODPLUG_RESAMPLE_FIR; /* RESAMP */
  this->settings.mChannels = MOD_CHANNELS;
  this->settings.mBits = MOD_BITS;
  this->settings.mFrequency = MOD_SAMPLERATE;
  ModPlug_SetSettings(&this->settings);

  this->mpfile = ModPlug_Load(this->buffer, this->filesize);
  if (this->mpfile==NULL) {
    xine_log(this->stream->xine, XINE_LOG_PLUGIN, "modplug - load error\n");
    free(this->buffer);
    return 0;
  }

  this->title = strdup(ModPlug_GetName(this->mpfile));
  this->artist = strdup("");
  this->copyright = strdup("");

  this->mod_length = ModPlug_GetLength(this->mpfile);
  if (this->mod_length < 1)
    this->mod_length = 1; /* avoids -ve & div-by-0 */

  return 1;
}

static int demux_mod_send_chunk(demux_plugin_t *this_gen) {
  demux_mod_t *this = (demux_mod_t *) this_gen;
  buf_element_t *buf;
  int mlen;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_LPCM_LE;

  mlen = ModPlug_Read(this->mpfile, buf->content, buf->max_size);
  if (mlen == 0) {
    this->status = DEMUX_FINISHED;
    buf->free_buffer(buf);
  }
  else {
    buf->size = mlen;
    buf->pts = this->current_pts;
    buf->extra_info->input_time = buf->pts / 90;

    buf->extra_info->input_normpos = buf->extra_info->input_time * 65535 / this->mod_length;
    buf->decoder_flags = BUF_FLAG_FRAME_END;

    if (this->seek_flag) {
      _x_demux_control_newpts(this->stream, buf->pts, BUF_FLAG_SEEK);
      this->seek_flag = 0;
    }

    this->audio_fifo->put (this->audio_fifo, buf);

    this->current_pts += 90000 * mlen / OUT_BYTES_PER_SECOND;
  }

  return this->status;
}

static void demux_mod_send_headers(demux_plugin_t *this_gen) {
  demux_mod_t *this = (demux_mod_t *) this_gen;
  buf_element_t *buf;
  char copyright[100];

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, MOD_CHANNELS);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, MOD_SAMPLERATE);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, MOD_BITS);

  _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->title);
  _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->artist);
  snprintf(copyright, 100, "(C) %s", this->copyright);
  _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, copyright);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to the audio decoder */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_LPCM_LE;
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = MOD_SAMPLERATE;
  buf->decoder_info[2] = MOD_BITS;
  buf->decoder_info[3] = MOD_CHANNELS;
  buf->size = 0;
  this->audio_fifo->put (this->audio_fifo, buf);
}

static int demux_mod_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_mod_t *this = (demux_mod_t *) this_gen;
  int64_t seek_millis;

  if (start_pos) {
    seek_millis = this->mod_length;
    seek_millis *= start_pos;
    seek_millis /= 65535;
  } else {
    seek_millis = start_time;
  }

  _x_demux_flush_engine(this->stream);
  ModPlug_Seek(this->mpfile, seek_millis);
  this->current_pts = seek_millis * 90;

  this->seek_flag = 1;
  return this->status;
}

static void demux_mod_dispose (demux_plugin_t *this_gen) {
  demux_mod_t *this = (demux_mod_t *) this_gen;

  ModPlug_Unload(this->mpfile);
  free(this->buffer);
  free(this->title);
  free(this->artist);
  free(this->copyright);
  free(this);
}

static int demux_mod_get_status (demux_plugin_t *this_gen) {
  demux_mod_t *this = (demux_mod_t *) this_gen;
  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_mod_get_stream_length (demux_plugin_t *this_gen) {
  demux_mod_t *this = (demux_mod_t *) this_gen;
  return ModPlug_GetLength(this->mpfile);
}

static uint32_t demux_mod_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mod_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_mod_t   *this;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_mod_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mod_send_headers;
  this->demux_plugin.send_chunk        = demux_mod_send_chunk;
  this->demux_plugin.seek              = demux_mod_seek;
  this->demux_plugin.dispose           = demux_mod_dispose;
  this->demux_plugin.get_status        = demux_mod_get_status;
  this->demux_plugin.get_stream_length = demux_mod_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mod_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mod_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "TEST mod decode\n");

  switch (stream->content_detection_method) {

  case METHOD_EXPLICIT:
  case METHOD_BY_MRL:
  break;

  case METHOD_BY_CONTENT:
    if (probe_mod_file(this) && open_mod_file(this))
      break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static void *demux_mod_init_plugin (xine_t *xine, void *data) {
  demux_mod_class_t     *this;

  this = calloc(1, sizeof(demux_mod_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("ModPlug Amiga MOD Music file demux plugin");
  this->demux_class.identifier      = "mod";
  this->demux_class.mimetypes       =
	 "audio/x-mod: mod: SoundTracker/NoiseTracker/ProTracker Module;"
         "audio/mod: mod: SoundTracker/NoiseTracker/ProTracker Module;"
         "audio/it: it: ImpulseTracker Module;"
         "audio/x-it: it: ImpulseTracker Module;"
         "audio/x-stm: stm: ScreamTracker 2 Module;"
         "audio/x-s3m: s3m: ScreamTracker 3 Module;"
         "audio/s3m: s3m: ScreamTracker 3 Module;"
         "application/playerpro: 669: 669 Tracker Module;"
         "application/adrift: amf: ADRIFT Module File;"
         "audio/med: med: Amiga MED/OctaMED Tracker Module Sound File;"
         "audio/x-amf: amf: ADRIFT Module File;"
         "audio/x-xm: xm: FastTracker II Audio;"
         "audio/xm: xm: FastTracker II Audio;";
  this->demux_class.extensions      = "mod it stm s3m 669 amf med mdl xm";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

static const demuxer_info_t demux_info_mod = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  { PLUGIN_DEMUX, 27, "modplug", XINE_VERSION_CODE, &demux_info_mod, demux_mod_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
