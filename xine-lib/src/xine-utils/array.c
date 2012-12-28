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
#include <string.h>
#include <xine/attributes.h>
#include <xine/array.h>

#define MIN_CHUNK_SIZE    32

/* Array internal struct */
struct xine_array_s {
  void    **chunk;
  size_t    chunk_size;
  size_t    size;
};

static void xine_array_ensure_chunk_size(xine_array_t *array, size_t size) {
  if (size > array->chunk_size) {
    /* realloc */
    size_t new_size = 2 * array->chunk_size;

    array->chunk = (void**)realloc(array->chunk, sizeof(void *) * new_size);
    array->chunk_size = new_size;
  }
}

/* Constructor */
xine_array_t *xine_array_new(size_t initial_size) {
  xine_array_t *new_array;

  new_array = (xine_array_t *)malloc(sizeof(xine_array_t));
  if (!new_array)
    return NULL;

  if (initial_size < MIN_CHUNK_SIZE)
    initial_size = MIN_CHUNK_SIZE;

  new_array->chunk = (void**)calloc(initial_size, sizeof(void*));
  if (!new_array->chunk) {
    free(new_array);
    return NULL;
  }
  new_array->chunk_size = initial_size;
  new_array->size = 0;

  return new_array;
}

/* Destructor */
void xine_array_delete(xine_array_t *array) {
  if (array->chunk) {
    free(array->chunk);
  }
  free(array);
}

size_t xine_array_size(const xine_array_t *array) {
  return array->size;
}

void xine_array_clear(xine_array_t *array) {
  array->size = 0;
}

void xine_array_add(xine_array_t *array, void *value) {
  xine_array_ensure_chunk_size(array, array->size + 1);
  array->chunk[array->size] = value;
  array->size++;
}

void xine_array_insert(xine_array_t *array, unsigned int position, void *value) {
  if (position < array->size) {
    xine_array_ensure_chunk_size(array, array->size + 1);
    memmove(&array->chunk[position + 1],
            &array->chunk[position],
            sizeof(void *) * (array->size - position));
    array->chunk[position] = value;
    array->size++;
  } else {
    xine_array_add(array, value);
  }
}

void xine_array_remove(xine_array_t *array, unsigned int position) {
  if (array->size > 0) {
    if (position < array->size) {
      memmove(&array->chunk[position],
              &array->chunk[position + 1],
              sizeof(void *) * (array->size - (position + 1)));
    }
    array->size--;
  }
}

void *xine_array_get(const xine_array_t *array, unsigned int position) {
  if (position < array->size)
    return array->chunk[position];
  else
    return NULL;
}

void xine_array_set(xine_array_t *array, unsigned int position, void *value) {
  if (position < array->size)
    array->chunk[position] = value;
}
