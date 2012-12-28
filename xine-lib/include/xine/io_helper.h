/*
 * Copyright (C) 2000-2003 the xine project,
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
 * abortable i/o helper functions
 */

#ifndef IO_HELPER_H
#define IO_HELPER_H

#include "xine_internal.h"


/* select states */
#define XIO_READ_READY    1
#define XIO_WRITE_READY   2

/* xine select return codes */
#define XIO_READY         0
#define XIO_ERROR         1
#define XIO_ABORTED       2
#define XIO_TIMEOUT       3


/*
 * Waits for a file descriptor/socket to change status.
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * params :
 *   stream        needed for aborting and reporting errors but may be NULL
 *   fd            file/socket descriptor
 *   state         XIO_READ_READY, XIO_WRITE_READY
 *   timeout_sec   timeout in seconds
 *
 * An other thread can abort this function if stream != NULL by setting
 * stream->demux_action_pending.
 *
 * return value :
 *   XIO_READY     the file descriptor is ready for cmd
 *   XIO_ERROR     an i/o error occured
 *   XIO_ABORTED   command aborted by an other thread
 *   XIO_TIMEOUT   the file descriptor is not ready after timeout_msec milliseconds
 */
int _x_io_select (xine_stream_t *stream, int fd, int state, int timeout_msec) XINE_PROTECTED;


/*
 * open a tcp connection
 *
 * params :
 *   stream        needed for reporting errors but may be NULL
 *   host          address of target
 *   port          port on target
 *
 * returns a socket descriptor or -1 if an error occured
 */
int _x_io_tcp_connect(xine_stream_t *stream, const char *host, int port) XINE_PROTECTED;

/*
 * wait for finish connection
 *
 * params :
 *   stream        needed for aborting and reporting errors but may be NULL
 *   fd            socket descriptor
 *   timeout_msec  timeout in milliseconds
 *
 * return value:
 *   XIO_READY     host respond, the socket is ready for cmd
 *   XIO_ERROR     an i/o error occured
 *   XIO_ABORTED   command aborted by an other thread
 *   XIO_TIMEOUT   the file descriptor is not ready after timeout
 */
int _x_io_tcp_connect_finish(xine_stream_t *stream, int fd, int timeout_msec) XINE_PROTECTED;

/*
 * read from tcp socket checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t _x_io_tcp_read (xine_stream_t *stream, int s, void *buf, off_t todo) XINE_PROTECTED;


/*
 * write to a tcp socket checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t _x_io_tcp_write (xine_stream_t *stream, int s, void *buf, off_t todo) XINE_PROTECTED;

/*
 * read from a file descriptor checking demux_action_pending
 *
 * the fifo input plugin should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t _x_io_file_read (xine_stream_t *stream, int fd, void *buf, off_t todo) XINE_PROTECTED;


/*
 * write to a file descriptor checking demux_action_pending
 *
 * the fifo input plugin should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if *abort is set
 */
off_t _x_io_file_write (xine_stream_t *stream, int fd, void *buf, off_t todo) XINE_PROTECTED;

/*
 * read a string from socket, return string length (same as strlen)
 * the string is always '\0' terminated but given buffer size is never exceeded
 * that is, _x_io_tcp_read_line(,,,X) <= (X-1) ; X > 0
 */
int _x_io_tcp_read_line(xine_stream_t *stream, int sock, char *str, int size) XINE_PROTECTED;

#endif
