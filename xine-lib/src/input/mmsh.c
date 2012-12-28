/*
 * Copyright (C) 2002-2003 the xine project
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
 * MMS over HTTP protocol
 *   written by Thibaut Mattern
 *   based on mms.c and specs from avifile
 *   (http://avifile.sourceforge.net/asf-1.0.htm)
 *
 * TODO:
 *   error messages
 *   http support cleanup, find a way to share code with input_http.c (http.h|c)
 *   http proxy support
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#define LOG_MODULE "mmsh"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#include "bswap.h"
#include "http_helper.h"
#include "mmsh.h"
#include "../demuxers/asfheader.h"

/* #define USERAGENT "User-Agent: NSPlayer/7.1.0.3055\r\n" */
#define USERAGENT "User-Agent: NSPlayer/4.1.0.3856\r\n"
#define CLIENTGUID "Pragma: xClientGUID={c77e7400-738a-11d2-9add-0020af0a3278}\r\n"


#define MMSH_PORT                  80
#define MMSH_UNKNOWN                0
#define MMSH_SEEKABLE               1
#define MMSH_LIVE                   2

#define CHUNK_HEADER_LENGTH         4
#define EXT_HEADER_LENGTH           8
#define CHUNK_TYPE_RESET       0x4324
#define CHUNK_TYPE_DATA        0x4424
#define CHUNK_TYPE_END         0x4524
#define CHUNK_TYPE_ASF_HEADER  0x4824
#define CHUNK_SIZE              65536  /* max chunk size */
#define ASF_HEADER_SIZE          8192  /* max header size */

#define SCRATCH_SIZE             1024

#define mmsh_FirstRequest \
    "GET %s HTTP/1.0\r\n" \
    "Accept: */*\r\n" \
    USERAGENT \
    "Host: %s:%d\r\n" \
    "Pragma: no-cache,rate=1.000000,stream-time=0,stream-offset=0:0,request-context=%u,max-duration=0\r\n" \
    CLIENTGUID \
    "Connection: Close\r\n\r\n"

#define mmsh_SeekableRequest \
    "GET %s HTTP/1.0\r\n" \
    "Accept: */*\r\n" \
    USERAGENT \
    "Host: %s:%d\r\n" \
    "Pragma: no-cache,rate=1.000000,stream-time=%u,stream-offset=%u:%u,request-context=%u,max-duration=%u\r\n" \
    CLIENTGUID \
    "Pragma: xPlayStrm=1\r\n" \
    "Pragma: stream-switch-count=%d\r\n" \
    "Pragma: stream-switch-entry=%s\r\n" /*  ffff:1:0 ffff:2:0 */ \
    "Connection: Close\r\n\r\n"

#define mmsh_LiveRequest \
    "GET %s HTTP/1.0\r\n" \
    "Accept: */*\r\n" \
    USERAGENT \
    "Host: %s:%d\r\n" \
    "Pragma: no-cache,rate=1.000000,request-context=%u\r\n" \
    "Pragma: xPlayStrm=1\r\n" \
    CLIENTGUID \
    "Pragma: stream-switch-count=%d\r\n" \
    "Pragma: stream-switch-entry=%s\r\n" \
    "Connection: Close\r\n\r\n"

/* Unused requests */
#if 0
#define mmsh_PostRequest \
    "POST %s HTTP/1.0\r\n" \
    "Accept: */*\r\n" \
    USERAGENT \
    "Host: %s\r\n" \
    "Pragma: client-id=%u\r\n" \
/*    "Pragma: log-line=no-cache,rate=1.000000,stream-time=%u,stream-offset=%u:%u,request-context=2,max-duration=%u\r\n"
 */ \
    "Pragma: Content-Length: 0\r\n" \
    CLIENTGUID \
    "\r\n"

#define mmsh_RangeRequest \
    "GET %s HTTP/1.0\r\n" \
    "Accept: */*\r\n" \
    USERAGENT \
    "Host: %s:%d\r\n" \
    "Range: bytes=%Lu-\r\n" \
    CLIENTGUID \
    "Connection: Close\r\n\r\n"
#endif


/*
 * mmsh specific types
 */


struct mmsh_s {

  xine_stream_t *stream;

  int           s;

  /* url parsing */
  char         *url;
  char         *proto;
  char         *host;
  int           port;
  char         *user;
  char         *password;
  char         *uri;

  char          str[SCRATCH_SIZE]; /* scratch buffer to built strings */

  asf_header_t *asf_header;
  int           stream_type;

  /* receive buffer */

  /* chunk */
  uint16_t      chunk_type;
  uint16_t      chunk_length;
  uint16_t      chunk_seq_number;
  uint8_t       buf[CHUNK_SIZE];

  int           buf_size;
  int           buf_read;

  uint8_t       asf_header_buffer[ASF_HEADER_SIZE];
  uint32_t      asf_header_len;
  uint32_t      asf_header_read;
  int           seq_num;

  int           video_stream;
  int           audio_stream;

  off_t         current_pos;
  int           user_bandwidth;

  int           playing;
  unsigned int  start_time;
};

static int send_command (mmsh_t *this, char *cmd)  {
  lprintf ("send_command:\n%s\n", cmd);

  const size_t length = strlen(cmd);
  if (_x_io_tcp_write(this->stream, this->s, cmd, length) != length) {
    xprintf (this->stream->xine, XINE_LOG_MSG, _("libmmsh: send error\n"));
    return 0;
  }
  return 1;
}

static int get_answer (mmsh_t *this) {

  int done, len, linenum;
  char *features;

  lprintf ("get_answer\n");

  done = 0; len = 0; linenum = 0;
  this->stream_type = MMSH_UNKNOWN;

  while (!done) {

    if (_x_io_tcp_read(this->stream, this->s, (char*)&(this->buf[len]), 1) != 1) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "libmmsh: alert: end of stream\n");
      return 0;
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;

      if ((len >= 0) && (this->buf[len] == '\015')) {
        this->buf[len] = '\0';
        len--;
      }

      linenum++;

      lprintf ("answer: >%s<\n", this->buf);

      if (linenum == 1) {
        int httpver, httpsub, httpcode;
        char httpstatus[51];

        if (sscanf((char*)this->buf, "HTTP/%d.%d %d %50[^\015\012]", &httpver, &httpsub,
            &httpcode, httpstatus) != 4) {
          xine_log (this->stream->xine, XINE_LOG_MSG,
                    _("libmmsh: bad response format\n"));
          return 0;
        }

        if (httpcode >= 300 && httpcode < 400) {
          xine_log (this->stream->xine, XINE_LOG_MSG,
                    _("libmmsh: 3xx redirection not implemented: >%d %s<\n"),
                    httpcode, httpstatus);
          return 0;
        }

        if (httpcode < 200 || httpcode >= 300) {
          xine_log (this->stream->xine, XINE_LOG_MSG,
                    _("libmmsh: http status not 2xx: >%d %s<\n"),
                    httpcode, httpstatus);
          return 0;
        }
      } else {

        if (!strncasecmp((char*)this->buf, "Location: ", 10)) {
          xine_log (this->stream->xine, XINE_LOG_MSG,
                    _("libmmsh: Location redirection not implemented\n"));
          return 0;
        }

        if (!strncasecmp((char*)this->buf, "Pragma:", 7)) {
          features = strstr((char*)(this->buf + 7), "features=");
          if (features) {
            if (strstr(features, "seekable")) {
              lprintf("seekable stream\n");
              this->stream_type = MMSH_SEEKABLE;
            } else {
              if (strstr(features, "broadcast")) {
                lprintf("live stream\n");
                this->stream_type = MMSH_LIVE;
              }
            }
          }
        }
      }

      if (len == -1) {
        done = 1;
      } else {
        len = 0;
      }
    } else {
      len ++;
    }
  }
  if (this->stream_type == MMSH_UNKNOWN) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             "libmmsh: unknown stream type\n");
    this->stream_type = MMSH_SEEKABLE; /* FIXME ? */
  }
  return 1;
}

static int get_chunk_header (mmsh_t *this) {
  uint8_t chunk_header[CHUNK_HEADER_LENGTH];
  uint8_t ext_header[EXT_HEADER_LENGTH];
  int read_len;
  int ext_header_len;

  lprintf ("get_chunk_header\n");

  /* read chunk header */
  read_len = _x_io_tcp_read(this->stream, this->s, (char*)chunk_header, CHUNK_HEADER_LENGTH);
  if (read_len != CHUNK_HEADER_LENGTH) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             "libmmsh: chunk header read failed, %d != %d\n", read_len, CHUNK_HEADER_LENGTH);
    return 0;
  }
  this->chunk_type       = _X_LE_16 (&chunk_header[0]);
  this->chunk_length     = _X_LE_16 (&chunk_header[2]);

  switch (this->chunk_type) {
    case CHUNK_TYPE_DATA:
      ext_header_len = 8;
      break;
    case CHUNK_TYPE_END:
      ext_header_len = 4;
      break;
    case CHUNK_TYPE_ASF_HEADER:
      ext_header_len = 8;
      break;
    case CHUNK_TYPE_RESET:
      ext_header_len = 4;
      break;
    default:
      ext_header_len = 0;
  }
  /* read extended header */
  if (ext_header_len > 0) {
    read_len = _x_io_tcp_read(this->stream, this->s, (char*)ext_header, ext_header_len);
    if (read_len != ext_header_len) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "extended header read failed, %d != %d\n", read_len, ext_header_len);
      return 0;
    }
  }

  switch (this->chunk_type) {
    case CHUNK_TYPE_DATA:
      this->chunk_seq_number = _X_LE_32 (&ext_header[0]);
      lprintf ("chunk type:       CHUNK_TYPE_DATA\n");
      lprintf ("chunk length:     %d\n", this->chunk_length);
      lprintf ("chunk seq:        %d\n", this->chunk_seq_number);
      lprintf ("unknown:          %d\n", ext_header[4]);
      lprintf ("mmsh seq:         %d\n", ext_header[5]);
      lprintf ("len2:             %d\n", _X_LE_16(&ext_header[6]));
      break;
    case CHUNK_TYPE_END:
      this->chunk_seq_number = _X_LE_32 (&ext_header[0]);
      lprintf ("chunk type:       CHUNK_TYPE_END\n");
      lprintf ("continue: %d\n", this->chunk_seq_number);
      break;
    case CHUNK_TYPE_ASF_HEADER:
      lprintf ("chunk type:       CHUNK_TYPE_ASF_HEADER\n");
      lprintf ("chunk length:     %d\n", this->chunk_length);
      lprintf ("unknown:          %2X %2X %2X %2X %2X %2X\n",
               ext_header[0], ext_header[1], ext_header[2], ext_header[3],
               ext_header[4], ext_header[5]);
      lprintf ("len2:             %d\n", _X_LE_16(&ext_header[6]));
      break;
    case CHUNK_TYPE_RESET:
      lprintf ("chunk type:       CHUNK_TYPE_RESET\n");
      lprintf ("chunk seq:        %d\n", this->chunk_seq_number);
      lprintf ("unknown:          %2X %2X %2X %2X\n",
               ext_header[0], ext_header[1], ext_header[2], ext_header[3]);
      break;
    default:
      lprintf ("unknown chunk:          %4X\n", this->chunk_type);
  }

  this->chunk_length -= ext_header_len;
  return 1;
}

static int get_header (mmsh_t *this) {
  int len = 0;

  lprintf("get_header\n");

  this->asf_header_len = 0;

  /* read chunk */
  while (1) {
    if (get_chunk_header(this)) {
      if (this->chunk_type == CHUNK_TYPE_ASF_HEADER) {
        if ((this->asf_header_len + this->chunk_length) > ASF_HEADER_SIZE) {
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
                   "libmmsh: the asf header exceed %d bytes\n", ASF_HEADER_SIZE);
          return 0;
        } else {
          len = _x_io_tcp_read(this->stream, this->s, (char*)(this->asf_header_buffer + this->asf_header_len),
                             this->chunk_length);
          this->asf_header_len += len;
          if (len != this->chunk_length) {
            return 0;
          }
        }
      } else {
        break;
      }
    } else {
      lprintf("get_chunk_header failed\n");
      return 0;
    }
  }

  if (this->chunk_type == CHUNK_TYPE_DATA) {
    /* read the first data chunk */
    len = _x_io_tcp_read(this->stream, this->s, (char*)this->buf, this->chunk_length);
    if (len != this->chunk_length) {
      return 0;
    } else {
      return 1;
    }
  } else {
    /* unexpected packet type */
    return 0;
  }
}

static int interp_header (mmsh_t *this) {

  lprintf ("interp_header, header_len=%d\n", this->asf_header_len);

  /* delete previous header */
  if (this->asf_header) {
    asf_header_delete(this->asf_header);
  }

  /* the header starts with :
   *   byte  0-15: header guid
   *   byte 16-23: header length
   */
  this->asf_header = asf_header_new(this->asf_header_buffer + 24, this->asf_header_len - 24);
  if (!this->asf_header)
    return 0;

  this->buf_size = this->asf_header->file->packet_size;
  return 1;
}

static const char mmsh_proto_s[][8] = { "mms", "mmsh", "" };

static int mmsh_valid_proto (char *proto) {
  int i = 0;

  lprintf("mmsh_valid_proto\n");

  if (!proto)
    return 0;

  while(*(mmsh_proto_s[i])) {
    if (!strcasecmp(proto, mmsh_proto_s[i])) {
      return 1;
    }
    i++;
  }
  return 0;
}

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  prg.description = _("Connecting MMS server (over http)...");
  prg.percent = p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

/*
 * returns 1 on error
 */
static int mmsh_tcp_connect(mmsh_t *this) {
  int progress, res;

  if (!this->port) this->port = MMSH_PORT;

  /*
   * try to connect
   */
  lprintf("try to connect to %s on port %d \n", this->host, this->port);

  this->s = _x_io_tcp_connect (this->stream, this->host, this->port);

  if (this->s == -1) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             "libmmsh: failed to connect '%s'\n", this->host);
    return 1;
  }

  /* connection timeout 15s */
  progress = 0;
  do {
    report_progress(this->stream, progress);
    res = _x_io_select (this->stream, this->s, XIO_WRITE_READY, 500);
    progress += 1;
  } while ((res == XIO_TIMEOUT) && (progress < 30));
  if (res != XIO_READY) {
    return 1;
  }
  lprintf ("connected\n");

  return 0;
}

/*
 * firts http request
 */
static int mmsh_connect_int(mmsh_t *this, int bandwidth) {
  /*
   * let the negotiations begin...
   */

  /* first request */
  lprintf("first http request\n");

  snprintf (this->str, SCRATCH_SIZE, mmsh_FirstRequest, this->uri,
            this->host, this->port, 1);

  if (!send_command (this, this->str))
    return 0;

  if (!get_answer (this))
    return 0;

  get_header (this); /* FIXME: it returns 0 */

  if (!interp_header (this))
    return 0;

  close (this->s);
  report_progress (this->stream, 20);

  asf_header_choose_streams (this->asf_header, bandwidth,
                             &this->video_stream, &this->audio_stream);

  lprintf("audio stream %d, video stream %d\n",
          this->audio_stream, this->video_stream);

  asf_header_disable_streams (this->asf_header,
                              this->video_stream, this->audio_stream);

  if (mmsh_tcp_connect(this))
    return 0;

  return 1;
}

/*
 * second http request
 */
static int mmsh_connect_int2(mmsh_t *this, int bandwidth) {
  int    i;
  char   stream_selection[10 * ASF_MAX_NUM_STREAMS]; /* 10 chars per stream */
  int    offset;

  /* second request */
  lprintf("second http request\n");

  /* stream selection string */
  /* The same selection is done with mmst */
  /* 0 means selected */
  /* 2 means disabled */
  offset = 0;
  for (i = 0; i < this->asf_header->stream_count; i++) {
    int size;
    if ((i == this->audio_stream) ||
        (i == this->video_stream)) {
      size = snprintf(stream_selection + offset, sizeof(stream_selection) - offset,
                      "ffff:%d:0 ", this->asf_header->streams[i]->stream_number);
    } else {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "disabling stream %d\n", this->asf_header->streams[i]->stream_number);
      size = snprintf(stream_selection + offset, sizeof(stream_selection) - offset,
                      "ffff:%d:2 ", this->asf_header->streams[i]->stream_number);
    }
    if (size < 0)
      return 0;
    offset += size;
  }

  switch (this->stream_type) {
    case MMSH_SEEKABLE:
      snprintf (this->str, SCRATCH_SIZE, mmsh_SeekableRequest, this->uri,
                this->host, this->port, this->start_time, 0, 0, 2, 0,
                this->asf_header->stream_count, stream_selection);
      break;
    case MMSH_LIVE:
      snprintf (this->str, SCRATCH_SIZE, mmsh_LiveRequest, this->uri,
                this->host, this->port, 2,
                this->asf_header->stream_count, stream_selection);
      break;
  }

  if (!send_command (this, this->str))
    return 0;

  lprintf("before read \n");

  if (!get_answer (this))
    return 0;

  if (!get_header (this))
    return 0;

#if 0
  if (!interp_header (this))
    return 0;

  asf_header_disable_streams (this->asf_header,
                              this->video_stream, this->audio_stream);
#endif

  return 1;
}

mmsh_t *mmsh_connect (xine_stream_t *stream, const char *url, int bandwidth) {
  mmsh_t *this;

  if (!url)
    return NULL;

  report_progress (stream, 0);

  this = calloc(1, sizeof (mmsh_t));

  this->stream          = stream;
  this->url             = strdup(url);
  this->s               = -1;
  this->asf_header_len  = 0;
  this->asf_header_read = 0;
  this->buf_size        = 0;
  this->buf_read        = 0;
  this->current_pos     = 0;
  this->user_bandwidth  = bandwidth;

  report_progress (stream, 0);

  if (!_x_parse_url (this->url, &this->proto, &this->host, &this->port,
                     &this->user, &this->password, &this->uri, NULL)) {
    xine_log (this->stream->xine, XINE_LOG_MSG, _("invalid url\n"));
    goto fail;
  }

  if (!mmsh_valid_proto(this->proto)) {
    xine_log (this->stream->xine, XINE_LOG_MSG, _("unsupported protocol\n"));
    goto fail;
  }

  if (mmsh_tcp_connect(this))
    goto fail;

  report_progress (stream, 30);

  if (!mmsh_connect_int(this, this->user_bandwidth))
    goto fail;

  report_progress (stream, 100);

  lprintf("mmsh_connect: passed\n" );

  return this;

fail:
  lprintf("mmsh_connect: failed\n" );
  if (this->s != -1)
    close(this->s);
  if (this->url)
    free(this->url);
  if (this->proto)
    free(this->proto);
  if (this->host)
    free(this->host);
  if (this->user)
    free(this->user);
  if (this->password)
    free(this->password);
  if (this->uri)
    free(this->uri);

  free(this);

  lprintf("mmsh_connect: failed return\n" );
  return NULL;
}


/*
 * returned value:
 *  0: error
 *  1: data packet read
 *  2: new header read
 */
static int get_media_packet (mmsh_t *this) {
  int len = 0;

  lprintf("get_media_packet: this->packet_length: %d\n", this->asf_header->file->packet_size);

  if (get_chunk_header(this)) {
    switch (this->chunk_type) {
      case CHUNK_TYPE_END:
        /* this->chunk_seq_number:
         *     0: stop
         *     1: a new stream follows
         */
        if (this->chunk_seq_number == 0)
          return 0;

        close(this->s);

        if (mmsh_tcp_connect(this))
          return 0;

        if (!mmsh_connect_int(this, this->user_bandwidth))
          return 0;

        this->playing = 0;

        /* mmsh_connect_int reads the first data packet */
        /* this->buf_size is set by mmsh_connect_int */
        return 2;

      case CHUNK_TYPE_DATA:
        /* nothing to do */
        break;

      case CHUNK_TYPE_RESET:
        /* next chunk is an ASF header */

        if (this->chunk_length != 0) {
          /* that's strange, don't know what to do */
          return 0;
        }
        if (!get_header(this))
          return 0;
        interp_header(this);
        return 2;

      default:
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
                 "libmmsh: unexpected chunk type\n");
        return 0;
    }

    len = _x_io_tcp_read (this->stream, this->s, (char*)this->buf, this->chunk_length);

    if (len == this->chunk_length) {
      /* explicit padding with 0 */
      if (this->chunk_length > this->asf_header->file->packet_size) {
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
                 "libmmsh: chunk_length(%d) > packet_length(%d)\n",
                 this->chunk_length, this->asf_header->file->packet_size);
        return 0;
      }
      memset(this->buf + this->chunk_length, 0,
             this->asf_header->file->packet_size - this->chunk_length);
      return 1;
    } else {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "libmmsh: read error, %d != %d\n", len, this->chunk_length);
      return 0;
    }
  } else {
    return 0;
  }
}

size_t mmsh_peek_header (mmsh_t *this, char *data, size_t maxsize) {
  size_t len;

  lprintf("mmsh_peek_header\n");

  len = (this->asf_header_len < maxsize) ? this->asf_header_len : maxsize;

  memcpy(data, this->asf_header_buffer, len);
  return len;
}

int mmsh_read (mmsh_t *this, char *data, int len) {
  int total;

  total = 0;

  lprintf ("mmsh_read: len: %d\n", len);

  while (total < len) {

    if (this->asf_header_read < this->asf_header_len) {
      int n, bytes_left ;

      bytes_left = this->asf_header_len - this->asf_header_read;

      if ((len-total) < bytes_left)
        n = len-total;
      else
        n = bytes_left;

      xine_fast_memcpy (&data[total], &this->asf_header_buffer[this->asf_header_read], n);

      this->asf_header_read += n;
      total += n;
      this->current_pos += n;

      if (this->asf_header_read == this->asf_header_len)
	break;
    } else {

      int n, bytes_left ;

      if (!this->playing) {
        if (!mmsh_connect_int2 (this, this->user_bandwidth))
          break;
        this->playing = 1;
      }

      bytes_left = this->buf_size - this->buf_read;

      if (bytes_left == 0) {
        int packet_type;

        this->buf_read = 0;
        packet_type = get_media_packet (this);

        if (packet_type == 0) {
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
                   "libmmsh: get_media_packet failed\n");
          return total;
        } else if (packet_type == 2) {
          continue;
        }

        bytes_left = this->buf_size;
      }

      if ((len - total) < bytes_left)
        n = len - total;
      else
        n = bytes_left;

      xine_fast_memcpy (&data[total], &this->buf[this->buf_read], n);

      this->buf_read += n;
      total += n;
      this->current_pos += n;
    }
  }
  return total;
}


void mmsh_close (mmsh_t *this) {

  lprintf("mmsh_close\n");

  if (this->s != -1)
    close(this->s);
  if (this->url)
    free (this->url);
  if (this->proto)
    free(this->proto);
  if (this->host)
    free(this->host);
  if (this->user)
    free(this->user);
  if (this->password)
    free(this->password);
  if (this->uri)
    free(this->uri);
  if (this->asf_header)
    asf_header_delete(this->asf_header);
  if (this)
    free (this);
}


uint32_t mmsh_get_length (mmsh_t *this) {
  return this->asf_header->file->file_size;
}

off_t mmsh_get_current_pos (mmsh_t *this) {
  return this->current_pos;
}

void mmsh_set_start_time (mmsh_t *this, int time_offset) {
  if (time_offset >= 0)
    this->start_time = time_offset;
}
