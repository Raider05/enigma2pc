/*
 * Copyright (C) 2000-2003 the xine project
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define LOG_MODULE "input_enigma"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/input_plugin.h>
//#include "net_buf_ctrl.h"
#include "combined_enigma.h"

// xvdr
#include <sys/time.h>

#define ENIGMA_ABS_FIFO_DIR     "/tmp"
#define DEFAULT_PTS_START       150000
#define BUFSIZE                 768
#define FILE_FLAGS O_RDONLY
#define FIFO_PUT                0

// xvdr 
#define XVDR_METRONOM_OPTION_BASE  0x1001
#define XVDR_METRONOM_LAST_VO_PTS  (XVDR_METRONOM_OPTION_BASE)
#define XVDR_METRONOM_TRICK_SPEED  (XVDR_METRONOM_OPTION_BASE + 1)
#define XVDR_METRONOM_STILL_MODE   (XVDR_METRONOM_OPTION_BASE + 2)
#define XVDR_METRONOM_ID           (XVDR_METRONOM_OPTION_BASE + 3)

#define XVDR_METRONOM_LIVE_BUFFERING   (XVDR_METRONOM_OPTION_BASE + 4)
#define XVDR_METRONOM_STREAM_START     (XVDR_METRONOM_OPTION_BASE + 5)

typedef struct enigma_input_plugin_s enigma_input_plugin_t;

typedef struct 
{
  metronom_t          metronom;
  metronom_t         *stream_metronom;
  enigma_input_plugin_t *input;
// xvdr 
  int     trickspeed;    /* current trick speed */
  int     still_mode;
  int64_t last_vo_pts;   /* last displayed video frame PTS */
  int     wired;         /* true if currently wired to stream */

  /* initial buffering in live mode */
  uint8_t  buffering;      /* buffering active */
  uint8_t  live_buffering; /* live buffering enabled */
  uint8_t  stream_start;
  int64_t  vid_pts;        /* last seen video pts */
  int64_t  aud_pts;        /* last seen audio pts */
  int64_t  disc_pts;       /* reported discontinuity pts */
  uint64_t buffering_start_time;
  uint64_t first_frame_seen_time;

  pthread_mutex_t mutex;
}
enigma_metronom_t;

typedef struct enigma_vpts_offset_s enigma_vpts_offset_t;

struct enigma_vpts_offset_s
{
  enigma_vpts_offset_t *next;
  int64_t            vpts;
  int64_t            offset;
};

struct enigma_input_plugin_s {
  input_plugin_t      input_plugin;
  xine_stream_t      *stream;
  int                 fh;
  char               *mrl;
  off_t               curpos;
  char                seek_buf[BUFSIZE];
  xine_t             *xine;
  int                 last_disc_type;
//  nbc_t              *nbc;

  uint8_t             trick_speed_mode;
  uint8_t             trick_speed_mode_blocked;
  pthread_mutex_t     trick_speed_mode_lock;
  pthread_cond_t      trick_speed_mode_cond;

  pthread_t           metronom_thread;
  pthread_mutex_t     metronom_thread_lock;
  int64_t             metronom_thread_request;
  int                 metronom_thread_reply;
  pthread_cond_t      metronom_thread_request_cond;
  pthread_cond_t      metronom_thread_reply_cond;
  pthread_mutex_t     metronom_thread_call_lock;

  uint8_t             find_sync_point;
  pthread_mutex_t     find_sync_point_lock;

  enigma_metronom_t      metronom;
//  int                 last_disc_type;

  enigma_vpts_offset_t  *vpts_offset_queue;
  enigma_vpts_offset_t  *vpts_offset_queue_tail;
  pthread_mutex_t     vpts_offset_queue_lock;
  pthread_cond_t      vpts_offset_queue_changed_cond;
  int                 vpts_offset_queue_changes;

//* xvdr 
  int     trickspeed;    // current trick speed
  int     still_mode;
  int64_t last_vo_pts;   // last displayed video frame PTS 
  int     wired;         // true if currently wired to stream 

  /* initial buffering in live mode */
/*  uint8_t  buffering;      // buffering active 
  uint8_t  live_buffering; // live buffering enabled 
  uint8_t  stream_start;
  int64_t  vid_pts;        // last seen video pts 
  int64_t  aud_pts;        // last seen audio pts 
  int64_t  disc_pts;       // reported discontinuity pts 
  uint64_t buffering_start_time;
  uint64_t first_frame_seen_time;

  pthread_mutex_t mutex;
*/
};

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
} enigma_input_class_t;

// xvdr

static uint64_t time_ms(void)
{
  struct timeval t;
#ifdef XINEUTILS_H
  if (xine_monotonic_clock(&t, NULL) == 0)
#else
  if (gettimeofday(&t, NULL) == 0)
#endif
     return ((uint64_t)t.tv_sec) * 1000ULL + t.tv_usec / 1000ULL;
  return 0;
}

static uint64_t elapsed(uint64_t t)
{
  return time_ms() - t;
}

static int warnings = 0;

static int64_t absdiff(int64_t a, int64_t b) { int64_t diff = a-b; if (diff<0) diff = -diff; return diff; }
static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }

static void check_buffering_done(enigma_metronom_t *this)
{
  /* both audio and video timestamps seen ? */
  if (this->vid_pts && this->aud_pts) {
    int64_t da = this->aud_pts - this->disc_pts;
    int64_t dv = this->vid_pts - this->disc_pts;
    int64_t d_min = min64(da, dv);
    printf("  stream A-V diff %d ms", (int)(this->vid_pts - this->aud_pts)/90);
    printf("  reported stream start at pts %"PRId64, this->disc_pts);
    printf("  output fifo end at: audio %"PRId64" video %"PRId64, this->aud_pts, this->vid_pts);
    printf("  dA %"PRId64" dV %"PRId64, da, dv);
    if (d_min < 0 && d_min > -10*90000) {
      printf("  *** output is late %"PRId64" ticks (%"PRId64" ms) ***", d_min, -d_min/90);
//      this->scr->jump(this->scr, d_min);
    }
    this->buffering = 0;
    this->stream_start = 0;
//    this->scr->set_buffering(this->scr, 0);
    return;
  }

  if (this->first_frame_seen_time) {
    int64_t ms_since_first_frame = elapsed(this->first_frame_seen_time);

    if (ms_since_first_frame > 1000) {

      this->stream_start = 0;

      /* abort buffering if no audio */
      if (this->vid_pts && !this->aud_pts) {
        printf("buffering stopped: NO AUDIO ? elapsed time %d ms", (int)ms_since_first_frame);
        this->buffering = 0;
//        this->scr->set_buffering(this->scr, 0);
        return;
      }

      /* abort buffering if no video */
      if (!this->vid_pts && this->aud_pts) {
        printf("buffering stopped: NO VIDEO ? elapsed time %d ms", (int)ms_since_first_frame);
        this->buffering = 0;
//        this->scr->set_buffering(this->scr, 0);
        return;
      }
    }
  }
}

static void got_video_frame(metronom_t *self, vo_frame_t *frame)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;
  int64_t          pts  = frame->pts;
/*
#if 1 /* xine-lib master-slave metronom causes some problems ... * /
  if (metronom->got_video_frame != got_video_frame) {
    if (!warnings++)
      LOGMSG("got_video_frame: invalid object");
    return;
  }
  warnings = 0;
#endif
*/
  if (this->still_mode) {
    printf("Still frame, type %d", frame->picture_coding_type);
    frame->pts       = 0;
  }

  if (this->trickspeed) {
    frame->pts       = 0;
    frame->duration *= 12; /* GOP */
  }

  /* initial buffering */
  pthread_mutex_lock(&this->mutex);
  if (this->buffering && !frame->bad_frame) {

    /* track video pts */
    if (pts) {
      if (this->vid_pts && (absdiff(this->vid_pts, pts) > 5*90000)) {
        printf("buffering: video jump resetted audio pts");
        this->aud_pts = 0;
      }
      if (this->vid_pts && this->aud_pts && (absdiff(this->vid_pts, this->aud_pts) > 5*90000)) {
        printf("buffering: A-V diff resetted audio pts");
        this->aud_pts = 0;
      }
      if (!this->vid_pts) {
        printf("got video pts, frame type %d (@%d ms)", frame->picture_coding_type, (int)elapsed(this->buffering_start_time));
        this->first_frame_seen_time = time_ms(); 
      }
      this->vid_pts = pts;
    }

    /* some logging */
    if (!pts) {
      printf("got video, pts 0, buffering, frame type %d, bad_frame %d", frame->picture_coding_type, frame->bad_frame);
    }
    if (pts && !frame->pts) {
      printf("*** ERROR: hiding video pts while buffering ***");
    }

    check_buffering_done(this);
  }

  pthread_mutex_unlock(&this->mutex);

  this->stream_metronom->got_video_frame (this->stream_metronom, frame);

  frame->pts = pts;
}

static int64_t got_audio_samples(metronom_t *self, int64_t pts, int nsamples)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

  pthread_mutex_lock(&this->mutex);

  /* initial buffering */
  if (this->buffering) {

    /* track audio pts */
    if (pts) {
      if (this->aud_pts && (this->aud_pts > pts || absdiff(pts, this->aud_pts) > 5*90000)) {
        printf("audio jump resetted video pts");
        this->vid_pts = 0;
      }
      if (this->aud_pts && this->vid_pts && (absdiff(this->vid_pts, this->aud_pts) > 5*90000)) {
        printf("buffering: A-V diff resetted video pts");
        this->vid_pts = 0;
      }
      if (!this->aud_pts) {
        printf("got audio pts (@%d ms)", (int)elapsed(this->buffering_start_time));
        this->first_frame_seen_time = time_ms();
      }
      this->aud_pts = pts;
    }

    /* some logging */
    if (!pts && !this->aud_pts) {
      printf("got audio, pts 0, buffering");
    }

    check_buffering_done(this);
  }

  pthread_mutex_unlock(&this->mutex);

  return this->stream_metronom->got_audio_samples (this->stream_metronom, pts, nsamples);
}

static int64_t got_spu_packet(metronom_t *self, int64_t pts)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;
  return this->stream_metronom->got_spu_packet(this->stream_metronom, pts);
}

static void start_buffering(enigma_metronom_t *this, int64_t disc_off)
{
  if (this->live_buffering && this->stream_start && disc_off) {
    if (!this->buffering) {
      printf("live mode buffering started (@%d ms)", (int)elapsed(this->buffering_start_time));

      this->aud_pts  = 0;
      this->vid_pts  = 0;
      this->disc_pts = disc_off;

      this->first_frame_seen_time = 0;

      this->buffering = 1;
//      this->scr->set_buffering(this->scr, 1);
    }
  } else {
    if (this->buffering) {
      printf("live mode buffering aborted (@%d ms)", (int)elapsed(this->buffering_start_time));
      this->buffering = 0;
//      this->scr->set_buffering(this->scr, 0);
    }
  }
}

static void handle_audio_discontinuity(metronom_t *self, int type, int64_t disc_off)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

  start_buffering(this, disc_off);

  this->stream_metronom->handle_audio_discontinuity(this->stream_metronom, type, disc_off);
}

static void handle_video_discontinuity(metronom_t *self, int type, int64_t disc_off)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

  start_buffering(this, disc_off);

  this->stream_metronom->handle_video_discontinuity(this->stream_metronom, type, disc_off);
}

static void set_audio_rate(metronom_t *self, int64_t pts_per_smpls)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;
  this->stream_metronom->set_audio_rate(this->stream_metronom, pts_per_smpls);
}

static void set_option(metronom_t *self, int option, int64_t value)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

  if (option == XVDR_METRONOM_LAST_VO_PTS) {
    if (value > 0) {
      pthread_mutex_lock(&this->mutex);
      this->last_vo_pts = value;
      pthread_mutex_unlock(&this->mutex);
    }
    return;
  }

  if (option == XVDR_METRONOM_LIVE_BUFFERING) {
    pthread_mutex_lock(&this->mutex);
    this->live_buffering = value;
    pthread_mutex_unlock(&this->mutex);
    return;
  }

  if (option == XVDR_METRONOM_STREAM_START) {
    pthread_mutex_lock(&this->mutex);
    this->stream_start = 1;
    this->buffering_start_time = time_ms();
    pthread_mutex_unlock(&this->mutex);
    return;
  }

  if (option == XVDR_METRONOM_TRICK_SPEED) {
    this->trickspeed = value;
    return;
  }

  if (option == XVDR_METRONOM_STILL_MODE) {
    this->still_mode = value;
    return;
  }

  this->stream_metronom->set_option(this->stream_metronom, option, value);
}

static int64_t get_option(metronom_t *self, int option)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

  if (option == XVDR_METRONOM_LAST_VO_PTS) {
    int64_t pts;
    pthread_mutex_lock(&this->mutex);
    pts = this->last_vo_pts;
    pthread_mutex_unlock(&this->mutex);
    return pts;
  }
  if (option == XVDR_METRONOM_TRICK_SPEED) {
    return this->trickspeed;
  }
  if (option == XVDR_METRONOM_STILL_MODE) {
    return this->still_mode;
  }
  if (option == XVDR_METRONOM_ID) {
    return XVDR_METRONOM_ID;
  }

  return this->stream_metronom->get_option(this->stream_metronom, option);
}

static void set_master(metronom_t *self, metronom_t *master)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;
  this->stream_metronom->set_master(this->stream_metronom, master);
}

static void metronom_exit(metronom_t *self)
{
  enigma_metronom_t *this = (enigma_metronom_t *)self;

/*  this->unwire(this);
  this->stream = NULL;

  if (this->orig_metronom) {
    metronom_t *orig_metronom = this->orig_metronom;
    this->orig_metronom = NULL;

    orig_metronom->exit(orig_metronom);
  }
*/
  _x_abort();
}

static off_t enigma_read_abort(xine_stream_t *stream, int fd, char *buf, off_t todo)
{
  off_t ret;

  while (1)
  {
    /*
     * System calls are not a thread cancellation point in Linux
     * pthreads.  However, the RT signal sent to cancel the thread
     * will cause recv() to return with EINTR, and we can manually
     * check cancellation.
     */
    pthread_testcancel();
    ret = _x_read_abort(stream, fd, buf, todo);
    pthread_testcancel();

    if (ret < 0
        && (errno == EINTR
          || errno == EAGAIN))
    {
      continue;
    }

    break;
  }

  return ret;
}

static off_t enigma_plugin_read (input_plugin_t *this_gen,
				void *buf_gen, off_t len) {

  enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen;
  uint8_t *buf = (uint8_t *)buf_gen;
  off_t n, total = 0;
#ifdef LOG_READ
  lprintf ("reading %lld bytes...\n", len);
#endif

  if( len > 0 )
  {
    int retries = 0;
    do
    {
      n = enigma_read_abort (this->stream, this->fh, (char *)&buf[total], len-total);
      //n = _x_io_file_read (this->stream, this->fh, &buf[total], len - total);
      if (0 == n)
        lprintf("read 0, retries: %d\n", retries);
    }
    while (0 == n
           && _x_continue_stream_processing(this->stream)
           && 200 > retries++); // 200 * 50ms
#ifdef LOG_READ
    lprintf ("got %lld bytes (%lld/%lld bytes read)\n", n, total, len);
#endif
    if (n < 0)
    {
      _x_message(this->stream, XINE_MSG_READ_ERROR, NULL);
      return 0;
    }

    this->curpos += n;
    total += n;
  }

  if (this->find_sync_point
    && total == 6)
  {
    pthread_mutex_lock(&this->find_sync_point_lock);

    while (this->find_sync_point
      && total == 6
      && buf[0] == 0x00
      && buf[1] == 0x00
      && buf[2] == 0x01)
    {
      int l, sp;

      if (buf[3] == 0xbe
        && buf[4] == 0xff)
      {
/* fprintf(stderr, "------- seen sync point: %02x, waiting for: %02x\n", buf[5], this->find_sync_point); */
        if (buf[5] == this->find_sync_point)
        {
          this->find_sync_point = 0;
          break;
        }
      }

      if ((buf[3] & 0xf0) != 0xe0
        && (buf[3] & 0xe0) != 0xc0
        && buf[3] != 0xbd
        && buf[3] != 0xbe)
      {
        break;
      }

      l = buf[4] * 256 + buf[5];
      if (l <= 0)
         break;

      sp = this->find_sync_point;
      this->find_sync_point = 0;
      this_gen->seek(this_gen, l, SEEK_CUR);
      total = this_gen->read(this_gen, buf, 6);
      this->find_sync_point = sp;
    }

    pthread_mutex_unlock(&this->find_sync_point_lock);
  }

  return total;

}

static buf_element_t *enigma_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
					       off_t todo) {

  off_t                 total_bytes;
  /* enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen; */
  buf_element_t         *buf = fifo->buffer_pool_alloc (fifo);

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = enigma_plugin_read (this_gen, (char*)buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

/* forward reference */
static off_t enigma_plugin_get_current_pos(input_plugin_t *this_gen);

static off_t enigma_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {

  enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen;

  printf ("seek %"PRId64" offset, %d origin...\n", offset, origin);

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
        return this->curpos;
    }

    this_gen->read (this_gen, this->seek_buf, offset);
  }

  if (origin == SEEK_SET) {

    if (offset < this->curpos) {

      //if( this->curpos <= this->preview_size )
      //  this->curpos = offset;
      //else
        xprintf (this->xine, XINE_VERBOSITY_LOG,
                 _("stdin: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n"),
                 (intmax_t)this->curpos, (intmax_t)offset);
        printf ("stdin: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n",
                 (intmax_t)this->curpos, (intmax_t)offset);

    } else {
      offset -= this->curpos;

      for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
        if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
          return this->curpos;
      }

      this_gen->read (this_gen, this->seek_buf, offset);
    }
  }

  return this->curpos;
}

static off_t enigma_plugin_get_length(input_plugin_t *this_gen) {
  return 0;
}

static uint32_t enigma_plugin_get_capabilities(input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

static uint32_t enigma_plugin_get_blocksize(input_plugin_t *this_gen) {

  return 0;
}

static off_t enigma_plugin_get_current_pos (input_plugin_t *this_gen){
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  return this->curpos;
}

static const char* enigma_plugin_get_mrl (input_plugin_t *this_gen) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  return this->mrl;
}

static void enigma_plugin_dispose (input_plugin_t *this_gen ) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  pthread_mutex_destroy(&this->metronom.mutex);

  pthread_mutex_destroy(&this->find_sync_point_lock); // need


  if (this->fh != -1)
    close(this->fh);

  free (this->mrl);

  this->stream->metronom = this->metronom.stream_metronom;
  this->metronom.stream_metronom = 0;

  free (this);
}

static int enigma_plugin_get_optional_data (input_plugin_t *this_gen,
					   void *data, int data_type) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;
  (void)this; //Add from 
  switch (data_type)
  {
  case INPUT_OPTIONAL_DATA_PREVIEW:
    /* just fake what mpeg_pes demuxer expects */
    memcpy (data, "\x00\x00\x01\xe0\x00\x03\x80\x00\x00", 9);
    return 9;
  case INPUT_OPTIONAL_DATA_DEMUXER:
    {
      char **tmp = (char**)data;
      *tmp = "mpeg-ts";
    }
    return 0;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int enigma_plugin_open (input_plugin_t *this_gen ) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  printf ("trying to open '%s'...\n", this->mrl);

  if (this->fh == -1) {
//    int err = 0;
    char *filename = (char *)ENIGMA_ABS_FIFO_DIR "/ENIGMA_FIFO";
    this->fh = open (filename, FILE_FLAGS);

    printf("filename '%s'\n", filename);

    if (this->fh == -1) {
      xprintf (this->xine, XINE_VERBOSITY_LOG, _("enigma_fifo: failed to open '%s'\n"), filename);
      printf ("enigma_fifo: failed to open '%s'\n", filename);
      return 0;
    }

  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this->curpos          = 0;

  return 1;
}

static input_plugin_t *enigma_class_get_instance (input_class_t *class_gen,
						 xine_stream_t *stream, const char *data) {

  enigma_input_class_t  *class = (enigma_input_class_t *) class_gen;
  enigma_input_plugin_t *this;
  char                 *mrl = strdup(data);
//  int                   fh;

  if (!strncasecmp(mrl, "enigma:/", 8)) {
    lprintf("Enigma plugin\n");
  } else {
    free(mrl);
    return NULL;
  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this       = calloc(1, sizeof(enigma_input_plugin_t));

  this->stream = stream;
  this->curpos = 0;
  this->mrl    = mrl;
  this->fh     = -1;
  this->xine   = class->xine;

  this->input_plugin.open              = enigma_plugin_open;
  this->input_plugin.get_capabilities  = enigma_plugin_get_capabilities;
  this->input_plugin.read              = enigma_plugin_read;
  this->input_plugin.read_block        = enigma_plugin_read_block;
  this->input_plugin.seek              = enigma_plugin_seek;
  this->input_plugin.get_current_pos   = enigma_plugin_get_current_pos;
  this->input_plugin.get_length        = enigma_plugin_get_length;
  this->input_plugin.get_blocksize     = enigma_plugin_get_blocksize;
  this->input_plugin.get_mrl           = enigma_plugin_get_mrl;
  this->input_plugin.dispose           = enigma_plugin_dispose;
  this->input_plugin.get_optional_data = enigma_plugin_get_optional_data;
  this->input_plugin.input_class       = class_gen;
/*
  pthread_mutex_init(&this->trick_speed_mode_lock, 0);
  pthread_cond_init(&this->trick_speed_mode_cond, 0);

  pthread_mutex_init(&this->metronom_thread_lock, 0);
  pthread_cond_init(&this->metronom_thread_request_cond, 0);
  pthread_cond_init(&this->metronom_thread_reply_cond, 0);
  pthread_mutex_init(&this->metronom_thread_call_lock, 0);
*/
  pthread_mutex_init(&this->find_sync_point_lock, 0);

  this->metronom.input = this;
/*  this->metronom.metronom.set_audio_rate             = enigma_metronom_set_audio_rate;
  this->metronom.metronom.got_video_frame            = enigma_metronom_got_video_frame;
  this->metronom.metronom.got_audio_samples          = enigma_metronom_got_audio_samples;
  this->metronom.metronom.got_spu_packet             = enigma_metronom_got_spu_packet;
  this->metronom.metronom.handle_audio_discontinuity = enigma_metronom_handle_audio_discontinuity;
  this->metronom.metronom.handle_video_discontinuity = enigma_metronom_handle_video_discontinuity;
  this->metronom.metronom.set_option                 = enigma_metronom_set_option;
  this->metronom.metronom.get_option                 = enigma_metronom_get_option;
  this->metronom.metronom.set_master                 = enigma_metronom_set_master;
  this->metronom.metronom.exit                       = enigma_metronom_exit;
*/

// xvdr 
  this->metronom.metronom.set_audio_rate             = set_audio_rate;
  this->metronom.metronom.got_video_frame            = got_video_frame;
  this->metronom.metronom.got_audio_samples          = got_audio_samples;
  this->metronom.metronom.got_spu_packet             = got_spu_packet;
  this->metronom.metronom.handle_audio_discontinuity = handle_audio_discontinuity;
  this->metronom.metronom.handle_video_discontinuity = handle_video_discontinuity;
  this->metronom.metronom.set_option                 = set_option;
  this->metronom.metronom.get_option                 = get_option;
  this->metronom.metronom.set_master                 = set_master;
  this->metronom.metronom.exit                       = metronom_exit;

  pthread_mutex_init(&this->metronom.mutex, NULL);

  this->metronom.stream_metronom = stream->metronom;
  stream->metronom = &this->metronom.metronom;

//  pthread_mutex_init(&this->vpts_offset_queue_lock, 0);
//  pthread_cond_init(&this->vpts_offset_queue_changed_cond, 0);

  /*
   * buffering control
   */
//  this->nbc    = enigma_nbc_init (this->stream);

  return &this->input_plugin;
}


//static void *init_class (xine_t *xine, void *data) {
void *init_class (xine_t *xine, void *data) {

  enigma_input_class_t  *this;

  this = calloc(1, sizeof (enigma_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = enigma_class_get_instance;
  this->input_class.identifier         = "ENIGMA";
  this->input_class.description        = N_("ENIGMA2PC display device plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

/*
const plugin_info_t xine_plugin_info[] EXPORTED = {
//  type, API, "name", version, special_info, init_function 
  { PLUGIN_INPUT, 18, "enigma", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
*/
