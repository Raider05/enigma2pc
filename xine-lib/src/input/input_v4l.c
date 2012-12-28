/*
 * Copyright (C) 2003-2008 the xine project
 * Copyright (C) 2003 J.Asselman <j.asselman@itsec-ps.nl>
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
 * v4l input plugin
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* From GStreamer's v4l plugin:
 * Because of some really cool feature in video4linux1, also known as
 * 'not including sys/types.h and sys/time.h', we had to include it
 * ourselves. In all their intelligence, these people decided to fix
 * this in the next version (video4linux2) in such a cool way that it
 * breaks all compilations of old stuff...
 * The real problem is actually that linux/time.h doesn't use proper
 * macro checks before defining types like struct timeval. The proper
 * fix here is to either fuck the kernel header (which is what we do
 * by defining _LINUX_TIME_H, an innocent little hack) or by fixing it
 * upstream, which I'll consider doing later on. If you get compiler
 * errors here, check your linux/time.h && sys/time.h header setup.
*/
#define _LINUX_TIME_H

#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

/* Used to capture the audio data */
#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#define XINE_ENABLE_EXPERIMENTAL_FEATURES

/********** logging **********/
/* #define LOG_MODULE "input_v4l" */
#define LOG_VERBOSE
/*
#define LOG
*/

#ifdef LOG
#define LOG_MODULE log_line_prefix()

static char *log_line_prefix()
{
    static int print_timestamp = 1;
    struct timeval now;
    struct tm now_tm;
    char buffer[64];

    if( print_timestamp ) {
        gettimeofday( &now, NULL );
        localtime_r( &now.tv_sec, &now_tm );
        strftime( buffer, sizeof( buffer ), "%Y-%m-%d %H:%M:%S", &now_tm );
        printf( "%s.%6.6ld: ", buffer, now.tv_usec );
    }
    return "input_v4l";
}
#endif

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#define NUM_FRAMES  15

/* Our CPU can't handle de-interlacing at 768. */
#define MAX_RES 640

typedef struct {
	int width;
	int height;
} resolution_t;

static const resolution_t resolutions[] = {
	{ 768, 576 },
	{ 640, 480 },
	{ 384, 288 },
	{ 320, 240 },
	{ 160, 120 }
};

#define NUM_RESOLUTIONS  (sizeof(resolutions)/sizeof(resolutions[0]))
#define RADIO_DEV        "/dev/radio0"
#define VIDEO_DEV        "/dev/video0"
#ifdef HAVE_ALSA
#define AUDIO_DEV	 "plughw:0,0"
#endif

static const char *const tv_standard_names[] = { "AUTO", "PAL", "NTSC", "SECAM", "OLD", NULL };
static const int tv_standard_values[] = { VIDEO_MODE_AUTO, VIDEO_MODE_PAL, VIDEO_MODE_NTSC, VIDEO_MODE_SECAM, -1 };

#if !defined(NDELAY) && defined(O_NDELAY)
#define FNDELAY O_NDELAY
#endif

typedef struct pvrscr_s pvrscr_t;

typedef struct {
  input_class_t            input_class;
  xine_t                  *xine;
} v4l_input_class_t;

typedef struct {
  input_plugin_t           input_plugin;

  xine_stream_t           *stream;
  char                    *mrl;

  off_t                    curpos;

  int		           old_interlace;
  int		           old_zoomx;
  int		           old_zoomy;
  int		           audio_only;

  buf_element_t           *frames_base;
  void                    *audio_content_base;
  void                    *video_content_base;

  /* Audio */
  buf_element_t           *aud_frames;
  pthread_mutex_t          aud_frames_lock;
  pthread_cond_t           aud_frame_freed;

#ifdef HAVE_ALSA
  /* Handle for the PCM device */
  snd_pcm_t	          *pcm_handle;

  /* Record stream (via line 1) */
  snd_pcm_stream_t         pcm_stream;

  /* Information and configuration for the PCM stream */
  snd_pcm_hw_params_t     *pcm_hwparams;

  /* Name of the PCM device, plughw:0,0?=>soundcard,device*/
  char		          *pcm_name;

  /* Use alsa to capture the sound (for a/v sync) */
  char		           audio_capture;

  int                      exact_rate;         /* Actual sample rate
						  sndpcm_hw_params_set_rate_near */
  int                      dir;                /* exact rate == rate --> dir =  0
						  exact rate  < rate --> dir = -1
						  exact rate  > rate --> dir =  1 */

  unsigned char           *pcm_data;

  int64_t                  pts_aud_start;
#endif

  int                      audio_header_sent;

  int                      rate;               /* Sample rate */
  int                      periods;            /* Number of periods */
  int                      periodsize;         /* Periodsize in bytes */
  int                      bits;

  /* Video */
  buf_element_t           *vid_frames;
  pthread_mutex_t          vid_frames_lock;
  pthread_cond_t           vid_frame_freed;

  int                      video_fd;
  int		           radio_fd;

  int                      input;
  int                      tuner;
  unsigned long	           frequency;
  unsigned long            calc_frequency;
  char		          *tuner_name;

  int		           radio;   /* ask for a radio channel */
  int		           channel; /* channel number */

  struct video_channel     video_channel;
  struct video_tuner       video_tuner;
  struct video_capability  video_cap;
  struct video_audio       audio;
  struct video_audio       audio_saved;
  struct video_mbuf        gb_buffers;

  int                      video_header_sent;

  int                      frame_format;
  const resolution_t      *resolution;
  int                      frame_size;
  int                      use_mmap;
  uint8_t                 *video_buf;
  int                      gb_frame;
  struct video_mmap        gb_buf;
  int64_t                  start_time;

  xine_event_queue_t      *event_queue;

  pvrscr_t                *scr;
  int	                   scr_tuning;

} v4l_input_plugin_t;

/*
 * ***************************************************
 * unix System Clock Reference + fine tuning
 *
 * This code is copied and paste from the input_pvr.c
 *
 * the fine tuning option is used to change play
 * speed in order to regulate fifo usage, that is,
 * trying to match the rate of generated data.
 *
 * OBS: use with audio.synchronization.av_sync_method=resample
 * ***************************************************
 */

#define SCR_PAUSED -2
#define SCR_FW -3
#define SCR_SKIP -4

struct pvrscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  int64_t          cur_pts;
  int              xine_speed;
  double           speed_factor;
  double           speed_tuning;

  pthread_mutex_t  lock;
};

static int pvrscr_get_priority(scr_plugin_t *scr)
{
  return 10; /* high priority */
}

/* Only call this when already mutex locked */
static void pvrscr_set_pivot(pvrscr_t *this)
{
  struct   timeval tv;
  int64_t pts;
  double   pts_calc;

  xine_monotonic_clock(&tv, NULL);
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
  pts      = this->cur_pts + pts_calc;

  /* This next part introduces a one off inaccuracy
   * to the scr due to rounding tv to pts.
   */
  this->cur_time.tv_sec  = tv.tv_sec;
  this->cur_time.tv_usec = tv.tv_usec;
  this->cur_pts          = pts;

  return;
}

static int pvrscr_set_fine_speed (scr_plugin_t *scr, int speed)
{
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->xine_speed   = speed;
  this->speed_factor = (double) speed * 90000.0 / XINE_FINE_SPEED_NORMAL *
    this->speed_tuning;

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void pvrscr_speed_tuning (pvrscr_t *this, double factor)
{
  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->speed_tuning = factor;
  this->speed_factor  = (double) this->xine_speed * 90000.0 / XINE_FINE_SPEED_NORMAL *
    this->speed_tuning;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_adjust (scr_plugin_t *scr, int64_t vpts)
{
  pvrscr_t        *this = (pvrscr_t*) scr;
  struct timeval   tv;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);
  this->cur_time.tv_sec  = tv.tv_sec;
  this->cur_time.tv_usec = tv.tv_usec;
  this->cur_pts          = vpts;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_start (scr_plugin_t *scr, int64_t start_vpts)
{
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);

  pvrscr_set_fine_speed (&this->scr, XINE_FINE_SPEED_NORMAL);
}

static int64_t pvrscr_get_current (scr_plugin_t *scr)
{
  pvrscr_t        *this = (pvrscr_t*) scr;
  struct timeval   tv;
  int64_t          pts;
  double           pts_calc;

  pthread_mutex_lock (&this->lock);

  xine_monotonic_clock(&tv, NULL);

  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
  pts      = this->cur_pts + pts_calc;

  pthread_mutex_unlock (&this->lock);

/*printf("returning pts %lld\n", pts);*/
  return pts;
}

static void pvrscr_exit (scr_plugin_t *scr)
{
   pvrscr_t *this = (pvrscr_t*) scr;

   pthread_mutex_destroy (&this->lock);
   free(this);
}

static pvrscr_t *XINE_MALLOC pvrscr_init (void)
{
   pvrscr_t *this;

   this = calloc(1, sizeof(pvrscr_t));

   this->scr.interface_version = 3;
   this->scr.get_priority      = pvrscr_get_priority;
   this->scr.set_fine_speed    = pvrscr_set_fine_speed;
   this->scr.adjust            = pvrscr_adjust;
   this->scr.start             = pvrscr_start;
   this->scr.get_current       = pvrscr_get_current;
   this->scr.exit              = pvrscr_exit;

   pthread_mutex_init (&this->lock, NULL);

   pvrscr_speed_tuning(this, 1.0 );
   pvrscr_set_fine_speed (&this->scr, XINE_SPEED_PAUSE);
#ifdef SCRLOG
   printf("input_v4l: scr init complete\n");
#endif

   return this;
}

/*** END COPY AND PASTE from PVR**************************/

/*** The following is copy and past from net_buf_ctrl ****/
static void report_progress (xine_stream_t *stream, int p)
{
   xine_event_t             event;
   xine_progress_data_t     prg;

   if (p == SCR_PAUSED) {
      prg.description = _("Buffer underrun...");
      p = 0;
   } else
   if (p == SCR_FW) {
      prg.description = _("Buffer overrun...");
      p = 100;
   } else
      prg.description = _("Adjusting...");

   prg.percent = (p>100)?100:p;

   event.type        = XINE_EVENT_PROGRESS;
   event.data        = &prg;
   event.data_length = sizeof (xine_progress_data_t);

   xine_event_send (stream, &event);
}

/**** END COPY AND PASTE from net_buf_ctrl ***************/

static int search_by_tuner(v4l_input_plugin_t *this, char *input_source);
static int search_by_channel(v4l_input_plugin_t *this, char *input_source);
static void v4l_event_handler(v4l_input_plugin_t *this);

/**
 * Allocate an audio frame.
 */
inline static buf_element_t *alloc_aud_frame (v4l_input_plugin_t *this)
{
   buf_element_t *frame;

   lprintf("alloc_aud_frame. trying to get lock...\n");

   pthread_mutex_lock (&this->aud_frames_lock) ;

   lprintf("got the lock\n");

   while (!this->aud_frames) {
      lprintf("no audio frame available...\n");
      pthread_cond_wait (&this->aud_frame_freed, &this->aud_frames_lock);
   }

   frame = this->aud_frames;
   this->aud_frames = this->aud_frames->next;

   pthread_mutex_unlock (&this->aud_frames_lock);

   lprintf("alloc_aud_frame done\n");

   return frame;
}

/**
 * Stores an audio frame.
 */
static void store_aud_frame (buf_element_t *frame)
{
   v4l_input_plugin_t *this = (v4l_input_plugin_t *) frame->source;

   lprintf("store_aud_frame\n");

   pthread_mutex_lock (&this->aud_frames_lock) ;

   frame->next      = this->aud_frames;
   this->aud_frames = frame;

   pthread_cond_signal (&this->aud_frame_freed);
   pthread_mutex_unlock (&this->aud_frames_lock);
}

/**
 * Allocate a video frame.
 */
inline static buf_element_t *alloc_vid_frame (v4l_input_plugin_t *this)
{
  buf_element_t *frame;

  lprintf("alloc_vid_frame. trying to get lock...\n");

  pthread_mutex_lock (&this->vid_frames_lock) ;

  lprintf("got the lock\n");

  while (!this->vid_frames) {
    lprintf("no video frame available...\n");
    pthread_cond_wait (&this->vid_frame_freed, &this->vid_frames_lock);
  }

  frame            = this->vid_frames;
  this->vid_frames = this->vid_frames->next;

  pthread_mutex_unlock (&this->vid_frames_lock);

  lprintf("alloc_vid_frame done\n");

  return frame;
}

/**
 * Stores a video frame.
 */
static void store_vid_frame (buf_element_t *frame)
{

  v4l_input_plugin_t *this = (v4l_input_plugin_t *) frame->source;

  lprintf("store_vid_frame\n");

  pthread_mutex_lock (&this->vid_frames_lock) ;

  frame->next      = this->vid_frames;
  this->vid_frames = frame;

  pthread_cond_signal (&this->vid_frame_freed);
  pthread_mutex_unlock (&this->vid_frames_lock);
}

static int extract_mrl(v4l_input_plugin_t *this, char *mrl)
{
  char   *tuner_name = NULL;
  int     frequency  = 0;
  char   *locator    = NULL;
  char   *begin      = NULL;

  if (mrl == NULL) {
    lprintf("Someone passed an empty mrl\n");
    return 0;
  }

  for (locator = mrl; *locator != '\0' && *locator !=  '/' ; locator++);

  /* Get tuner name */
  if (*locator == '/') {
    begin = ++locator;

    for (; *locator != '\0' && *locator != '/' ; locator++);

    tuner_name = (char *) strndup(begin, locator - begin);

    /* Get frequency, if available */
    sscanf(locator, "/%d", &frequency);

    /* cannot use xprintf to log in this routine */
    lprintf("input_v4l: Tuner name: %s frequency %d\n", tuner_name, frequency );
  }

  this->frequency  = frequency;
  this->tuner_name = tuner_name;

  return 1;
}

static int set_frequency(v4l_input_plugin_t *this, unsigned long frequency)
{
  int  ret = 0;
  int  fd;

  if (this->video_fd > 0)
    fd = this->video_fd;
  else
    fd = this->radio_fd;

  if (frequency != 0) {
    /* FIXME: Don't assume tuner 0 ? */
    this->tuner = 0;
    ret = ioctl(fd, VIDIOCSTUNER, &this->tuner);
    lprintf("(%d) Response on set tuner to %d\n", ret, this->tuner);
    this->video_tuner.tuner = this->tuner;

    if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
      this->calc_frequency = frequency * 16;
    } else {
      this->calc_frequency = (frequency * 16) / 1000;
    }

    ret = ioctl(fd, VIDIOCSFREQ, &this->calc_frequency);

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: set frequency (%ld) returned: %d\n", frequency, ret);
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: No frequency given. Expected syntax: v4l:/tuner/frequency\n"
            "input_v4l: Using currently tuned settings\n");
  }

  this->frequency = frequency;

  if (ret < 0)
    return ret;
  else
    return 1;
}

static int set_input_source(v4l_input_plugin_t *this, char *input_source)
{
  int ret = 0;

  if ((ret = search_by_channel(this, input_source)) != 1) {
    ret = search_by_tuner(this, input_source);
  }

  return ret;
}

static int search_by_tuner(v4l_input_plugin_t *this, char *input_source)
{
  int  ret       = 0;
  int  fd        = 0;
  int  cur_tuner = 0;

  if (this->video_fd > 0)
    fd = this->video_fd;
  else
    fd = this->radio_fd;

  this->video_tuner.tuner = cur_tuner;
  ioctl(fd, VIDIOCGCAP, &this->video_cap);

  lprintf("This device has %d channel(s)\n", this->video_cap.channels);

  for (ret = ioctl(fd, VIDIOCGTUNER, &this->video_tuner);
       ret == 0 && this->video_cap.channels > cur_tuner && strstr(this->video_tuner.name, input_source) == NULL;
       cur_tuner++) {

    this->video_tuner.tuner = cur_tuner;

    lprintf("(%d) V4L device currently set to: \n", ret);
    lprintf("Tuner:  %d\n", this->video_tuner.tuner);
    lprintf("Name:   %s\n", this->video_tuner.name);
    if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
      lprintf("Range:  %ld - %ld\n", this->video_tuner.rangelow / 16,  this->video_tuner.rangehigh * 16);
    } else {
      lprintf("Range:  %ld - %ld\n", this->video_tuner.rangelow * 1000 / 16, this->video_tuner.rangehigh * 1000 / 16);
    }
  }

  lprintf("(%d) V4L device final: \n", ret);
  lprintf("Tuner:  %d\n", this->video_tuner.tuner);
  lprintf("Name:   %s\n", this->video_tuner.name);
  if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
    lprintf("Range:  %ld - %ld\n", this->video_tuner.rangelow / 16,  this->video_tuner.rangehigh * 16);
  } else {
    lprintf("Range:  %ld - %ld\n", this->video_tuner.rangelow * 1000 / 16, this->video_tuner.rangehigh * 1000 / 16);
  }

  if (strstr(this->video_tuner.name, input_source) == NULL)
    return -1;

  return 1;
}

static int search_by_channel(v4l_input_plugin_t *this, char *input_source)
{
  int  ret = 0;
  int  fd  = 0;
  cfg_entry_t *tv_standard_entry;

  lprintf("input_source: %s\n", input_source);

  this->input = 0;

  if (this->video_fd > 0)
    fd = this->video_fd;
  else
    fd = this->radio_fd;

  /* Tune into channel */
  if (strlen(input_source) > 0) {
    for( this->video_channel.channel = 0;
         ioctl(fd, VIDIOCGCHAN, &this->video_channel) == 0;
         this->video_channel.channel++ ) {

      lprintf("V4L device currently set to:\n");
      lprintf("Channel: %d\n", this->video_channel.channel);
      lprintf("Name:    %s\n", this->video_channel.name);
      lprintf("Tuners:  %d\n", this->video_channel.tuners);
      lprintf("Flags:   %d\n", this->video_channel.flags);
      lprintf("Type:    %d\n", this->video_channel.type);
      lprintf("Norm:    %d\n", this->video_channel.norm);

      if (strstr(this->video_channel.name, input_source) != NULL) {
        this->input = this->video_channel.channel;
        break;
      }
    }

    if (strstr(this->video_channel.name, input_source) == NULL) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("Tuner name not found\n"));
      return -1;
    }

    tv_standard_entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.tv_standard");
    this->tuner_name = input_source;
    if (tv_standard_entry->num_value != 0) {
      this->video_channel.norm = tv_standard_values[ tv_standard_entry->num_value ];
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "input_v4l: TV Standard configured as STD %s (%d)\n",
              tv_standard_names[ tv_standard_entry->num_value ], this->video_channel.norm );
        ret = ioctl(fd, VIDIOCSCHAN, &this->video_channel);
    } else
        ret = ioctl(fd, VIDIOCSCHAN, &this->input);

    lprintf("(%d) Set channel to %d\n", ret, this->input);
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Not setting video source. No source given\n");
  }
  ret = ioctl(fd, VIDIOCGTUNER, &this->video_tuner);

  lprintf("(%d) Flags %d\n", ret, this->video_tuner.flags);

  lprintf("VIDEO_TUNER_PAL %s set\n", this->video_tuner.flags & VIDEO_TUNER_PAL ? "" : "not");
  lprintf("VIDEO_TUNER_NTSC %s set\n", this->video_tuner.flags & VIDEO_TUNER_NTSC ? "" : "not");
  lprintf("VIDEO_TUNER_SECAM %s set\n", this->video_tuner.flags & VIDEO_TUNER_SECAM ? "" : "not");
  lprintf("VIDEO_TUNER_LOW %s set\n", this->video_tuner.flags & VIDEO_TUNER_LOW ? "" : "not");
  lprintf("VIDEO_TUNER_NORM %s set\n", this->video_tuner.flags & VIDEO_TUNER_NORM ? "" : "not");
  lprintf("VIDEO_TUNER_STEREO_ON %s set\n", this->video_tuner.flags & VIDEO_TUNER_STEREO_ON ? "" : "not");
  lprintf("VIDEO_TUNER_RDS_ON %s set\n", this->video_tuner.flags & VIDEO_TUNER_RDS_ON ? "" : "not");
  lprintf("VIDEO_TUNER_MBS_ON %s set\n", this->video_tuner.flags & VIDEO_TUNER_MBS_ON ? "" : "not");

  switch (this->video_tuner.mode) {
  case VIDEO_MODE_PAL:
    lprintf("The tuner is in PAL mode\n");
    break;
  case VIDEO_MODE_NTSC:
    lprintf("The tuner is in NTSC mode\n");
    break;
  case VIDEO_MODE_SECAM:
    lprintf("The tuner is in SECAM mode\n");
    break;
  case VIDEO_MODE_AUTO:
    lprintf("The tuner is in AUTO mode\n");
    break;
  }

  return 1;
}

static void allocate_frames(v4l_input_plugin_t *this, unsigned dovideo)
{
  const size_t framescount = dovideo ? 2*NUM_FRAMES : NUM_FRAMES;

  /* Allocate a single memory area for both audio and video frames */
  buf_element_t *frames = this->frames_base =
    calloc(framescount, sizeof(buf_element_t));
  extra_info_t  *infos  =
    calloc(framescount, sizeof(extra_info_t));

  int i;

  uint8_t *audio_content = this->audio_content_base =
    calloc(NUM_FRAMES, this->periodsize);

  /* Set up audio frames */
  for (i = 0; i < NUM_FRAMES; i++) {
    /* Audio frame */
    frames[i].content     = audio_content;
    frames[i].type	   = BUF_AUDIO_LPCM_LE;
    frames[i].source      = this;
    frames[i].free_buffer = store_aud_frame;
    frames[i].extra_info  = &infos[i];

    audio_content += this->periodsize;
    store_aud_frame(&frames[i]);
  }

  if ( dovideo ) {
    uint8_t *video_content = this->video_content_base =
      calloc(NUM_FRAMES, this->frame_size);

    /* Set up video frames */
    for (i = NUM_FRAMES; i < 2*NUM_FRAMES; i++) {
      /* Video frame */
      frames[i].content     = video_content;
      frames[i].type	     = this->frame_format;
      frames[i].source      = this;
      frames[i].free_buffer = store_vid_frame;
      frames[i].extra_info  = &infos[i];

      video_content += this->frame_size;
      store_vid_frame(&frames[i]);
    }
  }
}

static void unmute_audio(v4l_input_plugin_t *this)
{
  int fd;

  lprintf("unmute_audio\n");

  if (this->video_fd > 0)
    fd = this->video_fd;
  else
    fd = this->radio_fd;

  ioctl(fd, VIDIOCGAUDIO, &this->audio);
  memcpy(&this->audio_saved, &this->audio, sizeof(this->audio));

  this->audio.flags  &= ~VIDEO_AUDIO_MUTE;
  this->audio.volume = 0xD000;

  ioctl(fd, VIDIOCSAUDIO, &this->audio);
}

static int open_radio_capture_device(v4l_input_plugin_t *this)
{
  int          tuner_found = 0;
  cfg_entry_t *entry;

  lprintf("open_radio_capture_device\n");

  entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.radio_device");

  if((this->radio_fd = xine_open_cloexec(entry->str_value, O_RDWR)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: error opening v4l device (%s): %s\n",
            entry->str_value, strerror(errno));
    return 0;
  }

  lprintf("Device opened, radio %d\n", this->radio_fd);

  if (set_input_source(this, this->tuner_name) > 0)
    tuner_found = 1;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);

  /* Pre-allocate some frames for audio so it doesn't have to be done during
   * capture */
  allocate_frames(this, 0);

  this->audio_only = 1;

  /* Unmute audio off video capture device */
  unmute_audio(this);

  set_frequency(this, this->frequency);

  if (tuner_found)
    return 1;
  else
    return 2;
}

/**
 * Open the video capture device.
 *
 * This opens the video capture device and if given, selects a tuner from
 * which the signal should be grabbed.
 * @return 1 on success, 0 on failure.
 */
static int open_video_capture_device(v4l_input_plugin_t *this)
{
  int          found       = 0;
  int          tuner_found = 0;
  int          ret;
  unsigned int j;
  cfg_entry_t *entry;

  lprintf("open_video_capture_device\n");

  entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.video_device");

  /* Try to open the video device */
  if((this->video_fd = xine_open_cloexec(entry->str_value, O_RDWR)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: error opening v4l device (%s): %s\n",
            entry->str_value, strerror(errno));
    return 0;
  }

  lprintf("Device opened, tv %d\n", this->video_fd);

  /* figure out the resolution */
  for (j = 0; j < NUM_RESOLUTIONS; j++)
    {
      if (resolutions[j].width <= this->video_cap.maxwidth
	  && resolutions[j].height <= this->video_cap.maxheight
	  && resolutions[j].width <= MAX_RES)
	{
	  found = 1;
	  break;
	}
    }

  if (found == 0 || resolutions[j].width < this->video_cap.minwidth
      || resolutions[j].height < this->video_cap.minheight)
    {
      /* Looks like the device does not support one of the preset resolutions */
      lprintf("Grab device does not support any preset resolutions");
      return 0;
    }

  this->resolution = &resolutions[j];

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);

  /* Pre-allocate some frames for audio and video so it doesn't have to be
   * done during capture */
  allocate_frames(this, 1);

  /* Unmute audio off video capture device */
  unmute_audio(this);

  if (strlen(this->tuner_name) > 0) {
    /* Tune into source and given frequency */
    if (set_input_source(this, this->tuner_name) <= 0)
      return 0;
    else
      tuner_found = 1;
  }

  set_frequency(this, this->frequency);

  /* Test for mmap video access */
  ret = ioctl(this->video_fd,VIDIOCGMBUF, &this->gb_buffers);

  if (ret < 0) {
    /* Device driver does not support mmap */
    /* try to use read based access */
    struct video_picture pict;
    int                  val;

    ioctl(this->video_fd, VIDIOCGPICT, &pict);

    /* try to choose a suitable video format */
    pict.palette = VIDEO_PALETTE_YUV420P;
    ret          = ioctl(this->video_fd, VIDIOCSPICT, &pict);
    if (ret < 0) {
      pict.palette = VIDEO_PALETTE_YUV422;
      ret          = ioctl(this->video_fd, VIDIOCSPICT, &pict);
      if (ret < 0) {
	close (this->video_fd);
	this->video_fd = -1;
	lprintf("Grab: no colour space format found\n");
	return 0;
      }
      else
	lprintf("Grab: format YUV 4:2:2\n");
    }
    else
      lprintf("Grab: format YUV 4:2:0\n");

    this->frame_format = pict.palette;
    val                = 1;
    ioctl(this->video_fd, VIDIOCCAPTURE, &val);

    this->use_mmap = 0;

  } else {
    /* Good, device driver support mmap. Mmap the memory */
    lprintf("using mmap, size %d\n", this->gb_buffers.size);
    this->video_buf = mmap(0, this->gb_buffers.size,
			   PROT_READ|PROT_WRITE, MAP_SHARED,
			   this->video_fd,0);
    if ((unsigned char*)-1 == this->video_buf) {
      /* mmap failed. */;
      perror("mmap");
      close (this->video_fd);
      return 0;
    }
    this->gb_frame = 0;

    /* start to grab the first frame */
    this->gb_buf.frame  = (this->gb_frame + 1) % this->gb_buffers.frames;
    this->gb_buf.height = resolutions[j].height;
    this->gb_buf.width  = resolutions[j].width;
    this->gb_buf.format = VIDEO_PALETTE_YUV420P;

    ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
    if (ret < 0 && errno != EAGAIN) {
      /* try YUV422 */
      this->gb_buf.format = VIDEO_PALETTE_YUV422;

      ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
    }
    else
      lprintf("(%d) YUV420 should work\n", ret);

    if (ret < 0) {
      if (errno != EAGAIN) {
	lprintf("grab device does not support suitable format\n");
      } else {
	lprintf("grab device does not receive any video signal\n");
      }
      close (this->video_fd);
      return 0;
    }
    this->frame_format = this->gb_buf.format;
    this->use_mmap     = 1;
  }

  switch(this->frame_format) {
  case VIDEO_PALETTE_YUV420P:
    this->frame_format = BUF_VIDEO_I420;
    this->frame_size = (resolutions[j].width * resolutions[j].height * 3) / 2;
    break;
  case VIDEO_PALETTE_YUV422:
    this->frame_format = BUF_VIDEO_YUY2;
    this->frame_size = resolutions[j].width * resolutions[j].height * 2;
    break;
  }

  /* Strip the vbi / sync signal from the image by zooming in */
  this->old_zoomx = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_X);
  this->old_zoomy = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_Y);

  xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, 103);
  xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, 103);

  /* Pre-allocate some frames for audio and video so it doesn't have to be
   * done during capture */
  allocate_frames(this, 1);

  /* If we made it here, everything went ok */
  this->audio_only = 0;
  if (tuner_found)
    return 1;
  else
    /* Not a real error, appart that the tuner name is unknown to us */
    return 2;
}

/**
 * Open audio capture device.
 *
 * This function opens an alsa capture device. This will be used to capture
 * audio data from.
 */
static int open_audio_capture_device(v4l_input_plugin_t *this)
{
#ifdef HAVE_ALSA
  int mode = 0;
  snd_pcm_uframes_t buf_size = (this->periodsize * this->periods) >> 2;
  lprintf("open_audio_capture_device\n");

  /* Allocate the snd_pcm_hw_params_t structure on the stack. */
  snd_pcm_hw_params_alloca(&this->pcm_hwparams);

  /* If we are not capturing video, open the sound device in blocking mode,
   * otherwise xine gets too many NULL bufs and doesn't seem to handle them
   * correctly. If we are capturing video, open the sound device in non-
   * blocking mode, otherwise we will loose video frames while waiting */
  if(!this->audio_only)
    mode = SND_PCM_NONBLOCK;

  /* Open the PCM device. */
  if(snd_pcm_open(&this->pcm_handle, this->pcm_name, this->pcm_stream, mode) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Error opening PCM device: %s\n", this->pcm_name);
    this->audio_capture = 0;
  }

  /* Get parameters */
  if (this->audio_capture &&
      (snd_pcm_hw_params_any(this->pcm_handle, this->pcm_hwparams) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Broken configuration for PCM device: No configurations available\n");
    this->audio_capture = 0;
  }

  /* Set access type */
  if (this->audio_capture &&
      (snd_pcm_hw_params_set_access(this->pcm_handle, this->pcm_hwparams,
				    SND_PCM_ACCESS_RW_INTERLEAVED) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Error setting SND_PCM_ACCESS_RW_INTERLEAVED\n");
    this->audio_capture = 0;
  }

  /* Set sample format */
  if (this->audio_capture &&
      (snd_pcm_hw_params_set_format(this->pcm_handle,
				    this->pcm_hwparams, SND_PCM_FORMAT_S16_LE) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Error setting SND_PCM_FORMAT_S16_LE\n");
    this->audio_capture = 0;
  }

  /* Set sample rate */
  this->exact_rate = this->rate;
  if (this->audio_capture &&
      (snd_pcm_hw_params_set_rate_near(this->pcm_handle, this->pcm_hwparams,
                                       &this->exact_rate, &this->dir) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Error setting samplerate\n");
    this->audio_capture = 0;
  }
  if (this->audio_capture && this->dir != 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Samplerate %d Hz is not supported by your hardware\n", this->rate);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: Using %d instead\n", this->exact_rate);
  }

  /* Set number of channels */
  if (this->audio_capture &&
      (snd_pcm_hw_params_set_channels(this->pcm_handle, this->pcm_hwparams, 2) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "input_v4l: Error setting PCM channels\n");
    this->audio_capture = 0;
  }

  if (this->audio_capture &&
      (snd_pcm_hw_params_set_periods(this->pcm_handle, this->pcm_hwparams, this->periods, 0) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "input_v4l: Error setting PCM periods\n");
    this->audio_capture = 0;
  }

  /* Set buffersize */
  if (this->audio_capture &&
      (snd_pcm_hw_params_set_buffer_size_near(this->pcm_handle,
					 this->pcm_hwparams,
					 &buf_size) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "input_v4l: Error setting PCM buffer size to %d\n", (int)buf_size );
    this->audio_capture = 0;
  }

  /* Apply HW parameter settings */
  if (this->audio_capture &&
      (snd_pcm_hw_params(this->pcm_handle, this->pcm_hwparams) < 0)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "input_v4l: Error Setting PCM HW params\n");
    this->audio_capture = 0;
  }

  if (this->audio_capture) {
    lprintf("Allocating memory for PCM capture :%d\n", this->periodsize);
    this->pcm_data = (unsigned char*) malloc(this->periodsize);
  } else
    this->pcm_data = NULL;

  lprintf("Audio device succesfully configured\n");
#endif
  return 0;
}

/**
 * Adjust realtime speed
 *
 * If xine is playing at normal speed, tries to adjust xines playing speed to
 * avoid buffer overrun and buffer underrun
 */
static int v4l_adjust_realtime_speed(v4l_input_plugin_t *this, fifo_buffer_t *fifo, int speed)
{
  int  num_used, num_free;
  int  scr_tuning = this->scr_tuning;

  if (fifo == NULL)
    return 0;

  num_used = fifo->size(fifo);
  num_free = NUM_FRAMES - num_used;

  if (!this->audio_only && num_used == 0 && scr_tuning != SCR_PAUSED) {
    /* Buffer is empty, and we did not pause playback */
    report_progress(this->stream, SCR_PAUSED);

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "input_v4l: Buffer empty, pausing playback (used: %d, num_free: %d)\n",
	    num_used, num_free);

    _x_set_speed(this->stream, XINE_SPEED_PAUSE);
    this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 0);

    this->scr_tuning = SCR_PAUSED;
    /*      pvrscr_speed_tuning(this->scr, 0.0); */

  } else if (num_free <= 1 && scr_tuning != SCR_SKIP) {
    this->scr_tuning = SCR_SKIP;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "input_v4l: Buffer full, skipping (used: %d, free: %d)\n", num_used, num_free);
    return 0;
  } else if (scr_tuning == SCR_PAUSED) {
    if (2 * num_used > num_free) {
      /* Playback was paused, but we have normal buffer usage again */
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_v4l: Resuming from paused (used: %d, free: %d)\n", num_used, num_free);

      this->scr_tuning = 0;

      pvrscr_speed_tuning(this->scr, 1.0);

      _x_set_speed(this->stream, XINE_SPEED_NORMAL);
      this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
    }
  } else if (scr_tuning == SCR_SKIP) {
    if (num_used < 2 * num_free) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "input_v4l: Resuming from skipping (used: %d, free %d)\n", num_used, num_free);
      this->scr_tuning = 0;
    } else {
      return 0;
    }
  } else if (speed == XINE_SPEED_NORMAL) {
    if (num_used > 2 * num_free)
      /* buffer used > 2/3. Increase playback speed to avoid buffer
       * overrun */
      scr_tuning = +1;
    else if (num_free > 2 * num_used)
      /* Buffer used < 1/3. Decrease playback speed to avoid buffer
       * underrun */
      scr_tuning = -1;
    else if ((scr_tuning > 0 && num_free > num_used) ||
	     (scr_tuning < 0 && num_used > num_free))
      /* Buffer usage is ok again. Set playback speed to normal */
      scr_tuning = 0;

    /* Check if speed adjustment should be changed */
    if (scr_tuning != this->scr_tuning) {
      this->scr_tuning = scr_tuning;
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "input_v4l: scr tuning = %d (used: %d, free: %d)\n",
              scr_tuning, num_used, num_free);
      pvrscr_speed_tuning(this->scr, 1.0 + (0.01 * scr_tuning));
    }
  } else if (this->scr_tuning) {
    /* Currently speed adjustment is on. But xine is not playing at normal
     * speed, so there is no reason why we should try to adjust our playback
     * speed
     */
    this->scr_tuning = 0;

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "input_v4l: scr tuning resetting (used: %d, free: %d\n", num_used, num_free);

    pvrscr_speed_tuning(this->scr, 1.0);
  }

  return 1;
}

/**
 * Plugin read.
 * This function is not supported by the plugin.
 */
static off_t v4l_plugin_read (input_plugin_t *this_gen, void *buf, off_t len) {
  lprintf("Read not supported\n");
  return 0;
}

/**
 * Get time.
 * Gets a pts time value.
 */
inline static int64_t get_time(void) {
  struct timeval tv;

  xine_monotonic_clock(&tv,NULL);

  return (int64_t) tv.tv_sec * 90000 + (int64_t) tv.tv_usec * 9 / 100;
}


/**
 * Plugin read block
 * Reads one data block. This is either an audio frame or an video frame
 */
static buf_element_t *v4l_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo)
{
  v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen;
  buf_element_t        *buf = NULL;
  uint8_t              *ptr;
  static char           video = 0;
  int                   speed = _x_get_speed(this->stream);

  v4l_event_handler(this);

#ifdef HAVE_ALSA
  if (!this->audio_header_sent) {
    lprintf("sending audio header\n");

    buf = alloc_aud_frame (this);

    buf->size          = 0;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;

    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->exact_rate;
    buf->decoder_info[2] = this->bits;
    buf->decoder_info[3] = 2;

    this->audio_header_sent = 1;

    return buf;
  }
#endif

  if (!this->audio_only && !this->video_header_sent) {
    xine_bmiheader bih;

    lprintf("sending video header");

    bih.biSize   = sizeof(xine_bmiheader);
    bih.biWidth  = this->resolution->width;
    bih.biHeight = this->resolution->height;

    buf = alloc_vid_frame (this);

    buf->size          = sizeof(xine_bmiheader);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;

    memcpy(buf->content, &bih, sizeof(xine_bmiheader));

    this->video_header_sent = 1;

    return buf;
  }

  if (!this->audio_only) {
    if (!v4l_adjust_realtime_speed(this, fifo, speed)) {
      return NULL;
    }
  }

  if (!this->audio_only)
    video = !video;
  else
    video = 0;

  lprintf("%lld bytes...\n", todo);

  if (this->start_time == 0)
    /* Create a start pts value */
    this->start_time = get_time(); /* this->stream->xine->clock->get_current_time(this->stream->xine->clock); */

  if (video) {
    /* Capture video */
    buf = alloc_vid_frame (this);
    buf->decoder_flags = BUF_FLAG_FRAME_START|BUF_FLAG_FRAME_END;

    this->gb_buf.frame = this->gb_frame;

    lprintf("VIDIOCMCAPTURE\n");

    while (ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf) < 0) {
      lprintf("Upper while loop\n");
      if (errno == EAGAIN) {
	lprintf("Cannot sync\n");
	continue;
      } else {
	perror("VIDIOCMCAPTURE");
	buf->free_buffer(buf);
	return NULL;
      }
    }

    this->gb_frame = (this->gb_frame + 1) % this->gb_buffers.frames;

    while (ioctl(this->video_fd, VIDIOCSYNC, &this->gb_frame) < 0 &&
	   (errno == EAGAIN || errno == EINTR))
      {
	lprintf("Waiting for videosync\n");
      }

    /* printf ("grabbing frame #%d\n", frame_num); */

    ptr      = this->video_buf + this->gb_buffers.offsets[this->gb_frame];
    buf->pts = get_time(); /* this->stream->xine->clock->get_current_time(this->stream->xine->clock); */
    xine_fast_memcpy (buf->content, ptr, this->frame_size);
  }
#ifdef HAVE_ALSA
  else if (this->audio_capture) {
    /* Record audio */
    int pcmreturn;

    if ((pcmreturn = snd_pcm_readi(this->pcm_handle, this->pcm_data, (this->periodsize)>> 2)) < 0) {
      switch (pcmreturn) {
      case -EAGAIN:
	/* No data available at the moment */
	break;
      case -EBADFD:     /* PCM device in wrong state */
	xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "input_v4l: PCM is not in the right state\n");
	break;
      case -EPIPE:      /* Buffer overrun */
	xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "input_v4l: PCM buffer Overrun (lost some samples)\n");
	/* On buffer overrun we need to re prepare the capturing pcm device */
	snd_pcm_prepare(this->pcm_handle);
	break;
      case -ESTRPIPE:   /* Suspend event */
	xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "input_v4l: PCM suspend event occured\n");
	break;
      default:	      /* Unknown */
	xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "input_v4l: Unknown PCM error code: %d\n", pcmreturn);
	snd_pcm_prepare(this->pcm_handle);
      }
    } else {
      /* Succesfully read audio data */

      if (this->pts_aud_start) {
	buf = alloc_aud_frame (this);
        buf->decoder_flags = 0;
      }

      /* We want the pts on the start of the sample. As the soundcard starts
       * sampling a new sample as soon as the read function returned with a
       * success we will save the current pts and assign the current pts to
       * that sample when we read it
       */

      /* Assign start pts to sample */
      if (buf)
	buf->pts = this->pts_aud_start;

      /* Save start pts */
      this->pts_aud_start = get_time(); /* this->stream->xine->clock->get_current_time(this->stream->xine->clock); */

      if (!buf)
	/* Skip first sample as we don't have a good pts for this one */
	return NULL;

      lprintf("Audio: Data read: %d [%d, %d]. Pos: %d\n",
	       pcmreturn, (int) (*this->pcm_data), (int) (*(this->pcm_data + this->periodsize - 3)),
	       (int) this->curpos);


      /* Tell decoder the number of bytes we have read */
      buf->size = pcmreturn<<2;

      this->curpos++;

      xine_fast_memcpy(buf->content, this->pcm_data, buf->size);
    }
  }
#endif

  lprintf("read block done\n");

  return buf;
}

/**
 * Plugin seek.
 * Not supported by the plugin.
 */
static off_t v4l_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  lprintf("seek %lld bytes, origin %d\n", offset, origin);
  return this->curpos;
}

/**
 * Plugin get length.
 * This is a live stream, and as such does not have an known end.
 */
static off_t v4l_plugin_get_length (input_plugin_t *this_gen) {
  /*
    v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen;
    off_t                 length;
  */

  return -1;
}

/**
 * Plugin get capabilitiets.
 * This plugin does not support any special capabilities.
 */
static uint32_t v4l_plugin_get_capabilities (input_plugin_t *this_gen)
{
  v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen;

  if (this->audio_only)
    return 0x10;
  else
    return 0; /* 0x10: Has audio only. */
}

/**
 * Plugin get block size.
 * Unsupported by the plugin.
 */
static uint32_t v4l_plugin_get_blocksize (input_plugin_t *this_gen)
{
  return 0;
}

/**
 * Plugin get current pos.
 * Unsupported by the plugin.
 */
static off_t v4l_plugin_get_current_pos (input_plugin_t *this_gen){
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  /*
    printf ("current pos is %lld\n", this->curpos);
  */

  return this->curpos;
}

/**
 * Event handler.
 *
 * Processes events from a frontend. This way frequencies can be changed
 * without closing the v4l plugin.
 */
static void v4l_event_handler (v4l_input_plugin_t *this) {
  xine_event_t *event;

  while ((event = xine_event_get (this->event_queue))) {
    xine_set_v4l2_data_t *v4l2_data = event->data;

    switch (event->type) {
    case XINE_EVENT_SET_V4L2:
      if( v4l2_data->input != this->input ||
	  v4l2_data->channel != this->channel ||
	  v4l2_data->frequency != this->frequency ) {

	this->input     = v4l2_data->input;
	this->channel   = v4l2_data->channel;
	this->frequency = v4l2_data->frequency;

	lprintf("Switching to input:%d chan:%d freq:%.2f\n",
		 v4l2_data->input,
		 v4l2_data->channel,
		 (float)v4l2_data->frequency);

	set_frequency(this, this->frequency);
	_x_demux_flush_engine(this->stream);
      }
      break;
      /*	 default:

      lprintf("Got an event, type 0x%08x\n", event->type);
      */
    }

    xine_event_free (event);
  }
}

/**
 * Dispose plugin.
 *
 * Closes the plugin, restore the V4L device in the initial state (volume) and
 * frees the allocated memory
 */
static void v4l_plugin_dispose (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  if(this->mrl)
    free(this->mrl);

  if (this->scr) {
    this->stream->xine->clock->unregister_scr(this->stream->xine->clock, &this->scr->scr);
    this->scr->scr.exit(&this->scr->scr);
  }

  /* Close and free video device */
  if (this->tuner_name)
    free(this->tuner_name);

  /* Close video device only if device was opened */
  if (this->video_fd > 0) {

    /* Restore v4l audio volume */
    lprintf("Restoring v4l audio volume %d\n",
	     ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio_saved));
    ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio_saved);

    /* Unmap memory */
    if (this->video_buf != NULL &&
	munmap(this->video_buf, this->gb_buffers.size) != 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "input_v4l: Could not unmap video memory: %s\n", strerror(errno));
    } else
      lprintf("Succesfully unmapped video memory (size %d)\n", this->gb_buffers.size);

    lprintf("Closing video filehandler %d\n", this->video_fd);

    /* Now close the video device */
    if (close(this->video_fd) != 0)
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "input_v4l: Error while closing video file handler: %s\n", strerror(errno));
    else
      lprintf("Video device succesfully closed\n");

    /* Restore zoom setting */
    xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, this->old_zoomx);
    xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, this->old_zoomy);
  }

  if (this->radio_fd > 0) {
    close(this->radio_fd);
  }

#ifdef HAVE_ALSA
  /* Close audio device */
  if (this->pcm_handle) {
    snd_pcm_drop(this->pcm_handle);
    snd_pcm_close(this->pcm_handle);
  }

  if (this->pcm_data) {
    free(this->pcm_data);
  }

  if (this->pcm_name) {
    free(this->pcm_name);
  }
#endif

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  /* All the frames, both video and audio, are allocated in a single
     memory area pointed by the frames_base pointer. The content of
     the frames is divided in two areas, one pointed by
     audio_content_base and the other by video_content_base. The
     extra_info structures are all allocated in the first frame
     data. */
  free(this->audio_content_base);
  free(this->video_content_base);
  if (this->frames_base)
    free(this->frames_base->extra_info);
  free(this->frames_base);

#ifdef LOG
  printf("\n");
#endif

  free (this);

  lprintf("plugin     Bye bye! \n");
}

/**
 * Get MRL.
 *
 * Get the current MRL used by the plugin.
 */
static const char* v4l_plugin_get_mrl (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  return this->mrl;
}

static int v4l_plugin_get_optional_data (input_plugin_t *this_gen,
                                         void *data, int data_type) {
  /* v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen; */

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int v4l_plugin_radio_open (input_plugin_t *this_gen)
{
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  if(open_radio_capture_device(this) != 1)
    return 0;

  open_audio_capture_device(this);

#ifdef HAVE_ALSA
  this->start_time     = 0;
  this->pts_aud_start  = 0;
  this->curpos         = 0;
  this->event_queue    = xine_event_new_queue (this->stream);
#endif

  return 1;
}


static int v4l_plugin_video_open (input_plugin_t *this_gen)
{
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;
  int64_t             time;

  if(!open_video_capture_device(this))
    return 0;

  open_audio_capture_device(this);

#ifdef HAVE_ALSA
  this->pts_aud_start = 0;
#endif
  this->start_time    = 0;
  this->curpos        = 0;

  /* Register our own scr provider */
  time                = this->stream->xine->clock->get_current_time(this->stream->xine->clock);
  this->scr           = pvrscr_init();
  this->scr->scr.start(&this->scr->scr, time);
  this->stream->xine->clock->register_scr(this->stream->xine->clock, &this->scr->scr);
  this->scr_tuning    = 0;

  /* enable resample method */
  this->stream->xine->config->update_num(this->stream->xine->config, "audio.synchronization.av_sync_method", 1);

  this->event_queue = xine_event_new_queue (this->stream);

  return 1;
}

/**
 * Create a new instance.
 *
 * Creates a new instance of the plugin. Doesn't initialise the V4L device,
 * does initialise the structure.
 */
static input_plugin_t *v4l_class_get_instance (input_class_t *cls_gen,
					       xine_stream_t *stream, const char *data)
{
  /* v4l_input_class_t  *cls = (v4l_input_class_t *) cls_gen; */
  v4l_input_plugin_t *this;
#ifdef HAVE_ALSA
  cfg_entry_t        *entry;
#endif
  char               *mrl     = strdup(data);

  /* Example mrl:  v4l:/Television/62500 */
  if(!mrl || strncasecmp(mrl, "v4l:/", 5)) {
    free(mrl);
    return NULL;
  }

  this = calloc(1, sizeof (v4l_input_plugin_t));

  extract_mrl(this, mrl);

  this->stream        = stream;
  this->mrl           = mrl;
  this->video_buf     = NULL;
  this->video_fd      = -1;
  this->radio_fd      = -1;
  this->event_queue   = NULL;
  this->scr           = NULL;
#ifdef HAVE_ALSA
  this->pcm_data      = NULL;
  this->pcm_hwparams  = NULL;

  /* Audio */
  this->pcm_stream    = SND_PCM_STREAM_CAPTURE;
  entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.audio_device");
  this->pcm_name      = strdup (entry->str_value);
  this->audio_capture = 1;
#endif
  this->rate          = 44100;
  this->periods       = 2;
  this->periodsize    = 2 * 8192;
  this->bits          = 16;

  pthread_mutex_init (&this->aud_frames_lock, NULL);
  pthread_cond_init  (&this->aud_frame_freed, NULL);

  pthread_mutex_init (&this->vid_frames_lock, NULL);
  pthread_cond_init  (&this->vid_frame_freed, NULL);

  this->input_plugin.get_capabilities  = v4l_plugin_get_capabilities;
  this->input_plugin.read              = v4l_plugin_read;
  this->input_plugin.read_block        = v4l_plugin_read_block;
  this->input_plugin.seek              = v4l_plugin_seek;
  this->input_plugin.get_current_pos   = v4l_plugin_get_current_pos;
  this->input_plugin.get_length        = v4l_plugin_get_length;
  this->input_plugin.get_blocksize     = v4l_plugin_get_blocksize;
  this->input_plugin.get_mrl           = v4l_plugin_get_mrl;
  this->input_plugin.dispose           = v4l_plugin_dispose;
  this->input_plugin.get_optional_data = v4l_plugin_get_optional_data;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

static input_plugin_t *v4l_class_get_video_instance (input_class_t *cls_gen,
						     xine_stream_t *stream, const char *data)
{
  v4l_input_plugin_t  *this  = NULL;
  int                  is_ok = 1;
  cfg_entry_t         *entry;

  this = (v4l_input_plugin_t *) v4l_class_get_instance (cls_gen, stream, data);

  if (this)
    this->input_plugin.open = v4l_plugin_video_open;
  else
    return NULL;

  entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.video_device");

  /* Try to open the video device */
  if((this->video_fd = xine_open_cloexec(entry->str_value, O_RDWR)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: error opening v4l device (%s): %s\n",
            entry->str_value, strerror(errno));
    is_ok = 0;
  } else
    lprintf("Device opened, tv %d\n", this->video_fd);

  /* Get capabilities */
  if (is_ok && ioctl(this->video_fd, VIDIOCGCAP, &this->video_cap) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: v4l card doesn't support some features needed by xine\n");
    is_ok = 0;;
  }

  if (is_ok && !(this->video_cap.type & VID_TYPE_CAPTURE)) {
    /* Capture is not supported by the device. This is a must though! */
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: v4l card doesn't support frame grabbing\n");
    is_ok = 0;
  }

  if (is_ok && set_input_source(this, this->tuner_name) <= 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: unable to locate the tuner name (%s) on your v4l card\n",
            this->tuner_name);
    is_ok = 0;
  }

  if (this->video_fd > 0) {
    close(this->video_fd);
    this->video_fd = -1;
  }

  if (!is_ok) {
    v4l_plugin_dispose((input_plugin_t *) this);
    return NULL;
  }

  return &this->input_plugin;
}


static input_plugin_t *v4l_class_get_radio_instance (input_class_t *cls_gen,
						     xine_stream_t *stream, const char *data)
{
  v4l_input_plugin_t *this  = NULL;
  int                 is_ok = 1;
  cfg_entry_t        *entry;

  if (strstr(data, "Radio") == NULL)
    return NULL;

  this = (v4l_input_plugin_t *) v4l_class_get_instance (cls_gen, stream, data);

  if (this)
    this->input_plugin.open = v4l_plugin_radio_open;
  else
    return NULL;

  entry = this->stream->xine->config->lookup_entry(this->stream->xine->config,
                                                   "media.video4linux.radio_device");

  if((this->radio_fd = xine_open_cloexec(entry->str_value, O_RDWR)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: error opening v4l device (%s): %s\n",
            entry->str_value, strerror(errno));
    is_ok = 0;
  } else
    lprintf("Device opened, radio %d\n", this->radio_fd);

  if (is_ok && set_input_source(this, this->tuner_name) <= 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "input_v4l: unable to locate the tuner name (%s) on your v4l card\n",
            this->tuner_name);
    is_ok = 0;
  }

  if (this->radio_fd > 0) {
    close(this->radio_fd);
    this->radio_fd = -1;
  }

  if (!is_ok) {
    v4l_plugin_dispose((input_plugin_t *) this);
    return NULL;
  }

  return &this->input_plugin;
}


/*
 * v4l input plugin class stuff
 */
static void *init_video_class (xine_t *xine, void *data)
{
  v4l_input_class_t  *this;
  config_values_t    *config = xine->config;

  this = calloc(1, sizeof (v4l_input_class_t));

  this->xine                           = xine;

  this->input_class.get_instance       = v4l_class_get_video_instance;
  this->input_class.identifier         = "v4l";
  this->input_class.description        = N_("v4l tv input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  config->register_filename (config, "media.video4linux.video_device",
			   VIDEO_DEV, XINE_CONFIG_STRING_IS_DEVICE_NAME,
			   _("v4l video device"),
			   _("The path to your Video4Linux video device."),
			   10, NULL, NULL);
#ifdef HAVE_ALSA
  config->register_filename (config, "media.video4linux.audio_device",
			   AUDIO_DEV, 0,
			   _("v4l ALSA audio input device"),
			   _("The name of the audio device which corresponds "
			     "to your Video4Linux video device."),
			   10, NULL, NULL);
#endif
  config->register_enum (config, "media.video4linux.tv_standard", 0 /* auto */,
                        tv_standard_names, _("v4l TV standard"),
                        _("Selects the TV standard of the input signals. "
                        "Either: AUTO, PAL, NTSC or SECAM. "), 20, NULL, NULL);

  return this;
}

static void *init_radio_class (xine_t *xine, void *data)
{
  v4l_input_class_t  *this;
  config_values_t    *config = xine->config;

  this = calloc(1, sizeof (v4l_input_class_t));

  this->xine                           = xine;

  this->input_class.get_instance       = v4l_class_get_radio_instance;
  this->input_class.identifier         = "v4l";
  this->input_class.description        = N_("v4l radio input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  config->register_filename (config, "media.video4linux.radio_device",
			   RADIO_DEV, XINE_CONFIG_STRING_IS_DEVICE_NAME,
			   _("v4l radio device"),
			   _("The path to your Video4Linux radio device."),
			   10, NULL, NULL);

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "v4l_radio", XINE_VERSION_CODE, NULL, init_radio_class },
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "v4l_tv", XINE_VERSION_CODE, NULL, init_video_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

/*
 * vim:sw=3:sts=3:
 */
