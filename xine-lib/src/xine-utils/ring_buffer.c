/*
 * Copyright (C) 2000-2006 the xine project
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

#include <pthread.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <xine/attributes.h>
#include <xine/pool.h>
#include <xine/list.h>
#include <xine/ring_buffer.h>

#define RING_BUFFER_EXTRA_BUFFER_SIZE (1024 * 8)

/* internal memory chunk struct */
typedef struct xine_ring_buffer_chunk_s xine_ring_buffer_chunk_t;
struct xine_ring_buffer_chunk_s {
  uint8_t *mem;
  size_t   size;
};

/* init */
static void xine_ring_buffer_chunk_create(void *object) {
  xine_ring_buffer_chunk_t *chunk = (xine_ring_buffer_chunk_t *)object;
  chunk->mem = NULL;
  chunk->size = 0;
}

/* cleanup */
static void xine_ring_buffer_chunk_return(void *object) {
  xine_ring_buffer_chunk_t *chunk = (xine_ring_buffer_chunk_t *)object;
  chunk->mem = NULL;
  chunk->size = 0;
}

/* ring buffer internal struct */
struct xine_ring_buffer_s {
  uint8_t        *head;
  uint8_t        *head_alloc;
  uint8_t        *tail;
  uint8_t        *tail_release;

  uint8_t        *buffer;
  size_t          buffer_size;       /* size of the allocated buffer */
  uint8_t        *buffer_end;

  size_t          free_size;         /* size of the free zone */
  size_t          full_size;         /* size of the full zone */

  pthread_cond_t  free_size_cond;
  pthread_cond_t  full_size_cond;

  int             free_size_needed;
  int             full_size_needed;

  xine_pool_t    *chunk_pool;
  xine_list_t    *alloc_list;
  xine_list_t    *get_list;

  uint8_t        *extra_buffer;
  size_t          extra_buffer_size;

  pthread_mutex_t lock;

  int             EOS;
};

/* Constructor */
xine_ring_buffer_t *xine_ring_buffer_new(size_t size) {
  xine_ring_buffer_t *new_ring_buffer = malloc(sizeof(xine_ring_buffer_t));

  new_ring_buffer->buffer = malloc(size);
  new_ring_buffer->buffer_size = size;

  new_ring_buffer->alloc_list = xine_list_new();
  new_ring_buffer->get_list = xine_list_new();

  new_ring_buffer->chunk_pool = xine_pool_new(sizeof(xine_ring_buffer_chunk_t),
                                              xine_ring_buffer_chunk_create,
                                              NULL,
                                              xine_ring_buffer_chunk_return,
                                              NULL);

  new_ring_buffer->head         = new_ring_buffer->buffer;
  new_ring_buffer->head_alloc   = new_ring_buffer->buffer;
  new_ring_buffer->tail         = new_ring_buffer->buffer;
  new_ring_buffer->tail_release = new_ring_buffer->buffer;

  new_ring_buffer->free_size = size;
  pthread_cond_init(&new_ring_buffer->free_size_cond, NULL);
  new_ring_buffer->free_size_needed = 0;
  new_ring_buffer->full_size = 0;
  pthread_cond_init(&new_ring_buffer->full_size_cond, NULL);
  new_ring_buffer->full_size_needed = 0;

  pthread_mutex_init(&new_ring_buffer->lock, NULL);

  new_ring_buffer->buffer_end = new_ring_buffer->buffer + size;

  new_ring_buffer->extra_buffer = malloc(RING_BUFFER_EXTRA_BUFFER_SIZE);
  new_ring_buffer->extra_buffer_size = RING_BUFFER_EXTRA_BUFFER_SIZE;

  new_ring_buffer->EOS = 0;

  return new_ring_buffer;
}

/* Destructor */
void xine_ring_buffer_delete(xine_ring_buffer_t *ring_buffer) {
  xine_list_delete(ring_buffer->alloc_list);
  xine_list_delete(ring_buffer->get_list);
  xine_pool_delete(ring_buffer->chunk_pool);
  pthread_mutex_destroy(&ring_buffer->lock);
  free (ring_buffer->buffer);
  free (ring_buffer);
}

static void xine_ring_buffer_display_stat(const xine_ring_buffer_t *ring_buffer) {
#if DEBUG_RING_BUFFER
  size_t free_size, full_size;

  printf("alloc: free_size1=%d full_size1=%d\n",
         ring_buffer->free_size, ring_buffer->full_size);
  if (ring_buffer->tail_release > ring_buffer->head_alloc) {
    free_size = ring_buffer->tail_release-ring_buffer->head_alloc;
  } else {
    free_size = ring_buffer->tail_release +
                (ring_buffer->buffer_end - ring_buffer->buffer) -
                ring_buffer->head_alloc;
  }
  if (ring_buffer->head > ring_buffer->tail) {
    full_size = ring_buffer->head - ring_buffer->tail;
  } else {
    full_size = ring_buffer->head +
                (ring_buffer->buffer_end - ring_buffer->buffer) -
                ring_buffer->tail;
  }
  printf("alloc: free_size2=%d full_size2=%d\n", free_size, full_size);
  printf("alloc: head_alloc=%d, head=%d, tail=%d, tail_release=%d\n",
         ring_buffer->head_alloc - ring_buffer->buffer,
         ring_buffer->head - ring_buffer->buffer,
         ring_buffer->tail_release - ring_buffer->buffer,
         ring_buffer->tail - ring_buffer->buffer);
#endif
}

void *xine_ring_buffer_alloc(xine_ring_buffer_t *ring_buffer, size_t size) {
  int ok = 0;
  xine_ring_buffer_chunk_t *chunk;

  assert(ring_buffer);

  pthread_mutex_lock(&ring_buffer->lock);

  xine_ring_buffer_display_stat(ring_buffer);

  do {
    while (size > ring_buffer->free_size) {
      /* we need more free room */
      ring_buffer->free_size_needed++;
      pthread_cond_wait (&ring_buffer->free_size_cond, &ring_buffer->lock);
      ring_buffer->free_size_needed--;
    }

    if ((ring_buffer->head_alloc + size)  >
        (ring_buffer->buffer + ring_buffer->buffer_size)) {
      /* we can't get a continuous buffer of this size from the ring buffer,
         define the end of the buffer here and go back to the beginning */
      ring_buffer->buffer_end = ring_buffer->head_alloc;
      ring_buffer->head_alloc = ring_buffer->buffer;
      ring_buffer->free_size -= (ring_buffer->buffer + ring_buffer->buffer_size) -
                                ring_buffer->buffer_end;
    } else {
      ok = 1;
    }
  } while (!ok);

  /* create a new chunk and add it to the allocated list */
  chunk = xine_pool_get(ring_buffer->chunk_pool);
  chunk->mem = ring_buffer->head_alloc;
  chunk->size = size;
  xine_list_push_back(ring_buffer->alloc_list, chunk);

  ring_buffer->head_alloc += size;
  ring_buffer->free_size  -= size;

  pthread_mutex_unlock(&ring_buffer->lock);
  return chunk->mem;
}

void xine_ring_buffer_put(xine_ring_buffer_t *ring_buffer, void *buffer) {
  xine_list_iterator_t ite;
  xine_ring_buffer_chunk_t *chunk = NULL;
  xine_ring_buffer_chunk_t *prev_chunk = NULL;

  /* at this point, the chunk contains data */
  /* find the chunk in the allocated list */
  pthread_mutex_lock(&ring_buffer->lock);
  for (ite = xine_list_front(ring_buffer->alloc_list);
       ite;
       ite = xine_list_next(ring_buffer->alloc_list, ite)) {
    chunk = xine_list_get_value(ring_buffer->alloc_list, ite);
    if (chunk->mem == buffer)
      break;
    prev_chunk = chunk;
  }
  assert(ite);
  assert(chunk);
  /* found associated chunk, is it the first in the list ? */
  if (!prev_chunk) {
    if (ring_buffer->head == ring_buffer->buffer_end) {
      ring_buffer->head = ring_buffer->buffer;
    }
    /* increment ring_buffer head, and full_size */
    ring_buffer->head += chunk->size;
    ring_buffer->full_size += chunk->size;

    /* notify */
    if (ring_buffer->full_size_needed) {
      pthread_cond_broadcast(&ring_buffer->full_size_cond);
    }
  } else {
    /* merge with previous chunk */
    prev_chunk->size += chunk->size;
  }
  xine_list_remove(ring_buffer->alloc_list, ite);
  xine_pool_put(ring_buffer->chunk_pool, chunk);

  pthread_mutex_unlock(&ring_buffer->lock);
}

void *xine_ring_buffer_get(xine_ring_buffer_t *ring_buffer, size_t size, size_t *rsize) {
  xine_ring_buffer_chunk_t *chunk;
  size_t continuous_size;
  uint8_t *mem_chunk;

  assert(ring_buffer);
  assert(rsize);
  pthread_mutex_lock(&ring_buffer->lock);
  while ((size > ring_buffer->full_size) && !ring_buffer->EOS) {
    /* we need more free room */
    ring_buffer->full_size_needed++;
    pthread_cond_wait (&ring_buffer->full_size_cond, &ring_buffer->lock);
    ring_buffer->full_size_needed--;
  }

  if (size > ring_buffer->full_size) {
    size = ring_buffer->full_size;
  }

  continuous_size = ring_buffer->buffer_end - ring_buffer->tail;
  if (size > continuous_size) {

    /* we can't get a continuous buffer of this size from the ring buffer,
       we accumulate the ringbuffer content into the extra buffer */
    if (ring_buffer->extra_buffer_size < size) {
      ring_buffer->extra_buffer = realloc(ring_buffer->extra_buffer, size);
      ring_buffer->extra_buffer_size = size;
    }
    memcpy(ring_buffer->extra_buffer,
           ring_buffer->tail,
           continuous_size);
    memcpy(ring_buffer->extra_buffer + continuous_size,
           ring_buffer->buffer,
           size - continuous_size);
    mem_chunk = ring_buffer->extra_buffer;
    ring_buffer->tail = ring_buffer->buffer + size - continuous_size;
  } else {
    mem_chunk = ring_buffer->tail;
    ring_buffer->tail += size;
  }

  /* create a new chunk and add it to the allocated list */
  chunk = xine_pool_get(ring_buffer->chunk_pool);
  chunk->mem = mem_chunk;
  chunk->size = size;
  xine_list_push_back(ring_buffer->get_list, chunk);

  *rsize = size;
  ring_buffer->full_size -= size;

  pthread_mutex_unlock(&ring_buffer->lock);
  return chunk->mem;
}

void xine_ring_buffer_release(xine_ring_buffer_t *ring_buffer, void *buffer) {
  xine_list_iterator_t ite;
  xine_ring_buffer_chunk_t *chunk = NULL;
  xine_ring_buffer_chunk_t *prev_chunk = NULL;

  /* the chunk can be reused */
  /* find the chunk in the get list */
  pthread_mutex_lock(&ring_buffer->lock);
  for (ite = xine_list_front(ring_buffer->get_list);
       ite;
       ite = xine_list_next(ring_buffer->get_list, ite)) {
    chunk = xine_list_get_value(ring_buffer->get_list, ite);
    if (chunk->mem == buffer)
      break;
    prev_chunk = chunk;
  }
  assert(ite);
  assert(chunk);
  /* found associated chunk, is it the first in the list ? */
  if (!prev_chunk) {
    size_t continuous_size;

    /* increment ringbuffer tail_release and free_size */

    continuous_size = ring_buffer->buffer_end - ring_buffer->tail_release;
    if (chunk->size > continuous_size) {
      ring_buffer->tail_release = ring_buffer->buffer + chunk->size - continuous_size;
      /* place the buffer_end back to the end */
      ring_buffer->free_size +=
        (ring_buffer->buffer + ring_buffer->buffer_size) - ring_buffer->buffer_end;
      ring_buffer->buffer_end = ring_buffer->buffer + ring_buffer->buffer_size;
    } else {
      ring_buffer->tail_release += chunk->size;
    }
    ring_buffer->free_size += chunk->size;

    /* notify */
    if (ring_buffer->free_size_needed) {
      pthread_cond_broadcast(&ring_buffer->free_size_cond);
    }
  } else {
    /* merge with previous chunk */
    prev_chunk->size += chunk->size;
  }
  xine_list_remove(ring_buffer->get_list, ite);
  xine_pool_put(ring_buffer->chunk_pool, chunk);

  pthread_mutex_unlock(&ring_buffer->lock);
}

void xine_ring_buffer_close(xine_ring_buffer_t *ring_buffer) {
  pthread_mutex_lock(&ring_buffer->lock);

  ring_buffer->EOS = 1;

  /* notify */
  if (ring_buffer->full_size_needed) {
    pthread_cond_broadcast(&ring_buffer->full_size_cond);
  }
  pthread_mutex_unlock(&ring_buffer->lock);
}
