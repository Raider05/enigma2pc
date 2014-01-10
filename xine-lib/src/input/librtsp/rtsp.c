/*
 * Copyright (C) 2000-2012 the xine project
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
 * a minimalistic implementation of rtsp protocol,
 * *not* RFC 2326 compilant yet.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <base64.h>
#else
#  include <libavutil/base64.h>
#endif

#define LOG_MODULE "rtsp"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "rtsp.h"
#include <xine/io_helper.h>
#include <xine/xineutils.h>

#define BUF_SIZE 4096
#define HEADER_SIZE 1024
#define MAX_FIELDS 256

struct rtsp_s {

  xine_stream_t *stream;

  int           s;

  char         *host;
  int           port;
  char         *path;
  char         *mrl;
  char         *user_agent;

  char         *server;
  unsigned int  server_state;
  uint32_t      server_caps;

  unsigned int  cseq;
  char         *session;
  char         *auth;

  char        *answers[MAX_FIELDS];   /* data of last message */
  char        *scheduled[MAX_FIELDS]; /* will be sent with next message */
};

/*
 * constants
 */

static const char rtsp_protocol_version[]="RTSP/1.0";

/* server states */
#define RTSP_CONNECTED 1
#define RTSP_INIT      2
#define RTSP_READY     4
#define RTSP_PLAYING   8
#define RTSP_RECORDING 16

/* server capabilities */
#define RTSP_OPTIONS       0x001
#define RTSP_DESCRIBE      0x002
#define RTSP_ANNOUNCE      0x004
#define RTSP_SETUP         0x008
#define RTSP_GET_PARAMETER 0x010
#define RTSP_SET_PARAMETER 0x020
#define RTSP_TEARDOWN      0x040
#define RTSP_PLAY          0x080
#define RTSP_RECORD        0x100

/*
 * rtsp_get gets a line from stream
 * and returns a null terminated string (must be freed).
 */

static char *rtsp_get(rtsp_t *s) {
  char buffer[BUF_SIZE];
  char *string = NULL;

  if ( _x_io_tcp_read_line(s->stream, s->s, buffer, BUF_SIZE) >= 0 ) {
    lprintf("<< '%s'\n", buffer);
    string = strdup( buffer );
  }

  return string;
}


/*
 * rtsp_put puts a line on stream
 */

static void rtsp_put(rtsp_t *s, const char *string) {

  size_t len=strlen(string);
  char *buf = malloc(sizeof(char)*len+2);

  lprintf(">> '%s'", string);

  memcpy(buf,string,len);
  buf[len]=0x0d;
  buf[len+1]=0x0a;

  _x_io_tcp_write(s->stream, s->s, buf, len+2);

  lprintf("done.\n");
}

/*
 * extract server status code
 */

static int rtsp_get_code(rtsp_t *s, const char *string) {

  char buf[4];
  int code=0;

  if (!strncmp(string, rtsp_protocol_version, strlen(rtsp_protocol_version)))
  {
    memcpy(buf, string+strlen(rtsp_protocol_version)+1, 3);
    buf[3]=0;
    code=atoi(buf);
  } else if (!strncmp(string, "SET_PARAMETER",8))
  {
    return RTSP_STATUS_SET_PARAMETER;
  }

  if(code != 200)
    xprintf(s->stream->xine, XINE_VERBOSITY_DEBUG, "librtsp: server responds: '%s'\n", string);
  if (code == 401)
    _x_message(s->stream, XINE_MSG_AUTHENTICATION_NEEDED,
               s->mrl, NULL, NULL);

  return code;
}

/*
 * send a request
 */

static void rtsp_send_request(rtsp_t *s, const char *type, const char *what) {

  char **payload=s->scheduled;
  char *buf;

  buf = _x_asprintf("%s %s %s",type, what, rtsp_protocol_version);
  rtsp_put(s,buf);

  if (s->auth) {
    rtsp_put(s, s->auth);
  }

  if (payload)
    while (*payload) {
      rtsp_put(s,*payload);
      payload++;
    }
  rtsp_put(s,"");
  rtsp_unschedule_all(s);
}

/*
 * schedule standard fields
 */

static void rtsp_schedule_standard(rtsp_t *s) {

  char tmp[17];

  sprintf(tmp, "Cseq: %u", s->cseq);
  rtsp_schedule_field(s, tmp);

  if (s->session) {
    char *buf;
    buf = _x_asprintf("Session: %s", s->session);
    rtsp_schedule_field(s, buf);
  }
}
/*
 * get the answers, if server responses with something != 200, return NULL
 */

static int rtsp_get_answers(rtsp_t *s) {

  char *answer=NULL;
  unsigned int answer_seq;
  char **answer_ptr=s->answers;
  int code;
  int ans_count = 0;

  answer=rtsp_get(s);
  if (!answer)
    return 0;
  code=rtsp_get_code(s, answer);
  free(answer);

  rtsp_free_answers(s);

  do { /* while we get answer lines */

    answer=rtsp_get(s);
    if (!answer)
      return 0;

    if (!strncasecmp(answer,"Cseq: ",6)) {
      sscanf(answer+6,"%u",&answer_seq);
      if (s->cseq != answer_seq) {
        lprintf("warning: Cseq mismatch. got %u, assumed %u", answer_seq, s->cseq);

        s->cseq=answer_seq;
      }
    }
    if (!strncasecmp(answer,"Server: ",8)) {
      free(s->server);
      s->server = strdup(answer + 8);
    }
    if (!strncasecmp(answer,"Session: ",9)) {
      char *tmp = answer + 9;
      if (s->session) {
        if (strcmp(tmp, s->session)) {
          xprintf(s->stream->xine, XINE_VERBOSITY_DEBUG,
		  "rtsp: warning: setting NEW session: %s\n", tmp);
          s->session=strdup(tmp);
        }
      } else
      {
        lprintf("setting session id to: %s\n", tmp);

        s->session=strdup(tmp);
      }
    }
    *answer_ptr=answer;
    answer_ptr++;
  } while ((strlen(answer)!=0) && (++ans_count < MAX_FIELDS));

  s->cseq++;

  *answer_ptr=NULL;
  rtsp_schedule_standard(s);

  return code;
}

/*
 * send an ok message
 */

int rtsp_send_ok(rtsp_t *s) {
  char cseq[16];

  rtsp_put(s, "RTSP/1.0 200 OK");
  sprintf(cseq,"CSeq: %u", s->cseq);
  rtsp_put(s, cseq);
  rtsp_put(s, "");
  return 0;
}

/*
 * implementation of must-have rtsp requests; functions return
 * server status code.
 */

int rtsp_request_options(rtsp_t *s, const char *what) {

  char *buf;

  if (what) {
    buf=strdup(what);
  } else
  {
    buf = _x_asprintf("rtsp://%s:%i", s->host, s->port);
  }
  rtsp_send_request(s,"OPTIONS",buf);
  free(buf);

  return rtsp_get_answers(s);
}

int rtsp_request_describe(rtsp_t *s, const char *what) {

  char *buf;

  if (what) {
    buf=strdup(what);
  } else
  {
    buf = _x_asprintf("rtsp://%s:%i/%s", s->host, s->port, s->path);
  }
  rtsp_send_request(s,"DESCRIBE",buf);
  free(buf);

  return rtsp_get_answers(s);
}

int rtsp_request_setup(rtsp_t *s, const char *what) {

  rtsp_send_request(s,"SETUP",what);

  return rtsp_get_answers(s);
}

int rtsp_request_setparameter(rtsp_t *s, const char *what) {

  char *buf;

  if (what) {
    buf=strdup(what);
  } else
  {
    buf = _x_asprintf("rtsp://%s:%i/%s", s->host, s->port, s->path);
  }
  rtsp_send_request(s,"SET_PARAMETER",buf);
  free(buf);

  return rtsp_get_answers(s);
}

int rtsp_request_play(rtsp_t *s, const char *what) {

  char *buf;

  if (what) {
    buf=strdup(what);
  } else
  {
    buf = _x_asprintf("rtsp://%s:%i/%s", s->host, s->port, s->path);
  }
  rtsp_send_request(s,"PLAY",buf);
  free(buf);

  return rtsp_get_answers(s);
}

#if 0
int rtsp_request_tearoff(rtsp_t *s, const char *what) {

  rtsp_send_request(s,"TEAROFF",what);

  return rtsp_get_answers(s);
}
#endif

/*
 * read opaque data from stream
 */

int rtsp_read_data(rtsp_t *s, void *buffer_gen, unsigned int size) {
  uint8_t *buffer = buffer_gen;
  int i,seq;

  if (size>=4) {
    i=_x_io_tcp_read(s->stream, s->s, buffer, 4);
    if (i<4) return i;
    if ((buffer[0]=='S')&&(buffer[1]=='E')&&(buffer[2]=='T')&&(buffer[3]=='_'))
    {
      char *rest=rtsp_get(s);
      if (!rest)
        return -1;

      seq=-1;
      do {
        free(rest);
        rest=rtsp_get(s);
        if (!rest)
          return -1;
        if (!strncasecmp(rest,"Cseq:",5))
          sscanf(rest,"%*s %u",&seq);
      } while (strlen(rest)!=0);
      free(rest);
      if (seq<0) {
        lprintf("warning: cseq not recognized!\n");

        seq=1;
      }
      /* lets make the server happy */
      rtsp_put(s, "RTSP/1.0 451 Parameter Not Understood");
      rest = _x_asprintf("CSeq: %u", seq);
      rtsp_put(s, rest);
      free(rest);
      rtsp_put(s, "");
      i=_x_io_tcp_read(s->stream, s->s, buffer, size);
    } else
    {
      i=_x_io_tcp_read(s->stream, s->s, buffer+4, size-4);
      i+=4;
    }
  } else
    i=_x_io_tcp_read(s->stream, s->s, buffer, size);

  lprintf("<< %d of %d bytes\n", i, size);

  return i;
}

/*
 * connect to a rtsp server
 */

static void rtsp_basicauth (const char *user, const char *password, char** dest) {
  const size_t totlen = strlen(user) + (password ? strlen(password) : 0) + 1;
  const size_t enclen = ((totlen + 2) * 4 ) / 3 + 12;
  char         tmp[totlen + 1];

  snprintf(tmp, totlen + 1, "%s:%s", user, password ? : "");

  *dest = malloc(enclen);
  av_base64_encode(*dest, enclen, tmp, totlen);
}

rtsp_t *rtsp_connect(xine_stream_t *stream, const char *mrl, const char *user_agent) {

  rtsp_t *s = malloc(sizeof(rtsp_t));
  char *mrl_ptr=strdup(mrl);
  char *slash, *colon, *amp;
  int hostend, i;
  size_t pathbegin;

  if (strncmp(mrl,"rtsp://",7))
  {
    xprintf(stream->xine, XINE_VERBOSITY_LOG, _("rtsp: bad mrl: %s\n"), mrl);
    free(s);
    return NULL;
  }

  mrl_ptr+=7;

  for (i=0; i<MAX_FIELDS; i++) {
    s->answers[i]=NULL;
    s->scheduled[i]=NULL;
  }

  s->stream=stream;
  s->host=NULL;
  s->port=554; /* rtsp standard port */
  s->path=NULL;
  s->mrl=NULL;
  s->mrl=strdup(mrl);

  s->server=NULL;
  s->server_state=0;
  s->server_caps=0;

  s->cseq=0;
  s->session=NULL;

  if (user_agent)
    s->user_agent=strdup(user_agent);
  else
    s->user_agent=strdup("User-Agent: RealMedia Player Version 6.0.9.1235 (linux-2.0-libc6-i386-gcc2.95)");

  amp  =strchr(mrl_ptr,'@');
  slash=strchr(mrl_ptr,'/');
  colon=strchr(mrl_ptr,':');

  if (amp && (!slash || amp < slash)) {
    char *username = NULL, *password = NULL;
    /* xxx@host:port/ */
    if (colon && colon < amp) {
      /* xxx:yyy@host:port/ */
      username = strndup(mrl_ptr, colon - mrl_ptr);
      password = strndup(colon + 1, amp - colon - 1);
    } else {
      username = strndup(mrl_ptr, amp - mrl_ptr);
    }

    mrl_ptr = amp + 1;
    slash = strchr(mrl_ptr, '/');
    colon = strchr(mrl_ptr, ':');

    if (username) {
      char *auth;
      rtsp_basicauth(username, password, &auth);
      s->auth = _x_asprintf("Authorization: Basic %s", auth);
      free(auth);
    }
    free(username);
    free(password);
  }

  if(!slash) slash=mrl_ptr+strlen(mrl_ptr)+1;
  if(!colon) colon=slash;
  if(colon > slash) colon=slash;

  pathbegin=slash-mrl_ptr;
  hostend=colon-mrl_ptr;

  s->host = strndup(mrl_ptr, hostend);

  if (pathbegin < strlen(mrl_ptr)) s->path=strdup(mrl_ptr+pathbegin+1);
  if (colon != slash) {
    char buffer[pathbegin-hostend];

    strncpy(buffer,mrl_ptr+hostend+1, pathbegin-hostend-1);
    buffer[pathbegin-hostend-1]=0;
    s->port=atoi(buffer);
    if (s->port < 0 || s->port > 65535) s->port = 554; /* rtsp standard port */
  }

  lprintf("got mrl: %s %i %s\n",s->host,s->port,s->path);

  s->s = _x_io_tcp_connect (stream, s->host, s->port);

  if (s->s < 0) {
    xprintf (stream->xine, XINE_VERBOSITY_LOG, _("rtsp: failed to connect to '%s'\n"), s->host);
    rtsp_close(s);
    return NULL;
  }

  s->server_state=RTSP_CONNECTED;

  /* now lets send an options request. */
  rtsp_schedule_field(s, "CSeq: 1");
  rtsp_schedule_field(s, s->user_agent);
  rtsp_schedule_field(s, "ClientChallenge: 9e26d33f2984236010ef6253fb1887f7");
  rtsp_schedule_field(s, "PlayerStarttime: [28/03/2003:22:50:23 00:00]");
  rtsp_schedule_field(s, "CompanyID: KnKV4M4I/B2FjJ1TToLycw==");
  rtsp_schedule_field(s, "GUID: 00000000-0000-0000-0000-000000000000");
  rtsp_schedule_field(s, "RegionData: 0");
  rtsp_schedule_field(s, "ClientID: Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
  /*rtsp_schedule_field(s, "Pragma: initiate-session");*/
  rtsp_request_options(s, NULL);

  return s;
}


/*
 * closes an rtsp connection
 */

void rtsp_close(rtsp_t *s) {

  if (s->server_state) close(s->s); /* TODO: send a TEAROFF */
  if (s->path) free(s->path);
  if (s->host) free(s->host);
  if (s->mrl) free(s->mrl);
  if (s->session) free(s->session);
  if (s->user_agent) free(s->user_agent);
  free(s->auth);
  rtsp_free_answers(s);
  rtsp_unschedule_all(s);
  free(s);
}

/*
 * search in answers for tags. returns a pointer to the content
 * after the first matched tag. returns NULL if no match found.
 */

char *rtsp_search_answers(rtsp_t *s, const char *tag) {

  char **answer;
  char *ptr;

  if (!s->answers) return NULL;
  answer=s->answers;

  while (*answer) {
    if (!strncasecmp(*answer,tag,strlen(tag))) {
      ptr=strchr(*answer,':');
      if (!ptr) return NULL;
      ptr++;
      while(*ptr==' ') ptr++;
      return ptr;
    }
    answer++;
  }

  return NULL;
}

#if 0
/*
 * session id management
 */

void rtsp_set_session(rtsp_t *s, const char *id) {

  if (s->session) free(s->session);

  s->session=strdup(id);

}

char *rtsp_get_session(rtsp_t *s) {

  return s->session;

}
#endif

char *rtsp_get_mrl(rtsp_t *s) {

  return s->mrl;

}

/*
 * schedules a field for transmission
 */

void rtsp_schedule_field(rtsp_t *s, const char *string) {

  int i=0;

  if (!string) return;

  while(s->scheduled[i]) {
    i++;
  }
  s->scheduled[i]=strdup(string);
}

#if 0
/*
 * removes the first scheduled field which prefix matches string.
 */

void rtsp_unschedule_field(rtsp_t *s, const char *string) {

  char **ptr=s->scheduled;

  if (!string) return;

  while(*ptr) {
    if (!strncmp(*ptr, string, strlen(string)))
      break;
  }
  if (*ptr) free(*ptr);
  ptr++;
  do {
    *(ptr-1)=*ptr;
  } while(*ptr);
}
#endif

/*
 * unschedule all fields
 */

void rtsp_unschedule_all(rtsp_t *s) {

  char **ptr;

  if (!s->scheduled) return;
  ptr=s->scheduled;

  while (*ptr) {
    free(*ptr);
    *ptr=NULL;
    ptr++;
  }
}
/*
 * free answers
 */

void rtsp_free_answers(rtsp_t *s) {

  char **answer;

  if (!s->answers) return;
  answer=s->answers;

  while (*answer) {
    free(*answer);
    *answer=NULL;
    answer++;
  }
}
