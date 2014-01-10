/*
 * Copyright (C) 2004-2013 the xine project
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
 * Flash Video (.flv) File Demuxer
 *   by Mike Melanson (melanson@pcisys.net) and
 *      Claudio Ciccani (klan@users.sf.net)
 *   rewritten by Torsten Jager (t.jager@gmx.de)
 *
 * For more information on the FLV file format, visit:
 * http://www.adobe.com/devnet/flv/pdf/video_file_format_spec_v10.pdf
 *
 * TJ. FLV actually is a persistent variant of Realtime Messaging Protocol
 * (rtmp). Some features, most notably message interleaving and relative
 * timestamps, have been removed. Official spec imposes further restrictions.
 * We should nevertheless be prepared for more general stuff left by rtmp
 * stream recorders.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_flv"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"

typedef struct {
  unsigned int         pts;
  off_t                offset;
} flv_index_entry_t;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_t              *xine;
  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned char        flags;
  off_t                start;  /* in bytes */
  off_t                size;   /* in bytes */

  unsigned char        got_video_header, got_audio_header, got_info;

  unsigned int         length; /* in ms */
  int                  width;
  int                  height;
  int                  duration;
  int                  videocodec;

  int                  samplerate;
  int                  samplesize;
  int                  audio_channels;
  int                  audiocodec;

  off_t                filesize;

  flv_index_entry_t   *index;
  unsigned int         num_indices;

  unsigned int         cur_pts;

  int64_t              last_pts[2];
  int                  send_newpts;
  int                  buf_flag_seek;

  int                  audiodelay;   /* fine tune a/v sync */

  unsigned int         zero_pts_count;
} demux_flv_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_flv_class_t;

/* an early FLV specification had 24bit bigendian timestamps. This
   limited clip duration to 4:39:37.215. The backwards compatible solution:
   hand over 1 byte from stream ID. Hence the weird byte order 2-1-0-3 */
#define gettimestamp(p,o) (((((((uint32_t)p[o+3]<<8)|p[o])<<8)|p[o+1])<<8)|p[o+2])

#define FLV_FLAG_HAS_VIDEO       0x01
#define FLV_FLAG_HAS_AUDIO       0x04

#define FLV_TAG_TYPE_AUDIO       0x08
#define FLV_TAG_TYPE_VIDEO       0x09
#define FLV_TAG_TYPE_NOTIFY      0x12

typedef enum {
  AF_PCM_BE,  /* officially "native endian"?? */
  AF_ADPCM,
  AF_MP3,
  AF_PCM_LE,  /* little endian */
  AF_NELLY16, /* Nellymoser 16KHz */
  AF_NELLY8,  /* Nellymoser 8KHz */
  AF_NELLY,   /* Nellymoser */
  AF_ALAW,    /* G.711 A-LAW */
  AF_MULAW,   /* G.711 MU-LAW */
  AF_reserved9,
  AF_AAC,     /* mp4a with global header */
  AF_SPEEX,
  AF_reserved12,
  AF_reserved13,
  AF_MP38,    /* MP3 8KHz */
  AF_DS       /* device specific sound */
} af_t;

/* audio types that support free samplerate from header */
/* got the message ? ;-) */
#define IS_PCM(id) ((((1<<AF_PCM_BE)|(1<<AF_ADPCM)|(1<<AF_PCM_LE)|(1<<AF_ALAW)|(1<<AF_MULAW))>>(id))&1)

typedef enum {
  VF_reserved0,
  VF_JPEG,
  VF_FLV1,    /* modified Sorenson H.263 */
  VF_SCREEN,  /* Macromedia screen video v1 */
  VF_VP6,     /* On2 VP6 */
  VF_VP6A,    /* On2 VP6 with alphachannel */
  VF_SCREEN2, /* v2 */
  VF_H264,    /* MPEG4 part 10, usually with global sequence parameter set */
  VF_H263,
  VF_MP4      /* MPEG4 part 2, usually with global sequence parameter set */
} vf_t;

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

#define WRAP_THRESHOLD           220000
#define PTS_AUDIO                0
#define PTS_VIDEO                1

static void check_newpts (demux_flv_t *this, int64_t pts, int video) {
  int64_t diff;
  lprintf ("check_newpts %"PRId64"\n", pts);
  if (this->buf_flag_seek) {
    _x_demux_control_newpts (this->stream, pts, BUF_FLAG_SEEK);
    this->buf_flag_seek = 0;
    this->send_newpts   = 0;
    this->last_pts[1 - video] = 0;
  } else {
    diff = pts - this->last_pts[video];
    if (pts && this->last_pts[video] && abs (diff) > WRAP_THRESHOLD) {
      lprintf ("diff=%"PRId64"\n", diff);
      _x_demux_control_newpts (this->stream, pts, 0);
      this->send_newpts = 0;
      this->last_pts[1-video] = 0;
    }
  }
  this->last_pts[video] = pts;
}

/* returns 1 if the FLV file was opened successfully, 0 otherwise */
static int open_flv_file(demux_flv_t *this) {
  unsigned char buffer[9];

  if (_x_demux_read_header(this->input, buffer, 9) != 9)
    return 0;

  if ((buffer[0] != 'F') || (buffer[1] != 'L') || (buffer[2] != 'V'))
    return 0;

  if (buffer[3] != 0x01) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
      _("unsupported FLV version (%d).\n"), buffer[3]);
    return 0;
  }

  this->flags = buffer[4];
  if ((this->flags & (FLV_FLAG_HAS_VIDEO | FLV_FLAG_HAS_AUDIO)) == 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
      _("neither video nor audio stream in this file.\n"));
    return 0;
  }

  this->start = _X_BE_32(&buffer[5]);
  this->size = this->input->get_length(this->input);

  this->input->seek(this->input, this->start, SEEK_SET);

  lprintf("  qualified FLV file, repositioned @ offset 0x%" PRIxMAX "\n",
          (intmax_t)this->start);

  return 1;
}

#define BE_F64(buf) ({\
  union { uint64_t q; double d; } _tmp;\
  _tmp.q = _X_BE_64(buf);\
  _tmp.d;\
})\

/* Action Message Format data types */
typedef enum {
  AMF0_NUMBER       = 0x00,  /* double_be */
  AMF0_BOOLEAN      = 0x01,  /* 1 byte TRUE or FALSE */
  AMF0_STRING       = 0x02,  /* u16_be length, then utf8 string without end byte */
  AMF0_OBJECT       = 0x03,  /* name/type/data triplets, then empty name plus
                                AMF0_OBJECT_END. name stored same way as AMF0_STRING */
  AMF0_MOVIECLIP    = 0x04,  /* reserved */
  AMF0_NULL_VALUE   = 0x05,  /* no data */
  AMF0_UNDEFINED    = 0x06,  /* no data */
  AMF0_REFERENCE    = 0x07,  /* u16be index into previous items table */
  AMF0_ECMA_ARRAY   = 0x08,  /* u32_be number_of_entries, then same as AMF0_OBJECT */
  AMF0_OBJECT_END   = 0x09,  /* end marker of AMF0_OBJECT */
  AMF0_STRICT_ARRAY = 0x0a,  /* u32_be n, then exactly n type/value pairs */
  AMF0_DATE         = 0x0b,  /* double_be milliseconds since Jan 01, 1970, then
                                s16_be minutes off UTC */
  AMF0_LONG_STRING  = 0x0c,  /* u32_be length, then utf8 string */
  AMF0_UNSUPPORTED  = 0x0d,  /* no data */
  AMF0_RECORD_SET   = 0x0e,  /* reserved */
  AMF0_XML_OBJECT   = 0x0f,  /* physically same as AMF0_LONG_STRING */
  AMF0_TYPED_OBJECT = 0x10,  /* very complex, should not appear in FLV */
  AMF0_AMF3         = 0x11,  /* switch to AMF3 from here */
} amf_type_t;

#define MAX_AMF_LEVELS 10
#define SPC (space + 2 * (MAX_AMF_LEVELS - level))
#define NEEDBYTES(n) if ((unsigned long int)(end - p) < n) return 0

static int parse_amf (demux_flv_t *this, unsigned char *buf, int size) {
  unsigned char *p = buf, *end = buf + size, *name, space[2 * MAX_AMF_LEVELS + 3];
  int level = 0, i, type, count[MAX_AMF_LEVELS], info = 0;
  unsigned int u, c;
  time_t tsecs;
  struct tm *tstruct;
  double val;

  /* init prettyprinter */
  memset (space, ' ', 2 * MAX_AMF_LEVELS + 2);
  space[2 * MAX_AMF_LEVELS + 2] = 0x00;
  /* top level has nameless vars */
  count[0] = 10000;
  while (1) {
    if (count[level] > 0) {
      /* next strict array item */
      if (--count[level] == 0) {
        /* one level up */
        if (--level < 0) return 0;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "%s}\n", SPC);
        continue;
      }
      if (p >= end) break;
      type = *p++;
      name = NULL;
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "%s", SPC);
    } else {
      /* get current name */
      NEEDBYTES (2);
      u = _X_BE_16 (p);
      p += 2;
      NEEDBYTES (u);
      name = p;
      p += u;
      if (u == 0) {
        /* object end, 1 level up */
        if (--level < 0) return 0;
        if ((p < end) && (*p == AMF0_OBJECT_END)) p++;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "%s}\n", SPC);
        continue;
      }
      NEEDBYTES (1);
      type = *p;
      *p++ = 0x00;
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "%s%s = ", SPC, name);
    }
    switch (type) {
      case AMF0_NUMBER:
        NEEDBYTES (8);
        val = BE_F64 (p);
        i = val;
        if (i == val) xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%d\n", i);
        else xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%.03lf\n", val);
        p += 8;
        if (name && info) {
          if (!strcmp (name, "duration")) {
            this->length = val * (double)1000;
          } else if (!strcmp (name, "width")) {
            this->width = i;
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, i);
          } else if (!strcmp (name, "height")) {
            this->height = i;
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, i);
          } else if (!strcmp (name, "framerate") || !strcmp (name, "videoframerate")) {
            if ((i > 0) && (i < 1000)) {
              this->duration = (double)90000 / val;
              _x_stream_info_set (this->stream, XINE_STREAM_INFO_FRAME_DURATION,
                this->duration);
            }
          } else if (!strcmp (name, "videodatarate")) {
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_BITRATE,
              val * (double)1000);
          } else if (!strcmp (name, "videocodecid")) {
            this->videocodec = i;
          } else if (!strcmp (name, "audiosamplerate")) {
            this->samplerate = i;
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, i);
          } else if (!strcmp (name, "audiosamplesize")) {
            this->samplesize = i;
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_AUDIO_BITS, i);
          } else if (!strcmp (name, "stereo")) {
            this->audio_channels = i ? 2 : 1;
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, i);
          } else if (!strcmp (name, "audiodatarate")) {
            _x_stream_info_set (this->stream, XINE_STREAM_INFO_AUDIO_BITRATE,
              val * (double)1000);
          } else if (!strcmp (name, "audiocodecid")) {
            this->audiocodec = i;
          } else if (!strcmp (name, "filesize")) {
            this->filesize = val;
          } else if (!strcmp (name, "audiodelay")) {
            this->audiodelay = val * (double)-1000;
          }
        }
      break;
      case AMF0_BOOLEAN:
        NEEDBYTES (1);
        i = !!(*p++);
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%s\n", i ? "yes" : "no");
        if (name && info) {
          if (!strcmp (name, "stereo"))
            this->audio_channels = i ? 2 : 1;
        }
      break;
      case AMF0_STRING:
        NEEDBYTES (2);
        u = _X_BE_16 (p);
        p += 2;
        NEEDBYTES (u);
        c = p[u];
        p[u] = 0x00;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "\"%s\"\n", p);
        if (!level && !strcmp (p, "onMetaData")) info = this->got_info = 1;
        if (name && info) {
          if ((!strcmp (name, "audiocodecid")) && (!strcmp (p, "mp4a")))
            this->audiocodec = AF_AAC;
          else if ((!strcmp (name, "videocodecid")) && (!strcmp (p, "avc1")))
            this->videocodec = VF_H264;
          else if (!strcmp (name, "stereo")) {
            if (!strcmp (p, "true") || !strcmp (p, "yes"))
              this->audio_channels = 2;
          }
        }
        p[u] = c;
        p += u;
      break;
      case AMF0_LONG_STRING:
      case AMF0_XML_OBJECT:
        NEEDBYTES (4);
        u = _X_BE_32 (p);
        p += 4;
        NEEDBYTES (u);
        /* avoid printf() overload */
        if (u > 4000) p[4000] = 0x00;
        c = p[u];
        p[u] = 0x00;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%s\n", p);
        p[u] = c;
        p += u;
      break;
      case AMF0_ECMA_ARRAY:
        NEEDBYTES (4);
        u = _X_BE_32 (p); /* this value is unreliable */
        p += 4;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "[%d] ", u);
        /* fall through */
      case AMF0_OBJECT:
        if (++level >= MAX_AMF_LEVELS) return 0;
        count[level] = -1;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "{\n");
      continue;
      case AMF0_STRICT_ARRAY:
        NEEDBYTES (4);
        u = _X_BE_32 (p);
        p += 4;
        c = 0;
        if (name) {
          if (!strcmp (name, "times")) c = 1;
          else if (!strcmp (name, "filepositions")) c = 2;
        }
        if (c) {
          NEEDBYTES (u * 9);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "[%d] {..}\n", u);
          if (!this->index || (this->num_indices != u)) {
            if (this->index) free (this->index);
            this->index = calloc (u, sizeof (flv_index_entry_t));
            if (!this->index) return 0;
            this->num_indices = u;
          }
          if (c == 1) for (i = 0; i < (int)u; i++) {
            if (*p++ != AMF0_NUMBER) return 0;
            this->index[i].pts = BE_F64 (p) * (double)1000;
            p += 8;
          } else for (i = 0; i < (int)u; i++) {
            if (*p++ != AMF0_NUMBER) return 0;
            this->index[i].offset = BE_F64 (p);
            p += 8;
          }
        } else {
          if (++level >= MAX_AMF_LEVELS) return 0;
          count[level] = u + 1;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "[%d] {\n", u);
        }
      break;
      case AMF0_DATE:
        NEEDBYTES (10);
        val = BE_F64 (p) / (double)1000;
        tsecs = val;
        p += 8;
        i = _X_BE_16 (p);
        p += 2;
        if (i & 0x8000) i |= ~0x7fff;
        tsecs += i * 60;
        tstruct = gmtime (&tsecs);
        if (tstruct) {
          char ts[200];
          if (strftime (ts, 200, "%x %X", tstruct) >= 0)
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%s\n", ts);
        }
      break;
      case AMF0_NULL_VALUE:
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "(null)\n");
      break;
      case AMF0_UNDEFINED:
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "(undefined)\n");
      break;
      case AMF0_REFERENCE:
        NEEDBYTES (2);
        u = _X_BE_16 (p);
        p += 2;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "(see #%u)\n", u);
      break;
      default:
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "(unhandled type %d)\n", type);
        return 0;
      break;
    }
  }
  return level == 0;
}

#define GETBYTES(n) \
  if (remaining_bytes < n) \
    continue; \
  if (this->input->read (this->input, (char *)buffer + 16, n) != n) \
    goto fail; \
  remaining_bytes -= n;

static int read_flv_packet(demux_flv_t *this, int preview) {
  fifo_buffer_t *fifo = NULL;
  buf_element_t *buf  = NULL;
  unsigned int   tag_type, mp4header, buf_type = 0;
  unsigned int   buf_flags = BUF_FLAG_FRAME_START;
  int            remaining_bytes = 0;

  unsigned int   pts; /* ms */
  int            ptsoffs = 0; /* pts ticks */
  int64_t        buf_pts;
  off_t          size, normpos = 0;

  this->status = DEMUX_OK;

  while (1) {
    /* define this here to prevent compiler caching it across multiple this->input->read ()'s.
       layout:
        0 ..  3  previous tags footer (not needed here)
        4        tag type
        5 ..  7  payload size
        8 .. 11  dts (= pts unless H.264 or MPEG4)
       12 .. 14  stream id (usually 0)
       15        codec id / frame type / audio params (A/V only)
       16        right / bottom crop (VP6(F))
                 frame subtype (AAC, H264, MPEG4)
       17 .. 19  ?? (VP6F)
                 pts - dts (H264, MPEG4) */
    unsigned char  buffer[20];
    /* skip rest, if any */
    if (remaining_bytes)
      this->input->seek (this->input, remaining_bytes, SEEK_CUR);
    /* we have a/v tags mostly, optimize for them */
    if (this->input->read (this->input, (char *)buffer, 16) != 16) {
  fail:
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    remaining_bytes = _X_BE_24(&buffer[5]);
    /* skip empty tags */
    if (--remaining_bytes < 0)
      continue;
    tag_type = buffer[4];
    pts = gettimestamp (buffer, 8);
    mp4header = 0;

    switch (tag_type) {
      case FLV_TAG_TYPE_AUDIO:
        if (!pts)
          this->zero_pts_count++;
        else if (this->audiodelay > 0)
          pts += this->audiodelay;
        this->audiocodec = buffer[15] >> 4;

        switch (this->audiocodec) {
          case AF_PCM_BE:
            buf_type = BUF_AUDIO_LPCM_BE;
          break;
          case AF_ADPCM:
            buf_type = BUF_AUDIO_FLVADPCM;
          break;
          case AF_MP3:
          case AF_MP38:
            buf_type = BUF_AUDIO_MPEG;
          break;
          case AF_PCM_LE:
            buf_type = BUF_AUDIO_LPCM_LE;
          break;
          case AF_ALAW:
            buf_type = BUF_AUDIO_ALAW;
          break;
          case AF_MULAW:
            buf_type = BUF_AUDIO_MULAW;
          break;
#ifdef BUF_AUDIO_NELLYMOSER
          case AF_NELLY:
          case AF_NELLY8:
          case AF_NELLY16:
            buf_type = BUF_AUDIO_NELLYMOSER;
          break;
#endif
          case AF_AAC:
            buf_type = BUF_AUDIO_AAC;
            GETBYTES (1);
            if (!buffer[16]) mp4header = 1;
          break;
          case AF_SPEEX:
            buf_type = BUF_AUDIO_SPEEX;
            break;
          default:
            lprintf("  unsupported audio format (%d)...\n", this->audiocodec);
            buf_type = BUF_AUDIO_UNKNOWN;
            break;
        }

        fifo = this->audio_fifo;
        if (!this->got_audio_header) {
          /* prefer tag header settings, unless we hit some unofficial libavformat extension */
          if (!IS_PCM (this->audiocodec) || (buffer[15] & 0x0c) || (this->samplerate < 4000)) {
            this->samplerate =
              this->audiocodec == AF_NELLY8 || this->audiocodec == AF_MP38 ? 8000 :
              this->audiocodec == AF_NELLY16 ? 16000 :
              44100 >> (3 - ((buffer[15] >> 2) & 3));
            this->audio_channels = (buffer[15] & 1) + 1;
          }
          if (!this->audio_channels)
            this->audio_channels = (buffer[15] & 1) + 1;
          /* send init info to audio decoder */
          buf = fifo->buffer_pool_alloc(fifo);
          buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
          buf->decoder_info[0] = 0;
          buf->decoder_info[1] = this->samplerate;
          buf->decoder_info[2] = (buffer[15] & 2) ? 16 : 8; /* bits per sample */
          buf->decoder_info[3] = this->audio_channels;
          buf->size = 0; /* no extra data */
          buf->type = buf_type;
          fifo->put(fifo, buf);
          this->got_audio_header = 1;
        }
      break;

      case FLV_TAG_TYPE_VIDEO:
        if (!pts)
          this->zero_pts_count++;
        else if (this->audiodelay < 0)
          pts -= this->audiodelay;
        /* check frame type */
        switch (buffer[15] >> 4) {
          case 1: /* Key or seekable frame */
          case 4: /* server generated keyframe */
            buf_flags |= BUF_FLAG_KEYFRAME;
          break;
          case 5:
            /* This is a rtmp server command.
               One known use:
               When doing a rtmp time seek between key frames, server may send:
               1. a type 5 frame of one 0x00 byte
               2. the nearest keyframe before the seek time
               3. the following frames before the seek time, if any
               4. a type 5 frame of one 0x01 byte */
          continue;
          default: ;
        }
        this->videocodec = buffer[15] & 0x0F;
        switch (this->videocodec) {
          case VF_FLV1:
            buf_type = BUF_VIDEO_FLV1;
          break;
          case VF_H263:
            buf_type = BUF_VIDEO_H263;
          break;
          case VF_MP4:
            buf_type = BUF_VIDEO_MPEG4;
          goto comm_mpeg4;
          case VF_H264:
            buf_type = BUF_VIDEO_H264;
            /* AVC extra header */
          comm_mpeg4:
            GETBYTES (4);
            if (buffer[16] == 2)
              continue; /* skip sequence footer */
            if (!buffer[16])
              mp4header = 1;
            /* pts really is dts here, buffer[17..19] has (pts - dts) signed big endian. */
            ptsoffs = _X_BE_24 (buffer + 17);
            if (ptsoffs & 0x800000)
              ptsoffs |= ~0xffffff;
            /* better: +/- 16 frames, but we cannot trust header framerate */
            if ((ptsoffs < -1000) || (ptsoffs > 1000))
              ptsoffs = 0;
            ptsoffs *= 90;
          break;
          case VF_VP6:
            buf_type = BUF_VIDEO_VP6F;
            GETBYTES (1);
          break;
          case VF_VP6A:
            buf_type = BUF_VIDEO_VP6F;
            GETBYTES (4);
          break;
          case VF_JPEG:
            buf_type = BUF_VIDEO_JPEG;
          break;
          default:
            lprintf("  unsupported video format (%d)...\n", this->videocodec);
            buf_type = BUF_VIDEO_UNKNOWN;
          break;
        }

        fifo = this->video_fifo;
        if (!this->got_video_header) {
          xine_bmiheader *bih;
          /* send init info to video decoder; send the bitmapinfo header to the decoder
           * primarily as a formality since there is no real data inside */
          buf = fifo->buffer_pool_alloc(fifo);
          buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
          if (this->duration) {
            buf->decoder_flags  |= BUF_FLAG_FRAMERATE;
            buf->decoder_info[0] = this->duration;
          }
          bih = (xine_bmiheader *) buf->content;
          memset (bih, 0, sizeof(xine_bmiheader));
          bih->biSize   = sizeof(xine_bmiheader);
          bih->biWidth  = this->width;
          bih->biHeight = this->height;
          buf->size     = sizeof(xine_bmiheader);
          buf->type     = buf_type;
          if (buf_type == BUF_VIDEO_VP6F) {
            *((unsigned char *)buf->content+buf->size) = buffer[16];
            bih->biSize++;
            buf->size++;
          }
          fifo->put(fifo, buf);
          this->got_video_header = 1;
        }
      break;

      case FLV_TAG_TYPE_NOTIFY:
        if (!this->got_info) {
          unsigned char *text;
          this->input->seek (this->input, -1, SEEK_CUR);
          remaining_bytes++;
          text = malloc (remaining_bytes + 1); /* 1 more byte for possible string end */
          if (!text || this->input->read (this->input, (char *)text, remaining_bytes) != remaining_bytes) {
            free (text);
            goto fail;
          }
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_flv: stream info:\n");
          parse_amf (this, text, remaining_bytes);
          free (text);
          return (this->status);
        }
        /* fall through */
      default:
        lprintf("  skipping packet...\n");
      continue;
    }

    /* send mpeg4 style headers in both normal and preview mode. This makes sure that
       they get through before they are needed. And it supports multiple sequences per
       stream (unless we seek too far). */
    if (mp4header) {
      buf = fifo->buffer_pool_alloc (fifo);
      buf->type = buf_type;
      buf->size = 0;
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
      buf->decoder_info[2] = remaining_bytes > buf->max_size ?
        buf->max_size : remaining_bytes;
      buf->decoder_info_ptr[2] = buf->mem;
      if ((this->input->read (this->input, (char *)buf->mem, buf->decoder_info[2]))
        != buf->decoder_info[2]) {
        buf->free_buffer (buf);
        goto fail;
      }
      remaining_bytes -= buf->decoder_info[2];
      if (remaining_bytes)
        this->input->seek (this->input, remaining_bytes, SEEK_CUR);
      remaining_bytes = 0;
      fifo->put (fifo, buf);
      break;
    }

    /* fkip frame contents in preview mode */
    if (preview) {
      if (remaining_bytes)
        this->input->seek (this->input, remaining_bytes, SEEK_CUR);
      return this->status;
    }

    /* send frame contents */
    buf_pts = (int64_t)pts * 90;
    check_newpts (this, buf_pts, (tag_type == FLV_TAG_TYPE_VIDEO));
    size = this->input->get_length (this->input);
    if (size > 0) {
      this->size = size;
      normpos = (int64_t)this->input->get_current_pos (this->input) * 65535 / size;
    }
    while (remaining_bytes) {
      buf       = fifo->buffer_pool_alloc (fifo);
      buf->type = buf_type;
      buf->pts  = buf_pts + ptsoffs;
      buf->extra_info->input_time = pts;
      if (size > 0)
        buf->extra_info->input_normpos = normpos;
      buf->size = remaining_bytes > buf->max_size ? buf->max_size : remaining_bytes;
      remaining_bytes -= buf->size;
      if (!remaining_bytes)
        buf_flags |= BUF_FLAG_FRAME_END;
      buf->decoder_flags = buf_flags;
      buf_flags &= ~BUF_FLAG_FRAME_START;
      if (this->input->read (this->input, (char *)buf->content, buf->size) != buf->size) {
        buf->free_buffer (buf);
        goto fail;
      }
      fifo->put(fifo, buf);
    }

    this->cur_pts = pts;
    break;
  }

  return this->status;
}

static void seek_flv_file (demux_flv_t *this, off_t seek_pos, int seek_pts) {
  int i;
  /* we start where we are */
  off_t pos1, pos2, size, used, found;
  unsigned char buf[4096], *p1, *p2;
  unsigned int now = 0, fpts = this->cur_pts, try;

  size = this->input->get_length (this->input);
  if (size > 0)
    this->size = size;
  found = this->input->get_current_pos (this->input) + 4;
  if (!seek_pos && this->length)
    pos2 = (uint64_t)size * seek_pts / this->length;
  else
    pos2 = (uint64_t)size * seek_pos / 65535;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "demux_flv: seek (%u.%03u, %"PRId64")\n",
    seek_pts / 1000, seek_pts % 1000, (int64_t)pos2);

   /* force send newpts */
  this->buf_flag_seek = 1;

  /* neither fileposition nor time given, restart at beginning */
  if (seek_pos == 0 && seek_pts == 0) {
    this->input->seek(this->input, this->start, SEEK_SET);
    this->cur_pts = 0;
    this->zero_pts_count = 0;
    return;
  }
 
  /* use file index for time based seek (if we got 1) */
  if (seek_pts && this->index) {
    flv_index_entry_t *x;
    uint32_t a = 0, b, c = this->num_indices;
    while (a + 1 < c) {
      b = (a + c) >> 1;
      if (this->index[b].pts <= seek_pts) a = b; else c = b;
    }
    x = &this->index[a];
    if ((x->offset >= this->start + 4) && (x->offset + 15 < size)) {
      this->input->seek (this->input, x->offset, SEEK_SET);
      this->input->read (this->input, (char *)buf, 15);
      if (!buf[8] && !buf[9] && !buf[10] && (
        ((buf[0] == FLV_TAG_TYPE_VIDEO) && ((buf[11] >> 4) == 1)) ||
        (buf[0] == FLV_TAG_TYPE_AUDIO)
        )) {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "demux_flv: seek_index (%u.%03u, %"PRId64")\n",
          x->pts / 1000, x->pts % 1000, (int64_t)x->offset);
        this->input->seek (this->input, x->offset - 4, SEEK_SET);
        this->cur_pts = x->pts;
        return;
      }
    }
    xprintf (this->xine, XINE_VERBOSITY_LOG, _("demux_flv: Not using broken seek index.\n"));
  }
  /* Up to 4 zero pts are OK (2 AAC/AVC sequence headers, 2 av tags).
     Otherwise, the file is non seekable. Try a size based seek. */
  if (this->zero_pts_count > 8) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      _("demux_flv: This file is non seekable. A/V lag may occur.\n"
        "           Recommend fixing the file with some flvtool.\n"));
    seek_pts = 0;
  }

  /* step 1: phonebook search. Estimate file position, find next tag header,
     check time, make better estimation, repeat */
  for (try = 4; try && (!seek_pts || abs ((int)seek_pts - (int)fpts) > 800); try--) {
    pos1 = found;
    found = 0;
    this->input->seek (this->input, pos2, SEEK_SET);
    used = this->input->read (this->input, (char *)buf + 4096 - 12, 12);
    for (i = 0; !found && (i < 50); i++) {
      memcpy (buf, buf + 4096 - 12, 12);
      used = this->input->read (this->input, (char *)buf + 12, 4096 - 12);
      if (used <= 0) break;
      p1 = buf;
      p2 = buf + used + 12;
      while (!found && (p1 + 11 < p2)) switch (*p1++) {
        case FLV_TAG_TYPE_AUDIO:
          if (p1[7] || p1[8] || p1[9] || ((p1[10] >> 4) != this->audiocodec)) continue;
          found = pos2 + (p1 - 1 - buf);
        break;
        case FLV_TAG_TYPE_VIDEO:
          if (p1[7] || p1[8] || p1[9] || ((p1[10] & 0x0f) != this->videocodec)) continue;
          found = pos2 + (p1 - 1 - buf);
        break;
      }
      pos2 += 4096 - 12;
    }
    if (found) {
      fpts = gettimestamp (p1, 3);
      if (seek_pts && fpts) pos2 = (uint64_t)found * seek_pts / fpts;
      else try = 1;
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "demux_flv: seek_quick (%u.%03u, %"PRId64")\n",
        fpts / 1000, fpts % 1000, (int64_t)found);
    } else found = pos1;
  }

  /* step 2: Traverse towards the desired time */
  if (seek_pts) {
    pos1 = 0;
    pos2 = found;
    i = 0;
    while (1) {
      if (pos2 < this->start + 4) break;
      this->input->seek (this->input, pos2 - 4, SEEK_SET);
      if (this->input->read (this->input, (char *)buf, 16) != 16) break;
      if ((buf[4] == FLV_TAG_TYPE_VIDEO) && ((buf[15] >> 4) == 1)) pos1 = pos2;
      if ((now = gettimestamp (buf, 8)) == 0) break;
      if (now >= seek_pts) {
        if (i > 0) break;
        if ((i = _X_BE_32 (buf)) == 0) break;
        found = pos2;
        pos2 -= i + 4;
        i = -1;
      } else {
        if (i < 0) break;
        pos2 += _X_BE_24 (&buf[5]) + 15;
        i = 1;
      }
    }
    if (pos1) found = pos1;
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
      "demux_flv: seek_traverse (%u.%03u, %"PRId64")\n",
      now / 1000, now % 1000, (int64_t)(pos2));
  }

  /* Go back to previous keyframe */
  if (this->videocodec) {
    pos1 = pos2 = found;
    found = 0;
    while (1) {
      if (pos2 < this->start + 4) break;
      this->input->seek (this->input, pos2 - 4, SEEK_SET);
      if (this->input->read (this->input, (char *)buf, 16) != 16) break;
      if ((buf[4] == FLV_TAG_TYPE_VIDEO) && ((buf[15] >> 4) == 1)) {found = pos2; break;}
      if ((i = _X_BE_32 (buf)) == 0) break;
      pos2 -= i + 4;
    }
    if (found) {
      now = gettimestamp (buf, 8);
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "demux_flv: seek_keyframe (%u.%03u, %"PRId64")\n",
        now / 1000, now % 1000, (int64_t)found);
    } else found = pos1;
  }

  /* we are there!! */
  this->input->seek (this->input, found + 4, SEEK_SET);
  this->input->read (this->input, (char *)buf, 4);
  this->cur_pts = gettimestamp (buf, 0);
  this->input->seek (this->input, found - 4, SEEK_SET);

  return;
}


static int demux_flv_send_chunk(demux_plugin_t *this_gen) {
  demux_flv_t *this = (demux_flv_t *) this_gen;

  return read_flv_packet(this, 0);
}

static void demux_flv_send_headers(demux_plugin_t *this_gen) {
  demux_flv_t *this = (demux_flv_t *) this_gen;
  int          i;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  this->buf_flag_seek = 1;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO,
                    (this->flags & FLV_FLAG_HAS_VIDEO) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
                    (this->flags & FLV_FLAG_HAS_AUDIO) ? 1 : 0);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* find first audio/video packets and send headers */
  for (i = 0; i < 20; i++) {
    if (read_flv_packet(this, 1) != DEMUX_OK)
      break;
    if (((!(this->flags & FLV_FLAG_HAS_VIDEO)) || this->got_video_header) &&
        ((!(this->flags & FLV_FLAG_HAS_AUDIO)) || this->got_audio_header)) {
      lprintf("  headers sent...\n");
      break;
    }
  }
}

static int demux_flv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_flv_t *this = (demux_flv_t *) this_gen;

  this->status = DEMUX_OK;

  /* if demux thread is not running, do some init stuff */
  if (!playing) {
    this->last_pts[0] = 0;
    this->last_pts[1] = 0;
    _x_demux_flush_engine(this->stream);
    seek_flv_file(this, start_pos, start_time);
    _x_demux_control_newpts (this->stream, 0, 0);
    return (this->status);
  }

  if (start_pos && !start_time)
    start_time = (int64_t) this->length * start_pos / 65535;

  /* always allow initial seek (this, 0, 0, 1) after send_headers ().
     It usually works at least due to xine input cache.
     Even if not, no problem there. */
  if ((!start_time && !start_pos) || INPUT_IS_SEEKABLE (this->input)) {
    if (!this->length || start_time < this->length) {
      _x_demux_flush_engine(this->stream);
      seek_flv_file(this, start_pos, start_time);
    }
  }

  return this->status;
}

static void demux_flv_dispose (demux_plugin_t *this_gen) {
  demux_flv_t *this = (demux_flv_t *) this_gen;

  if (this->index)
    free(this->index);
  free(this);
}

static int demux_flv_get_status (demux_plugin_t *this_gen) {
  demux_flv_t *this = (demux_flv_t *) this_gen;

  return this->status;
}

static int demux_flv_get_stream_length (demux_plugin_t *this_gen) {
  demux_flv_t *this = (demux_flv_t *) this_gen;

  return this->length;
}

static uint32_t demux_flv_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_AUDIOLANG;
}

static int demux_flv_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  demux_flv_t *this = (demux_flv_t *) this_gen;

  /* be a bit paranoid */
  if (this == NULL || this->stream == NULL)
    return DEMUX_OPTIONAL_UNSUPPORTED;

  switch (data_type) {
    case DEMUX_OPTIONAL_DATA_AUDIOLANG: {
      char *str   = data;
      int channel = *((int *)data);
      if (channel != 0) {
        strcpy (str, "none");
      } else {
        strcpy (str, "und");
        return DEMUX_OPTIONAL_SUCCESS;
      }
    }
    break;
    default: ;
  }
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {
  demux_flv_t *this;

  this         = calloc(1, sizeof (demux_flv_t));
  this->xine   = stream->xine;
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_flv_send_headers;
  this->demux_plugin.send_chunk        = demux_flv_send_chunk;
  this->demux_plugin.seek              = demux_flv_seek;
  this->demux_plugin.dispose           = demux_flv_dispose;
  this->demux_plugin.get_status        = demux_flv_get_status;
  this->demux_plugin.get_stream_length = demux_flv_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_flv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_flv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {
    case METHOD_BY_MRL:
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      if (!open_flv_file(this)) {
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

static void *init_plugin (xine_t *xine, void *data) {
  demux_flv_class_t     *this;

  this = calloc(1, sizeof (demux_flv_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Flash Video file demux plugin");
  this->demux_class.identifier      = "FLV";
  this->demux_class.mimetypes       = "video/x-flv: flv: Flash video;"
				      "video/flv: flv: Flash video;"
				      "application/x-flash-video: flv: Flash video;";
  this->demux_class.extensions      = "flv";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_flv = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "flashvideo", XINE_VERSION_CODE, &demux_info_flv, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

