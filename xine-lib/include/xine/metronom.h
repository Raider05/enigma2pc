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
 *
 * metronom: general pts => virtual calculation/assoc
 *
 * virtual pts: unit 1/90000 sec, always increasing
 *              can be used for synchronization
 *              video/audio frame with same pts also have same vpts
 *              but pts is likely to differ from vpts
 *
 * the basic idea is:
 *    video_pts + video_wrap_offset = video_vpts
 *    audio_pts + audio_wrap_offset = audio_vpts
 *
 *  - video_wrap_offset should be equal to audio_wrap_offset as to have
 *    perfect audio and video sync. They will differ on brief periods due
 *    discontinuity correction.
 *  - metronom should also interpolate vpts values most of the time as
 *    video_pts and audio_vpts are not given for every frame.
 *  - corrections to the frame rate may be needed to cope with bad
 *    encoded streams.
 */

#ifndef HAVE_METRONOM_H
#define HAVE_METRONOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

#include <xine/video_out.h>
#include <xine.h>

typedef struct metronom_s metronom_t ;
typedef struct metronom_clock_s metronom_clock_t;
typedef struct scr_plugin_s scr_plugin_t;

/* metronom prebuffer can be adjusted with XINE_PARAM_METRONOM_PREBUFFER.
 * it sets how much the first video/audio frame should be delayed to
 * have some prebuffering at the output layers. reducing this value (about
 * 1/8 sec) may result in faster seeking (good to simulate play backwards,
 * for example).
 */
#define PREBUFFER_PTS_OFFSET  12000

  /* see below */
#define DISC_STREAMSTART 0
#define DISC_RELATIVE    1
#define DISC_ABSOLUTE    2
#define DISC_STREAMSEEK  3

struct metronom_s {

  /*
   * called by audio output driver to inform metronom about current audio
   * samplerate
   *
   * parameter pts_per_smpls : 1/90000 sec per 65536 samples
   */
  void (*set_audio_rate) (metronom_t *self, int64_t pts_per_smpls);

  /*
   * called by video output driver for *every* frame
   *
   * parameter frame containing pts, scr, ... information
   *
   * will set vpts field in frame
   *
   * this function will also update video_wrap_offset if a discontinuity
   * is detected (read the comentaries below about discontinuities).
   *
   */

  void (*got_video_frame) (metronom_t *self, vo_frame_t *frame);

  /*
   * called by audio output driver whenever audio samples are delivered to it
   *
   * parameter pts      : pts for audio data if known, 0 otherwise
   *           nsamples : number of samples delivered
   *
   * return value: virtual pts for audio data
   *
   * this function will also update audio_wrap_offset if a discontinuity
   * is detected (read the comentaries below about discontinuities).
   *
   */

  int64_t (*got_audio_samples) (metronom_t *self, int64_t pts,
				int nsamples);

  /*
   * called by SPU decoder whenever a packet is delivered to it
   *
   * parameter pts      : pts for SPU packet if known, 0 otherwise
   *
   * return value: virtual pts for SPU packet
   * (this is the only pts to vpts function that cannot update the wrap_offset
   * due to the lack of regularity on spu packets)
   */

  int64_t (*got_spu_packet) (metronom_t *self, int64_t pts);

  /*
   * tell metronom about discontinuities.
   *
   * these functions are called due to a discontinuity detected at
   * demux stage.
   *
   * there are different types of discontinuities:
   *
   * DISC_STREAMSTART : new stream starts, expect pts values to start
   *                    from zero immediately
   * DISC_RELATIVE    : typically a wrap-around, expect pts with
   *                    a specified offset from the former ones soon
   * DISC_ABSOLUTE    : typically a new menu stream (nav packets)
   *                    pts will start from given value soon
   * DISC_STREAMSEEK  : used by video and audio decoder loop,
   *                    when a buffer with BUF_FLAG_SEEK set is encountered;
   *                    applies the necessary vpts offset for the seek in
   *                    metronom, but keeps the vpts difference between
   *                    audio and video, so that metronom doesn't cough
   */
  void (*handle_audio_discontinuity) (metronom_t *self, int type, int64_t disc_off);
  void (*handle_video_discontinuity) (metronom_t *self, int type, int64_t disc_off);

  /*
   * set/get options for metronom, constants see below
   */
  void (*set_option) (metronom_t *self, int option, int64_t value);
  int64_t (*get_option) (metronom_t *self, int option);

  /*
   * set a master metronom
   * this is currently useful to sync independently generated streams
   * (e.g. by post plugins) to the discontinuity domain of another
   * metronom
   */
  void (*set_master) (metronom_t *self, metronom_t *master);

  void (*exit) (metronom_t *self);

#ifdef METRONOM_INTERNAL
  /*
   * metronom internal stuff
   */
  xine_t         *xine;

  metronom_t     *master;

  int64_t         pts_per_smpls;

  int64_t         video_vpts;
  int64_t         spu_vpts;
  int64_t         audio_vpts;
  int64_t         audio_vpts_rmndr;  /* the remainder for integer division */

  int64_t         vpts_offset;

  int64_t         video_drift;
  int64_t         video_drift_step;

  int             audio_samples;
  int64_t         audio_drift_step;

  int64_t         prebuffer;
  int64_t         av_offset;
  int64_t         spu_offset;

  pthread_mutex_t lock;

  int             have_video;
  int             have_audio;
  int             video_discontinuity_count;
  int             audio_discontinuity_count;
  int             discontinuity_handled_count;
  pthread_cond_t  video_discontinuity_reached;
  pthread_cond_t  audio_discontinuity_reached;

  int             force_video_jump;
  int             force_audio_jump;

  int64_t         img_duration;
  int             img_cpt;
  int64_t         last_video_pts;
  int64_t         last_audio_pts;

  int             video_mode;
#endif
};

/*
 * metronom options
 */

#define METRONOM_AV_OFFSET        2
#define METRONOM_ADJ_VPTS_OFFSET  3
#define METRONOM_FRAME_DURATION   4
#define METRONOM_SPU_OFFSET       5
#define METRONOM_VPTS_OFFSET      6
#define METRONOM_PREBUFFER        7
#define METRONOM_VPTS             8
/* METRONOM_LOCK can be used to lock metronom when multiple options needs to be fetched atomically (ex. VPTS_OFFSET and AV_OFFSET).
 * example:
 *   metronom->set_option(metronom, METRONOM_LOCK, 1);
 *   vpts_offset = metronom->get_option(metronom, METRONOM_VPTS_OFFSET|METRONOM_NO_LOCK);
 *   av_offset   = metronom->get_option(metronom, METRONOM_AV_OFFSET|METRONOM_NO_LOCK);
 *   metronom->set_option(metronom, METRONOM_LOCK, 0);
 */
#define METRONOM_LOCK             9
#define METRONOM_NO_LOCK          0x8000

metronom_t *_x_metronom_init (int have_video, int have_audio, xine_t *xine) XINE_MALLOC XINE_PROTECTED;

/* FIXME: reorder this structure on the next cleanup to remove the dummies */
struct metronom_clock_s {

  /*
   * set/get options for clock, constants see below
   */
  void (*set_option) (metronom_clock_t *self, int option, int64_t value);
  int64_t (*get_option) (metronom_clock_t *self, int option);

  /*
   * system clock reference (SCR) functions
   */

#ifdef METRONOM_CLOCK_INTERNAL
  /*
   * start clock (no clock reset)
   * at given pts
   */
  void (*start_clock) (metronom_clock_t *self, int64_t pts);


  /*
   * stop metronom clock
   */
  void (*stop_clock) (metronom_clock_t *self);


  /*
   * resume clock from where it was stopped
   */
  void (*resume_clock) (metronom_clock_t *self);
#else
  void *dummy1;
  void *dummy2;
  void *dummy3;
#endif


  /*
   * get current clock value in vpts
   */
  int64_t (*get_current_time) (metronom_clock_t *self);


  /*
   * adjust master clock to external timer (e.g. audio hardware)
   */
  void (*adjust_clock) (metronom_clock_t *self, int64_t desired_pts);

#ifdef METRONOM_CLOCK_INTERNAL
  /*
   * set clock speed
   * for constants see xine_internal.h
   */

  int (*set_fine_speed) (metronom_clock_t *self, int speed);
#else
  void *dummy4;
#endif

  /*
   * (un)register a System Clock Reference provider at the metronom
   */
  int    (*register_scr) (metronom_clock_t *self, scr_plugin_t *scr);
  void (*unregister_scr) (metronom_clock_t *self, scr_plugin_t *scr);

#ifdef METRONOM_CLOCK_INTERNAL
  void (*exit) (metronom_clock_t *self);

  xine_t         *xine;

  scr_plugin_t   *scr_master;
  scr_plugin_t  **scr_list;
  pthread_t       sync_thread;
  int             thread_running;
  int             scr_adjustable;
#else
  void *dummy5;
  void *dummy6;
  void *dummy7;
  void *dummy8;
  pthread_t dummy9;
  int dummy10;
  int dummy11;
#endif

  int speed;

#ifdef METRONOM_CLOCK_INTERNAL
  pthread_mutex_t lock;
  pthread_cond_t  cancel;
#endif
};

metronom_clock_t *_x_metronom_clock_init(xine_t *xine) XINE_MALLOC XINE_PROTECTED;

/*
 * clock options
 */

#define CLOCK_SCR_ADJUSTABLE   1

/*
 * SCR (system clock reference) plugins
 */

struct scr_plugin_s
{
  int (*get_priority) (scr_plugin_t *self);

  /*
   * set/get clock speed
   *
   * for speed constants see xine_internal.h
   * returns actual speed
   */

  int (*set_fine_speed) (scr_plugin_t *self, int speed);

  void (*adjust) (scr_plugin_t *self, int64_t vpts);

  void (*start) (scr_plugin_t *self, int64_t start_vpts);

  int64_t (*get_current) (scr_plugin_t *self);

  void (*exit) (scr_plugin_t *self);

  metronom_clock_t *clock;

  int interface_version;
};

#ifdef __cplusplus
}
#endif

#endif
