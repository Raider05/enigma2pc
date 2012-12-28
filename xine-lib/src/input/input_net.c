/*
 * Copyright (C) 2000-2003 the xine project
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
 * Read from a tcp network stream over a lan (put a tweaked mp1e encoder the
 * other end and you can watch tv anywhere in the house ..)
 *
 * how to set up mp1e for use with this plugin:
 *
 * use mp1 to capture the live stream, e.g.
 * mp1e -b 1200 -R 4,32 -a 0 -B 160 -v >live.mpg
 *
 * add an extra service "xine" to /etc/services and /etc/inetd.conf, e.g.:
 * /etc/services:
 * xine       1025/tcp
 * /etc/inetd.conf:
 * xine            stream  tcp     nowait  bartscgr        /usr/sbin/tcpd /usr/bin/tail -f /home/bartscgr/Projects/inf.misc/live.mpg
 *
 * now restart inetd and you can use xine to watch the live stream, e.g.:
 * xine tcp://192.168.0.43:1025.mpg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef ENABLE_IPV6
#include <sys/types.h>
#include <netdb.h>
#endif

#ifndef WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <errno.h>
#include <sys/time.h>

#define LOG_MODULE "input_net"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"

#define NET_BS_LEN 2324
#define BUFSIZE                 1024

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;

  int              fh;
  char            *mrl;
  char            *host_port;

  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;

  off_t            curpos;

  nbc_t           *nbc;

  /* scratch buffer for forward seeking */
  char             seek_buf[BUFSIZE];


} net_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

} net_input_class_t;

/* **************************************************************** */
/*                       Private functions                          */
/* **************************************************************** */

#ifndef ENABLE_IPV6
static int host_connect_attempt_ipv4(struct in_addr ia, int port, xine_t *xine) {

  int                s;
  union {
    struct sockaddr_in in;
    struct sockaddr    sa;
  } sa;

  s = xine_socket_cloexec(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s==-1) {
    xine_log(xine, XINE_LOG_MSG,
             _("input_net: socket(): %s\n"), strerror(errno));
    return -1;
  }

  sa.in.sin_family = AF_INET;
  sa.in.sin_addr   = ia;
  sa.in.sin_port   = htons(port);

#ifndef WIN32
  if (connect(s, &sa.sa, sizeof(sa.in))==-1 && errno != EINPROGRESS)
#else
  if (connect(s, &sa.sa, sizeof(sa.in))==-1 && WSAGetLastError() != WSAEINPROGRESS)
#endif
  {
    xine_log(xine, XINE_LOG_MSG,
             _("input_net: connect(): %s\n"), strerror(errno));
    close(s);
    return -1;
  }

  return s;
}
#else
static int host_connect_attempt(int family, struct sockaddr* sin, int addrlen, xine_t *xine) {

  int s = xine_socket_cloexec(family, SOCK_STREAM, IPPROTO_TCP);

  if (s == -1) {
    xine_log(xine, XINE_LOG_MSG,
             _("input_net: socket(): %s\n"), strerror(errno));
    return -1;
  }

#ifndef WIN32
  if (connect(s, sin, addrlen)==-1 && errno != EINPROGRESS)
#else
  if (connect(s, sin, addrlen)==-1 && WSAGetLastError() != WSAEINPROGRESS)
#endif
  {
    xine_log(xine, XINE_LOG_MSG,
             _("input_net: connect(): %s\n"), strerror(errno));
    close(s);
    return -1;
  }

  return s;
}
#endif

#ifndef ENABLE_IPV6
static int host_connect_ipv4(const char *host, int port, xine_t *xine) {
  struct hostent *h;
  int             i;
  int             s;

  h = gethostbyname(host);
  if (h==NULL) {
    xine_log(xine, XINE_LOG_MSG,
             _("input_net: unable to resolve '%s'.\n"), host);
    return -1;
  }

  for (i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy (&ia, h->h_addr_list[i],4);
    s = host_connect_attempt_ipv4 (ia, port, xine);
    if (s != -1)
      return s;
  }

  xine_log(xine, XINE_LOG_MSG,
           _("input_net: unable to connect to '%s'.\n"), host);
  return -1;
}
#endif

static int host_connect(const char *host, int port, xine_t *xine) {

#ifndef ENABLE_IPV6
  return host_connect_ipv4(host, port, xine);
#else

  struct addrinfo hints, *res, *tmpaddr;
  int error;
  char strport[16];
  int             s;

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = PF_UNSPEC;

  snprintf(strport, sizeof(strport), "%d", port);

  lprintf("Resolving host '%s' at port '%s'\n", host, strport);

  error = getaddrinfo(host, strport, &hints, &res);

  if (error) {

    xine_log(xine, XINE_LOG_MSG,
             _("input_net: unable to resolve '%s'.\n"), host);
    return -1;
  }

  /* We loop over all addresses and try to connect */
  tmpaddr = res;
  while (tmpaddr) {

      s = host_connect_attempt (tmpaddr->ai_family,
				tmpaddr->ai_addr, tmpaddr->ai_addrlen, xine);
      if (s != -1)
	  return s;

      tmpaddr = tmpaddr->ai_next;
  }

  xine_log(xine, XINE_LOG_MSG,
           _("input_net: unable to connect to '%s'.\n"), host);
  return -1;

#endif

}

#define LOW_WATER_MARK  50
#define HIGH_WATER_MARK 100

static off_t net_plugin_read (input_plugin_t *this_gen,
			      void *buf_gen, off_t len) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;
  off_t n, total;

  lprintf("reading %" PRIdMAX " bytes...\n", (intmax_t)len);

  if (len < 0)
    return -1;

  total=0;
  if (this->curpos < this->preview_size) {
    n = this->preview_size - this->curpos;
    if (n > (len - total))
      n = len - total;

    lprintf("%" PRIdMAX " bytes from preview (which has %" PRIdMAX " bytes)\n", (intmax_t)n, (intmax_t)this->preview_size);

    memcpy (&buf[total], &this->preview[this->curpos], n);
    this->curpos += n;
    total += n;
  }

  if( (len-total) > 0 ) {
    n = _x_read_abort (this->stream, this->fh, &buf[total], len-total);

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_net: got %" PRIdMAX " bytes (%" PRIdMAX "/%" PRIdMAX " bytes read)\n", (intmax_t)n, (intmax_t)total, (intmax_t)len);

    if (n < 0) {
      _x_message(this->stream, XINE_MSG_READ_ERROR, this->host_port, NULL);
      return 0;
    }

    this->curpos += n;
    total += n;
  }
  return total;
}

static buf_element_t *net_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t todo) {
  /* net_input_plugin_t   *this = (net_input_plugin_t *) this_gen; */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  off_t                 total_bytes;

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = net_plugin_read (this_gen, (char*)buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t net_plugin_get_length (input_plugin_t *this_gen) {

  return 0;
}

static uint32_t net_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

static uint32_t net_plugin_get_blocksize (input_plugin_t *this_gen) {

  return NET_BS_LEN;

}

static off_t net_plugin_get_current_pos (input_plugin_t *this_gen){
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->curpos;
}

static off_t net_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
        return this->curpos;
    }

    this_gen->read (this_gen, this->seek_buf, offset);
  }

  if (origin == SEEK_SET) {

    if (offset < this->curpos) {

      if( this->curpos <= this->preview_size )
        this->curpos = offset;
      else
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "input_net: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n",
                (intmax_t)this->curpos, (intmax_t)offset);

    } else {
      offset -= this->curpos;

      for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
        if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
          return this->curpos;
      }

      this_gen->read (this_gen, this->seek_buf, offset);
    }
  }

  return this->curpos;
}


static const char* net_plugin_get_mrl (input_plugin_t *this_gen) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->mrl;
}

static int net_plugin_get_optional_data (input_plugin_t *this_gen,
					 void *data, int data_type) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void net_plugin_dispose (input_plugin_t *this_gen ) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  if (this->fh != -1) {
    close(this->fh);
    this->fh = -1;
  }

  free (this->mrl);
  free (this->host_port);

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  free (this_gen);
}

static int net_plugin_open (input_plugin_t *this_gen ) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  char *filename;
  char *pptr;
  int port = 7658;
  int toread = MAX_PREVIEW_SIZE;
  int trycount = 0;

  filename = this->host_port;
  pptr=strrchr(filename, ':');
  if(pptr) {
    *pptr++ = 0;
    sscanf(pptr,"%d", &port);
  }

  this->fh     = host_connect(filename, port, this->stream->xine);
  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  /*
   * fill preview buffer
   */
  while ((toread > 0) && (trycount < 10)) {
#ifndef WIN32
    this->preview_size += read (this->fh, this->preview + this->preview_size, toread);
#else
    this->preview_size += recv (this->fh, this->preview + this->preview_size, toread, 0);
#endif
    trycount++;
    toread = MAX_PREVIEW_SIZE - this->preview_size;
  }

  this->curpos       = 0;

  return 1;
}

static input_plugin_t *net_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {
  /* net_input_plugin_t *this = (net_input_plugin_t *) this_gen; */
  net_input_plugin_t *this;
  nbc_t *nbc = NULL;
  char *filename;

  if (!strncasecmp (mrl, "tcp://", 6)) {
    filename = (char *) &mrl[6];

    if((!filename) || (strlen(filename) == 0)) {
      return NULL;
    }

    nbc = nbc_init (stream);

  } else if (!strncasecmp (mrl, "slave://", 8)) {

    filename = (char *) &mrl[8];

    if((!filename) || (strlen(filename) == 0)) {
      return NULL;
    }

    /* the only difference for slave:// is that network buffering control
     * is not used. otherwise, dvd still menus are not displayed (it freezes
     * with "buffering..." all the time)
     */

    nbc = NULL;

  } else {
    return NULL;
  }

  this = calloc(1, sizeof(net_input_plugin_t));
  this->mrl           = strdup(mrl);
  this->host_port     = strdup(filename);
  this->stream        = stream;
  this->fh            = -1;
  this->curpos        = 0;
  this->nbc           = nbc;
  this->preview_size  = 0;

  this->input_plugin.open              = net_plugin_open;
  this->input_plugin.get_capabilities  = net_plugin_get_capabilities;
  this->input_plugin.read              = net_plugin_read;
  this->input_plugin.read_block        = net_plugin_read_block;
  this->input_plugin.seek              = net_plugin_seek;
  this->input_plugin.get_current_pos   = net_plugin_get_current_pos;
  this->input_plugin.get_length        = net_plugin_get_length;
  this->input_plugin.get_blocksize     = net_plugin_get_blocksize;
  this->input_plugin.get_mrl           = net_plugin_get_mrl;
  this->input_plugin.get_optional_data = net_plugin_get_optional_data;
  this->input_plugin.dispose           = net_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}


/*
 *  net plugin class
 */

static void *init_class (xine_t *xine, void *data) {

  net_input_class_t  *this;

  this         = calloc(1, sizeof(net_input_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->input_class.get_instance      = net_class_get_instance;
  this->input_class.description       = N_("net input plugin as shipped with xine");
  this->input_class.identifier        = "TCP";
  this->input_class.get_dir           = NULL;
  this->input_class.get_autoplay_list = NULL;
  this->input_class.dispose           = default_input_class_dispose;
  this->input_class.eject_media       = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 18, "tcp", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

