/*
 * Copyright (C) 2004 the xine project
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
 *
 * For more information on the FLV file format, visit:
 * http://www.adobe.com/devnet/flv/pdf/video_file_format_spec_v9.pdf
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
#include "group_games.h"

typedef struct {
  unsigned int         pts;
  unsigned int         offset;
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

  unsigned char        got_video_header;
  unsigned char        got_audio_header;

  unsigned int         length; /* in ms */
  int                  width;
  int                  height;
  int                  duration;
  int                  videocodec;

  int                  samplerate;
  int                  samplesize;
  int                  stereo;
  int                  audiocodec;

  off_t                filesize;

  flv_index_entry_t   *index;
  unsigned int         num_indices;

  unsigned int         cur_pts;

  int64_t              last_pts[2];
  int                  send_newpts;
  int                  buf_flag_seek;
} demux_flv_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_flv_class_t;


#define FLV_FLAG_HAS_VIDEO       0x01
#define FLV_FLAG_HAS_AUDIO       0x04

#define FLV_TAG_TYPE_AUDIO       0x08
#define FLV_TAG_TYPE_VIDEO       0x09
#define FLV_TAG_TYPE_SCRIPT      0x12

#define FLV_SOUND_FORMAT_PCM_BE  0x00
#define FLV_SOUND_FORMAT_ADPCM   0x01
#define FLV_SOUND_FORMAT_MP3     0x02
#define FLV_SOUND_FORMAT_PCM_LE  0x03
#define FLV_SOUND_FORMAT_NELLY16 0x04 /* Nellymoser 16KHz */
#define FLV_SOUND_FORMAT_NELLY8  0x05 /* Nellymoser 8KHz */
#define FLV_SOUND_FORMAT_NELLY   0x06 /* Nellymoser */
#define FLV_SOUND_FORMAT_ALAW    0x07 /* G.711 A-LAW */
#define FLV_SOUND_FORMAT_MULAW   0x08 /* G.711 MU-LAW */
#define FLV_SOUND_FORMAT_AAC     0x0a
#define FLV_SOUND_FORMAT_MP38    0x0e /* MP3 8KHz */

#define FLV_VIDEO_FORMAT_FLV1    0x02 /* Sorenson H.263 */
#define FLV_VIDEO_FORMAT_SCREEN  0x03
#define FLV_VIDEO_FORMAT_VP6     0x04 /* On2 VP6 */
#define FLV_VIDEO_FORMAT_VP6A    0x05 /* On2 VP6 with alphachannel */
#define FLV_VIDEO_FORMAT_SCREEN2 0x06
#define FLV_VIDEO_FORMAT_H264    0x07

#define FLV_DATA_TYPE_NUMBER     0x00
#define FLV_DATA_TYPE_BOOL       0x01
#define FLV_DATA_TYPE_STRING     0x02
#define FLV_DATA_TYPE_OBJECT     0x03
#define FLC_DATA_TYPE_CLIP       0x04
#define FLV_DATA_TYPE_REFERENCE  0x07
#define FLV_DATA_TYPE_ECMARRAY   0x08
#define FLV_DATA_TYPE_ENDOBJECT  0x09
#define FLV_DATA_TYPE_ARRAY      0x0a
#define FLV_DATA_TYPE_DATE       0x0b
#define FLV_DATA_TYPE_LONGSTRING 0x0c


/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

#define WRAP_THRESHOLD           220000
#define PTS_AUDIO                0
#define PTS_VIDEO                1

static void check_newpts(demux_flv_t *this, int64_t pts, int video) {
  int64_t diff;

  diff = pts - this->last_pts[video];
  lprintf ("check_newpts %"PRId64"\n", pts);

  if (pts && (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD))) {
    lprintf ("diff=%"PRId64"\n", diff);

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if (pts)
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

static int parse_flv_var(demux_flv_t *this,
                         unsigned char *buf, int size, char *key, int keylen) {
  unsigned char *tmp = buf;
  unsigned char *end = buf + size;
  char          *str;
  unsigned char  type;
  unsigned int   len, num;

  if (size < 1)
    return 0;

  type = *tmp++;

  switch (type) {
    case FLV_DATA_TYPE_NUMBER:
      lprintf("  got number (%f)\n", BE_F64(tmp));
      if (key) {
        double val = BE_F64(tmp);
        if (keylen == 8 && !strncmp(key, "duration", 8)) {
          this->length = val * 1000.0;
        }
        else if (keylen == 5 && !strncmp(key, "width", 5)) {
          this->width = val;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width);
        }
        else if (keylen == 6 && !strncmp(key, "height", 6)) {
          this->height = val;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
        }
        else if (keylen == 9 && !strncmp(key, "framerate", 9)) {
          if (val > 0) {
            this->duration = 90000.0 / val;
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->duration);
          }
        }
        else if (keylen == 13 && !strncmp(key, "videodatarate", 13)) {
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_BITRATE, val*1000.0);
        }
        else if (keylen == 12 && !strncmp(key, "videocodecid", 12)) {
          this->videocodec = val;
        }
        else if (keylen == 15 && !strncmp(key, "audiosamplerate", 15)) {
          this->samplerate = val;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, this->samplerate);
        }
        else if (keylen == 15 && !strncmp(key, "audiosamplesize", 15)) {
          this->samplesize = val;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, this->samplesize);
        }
        else if (keylen == 5 && !strncmp(key, "stereo", 5)) {
          this->stereo = val;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, this->stereo ? 2 : 1);
        }
        else if (keylen == 13 && !strncmp(key, "audiodatarate", 13)) {
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, val*1000.0);
        }
        else if (keylen == 12 && !strncmp(key, "audiocodecid", 12)) {
          this->audiocodec = val;
        }
        else if (keylen == 8 && !strncmp(key, "filesize", 8)) {
          this->filesize = val;
        }
      }
      tmp += 8;
      break;
    case FLV_DATA_TYPE_BOOL:
      lprintf("  got bool (%d)\n", *tmp);
      tmp++;
      break;
    case FLV_DATA_TYPE_STRING:
      lprintf("  got string (%s)\n", tmp+2);
      len = _X_BE_16(tmp);
      tmp += len + 2;
      break;
    case FLV_DATA_TYPE_OBJECT:
      while ((len = _X_BE_16(tmp)) && tmp < end) {
        lprintf("  got object var (%s)\n", tmp+2);
        str = tmp + 2;
        tmp += len + 2;
        len = parse_flv_var(this, tmp, end-tmp, str, len);
        if (!len)
          return 0;
        tmp += len;
      }
      if (*tmp++ != FLV_DATA_TYPE_ENDOBJECT)
        return 0;
      break;
    case FLV_DATA_TYPE_ECMARRAY:
      lprintf("  got EMCA array (%d indices)\n", _X_BE_32(tmp));
      num = _X_BE_32(tmp);
      tmp += 4;
      while (num-- && tmp < end) {
        lprintf("  got array key (%s)\n", tmp+2);
        len = _X_BE_16(tmp);
        str = tmp + 2;
        tmp += len + 2;
        len = parse_flv_var(this, tmp, end-tmp, str, len);
        if (!len)
          return 0;
        tmp += len;
      }
      break;
    case FLV_DATA_TYPE_ARRAY:
      lprintf("  got array (%d indices)\n", _X_BE_32(tmp));
      num = _X_BE_32(tmp);
      tmp += 4;
      if (key && keylen == 5 && !strncmp(key, "times", 5)) {
        if (!this->index || this->num_indices != num) {
          if (this->index)
            free(this->index);
          this->index = calloc(num, sizeof(flv_index_entry_t));
          if (!this->index)
            return 0;
          this->num_indices = num;
        }
        for (num = 0; num < this->num_indices && tmp < end; num++) {
          if (*tmp++ == FLV_DATA_TYPE_NUMBER) {
            lprintf("  got number (%f)\n", BE_F64(tmp));
            this->index[num].pts = BE_F64(tmp) * 1000.0;
            tmp += 8;
          }
        }
        break;
      }
      if (key && keylen == 13 && !strncmp(key, "filepositions", 13)) {
        if (!this->index || this->num_indices != num) {
          if (this->index)
            free(this->index);
          this->index = calloc(num, sizeof(flv_index_entry_t));
          if (!this->index)
            return 0;
          this->num_indices = num;
        }
        for (num = 0; num < this->num_indices && tmp < end; num++) {
          if (*tmp++ == FLV_DATA_TYPE_NUMBER) {
            lprintf("  got number (%f)\n", BE_F64(tmp));
            this->index[num].offset = BE_F64(tmp);
            tmp += 8;
          }
        }
        break;
      }
      while (num-- && tmp < end) {
        len = parse_flv_var(this, tmp, end-tmp, NULL, 0);
        if (!len)
          return 0;
        tmp += len;
      }
      break;
    case FLV_DATA_TYPE_DATE:
      lprintf("  got date (%"PRId64", %d)\n", _X_BE_64(tmp), _X_BE_16(tmp+8));
      tmp += 10;
      break;
    default:
      lprintf("  got type %d\n", type);
      break;
  }

  return (tmp - buf);
}

static void parse_flv_script(demux_flv_t *this, int size) {
  unsigned char *buf = malloc(size);
  unsigned char *tmp = buf;
  unsigned char *end = buf + size;
  int            len;

  if (!buf || this->input->read(this->input, buf, size ) != size) {
    this->status = DEMUX_FINISHED;
    free(buf);
    return;
  }

  while (tmp < end) {
    len = parse_flv_var(this, tmp, end-tmp, NULL, 0);
    if (len < 1)
      break;
    tmp += len;
  }

  free(buf);
}

static int read_flv_packet(demux_flv_t *this, int preview) {
  fifo_buffer_t *fifo = NULL;
  buf_element_t *buf  = NULL;

  while (1) {
    unsigned char buffer[12], extrabuffer[4];
    unsigned char tag_type, avinfo;
    unsigned int  remaining_bytes;
    unsigned int  buf_type = 0;
    unsigned int  buf_flags = 0;
    unsigned int  pts;

    lprintf ("  reading FLV tag...\n");
    this->input->seek(this->input, 4, SEEK_CUR);
    if (this->input->read(this->input, buffer, 11) != 11) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    tag_type = buffer[0];
    remaining_bytes = _X_BE_24(&buffer[1]);
    pts = _X_BE_24(&buffer[4]) | (buffer[7] << 24);

    lprintf("  tag_type = 0x%02X, 0x%X bytes, pts %u\n",
            tag_type, remaining_bytes, pts/90);

    switch (tag_type) {
      case FLV_TAG_TYPE_AUDIO:
        lprintf("  got audio tag..\n");
        if (this->input->read(this->input, &avinfo, 1) != 1) {
          this->status = DEMUX_FINISHED;
          return this->status;
        }
        remaining_bytes--;

        this->audiocodec = avinfo >> 4; /* override */
        switch (this->audiocodec) {
          case FLV_SOUND_FORMAT_PCM_BE:
            buf_type = BUF_AUDIO_LPCM_BE;
            break;
          case FLV_SOUND_FORMAT_ADPCM:
            buf_type = BUF_AUDIO_FLVADPCM;
            break;
          case FLV_SOUND_FORMAT_MP3:
          case FLV_SOUND_FORMAT_MP38:
            buf_type = BUF_AUDIO_MPEG;
            break;
          case FLV_SOUND_FORMAT_PCM_LE:
            buf_type = BUF_AUDIO_LPCM_LE;
            break;
          case FLV_SOUND_FORMAT_ALAW:
            buf_type = BUF_AUDIO_ALAW;
            break;
          case FLV_SOUND_FORMAT_MULAW:
            buf_type = BUF_AUDIO_MULAW;
            break;
          case FLV_SOUND_FORMAT_AAC:
            buf_type = BUF_AUDIO_AAC;
            /* AAC extra header */
            this->input->read(this->input, extrabuffer, 1 );
            remaining_bytes--;
            break;
          default:
            lprintf("  unsupported audio format (%d)...\n", this->audiocodec);
            buf_type = BUF_AUDIO_UNKNOWN;
            break;
        }

        fifo = this->audio_fifo;
        if (preview && !this->got_audio_header) {
          /* send init info to audio decoder */
          buf = fifo->buffer_pool_alloc(fifo);
          buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
          buf->decoder_info[0] = 0;
          buf->decoder_info[1] = 44100 >> (3 - ((avinfo >> 2) & 3)); /* samplerate */
          buf->decoder_info[2] = (avinfo & 2) ? 16 : 8; /* bits per sample */
          buf->decoder_info[3] = (avinfo & 1) + 1; /* channels */
          buf->size = 0; /* no extra data */
          buf->type = buf_type;
          fifo->put(fifo, buf);
          this->got_audio_header = 1;
          if (!INPUT_IS_SEEKABLE(this->input)) {
             /* stop preview processing immediately, this enables libfaad to
              * initialize even without INPUT_CAP_SEEKABLE of input stream.
              */
             preview = 0;
          }
        }
        break;

      case FLV_TAG_TYPE_VIDEO:
        lprintf("  got video tag..\n");
        if (this->input->read(this->input, &avinfo, 1) != 1) {
          this->status = DEMUX_FINISHED;
          return this->status;
        }
        remaining_bytes--;

        switch ((avinfo >> 4)) {
          case 0x01:
            buf_flags = BUF_FLAG_KEYFRAME;
            break;
          case 0x05:
            /* skip server command */
            this->input->seek(this->input, remaining_bytes, SEEK_CUR);
            continue;
          default:
            break;
        }

        this->videocodec = avinfo & 0x0F; /* override */
        switch (this->videocodec) {
          case FLV_VIDEO_FORMAT_FLV1:
            buf_type = BUF_VIDEO_FLV1;
            break;
          case FLV_VIDEO_FORMAT_VP6:
            buf_type = BUF_VIDEO_VP6F;
            /* VP6 extra header */
            this->input->read(this->input, extrabuffer, 1 );
            remaining_bytes--;
            break;
          case FLV_VIDEO_FORMAT_VP6A:
            buf_type = BUF_VIDEO_VP6F;
            /* VP6A extra header */
            this->input->read(this->input, extrabuffer, 4);
            remaining_bytes -= 4;
            break;
          case FLV_VIDEO_FORMAT_H264:
            buf_type = BUF_VIDEO_H264;
            /* AVC extra header */
            this->input->read(this->input, extrabuffer, 4);
            remaining_bytes -= 4;
            break;
          default:
            lprintf("  unsupported video format (%d)...\n", this->videocodec);
            buf_type = BUF_VIDEO_UNKNOWN;
            break;
        }

        fifo = this->video_fifo;
        if (preview && !this->got_video_header) {
          xine_bmiheader *bih;
          /* send init info to video decoder; send the bitmapinfo header to the decoder
           * primarily as a formality since there is no real data inside */
          buf = fifo->buffer_pool_alloc(fifo);
          buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER |
                               BUF_FLAG_FRAMERATE | BUF_FLAG_FRAME_END;
          buf->decoder_info[0] = this->duration;
          bih = (xine_bmiheader *) buf->content;
          memset(bih, 0, sizeof(xine_bmiheader));
          bih->biSize = sizeof(xine_bmiheader);
          bih->biWidth = this->width;
          bih->biHeight = this->height;
          buf->size = sizeof(xine_bmiheader);
          buf->type = buf_type;
          if (buf_type == BUF_VIDEO_VP6F) {
            *((unsigned char *)buf->content+buf->size) = extrabuffer[0];
            bih->biSize++;
            buf->size++;
          }
          else if (buf_type == BUF_VIDEO_H264 && extrabuffer[0] == 0) {
            /* AVC sequence header */
            if (remaining_bytes > buf->max_size-buf->size) {
              xprintf(this->xine, XINE_VERBOSITY_LOG,
                    _("sequence header too big (%u bytes)!\n"), remaining_bytes);
              this->input->read(this->input, buf->content+buf->size, buf->max_size-buf->size);
              this->input->seek(this->input, remaining_bytes-buf->max_size-buf->size, SEEK_CUR);
              bih->biSize = buf->max_size;
              buf->size = buf->max_size;
            }
            else {
              this->input->read(this->input, buf->content+buf->size, remaining_bytes);
              bih->biSize += remaining_bytes;
              buf->size += remaining_bytes;
            }
            remaining_bytes = 0;
          }
          fifo->put(fifo, buf);
          this->got_video_header = 1;
        }
        break;

      case FLV_TAG_TYPE_SCRIPT:
        lprintf("  got script tag...\n");
        if (preview) {
          parse_flv_script(this, remaining_bytes);

          /* send init info to decoders using script information as reference */
          if (!this->got_audio_header && this->audiocodec) {
            buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
            buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
            buf->decoder_info[0] = 0;
            buf->decoder_info[1] = this->samplerate;
            buf->decoder_info[2] = this->samplesize;
            buf->decoder_info[3] = this->stereo ? 2 : 1;
            switch (this->audiocodec) {
              case FLV_SOUND_FORMAT_PCM_BE:
                buf->type = BUF_AUDIO_LPCM_BE;
                break;
              case FLV_SOUND_FORMAT_ADPCM:
                buf->type = BUF_AUDIO_FLVADPCM;
                break;
              case FLV_SOUND_FORMAT_MP3:
              case FLV_SOUND_FORMAT_MP38:
                buf->type = BUF_AUDIO_MPEG;
                break;
              case FLV_SOUND_FORMAT_PCM_LE:
                buf->type = BUF_AUDIO_LPCM_LE;
                break;
              case FLV_SOUND_FORMAT_ALAW:
                buf->type = BUF_AUDIO_ALAW;
                break;
              case FLV_SOUND_FORMAT_MULAW:
                buf->type = BUF_AUDIO_MULAW;
                break;
              case FLV_SOUND_FORMAT_AAC:
                buf->type = BUF_AUDIO_AAC;
                break;
              default:
                buf->type = BUF_AUDIO_UNKNOWN;
                break;
            }
            buf->size = 0;
            this->audio_fifo->put(this->audio_fifo, buf);
            this->got_audio_header = 1;
            lprintf("  got audio header from metadata...\n");
          }

          if (!this->got_video_header && this->videocodec && this->videocodec != FLV_VIDEO_FORMAT_H264) {
            xine_bmiheader *bih;
            buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
            buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER |
                                 BUF_FLAG_FRAMERATE | BUF_FLAG_FRAME_END;
            buf->decoder_info[0] = this->duration;
            switch (this->videocodec) {
              case FLV_VIDEO_FORMAT_FLV1:
                buf->type = BUF_VIDEO_FLV1;
                break;
              case FLV_VIDEO_FORMAT_VP6:
              case FLV_VIDEO_FORMAT_VP6A:
                buf->type = BUF_VIDEO_VP6F;
                break;
              default:
                buf->type = BUF_VIDEO_UNKNOWN;
                break;
            }
            buf->size = sizeof(xine_bmiheader);
            bih = (xine_bmiheader *) buf->content;
            memset(bih, 0, sizeof(xine_bmiheader));
            bih->biSize = sizeof(xine_bmiheader);
            bih->biWidth = this->width;
            bih->biHeight = this->height;
            if (buf->type == BUF_VIDEO_VP6F) {
              *((uint8_t *)buf->content+buf->size) = ((16-(this->width&15)) << 4) |
                                                     ((16-(this->height&15)) & 0xf);
              bih->biSize++;
              buf->size++;
            }
            this->video_fifo->put(this->video_fifo, buf);
            this->got_video_header = 1;
            lprintf("  got video header from metadata...\n");
          }

          return this->status;
        }
        /* no preview */
        this->input->seek(this->input, remaining_bytes, SEEK_CUR);
        continue;

      default:
        lprintf("  skipping packet...\n");
        this->input->seek(this->input, remaining_bytes, SEEK_CUR);
        continue;
    }

    while (remaining_bytes) {
      buf = fifo->buffer_pool_alloc(fifo);
      buf->type = buf_type;

      buf->extra_info->input_time = pts;
      if (this->input->get_length(this->input)) {
        buf->extra_info->input_normpos =
            (int)((double)this->input->get_current_pos(this->input) * 65535.0 / this->size);
      }

      if ((buf_type == BUF_VIDEO_H264 || buf_type == BUF_AUDIO_AAC) && extrabuffer[0] == 0) {
        /* AVC/AAC sequence header */
        buf->pts = 0;
        buf->size = 0;

        buf->decoder_flags = BUF_FLAG_SPECIAL | BUF_FLAG_HEADER;
        if (preview)
          buf->decoder_flags |= BUF_FLAG_PREVIEW;

        buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
        buf->decoder_info[2] = MIN(remaining_bytes, buf->max_size);
        buf->decoder_info_ptr[2] = buf->mem;

        if (this->input->read(this->input, buf->mem, buf->decoder_info[2]) != buf->decoder_info[2]) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        if (remaining_bytes > buf->max_size) {
          xprintf(this->xine, XINE_VERBOSITY_LOG,
                _("sequence header too big (%u bytes)!\n"), remaining_bytes);
          this->input->seek(this->input, remaining_bytes-buf->max_size, SEEK_CUR);
        }
        remaining_bytes = 0;
      }
      else {
        buf->pts = (int64_t) pts * 90;
        if (!preview)
          check_newpts(this, buf->pts, (tag_type == FLV_TAG_TYPE_VIDEO));

        if (remaining_bytes > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = remaining_bytes;
        remaining_bytes -= buf->size;

        buf->decoder_flags = buf_flags;
        if (preview)
          buf->decoder_flags |= BUF_FLAG_PREVIEW;
        if (!remaining_bytes)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;

        if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }
      }

      fifo->put(fifo, buf);
    }

    this->cur_pts = pts;
    break;
  }

  return this->status;
}

static void seek_flv_file(demux_flv_t *this, off_t seek_pos, int seek_pts) {
  unsigned char buffer[16];
  unsigned int  pts = this->cur_pts;
  int           len = 0;
  int           next_tag = 0;
  int           do_rewind = (seek_pts < this->cur_pts);
  int           i;

  lprintf("  seeking %s to %d...\n",
          do_rewind ? "backward" : "forward", seek_pts);

  if (seek_pos == 0 && seek_pts == 0) {
    this->input->seek(this->input, this->start, SEEK_SET);
    this->cur_pts = 0;
    return;
  }

  if (this->index) {
    if (do_rewind) {
      for (i = this->num_indices-1; i > 0; i--) {
        if (this->index[i-1].pts < seek_pts)
          break;
      }
    }
    else {
      for (i = 0; i < (this->num_indices-1); i++) {
        if (this->index[i+1].pts > seek_pts)
          break;
      }
    }

    if (this->index[i].offset >= this->start+4) {
      lprintf("  seeking to index entry %d (pts:%u, offset:%u).\n",
              i, this->index[i].pts, this->index[i].offset);

      this->input->seek(this->input, this->index[i].offset-4, SEEK_SET);
      this->cur_pts = this->index[i].pts;
    }
  }
  else if (seek_pos && this->videocodec && abs(seek_pts-this->cur_pts) > 300000) {
    off_t pos, size;

    pos = this->input->get_current_pos(this->input);
    size = this->filesize ? : this->input->get_length(this->input);
    this->input->seek(this->input, (uint64_t)size * seek_pos / 65535, SEEK_SET);
    lprintf("  resyncing...\n");

    /* resync */
    for (i = 0; i < 200000; i++) {
      uint8_t buf[4];

      if (this->input->read(this->input, buf, 1) < 1) {
        this->status = DEMUX_FINISHED;
        return;
      }
      if (buf[0] == FLV_TAG_TYPE_VIDEO) {
        this->input->seek(this->input, 7, SEEK_CUR);
        if (this->input->read(this->input, buf, 4) < 4) {
          this->status = DEMUX_FINISHED;
          return;
        }
        /* check StreamID and CodecID */
        if ( _X_ME_32(buf) == ME_FOURCC(0, 0, 0, (this->videocodec | 0x10)) ) {
          this->input->seek(this->input, -16, SEEK_CUR);
          lprintf("  ...resynced after %d bytes\n", i);
          return;
        }
        this->input->seek(this->input, -11, SEEK_CUR);
      }
    }

    lprintf("  ...resync failed!\n");
    this->input->seek(this->input, pos, SEEK_SET);
  }
  else if (seek_pts) {
    while (do_rewind ? (seek_pts < this->cur_pts) : (seek_pts > this->cur_pts)) {
      unsigned char tag_type;
      int           data_size;
      int           ptag_size;

      if (next_tag)
        this->input->seek(this->input, next_tag, SEEK_CUR);

      len = this->input->read(this->input, buffer, 16);
      if (len != 16) {
        len = (len < 0) ? 0 : len;
        break;
      }

      ptag_size = _X_BE_32(&buffer[0]);
      tag_type = buffer[4];
      data_size = _X_BE_24(&buffer[5]);
      pts = _X_BE_24(&buffer[8]) | (buffer[11] << 24);

      if (do_rewind) {
        if (!ptag_size)
          break; /* beginning of movie */
        next_tag = -(ptag_size + 16 + 4);
      }
      else {
        next_tag = data_size - 1;
      }

      if (this->flags & FLV_FLAG_HAS_VIDEO) {
        /* sync to video key frame */
        if (tag_type != FLV_TAG_TYPE_VIDEO || (buffer[15] >> 4) != 0x01)
          continue;
        lprintf("  video keyframe found at %d...\n", pts);
      }
      this->cur_pts = pts;
    }

    /* seek back to the beginning of the tag */
    this->input->seek(this->input, -len, SEEK_CUR);

    lprintf( "  seeked to %d.\n", pts);
  }
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
    if (((this->flags & FLV_FLAG_HAS_VIDEO) && this->got_video_header) &&
        ((this->flags & FLV_FLAG_HAS_AUDIO) && this->got_audio_header)) {
      lprintf("  headers sent...\n");
      break;
    }
  }
}

static int demux_flv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_flv_t *this = (demux_flv_t *) this_gen;

  this->status = DEMUX_OK;

  if (INPUT_IS_SEEKABLE(this->input)) {
    if (start_pos && !start_time) {
      if (this->length)
        start_time = (int64_t) this->length * start_pos / 65535;
      else if (this->index)
        start_time = this->index[(int)(start_pos * (this->num_indices-1) / 65535)].pts;
    }

    if (!this->length || start_time < this->length) {
      seek_flv_file(this, start_pos, start_time);

      if (playing) {
        this->buf_flag_seek = 1;
        _x_demux_flush_engine(this->stream);
      }
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
  return DEMUX_CAP_NOCAP;
}

static int demux_flv_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {
  demux_flv_t *this;

  this         = calloc(1, sizeof(demux_flv_t));
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

  this = calloc(1, sizeof(demux_flv_class_t));

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
