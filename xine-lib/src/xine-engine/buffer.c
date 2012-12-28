/*
 * Copyright (C) 2000-2004 the xine project
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
 *
 * contents:
 *
 * buffer_entry structure - serves as a transport encapsulation
 *   of the mpeg audio/video data through xine
 *
 * free buffer pool management routines
 *
 * FIFO buffer structures/routines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

/********** logging **********/
#define LOG_MODULE "buffer"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/xine_internal.h>

/*
 * put a previously allocated buffer element back into the buffer pool
 */
static void buffer_pool_free (buf_element_t *element) {

  fifo_buffer_t *this = (fifo_buffer_t *) element->source;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  element->next = this->buffer_pool_top;
  this->buffer_pool_top = element;

  this->buffer_pool_num_free++;
  if (this->buffer_pool_num_free > this->buffer_pool_capacity) {
    fprintf(stderr, _("xine-lib: buffer.c: There has been a fatal error: TOO MANY FREE's\n"));
    _x_abort();
  }

  pthread_cond_signal (&this->buffer_pool_cond_not_empty);

  pthread_mutex_unlock (&this->buffer_pool_mutex);
}

/*
 * allocate a buffer from buffer pool
 */

static buf_element_t *buffer_pool_alloc (fifo_buffer_t *this) {

  buf_element_t *buf;
  int i;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  for(i = 0; this->alloc_cb[i]; i++)
    this->alloc_cb[i](this, this->alloc_cb_data[i]);

  /* we always keep one free buffer for emergency situations like
   * decoder flushes that would need a buffer in buffer_pool_try_alloc() */
  while (this->buffer_pool_num_free < 2) {
    pthread_cond_wait (&this->buffer_pool_cond_not_empty, &this->buffer_pool_mutex);
  }

  buf = this->buffer_pool_top;
  this->buffer_pool_top = this->buffer_pool_top->next;
  this->buffer_pool_num_free--;

  pthread_mutex_unlock (&this->buffer_pool_mutex);

  /* set sane values to the newly allocated buffer */
  buf->content = buf->mem; /* 99% of demuxers will want this */
  buf->pts = 0;
  buf->size = 0;
  buf->decoder_flags = 0;
  memset(buf->decoder_info, 0, sizeof(buf->decoder_info));
  memset(buf->decoder_info_ptr, 0, sizeof(buf->decoder_info_ptr));
  _x_extra_info_reset( buf->extra_info );

  return buf;
}

/*
 * allocate a buffer from buffer pool - may fail if none is available
 */

static buf_element_t *buffer_pool_try_alloc (fifo_buffer_t *this) {

  buf_element_t *buf;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  if (this->buffer_pool_top) {

    buf = this->buffer_pool_top;
    this->buffer_pool_top = this->buffer_pool_top->next;
    this->buffer_pool_num_free--;

  } else {

    buf = NULL;

  }

  pthread_mutex_unlock (&this->buffer_pool_mutex);

  /* set sane values to the newly allocated buffer */
  if( buf ) {
    buf->content = buf->mem; /* 99% of demuxers will want this */
    buf->pts = 0;
    buf->size = 0;
    buf->decoder_flags = 0;
    memset(buf->decoder_info, 0, sizeof(buf->decoder_info));
    memset(buf->decoder_info_ptr, 0, sizeof(buf->decoder_info_ptr));
    _x_extra_info_reset( buf->extra_info );
  }
  return buf;
}


/*
 * append buffer element to fifo buffer
 */
static void fifo_buffer_put (fifo_buffer_t *fifo, buf_element_t *element) {
  int i;

  pthread_mutex_lock (&fifo->mutex);

  for(i = 0; fifo->put_cb[i]; i++)
    fifo->put_cb[i](fifo, element, fifo->put_cb_data[i]);

  if (fifo->last)
    fifo->last->next = element;
  else
    fifo->first = element;

  fifo->last = element;
  element->next = NULL;
  fifo->fifo_size++;
  fifo->fifo_data_size += element->size;

  pthread_cond_signal (&fifo->not_empty);

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * append buffer element to fifo buffer
 */
static void dummy_fifo_buffer_put (fifo_buffer_t *fifo, buf_element_t *element) {
  int i;

  pthread_mutex_lock (&fifo->mutex);

  for(i = 0; fifo->put_cb[i]; i++)
    fifo->put_cb[i](fifo, element, fifo->put_cb_data[i]);

  pthread_mutex_unlock (&fifo->mutex);

  element->free_buffer(element);
}

/*
 * insert buffer element to fifo buffer (demuxers MUST NOT call this one)
 */
static void fifo_buffer_insert (fifo_buffer_t *fifo, buf_element_t *element) {

  pthread_mutex_lock (&fifo->mutex);

  element->next = fifo->first;
  fifo->first = element;

  if( !fifo->last )
    fifo->last = element;

  fifo->fifo_size++;
  fifo->fifo_data_size += element->size;

  pthread_cond_signal (&fifo->not_empty);

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * insert buffer element to fifo buffer (demuxers MUST NOT call this one)
 */
static void dummy_fifo_buffer_insert (fifo_buffer_t *fifo, buf_element_t *element) {

  element->free_buffer(element);
}

/*
 * get element from fifo buffer
 */
static buf_element_t *fifo_buffer_get (fifo_buffer_t *fifo) {
  int i;
  buf_element_t *buf;

  pthread_mutex_lock (&fifo->mutex);

  while (fifo->first==NULL) {
    pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
  }

  buf = fifo->first;

  fifo->first = fifo->first->next;
  if (fifo->first==NULL)
    fifo->last = NULL;

  fifo->fifo_size--;
  fifo->fifo_data_size -= buf->size;

  for(i = 0; fifo->get_cb[i]; i++)
    fifo->get_cb[i](fifo, buf, fifo->get_cb_data[i]);

  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

/*
 * clear buffer (put all contained buffer elements back into buffer pool)
 */
static void fifo_buffer_clear (fifo_buffer_t *fifo) {

  buf_element_t *buf, *next, *prev;

  pthread_mutex_lock (&fifo->mutex);

  buf = fifo->first;
  prev = NULL;

  while (buf != NULL) {

    next = buf->next;

    if ((buf->type & BUF_MAJOR_MASK) !=  BUF_CONTROL_BASE) {
      /* remove this buffer */

      if (prev)
	prev->next = next;
      else
	fifo->first = next;

      if (!next)
	fifo->last = prev;

      fifo->fifo_size--;
      fifo->fifo_data_size -= buf->size;

      buf->free_buffer(buf);
    } else
      prev = buf;

    buf = next;
  }

  /* printf("Free buffers after clear: %d\n", fifo->buffer_pool_num_free); */
  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * Return the number of elements in the fifo buffer
 */
static int fifo_buffer_size (fifo_buffer_t *this) {
  int size;

  pthread_mutex_lock(&this->mutex);
  size = this->fifo_size;
  pthread_mutex_unlock(&this->mutex);

  return size;
}

/*
 * Return the amount of the data in the fifo buffer
 */
static uint32_t fifo_buffer_data_size (fifo_buffer_t *this) {
  uint32_t data_size;

  pthread_mutex_lock(&this->mutex);
  data_size = this->fifo_data_size;
  pthread_mutex_unlock(&this->mutex);

  return data_size;
}

/*
 * Return the number of free elements in the pool
 */
static int fifo_buffer_num_free (fifo_buffer_t *this) {
  int buffer_pool_num_free;

  pthread_mutex_lock(&this->mutex);
  buffer_pool_num_free = this->buffer_pool_num_free;
  pthread_mutex_unlock(&this->mutex);

  return buffer_pool_num_free;
}

/*
 * Destroy the buffer
 */
static void fifo_buffer_dispose (fifo_buffer_t *this) {

  buf_element_t *buf, *next;
  int received = 0;

  this->clear( this );
  buf = this->buffer_pool_top;

  while (buf != NULL) {

    next = buf->next;

    free (buf->extra_info);
    free (buf);
    received++;

    buf = next;
  }

  while (received < this->buffer_pool_capacity) {

    buf = this->get(this);

    free(buf->extra_info);
    free(buf);
    received++;
  }

  av_free (this->buffer_pool_base);
  pthread_mutex_destroy(&this->mutex);
  pthread_cond_destroy(&this->not_empty);
  pthread_mutex_destroy(&this->buffer_pool_mutex);
  pthread_cond_destroy(&this->buffer_pool_cond_not_empty);
  free (this);
}

/*
 * Register an "alloc" callback
 */
static void fifo_register_alloc_cb (fifo_buffer_t *this,
                                    void (*cb)(fifo_buffer_t *this,
                                               void *data_cb),
                                    void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->alloc_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->alloc_cb[i] = cb;
    this->alloc_cb_data[i] = data_cb;
    this->alloc_cb[i+1] = NULL;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Register a "put" callback
 */
static void fifo_register_put_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb),
                                  void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->put_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->put_cb[i] = cb;
    this->put_cb_data[i] = data_cb;
    this->put_cb[i+1] = NULL;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Register a "get" callback
 */
static void fifo_register_get_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb),
                                  void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->get_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->get_cb[i] = cb;
    this->get_cb_data[i] = data_cb;
    this->get_cb[i+1] = NULL;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister an "alloc" callback
 */
static void fifo_unregister_alloc_cb (fifo_buffer_t *this,
                                      void (*cb)(fifo_buffer_t *this,
                                                 void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->alloc_cb[i]; i++) {
    if( this->alloc_cb[i] == cb ) {
      for(j = i; this->alloc_cb[j]; j++) {
        this->alloc_cb[j] = this->alloc_cb[j+1];
        this->alloc_cb_data[j] = this->alloc_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister a "put" callback
 */
static void fifo_unregister_put_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->put_cb[i]; i++) {
    if( this->put_cb[i] == cb ) {
      for(j = i; this->put_cb[j]; j++) {
        this->put_cb[j] = this->put_cb[j+1];
        this->put_cb_data[j] = this->put_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister a "get" callback
 */
static void fifo_unregister_get_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->get_cb[i]; i++) {
    if( this->get_cb[i] == cb ) {
      for(j = i; this->get_cb[j]; j++) {
        this->get_cb[j] = this->get_cb[j+1];
        this->get_cb_data[j] = this->get_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
fifo_buffer_t *_x_fifo_buffer_new (int num_buffers, uint32_t buf_size) {

  fifo_buffer_t *this;
  int            i;
  unsigned char *multi_buffer = NULL;

  this = calloc(1, sizeof(fifo_buffer_t));

  this->first               = NULL;
  this->last                = NULL;
  this->fifo_size           = 0;
  this->put                 = fifo_buffer_put;
  this->insert              = fifo_buffer_insert;
  this->get                 = fifo_buffer_get;
  this->clear               = fifo_buffer_clear;
  this->size                = fifo_buffer_size;
  this->num_free            = fifo_buffer_num_free;
  this->data_size           = fifo_buffer_data_size;
  this->dispose             = fifo_buffer_dispose;
  this->register_alloc_cb   = fifo_register_alloc_cb;
  this->register_get_cb     = fifo_register_get_cb;
  this->register_put_cb     = fifo_register_put_cb;
  this->unregister_alloc_cb = fifo_unregister_alloc_cb;
  this->unregister_get_cb   = fifo_unregister_get_cb;
  this->unregister_put_cb   = fifo_unregister_put_cb;
  pthread_mutex_init (&this->mutex, NULL);
  pthread_cond_init (&this->not_empty, NULL);

  /*
   * init buffer pool, allocate nNumBuffers of buf_size bytes each
   */


  /*
  printf ("Allocating %d buffers of %ld bytes in one chunk\n",
	  num_buffers, (long int) buf_size);
	  */
  multi_buffer = this->buffer_pool_base = av_mallocz (num_buffers * buf_size);

  this->buffer_pool_top = NULL;

  pthread_mutex_init (&this->buffer_pool_mutex, NULL);
  pthread_cond_init (&this->buffer_pool_cond_not_empty, NULL);

  this->buffer_pool_num_free  = 0;
  this->buffer_pool_capacity  = num_buffers;
  this->buffer_pool_buf_size  = buf_size;
  this->buffer_pool_alloc     = buffer_pool_alloc;
  this->buffer_pool_try_alloc = buffer_pool_try_alloc;

  for (i = 0; i<num_buffers; i++) {
    buf_element_t *buf;

    buf = calloc(1, sizeof(buf_element_t));

    buf->mem = multi_buffer;
    multi_buffer += buf_size;

    buf->max_size    = buf_size;
    buf->free_buffer = buffer_pool_free;
    buf->source      = this;
    buf->extra_info  = malloc(sizeof(extra_info_t));

    buffer_pool_free (buf);
  }
  this->alloc_cb[0]              = NULL;
  this->get_cb[0]                = NULL;
  this->put_cb[0]                = NULL;
  this->alloc_cb_data[0]         = NULL;
  this->get_cb_data[0]           = NULL;
  this->put_cb_data[0]           = NULL;
  return this;
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
fifo_buffer_t *_x_dummy_fifo_buffer_new (int num_buffers, uint32_t buf_size) {

  fifo_buffer_t *this;

  this = _x_fifo_buffer_new(num_buffers, buf_size);
  this->put    = dummy_fifo_buffer_put;
  this->insert = dummy_fifo_buffer_insert;
  return this;
}
