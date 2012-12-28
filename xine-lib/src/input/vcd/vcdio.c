/*
  $Id: vcdio.c,v 1.9 2007/03/23 21:47:31 dsalt Exp $

  Copyright (C) 2002, 2003, 2004, 2005 Rocky Bernstein <rocky@panix.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_VCDNAV
#include <libvcd/types.h>
#include <libvcd/files.h>
#include <cdio/iso9660.h>
#else
#include "libvcd/types.h"
#include "libvcd/files.h"
#include "cdio/iso9660.h"
#endif

#include "vcdplayer.h"
#include "vcdio.h"

#define LOG_ERR(p_vcdplayer, s, args...) \
       if (p_vcdplayer != NULL && p_vcdplayer->log_err != NULL) \
          p_vcdplayer->log_err("%s:  "s, __func__ , ##args)

#define FREE_AND_NULL(ptr) if (NULL != ptr) free(ptr); ptr = NULL;

/*! Closes VCD device specified via "this", and also wipes memory of it
   from it inside "this". */
int
vcdio_close(vcdplayer_t *p_vcdplayer)
{
  p_vcdplayer->b_opened = false;

  FREE_AND_NULL(p_vcdplayer->psz_source);
  FREE_AND_NULL(p_vcdplayer->track);
  FREE_AND_NULL(p_vcdplayer->segment);
  FREE_AND_NULL(p_vcdplayer->entry);

  return vcdinfo_close(p_vcdplayer->vcd);
}


/*! Opens VCD device and initializes things.

   - do nothing if the device had already been open and is the same device.
   - if the device had been open and is a different, close it before trying
     to open new device.
*/
bool
vcdio_open(vcdplayer_t *p_vcdplayer, char *intended_vcd_device)
{
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;
  unsigned int i;

  dbg_print(INPUT_DBG_CALL, "called with %s\n", intended_vcd_device);

  if ( p_vcdplayer->b_opened ) {
    if ( strcmp(intended_vcd_device, p_vcdplayer->psz_source)==0 ) {
      /* Already open and the same device, so do nothing */
      return true;
    } else {
      /* Changing VCD device */
      vcdio_close(p_vcdplayer);
    }
  }

  switch ( vcdinfo_open(&p_vcdplayer->vcd, &intended_vcd_device,
                        DRIVER_UNKNOWN, NULL))
  {
    case VCDINFO_OPEN_ERROR:
      /* Failed to open the device => return failure */
      return false;

    case VCDINFO_OPEN_VCD:
      /* Opened the device, and it's a VCD => proceed */
      break;

    default:
      /* Opened the device, but it's not a VCD => is closed, return failure */
      return false;
  }

  p_vcdinfo = p_vcdplayer->vcd;

  p_vcdplayer->psz_source = strdup(intended_vcd_device);
  p_vcdplayer->b_opened   = true;
  p_vcdplayer->i_lids     = vcdinfo_get_num_LIDs(p_vcdinfo);
  p_vcdplayer->vcd_format = vcdinfo_get_format_version(p_vcdinfo);
  p_vcdplayer->i_still    = 0;

  if (vcdinfo_read_psd (p_vcdinfo)) {

    vcdinfo_visit_lot (p_vcdinfo, false);

    if (VCD_TYPE_VCD2 == p_vcdplayer->vcd_format &&
        vcdinfo_get_psd_x_size(p_vcdinfo)) {
        vcdinfo_visit_lot (p_vcdinfo, true);
    }
  }

  /*
     Save summary info on tracks, segments and entries...
   */

  if ( 0 < (p_vcdplayer->i_tracks = vcdinfo_get_num_tracks(p_vcdinfo)) ) {
    p_vcdplayer->track = (vcdplayer_play_item_info_t *)
      calloc(p_vcdplayer->i_tracks, sizeof(vcdplayer_play_item_info_t));

    for (i=0; i<p_vcdplayer->i_tracks; i++) {
      track_t i_track=i+1;
      p_vcdplayer->track[i].size
        = vcdinfo_get_track_sect_count(p_vcdinfo, i_track);
      p_vcdplayer->track[i].start_LSN
        = vcdinfo_get_track_lsn(p_vcdinfo, i_track);
    }
  } else
    p_vcdplayer->track = NULL;

  if ( 0 < (p_vcdplayer->i_entries = vcdinfo_get_num_entries(p_vcdinfo)) ) {
    p_vcdplayer->entry = (vcdplayer_play_item_info_t *)
      calloc(p_vcdplayer->i_entries, sizeof(vcdplayer_play_item_info_t));

    for (i=0; i<p_vcdplayer->i_entries; i++) {
      p_vcdplayer->entry[i].size
        = vcdinfo_get_entry_sect_count(p_vcdinfo, i);
      p_vcdplayer->entry[i].start_LSN
        = vcdinfo_get_entry_lsn(p_vcdinfo, i);
    }
  } else
    p_vcdplayer->entry = NULL;

  if ( 0 < (p_vcdplayer->i_segments = vcdinfo_get_num_segments(p_vcdinfo)) ) {
    p_vcdplayer->segment = (vcdplayer_play_item_info_t *)
      calloc(p_vcdplayer->i_segments,  sizeof(vcdplayer_play_item_info_t));

    for (i=0; i<p_vcdplayer->i_segments; i++) {
      p_vcdplayer->segment[i].size
        = vcdinfo_get_seg_sector_count(p_vcdinfo, i);
      p_vcdplayer->segment[i].start_LSN
        = vcdinfo_get_seg_lsn(p_vcdinfo, i);
    }
  } else
    p_vcdplayer->segment = NULL;

  return true;
}

/*!
  seek position, return new position

  if seeking failed, -1 is returned
*/
off_t
vcdio_seek (vcdplayer_t *p_vcdplayer, off_t offset, int origin)
{

  switch (origin) {
  case SEEK_SET:
    {
      lsn_t old_lsn = p_vcdplayer->i_lsn;
      p_vcdplayer->i_lsn = p_vcdplayer->origin_lsn + (offset / M2F2_SECTOR_SIZE);

      dbg_print(INPUT_DBG_SEEK_SET, "seek_set to %ld => %u (start is %u)\n",
		(long int) offset, p_vcdplayer->i_lsn, p_vcdplayer->origin_lsn);

      /* Seek was successful. Invalidate entry location by setting
         entry number back to 1. Over time it will adjust upward
         to the correct value. */
      if ( !vcdplayer_pbc_is_on(p_vcdplayer)
           && p_vcdplayer->play_item.type != VCDINFO_ITEM_TYPE_TRACK
           && p_vcdplayer->i_lsn < old_lsn) {
        dbg_print(INPUT_DBG_SEEK_SET, "seek_set entry backwards\n");
        p_vcdplayer->next_entry = 1;
      }
      break;
    }

  case SEEK_CUR:
    {
      off_t diff;
      if (offset) {
        LOG_ERR(p_vcdplayer, "%s: %d\n",
                _("SEEK_CUR not implemented for non-zero offset"),
                (int) offset);
        return (off_t) -1;
      }

      if (p_vcdplayer->slider_length == VCDPLAYER_SLIDER_LENGTH_TRACK) {
        diff = p_vcdplayer->i_lsn - p_vcdplayer->track_lsn;
        dbg_print(INPUT_DBG_SEEK_CUR,
                  "current pos: %u, track diff %ld\n",
                  p_vcdplayer->i_lsn, (long int) diff);
      } else {
        diff = p_vcdplayer->i_lsn - p_vcdplayer->origin_lsn;
        dbg_print(INPUT_DBG_SEEK_CUR,
                  "current pos: %u, entry diff %ld\n",
                  p_vcdplayer->i_lsn, (long int) diff);
      }

      if (diff < 0) {
        dbg_print(INPUT_DBG_SEEK_CUR, "Error: diff < 0\n");
        return (off_t) 0;
      } else {
        return (off_t)diff * M2F2_SECTOR_SIZE;
      }

      break;
    }

  case SEEK_END:
    LOG_ERR(p_vcdplayer, "%s\n", _("SEEK_END not implemented yet."));
    return (off_t) -1;
  default:
    LOG_ERR(p_vcdplayer, "%s %d\n", _("seek not implemented yet for"),
	     origin);
    return (off_t) -1;
  }

  return offset ; /* FIXME */
}

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
