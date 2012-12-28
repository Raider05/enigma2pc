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
* Xine Health Check:
*
* Overview: Checking the setup of the user's system is the task of
* xine-check.sh for now. At present this is intended to replace
* xine_check to provide a more robust way of informing users new
* to xine of the setup of their system.
*
* Interface: The function xine_health_check is the starting point
* to check the user's system. It is expected that the values for
* hc->cdrom_dev and hc->dvd_dev will be defined. For example,
* hc->cdrom_dev = /dev/cdrom and hc->/dev/dvd. If at any point a
* step fails the function returns with a failed status,
* XINE_HEALTH_CHECK_FAIL, and an error message contained in hc->msg.
*
* Author: Stephen Torri <storri@users.sourceforge.net>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <xine/xineutils.h>

#ifndef O_CLOEXEC
#  define O_CLOEXEC  0
#endif

#if defined(__linux__)

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/major.h>
#include <linux/hdreg.h>

#ifdef HAVE_X11
#  include <X11/Xlib.h>
#  ifdef HAVE_XV
#    include <X11/extensions/Xvlib.h>
#  endif
#endif

#ifndef SCSI_BLK_MAJOR
#define SCSI_BLK_MAJOR(m) \
  (((m) == SCSI_DISK0_MAJOR || \
  ((m) >= SCSI_DISK1_MAJOR && (m) <= SCSI_DISK7_MAJOR)) || \
  ((m) == SCSI_CDROM_MAJOR))
#endif

#endif  /* !__linux__ */

static void XINE_FORMAT_PRINTF(3, 4)
set_hc_result(xine_health_check_t* hc, int state, const char *format, ...)
{

  va_list   args;
  char     *buf = NULL;

  if (!hc) {
    printf ("xine_check: GASP, hc is NULL\n");
    _x_abort();
  }

  if (!format) {
    printf ("xine_check: GASP, format is NULL\n");
    _x_abort();
  }

  va_start(args, format);
  if (vasprintf (&buf, format, args) < 0)
    buf = NULL;
  va_end(args);

  if (!buf)
    _x_abort();

  hc->msg         = buf;
  hc->status      = state;
}

#if defined(__linux__)

static xine_health_check_t* _x_health_check_kernel (xine_health_check_t* hc) {
  struct utsname kernel;

  hc->title       = "Check for kernel version";
  hc->explanation = "Probably you're not running a Linux-Like system.";

  if (uname (&kernel) == 0) {
    fprintf (stdout,"  sysname: %s\n", kernel.sysname);
    fprintf (stdout,"  release: %s\n", kernel.release);
    fprintf (stdout,"  machine: %s\n", kernel.machine);
    hc->status = XINE_HEALTH_CHECK_OK;
  }
  else {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL,
		   "FAILED - Could not get kernel information.");
  }
  return hc;
}

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static xine_health_check_t* _x_health_check_mtrr (xine_health_check_t* hc) {
  FILE *fd;

  hc->title       = "Check for MTRR support";
  hc->explanation = "Make sure your kernel has MTRR support compiled in.";

  fd = fopen("/proc/mtrr", "r");
  if (!fd) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL,
		   "FAILED: mtrr is not enabled.");
  } else {
    hc->status = XINE_HEALTH_CHECK_OK;
    fclose (fd);
  }
  return hc;
}
#else
static xine_health_check_t* _x_health_check_mtrr (xine_health_check_t* hc) {

  hc->title       = "Check for MTRR support";
  hc->explanation = "Don't worry about this one";

  set_hc_result (hc, XINE_HEALTH_CHECK_OK,
		 "mtrr does not apply on this hw platform.");
  return hc;
}
#endif

static xine_health_check_t* _x_health_check_cdrom (xine_health_check_t* hc) {
  struct stat cdrom_st;
  int fd;

  hc->title       = "Check for CDROM drive";
  hc->explanation = "Either create a symbolic link /dev/cdrom pointing to "
                    "your cdrom device or set your cdrom device in the "
                    "preferences dialog.";

  if (stat (hc->cdrom_dev,&cdrom_st) < 0) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - could not access cdrom: %s\n", hc->cdrom_dev);
    return hc;
  }

  if ((cdrom_st.st_mode & S_IFMT) != S_IFBLK) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - %s is not a block device.\n", hc->cdrom_dev);
    return hc;
  }

  if ( (fd = open(hc->cdrom_dev, O_RDWR | O_CLOEXEC)) < 0) {
    switch (errno) {
    case EACCES:
      set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - %s permissions are not sufficient\n.", hc->cdrom_dev);
      return hc;
    case ENXIO:
    case ENODEV:
      set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - there is no device behind %s\n.", hc->cdrom_dev);
      return hc;
    }
  } else
    close(fd);

  hc->status = XINE_HEALTH_CHECK_OK;
  return hc;
}

static xine_health_check_t* _x_health_check_dvdrom(xine_health_check_t* hc) {
  struct stat dvdrom_st;
  int fd;

  hc->title       = "Check for DVD drive";
  hc->explanation = "Either create a symbolic link /dev/dvd pointing to "
                    "your cdrom device or set your cdrom device in the "
                    "preferences dialog.";

  if (stat (hc->dvd_dev,&dvdrom_st) < 0) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - could not access dvdrom: %s\n", hc->dvd_dev);
    return hc;
  }

  if ((dvdrom_st.st_mode & S_IFMT) != S_IFBLK) {
    set_hc_result(hc, XINE_HEALTH_CHECK_FAIL, "FAILED - %s is not a block device.\n", hc->dvd_dev);
    return hc;
  }

  if ( (fd = open(hc->dvd_dev, O_RDWR | O_CLOEXEC)) < 0) {
    switch (errno) {
    case EACCES:
      set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - %s permissions are not sufficient\n.", hc->dvd_dev);
      return hc;
    case ENXIO:
    case ENODEV:
      set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - there is no device behind %s\n.", hc->dvd_dev);
      return hc;
    }
  } else
    close(fd);

  hc->status = XINE_HEALTH_CHECK_OK;
  return hc;
}

static xine_health_check_t* _x_health_check_dma (xine_health_check_t* hc) {

  int         is_scsi_dev = 0;
  int         fd = 0;
  static long param = 0;
  struct stat st;

  hc->title       = "Check for DMA mode on DVD drive";
  hc->explanation = "If you are using the ide-cd module ensure\n"
                    "that you have the following entry in /etc/modules.conf:\n"
                    "options ide-cd dma=1\n Reload ide-cd module.\n"
	            "otherwise run hdparm -d 1 on your dvd-device.";

  /* If /dev/dvd points to /dev/scd0 but the drive is IDE (e.g. /dev/hdc)
   * and not scsi how do we detect the correct one */
  if (stat (hc->dvd_dev, &st)) {
    set_hc_result(hc, XINE_HEALTH_CHECK_FAIL, "FAILED - Could not read stats for %s.\n", hc->dvd_dev);
    return hc;
  }

  if (SCSI_BLK_MAJOR(major(st.st_rdev))) {
    is_scsi_dev = 1;
    set_hc_result(hc, XINE_HEALTH_CHECK_OK, "SKIPPED - Operation not supported on SCSI drives or drives that use the ide-scsi module.");
    return hc;
  }

  fd = open (hc->dvd_dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    set_hc_result(hc, XINE_HEALTH_CHECK_FAIL, "FAILED - Could not open %s.\n", hc->dvd_dev);
    return hc;
  }

  if (!is_scsi_dev) {

    if(ioctl (fd, HDIO_GET_DMA, &param)) {
      set_hc_result(hc, XINE_HEALTH_CHECK_FAIL,
		    "FAILED -  HDIO_GET_DMA failed. Ensure the permissions for %s are 0664.\n",
		    hc->dvd_dev);
      return hc;
    }

    if (param != 1) {
      set_hc_result(hc, XINE_HEALTH_CHECK_FAIL,
		    "FAILED - DMA not turned on for %s.",
		    hc->dvd_dev);
      return hc;
    }
  }
  close (fd);
  hc->status = XINE_HEALTH_CHECK_OK;
  return hc;
}


static xine_health_check_t* _x_health_check_x (xine_health_check_t* hc) {
  char* env_display = getenv("DISPLAY");

  hc->title       = "Check for X11 environment";
  hc->explanation = "Make sure you're running X11, if this is an ssh connection,\n"
                    "make sure you have X11 forwarding enabled (ssh -X ...)";

  if (strlen (env_display) == 0) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "FAILED - DISPLAY environment variable not set.");
  }
  else {
    hc->status = XINE_HEALTH_CHECK_OK;
  }
  return hc;
}

static xine_health_check_t* _x_health_check_xv (xine_health_check_t* hc) {

#ifdef HAVE_X11
#ifdef HAVE_XV
  Display               *dpy;
  unsigned int          ver, rev, eventB, reqB, errorB;
  char                  *disname = NULL;
  void                  *x11_handle;
  void                  *xv_handle;
  Display               *(*xopendisplay)(char*);
  char                  *(*xdisplayname)(char*);
  int                   (*xvqueryextension)(Display*, unsigned int*, unsigned int*, unsigned int*, unsigned int*, unsigned int*);
  int                   (*xvqueryadaptors)(Display*, Window, int*, XvAdaptorInfo**);
  XvImageFormatValues   *(*xvlistimageformats)(Display*, XvPortID, int*);
  char                  *err = NULL;
  int                   formats, adaptors, i;
  XvImageFormatValues   *img_formats;
  XvAdaptorInfo         *adaptor_info;

  hc->title       = "Check for MIT Xv extension";
  hc->explanation = "You can improve performance by installing an X11\n"
    "driver that supports the Xv protocol extension.";

  /* Majority of thi code was taken from or inspired by the xvinfo.c file of XFree86 */

  dlerror(); /* clear error code */
  x11_handle = dlopen(LIBX11_SO, RTLD_LAZY);
  if(!x11_handle) {
    hc->msg = dlerror();
    hc->status = XINE_HEALTH_CHECK_FAIL;
    return hc;
  }

  /* Get reference to XOpenDisplay */
  xopendisplay = dlsym(x11_handle,"XOpenDisplay");
  if((err = dlerror()) != NULL) {
    hc->msg = err;
    hc->status = XINE_HEALTH_CHECK_FAIL;
    dlclose(x11_handle);
    return hc;
  }

  /* Get reference to XDisplayName */
  xdisplayname = dlsym(x11_handle,"XDisplayName");
  if((err = dlerror()) != NULL) {
    hc->msg = err;
    hc->status = XINE_HEALTH_CHECK_FAIL;
    dlclose(x11_handle);
    return hc;
  }

  dlerror(); /* clear error code */
  xv_handle = dlopen(LIBXV_SO, RTLD_LAZY);
  if(!xv_handle) {
    hc->msg = dlerror();
    /* Xv might still work when linked statically into the output plugin,
     * so reporting FAIL would be wrong here */
    hc->status = XINE_HEALTH_CHECK_UNSUPPORTED;
    dlclose(x11_handle);
    return hc;
  }

  /* Get reference to XvQueryExtension */
  xvqueryextension = dlsym(xv_handle,"XvQueryExtension");
  if((err = dlerror()) != NULL) {
    hc->msg = err;
    hc->status = XINE_HEALTH_CHECK_FAIL;
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  /* Get reference to XvQueryAdaptors */
  xvqueryadaptors = dlsym(xv_handle,"XvQueryAdaptors");
  if((err = dlerror()) != NULL) {
    hc->msg = err;
    hc->status = XINE_HEALTH_CHECK_FAIL;
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  /* Get reference to XvListImageFormats */
  xvlistimageformats = dlsym(xv_handle,"XvListImageFormats");
  if((err = dlerror()) != NULL) {
    hc->msg = err;
    hc->status = XINE_HEALTH_CHECK_FAIL;
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  if(!(dpy = (*xopendisplay)(disname))) {

    if (!disname) {
      disname = (*xdisplayname)(NULL);
    }
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "Unable to open display: %s\n", disname);
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  if((Success != xvqueryextension(dpy, &ver, &rev, &reqB, &eventB, &errorB))) {
    if (!disname) {
      disname = xdisplayname(NULL);
    }
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "Unable to open display: %s\n",disname);
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }
  else {
    printf("X-Video Extension version %d.%d\n", ver, rev);
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if (Success != xvqueryadaptors(dpy,DefaultRootWindow(dpy),
				 &adaptors,&adaptor_info))  {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "video_out_xv: XvQueryAdaptors failed.\n");
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  if (!adaptor_info) {
    set_hc_result (hc, XINE_HEALTH_CHECK_FAIL, "video_out_xv: No adaptors found.\n");
    dlclose(x11_handle);
    dlclose(xv_handle);
    return hc;
  }

  img_formats = xvlistimageformats (dpy, adaptor_info->base_id, &formats);

  for(i = 0; i < formats; i++) {

    printf ("video_out_xv: Xv image format: 0x%x (%4.4s) %s\n",
	    img_formats[i].id, (char*)&img_formats[i].id,
	    (img_formats[i].format == XvPacked) ? "packed" : "planar");

    if (img_formats[i].id == XINE_IMGFMT_YV12)  {
      printf("video_out_xv: this adaptor supports the yv12 format.\n");
      set_hc_result (hc, XINE_HEALTH_CHECK_OK, "video_out_xv: this adaptor supports the yv12 format.\n");
    } else if (img_formats[i].id == XINE_IMGFMT_YUY2) {
      printf("video_out_xv: this adaptor supports the yuy2 format.\n");
      set_hc_result (hc, XINE_HEALTH_CHECK_OK, "video_out_xv: this adaptor supports the yuy2 format.\n");
    }
  }

  dlclose(x11_handle);
  dlclose(xv_handle);

  return hc;
#else
  hc->title       = "Check for MIT Xv extension";
  hc->explanation = "You can improve performance by installing an X11\n"
    "driver that supports the Xv protocol extension.";

  set_hc_result(hc, XINE_HEALTH_CHECK_FAIL, "No X-Video Extension was present at compile time");
  return hc;
#endif /* ! HAVE_HV */
#else
  hc->title       = "Check for MIT Xv extension";
  hc->explanation = "You can improve performance by installing an X11\n"
    "driver that supports the Xv protocol extension.";

  set_hc_result(hc, XINE_HEALTH_CHECK_FAIL, "No X11 windowing system was present at compile time");
  return hc;
#endif /* ! HAVE_X11 */
}

xine_health_check_t* xine_health_check (xine_health_check_t* hc, int check_num) {

  switch(check_num) {
    case CHECK_KERNEL:
      hc = _x_health_check_kernel (hc);
      break;
    case CHECK_MTRR:
      hc = _x_health_check_mtrr (hc);
      break;
    case CHECK_CDROM:
      hc = _x_health_check_cdrom (hc);
      break;
    case CHECK_DVDROM:
      hc = _x_health_check_dvdrom (hc);
      break;
    case CHECK_DMA:
      hc = _x_health_check_dma (hc);
      break;
    case CHECK_X:
      hc = _x_health_check_x (hc);
      break;
    case CHECK_XV:
      hc = _x_health_check_xv (hc);
      break;
    default:
      hc->status = XINE_HEALTH_CHECK_NO_SUCH_CHECK;
  }

  return hc;
}

#else	/* !__linux__ */
xine_health_check_t* xine_health_check (xine_health_check_t* hc, int check_num) {
  hc->title       = "xine health check not supported on this platform";
  hc->explanation = "contact the xine-devel mailing list if you'd like to\n"
                    "contribute code for your platform.";
  set_hc_result(hc, XINE_HEALTH_CHECK_NO_SUCH_CHECK,
		"xine health check not supported on the OS.\n");
  return hc;
}
#endif	/* !__linux__ */
