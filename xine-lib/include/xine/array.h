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
 *
 * Array that can grow automatically when you add elements.
 * Inserting an element in the middle of the array implies memory moves.
 */
#ifndef XINE_ARRAY_H
#define XINE_ARRAY_H

/* Array type */
typedef struct xine_array_s xine_array_t;

/* Constructor */
xine_array_t *xine_array_new(size_t initial_size) XINE_MALLOC XINE_PROTECTED;

/* Destructor */
void xine_array_delete(xine_array_t *array) XINE_PROTECTED;

/* Returns the number of element stored in the array */
size_t xine_array_size(const xine_array_t *array) XINE_PROTECTED;

/* Removes all elements from an array */
void xine_array_clear(xine_array_t *array) XINE_PROTECTED;

/* Adds the element at the end of the array */
void xine_array_add(xine_array_t *array, void *value) XINE_PROTECTED;

/* Inserts an element into an array at the position specified */
void xine_array_insert(xine_array_t *array, unsigned int position, void *value) XINE_PROTECTED;

/* Removes one element from an array at the position specified */
void xine_array_remove(xine_array_t *array, unsigned int position) XINE_PROTECTED;

/* Get the element at the position specified */
void *xine_array_get(const xine_array_t *array, unsigned int position) XINE_PROTECTED;

/* Set the element at the position specified */
void xine_array_set(xine_array_t *array, unsigned int position, void *value) XINE_PROTECTED;

#endif

