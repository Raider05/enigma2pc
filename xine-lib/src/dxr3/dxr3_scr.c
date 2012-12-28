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
 */

/* dxr3 scr plugin.
 * enables xine to use the internal clock of the card as its
 * global time reference.
 */

#include <sys/ioctl.h>
#if defined(__sun)
#include <sys/ioccom.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define LOG_MODULE "dxr3_scr"
/* #define LOG_VERBOSE */
/* #define LOG */

#include "dxr3.h"
#include "dxr3_scr.h"


/* functions required by xine api */
static int     dxr3_scr_get_priority(scr_plugin_t *scr);
static void    dxr3_scr_start(scr_plugin_t *scr, int64_t vpts);
static int64_t dxr3_scr_get_current(scr_plugin_t *scr);
static void    dxr3_scr_adjust(scr_plugin_t *scr, int64_t vpts);
static int     dxr3_scr_set_speed(scr_plugin_t *scr, int speed);
static void    dxr3_scr_exit(scr_plugin_t *scr);

/* config callback */
static void    dxr3_scr_update_priority(void *this_gen, xine_cfg_entry_t *entry);

/* inline helper implementations */
static inline int dxr3_mvcommand(int fd_control, int command)
{
  em8300_register_t reg;

  reg.microcode_register = 1;
  reg.reg = 0;
  reg.val = command;

  return ioctl(fd_control, EM8300_IOCTL_WRITEREG, &reg);
}


dxr3_scr_t *dxr3_scr_init(xine_t *xine)
{
  dxr3_scr_t *this;
  int devnum;
  char tmpstr[128];

  this = calloc(1, sizeof(dxr3_scr_t));

  devnum = xine->config->register_num(xine->config,
    CONF_KEY, 0, CONF_NAME, CONF_HELP, 10, NULL, NULL);
  snprintf(tmpstr, sizeof(tmpstr), "/dev/em8300-%d", devnum);
  if ((this->fd_control = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_scr: Failed to open control device %s (%s)\n", tmpstr, strerror(errno));
    free(this);
    return NULL;
  }

  this->xine = xine;

  this->scr_plugin.interface_version = 3;
  this->scr_plugin.get_priority      = dxr3_scr_get_priority;
  this->scr_plugin.start             = dxr3_scr_start;
  this->scr_plugin.get_current       = dxr3_scr_get_current;
  this->scr_plugin.adjust            = dxr3_scr_adjust;
  this->scr_plugin.set_fine_speed    = dxr3_scr_set_speed;
  this->scr_plugin.exit              = dxr3_scr_exit;

  this->priority                     = xine->config->register_num(
    xine->config, "dxr3.scr_priority", 10, _("SCR plugin priority"),
    _("Priority of the DXR3 SCR plugin. Values less than 5 mean that the "
      "unix system timer will be used. Values greater 5 force to use "
      "DXR3's internal clock as sync source."), 25,
    dxr3_scr_update_priority, this);
  this->offset                       = 0;
  this->last_pts                     = 0;
  this->scanning                     = 0;
  this->sync                         = 0;

  pthread_mutex_init(&this->mutex, NULL);

  lprintf("init complete\n");
  return this;
}


static int dxr3_scr_get_priority(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  return this->priority;
}

static void dxr3_scr_start(scr_plugin_t *scr, int64_t vpts)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t vpts32 = vpts >> 1;

  pthread_mutex_lock(&this->mutex);
  this->last_pts = vpts32;
  this->offset = vpts - ((int64_t)vpts32 << 1);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: start failed (%s)\n", strerror(errno));
  lprintf("started with vpts %lld\n", vpts);
  /* mis-use vpts32 to set the clock speed to 0x900, which is normal speed */
  vpts32 = 0x900;
  ioctl(this->fd_control, EM8300_IOCTL_SCR_SETSPEED, &vpts32);
  this->scanning = 0;
  this->sync     = 0;
  pthread_mutex_unlock(&this->mutex);
}

static int64_t dxr3_scr_get_current(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t pts;
  int64_t current;

  pthread_mutex_lock(&this->mutex);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_GET, &pts))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: get current failed (%s)\n", strerror(errno));
  if (this->last_pts > 0xF0000000 && pts < 0x10000000)
    /* wrap around detected, compensate with offset */
    this->offset += (int64_t)1 << 33;
  if (pts == 0)
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: WARNING: pts dropped to zero.\n");
  this->last_pts = pts;
  current = ((int64_t)pts << 1) + this->offset;
  pthread_mutex_unlock(&this->mutex);

  return current;
}

static void dxr3_scr_adjust(scr_plugin_t *scr, int64_t vpts)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t current_pts32;
  int32_t offset32;

  pthread_mutex_lock(&this->mutex);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_GET, &current_pts32))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: adjust get failed (%s)\n", strerror(errno));
  this->last_pts = current_pts32;
  this->offset = vpts - ((int64_t)current_pts32 << 1);
  offset32 = this->offset / 4;
  /* kernel driver ignores diffs < 7200, so abs(offset32) must be > 7200 / 4 */
  if (offset32 < -7200/4 || offset32 > 7200/4) {
    uint32_t vpts32 = vpts >> 1;
    if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: adjust set failed (%s)\n", strerror(errno));
    this->last_pts = vpts32;
    this->offset = vpts - ((int64_t)vpts32 << 1);
  }
  lprintf("adjusted to vpts %lld\n", vpts);
  pthread_mutex_unlock(&this->mutex);
}

static int dxr3_scr_set_speed(scr_plugin_t *scr, int speed)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t em_speed;
  int playmode;

  pthread_mutex_lock(&this->mutex);

  em_speed = 0x900LL * (int64_t)speed / XINE_FINE_SPEED_NORMAL;
  switch (em_speed) {
  case 0:
    /* pause mode */
    playmode = MVCOMMAND_PAUSE;
    break;
  case 0x900:
    /* normal playback */
    if (this->sync)
      playmode = MVCOMMAND_SYNC;
    else
      playmode = MVCOMMAND_START;
    break;
  default:
    playmode = MVCOMMAND_START;
  }

  if (dxr3_mvcommand(this->fd_control, playmode))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: failed to playmode (%s)\n", strerror(errno));

  if(em_speed > 0x900)
    this->scanning = 1;
  else
    this->scanning = 0;

  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SETSPEED, &em_speed))
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "dxr3_scr: failed to set speed (%s)\n", strerror(errno));

  pthread_mutex_unlock(&this->mutex);

  lprintf("speed set to mode %d\n", speed);
  return speed;
}

static void dxr3_scr_exit(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;

  close(this->fd_control);
  pthread_mutex_destroy(&this->mutex);
  free(this);
}




static void dxr3_scr_update_priority(void *this_gen, xine_cfg_entry_t *entry)
{
  dxr3_scr_t *this = (dxr3_scr_t *)this_gen;

  this->priority = entry->num_value;
  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  "dxr3_scr: setting scr priority to %d\n", entry->num_value);
}
