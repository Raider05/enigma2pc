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

#include <stdlib.h>
#include <xine/attributes.h>
#include <xine/list.h>

#define MIN_CHUNK_SIZE    32
#define MAX_CHUNK_SIZE 65536

/* list element struct */
typedef struct xine_list_elem_s xine_list_elem_t;
struct xine_list_elem_s {
  xine_list_elem_t *prev;
  xine_list_elem_t *next;
  void            *value;
};

/* chunk of list elements */
typedef struct xine_list_chunk_s xine_list_chunk_t;
struct xine_list_chunk_s {
  xine_list_chunk_t *next_chunk;            /* singly linked list of chunks */

  xine_list_elem_t *elem_array;             /* the allocated elements */
  int chunk_size;                          /* element count in the chunk */
  int current_elem_id;                     /* next free elem in the chunk */
};

/* list struct */
struct xine_list_s {
  /* list of chunks */
  xine_list_chunk_t *chunk_list;
  size_t            chunk_list_size;
  xine_list_chunk_t *last_chunk;

  /* list elements */
  xine_list_elem_t  *elem_list_front;
  xine_list_elem_t  *elem_list_back;
  size_t            elem_list_size;

  /* list of free elements */
  xine_list_elem_t  *free_elem_list;
  size_t            free_elem_list_size;
};

/* Allocates a new chunk of n elements
 * One malloc call is used to allocate the struct and the elements.
 */
static xine_list_chunk_t *XINE_MALLOC xine_list_alloc_chunk(size_t size) {
  xine_list_chunk_t *new_chunk;
  size_t chunk_mem_size;

  chunk_mem_size  = sizeof(xine_list_chunk_t);
  chunk_mem_size += sizeof(xine_list_elem_t) * size;

  new_chunk = (xine_list_chunk_t *)malloc(chunk_mem_size);
  new_chunk->elem_array = (xine_list_elem_t*)(new_chunk + 1);
  new_chunk->next_chunk = NULL;
  new_chunk->current_elem_id = 0;
  new_chunk->chunk_size = size;

  return new_chunk;
}

/* Delete a chunk */
static void xine_list_delete_chunk(xine_list_chunk_t *chunk) {
  free(chunk);
}

/* Get a new element either from the free list either from the current chunk.
   Allocate a new chunk if needed */
static xine_list_elem_t *xine_list_alloc_elem(xine_list_t *list) {
  xine_list_elem_t *new_elem;

  /* check the free list */
  if (list->free_elem_list_size > 0) {
    new_elem = list->free_elem_list;
    list->free_elem_list = list->free_elem_list->next;
    list->free_elem_list_size--;
  } else {
    /* check current chunk */
    if (list->last_chunk->current_elem_id < list->last_chunk->chunk_size) {
      /* take the next elem in the chunk */
      new_elem = &list->last_chunk->elem_array[list->last_chunk->current_elem_id];
      list->last_chunk->current_elem_id++;
    } else {
      /* a new chunk is needed */
      xine_list_chunk_t *new_chunk;
      int chunk_size;

      chunk_size = list->last_chunk->chunk_size * 2;
      if (chunk_size > MAX_CHUNK_SIZE)
        chunk_size = MAX_CHUNK_SIZE;

      new_chunk = xine_list_alloc_chunk(chunk_size);

      list->last_chunk->next_chunk = new_chunk;
      list->last_chunk = new_chunk;
      list->chunk_list_size++;

      new_elem = &new_chunk->elem_array[0];
      new_chunk->current_elem_id++;
    }
  }
  return new_elem;
}

/* Push the elem into the free list */
static void xine_list_recycle_elem(xine_list_t *list,  xine_list_elem_t *elem) {
  elem->next = list->free_elem_list;
  elem->prev = NULL;

  list->free_elem_list = elem;
  list->free_elem_list_size++;
}

/* List constructor */
xine_list_t *xine_list_new(void) {
  xine_list_t *new_list;

  new_list = (xine_list_t*)malloc(sizeof(xine_list_t));
  new_list->chunk_list = xine_list_alloc_chunk(MIN_CHUNK_SIZE);
  new_list->chunk_list_size = 1;
  new_list->last_chunk = new_list->chunk_list;
  new_list->free_elem_list = NULL;
  new_list->free_elem_list_size = 0;
  new_list->elem_list_front = NULL;
  new_list->elem_list_back = NULL;
  new_list->elem_list_size = 0;

  return new_list;
}

void xine_list_delete(xine_list_t *list) {
  /* Delete each chunk */
  xine_list_chunk_t *current_chunk = list->chunk_list;

  while (current_chunk) {
    xine_list_chunk_t *next_chunk = current_chunk->next_chunk;

    xine_list_delete_chunk(current_chunk);
    current_chunk = next_chunk;
  }
  free(list);
}

unsigned int xine_list_size(xine_list_t *list) {
  return list->elem_list_size;
}

unsigned int xine_list_empty(xine_list_t *list) {
  return (list->elem_list_size == 0);
}

xine_list_iterator_t xine_list_front(xine_list_t *list) {
  return list->elem_list_front;
}

xine_list_iterator_t xine_list_back(xine_list_t *list) {
  return list->elem_list_back;
}

void xine_list_push_back(xine_list_t *list, void *value) {
  xine_list_elem_t *new_elem;

  new_elem = xine_list_alloc_elem(list);
  new_elem->value = value;

  if (list->elem_list_back) {
    new_elem->next = NULL;
    new_elem->prev = list->elem_list_back;
    list->elem_list_back->next = new_elem;
    list->elem_list_back = new_elem;
  } else {
    /* first elem in the list */
    list->elem_list_front = list->elem_list_back = new_elem;
    new_elem->next = NULL;
    new_elem->prev = NULL;
  }
  list->elem_list_size++;
}

void xine_list_push_front(xine_list_t *list, void *value) {
  xine_list_elem_t *new_elem;

  new_elem = xine_list_alloc_elem(list);
  new_elem->value = value;

  if (list->elem_list_front) {
    new_elem->next = list->elem_list_front;
    new_elem->prev = NULL;
    list->elem_list_front->prev = new_elem;
    list->elem_list_front = new_elem;
  } else {
    /* first elem in the list */
    list->elem_list_front = list->elem_list_back = new_elem;
    new_elem->next = NULL;
    new_elem->prev = NULL;
  }
  list->elem_list_size++;
}

void xine_list_clear(xine_list_t *list) {
  xine_list_elem_t *elem = list->elem_list_front;
  while (elem) {
    xine_list_elem_t *elem_next = elem->next;
    xine_list_recycle_elem(list, elem);
    elem = elem_next;
  }

  list->elem_list_front = NULL;
  list->elem_list_back = NULL;
  list->elem_list_size = 0;
}

xine_list_iterator_t xine_list_next(xine_list_t *list, xine_list_iterator_t ite) {
  xine_list_elem_t *elem = (xine_list_elem_t*)ite;

  if (ite == NULL)
    return list->elem_list_front;
  else
    return (xine_list_iterator_t)elem->next;
}

xine_list_iterator_t xine_list_prev(xine_list_t *list, xine_list_iterator_t ite) {
  xine_list_elem_t *elem = (xine_list_elem_t*)ite;

  if (ite == NULL)
    return list->elem_list_back;
  else
    return (xine_list_iterator_t)elem->prev;
}

void *xine_list_get_value(xine_list_t *list, xine_list_iterator_t ite) {
  xine_list_elem_t *elem = (xine_list_elem_t*)ite;

  return elem->value;
}

void xine_list_remove(xine_list_t *list, xine_list_iterator_t position) {
  xine_list_elem_t *elem = (xine_list_elem_t*)position;

  if (elem) {
    xine_list_elem_t *prev = elem->prev;
    xine_list_elem_t *next = elem->next;

    if (prev)
      prev->next = next;
    else
      list->elem_list_front = next;

    if (next)
      next->prev = prev;
    else
      list->elem_list_back = prev;

    xine_list_recycle_elem(list, elem);
    list->elem_list_size--;
  }
}

xine_list_iterator_t xine_list_insert(xine_list_t *list,
                                    xine_list_iterator_t position,
                                    void *value) {
  xine_list_elem_t *elem = (xine_list_elem_t*)position;
  xine_list_iterator_t new_position = NULL;

  if (elem == NULL) {
    /* insert at the end */
    xine_list_push_back(list, value);
    new_position = list->elem_list_back;
  } else {
    if (elem->prev == NULL) {
      /* insert at the beginning */
      xine_list_push_front(list, value);
      new_position = list->elem_list_front;
    } else {
      xine_list_elem_t *new_elem = xine_list_alloc_elem(list);
      xine_list_elem_t *prev = elem->prev;

      new_elem->next = elem;
      new_elem->prev = prev;
      new_elem->value = value;

      elem->prev = new_elem;
      prev->next = new_elem;

      new_position = (xine_list_iterator_t)elem;
    }
  }
  return new_position;
}

xine_list_iterator_t xine_list_find(xine_list_t *list, void *value) {

  xine_list_elem_t *elem;

  for (elem = list->elem_list_front; elem; elem = elem->next) {
    if (elem->value == value)
      break;
  }
  return elem;
}
