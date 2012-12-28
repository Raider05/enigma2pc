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

#include <assert.h>
#include <xine/attributes.h>
#include <xine/pool.h>
#include <xine/array.h>

#define MIN_CHUNK_SIZE    32
#define MAX_CHUNK_SIZE 65536

/* chunk of objects */
typedef struct xine_pool_chunk_s xine_pool_chunk_t;
struct xine_pool_chunk_s {
  void              *mem_base;     /* the allocated elements */
  int                count;        /* object count in the chunk */
  int                current_id;   /* next free object in the chunk */
};

struct xine_pool_s {
  size_t object_size;

  /* callbacks */
  void (*create_object)(void *object);
  void (*prepare_object)(void *object);
  void (*return_object)(void *object);
  void (*delete_object)(void *object);

  /* chunks */
  xine_array_t *chunk_list;
  xine_array_t *free_list;

};

/* Allocates a new chunk of n elements
 * One malloc call is used to allocate the struct and the elements.
 */
static xine_pool_chunk_t *XINE_MALLOC xine_pool_alloc_chunk(size_t object_size, size_t object_count) {
  xine_pool_chunk_t *new_chunk;
  size_t chunk_mem_size;;

  assert(object_size > 0);
  assert(object_count > 0);
  chunk_mem_size  = sizeof(xine_pool_chunk_t);
  chunk_mem_size += object_size * object_count;

  new_chunk = (xine_pool_chunk_t *)malloc(chunk_mem_size);
  new_chunk->mem_base = (xine_pool_chunk_t*)(new_chunk + 1);
  new_chunk->current_id = 0;
  new_chunk->count = object_count;

  return new_chunk;
}

/* Delete a chunk */
static void xine_pool_delete_chunk(xine_pool_chunk_t *chunk) {
  assert(chunk);
  free(chunk);
}

xine_pool_t *xine_pool_new(size_t object_size,
                           void (*create_object)(void *object),
                           void (*prepare_object)(void *object),
                           void (*return_object)(void *object),
                           void (*delete_object)(void *object)) {
  xine_pool_t *new_pool;
  assert(object_size > 0);

  new_pool = malloc(sizeof(xine_pool_t));
  new_pool->object_size = object_size;
  new_pool->create_object = create_object;
  new_pool->prepare_object = prepare_object;
  new_pool->return_object = return_object;
  new_pool->delete_object = delete_object;
  new_pool->chunk_list = xine_array_new(0);
  new_pool->free_list = xine_array_new(MIN_CHUNK_SIZE);

  xine_array_add(new_pool->chunk_list,
                 xine_pool_alloc_chunk (object_size, MIN_CHUNK_SIZE));
  return new_pool;
}

void xine_pool_delete(xine_pool_t *pool) {
  int list_id, list_size;

  assert(pool);
  list_size = xine_array_size(pool->chunk_list);

  for (list_id = 0; list_id < list_size; list_id++) {
    xine_pool_chunk_t *chunk = xine_array_get(pool->chunk_list, list_id);

    /* delete each created object */
    if (pool->delete_object) {
      int i;

      for (i = 0; i < chunk->current_id; i++) {
        void *object = ((uint8_t*)(chunk->mem_base)) + i * pool->object_size;
        pool->delete_object(object);
      }
    }
    xine_pool_delete_chunk(chunk);
  }
  free (pool);
}

void *xine_pool_get(xine_pool_t *pool) {
  void *object = NULL;
  int free_count;

  assert(pool);

  /* check the free list */
  free_count = xine_array_size(pool->free_list);
  if (free_count > 0) {
    object = xine_array_get(pool->free_list, free_count - 1);
    xine_array_remove(pool->free_list, free_count - 1);
  } else {
    /* check the current chunk */
    int chunk_count = xine_array_size(pool->chunk_list);
    xine_pool_chunk_t *current_chunk = xine_array_get(pool->chunk_list, chunk_count - 1);

    if (current_chunk->current_id < current_chunk->count) {
      /* take the next entry of the chunk */
      object = ((uint8_t*)(current_chunk->mem_base)) +
               current_chunk->current_id * pool->object_size;
      current_chunk->current_id++;
    } else {
      /* create a new chunk */
      xine_pool_chunk_t *new_chunk;
      int new_chunk_count = current_chunk->count * 2;
      if (new_chunk_count > MAX_CHUNK_SIZE) {
        new_chunk_count = MAX_CHUNK_SIZE;
      }
      new_chunk = xine_pool_alloc_chunk (pool->object_size, new_chunk_count);
      xine_array_add(pool->chunk_list, new_chunk);
      object = new_chunk->mem_base;
      new_chunk->current_id = 1;
    }
    if (pool->create_object) {
      pool->create_object(object);
    }
  }

  if (pool->prepare_object) {
    pool->prepare_object(object);
  }
  return object;
}

void xine_pool_put(xine_pool_t *pool, void *object) {
  assert(pool);
  assert(object);
  if (pool->return_object) {
    pool->return_object(object);
  }
  xine_array_add(pool->free_list, object);
}
