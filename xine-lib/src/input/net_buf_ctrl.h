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
 *
 * network buffering control
 */

#ifndef HAVE_NET_BUF_CTRL_H
#define HAVE_NET_BUF_CTRL_H

#include <xine/xine_internal.h>

struct nbc_s {

  xine_stream_t   *stream;

  int              buffering;
  int              enabled;

  int              progress;
  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;
  int              video_fifo_fill;
  int              audio_fifo_fill;
  int              video_fifo_free;
  int              audio_fifo_free;
  int64_t          video_fifo_length;     /* in ms */
  int64_t          audio_fifo_length;     /* in ms */
  int64_t          video_fifo_length_int; /* in ms */
  int64_t          audio_fifo_length_int; /* in ms */

  int64_t          high_water_mark;
  /* bitrate */
  int64_t          video_last_pts;
  int64_t          audio_last_pts;
  int64_t          video_first_pts;
  int64_t          audio_first_pts;
  int64_t          video_fifo_size;
  int64_t          audio_fifo_size;
  int64_t          video_br;
  int64_t          audio_br;

  int              video_in_disc;
  int              audio_in_disc;

  pthread_mutex_t  mutex;

  /* follow live dvb delivery speed.
     0 = fix disabled
     1 = play at normal speed
     2 = play 0.5% slower to fill video fifo
     3 = play 0.5% faster to empty video fifo
     4..6 = same as 1..3 but watch audio fifo instead
     7 = pause */
  int dvbspeed;
  int dvbs_center, dvbs_width, dvbs_audio_fill, dvbs_video_fill;
  int64_t dvbs_audio_in, dvbs_audio_out;
  int64_t dvbs_video_in, dvbs_video_out;
};


typedef struct nbc_s nbc_t;

nbc_t *nbc_init (xine_stream_t *xine) XINE_MALLOC;

void nbc_close (nbc_t *this);
void nbc_set_speed_pause (nbc_t *this);
void nbc_set_speed_normal (nbc_t *this);
void dvbspeed_init (nbc_t *this, int force);
void dvbspeed_close (nbc_t *this);
void dvbspeed_put (nbc_t *this, fifo_buffer_t * fifo, buf_element_t *b);
void dvbspeed_get (nbc_t *this, fifo_buffer_t * fifo, buf_element_t *b);
void nbc_put_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen);
void nbc_compute_fifo_length(nbc_t *this, fifo_buffer_t *fifo, buf_element_t *buf, int action);
void nbc_alloc_cb (fifo_buffer_t *fifo, void *this_gen);
void nbc_get_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen);

#define DEFAULT_HIGH_WATER_MARK 5000 /* in 1/1000 s */

#endif
