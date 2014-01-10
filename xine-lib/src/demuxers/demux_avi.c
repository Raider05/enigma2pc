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
 */

/*
 * demultiplexer for avi streams
 *
 * part of the code is taken from
 * avilib (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 */

/*
 * Ian Goldberg <ian@cypherpunks.ca> modified this code so that it can
 * handle "streaming" AVI files.  By that I mean real seekable files, but
 * ones that are growing as we're displaying them.  Examples include
 * AVI's you're downloading, or ones you're writing in real time using
 * xawtv streamer, or whatever.  This latter is really useful, for
 * example,  for doing the PVR trick of starting the streamer to record
 * TV, starting to watch it ~10 minutes later, and skipping the
 * commercials to catch up to real time.  If you accidentally hit the
 * end of the stream, just hit your "back 15 seconds" key, and all is
 * good.
 *
 * Theory of operation: the video and audio indices have been separated
 * out of the main avi_t and avi_audio_t structures into separate
 * structures that can grow during playback.  We use the idx_grow_t
 * structure to keep track of the offset into the AVI file where we
 * expect to find the next A/V frame.  We periodically check if we can
 * read data from the file at that offset.  If we can, we append index
 * data for as many frames as we can read at the time.
 */

/*
 * OpenDML (AVI2.0) stuff was done by Tilmann Bitterberg
 * <transcode@tibit.org> in December 2003.
 * Transcode's and xine's avi code comes from the same source and
 * still has a very similar architecture, so it wasn't much effort to
 * port it from transcode to xine.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_avi"
#define LOG_VERBOSE
/*
#define DEBUG_ODML
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include "bswap.h"

/*
 * stolen from wine headers
 */

#define AVIIF_KEYFRAME      0x00000010L

#define MAX_AUDIO_STREAMS 8

#define NUM_PREVIEW_BUFFERS 10

typedef struct{
  off_t     pos;
  uint32_t  len;
  uint32_t  flags;
} video_index_entry_t;

typedef struct{
  off_t     pos;
  uint32_t  len;
  off_t     tot;
  uint32_t  block_no;       /* block number, used compute pts in audio VBR streams */
} audio_index_entry_t;

/* For parsing OpenDML AVI2.0 files */

#define AVI_INDEX_OF_INDEXES 0x00             /* when each entry in aIndex */
                                              /* array points to an index chunk */
#define AVI_INDEX_OF_CHUNKS  0x01             /* when each entry in aIndex */
                                              /* array points to a chunk in the file */
#define AVI_INDEX_IS_DATA    0x80             /* when each entry is aIndex is */
                                              /* really the data */
/* bIndexSubtype codes for INDEX_OF_CHUNKS */

#define AVI_INDEX_2FIELD     0x01             /* when fields within frames */
                                              /* are also indexed */
typedef struct _avisuperindex_entry {
    uint64_t qwOffset;           /* absolute file offset */
    uint32_t dwSize;             /* size of index chunk at this offset */
    uint32_t dwDuration;          /* time span in stream ticks */
} avisuperindex_entry;

typedef struct _avistdindex_entry {
    uint32_t dwOffset;           /* qwBaseOffset + this is absolute file offset */
    uint32_t dwSize;             /* bit 31 is set if this is NOT a keyframe */
} avistdindex_entry;

/* Standard index  */
typedef struct _avistdindex_chunk {
    char      fcc[4];                 /* ix## */
    uint32_t  dwSize;                 /* size of this chunk */
    uint16_t  wLongsPerEntry;         /* must be sizeof(aIndex[0])/sizeof(DWORD) */
    uint8_t   bIndexSubType;          /* must be 0 */
    uint8_t   bIndexType;             /* must be AVI_INDEX_OF_CHUNKS */
    uint32_t  nEntriesInUse;          /* */
    char      dwChunkId[4];           /* '##dc' or '##db' or '##wb' etc.. */
    uint64_t  qwBaseOffset;           /* all dwOffsets in aIndex array are relative to this */
    uint32_t  dwReserved3;            /* must be 0 */
    avistdindex_entry *aIndex;
} avistdindex_chunk;


/* Base Index Form 'indx' */
typedef struct _avisuperindex_chunk {
    char           fcc[4];
    uint32_t  dwSize;                 /* size of this chunk */
    uint16_t  wLongsPerEntry;         /* size of each entry in aIndex array (must be 8 for us) */
    uint8_t   bIndexSubType;          /* future use. must be 0 */
    uint8_t   bIndexType;             /* one of AVI_INDEX_* codes */
    uint32_t  nEntriesInUse;          /* index of first unused member in aIndex array */
    char      dwChunkId[4];           /* fcc of what is indexed */
    uint32_t  dwReserved[3];          /* meaning differs for each index type/subtype. */
                                           /* 0 if unused */
    avisuperindex_entry *aIndex;      /* where are the ix## chunks */
    avistdindex_chunk **stdindex;     /* the ix## chunks itself (array) */
} avisuperindex_chunk;


/* These next three are the video and audio structures that can grow
 * during the playback of a streaming file. */

typedef struct{
  uint32_t  video_frames;   /* Number of video frames */
  uint32_t  alloc_frames;   /* Allocated number of frames */
  video_index_entry_t   *vindex;
} video_index_t;

typedef struct{
  uint32_t  audio_chunks;   /* Chunks of audio data in the file */
  uint32_t  alloc_chunks;   /* Allocated number of chunks */
  audio_index_entry_t   *aindex;
} audio_index_t;

typedef struct{
  off_t  nexttagoffset;     /* The offset into the AVI file where we expect */
                            /* to find the next A/V frame */
} idx_grow_t;


typedef struct{
  uint32_t  dwInitialFrames;
  uint32_t  dwScale;
  uint32_t  dwRate;
  uint32_t  dwStart;
  uint32_t  dwSampleSize;

  uint32_t  block_no;

  uint32_t  audio_type;      /* BUF_AUDIO_xxx type */

  uint32_t  audio_strn;      /* Audio stream number */
  char      audio_tag[4];    /* Tag of audio data */
  uint32_t  audio_posc;      /* Audio position: chunk */
  uint32_t  audio_posb;      /* Audio position: byte within chunk */


  int       wavex_len;
  xine_waveformatex *wavex;

  audio_index_t  audio_idx;

  avisuperindex_chunk *audio_superindex;

  off_t   audio_tot;         /* Total number of audio bytes */

} avi_audio_t;

typedef struct{
  int32_t           width;           /* Width  of a video frame */
  int32_t           height;          /* Height of a video frame */
  uint32_t          dwInitialFrames;
  uint32_t          dwScale;
  uint32_t          dwRate;
  uint32_t          dwStart;
  double            fps;             /* Frames per second */

  uint32_t          compressor;      /* Type of compressor */
  uint32_t          video_strn;      /* Video stream number */
  char              video_tag[4];    /* Tag of video data */
  uint32_t          video_posf;      /* Number of next frame to be read
		                        (if index present) */
  uint32_t          video_posb;      /* Video position: byte within frame */

  avi_audio_t      *audio[MAX_AUDIO_STREAMS];
  int	            n_audio;

  uint32_t          video_type;      /* BUF_VIDEO_xxx type */

  uint32_t          n_idx;           /* number of index entries actually filled */
  uint32_t          max_idx;         /* number of index entries actually allocated */
  unsigned char    (*idx)[16];       /* index entries (AVI idx1 tag) */
  video_index_t     video_idx;
  xine_bmiheader   *bih;
  off_t             movi_start;
  off_t             movi_end;

  int               palette_count;
  palette_entry_t   palette[256];

  int is_opendml;       /* set to 1 if this is an odml file with multiple index chunks */
  avisuperindex_chunk *video_superindex;  /* index of indices */
  int total_frames;     /* total number of frames if dmlh is present */
} avi_t;

typedef struct demux_avi_s {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;
  input_plugin_t      *input;
  int                  status;

  uint32_t             video_step;
  uint32_t             AVI_errno;

  /* seeking args backup */
  int                  seek_start_time;
  off_t                seek_start_pos;

  avi_t               *avi;

  idx_grow_t           idx_grow;

  uint8_t              no_audio:1;

  uint8_t              streaming:1;
  uint8_t              has_index:1;

  uint8_t              seek_request:1;

  /* discontinuity detection (only at seek) */
  uint8_t              buf_flag_seek:1;
  uint8_t              send_newpts:1;
} demux_avi_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_avi_class_t;


/* The following variable indicates the kind of error */
#define AVI_ERR_SIZELIM      1     /* The write of the data would exceed
                                      the maximum size of the AVI file.
                                      This is more a warning than an error
                                      since the file may be closed safely */

#define AVI_ERR_OPEN         2     /* Error opening the AVI file - wrong path
                                      name or file nor readable/writable */

#define AVI_ERR_READ         3     /* Error reading from AVI File */

#define AVI_ERR_WRITE        4     /* Error writing to AVI File,
                                      disk full ??? */

#define AVI_ERR_WRITE_INDEX  5     /* Could not write index to AVI file
                                      during close, file may still be
                                      usable */

#define AVI_ERR_CLOSE        6     /* Could not write header to AVI file
                                      or not truncate the file during close,
                                      file is most probably corrupted */

#define AVI_ERR_NOT_PERM     7     /* Operation not permitted:
                                      trying to read from a file open
                                      for writing or vice versa */

#define AVI_ERR_NO_MEM       8     /* malloc failed */

#define AVI_ERR_NO_AVI       9     /* Not an AVI file */

#define AVI_ERR_NO_HDRL     10     /* AVI file has no header list,
                                      corrupted ??? */

#define AVI_ERR_NO_MOVI     11     /* AVI file has no MOVI list,
                                      corrupted ??? */

#define AVI_ERR_NO_VIDS     12     /* AVI file contains no video data */

#define AVI_ERR_NO_IDX      13     /* The file has been opened with
                                      getIndex==0, but an operation has been
                                      performed that needs an index */

#define AVI_ERR_BAD_SIZE    14     /* A chunk has an invalid size */

#define AVI_HEADER_UNKNOWN  -1
#define AVI_HEADER_AUDIO     0
#define AVI_HEADER_VIDEO     1
#define AVI_HEADER_SIZE      8

#define WRAP_THRESHOLD   90000
#define PTS_AUDIO 0
#define PTS_VIDEO 1

/* bit 31 denotes a keyframe */
static uint32_t odml_len (unsigned char *str)
{
   return _X_LE_32(str) & 0x7fffffff;
}

/* if bit 31 is 0, its a keyframe */
static uint32_t odml_key (unsigned char *str)
{
   return (_X_LE_32(str) & 0x80000000)?0:0x10;
}

static void check_newpts (demux_avi_t *this, int64_t pts, int video) {

  if (this->send_newpts) {

    lprintf ("sending newpts %" PRId64 " (video = %d)\n", pts, video);

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }

    this->send_newpts = 0;
  }
}

/* Append an index entry for a newly-found video frame */
static int video_index_append(avi_t *AVI, off_t pos, uint32_t len, uint32_t flags) {
  video_index_t *vit = &(AVI->video_idx);

  /* Make sure there's room */
  if (vit->video_frames == vit->alloc_frames) {
    long newalloc = vit->alloc_frames + 4096;
    video_index_entry_t *newindex =
      realloc(vit->vindex, newalloc * sizeof(video_index_entry_t));
    if (!newindex) return -1;
    vit->vindex = newindex;
    vit->alloc_frames = newalloc;
  }

  /* Set the new index entry */
  vit->vindex[vit->video_frames].pos = pos;
  vit->vindex[vit->video_frames].len = len;
  vit->vindex[vit->video_frames].flags = flags;
  vit->video_frames += 1;

  return 0;
}

/* Append an index entry for a newly-found audio frame */
static int audio_index_append(avi_t *AVI, int stream, off_t pos, uint32_t len,
                              off_t tot, uint32_t block_no) {
  audio_index_t *ait = &(AVI->audio[stream]->audio_idx);

  /* Make sure there's room */
  if (ait->audio_chunks == ait->alloc_chunks) {
    uint32_t newalloc = ait->alloc_chunks + 4096;
    audio_index_entry_t *newindex =
      realloc(ait->aindex, newalloc * sizeof(audio_index_entry_t));
    if (!newindex) return -1;
    ait->aindex = newindex;
    ait->alloc_chunks = newalloc;
  }

  /* Set the new index entry */
  ait->aindex[ait->audio_chunks].pos      = pos;
  ait->aindex[ait->audio_chunks].len      = len;
  ait->aindex[ait->audio_chunks].tot      = tot;
  ait->aindex[ait->audio_chunks].block_no = block_no;
  ait->audio_chunks += 1;

  return 0;
}

#define PAD_EVEN(x) ( ((x)+1) & ~1 )

static int64_t get_audio_pts (demux_avi_t *this, int track, uint32_t posc,
			      off_t postot, uint32_t posb) {

  avi_audio_t *at = this->avi->audio[track];

  lprintf("get_audio_pts: track=%d, posc=%d, postot=%" PRIdMAX ", posb=%d\n", track, posc, (intmax_t)postot, posb);

  if ((at->dwSampleSize == 0) && (at->dwScale > 1)) {
    /* variable bitrate */
    lprintf("get_audio_pts: VBR: nBlockAlign=%d, dwSampleSize=%d, dwScale=%d, dwRate=%d\n",
            at->wavex->nBlockAlign, at->dwSampleSize, at->dwScale, at->dwRate);
    return (int64_t)(90000.0 * (double)(posc + at->dwStart) *
      (double)at->dwScale / (double)at->dwRate);
  } else {
    /* constant bitrate */
    lprintf("get_audio_pts: CBR: nBlockAlign=%d, dwSampleSize=%d, dwScale=%d, dwRate=%d\n",
            at->wavex->nBlockAlign, at->dwSampleSize, at->dwScale, at->dwRate);
    if( at->wavex && at->wavex->nBlockAlign ) {
      return (int64_t)((double)((postot + posb) / (double)at->wavex->nBlockAlign + at->dwStart) *
        (double)at->dwScale / (double)at->dwRate * 90000.0);
    } else {
      return (int64_t)((double)((postot + posb) / (double)at->dwSampleSize + at->dwStart) *
        (double)at->dwScale / (double)at->dwRate * 90000.0);
    }
  }
}

static int64_t get_video_pts (demux_avi_t *this, off_t pos) {
  lprintf("get_video_pts: dwScale=%d, dwRate=%d, pos=%" PRIdMAX "\n",
         this->avi->dwScale, this->avi->dwRate, (intmax_t)pos);
  return (int64_t)(90000.0 * (double)(pos + this->avi->dwStart) *
    (double)this->avi->dwScale / (double)this->avi->dwRate);
}

/* Some handy stopper tests for idx_grow, below. */

/* Use this one to ensure the current video frame is in the index. */
static int video_pos_stopper(demux_avi_t *this, void *data){
  if (this->avi->video_posf >= this->avi->video_idx.video_frames) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure the current audio chunk is in the index. */
static int audio_pos_stopper(demux_avi_t *this, void *data) {
  avi_audio_t *AVI_A = (avi_audio_t *)data;

  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure that a video frame with the given position
 * is in the index. */
static int start_pos_stopper(demux_avi_t *this, void *data) {
  off_t start_pos = *(off_t *)data;
  int32_t maxframe = this->avi->video_idx.video_frames - 1;

  while( maxframe >= 0 && this->avi->video_idx.vindex[maxframe].pos >= start_pos ) {
    if ( this->avi->video_idx.vindex[maxframe].flags & AVIIF_KEYFRAME )
      return 1;
    maxframe--;
  }
  return -1;
}

/* Use this one to ensure that a video frame with the given timestamp
 * is in the index. */
static int start_time_stopper(demux_avi_t *this, void *data) {
  int64_t video_pts = *(int64_t *)data;
  int32_t maxframe = this->avi->video_idx.video_frames - 1;

  while( maxframe >= 0 && get_video_pts(this,maxframe) >= video_pts ) {
    if ( this->avi->video_idx.vindex[maxframe].flags & AVIIF_KEYFRAME )
      return 1;
    maxframe--;
  }

  return -1;
}

/* This is called periodically to check if there's more file now than
 * there was before.  If there is, we constuct the index for (just) the
 * new part, and append it to the index we've got so far.  We stop
 * slurping in the new part when stopper(this, stopdata) returns a
 * non-negative value, or there's no more file to read.  If we're taking
 * a long time slurping in the new part, use the on-screen display to
 * notify the user.  Returns -1 if EOF was reached, the non-negative
 * return value of stopper otherwise. */
static int idx_grow(demux_avi_t *this, int (*stopper)(demux_avi_t *, void *),
                    void *stopdata) {
  int           retval = -1;
  int           num_read = 0;
  uint8_t       data[AVI_HEADER_SIZE];
  uint8_t       data2[4];
  off_t         savepos = this->input->seek(this->input, 0, SEEK_CUR);
  off_t         chunk_pos;
  uint32_t      chunk_len;
  int           sent_event = 0;

  this->input->seek(this->input, this->idx_grow.nexttagoffset, SEEK_SET);
  chunk_pos = this->idx_grow.nexttagoffset;

  while (((retval = stopper(this, stopdata)) < 0) &&
         (!_x_action_pending(this->stream))) {
    int valid_chunk = 0;

    num_read += 1;

    if (num_read % 1000 == 0) {
      /* send event to frontend about index generation progress */

      xine_event_t             event;
      xine_progress_data_t     prg;
      off_t                    file_len;

      file_len = this->input->get_length (this->input);

      prg.description = _("Restoring index...");
      prg.percent = 100 * this->idx_grow.nexttagoffset / file_len;

      event.type = XINE_EVENT_PROGRESS;
      event.data = &prg;
      event.data_length = sizeof (xine_progress_data_t);

      xine_event_send (this->stream, &event);

      sent_event = 1;
    }

    if (this->input->read(this->input, data, AVI_HEADER_SIZE) != AVI_HEADER_SIZE) {
      lprintf("read failed, chunk_pos=%" PRIdMAX "\n", (intmax_t)chunk_pos);
      break;
    }

    /* Dive into RIFF and LIST entries */
    if(strncasecmp(data, "LIST", 4) == 0 ||
        strncasecmp(data, "RIFF", 4) == 0) {
      this->idx_grow.nexttagoffset =
        this->input->seek(this->input, 4,SEEK_CUR);
      continue;
    }

    chunk_len = _X_LE_32(data + 4);
    this->idx_grow.nexttagoffset += PAD_EVEN(chunk_len + AVI_HEADER_SIZE);


    /* Video chunk */
    if ((data[0] == this->avi->video_tag[0]) &&
        (data[1] == this->avi->video_tag[1])) {

      int flags = AVIIF_KEYFRAME;
      off_t pos = chunk_pos + AVI_HEADER_SIZE;
      uint32_t tmp;

      valid_chunk = 1;
      /* FIXME:
       *   UGLY hack to detect a keyframe parsing decoder data
       *   AVI chuncks doesn't provide this info and we need it during
       *   index building
       *   this hack comes from mplayer (aviheader.c)
       *   i've added XVID which looks like iso mpeg 4
       */

      if (this->input->read(this->input, data2, 4) != 4) {
        lprintf("read failed\n");
        break;
      }
      tmp = data2[3] | (data2[2]<<8) | (data2[1]<<16) | (data2[0]<<24);
      switch(this->avi->video_type) {
        case BUF_VIDEO_MSMPEG4_V1:
          this->input->read(this->input, data2, 4);
          tmp = data2[3] | (data2[2]<<8) | (data2[1]<<16) | (data2[0]<<24);
          tmp = tmp << 5;
        case BUF_VIDEO_MSMPEG4_V2:
        case BUF_VIDEO_MSMPEG4_V3:
          if (tmp & 0x40000000) flags = 0;
          break;
        case BUF_VIDEO_DIVX5:
        case BUF_VIDEO_MPEG4:
        case BUF_VIDEO_XVID:
          if (tmp == 0x000001B6) flags = 0;
          break;
      }

      if (video_index_append(this->avi, pos, chunk_len, flags) == -1) {
        /* If we're out of memory, we just don't grow the index, but
         * nothing really bad happens. */
      }
    } else {
      int i;

      /* Audio chunk */
      for(i = 0; i < this->avi->n_audio; ++i) {
        avi_audio_t *audio = this->avi->audio[i];

        if ((data[0] == audio->audio_tag[0]) &&
            (data[1] == audio->audio_tag[1])) {
          off_t pos = chunk_pos + AVI_HEADER_SIZE;

          valid_chunk = 1;
          /* VBR streams (hack from mplayer) */
          if (audio->wavex && audio->wavex->nBlockAlign) {
            audio->block_no += (chunk_len + audio->wavex->nBlockAlign - 1) /
                               audio->wavex->nBlockAlign;
          } else {
            audio->block_no += 1;
          }

          if (audio_index_append(this->avi, i, pos, chunk_len, audio->audio_tot,
                                 audio->block_no) == -1) {
            /* As above. */
          }
          this->avi->audio[i]->audio_tot += chunk_len;
        }
      }
    }
    if (!valid_chunk) {
      xine_log(this->stream->xine, XINE_LOG_MSG, _("demux_avi: invalid avi chunk \"%c%c%c%c\" at pos %" PRIdMAX "\n"), data[0], data[1], data[2], data[3], (intmax_t)chunk_pos);
    }
    chunk_pos = this->input->seek(this->input, this->idx_grow.nexttagoffset, SEEK_SET);
    if (chunk_pos != this->idx_grow.nexttagoffset) {
      lprintf("seek failed: %" PRIdMAX " != %" PRIdMAX "\n", (intmax_t)chunk_pos, (intmax_t)this->idx_grow.nexttagoffset);
      break;
    }
  }

  if (sent_event == 1) {
    /* send event to frontend about index generation progress */
    xine_event_t             event;
    xine_progress_data_t     prg;

    prg.description = _("Restoring index...");
    prg.percent = 100;

    event.type = XINE_EVENT_PROGRESS;
    event.data = &prg;
    event.data_length = sizeof (xine_progress_data_t);

    xine_event_send (this->stream, &event);
  }

  this->input->seek (this->input, savepos, SEEK_SET);

  if (retval < 0) retval = -1;
  return retval;
}

/* Fetch the current video index entry, growing the index if necessary. */
static video_index_entry_t *video_cur_index_entry(demux_avi_t *this) {
  avi_t *AVI = this->avi;

  if (AVI->video_posf >= AVI->video_idx.video_frames) {
    /* We don't have enough frames; see if the file's bigger yet. */
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      /* We still don't have enough frames.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI->video_idx.vindex[AVI->video_posf]);
}

/* Fetch the current audio index entry, growing the index if necessary. */
static audio_index_entry_t *audio_cur_index_entry(demux_avi_t *this,
    avi_audio_t *AVI_A) {

  lprintf("posc: %d, chunks: %d\n", AVI_A->audio_posc, AVI_A->audio_idx.audio_chunks);
  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    /* We don't have enough chunks; see if the file's bigger yet. */
    if (idx_grow(this, audio_pos_stopper, AVI_A) < 0) {
      /* We still don't have enough chunks.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI_A->audio_idx.aindex[AVI_A->audio_posc]);
}

static void AVI_close(avi_t *AVI){
  int i;

  if(AVI->idx) free(AVI->idx);
  if(AVI->video_idx.vindex) free(AVI->video_idx.vindex);
  if(AVI->bih) free(AVI->bih);

  for(i=0; i<AVI->n_audio; i++) {
    if(AVI->audio[i]->audio_idx.aindex) free(AVI->audio[i]->audio_idx.aindex);
    if(AVI->audio[i]->wavex) free(AVI->audio[i]->wavex);
    free(AVI->audio[i]);
  }
  free(AVI);
}

#define ERR_EXIT(x)	\
do {			\
   this->AVI_errno = x; \
   free (AVI);  \
   return 0;		\
} while(0)


static void reset_idx(demux_avi_t *this, avi_t *AVI) {
  int n;

  this->idx_grow.nexttagoffset = AVI->movi_start;
  this->has_index = 0;

  AVI->video_idx.video_frames = 0;
  for(n = 0; n < AVI->n_audio; n++) {
    AVI->audio[n]->audio_idx.audio_chunks = 0;
  }
}

static avi_t *XINE_MALLOC AVI_init(demux_avi_t *this) {

  avi_t *AVI;
  int i, j, idx_type;
  uint32_t n;
  uint8_t *hdrl_data;
  int hdrl_len = 0;
  off_t ioff;
  int lasttag = 0;
  int vids_strh_seen = 0;
  int vids_strf_seen = 0;
  int num_stream = 0;
  uint8_t data[256];
  int strf_size;

  /* Create avi_t structure */
  lprintf("start\n");

  AVI = (avi_t *) calloc(1, sizeof(avi_t));
  if(AVI==NULL) {
    this->AVI_errno = AVI_ERR_NO_MEM;
    return 0;
  }

  /* Read first 12 bytes and check that this is an AVI file */
  if (!this->streaming)
    this->input->seek(this->input, 0, SEEK_SET);

  if( this->input->read(this->input, data,12) != 12 ) ERR_EXIT(AVI_ERR_READ) ;

  if( !( (strncasecmp(data  ,"ON2 ",4) == 0 &&
          strncasecmp(data+8,"ON2f",4) == 0) ||
         (strncasecmp(data  ,"RIFF",4) == 0 &&
          strncasecmp(data+8,"AVI ",4) == 0) ) )
    ERR_EXIT(AVI_ERR_NO_AVI) ;
  /* Go through the AVI file and extract the header list,
     the start position of the 'movi' list and an optionally
     present idx1 tag */

  hdrl_data = NULL;

  while(1) {
    off_t next_chunk;

    /* Keep track of the last place we tried to read something. */
    this->idx_grow.nexttagoffset = this->input->get_current_pos(this->input);

    if (this->input->read(this->input, data,8) != 8 ) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "failed to read 8 bytes at pos %" PRIdMAX "\n", (intmax_t)this->idx_grow.nexttagoffset);
      break; /* We assume it's EOF */
    }

    n = _X_LE_32(data + 4);
    n = PAD_EVEN(n);
    next_chunk = this->idx_grow.nexttagoffset + 8 + n;

    lprintf("chunk: %c%c%c%c, size: %" PRId64 "\n",
            data[0], data[1], data[2], data[3], (int64_t)n);

    if (n >= 4 && strncasecmp(data,"LIST",4) == 0) {
      if( this->input->read(this->input, data,4) != 4 ) ERR_EXIT(AVI_ERR_READ);
      n -= 4;

      lprintf("  chunk: %c%c%c%c\n",
              data[0], data[1], data[2], data[3]);

      if(strncasecmp(data,"hdrl",4) == 0) {

        hdrl_len = n;
        hdrl_data = (unsigned char *) malloc(n);
        if(hdrl_data==0)
          ERR_EXIT(AVI_ERR_NO_MEM);
        if (this->input->read(this->input, hdrl_data,n) != n )
          ERR_EXIT(AVI_ERR_READ);

      } else if(strncasecmp(data,"movi",4) == 0)  {

        AVI->movi_start = this->input->get_current_pos(this->input);
        AVI->movi_end = AVI->movi_start + n - 1;

        if (this->streaming)
          /* stop reading here, we can't seek back */
          break;
      }
    } else if(strncasecmp(data,"idx1",4) == 0 ||
              strncasecmp(data,"iddx",4) == 0) {

      /* n must be a multiple of 16, but the reading does not
      break if this is not the case */

      AVI->n_idx = AVI->max_idx = n / 16;
      free(AVI->idx);  /* On the off chance there are multiple index chunks */
      AVI->idx = (unsigned char((*)[16])) malloc(n);
      if (AVI->idx == 0)
        ERR_EXIT(AVI_ERR_NO_MEM);

      if (this->input->read(this->input, (char *)AVI->idx, n) != n ) {
        xine_log (this->stream->xine, XINE_LOG_MSG,
		  _("demux_avi: avi index is broken\n"));
        free (AVI->idx);	/* Index is broken, reconstruct */
        AVI->idx = NULL;
        AVI->n_idx = AVI->max_idx = 0;
        break; /* EOF */
      }
    }
    if (next_chunk != this->input->seek(this->input, next_chunk, SEEK_SET)) {
      xine_log (this->stream->xine, XINE_LOG_MSG, _("demux_avi: failed to seek to the next chunk (pos %" PRIdMAX ")\n"), (intmax_t)next_chunk);
      break;  /* probably slow seek */
    }
  }

  /* Interpret the header list */

  for (i = 0; i < hdrl_len;) {
    const int old_i = i;

    /* List tags are completly ignored */
    lprintf("tag: %c%c%c%c\n",
            hdrl_data[i], hdrl_data[i+1], hdrl_data[i+2], hdrl_data[i+3]);


    if (strncasecmp(hdrl_data + i, "LIST", 4) == 0) {
      i += 12;
      continue;
    }

    n = _X_LE_32(hdrl_data + i + 4);
    n = PAD_EVEN(n);

    /* Interpret the tag and its args */

    if(strncasecmp(hdrl_data + i, "strh", 4) == 0) {
      i += 8;
      lprintf("tag: %c%c%c%c\n",
              hdrl_data[i], hdrl_data[i+1], hdrl_data[i+2], hdrl_data[i+3]);
      if(strncasecmp(hdrl_data + i, "vids", 4) == 0 && !vids_strh_seen) {

        AVI->compressor = *(uint32_t *) (hdrl_data + i + 4);
        AVI->dwInitialFrames = _X_LE_32(hdrl_data + i + 16);
        AVI->dwScale         = _X_LE_32(hdrl_data + i + 20);
        AVI->dwRate          = _X_LE_32(hdrl_data + i + 24);
        AVI->dwStart         = _X_LE_32(hdrl_data + i + 28);

        if(AVI->dwScale!=0)
          AVI->fps = (double)AVI->dwRate/(double)AVI->dwScale;

        this->video_step = (long) (90000.0 / AVI->fps);

        AVI->video_strn = num_stream;
        vids_strh_seen = 1;
        lprintf("video stream header, num_stream=%d\n", num_stream);
        lasttag = 1; /* vids */
        lprintf("dwScale=%d, dwRate=%d, dwInitialFrames=%d, dwStart=%d, num_stream=%d\n",
                AVI->dwScale, AVI->dwRate, AVI->dwInitialFrames, AVI->dwStart, num_stream);

      } else if (strncasecmp (hdrl_data+i,"auds",4) ==0 /* && ! auds_strh_seen*/) {
        if(AVI->n_audio < MAX_AUDIO_STREAMS) {
          avi_audio_t *a = (avi_audio_t *) calloc(1, sizeof(avi_audio_t));
          if(a==NULL) {
            this->AVI_errno = AVI_ERR_NO_MEM;
            return 0;
          }
          AVI->audio[AVI->n_audio] = a;

          a->audio_strn      = num_stream;
          a->dwInitialFrames = _X_LE_32(hdrl_data + i + 16);
          a->dwScale         = _X_LE_32(hdrl_data + i + 20);
          a->dwRate          = _X_LE_32(hdrl_data + i + 24);
          a->dwStart         = _X_LE_32(hdrl_data + i + 28);

          lprintf("dwScale=%d, dwRate=%d, dwInitialFrames=%d, dwStart=%d, num_stream=%d\n",
                  a->dwScale, a->dwRate, a->dwInitialFrames, a->dwStart, num_stream);

          a->dwSampleSize  = _X_LE_32(hdrl_data + i + 44);
          a->audio_tot     = 0;
          lprintf("audio stream header, num_stream=%d\n", num_stream);

          lasttag = 2; /* auds */
          AVI->n_audio++;
        }
      } else {
        /* unknown stream type */
        lasttag = 0;
      }
      num_stream++;

    } else if(strncasecmp(hdrl_data+i,"dmlh",4) == 0) {
      AVI->total_frames = _X_LE_32(hdrl_data+i+8);
#ifdef DEBUG_ODML
      lprintf( "AVI: real number of frames %d\n", AVI->total_frames);
#endif
      i += 8;

    } else if(strncasecmp(hdrl_data + i, "strf", 4) == 0) {
      i += 4;
      strf_size = _X_LE_32(hdrl_data + i);
      i += 4;
      if(lasttag == 1) {
        /* lprintf ("size : %d\n",sizeof(AVI->bih)); */
        AVI->bih = (xine_bmiheader *)
          malloc((n < sizeof(xine_bmiheader)) ? sizeof(xine_bmiheader) : n);
        if(AVI->bih == NULL) {
          this->AVI_errno = AVI_ERR_NO_MEM;
          return 0;
        }

        memcpy (AVI->bih, hdrl_data+i, n);
        _x_bmiheader_le2me( AVI->bih );

        /* stream_read(demuxer->stream,(char*) &avi_header.bih,MIN(size2,sizeof(avi_header.bih))); */
        AVI->width  = AVI->bih->biWidth;
        AVI->height = AVI->bih->biHeight;

        /*
          lprintf ("size : %d x %d (%d x %d)\n", AVI->width, AVI->height, AVI->bih.biWidth, AVI->bih.biHeight);
          lprintf ("  biCompression %d='%.4s'\n", AVI->bih.biCompression,
                 &AVI->bih.biCompression);
        */
        lprintf("video stream format\n");

        vids_strf_seen = 1;

        /* load the palette, if there is one */
        AVI->palette_count = AVI->bih->biClrUsed;

        lprintf ("palette_count: %d\n", AVI->palette_count);
        if (AVI->palette_count > 256) {
          lprintf ("number of colours exceeded 256 (%d)", AVI->palette_count);
          AVI->palette_count = 256;
        }
        if ((strf_size - sizeof(xine_bmiheader)) >= (AVI->palette_count * 4)) {
          /* load the palette from the end of the strf chunk */
          for (j = 0; j < AVI->palette_count; j++) {
            AVI->palette[j].b = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 0);
            AVI->palette[j].g = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 1);
            AVI->palette[j].r = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 2);
          }
        } else {
          /* generate a greyscale palette */
          AVI->palette_count = 256;
          for (j = 0; j < AVI->palette_count; j++) {
            AVI->palette[j].r = j;
            AVI->palette[j].g = j;
            AVI->palette[j].b = j;
          }
        }

      } else if(lasttag == 2) {
        xine_waveformatex *wavex;

        wavex = (xine_waveformatex *)malloc(n);
        if (!wavex) {
          this->AVI_errno = AVI_ERR_NO_MEM;
          return 0;
        }
        memcpy((void *)wavex, hdrl_data+i, n);
        _x_waveformatex_le2me( wavex );

        AVI->audio[AVI->n_audio-1]->wavex     = wavex;
        AVI->audio[AVI->n_audio-1]->wavex_len = n;
        lprintf("audio stream format\n");
      }

    } else if(strncasecmp(hdrl_data + i, "indx",4) == 0) {
      uint8_t             *a;
      int                  j;
      avisuperindex_chunk *superindex;

      if (n < sizeof (avisuperindex_chunk)) {
         lprintf("broken index !, dwSize=%d\n", n);
         i += 8 + n;
         continue;
      }

      superindex = (avisuperindex_chunk *) malloc (sizeof (avisuperindex_chunk));
      a = hdrl_data + i;
      memcpy (superindex->fcc, a, 4);             a += 4;
      superindex->dwSize = _X_LE_32(a);              a += 4;
      superindex->wLongsPerEntry = _X_LE_16(a);      a += 2;
      superindex->bIndexSubType = *a;             a += 1;
      superindex->bIndexType = *a;                a += 1;
      superindex->nEntriesInUse = _X_LE_32(a);       a += 4;
      memcpy (superindex->dwChunkId, a, 4);       a += 4;

#ifdef DEBUG_ODML
      printf("FOURCC \"%c%c%c%c\"\n",
             superindex->fcc[0], superindex->fcc[1],
             superindex->fcc[2], superindex->fcc[3]);
      printf("LEN \"%ld\"\n", (long)superindex->dwSize);
      printf("wLongsPerEntry \"%d\"\n", superindex->wLongsPerEntry);
      printf("bIndexSubType \"%d\"\n", superindex->bIndexSubType);
      printf("bIndexType \"%d\"\n", superindex->bIndexType);
      printf("nEntriesInUse \"%ld\"\n", (long)superindex->nEntriesInUse);
      printf("dwChunkId \"%c%c%c%c\"\n",
             superindex->dwChunkId[0], superindex->dwChunkId[1],
             superindex->dwChunkId[2], superindex->dwChunkId[3]);
      printf("--\n");
#endif
      /* 3 * reserved */
      a += 4; a += 4; a += 4;

      if (superindex->bIndexSubType != 0) {
         lprintf("Invalid Header, bIndexSubType != 0\n");
      }

      if (superindex->nEntriesInUse > n / sizeof (avisuperindex_entry))
      {
         lprintf("broken index !, dwSize=%d, entries=%d\n", n, superindex->nEntriesInUse);
         i += 8 + n;
         continue;
      }

      superindex->aIndex = malloc (superindex->nEntriesInUse * sizeof (avisuperindex_entry));
      /* position of ix## chunks */
      for (j = 0; j < superindex->nEntriesInUse; ++j) {
        superindex->aIndex[j].qwOffset = _X_LE_64 (a);   a += 8;
        superindex->aIndex[j].dwSize = _X_LE_32 (a);     a += 4;
        superindex->aIndex[j].dwDuration = _X_LE_32 (a); a += 4;
#ifdef DEBUG_ODML
        printf("[%d] 0x%llx 0x%x %u\n", j,
               (uint64_t)superindex->aIndex[j].qwOffset,
               (uint32_t)superindex->aIndex[j].dwSize,
               (uint32_t)superindex->aIndex[j].dwDuration);
#endif
      }

      this->has_index = 1;
      if (lasttag == 1) {
         /* V I D E O */
         AVI->video_superindex = superindex;
         AVI->is_opendml = 1;
      } else if (lasttag == 2) {
         /* A U D I O */
         AVI->audio[AVI->n_audio-1]->audio_superindex = superindex;
         AVI->is_opendml = 1;
      } else {
         xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	               "demux_avi: there should not be an index there, lasttag = %d\n", lasttag);
      }
      i += 8;
    } else if(strncasecmp(hdrl_data + i, "JUNK", 4) == 0) {
      i += 8;
      /* do not reset lasttag */
    } else if(strncasecmp(hdrl_data + i, "strd", 4) == 0) {
      /* additional header data */
      i += 8;
      /* do not reset lasttag */
    } else if(strncasecmp(hdrl_data + i, "strn", 4) == 0) {
      /* stream name */
      i += 8;
      /* do not reset lasttag */
    } else if(strncasecmp(hdrl_data + i, "vprp", 4) == 0) {
      /* video properties header*/
      i += 8;
      /* do not reset lasttag */
    } else {
      i += 8;
      lasttag = 0;
    }
    i += n;
    if (i <= old_i)
      ERR_EXIT(AVI_ERR_BAD_SIZE);
  }

  if( hdrl_data )
    free( hdrl_data );
  hdrl_data = NULL;

  /* somehow ffmpeg doesn't specify the number of frames here */
  /* if (!vids_strh_seen || !vids_strf_seen || AVI->video_frames==0) */
  if (!vids_strh_seen || !vids_strf_seen)
    ERR_EXIT(AVI_ERR_NO_VIDS);


  AVI->video_tag[0] = AVI->video_strn / 10 + '0';
  AVI->video_tag[1] = AVI->video_strn % 10 + '0';
  /* do not use the two following bytes */
  AVI->video_tag[2] = 'd';
  AVI->video_tag[3] = 'b';


  for(i = 0; i < AVI->n_audio; i++) {
    /* Audio tag is set to "99wb" if no audio present */
    if (!AVI->audio[i]->wavex->nChannels)
      AVI->audio[i]->audio_strn = 99;

    AVI->audio[i]->audio_tag[0] = AVI->audio[i]->audio_strn / 10 + '0';
    AVI->audio[i]->audio_tag[1] = AVI->audio[i]->audio_strn % 10 + '0';
    /* do not use the two following bytes */
    AVI->audio[i]->audio_tag[2] = 'w';
    AVI->audio[i]->audio_tag[3] = 'b';
  }

  idx_type = 0;

  if (!this->streaming) {
    this->input->seek(this->input, AVI->movi_start, SEEK_SET);

    /* if the file has an idx1, check if this is relative
       to the start of the file or to the start of the movi list */

    if(AVI->idx) {
      off_t    pos;
      uint32_t len;

      /* Search the first videoframe in the idx1 and look where
         it is in the file */

      for(i = 0; i < AVI->n_idx; i++)
        if( (AVI->idx[i][0] == AVI->video_tag[0]) &&
            (AVI->idx[i][1] == AVI->video_tag[1]))
          break;

#if 0
      /* try again for ##ix */
      if (i >= AVI->n_idx) {
        AVI->video_tag[2] = 'i';
        AVI->video_tag[3] = 'x';
      }

      for(i = 0; i < AVI->n_idx; i++)
        if( strncasecmp(AVI->idx[i], AVI->video_tag, 3) == 0 ) break;
#endif
      if (i >= AVI->n_idx) {
        ERR_EXIT(AVI_ERR_NO_VIDS);
      }

      pos = _X_LE_32(AVI->idx[i] + 8);
      len = _X_LE_32(AVI->idx[i] + 12);

      this->input->seek(this->input, pos, SEEK_SET);
      if(this->input->read(this->input, data, 8) != 8)
        ERR_EXIT(AVI_ERR_READ) ;

      if( (strncasecmp(data, AVI->idx[i], 4) == 0) && (_X_LE_32(data + 4) == len) ) {
        idx_type = 1; /* Index from start of file */
      } else {

        this->input->seek(this->input, pos + AVI->movi_start - 4, SEEK_SET);
        if(this->input->read(this->input, data, 8) != 8)
          ERR_EXIT(AVI_ERR_READ) ;
        if( strncasecmp(data,AVI->idx[i], 4) == 0 && _X_LE_32(data + 4) == len ) {
          idx_type = 2; /* Index from start of movi list */
        }
      }
      /* idx_type remains 0 if neither of the two tests above succeeds */
    }
  }

  lprintf("idx_type=%d, AVI->n_idx=%d\n", idx_type, AVI->n_idx);

  if (idx_type != 0 && !AVI->is_opendml) {
    /* Now generate the video index and audio index arrays from the
     * idx1 record. */
    this->has_index = 1;

    ioff = (idx_type == 1) ? AVI_HEADER_SIZE : AVI->movi_start + 4;

    for(i = 0; i < AVI->n_idx; i++) {

      if((AVI->idx[i][0] == AVI->video_tag[0]) &&
         (AVI->idx[i][1] == AVI->video_tag[1])) {
        off_t pos = _X_LE_32(AVI->idx[i] + 8) + ioff;
        uint32_t len = _X_LE_32(AVI->idx[i] + 12);
        uint32_t flags = _X_LE_32(AVI->idx[i] + 4);

        if (video_index_append(AVI, pos, len, flags) == -1) {
          ERR_EXIT(AVI_ERR_NO_MEM) ;
        }
      } else {
        for(n = 0; n < AVI->n_audio; n++) {
          avi_audio_t *audio = AVI->audio[n];

          if((AVI->idx[i][0] == audio->audio_tag[0]) &&
             (AVI->idx[i][1] == audio->audio_tag[1])) {
            off_t pos = _X_LE_32(AVI->idx[i] + 8) + ioff;
            uint32_t len = _X_LE_32(AVI->idx[i] + 12);

            /* VBR streams (hack from mplayer) */
            if (audio->wavex && audio->wavex->nBlockAlign) {
              audio->block_no += (len + audio->wavex->nBlockAlign - 1) /
                                 audio->wavex->nBlockAlign;
            } else {
              audio->block_no += 1;
            }

            if (audio_index_append(AVI, n, pos, len, audio->audio_tot,
                                   audio->block_no) == -1) {
              ERR_EXIT(AVI_ERR_NO_MEM) ;
            }
            AVI->audio[n]->audio_tot += len;
            break;
          }
        }
      }
    }
  } else if (AVI->is_opendml && !this->streaming) {
      uint64_t offset = 0;
      int hdrl_len = 4+4+2+1+1+4+4+8+4;
      char *en, *chunk_start;
      int k = 0, audtr = 0;
      uint32_t nrEntries = 0;
      int nvi, nai[MAX_AUDIO_STREAMS];

      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "demux_avi: This is an OpenDML stream\n");
      nvi = 0;
      for(audtr=0; audtr<AVI->n_audio; ++audtr) nai[audtr] = 0;

      /* ************************ */
      /* VIDEO */
      /* ************************ */

      lprintf("video track\n");
      if (AVI->video_superindex != NULL) {
        for (j=0; j<AVI->video_superindex->nEntriesInUse; j++) {

          /* read from file */
          chunk_start = en = malloc (AVI->video_superindex->aIndex[j].dwSize+hdrl_len);

          if (!chunk_start)
             ERR_EXIT(AVI_ERR_NO_MEM);

          if (this->input->seek(this->input, AVI->video_superindex->aIndex[j].qwOffset, SEEK_SET) == (off_t)-1) {
             lprintf("cannot seek to 0x%" PRIx64 "\n", AVI->video_superindex->aIndex[j].qwOffset);
             free(chunk_start);
             continue;
          }

          if (this->input->read(this->input, en, AVI->video_superindex->aIndex[j].dwSize+hdrl_len) <= 0) {
             lprintf("cannot read from offset 0x%" PRIx64 " %ld bytes; broken (incomplete) file?\n",
               AVI->video_superindex->aIndex[j].qwOffset,
             (unsigned long)AVI->video_superindex->aIndex[j].dwSize+hdrl_len);
             free(chunk_start);
             continue;
          }

          nrEntries = _X_LE_32(en + 12);
  #ifdef DEBUG_ODML
          printf("[%d:0] Video nrEntries %ld\n", j, (long)nrEntries);
  #endif
          offset = _X_LE_64(en + 20);

          /* skip header */
          en += hdrl_len;
          nvi += nrEntries;

          while (k < nvi) {
            off_t pos;
            uint32_t len;
            uint32_t flags;

            pos = offset + _X_LE_32(en); en += 4;
            len = odml_len(en);
            flags = odml_key(en); en += 4;
            video_index_append(AVI, pos, len, flags);

#ifdef DEBUG_ODML
            /*
            printf("[%d] POS 0x%llX len=%d key=%s offset (%llx) (%ld)\n", k,
            pos, len, flags?"yes":"no ", offset,
            (long)AVI->video_superindex->aIndex[j].dwSize);
            */
#endif

            k++;
          }

          free(chunk_start);
        }
      } else {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	               "demux_avi: Warning: the video super index is NULL\n");
      }


      /* ************************ */
      /* AUDIO  */
      /* ************************ */
      lprintf("audio tracks\n");
      for(audtr=0; audtr<AVI->n_audio; ++audtr) {
        avi_audio_t *audio = AVI->audio[audtr];

        k = 0;
        if (!audio->audio_superindex) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                   "demux_avi: Warning: cannot read audio index for track %d\n", audtr);
          continue;
        }
        for (j=0; j<audio->audio_superindex->nEntriesInUse; j++) {

          /* read from file */
          chunk_start = en = malloc (audio->audio_superindex->aIndex[j].dwSize+hdrl_len);

          if (!chunk_start)
            ERR_EXIT(AVI_ERR_NO_MEM);

          if (this->input->seek(this->input, audio->audio_superindex->aIndex[j].qwOffset, SEEK_SET) == (off_t)-1) {
            lprintf("cannot seek to 0x%" PRIx64 "\n", audio->audio_superindex->aIndex[j].qwOffset);
            free(chunk_start);
            continue;
          }

          if (this->input->read(this->input, en, audio->audio_superindex->aIndex[j].dwSize+hdrl_len) <= 0) {
            lprintf("cannot read from offset 0x%" PRIx64 "; broken (incomplete) file?\n",
              audio->audio_superindex->aIndex[j].qwOffset);
            free(chunk_start);
            continue;
          }

          nrEntries = _X_LE_32(en + 12);
#ifdef DEBUG_ODML
          /*printf("[%d:%d] Audio nrEntries %ld\n", j, audtr, nrEntries); */
#endif
          offset = _X_LE_64(en + 20);

          /* skip header */
          en += hdrl_len;
          nai[audtr] += nrEntries;

          while (k < nai[audtr]) {

            off_t pos;
            uint32_t len;

            pos = offset + _X_LE_32(en); en += 4;
            len = odml_len(en); en += 4;

            /* VBR streams (hack from mplayer) */
            if (audio->wavex && audio->wavex->nBlockAlign) {
              audio->block_no += (len + audio->wavex->nBlockAlign - 1) /
                                 audio->wavex->nBlockAlign;
            } else {
              audio->block_no += 1;
            }

            audio_index_append(AVI, audtr, pos, len, audio->audio_tot, audio->block_no);

#ifdef DEBUG_ODML
            /*
            printf("[%d:%d] POS 0x%llX len=%d offset (%llx) (%ld)\n", k, audtr,
            pos, (int)len,
            offset, (long)audio->audio_superindex->aIndex[j].dwSize);
            */
#endif
            audio->audio_tot += len;
            ++k;
          }
          free(chunk_start);
        }
      }
  }

  if (this->has_index) {
    /* check index validity, there must be an index for each video/audio stream */
    if (AVI->video_idx.video_frames == 0) {
      reset_idx(this, AVI);
    }
    for(n = 0; n < AVI->n_audio; n++) {
      if (AVI->audio[n]->audio_idx.audio_chunks == 0) {
	reset_idx(this, AVI);
      }
    }
  } else {
    /* We'll just dynamically grow the index as needed. */
    lprintf("no index\n");
    this->idx_grow.nexttagoffset = AVI->movi_start;
    this->has_index = 0;
  }

  /* Reposition the file */
  if (!this->streaming)
    this->input->seek(this->input, AVI->movi_start, SEEK_SET);
  AVI->video_posf = 0;
  AVI->video_posb = 0;

  lprintf("done, pos=%"PRId64", AVI->movi_start=%" PRIdMAX "\n", this->input->get_current_pos(this->input), (intmax_t)AVI->movi_start);
  return AVI;
}

static void AVI_seek_start(avi_t *AVI) {
  int i;

  AVI->video_posf = 0;
  AVI->video_posb = 0;

  for(i = 0; i < AVI->n_audio; i++) {
    AVI->audio[i]->audio_posc = 0;
    AVI->audio[i]->audio_posb = 0;
  }
}

static int AVI_read_audio(demux_avi_t *this, avi_audio_t *AVI_A, char *audbuf,
                          uint32_t bytes, int *buf_flags) {

  off_t pos;
  int nr, left, todo;
  audio_index_entry_t *aie = audio_cur_index_entry(this, AVI_A);

  if(!aie)  {
    this->AVI_errno = AVI_ERR_NO_IDX;
    return -1;
  }

  nr = 0; /* total number of bytes read */

  /* lprintf ("avi audio package len: %d\n", AVI_A->audio_index[AVI_A->audio_posc].len); */

  left = aie->len - AVI_A->audio_posb;
  while ((bytes > 0) && (left > 0)) {
    if (bytes < left)
      todo = bytes;
    else
      todo = left;
    pos = aie->pos + AVI_A->audio_posb;
    /* lprintf ("read audio from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, audbuf + nr, todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI_A->audio_posb += todo;
    left = aie->len - AVI_A->audio_posb;
  }

  if (left == 0) {
    AVI_A->audio_posc++;
    AVI_A->audio_posb = 0;
    *buf_flags = BUF_FLAG_FRAME_END;
  } else {
    *buf_flags = 0;
  }

  return nr;
}

static int AVI_read_video(demux_avi_t *this, avi_t *AVI, char *vidbuf,
                          uint32_t bytes, int *buf_flags) {

  off_t pos;
  int nr, left, todo;
  video_index_entry_t *vie = video_cur_index_entry(this);

  if (!vie) {
    this->AVI_errno = AVI_ERR_NO_IDX;
    return -1;
  }

  nr = 0; /* total number of bytes read */

  left = vie->len - AVI->video_posb;

  while ((bytes > 0) && (left > 0)) {
    if (bytes < left)
      todo = bytes;
    else
      todo = left;
    pos = vie->pos + AVI->video_posb;
    /* lprintf ("read video from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, vidbuf + nr, todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI->video_posb += todo;
    left = vie->len - AVI->video_posb;
  }

  if (left == 0) {
    AVI->video_posf++;
    AVI->video_posb = 0;
    *buf_flags = BUF_FLAG_FRAME_END;
  } else {
    *buf_flags = 0;
  }
  return nr;
}


static int demux_avi_next (demux_avi_t *this, int decoder_flags) {

  int            i;
  buf_element_t *buf = NULL;
  int64_t        audio_pts, video_pts;
  int            do_read_video = (this->avi->n_audio == 0);
  int            video_sent = 0;
  int            audio_sent = 0;

  lprintf("begin\n");

  /* Try to grow the index, in case more of the avi file has shown up
   * since we last checked.  If it's still too small, well then we're at
   * the end of the stream. */
  if (this->avi->video_idx.video_frames <= this->avi->video_posf) {
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      lprintf("end of stream\n");
    }
  }

  for (i = 0; i < this->avi->n_audio; i++) {
    avi_audio_t *audio = this->avi->audio[i];

    if (!this->no_audio &&
        (audio->audio_idx.audio_chunks <= audio->audio_posc)) {
      if (idx_grow(this, audio_pos_stopper, this->avi->audio[i]) < 0) {
        lprintf("end of stream\n");
      }
    }
  }

  video_pts = get_video_pts (this, this->avi->video_posf);

  for (i=0; i < this->avi->n_audio; i++) {
    avi_audio_t *audio = this->avi->audio[i];
    audio_index_entry_t *aie = audio_cur_index_entry(this, audio);

    /* The tests above mean aie should never be NULL, but just to be
     * safe. */
    if (!aie) {
      lprintf("aie == NULL\n");
      continue;
    }

    audio_pts =
      get_audio_pts (this, i, aie->block_no, aie->tot, audio->audio_posb);

    lprintf ("video_pts %" PRId64 " audio_pts %" PRId64 "\n", video_pts, audio_pts);

    if (!this->no_audio && (audio_pts < video_pts)) {

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

      /* read audio */

      buf->pts    = audio_pts;

      buf->size   = AVI_read_audio (this, audio, buf->mem, buf->max_size, &buf->decoder_flags);
      buf->decoder_flags |= decoder_flags;

      if (buf->size < 0) {
        buf->free_buffer (buf);
        lprintf("audio buf->size < 0\n");
      } else {

        buf->type = audio->audio_type | i;
        buf->extra_info->input_time = audio_pts / 90;
        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                           65535 / this->input->get_length (this->input) );

        check_newpts (this, buf->pts, PTS_AUDIO);
        this->audio_fifo->put (this->audio_fifo, buf);

        audio_sent++;
      }
    } else
      do_read_video = 1;
  }

  if (audio_sent == 0) {
    do_read_video = 1;
  }

  if (do_read_video) {

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    /* read video */

    buf->pts        = video_pts;
    buf->size       = AVI_read_video (this, this->avi, buf->mem, buf->max_size, &buf->decoder_flags);
    buf->type       = this->avi->video_type;

    buf->extra_info->input_time = video_pts / 90;

    if (this->has_index && this->avi->video_idx.video_frames > 2) {
      /* use video_frames-2 instead of video_frames-1 to fix problems with weird
         non-interleaved streams */
      buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                       65535 /
                                       this->avi->video_idx.vindex[this->avi->video_idx.video_frames - 2].pos);
    } else {
      if( this->input->get_length (this->input) )
        buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                         65535 / this->input->get_length (this->input) );
    }
    buf->extra_info->frame_number = this->avi->video_posf;
    buf->decoder_flags |= decoder_flags;

    if (buf->size < 0) {
      buf->free_buffer (buf);
      lprintf("video buf->size < 0\n");
    } else {

      /*
        lprintf ("adding buf %d to video fifo, decoder_info[0]: %d\n",
        buf, buf->decoder_info[0]);
      */

      check_newpts (this, buf->pts, PTS_VIDEO);
      this->video_fifo->put (this->video_fifo, buf);
      video_sent++;
    }
  }

  if (!audio_sent && !video_sent) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             "demux_avi: video and audio streams are ended\n");
    return 0;
  }
  return 1;
}

/*
 * Returns next chunk type
 * It's used in streaming mode
 */
static int get_chunk_header(demux_avi_t *this, uint32_t *len, int *audio_stream) {
  int    i;
  char   data[AVI_HEADER_SIZE];

  while (1) {
    if (this->input->read(this->input, data, AVI_HEADER_SIZE) != AVI_HEADER_SIZE)
      break;
    *len = _X_LE_32(data + 4);

    lprintf("header: %c%c%c%c, pos=%" PRIdMAX ", len=%u\n",
            data[0], data[1], data[2], data[3],
            (intmax_t)this->input->get_current_pos(this->input), *len);

    /* Dive into RIFF and LIST entries */
    if(strncasecmp(data, "LIST", 4) == 0 ||
       strncasecmp(data, "RIFF", 4) == 0) {
      this->idx_grow.nexttagoffset =
        this->input->seek(this->input, 4,SEEK_CUR);
      continue;
    }

    /*
     * Check if we got a tag ##db, ##dc or ##wb
     * only the 2 first bytes are reliable
     */
    if ((data[0] == this->avi->video_tag[0]) &&
        (data[1] == this->avi->video_tag[1])) {
      lprintf("video header: %c %c %c %c\n", data[0], data[1], data[2], data[3]);
      return AVI_HEADER_VIDEO;
    }

    for(i = 0; i < this->avi->n_audio; ++i) {
      avi_audio_t *audio = this->avi->audio[i];
      /*
       * only the 2 first bytes are reliable
       */
      if ((data[0] == audio->audio_tag[0]) &&
          (data[1] == audio->audio_tag[1])) {
        *audio_stream = i;
        audio->audio_tot += *len;
        lprintf("audio header: %c %c %c %c\n", data[0], data[1], data[2], data[3]);
        return AVI_HEADER_AUDIO;
      }
    }
    xine_log (this->stream->xine, XINE_LOG_MSG, _("demux_avi: invalid avi chunk \"%c%c%c%c\" at pos %" PRIdMAX "\n"), data[0], data[1], data[2], data[3], (intmax_t)this->input->get_current_pos(this->input));
    return AVI_HEADER_UNKNOWN;
  }
  /* unreachable code */
  return AVI_HEADER_UNKNOWN;
}

/*
 * Read next chunk
 * It's streaming version of demux_avi_next()
 * There is no seeking here.
 */
static int demux_avi_next_streaming (demux_avi_t *this, int decoder_flags) {

  buf_element_t *buf = NULL;
  int64_t        audio_pts, video_pts;
  off_t          current_pos;
  int            left;
  int            header, chunk_len = 0, audio_stream;
  avi_audio_t   *audio;

  current_pos = this->input->get_current_pos(this->input);
  lprintf("input_pos=%" PRIdMAX "\n", (intmax_t)current_pos);

  header = get_chunk_header(this, &chunk_len, &audio_stream);

  switch (header) {
    case AVI_HEADER_AUDIO:
      audio = this->avi->audio[audio_stream];
      left = chunk_len;

      lprintf("AVI_HEADER_AUDIO: chunk %d, len=%d\n", audio->audio_posc, chunk_len);

      while (left > 0) {
        audio_pts =
          get_audio_pts (this, audio_stream, audio->block_no,
                         audio->audio_tot - chunk_len, chunk_len - left);

        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

        /* read audio */
        buf->pts = audio_pts;
        lprintf("audio pts: %" PRId64 "\n", audio_pts);

        if (left > this->audio_fifo->buffer_pool_buf_size) {
          buf->size = this->audio_fifo->buffer_pool_buf_size;
          buf->decoder_flags = 0;
        } else {
          buf->size = left;
          buf->decoder_flags = BUF_FLAG_FRAME_END;
        }
        left -= buf->size;
        if (this->input->read(this->input, buf->mem, buf->size) != buf->size) {
          buf->free_buffer (buf);
          return 0;
        }
        buf->extra_info->input_time = audio_pts / 90;
        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                           65535 / this->input->get_length (this->input) );

        buf->type = audio->audio_type | audio_stream;

        this->audio_fifo->put (this->audio_fifo, buf);
      }
      audio->audio_posc++;

      /* VBR streams (hack from mplayer) */
      if (audio->wavex && audio->wavex->nBlockAlign) {
        audio->block_no += (chunk_len + audio->wavex->nBlockAlign - 1) / audio->wavex->nBlockAlign;
      } else {
        audio->block_no += 1;
      }

      break;


    case AVI_HEADER_VIDEO:
      left = chunk_len;
      lprintf("AVI_HEADER_VIDEO: chunk %d, len=%d\n", this->avi->video_posf, chunk_len);

      while (left > 0) {
        video_pts = get_video_pts (this, this->avi->video_posf);

        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

        /* read video */
        buf->pts = video_pts;
        lprintf("video pts: %" PRId64 "\n", video_pts);

        if (left > this->video_fifo->buffer_pool_buf_size) {
          buf->size = this->video_fifo->buffer_pool_buf_size;
          buf->decoder_flags = 0;
        } else {
          buf->size = left;
          buf->decoder_flags = BUF_FLAG_FRAME_END;
        }
        left -= buf->size;
        if (this->input->read(this->input, buf->mem, buf->size) != buf->size) {
          buf->free_buffer (buf);
          return 0;
        }

        buf->type = this->avi->video_type;
        buf->extra_info->input_time = video_pts / 90;
        buf->extra_info->input_normpos = 65535;
        buf->extra_info->frame_number = this->avi->video_posf;
        buf->decoder_flags |= decoder_flags;
        this->video_fifo->put (this->video_fifo, buf);
      }

      this->avi->video_posf++;
      break;

    case AVI_HEADER_UNKNOWN:
      current_pos = this->input->get_current_pos(this->input);
      if (this->input->seek(this->input, chunk_len, SEEK_CUR) != (current_pos + chunk_len)) {
        return 0;
      }
      break;
  }

  /* skip padding */
  this->input->seek (this->input,
    this->input->get_current_pos(this->input) & 1, SEEK_CUR);

  lprintf("done\n");
  return 1;
}

static int demux_avi_seek_internal (demux_avi_t *this);

static int demux_avi_send_chunk (demux_plugin_t *this_gen) {
  demux_avi_t   *this = (demux_avi_t *) this_gen;

  if (this->streaming) {
    if (!demux_avi_next_streaming (this, 0)) {
      this->status = DEMUX_FINISHED;
    }
  } else {
    if (this->seek_request) {
      this->seek_request = 0;
      demux_avi_seek_internal(this);
    }

    if (!demux_avi_next (this, 0)) {
      this->status = DEMUX_FINISHED;
    }
  }

  return this->status;
}

static void demux_avi_dispose (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi)
    AVI_close (this->avi);

  free(this);
}

static int demux_avi_get_status (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  return this->status;
}

static void demux_avi_send_headers (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;
  int i;

  lprintf("start\n");
  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->avi->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->avi->height);

  if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    for (i=0; i < this->avi->n_audio; i++)
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_avi: audio format[%d] = 0x%x\n",
	      i, this->avi->audio[i]->wavex->wFormatTag);
  }
  this->no_audio = 0;

  if (!this->avi->bih->biCompression)
    this->avi->video_type = BUF_VIDEO_RGB;
  else
  {
    this->avi->video_type = _x_fourcc_to_buf_video(this->avi->bih->biCompression);
    if (!this->avi->video_type)
      _x_report_video_fourcc (this->stream->xine, LOG_MODULE,
			      this->avi->bih->biCompression);
  }

  for(i=0; i < this->avi->n_audio; i++) {
    this->avi->audio[i]->audio_type = _x_formattag_to_buf_audio (this->avi->audio[i]->wavex->wFormatTag);

    /* special case time: An AVI file encoded with Xan video will have Xan
     * DPCM audio marked as PCM; hack around this */
    if (this->avi->video_type == BUF_VIDEO_XXAN) {
      this->avi->audio[i]->audio_type = BUF_AUDIO_XAN_DPCM;
      this->avi->audio[i]->dwRate = 11025; /* why this ??? */
    }

    if( !this->avi->audio[i]->audio_type ) {
      this->no_audio  = 1;
      this->avi->audio[i]->audio_type     = BUF_AUDIO_UNKNOWN;
      _x_report_audio_format_tag (this->stream->xine, LOG_MODULE,
				  this->avi->audio[i]->wavex->wFormatTag);
    } else
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_avi: audio type %s (wFormatTag 0x%x)\n",
               _x_buf_audio_name(this->avi->audio[i]->audio_type),
               (int)this->avi->audio[i]->wavex->wFormatTag);
  }

  /*
   * I don't know who should we trust more, if avi->compressor or bih->biCompression.
   * however, at least for this case (compressor: xvid biCompression: DIVX), the
   * xvid fourcc must prevail as it is used by ffmpeg to detect encoder bugs. [MF]
   */
  if( this->avi->video_type == BUF_VIDEO_MPEG4 &&
      _x_fourcc_to_buf_video(this->avi->compressor) == BUF_VIDEO_XVID ) {
    this->avi->bih->biCompression = this->avi->compressor;
    this->avi->video_type = BUF_VIDEO_XVID;
  }

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, !this->no_audio);
  _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, _x_buf_video_name(this->avi->video_type));

  if (this->avi->audio[0] && !this->no_audio)
    _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC, _x_buf_audio_name(this->avi->audio[0]->audio_type));

  /*
   * send start/header buffers
   */
  {
    buf_element_t  *buf;
    int             i;

    _x_demux_control_start (this->stream);

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    if (this->avi->bih->biSize > buf->max_size) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "demux_avi: private video decoder data length (%d) is greater than fifo buffer length (%d)\n",
               this->avi->bih->biSize, buf->max_size);
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return;
    }

    /* wait, before sending out the video header, one more special case hack:
     * if the video type is RGB, indicate that it is upside down with a
     * negative height */
    if (this->avi->video_type == BUF_VIDEO_RGB) {
      this->avi->bih->biHeight = -this->avi->bih->biHeight;
    }

    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = this->video_step;
    memcpy (buf->content, this->avi->bih, this->avi->bih->biSize);
    buf->size = this->avi->bih->biSize;

    if (this->avi->video_type) {
      this->avi->compressor = this->avi->bih->biCompression;
    } else {
      this->avi->video_type = _x_fourcc_to_buf_video(this->avi->compressor);
      if (!this->avi->video_type)
        _x_fourcc_to_buf_video(this->avi->bih->biCompression);
    }

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC,
                         this->avi->compressor);

    if (!this->avi->video_type) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_avi: unknown video codec '%.4s'\n",
               (char*)&this->avi->bih->biCompression);
      this->avi->video_type = BUF_VIDEO_UNKNOWN;
    }
    buf->type = this->avi->video_type;

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_avi: video codec is '%s'\n",
             _x_buf_video_name(buf->type));

    this->video_fifo->put (this->video_fifo, buf);

    /* send off the palette, if there is one */
    if (this->avi->palette_count) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = this->avi->palette_count;
      buf->decoder_info_ptr[2] = &this->avi->palette;
      buf->size = 0;
      buf->type = this->avi->video_type;
      this->video_fifo->put (this->video_fifo, buf);
    }

    if (this->audio_fifo) {
      for (i=0; i<this->avi->n_audio; i++) {
        avi_audio_t *a = this->avi->audio[i];
        uint32_t todo = a->wavex_len;
        uint32_t done = 0;

        while (todo) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          if (todo > buf->max_size) {
            buf->size = buf->max_size;
          } else {
            buf->size = todo;
          }
          todo -= buf->size;

          buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER;
          if (todo == 0)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          memcpy (buf->content, (uint8_t *)a->wavex + done, buf->size);
          buf->type = a->audio_type | i;
          buf->decoder_info[0] = 0;                         /* first package, containing wavex */
          buf->decoder_info[1] = a->wavex->nSamplesPerSec;  /* Audio Rate */
          buf->decoder_info[2] = a->wavex->wBitsPerSample;  /* Audio bits */
          buf->decoder_info[3] = a->wavex->nChannels;       /* Audio channels */
          this->audio_fifo->put (this->audio_fifo, buf);
          done += buf->size;
        }
      }

      if(this->avi->n_audio == 1)
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC,
                             this->avi->audio[0]->wavex->wFormatTag);
    }

    /*
     * send preview buffers
     */

    AVI_seek_start (this->avi);

    if (!this->streaming) {
      for (i=0; i<NUM_PREVIEW_BUFFERS; i++) {
        if (!demux_avi_next(this, BUF_FLAG_PREVIEW))
          break;
      }
    }
    lprintf("done\n");
  }
}

/*
 * Seeking can take a lot of time due to the index reconstruction.
 * To avoid to block the frontend thread during a seek, the real seek is done
 * by the demux_loop thread (see demux_send_chunk).
 * This way index reconstruction can be interrupted by a stop() or an other
 * seek().
 */
static int demux_avi_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (!this->streaming) {

    _x_demux_flush_engine (this->stream);
    this->seek_request    = 1;
    this->seek_start_pos  = start_pos;
    this->seek_start_time = start_time;

    this->status = DEMUX_OK;
  }
  return this->status;
}

static int demux_avi_seek_internal (demux_avi_t *this) {

  int64_t              video_pts = 0, max_pos, min_pos = 0, cur_pos;
  video_index_entry_t *vie = NULL;
  int64_t              audio_pts;
  off_t                start_pos = this->seek_start_pos;
  int                  start_time = this->seek_start_time;

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  this->status = DEMUX_OK;

  if (this->streaming)
    return this->status;

  AVI_seek_start (this->avi);

  /*
   * seek to start pos / time
   */

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
          "start pos is %" PRIdMAX ", start time is %d\n", (intmax_t)start_pos,
          start_time);

  /* Seek video.  We do a single idx_grow at the beginning rather than
   * incrementally growing the index in a loop, so that if the index
   * grow is going to take a while, the user is notified via the OSD
   * (which only shows up if >= 1000 index entries are added at a time). */

  /* We know for sure the last index entry is past our starting
   * point; find the lowest index entry that's past our starting
   * point. */
  min_pos = 0;

  if (start_pos) {
    idx_grow(this, start_pos_stopper, &start_pos);
  } else if (start_time) {
    video_pts = start_time * 90;
    idx_grow(this, start_time_stopper, &video_pts);
  }

  if (start_pos || start_time)
    max_pos = this->avi->video_idx.video_frames - 1;
  else
    max_pos=0;

  cur_pos = this->avi->video_posf;
  if (max_pos < 0) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else if (start_pos) {
    while (min_pos < max_pos) {
      this->avi->video_posf = cur_pos = (min_pos + max_pos) / 2;
      if (cur_pos == min_pos) break;
      vie = video_cur_index_entry(this);
      if (vie->pos >= start_pos) {
        max_pos = cur_pos;
      } else {
        min_pos = cur_pos;
      }
    }
  } else if (start_time) {
    while (min_pos < max_pos) {
      this->avi->video_posf = cur_pos = (min_pos + max_pos) / 2;
      if (cur_pos == min_pos) break;
      vie = video_cur_index_entry(this);
      if (get_video_pts (this, cur_pos) >= video_pts) {
        max_pos = cur_pos;
      } else {
        min_pos = cur_pos;
      }
    }
  }

  while (vie && !(vie->flags & AVIIF_KEYFRAME) && cur_pos) {
    this->avi->video_posf = --cur_pos;
    vie = video_cur_index_entry(this);
  }
  if (!vie || !(vie->flags & AVIIF_KEYFRAME)) {
    lprintf ("No previous keyframe found\n");
  }
  video_pts = get_video_pts (this, cur_pos);

  /* Seek audio.  We can do this incrementally, on the theory that the
   * audio position we're looking for will be pretty close to the video
   * position we've already found, so we won't be seeking though the
   * file much at this point. */

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "video_pts = %" PRId64 "\n", video_pts);

  /* FIXME ? */
  audio_pts = 77777777;

  if (!this->no_audio && this->status == DEMUX_OK) {
    audio_index_entry_t *aie;
    int i;

    for(i = 0; i < this->avi->n_audio; i++) {
      max_pos = this->avi->audio[i]->audio_idx.audio_chunks - 1;
      min_pos = 0;
      lprintf("audio_chunks=%d, min=%" PRId64 ", max=%" PRId64 "\n", this->avi->audio[i]->audio_idx.audio_chunks, min_pos, max_pos);
      while (min_pos < max_pos) {
        cur_pos = this->avi->audio[i]->audio_posc = (max_pos + min_pos) / 2;
        if (cur_pos == min_pos) break;
        aie = audio_cur_index_entry(this, this->avi->audio[i]);
        if (aie) {
          if ( (audio_pts = get_audio_pts(this, i, aie->block_no, aie->tot, 0)) >= video_pts) {
            max_pos = cur_pos;
          } else {
            min_pos = cur_pos;
          }
          lprintf ("audio_pts = %" PRId64 " %" PRId64 " < %" PRId64 " < %" PRId64 "\n",
                   audio_pts, min_pos, cur_pos, max_pos);
        } else {
          if (cur_pos > min_pos) {
            max_pos = cur_pos;
          } else {
            this->status = DEMUX_FINISHED;
            lprintf ("audio seek to start failed\n");
            break;
          }
        }
      }
      lprintf ("audio_pts = %" PRId64 "\n", audio_pts);

      /*
       * try to make audio pos more accurate for long index entries
       *
       * yeah, i know this implementation is pathetic (gb)
       */

      if ((audio_pts > video_pts) && (this->avi->audio[i]->audio_posc>0))
	this->avi->audio[i]->audio_posc--;

      aie = audio_cur_index_entry(this, this->avi->audio[i]);
      if (aie) {
	while ((this->avi->audio[i]->audio_posb < aie->len)
	       && ((audio_pts = get_audio_pts(this, i, aie->block_no, aie->tot,
                                              this->avi->audio[i]->audio_posb)) < video_pts))
	  this->avi->audio[i]->audio_posb++;
      }
    }
  }
  lprintf ("video posc: %d\n", this->avi->video_posf);

  this->send_newpts = 1;
  this->buf_flag_seek = 1;
  _x_demux_control_newpts (this->stream, video_pts, BUF_FLAG_SEEK);

  return this->status;
}

static int demux_avi_get_stream_length (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi) {
    if (this->streaming) {
      return (int)(get_video_pts(this, this->avi->video_posf) / 90);
    } else {
      return (int)(get_video_pts(this, this->avi->video_idx.video_frames) / 90);
    }
  }

  return 0;
}

static uint32_t demux_avi_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_avi_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_avi_t    *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[12];

    if (input->get_capabilities(input) & INPUT_CAP_BLOCK)
      return NULL;

    if (_x_demux_read_header(input, buf, 12) != 12)
      return NULL;

    if( !( (strncasecmp(buf  ,"ON2 ",4) == 0 &&
            strncasecmp(buf+8,"ON2f",4) == 0) ||
           (strncasecmp(buf  ,"RIFF",4) == 0 &&
            strncasecmp(buf+8,"AVI ",4) == 0) ) )
      return NULL;
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  this         = calloc(1, sizeof(demux_avi_t));

  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_avi_send_headers;
  this->demux_plugin.send_chunk        = demux_avi_send_chunk;
  this->demux_plugin.seek              = demux_avi_seek;
  this->demux_plugin.dispose           = demux_avi_dispose;
  this->demux_plugin.get_status        = demux_avi_get_status;
  this->demux_plugin.get_stream_length = demux_avi_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_avi_get_capabilities;
  this->demux_plugin.get_optional_data = demux_avi_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "streaming mode\n");
    this->streaming = 1;
  }

  this->avi = AVI_init (this);
  if (!this->avi) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "AVI_init failed (AVI_errno: %d)\n", this->AVI_errno);
    free (this);
    return NULL;
  }

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
           "demux_avi: %d frames\n", this->avi->video_idx.video_frames);

  return &this->demux_plugin;
}

/*
 * demux avi class
 */
static void *init_class (xine_t *xine, void *data) {
  demux_avi_class_t     *this;

  this = calloc(1, sizeof(demux_avi_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("AVI/RIFF demux plugin");
  this->demux_class.identifier      = "AVI";
  this->demux_class.mimetypes       =
    "video/msvideo: avi: AVI video;"
    "video/x-msvideo: avi: AVI video;";
  this->demux_class.extensions      = "avi";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_avi = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "avi", XINE_VERSION_CODE, &demux_info_avi, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
