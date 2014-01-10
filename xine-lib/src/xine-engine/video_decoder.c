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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define XINE_ENGINE_INTERNAL

#define LOG_MODULE "video_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "xine_private.h"
#include <sched.h>

#define SPU_SLEEP_INTERVAL (90000/2)

#ifndef SCHED_OTHER
#define SCHED_OTHER 0
#endif


static void update_spu_decoder (xine_stream_t *stream, int type) {

  int streamtype = (type>>16) & 0xFF;

  if( stream->spu_decoder_streamtype != streamtype ||
      !stream->spu_decoder_plugin ) {

    if (stream->spu_decoder_plugin)
      _x_free_spu_decoder (stream, stream->spu_decoder_plugin);

    stream->spu_decoder_streamtype = streamtype;
    stream->spu_decoder_plugin = _x_get_spu_decoder (stream, streamtype);

  }
  return ;
}

int _x_spu_decoder_sleep(xine_stream_t *stream, int64_t next_spu_vpts)
{
  int64_t time, wait;
  int thread_vacant = 1;

  /* we wait until one second before the next SPU is due */
  next_spu_vpts -= 90000;

  do {
    if (next_spu_vpts)
      time = stream->xine->clock->get_current_time(stream->xine->clock);
    else
      time = 0;

    /* wait in pieces of one half second */
    if (next_spu_vpts - time < SPU_SLEEP_INTERVAL)
      wait = next_spu_vpts - time;
    else
      wait = SPU_SLEEP_INTERVAL;

    if (wait > 0) xine_usec_sleep(wait * 11);

    if (stream->xine->port_ticket->ticket_revoked)
      stream->xine->port_ticket->renew(stream->xine->port_ticket, 0);

    /* never wait, if we share the thread with a video decoder */
    thread_vacant = !stream->video_decoder_plugin;
    /* we have to return if video out calls for the decoder */
    if (thread_vacant && stream->video_fifo->first)
      thread_vacant = (stream->video_fifo->first->type != BUF_CONTROL_FLUSH_DECODER);
    /* we have to return if the demuxer needs us to release a buffer */
    if (thread_vacant)
      thread_vacant = !_x_action_pending(stream);

  } while (wait == SPU_SLEEP_INTERVAL && thread_vacant);

  return thread_vacant;
}

static void video_decoder_update_disable_flush_at_discontinuity(void *disable_decoder_flush_at_discontinuity, xine_cfg_entry_t *entry)
{
  *(int *)disable_decoder_flush_at_discontinuity = entry->num_value;
}

static void *video_decoder_loop (void *stream_gen) {

  buf_element_t   *buf;
  xine_stream_t   *stream = (xine_stream_t *) stream_gen;
  xine_ticket_t   *running_ticket = stream->xine->port_ticket;
  int              running = 1;
  int              streamtype;
  int              prof_video_decode = -1;
  int              prof_spu_decode = -1;
  uint32_t         buftype_unknown = 0;
  int              disable_decoder_flush_at_discontinuity;

#ifndef WIN32
  errno = 0;
  if (nice(-1) == -1 && errno)
    xine_log(stream->xine, XINE_LOG_MSG, "video_decoder: can't raise nice priority by 1: %s\n", strerror(errno));
#endif /* WIN32 */

  if (prof_video_decode == -1)
    prof_video_decode = xine_profiler_allocate_slot ("video decoder");
  if (prof_spu_decode == -1)
    prof_spu_decode = xine_profiler_allocate_slot ("spu decoder");

  disable_decoder_flush_at_discontinuity = stream->xine->config->register_bool(stream->xine->config, "engine.decoder.disable_flush_at_discontinuity", 0,
      _("disable decoder flush at discontinuity"),
      _("when watching live tv a discontinuity happens for example about every 26.5 hours due to a pts wrap.\n"
        "flushing the decoder at that time causes decoding errors for images after the pts wrap.\n"
        "to avoid the decoding errors, decoder flush at discontinuity should be disabled.\n\n"
        "WARNING: as the flush was introduced to fix some issues when playing DVD still images, it is\n"
        "likely that these issues may reappear in case they haven't been fixed differently meanwhile.\n"),
        20, video_decoder_update_disable_flush_at_discontinuity, &disable_decoder_flush_at_discontinuity);

  while (running) {

    lprintf ("getting buffer...\n");

    buf = stream->video_fifo->get (stream->video_fifo);

    _x_extra_info_merge( stream->video_decoder_extra_info, buf->extra_info );
    stream->video_decoder_extra_info->seek_count = stream->video_seek_count;

    lprintf ("got buffer 0x%08x\n", buf->type);

    switch (buf->type & 0xffff0000) {
    case BUF_CONTROL_HEADERS_DONE:
      pthread_mutex_lock (&stream->counter_lock);
      stream->header_count_video++;
      pthread_cond_broadcast (&stream->counter_changed);
      pthread_mutex_unlock (&stream->counter_lock);
      break;

    case BUF_CONTROL_START:

      /* decoder dispose might call port functions */
      running_ticket->acquire(running_ticket, 0);

      if (stream->video_decoder_plugin) {
	_x_free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }

      if (stream->spu_decoder_plugin) {
        _x_free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
        stream->spu_track_map_entries = 0;
      }

      running_ticket->release(running_ticket, 0);

      if( !(buf->decoder_flags & BUF_FLAG_GAPLESS_SW) )
        stream->metronom->handle_video_discontinuity (stream->metronom,
						      DISC_STREAMSTART, 0);

      buftype_unknown = 0;
      break;

    case BUF_CONTROL_SPU_CHANNEL:
      {
	xine_event_t  ui_event;

	/* We use widescreen spu as the auto selection, because widescreen
	 * display is common. SPU decoders can choose differently if it suits
	 * them. */
	stream->spu_channel_auto = buf->decoder_info[0];
	stream->spu_channel_letterbox = buf->decoder_info[1];
	stream->spu_channel_pan_scan = buf->decoder_info[2];
	if (stream->spu_channel_user == -1)
	  stream->spu_channel = stream->spu_channel_auto;

	/* Inform UI of SPU channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;

        xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_END:

      /* flush decoder frames if stream finished naturally (non-user stop) */
      if( buf->decoder_flags ) {
        running_ticket->acquire(running_ticket, 0);
        if (stream->video_decoder_plugin)
          stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        running_ticket->release(running_ticket, 0);
      }

      /*
       * wait the output fifos to run dry before sending the notification event
       * to the frontend. exceptions:
       * 1) don't wait if there is more than one stream attached to the current
       *    output port (the other stream might be sending data so we would be
       *    here forever)
       * 2) early_finish_event: send notification asap to allow gapless switch
       * 3) slave stream: don't wait. get into an unblocked state asap to allow
       *    new master actions.
       */
      while(1) {
        int num_bufs, num_streams;

        running_ticket->acquire(running_ticket, 0);
        num_bufs = stream->video_out->get_property(stream->video_out, VO_PROP_BUFS_IN_FIFO);
        num_streams = stream->video_out->get_property(stream->video_out, VO_PROP_NUM_STREAMS);
        running_ticket->release(running_ticket, 0);

        if( num_bufs > 0 && num_streams == 1 && !stream->early_finish_event &&
            stream->master == stream )
          xine_usec_sleep (10000);
        else
          break;
      }

      /* wait for audio to reach this marker, if necessary */

      pthread_mutex_lock (&stream->counter_lock);

      stream->finished_count_video++;

      lprintf ("reached end marker # %d\n",
	       stream->finished_count_video);

      pthread_cond_broadcast (&stream->counter_changed);

      if (stream->audio_thread_created) {

        while (stream->finished_count_video > stream->finished_count_audio) {
          struct timeval tv;
          struct timespec ts;
          gettimeofday(&tv, NULL);
          ts.tv_sec  = tv.tv_sec + 1;
          ts.tv_nsec = tv.tv_usec * 1000;
          /* use timedwait to workaround buggy pthread broadcast implementations */
          pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
        }
      }

      pthread_mutex_unlock (&stream->counter_lock);

      /* Wake up xine_play if it's waiting for a frame */
      pthread_mutex_lock (&stream->first_frame_lock);
      if (stream->first_frame_flag) {
        stream->first_frame_flag = 0;
        pthread_cond_broadcast(&stream->first_frame_reached);
      }
      pthread_mutex_unlock (&stream->first_frame_lock);
      break;

    case BUF_CONTROL_QUIT:
      /* decoder dispose might call port functions */
      running_ticket->acquire(running_ticket, 0);

      if (stream->video_decoder_plugin) {
	_x_free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }
      if (stream->spu_decoder_plugin) {
        _x_free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
        stream->spu_track_map_entries = 0;
      }

      running_ticket->release(running_ticket, 0);
      running = 0;
      break;

    case BUF_CONTROL_RESET_DECODER:
      _x_extra_info_reset( stream->video_decoder_extra_info );
      stream->video_seek_count++;

      running_ticket->acquire(running_ticket, 0);
      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->reset (stream->video_decoder_plugin);
      }
      if (stream->spu_decoder_plugin) {
        stream->spu_decoder_plugin->reset (stream->spu_decoder_plugin);
      }
      running_ticket->release(running_ticket, 0);
      break;

    case BUF_CONTROL_FLUSH_DECODER:
      if (stream->video_decoder_plugin) {
        running_ticket->acquire(running_ticket, 0);
        stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        running_ticket->release(running_ticket, 0);
      }
      break;

    case BUF_CONTROL_DISCONTINUITY:
      lprintf ("discontinuity ahead\n");

      if (stream->video_decoder_plugin) {
        running_ticket->acquire(running_ticket, 0);
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
        /* it might be a long time before we get back from a handle_video_discontinuity,
	 * so we better flush the decoder before */
        if (!disable_decoder_flush_at_discontinuity)
          stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        running_ticket->release(running_ticket, 0);
      }

      stream->metronom->handle_video_discontinuity (stream->metronom, DISC_RELATIVE, buf->disc_off);

      break;

    case BUF_CONTROL_NEWPTS:
      lprintf ("new pts %"PRId64"\n", buf->disc_off);

      if (stream->video_decoder_plugin) {
        running_ticket->acquire(running_ticket, 0);
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
        /* it might be a long time before we get back from a handle_video_discontinuity,
	 * so we better flush the decoder before */
        if (!disable_decoder_flush_at_discontinuity)
          stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        running_ticket->release(running_ticket, 0);
      }

      if (buf->decoder_flags & BUF_FLAG_SEEK) {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_STREAMSEEK, buf->disc_off);
      } else {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_ABSOLUTE, buf->disc_off);
      }
      break;

    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	xine_event_t  ui_event;
	/* Inform UI of AUDIO channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;
	xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_NOP:
      break;

    case BUF_CONTROL_RESET_TRACK_MAP:
      if (stream->spu_track_map_entries)
      {
        xine_event_t ui_event;

        stream->spu_track_map_entries = 0;

        ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
        ui_event.data_length = 0;
        xine_event_send(stream, &ui_event);
      }
      break;

    default:

      if ( (buf->type & 0xFF000000) == BUF_VIDEO_BASE ) {

        if (_x_stream_info_get(stream, XINE_STREAM_INFO_IGNORE_VIDEO))
          break;

        xine_profiler_start_count (prof_video_decode);

        running_ticket->acquire(running_ticket, 0);

	/*
	  printf ("video_decoder: got package %d, decoder_info[0]:%d\n",
	  buf, buf->decoder_info[0]);
	*/

	streamtype = (buf->type>>16) & 0xFF;

        if( buf->type != buftype_unknown &&
            (stream->video_decoder_streamtype != streamtype ||
            !stream->video_decoder_plugin) ) {

          if (stream->video_decoder_plugin) {
            _x_free_video_decoder (stream, stream->video_decoder_plugin);
          }

          stream->video_decoder_streamtype = streamtype;
          stream->video_decoder_plugin = _x_get_video_decoder (stream, streamtype);

          _x_stream_info_set(stream, XINE_STREAM_INFO_VIDEO_HANDLED, (stream->video_decoder_plugin != NULL));
        }

        if (stream->video_decoder_plugin)
          stream->video_decoder_plugin->decode_data (stream->video_decoder_plugin, buf);

        if (buf->type != buftype_unknown &&
            !_x_stream_info_get(stream, XINE_STREAM_INFO_VIDEO_HANDLED)) {
          xine_log (stream->xine, XINE_LOG_MSG,
                    _("video_decoder: no plugin available to handle '%s'\n"), _x_buf_video_name( buf->type ) );

          if( !_x_meta_info_get(stream, XINE_META_INFO_VIDEOCODEC))
	    _x_meta_info_set_utf8(stream, XINE_META_INFO_VIDEOCODEC, _x_buf_video_name( buf->type ));

          buftype_unknown = buf->type;

          /* fatal error - dispose plugin */
          if (stream->video_decoder_plugin) {
            _x_free_video_decoder (stream, stream->video_decoder_plugin);
            stream->video_decoder_plugin = NULL;
          }
        }

        if (running_ticket->ticket_revoked)
          running_ticket->renew(running_ticket, 0);
        running_ticket->release(running_ticket, 0);

        xine_profiler_stop_count (prof_video_decode);

      } else if ( (buf->type & 0xFF000000) == BUF_SPU_BASE ) {

        int      i,j;

        if (_x_stream_info_get(stream, XINE_STREAM_INFO_IGNORE_SPU))
          break;

        xine_profiler_start_count (prof_spu_decode);

        running_ticket->acquire(running_ticket, 0);

        update_spu_decoder(stream, buf->type);

        /* update track map */

        i = 0;
        while ( (i<stream->spu_track_map_entries) && (stream->spu_track_map[i]<buf->type) )
          i++;

        if ( (i==stream->spu_track_map_entries)
             || (stream->spu_track_map[i] != buf->type) ) {
          xine_event_t  ui_event;

          j = stream->spu_track_map_entries;

          if (j >= 50)
            break;

          while (j>i) {
            stream->spu_track_map[j] = stream->spu_track_map[j-1];
            j--;
          }
          stream->spu_track_map[i] = buf->type;
          stream->spu_track_map_entries++;

	  ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	  ui_event.data_length = 0;
	  xine_event_send (stream, &ui_event);
        }

        if (stream->spu_channel_user >= 0) {
          if (stream->spu_channel_user < stream->spu_track_map_entries)
            stream->spu_channel = (stream->spu_track_map[stream->spu_channel_user] & 0xFF);
          else
            stream->spu_channel = stream->spu_channel_auto;
        }

        if (stream->spu_decoder_plugin) {
          stream->spu_decoder_plugin->decode_data (stream->spu_decoder_plugin, buf);
        }

        if (running_ticket->ticket_revoked)
          running_ticket->renew(running_ticket, 0);
        running_ticket->release(running_ticket, 0);

        xine_profiler_stop_count (prof_spu_decode);

      } else if (buf->type != buftype_unknown) {
	xine_log (stream->xine, XINE_LOG_MSG,
		  _("video_decoder: error, unknown buffer type: %08x\n"), buf->type );
	buftype_unknown = buf->type;
      }

      break;

    }

    buf->free_buffer (buf);
  }

  return NULL;
}

int _x_video_decoder_init (xine_stream_t *stream) {

  if (stream->video_out == NULL) {
    stream->video_fifo = _x_dummy_fifo_buffer_new (5, 8192);
    stream->spu_track_map_entries = 0;
    return 1;
  } else {

    pthread_attr_t       pth_attrs;
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    struct sched_param   pth_params;
#endif
    int		       err, num_buffers;
    /* The fifo size is based on dvd playback where buffers are filled
     * with 2k of data. With 500 buffers and a typical video data rate
     * of 8 Mbit/s, the fifo can hold about 1 second of video, wich
     * should be enough to compensate for drive delays.
     * We provide buffers of 8k size instead of 2k for demuxers sending
     * larger chunks.
     */

    num_buffers = stream->xine->config->register_num (stream->xine->config,
                                                      "engine.buffers.video_num_buffers",
                                                      500,
                                                      _("number of video buffers"),
						      _("The number of video buffers (each is 8k in size) "
						        "xine uses in its internal queue. Higher values "
							"mean smoother playback for unreliable inputs, but "
							"also increased latency and memory consumption."),
                                                      20, NULL, NULL);

    stream->video_fifo = _x_fifo_buffer_new (num_buffers, 8192);
    if (stream->video_fifo == NULL) {
      xine_log(stream->xine, XINE_LOG_MSG, "video_decoder: can't allocated video fifo\n");
      return 0;
    }

    stream->spu_track_map_entries = 0;

    pthread_attr_init(&pth_attrs);
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    pthread_attr_getschedparam(&pth_attrs, &pth_params);
    pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
    pthread_attr_setschedparam(&pth_attrs, &pth_params);
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    stream->video_thread_created = 1;
    if ((err = pthread_create (&stream->video_thread,
                               &pth_attrs, video_decoder_loop, stream)) != 0) {
      xine_log (stream->xine, XINE_LOG_MSG, "video_decoder: can't create new thread (%s)\n",
                strerror(err));
      stream->video_thread_created = 0;
      pthread_attr_destroy(&pth_attrs);
      return 0;
    }

    pthread_attr_destroy(&pth_attrs);
  }
  return 1;
}

void _x_video_decoder_shutdown (xine_stream_t *stream) {

  buf_element_t *buf;
  void          *p;

  lprintf ("shutdown...\n");

  if (stream->video_thread_created) {

    /* stream->video_fifo->clear(stream->video_fifo); */

    buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);

    lprintf ("shutdown...2\n");

    buf->type = BUF_CONTROL_QUIT;
    stream->video_fifo->put (stream->video_fifo, buf);

    lprintf ("shutdown...3\n");

    pthread_join (stream->video_thread, &p);
    stream->video_thread_created = 0;

    lprintf ("shutdown...4\n");

  }

  stream->video_fifo->dispose (stream->video_fifo);
  stream->video_fifo = NULL;
}
