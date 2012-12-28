/*
 * Copyright (C) 2000-2004 the xine project
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
 * input plugin for http network streams
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
#include <ctype.h>

#ifndef WIN32
#include <netdb.h>
#include <errno.h>
#endif /* WIN32 */

#include <sys/time.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <base64.h>
#else
#  include <libavutil/base64.h>
#endif

#define LOG_MODULE "input_http"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"
#include "http_helper.h"

#define BUFSIZE                 1024

#define DEFAULT_HTTP_PORT         80

#define TAG_ICY_NAME       "icy-name:"
#define TAG_ICY_GENRE      "icy-genre:"
#define TAG_ICY_NOTICE1    "icy-notice1:"
#define TAG_ICY_NOTICE2    "icy-notice2:"
#define TAG_ICY_METAINT    "icy-metaint:"
#define TAG_CONTENT_TYPE   "Content-Type:"
#define TAG_LASTFM_SERVER  "Server: last.fm "

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;

  char            *mrl;

  nbc_t           *nbc;

  off_t            curpos;
  off_t            contentlength;

  char             buf[BUFSIZE];

  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;

  char            *mime_type;
  const char      *user_agent;
  char            *proto;
  char            *user;
  char            *password;
  char            *host;
  char            *uri;
  int              port;

  int              fh;

  /** Set to 1 if the stream is a NSV stream. */
  int              is_nsv:1;
  /** Set to 1 if the stream comes from last.fm. */
  int              is_lastfm:1;
  /** Set to 1 if the stream is ShoutCast. */
  int              shoutcast_mode:1;

  /* ShoutCast */
  int              shoutcast_metaint;
  off_t            shoutcast_pos;
  char            *shoutcast_songtitle;

  /* scratch buffer for forward seeking */

  char             seek_buf[BUFSIZE];

} http_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

  char             *proxyhost;
  char             *proxyhost_env;
  int               proxyport;
  int               proxyport_env;

  char             *proxyuser;
  char             *proxypassword;
  char             *noproxylist;
} http_input_class_t;

static void proxy_host_change_cb (void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyhost = cfg->str_value;
}

static void proxy_port_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyport = cfg->num_value;
}

static void proxy_user_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyuser = cfg->str_value;
}

static void proxy_password_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxypassword = cfg->str_value;
}

static void no_proxy_list_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->noproxylist = cfg->str_value;
}

/*
 * handle no-proxy list config option and returns, if use the proxy or not
 * if error occurred, is expected using the proxy
 */
static int _x_use_proxy(http_input_class_t *this, const char *host) {
  const char *target;
  char *no_proxy, *domain, *ptr = NULL;
  struct hostent *info;
  size_t i = 0, host_len, noprox_len;

  /*
   * get full host name
   */
  if ((info = gethostbyname(host)) == NULL) {
    xine_log(this->xine, XINE_LOG_MSG,
        _("input_http: gethostbyname(%s) failed: %s\n"), host,
        hstrerror(h_errno));
    return 1;
  }
  if (!info->h_name) return 1;

  if ( info->h_addr_list[0] ) {
    /* \177\0\0\1 is the *octal* representation of 127.0.0.1 */
    if ( info->h_addrtype == AF_INET && !memcmp(info->h_addr_list[0], "\177\0\0\1", 4) ) {
      lprintf("host '%s' is localhost\n", host);
      return 0;
    }
    /* TODO: IPv6 check */
  }

  target = info->h_name;

  host_len = strlen(target);
  no_proxy = strdup(this->noproxylist);
  domain = strtok_r(no_proxy, ",", &ptr);
  while (domain) {
    /* skip leading spaces */
    while (isspace (*domain))
      ++domain;
    /* only check for matches if we've not reached the end of the token */
    if (*domain) {
      /* special-case domain beginning with '=' -> is a host name */
      if (domain[0] == '=' && strcmp(target, domain + 1) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
	return 0;
      }
      noprox_len = strlen(domain);
      /* special-case host==domain, avoiding dot checks */
      if (host_len == noprox_len && strcmp(target, domain) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
	return 0;
      }
      /* check for host in domain, and require that (if matched) the domain
       * name is preceded by a dot, either in the host or domain strings,
       * e.g. "a.foo.bar" is in "foo.bar" and ".foo.bar" but not "o.bar"
       */
      if (host_len > noprox_len
	  && (domain[0] == '.' || target[host_len - noprox_len - 1] == '.')
	  && strcmp(target + host_len - noprox_len, domain) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
	return 0;
      }
      lprintf("host '%s' isn't in no-proxy domain '%s'\n", target, domain);
    }
    domain = strtok_r(NULL, ",", &ptr);
    i++;
  }
  free(no_proxy);

  return 1;
}

static void http_plugin_basicauth (const char *user, const char *password, char** dest) {
  const size_t totlen = strlen(user) + (password ? strlen(password) : 0) + 1;
  const size_t enclen = ((totlen + 2) * 4 ) / 3 + 12;
  char         tmp[totlen + 1];

  snprintf(tmp, totlen + 1, "%s:%s", user, password ? : "");

  *dest = malloc(enclen);
  av_base64_encode(*dest, enclen, tmp, totlen);
}

static int http_plugin_read_metainf (http_input_plugin_t *this) {

  char metadata_buf[255 * 16];
  unsigned char len = 0;
  char *title_end;
  char *songtitle;
  const char *radio;
  xine_event_t uevent;
  xine_ui_data_t data;

  /* get the length of the metadata */
  if (_x_io_tcp_read (this->stream, this->fh, (char*)&len, 1) != 1)
    return 0;

  lprintf ("http_plugin_read_metainf: len=%d\n", len);

  if (len > 0) {
    if (_x_io_tcp_read (this->stream, this->fh, metadata_buf, len * 16) != (len * 16))
      return 0;

    metadata_buf[len * 16] = '\0';

    lprintf ("http_plugin_read_metainf: %s\n", metadata_buf);

    /* Extract the title of the current song */
    if ((songtitle = strstr(metadata_buf, "StreamTitle="))) {
      char terminator[] = { ';', 0, 0 };
      songtitle += 12; /* skip "StreamTitle=" */
      if (*songtitle == '\'' || *songtitle == '"') {
        terminator[0] = *songtitle++;
        terminator[1] = ';';
      }
      if ((title_end = strstr(songtitle, terminator))) {
        *title_end = '\0';

        if ((!this->shoutcast_songtitle ||
             (strcmp(songtitle, this->shoutcast_songtitle))) &&
            (strlen(songtitle) > 0)) {

          lprintf ("http_plugin_read_metainf: songtitle: %s\n", songtitle);

          if (this->shoutcast_songtitle)
            free(this->shoutcast_songtitle);
          this->shoutcast_songtitle = strdup(songtitle);

          _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, songtitle);

          /* prepares the event */
          radio = _x_meta_info_get(this->stream, XINE_META_INFO_ALBUM);
          if (radio) {
	    snprintf (data.str, sizeof(data.str), "%s - %s", radio, songtitle);
          } else {
            strncpy(data.str, songtitle, sizeof(data.str)-1);
          }
          data.str[sizeof(data.str) - 1] = '\0';
          data.str_len = strlen(data.str) + 1;

          /* sends the event */
          uevent.type = XINE_EVENT_UI_SET_TITLE;
          uevent.stream = this->stream;
          uevent.data = &data;
          uevent.data_length = sizeof(data);
          xine_event_send(this->stream, &uevent);
        }
      }
    }
  }
  return 1;
}

/*
 * Handle shoutcast packets
 */
static off_t http_plugin_read_int (http_input_plugin_t *this,
                                   char *buf, off_t total) {
  int read_bytes = 0;
  int nlen;

  lprintf("total=%"PRId64"\n", total);
  while (total) {
    nlen = total;
    if (this->shoutcast_mode &&
        ((this->shoutcast_pos + nlen) >= this->shoutcast_metaint)) {
      nlen = this->shoutcast_metaint - this->shoutcast_pos;

      nlen = _x_io_tcp_read (this->stream, this->fh, &buf[read_bytes], nlen);
      if (nlen < 0)
        goto error;

      if (!http_plugin_read_metainf(this))
        goto error;
      this->shoutcast_pos = 0;

    } else {
      nlen = _x_io_tcp_read (this->stream, this->fh, &buf[read_bytes], nlen);
      if (nlen < 0)
        goto error;

      /* Identify SYNC string for last.fm, this is limited to last.fm
       * streaming servers to avoid hitting on tracks metadata for other
       * servers.
       */
      if ( this->is_lastfm &&
	   memmem(&buf[read_bytes], nlen, "SYNC", 4) != NULL ) {
	/* Tell frontend to update the UI */
	const xine_event_t event = {
	  .type = XINE_EVENT_UI_CHANNELS_CHANGED,
	  .stream = this->stream,
	  .data = NULL,
	  .data_length = 0
	};

	lprintf("SYNC from last.fm server received\n");

	xine_event_send(this->stream, &event);
      }

      this->shoutcast_pos += nlen;
    }

    /* end of file */
    if (nlen == 0)
      return read_bytes;
    read_bytes          += nlen;
    total               -= nlen;
  }
  return read_bytes;

error:
  if (!_x_action_pending(this->stream))
    _x_message (this->stream, XINE_MSG_READ_ERROR, this->host, NULL);
  xine_log (this->stream->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
  return read_bytes;
}

static off_t http_plugin_read (input_plugin_t *this_gen,
                               void *buf_gen, off_t nlen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;
  off_t n, num_bytes;

  num_bytes = 0;

  if (nlen < 0)
    return -1;

  if (this->curpos < this->preview_size) {

    if (nlen > (this->preview_size - this->curpos))
      n = this->preview_size - this->curpos;
    else
      n = nlen;

    lprintf ("%"PRId64" bytes from preview (which has %"PRId64" bytes)\n", n, this->preview_size);
    memcpy (buf, &this->preview[this->curpos], n);

    num_bytes += n;
    this->curpos += n;
  }

  n = nlen - num_bytes;

  if (n > 0) {
    int read_bytes;
    read_bytes = http_plugin_read_int (this, &buf[num_bytes], n);

    if (read_bytes < 0)
      return read_bytes;

    num_bytes += read_bytes;
    this->curpos += read_bytes;
  }

  return num_bytes;
}

static int resync_nsv(http_input_plugin_t *this) {
  uint8_t c;
  int pos = 0;
  int read_bytes = 0;

  lprintf("resyncing NSV stream\n");
  while ((pos < 3) && (read_bytes < (1024*1024))) {

    if (http_plugin_read_int(this, (char*)&c, 1) != 1)
      return 1;

    this->preview[pos] = c;
    switch (pos) {
      case 0:
        if (c == 'N')
          pos++;
        break;
      case 1:
        if (c == 'S')
          pos++;
        else
          if (c != 'N')
            pos = 0;
        break;
      case 2:
        if (c == 'V')
          pos++;
        else
          if (c == 'N')
            pos = 1;
          else
            pos = 0;
        break;
    }
    read_bytes++;
  }
  if (pos == 3) {
    lprintf("NSV stream resynced\n");
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
      "http: cannot resync NSV stream!\n");
    return 0;
  }

  return 1;
}

static buf_element_t *http_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 total_bytes;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = http_plugin_read (this_gen, (char*)buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    buf = NULL;
  }

  if (buf != NULL)
    buf->size = total_bytes;

  return buf;
}

static off_t http_plugin_get_length (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->contentlength;
}

static uint32_t http_plugin_get_capabilities (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  uint32_t caps = INPUT_CAP_PREVIEW;

  /* Nullsoft asked to not allow saving streaming nsv files */
  if (this->uri && strlen(this->uri) >= 4 &&
      !strncmp(this->uri + strlen(this->uri) - 4, ".nsv", 4))
    caps |= INPUT_CAP_RIP_FORBIDDEN;

  return caps;
}

static uint32_t http_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

static off_t http_plugin_get_current_pos (input_plugin_t *this_gen){
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->curpos;
}

static off_t http_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

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
          "http: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n",
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

static const char* http_plugin_get_mrl (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->mrl;
}

static int http_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *const data, int data_type) {

  void **const ptr = (void **const) data;
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:
    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

  case INPUT_OPTIONAL_DATA_MIME_TYPE:
    *ptr = this->mime_type;
  case INPUT_OPTIONAL_DATA_DEMUX_MIME_TYPE:
    return *this->mime_type ? INPUT_OPTIONAL_SUCCESS : INPUT_OPTIONAL_UNSUPPORTED;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void http_plugin_dispose (input_plugin_t *this_gen ) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  if (this->fh != -1) {
    close(this->fh);
    this->fh = -1;
  }

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if (this->mrl) free(this->mrl);
  if (this->proto) free(this->proto);
  if (this->host) free(this->host);
  if (this->user) free(this->user);
  if (this->password) free(this->password);
  if (this->uri) free(this->uri);
  free (this);
}

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  prg.description = _("Connecting HTTP server...");
  prg.percent = p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

static int http_plugin_open (input_plugin_t *this_gen ) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  http_input_class_t  *this_class = (http_input_class_t *) this->input_plugin.input_class;
  int                  done, len, linenum;
  int                  httpcode;
  int                  res;
  size_t               buflen;
  int                  use_proxy;
  int                  proxyport;
  int                  mpegurl_redirect = 0;
  char                 mime_type[256];

  mime_type[0] = 0;
  use_proxy = this_class->proxyhost && strlen(this_class->proxyhost);

  if (!_x_parse_url(this->mrl, &this->proto, &this->host, &this->port,
                    &this->user, &this->password, &this->uri,
                    &this->user_agent)) {
    _x_message(this->stream, XINE_MSG_GENERAL_WARNING, "malformed url", NULL);
    return 0;
  }
  use_proxy = use_proxy && _x_use_proxy(this_class, this->host);

  if (this->port == 0)
    this->port = DEFAULT_HTTP_PORT;

  if (this_class->proxyport == 0)
    proxyport = DEFAULT_HTTP_PORT;
  else
    proxyport = this_class->proxyport;

#ifdef LOG
  {
    printf ("input_http: host     : >%s<\n", this->host);
    printf ("input_http: port     : >%d<\n", this->port);
    printf ("input_http: user     : >%s<\n", this->user);
    printf ("input_http: password : >%s<\n", this->password);
    printf ("input_http: path     : >%s<\n", this->uri);


    if (use_proxy)
      printf (" via proxy >%s:%d<", this_class->proxyhost, proxyport);

    printf ("\n");
  }

#endif

  if (use_proxy)
    this->fh = _x_io_tcp_connect (this->stream, this_class->proxyhost, proxyport);
  else
    this->fh = _x_io_tcp_connect (this->stream, this->host, this->port);

  this->curpos = 0;

  if (this->fh == -1)
    return -2;

  {
    uint32_t         timeout, progress;
    xine_cfg_entry_t cfgentry;
    if (xine_config_lookup_entry (this->stream->xine, "media.network.timeout", &cfgentry)) {
      timeout = cfgentry.num_value * 1000;
    } else {
      timeout = 30000; /* 30K msecs = 30 secs */
    }

    progress = 0;
    do {
      report_progress(this->stream, progress);
      res = _x_io_select (this->stream, this->fh, XIO_WRITE_READY, 500);
      progress += (500*100000)/timeout;
    } while ((res == XIO_TIMEOUT) && (progress <= 100000));

    if (res != XIO_READY) {
      _x_message(this->stream, XINE_MSG_NETWORK_UNREACHABLE, this->mrl, NULL);
      return -3;
    }
  }

  if (use_proxy) {
    if (this->port != DEFAULT_HTTP_PORT) {
      snprintf (this->buf, BUFSIZE, "GET http://%s:%d%s HTTP/1.0\015\012",
	       this->host, this->port, this->uri);
    } else {
      snprintf (this->buf, BUFSIZE, "GET http://%s%s HTTP/1.0\015\012",
	       this->host, this->uri);
    }
  }
  else
    snprintf (this->buf, BUFSIZE, "GET %s HTTP/1.0\015\012", this->uri);

  buflen = strlen(this->buf);
  if (this->port != DEFAULT_HTTP_PORT)
    snprintf (this->buf + buflen, BUFSIZE - buflen, "Host: %s:%d\015\012",
	     this->host, this->port);
  else
    snprintf (this->buf + buflen, BUFSIZE - buflen, "Host: %s\015\012",
	     this->host);

  buflen = strlen(this->buf);
  if (use_proxy && this_class->proxyuser && strlen(this_class->proxyuser)) {
    char *proxyauth;
    http_plugin_basicauth (this_class->proxyuser, this_class->proxypassword,
			   &proxyauth);

    snprintf (this->buf + buflen, BUFSIZE - buflen,
              "Proxy-Authorization: Basic %s\015\012", proxyauth);
    buflen = strlen(this->buf);
    free(proxyauth);
  }
  if (this->user && strlen(this->user)) {
    char *auth;
    http_plugin_basicauth (this->user, this->password, &auth);

    snprintf (this->buf + buflen, BUFSIZE - buflen,
              "Authorization: Basic %s\015\012", auth);
    buflen = strlen(this->buf);
    free(auth);
  }

  snprintf(this->buf + buflen, BUFSIZE - buflen,
           "User-Agent: %s%sxine/%s\015\012"
           "Accept: */*\015\012"
           "Icy-MetaData: 1\015\012"
           "\015\012",
           this->user_agent ? this->user_agent : "",
           this->user_agent ? " " : "",
           VERSION);
  buflen = strlen(this->buf);
  if (_x_io_tcp_write (this->stream, this->fh, this->buf, buflen) != buflen) {
    _x_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "couldn't send request", NULL);
    xprintf(this_class->xine, XINE_VERBOSITY_DEBUG, "input_http: couldn't send request\n");
    return -4;
  }

  lprintf ("request sent: >%s<\n", this->buf);

  /* read and parse reply */
  done = 0; len = 0; linenum = 0;
  this->contentlength = 0;

  while (!done) {
    /* fprintf (stderr, "input_http: read...\n"); */

    if (_x_io_tcp_read (this->stream, this->fh, &this->buf[len], 1) <= 0) {
      return -5;
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;

      if (len >= 0 && this->buf[len] == '\015') {
	this->buf[len] = '\0';
	len--;
      }

      linenum++;

      lprintf ("answer: >%s<\n", this->buf);

      if (linenum == 1) {
        int httpver, httpsub;
	char httpstatus[51] = { 0, };

	if (
	    (sscanf(this->buf, "HTTP/%d.%d %d %50[^\015\012]", &httpver, &httpsub,
		    &httpcode, httpstatus) != 4) &&
	    (sscanf(this->buf, "HTTP/%d.%d %d", &httpver, &httpsub,
		    &httpcode) != 3) &&
	    (sscanf(this->buf, "ICY %d %50[^\015\012]", /* icecast 1 ? */
		    &httpcode, httpstatus) != 2)
	   ) {
	    _x_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "invalid http answer", NULL);
	    xine_log (this->stream->xine, XINE_LOG_MSG,
		      _("input_http: invalid http answer\n"));
	    return -6;
	}

	if (httpcode >= 300 && httpcode < 400) {
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: 3xx redirection: >%d %s<\n"),
		    httpcode, httpstatus);
	} else if (httpcode == 404) {
	  _x_message(this->stream, XINE_MSG_FILE_NOT_FOUND, this->mrl, NULL);
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: http status not 2xx: >%d %s<\n"),
		                        httpcode, httpstatus);
	  return -7;
	} else if (httpcode == 401) {
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: http status not 2xx: >%d %s<\n"),
		    httpcode, httpstatus);
          /* don't return - there may be a WWW-Authenticate header... */
	} else if (httpcode == 403) {
          _x_message(this->stream, XINE_MSG_PERMISSION_ERROR, this->mrl, NULL);
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: http status not 2xx: >%d %s<\n"),
		    httpcode, httpstatus);
	  return -8;
	} else if (httpcode < 200 || httpcode >= 300) {
	  _x_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "http status not 2xx: ",
	               httpstatus, NULL);
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: http status not 2xx: >%d %s<\n"),
		    httpcode, httpstatus);
	  return -9;
	}
      } else {
	if (this->contentlength == 0) {
	  intmax_t contentlength;

	  if (sscanf(this->buf, "Content-Length: %" SCNdMAX , &contentlength) == 1) {
	    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("input_http: content length = %" PRIdMAX " bytes\n"),
              contentlength);
	    this->contentlength = (off_t)contentlength;
	  }
        }

	if (!strncasecmp(this->buf, "Location: ", 10)) {
	  char *href = (this->buf + 10);

	  lprintf ("trying to open target of redirection: >%s<\n", href);

          href = _x_canonicalise_url (this->mrl, href);
          free(this->mrl);
          this->mrl = href;
          return http_plugin_open(this_gen);
        }

        if (!strncasecmp (this->buf, "WWW-Authenticate: ", 18))
          strcpy (this->preview, this->buf + 18);

	{
	  static const char mpegurl_ct_str[] = "Content-Type: audio/x-mpegurl";
	  static const size_t mpegurl_ct_size = sizeof(mpegurl_ct_str)-1;
	  if (!strncasecmp(this->buf, mpegurl_ct_str, mpegurl_ct_size)) {
	    lprintf("Opening an audio/x-mpegurl file, late redirect.");

	    mpegurl_redirect = 1;
	  }
	}

        /* Icecast / ShoutCast Stuff */
        if (!strncasecmp(this->buf, TAG_ICY_NAME, sizeof(TAG_ICY_NAME) - 1)) {
          _x_meta_info_set(this->stream, XINE_META_INFO_ALBUM,
                           (this->buf + sizeof(TAG_ICY_NAME) - 1 +
                            (*(this->buf + sizeof(TAG_ICY_NAME) - 1) == ' ')));
          _x_meta_info_set(this->stream, XINE_META_INFO_TITLE,
                           (this->buf + sizeof(TAG_ICY_NAME) - 1 +
                            (*(this->buf + sizeof(TAG_ICY_NAME) - 1) == ' ')));
        }

        if (!strncasecmp(this->buf, TAG_ICY_GENRE, sizeof(TAG_ICY_GENRE) - 1)) {
          _x_meta_info_set(this->stream, XINE_META_INFO_GENRE,
                          (this->buf + sizeof(TAG_ICY_GENRE) - 1 +
                           (*(this->buf + sizeof(TAG_ICY_GENRE) - 1) == ' ')));
        }

        /* icy-notice1 is always the same */
        if (!strncasecmp(this->buf, TAG_ICY_NOTICE2, sizeof(TAG_ICY_NOTICE2) - 1)) {
          char *end;
          if((end = strstr(this->buf, "<BR>")))
            *end = '\0';

          _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT,
                           (this->buf + sizeof(TAG_ICY_NOTICE2) - 1 +
                            (*(this->buf + sizeof(TAG_ICY_NOTICE2) - 1) == ' ')));
        }

        /* metadata interval (in byte) */
        if (sscanf(this->buf, TAG_ICY_METAINT"%d", &this->shoutcast_metaint) == 1) {
          lprintf("shoutcast_metaint: %d\n", this->shoutcast_metaint);
          this->shoutcast_mode = 1;
          this->shoutcast_pos = 0;
        }

        /* content type */
        if (!strncasecmp(this->buf, TAG_CONTENT_TYPE, sizeof(TAG_CONTENT_TYPE) - 1)) {
          const char *type = this->buf + sizeof (TAG_CONTENT_TYPE) - 1;
          while (isspace (*type))
            ++type;
          sprintf (mime_type, "%.255s", type);
          if (!strncasecmp (type, "video/nsv", 9)) {
            lprintf("shoutcast nsv detected\n");
            this->is_nsv = 1;
          }
        }
	if ( !strncasecmp(this->buf, TAG_LASTFM_SERVER, sizeof(TAG_LASTFM_SERVER)-1) ) {
	  lprintf("last.fm streaming server detected\n");
	  this->is_lastfm = 1;
	}
      }

      if (len == -1)
	done = 1;
      else
	len = 0;
    } else
      len ++;
    if ( len >= BUFSIZE ) {
       _x_message(this->stream, XINE_MSG_PERMISSION_ERROR, this->mrl, NULL);
       xine_log (this->stream->xine, XINE_LOG_MSG,
         _("input_http: buffer exhausted after %d bytes."), BUFSIZE);
       return -10;
    }
  }

  lprintf ("end of headers\n");

  if (httpcode == 401)
    _x_message(this->stream, XINE_MSG_AUTHENTICATION_NEEDED,
               this->mrl, *this->preview ? this->preview : NULL, NULL);

  if ( mpegurl_redirect ) {
    char buf[4096] = { 0, };
    char *newline = NULL;

    http_plugin_read_int(this, buf, 4095);
    newline = strstr(buf, "\r\n");

    /* If the newline can't be found, either the 4K buffer is too small, or
     * more likely something is fuzzy.
     */
    if ( newline ) {
      char *href;

      *newline = '\0';

      lprintf("mpegurl pointing to %s\n", buf);

      href = _x_canonicalise_url (this->mrl, buf);
      free(this->mrl);
      this->mrl = href;
      return http_plugin_open(this_gen);
    }
  }

  /*
   * fill preview buffer
   */
  this->preview_size = MAX_PREVIEW_SIZE;
  if (this->is_nsv) {
    if (!resync_nsv(this))
      return -11;

    /* the first 3 chars are "NSV" */
    this->preview_size = http_plugin_read_int (this, this->preview + 3, MAX_PREVIEW_SIZE - 3);
  } else {
    this->preview_size = http_plugin_read_int (this, this->preview, MAX_PREVIEW_SIZE);
  }
  if (this->preview_size < 0) {
    this->preview_size = 0;
    xine_log (this->stream->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
    return -12;
  }

  lprintf("preview_size=%"PRId64"\n", this->preview_size);
  this->curpos = 0;
  if (*mime_type)
    this->mime_type = strdup (mime_type);

  return 1;
}

/*
 * http input plugin class
 */
static input_plugin_t *http_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *mrl) {
  /* http_input_class_t  *cls = (http_input_class_t *) cls_gen;*/
  http_input_plugin_t *this;

  if (strncasecmp (mrl, "http://", 7) &&
      strncasecmp (mrl, "unsv://", 7) &&
      strncasecmp (mrl, "peercast://pls/", 15) &&
      !_x_url_user_agent (mrl) /* user agent hacks */) {
    return NULL;
  }
  this = calloc(1, sizeof(http_input_plugin_t));

  if (!strncasecmp (mrl, "peercast://pls/", 15)) {
    this->mrl = _x_asprintf ("http://127.0.0.1:7144/stream/%s", mrl+15);
  } else {
    this->mrl = strdup (mrl);
  }

  this->stream = stream;
  this->fh     = -1;
  this->nbc    = nbc_init (this->stream);

  this->input_plugin.open              = http_plugin_open;
  this->input_plugin.get_capabilities  = http_plugin_get_capabilities;
  this->input_plugin.read              = http_plugin_read;
  this->input_plugin.read_block        = http_plugin_read_block;
  this->input_plugin.seek              = http_plugin_seek;
  this->input_plugin.get_current_pos   = http_plugin_get_current_pos;
  this->input_plugin.get_length        = http_plugin_get_length;
  this->input_plugin.get_blocksize     = http_plugin_get_blocksize;
  this->input_plugin.get_mrl           = http_plugin_get_mrl;
  this->input_plugin.get_optional_data = http_plugin_get_optional_data;
  this->input_plugin.dispose           = http_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

static void http_class_dispose (input_class_t *this_gen) {
  http_input_class_t  *this = (http_input_class_t *) this_gen;

  free(this->proxyhost_env);

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  http_input_class_t  *this;
  config_values_t     *config;
  char                *proxy_env;

  this = calloc(1, sizeof (http_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = http_class_get_instance;
  this->input_class.identifier         = "http";
  this->input_class.description        = N_("http input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = http_class_dispose;
  this->input_class.eject_media        = NULL;

  /*
   * honour http_proxy envvar
   */
  if((proxy_env = getenv("http_proxy")) && *proxy_env) {
    int    proxy_port = DEFAULT_HTTP_PORT;
    char  *p;

    if(!strncmp(proxy_env, "http://", 7))
      proxy_env += 7;

    this->proxyhost_env = strdup(proxy_env);

    if((p = strrchr(this->proxyhost_env, ':')) && (strlen(p) > 1)) {
      *p++ = '\0';
      proxy_port = (int) strtol(p, &p, 10);
    }

    this->proxyport_env = proxy_port;
  }
  else
    proxy_env = NULL; /* proxy_env can be "" */

  /*
   * proxy settings
   */
  this->proxyhost = config->register_string(config,
					    "media.network.http_proxy_host", proxy_env ? this->proxyhost_env : "",
					    _("HTTP proxy host"), _("The hostname of the HTTP proxy."), 10,
					    proxy_host_change_cb, (void *) this);
  this->proxyport = config->register_num(config,
					 "media.network.http_proxy_port", proxy_env ? this->proxyport_env : DEFAULT_HTTP_PORT,
					 _("HTTP proxy port"), _("The port number of the HTTP proxy."), 10,
					 proxy_port_change_cb, (void *) this);

  /* registered entries could be empty. Don't ignore envvar */
  if(!strlen(this->proxyhost) && (proxy_env && strlen(proxy_env))) {
    config->update_string(config, "media.network.http_proxy_host", this->proxyhost_env);
    config->update_num(config, "media.network.http_proxy_port", this->proxyport_env);
  }

  this->proxyuser = config->register_string(config,
					    "media.network.http_proxy_user", "", _("HTTP proxy username"),
					    _("The user name for the HTTP proxy."), 10,
					    proxy_user_change_cb, (void *) this);
  this->proxypassword = config->register_string(config,
						"media.network.http_proxy_password", "", _("HTTP proxy password"),
						_("The password for the HTTP proxy."), 10,
						proxy_password_change_cb, (void *) this);
  this->noproxylist = config->register_string(config,
					      "media.network.http_no_proxy", "", _("Domains for which to ignore the HTTP proxy"),
					      _("A comma-separated list of domain names for which the proxy is to be ignored.\nIf a domain name is prefixed with '=' then it is treated as a host name only (full match required)."), 10,
					      no_proxy_list_change_cb, (void *) this);

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "http", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
