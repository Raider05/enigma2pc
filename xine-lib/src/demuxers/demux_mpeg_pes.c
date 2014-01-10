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
 * demultiplexer for mpeg 2 PES (Packetized Elementary Streams)
 * reads streams of variable blocksizes
 *
 * 1-7-2003 New implementation of mpeg 2 PES demuxers.
 *   (c) 2003 James Courtier-Dutton James@superbug.demon.co.uk
 *   This code might also decode normal MPG files.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#define LOG_MODULE "demux_mpeg_pes"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#define NUM_PREVIEW_BUFFERS   250
#define DISC_TRESHOLD       90000

#define WRAP_THRESHOLD     270000
#define PTS_AUDIO 0
#define PTS_VIDEO 1


/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

typedef struct demux_mpeg_pes_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int                   status;

  int                   rate;

  char                  cur_mrl[256];

  uint8_t              *scratch;

  int64_t               nav_last_end_pts;
  int64_t               nav_last_start_pts;
  int64_t               last_pts[2];
  int64_t               scr;
  uint32_t              packet_len;
  uint32_t              stream_id;

  int64_t               pts;
  int64_t               dts;

  uint8_t               send_newpts:1;
  uint8_t               buf_flag_seek:1;
  uint8_t               preview_mode:1;
  uint8_t               mpeg1:1;
  uint8_t               wait_for_program_stream_pack_header:1;
  uint8_t               mpeg12_h264_detected:2;

  int                   last_begin_time;
  int64_t               last_cell_time;
  off_t                 last_cell_pos;

  uint8_t               preview_data[ MAX_PREVIEW_SIZE ];
  off_t                 preview_size, preview_done;
} demux_mpeg_pes_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mpeg_pes_class_t;

static int32_t parse_video_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_audio_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_ancillary_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_system_header(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_private_stream_1(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_private_stream_2(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_map(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_padding_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_ecm_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_emm_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_dsmcc_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_emm_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_iec_13522_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeA_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeB_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeC_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeD_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeE_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_IEC14496_SL_packetized_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_IEC14496_FlexMux_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_directory(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_pack_header(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf);

static int detect_pts_discontinuity( demux_mpeg_pes_t *this, int64_t pts, int video )
{
  int64_t diff;

  /* discontinuity detection is difficult to implement in the demuxer as it gets
   * for example video packets in decoding order and there can be multiple audio
   * and video tracks. So for simplicity, let's just deal with a single audio and
   * a single video track.
   *
   * To start with, let's have a look at the audio and video track independently.
   * Whenever pts differs from last_pts[video] by at least WRAP_THRESHOLD, a jump
   * in pts is detected. Such a jump can happen for example when the pts counter
   * overflows, as shown below (video decoding order ignored for simplicity; the
   * variable values are shown after returning from the below function check_newpts;
   * an asterisk means that this value has been cleared (see check_newpts)):
   *
   *         pts: 7v 7a 8v 9v 9a : 0v 1v 1a 2v 3v 3a 4v
   * last_pts[0]: 6  7  7  7  9  : *  *  1  1  1  3  3
   * last_pts[1]: 7  7  8  9  9  : 0  1  1  2  3  3  4
   *                             | |     |
   *                             | |     +--- audio pts wrap ignored
   *                             | +--------- video pts wrap detected
   *                             +----------- pts wrap boundary
   */
  diff = pts - this->last_pts[video];

  if (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD)
    return 1;

  /* but the above code can cause a huge delay while replaying when audio and video
   * track are not aligned on a common pts wrap boundery, as shown below:
   *
   *         pts: 7v 8v 7a 9v : 0v 9a 1v 2v : 1a 3v 4v 3a
   * last_pts[0]: 6  6  7  7  : *  9  9  9  : 1  1  1  3
   * last_pts[1]: 7  8  8  9  : 0  0  1  2  : *  3  4  4
   *                          | |  |        | |
   *                          | |  |        | +--- audio pts wrap detected
   *                          | |  |        +----- audio pts wrap boundary
   *                          | |  +-------------- audio packet causes a huge delay
   *                          | +----------------- video pts wrap detected
   *                          +------------------- video pts wrap boundery
   *
   * So there is the need to compare audio track pts against video track pts
   * to detect when pts values are in between pts wrap bounderies, where a
   * jump needs to be detected too, as shown below:
   *
   *         pts: 7v 8v 7a 9v : 0v 9a 1v 2v : 1a 3v 4v 3a
   * last_pts[0]: 6  6  7  7  : *  9  *  *  : 1  1  1  3
   * last_pts[1]: 7  8  8  9  : 0  *  1  2  : 2  3  4  4
   *                          | |  |  |     | |
   *                          | |  |  |     | +--- (audio pts wrap ignored)
   *                          | |  |  |     +----- audio pts wrap boundary
   *                          | |  |  +----------- video pts wrap detected
   *                          | |  +-------------- audio pts wrap detected
   *                          | +----------------- video pts wrap detected
   *                          +------------------- (video pts wrap boundery)
   *
   * Basically, it's almost the same test like above, but against the other track's
   * pts value and with a different limit. As the pts counter is a 33 bit unsigned
   * integer, we choose 2^31 as limit (2^32 would require the tracks to be aligned).
   */
  diff = pts - this->last_pts[1-video];

  if (this->last_pts[1-video] && abs(diff)>(1u<<31))
    return 1;

  /* no discontinuity detected */
  return 0;
}

static void check_newpts( demux_mpeg_pes_t *this, int64_t pts, int video )
{
  if( pts && (this->send_newpts || detect_pts_discontinuity(this, pts, video) ) ) {

    /* check if pts is outside nav pts range. any stream without nav must enter here. */
    if( pts > this->nav_last_end_pts || pts < this->nav_last_start_pts )
    {
      lprintf("discontinuity detected by pts wrap\n");

      if (this->buf_flag_seek) {
        _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
        this->buf_flag_seek = 0;
      } else {
        _x_demux_control_newpts(this->stream, pts, 0);
      }
      this->send_newpts = 0;
    } else {
      lprintf("no wrap detected\n" );
    }

    /* clear pts on the other track to avoid detecting the same discontinuity again */
    this->last_pts[1-video] = 0;
  }

  if( pts )
    this->last_pts[video] = pts;
}

static off_t read_data(demux_mpeg_pes_t *this, uint8_t *buf, off_t nlen)
{
  int preview_avail;

  if (this->preview_size <= 0)
    return this->input->read(this->input, (char *)buf, nlen);

  preview_avail = this->preview_size - this->preview_done;
  if (preview_avail <= 0)
    return 0;

  if (nlen > preview_avail)
    nlen = preview_avail;

  memcpy(buf, &this->preview_data[ this->preview_done ], nlen);
  this->preview_done += nlen;

  return nlen;
}

static void demux_mpeg_pes_parse_pack (demux_mpeg_pes_t *this, int preview_mode) {

  buf_element_t *buf = NULL;
  uint8_t       *p;
  int32_t        result;
  off_t          i;
  uint8_t        buf6[ 6 ];

  this->scr = 0;
  this->preview_mode = preview_mode;

  /* read first 6 bytes of PES packet into a local buffer. */
  i = read_data(this, buf6, (off_t) 6);
  if (i != 6) {
    this->status = DEMUX_FINISHED;
    return;
  }

  p = buf6;

  while ((p[2] != 1) || p[0] || p[1]) {
    /* resync code */
    memmove(p, p+1, 5);
    i = read_data(this, p+5, (off_t) 1);
    if (i != 1) {
      this->status = DEMUX_FINISHED;
      return;
    }
  }

  /* FIXME: buf must be allocated from somewhere before calling here. */

  /* these streams should be allocated on the audio_fifo, if available. */
  if ((0xC0 <= p[ 3 ] && p[ 3 ] <= 0xDF) /* audio_stream */
      || 0xBD == p[ 3 ])                 /* private_sream_1 */
  {
    if (this->audio_fifo)
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  }

  if (!buf)  /* still no buffer => try video fifo first. */
  {
    if (this->video_fifo) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    } else if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    } else {
      return;
    }
  }

  p = buf->mem;

  /* copy local buffer to fifo element. */
  memcpy(p, buf6, sizeof(buf6));

  if (preview_mode)
    buf->decoder_flags = BUF_FLAG_PREVIEW;
  else
    buf->decoder_flags = 0;

  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );

    this->stream_id  = p[3];
    if (this->stream_id == 0xBA) {
      this->wait_for_program_stream_pack_header=0;
      /* This just fills this->scr, this->rate and this->mpeg1 */
      result = parse_program_stream_pack_header(this, p, buf);
      return;
    } else if (this->stream_id == 0xB9) {
      /* End of stream marker */
      buf->free_buffer (buf);
      return;
    } else if (this->stream_id < 0xB9) {
      /* FIXME: This should only be tested for after a seek. */
      buf->free_buffer (buf);
      return;
    }
#if 0
    /* FIXME: #if 0 while trying to detect mpeg1 in parse_pes_for_pts() */
    if (this->wait_for_program_stream_pack_header==1) {
      /* Wait until this->mpeg1 has been initialised. */
      buf->free_buffer (buf);
      return;
    }
#endif

    this->packet_len = p[4] << 8 | p[5];
    lprintf("stream_id=0x%x, packet_len=%d\n",this->stream_id, this->packet_len);

    if (this->packet_len <= (buf->max_size - 6)) {
      i = read_data(this, buf->mem+6, (off_t) this->packet_len);
      if (i != this->packet_len) {
        buf->free_buffer (buf);
        this->status = DEMUX_FINISHED;
        return;
      }
      buf->size = this->packet_len + 6;
    } else {
      lprintf("Jumbo PES packet length=%d, stream_id=0x%x\n",this->packet_len, this->stream_id);

      i = read_data(this, buf->mem+6, (off_t) (buf->max_size - 6));
      if (i != ( buf->max_size - 6)) {
        buf->free_buffer (buf);
        this->status = DEMUX_FINISHED;
        return;
      }
      buf->size = buf->max_size;
    }

    if (this->stream_id == 0xBB) {
      result = parse_program_stream_system_header(this, p, buf);
    } else if (this->stream_id == 0xBC) {
      result = parse_program_stream_map(this, p, buf);
    } else if (this->stream_id == 0xBD) {
      result = parse_private_stream_1(this, p, buf);
    } else if (this->stream_id == 0xBE) {
      result = parse_padding_stream(this, p, buf);
    } else if (this->stream_id == 0xBF) {
      buf->free_buffer (buf);
      return;
      result = parse_private_stream_2(this, p, buf);
    } else if ((this->stream_id >= 0xC0)
            && (this->stream_id <= 0xDF)) {
      result = parse_audio_stream(this, p, buf);
    } else if ((this->stream_id >= 0xE0)
            && (this->stream_id <= 0xEF)) {
      result = parse_video_stream(this, p, buf);
    } else if (this->stream_id == 0xF0) {
      result = parse_ecm_stream(this, p, buf);
    } else if (this->stream_id == 0xF1) {
      result = parse_emm_stream(this, p, buf);
    } else if (this->stream_id == 0xF2) {
      result = parse_dsmcc_stream(this, p, buf);
    } else if (this->stream_id == 0xF3) {
      result = parse_iec_13522_stream(this, p, buf);
    } else if (this->stream_id == 0xF4) {
      result = parse_h222_typeA_stream(this, p, buf);
    } else if (this->stream_id == 0xF5) {
      result = parse_h222_typeB_stream(this, p, buf);
    } else if (this->stream_id == 0xF6) {
      result = parse_h222_typeC_stream(this, p, buf);
    } else if (this->stream_id == 0xF7) {
      result = parse_h222_typeD_stream(this, p, buf);
    } else if (this->stream_id == 0xF8) {
      result = parse_h222_typeE_stream(this, p, buf);
    } else if (this->stream_id == 0xF9) {
      result = parse_ancillary_stream(this, p, buf);
    } else if (this->stream_id == 0xFA) {
      result = parse_IEC14496_SL_packetized_stream(this, p, buf);
    } else if (this->stream_id == 0xFB) {
      result = parse_IEC14496_FlexMux_stream(this, p, buf);
    /* 0xFC, 0xFD, 0xFE reserved */
    } else if (this->stream_id == 0xFF) {
      result = parse_program_stream_directory(this, p, buf);
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("xine-lib:demux_mpeg_pes: Unrecognised stream_id 0x%02x. "
		"Please report this to xine developers.\n"), this->stream_id);
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "xine-lib:demux_mpeg_pes: packet_len=%d\n", this->packet_len);
      buf->free_buffer (buf);
      return;
    }
    if (result < 0) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("demux_mpeg_pes: warning: PACK stream id=0x%x decode failed.\n"), this->stream_id);
      /* What to do here? */
      return;
    }
  return ;
}

static int32_t parse_padding_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* Just skip padding. */
  int todo = 6 + this->packet_len;
  int done = buf->size;

  while (done < todo)
  {
    /* Handle Jumbo frames from VDR. */
    int i;

    int size = buf->max_size;
    if ((todo - done) < size)
      size = todo - done;

    i = read_data(this, buf->mem, (off_t)size);
    if (i != size)
      break;

    done += i;
  }

  /* trigger detection of MPEG 1/2 respectively H.264 content */
  this->mpeg12_h264_detected = 0;

  buf->free_buffer(buf);
  return this->packet_len + 6;
}

static int32_t parse_program_stream_map(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x.\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_ecm_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_emm_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_dsmcc_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_iec_13522_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeA_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeB_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeC_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeD_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeE_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_IEC14496_SL_packetized_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_IEC14496_FlexMux_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_program_stream_directory(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_ancillary_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_pes: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}

static int32_t parse_program_stream_pack_header(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* program stream pack header */
  off_t          i;

  i = read_data(this, buf->mem+6, (off_t) 6);
  if (i != 6) {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
    return -1;
  }
  this->mpeg1 = (p[4] & 0x40) == 0;

  if (this->mpeg1) {
  /* system_clock_reference */

    this->scr  = (int64_t)(p[4] & 0x02) << 30;
    this->scr |= (p[5] & 0xFF) << 22;
    this->scr |= (p[6] & 0xFE) << 14;
    this->scr |= (p[7] & 0xFF) <<  7;
    this->scr |= (p[8] & 0xFE) >>  1;

    /* buf->scr = scr; */

    /* mux_rate */

    if (!this->rate) {
      this->rate = (p[9] & 0x7F) << 15;
      this->rate |= (p[10] << 7);
      this->rate |= (p[11] >> 1);
    }

    buf->free_buffer (buf);
    return 12;

  } else { /* mpeg2 */

    int      num_stuffing_bytes;

    /* system_clock_reference */

    this->scr  = (int64_t)(p[4] & 0x08) << 27 ;
    this->scr |= (p[4] & 0x03) << 28 ;
    this->scr |= p[5] << 20;
    this->scr |= (p[6] & 0xF8) << 12 ;
    this->scr |= (p[6] & 0x03) << 13 ;
    this->scr |= p[7] << 5;
    this->scr |= (p[8] & 0xF8) >> 3;
    /*  optional - decode extension:
    this->scr *=300;
    this->scr += ( (p[8] & 0x03 << 7) | (p[9] & 0xFE >> 1) );
    */

    lprintf ("SCR=%"PRId64"\n", this->scr);

    /* mux_rate */

    if (!this->rate) {
      this->rate = (p[0xA] << 14);
      this->rate |= (p[0xB] << 6);
      this->rate |= (p[0xC] >> 2);
    }
    i = read_data(this, buf->mem+12, (off_t) 2);
    if (i != 2) {
      buf->free_buffer (buf);
      this->status = DEMUX_FINISHED;
      return -1;
    }

    num_stuffing_bytes = p[0xD] & 0x07;
    i = read_data(this, buf->mem+14, (off_t) num_stuffing_bytes);
    if (i != num_stuffing_bytes) {
      buf->free_buffer (buf);
      this->status = DEMUX_FINISHED;
      return -1;
    }

    buf->free_buffer (buf);
    return 14 + num_stuffing_bytes;
  }

}

static int32_t parse_program_stream_system_header(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  /* program stream system header */
  /* FIXME: Implement */
  buf->free_buffer (buf);
  return 6 + this->packet_len;
}

static int32_t parse_private_stream_2(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  int64_t start_pts, end_pts;

  /* NAV Packet */

  start_pts  = ((int64_t)p[7+12] << 24);
  start_pts |= (p[7+13] << 16);
  start_pts |= (p[7+14] << 8);
  start_pts |= p[7+15];

  end_pts  = ((int64_t)p[7+16] << 24);
  end_pts |= (p[7+17] << 16);
  end_pts |= (p[7+18] << 8);
  end_pts |= p[7+19];

  /* some input plugins like DVD can have better timing information and have
   * already set the input_time, so we can use the cell elapsed time from
   * the NAV packet for a much more accurate timing */
  if (buf->extra_info->input_time) {
    int64_t cell_time, frames;

    cell_time  = (p[7+0x18] >> 4  ) * 10 * 60 * 60 * 1000;
    cell_time += (p[7+0x18] & 0x0f)      * 60 * 60 * 1000;
    cell_time += (p[7+0x19] >> 4  )      * 10 * 60 * 1000;
    cell_time += (p[7+0x19] & 0x0f)           * 60 * 1000;
    cell_time += (p[7+0x1a] >> 4  )           * 10 * 1000;
    cell_time += (p[7+0x1a] & 0x0f)                * 1000;
    frames  = ((p[7+0x1b] & 0x30) >> 4) * 10;
    frames += ((p[7+0x1b] & 0x0f)     )     ;

    if (p[7+0x1b] & 0x80)
      cell_time += (frames * 1000)/25;
    else
      cell_time += (frames * 1000)/30;

    this->last_cell_time = cell_time;
    this->last_cell_pos = this->input->get_current_pos (this->input);
    this->last_begin_time = buf->extra_info->input_time;
  }

  lprintf ("NAV packet, start pts = %"PRId64", end_pts = %"PRId64"\n",
           start_pts, end_pts);

  if (this->nav_last_end_pts != start_pts && !this->preview_mode) {

    lprintf("discontinuity detected by nav packet\n" );

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, start_pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, start_pts, 0);
    }
  }
  this->nav_last_end_pts = end_pts;
  this->nav_last_start_pts = start_pts;
  this->send_newpts = 0;
  this->last_pts[PTS_AUDIO] = this->last_pts[PTS_VIDEO] = 0;

  buf->content   = p;
  buf->size      = this->packet_len;
  buf->type      = BUF_SPU_DVD;
  buf->decoder_flags |= BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
  buf->decoder_info[2] = SPU_DVD_SUBTYPE_NAV;
  buf->pts       = 0;   /* NAV packets do not have PES values */
  this->video_fifo->put (this->video_fifo, buf);

  return this->packet_len;
}

/* FIXME: Extension data is not parsed, and is also not skipped. */

static int32_t parse_pes_for_pts(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  int32_t header_len;

  /* some input plugins like DVD can have better timing information and have
   * already set the total_time, so we can derive our datarate from this */
  if (buf->extra_info->total_time)
    this->rate = (int)((int64_t)this->input->get_length (this->input) * 1000 /
                       (buf->extra_info->total_time * 50));

  if (this->rate && this->last_cell_time) {
    if( this->last_begin_time == buf->extra_info->input_time )
      buf->extra_info->input_time = this->last_cell_time + buf->extra_info->input_time +
       ((this->input->get_current_pos (this->input) - this->last_cell_pos) * 1000 / (this->rate * 50));
  }

  if (this->rate && !buf->extra_info->input_time)
    buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                        * 1000 / (this->rate * 50));

  /* FIXME: This was determined by comparing a single MPEG1 and a single MPEG2 stream */
  if ((p[6] & 0xC0) != 0x80) {
    this->mpeg1 = 1;
  } else {
    this->mpeg1 = 0;
  }

  if (this->mpeg1) {
    header_len = 6;
    p   += 6; /* packet_len -= 6; */

    while ((p[0] & 0x80) == 0x80) {
      p++;
      header_len++;
      this->packet_len--;
      /* printf ("stuffing\n");*/
    }

    if ((p[0] & 0xc0) == 0x40) {
      /* STD_buffer_scale, STD_buffer_size */
      p += 2;
      header_len += 2;
      this->packet_len -= 2;
    }

    this->pts = 0;
    this->dts = 0;

    if ((p[0] & 0xf0) == 0x20) {
      this->pts  = (int64_t) (p[ 0] & 0x0E) << 29 ;
      this->pts |= (int64_t)  p[ 1]         << 22 ;
      this->pts |= (int64_t) (p[ 2] & 0xFE) << 14 ;
      this->pts |= (int64_t)  p[ 3]         <<  7 ;
      this->pts |= (int64_t) (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      header_len+= 5;
      this->packet_len -=5;
      return header_len;
    } else if ((p[0] & 0xf0) == 0x30) {
      this->pts  = (int64_t) (p[ 0] & 0x0E) << 29 ;
      this->pts |= (int64_t)  p[ 1]         << 22 ;
      this->pts |= (int64_t) (p[ 2] & 0xFE) << 14 ;
      this->pts |= (int64_t)  p[ 3]         <<  7 ;
      this->pts |= (int64_t) (p[ 4] & 0xFE) >>  1 ;

      this->dts  = (int64_t) (p[ 5] & 0x0E) << 29 ;
      this->dts |= (int64_t)  p[ 6]         << 22 ;
      this->dts |= (int64_t) (p[ 7] & 0xFE) << 14 ;
      this->dts |= (int64_t)  p[ 8]         <<  7 ;
      this->dts |= (int64_t) (p[ 9] & 0xFE) >>  1 ;

      p   += 10;
      header_len += 10;
      this->packet_len -= 10;
      return header_len;
    } else {
      p++;
      header_len++;
      this->packet_len--;
      return header_len;
    }

  } else { /* mpeg 2 */


    if ((p[6] & 0xC0) != 0x80) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("demux_mpeg_pes: warning: PES header reserved 10 bits not found\n"));
      buf->free_buffer(buf);
      return -1;
    }


    /* check PES scrambling_control */

    if ((p[6] & 0x30) != 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("demux_mpeg_pes: warning: PES header indicates that "
		"this stream may be encrypted (encryption mode %d)\n"), (p[6] & 0x30) >> 4);
      _x_message (this->stream, XINE_MSG_ENCRYPTED_SOURCE,
                      "Media stream scrambled/encrypted", NULL);
      this->status = DEMUX_FINISHED;
      buf->free_buffer(buf);
      return -1;
    }

    if (p[7] & 0x80) { /* pts avail */

      this->pts  = (int64_t) (p[ 9] & 0x0E) << 29 ;
      this->pts |= (int64_t)  p[10]         << 22 ;
      this->pts |= (int64_t) (p[11] & 0xFE) << 14 ;
      this->pts |= (int64_t)  p[12]         <<  7 ;
      this->pts |= (int64_t) (p[13] & 0xFE) >>  1 ;

      lprintf ("pts = %"PRId64"\n", this->pts);

    } else
      this->pts = 0;

    if (p[7] & 0x40) { /* dts avail */

      this->dts  = (int64_t) (p[14] & 0x0E) << 29 ;
      this->dts |= (int64_t)  p[15]         << 22 ;
      this->dts |= (int64_t) (p[16] & 0xFE) << 14 ;
      this->dts |= (int64_t)  p[17]         <<  7 ;
      this->dts |= (int64_t) (p[18] & 0xFE) >>  1 ;

    } else
      this->dts = 0;


    header_len = p[8];

    this->packet_len -= header_len + 3;
    return header_len + 9;
  }
  return 0;
}

static int32_t parse_private_stream_1(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {

    int track, spu_id;
    int32_t result;

    result = parse_pes_for_pts(this, p, buf);
    if (result < 0) return -1;

    p += result;
    /* printf("demux_mpeg_pes: private_stream_1: p[0] = 0x%02X\n", p[0]); */

    if((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      buf->content   = p+1;
      buf->size      = this->packet_len-1;

      buf->type      = BUF_SPU_DVD + spu_id;
      buf->decoder_flags |= BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
      buf->decoder_info[2] = SPU_DVD_SUBTYPE_PACKAGE;
      buf->pts       = this->pts;

      this->video_fifo->put (this->video_fifo, buf);
      lprintf ("SPU PACK put on fifo\n");

      return this->packet_len + result;
    }

    /* SVCD OGT subtitles in stream 0x70 */
    if(p[0] == 0x70 && (p[1] & 0xFC) == 0x00) {
      spu_id = p[1];

      buf->content   = p+1;
      buf->size      = this->packet_len-1;
      buf->type      = BUF_SPU_SVCD + spu_id;
      buf->pts       = this->pts;
      /* this is probably wrong:
      if( !preview_mode )
        check_newpts( this, this->pts, PTS_VIDEO );
      */
      this->video_fifo->put (this->video_fifo, buf);
      lprintf ("SPU SVCD PACK (%"PRId64", %d) put on fifo\n", this->pts, spu_id);

      return this->packet_len + result;
    }

    /* SVCD CVD subtitles in streams 0x00-0x03 */
    if((p[0] & 0xFC) == 0x00) {
      spu_id = (p[0] & 0x03);

      buf->content   = p+1;
      buf->size      = this->packet_len-1;
      buf->type      = BUF_SPU_CVD + spu_id;
      buf->pts       = this->pts;
      /* this is probably wrong:
      if( !preview_mode )
        check_newpts( this, this->pts, PTS_VIDEO );
      */
      this->video_fifo->put (this->video_fifo, buf);
      lprintf ("SPU CVD PACK (%"PRId64", %d) put on fifo\n", this->pts, spu_id);

      return this->packet_len + result;
    }

    if ((p[0]&0xF0) == 0x80) {

      track = p[0] & 0x0F; /* hack : ac3 track */
      buf->decoder_info[1] = p[1]; /* Number of frame headers */
      buf->decoder_info[2] = p[2] << 8 | p[3]; /* First access unit pointer */

      buf->content   = p+4;
      buf->size      = this->packet_len-4;
      if (track & 0x8) {
        buf->type      = BUF_AUDIO_DTS + (track & 0x07); /* DVDs only have 8 tracks */
      } else {
        buf->type      = BUF_AUDIO_A52 + track;
      }
      buf->pts       = this->pts;
      if( !this->preview_mode )
        check_newpts( this, this->pts, PTS_AUDIO );

      if(this->audio_fifo) {
	this->audio_fifo->put (this->audio_fifo, buf);
        lprintf ("A52 PACK put on fifo\n");

      } else {
	buf->free_buffer(buf);
      }
      return this->packet_len + result;

    /* EVOB AC3/E-AC-3 */
    } else if ((p[0]&0xf0) == 0xc0) {

      track = p[0] & 0x0F; /* hack : ac3 track */
      buf->decoder_info[1] = p[1]; /* Number of frame headers */
      buf->decoder_info[2] = p[2] << 8 | p[3]; /* First access unit pointer */

      buf->content   = p+4;
      buf->size      = this->packet_len-4;
      if (p[4] == 0x0b && p[5] == 0x77 && ((p[9] >> 3) & 0x1f) <= 8) {
        buf->type      = BUF_AUDIO_A52 + track;
      } else {
        buf->type      = BUF_AUDIO_EAC3 + track;
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      }
      buf->pts       = this->pts;
      if( !this->preview_mode )
        check_newpts( this, this->pts, PTS_AUDIO );

      if(this->audio_fifo) {
        this->audio_fifo->put (this->audio_fifo, buf);
        lprintf ("A52/EAC3 PACK put on fifo\n");

      } else {
        buf->free_buffer(buf);
      }
      return this->packet_len + result;

    } else if ((p[0]&0xf0) == 0xa0) {

      int pcm_offset;
#if 0
      int number_of_frame_headers;
      int first_access_unit_pointer;
      int audio_frame_number;
      int bits_per_sample;
      int sample_rate;
      int num_channels;
      int dynamic_range;
#endif
      /*
       * found in http://members.freemail.absa.co.za/ginggs/dvd/mpeg2_lpcm.txt
       * appears to be correct.
       */

      track = p[0] & 0x0F;
#if 0
      number_of_frame_headers = p[1];
      /* unknown = p[2]; */
      first_access_unit_pointer = p[3];
      audio_frame_number = p[4];

      /*
       * 000 => mono
       * 001 => stereo
       * 010 => 3 channel
       * ...
       * 111 => 8 channel
       */
      num_channels = (p[5] & 0x7) + 1;
      switch ((p[5]>>4) & 3) {
      case 0: sample_rate = 48000; break;
      case 1: sample_rate = 96000; break;
      case 2: sample_rate = 44100; break;
      case 3: sample_rate = 32000; break;
      }
      switch ((p[5]>>6) & 3) {
      case 3: /* illegal, use 16-bits? */
      default:
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "illegal lpcm sample format (%d), assume 16-bit samples\n",
		(p[5]>>6) & 3 );
      case 0: bits_per_sample = 16; break;
      case 1: bits_per_sample = 20; break;
      case 2: bits_per_sample = 24; break;
      }
      dynamic_range = p[6];
#endif

      /* send lpcm config byte */
      buf->decoder_flags |= BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      buf->decoder_info[2] = p[5];

      pcm_offset = 7;

      buf->content   = p+pcm_offset;
      buf->size      = this->packet_len-pcm_offset;
      buf->type      = BUF_AUDIO_LPCM_BE + track;
      buf->pts       = this->pts;
      if( !this->preview_mode )
        check_newpts( this, this->pts, PTS_AUDIO );

      if(this->audio_fifo) {
	this->audio_fifo->put (this->audio_fifo, buf);
        lprintf ("LPCM PACK put on fifo\n");

      } else {
	buf->free_buffer(buf);
      }
      return this->packet_len + result;

    } else if((p[0]==0x0b) && (p[1]==0x77)) {
      int offset;
      int size;

      /*
       * A52/AC3 streams in some DVB-S recordings made with VDR.
       * It is broadcast by a german tv-station called PRO7.
       * PRO7 uses dolby 5.1 (A52 5.1) in some of the movies they broadcast,
       * (and they would switch it to stereo-sound(A52 2.0) during commercials.)
       * Here is the coresponding line from a channel.conf for the astra-satelite:
       * Pro-7:12480:v:S19.2E:27500:255:256;257:32:0:898:0:0:0
       */

      buf->decoder_info[1] = 0; /* Number of frame headers */
      buf->decoder_info[2] = 0; /* First access unit pointer */

      buf->content   = p;
      size = this->packet_len;
      if ((size + result) > buf->max_size) {
        size = buf->max_size - result;
      }
      buf->size      = size;
      buf->type      = BUF_AUDIO_A52;
      buf->pts       = this->pts;
      if( !this->preview_mode )
        check_newpts( this, this->pts, PTS_AUDIO );

      this->audio_fifo->put (this->audio_fifo, buf);
      lprintf ("A52 PACK put on fifo\n");

      if (size == this->packet_len) {
        return this->packet_len + result;
      }

      /* Handle Jumbo A52 frames from VDR. */
      offset = size;
      while (offset < this->packet_len) {
        int i;
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        size = this->packet_len - offset;
        if (size > buf->max_size)
          size = buf->max_size;
        offset += size;
        i = read_data(this, buf->mem, (off_t) (size));
        if (i != size) {
          buf->free_buffer(buf);
          return this->packet_len + result;
        }
        buf->content   = buf->mem;
        buf->size      = size;
        buf->type      = BUF_AUDIO_A52;
        buf->pts       = 0;

        if(this->audio_fifo) {
	  this->audio_fifo->put (this->audio_fifo, buf);
          lprintf ("A52 PACK put on fifo\n");
        } else {
	  buf->free_buffer(buf);
        }
      }

      return this->packet_len + result;
    }



    /* Some new streams have been encountered.
       1) DVD+RW disc recorded with a Philips DVD recorder: -  new unknown sub-stream id of 0xff
     */
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("demux_mpeg_pes:Unrecognised private stream 1 0x%02x. Please report this to xine developers.\n"), p[0]);
    buf->free_buffer(buf);
    return this->packet_len + result;
}

static int32_t parse_video_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {
  int32_t result;
  uint32_t todo_length=0;
  uint32_t i;
  uint32_t chunk_length;
  int buf_type = BUF_VIDEO_MPEG;
  int payload_size;

  result = parse_pes_for_pts(this, p, buf);
  if (result < 0) return -1;

  p += result;

  buf->content = p;
  payload_size = buf->max_size - result;
  if (payload_size > this->packet_len)
    payload_size = this->packet_len;

  /* H.264 broadcasts via DVB-S use standard video PES packets,
     so there is no way other than scanning the video data to
     detect whether BUF_VIDEO_H264 needs to be used.
     For performance reasons, this is not a general scanner for
     H.264 content, as this kind of data format is likely to be
     used only by VDR and VDR will ensure that an AUD-NAL unit
     will be at the beginning of the PES packet's payload.
     To minimize false hits, the whole payload is scanned for
     MPEG 1/2 start codes, so there is only a little chance left
     that a MPEG 1/2 slice 9 start code will be considered as a
     H.264 access unit delimiter (should only happen after a seek).

     Meaning of bit 0 and 1 of mpeg12_h264_detected:
     Bit 0: H.264 access unit delimiter seen
     Bit 1: H.264 AUD seen again or MPEG 1/2 start code seen

     For performance reasons, the scanner is only active until
     a H.264 AUD has been seen a second time or a MPEG 1/2 start
     code has been seen. The scanner get's activated initially
     (e. g. when opening the stream), after seeking or when VDR
     sends a padding packet.
     Until the scanner is convinced of it's decision by setting
     bit 1, the default behaviour is to assume MPEG 1/2 unless
     an AUD has been found at the beginning of the payload.
   */
  if (this->mpeg12_h264_detected < 2) {
    uint8_t *pp = p + 2, *pp_limit = p + payload_size - 1;
    while (0 < pp && pp < pp_limit) {
      if (pp[0] == 0x01 && pp[-1] == 0x00 && pp[-2] == 0x00) {
        if (pp[1] >= 0x80 || !pp[1]) { /* MPEG 1/2 start code */
          this->mpeg12_h264_detected = 2;
          break;
        } else {
          int nal_type_code = pp[1] & 0x1f;
          if (nal_type_code == 9 && pp == (p + 2)) { /* access unit delimiter */
            if (this->mpeg12_h264_detected == 1) {
              this->mpeg12_h264_detected = 3;
              break;
            }
            this->mpeg12_h264_detected = 1;
          }
        }
      }
      pp++;
      pp = memchr(pp, 0x01, pp_limit - pp);
    }
    lprintf("%s%c\n", (this->mpeg12_h264_detected & 1) ? "H.264" : "MPEG1/2", (this->mpeg12_h264_detected & 2) ? '!' : '?');
  }

  /* when an H.264 AUD is seen, we first need to tell the decoder that the
     previous frame was complete.
   */
  if (this->mpeg12_h264_detected & 1) {
    buf_type = BUF_VIDEO_H264;
    /* omit sending BUF_FLAG_FRAME_END for the first AUD occurence */
    if (this->mpeg12_h264_detected > 2) {
      int nal_type_code = -1;
      if (payload_size >= 4 && p[2] == 0x01 && p[1] == 0x00 && p[0] == 0x00)
        nal_type_code = p[3] & 0x1f;
      if (nal_type_code == 9) { /* access unit delimiter */
        buf_element_t *b = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        b->content       = b->mem;
        b->size          = 0;
        b->pts           = 0;
        b->type          = buf_type;
        b->decoder_flags = BUF_FLAG_FRAME_END | (this->preview_mode ? BUF_FLAG_PREVIEW : 0);
        this->video_fifo->put (this->video_fifo, b);
      }
    }
  }

  if (this->packet_len <= (buf->max_size - result)) {
    buf->size = this->packet_len;
    /* VDR ensures that H.264 still images end with an end of sequence NAL unit. We
       need to detect this to inform the decoder that the current frame is complete.
     */
    if (this->mpeg12_h264_detected & 1) {
      uint8_t *t = buf->content + buf->size;
      if (buf->size >=4 && t[-1] == 10 && t[-2] == 0x01 && t[-3] == 0x00 && t[-4] == 0x00) /* end of sequence */
        buf->decoder_flags = BUF_FLAG_FRAME_END | (this->preview_mode ? BUF_FLAG_PREVIEW : 0);
    }
  } else {
    buf->size    = buf->max_size - result;
    todo_length  = this->packet_len - buf->size;
  }
  buf->type      = buf_type;
  buf->pts       = this->pts;
  buf->decoder_info[0] = this->pts - this->dts;
  if( !this->preview_mode )
    check_newpts( this, this->pts, PTS_VIDEO );

  this->video_fifo->put (this->video_fifo, buf);
  while (todo_length > 0) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    if (todo_length < buf->max_size) {
      chunk_length = todo_length;
    } else {
      chunk_length = buf->max_size;
    }
    i = read_data(this, buf->mem, (off_t) (chunk_length));
      if (i !=  chunk_length) {
        buf->free_buffer (buf);
        this->status = DEMUX_FINISHED;
        return -1;
      }
    buf->content   = buf->mem;
    buf->size      = chunk_length;
    buf->type      = buf_type;
    buf->pts       = 0;
    todo_length -= chunk_length;

    /* VDR ensures that H.264 still images end with an end of sequence NAL unit. We
       need to detect this to inform the decoder that the current frame is complete.
     */
    if ((this->mpeg12_h264_detected & 1) && todo_length <= 0) {
      uint8_t *t = buf->content + buf->size;
      if (buf->size >= 4 && t[-1] == 10 && t[-2] == 0x01 && t[-3] == 0x00 && t[-4] == 0x00) /* end of sequence */
        buf->decoder_flags = BUF_FLAG_FRAME_END | (this->preview_mode ? BUF_FLAG_PREVIEW : 0);
    }

    this->video_fifo->put (this->video_fifo, buf);
  }

  lprintf ("MPEG Video PACK put on fifo\n");

  return this->packet_len + result;
}

static int32_t parse_audio_stream(demux_mpeg_pes_t *this, uint8_t *p, buf_element_t *buf) {

  int track;
  int32_t result;

  result = parse_pes_for_pts(this, p, buf);
  if (result < 0) return -1;

  p += result;

  track = this->stream_id & 0x1f;

  buf->content   = p;
  buf->size      = this->packet_len;
  buf->type      = BUF_AUDIO_MPEG + track;
  buf->pts       = this->pts;
  if( !this->preview_mode )
      check_newpts( this, this->pts, PTS_AUDIO );

  if(this->audio_fifo) {
    this->audio_fifo->put (this->audio_fifo, buf);
    lprintf ("MPEG Audio PACK put on fifo\n");
  } else {
    buf->free_buffer(buf);
  }

  return this->packet_len + result;
}

static int demux_mpeg_pes_send_chunk (demux_plugin_t *this_gen) {

  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;

  demux_mpeg_pes_parse_pack(this, 0);

  return this->status;
}

#ifdef ESTIMATE_RATE_FIXED
/*!
   Estimate bitrate by looking inside the MPEG file for presentation
   time stamps (PTS) and computing how far apart these are
   in bytes and in time.

   On failure return 0.

   This might be used after deciding that mux_rate in a stream is faulty.

*/

/* How many *sucessful* PTS samples do we take? */
#define MAX_SAMPLES 5

/* How many times we read blocks before giving up. */
#define MAX_READS 30

/* TRUNCATE x to the nearest multiple of y. */
#define TRUNC(x,y) (((x) / (y)) * (y))

static int demux_mpeg_pes_estimate_rate (demux_mpeg_pes_t *this) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            is_mpeg1=0;
  off_t          pos, last_pos=0;
  off_t          step, mpeg_length;
  int64_t        pts, last_pts=0;
  int            reads=0    /* Number of blocks read so far */;
  int            count=0;   /* Number of sucessful PTS found so far */
  int            rate=0;    /* The return rate value */
  int            stream_id;

  /* We can't estimate by sampling if we don't thave the ability to
     randomly access the and more importantly reset after accessessing.  */
  if (!(this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE))
    return 0;

  mpeg_length= this->input->get_length (this->input);
  step = TRUNC((mpeg_length/MAX_SAMPLES), 2048);
  if (step <= 0) step = 2048; /* avoid endless loop for tiny files */
  pos = step;

  /* At this point "pos", and "step" are a multiple of blocksize and
     they should continue to be so throughout.
   */

  this->input->seek (this->input, pos, SEEK_SET);

  while ( (buf = this->input->read_block (this->input, this->video_fifo, 2048))
	  && count < MAX_SAMPLES && reads++ < MAX_READS ) {

    p = buf->content; /* len = this->mnBlocksize; */

    if (p[3] == 0xBA) { /* program stream pack header */

      is_mpeg1 = (p[4] & 0x40) == 0;

      if (is_mpeg1)
	p   += 12;
      else
	p += 14 + (p[0xD] & 0x07);
    }

    if (p[3] == 0xbb)  /* program stream system header */
      p  += 6 + ((p[4] << 8) | p[5]);

    /* we should now have a PES packet here */

    if (p[0] || p[1] || (p[2] != 1)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_mpeg_pes: error %02x %02x %02x (should be 0x000001) \n", p[0], p[1], p[2]);
      buf->free_buffer (buf);
      return rate;
    }

    stream_id  = p[3];
    pts = 0;

    if ((stream_id < 0xbc) || ((stream_id & 0xf0) != 0xe0)) {
      pos += (off_t) 2048;
      buf->free_buffer (buf);
      continue; /* only use video packets */
    }

    if (is_mpeg1) {

      if (p[3] != 0xBF) { /* stream_id */

	p += 6; /* packet_len -= 6; */

	while ((p[0] & 0x80) == 0x80) {
	  p++; /* stuffing */
	}

	if ((p[0] & 0xc0) == 0x40) {
	  /* STD_buffer_scale, STD_buffer_size */
	  p += 2;
	}

	if ( ((p[0] & 0xf0) == 0x20) || ((p[0] & 0xf0) == 0x30) ) {
	  pts  = (int64_t)(p[ 0] & 0x0E) << 29 ;
	  pts |=  p[ 1]         << 22 ;
	  pts |= (p[ 2] & 0xFE) << 14 ;
	  pts |=  p[ 3]         <<  7 ;
	  pts |= (p[ 4] & 0xFE) >>  1 ;
	}
      }
    } else { /* mpeg 2 */

      if (p[7] & 0x80) { /* pts avail */

	pts  = (int64_t)(p[ 9] & 0x0E) << 29 ;
	pts |=  p[10]         << 22 ;
	pts |= (p[11] & 0xFE) << 14 ;
	pts |=  p[12]         <<  7 ;
	pts |= (p[13] & 0xFE) >>  1 ;

      } else
	pts = 0;
    }

    if (pts) {


      if ( (pos>last_pos) && (pts>last_pts) ) {
	int cur_rate;

	cur_rate = ((pos - last_pos)*90000) / ((pts - last_pts) * 50);

	rate = (count * rate + cur_rate) / (count+1);

	count ++;

	/*
	printf ("demux_mpeg_pes: stream_id %02x, pos: %"PRId64", pts: %d, cur_rate = %d, overall rate : %d\n",
		stream_id, pos, pts, cur_rate, rate);
	*/
      }

      last_pos = pos;
      last_pts = pts;
      pos += step;
    } else
      pos += 2048;

    buf->free_buffer (buf);

    if (pos > mpeg_length || this->input->seek (this->input, pos, SEEK_SET) == (off_t)-1)
      break;

  }
}

  lprintf("est_rate=%d\n",rate);
  return rate;

}
#endif /*ESTIMATE_RATE_FIXED*/

static void demux_mpeg_pes_dispose (demux_plugin_t *this_gen) {

  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;

  av_free (this->scratch);
  free (this);
}

static int demux_mpeg_pes_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;

  return this->status;
}

static void demux_mpeg_pes_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  /*
   * send start buffer
   */

  _x_demux_control_start(this->stream);

#ifdef USE_ILL_ADVISED_ESTIMATE_RATE_INITIALLY
  if (!this->rate)
    this->rate = demux_mpeg_pes_estimate_rate (this);
#else
  /* Set to Use rate given in by stream initially. */
  this->rate = 0;
#endif

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->input->seek (this->input, 0, SEEK_SET);

    this->status = DEMUX_OK ;
    while ( (num_buffers>0) && (this->status == DEMUX_OK) ) {

      demux_mpeg_pes_parse_pack(this, 1);
      num_buffers --;
    }
  }
  else if((this->input->get_capabilities(this->input) & INPUT_CAP_PREVIEW) != 0) {

    this->preview_size = this->input->get_optional_data(this->input, &this->preview_data, INPUT_OPTIONAL_DATA_PREVIEW);
    this->preview_done = 0;

    this->status = DEMUX_OK ;
    while ( (this->preview_done < this->preview_size) && (this->status == DEMUX_OK) )
      demux_mpeg_pes_parse_pack(this, 1);

    this->preview_size = 0;
  }

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->rate * 50 * 8);
}


static int demux_mpeg_pes_seek (demux_plugin_t *this_gen,
				   off_t start_pos, int start_time, int playing) {

  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;
  start_time /= 1000;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    if (start_pos) {
      start_pos /= (off_t) 2048;
      start_pos *= (off_t) 2048;

      this->input->seek (this->input, start_pos, SEEK_SET);
    } else if (start_time) {

      if (this->last_cell_time) {
        start_pos = start_time - (this->last_cell_time + this->last_begin_time)/1000;
        start_pos *= this->rate;
        start_pos *= 50;
        start_pos += this->last_cell_pos;
      } else {
        start_pos = start_time;
        start_pos *= this->rate;
        start_pos *= 50;
      }
      start_pos /= (off_t) 2048;
      start_pos *= (off_t) 2048;

      this->input->seek (this->input, start_pos, SEEK_SET);
    } else
      this->input->seek (this->input, 0, SEEK_SET);
  }

  /*
   * now start demuxing
   */
  this->last_cell_time = 0;
  this->send_newpts = 1;
  if( !playing ) {

    this->buf_flag_seek = 0;
    this->nav_last_end_pts = this->nav_last_start_pts = 0;
    this->status   = DEMUX_OK ;
    this->last_pts[0]   = 0;
    this->last_pts[1]   = 0;
  } else {
    this->buf_flag_seek = 1;
    this->nav_last_end_pts = this->nav_last_start_pts = 0;
    /* trigger detection of MPEG 1/2 respectively H.264 content */
    this->mpeg12_h264_detected = 0;
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}


static void demux_mpeg_pes_accept_input (demux_mpeg_pes_t *this,
					   input_plugin_t *input) {

  this->input = input;

  if (strcmp (this->cur_mrl, input->get_mrl(input))) {

    this->rate = 0;

    strncpy (this->cur_mrl, input->get_mrl(input), 256);

    lprintf ("mrl %s is new\n", this->cur_mrl);

  }
  else {
    lprintf ("mrl %s is known, bitrate: %d\n",
             this->cur_mrl, this->rate * 50 * 8);
  }
}

static int demux_mpeg_pes_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_pes_t *this = (demux_mpeg_pes_t *) this_gen;
  /*
   * find input plugin
   */

  if (this->rate)
    return (int)((int64_t) 1000 * this->input->get_length (this->input) /
                 (this->rate * 50));
  else
    return 0;
}

static uint32_t demux_mpeg_pes_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpeg_pes_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input_gen) {

  input_plugin_t     *input = (input_plugin_t *) input_gen;
  demux_mpeg_pes_t *this;

  this         = calloc(1, sizeof(demux_mpeg_pes_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_pes_send_headers;
  this->demux_plugin.send_chunk        = demux_mpeg_pes_send_chunk;
  this->demux_plugin.seek              = demux_mpeg_pes_seek;
  this->demux_plugin.dispose           = demux_mpeg_pes_dispose;
  this->demux_plugin.get_status        = demux_mpeg_pes_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_pes_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpeg_pes_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpeg_pes_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->scratch    = av_mallocz(4096);
  this->status     = DEMUX_FINISHED;
  /* Don't start demuxing stream until we see a program_stream_pack_header */
  /* We need to system header in order to identify is the stream is mpeg1 or mpeg2. */
  this->wait_for_program_stream_pack_header = 1;
  /* trigger detection of MPEG 1/2 respectively H.264 content */
  this->mpeg12_h264_detected = 0;

  this->preview_size = 0;

  lprintf ("open_plugin:detection_method=%d\n",
	   stream->content_detection_method);

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {

    /* use demux_mpeg_block for block devices */
    if ((input->get_capabilities(input) & INPUT_CAP_BLOCK)) {
      av_free (this->scratch);
      free (this);
      return NULL;
    }

    if (((input->get_capabilities(input) & INPUT_CAP_PREVIEW) != 0) ) {

      int preview_size = input->get_optional_data(input, &this->preview_data, INPUT_OPTIONAL_DATA_PREVIEW);

      if (preview_size >= 6) {
	lprintf("open_plugin:get_optional_data worked\n");

        if (this->preview_data[0] || this->preview_data[1]
            || (this->preview_data[2] != 0x01) ) {
	  lprintf("open_plugin:preview_data failed\n");

          av_free (this->scratch);
          free (this);
          return NULL;
        }
        switch(this->preview_data[3]) {

        case 0xe0 ... 0xef:
        case 0xc0 ... 0xdf:
        case 0xbd ... 0xbe:
          break;
        default:
          av_free (this->scratch);
          free (this);
          return NULL;
        }

        demux_mpeg_pes_accept_input (this, input);
        lprintf("open_plugin:Accepting detection_method XINE_DEMUX_CONTENT_STRATEGY (preview_data)\n");

        break;
      }
    }

    if (((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) ) {

      input->seek(input, 0, SEEK_SET);
      if (input->read(input, (char *)this->scratch, 6) == 6) {
	lprintf("open_plugin:read worked\n");

        if (this->scratch[0] || this->scratch[1]
            || (this->scratch[2] != 0x01) ) {
	  lprintf("open_plugin:scratch failed\n");

          av_free (this->scratch);
          free (this);
          return NULL;
        }
        switch(this->scratch[3]) {

        case 0xe0 ... 0xef:
        case 0xc0 ... 0xdf:
        case 0xbd ... 0xbe:
          break;
        default:
          av_free (this->scratch);
          free (this);
          return NULL;
        }

        input->seek(input, 0, SEEK_SET);

        demux_mpeg_pes_accept_input (this, input);
        lprintf("open_plugin:Accepting detection_method XINE_DEMUX_CONTENT_STRATEGY \n");

        break;
      }
    }

    av_free (this->scratch);
    free (this);
    return NULL;
  }
  break;

  case METHOD_BY_MRL:
    break;

  case METHOD_EXPLICIT: {

    demux_mpeg_pes_accept_input (this, input);
  }
  break;

  default:
    av_free (this->scratch);
    free (this);
    return NULL;
  }
  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_mpeg_pes_class_t     *this;
  this         = calloc(1, sizeof(demux_mpeg_pes_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("mpeg pes demux plugin");
  this->demux_class.identifier      = "MPEG_PES";
  this->demux_class.mimetypes       = "video/mp2p: m2p: MPEG2 program stream;";
  this->demux_class.extensions      = "pes vdr:/ netvdr:/";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_mpeg_pes = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "mpeg_pes", XINE_VERSION_CODE, &demux_info_mpeg_pes, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
