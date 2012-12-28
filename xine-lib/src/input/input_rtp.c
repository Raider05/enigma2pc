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
 * Xine input plugin for multicast video streams.
 *
 * This is something of an experiment - it doesn't work well yet.  Originally
 * the intent was to read an rtp stream, from, for example, Cisco IP
 * Tv. That's still a long term goal but RTP doesn't fit well in an input
 * plugin because typically video is carried on one multicast group and audio
 * in another - i.e it's already demultiplexed and an input plugin would
 * actually have to reassemble the content. Now that demultiplexers are
 * becomming separate loadable objects the right thing to do is to write an
 * RTP demux plugin and a playlist plugin that handles SDP.
 *
 *
 * In the meantime some experience with multicast video was wanted.  Not
 * having hardware available to construct a stream on the fly a server was
 * written to multicast the contents of an mpeg program stream - it just
 * reads a pack then transmits it at the appropriate time as follows.
 *
 *  fd is open for read on mpeg stream, sock for write on a multicast socket.
 *
 *  while (1) {
 *      \/\* read pack *\/
 *      read(fd, buf, 2048)
 *      \/\* end of stream  *\/
 *      if (buf[3] == 0xb9)
 *          return 0;
 *
 *      \/\* extract the system reference clock, srcb, from the pack *\/
 *
 *      send_at = srcb/90000.0;
 *      while (time_now < send_at) {
 *          wait;
 *      }
 *      r = write(sock, buf, 2048);
 *  }
 *
 * One problem is that a stream from a DVD needs each pack sending
 * at approx 2.5ms intervals which is a shorter interval than the
 * standard linux clock. The RTC can be used for more finely grained
 * timing.
 *
 * If you live in a non multicast friendly environment then the stream
 * can be unicast.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <net/if.h>
#include <sys/select.h>

#if defined (__SVR4) && defined (__sun)
#  include <sys/sockio.h>
#endif

#define LOG_MODULE "input_rtp"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"

#ifdef __GNUC__
#define LOG_MSG(xine, message, args...) {                         \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                \
    lprintf(message, ##args);                                     \
  }
#else
#define LOG_MSG(xine, ...) {                                      \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                    \
    lprintf(__VA_ARGS__);                                         \
  }
#endif

#define BUFFER_SIZE (1024*1024)

typedef struct {
  input_plugin_t    input_plugin;

  xine_stream_t    *stream;

  char             *mrl;
  config_values_t  *config;

  char             *filename;
  int               port;
  char             *interface;    /* For multicast,  eth0, eth1 etc */
  int               is_rtp;

  int               fh;

  unsigned char    *buffer;	  /* circular buffer */
  unsigned char	   *buffer_get_ptr;  /* get pointer used by reader */
  unsigned char	   *buffer_put_ptr;  /* put pointer used by writer */
  long              buffer_count; /* number of bytes in the buffer */

  unsigned char     packet_buffer[65536];

  int               last_input_error;
  int               input_eof;

  pthread_t         reader_thread;

  int		    curpos;
  int               rtp_running;

  char              preview[MAX_PREVIEW_SIZE];
  int               preview_size;
  int               preview_read_done; /* boolean true after attempt to read input stream for preview */

  nbc_t		   *nbc;

  pthread_mutex_t   buffer_ring_mut;
  pthread_cond_t    writer_cond;
  pthread_cond_t    reader_cond;
} rtp_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

} rtp_input_class_t;


/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */

/*
 *
 */
static int host_connect_attempt(struct in_addr ia, int port,
				const char *interface,
				xine_t *xine) {
  int s = xine_socket_cloexec(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  union {
    struct sockaddr_in in;
    struct sockaddr sa;
  } saddr;
  int optval;
  int multicast = 0;  /* boolean, assume unicast */

  if(s == -1) {
    LOG_MSG(xine, _("xine_socket_cloexec(): %s.\n"), strerror(errno));
    return -1;
  }

  saddr.in.sin_family = AF_INET;
  saddr.in.sin_addr   = ia;
  saddr.in.sin_port   = htons(port);

  /* Is it a multicast address? */
  if ((ntohl(saddr.in.sin_addr.s_addr) >> 28) == 0xe) {
    LOG_MSG(xine, _("IP address specified is multicast\n"));
    multicast = 1;  /* boolean true */
  }


  /* Try to increase receive buffer to 1MB to avoid dropping packets */
  optval = BUFFER_SIZE;
  if ((setsockopt(s, SOL_SOCKET, SO_RCVBUF,
		  &optval, sizeof(optval))) < 0) {
    LOG_MSG(xine, _("setsockopt(SO_RCVBUF): %s.\n"), strerror(errno));
    return -1;
  }

  /* If multicast we allow multiple readers to open the same address */
  if (multicast) {
    if ((setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		    &saddr.in, sizeof(saddr.in))) < 0) {
      LOG_MSG(xine, _("setsockopt(SO_REUSEADDR): %s.\n"), strerror(errno));
      return -1;
    }
  }

  /* datagram socket */
  if (bind(s, &saddr.sa, sizeof(saddr.in))) {
    LOG_MSG(xine, _("bind(): %s.\n"), strerror(errno));
    return -1;
  }

  /* multicast ? */
  if (multicast) {

    struct ip_mreq mreq;
    struct ifreq ifreq;

    /* If the user specified an adapter we have to
     * look up the interface address to use it.
     * Ref: UNIX Network Programming 2nd edition
     * Section 19.6
     * W. Richard Stevens
     */

    if (interface != NULL) {
      strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
      if (ioctl(s, SIOCGIFADDR, &ifreq) < 0) {
	LOG_MSG(xine, _("Can't find address for iface %s:%s\n"),
		interface, strerror(errno));
	interface = NULL;
      }
    }

    /* struct ip_mreq mreq; */
    mreq.imr_multiaddr.s_addr = saddr.in.sin_addr.s_addr;
    if (interface == NULL) {
      mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    }
    else {
      memcpy(&mreq.imr_interface,
	     &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr,
	     sizeof(struct in_addr));
    }

    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))) {
      LOG_MSG(xine, _("setsockopt(IP_ADD_MEMBERSHIP) failed (multicast kernel?): %s.\n"),
	      strerror(errno));
      return -1;
    }
  }

  return s;
}

/*
 *
 */
static int host_connect(const char *host, int port,
			const char *interface,
			xine_t *xine)
{
  struct hostent *h;
  int i;
  int s;

  h=gethostbyname(host);
  if(h==NULL) {
    LOG_MSG(xine, _("unable to resolve '%s'.\n"), host);
    return -1;
  }

  for(i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy(&ia, h->h_addr_list[i],4);
    s = host_connect_attempt(ia, port, interface, xine);
    if (s != -1) return s;
  }
  LOG_MSG(xine, _("unable to bind to '%s'.\n"), host);
  return -1;
}

/*
 *
 */
static void * input_plugin_read_loop(void *arg) {

  rtp_input_plugin_t *this  = (rtp_input_plugin_t *) arg;
  unsigned char *data;
  long length;
  fd_set read_fds;

  while (1) {

    /* System calls are not a thread cancellation point in Linux
     * pthreads.  However, the RT signal sent to cancel the thread
     * will cause recv() to return with EINTR, and we can manually
     * check cancellation.
     */

    pthread_testcancel();
    {
    struct timeval recv_timeout;
    int rc;

    recv_timeout.tv_sec = 2;
    recv_timeout.tv_usec = 0;

    FD_ZERO( &read_fds );
    FD_SET( this->fh, &read_fds );

    /* wait for a packet to arrive - but do not hang! */
    rc = select( this->fh+1, &read_fds, NULL, NULL, &recv_timeout );
    if( rc > 0 )
    {
        length = recv(this->fh, this->packet_buffer,
                    sizeof(this->packet_buffer), 0);
    }
    else if( rc == 0 )
        length = 0;
    else
        length = -1;
    }
    pthread_testcancel();

    if (length < 0) {
      if (errno != EINTR) {
	LOG_MSG(this->stream->xine, _("recv(): %s.\n"), strerror(errno));
	return NULL;
      }
    }
    else {
      data = this->packet_buffer;

      if (this->is_rtp) {
	int pad, ext;
	int csrc;

	/* Do minimal RTP parsing to extract payload.  See
	 * http://www.faqs.org/rfcs/rfc1889.html for header format.
	 *
	 * WARNING: wholly untested code.  I don't have any RTP sender.
	 */

	if (length < 12) continue;

	pad = data[0] & 0x20;
	ext = data[0] & 0x10;
	csrc = data[0] & 0x0f;

	data += 12 + csrc * 4;
	length -= 12 + csrc * 4;

	if (ext) {
	  long hlen;

	  if (length < 4) continue;

	  hlen = (data[2] << 8) | data[3];
	  data += hlen;
	  length -= hlen;
	}

	if (pad) {
	  if (length < 1)
	    continue;

	  /* FIXME: is the pad length byte included in the
	   * length value or not?  We assume it is not.
	   */
	  length -= data[length - 1] + 1;
	}
      }

      /* insert data into cyclic buffer */
      if (length > 0) {

	/*
	 * if the buffer is full, wait for the reader
	 * to signal
	 */

        pthread_mutex_lock(&this->buffer_ring_mut);
        /* wait for enough space to write the whole of the recv'ed data */
	while( (BUFFER_SIZE - this->buffer_count) < length )
        {
          struct timeval tv;
          struct timespec timeout;

          gettimeofday(&tv, NULL);

          timeout.tv_nsec = tv.tv_usec * 1000;
          timeout.tv_sec = tv.tv_sec + 2;

          if( pthread_cond_timedwait(&this->writer_cond, &this->buffer_ring_mut, &timeout) != 0 )
          {
            fprintf( stdout, "input_rtp: buffer ring not read within 2 seconds!\n" );
          }
        }

	/* Now there's enough space to write some bytes into the buffer
	 * determine how many bytes can be written. If the buffer wraps
	 * around, write in two pieces: from the head pointer to the
	 * end of the buffer and from the base to the remaining number
	 * of bytes.
	 */

        {
        long buffer_space_remaining = BUFFER_SIZE - (this->buffer_put_ptr - this->buffer);

	if( buffer_space_remaining >= length )
        {
            /* data fits inside the buffer */
            memcpy(this->buffer_put_ptr, data, length);
            this->buffer_put_ptr += length;
    }
	else
        {
            /* data wrapped around the end of the buffer */
            memcpy(this->buffer_put_ptr, data, buffer_space_remaining);
            memcpy(this->buffer, &data[buffer_space_remaining], length - buffer_space_remaining);
            this->buffer_put_ptr = &this->buffer[ length - buffer_space_remaining ];
        }
        }

	this->buffer_count += length;

	/* signal the reader that there is new data	*/
	pthread_cond_signal(&this->reader_cond);
	pthread_mutex_unlock(&this->buffer_ring_mut);
      }
    }
  }
}

/* ***************************************************************** */
/*                         END OF PRIVATES                           */
/* ***************************************************************** */

static off_t rtp_plugin_read (input_plugin_t *this_gen,
			      void *buf_gen, off_t length) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;

  struct timeval tv;
  struct timespec timeout;
  off_t copied = 0;

  if (length < 0)
    return -1;

  while(length > 0) {

    off_t n;

    pthread_mutex_lock(&this->buffer_ring_mut);

    /*
     * if nothing in the buffer, wait for data for 5 seconds. If
     * no data is received within this timeout, return the number
     * of bytes already received (which is likely to be 0)
     */

    if(this->buffer_count == 0) {
      gettimeofday(&tv, NULL);
      timeout.tv_nsec = tv.tv_usec * 1000;
      timeout.tv_sec = tv.tv_sec + 5;

      if(pthread_cond_timedwait(&this->reader_cond, &this->buffer_ring_mut, &timeout) != 0)
	{
	  /* we timed out, no data available */
	  pthread_mutex_unlock(&this->buffer_ring_mut);
	  return copied;
	}
    }

    /* Now determine how many bytes can be read. If the buffer
     * will wrap the buffer is read in two pieces, first read
     * to the end of the buffer, wrap the tail pointer and
     * update the buffer count. Finally read the second piece
     * from the base to the remaining count
     */
    if(length  > this->buffer_count) {
      n = this->buffer_count;
    }
    else {
      n = length;
    }

    if(((this->buffer_get_ptr - this->buffer) + n) > BUFFER_SIZE) {
      n = BUFFER_SIZE - (this->buffer_get_ptr - this->buffer);
    }

    /* the actual read */
    memcpy(buf, this->buffer_get_ptr, n);

    buf += n;
    copied += n;
    length -= n;

    /* update the tail pointer, watch for wrap arounds */
    this->buffer_get_ptr += n;
    if(this->buffer_get_ptr - this->buffer >= BUFFER_SIZE)
      this->buffer_get_ptr = this->buffer;

    this->buffer_count -= n;

    /* signal the writer that there's space in the buffer again */
    pthread_cond_signal(&this->writer_cond);
    pthread_mutex_unlock(&this->buffer_ring_mut);
  }

  this->curpos += copied;

  return copied;
}

static buf_element_t *rtp_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t todo) {
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int                   total_bytes;

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type    = BUF_DEMUX_BLOCK;

  total_bytes = rtp_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

/*
 *
 */
static off_t rtp_plugin_seek (input_plugin_t *this_gen,
			      off_t offset, int origin) {

  return -1;
}

/*
 *
 */

static off_t rtp_plugin_get_length (input_plugin_t *this_gen) {
  return -1;
}

/*
 *
 */
static off_t rtp_plugin_get_current_pos (input_plugin_t *this_gen){
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  return this->curpos;
}

/*
 *
 */
static uint32_t rtp_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

/*
 *
 */
static uint32_t rtp_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static const char* rtp_plugin_get_mrl (input_plugin_t *this_gen) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static int rtp_plugin_get_optional_data (input_plugin_t *this_gen,
					 void *data, int data_type) {

  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  /* Since this input plugin deals with stream data, we
   * are not going to worry about retaining the data packet
   * retrieved for review purposes. Hence, the first non-preview
   * packet read made will return the 2nd packet from the UDP/RTP stream.
   * The first packet is only used for the preview.
   */

  if (data_type == INPUT_OPTIONAL_DATA_PREVIEW) {
    if (!this->preview_read_done) {
      this->preview_size = rtp_plugin_read(this_gen, this->preview, MAX_PREVIEW_SIZE);
      if (this->preview_size < 0)
	this->preview_size = 0;
      lprintf("Preview data length = %d\n", this->preview_size);

      this->preview_read_done = 1;
    }
    if (this->preview_size)
      memcpy(data, this->preview, this->preview_size);
    return this->preview_size;
  }
  else {
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
}

static void rtp_plugin_dispose (input_plugin_t *this_gen ) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  if (this->nbc) nbc_close(this->nbc);

  if (this->rtp_running) {
    LOG_MSG(this->stream->xine, _("RTP: stopping reading thread...\n"));
    pthread_cancel(this->reader_thread);
    pthread_join(this->reader_thread, NULL);
    LOG_MSG(this->stream->xine, _("RTP: reading thread terminated\n"));
  }

  if (this->fh != -1) close(this->fh);

  free(this->buffer);
  free(this->mrl);
  free(this);
}

static int rtp_plugin_open (input_plugin_t *this_gen ) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  int                 err;

  LOG_MSG(this->stream->xine,
	  _("Opening >filename:%s port:%d interface:%s<\n"),
	  this->filename,
	  this->port,
	  this->interface);

  this->fh = host_connect(this->filename, this->port,
			  this->interface, this->stream->xine);

  if (this->fh == -1) return 0;

  this->last_input_error = 0;
  this->input_eof = 0;
  this->curpos = 0;
  this->rtp_running = 1;

  if ((err = pthread_create(&this->reader_thread, NULL,
		            input_plugin_read_loop, (void *)this)) != 0) {
    LOG_MSG(this->stream->xine, _("input_rtp: can't create new thread (%s)\n"), strerror(err));
    _x_abort();
  }

  return 1;
}

static input_plugin_t *rtp_class_get_instance (input_class_t *cls_gen,
					       xine_stream_t *stream,
					       const char *data) {
  rtp_input_plugin_t *this;
  char               *filename = NULL;
  char               *pptr;
  char               *iptr;
  char               *mrl;
  int                 is_rtp = 0;
  int                 port = 7658;


  mrl = strdup(data);
  if (!strncasecmp (mrl, "rtp://", 6)) {
    filename = &mrl[6];
    is_rtp = 1;
  }
  else if (!strncasecmp (mrl, "udp://", 6)) {
    filename = &mrl[6];
    is_rtp = 0;
  }

  if (filename == NULL || strlen(filename) == 0) {
    free(mrl);
    return NULL;
  }

  /* Locate the port number */
  pptr=strchr(filename, ':');
  iptr = NULL;
  if (pptr) {
    *pptr++ = '\0';
    sscanf(pptr, "%d", &port);

    /* Locate the interface name for multicast IP, eth0, eth1 etc
     * The mrl will be udp://<address>:<port>?iface=eth0
     */

    if (*pptr != '\0') {
      if ( (pptr=strstr(pptr, "?iface=")) != NULL) {
	pptr += 7;
	if (*pptr != '\0') {
	  iptr = pptr;  // Ok ... user defined an interface
	}
      }
    }
  }

  this = calloc(1, sizeof(rtp_input_plugin_t));
  this->stream       = stream;
  this->mrl          = mrl;
  this->filename     = filename;
  this->port         = port;
  this->is_rtp       = is_rtp;
  this->fh           = -1;
  this->rtp_running  = 0;
  this->preview_size = 0;
  this->interface = NULL;

  if (iptr) this->interface = iptr;

  pthread_mutex_init(&this->buffer_ring_mut, NULL);

  pthread_cond_init(&this->reader_cond, NULL);
  pthread_cond_init(&this->writer_cond, NULL);

  this->buffer = malloc(BUFFER_SIZE);
  this->buffer_put_ptr = this->buffer;
  this->buffer_get_ptr = this->buffer;
  this->buffer_count = 0;
  this->curpos = 0;

  this->input_plugin.open              = rtp_plugin_open;
  this->input_plugin.get_capabilities  = rtp_plugin_get_capabilities;
  this->input_plugin.read              = rtp_plugin_read;
  this->input_plugin.read_block        = rtp_plugin_read_block;
  this->input_plugin.seek              = rtp_plugin_seek;
  this->input_plugin.get_current_pos   = rtp_plugin_get_current_pos;
  this->input_plugin.get_length        = rtp_plugin_get_length;
  this->input_plugin.get_blocksize     = rtp_plugin_get_blocksize;
  this->input_plugin.get_mrl           = rtp_plugin_get_mrl;
  this->input_plugin.get_optional_data = rtp_plugin_get_optional_data;
  this->input_plugin.dispose           = rtp_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  this->nbc = NULL;
  this->nbc = nbc_init(this->stream);

  return &this->input_plugin;
}


/*
 *  net plugin class
 */
static void *init_class (xine_t *xine, void *data) {

  rtp_input_class_t  *this;


  this         = calloc(1, sizeof(rtp_input_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->input_class.get_instance      = rtp_class_get_instance;
  this->input_class.description       = N_("RTP and UDP input plugin as shipped with xine");
  this->input_class.identifier        = "RTP/UDP";
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
  { PLUGIN_INPUT, 18, "rtp", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

