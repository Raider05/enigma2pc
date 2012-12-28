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
 *
 * demultiplexer for matroska streams
 *
 * TODO:
 *   more decoders init
 *   metadata
 *   non seekable input plugins support
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

#define LOG_MODULE "demux_matroska"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"

#include "ebml.h"
#include "matroska.h"
#include "demux_matroska.h"

static void check_newpts (demux_matroska_t *this, int64_t pts,
                          matroska_track_t *track) {
  int64_t diff;

  if ((track->track_type == MATROSKA_TRACK_VIDEO) ||
      (track->track_type == MATROSKA_TRACK_AUDIO)) {

    diff = pts - track->last_pts;

    if (pts && (this->send_newpts || (track->last_pts && abs(diff)>WRAP_THRESHOLD)) ) {
      int i;

      lprintf ("sending newpts %" PRId64 ", diff %" PRId64 ", track %d\n", pts, diff, track->track_num);

      if (this->buf_flag_seek) {
        _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
        this->buf_flag_seek = 0;
      } else {
        _x_demux_control_newpts(this->stream, pts, 0);
      }

      this->send_newpts = 0;
      for (i = 0; i < this->num_tracks; i++) {
        this->tracks[i]->last_pts = 0;
      }
    } else {
  #ifdef LOG
      if (pts)
        lprintf ("diff %" PRId64 ", track %d\n", diff, track->track_num);
  #endif
    }

    if (pts)
      track->last_pts = pts;
  }
}

/* Add an entry to the top_level element list */
static int add_top_level_entry (demux_matroska_t *this, off_t pos) {
  if (this->top_level_list_size == this->top_level_list_max_size) {
    this->top_level_list_max_size += 50;
    lprintf("top_level_list_max_size: %d\n", this->top_level_list_max_size);
    this->top_level_list = realloc(this->top_level_list,
                                   this->top_level_list_max_size * sizeof(off_t));
    if (this->top_level_list == NULL)
      return 0;
  }
  this->top_level_list[this->top_level_list_size] = pos;
  this->top_level_list_size++;
  return 1;
}

/* Find an entry in the top_level elem list
 * return
 *   0: not found
 *   1: found
 */
static int find_top_level_entry (demux_matroska_t *this, off_t pos) {
  int i;

  for (i = 0; i < this->top_level_list_size; i++) {
    if (this->top_level_list[i] == pos)
      return 1;
  }
  return 0;
}


static int parse_info(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  double duration = 0.0; /* in matroska unit */

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_I_TIMECODESCALE:
        lprintf("timecode_scale\n");
        if (!ebml_read_uint(ebml, &elem, &this->timecode_scale))
          return 0;
        break;

      case MATROSKA_ID_I_DURATION:
        lprintf("duration\n");
        if (!ebml_read_float(ebml, &elem, &duration))
          return 0;
        break;

      case MATROSKA_ID_I_TITLE:
        lprintf("title\n");
        if (NULL != this->title)
          free(this->title);

        this->title = ebml_alloc_read_ascii(ebml, &elem);
        _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TITLE, this->title);
        break;

      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  if (this->timecode_scale == 0) {
    this->timecode_scale = 1000000;
  }
  this->duration = (int)(duration * (double)this->timecode_scale / 1000000.0);
  lprintf("timecode_scale: %" PRId64 "\n", this->timecode_scale);
  lprintf("duration: %d\n", this->duration);
  lprintf("title: %s\n", (NULL != this->title ? this->title : "(none)"));

  return 1;
}


static int parse_video_track (demux_matroska_t *this, matroska_video_track_t *vt) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;
  uint64_t val;

  while (next_level == 4) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TV_FLAGINTERLACED:
        lprintf("MATROSKA_ID_TV_FLAGINTERLACED\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->flag_interlaced = val;
        break;
      case MATROSKA_ID_TV_PIXELWIDTH:
        lprintf("MATROSKA_ID_TV_PIXELWIDTH\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->pixel_width = val;
        break;
      case MATROSKA_ID_TV_PIXELHEIGHT:
        lprintf("MATROSKA_ID_TV_PIXELHEIGHT\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->pixel_height = val;
        break;
      case MATROSKA_ID_TV_VIDEODISPLAYWIDTH:
        lprintf("MATROSKA_ID_TV_VIDEODISPLAYWIDTH\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->display_width = val;
        break;
      case MATROSKA_ID_TV_VIDEODISPLAYHEIGHT:
        lprintf("MATROSKA_ID_TV_VIDEODISPLAYHEIGHT\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->display_height = val;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_audio_track (demux_matroska_t *this, matroska_audio_track_t *at) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;

  while (next_level == 4) {
    ebml_elem_t elem;
    uint64_t    val;
    double      fval;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TA_SAMPLINGFREQUENCY:
        lprintf("MATROSKA_ID_TA_SAMPLINGFREQUENCY\n");
        if (!ebml_read_float(ebml, &elem, &fval))
          return 0;
        at->sampling_freq = (int)fval;
        break;
      case MATROSKA_ID_TA_OUTPUTSAMPLINGFREQUENCY:
        lprintf("MATROSKA_ID_TA_OUTPUTSAMPLINGFREQUENCY\n");
        if (!ebml_read_float(ebml, &elem, &fval))
          return 0;
        at->output_sampling_freq = (int)fval;
        break;
      case MATROSKA_ID_TA_CHANNELS:
        lprintf("MATROSKA_ID_TA_CHANNELS\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        at->channels = val;
        break;
      case MATROSKA_ID_TA_BITDEPTH:
        lprintf("MATROSKA_ID_TA_BITDEPTH\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        at->bits_per_sample = val;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_content_compression (demux_matroska_t *this, matroska_track_t *track) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 6;

  while (next_level == 6) {
    ebml_elem_t elem;
    uint64_t    val;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CE_COMPALGO:
        lprintf("ContentCompAlgo\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        switch (val)
        {
          case MATROSKA_COMPRESS_ZLIB:
          case MATROSKA_COMPRESS_BZLIB:
          case MATROSKA_COMPRESS_LZO1X:
          case MATROSKA_COMPRESS_HEADER_STRIP:
            track->compress_algo = val;
            break;
          default:
            track->compress_algo = MATROSKA_COMPRESS_UNKNOWN;
            break;
        }
        break;
      case MATROSKA_ID_CE_COMPSETTINGS:
        lprintf("ContentCompSettings\n");
        track->compress_settings = calloc(1, elem.len);
        track->compress_len = elem.len;
        if (elem.len > this->compress_maxlen)
		this->compress_maxlen = elem.len;
        if(!ebml_read_binary(ebml, &elem, track->compress_settings))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_content_encoding (demux_matroska_t *this, matroska_track_t *track) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 5;

  while (next_level == 5) {
    ebml_elem_t elem;
    uint64_t    val;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CE_ORDER:
        lprintf("ContentEncodingOrder\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        if (val != 0) {  // multiple content encoding isn't supported
          lprintf("   warning: a non-zero encoding order is UNSUPPORTED\n");
          return 0;
        }
        break;
      case MATROSKA_ID_CE_SCOPE:
        lprintf("ContentEncodingScope\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        if (val != 1) {  // 1 (all frame contents) is the only supported option
          lprintf("   warning: UNSUPPORTED encoding scope (%" PRId64 ")\n", val);
          return 0;
        }
        break;
      case MATROSKA_ID_CE_TYPE:
        lprintf("ContentEncodingType\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        if (val != 0)  // only compression (0) is supported
          return 0;
        break;
      case MATROSKA_ID_CE_COMPRESSION:
        lprintf("ContentCompression\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_content_compression(this, track))
          return 0;
        break;
      case MATROSKA_ID_CE_ENCRYPTION:
        lprintf("ContentEncryption (UNSUPPORTED)\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_content_encodings (demux_matroska_t *this, matroska_track_t *track) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;

  while (next_level == 4) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CONTENTENCODING:
        lprintf("ContentEncoding\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_content_encoding(this, track))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static void init_codec_video(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  if (track->codec_private_len > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: private decoder data length (%d) is greater than fifo buffer length (%" PRId32 ")\n",
             track->codec_private_len, buf->max_size);
    buf->free_buffer(buf);
    return;
  }
  buf->size          = track->codec_private_len;
  buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
  buf->type          = track->buf_type;
  buf->pts           = 0;

  if (buf->size)
    xine_fast_memcpy (buf->content, track->codec_private, buf->size);
  else
    buf->content = NULL;

  if(track->default_duration) {
    buf->decoder_flags   |= BUF_FLAG_FRAMERATE;
    buf->decoder_info[0]  = (int64_t)track->default_duration *
                            (int64_t)90 / (int64_t)1000000;
  }

  if(track->video_track && track->video_track->display_width &&
     track->video_track->display_height) {
    buf->decoder_flags   |= BUF_FLAG_ASPECT;
    buf->decoder_info[1]  = track->video_track->display_width;
    buf->decoder_info[2]  = track->video_track->display_height;
  }

  track->fifo->put (track->fifo, buf);
}


static void init_codec_audio(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  if (track->codec_private_len > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: private decoder data length (%d) is greater than fifo buffer length (%" PRId32 ")\n",
             track->codec_private_len, buf->max_size);
    buf->free_buffer(buf);
    return;
  }
  buf->size = track->codec_private_len;

  /* default param */
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = 44100;
  buf->decoder_info[2] = 16;
  buf->decoder_info[3] = 2;
  /* track param */
  if (track->audio_track) {
    if (track->audio_track->sampling_freq)
      buf->decoder_info[1] = track->audio_track->sampling_freq;
    if (track->audio_track->bits_per_sample)
      buf->decoder_info[2] = track->audio_track->bits_per_sample;
    if (track->audio_track->channels)
      buf->decoder_info[3] = track->audio_track->channels;
  }
  lprintf("%d Hz, %d bits, %d channels\n", buf->decoder_info[1],
          buf->decoder_info[2], buf->decoder_info[3]);

  if (buf->size)
    xine_fast_memcpy (buf->content, track->codec_private, buf->size);
  else
    buf->content = NULL;

  buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
  buf->type          = track->buf_type;
  buf->pts           = 0;
  track->fifo->put (track->fifo, buf);
}


static void init_codec_real(demux_matroska_t *this, matroska_track_t * track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  if (track->codec_private_len > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: private decoder data length (%d) is greater than fifo buffer length (%" PRId32 ")\n",
             track->codec_private_len, buf->max_size);
    buf->free_buffer(buf);
    return;
  }

  buf->size          = track->codec_private_len;
  buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_FRAME_END;
  buf->type          = track->buf_type;
  buf->pts           = 0;

  if (buf->size)
    xine_fast_memcpy (buf->content, track->codec_private, buf->size);
  else
    buf->content = NULL;

  if(track->default_duration) {
    buf->decoder_flags   |= BUF_FLAG_FRAMERATE;
    buf->decoder_info[0]  = (int64_t)track->default_duration *
                            (int64_t)90 / (int64_t)1000000;
  }

  if(track->video_track && track->video_track->display_width &&
     track->video_track->display_height) {
    buf->decoder_flags   |= BUF_FLAG_ASPECT;
    buf->decoder_info[1]  = track->video_track->display_width;
    buf->decoder_info[2]  = track->video_track->display_height;
  }

  track->fifo->put (track->fifo, buf);
}

static void init_codec_xiph(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;
  uint8_t nb_lace;
  int frame[3];
  int i;
  uint8_t *data;

  if (track->codec_private_len < 3)
    return;
  nb_lace = track->codec_private[0];
  if (nb_lace != 2)
    return;

  frame[0] = track->codec_private[1];
  frame[1] = track->codec_private[2];
  frame[2] = track->codec_private_len - frame[0] - frame[1] - 3;
  if (frame[2] < 0)
    return;

  data = track->codec_private + 3;
  for (i = 0; i < 3; i++) {
    buf = track->fifo->buffer_pool_alloc (track->fifo);

    if (frame[i] > buf->max_size) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "demux_matroska: private decoder data length (%d) is greater than fifo buffer length (%" PRId32 ")\n",
              frame[i], buf->max_size);
      buf->free_buffer(buf);
      return;
    }
    buf->size = frame[i];

    buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_FRAME_START | BUF_FLAG_FRAME_END;
    buf->type          = track->buf_type;
    buf->pts           = 0;

    xine_fast_memcpy (buf->content, data, buf->size);
    data += buf->size;

    track->fifo->put (track->fifo, buf);
  }
}


static int aac_get_sr_index (uint32_t sample_rate) {
  if (92017 <= sample_rate)
    return 0;
  else if (75132 <= sample_rate)
    return 1;
  else if (55426 <= sample_rate)
    return 2;
  else if (46009 <= sample_rate)
    return 3;
  else if (37566 <= sample_rate)
    return 4;
  else if (27713 <= sample_rate)
    return 5;
  else if (23004 <= sample_rate)
    return 6;
  else if (18783 <= sample_rate)
    return 7;
  else if (13856 <= sample_rate)
    return 8;
  else if (11502 <= sample_rate)
    return 9;
  else if (9391 <= sample_rate)
    return 10;
  else
    return 11;
}

#define AAC_SYNC_EXTENSION_TYPE 0x02b7
static void init_codec_aac(demux_matroska_t *this, matroska_track_t *track) {
  matroska_audio_track_t *atrack = track->audio_track;
  buf_element_t *buf;
  int profile;
  int sr_index;

  /* Create a DecoderSpecificInfo for initialising libfaad */
  sr_index = aac_get_sr_index(atrack->sampling_freq);
  /* newer specification with appended CodecPrivate */
  if (strlen(track->codec_id) <= 12)
    profile = 3;
  /* older specification */
  else if (!strncmp (&track->codec_id[12], "MAIN", 4))
    profile = 0;
  else if (!strncmp (&track->codec_id[12], "LC", 2))
    profile = 1;
  else if (!strncmp (&track->codec_id[12], "SSR", 3))
    profile = 2;
  else
    profile = 3;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  buf->size = 0;
  buf->type = track->buf_type;
  buf->pts = 0;

  buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
  buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
  buf->decoder_info_ptr[2] = buf->mem;

  buf->mem[0] = ((profile + 1) << 3) | ((sr_index & 0x0e) >> 1);
  buf->mem[1] = ((sr_index & 0x01) << 7) | (atrack->channels << 3);

  if (strstr(track->codec_id, "SBR") == NULL)
    buf->decoder_info[2] = 2;
  else {
    /* HE-AAC (aka SBR AAC) */
    sr_index = aac_get_sr_index(atrack->sampling_freq*2);
    buf->mem[2] = AAC_SYNC_EXTENSION_TYPE >> 3;
    buf->mem[3] = ((AAC_SYNC_EXTENSION_TYPE & 0x07) << 5) | 5;
    buf->mem[4] = (1 << 7) | (sr_index << 3);

    buf->decoder_info[2] = 5;
  }

  track->fifo->put(track->fifo, buf);
}


static int vobsub_parse_size(matroska_track_t *t, const char *start) {
  if (sscanf(&start[6], "%dx%d", &t->sub_track->width,
             &t->sub_track->height) == 2) {
    lprintf("VobSub size: %ux%u\n", t->sub_track->width, t->sub_track->height);
    return 1;
  }
  return 0;
}

static int vobsub_parse_palette(matroska_track_t *t, const char *start) {
  int i, r, g, b, y, u, v, tmp;

  start += 8;
  while (isspace(*start))
    start++;
  for (i = 0; i < 16; i++) {
    if (sscanf(start, "%06x", &tmp) != 1)
      break;
    r = tmp >> 16 & 0xff;
    g = tmp >> 8 & 0xff;
    b = tmp & 0xff;
    y = MIN(MAX((int)(0.1494 * r + 0.6061 * g + 0.2445 * b), 0), 0xff);
    u = MIN(MAX((int)(0.6066 * r - 0.4322 * g - 0.1744 * b) + 128, 0), 0xff);
    v = MIN(MAX((int)(-0.08435 * r - 0.3422 * g + 0.4266 * b) + 128, 0), 0xff);
    t->sub_track->palette[i] = y << 16 | u << 8 | v;
    start += 6;
    while ((*start == ',') || isspace(*start))
      start++;
  }
  if (i == 16) {
    lprintf("VobSub palette: %06x,%06x,%06x,%06x,%06x,%06x,%06x,%06x,%06x,"
            "%06x,%06x,%06x,%06x,%06x,%06x,%06x\n", t->sub_track->palette[0],
            t->sub_track->palette[1], t->sub_track->palette[2],
            t->sub_track->palette[3], t->sub_track->palette[4],
            t->sub_track->palette[5], t->sub_track->palette[6],
            t->sub_track->palette[7], t->sub_track->palette[8],
            t->sub_track->palette[9], t->sub_track->palette[10],
            t->sub_track->palette[11], t->sub_track->palette[12],
            t->sub_track->palette[13], t->sub_track->palette[14],
            t->sub_track->palette[15]);
    return 2;
  }
  return 0;
}

static int vobsub_parse_custom_colors(matroska_track_t *t, const char *start) {
  int use_custom_colors, i;

  start += 14;
  while (isspace(*start))
    start++;
  use_custom_colors = 0;
  if (!strncasecmp(start, "ON", 2) || (*start == '1'))
    use_custom_colors = 1;
  else if (!strncasecmp(start, "OFF", 3) || (*start == '0'))
    use_custom_colors = 0;
  lprintf("VobSub custom colours: %s\n", use_custom_colors ? "ON" : "OFF");
  if ((start = strstr(start, "colors:")) != NULL) {
    start += 7;
    while (isspace(*start))
      start++;
    for (i = 0; i < 4; i++) {
      if (sscanf(start, "%06x", &t->sub_track->colors[i]) != 1)
        break;
      start += 6;
      while ((*start == ',') || isspace(*start))
        start++;
    }
    if (i == 4) {
      t->sub_track->custom_colors = 4;
      lprintf("VobSub colours: %06x,%06x,%06x,%06x\n", t->sub_track->colors[0],
              t->sub_track->colors[1], t->sub_track->colors[2],
              t->sub_track->colors[3]);
    }
  }
  if (!use_custom_colors)
    t->sub_track->custom_colors = 0;
  return 4;
}

static int vobsub_parse_forced_subs(matroska_track_t *t, const char *start) {
  start += 12;
  while (isspace(*start))
    start++;
  if (!strncasecmp(start, "on", 2) || (*start == '1'))
    t->sub_track->forced_subs_only = 1;
  else if (!strncasecmp(start, "off", 3) || (*start == '0'))
    t->sub_track->forced_subs_only = 0;
  else
    return 0;
  lprintf("VobSub forced subs: %d\n", t->sub_track->forced_subs_only);
  return 8;
}

static void init_codec_vobsub(demux_matroska_t *this,
                              matroska_track_t *track) {
  int things_found, last;
  char *buf, *pos, *start;
  buf_element_t *buf_el;

  lprintf("init_codec_vobsub for %d\n", track->track_num);

  if ((track->codec_private == NULL) || (track->codec_private_len == 0))
    return;

  track->sub_track = calloc(1, sizeof(matroska_sub_track_t));
  if (track->sub_track == NULL)
    return;
  things_found = 0;
  buf = (char *)malloc(track->codec_private_len + 1);
  if (buf == NULL)
    return;
  xine_fast_memcpy(buf, track->codec_private, track->codec_private_len);
  buf[track->codec_private_len] = 0;
  track->sub_track->type = 'v';

  pos = buf;
  start = buf;
  last = 0;

  do {
    if ((*pos == 0) || (*pos == '\r') || (*pos == '\n')) {
      if (*pos == 0)
        last = 1;
      *pos = 0;

      if (!strncasecmp(start, "size: ", 6))
        things_found |= vobsub_parse_size(track, start);
      else if (!strncasecmp(start, "palette:", 8))
        things_found |= vobsub_parse_palette(track, start);
      else if (!strncasecmp(start, "custom colours:", 14))
        things_found |= vobsub_parse_custom_colors(track, start);
      else if (!strncasecmp(start, "forced subs:", 12))
        things_found |= vobsub_parse_forced_subs(track, start);

      if (last)
        break;
      do {
        pos++;
      } while ((*pos == '\r') || (*pos == '\n'));
      start = pos;
    } else
      pos++;
  } while (!last && (*start != 0));

  free(buf);

  if ((things_found & 2) == 2) {
    buf_el = track->fifo->buffer_pool_alloc(track->fifo);
    xine_fast_memcpy(buf_el->content, track->sub_track->palette,
                     16 * sizeof(uint32_t));
    buf_el->type = BUF_SPU_DVD;
    buf_el->decoder_flags |= BUF_FLAG_SPECIAL;
    buf_el->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
    buf_el->decoder_info[2] = SPU_DVD_SUBTYPE_CLUT;
    track->fifo->put(track->fifo, buf_el);
  }
}

static void init_codec_spu(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  buf->size = 0;
  buf->type = track->buf_type;

  track->fifo->put (track->fifo, buf);
}

static void handle_realvideo (demux_plugin_t *this_gen, matroska_track_t *track,
                              int decoder_flags,
                              uint8_t *data, size_t data_len,
                              int64_t data_pts, int data_duration,
                              int input_normpos, int input_time) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int chunks;
  int chunk_tab_size;

  chunks = data[0];
  chunk_tab_size = (chunks + 1) * 8;

  lprintf("chunks: %d, chunk_tab_size: %d\n", chunks, chunk_tab_size);

  _x_demux_send_data(track->fifo,
                     data + chunk_tab_size + 1,
                     data_len - chunk_tab_size - 1,
                     data_pts, track->buf_type, decoder_flags,
                     input_normpos, input_time,
                     this->duration, 0);

  /* sends the fragment table */
  {
    buf_element_t *buf;

    buf = track->fifo->buffer_pool_alloc(track->fifo);

    if (chunk_tab_size > buf->max_size) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "demux_matroska: Real Chunk Table length (%d) is greater than fifo buffer length (%" PRId32 ")\n",
              chunk_tab_size, buf->max_size);
      buf->free_buffer(buf);
      return;
    }
    buf->decoder_flags = decoder_flags | BUF_FLAG_SPECIAL | BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = data_duration;
    buf->decoder_info[1] = BUF_SPECIAL_RV_CHUNK_TABLE;
    buf->decoder_info[2] = chunks;
    buf->decoder_info_ptr[2] = buf->content;

    buf->size = 0;
    buf->type = track->buf_type;

    xine_fast_memcpy(buf->decoder_info_ptr[2], data + 1, chunk_tab_size);

    track->fifo->put(track->fifo, buf);
  }
}

static void handle_sub_ssa (demux_plugin_t *this_gen, matroska_track_t *track,
                            int decoder_flags,
                            uint8_t *data, size_t data_len,
                            int64_t data_pts, int data_duration,
                            int input_normpos, int input_time) {
  buf_element_t *buf;
  uint32_t *val;
  int commas = 0;
  int lines = 1;
  char last_char = 0;
  char *dest;
  int dest_len;
  int skip = 0;

  lprintf ("pts: %" PRId64 ", duration: %d\n", data_pts, data_duration);
  /* skip ',' */
  while (data_len && (commas < 8)) {
    if (*data == ',') commas++;
    data++; data_len--;
  }

  buf = track->fifo->buffer_pool_alloc(track->fifo);
  buf->type = track->buf_type;
  buf->decoder_flags = decoder_flags | BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_CHARSET_ENCODING;
  buf->decoder_info_ptr[2] = "utf-8";
  buf->decoder_info[2] = strlen(buf->decoder_info_ptr[2]);

  val = (uint32_t *)buf->content;
  *val++ = data_pts / 90;                    /* start time */
  *val++ = (data_pts + data_duration) / 90;  /* end time   */

  dest = buf->content + 8;
  dest_len = buf->max_size - 8;

  while (data_len && dest_len) {
    if (skip) {
      if (*data == '}')
        skip--;
      else if (*data == '{')
        skip++;
    } else {
      if ((last_char == '\\') && ((*data == 'n') || (*data == 'N'))) {
        lines++;
        *dest = '\n';
        dest++; dest_len--;
      } else {
        if (*data != '\\') {
          if (*data == '{') {
            skip++;
          } else {
            *dest = *data;
            dest++; dest_len--;
          }
        }
      }
    }

    last_char = *data;
    data++; data_len--;
  }

  if (dest_len) {

    *dest = '\0'; dest++; dest_len--;
    buf->size = dest - (char *)buf->content;
    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;

    track->fifo->put(track->fifo, buf);
  } else {
    buf->free_buffer(buf);
  }
}

static void handle_sub_utf8 (demux_plugin_t *this_gen, matroska_track_t *track,
                             int decoder_flags,
                             uint8_t *data, size_t data_len,
                             int64_t data_pts, int data_duration,
                             int input_normpos, int input_time) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  buf_element_t *buf;
  uint32_t *val;

  buf = track->fifo->buffer_pool_alloc(track->fifo);

  buf->size = data_len + 9;  /* 2 uint32_t + '\0' */

  if (buf->max_size >= buf->size) {

    buf->decoder_flags = decoder_flags;
    buf->type = track->buf_type;
    buf->decoder_flags = decoder_flags | BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_CHARSET_ENCODING;
    buf->decoder_info_ptr[2] = "utf-8";
    buf->decoder_info[2] = strlen(buf->decoder_info_ptr[2]);

    val = (uint32_t *)buf->content;
    *val++ = data_pts / 90;                    /* start time */
    *val++ = (data_pts + data_duration) / 90;  /* end time   */

    xine_fast_memcpy(buf->content + 8, data, data_len);
    buf->content[8 + data_len] = '\0';

    lprintf("sub: %s\n", buf->content + 8);
    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;
    track->fifo->put(track->fifo, buf);
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: data length is greater than fifo buffer length\n");
    buf->free_buffer(buf);
  }
}


/* Note: This function assumes that the VobSub track is compressed with zlib.
 * This is not necessarily true - or not enough. The Matroska 'content
 * encoding' elements allow for a layer of changes applied to the contents,
 * e.g. compression or encryption. Anyway, only zlib compression is used
 * at the moment, and everyone compresses the VobSubs.
 */
static void handle_vobsub (demux_plugin_t *this_gen, matroska_track_t *track,
                           int decoder_flags,
                           uint8_t *data, size_t data_len,
                           int64_t data_pts, int data_duration,
                           int input_normpos, int input_time) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  buf_element_t *buf;

  if (track->compress_algo == MATROSKA_COMPRESS_ZLIB ||
      track->compress_algo == MATROSKA_COMPRESS_UNKNOWN) {
    z_stream zstream;
    uint8_t *dest;
    int old_data_len, result;

    old_data_len = data_len;
    zstream.zalloc = (alloc_func) 0;
    zstream.zfree = (free_func) 0;
    zstream.opaque = (voidpf) 0;
    if (inflateInit (&zstream) != Z_OK) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "demux_matroska: VobSub: zlib inflateInit failed.\n");
      return;
    }
    zstream.next_in = (Bytef *)data;
    zstream.avail_in = data_len;

    dest = (uint8_t *)malloc(data_len);
    zstream.avail_out = data_len;
    do {
      data_len += 4000;
      dest = (uint8_t *)realloc(dest, data_len);
      zstream.next_out = (Bytef *)(dest + zstream.total_out);
      result = inflate (&zstream, Z_NO_FLUSH);
      if ((result != Z_OK) && (result != Z_STREAM_END)) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_matroska: VobSub: zlib decompression failed for track %d (result = %d).\n",
                (int)track->track_num, result);
        free(dest);
        inflateEnd(&zstream);

        if (result == Z_DATA_ERROR && track->compress_algo == MATROSKA_COMPRESS_UNKNOWN) {
          track->compress_algo = MATROSKA_COMPRESS_NONE;
          data_len = old_data_len;
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_matroska: VobSub: falling back to uncompressed mode.\n");
          break;
        }
        return;
      }
      zstream.avail_out += 4000;
    } while ((zstream.avail_out == 4000) &&
            (zstream.avail_in != 0) && (result != Z_STREAM_END));

    if (track->compress_algo != MATROSKA_COMPRESS_NONE) {
      data_len = zstream.total_out;
      inflateEnd(&zstream);

      data = dest;
      track->compress_algo = MATROSKA_COMPRESS_ZLIB;
      lprintf("VobSub: decompression for track %d from %d to %d\n",
              (int)track->track_num, old_data_len, data_len);
    }
  }
  else
  {
    lprintf("VobSub: track %d isn't compressed (%d bytes)\n",
            (int)track->track_num, data_len);
  }

  buf = track->fifo->buffer_pool_alloc(track->fifo);

  buf->size = data_len;
  if (buf->max_size >= buf->size) {
    buf->decoder_flags = decoder_flags | BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
    buf->decoder_info[2] = SPU_DVD_SUBTYPE_VOBSUB_PACKAGE;
    buf->type = track->buf_type;

    xine_fast_memcpy(buf->content, data, data_len);

    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;

    buf->pts = data_pts;
    track->fifo->put(track->fifo, buf);

  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: VobSub: data length is greater than fifo buffer length\n");
    buf->free_buffer(buf);
  }

  if (track->compress_algo == MATROSKA_COMPRESS_ZLIB)
    free(data);
}

static int parse_track_entry(demux_matroska_t *this, matroska_track_t *track) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TR_NUMBER:
        {
          uint64_t num;
          lprintf("TrackNumber\n");
          if (!ebml_read_uint(ebml, &elem, &num))
            return 0;
          track->track_num = num;
        }
        break;

      case MATROSKA_ID_TR_TYPE:
        {
          uint64_t num;
          lprintf("TrackType\n");
          if (!ebml_read_uint(ebml, &elem, &num))
            return 0;
          track->track_type = num;
        }
        break;

      case MATROSKA_ID_TR_CODECID:
        {
          char *codec_id = ebml_alloc_read_ascii (ebml, &elem);
          lprintf("CodecID\n");
          if (!codec_id)
            return 0;
          track->codec_id = codec_id;
        }
        break;

      case MATROSKA_ID_TR_CODECPRIVATE:
        {
          uint8_t *codec_private;
          if (elem.len >= 0x80000000)
            return 0;
          codec_private = malloc (elem.len);
          if (! codec_private)
            return 0;
          lprintf("CodecPrivate\n");
          if (!ebml_read_binary(ebml, &elem, codec_private)) {
            free(codec_private);
            return 0;
          }
          track->codec_private = codec_private;
          track->codec_private_len = elem.len;
        }
        break;

      case MATROSKA_ID_TR_LANGUAGE:
        {
          char *language = ebml_alloc_read_ascii (ebml, &elem);
          lprintf("Language\n");
          if (!language)
            return 0;
          track->language = language;
        }
        break;

      case MATROSKA_ID_TV:
        lprintf("Video\n");
        if (track->video_track)
          return 1;
        track->video_track = (matroska_video_track_t *)calloc(1, sizeof(matroska_video_track_t));
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_video_track(this, track->video_track))
          return 0;
        break;

      case MATROSKA_ID_TA:
        lprintf("Audio\n");
        if (track->audio_track)
          return 1;
        track->audio_track = (matroska_audio_track_t *)calloc(1, sizeof(matroska_audio_track_t));
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_audio_track(this, track->audio_track))
          return 0;
        break;

      case MATROSKA_ID_TR_FLAGDEFAULT:
        {
          uint64_t val;

          lprintf("Default\n");
          if (!ebml_read_uint(ebml, &elem, &val))
            return 0;
          track->default_flag = (int)val;
        }
        break;

      case MATROSKA_ID_TR_DEFAULTDURATION:
        {
          uint64_t val;

          if (!ebml_read_uint(ebml, &elem, &val))
            return 0;
          track->default_duration = val;
          lprintf("Default Duration: %"PRIu64"\n", track->default_duration);
        }
        break;

      case MATROSKA_ID_CONTENTENCODINGS:
        {
          lprintf("ContentEncodings\n");
          if (!ebml_read_master (ebml, &elem))
            return 0;
          if ((elem.len > 0) && !parse_content_encodings(this, track))
            return 0;
        }
        break;

      case MATROSKA_ID_TR_UID:
        {
          uint64_t val;

          if (!ebml_read_uint(ebml, &elem, &val)) {
            lprintf("Track UID (invalid)\n");
            return 0;
          }

          track->uid = val;
          lprintf("Track UID: 0x%" PRIx64 "\n", track->uid);
        }
        break;

      case MATROSKA_ID_TR_FLAGENABLED:
      case MATROSKA_ID_TR_FLAGLACING:
      case MATROSKA_ID_TR_MINCACHE:
      case MATROSKA_ID_TR_MAXCACHE:
      case MATROSKA_ID_TR_TIMECODESCALE:
      case MATROSKA_ID_TR_NAME:
      case MATROSKA_ID_TR_CODECNAME:
      case MATROSKA_ID_TR_CODECSETTINGS:
      case MATROSKA_ID_TR_CODECINFOURL:
      case MATROSKA_ID_TR_CODECDOWNLOADURL:
      case MATROSKA_ID_TR_CODECDECODEALL:
      case MATROSKA_ID_TR_OVERLAY:
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem)) {
          return 0;
        }
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
          "demux_matroska: Track %d, %s %s\n",
          track->track_num,
          (track->codec_id ? track->codec_id : ""),
          (track->language ? track->language : ""));
  if (track->codec_id) {
    void (*init_codec)(demux_matroska_t *, matroska_track_t *) = NULL;

    if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_VFW_FOURCC)) {
      xine_bmiheader *bih;

      if (track->codec_private_len >= sizeof(xine_bmiheader)) {
        lprintf("MATROSKA_CODEC_ID_V_VFW_FOURCC\n");
        bih = (xine_bmiheader*)track->codec_private;
        _x_bmiheader_le2me(bih);

        track->buf_type = _x_fourcc_to_buf_video(bih->biCompression);
        if (!track->buf_type)
          _x_report_video_fourcc (this->stream->xine, LOG_MODULE, bih->biCompression);
        init_codec = init_codec_video;
      }

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_UNCOMPRESSED)) {
    } else if ((!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_SP)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_ASP)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_AP))) {
      xine_bmiheader *bih;

      lprintf("MATROSKA_CODEC_ID_V_MPEG4_*\n");
      if (track->codec_private_len > 0x7fffffff - sizeof(xine_bmiheader))
        track->codec_private_len = 0x7fffffff - sizeof(xine_bmiheader);

      /* create a bitmap info header struct for MPEG 4 */
      bih = calloc(1, sizeof(xine_bmiheader) + track->codec_private_len);
      bih->biSize = sizeof(xine_bmiheader) + track->codec_private_len;
      bih->biCompression = ME_FOURCC('M', 'P', '4', 'S');
      bih->biWidth = track->video_track->pixel_width;
      bih->biHeight = track->video_track->pixel_height;
      _x_bmiheader_le2me(bih);

      /* add bih extra data */
      memcpy(bih + 1, track->codec_private, track->codec_private_len);
      free(track->codec_private);
      track->codec_private = (uint8_t *)bih;
      track->codec_private_len = bih->biSize;
      track->buf_type = BUF_VIDEO_MPEG4;

      /* init as a vfw decoder */
      init_codec = init_codec_video;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_AVC)) {
      xine_bmiheader *bih;

      lprintf("MATROSKA_CODEC_ID_V_MPEG4_AVC\n");
      if (track->codec_private_len > 0x7fffffff - sizeof(xine_bmiheader))
        track->codec_private_len = 0x7fffffff - sizeof(xine_bmiheader);

      /* create a bitmap info header struct for h264 */
      bih = calloc(1, sizeof(xine_bmiheader) + track->codec_private_len);
      bih->biSize = sizeof(xine_bmiheader) + track->codec_private_len;
      bih->biCompression = ME_FOURCC('a', 'v', 'c', '1');
      bih->biWidth = track->video_track->pixel_width;
      bih->biHeight = track->video_track->pixel_height;
      _x_bmiheader_le2me(bih);

      /* add bih extra data */
      memcpy(bih + 1, track->codec_private, track->codec_private_len);
      free(track->codec_private);
      track->codec_private = (uint8_t *)bih;
      track->codec_private_len = bih->biSize;
      track->buf_type = BUF_VIDEO_H264;

      /* init as a vfw decoder */
      init_codec = init_codec_video;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MSMPEG4V3)) {
      track->buf_type = BUF_VIDEO_MSMPEG4_V3;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG1)) {
      lprintf("MATROSKA_CODEC_ID_V_MPEG1\n");
      track->buf_type = BUF_VIDEO_MPEG;
      init_codec = init_codec_video;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG2)) {
      lprintf("MATROSKA_CODEC_ID_V_MPEG2\n");
      track->buf_type = BUF_VIDEO_MPEG;
      init_codec = init_codec_video;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_VP8)) {
      xine_bmiheader *bih;

      lprintf("MATROSKA_CODEC_ID_V_VP8\n");
      if (track->codec_private_len > 0x7fffffff - sizeof(xine_bmiheader))
        track->codec_private_len = 0x7fffffff - sizeof(xine_bmiheader);

      /* create a bitmap info header struct for vp8 */
      bih = calloc(1, sizeof(xine_bmiheader) + track->codec_private_len);
      bih->biSize = sizeof(xine_bmiheader) + track->codec_private_len;
      bih->biCompression = ME_FOURCC('v', 'p', '8', '0');
      bih->biWidth = track->video_track->pixel_width;
      bih->biHeight = track->video_track->pixel_height;
      _x_bmiheader_le2me(bih);

      /* add bih extra data */
      memcpy(bih + 1, track->codec_private, track->codec_private_len);
      free(track->codec_private);
      track->codec_private = (uint8_t *)bih;
      track->codec_private_len = bih->biSize;
      track->buf_type = BUF_VIDEO_VP8;

      init_codec = init_codec_video;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV10)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV20)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV30)) {
      lprintf("MATROSKA_CODEC_ID_V_REAL_RV30\n");
      track->buf_type = BUF_VIDEO_RV30;
      track->handle_content = handle_realvideo;
      init_codec = init_codec_real;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV40)) {

      lprintf("MATROSKA_CODEC_ID_V_REAL_RV40\n");
      track->buf_type = BUF_VIDEO_RV40;
      track->handle_content = handle_realvideo;
      init_codec = init_codec_real;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MJPEG)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_THEORA)) {
      lprintf("MATROSKA_CODEC_ID_V_THEORA\n");
      track->buf_type = BUF_VIDEO_THEORA_RAW;
      init_codec = init_codec_xiph;
    } else if ((!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L1)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L2)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L3))) {
      lprintf("MATROSKA_CODEC_ID_A_MPEG1\n");
      track->buf_type = BUF_AUDIO_MPEG;
      init_codec = init_codec_audio;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_INT_BE)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_INT_LE)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_FLOAT)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_AC3)) {
      lprintf("MATROSKA_CODEC_ID_A_AC3\n");
      track->buf_type = BUF_AUDIO_A52;
      init_codec = init_codec_audio;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_EAC3)) {
      lprintf("MATROSKA_CODEC_ID_A_EAC3\n");
      track->buf_type = BUF_AUDIO_EAC3;
      init_codec = init_codec_audio;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_FLAC)) {
      lprintf("MATROSKA_CODEC_ID_A_FLAC\n");
      track->buf_type = BUF_AUDIO_FLAC;
      init_codec = init_codec_audio;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_DTS)) {
      lprintf("MATROSKA_CODEC_ID_A_DTS\n");
      track->buf_type = BUF_AUDIO_DTS;
      init_codec = init_codec_audio;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_VORBIS)) {

      lprintf("MATROSKA_CODEC_ID_A_VORBIS\n");
      track->buf_type = BUF_AUDIO_VORBIS;
      init_codec = init_codec_xiph;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_ACM)) {
      xine_waveformatex *wfh;
      lprintf("MATROSKA_CODEC_ID_A_ACM\n");

      if (track->codec_private_len >= sizeof(xine_waveformatex)) {
        wfh = (xine_waveformatex*)track->codec_private;
        _x_waveformatex_le2me(wfh);

        track->buf_type = _x_formattag_to_buf_audio(wfh->wFormatTag);
        if (!track->buf_type)
          _x_report_audio_format_tag (this->stream->xine, LOG_MODULE, wfh->wFormatTag);
        init_codec = init_codec_audio;
      }
    } else if (!strncmp(track->codec_id, MATROSKA_CODEC_ID_A_AAC,
                        sizeof(MATROSKA_CODEC_ID_A_AAC) - 1)) {
      lprintf("MATROSKA_CODEC_ID_A_AAC\n");
      track->buf_type = BUF_AUDIO_AAC;
      init_codec = init_codec_aac;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_14_4)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_28_8)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_COOK)) {
      lprintf("MATROSKA_CODEC_ID_A_REAL_COOK\n");
      track->buf_type = BUF_AUDIO_COOK;
      init_codec = init_codec_real;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_SIPR)) {
      lprintf("MATROSKA_CODEC_ID_A_REAL_SIPR\n");
      track->buf_type = BUF_AUDIO_SIPRO;
      init_codec = init_codec_real;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_RALF)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_ATRC)) {
      lprintf("MATROSKA_CODEC_ID_A_REAL_ATRC\n");
      track->buf_type = BUF_AUDIO_ATRK;
      init_codec = init_codec_real;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_TEXT_UTF8) ||
        !strcmp(track->codec_id, MATROSKA_CODEC_ID_S_UTF8)) {
      lprintf("MATROSKA_CODEC_ID_S_TEXT_UTF8\n");
      track->buf_type = BUF_SPU_OGM;
      track->handle_content = handle_sub_utf8;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_TEXT_SSA) ||
        !strcmp(track->codec_id, MATROSKA_CODEC_ID_S_SSA)) {
      lprintf("MATROSKA_CODEC_ID_S_TEXT_SSA\n");
      track->buf_type = BUF_SPU_OGM;
      track->handle_content = handle_sub_ssa;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_TEXT_ASS) ||
        !strcmp(track->codec_id, MATROSKA_CODEC_ID_S_ASS)) {
      lprintf("MATROSKA_CODEC_ID_S_TEXT_ASS\n");
      track->buf_type = BUF_SPU_OGM;
      track->handle_content = handle_sub_ssa;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_TEXT_USF)) {
      lprintf("MATROSKA_CODEC_ID_S_TEXT_USF\n");
      track->buf_type = BUF_SPU_OGM;
      track->handle_content = handle_sub_utf8;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_VOBSUB)) {
      lprintf("MATROSKA_CODEC_ID_S_VOBSUB\n");
      track->buf_type = BUF_SPU_DVD;
      track->handle_content = handle_vobsub;
      init_codec = init_codec_vobsub;

      /* Enable autodetection of the zlib compression, unless it was
       * explicitely set. Most vobsubs are compressed with zlib but
       * are not declared as such.
       */
      if (track->compress_algo == MATROSKA_COMPRESS_NONE) {
        track->compress_algo = MATROSKA_COMPRESS_UNKNOWN;
      }
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_S_HDMV_PGS)) {
      lprintf("MATROSKA_CODEC_ID_S_HDMV_PGS\n");
      track->buf_type = BUF_SPU_HDMV;
      init_codec = init_codec_spu;
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "unknown codec %s\n", track->codec_id);
    }

    if (track->buf_type) {

      switch(track->track_type) {
        case MATROSKA_TRACK_VIDEO:
          track->fifo = this->stream->video_fifo;
          track->buf_type |= this->num_video_tracks;
          this->num_video_tracks++;
          break;
        case MATROSKA_TRACK_AUDIO:
          track->fifo = this->stream->audio_fifo;
          track->buf_type |= this->num_audio_tracks;
          this->num_audio_tracks++;
          break;
        case MATROSKA_TRACK_SUBTITLE:
          track->fifo = this->stream->video_fifo;
          track->buf_type |= this->num_sub_tracks;
          this->num_sub_tracks++;
          break;
        case MATROSKA_TRACK_COMPLEX:
        case MATROSKA_TRACK_LOGO:
        case MATROSKA_TRACK_CONTROL:
          break;
      }

      if (init_codec) {
	if (! track->fifo) {
	  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		  "demux_matroska: Error: fifo not set up for track of type type %" PRIu32 "\n", track->track_type);
	  return 0;
        }
        init_codec(this, track);
      }
    }
  }

  return 1;
}


static int parse_tracks(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TR_ENTRY: {
        matroska_track_t *track;

        /* bail out early if no more tracks can be handled! */
        if (this->num_tracks >= MAX_STREAMS) {
          lprintf("Too many tracks!\n");
          return 0;
        }

        /* alloc and initialize a track with 0 */
        track = calloc(1, sizeof(matroska_track_t));
        track->compress_algo = MATROSKA_COMPRESS_NONE;
        this->tracks[this->num_tracks] = track;

        lprintf("TrackEntry\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_track_entry(this, track))
          return 0;
        this->num_tracks++;
      }
      break;

      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}

static int parse_cue_trackposition(demux_matroska_t *this, int *track_num,
                                   int64_t *pos) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;

  while (next_level == 4) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CU_TRACK: {
        uint64_t num;
        lprintf("CueTrackpositionTrack\n");
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        *track_num = num;
        break;
      }
      case MATROSKA_ID_CU_CLUSTERPOSITION: {
        uint64_t num;
        lprintf("CueTrackpositionClusterposition\n");
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        *pos = this->segment.start + num;
        break;
      }
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_cue_point(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;
  int64_t timecode = -1, pos = -1;
  int track_num = -1;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CU_TIME: {
        uint64_t num;
        lprintf("CueTime\n");
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        timecode = num;
        break;
      }
      case MATROSKA_ID_CU_TRACKPOSITION:
        lprintf("CueTrackPosition\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_cue_trackposition(this, &track_num, &pos))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  if ((timecode != -1) && (track_num != -1) && (pos != -1)) {
    matroska_index_t *index;
    int i;

    index = NULL;
    for (i = 0; i < this->num_indexes; i++)
      if (this->indexes[i].track_num == track_num) {
        index = &this->indexes[i];
        break;
      }
    if (index == NULL) {
      this->indexes = (matroska_index_t *)realloc(this->indexes,
                                                  (this->num_indexes + 1) *
                                                  sizeof(matroska_index_t));
      memset(&this->indexes[this->num_indexes], 0, sizeof(matroska_index_t));
      index = &this->indexes[this->num_indexes];
      index->track_num = track_num;
      this->num_indexes++;
    }
    if ((index->num_entries % 1024) == 0) {
      index->pos = realloc(index->pos, sizeof(off_t) *
			   (index->num_entries + 1024));
      index->timecode = realloc(index->timecode, sizeof(uint64_t) *
				(index->num_entries + 1024));
    }
    index->pos[index->num_entries] = pos;
    index->timecode[index->num_entries] = timecode;
    index->num_entries++;
  }

  return 1;
}


static int parse_cues(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CU_POINT:
        lprintf("CuePoint\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_cue_point(this))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_attachments(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_tags(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}

static void alloc_block_data (demux_matroska_t *this, size_t len) {
  /* memory management */
  if (this->block_data_size < len) {
    this->block_data = realloc(this->block_data, len);
    this->block_data_size = len;
  }
}


static int parse_ebml_uint(demux_matroska_t *this, uint8_t *data, uint64_t *num) {
  uint8_t mask = 0x80;
  int size = 1;
  int i;

  /* compute the size of the "data len" (1-8 bytes) */
  while (size <= 8 && !(data[0] & mask)) {
    size++;
    mask >>= 1;
  }
  if (size > 8) {
    off_t pos = this->input->get_current_pos(this->input);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: Invalid Track Number at position %" PRIdMAX "\n",
            (intmax_t)pos);
    return 0;
  }

  *num = data[0];
  *num &= mask - 1;

  for (i = 1; i < size; i++) {
    *num = (*num << 8) | data[i];
  }
  return size;
}


static int parse_ebml_sint(demux_matroska_t *this, uint8_t *data, int64_t *num) {
  uint64_t unum;
  int size;

  size = parse_ebml_uint(this, data, &unum);
  if (!size)
    return 0;

  /* formula taken from gstreamer demuxer */
  if (unum == -1)
    *num = -1;
  else
    *num = unum - ((1 << ((7 * size) - 1)) - 1);

  return size;
}

static int find_track_by_id(demux_matroska_t *this, int track_num,
                            matroska_track_t **track) {
  int i;

  *track = NULL;
  for (i = 0; i < this->num_tracks; i++) {
    if (this->tracks[i]->track_num == track_num) {
      *track = this->tracks[i];
      return 1;
    }
  }
  return 0;
}


static int read_block_data (demux_matroska_t *this, size_t len, size_t offset) {
  alloc_block_data(this, len + offset);

  /* block datas */
  if (! this->block_data) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: memory allocation error\n");
    return 0;
  }
  if (this->input->read(this->input, this->block_data + offset, len) != len) {
    off_t pos = this->input->get_current_pos(this->input);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: read error at position %" PRIdMAX "\n",
            (intmax_t)pos);
    return 0;
  }
  return 1;
}

static int parse_int16(uint8_t *data) {
  int value = (int)_X_BE_16(data);
  if (value & 1<<15)
  {
    value -= 1<<16;
  }
  return value;
}

static int parse_block (demux_matroska_t *this, size_t block_size,
                        uint64_t cluster_timecode, uint64_t block_duration,
                        int normpos, int is_key) {
  matroska_track_t *track;
  uint64_t          track_num;
  uint8_t          *data;
  uint8_t           flags;
  int               lacing, num_len;
  int16_t           timecode_diff;
  int64_t           pts, xduration;
  int               decoder_flags = 0;
  size_t            headers_len = 0;

  data = this->block_data + this->compress_maxlen;
  if (!(num_len = parse_ebml_uint(this, data, &track_num)))
    return 0;
  data += num_len;

  /* timecode_diff is signed */
  timecode_diff = (int16_t)parse_int16(data);
  data += 2;

  flags = *data;
  data += 1;

  lprintf("track_num: %" PRIu64 ", timecode_diff: %d, flags: 0x%x\n", track_num, timecode_diff, flags);

  /*gap = flags & 1;*/
  lacing = (flags >> 1) & 0x3;
/*fprintf(stderr, "lacing: %x\n", lacing);*/

  if (!find_track_by_id(this, (int)track_num, &track)) {
     xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
             "demux_matroska: invalid track id: %" PRIu64 "\n", track_num);
     return 0;
  }

  pts = ((int64_t)cluster_timecode + timecode_diff) *
        (int64_t)this->timecode_scale * (int64_t)90 /
        (int64_t)1000000;

  /* After seeking we have to skip to the next key frame. */
  if (this->skip_to_timecode > 0) {
    if ((this->skip_for_track != track->track_num) || !is_key ||
        (pts < this->skip_to_timecode))
      return 1;
    this->skip_to_timecode = 0;
  }

  if (block_duration) {
    xduration = (int64_t)block_duration *
                (int64_t)this->timecode_scale * (int64_t)90 /
                (int64_t)1000000;
  } else {
    block_duration = track->default_duration;
    xduration = (int64_t)block_duration * (int64_t)90 / (int64_t)1000000;
  }
  lprintf("pts: %" PRId64 ", duration: %" PRId64 "\n", pts, xduration);

  check_newpts(this, pts, track);

  if (this->preview_mode) {
    this->preview_sent++;
    decoder_flags |= BUF_FLAG_PREVIEW;
  }

  if (track->compress_algo == MATROSKA_COMPRESS_HEADER_STRIP)
    headers_len = track->compress_len;

  if (lacing == MATROSKA_NO_LACING) {
    size_t block_size_left;
    lprintf("no lacing\n");

    block_size_left = (this->block_data + block_size + this->compress_maxlen) - data;
    lprintf("size: %d, block_size: %u, block_offset: %u\n", block_size_left, block_size, this->compress_maxlen);

    if (headers_len) {
      data -= headers_len;
      xine_fast_memcpy(data, track->compress_settings, headers_len);
      block_size_left += headers_len;
    }

    if (track->handle_content != NULL) {
      track->handle_content((demux_plugin_t *)this, track,
                             decoder_flags,
                             data, block_size_left,
                             pts, xduration,
                             normpos, pts / 90);
    } else {
      _x_demux_send_data(track->fifo, data, block_size_left,
                         pts, track->buf_type, decoder_flags,
                         normpos, pts / 90,
                         this->duration, 0);
    }
  } else {

    size_t block_size_left;
    uint8_t lace_num;
    size_t frame[MAX_FRAMES];
    int i;

    /* number of laced frames */
    lace_num = *data;
    data++;
    lprintf("lace_num: %d\n", lace_num);
    if ((lace_num + 1) > MAX_FRAMES) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "demux_matroska: too many frames: %d\n", lace_num);
      return 0;
    }
    block_size_left = this->block_data + block_size + this->compress_maxlen - data;

    switch (lacing) {
      case MATROSKA_XIPH_LACING: {

        lprintf("xiph lacing\n");

        /* size of each frame */
        for (i = 0; i < lace_num; i++) {
          int size = 0;
          int partial_size;
          do
          {
            partial_size = *data;
            size += partial_size;
            data++; block_size_left--;
          } while (partial_size == 255);
          frame[i] = size;
          block_size_left -= size;
        }

        /* last frame */
        frame[lace_num] = block_size_left;
      }
      break;

      case MATROSKA_FIXED_SIZE_LACING: {
        int frame_size;

        lprintf("fixed size lacing\n");

        frame_size = block_size_left / (lace_num + 1);
        for (i = 0; i < lace_num; i++) {
          frame[i] = frame_size;
        }
        frame[lace_num] = block_size_left - (lace_num * frame_size);
        block_size_left = 0;
      }
      break;

      case MATROSKA_EBML_LACING: {
        uint64_t first_frame_size;

        lprintf("ebml lacing\n");

        /* size of each frame */
        if (!(num_len = parse_ebml_uint(this, data, &first_frame_size)))
          return 0;
        if (num_len > block_size_left) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_matroska: block too small\n");
          return 0;
        }
        if (first_frame_size > INT_MAX) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_matroska: invalid first frame size (%" PRId64 ")\n",
                  first_frame_size);
          return 0;
        }
        data += num_len; block_size_left -= num_len;
        frame[0] = (int) first_frame_size;
        lprintf("first frame len: %d\n", frame[0]);
        block_size_left -= frame[0];

        for (i = 1; i < lace_num; i++) {
          int64_t frame_size_diff;
          int64_t frame_size;

          if (!(num_len = parse_ebml_sint(this, data, &frame_size_diff)))
            return 0;

          if (num_len > block_size_left) {
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                    "demux_matroska: block too small\n");
            return 0;
          }
          data += num_len; block_size_left -= num_len;

          frame_size = frame[i-1] + frame_size_diff;
          if (frame_size > INT_MAX || frame_size < 0) {
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                    "demux_matroska: invalid frame size (%" PRId64 ")\n",
                    frame_size);
            return 0;
          }
          frame[i] = frame_size;
          block_size_left -= frame[i];
        }

        /* last frame */
        frame[lace_num] = block_size_left;
      }
      break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_matroska: invalid lacing: %d\n", lacing);
        return 0;
    }
    /* send each frame to the decoder */
    for (i = 0; i <= lace_num; i++) {

      if (headers_len) {
        data -= headers_len;
        xine_fast_memcpy(data, track->compress_settings, headers_len);
        frame[i] += headers_len;
      }

      if (track->handle_content != NULL) {
        track->handle_content((demux_plugin_t *)this, track,
                               decoder_flags,
                               data, frame[i],
                               pts, 0,
                               normpos, pts / 90);
      } else {
        _x_demux_send_data(track->fifo, data, frame[i],
                           pts, track->buf_type, decoder_flags,
                           normpos, pts / 90,
                           this->duration, 0);
      }
      data += frame[i];
      pts = 0;
    }
  }
  return 1;
}

static int parse_simpleblock(demux_matroska_t *this, size_t block_len, uint64_t cluster_timecode, uint64_t block_duration)
{
  off_t block_pos         = 0;
  off_t file_len          = 0;
  int normpos             = 0;
  int is_key              = 1;

  lprintf("simpleblock\n");
  block_pos = this->input->get_current_pos(this->input);
  file_len = this->input->get_length(this->input);
  if( file_len )
    normpos = (int) ( (double) block_pos * 65535 / file_len );

  if (!read_block_data(this, block_len, this->compress_maxlen))
    return 0;

    /* we have the duration, we can parse the block now */
  if (!parse_block(this, block_len, cluster_timecode, block_duration,
                   normpos, is_key))
    return 0;
  return 1;
}

static int parse_block_group(demux_matroska_t *this,
                             uint64_t cluster_timecode,
                             uint64_t cluster_duration) {
  ebml_parser_t *ebml     = this->ebml;
  int next_level          = 3;
  int has_block           = 0;
  uint64_t block_duration = 0;
  off_t block_pos         = 0;
  off_t file_len          = 0;
  int normpos             = 0;
  size_t block_len        = 0;
  int is_key              = 1;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CL_BLOCK:
        lprintf("block\n");
        block_pos = this->input->get_current_pos(this->input);
        block_len = elem.len;
        file_len = this->input->get_length(this->input);
        if( file_len )
          normpos = (int) ( (double) block_pos * 65535 / file_len );

        if (!read_block_data(this, elem.len, this->compress_maxlen))
          return 0;

          has_block = 1;
        break;
      case MATROSKA_ID_CL_BLOCKDURATION:
        /* should override track duration */
        if (!ebml_read_uint(ebml, &elem, &block_duration))
          return 0;
        lprintf("duration: %" PRIu64 "\n", block_duration);
        break;
      case MATROSKA_ID_CL_REFERENCEBLOCK:
        is_key = 0;
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  if (!has_block)
    return 0;

  /* we have the duration, we can parse the block now */
  if (!parse_block(this, block_len, cluster_timecode, block_duration,
                   normpos, is_key))
    return 0;
  return 1;
}

static int demux_matroska_seek (demux_plugin_t*, off_t, int, int);

static void handle_events(demux_matroska_t *this) {
  xine_event_t* event;

  while ((event = xine_event_get(this->event_queue))) {
    if (this->num_editions > 0) {
      matroska_edition_t* ed = this->editions[0];
      int chapter_idx = matroska_get_chapter(this, this->last_timecode, &ed);
      uint64_t next_time;

      if (chapter_idx < 0) {
        xine_event_free(event);
        continue;
      }

      switch(event->type) {
        case XINE_EVENT_INPUT_NEXT:
          if (chapter_idx < ed->num_chapters-1) {
            next_time = ed->chapters[chapter_idx+1]->time_start / 90;
            demux_matroska_seek((demux_plugin_t*)this, 0, next_time, 1);
          }
          break;

          /* TODO: should this try to implement common "start of chapter"
           *  functionality? */
        case XINE_EVENT_INPUT_PREVIOUS:
          if (chapter_idx > 0) {
            next_time = ed->chapters[chapter_idx-1]->time_start / 90;
            demux_matroska_seek((demux_plugin_t*)this, 0, next_time, 1);
          }
          break;

        default:
          break;
      }
    }

    xine_event_free(event);
  }
}

static int parse_cluster(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int this_level = ebml->level;
  int next_level = this_level;
  uint64_t timecode = 0;
  uint64_t duration = 0;

  if (!this->first_cluster_found) {
    int idx, entry;

    /* Scale the cues to ms precision. */
    for (idx = 0; idx < this->num_indexes; idx++) {
      matroska_index_t *index = &this->indexes[idx];
      for (entry = 0; entry < index->num_entries; entry++)
        index->timecode[entry] = index->timecode[entry] *
          this->timecode_scale / 1000000;
    }
    this->first_cluster_found = 1;
  }

  handle_events(this);

  while (next_level == this_level) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CL_TIMECODE:
        lprintf("timecode\n");
        if (!ebml_read_uint(ebml, &elem, &timecode))
          return 0;
        break;
      case MATROSKA_ID_CL_DURATION:
        lprintf("duration\n");
        if (!ebml_read_uint(ebml, &elem, &duration))
          return 0;
        break;
      case MATROSKA_ID_CL_BLOCKGROUP:
        lprintf("blockgroup\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_block_group(this, timecode, duration))
          return 0;
        break;
      case MATROSKA_ID_CL_SIMPLEBLOCK:
        lprintf("simpleblock\n");
        if (!parse_simpleblock(this, elem.len, timecode, duration))
          return 0;
        break;
      case MATROSKA_ID_CL_BLOCK:
        lprintf("block\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  /* at this point, we MUST have a timecode (according to format spec).
   * Use that to find the chapter we are in, and adjust the title.
   *
   * TODO: this only looks at the chapters in the first edition.
   */

  this->last_timecode = timecode;

  if (this->num_editions <= 0)
    return 1;
  matroska_edition_t *ed = this->editions[0];

  if (ed->num_chapters <= 0)
    return 1;

  /* fix up a makeshift title if none has been set yet (e.g. filename) */
  if (NULL == this->title && NULL != _x_meta_info_get(this->stream, XINE_META_INFO_TITLE))
    this->title = strdup(_x_meta_info_get(this->stream, XINE_META_INFO_TITLE));

  if (NULL == this->title)
    this->title = strdup("(No title)");

  if (NULL == this->title) {
    lprintf("Failed to determine a valid stream title!\n");
    return 1;
  }

  int chapter_idx = matroska_get_chapter(this, timecode, &ed);
  if (chapter_idx < 0) {
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TITLE, this->title);
    return 1;
  }

  xine_ui_data_t uidata = {
    .str = {0, },
    .str_len = 0,
  };

  uidata.str_len = snprintf(uidata.str, sizeof(uidata.str), "%s / (%d) %s",
      this->title, chapter_idx+1, ed->chapters[chapter_idx]->title);
  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TITLE, uidata.str);

  return 1;
}

static int parse_top_level_head(demux_matroska_t *this, int *next_level);

static int parse_seek_entry(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;
  int has_id = 0;
  int has_position = 0;
  uint64_t id = 0;
  uint64_t pos;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_S_ID:
        lprintf("SeekID\n");
        if (!ebml_read_uint(ebml, &elem, &id))
          return 0;
        has_id = 1;
        break;
      case MATROSKA_ID_S_POSITION:
        lprintf("SeekPosition\n");
        if (!ebml_read_uint(ebml, &elem, &pos))
          return 0;
        has_position = 1;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  /* do not parse clusters */
  if (id == MATROSKA_ID_CLUSTER) {
    lprintf("skip cluster\n");
    return 1;
  }

  /* parse the referenced element */
  if (has_id && has_position) {
    off_t current_pos, seek_pos;

    seek_pos = this->segment.start + pos;

    if ((seek_pos > 0) && (seek_pos < this->input->get_length(this->input))) {
      ebml_parser_t ebml_bak;

      /* backup current state */
      current_pos = this->input->get_current_pos(this->input);
      memcpy(&ebml_bak, this->ebml, sizeof(ebml_parser_t));   /* FIXME */

      /* seek and parse the top_level element */
      this->ebml->level = 1;
      if (this->input->seek(this->input, seek_pos, SEEK_SET) < 0) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "demux_matroska: failed to seek to pos: %" PRIdMAX "\n",
                (intmax_t)seek_pos);
        return 0;
      }
      if (!parse_top_level_head(this, &next_level))
        return 0;

      /* restore old state */
      memcpy(this->ebml, &ebml_bak, sizeof(ebml_parser_t));   /* FIXME */
      if (this->input->seek(this->input, current_pos, SEEK_SET) < 0) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "demux_matroska: failed to seek to pos: %" PRIdMAX "\n",
                (intmax_t)current_pos);
        return 0;
      }
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_matroska: out of stream seek pos: %" PRIdMAX "\n",
              (intmax_t)seek_pos);
    }
    return 1;
  } else {
    lprintf("incomplete Seek Entry\n");
    return 1;
  }
}


static int parse_seekhead(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_S_ENTRY:
        lprintf("Seek Entry\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_seek_entry(this))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  return 1;
}


/*
 * Function used to parse a top level when opening the file.
 * It does'nt parse clusters.
 * retuned value:
 *   0: error
 *   1: ok
 *   2: cluster
 */
static int parse_top_level_head(demux_matroska_t *this, int *next_level) {
  ebml_parser_t *ebml = this->ebml;
  ebml_elem_t elem;
  int ret_value = 1;
  off_t current_pos;


  current_pos = this->input->get_current_pos(this->input);
  lprintf("current_pos: %" PRIdMAX "\n", (intmax_t)current_pos);

  if (!ebml_read_elem_head(ebml, &elem))
    return 0;

  if (!find_top_level_entry(this, current_pos)) {

    if (!add_top_level_entry(this, current_pos))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_SEEKHEAD:
        lprintf("SeekHead\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_seekhead(this))
          return 0;
        break;
      case MATROSKA_ID_INFO:
        lprintf("Info\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_info(this))
          return 0;
        break;
      case MATROSKA_ID_TRACKS:
        lprintf("Tracks\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_tracks(this))
          return 0;
        break;
      case MATROSKA_ID_CHAPTERS:
        lprintf("Chapters\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !matroska_parse_chapters(this))
          return 0;
        break;
      case MATROSKA_ID_CLUSTER:
        lprintf("Cluster\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        ret_value = 2;
        break;
      case MATROSKA_ID_CUES:
        lprintf("Cues\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_cues(this))
          return 0;
        break;
      case MATROSKA_ID_ATTACHMENTS:
        lprintf("Attachments\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_attachments(this))
          return 0;
        break;
      case MATROSKA_ID_TAGS:
        lprintf("Tags\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if ((elem.len > 0) && !parse_tags(this))
          return 0;
        break;
      default:
        lprintf("unknown top_level ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
  } else {
    lprintf("top_level entry already parsed, ID: 0x%x\n", elem.id);
    if (!ebml_skip(ebml, &elem))
      return 0;
  }

  if (next_level)
    *next_level = ebml_get_next_level(ebml, &elem);

  return ret_value;
}

/*
 * Function used to parse a top level element during the playback.
 * It skips all elements except clusters.
 * Others elements should have been parsed before by the send_headers() function.
 */
static int parse_top_level(demux_matroska_t *this, int *next_level) {
  ebml_parser_t *ebml = this->ebml;
  ebml_elem_t elem;

  if (!ebml_read_elem_head(ebml, &elem))
    return 0;

  switch (elem.id) {
    case MATROSKA_ID_SEEKHEAD:
      lprintf("SeekHead\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      this->has_seekhead = 1;
      break;
    case MATROSKA_ID_INFO:
      lprintf("Info\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_TRACKS:
      lprintf("Tracks\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_CHAPTERS:
      lprintf("Chapters\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_CLUSTER:
      lprintf("Cluster\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_cluster(this))
        return 0;
      break;
    case MATROSKA_ID_CUES:
      lprintf("Cues\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_ATTACHMENTS:
      lprintf("Attachments\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_TAGS:
      lprintf("Tags\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;

    default:
      lprintf("Unhandled ID: 0x%x\n", elem.id);
      if (!ebml_skip(ebml, &elem))
        return 0;
  }
  if (next_level)
    *next_level = ebml_get_next_level(ebml, &elem);
  return 1;
}

/*
 * Parse the mkv file structure.
 */
static int parse_segment(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;

  /* check segment id */
  if (!ebml_read_elem_head(ebml, &this->segment))
    return 0;

  if (this->segment.id == MATROSKA_ID_SEGMENT) {
    int res;
    int next_level;

    lprintf("Segment detected\n");

    if (!ebml_read_master (ebml, &this->segment))
      return 0;

    res = 1;
    next_level = 1;
    /* stop the loop on the first cluster */
    while ((next_level == 1) && (res == 1)) {
      res = parse_top_level_head(this, &next_level);
      if (!res)
        return 0;
    }
    return 1;
  } else {
    /* not a segment */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_matroska: invalid segment\n");
    return 0;
  }
}

static int demux_matroska_send_chunk (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int next_level;

  if (!parse_top_level(this, &next_level)) {
    this->status = DEMUX_FINISHED;
  }
  return this->status;
}


static int demux_matroska_get_status (demux_plugin_t *this_gen) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;

  return this->status;
}


static void demux_matroska_send_headers (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int next_level;

  _x_demux_control_start (this->stream);

  if (!parse_segment(this))
    this->status = DEMUX_FINISHED;
  else
    this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, (this->num_video_tracks != 0));
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, (this->num_audio_tracks != 0));


  /*
   * send preview buffers
   */

  /* enter in the segment */
  ebml_read_master (this->ebml, &this->segment);

  /* seek back to the beginning of the segment */
  next_level = 1;
  if (this->input->seek(this->input, this->segment.start, SEEK_SET) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_matroska: failed to seek to pos: %" PRIdMAX "\n",
            (intmax_t)this->segment.start);
    this->status = DEMUX_FINISHED;
    return;
  }

  this->preview_sent = 0;
  this->preview_mode = 1;

  while ((this->preview_sent < NUM_PREVIEW_BUFFERS) && (next_level == 1)) {
    if (!parse_top_level (this, &next_level)) {
      break;
    }
  }
  this->preview_mode = 0;

  /* seek back to the beginning of the segment */
  next_level = 1;
  if (this->input->seek(this->input, this->segment.start, SEEK_SET) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_matroska: failed to seek to pos: %" PRIdMAX "\n",
            (intmax_t)this->segment.start);
    this->status = DEMUX_FINISHED;
  }
}


/* support function that performs a binary seek on a track; returns the
 * best index entry or -1 if the seek was beyond the end of the file */
static int binary_seek(matroska_index_t *index, off_t start_pos,
                       int start_time) {
  int best_index;
  int left, middle, right;
  int found;

  /* perform a binary search on the trak, testing the offset
   * boundaries first; offset request has precedent over time request */
  if (start_pos) {
    if (start_pos <= index->pos[0])
      best_index = 0;
    else if (start_pos >= index->pos[index->num_entries - 1])
      best_index = index->num_entries - 1;
    else {
      left = 0;
      right = index->num_entries - 1;
      found = 0;

      while (!found) {
        middle = (left + right + 1) / 2;
        if ((start_pos >= index->pos[middle]) &&
            (start_pos < index->pos[middle + 1]))
          found = 1;
        else if (start_pos < index->pos[middle])
          right = middle - 1;
        else
          left = middle;
      }

      best_index = middle;
    }
  } else {
    if (start_time <= index->timecode[0])
      best_index = 0;
    else if (start_time >= index->timecode[index->num_entries - 1])
      best_index = index->num_entries - 1;
    else {
      left = 0;
      right = index->num_entries - 1;
      do {
        middle = (left + right + 1) / 2;
        if (start_time < index->timecode[middle])
          right = (middle - 1);
        else
          left = middle;
      } while (left < right);

      best_index = left;
    }
  }

  return best_index;
}


static int demux_matroska_seek (demux_plugin_t *this_gen,
                                off_t start_pos, int start_time, int playing) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  matroska_index_t *index;
  matroska_track_t *track;
  int i, entry;

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  this->status = DEMUX_OK;

  /* engine sync stuff */
  for (i = 0; i < this->num_tracks; i++) {
    this->tracks[i]->last_pts = 0;
  }
  this->send_newpts   = 1;
  this->buf_flag_seek = 1;

  /* Seeking without an index is not supported yet. */
  if (!this->num_indexes)
    return this->status;

  /* Find an index for a video track and use the first available index
     otherwise. */
  index = NULL;
  for (i = 0; i < this->num_indexes; i++) {
    if (this->indexes[i].num_entries == 0)
      continue;
    if ((find_track_by_id(this, this->indexes[i].track_num, &track)) &&
        (track->track_type == MATROSKA_TRACK_VIDEO)) {
      lprintf("video track found\n");
      index = &this->indexes[i];
      break;
    }
  }
  if (index == NULL)
    for (i = 0; i < this->num_indexes; i++) {
      if (this->indexes[i].num_entries == 0)
        continue;
      if (find_track_by_id(this, this->indexes[i].track_num, &track)) {
        index = &this->indexes[i];
        break;
      }
    }

  /* No suitable index found. */
  if (index == NULL)
    return this->status;

  entry = binary_seek(index, start_pos, start_time);
  if (entry == -1) {
    lprintf("seeking for track %d to %s %" PRIdMAX " - no entry found/EOS.\n",
            index->track_num, start_pos ? "pos" : "time",
            start_pos ? (intmax_t)start_pos : (intmax_t)start_time);
    this->status = DEMUX_FINISHED;

  } else {
    lprintf("seeking for track %d to %s %" PRIdMAX ". decision is #%d at %" PRIu64 "/%" PRIdMAX "\n",
            index->track_num, start_pos ? "pos" : "time",
            start_pos ? (intmax_t)start_pos : (intmax_t)start_time,
            index->track_num, index->timecode[entry], (intmax_t)index->pos[entry]);

    if (this->input->seek(this->input, index->pos[entry], SEEK_SET) < 0)
      this->status = DEMUX_FINISHED;

    /* we always seek to the ebml level 1 */
    this->ebml->level = 1;

    this->skip_to_timecode = index->timecode[entry];
    this->skip_for_track = track->track_num;
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}


static void demux_matroska_dispose (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int i;

  free(this->block_data);

  /* free tracks */
  for (i = 0; i < this->num_tracks; i++) {
    matroska_track_t *const track = this->tracks[i];

    free (track->language);
    free (track->codec_id);
    free (track->codec_private);
    free (track->video_track);
    free (track->audio_track);
    free (track->sub_track);

    free (track);
  }
  /* Free the cues. */
  for (i = 0; i < this->num_indexes; i++) {
    free(this->indexes[i].pos);
    free(this->indexes[i].timecode);
  }
  free(this->indexes);

  /* Free the top_level elem list */
  free(this->top_level_list);

  free(this->title);

  matroska_free_editions(this);

  dispose_ebml_parser(this->ebml);
  xine_event_dispose_queue(this->event_queue);
  free (this);
}


static int demux_matroska_get_stream_length (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;

  return (int)this->duration;
}


static uint32_t demux_matroska_get_capabilities (demux_plugin_t *this_gen) {
  demux_matroska_t* this = (demux_matroska_t*)this_gen;
  uint32_t caps = DEMUX_CAP_SPULANG | DEMUX_CAP_AUDIOLANG;

  if(this->num_editions > 0 && this->editions[0]->num_chapters > 0)
    caps |= DEMUX_CAP_CHAPTERS;

  return caps;
}


static int demux_matroska_get_optional_data (demux_plugin_t *this_gen,
                                             void *data, int data_type) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  char *str = (char *) data;
  int channel = *((int *)data);
  int track_num;

  switch (data_type) {
    case DEMUX_OPTIONAL_DATA_SPULANG:
      lprintf ("DEMUX_OPTIONAL_DATA_SPULANG channel = %d\n",channel);
      if ((channel >= 0) && (channel < this->num_sub_tracks)) {
        for (track_num = 0; track_num < this->num_tracks; track_num++) {
          matroska_track_t *track = this->tracks[track_num];

          if ((track->buf_type & 0xFF00001F) == (BUF_SPU_BASE + channel)) {
            if (track->language) {
              strncpy (str, track->language, XINE_LANG_MAX);
              str[XINE_LANG_MAX - 1] = '\0';
              if (strlen(track->language) >= XINE_LANG_MAX)
                /* the string got truncated */
                str[XINE_LANG_MAX - 2] = str[XINE_LANG_MAX - 3] = str[XINE_LANG_MAX - 4] = '.';
            } else {
              snprintf(str, XINE_LANG_MAX, "eng");
            }
            return DEMUX_OPTIONAL_SUCCESS;
          }
        }
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;

    case DEMUX_OPTIONAL_DATA_AUDIOLANG:
      lprintf ("DEMUX_OPTIONAL_DATA_AUDIOLANG channel = %d\n",channel);
      if ((channel >= 0) && (channel < this->num_audio_tracks)) {
        for (track_num = 0; track_num < this->num_tracks; track_num++) {
          matroska_track_t *track = this->tracks[track_num];

          if ((track->buf_type & 0xFF00001F) == (BUF_AUDIO_BASE + channel)) {
            if (track->language) {
              strncpy (str, track->language, XINE_LANG_MAX);
              str[XINE_LANG_MAX - 1] = '\0';
              if (strlen(track->language) >= XINE_LANG_MAX)
                /* the string got truncated */
                str[XINE_LANG_MAX - 2] = str[XINE_LANG_MAX - 3] = str[XINE_LANG_MAX - 4] = '.';
            } else {
              snprintf(str, XINE_LANG_MAX, "eng");
            }
            return DEMUX_OPTIONAL_SUCCESS;
          }
        }
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;

    default:
      return DEMUX_OPTIONAL_UNSUPPORTED;
  }
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_matroska_t *this = NULL;
  ebml_parser_t    *ebml = NULL;

  lprintf("trying to open %s...\n", input->get_mrl(input));

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    if (!(input->get_capabilities (input) & INPUT_CAP_SEEKABLE))
      return NULL;
    input->seek(input, 0, SEEK_SET);
    ebml = new_ebml_parser(stream->xine, input);
    if (!ebml_check_header(ebml))
      goto error;
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  this = calloc(1, sizeof(demux_matroska_t));

  this->demux_plugin.send_headers      = demux_matroska_send_headers;
  this->demux_plugin.send_chunk        = demux_matroska_send_chunk;
  this->demux_plugin.seek              = demux_matroska_seek;
  this->demux_plugin.dispose           = demux_matroska_dispose;
  this->demux_plugin.get_status        = demux_matroska_get_status;
  this->demux_plugin.get_stream_length = demux_matroska_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_matroska_get_capabilities;
  this->demux_plugin.get_optional_data = demux_matroska_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->input      = input;
  this->status     = DEMUX_FINISHED;
  this->stream     = stream;

  if (!ebml) {
    ebml = new_ebml_parser(stream->xine, input);
    if (!ebml_check_header(ebml))
      goto error;
  }
  this->ebml = ebml;

  /* check header fields */
  if (ebml->max_id_len > 4)
    goto error;
  if (ebml->max_size_len > 8)
    goto error;
  /* handle both Matroska and WebM here; we don't (presently) differentiate */
  if (!ebml->doctype || (strcmp(ebml->doctype, "matroska") && strcmp(ebml->doctype, "webm")))
    goto error;

  this->event_queue = xine_event_new_queue(this->stream);

  return &this->demux_plugin;

error:
  dispose_ebml_parser(ebml);

  if (this != NULL && this->event_queue != NULL) {
    xine_event_dispose_queue(this->event_queue);
    free(this);
  }

  return NULL;
}


/*
 * demux matroska class
 */
static void *init_class (xine_t *xine, void *data) {

  demux_matroska_class_t     *this;

  this         = calloc(1, sizeof(demux_matroska_class_t));
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("matroska & webm demux plugin");
  this->demux_class.identifier      = "matroska";
  this->demux_class.mimetypes       = "video/mkv: mkv: matroska;"
				      "video/x-matroska: mkv: matroska;"
				      "video/webm: wbm,webm: WebM;";

  this->demux_class.extensions      = "mkv wbm webm";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_matroska = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "matroska", XINE_VERSION_CODE, &demux_info_matroska, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
