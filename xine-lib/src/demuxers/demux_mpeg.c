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
 * demultiplexer for mpeg 1/2 program streams
 * reads streams of variable blocksizes
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define LOG_MODULE "demux_mpeg"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/demux.h>
#include <xine/xineutils.h>

#define NUM_PREVIEW_BUFFERS 150
#define SCRATCH_SIZE 256

#define WRAP_THRESHOLD       120000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

typedef struct demux_mpeg_s {
  demux_plugin_t       demux_plugin;

  xine_stream_t	      *stream;
  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned char        dummy_space[100000];
  int                  preview_mode;
  int                  rate;

  int64_t              last_pts[2];
  int                  send_newpts;
  int                  buf_flag_seek;
  int                  has_pts;
} demux_mpeg_t;

typedef struct {
  demux_class_t     demux_class;
} demux_mpeg_class_t;

    /* code never reached, is it still usefull ?? */
/*
 * borrow a little knowledge from the Quicktime demuxer
 */
#include "bswap.h"

#define QT_ATOM BE_FOURCC
/* these are the known top-level QT atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')

#define ATOM_PREAMBLE_SIZE 8

/* a little something for dealing with RIFF headers */

#define FOURCC_TAG BE_FOURCC
#define RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define WAVE_TAG FOURCC_TAG('W', 'A', 'V', 'E')
#define AVI_TAG FOURCC_TAG('A', 'V', 'I', ' ')
#define FOURXM_TAG FOURCC_TAG('4', 'X', 'M', 'V')

/* arbitrary number of initial file bytes to check for an MPEG marker */
#define RIFF_CHECK_KILOBYTES 1024

#define MPEG_MARKER FOURCC_TAG( 0x00, 0x00, 0x01, 0xBA )

/*
 * This function traverses a file and looks for a mdat atom. Upon exit:
 * *mdat_offset contains the file offset of the beginning of the mdat
 *  atom (that means the offset  * of the 4-byte length preceding the
 *  characters 'mdat')
 * *mdat_size contains the 4-byte size preceding the mdat characters in
 *  the atom. Note that this will be 1 in the case of a 64-bit atom.
 * Both mdat_offset and mdat_size are set to -1 if not mdat atom was
 * found.
 *
 * Note: Do not count on the input stream being positioned anywhere in
 * particular when this function is finished.
 */
static void find_mdat_atom(input_plugin_t *input, off_t *mdat_offset,
  int64_t *mdat_size) {

  off_t atom_size;
  unsigned int atom;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];

  /* init the passed variables */
  *mdat_offset = *mdat_size = -1;

  /* take it from the top */
  if (input->seek(input, 0, SEEK_SET) != 0)
    return;

  /* traverse through the input */
  while (*mdat_offset == -1) {
    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      break;

    atom_size = _X_BE_32(&atom_preamble[0]);
    atom = _X_BE_32(&atom_preamble[4]);

    if (atom == MDAT_ATOM) {
      *mdat_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE;
      *mdat_size = atom_size;
      break;
    }

    /* make sure the atom checks out as some other top-level atom before
     * proceeding */
    if ((atom != FREE_ATOM) &&
        (atom != JUNK_ATOM) &&
        (atom != MOOV_ATOM) &&
        (atom != PNOT_ATOM) &&
        (atom != SKIP_ATOM) &&
        (atom != WIDE_ATOM))
      break;

    /* 64-bit length special case */
    if (atom_size == 1) {
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      atom_size = _X_BE_32(&atom_preamble[0]);
      atom_size <<= 32;
      atom_size |= _X_BE_32(&atom_preamble[4]);
      atom_size -= ATOM_PREAMBLE_SIZE * 2;
    } else
      atom_size -= ATOM_PREAMBLE_SIZE;


    input->seek(input, atom_size, SEEK_CUR);
  }
}

static uint32_t read_bytes (demux_mpeg_t *this, uint32_t n) {

  uint32_t res;
  uint32_t i;
  unsigned char buf[6];

  buf[4]=0;

  i = this->input->read (this->input, buf, n);

  if (i != n) {

    this->status = DEMUX_FINISHED;
  }

  switch (n)  {
  case 1:
    res = buf[0];
    break;
  case 2:
    res = (buf[0]<<8) | buf[1];
    break;
  case 3:
    res = (buf[0]<<16) | (buf[1]<<8) | buf[2];
    break;
  case 4:
    res = (buf[2]<<8) | buf[3] | (buf[1]<<16) | (buf[0] << 24);
    break;
  default:
    lprintf ("how how - something wrong in wonderland demux:read_bytes (%d)\n", n);
    _x_abort();
  }

  return res;
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts( demux_mpeg_t *this, int64_t pts, int video ) {
  int64_t diff;

  diff = pts - this->last_pts[video];

  if( !this->preview_mode && pts &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if( !this->preview_mode && pts )
    this->last_pts[video] = pts;
}

static void parse_mpeg2_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int            len, i;
  uint32_t       w, flags, header_len;
  int64_t        pts, dts;
  buf_element_t *buf = NULL;

  len = read_bytes(this, 2);

  //printf( "parse_mpeg2_packet: stream_id=%X\n", stream_id);

  if (stream_id==0xbd) {

    int track;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts=0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    this->input->read (this->input, this->dummy_space, header_len);

    i = this->input->read (this->input, this->dummy_space, 1);
    track = this->dummy_space[0] & 0x0F ;

    /* DVD spu/subtitles */
    if((this->dummy_space[0] & 0xE0) == 0x20) {

      buf = this->input->read_block (this->input, this->video_fifo, len-1);
      if (! buf) {
	this->status = DEMUX_FINISHED;
	return;
      }

      track = (this->dummy_space[0] & 0x1f);

      buf->type      = BUF_SPU_DVD + track;
      buf->decoder_flags |= BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
      buf->decoder_info[2] = SPU_DVD_SUBTYPE_PACKAGE;
      buf->pts       = pts;

      this->video_fifo->put (this->video_fifo, buf);

      return;
    }

    /* SVCD OGT subtitles are in stream 0x70 */
    if((this->dummy_space[0] & 0xFF) == 0x70) {
      int spu_id = this->dummy_space[1] & 0x03;

      buf = this->input->read_block (this->input, this->video_fifo, len-1);
      if (! buf) {
	this->status = DEMUX_FINISHED;
	return;
      }

      buf->type      = BUF_SPU_SVCD + spu_id;
      buf->pts       = pts;

      /* There is a bug in WinSubMux doesn't redo PACK headers in
	 the private stream 1. This might cause the below to mess up.
      if( !preview_mode )
        check_newpts( this, this->pts, PTS_VIDEO );
      */
      this->video_fifo->put (this->video_fifo, buf);
      lprintf ("SPU SVCD PACK (pts: %"PRId64", spu id: %d) put on FIFO\n",
	       buf->pts, spu_id);

      return;
    }

    /* CVD subtitles are in stream 0x00-0x03 */
    if((this->dummy_space[0] & 0xfc) == 0x00) {

      buf = this->input->read_block (this->input, this->video_fifo, len-1);
      if (! buf) {
	this->status = DEMUX_FINISHED;
	return;
      }

      buf->type      = BUF_SPU_CVD + (this->dummy_space[0] & 0x03);
      buf->pts       = pts;

      this->video_fifo->put (this->video_fifo, buf);

      return;
    }

    if((this->dummy_space[0] & 0xf0) == 0x80) {

      /* read rest of header - AC3 */
      this->input->read (this->input, this->dummy_space+1, 3);

      /* contents */
      for (i = len - 4; i > 0; i -= (this->audio_fifo)
           ? this->audio_fifo->buffer_pool_buf_size : this->video_fifo->buffer_pool_buf_size) {
        if(this->audio_fifo) {
          buf = this->input->read_block (this->input, this->audio_fifo,
            (i > this->audio_fifo->buffer_pool_buf_size) ? this->audio_fifo->buffer_pool_buf_size : i);

          if (buf == NULL) {
            this->status = DEMUX_FINISHED;
            return;
          }
          if (track & 0x8) {
            buf->type      = BUF_AUDIO_DTS + (track & 0x07); /* DVDs only have 8 tracks */
          } else {
            buf->type      = BUF_AUDIO_A52 + track;
          }
          buf->pts       = pts;
          check_newpts( this, pts, PTS_AUDIO );
          pts = 0;

          if (this->preview_mode)
            buf->decoder_flags = BUF_FLAG_PREVIEW;

          if( this->input->get_length (this->input) )
            buf->extra_info->input_normpos =
	      (int)( ((int64_t)this->input->get_current_pos (this->input) *
		      65535) / this->input->get_length (this->input) );

          if (this->rate)
            buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                                  * 1000 / (this->rate * 50));

          this->audio_fifo->put (this->audio_fifo, buf);

        } else
          this->input->read (this->input, this->dummy_space, i);

      }
      return;

    } else if((this->dummy_space[0] & 0xf0) == 0xa0) {
      i = this->input->read (this->input, this->dummy_space+1, 6);

      buf = this->input->read_block (this->input, this->video_fifo, len-7);
      if (! buf) {
	this->status = DEMUX_FINISHED;
	return;
      }

      buf->type      = BUF_AUDIO_LPCM_BE + track;
      buf->decoder_flags |= BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      buf->decoder_info[2] = this->dummy_space[5];
      buf->pts       = pts;

      check_newpts( this, pts, PTS_AUDIO );

      if (this->preview_mode)
        buf->decoder_flags |= BUF_FLAG_PREVIEW;

      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos =
	  (int)( ((int64_t)this->input->get_current_pos (this->input) *
		  65535) / this->input->get_length (this->input) );
      if (this->rate)
        buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                              * 1000 / (this->rate * 50));

      if(this->audio_fifo)
        this->audio_fifo->put (this->audio_fifo, buf);
      else
        buf->free_buffer(buf);

      return;

    } else {
      for (i = len-1; i > 0; i -= 10000)
        this->input->read (this->input, this->dummy_space, (i > 10000) ? 10000 : i);
    }

  } else if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    this->input->read (this->input, this->dummy_space, header_len);

    for (i = len; i > 0; i -= (this->audio_fifo)
	   ? this->audio_fifo->buffer_pool_buf_size : this->video_fifo->buffer_pool_buf_size) {
      if(this->audio_fifo) {
	buf = this->input->read_block (this->input, this->audio_fifo,
	  (i > this->audio_fifo->buffer_pool_buf_size) ? this->audio_fifo->buffer_pool_buf_size : i);

	if (buf == NULL) {
	  this->status = DEMUX_FINISHED;
	  return;
	}

	buf->type      = BUF_AUDIO_MPEG + track;
	buf->pts       = pts;
	check_newpts( this, pts, PTS_AUDIO );
	pts = 0;

	if (this->preview_mode)
	  buf->decoder_flags = BUF_FLAG_PREVIEW;

        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos =
	    (int)( ((int64_t)this->input->get_current_pos (this->input) *
		    65535) / this->input->get_length (this->input) );
        if (this->rate)
          buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                                * 1000 / (this->rate * 50));

	this->audio_fifo->put (this->audio_fifo, buf);

      } else
	this->input->read (this->input, this->dummy_space, i);

    }

  } else if ( ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) || stream_id==0xfd ) {

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts = 0;
    dts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    if ((flags & 0x40) == 0x40) {

      w = read_bytes(this, 1);
      dts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      dts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      dts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    /* read rest of header */
    this->input->read (this->input, this->dummy_space, header_len);

    /* contents */

    for (i = len; i > 0; i -= this->video_fifo->buffer_pool_buf_size) {
      buf = this->input->read_block (this->input, this->video_fifo,
	(i > this->video_fifo->buffer_pool_buf_size) ? this->video_fifo->buffer_pool_buf_size : i);

      if (buf == NULL) {
	this->status = DEMUX_FINISHED;
	return;
      }

      buf->type = (stream_id==0xfd) ? BUF_VIDEO_VC1 : BUF_VIDEO_MPEG;
      buf->pts  = pts;
      buf->decoder_info[0] = pts - dts;
      check_newpts( this, pts, PTS_VIDEO );
      pts = 0;

      if (this->preview_mode)
	buf->decoder_flags = BUF_FLAG_PREVIEW;

      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos =
	  (int)( ((int64_t)this->input->get_current_pos (this->input) *
		  65535) / this->input->get_length (this->input) );
      if (this->rate)
        buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                              * 1000 / (this->rate * 50));

      this->video_fifo->put (this->video_fifo, buf);
    }

  } else {

    for (i = len; i > 0; i -= 10000)
      this->input->read (this->input, this->dummy_space, (i > 10000) ? 10000 : i);
  }

}

static void parse_mpeg1_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int             len;
  uint32_t        w;
  int             i;
  int64_t         pts, dts;
  buf_element_t  *buf = NULL;

  len = read_bytes(this, 2);

  pts=0;
  dts=0;

  if (stream_id != 0xbf) {

    w = read_bytes(this, 1); len--;

    while ((w & 0x80) == 0x80)   {

      if (this->status != DEMUX_OK)
        return;

      /* stuffing bytes */
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xC0) == 0x40) {

      if (this->status != DEMUX_OK)
        return;

      /* buffer_scale, buffer size */
      w = read_bytes(this, 1); len--;
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xF0) == 0x20) {

      if (this->status != DEMUX_OK)
        return;

      pts = (int64_t)(w & 0xe) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;
      pts |= (w & 0xFFFE) >> 1;

      /* pts = 0; */

    } else if ((w & 0xF0) == 0x30) {

      if (this->status != DEMUX_OK)
        return;

      pts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) >> 1;

      w = read_bytes(this, 1); len -= 1;
      dts = (int64_t)(w & 0x0e) << 29 ;
      w = read_bytes(this, 2); len -= 2;
      dts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); len -= 2;
      dts |= (w & 0xFFFE) >> 1;

    } else {

      /*
      if (w != 0x0f)
        lprintf("ERROR w (%02x) != 0x0F ",w);
      */
    }

  }

  if (pts && !this->has_pts) {
    lprintf("this stream has pts\n");
    this->has_pts = 1;
  } else if (scr && !this->has_pts) {
    lprintf("use scr\n");
    pts = scr;
  }

  if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    for (i = len; i > 0; i -= (this->audio_fifo)
	   ? this->audio_fifo->buffer_pool_buf_size : this->video_fifo->buffer_pool_buf_size) {
      if(this->audio_fifo) {
	buf = this->input->read_block (this->input, this->audio_fifo,
	  (i > this->audio_fifo->buffer_pool_buf_size) ? this->audio_fifo->buffer_pool_buf_size : i);

	if (buf == NULL) {
	  this->status = DEMUX_FINISHED;
	  return;
	}

	buf->type      = BUF_AUDIO_MPEG + track ;
	buf->pts       = pts;
	check_newpts( this, pts, PTS_AUDIO );
	pts = 0;

	if (this->preview_mode)
	  buf->decoder_flags = BUF_FLAG_PREVIEW;

        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos =
	    (int)( ((int64_t)this->input->get_current_pos (this->input) *
		    65535) / this->input->get_length (this->input) );
        if (this->rate)
          buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                                * 1000 / (this->rate * 50));

	this->audio_fifo->put (this->audio_fifo, buf);

      } else
	this->input->read (this->input, this->dummy_space, i);

    }

  } else if ((stream_id & 0xf0) == 0xe0) {

    for (i = len; i > 0; i -= this->video_fifo->buffer_pool_buf_size) {
      buf = this->input->read_block (this->input, this->video_fifo,
	(i > this->video_fifo->buffer_pool_buf_size) ? this->video_fifo->buffer_pool_buf_size : i);

      if (buf == NULL) {
	this->status = DEMUX_FINISHED;
	return;
      }

      buf->type = BUF_VIDEO_MPEG;
      buf->pts  = pts;
      buf->decoder_info[0] = pts - dts;
      check_newpts( this, pts, PTS_VIDEO );
      pts = 0;

      if (this->preview_mode)
	buf->decoder_flags = BUF_FLAG_PREVIEW;

      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos =
	  (int)( ((int64_t)this->input->get_current_pos (this->input) *
		  65535) / this->input->get_length (this->input) );
      if (this->rate)
        buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                              * 1000 / (this->rate * 50));

      this->video_fifo->put (this->video_fifo, buf);
    }

  } else if (stream_id == 0xbd) {

    for (i = len; i > 0; i -= 10000)
      this->input->read (this->input, this->dummy_space, (i > 10000) ? 10000 : i);

  } else {

    for (i = len; i > 0; i -= 10000)
      this->input->read (this->input, this->dummy_space, (i > 10000) ? 10000 : i);
  }
}

static uint32_t parse_pack(demux_mpeg_t *this) {

  uint32_t  buf ;
  int       mpeg_version;
  int64_t   scr;


  buf = read_bytes (this, 1);

  if ((buf>>6) == 0x01) {

    int stuffing, i;

    mpeg_version = 2;

    /* system_clock_reference */

    scr  = (int64_t)(buf & 0x38) << 27;
    scr |= (buf & 0x03) << 28;
    buf  = read_bytes (this, 1);
    scr |= buf << 20;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) << 12 ;
    scr |= (buf & 0x03) << 13 ;
    buf  = read_bytes (this, 1);
    scr |= buf << 5;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) >> 3;
    buf  = read_bytes (this, 1); /* extension */

    /* mux_rate */

    if (!this->rate) {
      buf  = read_bytes (this, 1);
      this->rate = (buf << 14);
      buf  = read_bytes (this, 1);
      this->rate |= (buf << 6);
      buf  = read_bytes (this, 1);
      this->rate |= (buf >> 2);
    } else {
      read_bytes(this,3);
    }

    /* stuffing bytes */
    buf = read_bytes(this,1);
    stuffing = buf &0x03;
    for (i=0; i<stuffing; i++)
      read_bytes (this, 1);

  } else {

     mpeg_version = 1;

     /* system_clock_reference */

     scr = (int64_t)(buf & 0x2) << 30;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) << 14;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) >>1;

     /* mux_rate */

     if (!this->rate) {
       buf = read_bytes (this,1);
       this->rate = (buf & 0x7F) << 15;
       buf = read_bytes (this,1);
       this->rate |= (buf << 7);
       buf = read_bytes (this,1);
       this->rate |= (buf >> 1);

       lprintf ("mux_rate = %d\n",this->rate);

     } else
       buf = read_bytes (this, 3) ;
  }

  /* system header */

  buf = read_bytes (this, 4) ;

  /* lprintf("code = %08x\n",buf);*/

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);

    this->input->read (this->input, this->dummy_space, buf);

    buf = read_bytes (this, 4) ;
  }

  /* lprintf("code = %08x\n",buf); */

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
          && ((buf & 0xff) != 0xba) ) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, scr);
    else
      parse_mpeg2_packet (this, buf & 0xFF, scr);

    buf = read_bytes (this, 4);

  }

  return buf;

}

static uint32_t parse_pack_preview (demux_mpeg_t *this, int *num_buffers) {
  uint32_t buf ;
  int mpeg_version;

  /* system_clock_reference */
  buf = read_bytes (this, 1);

  if ((buf>>6) == 0x01) {
     buf = read_bytes(this, 1);
     mpeg_version = 2;
  } else {
     mpeg_version = 1;
  }

  buf = read_bytes (this, 4);

  /* mux_rate */

  if (!this->rate) {
    if (mpeg_version == 2) {
      buf  = read_bytes (this, 1);
      this->rate = (buf << 14);
      buf  = read_bytes (this, 1);
      this->rate |= (buf << 6);
      this->rate |= (buf >> 2);
      read_bytes (this, 1);
    } else {
      buf = read_bytes (this,1);
      this->rate = (buf & 0x7F) << 15;
      buf = read_bytes (this,1);
      this->rate |= (buf << 7);
      buf = read_bytes (this,1);
      this->rate |= (buf >> 1);
    }
    /* lprintf("mux_rate = %d\n",this->rate); */

  } else
    buf = read_bytes (this, 3) ;

  if( mpeg_version == 2 )
  {
    int i, stuffing;

    buf = read_bytes(this,1);

    /* stuffing bytes */
    stuffing = buf &0x03;
    for (i=0; i<stuffing; i++)
      read_bytes (this, 1);
  }

  /* system header */

  buf = read_bytes (this, 4) ;

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);
    this->input->read (this->input, this->dummy_space, buf);
    buf = read_bytes (this, 4) ;
  }

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
          && ((buf & 0xff) != 0xba)
          && (*num_buffers > 0)) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, 0);
    else
      parse_mpeg2_packet (this, buf & 0xFF, 0);

    buf = read_bytes (this, 4);
    *num_buffers = *num_buffers - 1;
  }

  return buf;

}

static void demux_mpeg_resync (demux_mpeg_t *this, uint32_t buf) {

  if (INPUT_IS_SEEKABLE(this->input)) {
    uint8_t dummy_buf[4096];
    off_t len, pos;

    /* fast resync, read 4K block at once */
    pos = 0;
    len = 0;
    while ((buf != 0x000001ba) && (this->status == DEMUX_OK)) {
      if (pos == len) {
	len = this->input->read(this->input, dummy_buf, sizeof(dummy_buf));
        pos = 0;
        if (len <= 0) {
          this->status = DEMUX_FINISHED;
	  break;
	}
      }
      buf = (buf << 8) | dummy_buf[pos];
      pos++;
    }
    /* seek back to the pos of the 0x00001ba tag */
    this->input->seek(this->input, pos-len, SEEK_CUR);

  } else {
    /* slow resync */
    while ((buf !=0x000001ba) && (this->status == DEMUX_OK)) {
      buf = (buf << 8) | read_bytes (this, 1);
    }
  }
}

static int demux_mpeg_send_chunk (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  uint32_t w=0;

  w = parse_pack (this);
  if (w != 0x000001ba)
    demux_mpeg_resync (this, w);

  return this->status;
}

static int demux_mpeg_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  return this->status;
}

static void demux_mpeg_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  uint32_t w;
  int num_buffers = NUM_PREVIEW_BUFFERS;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->rate          = 0; /* fixme */
  this->last_pts[0]   = 0;
  this->last_pts[1]   = 0;

  _x_demux_control_start(this->stream);

  /*
   * send preview buffers for stream/meta_info
   */

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  this->preview_mode = 1;

  this->input->seek (this->input, 4, SEEK_SET);

  this->status = DEMUX_OK ;

  do {

    w = parse_pack_preview (this, &num_buffers);

    if (w != 0x000001ba)
      demux_mpeg_resync (this, w);

    num_buffers --;

  } while ( (this->status == DEMUX_OK) && (num_buffers > 0));

  this->status = DEMUX_OK ;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->rate * 50 * 8);
}

static int demux_mpeg_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time, int playing) {

  demux_mpeg_t   *this = (demux_mpeg_t *) this_gen;
  start_time /= 1000;
  start_pos = (off_t) ( ((int64_t)start_pos * this->input->get_length (this->input)) / 65535);

  if (INPUT_IS_SEEKABLE(this->input)) {

    if ( (!start_pos) && (start_time)) {
      start_pos = start_time;
      start_pos *= this->rate;
      start_pos *= 50;
    }

    this->input->seek (this->input, start_pos+4, SEEK_SET);

    if( start_pos )
      demux_mpeg_resync (this, read_bytes(this, 4) );

  } else
    read_bytes(this, 4);

  this->send_newpts = 1;
  this->status = DEMUX_OK ;

  if( !playing ) {
    this->preview_mode = 0;
    this->buf_flag_seek = 0;
  } else {
    this->buf_flag_seek = 1;
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}

static int demux_mpeg_get_stream_length (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  if (this->rate)
    return (int)((int64_t) 1000 * this->input->get_length (this->input) /
                 (this->rate * 50));
  else
    return 0;

}

static uint32_t demux_mpeg_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpeg_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
				    input_plugin_t *input) {
  demux_mpeg_t       *this;

  this         = calloc(1, sizeof(demux_mpeg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_send_headers;
  this->demux_plugin.send_chunk       = demux_mpeg_send_chunk;
  this->demux_plugin.seek              = demux_mpeg_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_mpeg_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpeg_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpeg_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  this->has_pts = 0;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    off_t mdat_atom_offset = -1;
    int64_t mdat_atom_size = -1;
    unsigned int fourcc_tag;
    int i, j, read;
    int ok = 0;
    uint8_t buf[SCRATCH_SIZE];

    /* use demux_mpeg_block for block devices */
    if (input->get_capabilities(input) & INPUT_CAP_BLOCK ) {
      free (this);
      return NULL;
    }

    /* look for mpeg header */
    read = _x_demux_read_header(input, buf, SCRATCH_SIZE);
    if (!read) {
      free (this);
      return NULL;
    }

    for (i = 0; i < read - 4; i++) {
      lprintf ("%02x %02x %02x %02x\n", buf[i], buf[i+1], buf[i+2], buf[i+3]);
      if (!buf[i] && !buf[i+1] && (buf[i+2] == 0x01)
          && (buf[i+3] == 0xba)) /* if so, take it */ {
        ok = 1;
        break;
      }
    }

    if (ok == 1)
      break;

    /* the special cases need seeking */
    if (!INPUT_IS_SEEKABLE(input)) {
      free (this);
      return NULL;
    }

    /* special case for MPEG streams hidden inside QT files; check
     * is there is an mdat atom  */
    find_mdat_atom(input, &mdat_atom_offset, &mdat_atom_size);
    if (mdat_atom_offset != -1) {
      /* seek to the start of the mdat data, which might be in different
       * depending on the size type of the atom */
      if (mdat_atom_size == 1)
	input->seek(input, mdat_atom_offset + 16, SEEK_SET);
      else
	input->seek(input, mdat_atom_offset + 8, SEEK_SET);

      /* go through the same MPEG detection song and dance */
      if (input->read(input, buf, 4) == 4) {

        if (!buf[0] && !buf[1] && (buf[2] == 0x01)
	    && (buf[3] == 0xba)) /* if so, take it */
	  break;
      }

      free (this);
      return NULL;
    }

    /* reset position for next check */
    if (input->seek(input, 0, SEEK_SET) != 0) {
      free (this);
      return NULL;
    }

    /* special case for MPEG streams with a RIFF header */
    fourcc_tag = _X_BE_32(&buf[0]);
    if (fourcc_tag == RIFF_TAG) {
      uint8_t large_buf[1024];

      if (input->read(input, large_buf, 12) != 12) {
        free(this);
        return NULL;
      }
      fourcc_tag = _X_BE_32(&large_buf[8]);
      /* disregard the RIFF file if it is certainly a better known
       * format like AVI or WAVE */
      if ((fourcc_tag == WAVE_TAG) ||
	  (fourcc_tag == AVI_TAG) ||
	  (fourcc_tag == FOURXM_TAG)) {
	free (this);
	return NULL;
      }

      /* Iterate through first n kilobytes of RIFF file searching for
       * MPEG video marker. No, it's not a very efficient approach, but
       * if execution has reached this special case, this is currently
       * the best chance for detecting the file automatically. Also,
       * be extra lazy and do not bother skipping over the data
       * header. */
      for (i = 0; i < RIFF_CHECK_KILOBYTES && !ok; i++) {
	if (input->read(input, large_buf, 1024) != 1024)
	  break;
	for (j = 0; j < 1024 - 4; j++) {
	  if (_X_BE_32(&large_buf[j]) == MPEG_MARKER) {
	    ok = 1;
	    break;
	  }
	}
      }
      if (ok)
	break;
    }
    free (this);
    return NULL;
  }

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
    break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_mpeg_class_t     *this;

  this = calloc(1, sizeof(demux_mpeg_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("MPEG program stream demux plugin");
  this->demux_class.identifier      = "MPEG";
  this->demux_class.mimetypes       =
    "video/mpeg: mpeg, mpg, mpe: MPEG animation;"
    "video/x-mpeg: mpeg, mpg, mpe: MPEG animation;";
  this->demux_class.extensions      = "mpg mpeg";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_mpeg = {
  9                        /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "mpeg", XINE_VERSION_CODE, &demux_info_mpeg, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
