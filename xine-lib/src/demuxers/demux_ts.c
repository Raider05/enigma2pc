
/*
 * Copyright (C) 2000-2013 the xine project
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
 * Demultiplexer for MPEG2 Transport Streams.
 *
 * For the purposes of playing video, we make some assumptions about the
 * kinds of TS we have to process. The most important simplification is to
 * assume that the TS contains a single program (SPTS) because this then
 * allows significant simplifications to be made in processing PATs.
 *
 * The next simplification is to assume that the program has a reasonable
 * number of video, audio and other streams. This allows PMT processing to
 * be simplified.
 *
 * MODIFICATION HISTORY
 *
 * Date        Author
 * ----        ------
 *
 *  8-Apr-2009 Petri Hintukainen <phi@sdf-eu.org>
 *                  - support for 192-byte packets (HDMV/BluRay)
 *                  - support for audio inside PES PID 0xfd (HDMV/BluRay)
 *                  - demux HDMV/BluRay bitmap subtitles
 *
 * 28-Nov-2004 Mike Lampard <mlampard>
 *                  - Added support for PMT sections larger than 1 ts packet
 *
 * 28-Aug-2004 James Courtier-Dutton <jcdutton>
 *                  - Improve PAT and PMT handling. Added some FIXME comments.
 *
 *  9-Aug-2003 James Courtier-Dutton <jcdutton>
 *                  - Improve readability of code. Added some FIXME comments.
 *
 * 25-Nov-2002 Peter Liljenberg
 *                  - Added DVBSUB support
 *
 * 07-Nov-2992 Howdy Pierce
 *                  - various bugfixes
 *
 * 30-May-2002 Mauro Borghi
 *                  - dynamic allocation leaks fixes
 *
 * 27-May-2002 Giovanni Baronetti and Mauro Borghi <mauro.borghi@tilab.com>
 *                  - fill buffers before putting them in fifos
 *                  - force PMT reparsing when PMT PID changes
 *                  - accept non seekable input plugins -- FIX?
 *                  - accept dvb as input plugin
 *                  - optimised read operations
 *                  - modified resync code
 *
 * 16-May-2002 Thibaut Mattern <tmattern@noos.fr>
 *                  - fix demux loop
 *
 * 07-Jan-2002 Andr Draszik <andid@gmx.net>
 *                  - added support for single-section PMTs
 *                    spanning multiple TS packets
 *
 * 10-Sep-2001 James Courtier-Dutton <jcdutton>
 *                  - re-wrote sync code so that it now does not loose any data
 *
 * 27-Aug-2001 Hubert Matthews  Reviewed by: n/a
 *                  - added in synchronisation code
 *
 *  1-Aug-2001 James Courtier-Dutton <jcdutton>  Reviewed by: n/a
 *                  - TS Streams with zero PES length should now work
 *
 * 30-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                  - PATs and PMTs seem to work
 *
 * 29-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                  - Compiles!
 *
 *
 * TODO: do without memcpys, preview buffers
 */


/** HOW TO IMPLEMENT A DVBSUB DECODER.
 *
 * The DVBSUB protocol is specified in ETSI EN 300 743.  It can be
 * downloaded for free (registration required, though) from
 * www.etsi.org.
 *
 * The spu_decoder should handle the packet type BUF_SPU_DVB.
 *
 * BUF_SPU_DVBSUB packets without the flag BUF_FLAG_SPECIAL contain
 * the payload of the PES packets carrying DVBSUB data.  Since the
 * payload can be broken up over several buf_element_t and the DVBSUB
 * is PES oriented, the decoder_info[2] field (low 16 bits) is used to convey the
 * packet boundaries to the decoder:
 *
 * + For the first buffer of a packet, buf->content points to the
 *   first byte of the PES payload.  decoder_info[2] is set to the length of the
 *   payload.  The decoder can use this value to determine when a
 *   complete PES packet has been collected.
 *
 * + For the following buffers of the PES packet, decoder_info[2] is 0.
 *
 * The decoder can either use this information to reconstruct the PES
 * payload, or ignore it and implement a parser that handles the
 * irregularites at the start and end of PES packets.
 *
 * In any case buf->pts is always set to the PTS of the PES packet.
 *
 *
 * BUF_SPU_DVB with BUF_FLAG_SPECIAL set contains no payload, and is
 * used to pass control information to the decoder.
 *
 * If decoder_info[1] == BUF_SPECIAL_SPU_DVB_DESCRIPTOR then
 * decoder_info_ptr[2] either points to a spu_dvb_descriptor_t or is NULL.
 *
 * If it is 0, the user has disabled the subtitling, or has selected a
 * channel that is not present in the stream.  The decoder should
 * remove any visible subtitling.
 *
 * If it is a pointer, the decoder should reset itself and start
 * extracting the subtitle service identified by comp_page_id and
 * aux_page_id in the spu_dvb_descriptor_t, (the composition and
 * auxilliary page ids, respectively).
 **/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <crc.h>
#else
#  include <libavutil/crc.h>
#endif

#define LOG_MODULE "demux_ts"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

/*
  #define TS_LOG
  #define TS_PMT_LOG
  #define TS_PAT_LOG

  #define TS_READ_STATS // activates read statistics generation
  #define TS_HEADER_LOG // prints out the Transport packet header.
*/

/*
 *  The maximum number of PIDs we are prepared to handle in a single program
 *  is the number that fits in a single-packet PMT.
 */
#define PKT_SIZE 188
#define BODY_SIZE (188 - 4)
/* more PIDS are needed due "auto-detection". 40 spare media entries  */
#define MAX_PIDS ((BODY_SIZE - 1 - 13) / 4) + 40

#define MAX_PMTS 128
#define PAT_BUF_SIZE (4 * MAX_PMTS + 20)

#define SYNC_BYTE   0x47

#define MIN_SYNCS 3
#define NPKT_PER_READ 96  // 96*188 = 94*192

#define BUF_SIZE (NPKT_PER_READ * (PKT_SIZE + 4))

#define CORRUPT_PES_THRESHOLD 10

#define NULL_PID 0x1fff
#define INVALID_PID ((unsigned int)(-1))
#define INVALID_PROGRAM ((unsigned int)(-1))
#define INVALID_CC ((unsigned int)(-1))

#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM   0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF
#define ECM_STREAM       0xF0
#define EMM_STREAM       0xF1
#define DSM_CC_STREAM    0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

/* descriptors in PMT stream info */
#define DESCRIPTOR_REG_FORMAT  0x05
#define DESCRIPTOR_LANG        0x0a
#define DESCRIPTOR_TELETEXT    0x56
#define DESCRIPTOR_DVBSUB      0x59
#define DESCRIPTOR_AC3         0x6a
#define DESCRIPTOR_EAC3        0x7a
#define DESCRIPTOR_DTS         0x7b
#define DESCRIPTOR_AAC         0x7c

  typedef enum
    {
      ISO_11172_VIDEO = 0x01,           /* ISO/IEC 11172 Video */
      ISO_13818_VIDEO = 0x02,           /* ISO/IEC 13818-2 Video */
      ISO_11172_AUDIO = 0x03,           /* ISO/IEC 11172 Audio */
      ISO_13818_AUDIO = 0x04,           /* ISO/IEC 13818-3 Audi */
      ISO_13818_PRIVATE = 0x05,         /* ISO/IEC 13818-1 private sections */
      ISO_13818_PES_PRIVATE = 0x06,     /* ISO/IEC 13818-1 PES packets containing private data */
      ISO_13522_MHEG = 0x07,            /* ISO/IEC 13512 MHEG */
      ISO_13818_DSMCC = 0x08,           /* ISO/IEC 13818-1 Annex A  DSM CC */
      ISO_13818_TYPE_A = 0x0a,          /* ISO/IEC 13818-6 Multiprotocol encapsulation */
      ISO_13818_TYPE_B = 0x0b,          /* ISO/IEC 13818-6 DSM-CC U-N Messages */
      ISO_13818_TYPE_C = 0x0c,          /* ISO/IEC 13818-6 Stream Descriptors */
      ISO_13818_TYPE_D = 0x0d,          /* ISO/IEC 13818-6 Sections (any type, including private data) */
      ISO_13818_AUX = 0x0e,             /* ISO/IEC 13818-1 auxiliary */
      ISO_13818_PART7_AUDIO = 0x0f,     /* ISO/IEC 13818-7 Audio with ADTS transport sytax */
      ISO_14496_PART2_VIDEO = 0x10,     /* ISO/IEC 14496-2 Visual (MPEG-4) */
      ISO_14496_PART3_AUDIO = 0x11,     /* ISO/IEC 14496-3 Audio with LATM transport syntax */
      ISO_14496_PART10_VIDEO = 0x1b,    /* ISO/IEC 14496-10 Video (MPEG-4 part 10/AVC, aka H.264) */
      STREAM_VIDEO_HEVC = 0x24,

      STREAM_VIDEO_MPEG      = 0x80,
      STREAM_AUDIO_AC3       = 0x81,

      STREAM_VIDEO_VC1       = 0xea,    /* VC-1 Video */

      HDMV_AUDIO_80_PCM       = 0x80, /* BluRay PCM */
      HDMV_AUDIO_82_DTS       = 0x82, /* DTS */
      HDMV_AUDIO_83_TRUEHD    = 0x83, /* Dolby TrueHD, primary audio */
      HDMV_AUDIO_84_EAC3      = 0x84, /* Dolby Digital plus, primary audio */
      HDMV_AUDIO_85_DTS_HRA   = 0x85, /* DTS-HRA */
      HDMV_AUDIO_86_DTS_HD_MA = 0x86, /* DTS-HD Master audio */

      HDMV_SPU_BITMAP      = 0x90,
      HDMV_SPU_INTERACTIVE = 0x91,
      HDMV_SPU_TEXT        = 0x92,

      /* pseudo tags */
      STREAM_AUDIO_EAC3    = (DESCRIPTOR_EAC3 << 8),
      STREAM_AUDIO_DTS     = (DESCRIPTOR_DTS  << 8),

    } streamType;

#define WRAP_THRESHOLD       360000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

/* bitrate estimation */
#define TBRE_MIN_TIME (  2 * 90000)
#define TBRE_TIME     (480 * 90000)

#define TBRE_MODE_PROBE     0
#define TBRE_MODE_AUDIO_PTS 1
#define TBRE_MODE_AUDIO_PCR 2
#define TBRE_MODE_PCR       3
#define TBRE_MODE_DONE      4


#undef  MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#undef  MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

/*
**
** DATA STRUCTURES
**
*/

/*
 * Describe a single elementary stream.
 */
typedef struct {
  unsigned int     pid;
  fifo_buffer_t   *fifo;
  uint32_t         type;
  int64_t          pts;
  buf_element_t   *buf;
  unsigned int     counter;
  uint16_t         descriptor_tag; /* +0x100 for PES stream IDs (no available TS descriptor tag?) */
  uint8_t          keep;           /* used by demux_ts_dynamic_pmt_*() */
  int              corrupted_pes;
  int              pes_bytes_left; /* butes left if PES packet size is known */

  int              input_normpos;
  int              input_time;
} demux_ts_media;

/* DVBSUB */
#define MAX_SPU_LANGS 32

typedef struct {
  spu_dvb_descriptor_t desc;
  unsigned int pid;
  unsigned int media_index;
} demux_ts_spu_lang;

/* Audio Channels */
#define MAX_AUDIO_TRACKS 32

typedef struct {
    unsigned int pid;
    unsigned int media_index;
    char lang[4];
} demux_ts_audio_track;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;

  const AVCRC      *av_crc;
} demux_ts_class_t;

typedef struct {
  /*
   * The first field must be the "base class" for the plugin!
   */
  demux_plugin_t   demux_plugin;

  xine_stream_t   *stream;

  config_values_t *config;

  fifo_buffer_t   *audio_fifo;
  fifo_buffer_t   *video_fifo;

  input_plugin_t  *input;
  unsigned int     read_retries;

  demux_ts_class_t *class;

  int              status;

  int              hdmv;       /* -1 = unknown, 0 = mpeg-ts, 1 = hdmv/m2ts */
  int              pkt_size;   /* TS packet size */
  int              pkt_offset; /* TS packet offset */

  int              blockSize;
  int              rate;
  unsigned int     media_num;
  demux_ts_media   media[MAX_PIDS];

  /* PAT */
  uint8_t          pat[PAT_BUF_SIZE];
  int              pat_write_pos;
  uint32_t         last_pat_crc;
  uint32_t         transport_stream_id;
  /* programs */
  uint32_t         program_number[MAX_PMTS];
  uint32_t         pmt_pid[MAX_PMTS];
  uint8_t         *pmt[MAX_PMTS];
  int              pmt_write_pos[MAX_PMTS];
  uint32_t         last_pmt_crc;
  /*
   * Stuff to do with the transport header. As well as the video
   * and audio PIDs, we keep the index of the corresponding entry
   * inthe media[] array.
   */
  unsigned int     pcr_pid;
  unsigned int     videoPid;
  unsigned int     videoMedia;

  demux_ts_audio_track audio_tracks[MAX_AUDIO_TRACKS];
  int              audio_tracks_count;

  int64_t          last_pts[2];
  int              send_newpts;
  int              buf_flag_seek;

  unsigned int     scrambled_pids[MAX_PIDS];
  unsigned int     scrambled_npids;

#ifdef TS_READ_STATS
  uint32_t         rstat[NPKT_PER_READ + 1];
#endif

  /* DVBSUB */
  unsigned int      spu_pid;
  unsigned int      spu_media;
  demux_ts_spu_lang spu_langs[MAX_SPU_LANGS];
  int               spu_langs_count;
  int               current_spu_channel;

  /* dvb */
  xine_event_queue_t *event_queue;
  /* For syncronisation */
  int32_t packet_number;
  /* NEW: var to keep track of number of last read packets */
  int32_t npkt_read;

  uint8_t buf[BUF_SIZE]; /* == PKT_SIZE * NPKT_PER_READ */

  off_t   frame_pos; /* current ts packet position in input stream (bytes from beginning) */

  /* bitrate estimation */
  off_t        tbre_bytes, tbre_lastpos;
  int64_t      tbre_time, tbre_lasttime;
  unsigned int tbre_mode, tbre_pid;

} demux_ts_t;


static void reset_track_map(fifo_buffer_t *fifo)
{
  buf_element_t *buf = fifo->buffer_pool_alloc (fifo);

  buf->type            = BUF_CONTROL_RESET_TRACK_MAP;
  buf->decoder_info[1] = -1;

  fifo->put (fifo, buf);
}

/* TJ. dynamic PMT support. The idea is:
   First, reuse unchanged pids and add new ones.
   Then, comb out those who are no longer referenced.
   For example, the Kaffeine dvb frontend preserves original pids but only
   sends the currently user selected ones, plus matching generated pat/pmt */

static int demux_ts_dynamic_pmt_find (demux_ts_t *this,
  int pid, int type, unsigned int descriptor_tag) {
  unsigned int i;
  demux_ts_media *m;
  for (i = 0; i < this->media_num; i++) {
    m = &this->media[i];
    if ((m->pid == pid) && ((m->type & BUF_MAJOR_MASK) == type)) {
      /* mark this media decriptor for reuse */
      m->keep = 1;
      return i;
    }
  }
  if (i < MAX_PIDS) {
    /* prepare new media descriptor */
#ifdef LOG_DYNAMIC_PMT
    const char *name = "";
    if (type == BUF_VIDEO_BASE) name = "video";
    else if (type == BUF_AUDIO_BASE) name = "audio";
    else if (type == BUF_SPU_BASE) name = "subtitle";
    printf ("demux_ts: new %s pid %d\n", name, pid);
#endif
    m = &this->media[i];
    if (type == BUF_AUDIO_BASE) {
      /* allocate new audio track as well */
      if (this->audio_tracks_count >= MAX_AUDIO_TRACKS) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "demux_ts: too many audio PIDs, ignoring pid %d\n", pid);
	return -1;
      }
      m->type = type | this->audio_tracks_count;
      this->audio_tracks[this->audio_tracks_count].pid = pid;
      this->audio_tracks[this->audio_tracks_count].media_index = i;
      this->audio_tracks_count++;
      m->fifo = this->stream->audio_fifo;
    } else {
      m->type = type;
      m->fifo = this->stream->video_fifo;
    }
    m->pid = pid;

    if (m->buf) {
      m->buf->free_buffer(m->buf);
      m->buf = NULL;
    }
    m->counter = INVALID_CC;
    m->corrupted_pes = 1;
    m->pts = 0;

    m->descriptor_tag = descriptor_tag;

    m->keep = 1;
    this->media_num++;
    return i;
  }
  /* table full */
  return -1;
}

static void demux_ts_dynamic_pmt_clean (demux_ts_t *this) {
  int i, count = 0, tracks = 0, spus = 0;
  /* densify media table */
  for (i = 0; i < this->media_num; i++) {
    demux_ts_media *m = &this->media[i];
    int type = m->type & BUF_MAJOR_MASK;
    int chan = m->type & 0xff;
    if (m->keep) {
      m->keep = 0;
      if (type == BUF_VIDEO_BASE) {
        /* adjust single video link */
        this->videoMedia = count;
      } else if (type == BUF_AUDIO_BASE) {
        /* densify audio track table */
        this->audio_tracks[chan].media_index = count;
        if (chan > tracks) {
          m->type = (m->type & ~0xff) | tracks;
          this->audio_tracks[tracks] = this->audio_tracks[chan];
        }
        tracks++;
      } else if (type == BUF_SPU_BASE) {
        /* spu language table has already been rebuilt from scratch.
           Adjust backlinks only */
        while ((spus < this->spu_langs_count) && (this->spu_langs[spus].pid == m->pid)) {
          this->spu_langs[spus].media_index = count;
          spus++;
        }
      }
      if (i > count) {
        this->media[count] = *m;
        m->buf = NULL;
        m->pid = INVALID_PID;
      }
      count++;
    } else {
      /* drop this no longer needed media descriptor */
#ifdef LOG_DYNAMIC_PMT
      const char *name = "";
      if (type == BUF_VIDEO_BASE) name = "video";
      else if (type == BUF_AUDIO_BASE) name = "audio";
      else if (type == BUF_SPU_BASE) name = "subtitle";
      printf ("demux_ts: dropped %s pid %d\n", name, m->pid);
#endif
      if (m->buf) {
        m->buf->free_buffer (m->buf);
        m->buf = NULL;
      }
      m->pid = INVALID_PID;
    }
  }
  if ((tracks < this->audio_tracks_count) && this->audio_fifo) {
    /* at least 1 audio track removed, tell audio decoder loop */
    reset_track_map(this->audio_fifo);
#ifdef LOG_DYNAMIC_PMT
    printf ("demux_ts: new audio track map\n");
#endif
  }
#ifdef LOG_DYNAMIC_PMT
  printf ("demux_ts: using %d pids, %d audio %d subtitle channels\n", count, tracks, spus);
#endif
  /* adjust table sizes */
  this->media_num = count;
  this->audio_tracks_count = tracks;
  /* should really have no effect */
  this->spu_langs_count = spus;
}

static void demux_ts_dynamic_pmt_clear (demux_ts_t *this) {
  unsigned int i;
  for (i = 0; i < this->media_num; i++) {
    if (this->media[i].buf) {
      this->media[i].buf->free_buffer (this->media[i].buf);
      this->media[i].buf = NULL;
    }
  }
  this->media_num = 0;

  this->videoPid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;
  this->spu_media = 0;

  this->pcr_pid = INVALID_PID;

  this->last_pmt_crc = 0;
}


static void demux_ts_tbre_reset (demux_ts_t *this) {
  if (this->tbre_time <= TBRE_TIME) {
    this->tbre_pid  = INVALID_PID;
    this->tbre_mode = TBRE_MODE_PROBE;
  }
}

static void demux_ts_tbre_update (demux_ts_t *this, unsigned int mode, int64_t now) {
  /* select best available timesource on the fly */
  if ((mode < this->tbre_mode) || (now <= 0))
    return;

  if (mode == this->tbre_mode) {
    /* skip discontinuities */
    int64_t diff = now - this->tbre_lasttime;
    if ((diff < 0 ? -diff : diff) < 220000) {
      /* add this step */
      this->tbre_bytes += this->frame_pos - this->tbre_lastpos;
      this->tbre_time += diff;
      /* update bitrate */
      if (this->tbre_time > TBRE_MIN_TIME)
        this->rate = this->tbre_bytes * 90000 / this->tbre_time;
      /* stop analyzing */
      if (this->tbre_time > TBRE_TIME)
        this->tbre_mode = TBRE_MODE_DONE;
    }
  } else {
    /* upgrade timesource */
    this->tbre_mode = mode;
  }

  /* remember where and when */
  this->tbre_lastpos  = this->frame_pos;
  this->tbre_lasttime = now;
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts( demux_ts_t *this, int64_t pts, int video )
{
  int64_t diff;

#ifdef TS_LOG
  printf ("demux_ts: check_newpts %lld, send_newpts %d, buf_flag_seek %d\n",
	  pts, this->send_newpts, this->buf_flag_seek);
#endif

  diff = pts - this->last_pts[video];

  if( pts &&
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

  if( pts )
  {
    /* don't detect a discontinuity only for video respectively audio. It's also a discontinuity
       indication when audio and video pts differ to much e. g. when a pts wrap happens.
       The original code worked well when the wrap happend like this:

       V7 A7 V8 V9 A9 Dv V0 V1 da A1 V2 V3 A3 V4

       Legend:
       Vn = video packet with timestamp n
       An = audio packet with timestamp n
       Dv = discontinuity detected on following video packet
       Da = discontinuity detected on following audio packet
       dv = discontinuity detected on following video packet but ignored
       da = discontinuity detected on following audio packet but ignored

       But with a certain delay between audio and video packets (e. g. the way DVB-S broadcasts
       the packets) the code didn't work:

       V7 V8 A7 V9 Dv V0 _A9_ V1 V2 Da _A1_ V3 V4 A3

       Packet A9 caused audio to jump forward and A1 caused it to jump backward with inserting
       a delay of almoust 26.5 hours!

       The new code gives the following sequences for the above examples:

       V7 A7 V8 V9 A9 Dv V0 V1 A1 V2 V3 A3 V4

       V7 V8 A7 V9 Dv V0 Da A9 Dv V1 V2 A1 V3 V4 A3

       After proving this code it should be cleaned up to use just a single variable "last_pts". */

/*
    this->last_pts[video] = pts;
*/
    this->last_pts[video] = this->last_pts[1-video] = pts;
  }
}

/* Send a BUF_SPU_DVB to let xine know of that channel. */
static void demux_send_special_spu_buf( demux_ts_t *this, uint32_t spu_type, int spu_channel )
{
  buf_element_t *buf;

  buf = this->video_fifo->buffer_pool_alloc( this->video_fifo );
  buf->type = spu_type|spu_channel;
  buf->content = buf->mem;
  buf->size = 0;
  this->video_fifo->put( this->video_fifo, buf );
}

/*
 * demux_ts_update_spu_channel
 *
 * Send a BUF_SPU_DVB with BUF_SPECIAL_SPU_DVB_DESCRIPTOR to tell
 * the decoder to reset itself on the new channel.
 */
static void demux_ts_update_spu_channel(demux_ts_t *this)
{
  buf_element_t *buf;

  this->current_spu_channel = this->stream->spu_channel;

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->type = BUF_SPU_DVB;
  buf->content = buf->mem;
  buf->decoder_flags = BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_SPU_DVB_DESCRIPTOR;
  buf->size = 0;

  if (this->current_spu_channel >= 0
      && this->current_spu_channel < this->spu_langs_count)
    {
      demux_ts_spu_lang *lang = &this->spu_langs[this->current_spu_channel];

      buf->decoder_info[2] = sizeof(lang->desc);
      buf->decoder_info_ptr[2] = &(lang->desc);
      buf->type |= this->current_spu_channel;

      this->spu_pid = lang->pid;
      this->spu_media = lang->media_index;

      /* multiple spu langs can share same media descriptor */
      this->media[lang->media_index].type =
        (this->media[lang->media_index].type & ~0xff) | this->current_spu_channel;
#ifdef TS_LOG
      printf("demux_ts: DVBSUB: selecting lang: %s  page %ld %ld\n",
	     lang->desc.lang, lang->desc.comp_page_id, lang->desc.aux_page_id);
#endif
    }
  else
    {
      buf->decoder_info_ptr[2] = NULL;

      this->spu_pid = INVALID_PID;

#ifdef TS_LOG
      printf("demux_ts: DVBSUB: deselecting lang\n");
#endif
    }

 if ((this->media[this->spu_media].type & BUF_MAJOR_MASK) == BUF_SPU_HDMV) {
   buf->type = BUF_SPU_HDMV;
   buf->type |= this->current_spu_channel;
 }

  this->video_fifo->put(this->video_fifo, buf);
}

static void demux_ts_send_buffer(demux_ts_media *m, int flags)
{
  if (m->buf) {
    m->buf->content = m->buf->mem;
    m->buf->type = m->type;
    m->buf->decoder_flags |= flags;
    m->buf->pts = m->pts;
    m->buf->decoder_info[0] = 1;
    m->buf->extra_info->input_normpos = m->input_normpos;
    m->buf->extra_info->input_time = m->input_time;

    m->fifo->put(m->fifo, m->buf);
    m->buf = NULL;

#ifdef TS_LOG
    printf ("demux_ts: produced buffer, pts=%lld\n", m->pts);
#endif
  }
}

static void demux_ts_flush_media(demux_ts_media *m)
{
  demux_ts_send_buffer(m, BUF_FLAG_FRAME_END);
}

static void demux_ts_flush(demux_ts_t *this)
{
  unsigned int i;
  for (i = 0; i < this->media_num; ++i) {
    demux_ts_flush_media(&this->media[i]);
    this->media[i].corrupted_pes = 1;
  }
}

/*
 * demux_ts_parse_pat
 *
 * Parse a program association table (PAT).
 * The PAT is expected to be exactly one section long.
 *
 * We can cope with the stupidity of SPTSs which contain NITs.
 */
static void demux_ts_parse_pat (demux_ts_t*this, unsigned char *original_pkt,
                                unsigned char *pkt, unsigned int pusi, int len) {
#ifdef TS_PAT_LOG
  uint32_t       table_id;
  uint32_t       version_number;
#endif
  uint32_t       section_syntax_indicator;
  int32_t        section_length;
  uint32_t       transport_stream_id;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       crc32;
  uint32_t       calc_crc32;

  unsigned char *program;
  unsigned int   program_number;
  unsigned int   pmt_pid;
  unsigned int   program_count;

  /* reassemble the section */
  if (pusi) {
    this->pat_write_pos = 0;
    /* offset the section by n + 1 bytes. this is sometimes used to let it end
       at an exact TS packet boundary */
    len -= (unsigned int)pkt[0] + 1;
    pkt += (unsigned int)pkt[0] + 1;
    if (len < 1) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: demux error! PAT with invalid pointer\n");
      return;
    }
  } else {
    if (!this->pat_write_pos)
      return;
  }
  if (len > PAT_BUF_SIZE - this->pat_write_pos)
    len = PAT_BUF_SIZE - this->pat_write_pos;
  memcpy (this->pat + this->pat_write_pos, pkt, len);
  this->pat_write_pos +=len;

  /* lets see if we got the section length already */
  pkt = this->pat;
  if (this->pat_write_pos < 3)
    return;
  section_length = ((((uint32_t)pkt[1] << 8) | pkt[2]) & 0x03ff) + 3;
  /* this should be at least the head plus crc */
  if (section_length < 8 + 4) {
    this->pat_write_pos = 0;
    return;
  }
  /* and it should fit into buf */
  if (section_length > PAT_BUF_SIZE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: PAT too large (%d bytes)\n", section_length);
    this->pat_write_pos = 0;
    return;
  }

  /* lets see if we got the section complete */
  if (this->pat_write_pos < section_length)
    return;

  /* OK now parse it */
  this->pat_write_pos = 0;
#ifdef TS_PAT_LOG
  table_id                  = (unsigned int)pkt[0];
#endif
  section_syntax_indicator  = (pkt[1] >> 7) & 0x01;
  transport_stream_id       = ((uint32_t)pkt[3] << 8) | pkt[4];
#ifdef TS_PAT_LOG
  version_number            = ((uint32_t)pkt[5] >> 1) & 0x1f;
#endif
  current_next_indicator    =  pkt[5] & 0x01;
  section_number            =  pkt[6];
  last_section_number       =  pkt[7];

#ifdef TS_PAT_LOG
  printf ("demux_ts: PAT table_id: %.2x\n", table_id);
  printf ("              section_syntax: %d\n", section_syntax_indicator);
  printf ("              section_length: %d (%#.3x)\n", section_length, section_length);
  printf ("              transport_stream_id: %#.4x\n", transport_stream_id);
  printf ("              version_number: %d\n", version_number);
  printf ("              c/n indicator: %d\n", current_next_indicator);
  printf ("              section_number: %d\n", section_number);
  printf ("              last_section_number: %d\n", last_section_number);
#endif

  if ((section_syntax_indicator != 1) || !current_next_indicator)
    return;

  if ((section_number != 0) || (last_section_number != 0)) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: FIXME (unsupported) PAT consists of multiple (%d) sections\n", last_section_number);
    return;
  }

  /* Check CRC. */
  crc32  = (uint32_t) pkt[section_length - 4] << 24;
  crc32 |= (uint32_t) pkt[section_length - 3] << 16;
  crc32 |= (uint32_t) pkt[section_length - 2] << 8;
  crc32 |= (uint32_t) pkt[section_length - 1];

  calc_crc32 = htonl (av_crc (this->class->av_crc, 0xffffffff, pkt, section_length - 4));
  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! PAT with invalid CRC32: packet_crc32: %.8x calc_crc32: %.8x\n",
	     crc32,calc_crc32);
    return;
  }
#ifdef TS_PAT_LOG
  else {
    printf ("demux_ts: PAT CRC32 ok.\n");
  }
#endif

  if (crc32 == this->last_pat_crc &&
      this->transport_stream_id == transport_stream_id) {
    lprintf("demux_ts: PAT CRC unchanged\n");
    return;
  }

  if (this->transport_stream_id != transport_stream_id) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: PAT transport_stream_id changed\n");
  }

  this->last_pat_crc = crc32;
  this->transport_stream_id = transport_stream_id;

  /*
   * Process all programs in the program loop.
   */
  program_count = 0;
  for (program = pkt + 8;
       (program < pkt + section_length - 4) && (program_count < MAX_PMTS);
       program += 4) {
    program_number = ((unsigned int)program[0] << 8) | program[1];
    pmt_pid = (((unsigned int)program[2] & 0x1f) << 8) | program[3];

    /*
     * completely skip NIT pids.
     */
    if (program_number == 0x0000)
      continue;

    /*
     * Add the program number to the table if we haven't already
     * seen it. The order of the program numbers is assumed not
     * to change between otherwise identical PATs.
     */
    if (this->program_number[program_count] != program_number) {
      this->program_number[program_count] = program_number;
      this->pmt_pid[program_count] = INVALID_PID;
    }

    /* force PMT reparsing when pmt_pid changes */
    if (this->pmt_pid[program_count] != pmt_pid) {
      this->pmt_pid[program_count] = pmt_pid;
      demux_ts_dynamic_pmt_clear (this);

      if (this->pmt[program_count] != NULL) {
	free(this->pmt[program_count]);
	this->pmt[program_count] = NULL;
	this->pmt_write_pos[program_count] = 0;
      }
    }
#ifdef TS_PAT_LOG
    if (this->program_number[program_count] != INVALID_PROGRAM)
      printf ("demux_ts: PAT acquired count=%d programNumber=0x%04x "
              "pmtPid=0x%04x\n",
              program_count,
              this->program_number[program_count],
              this->pmt_pid[program_count]);
#endif

    ++program_count;
  }

  /* Add "end of table" markers. */
  if (program_count < MAX_PMTS) {
    this->program_number[program_count] = INVALID_PROGRAM;
    this->pmt_pid[program_count] = INVALID_PID;
  }
}

static int demux_ts_parse_pes_header (xine_t *xine, demux_ts_media *m,
                                      uint8_t *buf, int packet_len) {

  unsigned char *p;
  uint32_t       header_len;
  int64_t        pts;
  uint32_t       stream_id;

  if (packet_len < 9) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: too short PES packet header (%d bytes)\n", packet_len);
    return 0;
  }

  p = buf;

  /* we should have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: error %02x %02x %02x (should be 0x000001) \n", p[0], p[1], p[2]);
    return 0 ;
  }

  stream_id  = p[3];
  header_len = p[8] + 9;

  /* sometimes corruption on header_len causes segfault in memcpy below */
  if (header_len > packet_len) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: illegal value for PES_header_data_length (0x%x)\n", header_len - 9);
    return 0;
  }

#ifdef TS_LOG
  printf ("demux_ts: packet stream id: %.2x len: %d (%x)\n",
          stream_id, packet_len, packet_len);
#endif

  if (p[7] & 0x80) { /* pts avail */

    if (header_len < 14) {
      return 0;
    }

    pts  = (int64_t)(p[ 9] & 0x0E) << 29 ;
    pts |=  p[10]         << 22 ;
    pts |= (p[11] & 0xFE) << 14 ;
    pts |=  p[12]         <<  7 ;
    pts |= (p[13] & 0xFE) >>  1 ;

  } else
    pts = 0;

  /* code works but not used in xine
     if (p[7] & 0x40) {

     DTS  = (p[14] & 0x0E) << 29 ;
     DTS |=  p[15]         << 22 ;
     DTS |= (p[16] & 0xFE) << 14 ;
     DTS |=  p[17]         <<  7 ;
     DTS |= (p[18] & 0xFE) >>  1 ;

     } else
     DTS = 0;
  */

  m->pts       = pts;

  m->pes_bytes_left = (int)(p[4] << 8 | p[5]) - header_len + 6;
  lprintf("PES packet payload left: %d bytes\n", m->pes_bytes_left);

  p += header_len;
  packet_len -= header_len;

  if (m->descriptor_tag == STREAM_VIDEO_VC1) {
    m->type      = BUF_VIDEO_VC1;
    return header_len;
  }

  if (m->descriptor_tag == STREAM_VIDEO_HEVC) {
    m->type      = BUF_VIDEO_HEVC;
    return header_len;
  }

  if (m->descriptor_tag == HDMV_SPU_BITMAP) {
    m->type |= BUF_SPU_HDMV;
    m->buf->decoder_info[2] = m->pes_bytes_left;
    return header_len;

  } else

  if (stream_id == 0xbd || stream_id == 0xfd /* HDMV */) {

    int spu_id;

    lprintf ("audio buf = %02X %02X %02X %02X %02X %02X %02X %02X\n",
	     p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

    /*
     * we check the descriptor tag first because some stations
     * do not include any of the ac3 header info in their audio tracks
     * these "raw" streams may begin with a byte that looks like a stream type.
     * For audio streams, m->type already contains the stream no.
     */
    if(m->descriptor_tag == HDMV_AUDIO_84_EAC3 ||
       m->descriptor_tag == STREAM_AUDIO_EAC3) {
      m->type |= BUF_AUDIO_EAC3;
      return header_len;

    } else if(m->descriptor_tag == STREAM_AUDIO_AC3) {    /* ac3 - raw */
      m->type |= BUF_AUDIO_A52;
      return header_len;

    } else if (m->descriptor_tag == HDMV_AUDIO_83_TRUEHD) {
      /* TODO: separate AC3 and TrueHD streams ... */
      m->type |= BUF_AUDIO_A52;
      return header_len;

    } else if (m->descriptor_tag == STREAM_AUDIO_DTS ||
               m->descriptor_tag == HDMV_AUDIO_82_DTS ||
               m->descriptor_tag == HDMV_AUDIO_86_DTS_HD_MA ) {
      m->type |= BUF_AUDIO_DTS;
      return header_len;

    } else if (packet_len < 2) {
      return 0;

    } else if (m->descriptor_tag == HDMV_AUDIO_80_PCM) {

      if (packet_len < 4) {
        return 0;
      }

      m->type   |= BUF_AUDIO_LPCM_BE;

      m->buf->decoder_flags  |= BUF_FLAG_SPECIAL;
      m->buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      m->buf->decoder_info[2] = (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];

      m->pes_bytes_left -= 4;
      return header_len + 4;

    } else if (m->descriptor_tag == ISO_13818_PES_PRIVATE
	     && p[0] == 0x20 && p[1] == 0x00) {
      /* DVBSUB */
      m->type |= BUF_SPU_DVB;
      m->buf->decoder_info[2] = m->pes_bytes_left;
      return header_len;

    } else if (p[0] == 0x0B && p[1] == 0x77) { /* ac3 - syncword */
      m->type |= BUF_AUDIO_A52;
      return header_len;

    } else if ((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      m->type      = BUF_SPU_DVD + spu_id;
      m->pes_bytes_left -= 1;
      return header_len + 1;

    } else if ((p[0] & 0xF0) == 0x80) {

      if (packet_len < 4) {
        return 0;
      }

      m->type      |= BUF_AUDIO_A52;
      m->pes_bytes_left -= 4;
      return header_len + 4;

#if 0
    /* commented out: does not set PCM type. Decoder can't handle raw PCM stream without configuration. */
    } else if ((p[0]&0xf0) == 0xa0) {

      unsigned int pcm_offset;

      for (pcm_offset=0; ++pcm_offset < packet_len-1 ; ){
        if (p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
          pcm_offset += 2;
          break;
        }
      }

      if (packet_len < pcm_offset) {
        return 0;
      }

      m->type      |= BUF_AUDIO_LPCM_BE;
      m->pes_bytes_left -= pcm_offset;
      return header_len + pcm_offset;
#endif
    }

  } else if ((stream_id & 0xf0) == 0xe0) {

    switch (m->descriptor_tag) {
    case ISO_11172_VIDEO:
    case ISO_13818_VIDEO:
    case STREAM_VIDEO_MPEG:
      lprintf ("demux_ts: found MPEG video type.\n");
      m->type      = BUF_VIDEO_MPEG;
      break;
    case ISO_14496_PART2_VIDEO:
      lprintf ("demux_ts: found MPEG4 video type.\n");
      m->type      = BUF_VIDEO_MPEG4;
      break;
    case ISO_14496_PART10_VIDEO:
      lprintf ("demux_ts: found H264 video type.\n");
      m->type      = BUF_VIDEO_H264;
      break;
    default:
      lprintf ("demux_ts: unknown video type: %d, defaulting to MPEG.\n", m->descriptor_tag);
      m->type      = BUF_VIDEO_MPEG;
      break;
    }
    return header_len;

  } else if ((stream_id & 0xe0) == 0xc0) {

    switch (m->descriptor_tag) {
    case  ISO_11172_AUDIO:
    case  ISO_13818_AUDIO:
      lprintf ("demux_ts: found MPEG audio track.\n");
      m->type      |= BUF_AUDIO_MPEG;
      break;
    case  ISO_13818_PART7_AUDIO:
      lprintf ("demux_ts: found AAC audio track.\n");
      m->type      |= BUF_AUDIO_AAC;
      break;
    case  ISO_14496_PART3_AUDIO:
      lprintf ("demux_ts: found AAC LATM audio track.\n");
      m->type      |= BUF_AUDIO_AAC_LATM;
      break;
    default:
      lprintf ("demux_ts: unknown audio type: %d, defaulting to MPEG.\n", m->descriptor_tag);
      m->type      |= BUF_AUDIO_MPEG;
      break;
    }
    return header_len;

  } else {
#ifdef TS_LOG
    printf ("demux_ts: unknown packet, id: %x\n", stream_id);
#endif
  }

  return 0 ;
}

static void update_extra_info(demux_ts_t *this, demux_ts_media *m)
{
  off_t length = this->input->get_length (this->input);

  /* cache frame position */

  if (length > 0) {
    m->input_normpos = (double)this->frame_pos * 65535.0 / length;
  }
  if (this->rate) {
    m->input_time = this->frame_pos * 1000 / this->rate;
  }
}

/*
 *  buffer arriving pes data
 */
static void demux_ts_buffer_pes(demux_ts_t*this, unsigned char *ts,
                                unsigned int mediaIndex,
                                unsigned int pus,
                                unsigned int cc,
                                unsigned int len) {

  demux_ts_media *m = &this->media[mediaIndex];

  if (!m->fifo) {
#ifdef TS_LOG
    printf ("fifo unavailable (%d)\n", mediaIndex);
#endif
    return; /* To avoid segfault if video out or audio out plugin not loaded */
  }

  /* By checking the CC here, we avoid the need to check for the no-payload
     case (i.e. adaptation field only) when it does not get bumped. */
  if (m->counter != INVALID_CC) {
    if ((m->counter & 0x0f) != cc) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_ts: PID 0x%.4x: unexpected cc %d (expected %d)\n", m->pid, cc, m->counter);
    }
  }
  m->counter = cc;
  m->counter++;

  if (pus) { /* new PES packet */

    demux_ts_flush_media(m);

    /* allocate the buffer here, as pes_header needs a valid buf for dvbsubs */
    m->buf = m->fifo->buffer_pool_alloc(m->fifo);
    /* dont let decoder crash on incomplete frames.
       Also needed by net_buf_ctrl/dvbspeed. */
    m->buf->decoder_flags |= BUF_FLAG_FRAME_START;

    int pes_header_len = demux_ts_parse_pes_header(this->stream->xine, m, ts, len);

    if (pes_header_len <= 0) {
      m->buf->free_buffer(m->buf);
      m->buf = NULL;

      m->corrupted_pes++;
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PID 0x%.4x: corrupted pes encountered\n", m->pid);
    } else {

      m->corrupted_pes = 0;

      /* skip PES header */
      ts  += pes_header_len;
      len -= pes_header_len;

      update_extra_info(this, m);

      /* rate estimation */
      if ((this->tbre_pid == INVALID_PID) && (this->audio_fifo == m->fifo))
        this->tbre_pid = m->pid;
      if (m->pid == this->tbre_pid)
        demux_ts_tbre_update (this, TBRE_MODE_AUDIO_PTS, m->pts);
    }
  }

  if (!m->corrupted_pes) {

    if ((m->buf->size + len) > m->buf->max_size) {
      m->pes_bytes_left -= m->buf->size;
      demux_ts_send_buffer(m, 0);
      m->buf = m->fifo->buffer_pool_alloc(m->fifo);
    }

    memcpy(m->buf->mem + m->buf->size, ts, len);
    m->buf->size += len;

    if (m->pes_bytes_left > 0 && m->buf->size >= m->pes_bytes_left) {
      /* PES payload complete */
      m->pes_bytes_left -= m->buf->size;
      demux_ts_flush_media(m);
      /* skip rest data - there shouldn't be any */
      m->corrupted_pes = 1;
    } else {

      /* If video data ends to sequence end code, flush buffer. */
      /* (there won't be any more data -> no pusi -> last buffer is never flushed) */
      if (m->pid == this->videoPid && m->buf->size > 4 && m->buf->mem[m->buf->size-4] == 0) {

        if (m->type == BUF_VIDEO_MPEG) {
          if (!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\xb7", 4)) {
            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "demux_ts: PID 0x%.4x: flushing after MPEG end of sequence code\n", m->pid);
            demux_ts_flush_media(m);
          }

        } else if (m->type == BUF_VIDEO_H264) {
          if ((!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\x0a", 4)) ||
              (m->buf->size > 5 &&
               !memcmp(&m->buf->mem[m->buf->size-5], "\x00\x00\x01\x0a", 4))) {
            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "demux_ts: PID 0x%.4x: flushing after H.264 end of sequence code\n", m->pid);
            demux_ts_flush_media(m);
          }
        } else if (m->type == BUF_VIDEO_VC1) {
          if (!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\x0a", 4)) {
            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "demux_ts: PID 0x%.4x: flushing after VC-1 end of sequence code\n", m->pid);
            demux_ts_flush_media(m);
          }
        }
      }
    }
  }
}

/* Find the first ISO 639 language descriptor (tag 10) and
 * store the 3-char code in dest, nullterminated.  If no
 * code is found, zero out dest.
 **/
static void demux_ts_get_lang_desc(demux_ts_t *this, char *dest,
				   const unsigned char *data, int length)
{
  const unsigned char *d = data;

  while (d < (data + length))

    {
      if (d[0] == DESCRIPTOR_LANG && d[1] >= 4)

	{
      memcpy(dest, d + 2, 3);
	  dest[3] = 0;
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found ISO 639 lang: %s\n", dest);
	  return;
	}
      d += 2 + d[1];
    }
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found no ISO 639 lang\n");
  memset(dest, 0, 4);
}

/* Find the registration code (tag=5) and return it as a uint32_t
 * This should return "AC-3" or 0x41432d33 for AC3/A52 audio tracks.
 */
static void demux_ts_get_reg_desc(demux_ts_t *this, uint32_t *dest,
				   const unsigned char *data, int length)
{
  const unsigned char *d = data;

  while (d < (data + length))

    {
      if (d[0] == DESCRIPTOR_REG_FORMAT && d[1] >= 4)

	{
          *dest = (d[2] << 24) | (d[3] << 16) | (d[4] << 8) | d[5];
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "demux_ts: found registration format identifier: 0x%.4x\n", *dest);
	  return;
	}
      d += 2 + d[1];
    }
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found no format id\n");
  *dest = 0;
}

static inline int ts_payloadsize(unsigned char * tsp)
{
  if (!(tsp[3] & 0x10))
     return 0;
  if (tsp[3] & 0x20) {
     if (tsp[4] > 183)
       return 0;
     else
       return 183 - tsp[4];
  }
  return 184;
}

/* check if an apid is in the list of known apids */
static int apid_check(demux_ts_t*this, unsigned int pid) {
  int i;
  for (i = 0; i < this->audio_tracks_count; i++) {
    if (this->audio_tracks[i].pid == pid)
      return i;
  }
  return -1;
}

/*
 * NAME demux_ts_parse_pmt
 *
 * Parse a PMT. The PMT is expected to be exactly one section long.
 *
 * In other words, the PMT is assumed to describe a reasonable number of
 * video, audio and other streams (with descriptors).
 * FIXME: Implement support for multi section PMT.
 */
static void demux_ts_parse_pmt (demux_ts_t     *this,
                                unsigned char *originalPkt,
                                unsigned char *pkt,
                                unsigned int   pusi,
                                int            plen,
                                uint32_t       program_count) {

#ifdef TS_PMT_LOG
  uint32_t       table_id;
  uint32_t       version_number;
#endif
  uint32_t       section_syntax_indicator;
  uint32_t       section_length;
  uint32_t       program_number;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       program_info_length;
  uint32_t       crc32;
  uint32_t       calc_crc32;
  uint32_t       coded_length;
  unsigned int	 pid;
  unsigned char *stream;
  unsigned int	 i;
  int            mi;

  /* reassemble the section */
  if (pusi) {
    /* allocate space for largest possible section */
    if (!this->pmt[program_count])
      this->pmt[program_count] = malloc (4098);
    if (!this->pmt[program_count])
      return;
    this->pmt_write_pos[program_count] = 0;
    /* offset the section by n + 1 bytes. this is sometimes used to let it end
       at an exact TS packet boundary */
    plen -= (unsigned int)pkt[0] + 1;
    pkt += (unsigned int)pkt[0] + 1;
    if (plen < 1) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: demux error! PMT with invalid pointer\n");
      return;
    }
  } else {
    if (!this->pmt_write_pos[program_count])
      return;
  }
  if (plen > 4098 - this->pmt_write_pos[program_count])
    plen = 4098 - this->pmt_write_pos[program_count];
  memcpy (this->pmt[program_count] + this->pmt_write_pos[program_count], pkt, plen);
  this->pmt_write_pos[program_count] += plen;

  /* lets see if we got the section length already */
  pkt = this->pmt[program_count];
  if (this->pmt_write_pos[program_count] < 3)
    return;
  section_length = ((((uint32_t)pkt[1] << 8) | pkt[2]) & 0xfff) + 3;
  /* this should be at least the head plus crc */
  if (section_length < 8 + 4) {
    this->pmt_write_pos[program_count] = 0;
    return;
  }

  /* lets see if we got the section complete */
  if (this->pmt_write_pos[program_count] < section_length)
    return;

  /* OK now parse it */
  this->pmt_write_pos[program_count] = 0;
#ifdef TS_PMT_LOG
  table_id                  = (unsigned int)pkt[0];
#endif
  section_syntax_indicator  = (pkt[1] >> 7) & 0x01;
  program_number            = ((uint32_t)pkt[3] << 8) | pkt[4];
#ifdef TS_PMT_LOG
  version_number            = ((uint32_t)pkt[5] >> 1) & 0x1f;
#endif
  current_next_indicator    =  pkt[5] & 0x01;
  section_number            =  pkt[6];
  last_section_number       =  pkt[7];

#ifdef TS_PMT_LOG
  printf ("demux_ts: PMT table_id: %.2x\n", table_id);
  printf ("              section_syntax: %d\n", section_syntax_indicator);
  printf ("              section_length: %d (%#.3x)\n", section_length, section_length);
  printf ("              program_number: %#.4x\n", program_number);
  printf ("              version_number: %d\n", version_number);
  printf ("              c/n indicator: %d\n", current_next_indicator);
  printf ("              section_number: %d\n", section_number);
  printf ("              last_section_number: %d\n", last_section_number);
#endif

  if ((section_syntax_indicator != 1) || !current_next_indicator)
    return;

  if ((section_number != 0) || (last_section_number != 0)) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: FIXME (unsupported) PMT consists of multiple (%d) sections\n", last_section_number);
    return;
  }

  /* Check CRC. */
  crc32  = (uint32_t) pkt[section_length - 4] << 24;
  crc32 |= (uint32_t) pkt[section_length - 3] << 16;
  crc32 |= (uint32_t) pkt[section_length - 2] << 8;
  crc32 |= (uint32_t) pkt[section_length - 1];

  calc_crc32 = htonl (av_crc (this->class->av_crc, 0xffffffff, pkt, section_length - 4));
  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: demux error! PMT with invalid CRC32: packet_crc32: %#.8x calc_crc32: %#.8x\n",
      crc32, calc_crc32);
    return;
  }
#ifdef TS_PMT_LOG
  printf ("demux_ts: PMT CRC32 ok.\n");
#endif

  if (program_number != this->program_number[program_count]) {
    /* several programs can share the same PMT pid */
#ifdef TS_PMT_LOG
printf("Program Number is %i, looking for %i\n",program_number,this->program_number[program_count]);
      printf ("ts_demux: waiting for next PMT on this PID...\n");
#endif
    return;
  }

  if (crc32 == this->last_pmt_crc) {
#ifdef TS_PMT_LOG
    printf("demux_ts: PMT with CRC32=%d already parsed. Skipping.\n", crc32);
#endif
    return;
  }
  this->last_pmt_crc = crc32;

  /* dont "parse" the CRC */
  section_length -= 4;

  /*
   * Forget the current video, audio and subtitle PIDs; if the PMT has not
   * changed, we'll pick them up again when we parse this PMT, while if the
   * PMT has changed (e.g. an IPTV streamer that's just changed its source),
   * we'll get new PIDs that we should follow.
   */
  this->videoPid = INVALID_PID;
  this->spu_pid = INVALID_PID;

  this->spu_langs_count = 0;
  reset_track_map(this->video_fifo);

  /*
   * ES definitions start here...we are going to learn upto one video
   * PID and one audio PID.
   */
  program_info_length = ((pkt[10] << 8) | pkt[11]) & 0x0fff;

/* Program info descriptor is currently just ignored.
 * printf ("demux_ts: program_info_desc: ");
 * for (i = 0; i < program_info_length; i++)
 *       printf ("%.2x ", this->pmt[program_count][12+i]);
 * printf ("\n");
 */
  stream = pkt + 12 + program_info_length;
  coded_length = 12 + program_info_length;
  if (coded_length > section_length) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux error! PMT with inconsistent progInfo length\n");
    return;
  }
  section_length -= coded_length;

  /*
   * Extract the elementary streams.
   */
  while (section_length > 0) {
    unsigned int stream_info_length;

    pid = ((stream[1] << 8) | stream[2]) & 0x1fff;
    stream_info_length = ((stream[3] << 8) | stream[4]) & 0x0fff;
    coded_length = 5 + stream_info_length;
    if (coded_length > section_length) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux error! PMT with inconsistent streamInfo length\n");
      return;
    }

    /*
     * Squirrel away the first audio and the first video stream. TBD: there
     * should really be a way to select the stream of interest.
     */
    switch (stream[0]) {
    case ISO_11172_VIDEO:
    case ISO_13818_VIDEO:
    case ISO_14496_PART2_VIDEO:
    case ISO_14496_PART10_VIDEO:
    case STREAM_VIDEO_HEVC:
    case STREAM_VIDEO_VC1:
      if (this->videoPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT video pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif

        mi = demux_ts_dynamic_pmt_find (this, pid, BUF_VIDEO_BASE, stream[0]);
        if (mi >= 0) {
	  this->videoMedia = mi;
	  this->videoPid = pid;
	}
      }

      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
    case ISO_13818_PART7_AUDIO:
    case ISO_14496_PART3_AUDIO:
      if (this->audio_tracks_count < MAX_AUDIO_TRACKS) {

        mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE, stream[0]);
        if (mi >= 0) {
#ifdef TS_PMT_LOG
          printf ("demux_ts: PMT audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
          demux_ts_get_lang_desc (this,
            this->audio_tracks[this->media[mi].type & 0xff].lang,
            stream + 5, stream_info_length);
        }

      }
      break;
    case ISO_13818_PRIVATE:
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype 13818_PRIVATE, pid: 0x%.4x type %2.2x\n", pid, stream[0]);

      for (i = 5; i < coded_length; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    case  ISO_13818_TYPE_C: /* data carousel */
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype 13818_TYPE_C, pid: 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
      break;
    case ISO_13818_PES_PRIVATE:
      {
        uint32_t format_identifier=0;
        demux_ts_get_reg_desc(this, &format_identifier, stream + 5, stream_info_length);
        if (format_identifier == 0x48455643 /* HEVC */ ) {
          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_VIDEO_BASE, STREAM_VIDEO_HEVC);
          if (mi >= 0) {
            this->videoMedia = mi;
            this->videoPid = pid;
          }
          break;
        }
      }

      for (i = 5; i < coded_length; i += stream[i+1] + 2) {

        if ((stream[i] == DESCRIPTOR_AC3) || (stream[i] == DESCRIPTOR_EAC3) || (stream[i] == DESCRIPTOR_DTS)) {
          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE,
            stream[i] == DESCRIPTOR_AC3 ? STREAM_AUDIO_AC3 :
            stream[i] == DESCRIPTOR_DTS ? STREAM_AUDIO_DTS :
            STREAM_AUDIO_EAC3);
          if (mi >= 0) {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT AC3 audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
            demux_ts_get_lang_desc (this,
              this->audio_tracks[this->media[mi].type & 0xff].lang,
              stream + 5, stream_info_length);
            break;
          }
        }

        /* Teletext */
        else if (stream[i] == DESCRIPTOR_TELETEXT)
          {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT Teletext, pid: 0x%.4x type %2.2x\n", pid, stream[0]);

            for (i = 5; i < coded_length; i++)
              printf ("%.2x ", stream[i]);
            printf ("\n");
#endif
            break;
          }

	/* DVBSUB */
	else if (stream[i] == DESCRIPTOR_DVBSUB)
	  {
	    int pos;

	    mi = demux_ts_dynamic_pmt_find (this, pid, BUF_SPU_BASE, stream[0]);
	    if (mi < 0) break;

            for (pos = i + 2;
		 pos + 8 <= i + 2 + stream[i + 1]
		   && this->spu_langs_count < MAX_SPU_LANGS;
		 pos += 8)
	      {
		int no = this->spu_langs_count;
		demux_ts_spu_lang *lang = &this->spu_langs[no];

		this->spu_langs_count++;

		memcpy(lang->desc.lang, &stream[pos], 3);
		lang->desc.lang[3] = 0;
		lang->desc.comp_page_id =
		  (stream[pos + 4] << 8) | stream[pos + 5];
		lang->desc.aux_page_id =
		  (stream[pos + 6] << 8) | stream[pos + 7];
		lang->pid = pid;
		lang->media_index = mi;
		demux_send_special_spu_buf( this, BUF_SPU_DVB, no );
#ifdef TS_LOG
		printf("demux_ts: DVBSUB: pid 0x%.4x: %s  page %ld %ld type %2.2x\n",
		       pid, lang->desc.lang,
		       lang->desc.comp_page_id,
		       lang->desc.aux_page_id,
                       stream[0]);
#endif
	      }
	  }
      }
      break;

    case HDMV_SPU_INTERACTIVE:
    case HDMV_SPU_TEXT:
      if (this->hdmv > 0) {
        printf("demux_ts: Skipping unsupported HDMV subtitle stream_type: 0x%.2x pid: 0x%.4x\n",
               stream[0], pid);
        break;
      }
      /* fall thru */

    case HDMV_SPU_BITMAP:
      if (this->hdmv > 0) {
	if (pid >= 0x1200 && pid < 0x1300) {
	  /* HDMV Presentation Graphics / SPU */

	  if (this->spu_langs_count >= MAX_SPU_LANGS) {
	    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		     "demux_ts: too many SPU tracks! ignoring pid 0x%.4x\n",
		     pid);
	    break;
	  }

	  mi = demux_ts_dynamic_pmt_find (this, pid, BUF_SPU_BASE, stream[0]);
	  if (mi < 0) break;


	  demux_ts_spu_lang *lang = &this->spu_langs[this->spu_langs_count];

	  memset(lang->desc.lang, 0, sizeof(lang->desc.lang));
	  /*memcpy(lang->desc.lang, &stream[pos], 3);*/
	  /*lang->desc.lang[3] = 0;*/
	  lang->pid = pid;
	  lang->media_index = mi;
	  demux_send_special_spu_buf( this, BUF_SPU_HDMV, this->spu_langs_count );
	  this->spu_langs_count++;
#ifdef TS_PMT_LOG
	  printf("demux_ts: HDMV subtitle stream_type: 0x%.2x pid: 0x%.4x\n",
		 stream[0], pid);
#endif
	  break;
	}
      }
      /* fall thru */
    default:

/* This following section handles all the cases where the audio track info is stored in PMT user info with stream id >= 0x80
 * We first check that the stream id >= 0x80, because all values below that are invalid if not handled above,
 * then we check the registration format identifier to see if it holds "AC-3" (0x41432d33) and
 * if is does, we tag this as an audio stream.
 * FIXME: This will need expanding if we ever see a DTS or other media format here.
 */
      if ((this->audio_tracks_count < MAX_AUDIO_TRACKS) && (stream[0] >= 0x80) ) {

        uint32_t format_identifier=0;
        demux_ts_get_reg_desc(this, &format_identifier, stream + 5, stream_info_length);
        /* If no format identifier, assume A52 */
        if (( format_identifier == 0x41432d33) ||
            ( format_identifier == 0) ||
            ((format_identifier == 0x48444d56 || this->hdmv>0) && stream[0] == HDMV_AUDIO_80_PCM) /* BluRay PCM */) {

          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE, stream[0]);
          if (mi >= 0) {
            demux_ts_get_lang_desc (this,
              this->audio_tracks[this->media[mi].type & 0xff].lang,
              stream + 5, stream_info_length);
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
            break;
          }
        }
      }

      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PMT unknown stream_type: 0x%.2x pid: 0x%.4x\n",
              stream[0], pid);

#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT unknown stream_type: 0x%.2x pid: 0x%.4x\n",
              stream[0], pid);

      for (i = 5; i < coded_length; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    }
    stream += coded_length;
    section_length -= coded_length;
  }

  demux_ts_dynamic_pmt_clean (this);

  /*
   * Get the current PCR PID.
   */
  pid = ((this->pmt[program_count][8] << 8)
         | this->pmt[program_count][9]) & 0x1fff;
  if (this->pcr_pid != pid) {
#ifdef TS_PMT_LOG
    if (this->pcr_pid == INVALID_PID) {
      printf ("demux_ts: PMT pcr pid 0x%.4x\n", pid);
    } else {
      printf ("demux_ts: PMT pcr pid changed 0x%.4x\n", pid);
    }
#endif
    this->pcr_pid = pid;
  }

  if ( this->stream->spu_channel>=0 && this->spu_langs_count>0 )
    demux_ts_update_spu_channel( this );

  demux_ts_tbre_reset (this);

  /* Inform UI of channels changes */
  xine_event_t ui_event;
  ui_event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  ui_event.data_length = 0;
  xine_event_send( this->stream, &ui_event );
}

static int sync_correct(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int p = 0;
  int n = 0;
  int i = 0;
  int sync_ok = 0;
  int read_length;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: about to resync!\n");

  for (p=0; p < npkt_read; p++) {
    for(n=0; n < this->pkt_size; n++) {
      sync_ok = 1;
      for (i=0; i < MIN(MIN_SYNCS, npkt_read - p); i++) {
	if (buf[this->pkt_offset + n + ((i+p) * this->pkt_size)] != SYNC_BYTE) {
	  sync_ok = 0;
	  break;
	}
      }
      if (sync_ok) break;
    }
    if (sync_ok) break;
  }

  if (sync_ok) {
    /* Found sync, fill in */
    memmove(&buf[0], &buf[n + p * this->pkt_size],
	    ((this->pkt_size * (npkt_read - p)) - n));
    read_length = this->input->read(this->input,
				    &buf[(this->pkt_size * (npkt_read - p)) - n],
				    n + p * this->pkt_size);
    /* FIXME: when read_length is not as required... we now stop demuxing */
    if (read_length != (n + p * this->pkt_size)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_ts_tsync_correct: sync found, but read failed\n");
      return 0;
    }
  } else {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts_tsync_correct: sync not found! Stop demuxing\n");
    return 0;
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: resync successful!\n");
  return 1;
}

static int sync_detect(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int i, sync_ok;

  sync_ok = 1;

  if (this->hdmv) {
    this->pkt_size   = PKT_SIZE + 4;
    this->pkt_offset = 4;
    for (i=0; i < MIN(MIN_SYNCS, npkt_read - 3); i++) {
      if (buf[this->pkt_offset + i * this->pkt_size] != SYNC_BYTE) {
	sync_ok = 0;
	break;
      }
    }
    if (sync_ok) {
      if (this->hdmv < 0) {
        /* fix npkt_read (packet size is 192, not 188) */
        this->npkt_read = npkt_read * PKT_SIZE / this->pkt_size;
      }
      this->hdmv = 1;
      return sync_ok;
    }
    if (this->hdmv > 0)
      return sync_correct(this, buf, npkt_read);

    /* plain ts */
    this->hdmv       = 0;
    this->pkt_size   = PKT_SIZE;
    this->pkt_offset = 0;
  }

  for (i=0; i < MIN(MIN_SYNCS, npkt_read); i++) {
    if (buf[i * PKT_SIZE] != SYNC_BYTE) {
      sync_ok = 0;
      break;
    }
  }
  if (!sync_ok) return sync_correct(this, buf, npkt_read);
  return sync_ok;
}


/*
 *  Main synchronisation routine.
 */
static unsigned char * demux_synchronise(demux_ts_t* this) {

  uint8_t *return_pointer = NULL;
  int32_t read_length;

  this->frame_pos += this->pkt_size;

  if ( (this->packet_number) >= this->npkt_read) {

    /* NEW: handle read returning less packets than NPKT_PER_READ... */
    do {
      this->frame_pos = this->input->get_current_pos (this->input);

      read_length = this->input->read(this->input, this->buf,
                                      this->pkt_size * NPKT_PER_READ);

      if (read_length < 0) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                 "demux_ts: read returned %d\n", read_length);
        if (this->read_retries > 2)
          this->status = DEMUX_FINISHED;
        this->read_retries++;
        return NULL;
      }
      this->read_retries = 0;

      if (read_length % this->pkt_size) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "demux_ts: read returned %d bytes (not a multiple of %d!)\n",
		 read_length, this->pkt_size);
	this->status = DEMUX_FINISHED;
	return NULL;
      }
      this->npkt_read = read_length / this->pkt_size;

#ifdef TS_READ_STATS
      this->rstat[this->npkt_read]++;
#endif
      /*
       * what if this->npkt_read < 5 ? --> ok in sync_detect
       *
       * NEW: stop demuxing if read returns 0 a few times... (200)
       */

      if (this->npkt_read == 0) {
        demux_ts_flush(this);
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: read 0 packets\n");
	this->status = DEMUX_FINISHED;
	return NULL;
      }

    } while (! read_length);

    this->packet_number = 0;

    if (!sync_detect(this, &(this->buf)[0], this->npkt_read)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: sync error.\n");
      this->status = DEMUX_FINISHED;
      return NULL;
    }
  }
  return_pointer = &(this->buf)[this->pkt_offset + this->pkt_size * this->packet_number];
  this->packet_number++;
  return return_pointer;
}


static int64_t demux_ts_adaptation_field_parse(uint8_t *data,
					       uint32_t adaptation_field_length) {

#ifdef TS_LOG
  uint32_t    discontinuity_indicator=0;
  uint32_t    random_access_indicator=0;
  uint32_t    elementary_stream_priority_indicator=0;
#endif
  uint32_t    PCR_flag=0;
  int64_t     PCR=-1;
#ifdef TS_LOG
  uint32_t    EPCR=0;
  uint32_t    OPCR_flag=0;
  uint32_t    OPCR=0;
  uint32_t    EOPCR=0;
  uint32_t    slicing_point_flag=0;
  uint32_t    transport_private_data_flag=0;
  uint32_t    adaptation_field_extension_flag=0;
#endif
  uint32_t    offset = 1;

#ifdef TS_LOG
  discontinuity_indicator = ((data[0] >> 7) & 0x01);
  random_access_indicator = ((data[0] >> 6) & 0x01);
  elementary_stream_priority_indicator = ((data[0] >> 5) & 0x01);
#endif
  PCR_flag = ((data[0] >> 4) & 0x01);
#ifdef TS_LOG
  OPCR_flag = ((data[0] >> 3) & 0x01);
  slicing_point_flag = ((data[0] >> 2) & 0x01);
  transport_private_data_flag = ((data[0] >> 1) & 0x01);
  adaptation_field_extension_flag = (data[0] & 0x01);
#endif

#ifdef TS_LOG
  printf ("demux_ts: ADAPTATION FIELD length: %d (%x)\n",
          adaptation_field_length, adaptation_field_length);
  if(discontinuity_indicator) {
    printf ("               Discontinuity indicator: %d\n",
            discontinuity_indicator);
  }
  if(random_access_indicator) {
    printf ("               Random_access indicator: %d\n",
            random_access_indicator);
  }
  if(elementary_stream_priority_indicator) {
    printf ("               Elementary_stream_priority_indicator: %d\n",
            elementary_stream_priority_indicator);
  }
#endif

  if(PCR_flag) {
    if (adaptation_field_length < offset + 6)
      return -1;

    PCR  = (((int64_t) data[offset]) & 0xFF) << 25;
    PCR += (int64_t) ((data[offset+1] & 0xFF) << 17);
    PCR += (int64_t) ((data[offset+2] & 0xFF) << 9);
    PCR += (int64_t) ((data[offset+3] & 0xFF) << 1);
    PCR += (int64_t) ((data[offset+4] & 0x80) >> 7);

#ifdef TS_LOG
    EPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];
    printf ("demux_ts: PCR: %lld, EPCR: %u\n",
            PCR, EPCR);
#endif
    offset+=6;
  }

#ifdef TS_LOG
  if(OPCR_flag) {
    if (adaptation_field_length < offset + 6)
      return PCR;

    OPCR = data[offset] << 25;
    OPCR |= data[offset+1] << 17;
    OPCR |= data[offset+2] << 9;
    OPCR |= data[offset+3] << 1;
    OPCR |= (data[offset+4] >> 7) & 0x01;
    EOPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];

    printf ("demux_ts: OPCR: %u, EOPCR: %u\n",
            OPCR,EOPCR);

    offset+=6;
  }

  if(slicing_point_flag) {
    printf ("demux_ts: slicing_point_flag: %d\n",
            slicing_point_flag);
  }
  if(transport_private_data_flag) {
    printf ("demux_ts: transport_private_data_flag: %d\n",
	    transport_private_data_flag);
  }
  if(adaptation_field_extension_flag) {
    printf ("demux_ts: adaptation_field_extension_flag: %d\n",
            adaptation_field_extension_flag);
  }
#endif /* TS_LOG */

  return PCR;
}

/* transport stream packet layer */
static void demux_ts_parse_packet (demux_ts_t*this) {

  unsigned char *originalPkt;
  unsigned int   sync_byte;
  unsigned int   transport_error_indicator;
  unsigned int   payload_unit_start_indicator;
#ifdef TS_HEADER_LOG
  unsigned int   transport_priority;
#endif
  unsigned int   pid;
  unsigned int   transport_scrambling_control;
  unsigned int   adaptation_field_control;
  unsigned int   continuity_counter;
  unsigned int   data_offset;
  unsigned int   data_len;
  uint32_t       program_count;
  int i;

  /* get next synchronised packet, or NULL */
  originalPkt = demux_synchronise(this);
  if (originalPkt == NULL)
    return;

  sync_byte                      = originalPkt[0];
  transport_error_indicator      = (originalPkt[1]  >> 7) & 0x01;
  payload_unit_start_indicator   = (originalPkt[1] >> 6) & 0x01;
#ifdef TS_HEADER_LOG
  transport_priority             = (originalPkt[1] >> 5) & 0x01;
#endif
  pid                            = ((originalPkt[1] << 8) |
				    originalPkt[2]) & 0x1fff;
  transport_scrambling_control   = (originalPkt[3] >> 6)  & 0x03;
  adaptation_field_control       = (originalPkt[3] >> 4) & 0x03;
  continuity_counter             = originalPkt[3] & 0x0f;


#ifdef TS_HEADER_LOG
  printf("demux_ts:ts_header:sync_byte=0x%.2x\n",sync_byte);
  printf("demux_ts:ts_header:transport_error_indicator=%d\n", transport_error_indicator);
  printf("demux_ts:ts_header:payload_unit_start_indicator=%d\n", payload_unit_start_indicator);
  printf("demux_ts:ts_header:transport_priority=%d\n", transport_priority);
  printf("demux_ts:ts_header:pid=0x%.4x\n", pid);
  printf("demux_ts:ts_header:transport_scrambling_control=0x%.1x\n", transport_scrambling_control);
  printf("demux_ts:ts_header:adaptation_field_control=0x%.1x\n", adaptation_field_control);
  printf("demux_ts:ts_header:continuity_counter=0x%.1x\n", continuity_counter);
#endif
  /*
   * Discard packets that are obviously bad.
   */
  if (sync_byte != SYNC_BYTE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux error! invalid ts sync byte %.2x\n", sync_byte);
    return;
  }
  if (transport_error_indicator) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux error! transport error\n");
    return;
  }
  if (pid == 0x1ffb) {
      /* printf ("demux_ts: PSIP table. Program Guide etc....not supported yet. PID = 0x1ffb\n"); */
      return;
  }

  if (transport_scrambling_control) {
    if (this->videoPid == pid) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_ts: selected videoPid is scrambled; skipping...\n");
    }
    for (i=0; i < this->scrambled_npids; i++) {
      if (this->scrambled_pids[i] == pid) return;
    }
    if (this->scrambled_npids < MAX_PIDS) {
      this->scrambled_pids[this->scrambled_npids] = pid;
      this->scrambled_npids++;
    }

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: PID 0x%.4x is scrambled!\n", pid);
    return;
  }

  data_offset = 4;

  if( adaptation_field_control & 0x2 ){
    uint32_t adaptation_field_length = originalPkt[4];
    if (adaptation_field_length > 0) {
      int64_t pcr = demux_ts_adaptation_field_parse (originalPkt+5, adaptation_field_length);
      if (pid == this->pcr_pid)
        demux_ts_tbre_update (this, TBRE_MODE_PCR, pcr);
      else if (pid == this->tbre_pid)
        demux_ts_tbre_update (this, TBRE_MODE_AUDIO_PCR, pcr);
    }
    /*
     * Skip adaptation header.
     */
    data_offset += adaptation_field_length + 1;
  }

  if (! (adaptation_field_control & 0x1)) {
    return;
  }

  /* PAT */
  /* PMT */
  // PAT and PMT are not processed for openpliPC. PIDs are recognized in E2

  data_len = PKT_SIZE - data_offset;

  if (data_len > PKT_SIZE) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! invalid payload size %d\n", data_len);

  } else {

    /*
     * Do the demuxing in descending order of packet frequency!
     */
    int index;
    if (pid == this->videoPid) {
#ifdef TS_LOG
      printf ("demux_ts: Video pid: 0x%.4x\n", pid);
#endif
      check_newpts(this, this->media[this->videoMedia].pts, PTS_VIDEO);
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->videoMedia,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if ((index = apid_check(this, pid)) > -1) {
#ifdef TS_LOG
      printf ("demux_ts: Audio pid: 0x%.4x\n", pid);
#endif
      check_newpts(this, this->media[this->audio_tracks[index].media_index].pts, PTS_AUDIO);
      demux_ts_buffer_pes (this, originalPkt+data_offset,
               this->audio_tracks[index].media_index,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if (pid == NULL_PID) {
#ifdef TS_LOG
      printf ("demux_ts: Null Packet\n");
#endif
      return;
    }
    /* DVBSUB */
    else if (pid == this->spu_pid) {
#ifdef TS_LOG
      printf ("demux_ts: SPU pid: 0x%.4x\n", pid);
#endif
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->spu_media,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
  }
}

/*
 * check for pids change events
 */

static void demux_ts_event_handler (demux_ts_t *this) {

  xine_event_t *event;
  int           mi;

  while ((event = xine_event_get (this->event_queue))) {


    switch (event->type) {

    case XINE_EVENT_END_OF_CLIP:
      /* flush all streams */
      demux_ts_flush(this);
      /* fall thru */
      break;
      
    case XINE_EVENT_PIDS_CHANGE:
      demux_ts_dynamic_pmt_clear(this); 
      this->send_newpts = 1;
      _x_demux_control_start (this->stream);
      break;
      
    case XINE_EVENT_SET_VIDEO_STREAMTYPE:
      printf("RECEIVED XINE_EVENT_SET_VIDEO_STREAMTYPE\n");
 
      if (event->data) {
        xine_streamtype_data_t* data = (xine_streamtype_data_t*)event->data;

        mi = demux_ts_dynamic_pmt_find (this, data->pid, BUF_VIDEO_BASE, data->streamtype);
        if (mi >= 0) {
          this->videoPid = data->pid;
          this->videoMedia = mi;
        }
      }
      break;

    case XINE_EVENT_SET_AUDIO_STREAMTYPE:
      printf("RECEIVED XINE_EVENT_SET_AUDIO_STREAMTYPE\n");

      if (event->data) {
        xine_streamtype_data_t* data = (xine_streamtype_data_t*)event->data;

        mi = demux_ts_dynamic_pmt_find (this, data->pid, BUF_AUDIO_BASE, data->streamtype);
      }
      break;  

    }

    xine_event_free (event);
  }
}

/*
 * send a piece of data down the fifos
 */

static int demux_ts_send_chunk (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  demux_ts_event_handler (this);

  demux_ts_parse_packet(this);

  /* DVBSUB: check if channel has changed.  Dunno if I should, or
   * even could, lock the xine object. */
  if (this->stream->spu_channel != this->current_spu_channel) {
    demux_ts_update_spu_channel(this);
  }

  return this->status;
}

static void demux_ts_dispose (demux_plugin_t *this_gen) {
  int i;
  demux_ts_t*this = (demux_ts_t*)this_gen;

  for (i=0; i < MAX_PMTS; i++) {
    if (this->pmt[i] != NULL) {
      free(this->pmt[i]);
      this->pmt[i] = NULL;
    }
  }
  for (i=0; i < MAX_PIDS; i++) {
    if (this->media[i].buf != NULL) {
      this->media[i].buf->free_buffer(this->media[i].buf);
      this->media[i].buf = NULL;
    }
  }

  xine_event_dispose_queue (this->event_queue);

  free(this_gen);
}

static int demux_ts_get_status(demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  return this->status;
}

static void demux_ts_send_headers (demux_plugin_t *this_gen) {

  demux_ts_t *this = (demux_ts_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /*
   * send start buffers
   */

  this->videoPid = INVALID_PID;
  this->pcr_pid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->media_num= 0;
  this->last_pmt_crc = 0;

  _x_demux_control_start (this->stream);

  this->input->seek (this->input, 0, SEEK_SET);

  this->send_newpts = 1;

  this->status = DEMUX_OK ;

  this->scrambled_npids   = 0;

  /* DVBSUB */
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;
  this->current_spu_channel = -1;

  /* FIXME ? */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
}

static int demux_ts_seek (demux_plugin_t *this_gen,
			  off_t start_pos, int start_time, int playing) {

  demux_ts_t *this = (demux_ts_t *) this_gen;
  int i;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    if ((!start_pos) && (start_time)) {

      if (this->input->seek_time) {
        this->input->seek_time(this->input, start_time, SEEK_SET);
      } else {
        start_pos = (int64_t)start_time * this->rate / 1000;
        this->input->seek (this->input, start_pos, SEEK_SET);
      }

    } else {
      this->input->seek (this->input, start_pos, SEEK_SET);
    }
  }

  this->send_newpts = 1;

  for (i=0; i<MAX_PIDS; i++) {
    demux_ts_media *m = &this->media[i];

    if (m->buf != NULL)
      m->buf->free_buffer(m->buf);
    m->buf            = NULL;
    m->counter        = INVALID_CC;
    m->corrupted_pes  = 1;
    m->pts            = 0;
  }

  if( !playing ) {

    this->status        = DEMUX_OK;
    this->buf_flag_seek = 0;

  } else {

    this->buf_flag_seek = 1;
    _x_demux_flush_engine(this->stream);

  }

  demux_ts_tbre_reset (this);

  return this->status;
}

static int demux_ts_get_stream_length (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  if (this->rate)
    return (int)((int64_t) this->input->get_length (this->input)
                 * 1000 / this->rate);
  else
    return 0;
}


static uint32_t demux_ts_get_capabilities(demux_plugin_t *this_gen)
{
  return DEMUX_CAP_AUDIOLANG | DEMUX_CAP_SPULANG;
}

static int demux_ts_get_optional_data(demux_plugin_t *this_gen,
				      void *data, int data_type)
{
  demux_ts_t *this = (demux_ts_t *) this_gen;
  char *str = data;
  int channel = *((int *)data);

  /* be a bit paranoid */
  if (this == NULL || this->stream == NULL)
    return DEMUX_OPTIONAL_UNSUPPORTED;

  switch (data_type)
    {
    case DEMUX_OPTIONAL_DATA_AUDIOLANG:
      if ((channel >= 0) && (channel < this->audio_tracks_count)) {
        if (this->audio_tracks[channel].lang[0]) {
          strcpy(str, this->audio_tracks[channel].lang);
        } else {
          /* input plugin may know the language */
          if (this->input->get_capabilities(this->input) & INPUT_CAP_AUDIOLANG)
            return DEMUX_OPTIONAL_UNSUPPORTED;
          sprintf(str, "%3i", channel);
        }
        return DEMUX_OPTIONAL_SUCCESS;
      }
      else {
        strcpy(str, "none");
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;

    case DEMUX_OPTIONAL_DATA_SPULANG:
      if (channel>=0 && channel<this->spu_langs_count) {
        if (this->spu_langs[channel].desc.lang[0]) {
          strcpy(str, this->spu_langs[channel].desc.lang);
        } else {
          /* input plugin may know the language */
          if (this->input->get_capabilities(this->input) & INPUT_CAP_SPULANG)
            return DEMUX_OPTIONAL_UNSUPPORTED;
          sprintf(str, "%3i", channel);
        }
        return DEMUX_OPTIONAL_SUCCESS;
      } else {
        strcpy(str, "none");
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;

    default:
      return DEMUX_OPTIONAL_UNSUPPORTED;
    }
}

static int detect_ts(uint8_t *buf, size_t len, int ts_size)
{
  int    i, j;
  int    try_again, ts_detected = 0;
  size_t packs = len / ts_size - 2;

  for (i = 0; i < ts_size; i++) {
    try_again = 0;
    if (buf[i] == SYNC_BYTE) {
      for (j = 1; j < packs; j++) {
	if (buf[i + j*ts_size] != SYNC_BYTE) {
	  try_again = 1;
	  break;
	}
      }
      if (try_again == 0) {
#ifdef TS_LOG
	printf ("demux_ts: found 0x47 pattern at offset %d\n", i);
#endif
	ts_detected = 1;
      }
    }
  }

  return ts_detected;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
				    xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_ts_t *this;
  int         i;
  int         hdmv = -1;
  int         size;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[2069];

    size = _x_demux_read_header(input, buf, sizeof(buf));
    if (size < PKT_SIZE)
      return NULL;

    if (detect_ts(buf, sizeof(buf), PKT_SIZE))
      hdmv = 0;
    else if (size >= PKT_SIZE + 4 && detect_ts(buf, sizeof(buf), PKT_SIZE+4))
      hdmv = 1;
    else
      return NULL;
  }
    break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
    break;

  default:
    return NULL;
  }

  /*
   * if we reach this point, the input has been accepted.
   */

  this            = calloc(1, sizeof(*this));
  this->stream    = stream;
  this->input     = input;
  this->class     = (demux_ts_class_t*)class_gen;

  this->demux_plugin.send_headers      = demux_ts_send_headers;
  this->demux_plugin.send_chunk        = demux_ts_send_chunk;
  this->demux_plugin.seek              = demux_ts_seek;
  this->demux_plugin.dispose           = demux_ts_dispose;
  this->demux_plugin.get_status        = demux_ts_get_status;
  this->demux_plugin.get_stream_length = demux_ts_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ts_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ts_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  /*
   * Initialise our specialised data.
   */

  this->last_pat_crc = 0;
  this->transport_stream_id = -1;

  for (i = 0; i < MAX_PIDS; i++) {
    this->media[i].pid = INVALID_PID;
    this->media[i].buf = NULL;
  }

  for (i = 0; i < MAX_PMTS; i++) {
    this->program_number[i]          = INVALID_PROGRAM;
    this->pmt_pid[i]                 = INVALID_PID;
    this->pmt[i]                     = NULL;
    this->pmt_write_pos[i]           = 0;
  }

  this->scrambled_npids = 0;
  this->videoPid = INVALID_PID;
  this->pcr_pid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->last_pmt_crc = 0;

  this->rate = 1000000; /* byte/sec */
  this->tbre_pid = INVALID_PID;

  this->status = DEMUX_FINISHED;

  /* DVBSUB */
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;
  this->current_spu_channel = -1;

  /* dvb */
  this->event_queue = xine_event_new_queue (this->stream);

  /* HDMV */
  this->hdmv       = hdmv;
  this->pkt_offset = (hdmv > 0) ? 4 : 0;
  this->pkt_size   = PKT_SIZE + this->pkt_offset;

  return &this->demux_plugin;
}

/*
 * ts demuxer class
 */
static void *init_class (xine_t *xine, void *data) {

  demux_ts_class_t     *this;

  this         = calloc(1, sizeof(demux_ts_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("MPEG Transport Stream demuxer");
  this->demux_class.identifier      = "MPEG_TS";
  this->demux_class.mimetypes       = "video/mp2t: m2t: MPEG2 transport stream;";

  /* accept dvb streams; also handle the special dvbs,dvbt and dvbc
   * mrl formats: the content is exactly the same but the input plugin
   * uses a different tuning algorithm [Pragma]
   */
  this->demux_class.extensions      = "ts m2t trp m2ts mts dvb:// dvbs:// dvbc:// dvbt://";
  this->demux_class.dispose         = default_demux_class_dispose;

  this->av_crc = av_crc_get_table(AV_CRC_32_IEEE);

  return this;
}


/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_ts = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "mpeg-ts", XINE_VERSION_CODE, &demux_info_ts, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

