/*
 * CDDA / DVD server
 *
 * Copyright (C) 2003-2007 the xine project
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
 *
 * This is a TCP server that can be used with xine's cdda input plugin to
 * play audio CDs over the network. It also supports playing DVDs with a
 * patched version of libdvdcss.
 *
 * quick howto:
 * - compile it:
 *   gcc -o cdda_server cdda_server.c -ldl
 *
 * - start the server:
 *   ./cdda_server /dev/cdrom 3000
 *
 * - start the client:
 *   xine cdda://server:3000/1
 *
 * to play the entire cd (using GUI's "CD" button) just change
 * media.audio_cd.device to the server's mrl.
 *
 * 6 May 2003 - Miguel Freitas
 * This feature was sponsored by 1Control
 *
 * note: see also libdvdcss-1.2.6-network.patch
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dlfcn.h>

#define QLEN 5    /* maximum connection queue length */
#define _BUFSIZ 300

/* CD-relevant defines and data structures */
#define CD_SECONDS_PER_MINUTE   60
#define CD_FRAMES_PER_SECOND    75
#define CD_RAW_FRAME_SIZE       2352
#define CD_LEADOUT_TRACK        0xAA
#define DVD_BLOCK_SIZE          2048

/* functions from external DVD lib */
typedef struct dvd_s *dvd_handle;
static dvd_handle    (*dvd_open)  (const char *);
static int           (*dvd_close) (dvd_handle);
static int           (*dvd_seek)  (dvd_handle, int, int);
static int           (*dvd_title) (dvd_handle, int);
static int           (*dvd_read)  (dvd_handle, void *, int, int);
static char *        (*dvd_error) (dvd_handle);

static int            dvd_support;
static int            msock;
static int            cdda_fd;
static dvd_handle     dvd;
static char          *cdrom_device;


#if defined (__linux__)

#include <linux/cdrom.h>

static int read_cdrom_toc_header(int fd, int *first_track, int *last_track) {

  struct cdrom_tochdr tochdr;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return -1;
  }

  *first_track = tochdr.cdth_trk0;
  *last_track = tochdr.cdth_trk1;
  return 0;
}

static int read_cdrom_toc_entry(int fd, int track, int *track_mode,
    int *first_frame_minute, int *first_frame_second, int *first_frame_frame ) {

  struct cdrom_tocentry tocentry;

  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = track;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    return -1;
  }

  *track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  *first_frame_minute = tocentry.cdte_addr.msf.minute;
  *first_frame_second = tocentry.cdte_addr.msf.second;
  *first_frame_frame = tocentry.cdte_addr.msf.frame;

  return 0;
}

static int read_cdrom_frames(int fd, int frame, int num_frames,
  unsigned char *data) {

  struct cdrom_msf msf;

  while( num_frames ) {
    /* read from starting frame... */
    msf.cdmsf_min0 = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec0 = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame0 = frame % CD_FRAMES_PER_SECOND;

    /* read until ending track (starting frame + 1)... */
    msf.cdmsf_min1 = (frame + 1) / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec1 = ((frame + 1) / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame1 = (frame + 1) % CD_FRAMES_PER_SECOND;

    /* MSF structure is the input to the ioctl */
    memcpy(data, &msf, sizeof(msf));

    /* read a frame */
    if(ioctl(fd, CDROMREADRAW, data, data) < 0) {
      perror("CDROMREADRAW");
      return -1;
    }
    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#endif

/* network functions */

static int sock_has_data(int socket){

  fd_set readfds;
  fd_set writefds;
  fd_set exceptfds;
  struct timeval tv;

  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);
  FD_SET(socket, &readfds);

  return (select(socket+1, &readfds, &writefds, &exceptfds, &tv) > 0);
}

static int sock_check_opened(int socket) {
  fd_set   readfds, writefds, exceptfds;
  int      retval;
  struct   timeval timeout;

  for(;;) {
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(socket, &exceptfds);

    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    retval = select(socket + 1, &readfds, &writefds, &exceptfds, &timeout);

    if(retval == -1 && (errno != EAGAIN && errno != EINTR))
      return 0;

    if (retval != -1)
      return 1;
  }

  return 0;
}

#if 0
/*
 * read binary data from socket
 */
static int sock_data_read (int socket, char *buf, int nlen) {
  int n, num_bytes;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  num_bytes = 0;

  while (num_bytes < nlen) {

    n = read (socket, &buf[num_bytes], nlen - num_bytes);

    /* read errors */
    if (n < 0) {
      if(errno == EAGAIN) {
        fd_set rset;
        struct timeval timeout;

        FD_ZERO (&rset);
        FD_SET  (socket, &rset);

        timeout.tv_sec  = 30;
        timeout.tv_usec = 0;

        if (select (socket+1, &rset, NULL, NULL, &timeout) <= 0) {
          printf ("network: timeout on read\n");
          return 0;
        }
        continue;
      }
      printf ("network: read error %d\n", errno);
      return 0;
    }

    num_bytes += n;

    /* end of stream */
    if (!n) break;
  }

  return num_bytes;
}
#endif

/*
 * read a line (\n-terminated) from socket
 */
static int sock_string_read(int socket, char *buf, int len) {
  char    *pbuf;
  int      r, rr;
  void    *nl;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  if (--len < 1)
    return(-1);

  pbuf = buf;

  do {

    if((r = recv(socket, pbuf, len, MSG_PEEK)) <= 0)
      return -1;

    if((nl = memchr(pbuf, '\n', r)) != NULL)
      r = ((char *) nl) - pbuf + 1;

    if((rr = read(socket, pbuf, r)) < 0)
      return -1;

    pbuf += rr;
    len -= rr;

  } while((nl == NULL) && len);

  if (pbuf > buf && *(pbuf-1) == '\n'){
    *(pbuf-1) = '\0';
  }
  *pbuf = '\0';
  return (pbuf - buf);
}

/*
 * Write to socket.
 */
static int sock_data_write(int socket, char *buf, int len) {
  ssize_t  size;
  int      wlen = 0;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  while(len) {
    size = write(socket, buf, len);

    if(size <= 0) {
      printf("error writing to socket %d\n",socket);
      return -1;
    }

    len -= size;
    wlen += size;
    buf += size;
  }

  return wlen;
}

int sock_string_write(int socket, char *msg, ...) {
  char     buf[_BUFSIZ];
  va_list  args;

  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ - 1, msg, args);
  va_end(args);

  /* Each line sent is '\n' terminated */
  if((buf[strlen(buf)] == '\0') && (buf[strlen(buf) - 1] != '\n'))
      strcat(buf, "\n");

  return sock_data_write(socket, buf, strlen(buf));
}


/**
 * Setup dvd read functions
 */
int dvdinput_setup(void)
{
  void *dvdcss_library = NULL;
  char **dvdcss_version = NULL;

  /* dlopening libdvdcss */

#ifdef HOST_OS_DARWIN
  dvdcss_library = dlopen("libdvdcss.2.dylib", RTLD_LAZY);
#elif defined(WIN32)
  dvdcss_library = dlopen("libdvdcss.dll", RTLD_LAZY);
#else
  dvdcss_library = dlopen("libdvdcss.so.2", RTLD_LAZY);
#endif

  if(dvdcss_library != NULL) {
#if defined(__OpenBSD__) && !defined(__ELF__)
#define U_S "_"
#else
#define U_S
#endif
    dvd_open = (dvd_handle (*)(const char*))
      dlsym(dvdcss_library, U_S "dvdcss_open");
    dvd_close = (int (*)(dvd_handle))
      dlsym(dvdcss_library, U_S "dvdcss_close");
    dvd_title = (int (*)(dvd_handle, int))
      dlsym(dvdcss_library, U_S "dvdcss_title");
    dvd_seek = (int (*)(dvd_handle, int, int))
      dlsym(dvdcss_library, U_S "dvdcss_seek");
    dvd_read = (int (*)(dvd_handle, void*, int, int))
      dlsym(dvdcss_library, U_S "dvdcss_read");
    dvd_error = (char* (*)(dvd_handle))
      dlsym(dvdcss_library, U_S "dvdcss_error");

    dvdcss_version = (char **)dlsym(dvdcss_library, U_S "dvdcss_interface_2");

    if(dlsym(dvdcss_library, U_S "dvdcss_crack")) {
      fprintf(stderr,
	      "libdvdread: Old (pre-0.0.2) version of libdvdcss found.\n"
	      "libdvdread: You should get the latest version from "
	      "http://www.videolan.org/\n" );
      dlclose(dvdcss_library);
      dvdcss_library = NULL;
    } else if(!dvd_open  || !dvd_close || !dvd_title || !dvd_seek
	      || !dvd_read || !dvd_error || !dvdcss_version) {
      fprintf(stderr,  "libdvdread: Missing symbols in libdvdcss, "
	      "this shouldn't happen !\n");
      dlclose(dvdcss_library);
    }
  }

  if(dvdcss_library != NULL) {
    printf("Using libdvdcss version %s for DVD access\n",
            *dvdcss_version);

    return 1;

  } else {
    printf("No libdvdcss: DVD support unavailable.\n");

    return 0;
  }
}


#define CMD_CDDA_OPEN       "cdda_open"
#define CMD_CDDA_READ       "cdda_read"
#define CMD_CDDA_TOCHDR     "cdda_tochdr"
#define CMD_CDDA_TOCENTRY   "cdda_tocentry"
#define CMD_DVD_OPEN        "dvd_open"
#define CMD_DVD_ERROR       "dvd_error"
#define CMD_DVD_SEEK        "dvd_seek"
#define CMD_DVD_READ        "dvd_read"
#define CMD_DVD_TITLE       "dvd_title"

static int process_commands( int socket )
{
  char     cmd[_BUFSIZ];
  int      start_frame, num_frames, i;
  int      blocks, flags;
  int      ret, n;

  while( sock_has_data(socket) )
  {
    if( sock_string_read(socket, cmd, _BUFSIZ) <= 0 )
         return -1;

    if( !strncmp(cmd, CMD_CDDA_OPEN, strlen(CMD_CDDA_OPEN)) ) {

      if( cdda_fd != -1 )
        close(cdda_fd);

      cdda_fd = open ( cdrom_device, O_RDONLY);
      if( cdda_fd == -1 )
      {
        printf( "argh ! couldn't open CD (%s)\n", cdrom_device );
        if( sock_string_write(socket,"-1 0") < 0 )
          return -1;
      }
      else {
        if( sock_string_write(socket,"0 0") < 0 )
          return -1;
      }
      continue;
    }

    if( dvd_support && !strncmp(cmd, CMD_DVD_OPEN, strlen(CMD_DVD_OPEN)) ) {

      if( dvd != NULL )
        dvd_close(dvd);

      dvd = dvd_open ( cdrom_device );
      if( !dvd )
      {
        printf( "argh ! couldn't open DVD (%s)\n", cdrom_device );
        if( sock_string_write(socket,"-1 0") < 0 )
          return -1;
      }
      else {
        if( sock_string_write(socket,"0 0") < 0 )
          return -1;
      }
      continue;
    }

    if( cdda_fd != -1 ) {

      if( !strncmp(cmd, CMD_CDDA_READ, strlen(CMD_CDDA_READ)) ) {
        char *buf;

        sscanf(cmd,"%*s %d %d", &start_frame, &num_frames);

        if (num_frames > INT_MAX / CD_RAW_FRAME_SIZE)
        {
          printf ("fatal error: integer overflow\n");
          exit (1);
        }

        n = num_frames * CD_RAW_FRAME_SIZE;
        buf = malloc( n );
        if( !buf )
        {
          printf("fatal error: could not allocate memory\n");
          exit(1);
        }

        ret = read_cdrom_frames(cdda_fd, start_frame, num_frames, buf );

        if( sock_string_write(socket,"%d %d", ret, n) < 0 )
          return -1;

        if( sock_data_write(socket,buf,n) < 0 )
          return -1;

        free(buf);
        continue;
      }

      if( !strncmp(cmd, CMD_CDDA_TOCHDR, strlen(CMD_CDDA_TOCHDR)) ) {
        int first_track, last_track;

        ret = read_cdrom_toc_header(cdda_fd, &first_track, &last_track);

        if( sock_string_write(socket,"%d 0 %d %d", ret, first_track, last_track) < 0 )
          return -1;

        continue;
      }

      if( !strncmp(cmd, CMD_CDDA_TOCENTRY, strlen(CMD_CDDA_TOCENTRY)) ) {
        int track_mode, first_frame_minute, first_frame_second, first_frame_frame;

        sscanf(cmd,"%*s %d", &i);

        ret = read_cdrom_toc_entry(cdda_fd, i, &track_mode,
               &first_frame_minute, &first_frame_second, &first_frame_frame );

        if( sock_string_write(socket,"%d 0 %d %d %d %d", ret,
             track_mode, first_frame_minute, first_frame_second, first_frame_frame) < 0 )
          return -1;

        continue;
      }

    } else if ( dvd != NULL ) {

      if( !strncmp(cmd, CMD_DVD_ERROR, strlen(CMD_DVD_ERROR)) ) {
        char *errmsg = dvd_error( dvd );

        n = strlen(errmsg)+1;
        if( sock_string_write(socket,"0 %d", n) < 0 )
          return -1;

        if( sock_data_write(socket,errmsg,n) < 0 )
          return -1;

        continue;
      }

      if( !strncmp(cmd, CMD_DVD_SEEK, strlen(CMD_DVD_SEEK)) ) {

        sscanf(cmd,"%*s %d %d", &blocks, &flags);
        ret = dvd_seek(dvd, blocks, flags);

        if( sock_string_write(socket,"%d 0", ret) < 0 )
          return -1;

        continue;
      }

      if( !strncmp(cmd, CMD_DVD_READ, strlen(CMD_DVD_READ)) ) {
        char *buf;

        sscanf(cmd,"%*s %d %d", &blocks, &flags);
        if (blocks > INT_MAX / DVD_BLOCK_SIZE)
        {
          printf ("fatal error: integer overflow\n");
          exit (1);
        }

        n = blocks * DVD_BLOCK_SIZE;
        buf = malloc( n );
        if( !buf )
        {
          printf("fatal error: could not allocate memory\n");
          exit(1);
        }
        ret = dvd_read(dvd, buf, blocks, flags);
        if( sock_string_write(socket,"%d %d", ret, n) < 0 )
          return -1;

        if( sock_data_write(socket,buf,n) < 0 )
          return -1;

        free(buf);
        continue;
      }

      if( !strncmp(cmd, CMD_DVD_TITLE, strlen(CMD_DVD_TITLE)) ) {

        sscanf(cmd,"%*s %d", &blocks);

        ret = dvd_title(dvd, blocks);

        if( sock_string_write(socket,"%d 0", ret) < 0 )
          return -1;

        continue;
      }

    }

    /* no device open, or unknown command. return error */
    if( sock_string_write(socket,"-1 0") < 0 )
      return -1;
  }
  return 0;
}

static void server_loop()
{
  struct sockaddr_in fsin; /* the from address of a client */
  int  alen;                /*  from-address length */
  int fd, nfds;
  fd_set  rfds;          /* read file descriptor set */
  fd_set  afds;          /* active file descriptor set */

  /* SIGPIPE when connection closed during write call */
  signal( SIGPIPE, SIG_IGN );

  nfds = getdtablesize();

  FD_ZERO(&afds);

  FD_SET(msock, &afds);

  while (1)
  {
    memcpy( &rfds, &afds, sizeof(rfds) );

    if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0)
      continue; /* erro?? */

    if (FD_ISSET(msock, &rfds))
    {
      int   ssock;
      alen = sizeof(fsin);

      ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
      if (ssock < 0)
        continue; /* erro?? */

      FD_SET(ssock, &afds);
      printf("new connection socket %d\n", ssock);
    }

    for (fd=0; fd<nfds; ++fd)
    {
      if  (fd  != msock && FD_ISSET(fd, &rfds))
        if (process_commands(fd) < 0)
        {
          printf("closing socket %d\n", fd);
          close(fd);
          FD_CLR(fd, &afds);

          if( cdda_fd != -1 ) {
            close(cdda_fd);
            cdda_fd = -1;
          }

          if( dvd != NULL ) {
            dvd_close(dvd);
            dvd = NULL;
          }
        }
    }
  }
}


int main( int argc, char *argv[] )
{
  unsigned int   port;
  struct sockaddr_in servAddr;

  /* Print version number */
  printf( "CDDA / DVD tcp server v0.1\n" );
  dvd_support = dvdinput_setup();

  /* Check for 2 arguments */
  if( argc != 3 )
  {
    printf( "usage: %s <device> <server_port>\n", argv[0] );
    return -1;
  }

  port = atoi( argv[2] );

  cdda_fd = -1;
  dvd = NULL;
  cdrom_device = argv[1];

  msock = socket(PF_INET, SOCK_STREAM, 0);
  if( msock < 0 )
  {
    printf("error opening master socket.\n");
    return 0;
  }
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servAddr.sin_port = htons(port);

  if(bind(msock, (struct sockaddr *) &servAddr, sizeof(servAddr))<0)
  {
    printf("bind port %d error\n", port);
    return 0;
  }

  listen(msock,QLEN);

  printf("listening on port %d...\n", port);

  server_loop();

  close(msock);

  return 0;
}

