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
 * Doubly-linked linked list.
 *
 * Exemples:
 *
 *   Create a list:
 *     xine_list_t *list = xine_list_new();
 *
 *   Delete a list:
 *     xine_list_delete(list);
 *
 *   Walk thru a list:
 *     xine_list_iterator_t ite = xine_list_front(list);
 *     while (ite) {
 *       _useful code here_
 *       ite = xine_list_next(list, ite);
 *     }
 *
 * The list elements are managed using memory chunks and a free list. The first
 * chunk contains 32 elements, each following chunk is two time as big as the
 * previous one, with a limit of 64K elements.
 */
#ifndef XINE_LIST_H
#define XINE_LIST_H

/* Doubly-linked list type */
typedef struct xine_list_s xine_list_t;

/* List iterator */
typedef void* xine_list_iterator_t;

/* Constructor */
xine_list_t *xine_list_new(void) XINE_MALLOC XINE_PROTECTED;

/* Destructor */
void xine_list_delete(xine_list_t *list) XINE_PROTECTED;

/* Returns the number of element stored in the list */
unsigned int xine_list_size(xine_list_t *list) XINE_PROTECTED;

/* Returns true if the number of elements is zero, false otherwise */
unsigned int xine_list_empty(xine_list_t *list) XINE_PROTECTED;

/* Adds the element at the beginning of the list */
void xine_list_push_front(xine_list_t *list, void *value) XINE_PROTECTED;

/* Adds the element at the end of the list */
void xine_list_push_back(xine_list_t *list, void *value) XINE_PROTECTED;

/* Remove all elements from a list */
void xine_list_clear(xine_list_t *list) XINE_PROTECTED;

/* Insert the element elem into the list at the position specified by the
   iterator (before the element, if any, that was previously at the iterator's
   position). The return value is an iterator that specifies the position of
   the inserted element. */
xine_list_iterator_t xine_list_insert(xine_list_t *list,
                                    xine_list_iterator_t position,
                                    void *value) XINE_PROTECTED;

/* Remove one element from a list.*/
void xine_list_remove(xine_list_t *list, xine_list_iterator_t position) XINE_PROTECTED;

/* Returns an iterator that references the first element of the list */
xine_list_iterator_t xine_list_front(xine_list_t *list) XINE_PROTECTED;

/* Returns an iterator that references the last element of the list */
xine_list_iterator_t xine_list_back(xine_list_t *list) XINE_PROTECTED;

/* Perform a linear search of a given value, and returns an iterator that
   references this value or NULL if not found */
xine_list_iterator_t xine_list_find(xine_list_t *list, void *value) XINE_PROTECTED;

/* Increments the iterator's value, so it specifies the next element in the list
   or NULL at the end of the list */
xine_list_iterator_t xine_list_next(xine_list_t *list, xine_list_iterator_t ite) XINE_PROTECTED;

/* Increments the iterator's value, so it specifies the previous element in the list
   or NULL at the beginning of the list */
xine_list_iterator_t xine_list_prev(xine_list_t *list, xine_list_iterator_t ite) XINE_PROTECTED;

/* Returns the value at the position specified by the iterator */
void *xine_list_get_value(xine_list_t *list, xine_list_iterator_t ite) XINE_PROTECTED;

#endif

