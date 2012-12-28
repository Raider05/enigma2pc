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
 * Fifo + Ring Buffer
 */
typedef struct xine_ring_buffer_s xine_ring_buffer_t;

/* Creates a new ring buffer */
xine_ring_buffer_t *xine_ring_buffer_new(size_t size) XINE_MALLOC XINE_PROTECTED;

/* Deletes a ring buffer */
void xine_ring_buffer_delete(xine_ring_buffer_t *ring_buffer) XINE_PROTECTED;

/* Returns a new chunk of the specified size */
/* Might block if the ring buffer is full */
void *xine_ring_buffer_alloc(xine_ring_buffer_t *ring_buffer, size_t size) XINE_PROTECTED;

/* Put a chunk into the ring */
void xine_ring_buffer_put(xine_ring_buffer_t *ring_buffer, void *chunk) XINE_PROTECTED;

/* Get a chunk of a specified size from the ring buffer
 * Might block if the ring buffer is empty
 * param size: the desired size
 * param rsize: the size of the chunk returned
 * rsize is not equal to size at the end of stream, the caller MUST check
 * rsize value.
 */
void *xine_ring_buffer_get(xine_ring_buffer_t *ring_buffer, size_t size, size_t *rsize) XINE_PROTECTED;

/* Releases the chunk, makes memory available for the alloc function */
void xine_ring_buffer_release(xine_ring_buffer_t *ring_buffer, void *chunk) XINE_PROTECTED;

/* Closes the ring buffer
 * The writer uses this function to signal the end of stream to the reader.
 * The reader MUST check the rsize value returned by the get function.
 */
void xine_ring_buffer_close(xine_ring_buffer_t *ring_buffer) XINE_PROTECTED;


