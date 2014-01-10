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
 * demultiplexer for mpeg 1/2 program streams
 * used with fixed blocksize devices (like dvd/vcd)
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

#define LOG_MODULE "demux_mpeg_block"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#define NUM_PREVIEW_BUFFERS   250
#define DISC_TRESHOLD       90000

#define WRAP_THRESHOLD     120000
#define PTS_AUDIO 0
#define PTS_VIDEO 1


/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

typedef struct demux_mpeg_block_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int                   status;

  int                   blocksize;
  int                   rate;

  char                  cur_mrl[256];

  int64_t               nav_last_end_pts;
  int64_t               nav_last_start_pts;
  int64_t               last_pts[2];
  int                   send_newpts;
  int                   preview_mode;
  int                   buf_flag_seek;
  int64_t               scr;
  uint32_t              packet_len;
  int64_t               pts;
  int64_t               dts;
  uint32_t              stream_id;
  int32_t               mpeg1;

  int64_t               last_cell_time;
  off_t                 last_cell_pos;
  int                   last_begin_time;
} demux_mpeg_block_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mpeg_block_class_t;

static int32_t parse_video_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_audio_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_ancillary_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_system_header(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_private_stream_1(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_private_stream_2(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_map(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_padding_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_ecm_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_emm_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_dsmcc_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_emm_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_iec_13522_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeA_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeB_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeC_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeD_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_h222_typeE_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_IEC14496_SL_packetized_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_IEC14496_FlexMux_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_directory(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);
static int32_t parse_program_stream_pack_header(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf);


/* OK, i think demux_mpeg_block discontinuity handling demands some
   explanation:

   - The preferred discontinuity handling/detection for DVD is based on
   NAV packets information. Most of the time it will provide us very
   accurate and reliable information.

   - Has been shown that sometimes a DVD discontinuity may happen before
   a new NAV packet arrives (seeking?). To avoid sending wrong PTS to
   decoders we _used_ to check for SCR discontinuities. Unfortunately
   some VCDs have very broken SCR values causing false triggering.

   - To fix the above problem (also note that VCDs don't have NAV
   packets) we fallback to the same PTS-based wrap detection as used
   in demux_mpeg. The only trick is to not send discontinuity information
   if NAV packets have already done the job.

   [Miguel 02-05-2002]
*/

static void check_newpts( demux_mpeg_block_t *this, int64_t pts, int video )
{
  int64_t diff;

  diff = pts - this->last_pts[video];

  if( pts && (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

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

    this->last_pts[1-video] = 0;
  }

  if( pts )
    this->last_pts[video] = pts;
}

static void demux_mpeg_block_parse_pack (demux_mpeg_block_t *this, int preview_mode) {

  buf_element_t *buf = NULL;
  uint8_t       *p;
  int32_t        result;

  this->scr = 0;
  this->preview_mode = preview_mode;

  lprintf ("read_block\n");

  buf = this->input->read_block (this->input, this->video_fifo, this->blocksize);

  if (buf==NULL) {
    this->status = DEMUX_FINISHED;
    return ;
  }

  /* If this is not a block for the demuxer, pass it
   * straight through. */
  if (buf->type != BUF_DEMUX_BLOCK) {
    buf_element_t *cbuf;

    this->video_fifo->put (this->video_fifo, buf);

    /* duplicate goes to audio fifo */

    if (this->audio_fifo) {
      cbuf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

      cbuf->type = buf->type;
      cbuf->decoder_flags = buf->decoder_flags;
      memcpy( cbuf->decoder_info, buf->decoder_info, sizeof(cbuf->decoder_info) );
      memcpy( cbuf->decoder_info_ptr, buf->decoder_info_ptr, sizeof(cbuf->decoder_info_ptr) );

      this->audio_fifo->put (this->audio_fifo, cbuf);
    }

    lprintf ("type %08x != BUF_DEMUX_BLOCK\n", buf->type);

    return;
  }

  p = buf->content; /* len = this->blocksize; */
  if (preview_mode)
    buf->decoder_flags = BUF_FLAG_PREVIEW;
  else
    buf->decoder_flags = 0;

  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                     65535 / this->input->get_length (this->input) );

  while(p < (buf->content + this->blocksize)) {
    if (p[0] || p[1] || (p[2] != 1)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_mpeg_block: error! %02x %02x %02x (should be 0x000001)\n", p[0], p[1], p[2]);
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_mpeg_block: bad block. skipping.\n");
      /* FIXME: We should find some way for the input plugin to inform us of: -
       * 1) Normal sector read.
       * 2) Sector error read due to bad crc etc.
       * Because we would like to handle these two cases differently.
       */
      buf->free_buffer (buf);
      return;
    }

    this->stream_id  = p[3];

    if (this->stream_id == 0xBA) {
      result = parse_program_stream_pack_header(this, p, buf);
    } else if (this->stream_id == 0xBB) {
      result = parse_program_stream_system_header(this, p, buf);
    } else if (this->stream_id == 0xBC) {
      result = parse_program_stream_map(this, p, buf);
    } else if (this->stream_id == 0xBD) {
      result = parse_private_stream_1(this, p, buf);
    } else if (this->stream_id == 0xBE) {
      result = parse_padding_stream(this, p, buf);
    } else if (this->stream_id == 0xBF) {
      result = parse_private_stream_2(this, p, buf);
    } else if ((this->stream_id >= 0xC0)
            && (this->stream_id < 0xDF)) {
      result = parse_audio_stream(this, p, buf);
    } else if ((this->stream_id >= 0xE0)
            && (this->stream_id < 0xEF)) {
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
	      _("xine-lib:demux_mpeg_block: "
		"Unrecognised stream_id 0x%02x. Please report this to xine developers.\n"), this->stream_id);
      buf->free_buffer (buf);
      return;
    }
    if (result < 0) {
      return;
    }
    p+=result;
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	   _("demux_mpeg_block: error! freeing. Please report this to xine developers.\n"));
  buf->free_buffer (buf);
  return ;
}

static int32_t parse_padding_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* Just skip padding. */
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_program_stream_map(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x.\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_ecm_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_emm_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_dsmcc_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_iec_13522_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeA_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeB_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeC_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeD_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_h222_typeE_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_IEC14496_SL_packetized_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_IEC14496_FlexMux_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_program_stream_directory(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}
static int32_t parse_ancillary_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* FIXME: Implement */
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "xine-lib:demux_mpeg_block: Unhandled stream_id 0x%02x\n", this->stream_id);
  buf->free_buffer (buf);
  return -1;
}

static int32_t parse_program_stream_pack_header(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* program stream pack header */

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

    num_stuffing_bytes = p[0xD] & 0x07;

    return 14 + num_stuffing_bytes;
  }

}

static int32_t parse_program_stream_system_header(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  /* program stream system header */

  int32_t header_len;

  header_len = (p[4] << 8) | p[5];
  return 6 + header_len;
}

static int32_t parse_private_stream_2(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  int64_t start_pts, end_pts;

  /* NAV Packet */
  this->packet_len = p[4] << 8 | p[5];

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

  return -1;
}

/* FIXME: Extension data is not parsed, and is also not skipped. */

static int32_t parse_pes_for_pts(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  int32_t header_len;

  this->packet_len = p[4] << 8 | p[5];
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
      this->pts  = (int64_t)(p[ 0] & 0x0E) << 29 ;
      this->pts |=  p[ 1]         << 22 ;
      this->pts |= (p[ 2] & 0xFE) << 14 ;
      this->pts |=  p[ 3]         <<  7 ;
      this->pts |= (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      header_len+= 5;
      this->packet_len -=5;
      return header_len;
    } else if ((p[0] & 0xf0) == 0x30) {
      this->pts  = (int64_t)(p[ 0] & 0x0E) << 29 ;
      this->pts |=  p[ 1]         << 22 ;
      this->pts |= (p[ 2] & 0xFE) << 14 ;
      this->pts |=  p[ 3]         <<  7 ;
      this->pts |= (p[ 4] & 0xFE) >>  1 ;

      this->dts  = (int64_t)(p[ 5] & 0x0E) << 29 ;
      this->dts |=  p[ 6]         << 22 ;
      this->dts |= (p[ 7] & 0xFE) << 14 ;
      this->dts |=  p[ 8]         <<  7 ;
      this->dts |= (p[ 9] & 0xFE) >>  1 ;

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
		_("demux_mpeg_block: warning: PES header reserved 10 bits not found\n"));
      buf->free_buffer(buf);
      return -1;
    }


    /* check PES scrambling_control */

    if ((p[6] & 0x30) != 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("demux_mpeg_block: warning: PES header indicates that this stream "
		"may be encrypted (encryption mode %d)\n"), (p[6] & 0x30) >> 4);
      _x_message (this->stream, XINE_MSG_ENCRYPTED_SOURCE,
		  "Media stream scrambled/encrypted", NULL);
      this->status = DEMUX_FINISHED;
      buf->free_buffer(buf);
      return -1;
    }

    if (p[7] & 0x80) { /* pts avail */

      this->pts  = (int64_t)(p[ 9] & 0x0E) << 29 ;
      this->pts |=  p[10]         << 22 ;
      this->pts |= (p[11] & 0xFE) << 14 ;
      this->pts |=  p[12]         <<  7 ;
      this->pts |= (p[13] & 0xFE) >>  1 ;

      lprintf ("pts = %"PRId64"\n", this->pts);

    } else
      this->pts = 0;

    if (p[7] & 0x40) { /* dts avail */

      this->dts  = (int64_t)(p[14] & 0x0E) << 29 ;
      this->dts |=  p[15]         << 22 ;
      this->dts |= (p[16] & 0xFE) << 14 ;
      this->dts |=  p[17]         <<  7 ;
      this->dts |= (p[18] & 0xFE) >>  1 ;

    } else
      this->dts = 0;


    header_len = p[8];

    this->packet_len -= header_len + 3;
    return header_len + 9;
  }
  return 0;
}

static int32_t parse_private_stream_1(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {

    int track, spu_id;
    int32_t result;

    result = parse_pes_for_pts(this, p, buf);
    if (result < 0) return -1;

    p += result;
    /* printf("demux_mpeg_block: private_stream_1: p[0] = 0x%02X\n", p[0]); */

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

      return -1;
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

      return -1;
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

      return -1;
    }

    if ((p[0]&0xF0) == 0x80) {

      track = p[0] & 0x0F; /* hack : ac3 track */

      /* Number of frame headers
       *
       * Describes the number of a52 frame headers that start in this pack (One pack per DVD sector).
       *
       * Likely values: 1 or 2.
       */
      buf->decoder_info[1] = p[1];
      /* First access unit pointer.
       *
       * Describes the byte offset within this pack to the beginning of the first A52 frame header.
       * The PTS from this pack applies to the beginning of the first A52 frame that starts in this pack.
       * Any bytes before this offset should be considered to belong to the previous A52 frame.
       * and therefore be tagged with a PTS value derived from the previous pack.
       *
       * Likely values: Anything from 1 to the size of an A52 frame.
       */
      buf->decoder_info[2] = p[2] << 8 | p[3];
      /* Summary: If the first pack contains the beginning of 2 A52 frames( decoder_info[1]=2),
       *            the PTS value applies to the first A52 frame.
       *          The second A52 frame will have no PTS value.
       *          The start of the next pack will contain the rest of the second A52 frame and thus, no PTS value.
       *          The third A52 frame will have the PTS value from the second pack.
       *
       *          If the first pack contains the beginning of 1 frame( decoder_info[1]=1 ),
       *            this first pack must then contain a certain amount of the previous A52 frame.
       *            the PTS value will not apply to this previous A52 frame,
       *            the PTS value will apply to the beginning of the frame that begins in this pack.
       *
       * LPCM note: The first_access_unit_pointer will point to
       *            the PCM sample within the pack at which the PTS values applies.
       *
       * Example to values in a real stream, each line is a pack (dvd sector): -
       * decoder_info[1], decoder_info[2],  PTS
       * 2                   5             125640
       * 1                1061             131400
       * 1                 581             134280
       * 2                 101             137160
       * 1                1157             142920
       * 1                 677             145800
       * 2                 197             148680
       * 1                1253             154440
       * 1                 773             157320
       * 2                 293             160200
       * 1                1349             165960
       * 1                 869             168840
       * 2                 389             171720
       * 1                1445             177480
       * 1                 965             180360
       * 1                 485             183240
       * 2                   5             186120
       * 1                1061             191880
       * 1                 581             194760
       * 2                 101             197640
       * 1                1157             203400
       * 1                 677             206280
       * 2                 197             209160
       * Notice the repeating pattern of both decoder_info[1] and decoder_info[2].
       * The resulting A52 frames will have these PTS values: -
       *  PTS
       * 125640
       *      0
       * 131400
       * 134280
       * 137160
       *      0
       * 142920
       * 145800
       * 148680
       *      0
       * 154440
       * 157320
       * 160200
       *      0
       * 165960
       * 168840
       * 171720
       *      0
       * 177480
       * 180360
       * 183240
       * 186120
       *      0
       * 191880
       * 194760
       * 197640
       *      0
       * 203400
       * 206280
       * 209160
       *      0 (Partial A52 frame.)
       */

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

        return -1;
      } else {
	buf->free_buffer(buf);
        return -1;
      }

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
      sample_rate = p[5] & 0x10 ? 96000 : 48000;
      switch ((p[5]>>6) & 3) {
      case 3: /* illegal, use 16-bits? */
      default:
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "illegal lpcm sample format (%d), assume 16-bit samples\n", (p[5]>>6) & 3 );
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

        return -1;
      } else {
	buf->free_buffer(buf);
        return -1;
      }

    }
    /* Some new streams have been encountered.
       1) DVD+RW disc recorded with a Philips DVD recorder: -  new unknown sub-stream id of 0xff
     */
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    "demux_mpeg_block:Unrecognised private stream 1 0x%02x. Please report this to xine developers.\n", p[0]);
    buf->free_buffer(buf);
    return -1;
}

static int32_t parse_video_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {
  int32_t result;

  result = parse_pes_for_pts(this, p, buf);
  if (result < 0) return -1;

  p += result;

  buf->content   = p;
  buf->size      = this->packet_len;
  buf->type      = BUF_VIDEO_MPEG;
  buf->pts       = this->pts;
  buf->decoder_info[0] = this->pts - this->dts;
  if( !this->preview_mode )
    check_newpts( this, this->pts, PTS_VIDEO );

  this->video_fifo->put (this->video_fifo, buf);
  lprintf ("MPEG Video PACK put on fifo\n");

  return -1;
}

static int32_t parse_audio_stream(demux_mpeg_block_t *this, uint8_t *p, buf_element_t *buf) {

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

  return -1;
}

static int demux_mpeg_block_send_chunk (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  demux_mpeg_block_parse_pack(this, 0);

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

static int demux_mpeg_block_estimate_rate (demux_mpeg_block_t *this) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            is_mpeg1=0;
  off_t          pos, last_pos=0;
  off_t          step, mpeg_length;
  off_t          blocksize = this->blocksize;
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
  step = TRUNC((mpeg_length/MAX_SAMPLES), blocksize);
  if (step <= 0) step = blocksize; /* avoid endless loop for tiny files */
  pos = step;

  /* At this point "pos", and "step" are a multiple of blocksize and
     they should continue to be so throughout.
   */

  this->input->seek (this->input, pos, SEEK_SET);

  while ( (buf = this->input->read_block (this->input, this->video_fifo, blocksize))
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
	       "demux_mpeg_block: error %02x %02x %02x (should be 0x000001) \n", p[0], p[1], p[2]);
      buf->free_buffer (buf);
      return rate;
    }

    stream_id  = p[3];
    pts = 0;

    if ((stream_id < 0xbc) || ((stream_id & 0xf0) != 0xe0)) {
      pos += (off_t) blocksize;
      buf->free_buffer (buf);
      buf = NULL;
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
	printf ("demux_mpeg_block: stream_id %02x, pos: %"PRId64", pts: %d, cur_rate = %d, overall rate : %d\n",
		stream_id, pos, pts, cur_rate, rate);
	*/
      }

      last_pos = pos;
      last_pts = pts;
      pos += step;
    } else
      pos += blocksize;

    buf->free_buffer (buf);
    buf = NULL;

    if (pos > mpeg_length || this->input->seek (this->input, pos, SEEK_SET) == (off_t)-1)
      break;

  }

  if( buf )
    buf->free_buffer (buf);
}

  lprintf("est_rate=%d\n",rate);
  return rate;

}
#endif /*ESTIMATE_RATE_FIXED*/

static void demux_mpeg_block_dispose (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  free (this);
}

static int demux_mpeg_block_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  return this->status;
}

static int demux_mpeg_detect_blocksize(demux_mpeg_block_t *this,
				       input_plugin_t *input)
{
  uint8_t scratch[4];
  input->seek(input, 2048, SEEK_SET);
  if (input->read(input, scratch, 4) != 4)
    return 0;

  if (scratch[0] || scratch[1]
      || (scratch[2] != 0x01) || (scratch[3] != 0xba)) {

    input->seek(input, 2324, SEEK_SET);
    if (input->read(input, scratch, 4) != 4)
      return 0;
    if (scratch[0] || scratch[1]
        || (scratch[2] != 0x01) || (scratch[3] != 0xba))
      return 0;

    return 2324;
  } else
    return 2048;
}

static void demux_mpeg_block_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    if (!this->blocksize)
      this->blocksize = demux_mpeg_detect_blocksize( this, this->input );

    if (!this->blocksize)
      return;
  }

  /*
   * send start buffer
   */

  _x_demux_control_start(this->stream);

#ifdef USE_ILL_ADVISED_ESTIMATE_RATE_INITIALLY
  if (!this->rate)
    this->rate = demux_mpeg_block_estimate_rate (this);
#else
  /* Set to Use rate given in by stream initially. */
  this->rate = 0;
#endif

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->input->seek (this->input, 0, SEEK_SET);

    this->status = DEMUX_OK ;
    while ( (num_buffers>0) && (this->status == DEMUX_OK) ) {

      demux_mpeg_block_parse_pack(this, 1);
      num_buffers --;
    }
  }
  /* else FIXME: implement preview generation from PREVIEW data */

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->rate * 50 * 8);
}


static int demux_mpeg_block_seek (demux_plugin_t *this_gen,
				   off_t start_pos, int start_time, int playing) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    if (start_pos) {
      start_pos /= (off_t) this->blocksize;
      start_pos *= (off_t) this->blocksize;

      this->input->seek (this->input, start_pos, SEEK_SET);
    } else if (start_time) {

      if( this->input->seek_time ) {
        this->input->seek_time (this->input, start_time, SEEK_SET);
      } else {
        start_time /= 1000;

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
        start_pos /= (off_t) this->blocksize;
        start_pos *= (off_t) this->blocksize;

        this->input->seek (this->input, start_pos, SEEK_SET);
      }
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
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}


static void demux_mpeg_block_accept_input (demux_mpeg_block_t *this,
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

static int demux_mpeg_block_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  /*
   * find input plugin
   */

  if (this->rate)
    return (int)((int64_t) 1000 * this->input->get_length (this->input) /
                 (this->rate * 50));
  else
    return 0;
}

static uint32_t demux_mpeg_block_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpeg_block_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input_gen) {

  input_plugin_t     *input = (input_plugin_t *) input_gen;
  demux_mpeg_block_t *this;

  this         = calloc(1, sizeof(demux_mpeg_block_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_block_send_headers;
  this->demux_plugin.send_chunk        = demux_mpeg_block_send_chunk;
  this->demux_plugin.seek              = demux_mpeg_block_seek;
  this->demux_plugin.dispose           = demux_mpeg_block_dispose;
  this->demux_plugin.get_status        = demux_mpeg_block_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_block_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpeg_block_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpeg_block_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status     = DEMUX_FINISHED;

  lprintf ("open_plugin:detection_method=%d\n",
	   stream->content_detection_method);

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {

    /* use demux_mpeg for non-block devices */
    if (!(input->get_capabilities(input) & INPUT_CAP_BLOCK)) {
      free (this);
      return NULL;
    }

    if (((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) ) {

      uint8_t scratch[5] = {0xff, 0xff, 0xff, 0xff, 0xff}; /* result of input->read() won't matter */

      this->blocksize = input->get_blocksize(input);
      lprintf("open_plugin:blocksize=%d\n",this->blocksize);

      if (!this->blocksize)
        this->blocksize = demux_mpeg_detect_blocksize( this, input );

      if (!this->blocksize) {
        free (this);
        return NULL;
      }

      input->seek(input, 0, SEEK_SET);
      if (input->read(input, scratch, 5)) {
	lprintf("open_plugin:read worked\n");

        if (scratch[0] || scratch[1]
            || (scratch[2] != 0x01) || (scratch[3] != 0xba)) {
	  lprintf("open_plugin:scratch failed\n");

          free (this);
          return NULL;
        }

        /* if it's a file then make sure it's mpeg-2 */
        if ( !input->get_blocksize(input)
             && ((scratch[4]>>4) != 4) ) {
          free (this);
          return NULL;
        }

        input->seek(input, 0, SEEK_SET);

        demux_mpeg_block_accept_input (this, input);
        lprintf("open_plugin:Accepting detection_method XINE_DEMUX_CONTENT_STRATEGY blocksize=%d\n",
                this->blocksize);

        break;
      }
    }
    free (this);
    return NULL;
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT: {

    this->blocksize = input->get_blocksize(input);
    lprintf("open_plugin:blocksize=%d\n",this->blocksize);

    if (!this->blocksize &&
	((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0))
      this->blocksize = demux_mpeg_detect_blocksize( this, input );

    if (!this->blocksize) {
      free (this);
      return NULL;
    }

    demux_mpeg_block_accept_input (this, input);
  }
  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_mpeg_block_class_t     *this;

  this         = calloc(1, sizeof(demux_mpeg_block_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("DVD/VOB demux plugin");
  this->demux_class.identifier      = "MPEG_BLOCK";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "vob vcd:/ dvd:/ pvr:/";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_mpeg_block = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "mpeg_block", XINE_VERSION_CODE, &demux_info_mpeg_block, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
