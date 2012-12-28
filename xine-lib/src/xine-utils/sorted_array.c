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
#include <xine/sorted_array.h>

/* Array internal struct */
struct xine_sarray_s {
  xine_array_t             *array;
  xine_sarray_comparator_t  comparator;
};

/* Constructor */
xine_sarray_t *xine_sarray_new(size_t initial_size, xine_sarray_comparator_t comparator) {
  xine_sarray_t *new_sarray;

  new_sarray = (xine_sarray_t *)malloc(sizeof(xine_sarray_t));
  if (!new_sarray)
    return NULL;

  new_sarray->array = xine_array_new(initial_size);
  new_sarray->comparator = comparator;

  return new_sarray;
}

/* Destructor */
void xine_sarray_delete(xine_sarray_t *sarray) {
  if (sarray->array) {
    xine_array_delete(sarray->array);
  }
  free(sarray);
}

size_t xine_sarray_size(const xine_sarray_t *sarray) {
  return xine_array_size(sarray->array);
}

void xine_sarray_clear(xine_sarray_t *sarray) {
  xine_array_clear(sarray->array);
}

int xine_sarray_add(xine_sarray_t *sarray, void *value) {
  int pos;

  pos = xine_sarray_binary_search(sarray, value);
  if (pos < 0)
    pos = ~pos;
  xine_array_insert(sarray->array, pos, value);

  return pos;
}

void xine_sarray_remove(xine_sarray_t *sarray, unsigned int position) {
  xine_array_remove(sarray->array, position);
}

void *xine_sarray_get(xine_sarray_t *sarray, unsigned int position) {
  return xine_array_get(sarray->array, position);
}

int xine_sarray_binary_search(xine_sarray_t *sarray, void *key) {
  int low, high, mid, pos;
  int comp;

  if (xine_array_size(sarray->array) > 0) {
    low = 0;
    high = xine_array_size(sarray->array) - 1;

    while ((high - low ) > 1) {
      mid = low + (high - low) / 2;
      if (sarray->comparator(key, xine_array_get(sarray->array, mid)) < 0) {
        high = mid;
      } else {
        low = mid;
      }
    }

    if ((comp = sarray->comparator(key, xine_array_get(sarray->array, low))) < 0) {
      pos = ~low;         /* not found */
    } else if (comp == 0) {
      pos = low;          /* found */
    } else if ((comp = sarray->comparator(key, xine_array_get(sarray->array, high))) < 0) {
      pos = ~high;        /* not found */
    } else if (comp == 0) {
      pos = high;         /* found */
    } else {
      pos = ~(high + 1);  /* not found */
    }
  } else {
    pos = ~0; /* not found */
  }
  return pos;
}
