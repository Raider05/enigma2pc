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
 */

/*
 * demultiplexer for ogg streams
 */
/* 2003.02.09 (dilb) update of the handling for audio/video infos for strongarm cpus. */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>

#include <ogg/ogg.h>

#ifdef HAVE_VORBIS
#include <vorbis/codec.h>
#endif

#ifdef HAVE_SPEEX
#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>
#include <speex/speex_callbacks.h>
#endif

#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif

#define LOG_MODULE "demux_ogg"
#define LOG_VERBOSE

/*
#define LOG
*/

#define DEBUG_PACKETS 0
#define DEBUG_PREVIEWS 0
#define DEBUG_PTS 0
#define DEBUG_VIDEO_PACKETS 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include "bswap.h"
#include "flacutils.h"

#include "ogg_combined.h"

#define CHUNKSIZE                8500
#define PACKET_TYPE_HEADER       0x01
#define PACKET_TYPE_COMMENT      0x03
#define PACKET_TYPE_CODEBOOK     0x05
#define PACKET_TYPE_BITS	 0x07
#define PACKET_LEN_BITS01        0xc0
#define PACKET_LEN_BITS2         0x02
#define PACKET_IS_SYNCPOINT      0x08

#define MAX_STREAMS              32

#define PTS_AUDIO                0
#define PTS_VIDEO                1

#define WRAP_THRESHOLD           900000

#define SUB_BUFSIZE 1024

typedef struct chapter_entry_s {
  int64_t           start_pts;
  char              *name;
} chapter_entry_t;

typedef struct chapter_info_s {
  int                current_chapter;
  int                max_chapter;
  chapter_entry_t   *entries;
} chapter_info_t;

typedef struct stream_info_s {
  ogg_stream_state      oss;
  uint32_t              buf_types;
  int                   headers;
  int64_t               header_granulepos;
  int64_t               factor;
  int64_t               quotient;
  int                   resync;
  char                  *language;
  /* CMML, Ogg Skeleton stream information */
  int                   granuleshift;
  /* Annodex v2 stream information */
  int                   hide_first_header;
  int                   delivered_bos;
  int                   delivered_eos;
} stream_info_t;

typedef struct demux_ogg_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;
  input_plugin_t       *input;
  int                   status;

  int                   frame_duration;

#ifdef HAVE_THEORA
  theora_info           t_info;
  theora_comment        t_comment;
#endif

  ogg_sync_state        oy;
  ogg_page              og;

  int64_t               start_pts;
  int64_t               last_pts[2];

  int                   time_length;

  int                   num_streams;
  stream_info_t        *si[MAX_STREAMS];   /* stream info */

  int                   num_audio_streams;
  int                   num_video_streams;
  int                   unhandled_video_streams;
  int                   num_spu_streams;

  off_t                 avg_bitrate;

  char		       *meta[XINE_STREAM_INFO_MAX];
  chapter_info_t       *chapter_info;
  xine_event_queue_t   *event_queue;

  uint8_t               send_newpts:1;
  uint8_t               buf_flag_seek:1;
  uint8_t               keyframe_needed:1;
  uint8_t               ignore_keyframes:1;
} demux_ogg_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_ogg_class_t;

typedef struct {
  demux_class_t     demux_class;
} demux_anx_class_t;


#ifdef HAVE_THEORA
static int intlog(int num) {
  int ret=0;

  while(num>0){
    num=num/2;
    ret=ret+1;
  }
  return(ret);
}
#endif

static int get_stream (demux_ogg_t *this, int serno) {
  /*finds the stream_num, which belongs to a ogg serno*/
  int i;

  for (i = 0; i<this->num_streams; i++) {
    if (this->si[i]->oss.serialno == serno) {
      return i;
    }
  }
  return -1;
}

static int new_stream_info (demux_ogg_t *this, const int cur_serno) {
  int stream_num;

  this->si[this->num_streams] = (stream_info_t *)calloc(1, sizeof(stream_info_t));
  ogg_stream_init(&this->si[this->num_streams]->oss, cur_serno);
  stream_num = this->num_streams;
  this->si[stream_num]->buf_types = 0;
  this->si[stream_num]->header_granulepos = -1;
  this->si[stream_num]->headers = 0;
  this->num_streams++;

  return stream_num;
}

static int64_t get_pts (demux_ogg_t *this, int stream_num , int64_t granulepos ) {
  /*calculates an pts from an granulepos*/
  if (granulepos<0) {
    if ( this->si[stream_num]->header_granulepos>=0 ) {
      /*return the smallest valid pts*/
      return 1;
    } else
      return 0;
  } else if (this->si[stream_num]->buf_types == BUF_VIDEO_THEORA ||
	     (this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_SPU_CMML) {
    int64_t iframe, pframe;
    int granuleshift;
    granuleshift = this->si[stream_num]->granuleshift;
    iframe = granulepos >> granuleshift;
    pframe = granulepos - (iframe << granuleshift);
    if (this->si[stream_num]->quotient)
      return 1+((iframe+pframe) * this->si[stream_num]->factor / this->si[stream_num]->quotient);
    else
      return 0;
  } else if (this->si[stream_num]->quotient)
    return 1+(granulepos * this->si[stream_num]->factor / this->si[stream_num]->quotient);
  else
    return 0;
}

static int read_ogg_packet (demux_ogg_t *this) {
  char *buffer;
  long bytes;
  long total = 0;
  while (ogg_sync_pageout(&this->oy,&this->og)!=1) {
    buffer = ogg_sync_buffer(&this->oy, CHUNKSIZE);
    bytes  = this->input->read(this->input, buffer, CHUNKSIZE);
    if (bytes <= 0) {
      if (total == 0) {
        lprintf("read_ogg_packet read nothing\n");
        return 0;
      }
      break;
    }
    ogg_sync_wrote(&this->oy, bytes);
    total += bytes;
  }
  return 1;
}

static void get_stream_length (demux_ogg_t *this) {
  /*determine the streamlenght and set this->time_length accordingly.
    ATTENTION:current_pos and oggbuffers will be destroyed by this function,
    there will be no way to continue playback uninterrupted.

    You have to seek afterwards, because after get_stream_length, the
    current_position is at the end of the file */

  off_t filelength;
  int done=0;
  int stream_num;

  this->time_length=-1;

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    filelength=this->input->get_length(this->input);

    if (filelength!=-1) {
      if (filelength>70000) {
        this->demux_plugin.seek(&this->demux_plugin,
				(off_t) ( (double)(filelength-65536)/filelength*65535), 0, 0);
      }
      done=0;
      while (!done) {
        if (!read_ogg_packet (this)) {
          if (this->time_length) {
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE,
                               ((int64_t) 8000*filelength)/this->time_length);
            /*this is a fine place to compute avg_bitrate*/
            this->avg_bitrate= 8000*filelength/this->time_length;
          }
          return;
        }
        stream_num=get_stream(this, ogg_page_serialno (&this->og) );
        if (stream_num!=-1) {
          if (this->time_length < (get_pts(this, stream_num, ogg_page_granulepos(&this->og) / 90)))
            this->time_length = get_pts(this, stream_num, ogg_page_granulepos(&this->og)) / 90;
        }
      }
    }
  }
}

#ifdef HAVE_THEORA
static void send_ogg_packet (demux_ogg_t *this,
                               fifo_buffer_t *fifo,
                               ogg_packet *op,
                               int64_t pts,
                               uint32_t decoder_flags,
                               int stream_num) {

  buf_element_t *buf;

  int done=0,todo=op->bytes;
  const size_t op_size = sizeof(ogg_packet);

  while (done<todo) {
    size_t offset=0;
    buf = fifo->buffer_pool_alloc (fifo);
    buf->decoder_flags = decoder_flags;
    if (done==0) {
      memcpy (buf->content, op, op_size);
      offset=op_size;
      buf->decoder_flags = buf->decoder_flags | BUF_FLAG_FRAME_START;
    }

    if (done+buf->max_size-offset < todo) {
      memcpy (buf->content+offset, op->packet+done, buf->max_size-offset);
      buf->size = buf->max_size;
      done=done+buf->max_size-offset;
    } else {
      memcpy (buf->content+offset , op->packet+done, todo-done);
      buf->size = todo-done+offset;
      done=todo;
      buf->decoder_flags = buf->decoder_flags | BUF_FLAG_FRAME_END;
    }

    buf->pts = pts;
    if( this->input->get_length (this->input) )
      buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                       65535 / this->input->get_length (this->input) );
    buf->extra_info->input_time = buf->pts / 90 ;
    buf->type       = this->si[stream_num]->buf_types;

    fifo->put (fifo, buf);
  }
}
#endif

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts (demux_ogg_t *this, int64_t pts, int video, int preview) {
  int64_t diff;

  llprintf(DEBUG_PTS, "new pts %" PRId64 " found in stream\n",pts);

  diff = pts - this->last_pts[video];

  if (!preview && (pts>=0) &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "diff=%" PRId64 " (pts=%" PRId64 ", last_pts=%" PRId64 ")\n", diff, pts, this->last_pts[video]);

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if (!preview && (pts>=0) )
    this->last_pts[video] = pts;

  /* use pts for bitrate measurement */

  /*compute avg_bitrate if time_length isn't set*/
  if ((pts>180000) && !(this->time_length)) {
    this->avg_bitrate = this->input->get_current_pos (this->input) * 8 * 90000/ pts;

    if (this->avg_bitrate<1)
      this->avg_bitrate = 1;

  }
}

static void ogg_handle_event (demux_ogg_t *this) {
  xine_event_t *event;

  while ((event = xine_event_get(this->event_queue))) {
    switch(event->type) {
    case XINE_EVENT_INPUT_NEXT:
      {
        if (this->chapter_info) {
          int c_chap = this->chapter_info->current_chapter;
          if (c_chap+1 < this->chapter_info->max_chapter) {
            int start_time = this->chapter_info->entries[c_chap+1].start_pts / 90;
            this->demux_plugin.seek((demux_plugin_t *)this, 0, start_time, 1);
          }
        }
      }
      break;
    case XINE_EVENT_INPUT_PREVIOUS:
      {
        if (this->chapter_info) {
          int c_chap = this->chapter_info->current_chapter;
          if (c_chap >= 1) {
            int start_time = this->chapter_info->entries[c_chap-1].start_pts / 90;
            this->demux_plugin.seek((demux_plugin_t *)this, 0, start_time, 1);
          }
        }
      }
      break;
    }
    xine_event_free(event);
  }
  return;
}


#define OGG_META(TAG,APPEND) { #TAG"=", XINE_META_INFO_##TAG, APPEND }
#define OGG_META_L(TAG,APPEND,META) { #TAG"=", XINE_META_INFO_##META, APPEND }
static const struct ogg_meta {
  char tag[16];
  int meta;
  int append;
} metadata[] = {
  OGG_META   (ALBUM,       0),
  OGG_META   (ARTIST,      0),
  OGG_META   (PUBLISHER,   0),
  OGG_META   (COPYRIGHT,   0),
  OGG_META   (DISCNUMBER,  0),
  OGG_META   (LICENSE,     0),
  OGG_META   (TITLE,       0),
  OGG_META_L (TRACKNUMBER, 0, TRACK_NUMBER),
  OGG_META   (COMPOSER,    1),
  OGG_META   (ARRANGER,    1),
  OGG_META   (LYRICIST,    1),
  OGG_META   (AUTHOR,      1),
  OGG_META   (CONDUCTOR,   1),
  OGG_META   (PERFORMER,   1),
  OGG_META   (ENSEMBLE,    1),
  OGG_META   (OPUS,        0),
  OGG_META   (PART,        0),
  OGG_META   (PARTNUMBER,  0),
  OGG_META   (GENRE,       1),
  OGG_META_L (DATE,        1, YEAR), /* hmm... */
  OGG_META   (LOCATION,    0),
  OGG_META   (COMMENT,     0),
};

#if 0
/* ensure that those marked "append" are cleared */
/* FIXME: is this useful? Should they be cleared on first write? */
static void prepare_read_comments (demux_ogg_t *this)
{
  int i;

  for (i = 0; i < sizeof (metadata) / sizeof (struct ogg_meta); ++i)
    if (metadata[i].append) {
      free (this->meta[metadata[i].meta]);
      this->meta[metadata[i].meta] = NULL;
    }
}
#endif

static int read_comments (demux_ogg_t *this, const char *comment)
{
  int i;

  for (i = 0; i < sizeof (metadata) / sizeof (struct ogg_meta); ++i) {
    size_t ml = strlen (metadata[i].tag);
    if (!strncasecmp (metadata[i].tag, comment, ml) && comment[ml]) {
      if (metadata[i].append && this->meta[metadata[i].meta]) {
        char *newstr;
        if (asprintf (&newstr, "%s\n%s", this->meta[metadata[i].meta], comment + ml) >= 0) {
          free (this->meta[metadata[i].meta]);
          this->meta[metadata[i].meta] = newstr;
        }
      }
      else {
        free (this->meta[metadata[i].meta]);
        this->meta[metadata[i].meta] = strdup (comment + ml);
      }
      _x_meta_info_set_utf8(this->stream, metadata[i].meta, this->meta[metadata[i].meta]);
      return 1;
    }
  }
  return 0;
}

/*
 * utility function to read a LANGUAGE= line from the user_comments,
 * to label audio and spu streams
 * utility function to read CHAPTER*=, TITLE= etc. from the user_comments,
 * to name (parts of) the stream
 */
static void read_language_comment (demux_ogg_t *this, ogg_packet *op, int stream_num) {
#ifdef HAVE_VORBIS
  char           **ptr;
  char           *comment;
  vorbis_comment vc;
  vorbis_info    vi;

  vorbis_comment_init(&vc);
  vorbis_info_init(&vi);

  /* this is necessary to make libvorbis accept this vorbis_info*/
  vi.rate=1;

  if ( vorbis_synthesis_headerin(&vi, &vc, op) >= 0) {
    ptr=vc.user_comments;
    while(*ptr) {
      comment=*ptr++;
      if ( !strncasecmp ("LANGUAGE=", comment, 9) ) {
        this->si[stream_num]->language = strdup (comment + strlen ("LANGUAGE=") );
      }
      else
        read_comments (this, comment);
    }
  }
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
#endif
}

/*
 * utility function to read CHAPTER*= from the user_comments,
 * to name parts of the stream
 */
static void read_chapter_comment (demux_ogg_t *this, ogg_packet *op) {
#ifdef HAVE_VORBIS
  char           **ptr;
  char           *comment;
  vorbis_comment vc;
  vorbis_info    vi;

  vorbis_comment_init(&vc);
  vorbis_info_init(&vi);

  /* this is necessary to make libvorbis accept this vorbis_info*/
  vi.rate=1;

  if ( vorbis_synthesis_headerin(&vi, &vc, op) >= 0) {
    char *chapter_time = 0;
    char *chapter_name = 0;
    int   chapter_no = 0;

    ptr=vc.user_comments;

    while(*ptr) {
      comment=*ptr++;
      if (read_comments (this, comment))
        continue;

      if ( !chapter_time && strlen(comment) == 22 &&
          !strncasecmp ("CHAPTER" , comment, 7) &&
          isdigit(*(comment+7)) && isdigit(*(comment+8)) &&
          (*(comment+9) == '=')) {

        chapter_time = strdup(comment+10);
        chapter_no   = strtol(comment+7, NULL, 10);
      }
      if ( !chapter_name && !strncasecmp("CHAPTER", comment, 7) &&
          isdigit(*(comment+7)) && isdigit(*(comment+8)) &&
          !strncasecmp ("NAME=", comment+9, 5)) {

        if (strtol(comment+7,NULL,10) == chapter_no) {
          chapter_name = strdup(comment+14);
        }
      }
      if (chapter_time && chapter_name && chapter_no){
        int hour, min, sec, msec;

        lprintf("create chapter entry: no=%d name=%s time=%s\n", chapter_no, chapter_name, chapter_time);
        hour= strtol(chapter_time, NULL, 10);
        min = strtol(chapter_time+3, NULL, 10);
        sec = strtol(chapter_time+6, NULL, 10);
        msec = strtol(chapter_time+9, NULL, 10);
        lprintf("time: %d %d %d %d\n", hour, min,sec,msec);

        if (!this->chapter_info) {
          this->chapter_info = (chapter_info_t *)calloc(1, sizeof(chapter_info_t));
          this->chapter_info->current_chapter = -1;
        }
        this->chapter_info->max_chapter = chapter_no;
        this->chapter_info->entries = realloc( this->chapter_info->entries, chapter_no*sizeof(chapter_entry_t));
        this->chapter_info->entries[chapter_no-1].name = chapter_name;
        this->chapter_info->entries[chapter_no-1].start_pts = (msec + (1000.0 * sec) + (60000.0 * min) + (3600000.0 * hour))*90;

        free (chapter_time);
        chapter_no = 0;
        chapter_time = chapter_name = 0;
      }
    }
  }
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
#endif
}

/*
 * update the display of the title, if needed
 */
static void update_chapter_display (demux_ogg_t *this, int stream_num, ogg_packet *op) {
  int chapter = 0;
  int64_t pts = get_pts(this, stream_num, op->granulepos );

  while (chapter < this->chapter_info->max_chapter &&
    this->chapter_info->entries[chapter].start_pts < pts) {
    chapter++;
  }
  chapter--;

  if (chapter != this->chapter_info->current_chapter){
    xine_ui_data_t data = {
      .str = { 0, },
      .str_len = 0
    };
    xine_event_t uevent = {
      .type = XINE_EVENT_UI_SET_TITLE,
      .stream = this->stream,
      .data = &data,
      .data_length = sizeof(data)
    };

    this->chapter_info->current_chapter = chapter;

    if (chapter >= 0) {
      if (this->meta[XINE_META_INFO_TITLE]) {
        data.str_len = snprintf(data.str, sizeof(data.str), "%s / %s", this->meta[XINE_META_INFO_TITLE], this->chapter_info->entries[chapter].name);
      } else {
	strncpy(data.str, this->chapter_info->entries[chapter].name, sizeof(data.str)-1);
      }
    } else {
      strncpy(data.str, this->meta[XINE_META_INFO_TITLE], sizeof(data.str));
    }
    if ( data.str_len == 0 )
      data.str_len = strlen(data.str);

    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, data.str);
    lprintf("new TITLE: %s\n", data.str);

    xine_event_send(this->stream, &uevent);
  }
}

/*
 * utility function to pack one ogg_packet into a xine
 * buffer, fill out all needed fields
 * and send it to the right fifo
 */

static void send_ogg_buf (demux_ogg_t *this,
                          ogg_packet  *op,
                          int          stream_num,
                          uint32_t     decoder_flags) {

  int hdrlen;
  int normpos = 0;

  if( this->input->get_length (this->input) )
    normpos = (int)( (double) this->input->get_current_pos (this->input) *
                              65535 / this->input->get_length (this->input) );


  hdrlen = (*op->packet & PACKET_LEN_BITS01) >> 6;
  hdrlen |= (*op->packet & PACKET_LEN_BITS2) << 1;

  /* for Annodex files: the first packet after the AnxData info packet needs
   * to have its BOS flag set: we set it here */
  if (!this->si[stream_num]->delivered_bos) {
    op->b_o_s = 1;
    this->si[stream_num]->delivered_bos = 1;
  }

  if ( this->audio_fifo
       && (this->si[stream_num]->buf_types & 0xFF000000) == BUF_AUDIO_BASE) {
    uint8_t *data;
    int size;
    int64_t pts;

    if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      read_language_comment(this, op, stream_num);
    }

    if ((this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_AUDIO_SPEEX ||
        (this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_AUDIO_FLAC ||
        (this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_AUDIO_VORBIS) {
      data = op->packet;
      size = op->bytes;
    } else {
      data = op->packet+1+hdrlen;
      size = op->bytes-1-hdrlen;
    }
    llprintf(DEBUG_PACKETS, "audio data size %d\n", size);

    if ((op->granulepos != -1) || (this->si[stream_num]->header_granulepos != -1)) {
      pts = get_pts(this, stream_num, op->granulepos );
      check_newpts( this, pts, PTS_AUDIO, decoder_flags );
    } else
      pts = 0;

    llprintf(DEBUG_PACKETS,
             "audiostream %d op-gpos %" PRId64 " hdr-gpos %" PRId64 " pts %" PRId64 " \n",
             stream_num,
             op->granulepos,
             this->si[stream_num]->header_granulepos,
             pts);

    _x_demux_send_data(this->audio_fifo, data, size,
		       pts, this->si[stream_num]->buf_types, decoder_flags,
		       normpos,
		       pts / 90, this->time_length, 0);

#ifdef HAVE_THEORA
  } else if ((this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_VIDEO_THEORA) {

    int64_t pts;
    theora_info t_info;
    theora_comment t_comment;

    theora_info_init (&t_info);
    theora_comment_init (&t_comment);

    /*Lets see if this is an Header*/
    if ((theora_decode_header(&t_info, &t_comment, op))>=0) {
      decoder_flags=decoder_flags|BUF_FLAG_HEADER;
      lprintf ("found an header\n");
    }

    if ((op->granulepos != -1) || (this->si[stream_num]->header_granulepos != -1)) {
      pts = get_pts(this, stream_num, op->granulepos );
      check_newpts( this, pts, PTS_VIDEO, decoder_flags );
    } else
      pts = 0;

    llprintf(DEBUG_PACKETS,
             "theorastream %d op-gpos %" PRId64 " hdr-gpos %" PRId64 " pts %" PRId64 " \n",
             stream_num,
             op->granulepos,
             this->si[stream_num]->header_granulepos,
             pts);

    send_ogg_packet (this, this->video_fifo, op, pts, decoder_flags, stream_num);

    theora_comment_clear (&t_comment);
    theora_info_clear (&t_info);
#endif

  } else if ((this->si[stream_num]->buf_types & 0xFF000000) == BUF_VIDEO_BASE) {

    uint8_t *data;
    int size;
    int64_t pts;

    llprintf(DEBUG_VIDEO_PACKETS,
             "video buffer, type=%08x\n", this->si[stream_num]->buf_types);

    if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      read_chapter_comment(this, op);
    }else{
      data = op->packet+1+hdrlen;
      size = op->bytes-1-hdrlen;

      if ((op->granulepos != -1) || (this->si[stream_num]->header_granulepos != -1)) {
        pts = get_pts(this, stream_num, op->granulepos );
        check_newpts( this, pts, PTS_VIDEO, decoder_flags );
      } else
        pts = 0;

      llprintf(DEBUG_VIDEO_PACKETS,
               "videostream %d op-gpos %" PRId64 " hdr-gpos %" PRId64 " pts %" PRId64 " \n",
               stream_num,
               op->granulepos,
               this->si[stream_num]->header_granulepos,
               pts);

      _x_demux_send_data(this->video_fifo, data, size,
                         pts, this->si[stream_num]->buf_types, decoder_flags,
                         normpos,
                         pts / 90, this->time_length, 0);

      if (this->chapter_info && op->granulepos != -1) {
        update_chapter_display(this, stream_num, op);
      }
    }
  } else if ((this->si[stream_num]->buf_types & 0xFFFF0000) == BUF_SPU_CMML) {
    buf_element_t *buf;
    uint32_t *val;
    char *str;

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    buf->type = this->si[stream_num]->buf_types;

    buf->pts = get_pts (this, stream_num, op->granulepos);

    val = (uint32_t * )buf->content;
    str = (char *)val;

    memcpy(str, op->packet, op->bytes);
    str[op->bytes] = '\0';

    buf->size = 12 + op->bytes + 1;

    lprintf ("CMML stream %d (bytes=%ld): PTS %"PRId64": %s\n",
             stream_num, op->bytes, buf->pts, str);

    this->video_fifo->put (this->video_fifo, buf);
  } else if ((this->si[stream_num]->buf_types & 0xFF000000) == BUF_SPU_BASE) {

    buf_element_t *buf;
    int i;
    char *subtitle,*str;
    int lenbytes;
    int start,end;
    uint32_t *val;

    for (i = 0, lenbytes = 0; i < hdrlen; i++) {
      lenbytes = lenbytes << 8;
      lenbytes += *((unsigned char *) op->packet + hdrlen - i);
    }

    if (op->packet[0] == PACKET_TYPE_HEADER ) {
      lprintf ("Textstream-header-packet\n");
    } else if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      lprintf ("Textstream-comment-packet\n");
      read_language_comment(this, op, stream_num);
    } else {
      subtitle = (char *)&op->packet[hdrlen + 1];

      if ((strlen(subtitle) > 1) || (*subtitle != ' ')) {
        start = op->granulepos;
        end = start+lenbytes;
        lprintf ("subtitlestream %d: %d -> %d :%s\n",stream_num,start,end,subtitle);
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

        buf->type = this->si[stream_num]->buf_types;
        buf->pts = 0;

        val = (uint32_t * )buf->content;
        *val++ = start;
        *val++ = end;
        str = (char *)val;

        memcpy (str, subtitle, 1+strlen(subtitle));

        this->video_fifo->put (this->video_fifo, buf);
      }
    }
  } else {
    lprintf("unknown stream type %x\n", this->si[stream_num]->buf_types);
  }
}

static void decode_vorbis_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
#ifdef HAVE_VORBIS
  vorbis_info       vi;
  vorbis_comment    vc;

  this->si[stream_num]->buf_types = BUF_AUDIO_VORBIS
    +this->num_audio_streams++;

  this->si[stream_num]->headers = 3;

  vorbis_info_init(&vi);
  vorbis_comment_init(&vc);
  if (vorbis_synthesis_headerin(&vi, &vc, op) >= 0) {

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, vi.bitrate_nominal);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, vi.rate);

    this->si[stream_num]->factor = 90000;
    this->si[stream_num]->quotient = vi.rate;

    if (vi.bitrate_nominal<1)
      this->avg_bitrate += 100000; /* assume 100 kbit */
    else
      this->avg_bitrate += vi.bitrate_nominal;

  } else {
    this->si[stream_num]->factor = 900;
    this->si[stream_num]->quotient = 441;

    this->si[stream_num]->headers = 0;
    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("ogg: vorbis audio track indicated but no vorbis stream header found.\n"));
  }
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
#endif
}

static void decode_speex_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
#ifdef HAVE_SPEEX
  void *st;
  SpeexMode *mode;
  SpeexHeader *header;

  this->si[stream_num]->buf_types = BUF_AUDIO_SPEEX
    +this->num_audio_streams++;

  this->si[stream_num]->headers = 1;

  header = speex_packet_to_header (op->packet, op->bytes);

  if (header) {
    int bitrate;
    mode = (SpeexMode *) speex_mode_list[header->mode];

    st = speex_decoder_init (mode);

    speex_decoder_ctl (st, SPEEX_GET_BITRATE, &bitrate);

    if (bitrate <= 1)
      bitrate = 16000; /* assume 16 kbit */

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, bitrate);

    this->si[stream_num]->factor = 90000;
    this->si[stream_num]->quotient = header->rate;

    this->avg_bitrate += bitrate;

    lprintf ("detected Speex stream,\trate %d\tbitrate %d\n", header->rate, bitrate);

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, header->rate);
    this->si[stream_num]->headers += header->extra_headers;
  }
#else
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "Speex stream detected, unable to play\n");

  this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
#endif
}

static void decode_video_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
  buf_element_t    *buf;
  xine_bmiheader    bih;
  int               channel;

#ifdef LOG
  int16_t          locbits_per_sample;
  int32_t          locsize, locdefault_len, locbuffersize;
  int64_t          locsamples_per_unit;
#endif
  uint32_t         locsubtype;
  int32_t          locwidth, locheight;
  int64_t          loctime_unit;

  /* read fourcc with machine endianness */
  locsubtype = *((uint32_t *)&op->packet[9]);

  /* everything else little endian */
  loctime_unit = _X_LE_64(&op->packet[17]);
#ifdef LOG
  locsize = _X_LE_32(&op->packet[13]);
  locsamples_per_unit = _X_LE_64(&op->packet[25]);
  locdefault_len = _X_LE_32(&op->packet[33]);
  locbuffersize = _X_LE_32(&op->packet[37]);
  locbits_per_sample = _X_LE_16(&op->packet[41]);
#endif
  locwidth = _X_LE_32(&op->packet[45]);
  locheight = _X_LE_32(&op->packet[49]);

  lprintf ("direct show filter created stream detected, hexdump:\n");
#ifdef LOG
  xine_hexdump (op->packet, op->bytes);
#endif

  channel = this->num_video_streams++;

  this->si[stream_num]->buf_types = _x_fourcc_to_buf_video (locsubtype);
  if( !this->si[stream_num]->buf_types )
  {
    this->si[stream_num]->buf_types = BUF_VIDEO_UNKNOWN;
    _x_report_video_fourcc (this->stream->xine, LOG_MODULE, locsubtype);
  }
  this->si[stream_num]->buf_types |= channel;
  this->si[stream_num]->headers = 0; /* header is sent below */

  lprintf ("subtype          %.4s\n", (char*)&locsubtype);
  lprintf ("time_unit        %" PRId64 "\n", loctime_unit);
  lprintf ("samples_per_unit %" PRId64 "\n", locsamples_per_unit);
  lprintf ("default_len      %d\n", locdefault_len);
  lprintf ("buffersize       %d\n", locbuffersize);
  lprintf ("bits_per_sample  %d\n", locbits_per_sample);
  lprintf ("width            %d\n", locwidth);
  lprintf ("height           %d\n", locheight);
  lprintf ("buf_type         %08x\n",this->si[stream_num]->buf_types);

  bih.biSize=sizeof(xine_bmiheader);
  bih.biWidth = locwidth;
  bih.biHeight= locheight;
  bih.biPlanes= 0;
  memcpy(&bih.biCompression, &locsubtype, 4);
  bih.biBitCount= 0;
  bih.biSizeImage=locwidth*locheight;
  bih.biXPelsPerMeter=1;
  bih.biYPelsPerMeter=1;
  bih.biClrUsed=0;
  bih.biClrImportant=0;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                       BUF_FLAG_FRAME_END;
  this->frame_duration = loctime_unit * 9 / 1000;
  this->si[stream_num]->factor = loctime_unit * 9;
  this->si[stream_num]->quotient = 1000;
  buf->decoder_info[0] = this->frame_duration;
  memcpy (buf->content, &bih, sizeof (xine_bmiheader));
  buf->size = sizeof (xine_bmiheader);
  buf->type = this->si[stream_num]->buf_types;

  /* video metadata */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC, locsubtype);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, locwidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, locheight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->frame_duration);

  this->avg_bitrate += 500000; /* FIXME */

  this->video_fifo->put (this->video_fifo, buf);
}

static void decode_audio_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {

  if (this->audio_fifo) {
    buf_element_t    *buf;
    int               codec;
    char              str[5];
    int               channel;

#ifdef LOG
    int16_t          locblockalign;
    int32_t          locsize, locdefault_len, locbuffersize;
    int64_t          loctime_unit;
#endif
    int16_t          locbits_per_sample, locchannels;
    int32_t          locavgbytespersec;
    int64_t          locsamples_per_unit;

#ifdef LOG
    locsize = _X_LE_32(&op->packet[13]);
    loctime_unit = _X_LE_64(&op->packet[17]);
    locbuffersize = _X_LE_32(&op->packet[37]);
    locdefault_len = _X_LE_32(&op->packet[33]);
    locblockalign = _X_LE_16(&op->packet[47]);
#endif
    locsamples_per_unit = _X_LE_64(&op->packet[25]);
    locbits_per_sample = _X_LE_16(&op->packet[41]);
    locchannels = _X_LE_16(&op->packet[45]);
    locavgbytespersec= _X_LE_32(&op->packet[49]);

    lprintf ("direct show filter created audio stream detected, hexdump:\n");
#ifdef LOG
    xine_hexdump (op->packet, op->bytes);
#endif

    memcpy(str, &op->packet[9], 4);
    str[4] = 0;
    codec = strtoul(str, NULL, 16);

    channel= this->num_audio_streams++;

    this->si[stream_num]->buf_types = _x_formattag_to_buf_audio(codec);
    if( this->si[stream_num]->buf_types ) {
      this->si[stream_num]->buf_types |= channel;
    } else {
      this->si[stream_num]->buf_types = BUF_AUDIO_UNKNOWN;
      _x_report_audio_format_tag (this->stream->xine, LOG_MODULE, codec);
      /*break;*/
    }

    lprintf ("subtype          0x%x\n", codec);
    lprintf ("time_unit        %" PRId64 "\n", loctime_unit);
    lprintf ("samples_per_unit %" PRId64 "\n", locsamples_per_unit);
    lprintf ("default_len      %d\n", locdefault_len);
    lprintf ("buffersize       %d\n", locbuffersize);
    lprintf ("bits_per_sample  %d\n", locbits_per_sample);
    lprintf ("channels         %d\n", locchannels);
    lprintf ("blockalign       %d\n", locblockalign);
    lprintf ("avgbytespersec   %d\n", locavgbytespersec);
    lprintf ("buf_type         %08x\n",this->si[stream_num]->buf_types);

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->si[stream_num]->buf_types;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = locsamples_per_unit;
    buf->decoder_info[2] = locbits_per_sample;
    buf->decoder_info[3] = locchannels;
    this->audio_fifo->put (this->audio_fifo, buf);

    this->si[stream_num]->headers = 0; /* header already sent */
    this->si[stream_num]->factor = 90000;
    this->si[stream_num]->quotient = locsamples_per_unit;

    this->avg_bitrate += locavgbytespersec*8;

    /* audio metadata */
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, codec);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, locchannels);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, locbits_per_sample);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, locsamples_per_unit);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, locavgbytespersec * 8);

  } else /* no audio_fifo there */
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
}

static void decode_dshow_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {

  lprintf ("older Direct Show filter-generated stream header detected. Hexdump:\n");
#ifdef LOG
  xine_hexdump (op->packet, op->bytes);
#endif

  this->si[stream_num]->headers = 0; /* header is sent below */

  if ( (_X_LE_32(&op->packet[96]) == 0x05589f80) && (op->bytes >= 184)) {

    buf_element_t    *buf;
    xine_bmiheader    bih;
    int               channel;
    uint32_t          fcc;

    lprintf ("seems to be a video stream.\n");

    channel = this->num_video_streams++;
    fcc = *(uint32_t*)(op->packet+68);
    lprintf ("fourcc %08x\n", fcc);

    this->si[stream_num]->buf_types = _x_fourcc_to_buf_video (fcc);
    if( !this->si[stream_num]->buf_types )
    {
      this->si[stream_num]->buf_types = BUF_VIDEO_UNKNOWN;
      _x_report_video_fourcc (this->stream->xine, LOG_MODULE, fcc);
    }
    this->si[stream_num]->buf_types |= channel;

    bih.biSize          = sizeof(xine_bmiheader);
    bih.biWidth         = _X_LE_32(&op->packet[176]);
    bih.biHeight        = _X_LE_32(&op->packet[180]);
    bih.biPlanes        = 0;
    memcpy (&bih.biCompression, op->packet+68, 4);
    bih.biBitCount      = _X_LE_16(&op->packet[182]);
    if (!bih.biBitCount)
      bih.biBitCount = 24; /* FIXME ? */
    bih.biSizeImage     = (bih.biBitCount>>3)*bih.biWidth*bih.biHeight;
    bih.biXPelsPerMeter = 1;
    bih.biYPelsPerMeter = 1;
    bih.biClrUsed       = 0;
    bih.biClrImportant  = 0;

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;
    this->frame_duration = (*(int64_t*)(op->packet+164)) * 9 / 1000;
    this->si[stream_num]->factor = (*(int64_t*)(op->packet+164)) * 9;
    this->si[stream_num]->quotient = 1000;

    buf->decoder_info[0] = this->frame_duration;
    memcpy (buf->content, &bih, sizeof (xine_bmiheader));
    buf->size = sizeof (xine_bmiheader);
    buf->type = this->si[stream_num]->buf_types;
    this->video_fifo->put (this->video_fifo, buf);

    lprintf ("subtype          %.4s\n", (char*)&fcc);
    lprintf ("buf_type         %08x\n", this->si[stream_num]->buf_types);
    lprintf ("video size       %d x %d\n", bih.biWidth, bih.biHeight);
    lprintf ("frame duration   %d\n", this->frame_duration);

    /* video metadata */
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, bih.biWidth);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, bih.biHeight);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->frame_duration);

    this->avg_bitrate += 500000; /* FIXME */

    this->ignore_keyframes = 1;

  } else if (_X_LE_32(&op->packet[96]) == 0x05589F81) {

#if 0
    /* FIXME: no test streams */

    buf_element_t    *buf;
    int               codec;
    char              str[5];
    int               channel;
    int               extra_size;

    extra_size         = *(int16_t*)(op->packet+140);
    format             = *(int16_t*)(op->packet+124);
    channels           = *(int16_t*)(op->packet+126);
    samplerate         = *(int32_t*)(op->packet+128);
    nAvgBytesPerSec    = *(int32_t*)(op->packet+132);
    nBlockAlign        = *(int16_t*)(op->packet+136);
    wBitsPerSample     = *(int16_t*)(op->packet+138);
    samplesize         = (sh_a->wf->wBitsPerSample+7)/8;
    cbSize             = extra_size;
    if(extra_size > 0)
      memcpy(wf+sizeof(WAVEFORMATEX),op->packet+142,extra_size);
#endif

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "FIXME, old audio format not handled\n");

    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;

  } else {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "old header detected but stream type is unknown\n");
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
  }
}

static void decode_text_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
  int channel=0;
  uint32_t *val;
  buf_element_t *buf;

  lprintf ("textstream detected.\n");
  this->si[stream_num]->headers = 2;
  channel = this->num_spu_streams++;
  this->si[stream_num]->buf_types = BUF_SPU_OGM | channel;

  /*send an empty spu to inform the video_decoder, that there is a stream*/
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = this->si[stream_num]->buf_types;
  buf->pts = 0;
  val = (uint32_t * )buf->content;
  *val++=0;
  *val++=0;
  *val++=0;
  this->video_fifo->put (this->video_fifo, buf);
}

static void decode_theora_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {

#ifdef HAVE_THEORA
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_ogg: Theorastreamsupport is highly alpha at the moment\n");

  if (theora_decode_header(&this->t_info, &this->t_comment, op) >= 0) {

    this->num_video_streams++;

    this->si[stream_num]->factor = (int64_t) 90000 * (int64_t) this->t_info.fps_denominator;

    if (!this->t_info.fps_numerator) {
      this->t_info.fps_numerator = 1;   /* FIXME: default value ? */
    }
    this->si[stream_num]->quotient = this->t_info.fps_numerator;

    this->frame_duration = ((int64_t) 90000*this->t_info.fps_denominator);
    this->frame_duration /= this->t_info.fps_numerator;

    this->si[stream_num]->granuleshift = intlog(this->t_info.keyframe_frequency_force-1);

    this->si[stream_num]->headers=3;
    this->si[stream_num]->buf_types = BUF_VIDEO_THEORA;

    _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "theora");
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->t_info.frame_width);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->t_info.frame_height);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->frame_duration);

    /*currently aspect_nominator and -denumerator are 0?*/
    if (this->t_info.aspect_denominator) {
      int64_t ratio = ((int64_t) this->t_info.aspect_numerator * 10000);

      ratio /= this->t_info.aspect_denominator;
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ratio);
    }

    lprintf ("decoded theora header \n");
    lprintf ("frameduration %d\n",this->frame_duration);
    lprintf ("w:%d h:%d \n",this->t_info.frame_width,this->t_info.frame_height);
    lprintf ("an:%d ad:%d \n",this->t_info.aspect_numerator,this->t_info.aspect_denominator);
  } else {
    /*Rejected stream*/
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "A theora header was rejected by libtheora\n");
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
    this->si[stream_num]->headers = 0; /* FIXME: don't know */
  }
#else
  this->si[stream_num]->buf_types = BUF_VIDEO_THEORA;
  this->num_video_streams++;
  this->unhandled_video_streams++;
  _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "theora");
#endif
}

static void decode_flac_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
  xine_flac_metadata_header header;
  xine_flac_streaminfo_block streaminfo = {};
  buf_element_t *buf;
  xine_waveformatex wave;

  /* Packet type */
  _x_assert(op->packet[0] == 0x7F);

  /* OggFLAC signature */
  _x_assert(_X_BE_32(&op->packet[1]) == ME_FOURCC('F', 'L', 'A', 'C'));

  /* Version: supported only 1.0 */
  _x_assert(op->packet[5] == 1); _x_assert(op->packet[6] == 0);

  /* Header count */
  this->si[stream_num]->headers = 0/*_X_BE_16(&op->packet[7]) +1*/;

  /* fLaC signature */
  _x_assert(_X_BE_32(&op->packet[9]) == ME_FOURCC('f', 'L', 'a', 'C'));

  _x_parse_flac_metadata_header(&op->packet[13], &header);

  switch ( header.blocktype ) {
  case FLAC_BLOCKTYPE_STREAMINFO:
    _x_assert(header.length == FLAC_STREAMINFO_SIZE);
    _x_parse_flac_streaminfo_block(&op->packet[17], &streaminfo);

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, streaminfo.samplerate);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, streaminfo.channels);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, streaminfo.bits_per_sample);

    break;
  }

  this->si[stream_num]->buf_types = BUF_AUDIO_FLAC
    +this->num_audio_streams++;

  this->si[stream_num]->factor = 90000;

  buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);

  buf->type = BUF_AUDIO_FLAC;
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;

  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = streaminfo.samplerate;
  buf->decoder_info[2] = streaminfo.bits_per_sample;
  buf->decoder_info[3] = streaminfo.channels;
  buf->size = sizeof(xine_waveformatex) + FLAC_STREAMINFO_SIZE;
  memcpy(buf->content+sizeof(xine_waveformatex), &op->packet[17], FLAC_STREAMINFO_SIZE);
  xine_hexdump(&op->packet[17], FLAC_STREAMINFO_SIZE);
  wave.cbSize = FLAC_STREAMINFO_SIZE;
  memcpy(buf->content, &wave, sizeof(xine_waveformatex));

  this->audio_fifo->put(this->audio_fifo, buf);

  /* Skip the Ogg framing info */
  op->bytes -= 9;
  op->packet += 9;
}

static void decode_annodex_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
  lprintf ("Annodex stream detected\n");
  this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
  this->si[stream_num]->headers = 1;
  this->si[stream_num]->header_granulepos = op->granulepos;
  _x_meta_info_set(this->stream, XINE_META_INFO_SYSTEMLAYER, "Annodex");
}

static void decode_anxdata_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
  int64_t granule_rate_n, granule_rate_d;
  uint32_t secondary_headers;
  const char *content_type = "";
  size_t content_type_length = 0;

  lprintf("AnxData stream detected\n");

  /* read granule rate */
  granule_rate_n = _X_LE_64(&op->packet[8]);
  granule_rate_d = _X_LE_64(&op->packet[16]);
  secondary_headers = _X_LE_32(&op->packet[24]);

  lprintf("granule_rate %" PRId64 "/%" PRId64 ", %d secondary headers\n",
      granule_rate_n, granule_rate_d, secondary_headers);

  /* read "Content-Type" MIME header */
  const char *startline = &op->packet[28];
  const char *endline;
  if ( strcmp(&op->packet[28], "Content-Type: ") == 0 &&
       (endline = strstr(startline, "\r\n")) ) {
    content_type = startline + sizeof("Content-Type: ");
    content_type_length = startline - endline;
  }

  lprintf("Content-Type: %s (length:%td)\n", content_type, content_type_length);

  /* how many header packets in the AnxData stream? */
  this->si[stream_num]->headers = secondary_headers + 1;
  this->si[stream_num]->hide_first_header = 1;

  /* set factor and quotient */
  this->si[stream_num]->factor = (int64_t) 90000 * granule_rate_d;
  this->si[stream_num]->quotient = granule_rate_n;

  lprintf("factor: %" PRId64 ", quotient: %" PRId64 "\n",
          this->si[stream_num]->factor, this->si[stream_num]->quotient);

  /* what type of stream are we dealing with? */
  if (!strncmp(content_type, "audio/x-vorbis", content_type_length)) {
#ifdef HAVE_VORBIS
    this->si[stream_num]->buf_types = BUF_AUDIO_VORBIS;
#else
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
#endif
    this->num_audio_streams++;
  } else if (!strncmp(content_type, "audio/x-speex", content_type_length)) {
    this->num_audio_streams++;
#ifdef HAVE_SPEEX
    this->si[stream_num]->buf_types = BUF_AUDIO_SPEEX;
#else
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
#endif
  } else if (!strncmp(content_type, "video/x-theora", content_type_length)) {
    this->num_video_streams++;
#ifdef HAVE_THEORA
    this->si[stream_num]->buf_types = BUF_VIDEO_THEORA;
#else
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
#endif
  } else if (!strncmp(content_type, "text/x-cmml", content_type_length)) {
    unsigned int channel = this->num_spu_streams++;
    this->si[stream_num]->headers = 0;
    this->si[stream_num]->buf_types = BUF_SPU_CMML | channel;
    this->si[stream_num]->granuleshift = 0;
  } else {
    this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
  }

}

static void decode_cmml_header (demux_ogg_t *this, const int stream_num, ogg_packet *op) {
    unsigned int channel = this->num_spu_streams++;
    this->si[stream_num]->headers = 0;
    this->si[stream_num]->buf_types = BUF_SPU_CMML | channel;

    this->si[stream_num]->factor = 90000 * _X_LE_64(&op->packet[20]);
    this->si[stream_num]->quotient = _X_LE_64(&op->packet[12]);
    this->si[stream_num]->granuleshift = (int)op->packet[28];
}

/*
 * interpret stream start packages, send headers
 */
static void send_header (demux_ogg_t *this) {

  int          stream_num = -1;
  int          cur_serno;
  int          done = 0;
  ogg_packet   op;
  xine_event_t ui_event;

  lprintf ("detecting stream types...\n");

  this->ignore_keyframes = 0;

  while (!done) {
    if (!read_ogg_packet(this) || !this->og.header || !this->og.body) {
      return;
    }
    /* now we've got at least one new page */

    cur_serno = ogg_page_serialno (&this->og);

    if (ogg_page_bos(&this->og)) {
      lprintf ("beginning of stream\n");
      lprintf ("serial number %d\n", cur_serno);

      if( this->num_streams == MAX_STREAMS ) {
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "demux_ogg: MAX_STREAMS exceeded, aborting.\n");
        this->status = DEMUX_FINISHED;
        return;
      }
      stream_num = new_stream_info(this, cur_serno);

    } else {
      stream_num = get_stream(this, cur_serno);
      if (stream_num == -1) {
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "demux_ogg: stream with no beginning!\n");
        this->status = DEMUX_FINISHED;
        return;
      }
    }

    ogg_stream_pagein(&this->si[stream_num]->oss, &this->og);

    while (ogg_stream_packetout(&this->si[stream_num]->oss, &op) == 1) {

      if (!this->si[stream_num]->buf_types) {

        /* detect buftype */
        if (!memcmp (&op.packet[1], "vorbis", 6)) {
          decode_vorbis_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[0], "Speex", 5)) {
          decode_speex_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[1], "video", 5)) {
          decode_video_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[1], "audio", 5)) {
          decode_audio_header(this, stream_num, &op);
        } else if (op.bytes >= 142
                   && !memcmp (&op.packet[1], "Direct Show Samples embedded in Ogg", 35) ) {
          decode_dshow_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[1], "text", 4)) {
          decode_text_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[1], "theora", 6)) {
          decode_theora_header(this, stream_num, &op);
	} else if (!memcmp (&op.packet[1], "FLAC", 4)) {
	  decode_flac_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[0], "Annodex", 7)) {
          decode_annodex_header(this, stream_num, &op);
        } else if (!memcmp (&op.packet[0], "AnxData", 7)) {
          decode_anxdata_header(this, stream_num, &op);
	} else if (!memcmp (&op.packet[0], "CMML", 4)) {
	  decode_cmml_header(this, stream_num, &op);
        } else {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "demux_ogg: unknown stream type (signature >%.8s<). hex dump of bos packet follows:\n",
                  op.packet);
          if(this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
            xine_hexdump (op.packet, op.bytes);

          this->si[stream_num]->buf_types = BUF_CONTROL_NOP;
        }
      }

      /* send preview buffer */
      if (this->si[stream_num]->headers > 0 ||
          op.packet[0] == PACKET_TYPE_COMMENT) {
        if (this->si[stream_num]->hide_first_header)
          this->si[stream_num]->hide_first_header = 0;
        else {
          lprintf ("sending preview buffer of stream type %08x\n",
              this->si[stream_num]->buf_types);

          send_ogg_buf (this, &op, stream_num, BUF_FLAG_HEADER);
          this->si[stream_num]->headers --;
        }
      }

      /* are we finished ? */
      if (!ogg_page_bos(&this->og)) {
        int i;
        done = 1;

        for (i=0; i<this->num_streams; i++) {
          if (this->si[i]->headers > 0)
            done = 0;

          llprintf(DEBUG_PREVIEWS,
                   "%d preview buffers left to send from stream %d\n",
                   this->si[i]->headers, i);
        }
      }
    }
  }

  ui_event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  ui_event.data_length = 0;
  xine_event_send(this->stream, &ui_event);

  /*get the streamlength*/
  get_stream_length (this);

}

static int demux_ogg_send_chunk (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  int stream_num;
  int cur_serno;

  ogg_packet op;

  ogg_handle_event(this);

  llprintf(DEBUG_PACKETS, "send package...\n");

  if (!read_ogg_packet(this)) {
    this->status = DEMUX_FINISHED;
    lprintf ("EOF\n");
    return this->status;
  }

  if (!this->og.header || !this->og.body) {
    this->status = DEMUX_FINISHED;
    lprintf ("EOF\n");
    return this->status;
  }

  /* now we've got one new page */

  cur_serno = ogg_page_serialno (&this->og);
  stream_num = get_stream(this, cur_serno);
  if (stream_num < 0) {
    lprintf ("error: unknown stream, serialnumber %d\n", cur_serno);

    if (!ogg_page_bos(&this->og)) {
      lprintf ("help, stream with no beginning!\n");
    }
    lprintf ("adding late stream with serial number %d (all content will be discarded)\n", cur_serno);

    if( this->num_streams == MAX_STREAMS ) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "demux_ogg: MAX_STREAMS exceeded, aborting.\n");
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    stream_num = new_stream_info(this, cur_serno);
  }

  ogg_stream_pagein(&this->si[stream_num]->oss, &this->og);

  if (ogg_page_bos(&this->og)) {
    lprintf ("beginning of stream: serial number %d - discard\n",
             ogg_page_serialno (&this->og));
    while (ogg_stream_packetout(&this->si[stream_num]->oss, &op) == 1) ;
    return this->status;
  }

  /*while keyframeseeking only process videostream*/
    if (!this->ignore_keyframes && this->keyframe_needed
      && ((this->si[stream_num]->buf_types & 0xFF000000) != BUF_VIDEO_BASE))
    return this->status;

  while (ogg_stream_packetout(&this->si[stream_num]->oss, &op) == 1) {
    /* printf("demux_ogg: packet: %.8s\n", op.packet); */
    /* printf("demux_ogg:   got a packet\n"); */

    if ((*op.packet & PACKET_TYPE_HEADER) &&
        (this->si[stream_num]->buf_types!=BUF_VIDEO_THEORA) && (this->si[stream_num]->buf_types!=BUF_AUDIO_SPEEX) && (this->si[stream_num]->buf_types!=BUF_AUDIO_FLAC)) {
      if (op.granulepos != -1) {
        this->si[stream_num]->header_granulepos = op.granulepos;
        lprintf ("header with granulepos, remembering granulepos\n");
      } else {
        lprintf ("header => discard\n");
      }
      continue;
    }

    /*discard granulepos-less packets and to early audiopackets*/
    if (this->si[stream_num]->resync) {
      if ((this->si[stream_num]->buf_types & 0xFF000000) == BUF_SPU_BASE) {
        /*never drop subtitles*/
        this->si[stream_num]->resync=0;
      } else if ((op.granulepos == -1) && (this->si[stream_num]->header_granulepos == -1)) {
        continue;
      } else {

        /*dump too early packets*/
        if ((get_pts(this,stream_num,op.granulepos)-this->start_pts) > -90000)
          this->si[stream_num]->resync=0;
        else
          continue;
      }
    }

    if (!this->ignore_keyframes && this->keyframe_needed) {
      lprintf ("keyframe needed... buf_type=%08x\n", this->si[stream_num]->buf_types);
      if (this->si[stream_num]->buf_types == BUF_VIDEO_THEORA) {
#ifdef HAVE_THEORA

        int keyframe_granule_shift;
        int64_t pframe=-1,iframe=-1;

	keyframe_granule_shift = this->si[stream_num]->granuleshift;

        if(op.granulepos>=0){
          iframe=op.granulepos>>keyframe_granule_shift;
          pframe=op.granulepos-(iframe<<keyframe_granule_shift);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                   "seeking keyframe i %" PRId64 " p %" PRId64 "\n", iframe, pframe);
          if (pframe!=0)
            continue;
        } else
          continue;
        this->keyframe_needed = 0;
        this->start_pts=get_pts(this,stream_num,op.granulepos);
#endif
      } else if ((this->si[stream_num]->buf_types & 0xFF000000) == BUF_VIDEO_BASE) {

      /*calculate the current pts*/
      if (op.granulepos!=-1) {
        this->start_pts=get_pts(this, stream_num, op.granulepos);
      } else if (this->start_pts!=-1)
        this->start_pts=this->start_pts+this->frame_duration;

      /*seek the keyframe*/
      if ((*op.packet == PACKET_IS_SYNCPOINT) && (this->start_pts!=-1))
        this->keyframe_needed = 0;
      else
        continue;

      } else if ((this->si[stream_num]->buf_types & 0xFF000000) == BUF_VIDEO_BASE) continue;
    }
    send_ogg_buf (this, &op, stream_num, 0);

    /*delete used header_granulepos*/
    if (op.granulepos == -1)
      this->si[stream_num]->header_granulepos = -1;

  }
  if (ogg_page_eos(&this->og)) {
    int i;
    int finished_streams = 0;

    lprintf("end of stream, serialnumber %d\n", cur_serno);
    this->si[stream_num]->delivered_eos = 1;

    /* check if all logical streams are finished */
    for (i = 0; i < this->num_streams; i++) {
      finished_streams += this->si[i]->delivered_eos;
    }

    /* if all streams are finished, perhaps a chained stream follows */
    if (finished_streams == this->num_streams) {
      /* delete current logical streams */
      for (i = 0; i < this->num_streams; i++) {
        ogg_stream_clear(&this->si[i]->oss);
        if (this->si[i]->language) {
          free (this->si[i]->language);
        }
        free (this->si[i]);
      }
      this->num_streams       = 0;
      this->num_audio_streams = 0;
      this->num_video_streams = 0;
      this->unhandled_video_streams = 0;
      this->num_spu_streams   = 0;
      this->avg_bitrate       = 1;

      /* try to read a chained stream */
      this->send_newpts = 1;
      this->last_pts[0] = 0;
      this->last_pts[1] = 0;

      /* send control buffer to avoid buffer leak */
      _x_demux_control_end(this->stream, 0);
      _x_demux_control_start(this->stream);
      send_header(this);
    }
  }

  return this->status;
}

static void demux_ogg_dispose (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int i;

  for (i=0; i<this->num_streams; i++) {
    ogg_stream_clear(&this->si[i]->oss);

    if (this->si[i]->language) {
      free (this->si[i]->language);
    }
    free(this->si[i]);
  }

  ogg_sync_clear(&this->oy);

#ifdef HAVE_THEORA
  theora_comment_clear (&this->t_comment);
  theora_info_clear (&this->t_info);
#endif

  if (this->chapter_info){
    free (this->chapter_info->entries);
    free (this->chapter_info);
  }
  for (i = 0; i < XINE_STREAM_INFO_MAX; ++i)
    free (this->meta[i]);

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  free (this);
}

static int demux_ogg_get_status (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return this->status;
}

static void demux_ogg_send_headers (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /*
   * send start buffers
   */

  this->last_pts[0]   = 0;
  this->last_pts[1]   = 0;

  /*
   * initialize ogg engine
   */
  ogg_sync_init(&this->oy);

  this->num_streams       = 0;
  this->num_audio_streams = 0;
  this->num_video_streams = 0;
  this->num_spu_streams   = 0;
  this->avg_bitrate       = 1;

  this->input->seek (this->input, 0, SEEK_SET);

  if (this->status == DEMUX_OK) {
    _x_demux_control_start(this->stream);
    send_header (this);
    lprintf ("headers sent, avg bitrate is %" PRId64 "\n", this->avg_bitrate);
  }

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO,
                       this->num_video_streams > 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED,
		     this->num_video_streams > this->unhandled_video_streams);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
                       this->num_audio_streams > 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_MAX_SPU_CHANNEL,
                       this->num_spu_streams);
}

static int demux_ogg_seek (demux_plugin_t *this_gen,
			   off_t start_pos, int start_time, int playing) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int i;
  start_time /= 1000;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );
  /*
   * seek to start position
   */

  if (INPUT_IS_SEEKABLE(this->input)) {

    this->keyframe_needed = (this->num_video_streams>0);

    if ( (!start_pos) && (start_time)) {
      if (this->time_length != -1) {
	/*do the seek via time*/
	int current_time=-1;
	off_t current_pos;
	current_pos=this->input->get_current_pos(this->input);

	/*try to find out the current time*/
	if (this->last_pts[PTS_VIDEO]) {
	  current_time=this->last_pts[PTS_VIDEO]/90000;
	} else if (this->last_pts[PTS_AUDIO]) {
	  current_time=this->last_pts[PTS_AUDIO]/90000;
	}

	/*fixme, the file could grow, do something
	 about this->time_length using get_lenght to verify, that the stream
	hasn` changed its length, otherwise no seek to "new" data is possible*/

	lprintf ("seek to time %d called\n",start_time);
	lprintf ("current time is %d\n",current_time);

	if (current_time > start_time) {
	  /*seek between beginning and current_pos*/

	  /*fixme - sometimes we seek backwards and during
	    keyframeseeking, we undo the seek*/

	  start_pos = start_time * current_pos
	  / current_time ;
	} else {
	  /*seek between current_pos and end*/
	  start_pos = current_pos +
	    ((start_time - current_time) *
	     ( this->input->get_length(this->input) - current_pos ) /
	     ( (this->time_length / 1000) - current_time)
	    );
	}

	lprintf ("current_pos is %" PRId64 "\n",current_pos);
	lprintf ("new_pos is %" PRId64 "\n",start_pos);

      } else {
	/*seek using avg_bitrate*/
	start_pos = start_time * this->avg_bitrate/8;
      }

      lprintf ("seeking to %d seconds => %" PRId64 " bytes\n",
	      start_time, start_pos);

    }

    ogg_sync_reset(&this->oy);

    for (i=0; i<this->num_streams; i++) {
      this->si[i]->header_granulepos = -1;
      ogg_stream_reset(&this->si[i]->oss);
    }

    /*some strange streams have no syncpoint flag set at the beginning*/
    if (start_pos == 0)
      this->keyframe_needed = 0;

    lprintf ("seek to %" PRId64 " called\n",start_pos);

    this->input->seek (this->input, start_pos, SEEK_SET);

  }

  /* fixme - this would be a nice position to do the following tasks
     1. adjust an ogg videostream to a keyframe
     2. compare the keyframe_pts with start_time. if the difference is to
        high (e.g. larger than max keyframe_intervall, do a new seek or
	continue reading
     3. adjust the audiostreams in such a way, that the
        difference is not to high.

     In short words, do all the cleanups necessary to continue playback
     without further actions
  */

  this->send_newpts     = 1;
  this->status          = DEMUX_OK;

  if( !playing ) {

    this->buf_flag_seek     = 0;

  } else {
    if (start_pos!=0) {
      this->buf_flag_seek = 1;
      /*each stream has to continue with a packet that has an
       granulepos*/
      for (i=0; i<this->num_streams; i++) {
	this->si[i]->resync = 1;
      }

      this->start_pts=-1;
    }

    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}

static int demux_ogg_get_stream_length (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  if (this->time_length==-1){
    if (this->avg_bitrate) {
      return (int)((int64_t)1000 * this->input->get_length (this->input) * 8 /
		   this->avg_bitrate);
    } else {
      return 0;
    }
  } else {
    return this->time_length;
  }
}

static uint32_t demux_ogg_get_capabilities(demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int cap_chapter = 0;

  if (this->chapter_info)
    cap_chapter = DEMUX_CAP_CHAPTERS;

  return DEMUX_CAP_SPULANG | DEMUX_CAP_AUDIOLANG | cap_chapter;
}

static int format_lang_string (demux_ogg_t * this, uint32_t buf_mask, uint32_t buf_type, int channel, char *str) {
  int stream_num;

  for (stream_num=0; stream_num<this->num_streams; stream_num++) {
    if ((this->si[stream_num]->buf_types & buf_mask) == buf_type) {
      if (this->si[stream_num]->language) {
        if (snprintf (str, XINE_LANG_MAX, "%s", this->si[stream_num]->language) >= XINE_LANG_MAX)
          /* the string got truncated */
          str[XINE_LANG_MAX - 2] = str[XINE_LANG_MAX - 3] = str[XINE_LANG_MAX - 4] = '.';
        /* TODO: provide long version in XINE_META_INFO_FULL_LANG */
      } else {
        snprintf(str, XINE_LANG_MAX, "channel %d",channel);
      }
      return DEMUX_OPTIONAL_SUCCESS;
    }
  }
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static int demux_ogg_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  char *str=(char *) data;
  int channel = *((int *)data);

  switch (data_type) {
  case DEMUX_OPTIONAL_DATA_SPULANG:
    lprintf ("DEMUX_OPTIONAL_DATA_SPULANG channel = %d\n",channel);
    if (channel==-1) {
      strcpy( str, "none");
      return DEMUX_OPTIONAL_SUCCESS;
    } else if ((channel>=0) && (channel<this->num_streams)) {
      return format_lang_string (this, 0xFFFFFFFF, BUF_SPU_OGM+channel, channel, str);
    }
    return DEMUX_OPTIONAL_UNSUPPORTED;
  case DEMUX_OPTIONAL_DATA_AUDIOLANG:
    lprintf ("DEMUX_OPTIONAL_DATA_AUDIOLANG channel = %d\n",channel);
    if (channel==-1) {
      return format_lang_string (this, 0xFF00001F, BUF_AUDIO_BASE, channel, str);
    } else if ((channel>=0) && (channel<this->num_streams)) {
      return format_lang_string (this, 0xFF00001F, BUF_AUDIO_BASE+channel, channel, str);
    }
    return DEMUX_OPTIONAL_UNSUPPORTED;
  default:
    return DEMUX_OPTIONAL_UNSUPPORTED;
  }
}

static int detect_ogg_content (int detection_method, demux_class_t *class_gen,
                               input_plugin_t *input) {

  switch (detection_method) {

    case METHOD_BY_CONTENT: {
      uint32_t header;

      if (_x_demux_read_header(input, &header, 4) != 4)
        return 0;

      return !!( header == ME_FOURCC('O', 'g', 'g', 'S') );
    }

    case METHOD_BY_MRL:
    case METHOD_EXPLICIT:
      return 1;

    default:
      return 0;
  }
}

static int detect_anx_content (int detection_method, demux_class_t *class_gen,
    input_plugin_t *input) {

  if (detect_ogg_content(detection_method, class_gen, input) == 0)
    return 0;

  switch (detection_method) {

#define ANNODEX_SIGNATURE_SEARCH 128

    case METHOD_BY_CONTENT: {
      uint8_t buf[ANNODEX_SIGNATURE_SEARCH];

      if (_x_demux_read_header(input, buf, ANNODEX_SIGNATURE_SEARCH) !=
          ANNODEX_SIGNATURE_SEARCH)
        return 0;

      /* scan for 'Annodex' signature in the first 64 bytes */
      return !!memmem(buf, ANNODEX_SIGNATURE_SEARCH,
		      "Annodex", sizeof("Annodex")-1);
    }

#undef ANNODEX_SIGNATURE_SEARCH

    case METHOD_BY_MRL:
    case METHOD_EXPLICIT:
      return 1;

    default:
      return 0;
  }
}

static demux_plugin_t *anx_open_plugin (demux_class_t *class_gen,
				        xine_stream_t *stream,
				        input_plugin_t *input) {

  demux_ogg_t *this;
  int i;

  if (detect_anx_content(stream->content_detection_method, class_gen, input) == 0)
    return NULL;

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = calloc(1, sizeof(demux_ogg_t));
  this->stream = stream;
  this->input  = input;

  /* the Annodex demuxer currently calls into exactly the same functions as
   * the Ogg demuxer, which seems to make this function a bit redundant, but
   * this design leaves us a bit more room to change an Annodex demuxer's
   * behaviour in the future if necessary */
  this->demux_plugin.send_headers      = demux_ogg_send_headers;
  this->demux_plugin.send_chunk        = demux_ogg_send_chunk;
  this->demux_plugin.seek              = demux_ogg_seek;
  this->demux_plugin.dispose           = demux_ogg_dispose;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ogg_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ogg_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

#ifdef HAVE_THEORA
  theora_info_init (&this->t_info);
  theora_comment_init (&this->t_comment);
#endif

  for (i = 0; i < XINE_STREAM_INFO_MAX; ++i)
    this->meta[i] = NULL;
  this->chapter_info = 0;
  this->event_queue = xine_event_new_queue (this->stream);

  return &this->demux_plugin;
}

static demux_plugin_t *ogg_open_plugin (demux_class_t *class_gen,
				        xine_stream_t *stream,
				        input_plugin_t *input) {

  demux_ogg_t *this;
  int i;

  if (detect_ogg_content(stream->content_detection_method, class_gen, input) == 0)
    return NULL;

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = calloc(1, sizeof(demux_ogg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ogg_send_headers;
  this->demux_plugin.send_chunk        = demux_ogg_send_chunk;
  this->demux_plugin.seek              = demux_ogg_seek;
  this->demux_plugin.dispose           = demux_ogg_dispose;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ogg_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ogg_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

#ifdef HAVE_THEORA
  theora_info_init (&this->t_info);
  theora_comment_init (&this->t_comment);
#endif

  this->chapter_info = 0;
  for (i = 0; i < XINE_STREAM_INFO_MAX; ++i)
    this->meta[i] = NULL;
  this->event_queue = xine_event_new_queue (this->stream);

  return &this->demux_plugin;
}

/*
 * Annodex demuxer class
 */
static void *anx_init_class (xine_t *xine, void *data) {
  demux_anx_class_t     *this;

  this = calloc(1, sizeof(demux_anx_class_t));

  this->demux_class.open_plugin     = anx_open_plugin;
  this->demux_class.description     = N_("Annodex demux plugin");
  this->demux_class.identifier      = "Annodex";
  this->demux_class.mimetypes       =
    "application/annodex: anx: Annodex media;"
    "application/x-annodex: anx: Annodex media;"
    "audio/annodex: axa: Annodex audio;"
    "audio/x-annodex: axa: Annodex audio;"
    "video/annodex: axv: Annodex video;"
    "video/x-annodex: axv: Annodex video;";
  this->demux_class.extensions      = "anx axa axv";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * ogg demuxer class
 */
static void *ogg_init_class (xine_t *xine, void *data) {
  demux_ogg_class_t     *this;

  this = calloc(1, sizeof(demux_ogg_class_t));

  this->demux_class.open_plugin     = ogg_open_plugin;
  this->demux_class.description     = N_("OGG demux plugin");
  this->demux_class.identifier      = "OGG";
  this->demux_class.mimetypes       =
    "application/ogg: ogx: Ogg Stream;"
    "application/x-ogm: ogx: Ogg Stream;"
    "application/x-ogm-audio: oga: Ogg Audio;"
    "application/x-ogm-video: ogv: Ogg Video;"
    "application/x-ogg: ogx: Ogg Stream;"
    "audio/ogg: oga: Ogg Audio;"
    "audio/x-ogg: oga: Ogg Audio;"
    "video/ogg: ogv: Ogg Video;"
    "video/x-ogg: ogv: Ogg Video;";
  this->demux_class.extensions      = "ogx ogv oga ogg spx ogm";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_anx = {
  20                       /* priority */
};

static const demuxer_info_t demux_info_ogg = {
  10                       /* priority */
};

extern const demuxer_info_t dec_info_vorbis;
extern const demuxer_info_t dec_info_speex;
extern const demuxer_info_t dec_info_theora;

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "ogg", XINE_VERSION_CODE, &demux_info_ogg, ogg_init_class },
  { PLUGIN_DEMUX, 27, "anx", XINE_VERSION_CODE, &demux_info_anx, anx_init_class },
#ifdef HAVE_VORBIS
  { PLUGIN_AUDIO_DECODER, 16, "vorbis", XINE_VERSION_CODE, &dec_info_vorbis, vorbis_init_plugin },
#endif
#ifdef HAVE_SPEEX
  { PLUGIN_AUDIO_DECODER, 16, "speex", XINE_VERSION_CODE, &dec_info_speex, speex_init_plugin },
#endif
#ifdef HAVE_THEORA
  { PLUGIN_VIDEO_DECODER, 19, "theora", XINE_VERSION_CODE, &dec_info_theora, theora_init_plugin },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
