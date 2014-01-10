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
 *
 * frame allocation / queuing / scheduling / output functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#define XINE_ENABLE_EXPERIMENTAL_FEATURES
#define XINE_ENGINE_INTERNAL

#define LOG_MODULE "video_out"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/metronom.h>
#include <xine/xineutils.h>
#include <yuv2rgb.h>

#define NUM_FRAME_BUFFERS          15
#define MAX_USEC_TO_SLEEP       20000
#define DEFAULT_FRAME_DURATION   3000    /* 30 frames per second */

/* wait this delay if the first frame is still referenced */
#define FIRST_FRAME_POLL_DELAY   3000
#define FIRST_FRAME_MAX_POLL       10    /* poll n times at most */

/* experimental optimization: try to allocate frames from free queue
 * in the same format as requested (avoid unnecessary free/alloc in
 * vo driver). up to 25% less cpu load using deinterlace with film mode.
 */
#define EXPERIMENTAL_FRAME_QUEUE_OPTIMIZATION 1

static vo_frame_t * crop_frame( xine_video_port_t *this_gen, vo_frame_t *img );

typedef struct vos_grab_video_frame_s vos_grab_video_frame_t;
struct vos_grab_video_frame_s {
  xine_grab_video_frame_t grab_frame;

  vos_grab_video_frame_t *next;
  int finished;
  xine_video_port_t *video_port;
  vo_frame_t *vo_frame;
  yuv2rgb_factory_t *yuv2rgb_factory;
  yuv2rgb_t *yuv2rgb;
  int vo_width, vo_height;
  int grab_width, grab_height;
  int y_stride, uv_stride;
  int img_size;
  uint8_t *img;
};


typedef struct {
  vo_frame_t        *first;
  vo_frame_t        *last;
  int                num_buffers;
  int                num_buffers_max;

  int                locked_for_read;
  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
} img_buf_fifo_t;

typedef struct {

  xine_video_port_t         vo; /* public part */

  vo_driver_t              *driver;
  pthread_mutex_t           driver_lock;
  xine_t                   *xine;
  metronom_clock_t         *clock;
  xine_list_t              *streams;
  pthread_mutex_t           streams_lock;

  img_buf_fifo_t           *free_img_buf_queue;
  img_buf_fifo_t           *display_img_buf_queue;

  vo_frame_t               *img_backup;

  vo_frame_t               *last_frame;
  vos_grab_video_frame_t   *pending_grab_request;
  pthread_mutex_t           grab_lock;
  pthread_cond_t            grab_cond;

  uint32_t                  video_loop_running:1;
  uint32_t                  video_opened:1;

  uint32_t                  overlay_enabled:1;

  uint32_t                  warn_threshold_event_sent:1;

  /* do we true real-time output or is this a grab only instance ? */
  uint32_t                  grab_only:1;

  uint32_t                  redraw_needed:3;
  int                       discard_frames;

  pthread_t                 video_thread;

  int                       num_frames_delivered;
  int                       num_frames_skipped;
  int                       num_frames_discarded;

  /* threshold for sending XINE_EVENT_DROPPED_FRAMES */
  int                       warn_skipped_threshold;
  int                       warn_discarded_threshold;
  int                       warn_threshold_exceeded;

  /* pts value when decoder delivered last video frame */
  int64_t                   last_delivery_pts;
  int64_t                   last_pts;

  video_overlay_manager_t  *overlay_source;

  extra_info_t             *extra_info_base; /* used to free mem chunk */

  int                       current_width, current_height;
  int64_t                   current_duration;
  int                       framerate;
  int                       frame_drop_limit_max;
  int                       frame_drop_limit;
  int                       frame_drop_cpt;
  int                       frame_drop_suggested;
  int                       crop_left, crop_right, crop_top, crop_bottom;
  pthread_mutex_t           trigger_drawing_mutex;
  pthread_cond_t            trigger_drawing_cond;
  int                       trigger_drawing;
} vos_t;


/*
 * frame queue (fifo) util functions
 */

static img_buf_fifo_t *XINE_MALLOC vo_new_img_buf_queue () {

  img_buf_fifo_t *queue;

  queue = (img_buf_fifo_t *) calloc(1, sizeof(img_buf_fifo_t));
  if( queue ) {
    queue->first           = NULL;
    queue->last            = NULL;
    queue->num_buffers     = 0;
    queue->num_buffers_max = 0;

    queue->locked_for_read = 0;
    pthread_mutex_init (&queue->mutex, NULL);
    pthread_cond_init  (&queue->not_empty, NULL);
  }
  return queue;
}

static void vo_append_to_img_buf_queue_int (img_buf_fifo_t *queue,
					vo_frame_t *img) {

  /* img already enqueue? (serious leak) */
  assert (img->next==NULL);

  img->next = NULL;

  if (!queue->first) {
    queue->first = img;
    queue->last  = img;
    queue->num_buffers = 0;
  }
  else if (queue->last) {
    queue->last->next = img;
    queue->last  = img;
  }

  queue->num_buffers++;
  if (queue->num_buffers_max < queue->num_buffers)
    queue->num_buffers_max = queue->num_buffers;

  pthread_cond_signal (&queue->not_empty);
}

static void vo_append_to_img_buf_queue (img_buf_fifo_t *queue,
					vo_frame_t *img) {
  pthread_mutex_lock (&queue->mutex);
  vo_append_to_img_buf_queue_int (queue, img);
  pthread_mutex_unlock (&queue->mutex);
}

static vo_frame_t *vo_remove_from_img_buf_queue_int (img_buf_fifo_t *queue, int blocking,
                                                     uint32_t width, uint32_t height,
                                                     double ratio, int format,
                                                     int flags) {
  vo_frame_t *img = NULL;
  vo_frame_t *previous = NULL;

  while (!img || queue->locked_for_read) {

    img = (queue->locked_for_read) ? NULL : queue->first;

#if EXPERIMENTAL_FRAME_QUEUE_OPTIMIZATION
    if (img) {
      /* try to obtain a frame with the same format first.
       * doing so may avoid unnecessary alloc/free's at the vo
       * driver, specially when using post plugins that change
       * format like the tvtime deinterlacer does.
       */
      int i = 0;
      while( img && width && height &&
            (img->width != width || img->height != height ||
            img->ratio != ratio || img->format != format) ) {
        previous = img;
        img = img->next;
        i++;
      }

      if( width && height ) {
        if( !img ) {
          if( queue->num_buffers == 1 && !blocking && queue->num_buffers_max > 8) {
            /* non-blocking and only a single frame on fifo with different
             * format -> ignore it (give another chance of a frame format hit)
             * only if we have a lot of buffers at all.
             */
            lprintf("frame format mismatch - will wait another frame\n");
          } else {
            /* we have just a limited number of buffers or at least 2 frames
             * on fifo but they don't match -> give up. return whatever we got.
             */
            img = queue->first;
            lprintf("frame format miss (%d/%d)\n", i, queue->num_buffers);
          }
        } else {
          /* good: format match! */
          lprintf("frame format hit (%d/%d)\n", i, queue->num_buffers);
        }
      }
    }
#endif

    if(!img) {
      if (blocking)
        pthread_cond_wait (&queue->not_empty, &queue->mutex);
      else {
        struct timeval tv;
        struct timespec ts;
        gettimeofday(&tv, NULL);
        ts.tv_sec  = tv.tv_sec + 1;
        ts.tv_nsec = tv.tv_usec * 1000;
        if (pthread_cond_timedwait (&queue->not_empty, &queue->mutex, &ts) != 0)
          return NULL;
      }
    }
  }

  if (img) {

    if( img == queue->first ) {
      queue->first = img->next;
    } else {
      previous->next = img->next;
      if( img == queue->last )
        queue->last = previous;
    }

    img->next = NULL;
    if (!queue->first) {
      queue->last = NULL;
      queue->num_buffers = 0;
    } else {
      queue->num_buffers--;
    }
  }

  return img;
}

static vo_frame_t *vo_remove_from_img_buf_queue (img_buf_fifo_t *queue) {
  vo_frame_t *img;

  pthread_mutex_lock (&queue->mutex);
  img = vo_remove_from_img_buf_queue_int(queue, 1, 0, 0, 0, 0, 0);
  pthread_mutex_unlock (&queue->mutex);

  return img;
}

static vo_frame_t *vo_remove_from_img_buf_queue_nonblock (img_buf_fifo_t *queue,
                                                          uint32_t width, uint32_t height,
                                                          double ratio, int format,
                                                          int flags) {
  vo_frame_t *img;

  pthread_mutex_lock (&queue->mutex);
  img = vo_remove_from_img_buf_queue_int(queue, 0, width, height, ratio, format, flags);
  pthread_mutex_unlock (&queue->mutex);

  return img;
}

/*
 * functions to maintain lock_counter
 */
static void vo_frame_inc_lock (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);

  img->lock_counter++;

  pthread_mutex_unlock (&img->mutex);
}

static void vo_frame_dec_lock (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);

  img->lock_counter--;
  if (!img->lock_counter) {
    vos_t *this = (vos_t *) img->port;
    if (img->stream)
      _x_refcounter_dec(img->stream->refcounter);
    vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}


/*
 * functions for grabbing RGB images from displayed frames
 */
static void vo_dispose_grab_video_frame(xine_grab_video_frame_t *frame_gen)
{
  vos_grab_video_frame_t *frame = (vos_grab_video_frame_t *) frame_gen;

  if (frame->vo_frame)
    vo_frame_dec_lock(frame->vo_frame);

  if (frame->yuv2rgb)
    frame->yuv2rgb->dispose(frame->yuv2rgb);

  if (frame->yuv2rgb_factory)
    frame->yuv2rgb_factory->dispose(frame->yuv2rgb_factory);

  free(frame->img);
  free(frame->grab_frame.img);
  free(frame);
}


static int vo_grab_grab_video_frame (xine_grab_video_frame_t *frame_gen) {
  vos_grab_video_frame_t *frame = (vos_grab_video_frame_t *) frame_gen;
  vos_t *this = (vos_t *) frame->video_port;
  vo_frame_t *vo_frame;
  int format, y_stride, uv_stride;
  uint8_t *base[3];

  if (frame->grab_frame.flags & XINE_GRAB_VIDEO_FRAME_FLAGS_WAIT_NEXT) {
    struct timeval tvnow, tvdiff, tvtimeout;
    struct timespec ts;

    /* calculate absolute timeout time */
    tvdiff.tv_sec = frame->grab_frame.timeout / 1000;
    tvdiff.tv_usec = frame->grab_frame.timeout % 1000;
    tvdiff.tv_usec *= 1000;
    gettimeofday(&tvnow, NULL);
    timeradd(&tvnow, &tvdiff, &tvtimeout);
    ts.tv_sec  = tvtimeout.tv_sec;
    ts.tv_nsec = tvtimeout.tv_usec;
    ts.tv_nsec *= 1000;

    pthread_mutex_lock(&this->grab_lock);

    /* insert grab request into grab queue */
    frame->next = this->pending_grab_request;
    this->pending_grab_request = frame;

    /* wait until our request is finished */
    frame->finished = 0;
    while (!frame->finished) {
      if (pthread_cond_timedwait(&this->grab_cond, &this->grab_lock, &ts) == ETIMEDOUT) {
        vos_grab_video_frame_t *prev = this->pending_grab_request;
        while (prev) {
          if (prev == frame) {
            this->pending_grab_request = frame->next;
            break;
          } else if (prev->next == frame) {
            prev->next = frame->next;
            break;
          }
          prev = prev->next;
        }
        frame->next = NULL;
        pthread_mutex_unlock(&this->grab_lock);
        return 1;   /* no frame available */
      }
    }

    pthread_mutex_unlock(&this->grab_lock);

    vo_frame = frame->vo_frame;
    frame->vo_frame = NULL;
    if (!vo_frame)
      return -1; /* error happened */
  } else {
    pthread_mutex_lock(&this->grab_lock);

    /* use last displayed frame */
    vo_frame = this->last_frame;
    if (!vo_frame) {
      pthread_mutex_unlock(&this->grab_lock);
      return 1;   /* no frame available */
    }
    if (vo_frame->format != XINE_IMGFMT_YV12 && vo_frame->format != XINE_IMGFMT_YUY2 && !vo_frame->proc_provide_standard_frame_data) {
      pthread_mutex_unlock(&this->grab_lock);
      return -1; /* error happened */
    }
    vo_frame_inc_lock(vo_frame);
    pthread_mutex_unlock(&this->grab_lock);
    frame->grab_frame.vpts = vo_frame->vpts;
  }

  int width = vo_frame->width;
  int height = vo_frame->height;

  if (vo_frame->format == XINE_IMGFMT_YV12 || vo_frame->format == XINE_IMGFMT_YUY2) {
    format = vo_frame->format;
    y_stride = vo_frame->pitches[0];
    uv_stride = vo_frame->pitches[1];
    base[0] = vo_frame->base[0];
    base[1] = vo_frame->base[1];
    base[2] = vo_frame->base[2];
  } else {
    /* retrieve standard format image data from output driver */
    xine_current_frame_data_t data;
    memset(&data, 0, sizeof(data));
    vo_frame->proc_provide_standard_frame_data(vo_frame, &data);
    if (data.img_size > frame->img_size) {
      free(frame->img);
      frame->img_size = data.img_size;
      frame->img = calloc(data.img_size, sizeof(uint8_t));
      if (!frame->img) {
        vo_frame_dec_lock(vo_frame);
        return -1; /* error happened */
      }
    }
    data.img = frame->img;
    vo_frame->proc_provide_standard_frame_data(vo_frame, &data);
    format = data.format;
    if (format == XINE_IMGFMT_YV12) {
      y_stride = width;
      uv_stride = width / 2;
      base[0] = data.img;
      base[1] = data.img + width * height;
      base[2] = data.img + width * height + width * height / 4;
    } else { // XINE_IMGFMT_YUY2
      y_stride = width * 2;
      uv_stride = 0;
      base[0] = data.img;
      base[1] = NULL;
      base[2] = NULL;
    }
  }

  /* take cropping parameters into account */
  int crop_left = (vo_frame->crop_left + frame->grab_frame.crop_left) & ~1;
  int crop_right = (vo_frame->crop_right + frame->grab_frame.crop_right) & ~1;
  int crop_top = vo_frame->crop_top + frame->grab_frame.crop_top;
  int crop_bottom = vo_frame->crop_bottom + frame->grab_frame.crop_bottom;

  if (crop_left || crop_right || crop_top || crop_bottom) {
    if ((width - crop_left - crop_right) >= 8)
      width = width - crop_left - crop_right;
    else
      crop_left = crop_right = 0;

    if ((height - crop_top - crop_bottom) >= 8)
      height = height - crop_top - crop_bottom;
    else
      crop_top = crop_bottom = 0;

    if (format == XINE_IMGFMT_YV12) {
      base[0] += crop_top * y_stride + crop_left;
      base[1] += crop_top/2 * uv_stride + crop_left/2;
      base[2] += crop_top/2 * uv_stride + crop_left/2;
    } else { // XINE_IMGFMT_YUY2
      base[0] += crop_top * y_stride + crop_left*2;
    }
  }

  /* get pixel aspect ratio */
  double sar = 1.0;
  {
    int sarw = vo_frame->width  - vo_frame->crop_left - vo_frame->crop_right;
    int sarh = vo_frame->height - vo_frame->crop_top  - vo_frame->crop_bottom;
    if ((vo_frame->ratio > 0.0) && (sarw > 0) && (sarh > 0))
      sar = vo_frame->ratio * sarh / sarw;
  }

  /* if caller does not specify frame size we return the actual size of grabbed frame */
  if ((frame->grab_frame.width <= 0) && (frame->grab_frame.height <= 0)) {
    if (sar > 1.0) {
      frame->grab_frame.width  = sar * width + 0.5;
      frame->grab_frame.height = height;
    } else {
      frame->grab_frame.width  = width;
      frame->grab_frame.height = (double)height / sar + 0.5;
    }
  } else if (frame->grab_frame.width <= 0)
    frame->grab_frame.width = frame->grab_frame.height * width * sar / height + 0.5;
  else if (frame->grab_frame.height <= 0)
    frame->grab_frame.height = (frame->grab_frame.width * height) / (sar * width) + 0.5;

  /* allocate grab frame image buffer */
  if (frame->grab_frame.width != frame->grab_width || frame->grab_frame.height != frame->grab_height) {
    free(frame->grab_frame.img);
    frame->grab_frame.img = NULL;
  }
  if (frame->grab_frame.img == NULL) {
    frame->grab_frame.img = (uint8_t *) calloc(frame->grab_frame.width * frame->grab_frame.height, 3);
    if (frame->grab_frame.img == NULL) {
      vo_frame_dec_lock(vo_frame);
      return -1; /* error happened */
    }
  }

  /* initialize yuv2rgb factory */
  if (!frame->yuv2rgb_factory) {
    int cm = VO_GET_FLAGS_CM (vo_frame->flags);
    frame->yuv2rgb_factory = yuv2rgb_factory_init(MODE_24_RGB, 0, NULL);
    if (!frame->yuv2rgb_factory) {
      vo_frame_dec_lock(vo_frame);
      return -1; /* error happened */
    }
    if ((cm >> 1) == 2) /* color matrix undefined */
      cm = (cm & 1) |
        ((vo_frame->height - vo_frame->crop_top - vo_frame->crop_bottom >= 720) ||
         (vo_frame->width - vo_frame->crop_left - vo_frame->crop_right >= 1280) ? 2 : 10);
    else if ((cm >> 1) == 0) /* converted RGB source, always ITU 601 */
      cm = (cm & 1) | 10;
    frame->yuv2rgb_factory->set_csc_levels (frame->yuv2rgb_factory, 0, 128, 128, cm);
  }

  /* retrieve a yuv2rgb converter */
  if (!frame->yuv2rgb) {
    frame->yuv2rgb = frame->yuv2rgb_factory->create_converter(frame->yuv2rgb_factory);
    if (!frame->yuv2rgb) {
      vo_frame_dec_lock(vo_frame);
      return -1; /* error happened */
    }
  }

  /* configure yuv2rgb converter */
  if (width != frame->vo_width ||
        height != frame->vo_height ||
        frame->grab_frame.width != frame->grab_width ||
        frame->grab_frame.height != frame->grab_height ||
        y_stride != frame->y_stride ||
        uv_stride != frame->uv_stride) {
    frame->vo_width = width;
    frame->vo_height = height;
    frame->grab_width = frame->grab_frame.width;
    frame->grab_height = frame->grab_frame.height;
    frame->y_stride = y_stride;
    frame->uv_stride = uv_stride;
    frame->yuv2rgb->configure(frame->yuv2rgb, width, height, y_stride, uv_stride, frame->grab_width, frame->grab_height, frame->grab_width * 3);
  }

  /* convert YUV to RGB image taking possible scaling into account */
  if(format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun(frame->yuv2rgb, frame->grab_frame.img, base[0], base[1], base[2]);
  else
    frame->yuv2rgb->yuy22rgb_fun(frame->yuv2rgb, frame->grab_frame.img, base[0]);

  vo_frame_dec_lock(vo_frame);
  return 0;
}


static xine_grab_video_frame_t *vo_new_grab_video_frame(xine_video_port_t *this_gen)
{
  vos_grab_video_frame_t *frame = calloc(1, sizeof(vos_grab_video_frame_t));
  if (frame) {
    frame->grab_frame.dispose = vo_dispose_grab_video_frame;
    frame->grab_frame.grab = vo_grab_grab_video_frame;
    frame->grab_frame.vpts = -1;
    frame->grab_frame.timeout = XINE_GRAB_VIDEO_FRAME_DEFAULT_TIMEOUT;
    frame->video_port = this_gen;
  }
  return (xine_grab_video_frame_t *)frame;
}


static void vo_grab_current_frame (vos_t *this, vo_frame_t *vo_frame, int64_t vpts)
{
  pthread_mutex_lock(&this->grab_lock);

  /* hold current frame for snapshot feature */
  if (this->last_frame)
    vo_frame_dec_lock(this->last_frame);
  vo_frame_inc_lock(vo_frame);
  this->last_frame = vo_frame;

  /* process grab queue */
  vos_grab_video_frame_t *frame = this->pending_grab_request;
  if (frame) {
    while (frame) {
      if (frame->vo_frame)
        vo_frame_dec_lock(frame->vo_frame);
      frame->vo_frame = NULL;

      if (vo_frame->format == XINE_IMGFMT_YV12 || vo_frame->format == XINE_IMGFMT_YUY2 || vo_frame->proc_provide_standard_frame_data) {
        vo_frame_inc_lock(vo_frame);
        frame->vo_frame = vo_frame;
        frame->grab_frame.vpts = vpts;
      }

      frame->finished = 1;
      vos_grab_video_frame_t *next = frame->next;
      frame->next = NULL;
      frame = next;
    }

    this->pending_grab_request = NULL;
    pthread_cond_broadcast(&this->grab_cond);
  }

  pthread_mutex_unlock(&this->grab_lock);
}


/* call vo_driver->proc methods for the entire frame */
static void vo_frame_driver_proc(vo_frame_t *img)
{
  if (img->proc_frame) {
    img->proc_frame(img);
  }
  if (img->proc_called) return;

  if (img->proc_slice) {
    int height = img->height;
    uint8_t* src[3];

    switch (img->format) {
    case XINE_IMGFMT_YV12:
      src[0] = img->base[0];
      src[1] = img->base[1];
      src[2] = img->base[2];
      while ((height -= 16) > -16) {
        img->proc_slice(img, src);
        src[0] += 16 * img->pitches[0];
        src[1] +=  8 * img->pitches[1];
        src[2] +=  8 * img->pitches[2];
      }
      break;
    case XINE_IMGFMT_YUY2:
      src[0] = img->base[0];
      while ((height -= 16) > -16) {
        img->proc_slice(img, src);
        src[0] += 16 * img->pitches[0];
      }
      break;
    }
  }
}

/*
 *
 * functions called by video decoder:
 *
 * get_frame => alloc frame for rendering
 *
 * frame_draw=> queue finished frame for display
 *
 * frame_free=> frame no longer used as reference frame by decoder
 *
 */

static vo_frame_t *vo_get_frame (xine_video_port_t *this_gen,
				 uint32_t width, uint32_t height,
				 double ratio, int format,
				 int flags) {

  vo_frame_t *img;
  vos_t      *this = (vos_t *) this_gen;

  lprintf ("get_frame (%d x %d)\n", width, height);

  while (!(img = vo_remove_from_img_buf_queue_nonblock (this->free_img_buf_queue,
                 width, height, ratio, format, flags)))
    if (this->xine->port_ticket->ticket_revoked)
      this->xine->port_ticket->renew(this->xine->port_ticket, 1);

  lprintf ("got a frame -> pthread_mutex_lock (&img->mutex)\n");

  /* some decoders report strange ratios */
  if (ratio <= 0.0)
    ratio = (double)width / (double)height;

  pthread_mutex_lock (&img->mutex);
  img->lock_counter   = 1;
  img->width          = width;
  img->height         = height;
  img->ratio          = ratio;
  img->format         = format;
  img->flags          = flags;
  img->proc_called    = 0;
  img->bad_frame      = 0;
  img->progressive_frame  = 0;
  img->repeat_first_field = 0;
  img->top_field_first    = 1;
  img->crop_left      = 0;
  img->crop_right     = 0;
  img->crop_top       = 0;
  img->crop_bottom    = 0;
  img->overlay_offset_x = 0;
  img->overlay_offset_y = 0;
  img->stream         = NULL;

  _x_extra_info_reset ( img->extra_info );

  /* let driver ensure this image has the right format */

  this->driver->update_frame_format (this->driver, img, width, height,
				     ratio, format, flags);

  pthread_mutex_unlock (&img->mutex);

  lprintf ("get_frame (%d x %d) done\n", width, height);

  return img;
}

static int vo_frame_draw (vo_frame_t *img, xine_stream_t *stream) {

  vos_t         *this = (vos_t *) img->port;
  int64_t        diff;
  int64_t        cur_vpts;
  int64_t        pic_vpts ;
  int            frames_to_skip;
  int            duration;

  /* handle anonymous streams like NULL for easy checking */
  if (stream == XINE_ANON_STREAM) stream = NULL;

  img->stream = stream;
  this->current_width = img->width;
  this->current_height = img->height;

  if (stream) {

    int new_framerate = img->duration==0?0:90000*1000/img->duration;
    if (this->framerate != new_framerate) {
      this->framerate = new_framerate;

      xine_event_t event;
      xine_framerate_data_t data;
      event.type = XINE_EVENT_FRAMERATE_CHANGE;
      event.stream = stream;
      event.data = &data;
      event.data_length = sizeof(data);
      data.framerate = this->framerate;
      xine_event_send( stream, &event );
    }

    if (img->pts!=0)
      this->last_pts = img->pts;

    _x_refcounter_inc(stream->refcounter);
    _x_extra_info_merge( img->extra_info, stream->video_decoder_extra_info );
    stream->metronom->got_video_frame (stream->metronom, img);
  }
  this->current_duration = img->duration;

  if (!this->grab_only) {

    pic_vpts = img->vpts;
    img->extra_info->vpts = img->vpts;

    cur_vpts = this->clock->get_current_time(this->clock);
    this->last_delivery_pts = cur_vpts;

    lprintf ("got image oat master vpts %" PRId64 ". vpts for picture is %" PRId64 " (pts was %" PRId64 ")\n",
	     cur_vpts, pic_vpts, img->pts);

    this->num_frames_delivered++;

    diff = pic_vpts - cur_vpts;

    /* avoid division by zero */
    if( img->duration <= 0 )
      duration = DEFAULT_FRAME_DURATION;
    else
      duration = img->duration;

    /* Frame dropping slow start:
     *   The engine starts to drop frames if there are less than frame_drop_limit
     *   frames in advance. There might be a problem just after a seek because
     *   there is no frame in advance yet.
     *   The following code increases progressively the frame_drop_limit (-2 -> 3)
     *   after a seek to give a chance to the engine to display the first frames
     *   smoothly before starting to drop frames if the decoder is really too
     *   slow.
     *   The above numbers are the result of frame_drop_limit_max beeing 3. They
     *   will be (-4 -> 1) when frame_drop_limit_max is only 1. This maximum value
     *   depends on the number of video buffers which the output device provides.
     */
    if (stream && stream->first_frame_flag == 2)
      this->frame_drop_cpt = 10;

    if (this->frame_drop_cpt) {
      this->frame_drop_limit = this->frame_drop_limit_max - (this->frame_drop_cpt / 2);
      this->frame_drop_cpt--;
    }
    frames_to_skip = ((-1 * diff) / duration + this->frame_drop_limit) * 2;

    /* do not skip decoding until output fifo frames are consumed */
    if (this->display_img_buf_queue->num_buffers >= this->frame_drop_limit ||
        frames_to_skip < 0)
      frames_to_skip = 0;

    /* Do not drop frames immediately, but remember this as suggestion and give
     * decoder a further chance to supply frames.
     * This avoids unnecessary frame drops in situations where there is only
     * a very little number of image buffers, e. g. when using xxmc.
     */
    if (this->frame_drop_suggested && frames_to_skip == 0)
      this->frame_drop_suggested = 0;

    if (frames_to_skip > 0) {
      if (!this->frame_drop_suggested) {
        this->frame_drop_suggested = 1;
        frames_to_skip = 0;
      }
    }

    lprintf ("delivery diff : %" PRId64 ", current vpts is %" PRId64 ", %d frames to skip\n",
	     diff, cur_vpts, frames_to_skip);

  } else {
    frames_to_skip = 0;

    if (this->discard_frames) {
      lprintf ("i'm in flush mode, not appending this frame to queue\n");

      return 0;
    }
  }


  if (!img->bad_frame) {

    int img_already_locked = 0;
    xine_list_iterator_t ite;

    /* add cropping requested by frontend */
    img->crop_left   = (img->crop_left + this->crop_left) & ~1;
    img->crop_right  = (img->crop_right + this->crop_right) & ~1;
    img->crop_top    += this->crop_top;
    img->crop_bottom += this->crop_bottom;

    /* perform cropping when vo driver does not support it */
    if( (img->crop_left || img->crop_top ||
         img->crop_right || img->crop_bottom) &&
        (this->grab_only ||
         !(this->driver->get_capabilities (this->driver) & VO_CAP_CROP)) ) {
      if (img->format == XINE_IMGFMT_YV12 || img->format == XINE_IMGFMT_YUY2) {
        img->overlay_offset_x -= img->crop_left;
        img->overlay_offset_y -= img->crop_top;
        img = crop_frame( img->port, img );
        img_already_locked = 1;
      } else {
	/* noone knows how to crop this, so we can only ignore the cropping */
	img->crop_left   = 0;
	img->crop_top    = 0;
	img->crop_right  = 0;
	img->crop_bottom = 0;
      }
    }

    /* do not call proc_*() for frames that will be dropped */
    if( !frames_to_skip && !img->proc_called )
      vo_frame_driver_proc(img);

    /*
     * put frame into FIFO-Buffer
     */

    lprintf ("frame is ok => appending to display buffer\n");

    /*
     * check for first frame after seek and mark it
     */
    img->is_first = 0;
    /* avoid a complex deadlock situation caused by net_buf_control */
    if (!pthread_mutex_trylock(&this->streams_lock)) {
      for (ite = xine_list_front(this->streams); ite;
           ite = xine_list_next(this->streams, ite)) {
        stream = xine_list_get_value(this->streams, ite);
        if (stream == XINE_ANON_STREAM) continue;
        pthread_mutex_lock (&stream->first_frame_lock);
        if (stream->first_frame_flag == 2) {
          if (this->grab_only) {
            stream->first_frame_flag = 0;
            pthread_cond_broadcast(&stream->first_frame_reached);
          } else {
            stream->first_frame_flag = 1;
          }
          img->is_first = FIRST_FRAME_MAX_POLL;

          lprintf ("get_next_video_frame first_frame_reached\n");
        }
        pthread_mutex_unlock (&stream->first_frame_lock);
      }
      pthread_mutex_unlock(&this->streams_lock);
    }

    if (!img_already_locked)
      vo_frame_inc_lock( img );
    vo_append_to_img_buf_queue (this->display_img_buf_queue, img);

  } else {
    lprintf ("bad_frame\n");

    if (stream) {
      pthread_mutex_lock( &stream->current_extra_info_lock );
      _x_extra_info_merge( stream->current_extra_info, img->extra_info );
      pthread_mutex_unlock( &stream->current_extra_info_lock );
    }

    this->num_frames_skipped++;
  }

  /*
   * performance measurement
   */

  if ((this->num_frames_delivered % 200) == 0 && this->num_frames_delivered) {
    int send_event;
    xine_list_iterator_t ite;

    if( (100 * this->num_frames_skipped / this->num_frames_delivered) >
         this->warn_skipped_threshold ||
        (100 * this->num_frames_discarded / this->num_frames_delivered) >
         this->warn_discarded_threshold )
      this->warn_threshold_exceeded++;
    else
      this->warn_threshold_exceeded = 0;

    /* make sure threshold has being consistently exceeded - 5 times in a row
     * (that is, this is not just a small burst of dropped frames).
     */
    send_event = (this->warn_threshold_exceeded == 5 &&
                  !this->warn_threshold_event_sent);
    this->warn_threshold_event_sent = send_event;

    pthread_mutex_lock(&this->streams_lock);
    for (ite = xine_list_front(this->streams); ite;
         ite = xine_list_next(this->streams, ite)) {
      stream = xine_list_get_value(this->streams, ite);
      if (stream == XINE_ANON_STREAM) continue;
      _x_stream_info_set(stream, XINE_STREAM_INFO_SKIPPED_FRAMES,
			 1000 * this->num_frames_skipped / this->num_frames_delivered);
      _x_stream_info_set(stream, XINE_STREAM_INFO_DISCARDED_FRAMES,
			 1000 * this->num_frames_discarded / this->num_frames_delivered);

      /* we send XINE_EVENT_DROPPED_FRAMES to frontend to warn that
       * number of skipped or discarded frames is too high.
       */
      if( send_event ) {
         xine_event_t          event;
         xine_dropped_frames_t data;

         event.type        = XINE_EVENT_DROPPED_FRAMES;
         event.stream      = stream;
         event.data        = &data;
         event.data_length = sizeof(data);
         data.skipped_frames = _x_stream_info_get(stream, XINE_STREAM_INFO_SKIPPED_FRAMES);
         data.skipped_threshold = this->warn_skipped_threshold * 10;
         data.discarded_frames = _x_stream_info_get(stream, XINE_STREAM_INFO_DISCARDED_FRAMES);
         data.discarded_threshold = this->warn_discarded_threshold * 10;
         xine_event_send(stream, &event);
      }
    }
    pthread_mutex_unlock(&this->streams_lock);


    if( this->num_frames_skipped || this->num_frames_discarded ) {
      xine_log(this->xine, XINE_LOG_MSG,
	       _("%d frames delivered, %d frames skipped, %d frames discarded\n"),
	       this->num_frames_delivered,
	       this->num_frames_skipped, this->num_frames_discarded);
    }

    this->num_frames_delivered = 0;
    this->num_frames_discarded = 0;
    this->num_frames_skipped   = 0;
  }

  return frames_to_skip;
}

/*
 *
 * video out loop related functions
 *
 */

/* duplicate_frame(): this function is used to keep playing frames
 * while video is still or player paused.
 *
 * frame allocation inside vo loop is dangerous:
 * we must never wait for a free frame -> deadlock condition.
 * to avoid deadlocks we don't use vo_remove_from_img_buf_queue()
 * and reimplement a slightly modified version here.
 * free_img_buf_queue->mutex must be grabbed prior entering it.
 * (must assure that free frames won't be exhausted by decoder thread).
 */
static vo_frame_t * duplicate_frame( vos_t *this, vo_frame_t *img ) {

  vo_frame_t *dupl;

  if( !this->free_img_buf_queue->first)
    return NULL;

  dupl = this->free_img_buf_queue->first;
  this->free_img_buf_queue->first = dupl->next;
  dupl->next = NULL;
  if (!this->free_img_buf_queue->first) {
    this->free_img_buf_queue->last = NULL;
    this->free_img_buf_queue->num_buffers = 0;
  }
  else {
    this->free_img_buf_queue->num_buffers--;
  }

  pthread_mutex_lock (&dupl->mutex);
  dupl->lock_counter   = 1;
  dupl->width          = img->width;
  dupl->height         = img->height;
  dupl->ratio          = img->ratio;
  dupl->format         = img->format;
  dupl->flags          = img->flags | VO_BOTH_FIELDS;
  dupl->progressive_frame  = img->progressive_frame;
  dupl->repeat_first_field = img->repeat_first_field;
  dupl->top_field_first    = img->top_field_first;
  dupl->crop_left      = img->crop_left;
  dupl->crop_right     = img->crop_right;
  dupl->crop_top       = img->crop_top;
  dupl->crop_bottom    = img->crop_bottom;
  dupl->overlay_offset_x = img->overlay_offset_x;
  dupl->overlay_offset_y = img->overlay_offset_y;

  this->driver->update_frame_format (this->driver, dupl, dupl->width, dupl->height,
				     dupl->ratio, dupl->format, dupl->flags);

  pthread_mutex_unlock (&dupl->mutex);

  if (dupl->proc_duplicate_frame_data) {
    dupl->proc_duplicate_frame_data(dupl,img);
  } else {

    switch (img->format) {
    case XINE_IMGFMT_YV12:
      yv12_to_yv12(
       /* Y */
        img->base[0], img->pitches[0],
        dupl->base[0], dupl->pitches[0],
       /* U */
        img->base[1], img->pitches[1],
        dupl->base[1], dupl->pitches[1],
       /* V */
        img->base[2], img->pitches[2],
        dupl->base[2], dupl->pitches[2],
       /* width x height */
        img->width, img->height);
      break;
    case XINE_IMGFMT_YUY2:
      yuy2_to_yuy2(
       /* src */
        img->base[0], img->pitches[0],
       /* dst */
        dupl->base[0], dupl->pitches[0],
       /* width x height */
        img->width, img->height);
      break;
    }
  }

  dupl->bad_frame   = 0;
  dupl->pts         = 0;
  dupl->vpts        = 0;
  dupl->proc_called = 0;

  dupl->duration  = img->duration;
  dupl->is_first  = 0;

  dupl->stream    = NULL;
  memcpy( dupl->extra_info, img->extra_info, sizeof(extra_info_t) );

  /* delay frame processing for now, we might not even need it (eg. frame will be discarded) */
  /* vo_frame_driver_proc(dupl); */

  return dupl;
}


static void expire_frames (vos_t *this, int64_t cur_vpts) {

  int64_t       pts;
  int64_t       diff;
  vo_frame_t   *img;
  int           duration;

  pthread_mutex_lock(&this->display_img_buf_queue->mutex);

  img = this->display_img_buf_queue->first;

  /*
   * throw away expired frames
   */

  diff = 1000000; /* always enter the while-loop */
  duration = 0;

  while (img) {

    if (img->is_first > 0) {
      lprintf("expire_frames: first_frame !\n");

      /*
       * before displaying the first frame without
       * "metronom prebuffering" we should make sure it's
       * not used as a decoder reference anymore.
       */
      if( img->lock_counter == 1 ) {
        /* display it immediately */
        img->vpts = cur_vpts;
      } else {
        /* poll */
        lprintf("frame still referenced %d times, is_first=%d\n", img->lock_counter, img->is_first);
        img->vpts = cur_vpts + FIRST_FRAME_POLL_DELAY;
      }
      img->is_first--;
      /* make sure to wake up xine_play even if this first frame gets discarded */
      if (img->is_first == 0) img->is_first = -1;
      break;
    }

    if( !img->duration ) {
      if( img->next )
        duration = img->next->vpts - img->vpts;
      else
        duration = DEFAULT_FRAME_DURATION;
    } else
      duration = img->duration;

    pts = img->vpts;
    diff = cur_vpts - pts;

    if (diff > duration || this->discard_frames) {

      if( !this->discard_frames ) {
        xine_log(this->xine, XINE_LOG_MSG,
	         _("video_out: throwing away image with pts %" PRId64 " because it's too old (diff : %" PRId64 ").\n"), pts, diff);

        this->num_frames_discarded++;
      }

      img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue, 1, 0, 0, 0, 0, 0);

      if (img->stream) {
	pthread_mutex_lock( &img->stream->current_extra_info_lock );
	_x_extra_info_merge( img->stream->current_extra_info, img->extra_info );
	pthread_mutex_unlock( &img->stream->current_extra_info_lock );
	/* wake up xine_play now if we just discarded first frame */
	if (img->is_first != 0) {
	  xine_list_iterator_t ite;
	  pthread_mutex_lock (&this->streams_lock);
	  for (ite = xine_list_front(this->streams); ite;
	    ite = xine_list_next(this->streams, ite)) {
	    xine_stream_t *stream = xine_list_get_value (this->streams, ite);
	    if (stream == XINE_ANON_STREAM) continue;
	    pthread_mutex_lock (&stream->first_frame_lock);
	    if (stream->first_frame_flag) {
	      stream->first_frame_flag = 0;
	      pthread_cond_broadcast (&stream->first_frame_reached);
	    }
	    pthread_mutex_unlock (&stream->first_frame_lock);
	  }
	  pthread_mutex_unlock(&this->streams_lock);
	  xine_log (this->xine, XINE_LOG_MSG, _("video_out: just discarded first frame after seek\n"));
	}
      }

      /* when flushing frames, keep the first one as backup */
      if( this->discard_frames ) {

        if (!this->img_backup) {
	  this->img_backup = img;
        } else {
	  vo_frame_dec_lock( img );
        }

      } else {
        /*
         * last frame? back it up for
         * still frame creation
         */

        if (!this->display_img_buf_queue->first) {

	  if (this->img_backup) {
	    lprintf("overwriting frame backup\n");

	    vo_frame_dec_lock( this->img_backup );
	  }

	  lprintf("possible still frame (old)\n");

	  this->img_backup = img;

	  /* wait 4 frames before drawing this one.
	     this allow slower systems to recover. */
	  this->redraw_needed = 4;
        } else {
	  vo_frame_dec_lock( img );
        }
      }
      img = this->display_img_buf_queue->first;

    } else
      break;
  }

  pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
}

/* If it's not the time to display the next frame,
 * the vpts of the next frame (if any) is returned, 0 otherwise.
 */
static vo_frame_t *get_next_frame (vos_t *this, int64_t cur_vpts,
                                   int64_t *next_frame_vpts) {

  vo_frame_t   *img;

  pthread_mutex_lock(&this->display_img_buf_queue->mutex);

  img = this->display_img_buf_queue->first;

  *next_frame_vpts = 0;

  /*
   * still frame detection:
   */

  /* no frame? => still frame detection */

  if (!img) {

    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

    lprintf ("no frame\n");

    if (this->img_backup && (this->redraw_needed==1)) {

      lprintf("generating still frame (cur_vpts = %" PRId64 ") \n", cur_vpts);

      /* keep playing still frames */
      pthread_mutex_lock( &this->free_img_buf_queue->mutex );
      img = duplicate_frame (this, this->img_backup );
      pthread_mutex_unlock( &this->free_img_buf_queue->mutex );
      if( img ) {
        img->vpts = cur_vpts;
        img->duration = DEFAULT_FRAME_DURATION;
        /* extra info of the backup is thrown away, because it is not up to date */
        _x_extra_info_reset(img->extra_info);
        img->future_frame = NULL;
      }
      return img;

    } else {

      if( this->redraw_needed )
        this->redraw_needed--;

      lprintf ("no frame, but no backup frame\n");

      return NULL;
    }
  } else {

    int64_t diff;

    diff = cur_vpts - img->vpts;

    /*
     * time to display frame "img" ?
     */

    lprintf ("diff %" PRId64 "\n", diff);

    if (diff < 0) {
      *next_frame_vpts = img->vpts;
      pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
      return NULL;
    }

    if (this->img_backup) {
      lprintf("freeing frame backup\n");

      vo_frame_dec_lock( this->img_backup );
      this->img_backup = NULL;
    }

    /*
     * last frame? make backup for possible still image
     */
    pthread_mutex_lock( &this->free_img_buf_queue->mutex );
    if (img && !img->next) {

      if (!img->stream ||
          _x_stream_info_get(img->stream, XINE_STREAM_INFO_VIDEO_HAS_STILL) ||
          !img->stream->video_fifo ||
          img->stream->video_fifo->size(img->stream->video_fifo) < 10) {

        lprintf ("possible still frame\n");

        this->img_backup = duplicate_frame (this, img);
      }
    }
    pthread_mutex_unlock( &this->free_img_buf_queue->mutex );

    /*
     * remove frame from display queue and show it
     */

    if ( img ) {
      if ( img->next )
        img->future_frame = img->next;
      else
        img->future_frame = NULL;
    }
    
    img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue, 1, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

    return img;
  }
}

static void overlay_and_display_frame (vos_t *this,
				       vo_frame_t *img, int64_t vpts) {
  xine_stream_t *stream;
  xine_list_iterator_t ite;

  lprintf ("displaying image with vpts = %" PRId64 "\n", img->vpts);

  /* no, this is not were proc_*() is usually called.
   * it's just to catch special cases like late or duplicated frames.
   */
  if(!img->proc_called )
    vo_frame_driver_proc(img);

  if (img->stream) {
    int64_t diff;
    pthread_mutex_lock( &img->stream->current_extra_info_lock );
    diff = img->extra_info->vpts - img->stream->current_extra_info->vpts;
    if ((diff > 3000) || (diff<-300000))
      _x_extra_info_merge( img->stream->current_extra_info, img->extra_info );
    pthread_mutex_unlock( &img->stream->current_extra_info_lock );
  }

  /* xine_play() may be called from a thread that has the display device locked
   * (eg an X window event handler). If it is waiting for a frame we better wake
   * it up _before_ we start displaying, or the first 10 seconds of video are lost.
   */
  if( img->is_first ) {
    pthread_mutex_lock(&this->streams_lock);
    for (ite = xine_list_front(this->streams); ite;
         ite = xine_list_next(this->streams, ite)) {
      stream = xine_list_get_value(this->streams, ite);
      if (stream == XINE_ANON_STREAM) continue;
      pthread_mutex_lock (&stream->first_frame_lock);
      if (stream->first_frame_flag) {
        stream->first_frame_flag = 0;
        pthread_cond_broadcast(&stream->first_frame_reached);
      }
      pthread_mutex_unlock (&stream->first_frame_lock);
    }
    pthread_mutex_unlock(&this->streams_lock);
  }

  if (this->overlay_source) {
    this->overlay_source->multiple_overlay_blend (this->overlay_source,
						  vpts,
						  this->driver, img,
						  this->video_loop_running && this->overlay_enabled);
  }

  vo_grab_current_frame (this, img, vpts);

  this->driver->display_frame (this->driver, img);

  this->redraw_needed = 0;
}

static void check_redraw_needed (vos_t *this, int64_t vpts) {

  if (this->overlay_source) {
    if( this->overlay_source->redraw_needed (this->overlay_source, vpts) )
      this->redraw_needed = 1;
  }

  if( this->driver->redraw_needed (this->driver) )
    this->redraw_needed = 1;
}

static int interruptable_sleep(vos_t *this, int usec_to_sleep)
{
  int timedout = 0;

  struct timeval now;
  gettimeofday(&now, 0);

  pthread_mutex_lock (&this->trigger_drawing_mutex);
  if (!this->trigger_drawing) {
    struct timespec abstime;
    abstime.tv_sec  = now.tv_sec + usec_to_sleep / 1000000;
    abstime.tv_nsec = now.tv_usec * 1000 + (usec_to_sleep % 1000000) * 1000;

    if (abstime.tv_nsec > 1000000000) {
      abstime.tv_nsec -= 1000000000;
      abstime.tv_sec++;
    }

    timedout = pthread_cond_timedwait(&this->trigger_drawing_cond, &this->trigger_drawing_mutex, &abstime);
  }
  this->trigger_drawing = 0;
  pthread_mutex_unlock (&this->trigger_drawing_mutex);

  return timedout;
}

/* PRIVATE to paused_loop () */
static vo_frame_t *force_duplicate_frame (vos_t *this, vo_frame_t *s) {
  vo_frame_t *img, *prev = NULL;

  if (!s)
    return NULL;

  pthread_mutex_lock (&this->free_img_buf_queue->mutex);
  img = this->free_img_buf_queue->first;

  if (!img) {
    /* OK we run out of free frames. Try to whistle back a frame already waiting for display.
       Search for one that is _not_ a DR1 reference frame that the decoder wants unchanged */
    pthread_mutex_lock (&this->display_img_buf_queue->mutex);
    for (img = this->display_img_buf_queue->first; img; img = img->next) {
      if (img->lock_counter == 1) break;
      prev = img;
    }

    if (img) {
      if (img == this->display_img_buf_queue->first)
        this->display_img_buf_queue->first = img->next;
      if (prev)
        prev->next = img->next;
      if (img == this->display_img_buf_queue->last)
        this->display_img_buf_queue->last = prev;
      if (!this->display_img_buf_queue->first)
        this->display_img_buf_queue->num_buffers = 0;
      else
        this->display_img_buf_queue->num_buffers--;

      img->next = NULL;

      if (img != s) {
        /* Now put it into free queue where it gets taken from next */
        this->free_img_buf_queue->first = img;
        this->free_img_buf_queue->last  = img;
        this->free_img_buf_queue->num_buffers = 1;
        img->lock_counter = 0;
      }
    }
    pthread_mutex_unlock (&this->display_img_buf_queue->mutex);
  }

  if (img != s)
    img = duplicate_frame (this, s);

  pthread_mutex_unlock (&this->free_img_buf_queue->mutex);

  return img;
}

/* special loop for paused mode
 * needed to update screen due overlay changes, resize, window
 * movement, brightness adjusting etc.
 */
static void paused_loop( vos_t *this, int64_t vpts )
{
  vo_frame_t   *img;

  /* prevent decoder thread from allocating new frames */
  pthread_mutex_lock (&this->free_img_buf_queue->mutex);
  this->free_img_buf_queue->locked_for_read = 1;
  pthread_mutex_unlock (&this->free_img_buf_queue->mutex);

  while (this->clock->speed == XINE_SPEED_PAUSE && this->video_loop_running) {

    /* set img_backup to play the same frame several times */
    if (!this->img_backup) {
      this->img_backup = force_duplicate_frame (this, this->display_img_buf_queue->first);
      this->redraw_needed = 1;
    }

    check_redraw_needed( this, vpts );

    if (this->redraw_needed) {
      img = force_duplicate_frame (this, this->img_backup);
      if( img ) {
        /* extra info of the backup is thrown away, because it is not up to date */
        _x_extra_info_reset(img->extra_info);
        overlay_and_display_frame (this, img, vpts);
      }
    }

    interruptable_sleep(this, 20000);
  }

  pthread_mutex_lock (&this->free_img_buf_queue->mutex);
  this->free_img_buf_queue->locked_for_read = 0;
  pthread_mutex_unlock (&this->free_img_buf_queue->mutex);
}

static void video_out_update_disable_flush_from_video_out(void *disable_decoder_flush_from_video_out, xine_cfg_entry_t *entry)
{
  *(int *)disable_decoder_flush_from_video_out = entry->num_value;
}

static void *video_out_loop (void *this_gen) {

  int64_t            vpts, diff;
  vo_frame_t        *img;
  vos_t             *this = (vos_t *) this_gen;
  int64_t            next_frame_vpts = 0;
  int64_t            usec_to_sleep;
  int                disable_decoder_flush_from_video_out;

#ifndef WIN32
  errno = 0;
  if (nice(-2) == -1 && errno)
    xine_log(this->xine, XINE_LOG_MSG, "video_out: can't raise nice priority by 2: %s\n", strerror(errno));
#endif /* WIN32 */

  disable_decoder_flush_from_video_out = this->xine->config->register_bool(this->xine->config, "engine.decoder.disable_flush_from_video_out", 0,
      _("disable decoder flush from video out"),
      _("video out causes a decoder flush when video out runs out of frames for displaying,\n"
        "because the decoder hasn't deliverd new frames for quite a while.\n"
        "flushing the decoder causes decoding errors for images decoded after the flush.\n"
        "to avoid the decoding errors, decoder flush at video out should be disabled.\n\n"
        "WARNING: as the flush was introduced to fix some issues when playing DVD still images, it is\n"
        "likely that these issues may reappear in case they haven't been fixed differently meanwhile.\n"),
        20, video_out_update_disable_flush_from_video_out, &disable_decoder_flush_from_video_out);

  /*
   * here it is - the heart of xine (or rather: one of the hearts
   * of xine) : the video output loop
   */

  lprintf ("loop starting...\n");

  while ( this->video_loop_running ) {

    /*
     * get current time and find frame to display
     */

    vpts = this->clock->get_current_time (this->clock);

    lprintf ("loop iteration at %" PRId64 "\n", vpts);

    expire_frames (this, vpts);

    img = get_next_frame (this, vpts, &next_frame_vpts);

    /*
     * if we have found a frame, display it
     */

    if (img) {
      lprintf ("displaying frame (id=%d)\n", img->id);

      overlay_and_display_frame (this, img, vpts);

    } else {

      check_redraw_needed( this, vpts );
    }

    /*
     * if we haven't heared from the decoder for some time
     * flush it
     * test display fifo empty to protect from deadlocks
     */

    diff = vpts - this->last_delivery_pts;
    if (diff > 30000 && !this->display_img_buf_queue->first) {
      xine_list_iterator_t ite;

      pthread_mutex_lock(&this->streams_lock);
      for (ite = xine_list_front(this->streams); ite;
           ite = xine_list_next(this->streams, ite)) {
	xine_stream_t *stream = xine_list_get_value(this->streams, ite);
	if (stream == XINE_ANON_STREAM) continue;
        if (stream->video_decoder_plugin && stream->video_fifo && !disable_decoder_flush_from_video_out) {
          buf_element_t *buf;

	  lprintf ("flushing current video decoder plugin\n");

          buf = stream->video_fifo->buffer_pool_try_alloc (stream->video_fifo);
          if( buf ) {
            buf->type = BUF_CONTROL_FLUSH_DECODER;
            stream->video_fifo->insert(stream->video_fifo, buf);
          }
        }
      }
      pthread_mutex_unlock(&this->streams_lock);

      this->last_delivery_pts = vpts;
    }

    /*
     * wait until it's time to display next frame
     */
    if (img) {
      next_frame_vpts = img->vpts + img->duration;
    }
    /* else next_frame_vpts is returned by get_next_frame */

    lprintf ("next_frame_vpts is %" PRId64 "\n", next_frame_vpts);

    do {
      vpts = this->clock->get_current_time (this->clock);

      if (this->clock->speed == XINE_SPEED_PAUSE)
        paused_loop (this, vpts);

      if (next_frame_vpts && this->clock->speed > 0) {
        usec_to_sleep = (next_frame_vpts - vpts) * 100 * XINE_FINE_SPEED_NORMAL / (9 * this->clock->speed);
      } else {
        /* we don't know when the next frame is due, only wait a little */
        usec_to_sleep = 1000;
        next_frame_vpts = vpts; /* wait only once */
      }

      /* limit usec_to_sleep to maintain responsiveness */
      if (usec_to_sleep > MAX_USEC_TO_SLEEP)
        usec_to_sleep = MAX_USEC_TO_SLEEP;

      lprintf ("%" PRId64 " usec to sleep at master vpts %" PRId64 "\n", usec_to_sleep, vpts);

      if ( (next_frame_vpts - vpts) > 2*90000 )
        xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		"video_out: vpts/clock error, next_vpts=%" PRId64 " cur_vpts=%" PRId64 "\n", next_frame_vpts,vpts);

      if (usec_to_sleep > 0)
      {
        /* honor trigger update only when a backup img is available */
        if (0 == interruptable_sleep(this, usec_to_sleep) && this->img_backup)
          break;
      }

      if (this->discard_frames)
        break;

    } while ( (usec_to_sleep > 0) && this->video_loop_running);
  }

  /*
   * throw away undisplayed frames
   */

  pthread_mutex_lock(&this->display_img_buf_queue->mutex);
  img = this->display_img_buf_queue->first;
  while (img) {

    img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue, 1, 0, 0, 0, 0, 0);
    vo_frame_dec_lock( img );

    img = this->display_img_buf_queue->first;
  }
  pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

  if (this->img_backup) {
    vo_frame_dec_lock( this->img_backup );
    this->img_backup = NULL;
  }

  pthread_mutex_lock(&this->grab_lock);
  if (this->last_frame) {
    vo_frame_dec_lock( this->last_frame );
    this->last_frame = NULL;
  }
  pthread_mutex_unlock(&this->grab_lock);

  return NULL;
}

/*
 * public function for video processing frontends to manually
 * consume video frames
 */

int xine_get_next_video_frame (xine_video_port_t *this_gen,
			       xine_video_frame_t *frame) {

  vos_t         *this   = (vos_t *) this_gen;
  vo_frame_t    *img    = NULL;
  xine_stream_t *stream = NULL;

  while (!img || !stream) {
    xine_list_iterator_t ite = xine_list_front(this->streams);
    stream = xine_list_get_value(this->streams, ite);
    if (!stream) {
      xine_usec_sleep (5000);
      continue;
    }

    /* FIXME: ugly, use conditions and locks instead? */

    pthread_mutex_lock(&this->display_img_buf_queue->mutex);
    img = this->display_img_buf_queue->first;
    if (!img) {
      pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
      if (stream != XINE_ANON_STREAM && stream->video_fifo->fifo_size == 0 &&
          stream->demux_plugin->get_status(stream->demux_plugin) != DEMUX_OK)
        /* no further data can be expected here */
        return 0;
      xine_usec_sleep (5000);
      continue;
    }
  }

  /*
   * remove frame from display queue and show it
   */

  img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue, 1, 0, 0, 0, 0, 0);
  pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

  frame->vpts         = img->vpts;
  frame->duration     = img->duration;
  frame->width        = img->width;
  frame->height       = img->height;
  frame->pos_stream   = img->extra_info->input_normpos;
  frame->pos_time     = img->extra_info->input_time;
  frame->frame_number = img->extra_info->frame_number;
  frame->aspect_ratio = img->ratio;
  frame->colorspace   = img->format;
  frame->data         = img->base[0];
  frame->xine_frame   = img;

  return 1;
}

void xine_free_video_frame (xine_video_port_t *port,
			    xine_video_frame_t *frame) {

  vo_frame_t *img = (vo_frame_t *) frame->xine_frame;

  vo_frame_dec_lock (img);
}


static uint32_t vo_get_capabilities (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->driver->get_capabilities (this->driver);
}

static void vo_open (xine_video_port_t *this_gen, xine_stream_t *stream) {

  vos_t      *this = (vos_t *) this_gen;

  lprintf("vo_open\n");

  this->video_opened = 1;
  this->discard_frames = 0;
  this->last_delivery_pts = 0;
  this->warn_threshold_event_sent = this->warn_threshold_exceeded = 0;
  if (!this->overlay_enabled && (stream == XINE_ANON_STREAM || stream == NULL || stream->spu_channel_user > -2))
    /* enable overlays if our new stream might want to show some */
    this->overlay_enabled = 1;
  pthread_mutex_lock(&this->streams_lock);
  xine_list_push_back(this->streams, stream);
  pthread_mutex_unlock(&this->streams_lock);
}

static void vo_close (xine_video_port_t *this_gen, xine_stream_t *stream) {

  vos_t      *this = (vos_t *) this_gen;
  xine_list_iterator_t ite;

  /* this will make sure all hide events were processed */
  if (this->overlay_source)
    this->overlay_source->flush_events (this->overlay_source);

  this->video_opened = 0;

  /* unregister stream */
  pthread_mutex_lock(&this->streams_lock);
  for (ite = xine_list_front(this->streams); ite;
       ite = xine_list_next(this->streams, ite)) {
    xine_stream_t *cur = xine_list_get_value(this->streams, ite);
    if (cur == stream) {
      xine_list_remove(this->streams, ite);
      break;
    }
  }
  pthread_mutex_unlock(&this->streams_lock);
}


static int vo_get_property (xine_video_port_t *this_gen, int property) {
  vos_t *this = (vos_t *) this_gen;
  int ret;

  switch (property) {
  case VO_PROP_DISCARD_FRAMES:
    ret = this->discard_frames;
    break;

  case VO_PROP_BUFS_IN_FIFO:
    ret = this->video_loop_running ? this->display_img_buf_queue->num_buffers : -1;
    break;

  case VO_PROP_BUFS_FREE:
    ret = this->video_loop_running ? this->free_img_buf_queue->num_buffers : -1;
    break;

  case VO_PROP_BUFS_TOTAL:
    ret = this->video_loop_running ? this->free_img_buf_queue->num_buffers_max : -1;
    break;

  case VO_PROP_NUM_STREAMS:
    pthread_mutex_lock(&this->streams_lock);
    ret = xine_list_size(this->streams);
    pthread_mutex_unlock(&this->streams_lock);
    break;

  case VO_PROP_LAST_PTS:
    ret = (int)&this->last_pts;
    break;

  /*
   * handle XINE_PARAM_xxx properties (convert from driver's range)
   */
  case XINE_PARAM_VO_CROP_LEFT:
    ret = this->crop_left;
    break;
  case XINE_PARAM_VO_CROP_RIGHT:
    ret = this->crop_right;
    break;
  case XINE_PARAM_VO_CROP_TOP:
    ret = this->crop_top;
    break;
  case XINE_PARAM_VO_CROP_BOTTOM:
    ret = this->crop_bottom;
    break;

  case XINE_PARAM_VO_SHARPNESS:
  case XINE_PARAM_VO_NOISE_REDUCTION:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_GAMMA:
   {
    int v, min_v, max_v, range_v;

    pthread_mutex_lock( &this->driver_lock );
    this->driver->get_property_min_max (this->driver,
					property & 0xffffff,
					&min_v, &max_v);

    v = this->driver->get_property (this->driver, property & 0xffffff);

    range_v = max_v - min_v + 1;

    if (range_v > 0)
      ret = ((v-min_v) * 65536 + 32768) / range_v;
    else
      ret = 0;
    pthread_mutex_unlock( &this->driver_lock );
  }
    break;

  default:
    pthread_mutex_lock( &this->driver_lock );
    ret = this->driver->get_property(this->driver, property & 0xffffff);
    pthread_mutex_unlock( &this->driver_lock );
  }
  return ret;
}

static int vo_set_property (xine_video_port_t *this_gen, int property, int value) {
  vos_t *this = (vos_t *) this_gen;
  int ret;

  switch (property) {

  case VO_PROP_DISCARD_FRAMES:
    /* recursive discard frames setting */
    pthread_mutex_lock(&this->display_img_buf_queue->mutex);
    if(value)
      this->discard_frames++;
    else if (this->discard_frames)
      this->discard_frames--;
    else
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "vo_set_property: discard_frames is already zero\n");
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
    ret = this->discard_frames;

    /* discard buffers here because we have no output thread */
    if (this->grab_only && this->discard_frames) {
      vo_frame_t *img;

      pthread_mutex_lock(&this->display_img_buf_queue->mutex);

      while ((img = this->display_img_buf_queue->first)) {

        lprintf ("flushing out frame\n");

        img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue, 1, 0, 0, 0, 0, 0);

        vo_frame_dec_lock (img);
      }
      pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
    }
    break;

  /*
   * handle XINE_PARAM_xxx properties (convert to driver's range)
   */
  case XINE_PARAM_VO_CROP_LEFT:
    if( value < 0 )
      value = 0;
    ret = this->crop_left = value;
    break;
  case XINE_PARAM_VO_CROP_RIGHT:
    if( value < 0 )
      value = 0;
    ret = this->crop_right = value;
    break;
  case XINE_PARAM_VO_CROP_TOP:
    if( value < 0 )
      value = 0;
    ret = this->crop_top = value;
    break;
  case XINE_PARAM_VO_CROP_BOTTOM:
    if( value < 0 )
      value = 0;
    ret = this->crop_bottom = value;
    break;

  case XINE_PARAM_VO_SHARPNESS:
  case XINE_PARAM_VO_NOISE_REDUCTION:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_GAMMA:
    if (!this->grab_only) {
      int v, min_v, max_v, range_v;

      pthread_mutex_lock( &this->driver_lock );

      this->driver->get_property_min_max (this->driver,
					property & 0xffffff,
					&min_v, &max_v);

      range_v = max_v - min_v + 1;

      v = (value * range_v + (range_v/2)) / 65536 + min_v;

      this->driver->set_property(this->driver, property & 0xffffff, v);
      pthread_mutex_unlock( &this->driver_lock );
      ret = value;
    } else
      ret = 0;
    break;


  default:
    if (!this->grab_only) {
      pthread_mutex_lock( &this->driver_lock );
      ret =  this->driver->set_property(this->driver, property & 0xffffff, value);
      pthread_mutex_unlock( &this->driver_lock );
    } else
      ret = 0;
  }

  return ret;
}


static int vo_status (xine_video_port_t *this_gen, xine_stream_t *stream,
                      int *width, int *height, int64_t *img_duration) {

  vos_t      *this = (vos_t *) this_gen;
  xine_list_iterator_t ite;
  int ret = 0;

  pthread_mutex_lock(&this->streams_lock);
  for (ite = xine_list_front(this->streams); ite;
       ite = xine_list_next(this->streams, ite)) {
    xine_stream_t *cur = xine_list_get_value(this->streams, ite);
    if (cur == stream || !stream) {
      *width = this->current_width;
      *height = this->current_height;
      *img_duration = this->current_duration;
      ret = !!stream; /* return false for a NULL stream, true otherwise */
      break;
    }
  }
  pthread_mutex_unlock(&this->streams_lock);

  return ret;
}


static void vo_free_img_buffers (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  vo_frame_t *img;

  while (this->free_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->free_img_buf_queue);
    img->dispose (img);
  }

  while (this->display_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue) ;
    img->dispose (img);
  }

  free (this->extra_info_base);
}

static void vo_exit (xine_video_port_t *this_gen) {

  vos_t      *this = (vos_t *) this_gen;

  lprintf ("vo_exit...\n");

  if (this->video_loop_running) {
    void *p;

    this->video_loop_running = 0;

    pthread_join (this->video_thread, &p);
  }

  vo_free_img_buffers (this_gen);

  this->driver->dispose (this->driver);

  lprintf ("vo_exit... done\n");

  if (this->overlay_source) {
    this->overlay_source->dispose (this->overlay_source);
  }

  xine_list_delete(this->streams);
  pthread_mutex_destroy(&this->streams_lock);

  free (this->free_img_buf_queue);
  free (this->display_img_buf_queue);

  pthread_cond_destroy(&this->trigger_drawing_cond);
  pthread_mutex_destroy(&this->trigger_drawing_mutex);

  pthread_mutex_destroy(&this->grab_lock);
  pthread_cond_destroy(&this->grab_cond);

  free (this);
}

static vo_frame_t *vo_get_last_frame (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  vo_frame_t *last_frame;
  
  pthread_mutex_lock(&this->grab_lock);

  last_frame = this->last_frame;
  if (last_frame)
    vo_frame_inc_lock(last_frame);

  pthread_mutex_unlock(&this->grab_lock);

  return last_frame;
}

/*
 * overlay stuff
 */

static video_overlay_manager_t *vo_get_overlay_manager (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->overlay_source;
}

static void vo_enable_overlay (xine_video_port_t *this_gen, int overlay_enabled) {
  vos_t      *this = (vos_t *) this_gen;

  if (overlay_enabled) {
    /* we always ENable ... */
    this->overlay_enabled = 1;
  } else {
    /* ... but we only actually DISable, if all associated streams have SPU off */
    xine_list_iterator_t ite;

    pthread_mutex_lock(&this->streams_lock);
    for (ite = xine_list_front(this->streams); ite;
         ite = xine_list_next(this->streams, ite)) {
      xine_stream_t *stream = xine_list_get_value(this->streams, ite);
      if (stream == XINE_ANON_STREAM || stream->spu_channel_user > -2) {
	pthread_mutex_unlock(&this->streams_lock);
	return;
      }
    }
    pthread_mutex_unlock(&this->streams_lock);
    this->overlay_enabled = 0;
  }
}

/*
 * Flush video_out fifo
 */
static void vo_flush (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  vo_frame_t *img;

  if( this->video_loop_running ) {
    pthread_mutex_lock(&this->display_img_buf_queue->mutex);
    this->discard_frames++;
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

    /* do not try this in paused mode */
    while(this->clock->speed != XINE_SPEED_PAUSE) {
      pthread_mutex_lock(&this->display_img_buf_queue->mutex);
      img = this->display_img_buf_queue->first;
      pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
      if(!img)
        break;
      xine_usec_sleep (20000); /* pthread_cond_t could be used here */
    }

    pthread_mutex_lock(&this->display_img_buf_queue->mutex);
    this->discard_frames--;
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
  }
}

static void vo_trigger_drawing (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;

  pthread_mutex_lock (&this->trigger_drawing_mutex);
  this->trigger_drawing = 1;
  pthread_cond_signal (&this->trigger_drawing_cond);
  pthread_mutex_unlock (&this->trigger_drawing_mutex);
}

/* crop_frame() will allocate a new frame to copy in the given image
 * while cropping. maybe someday this will be an automatic post plugin.
 */
static vo_frame_t * crop_frame( xine_video_port_t *this_gen, vo_frame_t *img ) {

  vo_frame_t *dupl;

  dupl = vo_get_frame ( this_gen,
                        img->width - img->crop_left - img->crop_right,
                        img->height - img->crop_top - img->crop_bottom,
                        img->ratio, img->format, img->flags | VO_BOTH_FIELDS);

  dupl->progressive_frame  = img->progressive_frame;
  dupl->repeat_first_field = img->repeat_first_field;
  dupl->top_field_first    = img->top_field_first;
  dupl->overlay_offset_x   = img->overlay_offset_x;
  dupl->overlay_offset_y   = img->overlay_offset_y;

  switch (img->format) {
  case XINE_IMGFMT_YV12:
    yv12_to_yv12(
     /* Y */
      img->base[0] + img->crop_top * img->pitches[0] +
        img->crop_left, img->pitches[0],
      dupl->base[0], dupl->pitches[0],
     /* U */
      img->base[1] + img->crop_top/2 * img->pitches[1] +
        img->crop_left/2, img->pitches[1],
      dupl->base[1], dupl->pitches[1],
     /* V */
      img->base[2] + img->crop_top/2 * img->pitches[2] +
        img->crop_left/2, img->pitches[2],
      dupl->base[2], dupl->pitches[2],
     /* width x height */
      dupl->width, dupl->height);
    break;
  case XINE_IMGFMT_YUY2:
    yuy2_to_yuy2(
     /* src */
      img->base[0] + img->crop_top * img->pitches[0] +
        img->crop_left*2, img->pitches[0],
     /* dst */
      dupl->base[0], dupl->pitches[0],
     /* width x height */
      dupl->width, dupl->height);
    break;
  }

  dupl->bad_frame   = 0;
  dupl->pts         = img->pts;
  dupl->vpts        = img->vpts;
  dupl->proc_called = 0;

  dupl->duration  = img->duration;
  dupl->is_first  = img->is_first;

  dupl->stream    = img->stream;
  if (img->stream)
    _x_refcounter_inc(img->stream->refcounter);
  memcpy( dupl->extra_info, img->extra_info, sizeof(extra_info_t) );

  /* delay frame processing for now, we might not even need it (eg. frame will be discarded) */
  /* vo_frame_driver_proc(dupl); */

  return dupl;
}

xine_video_port_t *_x_vo_new_port (xine_t *xine, vo_driver_t *driver, int grabonly) {

  vos_t            *this;
  int               i;
  pthread_attr_t    pth_attrs;
  int		    err;
  int               num_frame_buffers;


  this = calloc(1, sizeof(vos_t)) ;

  this->xine                  = xine;
  this->clock                 = xine->clock;
  this->driver                = driver;
  this->streams               = xine_list_new();

  pthread_mutex_init(&this->streams_lock, NULL);
  pthread_mutex_init(&this->driver_lock, NULL );

  this->vo.open                  = vo_open;
  this->vo.get_frame             = vo_get_frame;
  this->vo.get_last_frame        = vo_get_last_frame;
  this->vo.new_grab_video_frame  = vo_new_grab_video_frame;
  this->vo.close                 = vo_close;
  this->vo.exit                  = vo_exit;
  this->vo.get_capabilities      = vo_get_capabilities;
  this->vo.enable_ovl            = vo_enable_overlay;
  this->vo.get_overlay_manager   = vo_get_overlay_manager;
  this->vo.flush                 = vo_flush;
  this->vo.trigger_drawing       = vo_trigger_drawing;
  this->vo.get_property          = vo_get_property;
  this->vo.set_property          = vo_set_property;
  this->vo.status                = vo_status;
  this->vo.driver                = driver;

  this->num_frames_delivered  = 0;
  this->num_frames_skipped    = 0;
  this->num_frames_discarded  = 0;
  this->free_img_buf_queue    = vo_new_img_buf_queue ();
  this->display_img_buf_queue = vo_new_img_buf_queue ();
  this->video_loop_running    = 0;

  this->img_backup            = NULL;

  this->last_frame            = NULL;
  this->pending_grab_request  = NULL;
  pthread_mutex_init(&this->grab_lock, NULL);
  pthread_cond_init(&this->grab_cond, NULL);

  this->overlay_source        = _x_video_overlay_new_manager(xine);
  this->overlay_source->init (this->overlay_source);
  this->overlay_enabled       = 1;


  /* default number of video frames from config */
  num_frame_buffers = xine->config->register_num (xine->config,
                                                  "engine.buffers.video_num_frames",
                                                  NUM_FRAME_BUFFERS, /* default */
                                                  _("default number of video frames"),
						  _("The default number of video frames to request "
						    "from xine video out driver. Some drivers will "
						    "override this setting with their own values."),
                                                    20, NULL, NULL);

  /* check driver's limit and use the smaller value */
  i = driver->get_property (driver, VO_PROP_MAX_NUM_FRAMES);
  if (i && i < num_frame_buffers)
    num_frame_buffers = i;

  /* we need at least 5 frames */
  if (num_frame_buffers<5)
    num_frame_buffers = 5;

  /* Choose a frame_drop_limit which matches num_frame_buffers.
   * xxmc for example supplies only 8 buffers. 2 are occupied by
   * MPEG2 decoding, further 2 for displaying and the remaining 4 can
   * hardly be filled all the time.
   * The below constants reserve buffers for decoding, displaying and
   * buffer fluctuation.
   * A frame_drop_limit_max below 1 will disable frame drops at all.
   */
  this->frame_drop_limit_max  = num_frame_buffers - 2 - 2 - 1;
  if (this->frame_drop_limit_max < 1)
    this->frame_drop_limit_max = 1;
  else if (this->frame_drop_limit_max > 3)
    this->frame_drop_limit_max = 3;

  this->frame_drop_limit      = this->frame_drop_limit_max;
  this->frame_drop_cpt        = 0;
  this->frame_drop_suggested  = 0;

  this->extra_info_base = calloc (num_frame_buffers,
					  sizeof(extra_info_t));

  for (i=0; i<num_frame_buffers; i++) {
    vo_frame_t *img;

    img = driver->alloc_frame (driver) ;
    if (!img) break;
    img->proc_duplicate_frame_data = NULL;

    img->id        = i;

    img->port      = &this->vo;
    img->free      = vo_frame_dec_lock;
    img->lock      = vo_frame_inc_lock;
    img->draw      = vo_frame_draw;

    img->extra_info = &this->extra_info_base[i];

    vo_append_to_img_buf_queue (this->free_img_buf_queue,
				img);
  }

  this->warn_skipped_threshold =
    xine->config->register_num (xine->config, "engine.performance.warn_skipped_threshold", 10,
    _("percentage of skipped frames to tolerate"),
    _("When more than this percentage of frames are not shown, because they "
      "were not decoded in time, xine sends a notification."),
    20, NULL, NULL);
  this->warn_discarded_threshold =
    xine->config->register_num (xine->config, "engine.performance.warn_discarded_threshold", 10,
    _("percentage of discarded frames to tolerate"),
    _("When more than this percentage of frames are not shown, because they "
      "were not scheduled for display in time, xine sends a notification."),
    20, NULL, NULL);

  pthread_mutex_init(&this->trigger_drawing_mutex, NULL);
  pthread_cond_init(&this->trigger_drawing_cond, NULL);
  this->trigger_drawing = 0;

  if (grabonly) {

    this->video_loop_running   = 0;
    this->video_opened         = 0;
    this->grab_only            = 1;

  } else {

    /*
     * start video output thread
     *
     * this thread will alwys be running, displaying the
     * logo when "idle" thus making it possible to have
     * osd when not playing a stream
     */

    this->video_loop_running   = 1;
    this->video_opened         = 0;
    this->grab_only            = 0;

    pthread_attr_init(&pth_attrs);
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    if ((err = pthread_create (&this->video_thread,
			       &pth_attrs, video_out_loop, this)) != 0) {

      xprintf (this->xine, XINE_VERBOSITY_NONE, "video_out: can't create thread (%s)\n", strerror(err));
      /* FIXME: how does this happen ? */
      xprintf (this->xine, XINE_VERBOSITY_LOG,
	       _("video_out: sorry, this should not happen. please restart xine.\n"));
      _x_abort();
    }
    else
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out: thread created\n");

    pthread_attr_destroy(&pth_attrs);
  }

  return &this->vo;
}
