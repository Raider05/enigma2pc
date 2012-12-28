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
 * high level interface to rtsp servers.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "rtsp_session"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "rtsp.h"
#include "rtsp_session.h"
#include "real.h"
#include "rmff.h"
#include "asmrp.h"
#include <xine/xineutils.h>

#define BUF_SIZE 4096
#define HEADER_SIZE 4096

struct rtsp_session_s {

  rtsp_t       *s;

  /* receive buffer */
  uint8_t      *recv;
  int           recv_size;
  int           recv_read;

  /* header buffer */
  uint8_t       header[HEADER_SIZE];
  int           header_len;
  int           header_left;

  int           playing;
  int           start_time;
};

/* network bandwidth */
static const uint32_t rtsp_bandwidths[]={14400,19200,28800,33600,34430,57600,
					 115200,262200,393216,524300,1544000,10485800};

static const char *const rtsp_bandwidth_strs[]={"14.4 Kbps (Modem)", "19.2 Kbps (Modem)",
						"28.8 Kbps (Modem)", "33.6 Kbps (Modem)",
						"34.4 Kbps (Modem)", "57.6 Kbps (Modem)",
						"115.2 Kbps (ISDN)", "262.2 Kbps (Cable/DSL)",
						"393.2 Kbps (Cable/DSL)","524.3 Kbps (Cable/DSL)",
						"1.5 Mbps (T1)", "10.5 Mbps (LAN)", NULL};


rtsp_session_t *rtsp_session_start(xine_stream_t *stream, char *mrl) {

  rtsp_session_t *rtsp_session = calloc(1, sizeof(rtsp_session_t));
  xine_t *xine = stream->xine;
  char *server;
  char *mrl_line=strdup(mrl);
  rmff_header_t *h;
  int bandwidth_id;
  uint32_t bandwidth;

  bandwidth_id = xine->config->register_enum(xine->config, "media.network.bandwidth", 10,
			      rtsp_bandwidth_strs,
			      _("network bandwidth"),
			      _("Specify the bandwidth of your internet connection here. "
			        "This will be used when streaming servers offer different versions "
              "with different bandwidth requirements of the same stream."),
			      0, NULL, NULL);
  bandwidth = rtsp_bandwidths[bandwidth_id];

  rtsp_session->recv = xine_buffer_init(BUF_SIZE);

connect:

  /* connect to server */
  rtsp_session->s=rtsp_connect(stream, mrl_line, NULL);
  if (!rtsp_session->s)
  {
    xprintf(stream->xine, XINE_VERBOSITY_LOG,
	    _("rtsp_session: failed to connect to server %s\n"), mrl_line);
    xine_buffer_free(rtsp_session->recv);
    free(rtsp_session);
    return NULL;
  }

  /* looking for server type */
  if (rtsp_search_answers(rtsp_session->s,"Server"))
    server=strdup(rtsp_search_answers(rtsp_session->s,"Server"));
  else {
    if (rtsp_search_answers(rtsp_session->s,"RealChallenge1"))
      server=strdup("Real");
    else
      server=strdup("unknown");
  }

  if (strstr(server,"Real") || strstr(server,"Helix"))
  {
    /* we are talking to a real server ... */

    h=real_setup_and_get_header(rtsp_session->s, bandwidth);
    if (!h) {
      /* got an redirect? */
      if (rtsp_search_answers(rtsp_session->s, "Location"))
      {
        free(mrl_line);
	mrl_line=strdup(rtsp_search_answers(rtsp_session->s, "Location"));
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "rtsp_session: redirected to %s\n", mrl_line);
	rtsp_close(rtsp_session->s);
	free(server);
	goto connect; /* *shudder* i made a design mistake somewhere */
      } else
      {
        xprintf(stream->xine, XINE_VERBOSITY_LOG,
		_("rtsp_session: session can not be established.\n"));
        rtsp_close(rtsp_session->s);
        xine_buffer_free(rtsp_session->recv);
        free(rtsp_session);
        return NULL;
      }
    }

	  rtsp_session->header_left =
    rtsp_session->header_len  = rmff_dump_header(h,rtsp_session->header,HEADER_SIZE);
    if (rtsp_session->header_len < 0) {
      xprintf (stream->xine, XINE_VERBOSITY_LOG,
	       _("rtsp_session: rtsp server returned overly-large headers, session can not be established.\n"));
      goto session_abort;
    }

    xine_buffer_copyin(rtsp_session->recv, 0, rtsp_session->header, rtsp_session->header_len);
    rtsp_session->recv_size = rtsp_session->header_len;
    rtsp_session->recv_read = 0;

  } else
  {
    xprintf(stream->xine, XINE_VERBOSITY_LOG,
	    _("rtsp_session: rtsp server type '%s' not supported yet. sorry.\n"), server);
    session_abort:
    rtsp_close(rtsp_session->s);
    free(server);
    xine_buffer_free(rtsp_session->recv);
    free(rtsp_session);
    return NULL;
  }
  free(server);

  return rtsp_session;
}

void rtsp_session_set_start_time (rtsp_session_t *this, int start_time) {

  if (start_time >= 0)
    this->start_time = start_time;
}

static void rtsp_session_play (rtsp_session_t *this) {

  char buf[256];

  snprintf (buf, sizeof(buf), "Range: npt=%d.%03d-",
            this->start_time/1000, this->start_time%1000);

  rtsp_schedule_field (this->s, buf);
  rtsp_request_play (this->s,NULL);
}

int rtsp_session_read (rtsp_session_t *this, char *data, int len) {

  int to_copy;
  char *dest=data;
  uint8_t *source=this->recv + this->recv_read;
  int fill=this->recv_size - this->recv_read;

  if (len < 0)
    return 0;

  if (this->header_left) {
    if (len > this->header_left)
      len = this->header_left;

    this->header_left -= len;
  }

  to_copy = len;
  while (to_copy > fill) {

    if (!this->playing) {
      rtsp_session_play (this);
      this->playing = 1;
    }

    memcpy(dest, source, fill);
    to_copy -= fill;
    dest += fill;
    this->recv_read = 0;
    this->recv_size = real_get_rdt_chunk (this->s, &this->recv);
    source = this->recv;
    fill = this->recv_size;

    if (this->recv_size == 0) {
      lprintf ("%d of %d bytes provided\n", len-to_copy, len);

      return len-to_copy;
    }
  }

  memcpy(dest, source, to_copy);
  this->recv_read += to_copy;

  lprintf ("%d bytes provided\n", len);

  return len;
}

int rtsp_session_peek_header(rtsp_session_t *this, char *buf, int maxsize) {

  int len;

  len = (this->header_len < maxsize) ? this->header_len : maxsize;

  memcpy(buf, this->header, len);
  return len;
}

void rtsp_session_end(rtsp_session_t *session) {

  rtsp_close(session->s);
  xine_buffer_free(session->recv);
  free(session);
}
