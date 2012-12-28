/*
 * Copyright (C) 2000-2006 the xine project,
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define XINE_ENGINE_INTERNAL

#include <xine/io_helper.h>

/* private constants */
#define XIO_FILE_READ             0
#define XIO_FILE_WRITE            1
#define XIO_TCP_READ              2
#define XIO_TCP_WRITE             3
#define XIO_POLLING_INTERVAL  50000  /* usec */


#ifndef ENABLE_IPV6
static int _x_io_tcp_connect_ipv4(xine_stream_t *stream, const char *host, int port) {

  struct hostent *h;
  int             i, s;

  h = gethostbyname(host);
  if (h == NULL) {
    _x_message(stream, XINE_MSG_UNKNOWN_HOST, "unable to resolve", host, NULL);
    return -1;
  }

  s = xine_socket_cloexec(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == -1) {
    _x_message(stream, XINE_MSG_CONNECTION_REFUSED, "failed to create socket", strerror(errno), NULL);
    return -1;
  }

#ifndef WIN32
  if (fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK) == -1) {
    _x_message(stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", strerror(errno), NULL);
    return -1;
  }
#else
  {
	unsigned long non_block = 1;
	int rc;

    rc = ioctlsocket(s, FIONBIO, &non_block);

    if (rc == SOCKET_ERROR) {
      _x_message(stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", strerror(errno), NULL);
	  return -1;
    }
  }
#endif

  for (i = 0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    union {
      struct sockaddr    sa;
      struct sockaddr_in in;
    } saddr;
    memcpy (&ia, h->h_addr_list[i], 4);
    saddr.in.sin_family = AF_INET;
    saddr.in.sin_addr   = ia;
    saddr.in.sin_port   = htons(port);

#ifndef WIN32
    if (connect(s, &saddr.sa, sizeof(saddr.in))==-1 && errno != EINPROGRESS) {
#else
    if (connect(s, &saddr.sa, sizeof(saddr.in))==-1 && WSAGetLastError() != WSAEWOULDBLOCK) {
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: WSAGetLastError() = %d\n", WSAGetLastError());
#endif /* WIN32 */

      _x_message(stream, XINE_MSG_CONNECTION_REFUSED, strerror(errno), NULL);
      close(s);
      continue;
    }

    return s;
  }
  return -1;
}
#endif

int _x_io_tcp_connect(xine_stream_t *stream, const char *host, int port) {

#ifndef ENABLE_IPV6
    return _x_io_tcp_connect_ipv4(stream, host, port);
#else
  int             s;
  struct addrinfo hints, *res, *tmpaddr;
  int error;
  char strport[16];

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = PF_UNSPEC;

  snprintf(strport, sizeof(strport), "%d", port);

  if (stream)
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "Resolving host '%s' at port '%s'\n", host, strport);

  error = getaddrinfo(host, strport, &hints, &res);

  if (error) {
    _x_message(stream, XINE_MSG_UNKNOWN_HOST,
		 "unable to resolve", host, NULL);
    return -1;
  }

  tmpaddr = res;

  while (tmpaddr) {

      s = xine_socket_cloexec(tmpaddr->ai_family, SOCK_STREAM, IPPROTO_TCP);
      if (s == -1) {
	  _x_message(stream, XINE_MSG_CONNECTION_REFUSED,
		       "failed to create socket", strerror(errno), NULL);
	  tmpaddr = tmpaddr->ai_next;
	  continue;
      }

      /*
       * Enable the non-blocking features only when there's no other
       * address, allowing to use other addresses if available.
       * there will be an error if no address worked at all
       */
      if ( ! tmpaddr->ai_next ) {
#ifndef WIN32
	if (fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK) == -1) {
	  _x_message(stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", strerror(errno), NULL);
	  return -1;
	}
#else
	unsigned long non_block = 1;
	int rc;

	rc = ioctlsocket(s, FIONBIO, &non_block);

	if (rc == SOCKET_ERROR) {
	  _x_message(stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", strerror(errno), NULL);
	  return -1;
	}
#endif
      }

#ifndef WIN32
    if (connect(s, tmpaddr->ai_addr,
		tmpaddr->ai_addrlen)==-1 && errno != EINPROGRESS) {

#else
    if (connect(s, tmpaddr->ai_addr,
		tmpaddr->ai_addrlen)==-1 &&
	WSAGetLastError() != WSAEWOULDBLOCK) {

      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: WSAGetLastError() = %d\n", WSAGetLastError());
#endif /* WIN32 */

      error = errno;
      close(s);
      tmpaddr = tmpaddr->ai_next;
      continue;
    } else {
      return s;
    }

    tmpaddr = tmpaddr->ai_next;
  }

  _x_message(stream, XINE_MSG_CONNECTION_REFUSED, strerror(error), NULL);

  return -1;
#endif
}


int _x_io_select (xine_stream_t *stream, int fd, int state, int timeout_msec) {

  fd_set fdset;
  fd_set *rset, *wset;
  struct timeval select_timeout;
  int timeout_usec, total_time_usec;
  int ret;
#ifdef WIN32
  HANDLE h;
  DWORD dwret;
  char msg[256];
#endif

#ifdef WIN32
  /* handle console file descriptiors differently on Windows */
  switch (fd) {
    case STDIN_FILENO: h = GetStdHandle(STD_INPUT_HANDLE); break;
    case STDOUT_FILENO: h = GetStdHandle(STD_OUTPUT_HANDLE); break;
    case STDERR_FILENO: h = GetStdHandle(STD_ERROR_HANDLE); break;
    default: h = INVALID_HANDLE_VALUE;
  }
#endif
  timeout_usec = 1000 * timeout_msec;
  total_time_usec = 0;

#ifdef WIN32
  if (h != INVALID_HANDLE_VALUE) {
    while (total_time_usec < timeout_usec) {
      dwret = WaitForSingleObject(h, timeout_msec);

      switch (dwret) {
        case WAIT_OBJECT_0: return XIO_READY;
        case WAIT_TIMEOUT:
          /* select timeout
           *   aborts current read if action pending. otherwise xine
           *   cannot be stopped when no more data is available.
           */
          if (stream && _x_action_pending(stream))
            return XIO_ABORTED;
          break;
        case WAIT_ABANDONED:
	  xine_log(stream->xine, XINE_LOG_MSG,
                 _("io_helper: waiting abandoned\n"));
          return XIO_ERROR;
        case WAIT_FAILED:
        default:
          dwret = GetLastError();
          FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, (LPSTR)&msg, sizeof(msg), NULL);
	  xine_log(stream->xine, XINE_LOG_MSG,
                 _("io_helper: waiting failed: %s\n"), msg);
          return XIO_ERROR;
      }
    }
    total_time_usec += XIO_POLLING_INTERVAL;
    return XIO_TIMEOUT;
  }
#endif
  while (total_time_usec < timeout_usec) {

    FD_ZERO (&fdset);
    FD_SET  (fd, &fdset);

    select_timeout.tv_sec  = 0;
    select_timeout.tv_usec = XIO_POLLING_INTERVAL;

    rset = (state & XIO_READ_READY) ? &fdset : NULL;
    wset = (state & XIO_WRITE_READY) ? &fdset : NULL;
    ret = select (fd + 1, rset, wset, NULL, &select_timeout);

    if (ret == -1 && errno != EINTR) {
      /* select error */
      return XIO_ERROR;
    } else if (ret == 1) {
      /* fd is ready */
      return XIO_READY;
    }

    /* select timeout
     *   aborts current read if action pending. otherwise xine
     *   cannot be stopped when no more data is available.
     */
    if (stream && _x_action_pending(stream))
      return XIO_ABORTED;

    total_time_usec += XIO_POLLING_INTERVAL;
  }
  return XIO_TIMEOUT;
}


/*
 * wait for finish connection
 */
int _x_io_tcp_connect_finish(xine_stream_t *stream, int fd, int timeout_msec) {
  int ret;

  ret = _x_io_select(stream, fd, XIO_WRITE_READY, timeout_msec);

  /* find out, if connection is successfull */
  if (ret == XIO_READY) {
    socklen_t len = sizeof(int);
    int err;

    if ((getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &len)) == -1) {
      _x_message(stream, XINE_MSG_CONNECTION_REFUSED, _("failed to get status of socket"), strerror(errno), NULL);
      return XIO_ERROR;
    }
    if (err) {
      _x_message(stream, XINE_MSG_CONNECTION_REFUSED, strerror(errno), NULL);
      return XIO_ERROR;
    }
  }

  return ret;
}


static off_t xio_rw_abort(xine_stream_t *stream, int fd, int cmd, void *buf_gen, off_t todo) {
  uint8_t *buf = buf_gen;

  off_t ret = -1;
  off_t total = 0;
  int sret;
  int state = 0;
  xine_cfg_entry_t cfgentry;
  unsigned int timeout;

  _x_assert(buf != NULL);

  if ((cmd == XIO_TCP_READ) || (cmd == XIO_FILE_READ)) {
    state = XIO_READ_READY;
  } else {
    state = XIO_WRITE_READY;
  }

  if (xine_config_lookup_entry (stream->xine, "media.network.timeout", &cfgentry)) {
    timeout = cfgentry.num_value * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  while (total < todo) {

    sret = _x_io_select(stream, fd, state, timeout);

    if (sret != XIO_READY)
      return -1;

    switch (cmd) {
      case XIO_FILE_READ:
        ret = read(fd, &buf[total], todo - total);
        break;
      case XIO_FILE_WRITE:
        ret = write(fd, &buf[total], todo - total);
        break;
      case XIO_TCP_READ:
        ret = recv(fd, &buf[total], todo - total, 0);
        break;
      case XIO_TCP_WRITE:
        ret = send(fd, &buf[total], todo - total, 0);
        break;
      default:
        assert(1);
    }
    /* check EOF */
    if (!ret)
      break;

    /* check errors */
    if (ret < 0) {

      /* non-blocking mode */
#ifndef WIN32
      if (errno == EAGAIN)
        continue;

      if (errno == EACCES) {
        _x_message(stream, XINE_MSG_PERMISSION_ERROR, NULL, NULL);
	xine_log (stream->xine, XINE_LOG_MSG,
		  _("io_helper: Permission denied\n"));
      } else if (errno == ENOENT) {
        _x_message(stream, XINE_MSG_FILE_NOT_FOUND, NULL, NULL);
	xine_log (stream->xine, XINE_LOG_MSG,
		  _("io_helper: File not found\n"));
      } else if (errno == ECONNREFUSED) {
	_x_message(stream, XINE_MSG_CONNECTION_REFUSED, NULL, NULL);
	xine_log (stream->xine, XINE_LOG_MSG,
		  _("io_helper: Connection Refused\n"));
      } else {
        perror("io_helper: I/O error");
      }
#else
      if (WSAGetLastError() == WSAEWOULDBLOCK)
        continue;
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: WSAGetLastError() = %d\n", WSAGetLastError());
#endif

      return ret;
    }
    total += ret;
  }
  return total;
}

off_t _x_io_tcp_read (xine_stream_t *stream, int s, void *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_TCP_READ, buf, todo);
}

off_t _x_io_tcp_write (xine_stream_t *stream, int s, void *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_TCP_WRITE, buf, todo);
}

off_t _x_io_file_read (xine_stream_t *stream, int s, void *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_FILE_READ, buf, todo);
}

off_t _x_io_file_write (xine_stream_t *stream, int s, void *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_FILE_WRITE, buf, todo);
}

/*
 * read a string from socket, return string length (same as strlen)
 * the string is always '\0' terminated but given buffer size is never exceeded
 * that is, _x_io_tcp_read_line(,,,X) <= (X-1) ; X > 0
 */
int _x_io_tcp_read_line(xine_stream_t *stream, int sock, char *str, int size) {
  int i = 0;
  char c;
  off_t r;

  if( size <= 0 )
    return 0;

  while ((r = xio_rw_abort(stream, sock, XIO_TCP_READ, &c, 1)) != -1) {
    if (c == '\r' || c == '\n')
      break;
    if (i+1 == size)
      break;

    str[i] = c;
    i++;
  }

  if (r != -1 && c == '\r')
    r = xio_rw_abort(stream, sock, XIO_TCP_READ, &c, 1);

  str[i] = '\0';

  return (r != -1) ? i : (int)r;
}
