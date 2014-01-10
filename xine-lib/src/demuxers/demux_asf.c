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
 *
 * demultiplexer for asf streams
 *
 * based on ffmpeg's
 * ASF compatible encoder and decoder.
 * Copyright (c) 2000, 2001 Gerard Lantau.
 *
 * GUID list from avifile
 * some other ideas from MPlayer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#define LOG_MODULE "demux_asf"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/demux.h>
#include <xine/xineutils.h>
#include "bswap.h"
#include "asfheader.h"
#include <xine/xmlparser.h>

#define CODEC_TYPE_AUDIO          0
#define CODEC_TYPE_VIDEO          1
#define CODEC_TYPE_CONTROL        2
#define MAX_NUM_STREAMS          23

#define DEFRAG_BUFSIZE        65536

#define WRAP_THRESHOLD     20*90000
#define MAX_FRAME_DUR         90000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

#define ASF_MODE_NORMAL            0
#define ASF_MODE_ASX_REF           1
#define ASF_MODE_HTTP_REF          2
#define ASF_MODE_ASF_REF           3
#define ASF_MODE_ENCRYPTED_CONTENT 4
#define ASF_MODE_NO_CONTENT        5

typedef struct {
  int                 seq;

  int                 frag_offset;
  int64_t             timestamp;
  int                 ts_per_kbyte;
  int                 defrag;

  uint32_t            buf_type;
  int                 stream_id;
  fifo_buffer_t      *fifo;

  uint8_t            *buffer;
  int                 skip;
  int                 resync;
  int                 first_seq;

  int                 payload_size;

  /* palette handling */
  int                  palette_count;
  palette_entry_t      palette[256];

} asf_demux_stream_t;

typedef struct demux_asf_s {
  demux_plugin_t     demux_plugin;

  xine_stream_t     *stream;

  fifo_buffer_t     *audio_fifo;
  fifo_buffer_t     *video_fifo;

  input_plugin_t    *input;

  int64_t            keyframe_ts;
  int                keyframe_found;

  int                seqno;
  uint32_t           packet_size;
  uint8_t            packet_len_flags;
  uint32_t           data_size;
  uint64_t           packet_count;

  asf_demux_stream_t streams[MAX_NUM_STREAMS];
  int                video_stream;
  int                audio_stream;

  int64_t            length;
  uint32_t           rate;

  /* packet filling */
  int                packet_size_left;

  /* discontinuity detection */
  int64_t            last_pts[2];
  int                send_newpts;

  /* only for reading */
  uint32_t           packet_padsize;
  int                nb_frames;
  uint8_t            frame_flag;
  uint8_t            packet_prop_flags;
  int                frame;

  int                status;

  /* byte reordering from audio streams */
  int                reorder_h;
  int                reorder_w;
  int                reorder_b;

  int                buf_flag_seek;

  /* first packet position */
  int64_t            first_packet_pos;

  int                mode;

  /* for fewer error messages */
  GUID               last_unknown_guid;

  asf_header_t      *asf_header;

} demux_asf_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_asf_class_t;


static uint8_t get_byte (demux_asf_t *this) {

  uint8_t buf;
  int     i;

  i = this->input->read (this->input, &buf, 1);

  /* printf ("%02x ", buf); */

  if (i != 1) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return buf;
}

static uint16_t get_le16 (demux_asf_t *this) {

  uint8_t buf[2];
  int     i;

  i = this->input->read (this->input, buf, 2);

  /* printf (" [%02x %02x] ", buf[0], buf[1]); */

  if (i != 2) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return _X_LE_16(buf);
}

static uint32_t get_le32 (demux_asf_t *this) {

  uint8_t buf[4];
  int     i;

  i = this->input->read (this->input, buf, 4);

  /* printf ("%02x %02x %02x %02x ", buf[0], buf[1], buf[2], buf[3]); */

  if (i != 4) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return _X_LE_32(buf);
}

static uint64_t get_le64 (demux_asf_t *this) {

  uint8_t buf[8];
  int     i;

  i = this->input->read (this->input, buf, 8);

  if (i != 8) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return _X_LE_64(buf);
}

static int get_guid_id (demux_asf_t *this, GUID *g) {
  int i;

  for (i = 1; i < GUID_END; i++) {
    if (!memcmp(g, &guids[i].guid, sizeof(GUID))) {
      lprintf ("GUID: %s\n", guids[i].name);
      return i;
    }
  }

  if (!memcmp(g, &this->last_unknown_guid, sizeof(GUID))) return GUID_ERROR;
  memcpy(&this->last_unknown_guid, g, sizeof(GUID));
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "demux_asf: unknown GUID: 0x%" PRIx32 ", 0x%" PRIx16 ", 0x%" PRIx16 ", "
	  "{ 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 " }\n",
	  g->Data1, g->Data2, g->Data3,
	  g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);

  return GUID_ERROR;
}


static int get_guid (demux_asf_t *this) {
  int i;
  GUID g;

  g.Data1 = get_le32(this);
  g.Data2 = get_le16(this);
  g.Data3 = get_le16(this);
  for(i = 0; i < 8; i++) {
    g.Data4[i] = get_byte(this);
  }

  return get_guid_id(this, &g);
}

#if 0
static void get_str16_nolen(demux_asf_t *this, int len,
			    char *buf, int buf_size) {

  int c;
  char *q;

  q = buf;
  while (len > 0) {
    c = get_le16(this);
    if ((q - buf) < buf_size - 1)
      *q++ = c;
    len-=2;
  }
  *q = '\0';
}
#endif

static void asf_send_audio_header (demux_asf_t *this, int stream) {
  buf_element_t *buf;
  asf_stream_t *asf_stream = this->asf_header->streams[stream];
  xine_waveformatex  *wavex = (xine_waveformatex *)asf_stream->private_data;

  if (!this->audio_fifo)
    return;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  if (asf_stream->private_data_length > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_asf: private decoder data length (%d) is greater than fifo buffer length (%d)\n",
            asf_stream->private_data_length, buf->max_size);
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return;
  }

  memcpy (buf->content, wavex, asf_stream->private_data_length);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, wavex->wFormatTag);

  lprintf ("wavex header is %d bytes long\n", asf_stream->private_data_length);

  buf->size = asf_stream->private_data_length;
  buf->type = this->streams[stream].buf_type;
  buf->decoder_flags   = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
  buf->decoder_info[1] = wavex->nSamplesPerSec;
  buf->decoder_info[2] = wavex->wBitsPerSample;
  buf->decoder_info[3] = wavex->nChannels;

  this->audio_fifo->put (this->audio_fifo, buf);
}

#if 0
static unsigned long str2ulong(unsigned char *str) {
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}
#endif

static void asf_send_video_header (demux_asf_t *this, int stream) {

  buf_element_t      *buf;
  asf_demux_stream_t *demux_stream = &this->streams[stream];
  asf_stream_t       *asf_stream = this->asf_header->streams[stream];
  xine_bmiheader     *bih =  (xine_bmiheader *)(asf_stream->private_data + 11);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC, bih->biCompression);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  if ((asf_stream->private_data_length-11) > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_asf: private decoder data length (%d) is greater than fifo buffer length (%d)\n",
            asf_stream->private_data_length-10, buf->max_size);
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return;
  }

  buf->decoder_flags   = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;

  buf->decoder_info[0] = 0;

  if (this->asf_header->aspect_ratios[stream].x && this->asf_header->aspect_ratios[stream].y)
  {
    buf->decoder_flags  |= BUF_FLAG_ASPECT;
    buf->decoder_info[1] = bih->biWidth  * this->asf_header->aspect_ratios[stream].x;
    buf->decoder_info[2] = bih->biHeight * this->asf_header->aspect_ratios[stream].y;
  }

  buf->size = asf_stream->private_data_length - 11;
  memcpy (buf->content, bih, buf->size);
  buf->type = this->streams[stream].buf_type;

  this->video_fifo->put (this->video_fifo, buf);


  /* send off the palette, if there is one */
  if (demux_stream->palette_count) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    "demux_asf: stream %d, palette : %d entries\n", stream, demux_stream->palette_count);
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
    buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
    buf->decoder_info[2] = demux_stream->palette_count;
    buf->decoder_info_ptr[2] = &demux_stream->palette;
    buf->size = 0;
    buf->type = this->streams[stream].buf_type;
    this->video_fifo->put (this->video_fifo, buf);
  }
}

static int asf_read_header (demux_asf_t *this) {
  int i;
  uint64_t asf_header_len;
  uint8_t *asf_header_buffer = NULL;

  asf_header_len = get_le64(this);
  if (asf_header_len > 4 * 1024 * 1024)
  {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "demux_asf: asf_read_header: overly-large header? (%"PRIu64" bytes)\n",
	    asf_header_len);
    return 0;
  }

  asf_header_buffer = malloc (asf_header_len);

  if (this->input->read (this->input, asf_header_buffer, asf_header_len) != asf_header_len)
  {
    free (asf_header_buffer);
    return 0;
  }

  /* delete previous header */
  if (this->asf_header) {
    asf_header_delete(this->asf_header);
  }

  /* the header starts with :
   *   byte  0-15: header guid
   *   byte 16-23: header length
   */
  this->asf_header = asf_header_new(asf_header_buffer, asf_header_len);
  if (!this->asf_header)
  {
    free (asf_header_buffer);
    return 0;
  }
  free (asf_header_buffer);

  lprintf("asf header parsing ok\n");

  this->packet_size = this->asf_header->file->packet_size;
  this->packet_count = this->asf_header->file->data_packet_count;

  /* compute stream duration */
  this->length = (this->asf_header->file->send_duration -
                  this->asf_header->file->preroll) / 10000;
  if (this->length < 0)
    this->length = 0;

  /* compute average byterate (needed for seeking) */
  if (this->asf_header->file->max_bitrate)
    this->rate = this->asf_header->file->max_bitrate >> 3;
  else if (this->length)
    this->rate = (int64_t) this->input->get_length(this->input) * 1000 / this->length;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->asf_header->file->max_bitrate);

  for (i = 0; i < this->asf_header->stream_count; i++) {
    asf_stream_t *asf_stream = this->asf_header->streams[i];
    asf_demux_stream_t *demux_stream = &this->streams[i];

    if (!asf_stream) {
      if (this->mode != ASF_MODE_NO_CONTENT) {
	xine_log(this->stream->xine, XINE_LOG_MSG,
		 _("demux_asf: warning: A stream appears to be missing.\n"));
	_x_message(this->stream, XINE_MSG_READ_ERROR,
		   _("Media stream missing?"), NULL);
	this->mode = ASF_MODE_NO_CONTENT;
      }
      return 0;
    }

    if (asf_stream->encrypted_flag) {
      if (this->mode != ASF_MODE_ENCRYPTED_CONTENT) {
	xine_log(this->stream->xine, XINE_LOG_MSG,
		 _("demux_asf: warning: The stream id=%d is encrypted.\n"), asf_stream->stream_number);
	_x_message(this->stream, XINE_MSG_ENCRYPTED_SOURCE,
		   _("Media stream scrambled/encrypted"), NULL);
	this->mode = ASF_MODE_ENCRYPTED_CONTENT;
      }
    }
    switch (asf_stream->stream_type) {
    case GUID_ASF_AUDIO_MEDIA:

      _x_waveformatex_le2me( (xine_waveformatex *) asf_stream->private_data );
      if (asf_stream->error_correction_type == GUID_ASF_AUDIO_SPREAD) {
	this->reorder_h = asf_stream->error_correction_data[0];
	this->reorder_w = (asf_stream->error_correction_data[2]<<8)|asf_stream->error_correction_data[1];
	this->reorder_b = (asf_stream->error_correction_data[4]<<8)|asf_stream->error_correction_data[3];
	this->reorder_w /= this->reorder_b;
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"demux_asf: audio conceal interleave detected (%d x %d x %d)\n",
		this->reorder_w, this->reorder_h, this->reorder_b );
      } else {
	this->reorder_b = this->reorder_h = this->reorder_w = 1;
      }


      demux_stream->buf_type = _x_formattag_to_buf_audio
	(	((xine_waveformatex *)asf_stream->private_data)->wFormatTag );
      if ( !demux_stream->buf_type ) {
	demux_stream->buf_type = BUF_AUDIO_UNKNOWN;
	_x_report_audio_format_tag (this->stream->xine, LOG_MODULE,
				    ((xine_waveformatex *)asf_stream->private_data)->wFormatTag);
      }

      _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC, _x_buf_audio_name(demux_stream->buf_type));

      this->streams[i].fifo        = this->audio_fifo;
      this->streams[i].frag_offset = 0;
      this->streams[i].seq         = 0;
      if (this->reorder_h > 1 && this->reorder_w > 1 ) {
	if( !this->streams[i].buffer )
	  this->streams[i].buffer = malloc( DEFRAG_BUFSIZE );
	this->streams[i].defrag = 1;
      } else
	this->streams[i].defrag = 0;

      lprintf ("found an audio stream id=%d \n", asf_stream->stream_number);
      break;

    case GUID_ASF_VIDEO_MEDIA:
      {
        /* video private data
         * 11 bytes : header
         * 40 bytes : bmiheader
         * XX bytes : optional palette
         */
	uint32_t width, height;
	/*uint16_t bmiheader_size;*/
	xine_bmiheader *bmiheader;

	width = _X_LE_32(asf_stream->private_data);
	height = _X_LE_32(asf_stream->private_data + 4);
	/* there is one unknown byte between height and the bmiheader size */
	/*bmiheader_size = _X_LE_16(asf_stream->private_data + 9);*/

	/* FIXME: bmiheader_size must be >= sizeof(xine_bmiheader) */

	bmiheader = (xine_bmiheader *) (asf_stream->private_data + 11);
	_x_bmiheader_le2me(bmiheader);

	/* FIXME: check if (bmi_header_size == bmiheader->biSize) ? */

	demux_stream->buf_type = _x_fourcc_to_buf_video(bmiheader->biCompression);
	if( !demux_stream->buf_type ) {
	  demux_stream->buf_type = BUF_VIDEO_UNKNOWN;
	  _x_report_video_fourcc (this->stream->xine, LOG_MODULE, bmiheader->biCompression);
	}

	_x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, _x_buf_video_name(demux_stream->buf_type));
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, width);
	_x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, height);

	this->streams[i].fifo         = this->video_fifo;
	this->streams[i].frag_offset  = 0;
	this->streams[i].defrag       = 0;

	/* load the palette, if there is one */
	demux_stream->palette_count = bmiheader->biClrUsed;

	lprintf ("palette_count: %d\n", demux_stream->palette_count);
	if (demux_stream->palette_count > 256) {
	  lprintf ("number of colours exceeded 256 (%d)", demux_stream->palette_count);
	  demux_stream->palette_count = 256;
	}
	if ((asf_stream->private_data_length - sizeof(xine_bmiheader) - 11) >= (demux_stream->palette_count * 4)) {
	  int j;
	  uint8_t *palette;

	  /* according to msdn the palette is located here : */
	  palette = (uint8_t *)bmiheader + bmiheader->biSize;

	  /* load the palette */
	  for (j = 0; j < demux_stream->palette_count; j++) {
	    demux_stream->palette[j].b = *(palette + j * 4 + 0);
	    demux_stream->palette[j].g = *(palette + j * 4 + 1);
	    demux_stream->palette[j].r = *(palette + j * 4 + 2);
	  }
	} else {
	  int j;

	  /* generate a greyscale palette */
	  demux_stream->palette_count = 256;
	  for (j = 0; j < demux_stream->palette_count; j++) {
	  demux_stream->palette[j].r = j;
	  demux_stream->palette[j].g = j;
	  demux_stream->palette[j].b = j;
	  }
	}

	lprintf ("found a video stream id=%d, buf_type=%08x \n",
		 this->asf_header->streams[i]->stream_number, this->streams[i].buf_type);
      }
      break;
    }
  }

  this->input->seek (this->input, sizeof(GUID) + 10, SEEK_CUR);
  this->packet_size_left = 0;
  this->first_packet_pos = this->input->get_current_pos (this->input);
  return 1;
}

static int demux_asf_send_headers_common (demux_asf_t *this) {

  /* will get overridden later */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);

  /*
   * initialize asf engine
   */
  this->audio_stream             = -1;
  this->video_stream             = -1;
  this->packet_size              = 0;
  this->seqno                    = 0;

  if (!asf_read_header (this)) {

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_read_header failed.\n");

    this->status = DEMUX_FINISHED;
    return 1;
  } else {

    /*
     * send start buffer
     */
    _x_demux_control_start(this->stream);

    if (this->asf_header->content) {
      _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->asf_header->content->title);
      _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->asf_header->content->author);
      _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, this->asf_header->content->description);
    }

    /*  Choose the best audio and the best video stream.
     *  Use the bitrate to do the choice.
     */
    asf_header_choose_streams(this->asf_header, -1, &this->video_stream, &this->audio_stream);


    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "demux_asf: video stream_id: %d, audio stream_id: %d\n",
	    this->video_stream != -1 ? this->asf_header->streams[this->video_stream]->stream_number : -1,
	    this->audio_stream != -1 ? this->asf_header->streams[this->audio_stream]->stream_number : -1);

    if (this->audio_stream != -1) {
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
      asf_send_audio_header(this, this->audio_stream);
    }
    if (this->video_stream != -1) {
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
      asf_send_video_header(this, this->video_stream);
    }
  }
  return 0;
}

static void asf_reorder(demux_asf_t *this, uint8_t *src, int len){
  uint8_t dst[len];
  uint8_t *s2 = src;
  int i = 0, x, y;

  while(len-i >= this->reorder_h * this->reorder_w*this->reorder_b){
        for(x = 0; x < this->reorder_w; x++)
          for(y = 0; y < this->reorder_h; y++){
            memcpy(&dst[i], s2 + (y * this->reorder_w+x) * this->reorder_b,
                   this->reorder_b);
            i += this->reorder_b;
          }
        s2 += this->reorder_h * this->reorder_w * this->reorder_b;
  }

  xine_fast_memcpy(src,dst,i);
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts (demux_asf_t *this, int64_t pts, int video, int frame_end) {
  int64_t diff;

  diff = pts - this->last_pts[video];

#ifdef LOG
  if (pts) {
    if (video) {
      printf ("demux_asf: VIDEO: pts = %8"PRId64", diff = %8"PRId64"\n", pts, pts - this->last_pts[video]);
    } else {
      printf ("demux_asf: AUDIO: pts = %8"PRId64", diff = %8"PRId64"\n", pts, pts - this->last_pts[video]);
    }
  }
#endif
  if (pts && (this->send_newpts || (this->last_pts[video] && abs(diff) > WRAP_THRESHOLD))) {

    lprintf ("sending newpts %"PRId64" (video = %d diff = %"PRId64")\n", pts, video, diff);

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }

    this->send_newpts         = 0;
    this->last_pts[1 - video] = 0;
  }

  if (pts)
    this->last_pts[video] = pts;

}


static void asf_send_buffer_nodefrag (demux_asf_t *this, asf_demux_stream_t *stream,
				      int frag_offset, int64_t timestamp,
				      int frag_len) {

  buf_element_t *buf;
  int            bufsize;
  int            package_done;

  lprintf ("pts=%"PRId64", off=%d, len=%d, total=%d\n",
           timestamp * 90, frag_offset, frag_len, stream->payload_size);

  if (frag_offset == 0) {
    /* new packet */
    stream->frag_offset = 0;
    lprintf("new packet\n");
  } else {
    if (frag_offset == stream->frag_offset) {
      /* continuing packet */
      lprintf("continuing packet: offset=%d\n", frag_offset);
    } else {
      /* cannot continue current packet: free it */
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_send_buffer_nodefrag: stream offset: %d, invalid offset: %d\n", stream->frag_offset, frag_offset);
      this->input->seek (this->input, frag_len, SEEK_CUR);
      return ;
    }
  }

  while( frag_len ) {
    if ( frag_len < stream->fifo->buffer_pool_buf_size )
      bufsize = frag_len;
    else
      bufsize = stream->fifo->buffer_pool_buf_size;

    buf = stream->fifo->buffer_pool_alloc (stream->fifo);
    if (this->input->read (this->input, buf->content, bufsize) != bufsize) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: input buffer starved\n");
      return ;
    }

    lprintf ("data: %d %d %d %d\n", buf->content[0], buf->content[1], buf->content[2], buf->content[3]);

    if( this->input->get_length (this->input) )
      buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                       65535 / this->input->get_length (this->input) );
    buf->extra_info->input_time = timestamp;

    lprintf ("input normpos is %d, input time is %d, rate %d\n",
             buf->extra_info->input_normpos,
             buf->extra_info->input_time,
             this->rate);

    buf->pts        = timestamp * 90;

    buf->type       = stream->buf_type;
    buf->size       = bufsize;
    timestamp       = 0;

    if (stream->frag_offset == 0)
      buf->decoder_flags |= BUF_FLAG_FRAME_START;

    stream->frag_offset += bufsize;
    frag_len -= bufsize;

    package_done = (stream->frag_offset >= stream->payload_size);

    if ((buf->type & BUF_MAJOR_MASK) == BUF_VIDEO_BASE)
      check_newpts (this, buf->pts, PTS_VIDEO, package_done);
    else
      check_newpts (this, buf->pts, PTS_AUDIO, package_done);

    /* test if whole packet read */
    if (package_done) {
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
      lprintf("packet done: offset=%d, payload=%d\n", stream->frag_offset, stream->payload_size);
    }

    lprintf ("buffer type %08x %8d bytes, %8lld pts\n",
             buf->type, buf->size, buf->pts);

    stream->fifo->put (stream->fifo, buf);
  }
}

static void asf_send_buffer_defrag (demux_asf_t *this, asf_demux_stream_t *stream,
				    int frag_offset, int64_t timestamp,
				    int frag_len) {

  buf_element_t *buf;
  int            package_done;

  /*
    printf("asf_send_buffer seq=%d frag_offset=%d frag_len=%d\n",
    seq, frag_offset, frag_len );
  */
  lprintf ("asf_send_buffer_defrag: timestamp=%"PRId64", pts=%"PRId64"\n", timestamp, timestamp * 90);

  if (frag_offset == 0) {
    /* new packet */
    lprintf("new packet\n");
    stream->frag_offset = 0;
    stream->timestamp = timestamp;
  } else {
    if (frag_offset == stream->frag_offset) {
      /* continuing packet */
      lprintf("continuing packet: offset=%d\n", frag_offset);
    } else {
      /* cannot continue current packet: free it */
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_send_buffer_defrag: invalid offset\n");
      this->input->seek (this->input, frag_len, SEEK_CUR);
      return ;
    }
  }

  if( stream->frag_offset + frag_len > DEFRAG_BUFSIZE ) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: buffer overflow on defrag!\n");
  } else {
    if (this->input->read (this->input, &stream->buffer[stream->frag_offset], frag_len) != frag_len) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: input buffer starved\n");
      return ;
    }
    stream->frag_offset += frag_len;
  }

  package_done = (stream->frag_offset >= stream->payload_size);

  if (package_done) {
    int bufsize;
    uint8_t *p;

    lprintf("packet done: offset=%d, payload=%d\n", stream->frag_offset, stream->payload_size);

    if (stream->fifo == this->audio_fifo &&
        this->reorder_h > 1 && this->reorder_w > 1 ) {
      asf_reorder(this,stream->buffer,stream->frag_offset);
    }

    p = stream->buffer;
    while( stream->frag_offset ) {
      if ( stream->frag_offset < stream->fifo->buffer_pool_buf_size )
        bufsize = stream->frag_offset;
      else
        bufsize = stream->fifo->buffer_pool_buf_size;

      buf = stream->fifo->buffer_pool_alloc (stream->fifo);
      xine_fast_memcpy (buf->content, p, bufsize);

      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                         65535 / this->input->get_length (this->input) );
      buf->extra_info->input_time = stream->timestamp;

      /* send the same pts for the entire frame */
      buf->pts        = stream->timestamp * 90;

      buf->type       = stream->buf_type;
      buf->size       = bufsize;

      lprintf ("buffer type %08x %8d bytes, %8lld pts\n",
	       buf->type, buf->size, buf->pts);

      stream->frag_offset -= bufsize;
      p+=bufsize;

      if ((buf->type & BUF_MAJOR_MASK) == BUF_VIDEO_BASE)
        check_newpts (this, buf->pts, PTS_VIDEO, !stream->frag_offset);
      else
        check_newpts (this, buf->pts, PTS_AUDIO, !stream->frag_offset);

      /* test if whole packet read */
      if ( !stream->frag_offset )
        buf->decoder_flags   |= BUF_FLAG_FRAME_END;

      stream->fifo->put (stream->fifo, buf);
    }
  }
}

/* return 0 if ok */
static int asf_parse_packet_align(demux_asf_t *this) {

  uint64_t current_pos, packet_pos;
  uint32_t mod;
  uint64_t packet_num;


  current_pos = this->input->get_current_pos (this->input);

  /* seek to the beginning of the next packet */
  mod = (current_pos - this->first_packet_pos) % this->packet_size;
  this->packet_size_left = mod ? this->packet_size - mod : 0;
  packet_pos = current_pos + this->packet_size_left;

  if (this->packet_size_left) {
    lprintf("last packet is not finished, %d bytes\n", this->packet_size_left);
    current_pos = this->input->seek (this->input, packet_pos, SEEK_SET);
    if (current_pos != packet_pos) {
      return 1;
    }
  }
  this->packet_size_left = 0;

  /* check packet_count */
  packet_num = (packet_pos - this->first_packet_pos) / this->packet_size;
  lprintf("packet_num=%"PRId64", packet_count=%"PRId64"\n", packet_num, this->packet_count);
  if (packet_num >= this->packet_count) {
    /* end of payload data */
    current_pos = this->input->get_current_pos (this->input);
    lprintf("end of payload data, current_pos=%"PRId64"\n", current_pos);
    {
      /* check new asf header */
      if (get_guid(this) == GUID_ASF_HEADER) {
        lprintf("new asf header detected\n");
        _x_demux_control_end(this->stream, 0);
        if (demux_asf_send_headers_common(this))
          return 1;
      } else {
	lprintf("not an ASF stream or end of stream\n");
        return 1;
      }
    }
  }

  return 0;
}

/* return 0 if ok */
static int asf_parse_packet_ecd(demux_asf_t *this, uint32_t  *p_hdr_size) {

  uint8_t   ecd_flags;
  uint8_t   buf[16];
  int       invalid_packet;

  do {
    ecd_flags = get_byte(this); *p_hdr_size = 1;
    if (this->status == DEMUX_FINISHED)
      return 1;
    invalid_packet = 0;
    {
      int ecd_len;
      int ecd_opaque;
      int ecd_len_type;
      int ecd_present;

      ecd_len      = ecd_flags & 0xF;
      ecd_opaque   = (ecd_flags >> 4) & 0x1;
      ecd_len_type = (ecd_flags >> 5) & 0x3;
      ecd_present  = (ecd_flags >> 7) & 0x1;

      /* skip ecd */
      if (ecd_present && !ecd_opaque && !ecd_len_type) {
        int read_size;

        read_size = this->input->read (this->input, buf, ecd_len);
        if (read_size != ecd_len) {
          this->status = DEMUX_FINISHED;
          return 1;
        }
        *p_hdr_size += read_size;

      } else {
        GUID *guid = (GUID *)buf;

        /* check if it's a new stream */
        buf[0] = ecd_flags;
        if (this->input->read (this->input, buf + 1, 15) != 15) {
          this->status = DEMUX_FINISHED;
          return 1;
        }
        *p_hdr_size += 15;
        guid->Data1 = _X_LE_32(buf);
        guid->Data2 = _X_LE_16(buf + 4);
        guid->Data3 = _X_LE_16(buf + 6);
        if (get_guid_id(this, guid) == GUID_ASF_HEADER) {
          lprintf("new asf header detected\n");
          _x_demux_control_end(this->stream, 0);
          if (demux_asf_send_headers_common(this))
            return 1;
        } else {

          /* skip invalid packet */
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: skip invalid packet: %2X\n", ecd_flags);
          this->input->seek (this->input, this->packet_size - *p_hdr_size, SEEK_CUR);
        }
        invalid_packet = 1;
      }
    }
  } while (invalid_packet);

  return 0;
}

/* return 0 if ok */
static int asf_parse_packet_payload_header(demux_asf_t *this, uint32_t p_hdr_size) {

#ifdef LOG
  int64_t   timestamp;
  int64_t   duration;
#endif

  this->packet_len_flags = get_byte(this);  p_hdr_size += 1;
  this->packet_prop_flags = get_byte(this);  p_hdr_size += 1;

  /* packet size */
  switch((this->packet_len_flags >> 5) & 3) {
    case 1:
      this->data_size = get_byte(this); p_hdr_size += 1; break;
    case 2:
      this->data_size = get_le16(this); p_hdr_size += 2; break;
    case 3:
      this->data_size = get_le32(this); p_hdr_size += 4; break;
    default:
      this->data_size = 0;
  }

  /* sequence */
  switch ((this->packet_len_flags >> 1) & 3) {
    case 1:
      get_byte(this); p_hdr_size += 1; break;
    case 2:
      get_le16(this); p_hdr_size += 2; break;
    case 3:
      get_le32(this); p_hdr_size += 4; break;
  }

  /* padding size */
  switch ((this->packet_len_flags >> 3) & 3){
    case 1:
      this->packet_padsize = get_byte(this); p_hdr_size += 1; break;
    case 2:
      this->packet_padsize = get_le16(this); p_hdr_size += 2; break;
    case 3:
      this->packet_padsize = get_le32(this); p_hdr_size += 4; break;
    default:
      this->packet_padsize = 0;
  }

#ifdef LOG
  timestamp = get_le32(this); p_hdr_size += 4;
  duration  = get_le16(this); p_hdr_size += 2;
#else
  /* skip the above bytes */
  this->input->seek (this->input, 6, SEEK_CUR);
  p_hdr_size += 6;
#endif

  lprintf ("timestamp=%"PRId64", duration=%"PRId64"\n", timestamp, duration);

  if ((this->packet_len_flags >> 5) & 3) {
    /* absolute data size */
    lprintf ("absolute data size\n");

    this->packet_padsize = this->packet_size - this->data_size; /* not used */
  } else {
    /* relative data size */
    lprintf ("relative data size\n");

    this->data_size = this->packet_size - this->packet_padsize;
  }

  if (this->packet_padsize > this->packet_size) {
    /* skip packet */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: invalid padsize: %d\n", this->packet_padsize);
    return 1;
  }

  /* Multiple frames */
  if (this->packet_len_flags & 0x01) {
    this->frame_flag = get_byte(this); p_hdr_size += 1;
    this->nb_frames = (this->frame_flag & 0x3F);

    lprintf ("multiple frames %d\n", this->nb_frames);
  } else {
    this->frame_flag = 0;
    this->nb_frames = 1;
  }

  /* this->packet_size_left = this->packet_size - p_hdr_size; */
  this->packet_size_left = this->data_size - p_hdr_size;
  lprintf ("new packet, size = %d, size_left = %d, flags = 0x%02x, padsize = %d, this->packet_size = %d\n",
	   this->data_size, this->packet_size_left, this->packet_len_flags, this->packet_padsize, this->packet_size);

  return 0;
}

/* return 0 if ok */
static int asf_parse_packet_payload_common(demux_asf_t *this,
                                           uint8_t raw_id,
                                           asf_demux_stream_t  **stream,
                                           uint32_t *frag_offset,
					   uint32_t *rlen) {
  uint8_t        stream_id;
  int            i;
  uint32_t       s_hdr_size = 0;
  uint32_t       seq = 0;
  uint32_t       next_seq = 0;
  buf_element_t *buf;

  stream_id  = raw_id & 0x7f;
  *stream    = NULL;

  lprintf ("got raw_id=%d, stream_id=%d\n", raw_id, stream_id);

  for (i = 0; i < this->asf_header->stream_count; i++) {
  lprintf ("stream_number = %d\n", this->asf_header->streams[i]->stream_number);
    if ((this->asf_header->streams[i]->stream_number == stream_id) &&
        (( this->audio_stream != -1 && stream_id == this->asf_header->streams[this->audio_stream]->stream_number ) ||
	 ( this->video_stream != -1 && stream_id == this->asf_header->streams[this->video_stream]->stream_number ))) {
      *stream = &this->streams[i];
      break;
    }
  }

  switch ((this->packet_prop_flags >> 4) & 3){
  case 1:
    seq = get_byte(this); s_hdr_size += 1;
    if (*stream) {
      (*stream)->seq = (*stream)->seq % 256;
      next_seq = ((*stream)->seq + 1) % 256;
    }
    break;
  case 2:
    seq = get_le16(this); s_hdr_size += 2;
    if (*stream) {
      (*stream)->seq = (*stream)->seq % 65536;
      next_seq = ((*stream)->seq + 1) % 65536;
    }
    break;
  case 3:
    seq = get_le32(this); s_hdr_size += 4;
    if (*stream)
      next_seq = (*stream)->seq + 1;
    break;
  default:
    lprintf ("seq=0\n");
    seq = 0;
  }

  /* check seq number */
  if (*stream) {
    lprintf ("stream_id = %d, seq = %d\n", stream_id, seq);
    if ((*stream)->first_seq || (*stream)->skip) {
      next_seq = seq;
      (*stream)->first_seq = 0;
    }
    if ((seq != (*stream)->seq) && (seq != next_seq)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_asf: bad seq: seq = %d, next_seq = %d, stream seq = %d!\n", seq, next_seq, (*stream)->seq);

      /* the stream is corrupted, reset the decoder and restart at a new keyframe */
      if ((*stream)->fifo) {
        buf = (*stream)->fifo->buffer_pool_alloc ((*stream)->fifo);
        buf->type = BUF_CONTROL_RESET_DECODER;
        (*stream)->fifo->put((*stream)->fifo, buf);
      }
      if (this->video_stream != -1 && stream_id == this->asf_header->streams[this->video_stream]->stream_number) {
        lprintf ("bad seq: waiting for keyframe\n");

        (*stream)->resync    =  1;
        (*stream)->skip      =  1;
        this->keyframe_found =  0;
      }
    }
    (*stream)->seq = seq;
  }

  switch ((this->packet_prop_flags >> 2) & 3) {
    case 1:
      *frag_offset = get_byte(this); s_hdr_size += 1; break;
    case 2:
      *frag_offset = get_le16(this); s_hdr_size += 2; break;
    case 3:
      *frag_offset = get_le32(this); s_hdr_size += 4; break;
    default:
      lprintf ("frag_offset=0\n");
      *frag_offset = 0;
  }

  switch (this->packet_prop_flags & 3) {
    case 1:
      *rlen = get_byte(this); s_hdr_size += 1; break;
    case 2:
      *rlen = get_le16(this); s_hdr_size += 2; break;
    case 3:
      *rlen = get_le32(this); s_hdr_size += 4; break;
    default:
      *rlen = 0;
  }

  if (*rlen > this->packet_size_left) {
    /* skip packet */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: invalid rlen %d\n", *rlen);
    return 1;
  }

  lprintf ("segment header, stream id %02x, frag_offset %d, flags : %02x\n",
          stream_id, *frag_offset, *rlen);

  this->packet_size_left -= s_hdr_size;
  return 0;
}

/* return 0 if ok */
static int asf_parse_packet_compressed_payload(demux_asf_t *this,
                                               asf_demux_stream_t  *stream,
                                               uint8_t raw_id,
                                               uint32_t frag_offset,
                                               int64_t *timestamp) {
  uint32_t s_hdr_size = 0;
  uint32_t data_length = 0;
  uint32_t data_sent = 0;

  *timestamp = frag_offset;
  if (*timestamp)
    *timestamp -= this->asf_header->file->preroll;

  frag_offset = 0;
  get_byte (this); s_hdr_size += 1;

  if (this->packet_len_flags & 0x01) {
    /* multiple frames */
    switch ((this->frame_flag >> 6) & 3) {
      case 1:
        data_length = get_byte(this); s_hdr_size += 1; break;
      case 2:
        data_length = get_le16(this); s_hdr_size += 2; break;
      case 3:
        data_length = get_le32(this); s_hdr_size += 4; break;
      default:
        lprintf ("invalid frame_flag %d\n", this->frame_flag);
        data_length = get_le16(this); s_hdr_size += 2;
    }

    lprintf ("reading multiple payload, size = %d\n", data_length);

  } else {
    data_length = this->packet_size_left - s_hdr_size;

    lprintf ("reading single payload, size = %d\n", data_length);
  }

  if (data_length > this->packet_size_left) {
    /* skip packet */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: invalid data_length\n");
    return 1;
  }

  this->packet_size_left -= s_hdr_size;

  while (data_sent < data_length) {
    int object_length = get_byte(this);

    lprintf ("sending grouped object, len = %d\n", object_length);

    if (stream && stream->fifo) {
      stream->payload_size = object_length;

      /* keyframe detection for non-seekable input plugins */
      if (stream->skip && (raw_id & 0x80) && !this->keyframe_found) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: keyframe detected\n");
        this->keyframe_ts = *timestamp;
        this->keyframe_found = 1;
      }

      if (stream->resync && (this->keyframe_found) && (*timestamp >= this->keyframe_ts)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: stream resynced\n");
        stream->resync = 0;
        stream->skip = 0;
      }

      if (!stream->skip) {
        lprintf ("sending buffer of type %08x\n", stream->buf_type);

        if (stream->defrag)
          asf_send_buffer_defrag (this, stream, 0, *timestamp, object_length);
        else
          asf_send_buffer_nodefrag (this, stream, 0, *timestamp, object_length);
      } else {
        lprintf ("skip object\n");

        this->input->seek (this->input, object_length, SEEK_CUR);
      }
      stream->seq++;
    } else {
      lprintf ("unhandled stream type\n");

      this->input->seek (this->input, object_length, SEEK_CUR);
    }
    data_sent += object_length + 1;
    this->packet_size_left -= object_length + 1;
    *timestamp = 0;
  }
  *timestamp = frag_offset;
  return 0;
}

/* return 0 if ok */
static int asf_parse_packet_payload(demux_asf_t *this,
                                    asf_demux_stream_t *stream,
                                    uint8_t raw_id,
                                    uint32_t frag_offset,
                                    uint32_t rlen,
                                    int64_t *timestamp) {
  uint32_t s_hdr_size = 0;
  uint32_t frag_len;
  uint32_t payload_size = 0;

  if (rlen >= 8) {
    payload_size  = get_le32(this); s_hdr_size += 4;
    *timestamp    = get_le32(this); s_hdr_size += 4;
    if (*timestamp)
      *timestamp -= this->asf_header->file->preroll;
    if (stream)
      stream->payload_size = payload_size;
    if ((rlen - 8) > 0)
      this->input->seek (this->input, rlen - 8, SEEK_CUR);
    s_hdr_size += rlen - 8;
  } else {
    *timestamp = 0;
    if (rlen) this->input->seek (this->input, rlen, SEEK_CUR);
    s_hdr_size += rlen;
  }

  if (this->packet_len_flags & 0x01) {
    switch ((this->frame_flag >> 6) & 3) {
      case 1:
        frag_len = get_byte(this); s_hdr_size += 1; break;
      case 2:
        frag_len = get_le16(this); s_hdr_size += 2; break;
      case 3:
        frag_len = get_le32(this); s_hdr_size += 4; break;
      default:
        lprintf ("invalid frame_flag %d\n", this->frame_flag);

        frag_len = get_le16(this); s_hdr_size += 2;
    }

    lprintf ("reading multiple payload, payload_size=%d, frag_len=%d\n", payload_size, frag_len);
  } else {
    frag_len = this->packet_size_left - s_hdr_size;

    lprintf ("reading single payload, payload_size=%d, frag_len = %d\n", payload_size, frag_len);
  }

  if (frag_len > this->packet_size_left) {
    /* skip packet */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: invalid frag_len %d\n", frag_len);
    return 1;
  }

  this->packet_size_left -= s_hdr_size;

  if (stream && stream->fifo) {
    if (!frag_offset) {
      /* keyframe detection for non-seekable input plugins */
      if (stream->skip && (raw_id & 0x80) && !this->keyframe_found) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: keyframe detected\n");
        this->keyframe_found = 1;
        this->keyframe_ts = *timestamp;
      }
      if (stream->resync && this->keyframe_found && (*timestamp >= this->keyframe_ts) &&
          !frag_offset) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: stream resynced\n");
        stream->resync = 0;
        stream->skip = 0;
      }
    }

    if (!stream->skip) {
      lprintf ("sending buffer of type %08x\n", stream->buf_type);

      if (stream->defrag)
        asf_send_buffer_defrag (this, stream, frag_offset, *timestamp, frag_len);
      else
        asf_send_buffer_nodefrag (this, stream, frag_offset, *timestamp, frag_len);
    } else {
      lprintf ("skip fragment\n");

      this->input->seek (this->input, frag_len, SEEK_CUR);
    }
  } else {
    lprintf ("unhandled stream type\n");

    this->input->seek (this->input, frag_len, SEEK_CUR);
  }
  this->packet_size_left -= frag_len;
  return 0;
}

/*
 * parse a m$ http reference
 * format :
 * [Reference]
 * Ref1=http://www.blabla.com/blabla
 */
static int demux_asf_parse_http_references( demux_asf_t *this) {
  char           *buf = NULL;
  char           *ptr, *end;
  int             buf_size = 0;
  int             buf_used = 0;
  int             len;
  char           *href = NULL;
  int             free_href = 0;

  /* read file to memory.
   * warning: dumb code, but hopefuly ok since reference file is small */
  do {
    buf_size += 1024;
    buf = realloc(buf, buf_size+1);

    len = this->input->read(this->input, &buf[buf_used], buf_size-buf_used);

    if( len > 0 )
      buf_used += len;

    /* 50k of reference file? no way. something must be wrong */
    if( buf_used > 50*1024 )
      break;
  } while( len > 0 );

  if(buf_used)
    buf[buf_used] = '\0';

  ptr = buf;
  if (!strncmp(ptr, "[Reference]", 11)) {

    const char *const mrl = this->input->get_mrl(this->input);
    if (!strncmp(mrl, "http", 4)) {
      /* never trust a ms server, reopen the same mrl with the mms input plugin
       * some servers are badly configured and return a incorrect reference.
       */
      href = strdup(mrl);
      free_href = 1;
    } else {
      ptr += 11;
      if (*ptr == '\r') ptr ++;
      if (*ptr == '\n') ptr ++;
      href = strchr(ptr, '=');
      if (!href) goto failure;
      href++;
      end = strchr(href, '\r');
      if (!end) goto failure;
      *end = '\0';
    }

    /* replace http by mmsh */
    if (!strncmp(href, "http", 4)) {
      memcpy(href, "mmsh", 4);
    }

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: http ref: %s\n", href);
    _x_demux_send_mrl_reference (this->stream, 0, href, NULL, 0, 0);

    if (free_href)
      free(href);
  }

failure:
  free (buf);
  this->status = DEMUX_FINISHED;
  return this->status;
}

/*
 * parse a stupid ASF reference in an asx file
 * format : "ASF http://www.blabla.com/blabla"
 */
static int demux_asf_parse_asf_references( demux_asf_t *this) {
  char           *buf = NULL;
  char           *ptr;
  int             buf_size = 0;
  int             buf_used = 0;
  int             len;
  int             i;

  /* read file to memory.
   * warning: dumb code, but hopefuly ok since reference file is small */
  do {
    buf_size += 1024;
    buf = realloc(buf, buf_size+1);

    len = this->input->read(this->input, &buf[buf_used], buf_size-buf_used);

    if( len > 0 )
      buf_used += len;

    /* 50k of reference file? no way. something must be wrong */
    if( buf_used > 50*1024 )
      break;
  } while( len > 0 );

  if(buf_used)
    buf[buf_used] = '\0';

  ptr = buf;
  if (!strncmp(ptr, "ASF ", 4)) {
    ptr += 4;

    /* find the end of the string */
    for (i = 4; i < buf_used; i++) {
      if ((buf[i] == ' ') || (buf[i] == '\r') || (buf[i] == '\n')) {
        buf[i] = '\0';
        break;
      }
    }

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf ref: %s\n", ptr);
    _x_demux_send_mrl_reference (this->stream, 0, ptr, NULL, 0, 0);
  }

  free (buf);

  this->status = DEMUX_FINISHED;
  return this->status;
}


/* .asx playlist parser helper functions */
static uint32_t asx_get_time_value (const xml_node_t *node)
{
  const char *value = xml_parser_get_property (node, "VALUE");

  if (value)
  {
    int hours, minutes;
    double seconds;

    if (sscanf (value, "%d:%d:%lf", &hours, &minutes, &seconds) == 3)
      return hours * 3600000 + minutes * 60000 + seconds * 1000;

    if (sscanf (value, "%d:%lf", &minutes, &seconds) == 3)
      return minutes * 60000 + seconds * 1000;

    /* FIXME: single element is minutes or seconds? */
  }

  return 0; /* value not found */
}

/*
 * parse .asx playlist files
 */
static int demux_asf_parse_asx_references( demux_asf_t *this) {

  char           *buf = NULL;
  int             buf_size = 0;
  int             buf_used = 0;
  int             len;
  xml_node_t     *xml_tree, *asx_entry, *asx_ref;
  xml_parser_t   *xml_parser;
  int             result;


  /* read file to memory.
   * warning: dumb code, but hopefuly ok since reference file is small */
  do {
    buf_size += 1024;
    buf = realloc(buf, buf_size+1);

    len = this->input->read(this->input, &buf[buf_used], buf_size-buf_used);

    if( len > 0 )
      buf_used += len;

    /* 50k of reference file? no way. something must be wrong */
    if( buf_used > 50*1024 )
      break;
  } while( len > 0 );

  if(buf_used)
    buf[buf_used] = '\0';

  xml_parser = xml_parser_init_r(buf, buf_used, XML_PARSER_CASE_INSENSITIVE);
  if((result = xml_parser_build_tree_r(xml_parser, &xml_tree)) != XML_PARSER_OK) {
    xml_parser_finalize_r(xml_parser);
    goto failure;
  }

  xml_parser_finalize_r(xml_parser);

  if(!strcasecmp(xml_tree->name, "ASX")) {
    /* Attributes: VERSION, PREVIEWMODE, BANNERBAR
     * Child elements: ABSTRACT, AUTHOR, BASE, COPYRIGHT, DURATION, ENTRY,
                       ENTRYREF, MOREINFO, PARAM, REPEAT, TITLE
     */

    /*const char *base_href = NULL;*/

    for (asx_entry = xml_tree->child; asx_entry; asx_entry = asx_entry->next)
    {
      /*const char *ref_base_href = base_href;*/

      if (!strcasecmp (asx_entry->name, "ENTRY"))
      {
        /* Attributes: CLIENTSKIP, SKIPIFREF
         * Child elements: ABSTRACT, AUTHOR, BASE, COPYRIGHT, DURATION,
                           ENDMARKER, MOREINFO, PARAM, REF, STARTMARKER,
                           STARTTIME, TITLE
         */
        const char *href = NULL;
        const char *title = NULL;
        uint32_t start_time = (uint32_t)-1;
        uint32_t duration = (uint32_t)-1;

        for (asx_ref = asx_entry->child; asx_ref; asx_ref = asx_ref->next)
        {
          if (!strcasecmp(asx_ref->name, "REF"))
          {
            xml_node_t *asx_sub;
            /* Attributes: HREF
             * Child elements: DURATION, ENDMARKER, STARTMARKER, STARTTIME
             */

            /* FIXME: multiple REFs => alternative streams
             * (and per-ref start times and durations?).
             * Just the one title, though.
             */
            href = xml_parser_get_property (asx_ref, "HREF");

            for (asx_sub = asx_ref->child; asx_sub; asx_sub = asx_sub->next)
            {
              if (!strcasecmp (asx_sub->name, "STARTTIME"))
                start_time = asx_get_time_value (asx_sub);
              else if (!strcasecmp (asx_sub->name, "DURATION"))
                duration = asx_get_time_value (asx_sub);
            }
          }

          else if (!strcasecmp (asx_ref->name, "TITLE"))
          {
            if (!title)
              title = asx_ref->data;
          }

          else if (!strcasecmp (asx_ref->name, "STARTTIME"))
          {
            if (start_time == (uint32_t)-1)
              start_time = asx_get_time_value (asx_ref);
          }

          else if (!strcasecmp (asx_ref->name, "DURATION"))
          {
            if (duration == (uint32_t)-1)
              duration = asx_get_time_value (asx_ref);
          }

#if 0
          else if (!strcasecmp (asx_ref->name, "BASE"))
            /* Attributes: HREF */
            ref_base_href = xml_parser_get_property (asx_entry, "HREF");
#endif
        }

        /* FIXME: prepend ref_base_href to href */
        if (href && *href)
          _x_demux_send_mrl_reference (this->stream, 0, href, title,
                                       start_time == (uint32_t)-1 ? 0 : start_time,
                                       duration == (uint32_t)-1 ? -1 : duration);
      }

      else if (!strcasecmp (asx_entry->name, "ENTRYREF"))
      {
        /* Attributes: HREF, CLIENTBIND */
        const char *href = xml_parser_get_property (asx_entry, "HREF");
        if (href && *href)
          _x_demux_send_mrl_reference (this->stream, 0, href, NULL, 0, -1);
      }

#if 0
      else if (!strcasecmp (asx_entry->name, "BASE"))
        /* Attributes: HREF */
        base_href = xml_parser_get_property (asx_entry, "HREF");
#endif
    }
  }
  else
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "demux_asf: Unsupported XML type: '%s'.\n", xml_tree->name);

  xml_parser_free_tree(xml_tree);
failure:
  free(buf);

  this->status = DEMUX_FINISHED;
  return this->status;
}


/*
 * xine specific functions start here
 */

static int demux_asf_send_chunk (demux_plugin_t *this_gen) {
  demux_asf_t *this = (demux_asf_t *) this_gen;
  asf_demux_stream_t  *stream = NULL;
  uint32_t frag_offset = 0;
  uint32_t rlen = 0;
  uint8_t  raw_id = 0;
  int64_t  ts = 0;

  switch (this->mode) {
    case ASF_MODE_ASX_REF:
      return demux_asf_parse_asx_references(this);

    case ASF_MODE_HTTP_REF:
      return demux_asf_parse_http_references(this);

    case ASF_MODE_ASF_REF:
      return demux_asf_parse_asf_references(this);

    case ASF_MODE_ENCRYPTED_CONTENT:
    case ASF_MODE_NO_CONTENT:
      this->status = DEMUX_FINISHED;
      return this->status;

    default:
    {
      uint32_t header_size = 0;

      if (asf_parse_packet_align(this)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_parse_packet_align failed\n");
        this->status = DEMUX_FINISHED;
        return this->status;
      }
      if (asf_parse_packet_ecd(this, &header_size)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_parse_packet_ecd failed\n");
        this->status = DEMUX_FINISHED;
        return this->status;
      }
      if (asf_parse_packet_payload_header(this, header_size)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_parse_packet_header failed\n");
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      for (this->frame = 0; this->frame < (this->nb_frames & 0x3f); this->frame++) {
        raw_id = get_byte(this); this->packet_size_left -= 1;

        if (asf_parse_packet_payload_common(this, raw_id, &stream, &frag_offset, &rlen))
          break;
        if (rlen == 1) {
          if (asf_parse_packet_compressed_payload(this, stream, raw_id, frag_offset, &ts))
            break;
        } else {
          if (asf_parse_packet_payload(this, stream, raw_id, frag_offset, rlen, &ts))
            break;
        }
      }
      return this->status;
    }
  }
}

static void demux_asf_dispose (demux_plugin_t *this_gen) {

  demux_asf_t *this = (demux_asf_t *) this_gen;

  if (this->asf_header) {
    int i;

    for (i=0; i<this->asf_header->stream_count; i++) {
      asf_demux_stream_t *asf_stream;

      asf_stream = &this->streams[i];
      if (asf_stream->buffer) {
        free (asf_stream->buffer);
        asf_stream->buffer = NULL;
      }
    }

    asf_header_delete (this->asf_header);
  }

  free (this);
}

static int demux_asf_get_status (demux_plugin_t *this_gen) {
  demux_asf_t *this = (demux_asf_t *) this_gen;

  return this->status;
}


static void demux_asf_send_headers (demux_plugin_t *this_gen) {
  demux_asf_t *this = (demux_asf_t *) this_gen;
  int          guid;

  this->video_fifo     = this->stream->video_fifo;
  this->audio_fifo     = this->stream->audio_fifo;

  this->last_pts[0]    = 0;
  this->last_pts[1]    = 0;

  this->status         = DEMUX_OK;

  if (this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE)
    this->input->seek (this->input, 0, SEEK_SET);

  if ((this->mode == ASF_MODE_ASX_REF) ||
      (this->mode == ASF_MODE_HTTP_REF) ||
      (this->mode == ASF_MODE_ASF_REF)) {
    _x_demux_control_start(this->stream);
    return;
  }

  guid = get_guid(this);
  if (guid != GUID_ASF_HEADER) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: file doesn't start with an asf header\n");
    this->status = DEMUX_FINISHED;
    return;
  }

  demux_asf_send_headers_common(this);

  lprintf ("send header done\n");
}

static int demux_asf_seek (demux_plugin_t *this_gen,
			    off_t start_pos, int start_time, int playing) {

  demux_asf_t *this = (demux_asf_t *) this_gen;
  asf_demux_stream_t *stream = NULL;
  uint32_t       frag_offset = 0;
  uint32_t       rlen        = 0;
  uint8_t        raw_id, stream_id;
  int            i, state;
  int64_t        ts;

  lprintf ("demux_asf_seek: start_pos=%"PRId64", start_time=%d\n",
	   start_pos, start_time);

  this->status = DEMUX_OK;

  if (this->mode != ASF_MODE_NORMAL) {
    return this->status;
  }

  /*
   * seek to start position
   */
  for(i = 0; i < this->asf_header->stream_count; i++) {
    this->streams[i].frag_offset =  0;
    this->streams[i].first_seq   =  1;
    this->streams[i].seq         =  0;
    this->streams[i].timestamp   =  0;
  }
  this->last_pts[PTS_VIDEO] = 0;
  this->last_pts[PTS_AUDIO] = 0;
  this->keyframe_ts = 0;
  this->keyframe_found = 0;

  /* engine sync stuff */
  this->send_newpts   = 1;
  this->buf_flag_seek = 1;

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    _x_demux_flush_engine(this->stream);

    start_time /= 1000;
    start_pos = (off_t) ( (double) start_pos / 65535 *
                this->input->get_length (this->input) );

    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate;

    if (start_pos < this->first_packet_pos)
      start_pos = this->first_packet_pos;

    /*
     * Find the previous keyframe
     *
     * states : 0  start, search a video keyframe
     *          1  video keyframe found, search an audio packet
     *          2  no video stream, search an audio packet
     *          5  end
     */
    state = 0;

    /* no video stream */
    if (this->video_stream == -1) {
      if (this->audio_stream == -1) {
        lprintf ("demux_asf_seek: no video stream, no audio stream\n");
        return this->status;
      } else {
        lprintf ("demux_asf_seek: no video stream\n");
        state = 2;
      }
    }

    /* force the demuxer to not send data to decoders */

    if (this->video_stream >= 0) {
      this->streams[this->video_stream].skip = 1;
      this->streams[this->video_stream].resync = 0;
    }
    if (this->audio_stream >= 0) {
      this->streams[this->audio_stream].skip = 1;
      this->streams[this->audio_stream].resync = 0;
    }

    start_pos -= (start_pos - this->first_packet_pos) % this->packet_size;
    while ((start_pos >= this->first_packet_pos) && (state != 5)) {
      uint32_t header_size;

      /* seek to the beginning of the previous packet */
      lprintf ("demux_asf_seek: seek back\n");

      if (this->input->seek (this->input, start_pos, SEEK_SET) != start_pos) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: demux_asf_seek: seek failed\n");
        goto error;
      }
      header_size = 0;
      if (asf_parse_packet_ecd(this, &header_size)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_parse_packet_ecd failed\n");
        this->status = DEMUX_FINISHED;
        return this->status;
      }
      if (asf_parse_packet_payload_header(this, header_size)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: asf_parse_packet_header failed\n");
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      for (this->frame = 0; this->frame < (this->nb_frames & 0x3f); this->frame++) {
        raw_id = get_byte(this); this->packet_size_left -= 1;

        lprintf ("demux_asf_seek: raw_id = %d\n", raw_id);

        stream_id = raw_id & 0x7f;
        if (asf_parse_packet_payload_common(this, raw_id, &stream, &frag_offset, &rlen))
          break;

        if (rlen == 1) {
          if (asf_parse_packet_compressed_payload(this, stream, raw_id, frag_offset, &ts))
            break;
        } else {
          if (asf_parse_packet_payload(this, stream, raw_id, frag_offset, rlen, &ts))
            break;
        }

        if (state == 0) {
          if (this->keyframe_found) {
            if (this->audio_stream == -1) {
              lprintf ("demux_asf_seek: no audio stream\n");
              state = 5;
            }
            state = 1; /* search an audio packet with pts < this->keyframe_pts */

            lprintf ("demux_asf_seek: keyframe found at %"PRId64", timestamp = %"PRId64"\n", start_pos, ts);
            check_newpts (this, ts * 90, 1, 0);
          }
        } else if (state == 1) {
          if ((this->audio_stream != -1 && stream_id == this->asf_header->streams[this->audio_stream]->stream_number) && ts &&
              (ts <= this->keyframe_ts)) {
            lprintf ("demux_asf_seek: audio packet found at %"PRId64", ts = %"PRId64"\n", start_pos, ts);

            state = 5; /* end */
            break;
          }
        } else if (state == 2) {
          if ((this->audio_stream != -1 && stream_id == this->asf_header->streams[this->audio_stream]->stream_number) && !frag_offset) {
            this->keyframe_found = 1;
            this->keyframe_ts = ts;
            state = 5; /* end */

            lprintf ("demux_asf_seek: audio packet found at %"PRId64", timestamp = %"PRId64"\n", start_pos, ts);
            check_newpts (this, ts * 90, 0, 0);
          }
        }
      }
      start_pos -= this->packet_size;
    }
    if (state != 5) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_asf: demux_asf_seek: beginning of the stream\n");
      this->input->seek (this->input, this->first_packet_pos, SEEK_SET);
      this->keyframe_found = 1;
    } else {
      this->input->seek (this->input, start_pos + this->packet_size, SEEK_SET);
    }
    lprintf ("demux_asf_seek: keyframe_found=%d, keyframe_ts=%"PRId64"\n",
             this->keyframe_found, this->keyframe_ts);
    if (this->video_stream >= 0) {
      this->streams[this->video_stream].resync = 1;
      this->streams[this->video_stream].skip   = 1;
    }
    if (this->audio_stream >= 0) {
      this->streams[this->audio_stream].resync = 1;
      this->streams[this->audio_stream].skip   = 1;
    }
  } else if (!playing && this->input->seek_time != NULL) {
    if (start_pos && !start_time)
      start_time = this->length * start_pos / 65535;

    this->input->seek_time (this->input, start_time, SEEK_SET);

    this->keyframe_ts = 0;
    this->keyframe_found = 0; /* means next keyframe */
    if (this->video_stream >= 0) {
      this->streams[this->video_stream].resync = 1;
      this->streams[this->video_stream].skip   = 1;
    }
    if (this->audio_stream >= 0) {
      this->streams[this->audio_stream].resync = 0;
      this->streams[this->audio_stream].skip   = 0;
    }
  } else {
    /* "streaming" mode */
    this->keyframe_ts = 0;
    this->keyframe_found = 0; /* means next keyframe */
    if (this->video_stream >= 0) {
      this->streams[this->video_stream].resync = 1;
      this->streams[this->video_stream].skip   = 1;
    }
    if (this->audio_stream >= 0) {
      this->streams[this->audio_stream].resync = 0;
      this->streams[this->audio_stream].skip   = 0;
    }
  }
  return this->status;

error:
  this->status = DEMUX_FINISHED;
  return this->status;
}

static int demux_asf_get_stream_length (demux_plugin_t *this_gen) {

  demux_asf_t *this = (demux_asf_t *) this_gen;

  return this->length;
}

static uint32_t demux_asf_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_asf_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
				    xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_asf_t       *this;
  uint8_t            buf[MAX_PREVIEW_SIZE+1];
  int                len;

  switch (stream->content_detection_method) {
  case METHOD_BY_CONTENT:

    /*
     * try to get a preview of the data
     */
    len = input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
    if (len == INPUT_OPTIONAL_UNSUPPORTED) {

      if (input->get_capabilities (input) & INPUT_CAP_SEEKABLE) {

	input->seek (input, 0, SEEK_SET);
	if ( (len=input->read (input, buf, 1024)) <= 0)
	  return NULL;

	lprintf ("PREVIEW data unavailable, but seek+read worked.\n");

      } else
	return NULL;
    }

    if (memcmp(buf, &guids[GUID_ASF_HEADER].guid, sizeof(GUID))) {
      buf[len] = '\0';
      if( !strstr(buf,"asx") &&
          !strstr(buf,"ASX") &&
          strncmp(buf,"[Reference]", 11) &&
          strncmp(buf,"ASF ", 4) &&
	  memcmp(buf, "\x30\x26\xB2\x75", 4)
	  )
        return NULL;
    }

    lprintf ("file starts with an asf header\n");

    break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_asf: warning, unknown method %d\n", stream->content_detection_method);
    return NULL;
  }

  this         = calloc(1, sizeof(demux_asf_t));
  this->stream = stream;
  this->input  = input;

  /*
   * check for reference stream
   */
  this->mode = ASF_MODE_NORMAL;
  len = input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
  if ( (len == INPUT_OPTIONAL_UNSUPPORTED) &&
       (input->get_capabilities (input) & INPUT_CAP_SEEKABLE) ) {
    input->seek (input, 0, SEEK_SET);
    len=input->read (input, buf, 1024);
  }
  if(len > 0) {
    buf[len] = '\0';
    if( strstr((char*)buf,"asx") || strstr((char*)buf,"ASX") )
      this->mode = ASF_MODE_ASX_REF;
    if( strstr((char*)buf,"[Reference]") )
      this->mode = ASF_MODE_HTTP_REF;
    if( strstr((char*)buf,"ASF ") )
      this->mode = ASF_MODE_ASF_REF;
  }

  this->demux_plugin.send_headers      = demux_asf_send_headers;
  this->demux_plugin.send_chunk        = demux_asf_send_chunk;
  this->demux_plugin.seek              = demux_asf_seek;
  this->demux_plugin.dispose           = demux_asf_dispose;
  this->demux_plugin.get_status        = demux_asf_get_status;
  this->demux_plugin.get_stream_length = demux_asf_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_asf_get_capabilities;
  this->demux_plugin.get_optional_data = demux_asf_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  return &this->demux_plugin;
}

static void *init_class (xine_t *xine, void *data) {

  demux_asf_class_t     *this;

  this         = calloc(1, sizeof(demux_asf_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("ASF demux plugin");
  this->demux_class.identifier      = "ASF";
  this->demux_class.mimetypes       =
    "video/x-ms-asf: asf: ASF stream;"
    "video/x-ms-wmv: wmv: Windows Media Video;"
    "audio/x-ms-wma: wma: Windows Media Audio;"
    "application/vnd.ms-asf: asf: ASF stream;"
    "application/x-mplayer2: asf,asx,asp: mplayer2;"
    "video/x-ms-asf-plugin: asf,asx,asp: mms animation;"
    "video/x-ms-wvx: wvx: wmv metafile;"
    "video/x-ms-wax: wva: wma metafile;";
  /* asx, wvx, wax are metafile or playlist */
  this->demux_class.extensions      = "asf wmv wma asx wvx wax";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}


/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_asf = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "asf", XINE_VERSION_CODE, &demux_info_asf, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
