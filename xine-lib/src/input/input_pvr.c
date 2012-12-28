/*
 * Copyright (C) 2000-2005 the xine project
 * March 2003 - Miguel Freitas
 * This plugin was sponsored by 1Control
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
 * pvr input plugin for WinTV-PVR 250/350 pci cards using driver from:
 *   http://ivtv.sf.net
 *
 * features:
 *   - play live mpeg2 stream (realtime mode) while recording
 *   - can pause, play slow, fast and seek back into the recorded stream
 *   - switches back to realtime mode if played until the end
 *   - may erase files as they get old
 *
 * requires:
 *   - audio.synchronization.av_sync_method=resample
 *   - ivtv driver (01 Jul 2003 cvs is known to work)
 *
 * MRL:
 *   pvr:/<prefix_to_tmp_files>!<prefix_to_saved_files>!<max_page_age>
 *
 * usage:
 *   xine pvr:/<prefix_to_tmp_files>\!<prefix_to_saved_files>\!<max_page_age>
 */

/**************************************************************************

 Programmer's note (or how to write your PVR frontend):

 - in order to use live pause functionality you must capture data to disk.
   this is done using XINE_EVENT_SET_V4L2 event. it is important to set the
   inputs/channel/frequency you want to capture data from.

   comments:
   1) session_id must be set: it is used to create the temporary filenames.

   2) if session_id = -1 no data will be recorded to disk (no pause/replay)

   3) if session_id is the same as previous value it will just set the "sync
      point". sync point (show_page) may be used by the PVR frontend to tell
      that a new show has began. of course, the PVR frontend should be aware
      of TV guide and stuff.

 - when user wants to start recording (that is: temporary data will be made
   permanent) it should issue a XINE_EVENT_PVR_SAVE.
   mode can be one of the following:

   -1 = do nothing, just set the name (see below)
   0 = truncate current session and save from now on
   1 = save from last sync point
   2 = save everything on current session

   saving actually means just marking the current pages as not temporary.
   when a session is finished, instead of erasing the files they will be
   renamed using the save file prefix.

 - the permanent name can be set in two ways:

   1) passing a name with the XINE_EVENT_PVR_SAVE before closing the
      current session. (id = -1)
   2) when a saved session is closed without setting the name, it will be
      given a stardard name based on channel number and time. an event
      XINE_EVENT_PVR_REPORT_NAME is sent to report the name and a unique
      identifier. frontend may then ask the user the name he wants and may
      pass back a XINE_EVENT_PVR_SAVE with id set. pvr plugin will rename
      the files again.

***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_VIDEOIO_H
# include <sys/videoio.h>
#elif defined(HAVE_SYS_VIDEODEV2_H)
# include <sys/videodev2.h>
#else
# include <linux/videodev2.h>
#endif

#define XINE_ENABLE_EXPERIMENTAL_FEATURES

#define LOG_MODULE "input_pvr"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>

#define PVR_DEVICE        "/dev/video0"

#define PVR_BLOCK_SIZE    2048			/* pvr works with dvd-like data */
#define BLOCKS_PER_PAGE   102400		/* 200MB per page. each session can have several pages */
#define MAX_PAGES         10000			/* maximum number of pages to keep track */

#define NUM_PREVIEW_BUFFERS   250  /* used in mpeg_block demuxer */

/*
#define SCRLOG 1
*/

/* external API borrowed from ivtv.h */
#define IVTV_IOC_G_CODEC	0xFFEE7703
#define IVTV_IOC_S_CODEC	0xFFEE7704

/* Stream types */
#define IVTV_STREAM_PS		0
#define IVTV_STREAM_TS		1
#define IVTV_STREAM_MPEG1	2
#define IVTV_STREAM_PES_AV	3
#define IVTV_STREAM_PES_V	5
#define IVTV_STREAM_PES_A	7
#define IVTV_STREAM_DVD		10

/* For use with IVTV_IOC_G_CODEC and IVTV_IOC_S_CODEC */
struct ivtv_ioctl_codec {
	uint32_t aspect;
	uint32_t audio_bitmask;
	uint32_t bframes;
	uint32_t bitrate_mode;
	uint32_t bitrate;
	uint32_t bitrate_peak;
	uint32_t dnr_mode;
	uint32_t dnr_spatial;
	uint32_t dnr_temporal;
	uint32_t dnr_type;
	uint32_t framerate;
	uint32_t framespergop;
	uint32_t gop_closure;
	uint32_t pulldown;
	uint32_t stream_type;
};

typedef struct pvrscr_s pvrscr_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

  char             *devname;

} pvr_input_class_t;


typedef struct {
  input_plugin_t      input_plugin;

  pvr_input_class_t  *class;

  xine_stream_t      *stream;

  xine_event_queue_t *event_queue;

  pvrscr_t           *scr;
  int                 scr_tunning;
  int                 speed_before_pause;

  uint32_t            session;		/* session number used to identify the pvr file */
  int                 new_session;      /* force going to realtime for new sessions */

  int                 dev_fd;		/* fd of the mpeg2 encoder device */
  int                 rec_fd;		/* fd of the current recording file (session/page) */
  int                 play_fd;		/* fd of the current playback (-1 when realtime) */

  uint32_t            rec_blk;		/* next block to record */
  uint32_t            rec_page;		/* page of current rec_fd file */
  uint32_t            play_blk;		/* next block to play */
  uint32_t            play_page;	/* page of current play_fd file */
  uint32_t            first_page;	/* first page available (not erased yet) */
  uint32_t            max_page_age;	/* max age to retire (erase) pages */
  uint32_t            show_page;	/* first page of current show */
  uint32_t            save_page;	/* first page to save */
  uint32_t            page_block[MAX_PAGES]; /* first block of each page */

  char               *mrl;
  char               *tmp_prefix;
  char               *save_prefix;
  char               *save_name;
  xine_list_t        *saved_shows;
  int                 saved_id;

  time_t              start_time;	/* time when recording started */
  time_t              show_time;	/* time when current show started */

  /* buffer to pass data from pvr thread to xine */
  uint8_t             data[PVR_BLOCK_SIZE];
  int                 valid_data;
  int                 want_data;

  pthread_mutex_t     lock;
  pthread_mutex_t     dev_lock;
  pthread_cond_t      has_valid_data;
  pthread_cond_t      wake_pvr;
  pthread_t           pvr_thread;
  int                 pvr_running;
  int                 pvr_playing;
  int                 pvr_play_paused;

  int                 preview_buffers;

  /* device properties */
  int                 input;
  int                 channel;
  uint32_t            frequency;

} pvr_input_plugin_t;

typedef struct {
  int                 id;
  char               *base_name;
  int                 pages;
} saved_show_t;

/*
 * ***************************************************
 * unix System Clock Reference + fine tunning
 *
 * on an ideal world we would be using scr from mpeg2
 * encoder just like dxr3 does.
 * unfortunately it is not supported by ivtv driver,
 * and perhaps not even possible with wintv cards.
 *
 * the fine tunning option is used to change play
 * speed in order to regulate fifo usage, that is,
 * trying to match the rate of generated data.
 *
 * OBS: use with audio.synchronization.av_sync_method=resample
 * ***************************************************
 */

struct pvrscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  int64_t          cur_pts;
  int              xine_speed;
  double           speed_factor;
  double           speed_tunning;

  pthread_mutex_t  lock;

};

static int pvrscr_get_priority (scr_plugin_t *scr) {
  return 10; /* high priority */
}

/* Only call this when already mutex locked */
static void pvrscr_set_pivot (pvrscr_t *this) {

  struct   timeval tv;
  int64_t pts;
  double   pts_calc;

  xine_monotonic_clock(&tv, NULL);
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
  pts = this->cur_pts + pts_calc;

/* This next part introduces a one off inaccuracy
 * to the scr due to rounding tv to pts.
 */
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts=pts;

  return ;
}

static int pvrscr_set_speed (scr_plugin_t *scr, int speed) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->xine_speed   = speed;
  this->speed_factor = (double) speed * 90000.0 / XINE_FINE_SPEED_NORMAL *
                       this->speed_tunning;

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void pvrscr_speed_tunning (pvrscr_t *this, double factor) {
  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->speed_tunning = factor;
  this->speed_factor = (double) this->xine_speed * 90000.0 / XINE_FINE_SPEED_NORMAL *
                       this->speed_tunning;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_adjust (scr_plugin_t *scr, int64_t vpts) {
  pvrscr_t *this = (pvrscr_t*) scr;
  struct   timeval tv;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts = vpts;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);

  pvrscr_set_speed (&this->scr, XINE_FINE_SPEED_NORMAL);
}

static int64_t pvrscr_get_current (scr_plugin_t *scr) {
  pvrscr_t *this = (pvrscr_t*) scr;

  struct   timeval tv;
  int64_t pts;
  double   pts_calc;
  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);

  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;

  pts = this->cur_pts + pts_calc;

  pthread_mutex_unlock (&this->lock);

  return pts;
}

static void pvrscr_exit (scr_plugin_t *scr) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_destroy (&this->lock);
  free(this);
}

static pvrscr_t *XINE_MALLOC pvrscr_init (void) {
  pvrscr_t *this;

  this = calloc(1, sizeof(pvrscr_t));

  this->scr.interface_version = 3;
  this->scr.get_priority      = pvrscr_get_priority;
  this->scr.set_fine_speed    = pvrscr_set_speed;
  this->scr.adjust            = pvrscr_adjust;
  this->scr.start             = pvrscr_start;
  this->scr.get_current       = pvrscr_get_current;
  this->scr.exit              = pvrscr_exit;

  pthread_mutex_init (&this->lock, NULL);

  pvrscr_speed_tunning(this, 1.0 );
  pvrscr_set_speed (&this->scr, XINE_SPEED_PAUSE);
#ifdef SCRLOG
  printf("input_pvr: scr init complete\n");
#endif

  return this;
}

/*****************************************************/


static uint32_t block_to_page(pvr_input_plugin_t *this, uint32_t block) {
  uint32_t page;

  for( page = 0; page < this->rec_page; page++ ) {
    if( block < this->page_block[page+1] )
      break;
  }
  return page;
}

static uint32_t pvr_plugin_get_capabilities (input_plugin_t *this_gen) {

  /* pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen; */

  return INPUT_CAP_BLOCK | INPUT_CAP_SEEKABLE;
}


static off_t pvr_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len) {
  /*pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;*/
  char *buf = (char *)buf_gen;

  if (len < 4)
    return -1;

  /* FIXME: Tricking the demux_mpeg_block plugin */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0x01;
  buf[3] = 0xba;
  return 4;
}


/*
 * this function will adjust playback speed to control buffer utilization.
 * we must avoid:
 * - overflow: buffer runs full. no data is read from the mpeg2 card so it will discard
 *             mpeg2 packets and get out of sync with the block size.
 * - underrun: buffer gets empty. playback will suffer a pausing effect, also discarding
 *             video frames.
 *
 * OBS: use with audio.synchronization.av_sync_method=resample
 */
static void pvr_adjust_realtime_speed(pvr_input_plugin_t *this, fifo_buffer_t *fifo, int speed ) {

  int num_used, num_free;
  int scr_tunning = this->scr_tunning;

  num_used = fifo->size(fifo);
  num_free = fifo->num_free(fifo);

  if( num_used == 0 && scr_tunning != -2 ) {

    /* buffer is empty. pause it for a while */
    this->scr_tunning = -2; /* marked as paused */
    pvrscr_speed_tunning(this->scr, 1.0);
    this->speed_before_pause = speed;
    _x_set_speed(this->stream, XINE_SPEED_PAUSE);
#ifdef SCRLOG
    printf("input_pvr: buffer empty, pausing playback\n" );
#endif

  } else if( scr_tunning == -2 ) {

    /* currently paused, revert to normal if 1/3 full */
    if( 2*num_used > num_free ) {
      this->scr_tunning = 0;

      pvrscr_speed_tunning(this->scr, 1.0 );
      _x_set_speed(this->stream, this->speed_before_pause);
#ifdef SCRLOG
      printf("input_pvr: resuming playback\n" );
#endif
    }

  } else if( speed == XINE_SPEED_NORMAL && this->play_fd == -1 ) {

    /* when playing realtime, adjust the scr to make xine buffers half full */
    if( num_used > 2*num_free )
      scr_tunning = +1; /* play faster */
    else if( num_free > 2*num_used )
      scr_tunning = -1; /* play slower */
    else if( (scr_tunning > 0 && num_free > num_used) ||
             (scr_tunning < 0 && num_used > num_free) )
      scr_tunning = 0;

    if( scr_tunning != this->scr_tunning ) {
      this->scr_tunning = scr_tunning;
#ifdef SCRLOG
      printf("input_pvr: scr_tunning = %d (used: %d free: %d)\n", scr_tunning, num_used, num_free );
#endif

      /* make it play .5% faster or slower */
      pvrscr_speed_tunning(this->scr, 1.0 + (0.005 * scr_tunning) );
    }

  } else if( this->scr_tunning ) {
    this->scr_tunning = 0;

    pvrscr_speed_tunning(this->scr, 1.0 );
  }
}

#define PVR_FILENAME      "%s%08d_%08d.vob"

static char *make_temp_name(pvr_input_plugin_t *this, int page) {

  return _x_asprintf(PVR_FILENAME, this->tmp_prefix, this->session, page);
}

#define SAVE_BASE_FILENAME     "ch%03d %02d-%02d-%04d %02d:%02d:%02d"

static char *make_base_save_name(int channel, time_t tm) {
  struct tm rec_time;

  localtime_r(&tm, &rec_time);

  return _x_asprintf(SAVE_BASE_FILENAME,
           channel, rec_time.tm_mon+1, rec_time.tm_mday,
           rec_time.tm_year+1900, rec_time.tm_hour, rec_time.tm_min,
           rec_time.tm_sec);
}

#define SAVE_FILENAME      "%s%s_%04d.vob"

static char *make_save_name(pvr_input_plugin_t *this, char *base, int page) {

  return _x_asprintf(SAVE_FILENAME, this->save_prefix, base, page);
}

/*
 * send event to frontend about realtime status
 */
static void pvr_report_realtime (pvr_input_plugin_t *this, int mode) {

  xine_event_t         event;
  xine_pvr_realtime_t  data;

  event.type        = XINE_EVENT_PVR_REALTIME;
  event.stream      = this->stream;
  event.data        = &data;
  event.data_length = sizeof(data);
  gettimeofday(&event.tv, NULL);
  data.mode = mode;
  xine_event_send(this->stream, &event);
}

/*
 * close current recording page and open a new one
 */
static int pvr_break_rec_page (pvr_input_plugin_t *this) {

  char *filename;

  if( this->session == (unsigned)-1 ) /* not recording */
    return 1;

  if( this->rec_fd != -1 && this->rec_fd != this->play_fd ) {
    close(this->rec_fd);
  }

  if( this->rec_fd == -1 )
    this->rec_page = 0;
  else
    this->rec_page++;

  this->page_block[this->rec_page] = this->rec_blk;

  filename = make_temp_name(this, this->rec_page);

  lprintf("opening pvr file for writing (%s)\n", filename);

  this->rec_fd = xine_create_cloexec(filename, O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if( this->rec_fd == -1 ) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("input_pvr: error creating pvr file (%s)\n"), filename);
    free(filename);
    return 0;
  }
  free(filename);

  /* erase first_page if old and not to be saved */
  if( this->max_page_age != (unsigned)-1 &&
      this->rec_page - this->max_page_age == this->first_page &&
      (this->save_page == (unsigned)-1 || this->first_page < this->save_page) ) {

    filename = make_temp_name(this, this->first_page);

    lprintf("erasing old pvr file (%s)\n", filename);

    this->first_page++;
    if(this->play_fd != -1 && this->play_page < this->first_page) {
      this->play_blk = this->page_block[this->first_page];
      close(this->play_fd);
      this->play_fd = -1;
    }

    remove(filename);
    free(filename);
  }
  return 1;
}

/*
 * check the status of recording file, open new one as needed and write the current data.
 */
static int pvr_rec_file(pvr_input_plugin_t *this) {

  off_t pos;

  if( this->session == (unsigned)-1 ) /* not recording */
    return 1;

  /* check if it's time to change page/file */
  if( this->rec_fd == -1 || (this->rec_blk - this->page_block[this->rec_page]) >= BLOCKS_PER_PAGE ) {
    if( !pvr_break_rec_page(this) )
      return 0;
  }
  pos = (off_t)(this->rec_blk - this->page_block[this->rec_page]) * PVR_BLOCK_SIZE;
  if( lseek (this->rec_fd, pos, SEEK_SET) != pos ) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "input_pvr: error setting position for writing %" PRIdMAX "\n", (intmax_t)pos);
    return 0;
  }
  if( this->rec_fd != -1 ) {
    if( write(this->rec_fd, this->data, PVR_BLOCK_SIZE) < PVR_BLOCK_SIZE ) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_pvr: short write to pvr file (out of disk space?)\n");
      return 0;
    }
    this->rec_blk++;
  }

  return 1;
}

/*
 * check for playback mode, switching realtime <-> non-realtime.
 * gets data from file in non-realtime mode.
 */
static int pvr_play_file(pvr_input_plugin_t *this, fifo_buffer_t *fifo, uint8_t *buffer, int speed) {

  off_t pos;

  /* check for realtime. don't switch back unless enough buffers are
   * free to not block the pvr thread */
  if( this->new_session ||
      (this->play_blk+1 >= this->rec_blk && speed >= XINE_SPEED_NORMAL &&
       (this->play_fd == -1 || fifo->size(fifo) < fifo->num_free(fifo))) ) {

    this->play_blk = (this->rec_blk) ? (this->rec_blk-1) : 0;

    if( speed > XINE_SPEED_NORMAL )
      _x_set_speed(this->stream, XINE_SPEED_NORMAL);

    if( this->play_fd != -1 ) {
      if(this->play_fd != this->rec_fd )
        close(this->play_fd);
      this->play_fd = -1;

      lprintf("switching back to realtime\n");

      pvr_report_realtime(this,1);

    } else if (this->new_session) {
      lprintf("starting new session in realtime\n");
      pvr_report_realtime(this,1);
    }

    this->want_data = 1;
    this->new_session = 0;

  } else {

    if( this->rec_fd == -1 )
      return 1;

    if(speed != XINE_SPEED_PAUSE) {
      /* cannot run faster than the writing thread */
      while( this->play_blk+1 >= this->rec_blk ) {
        if( this->valid_data ) {
          this->valid_data = 0;
          pthread_cond_signal (&this->wake_pvr);
        }
        pthread_cond_wait (&this->has_valid_data, &this->lock);
      }
    }

    if( this->play_fd == -1 ||
        ((this->play_blk - this->page_block[this->play_page]) >= BLOCKS_PER_PAGE) ||
        (this->rec_page > this->play_page && this->play_blk >= this->page_block[this->play_page+1]) ) {

       if(this->play_fd == -1) {
         lprintf("switching to non-realtime\n");

         pvr_report_realtime(this,0);
       }

       if( this->play_fd != -1 && this->play_fd != this->rec_fd ) {
         close(this->play_fd);
       }

       if( this->play_fd == -1 )
         this->play_page = block_to_page(this, this->play_blk);
       else
         this->play_page++;

       if( this->play_page < this->first_page ) {
         this->play_page = this->first_page;
         this->play_blk = this->page_block[this->play_page];
       }

       /* should be impossible */
       if( this->play_page > this->rec_page ||
           this->play_blk > this->rec_blk ) {
         this->play_page = this->rec_page;
         this->play_blk = this->rec_blk;
       }

       /* check if we can reuse the same handle */
       if( this->play_page == this->rec_page ) {
         this->play_fd = this->rec_fd;
       } else {
         char *filename;

         filename = make_temp_name(this, this->play_page);

         lprintf("opening pvr file for reading (%s)\n", filename);

         this->play_fd = xine_open_cloexec(filename, O_RDONLY);
         if( this->play_fd == -1 ) {
           xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		   _("input_pvr: error opening pvr file (%s)\n"), filename);
           free(filename);
           return 0;
         }
         free(filename);
      }
      this->want_data = 0;
      pthread_cond_signal (&this->wake_pvr);
    }

    if(speed != XINE_SPEED_PAUSE) {

      pos = (off_t)(this->play_blk - this->page_block[this->play_page]) * PVR_BLOCK_SIZE;
      if( lseek (this->play_fd, pos, SEEK_SET) != pos ) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"input_pvr: error setting position for reading %" PRIdMAX "\n", (intmax_t)pos);
        return 0;
      }
      if( read(this->play_fd, buffer, PVR_BLOCK_SIZE) < PVR_BLOCK_SIZE ) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"input_pvr: short read from pvr file\n");
        return 0;
      }
      this->play_blk++;
    }
  }

  /* now we are done on input/demuxer thread, engine may be paused safely */
  if( this->pvr_play_paused ) {
    _x_set_speed (this->stream, XINE_SPEED_PAUSE);
    this->pvr_play_paused = 0;
  }

  return 1;
}


static int pvr_mpeg_resync (int fd) {
  uint32_t seq = 0;
  uint8_t c;

  while (seq != 0x000001ba) {
    if( read(fd, &c, 1) < 1 )
      return 0;
    seq = (seq << 8) | c;
  }
  return 1;
}

/*
 * captures data from mpeg2 encoder card to disk.
 * may wait xine to get data when in realtime mode.
 */
static void *pvr_loop (void *this_gen) {

  pvr_input_plugin_t   *this = (pvr_input_plugin_t *) this_gen;
  off_t                 num_bytes, total_bytes;
  int                   lost_sync;

  while( this->pvr_running ) {

    pthread_mutex_lock(&this->lock);
    this->valid_data = 0;
    pthread_mutex_unlock(&this->lock);

    total_bytes = 0;
    do {

      lost_sync = 0;

      pthread_mutex_lock(&this->dev_lock);
      while (total_bytes < PVR_BLOCK_SIZE) {
        num_bytes = read (this->dev_fd, this->data + total_bytes, PVR_BLOCK_SIZE-total_bytes);
        if (num_bytes <= 0) {
          if (num_bytes < 0)
            xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
		     _("input_pvr: read error (%s)\n"), strerror(errno));
          this->pvr_running = 0;
          break;
        }
        total_bytes += num_bytes;
      }

      if( this->data[0] || this->data[1] || this->data[2] != 1 || this->data[3] != 0xba ) {
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "resyncing mpeg stream\n");

        if( !pvr_mpeg_resync(this->dev_fd) ) {
          this->pvr_running = 0;
        } else {
          lost_sync = 1;
          this->data[0] = 0; this->data[1] = 0; this->data[2] = 1; this->data[3] = 0xba;
          total_bytes = 4;
        }
      }
      pthread_mutex_unlock(&this->dev_lock);

    } while( lost_sync );

    pthread_mutex_lock(&this->lock);

    if( !pvr_rec_file(this) ) {
      this->pvr_running = 0;
    }

    this->valid_data = 1;
    pthread_cond_signal (&this->has_valid_data);

    while(this->valid_data && this->play_fd == -1 &&
          this->want_data && this->pvr_playing) {
      pthread_cond_wait (&this->wake_pvr, &this->lock);
    }

    pthread_mutex_unlock(&this->lock);
  }

  pthread_exit(NULL);
}

/*
 * finishes the current recording.
 * checks this->save_page if the recording should be saved or removed.
 * moves files to a permanent diretory (save_path) using a given show
 * name (save_name) or a default one using channel and time.
 */
static void pvr_finish_recording (pvr_input_plugin_t *this) {

  char *src_filename;
  char *save_base;
  char *dst_filename;
  uint32_t i;

  lprintf("finish_recording\n");

  if( this->rec_fd != -1 ) {
    close(this->rec_fd);

    if( this->play_fd != -1 && this->play_fd != this->rec_fd )
      close(this->play_fd);

    this->rec_fd = this->play_fd = -1;

    if( this->save_page == this->show_page )
      save_base = make_base_save_name(this->channel, this->show_time);
    else
      save_base = make_base_save_name(this->channel, this->start_time);

    for( i = this->first_page; i <= this->rec_page; i++ ) {

      src_filename = make_temp_name(this, i);

      if( this->save_page == (unsigned)-1 || i < this->save_page ) {
        lprintf("erasing old pvr file (%s)\n", src_filename);

        remove(src_filename);
      } else {

        if( !this->save_name || !strlen(this->save_name) )
          dst_filename = make_save_name(this, save_base, i-this->save_page+1);
        else
          dst_filename = make_save_name(this, this->save_name, i-this->save_page+1);

        lprintf("moving (%s) to (%s)\n", src_filename, dst_filename);

        rename(src_filename,dst_filename);
        free(dst_filename);
      }
      free(src_filename);
    }

    if( this->save_page != (unsigned)-1 && (!this->save_name || !strlen(this->save_name)) ) {
      saved_show_t        *show = malloc(sizeof(saved_show_t));
      xine_event_t         event;
      xine_pvr_save_data_t data;

      show->base_name = save_base;
      show->id = ++this->saved_id;
      show->pages = this->rec_page - this->save_page + 1;
      xine_list_push_back (this->saved_shows, show);

      lprintf("sending event with base name [%s]\n", show->base_name);

      /* tell frontend the name of the saved show */
      event.type        = XINE_EVENT_PVR_REPORT_NAME;
      event.stream      = this->stream;
      event.data        = &data;
      event.data_length = sizeof(data);
      gettimeofday(&event.tv, NULL);

      data.mode = 0;
      data.id = show->id;
      strncpy(data.name, show->base_name, sizeof(data.name));
      data.name[sizeof(data.name) - 1] = '\0';

      xine_event_send(this->stream, &event);
    } else {
      free(save_base);
    }
  }

  this->first_page = 0;
  this->show_page = 0;
  this->save_page = -1;
  this->play_blk = this->rec_blk = 0;
  this->play_page = this->rec_page = 0;
  if( this->save_name )
    free( this->save_name );
  this->save_name = NULL;
  this->valid_data = 0;
  pthread_cond_signal (&this->wake_pvr);
}

/*
 * event handler: process external pvr commands
 * may switch channel, inputs, start/stop recording
 * set flag to save current session permanently
 */
static void pvr_event_handler (pvr_input_plugin_t *this) {

  xine_event_t *event;

  while ((event = xine_event_get (this->event_queue))) {
    xine_set_v4l2_data_t *v4l2_data = event->data;
    xine_pvr_save_data_t *save_data = event->data;
    xine_pvr_pause_t  *pause_data = event->data;
    xine_set_mpeg_data_t *mpeg_data = event->data;

    switch (event->type) {

    case XINE_EVENT_SET_V4L2:
      /* make sure we are not paused */
      _x_set_speed(this->stream, XINE_SPEED_NORMAL);

      if ( v4l2_data->session_id != -1) {
        if( v4l2_data->session_id != this->session ) {
          /* if session changes -> closes the old one */
          pthread_mutex_lock(&this->lock);
          pvr_finish_recording(this);
          time(&this->start_time);
          this->show_time = this->start_time;
          this->session = v4l2_data->session_id;
          this->new_session = 1;
          this->pvr_play_paused = 0;
          this->scr_tunning = 0;
          pvrscr_speed_tunning(this->scr, 1.0 );
          pvr_break_rec_page(this);
          pthread_mutex_unlock(&this->lock);
          _x_demux_flush_engine (this->stream);
        } else {
          /* no session change, break the page and store a new show_time */
          pthread_mutex_lock(&this->lock);
          pvr_break_rec_page(this);
          this->show_page = this->rec_page;
          pthread_mutex_unlock(&this->lock);
          time(&this->show_time);
        }
      }

      pthread_mutex_lock(&this->dev_lock);

      /* change input */
      if (v4l2_data->input != -1 && v4l2_data->input != this->input) {
        this->input = v4l2_data->input;

        /* as of ivtv 0.10.6: must close and reopen to set input */
        close(this->dev_fd);
        this->dev_fd = xine_open_cloexec(this->class->devname, O_RDWR);
        if (this->dev_fd < 0) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "input_pvr: error opening device %s\n", this->class->devname );
        } else {
          if( ioctl(this->dev_fd, VIDIOC_S_INPUT, &this->input) == 0 ) {
            lprintf("Tuner Input set to:%d\n", v4l2_data->input);
          } else {
            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "input_pvr: error setting v4l2 input\n");
          }
        }
      }

      /* change channel */
      if (v4l2_data->channel != -1 && v4l2_data->channel != this->channel) {
        lprintf("change channel to:%d\n", v4l2_data->channel);
        this->channel = v4l2_data->channel;
      }

      /* change frequency */
      if (v4l2_data->frequency != -1 && v4l2_data->frequency != this->frequency) {
        double freq = (double)v4l2_data->frequency / 1000.0;
        struct v4l2_frequency vf;
        struct v4l2_tuner vt;
        double fac = 16;

        memset(&vf, 0, sizeof(vf));
        memset(&vt, 0, sizeof(vt));

        this->frequency = v4l2_data->frequency;

        if (ioctl(this->dev_fd, VIDIOC_G_TUNER, &vt) == 0) {
          fac = (vt.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
        }

        vf.tuner = 0;
        vf.type = vt.type;
        vf.frequency = (__u32)(freq * fac);

        if (ioctl(this->dev_fd, VIDIOC_S_FREQUENCY, &vf) == 0) {
          lprintf("Tuner Frequency set to %d (%f.3 MHz)\n", vf.frequency, vf.frequency / fac);
        } else {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "input_pvr: error setting v4l2 frequency\n");
        }
      }

      pthread_mutex_unlock(&this->dev_lock);

      /* FIXME: also flush the device */
      /* _x_demux_flush_engine(this->stream); */

      break;


    case XINE_EVENT_PVR_SAVE:
      if( this->session != -1 ) {
        switch( save_data->mode ) {
          case 0:
            lprintf("saving from this point\n");

            pthread_mutex_lock(&this->lock);
            pvr_break_rec_page(this);
            this->save_page = this->rec_page;
            time(&this->start_time);
            pthread_mutex_unlock(&this->lock);
            break;
          case 1:
            lprintf("saving from show start\n");

            pthread_mutex_lock(&this->lock);
            this->save_page = this->show_page;
            pthread_mutex_unlock(&this->lock);
            break;
          case 2:
	    lprintf("saving everything so far\n");

            pthread_mutex_lock(&this->lock);
            this->save_page = this->first_page;
            pthread_mutex_unlock(&this->lock);
            break;
        }
      }
      if( strlen(save_data->name) ) {
        if( this->save_name )
          free( this->save_name );
        this->save_name = NULL;

        if( save_data->id < 0 ) {
          /* no id: set name for current recording */
          this->save_name = strdup(save_data->name);

        } else {
          /* search for the ID of saved shows and rename it
           * to the given name. */
          char *src_filename;
          char *dst_filename;
          saved_show_t  *show;
	  xine_list_iterator_t ite;

          pthread_mutex_lock(&this->lock);

          ite = xine_list_front (this->saved_shows);
          while (ite) {
            show = xine_list_get_value(this->saved_shows, ite);
            if( show->id == save_data->id ) {
              int i;

              for( i = 0; i < show->pages; i++ ) {

                src_filename = make_save_name(this, show->base_name, i+1);
                dst_filename = make_save_name(this, save_data->name, i+1);

                lprintf("moving (%s) to (%s)\n", src_filename, dst_filename);

                rename(src_filename,dst_filename);
                free(dst_filename);
                free(src_filename);
              }
              xine_list_remove (this->saved_shows, ite);
              free (show->base_name);
              free (show);
              break;
            }
            ite = xine_list_next (this->saved_shows, ite);
          }

          pthread_mutex_unlock(&this->lock);
        }
      }
      break;

    case XINE_EVENT_PVR_PAUSE:
      /* ignore event if trying to pause, but already paused */
      if(_x_get_speed(this->stream) != XINE_SPEED_PAUSE ||
         !pause_data->mode)
        this->pvr_play_paused = pause_data->mode;
      break;

    case XINE_EVENT_SET_MPEG_DATA: {
       struct ivtv_ioctl_codec codec;

       pthread_mutex_lock(&this->dev_lock);

       /* how lame. we must close and reopen to change bitrate. */
       close(this->dev_fd);
       this->dev_fd = xine_open_cloexec(this->class->devname, O_RDWR);
       if (this->dev_fd == -1) {
         xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		 _("input_pvr: error opening device %s\n"), this->class->devname );
         return;
       }

       if (ioctl(this->dev_fd, IVTV_IOC_G_CODEC, &codec) < 0) {
         xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		 _("input_pvr: IVTV_IOC_G_CODEC failed, maybe API changed?\n"));
       } else {
         codec.bitrate      = mpeg_data->bitrate_mean;
         codec.bitrate_peak = mpeg_data->bitrate_peak;
         codec.stream_type  = IVTV_STREAM_DVD;

         if (ioctl(this->dev_fd, IVTV_IOC_S_CODEC, &codec) < 0) {
           xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		   _("input_pvr: IVTV_IOC_S_CODEC failed, maybe API changed?\n"));
         }
       }
       pthread_mutex_unlock(&this->dev_lock);
      }
      break;

#if 0
    default:
      printf ("input_pvr: got an event, type 0x%08x\n", event->type);
#endif
    }

    xine_event_free (event);
  }
}


/*
 * pvr read_block function.
 * - adjust playing speed to keep buffers half-full
 * - check current playback mode
 * - get data from file (non-realtime) or the pvr thread (realtime)
 */
static buf_element_t *pvr_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  pvr_input_plugin_t   *this = (pvr_input_plugin_t *) this_gen;
  buf_element_t        *buf;
  int                   speed = _x_get_speed(this->stream);

  if( !this->pvr_running ) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_pvr: thread died, aborting\n");
    return NULL;
  }

  buf = fifo->buffer_pool_alloc (fifo);
  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer(buf);
    return NULL;
  }

  if( this->scr_tunning == -2 )
    speed = this->speed_before_pause;

  if( this->pvr_play_paused )
    speed = XINE_SPEED_PAUSE;

  if( this->pvr_playing && _x_stream_info_get(this->stream, XINE_STREAM_INFO_IGNORE_VIDEO) ) {
    /* video decoding has being disabled. avoid tweaking the clock */
    this->pvr_playing = 0;
    this->scr_tunning = 0;
    pvrscr_speed_tunning(this->scr, 1.0 );
    this->want_data = 0;
    pthread_cond_signal (&this->wake_pvr);
  } else if ( !this->pvr_playing && !_x_stream_info_get(this->stream,XINE_STREAM_INFO_IGNORE_VIDEO) ) {
    this->pvr_playing = 1;
    this->play_blk = this->rec_blk;
  }

  if( this->pvr_playing )
    pvr_adjust_realtime_speed(this, fifo, speed);

  pvr_event_handler(this);

  buf->content = buf->mem;

  pthread_mutex_lock(&this->lock);

  if( this->pvr_playing )
    if( !pvr_play_file(this, fifo, buf->content, speed) ) {
      buf->free_buffer(buf);
      pthread_mutex_unlock(&this->lock);
      return NULL;
    }

  if( todo == PVR_BLOCK_SIZE && speed != XINE_SPEED_PAUSE &&
      this->pvr_playing ) {
    buf->type = BUF_DEMUX_BLOCK;
    buf->size = PVR_BLOCK_SIZE;

    if(this->play_fd == -1) {

      /* realtime mode: wait for valid data from pvr thread */
      this->want_data = 1;
      while(!this->valid_data && this->pvr_running)
        pthread_cond_wait (&this->has_valid_data, &this->lock);

      this->play_blk = this->rec_blk;
      xine_fast_memcpy(buf->content, this->data, PVR_BLOCK_SIZE);

      this->valid_data = 0;
      pthread_cond_signal (&this->wake_pvr);
    }
    pthread_mutex_unlock(&this->lock);

  } else {
    pthread_mutex_unlock(&this->lock);

    buf->type = BUF_CONTROL_NOP;
    buf->size = 0;

    if(this->preview_buffers)
      this->preview_buffers--;
    else
      xine_usec_sleep (20000);
  }

  return buf;
}


static off_t pvr_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  pthread_mutex_lock(&this->lock);

  switch( origin ) {
    case SEEK_SET:
      this->play_blk = (offset / PVR_BLOCK_SIZE) + this->page_block[this->first_page];
      break;
    case SEEK_CUR:
      this->play_blk += offset / PVR_BLOCK_SIZE;
      break;
    case SEEK_END:
      this->play_blk = this->rec_blk + (offset / PVR_BLOCK_SIZE);
      break;
  }

  /* invalidate the fd if needed */
  if( this->play_fd != -1 && block_to_page(this,this->play_blk) != this->play_page ) {
    if( this->play_fd != this->rec_fd )
      close(this->play_fd);
    this->play_fd = -1;

    if( this->play_blk >= this->rec_blk )
      pvr_report_realtime(this,1);
  }
  pthread_mutex_unlock(&this->lock);

  return (off_t) (this->play_blk - this->page_block[this->first_page]) * PVR_BLOCK_SIZE;
}

static off_t pvr_plugin_get_current_pos (input_plugin_t *this_gen){
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return (off_t) (this->play_blk - this->page_block[this->first_page]) * PVR_BLOCK_SIZE;
}

static off_t pvr_plugin_get_length (input_plugin_t *this_gen) {

  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return (off_t) (this->rec_blk - this->page_block[this->first_page]) * PVR_BLOCK_SIZE;
}

static uint32_t pvr_plugin_get_blocksize (input_plugin_t *this_gen) {
  return PVR_BLOCK_SIZE;
}

static const char* pvr_plugin_get_mrl (input_plugin_t *this_gen) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return this->mrl;
}

static int pvr_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void pvr_plugin_dispose (input_plugin_t *this_gen ) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;
  void          *p;
  saved_show_t  *show;
  xine_list_iterator_t ite;

  if( this->pvr_running ) {
    lprintf("finishing pvr thread\n");

    pthread_mutex_lock(&this->lock);
    this->pvr_running = 0;
    this->want_data = 0;
    pthread_cond_signal (&this->wake_pvr);
    pthread_mutex_unlock(&this->lock);
    pthread_join (this->pvr_thread, &p);

    lprintf("pvr thread joined\n");
  }

  if (this->scr) {
    this->stream->xine->clock->unregister_scr(this->stream->xine->clock, &this->scr->scr);
    this->scr->scr.exit(&this->scr->scr);
  }

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  if (this->dev_fd != -1)
    close(this->dev_fd);

  pvr_finish_recording(this);

  free (this->mrl);

  if (this->tmp_prefix)
    free (this->tmp_prefix);

  if (this->save_prefix)
    free (this->save_prefix);

  ite = xine_list_front (this->saved_shows);
  while (ite) {
    show = xine_list_get_value(this->saved_shows, ite);
    free (show->base_name);
    free (show);
    ite = xine_list_next (this->saved_shows, ite);
  }
  xine_list_delete(this->saved_shows);
  free (this);
}

static int pvr_plugin_open (input_plugin_t *this_gen ) {
  pvr_input_plugin_t  *this = (pvr_input_plugin_t *) this_gen;
  int64_t              time;
  int                  err;
  struct ivtv_ioctl_codec codec;

  this->session = 0;
  this->rec_fd = -1;
  this->play_fd = -1;
  this->first_page = 0;
  this->show_page = 0;
  this->save_page = -1;
  this->input = -1;
  this->channel = -1;
  this->pvr_playing = 1;
  this->preview_buffers = NUM_PREVIEW_BUFFERS;

  this->saved_id = 0;

  this->dev_fd = xine_open_cloexec(this->class->devname, O_RDWR);
  if (this->dev_fd == -1) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("input_pvr: error opening device %s\n"), this->class->devname );
    return 0;
  }

  if (ioctl(this->dev_fd, IVTV_IOC_G_CODEC, &codec) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("input_pvr: IVTV_IOC_G_CODEC failed, maybe API changed?\n"));
  } else {
    codec.bitrate_mode  = 0;
    codec.bitrate	= 6000000;
    codec.bitrate_peak	= 9000000;
    codec.stream_type	= IVTV_STREAM_DVD;

    if (ioctl(this->dev_fd, IVTV_IOC_S_CODEC, &codec) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("input_pvr: IVTV_IOC_S_CODEC failed, maybe API changed?\n"));
    }
  }

  /* register our own scr provider */
  time = this->stream->xine->clock->get_current_time(this->stream->xine->clock);
  this->scr = pvrscr_init();
  this->scr->scr.start(&this->scr->scr, time);
  this->stream->xine->clock->register_scr(this->stream->xine->clock, &this->scr->scr);
  this->scr_tunning = 0;

  this->event_queue = xine_event_new_queue (this->stream);

  /* enable resample method */
  this->stream->xine->config->update_num(this->stream->xine->config,"audio.synchronization.av_sync_method",1);

  this->pvr_running = 1;

  if ((err = pthread_create (&this->pvr_thread,
			     NULL, pvr_loop, this)) != 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_NONE,
	     "input_pvr: can't create new thread (%s)\n", strerror(err));
    _x_abort();
  }

  return 1;
}

static input_plugin_t *pvr_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *data) {

  pvr_input_class_t   *cls = (pvr_input_class_t *) cls_gen;
  pvr_input_plugin_t  *this;
  char                *mrl;
  char                *aux;

  if (strncasecmp (data, "pvr:/", 5))
    return NULL;

  mrl = strdup(data);
  aux = &mrl[5];

  this = calloc(1, sizeof (pvr_input_plugin_t));
  this->class        = cls;
  this->stream       = stream;
  this->dev_fd       = -1;
  this->mrl          = mrl;
  this->max_page_age = 3;

  /* decode configuration options from mrl */
  if( strlen(aux) ) {
    this->tmp_prefix = strdup(aux);

    aux = strchr(this->tmp_prefix,'!');
    if( aux ) {
      aux[0] = '\0';
      this->save_prefix = strdup(aux+1);

      aux = strchr(this->save_prefix, '!');
      if( aux ) {
        aux[0] = '\0';
        if( atoi(aux+1) )
          this->max_page_age = atoi(aux+1);
      }
    } else {
      this->save_prefix=strdup(this->tmp_prefix);
    }
  } else {
    this->tmp_prefix=strdup("./");
    this->save_prefix=strdup("./");
  }

  lprintf("tmp_prefix=%s\n", this->tmp_prefix);
  lprintf("save_prefix=%s\n", this->save_prefix);
  lprintf("max_page_age=%d\n", this->max_page_age);

  this->input_plugin.open               = pvr_plugin_open;
  this->input_plugin.get_capabilities   = pvr_plugin_get_capabilities;
  this->input_plugin.read               = pvr_plugin_read;
  this->input_plugin.read_block         = pvr_plugin_read_block;
  this->input_plugin.seek               = pvr_plugin_seek;
  this->input_plugin.get_current_pos    = pvr_plugin_get_current_pos;
  this->input_plugin.get_length         = pvr_plugin_get_length;
  this->input_plugin.get_blocksize      = pvr_plugin_get_blocksize;
  this->input_plugin.get_mrl            = pvr_plugin_get_mrl;
  this->input_plugin.get_optional_data  = pvr_plugin_get_optional_data;
  this->input_plugin.dispose            = pvr_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  this->scr = NULL;
  this->event_queue = NULL;
  this->save_name = NULL;
  this->saved_shows = xine_list_new();

  pthread_mutex_init (&this->lock, NULL);
  pthread_mutex_init (&this->dev_lock, NULL);
  pthread_cond_init  (&this->has_valid_data,NULL);
  pthread_cond_init  (&this->wake_pvr,NULL);

  return &this->input_plugin;
}


/*
 * plugin class functions
 */
static void *init_plugin (xine_t *xine, void *data) {

  pvr_input_class_t  *this;

  this = calloc(1, sizeof (pvr_input_class_t));

  this->xine   = xine;
  this->config = xine->config;

  this->devname = this->config->register_filename(this->config,
				    "media.wintv_pvr.device",
				    PVR_DEVICE, XINE_CONFIG_STRING_IS_DEVICE_NAME,
				    _("device used for WinTV-PVR 250/350 (pvr plugin)"),
				    _("The path to the device of your WinTV card."),
				    10, NULL,
				    NULL);

  this->input_class.get_instance       = pvr_class_get_instance;
  this->input_class.identifier         = "pvr";
  this->input_class.description        = N_("WinTV-PVR 250/350 input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "pvr", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

