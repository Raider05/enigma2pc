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

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * include public part of xine header
 */

#include <xine.h>
#include <xine/refcounter.h>
#include <xine/input_plugin.h>
#include <xine/demux.h>
#include <xine/video_out.h>
#include <xine/audio_out.h>
#include <xine/metronom.h>
#include <xine/osd.h>
#include <xine/xineintl.h>
#include <xine/plugin_catalog.h>
#include <xine/video_decoder.h>
#include <xine/audio_decoder.h>
#include <xine/spu_decoder.h>
#include <xine/scratch.h>
#include <xine/broadcaster.h>
#include <xine/io_helper.h>
#include <xine/info_helper.h>
#include <xine/alphablend.h>

#define XINE_MAX_EVENT_LISTENERS         50
#define XINE_MAX_EVENT_TYPES             100
#define XINE_MAX_TICKET_HOLDER_THREADS   64

/* used by plugin loader */
#define XINE_VERSION_CODE                XINE_MAJOR_VERSION*10000+XINE_MINOR_VERSION*100+XINE_SUB_VERSION


/*
 * log constants
 */

#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_PLUGIN    1
#define XINE_LOG_TRACE     2
#define XINE_LOG_NUM       3 /* # of log buffers defined */

#define XINE_STREAM_INFO_MAX 99

typedef struct xine_ticket_s xine_ticket_t;

/*
 * the "big" xine struct, holding everything together
 */

#ifndef XDG_BASEDIR_H
/* present here for internal convenience only */
typedef struct { void *reserved; } xdgHandle;
#endif

struct xine_s {

  config_values_t           *config;

  plugin_catalog_t          *plugin_catalog;

  int                        verbosity;

  int                        demux_strategy;
  char                      *save_path;

  /* log output that may be presented to the user */
  scratch_buffer_t          *log_buffers[XINE_LOG_NUM];

  xine_list_t               *streams;
  pthread_mutex_t            streams_lock;

  metronom_clock_t          *clock;

  /** Handle for libxdg-basedir functions. */
  xdgHandle                  basedir_handle;

#ifdef XINE_ENGINE_INTERNAL
  xine_ticket_t             *port_ticket;
  pthread_mutex_t            log_lock;

  xine_log_cb_t              log_cb;
  void                      *log_cb_user_data;
#endif
};

/* FIXME-ABI Some global flag bits */
/* See xine_set_flags() */
#ifdef XINE_ENGINE_INTERNAL
extern int _x_flags XINE_PROTECTED;
#endif

/*
 * xine thread tickets
 */

struct xine_ticket_s {

  /* the ticket owner must assure to check for ticket revocation in
   * intervals of finite length; this means that you must release
   * the ticket before any operation that might block
   *
   * you must never write to this member directly
   */
  int    ticket_revoked;

  /* apply for a ticket; between acquire and relese of an irrevocable
   * ticket (be sure to pair them properly!), it is guaranteed that you
   * will never be blocked by ticket revocation */
  void (*acquire)(xine_ticket_t *self, int irrevocable);

  /* give a ticket back */
  void (*release)(xine_ticket_t *self, int irrevocable);

  /* renew a ticket, when it has been revoked, see ticket_revoked above;
   * irrevocable must be set to one, if your thread might have acquired
   * irrevocable tickets you don't know of; set it to zero only when
   * you know that this is impossible */
  void (*renew)(xine_ticket_t *self, int irrevocable);

#ifdef XINE_ENGINE_INTERNAL
  /* allow handing out new tickets */
  void (*issue)(xine_ticket_t *self, int atomic);

  /* revoke all tickets and deny new ones;
   * a pair of atomic revoke and issue cannot be interrupted by another
   * revocation or by other threads acquiring tickets */
  void (*revoke)(xine_ticket_t *self, int atomic);

  /* behaves like acquire() but doesn't block the calling thread; when
   * the thread would have been blocked, 0 is returned otherwise 1
   * this function acquires a ticket even if ticket revocation is active */
  int (*acquire_nonblocking)(xine_ticket_t *self, int irrevocable);

  /* behaves like release() but doesn't block the calling thread; should
   * be used in combination with acquire_nonblocking() */
  void (*release_nonblocking)(xine_ticket_t *self, int irrevocable);

  int (*lock_port_rewiring)(xine_ticket_t *self, int ms_timeout);
  void (*unlock_port_rewiring)(xine_ticket_t *self);

  void (*dispose)(xine_ticket_t *self);

  pthread_mutex_t lock;
  pthread_mutex_t revoke_lock;
  pthread_cond_t  issued;
  pthread_cond_t  revoked;
  int             tickets_granted;
  int             irrevocable_tickets;
  int             pending_revocations;
  int             atomic_revoke;
  pthread_t       atomic_revoker_thread;
  pthread_mutex_t port_rewiring_lock;
  struct {
    int count;
    pthread_t holder;
  } *holder_threads;
  unsigned        holder_thread_count;
#endif
};

/*
 * xine event queue
 */

struct xine_event_queue_s {
  xine_list_t               *events;
  pthread_mutex_t            lock;
  pthread_cond_t             new_event;
  pthread_cond_t             events_processed;
  xine_stream_t             *stream;
  pthread_t                 *listener_thread;
  void                      *user_data;
  xine_event_listener_cb_t   callback;
  int                        callback_running;
};

/*
 * xine_stream - per-stream parts of the xine engine
 */

struct xine_stream_s {

  /* reference to xine context */
  xine_t                    *xine;

  /* metronom instance used by current stream */
  metronom_t                *metronom;

  /* demuxers use input_plugin to read data */
  input_plugin_t            *input_plugin;

  /* used by video decoders */
  xine_video_port_t         *video_out;

  /* demuxers send data to video decoders using this fifo */
  fifo_buffer_t             *video_fifo;

  /* used by audio decoders */
  xine_audio_port_t         *audio_out;

  /* demuxers send data to audio decoders using this fifo */
  fifo_buffer_t             *audio_fifo;

  /* provide access to osd api */
  osd_renderer_t            *osd_renderer;

  /* master/slave streams */
  xine_stream_t             *master; /* usually a pointer to itself */
  xine_stream_t             *slave;

  /* input_dvd uses this one. is it possible to add helper functions instead? */
  spu_decoder_t             *spu_decoder_plugin;

  /* dxr3 use this one, should be possible to fix to use the port instead */
  vo_driver_t               *video_driver;

  /* these definitely should be made private! */
  int                        audio_channel_auto;
  int                        spu_decoder_streamtype;
  int                        spu_channel_user;
  int                        spu_channel_auto;
  int                        spu_channel_letterbox;
  int                        spu_channel;

  /* current content detection method, see METHOD_BY_xxx */
  int                        content_detection_method;

#ifdef XINE_ENGINE_INTERNAL
  /* these are private variables, plugins must not access them */

  int                        status;

  /* lock controlling speed change access */
  pthread_mutex_t            speed_change_lock;
  uint32_t                   ignore_speed_change:1;  /*< speed changes during stop can be disastrous */
  uint32_t                   video_thread_created:1;
  uint32_t                   audio_thread_created:1;
  uint32_t                   first_frame_flag:2;
  uint32_t                   demux_action_pending:1;
  uint32_t                   demux_thread_created:1;
  uint32_t                   demux_thread_running:1;
  uint32_t                   slave_is_subtitle:1;    /*< ... and will be automaticaly disposed */
  uint32_t                   emergency_brake:1;      /*< something went really wrong and this stream must be
                                                      *  stopped. usually due some fatal error on output
                                                      *  layers as they cannot call xine_stop. */
  uint32_t                   early_finish_event:1;   /*< do not wait fifos get empty before sending event */
  uint32_t                   gapless_switch:1;       /*< next stream switch will be gapless */
  uint32_t                   keep_ao_driver_open:1;

  input_class_t             *eject_class;
  demux_plugin_t            *demux_plugin;

/*  vo_driver_t               *video_driver;*/
  pthread_t                  video_thread;
  video_decoder_t           *video_decoder_plugin;
  extra_info_t              *video_decoder_extra_info;
  int                        video_decoder_streamtype;
  int                        video_channel;

  uint32_t                   audio_track_map[50];
  int                        audio_track_map_entries;

  int                        audio_decoder_streamtype;
  pthread_t                  audio_thread;
  audio_decoder_t           *audio_decoder_plugin;
  extra_info_t              *audio_decoder_extra_info;

  uint32_t                   audio_type;
  /* *_user: -2 => off
             -1 => auto (use *_auto value)
	    >=0 => respect the user's choice
  */
  int                        audio_channel_user;
/*  int                        audio_channel_auto; */

/*  spu_decoder_t             *spu_decoder_plugin; */
/*  int                        spu_decoder_streamtype; */
  uint32_t                   spu_track_map[50];
  int                        spu_track_map_entries;
/*  int                        spu_channel_user; */
/*  int                        spu_channel_auto; */
/*  int                        spu_channel_letterbox; */
  int                        spu_channel_pan_scan;
/*  int                        spu_channel; */

  /* lock for public xine player functions */
  pthread_mutex_t            frontend_lock;

  /* stream meta information */
  /* NEVER access directly, use helpers (see info_helper.c) */
  pthread_mutex_t            info_mutex;
  int                        stream_info_public[XINE_STREAM_INFO_MAX];
  int                        stream_info[XINE_STREAM_INFO_MAX];
  pthread_mutex_t            meta_mutex;
  char                      *meta_info_public[XINE_STREAM_INFO_MAX];
  char                      *meta_info[XINE_STREAM_INFO_MAX];

  /* seeking slowdown */
  pthread_mutex_t            first_frame_lock;
  pthread_cond_t             first_frame_reached;

  /* wait for headers sent / stream decoding finished */
  pthread_mutex_t            counter_lock;
  pthread_cond_t             counter_changed;
  int                        header_count_audio;
  int                        header_count_video;
  int                        finished_count_audio;
  int                        finished_count_video;

  /* event mechanism */
  xine_list_t               *event_queues;
  pthread_mutex_t            event_queues_lock;

  /* demux thread stuff */
  pthread_t                  demux_thread;
  pthread_mutex_t            demux_lock;
  pthread_mutex_t            demux_action_lock;
  pthread_cond_t             demux_resume;
  pthread_mutex_t            demux_mutex; /* used in _x_demux_... functions to synchronize order of pairwise A/V buffer operations */

  extra_info_t              *current_extra_info;
  pthread_mutex_t            current_extra_info_lock;
  int                        video_seek_count;

  int                        delay_finish_event; /* delay event in 1/10 sec units. 0=>no delay, -1=>forever */

  int                        slave_affection;   /* what operations need to be propagated down to the slave? */

  int                        err;

  xine_post_out_t            video_source;
  xine_post_out_t            audio_source;

  broadcaster_t             *broadcaster;

  refcounter_t              *refcounter;
#endif
};

/* when explicitly noted, some functions accept an anonymous stream,
 * which is a valid stream that does not want to be addressed. */
#define XINE_ANON_STREAM ((xine_stream_t *)-1)

typedef struct
{
  int total;
  int ready;
  int avail;
}
xine_query_buffers_data_t;

typedef struct
{
  xine_query_buffers_data_t vi;
  xine_query_buffers_data_t ai;
  xine_query_buffers_data_t vo;
  xine_query_buffers_data_t ao;
}
xine_query_buffers_t;

/*
 * private function prototypes:
 */

int _x_query_buffers(xine_stream_t *stream, xine_query_buffers_t *query) XINE_PROTECTED;
int _x_query_buffer_usage(xine_stream_t *stream, int *num_video_buffers, int *num_audio_buffers, int *num_video_frames, int *num_audio_frames) XINE_PROTECTED;
int _x_lock_port_rewiring(xine_t *xine, int ms_to_time_out) XINE_PROTECTED;
void _x_unlock_port_rewiring(xine_t *xine) XINE_PROTECTED;
int _x_lock_frontend(xine_stream_t *stream, int ms_to_time_out) XINE_PROTECTED;
void _x_unlock_frontend(xine_stream_t *stream) XINE_PROTECTED;
int _x_query_unprocessed_osd_events(xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_seek(xine_stream_t *stream, off_t start_pos, int start_time, int playing) XINE_PROTECTED;
int _x_continue_stream_processing(xine_stream_t *stream) XINE_PROTECTED;
void _x_trigger_relaxed_frame_drop_mode(xine_stream_t *stream) XINE_PROTECTED;
void _x_reset_relaxed_frame_drop_mode(xine_stream_t *stream) XINE_PROTECTED;

void _x_handle_stream_end      (xine_stream_t *stream, int non_user) XINE_PROTECTED;

/* report message to UI. usually these are async errors */

int _x_message(xine_stream_t *stream, int type, ...) XINE_SENTINEL XINE_PROTECTED;

/* flush the message queues */

void _x_flush_events_queues (xine_stream_t *stream) XINE_PROTECTED;

/* extra_info operations */
void _x_extra_info_reset( extra_info_t *extra_info ) XINE_PROTECTED;

void _x_extra_info_merge( extra_info_t *dst, extra_info_t *src ) XINE_PROTECTED;

void _x_get_current_info (xine_stream_t *stream, extra_info_t *extra_info, int size) XINE_PROTECTED;


/* demuxer helper functions from demux.c */

/*
 *  Flush audio and video buffers. It is called from demuxers on
 *  seek/stop, and may be useful when user input changes a stream and
 *  xine-lib has cached buffers that have yet to be played.
 *
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void _x_demux_flush_engine         (xine_stream_t *stream) XINE_PROTECTED;

void _x_demux_control_nop          (xine_stream_t *stream, uint32_t flags) XINE_PROTECTED;
void _x_demux_control_newpts       (xine_stream_t *stream, int64_t pts, uint32_t flags) XINE_PROTECTED;
void _x_demux_control_headers_done (xine_stream_t *stream) XINE_PROTECTED;
void _x_demux_control_start        (xine_stream_t *stream) XINE_PROTECTED;
void _x_demux_control_end          (xine_stream_t *stream, uint32_t flags) XINE_PROTECTED;
int _x_demux_start_thread          (xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_stop_thread           (xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_read_header           (input_plugin_t *input, void *buffer, off_t size) XINE_PROTECTED;
int _x_demux_check_extension       (const char *mrl, const char *extensions);

off_t _x_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) XINE_PROTECTED;

int _x_action_pending (xine_stream_t *stream) XINE_PROTECTED;

void _x_action_raise (xine_stream_t *stream) XINE_PROTECTED;
void _x_action_lower (xine_stream_t *stream) XINE_PROTECTED;

void _x_demux_send_data(fifo_buffer_t *fifo, uint8_t *data, int size,
                        int64_t pts, uint32_t type, uint32_t decoder_flags,
                        int input_normpos, int input_time, int total_time,
                        uint32_t frame_number) XINE_PROTECTED;

int _x_demux_read_send_data(fifo_buffer_t *fifo, input_plugin_t *input,
                            int size, int64_t pts, uint32_t type,
                            uint32_t decoder_flags, off_t input_normpos,
                            int input_time, int total_time,
                            uint32_t frame_number) XINE_PROTECTED;

void _x_demux_send_mrl_reference (xine_stream_t *stream, int alternative,
				  const char *mrl, const char *title,
				  int start_time, int duration) XINE_PROTECTED;

/*
 * MRL escaped-character decoding (overwrites the source string)
 */
void _x_mrl_unescape(char *mrl) XINE_PROTECTED;

/*
 * Return a copy of mrl without authentication credentials
 */
char *_x_mrl_remove_auth(const char *mrl) XINE_PROTECTED;

/*
 * plugin_loader functions
 *
 */

/* on-demand loading of audio/video/spu decoder plugins */

video_decoder_t *_x_get_video_decoder  (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_video_decoder (xine_stream_t *stream, video_decoder_t *decoder) XINE_PROTECTED;
audio_decoder_t *_x_get_audio_decoder  (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_audio_decoder (xine_stream_t *stream, audio_decoder_t *decoder) XINE_PROTECTED;
spu_decoder_t   *_x_get_spu_decoder    (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_spu_decoder   (xine_stream_t *stream, spu_decoder_t *decoder) XINE_PROTECTED;
/* check for decoder availability - but don't try to initialize it */
int              _x_decoder_available  (xine_t *xine, uint32_t buftype) XINE_PROTECTED;

/*
 * load_video_output_plugin
 *
 * load a specific video output plugin
 */

vo_driver_t *_x_load_video_output_plugin(xine_t *self,
					 char *id, int visual_type, void *visual) XINE_PROTECTED;

/*
 * audio output plugin dynamic loading stuff
 */

/*
 * load_audio_output_plugin
 *
 * load a specific audio output plugin
 */

ao_driver_t *_x_load_audio_output_plugin (xine_t *self, const char *id) XINE_PROTECTED;


void _x_set_speed (xine_stream_t *stream, int speed) XINE_PROTECTED;

int _x_get_speed (xine_stream_t *stream) XINE_PROTECTED;

void _x_set_fine_speed (xine_stream_t *stream, int speed) XINE_PROTECTED;

int _x_get_fine_speed (xine_stream_t *stream) XINE_PROTECTED;

void _x_select_spu_channel (xine_stream_t *stream, int channel) XINE_PROTECTED;

int _x_get_audio_channel (xine_stream_t *stream) XINE_PROTECTED;

int _x_get_spu_channel (xine_stream_t *stream) XINE_PROTECTED;

/*
 * internal events
 */

/* sent by dvb frontend to inform ts demuxer of new pids */
#define XINE_EVENT_PIDS_CHANGE	          0x80000000
/* sent by BluRay input plugin to inform ts demuxer about end of clip */
#define XINE_EVENT_END_OF_CLIP            0x80000001

/*
 * pids change event - inform ts demuxer of new pids
 */
typedef struct {
  int                 vpid; /* video program id */
  int                 apid; /* audio program id */
} xine_pids_data_t;

#ifdef __cplusplus
}
#endif

#endif
