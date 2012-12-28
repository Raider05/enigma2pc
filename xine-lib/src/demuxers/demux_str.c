/*
 * Copyright (C) 2000-2008 the xine project
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
 * STR File Demuxer by Mike Melanson (melanson@pcisys.net)
 *                  and Stuart Caie (kyzer@4u.net)
 * This demuxer handles either raw STR files (which are just a concatenation
 * of raw compact disc sectors) or STR files with RIFF headers.
 */

/*
 * CD-XA format:
 *
 * - the format is a series of 2352 byte CD sectors
 *   - 0x000:   12 bytes: sync header (00 FF FF FF FF FF FF FF FF FF FF 00)
 *   - 0x00C:    4 bytes: timecode (mm ss ff 02; BCD, not decimal!)
 *   - 0x010:    4 bytes: sector parameters
 *                        - 0x10 file_num
 *                        - 0x11 channel_num
 *                        - 0x12 subcode
 *                        - 0x13 coding_info
 *   - 0x014:    4 bytes: copy of parameters (should be identical)
 *   - 0x018: 2324 bytes: sector data
 *   - 0x92C:    4 bytes: EDC error correction code
 *   - 0x930: SIZEOF
 *
 *   - file_num is purely to distinguish where a 'file' ends and a new
 *     'file' begins among the sectors. It's usually 1.
 *   - channel_num is a sub-channel in this 'file'. Video, audio and data
 *     sectors can be mixed into the same channel or can be on seperate
 *     channels. Usually used for multiple audio tracks (e.g. 5 different
 *     songs in the same 'file', on channels 0, 1, 2, 3 and 4)
 *   - subcode is a set of bits
 *     - bit 7: eof_marker -- 0, or 1 if this sector is the end of the 'file'
 *     - bit 6: real_time  -- unimportant (always set in PSX STR streams)
 *     - bit 5: form       -- unimportant
 *     - bit 4: trigger    -- for use by reader application (unimportant)
 *     - bit 3: DATA       -- set to 1 to indicate DATA  sector, otherwise 0
 *     - bit 2: AUDIO      -- set to 1 to indicate AUDIO sector, otherwise 0
 *     - bit 1: VIDEO      -- set to 1 to indicate VIDEO sector, otherwise 0
 *     - bit 0: end_audio  -- end of audio frame (never set in PSX STR streams)
 *     - bits 1, 2 and 3 are mutually exclusive
 *   - coding_info is a set of bits, interpretation is dependant on the
 *     DATA/AUDIO/VIDEO bits setting of subcode.
 *     - For AUDIO:
 *       - bit 7: reserved -- should always be 0
 *       - bit 6: emphasis -- boost audio volume (ignored by us)
 *       - bit 5: bitssamp -- must always be 0
 *       - bit 4: bitssamp -- 0 for mode B/C (4 bits/sample, 8 sound sectors)
 *                            1 for mode A   (8 bits/sample, 4 sound sectors)
 *       - bit 3: samprate -- must always be 0
 *       - bit 2: samprate -- 0 for 37.8kHz playback, 1 for 18.9kHz playback
 *       - bit 1: stereo   -- must always be 0
 *       - bit 0: stereo   -- 0 for mono sound, 1 for stereo sound
 *     - For DATA or VIDEO:
 *       - always seems to be 0 in PSX STR files
 *
 * Format of sector data in AUDIO sectors:
 * - 18 "sound groups" of 128 byte structures
 * - 20 unused bytes
 * - we pass these 18*128 bytes to the XA_ADPCM audio decoder
 *
 * Format of sector data in DATA or VIDEO sectors:
 * - all values are little-endian
 * - 0x00: 32 bits; unknown -- usually 0x80010160 for a video frame.
 *                  according to PSX hardware guide, this value is written
 *                  to mdec0 register:
 *                  - bit 27: 1 for 16-bit colour, 0 for 24-bit colour depth
 *                  - bit 24: if 16-bit colour, 1/0=set/clear transparency bit
 *                  - all other bits unknown
 *         - if not set to this value, it's not a video sector.
 * - 0x04: 16 bits; 'chunk number' of this video frame (0 to numchunks-1)
 * - 0x06: 16 bits; number of chunks in this frame
 * - 0x08: 32 bits; frame number (1 to ...)
 * - 0x0C: 32 bits; seemingly random number. frame duration?
 * - 0x10: 16 bits; width of frame in pixels
 * - 0x12: 16 bits; height of frame in pixels
 * - remainder of data (up to 2304 bytes): compressed MDEC stream
 *   - 32 bits: (0x3800 << 16) | size of data (in bytes) following this header
 *   - any number of macroblocks (which each represent a 16x16 pixel area)
 *     - a macroblock is 6 blocks (Cb, Cr, Y0, Y1, Y2 and Y3)
 *       - a block is a DCT setting then an RLE data stream
 *       - 16 bits: DCT (bits 15-10: quantisation factor (unsigned)
 *                       bits 9-0: Direct Current reference (signed))
 *       - then follows 16-bit RLE data until the EOD
 *         - RLE format: bits 15-10: # of 0s preceding this value (unsigned)
 *                       bits 9-0: this value (signed)
 *         - e.g. 3 bytes (2,10)(0,20)(3,30) -> 0 0 10 20 0 0 0 30
 *       - 16 bits: EOD (0xFE00)
 *   - 16 bits: 0xFE00 end-of-data footer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_str"
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

/* There may be a RIFF/CDXA header at the beginning of the file, which
 * accounts for 0x2C bytes.  We need at most 0x30 bytes of the sector to
 * verify whether it's a CDXA/MDEC file
 */

#define STR_CHECK_BYTES (0x2C + 0x30)

#define CD_RAW_SECTOR_SIZE 2352

#define STR_MAGIC "\x60\x01\x01\x80"
#define STR_MAX_CHANNELS 32

#define CDXA_TYPE_MASK     0x0E
#define CDXA_TYPE_DATA     0x08
#define CDXA_TYPE_AUDIO    0x04
#define CDXA_TYPE_VIDEO    0x02
#define CDXA_SUBMODE_EOF   0x80 /* set if EOF */

/* FIXME */
#define FRAME_DURATION 45000

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_start;
  off_t                data_size;
  off_t                current_pos;

  xine_bmiheader       bih[STR_MAX_CHANNELS];
  unsigned char        audio_info[STR_MAX_CHANNELS];
  unsigned char        channel_type[STR_MAX_CHANNELS];
  int64_t              audio_pts[STR_MAX_CHANNELS];

  int                  seek_flag;
  int                  default_video_channel;
} demux_str_t;

typedef struct {
  demux_class_t     demux_class;
} demux_str_class_t;


/* returns 1 if the STR file was opened successfully, 0 otherwise */
static int open_str_file(demux_str_t *this) {
  unsigned char check_bytes[STR_CHECK_BYTES];
  int local_offset, sector, channel;

  memset(this->channel_type, 0, sizeof(this->channel_type));

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, check_bytes, STR_CHECK_BYTES) !=
      STR_CHECK_BYTES) {
    lprintf("read error\n");
    return 0;
  }

  /* check for STR with a RIFF header */
  if ( _x_is_fourcc(&check_bytes[0], "RIFF") &&
       _x_is_fourcc(&check_bytes[8], "CDXA") )
    local_offset = 0x2C;
  else
    local_offset = 0;

  this->data_start = (off_t) local_offset;

  /* we need to check up to 32 sectors for up to 32 audio/video channels */
  for (sector = 0; sector < STR_MAX_CHANNELS; sector++) {

    lprintf("file=%d channel=%-2d submode=%02x coding_info=%02x\n",
            check_bytes[local_offset + 0x10],
            check_bytes[local_offset + 0x11],
            check_bytes[local_offset + 0x12],
            check_bytes[local_offset + 0x13]);

    /* check for 12-byte sync marker */
    static const uint8_t sync_marker[12] =
      { 0x00, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0X00
      };
    if ( memcmp(&check_bytes[local_offset],
		sync_marker, sizeof(sync_marker)) != 0 ) {
      lprintf("sector %d sync error\n", sector);
      return 0;
    }

    /* the 32 bits starting at 0x10 and at 0x14 should be the same */
    if (memcmp(&check_bytes[local_offset + 0x10],
	       &check_bytes[local_offset + 0x14], 4) != 0) {
      lprintf("sector %d control bits copy error\n", sector);
      return 0;
    }

    /* channel should be from 0 to 31 */
    channel = check_bytes[local_offset + 0x11];
    if (channel >= STR_MAX_CHANNELS) {
      lprintf("sector %d channel %d error\n", sector, channel);
      return 0;
    }

    /* switch on the sector type */
    switch(check_bytes[local_offset + 0x12] & CDXA_TYPE_MASK) {

    case CDXA_TYPE_DATA:
    case CDXA_TYPE_VIDEO:
      /* first time we have seen video/data in this channel? */
      if ((!(this->channel_type[channel] & CDXA_TYPE_DATA)) &&
	  (_x_is_fourcc(&check_bytes[local_offset + 0x18], STR_MAGIC))) {

	/* mark this channel as having video data */
	this->channel_type[channel] |= CDXA_TYPE_VIDEO;

	this->bih[channel].biWidth =
	  _X_LE_16(&check_bytes[local_offset + 0x18 + 0x10]);
	this->bih[channel].biHeight =
	  _X_LE_16(&check_bytes[local_offset + 0x18 + 0x12]);
      }
      break;

    case CDXA_TYPE_AUDIO:
      /* first time we have seen audio in this channel? */
      if (!(this->channel_type[channel] & CDXA_TYPE_AUDIO)) {

	/* mark this channel as having audio data */
	this->channel_type[channel] |= CDXA_TYPE_AUDIO;

	this->audio_info[channel] = check_bytes[local_offset + 0x13];
      }
      break;

    default:
      lprintf("sector %d channel %d unknown type error\n", sector, channel);
      /* several films (e.g. 37xa16.xap in Strider 1) have empty
       * sectors with 0 as the type, despite having plenty of
       * video/audio sectors
       */
       /*return 0*/;
    }

    /* seek to the next sector and read in the header */
    local_offset = 0;
    this->input->seek(this->input, this->data_start +
		      ((sector+1) * CD_RAW_SECTOR_SIZE), SEEK_SET);
    if (this->input->read(this->input, check_bytes, 0x30) != 0x30) {
      lprintf("sector %d read error\n", sector);
      return 0;
    }
  }

  if(this->channel_type[0] == 0)
    return 0;

  /* acceptable STR file */
  this->data_size = this->input->get_length(this->input) - this->data_start;

#ifdef LOG
  for (channel = 0; channel < STR_MAX_CHANNELS; channel++) {
    char vidinfo[22]; /* "Video (XXXXX x XXXXX)" */
    char audinfo[33]; /* "Audio (XX.XkHz XXXXXX, mode XXX)" */
    if (this->channel_type[channel]) {
      if (this->channel_type[channel] & CDXA_TYPE_VIDEO) {
	snprintf(vidinfo, 22, "Video (%d x %d)",
		 this->bih[channel].biWidth,
		 this->bih[channel].biHeight);
      }
      else {
	strcpy(vidinfo, "No video");
      }
      if (this->channel_type[channel] & CDXA_TYPE_AUDIO) {
	snprintf(audinfo, 33, "Audio (%skHz %s, mode %s)",
		 this->audio_info[channel] & 0x04 ? "18.9" : "37.8",
		 this->audio_info[channel] & 0x01 ? "stereo" : "mono",
		 this->audio_info[channel] & 0x10 ? "A" : "B/C");
      }
      else {
	strcpy(audinfo, "No audio");
      }
      lprintf("channel %-2d %-22s%s\n", channel, vidinfo, audinfo);
    }
  }
#endif

  return 1;
}

static int demux_str_send_chunk(demux_plugin_t *this_gen) {
  demux_str_t *this = (demux_str_t *) this_gen;
  unsigned char sector[CD_RAW_SECTOR_SIZE], channel;
  uint32_t frame_number;
  buf_element_t *buf;
  off_t current_pos;

  current_pos = this->current_pos;
  this->current_pos += CD_RAW_SECTOR_SIZE;
  if (this->input->read(this->input, sector, CD_RAW_SECTOR_SIZE) !=
      CD_RAW_SECTOR_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  channel = sector[0x11];
  if (channel >= STR_MAX_CHANNELS) return 0;

  switch (sector[0x12] & CDXA_TYPE_MASK) {
  case CDXA_TYPE_VIDEO:
  case CDXA_TYPE_DATA:
    /* video chunk */

    if (!_x_is_fourcc(&sector[0x18], STR_MAGIC) ||
	channel != this->default_video_channel) {
      return 0;
    }

    frame_number = _X_LE_32(&sector[0x18 + 0x08]);
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->pts = frame_number * FRAME_DURATION;

    if (this->seek_flag) {
      _x_demux_control_newpts(this->stream, buf->pts, BUF_FLAG_SEEK);
      this->seek_flag = 0;
    }

    /* first chunk of frame? sync forthcoming audio packets */
    /* FIXME */
    /*if (_X_LE_16(&sector[0x18+0x04]) == 0) {
     *  int i;
     *  for (i = 0; i < STR_MAX_CHANNELS; i++) this->audio_pts[i] = buf->pts;
     *}
     */

    if( this->data_size )
      buf->extra_info->input_normpos = (int)( (double) current_pos *
                                     65535 / this->data_size );
    buf->extra_info->input_time = (current_pos*1000)/(CD_RAW_SECTOR_SIZE*75);

    /* constant size chunk */
    buf->size = 2304;
    xine_fast_memcpy(buf->content, &sector[0x18+0x14], 2304);

    /* entirely intracoded */
    buf->decoder_flags |= BUF_FLAG_KEYFRAME;

    /* if the current chunk is 1 less than the chunk count, this is the
     * last chunk of the frame */
    if ((_X_LE_16(&sector[0x18+0x04]) + 1) == _X_LE_16(&sector[0x18+0x06]))
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    buf->type = BUF_VIDEO_PSX_MDEC | channel;
    this->video_fifo->put(this->video_fifo, buf);
    break;

  case CDXA_TYPE_AUDIO:
    /* audio frame */

    if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

      buf->pts = this->audio_pts[channel];
      this->audio_pts[channel] +=
	(((sector[0x13] & 0x10) ? 2016 : 4032) *
	 ((sector[0x13] & 0x01) ? 45000 : 90000)) /
	((sector[0x13] & 0x04) ? 18900 : 37800);

      if (this->seek_flag) {
	_x_demux_control_newpts(this->stream, buf->pts, BUF_FLAG_SEEK);
	this->seek_flag = 0;
      }

      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double) current_pos *
                                     65535 / this->data_size );
      buf->extra_info->input_time = (current_pos*1000)/(CD_RAW_SECTOR_SIZE*75);

      buf->size = 2304;
      xine_fast_memcpy(buf->content, &sector[0x18], 2304);

      buf->type = BUF_AUDIO_XA_ADPCM | channel;
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
    break;
  }

  return this->status;
}

static void demux_str_send_headers(demux_plugin_t *this_gen) {
  demux_str_t *this = (demux_str_t *) this_gen;
  buf_element_t *buf;
  char audio_info;
  int channel, video_channel = -1;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_SEEKABLE, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);

  for (channel = 0; channel < STR_MAX_CHANNELS; channel++) {
    if (this->channel_type[channel] & CDXA_TYPE_VIDEO) {
      if (video_channel == -1) {
	/* FIXME: until I figure out how to comfortably let the user
	 * pick a video channel, just pick a single video channel */
	video_channel = this->default_video_channel = channel;
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);

	_x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
	                     this->bih[channel].biWidth);
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
	                     this->bih[channel].biHeight);

	/* send init info to video decoder */
	buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                             BUF_FLAG_FRAME_END;
	buf->decoder_info[0] = FRAME_DURATION;  /* initial video_step */
	buf->size = sizeof(xine_bmiheader);
	memcpy(buf->content, &this->bih[channel], buf->size);
	buf->type = BUF_VIDEO_PSX_MDEC;
	this->video_fifo->put (this->video_fifo, buf);
      }
    }

    if (this->channel_type[channel] & CDXA_TYPE_AUDIO) {
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

      audio_info = this->audio_info[channel];
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
	                   (audio_info & 0x01) ? 2 : 1);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
	                   (audio_info & 0x04) ? 18900 : 37800);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, 16);

      /* send init info to the audio decoder */
      if (this->audio_fifo) {
	buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	buf->type = BUF_AUDIO_XA_ADPCM | channel;
	buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
	buf->decoder_info[0] = 0;
	buf->decoder_info[1] = (audio_info & 0x04) ? 18900 : 37800;
	buf->decoder_info[2] = (audio_info & 0x10) ? 1 : 0;
	buf->decoder_info[3] = (audio_info & 0x01) ? 2 : 1;
	this->audio_fifo->put (this->audio_fifo, buf);

	this->audio_pts[channel] = 0;
      }
    }
  }
}

static int demux_str_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing) {
  demux_str_t *this = (demux_str_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  _x_demux_flush_engine (this->stream);

  /* round to ensure we start on a sector */
  start_pos /= CD_RAW_SECTOR_SIZE;
  lprintf("seeking to sector %d (%02d:%02d)\n",
    (int)start_pos, (int)start_pos / (60*75),
    ((int)start_pos / 75)%60);

  /* reposition at the chosen sector */
  this->current_pos = start_pos * CD_RAW_SECTOR_SIZE;
  this->input->seek(this->input, this->data_start+this->current_pos, SEEK_SET);
  this->seek_flag = 1;
  this->status = DEMUX_OK;

  return this->status;
}

static int demux_str_get_status (demux_plugin_t *this_gen) {
  demux_str_t *this = (demux_str_t *) this_gen;

  return this->status;
}

static int demux_str_get_stream_length (demux_plugin_t *this_gen) {
  demux_str_t *this = (demux_str_t *) this_gen;

  return (int)((int64_t) this->input->get_length(this->input)
                * 1000 / (CD_RAW_SECTOR_SIZE * 75));
}

static uint32_t demux_str_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_str_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_str_t    *this;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this         = calloc(1, sizeof(demux_str_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_str_send_headers;
  this->demux_plugin.send_chunk        = demux_str_send_chunk;
  this->demux_plugin.seek              = demux_str_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_str_get_status;
  this->demux_plugin.get_stream_length = demux_str_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_str_get_capabilities;
  this->demux_plugin.get_optional_data = demux_str_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_str_file(this)) {
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

void *demux_str_init_plugin (xine_t *xine, void *data) {
  demux_str_class_t     *this;

  this = calloc(1, sizeof(demux_str_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Sony Playstation STR file demux plugin");
  this->demux_class.identifier      = "PSX STR";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "str iki ik2 dps dat xa xa1 xa2 xas xap";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}
