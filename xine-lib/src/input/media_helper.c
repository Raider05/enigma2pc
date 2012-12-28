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
 */

/* Standard includes */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/wait.h>
#include <sys/ioctl.h>
#endif

#include <unistd.h>
#include <string.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD_kernel__)
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
#elif WIN32
#else
#warning "This might not compile due to missing cdrom ioctls"
#endif

#include "media_helper.h"

#define LOG_MEDIA_EJECT

#ifndef WIN32
static int media_umount_media(const char *device)
{
  pid_t pid;
  int status;

  pid=fork();
  if (pid == 0) {
    execl("/bin/umount", "umount", device, NULL);
    exit(127);
  }
  do {
    if(waitpid(pid, &status, 0) == -1) {
      if (errno != EINTR)
	return -1;
    }
    else {
      return WEXITSTATUS(status);
    }
  } while(1);

  return -1;
}
#endif

int media_eject_media (xine_t *xine, const char *device)
{
#if defined (__sun)

  if (fork() == 0) {
    execl("/usr/bin/eject", "eject", device, NULL);
    _exit(EXIT_FAILURE);
  }

  return 1;

#elif defined (WIN32)

  return 0;

#else

  int fd;

  /* printf("input_dvd: Eject Device %s current device %s opened=%d handle=%p trying...\n",device, this->current_dvd_device, this->opened, this->dvdnav); */
  media_umount_media(device);
  /* printf("input_dvd: umount result: %s\n", strerror(errno)); */

  if ((fd = xine_open_cloexec(device, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (__linux__)
    int ret, status;

    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
        if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
#ifdef LOG_MEDIA_EJECT
          printf("input_dvd: CDROMCLOSETRAY failed: %s\n", strerror(errno));
#endif
        }
        break;
      case CDS_DISC_OK:
        if((ret = ioctl(fd, CDROMEJECT)) != 0) {
#ifdef LOG_MEDIA_EJECT
          printf("input_dvd: CDROMEJECT failed: %s\n", strerror(errno));
#endif
        }
        break;
      }
    }
    else {
#ifdef LOG_MEDIA_EJECT
      printf("input_dvd: CDROM_DRIVE_STATUS failed: %s\n", strerror(errno));
#endif
      close(fd);
      return 0;
    }

#elif defined (__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD_kernel__)

    if (ioctl(fd, CDIOCALLOW) == -1) {
      xprintf(xine, XINE_VERBOSITY_DEBUG, "ioctl(cdromallow): %s\n", strerror(errno));
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
        xprintf(xine, XINE_VERBOSITY_DEBUG, "ioctl(cdromeject): %s\n", strerror(errno));
      }
    }

#endif

    close(fd);
  }
  else {
    xprintf(xine, XINE_VERBOSITY_LOG, _("input_dvd: Device %s failed to open during eject calls\n"), device);
  }

  return 1;

#endif
}

