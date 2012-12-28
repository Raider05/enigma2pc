/*
  $Id: vcdplayer.c,v 1.20 2007/02/21 23:17:14 dgp85 Exp $

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
#include <unistd.h>
#include <time.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>

#ifdef HAVE_VCDNAV
#include <libvcd/files.h>
#include <cdio/iso9660.h>
#else
#include "libvcd/files.h"
#include "cdio/iso9660.h"
#endif

#include "vcdplayer.h"
#include "vcdio.h"

/* This function is _not_ exported by libvcd, its usage should be avoided, most
 * likely.
 */
void vcdinfo_get_seg_resolution(const vcdinfo_obj_t *p_vcdinfo, segnum_t i_seg,
                                /*out*/ uint16_t *max_x, /*out*/ uint16_t *max_y);

#define LOG_ERR(p_vcdplayer, s, args...) \
       if (p_vcdplayer != NULL && p_vcdplayer->log_err != NULL) \
          p_vcdplayer->log_err("%s:  "s, __func__ , ##args)

unsigned long int vcdplayer_debug = 0;

static void  _vcdplayer_set_origin(vcdplayer_t *p_vcdplayer);

/*!
  Return true if playback control (PBC) is on
*/
bool
vcdplayer_pbc_is_on(const vcdplayer_t *p_vcdplayer)
{
  return VCDINFO_INVALID_ENTRY != p_vcdplayer->i_lid;
}

/* Given an itemid, return the size for the object (via information
   previously stored when opening the vcd). */
static size_t
_vcdplayer_get_item_size(vcdplayer_t *p_vcdplayer, vcdinfo_itemid_t itemid)
{
  switch (itemid.type) {
  case VCDINFO_ITEM_TYPE_ENTRY:
    return p_vcdplayer->entry[itemid.num].size;
    break;
  case VCDINFO_ITEM_TYPE_SEGMENT:
    return p_vcdplayer->segment[itemid.num].size;
    break;
  case VCDINFO_ITEM_TYPE_TRACK:
    return p_vcdplayer->track[itemid.num-1].size;
    break;
  case VCDINFO_ITEM_TYPE_LID:
    /* Play list number (LID) */
    return 0;
    break;
  case VCDINFO_ITEM_TYPE_NOTFOUND:
  case VCDINFO_ITEM_TYPE_SPAREID2:
  default:
    LOG_ERR(p_vcdplayer, "%s %d\n", _("bad item type"), itemid.type);
    return 0;
  }
}

#define add_format_str_info(val)			\
  {							\
    const char *str = val;				\
    unsigned int len;					\
    if (val != NULL) {					\
      len=strlen(str);					\
      if (len != 0) {					\
	strncat(tp, str, TEMP_STR_LEN-(tp-temp_str));	\
	tp += len;					\
      }							\
      saw_control_prefix = false;			\
    }							\
  }

#define add_format_num_info(val, fmt)			\
  {							\
    char num_str[10];					\
    unsigned int len;                                   \
    snprintf(num_str, sizeof(num_str), fmt, val);	\
    len=strlen(num_str);                                \
    if (len != 0) {					\
      strncat(tp, num_str, TEMP_STR_LEN-(tp-temp_str));	\
      tp += len;					\
    }							\
    saw_control_prefix = false;				\
  }

/*!
   Take a format string and expand escape sequences, that is sequences that
   begin with %, with information from the current VCD.
   The expanded string is returned. Here is a list of escape sequences:

   %A : The album information
   %C : The VCD volume count - the number of CD's in the collection.
   %c : The VCD volume num - the number of the CD in the collection.
   %F : The VCD Format, e.g. VCD 1.0, VCD 1.1, VCD 2.0, or SVCD
   %I : The current entry/segment/playback type, e.g. ENTRY, TRACK, SEGMENT...
   %L : The playlist ID prefixed with " LID" if it exists
   %N : The current number of the above - a decimal number
   %P : The publisher ID
   %p : The preparer ID
   %S : If we are in a segment (menu), the kind of segment
   %T : The track number
   %V : The volume set ID
   %v : The volume ID
       A number between 1 and the volume count.
   %% : a %
*/
char *
vcdplayer_format_str(vcdplayer_t *p_vcdplayer, const char format_str[])
{
#define TEMP_STR_SIZE 256
#define TEMP_STR_LEN (TEMP_STR_SIZE-1)
  static char    temp_str[TEMP_STR_SIZE];
  size_t i;
  char * tp = temp_str;
  bool saw_control_prefix = false;
  size_t format_len = strlen(format_str);
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;

  memset(temp_str, 0, TEMP_STR_SIZE);

  for (i=0; i<format_len; i++) {

    if (!saw_control_prefix && format_str[i] != '%') {
      *tp++ = format_str[i];
      saw_control_prefix = false;
      continue;
    }

    switch(format_str[i]) {
    case '%':
      if (saw_control_prefix) {
	*tp++ = '%';
      }
      saw_control_prefix = !saw_control_prefix;
      break;
    case 'A':
      add_format_str_info(vcdinfo_strip_trail(vcdinfo_get_album_id(p_vcdinfo),
                                              MAX_ALBUM_LEN));
      break;

    case 'c':
      add_format_num_info(vcdinfo_get_volume_num(p_vcdinfo), "%d");
      break;

    case 'C':
      add_format_num_info(vcdinfo_get_volume_count(p_vcdinfo), "%d");
      break;

    case 'F':
      add_format_str_info(vcdinfo_get_format_version_str(p_vcdinfo));
      break;

    case 'I':
      {
	switch (p_vcdplayer->play_item.type) {
	case VCDINFO_ITEM_TYPE_TRACK:
	  strncat(tp, "Track", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Track");
	break;
	case VCDINFO_ITEM_TYPE_ENTRY:
	  strncat(tp, "Entry", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Entry");
	  break;
	case VCDINFO_ITEM_TYPE_SEGMENT:
	  strncat(tp, "Segment", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Segment");
	  break;
	case VCDINFO_ITEM_TYPE_LID:
	  strncat(tp, "List ID", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("List ID");
	  break;
	case VCDINFO_ITEM_TYPE_SPAREID2:
	  strncat(tp, "Navigation", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Navigation");
	  break;
	default:
	  /* What to do? */
          ;
	}
	saw_control_prefix = false;
      }
      break;

    case 'L':
      if (vcdplayer_pbc_is_on(p_vcdplayer)) {
        char num_str[20];
        snprintf(num_str, sizeof(num_str), " List ID %d", p_vcdplayer->i_lid);
        strncat(tp, num_str, TEMP_STR_LEN-(tp-temp_str));
        tp += strlen(num_str);
      }
      saw_control_prefix = false;
      break;

    case 'N':
      add_format_num_info(p_vcdplayer->play_item.num, "%d");
      break;

    case 'p':
      add_format_str_info(vcdinfo_get_preparer_id(p_vcdinfo));
      break;

    case 'P':
      add_format_str_info(vcdinfo_get_publisher_id(p_vcdinfo));
      break;

    case 'S':
      if ( VCDINFO_ITEM_TYPE_SEGMENT==p_vcdplayer->play_item.type ) {
        char seg_type_str[30];

        snprintf(seg_type_str, sizeof(seg_type_str), " %s",
                vcdinfo_video_type2str(p_vcdinfo, p_vcdplayer->play_item.num));
        strncat(tp, seg_type_str, TEMP_STR_LEN-(tp-temp_str));
        tp += strlen(seg_type_str);
      }
      saw_control_prefix = false;
      break;

    case 'T':
      add_format_num_info(p_vcdplayer->i_track, "%d");
      break;

    case 'V':
      add_format_str_info(vcdinfo_get_volumeset_id(p_vcdinfo));
      break;

    case 'v':
      add_format_str_info(vcdinfo_get_volume_id(p_vcdinfo));
      break;

    default:
      *tp++ = '%';
      *tp++ = format_str[i];
      saw_control_prefix = false;
    }
  }
  return strdup(temp_str);
}

static void
_vcdplayer_update_entry(vcdinfo_obj_t *p_vcdinfo, uint16_t ofs,
                        uint16_t *entry, const char *label)
{
  if ( ofs == VCDINFO_INVALID_OFFSET ) {
    *entry = VCDINFO_INVALID_ENTRY;
  } else {
    vcdinfo_offset_t *off = vcdinfo_get_offset_t(p_vcdinfo, ofs);
    if (off != NULL) {
      *entry = off->lid;
      dbg_print(INPUT_DBG_PBC, "%s: LID %d\n", label, off->lid);
    } else
      *entry = VCDINFO_INVALID_ENTRY;
  }
}

/*!
  Update next/prev/return/default navigation buttons
  (via p_vcdplayer->i_lid). Update size of play-item
  (via p_vcdplayer->play_item).
*/
void
vcdplayer_update_nav(vcdplayer_t *p_vcdplayer)
{
  int play_item = p_vcdplayer->play_item.num;
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;

  int min_entry = 1;
  int max_entry = 0;

  if  (vcdplayer_pbc_is_on(p_vcdplayer)) {

    vcdinfo_lid_get_pxd(p_vcdinfo, &(p_vcdplayer->pxd), p_vcdplayer->i_lid);

    switch (p_vcdplayer->pxd.descriptor_type) {
    case PSD_TYPE_SELECTION_LIST:
    case PSD_TYPE_EXT_SELECTION_LIST:
      if (p_vcdplayer->pxd.psd == NULL) return;
      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_psd_get_prev_offset(p_vcdplayer->pxd.psd),
                              &(p_vcdplayer->prev_entry), "prev");

      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_psd_get_next_offset(p_vcdplayer->pxd.psd),
                              &(p_vcdplayer->next_entry), "next");

      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_psd_get_return_offset(p_vcdplayer->pxd.psd),
                              &(p_vcdplayer->return_entry), "return");

      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinfo_get_default_offset(p_vcdinfo,
                                                         p_vcdplayer->i_lid),
                              &(p_vcdplayer->default_entry), "default");
      break;
    case PSD_TYPE_PLAY_LIST:
      if (p_vcdplayer->pxd.pld == NULL) return;
      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_pld_get_prev_offset(p_vcdplayer->pxd.pld),
                              &(p_vcdplayer->prev_entry), "prev");

      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_pld_get_next_offset(p_vcdplayer->pxd.pld),
                              &(p_vcdplayer->next_entry), "next");

      _vcdplayer_update_entry(p_vcdinfo,
                              vcdinf_pld_get_return_offset(p_vcdplayer->pxd.pld),
                              &(p_vcdplayer->return_entry), "return");
      p_vcdplayer->default_entry = VCDINFO_INVALID_ENTRY;
      break;
    case PSD_TYPE_END_LIST:
      p_vcdplayer->origin_lsn = p_vcdplayer->i_lsn = p_vcdplayer->end_lsn
        = VCDINFO_NULL_LSN;
      /* Fall through */
    case PSD_TYPE_COMMAND_LIST:
      p_vcdplayer->next_entry = p_vcdplayer->prev_entry
        = p_vcdplayer->return_entry = VCDINFO_INVALID_ENTRY;
      p_vcdplayer->default_entry = VCDINFO_INVALID_ENTRY;
      break;
    }

    if (p_vcdplayer->update_title)
      p_vcdplayer->update_title();
    return;
  }

  /* PBC is not on. Set up for simplified next, prev, and return. */

  switch (p_vcdplayer->play_item.type) {
  case VCDINFO_ITEM_TYPE_ENTRY:
  case VCDINFO_ITEM_TYPE_SEGMENT:
  case VCDINFO_ITEM_TYPE_TRACK:

    switch (p_vcdplayer->play_item.type) {
    case VCDINFO_ITEM_TYPE_ENTRY:
      max_entry = p_vcdplayer->i_entries;
      min_entry = 0; /* Can remove when Entries start at 1. */
      p_vcdplayer->i_track = vcdinfo_get_track(p_vcdinfo, play_item);
      p_vcdplayer->track_lsn = vcdinfo_get_track_lsn(p_vcdinfo,
                                                     p_vcdplayer->i_track);
      break;
    case VCDINFO_ITEM_TYPE_SEGMENT:
      max_entry            = p_vcdplayer->i_segments;
      p_vcdplayer->i_track = VCDINFO_INVALID_TRACK;

      break;
    case VCDINFO_ITEM_TYPE_TRACK:
      max_entry       = p_vcdplayer->i_tracks;
      p_vcdplayer->i_track   = p_vcdplayer->play_item.num;
      p_vcdplayer->track_lsn = vcdinfo_get_track_lsn(p_vcdinfo,
                                                     p_vcdplayer->i_track);
      break;
    default: ; /* Handle exceptional cases below */
    }

    _vcdplayer_set_origin(p_vcdplayer);
    /* Set next, prev, return and default to simple and hopefully
       useful values.
     */
    if (play_item+1 >= max_entry)
      p_vcdplayer->next_entry = VCDINFO_INVALID_ENTRY;
    else
      p_vcdplayer->next_entry = play_item+1;

    if (play_item-1 >= min_entry)
      p_vcdplayer->prev_entry = play_item-1;
    else
      p_vcdplayer->prev_entry = VCDINFO_INVALID_ENTRY;

    p_vcdplayer->default_entry = play_item;
    p_vcdplayer->return_entry  = min_entry;
    break;

  case VCDINFO_ITEM_TYPE_LID:
    {
      /* Should have handled above. */
      break;
    }
  default: ;
  }
  p_vcdplayer->update_title();
}

/*!
  Set reading to play an entire track.
*/
static void
_vcdplayer_set_track(vcdplayer_t *p_vcdplayer, unsigned int i_track)
{
  if (i_track < 1 || i_track > p_vcdplayer->i_tracks)
    return;
  else {
    vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;
    vcdinfo_itemid_t itemid;

    itemid.num             = i_track;
    itemid.type            = VCDINFO_ITEM_TYPE_TRACK;
    p_vcdplayer->i_still   = 0;
    p_vcdplayer->i_lsn     = vcdinfo_get_track_lsn(p_vcdinfo, i_track);
    p_vcdplayer->play_item = itemid;
    p_vcdplayer->i_track   = i_track;
    p_vcdplayer->track_lsn = p_vcdplayer->i_lsn;

    _vcdplayer_set_origin(p_vcdplayer);

    dbg_print(INPUT_DBG_LSN, "LSN: %u\n", p_vcdplayer->i_lsn);
  }
}

/*!
  Set reading to play an entry
*/
static void
_vcdplayer_set_entry(vcdplayer_t *p_vcdplayer, unsigned int num)
{
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;
  const unsigned int i_entries = vcdinfo_get_num_entries(p_vcdinfo);

  if (num >= i_entries) {
    LOG_ERR(p_vcdplayer, "%s %d\n", _("bad entry number"), num);
    return;
  } else {
    vcdinfo_itemid_t itemid;

    itemid.num             = num;
    itemid.type            = VCDINFO_ITEM_TYPE_ENTRY;
    p_vcdplayer->i_still   = 0;
    p_vcdplayer->i_lsn     = vcdinfo_get_entry_lsn(p_vcdinfo, num);
    p_vcdplayer->play_item = itemid;
    p_vcdplayer->i_track   = vcdinfo_get_track(p_vcdinfo, num);
    p_vcdplayer->track_lsn = vcdinfo_get_track_lsn(p_vcdinfo,
                                                   p_vcdplayer->i_track);
    p_vcdplayer->track_end_lsn = p_vcdplayer->track_lsn +
      p_vcdplayer->track[p_vcdplayer->i_track-1].size;

    _vcdplayer_set_origin(p_vcdplayer);

    dbg_print((INPUT_DBG_LSN|INPUT_DBG_PBC), "LSN: %u, track_end LSN: %u\n",
              p_vcdplayer->i_lsn, p_vcdplayer->track_end_lsn);
  }
}

/*!
  Set reading to play an segment (e.g. still frame)
*/
static void
_vcdplayer_set_segment(vcdplayer_t *p_vcdplayer, unsigned int num)
{
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;
  segnum_t i_segs  = vcdinfo_get_num_segments(p_vcdinfo);

  if (num >= i_segs) {
    LOG_ERR(p_vcdplayer, "%s %d\n", _("bad segment number"), num);
    return;
  } else {
    vcdinfo_itemid_t itemid;

    p_vcdplayer->i_lsn   = vcdinfo_get_seg_lsn(p_vcdinfo, num);
    p_vcdplayer->i_track = 0;

    if (VCDINFO_NULL_LSN==p_vcdplayer->i_lsn) {
      LOG_ERR(p_vcdplayer, "%s %d\n",
              _("Error in getting current segment number"), num);
      return;
    }

    itemid.num = num;
    itemid.type = VCDINFO_ITEM_TYPE_SEGMENT;
    p_vcdplayer->play_item = itemid;

    _vcdplayer_set_origin(p_vcdplayer);

    dbg_print(INPUT_DBG_LSN, "LSN: %u\n", p_vcdplayer->i_lsn);
  }
}

/* Play entry. */
/* Play a single item. */
static void
vcdplayer_play_single_item(vcdplayer_t *p_vcdplayer, vcdinfo_itemid_t itemid)
{
  vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;

  dbg_print(INPUT_DBG_CALL, "called itemid.num: %d, itemid.type: %d\n",
            itemid.num, itemid.type);

  p_vcdplayer->i_still = 0;

  switch (itemid.type) {
  case VCDINFO_ITEM_TYPE_SEGMENT:
    {
      vcdinfo_video_segment_type_t segtype
        = vcdinfo_get_video_type(p_vcdinfo, itemid.num);
      segnum_t i_segs = vcdinfo_get_num_segments(p_vcdinfo);

      dbg_print(INPUT_DBG_PBC, "%s (%d), itemid.num: %d\n",
                vcdinfo_video_type2str(p_vcdinfo, itemid.num),
                (int) segtype, itemid.num);

      if (itemid.num >= i_segs) return;
      _vcdplayer_set_segment(p_vcdplayer, itemid.num);

      vcdinfo_get_seg_resolution(p_vcdinfo, itemid.num,
                                 &(p_vcdplayer->max_x),
                                 &(p_vcdplayer->max_y));

      switch (segtype) {
      case VCDINFO_FILES_VIDEO_NTSC_STILL:
      case VCDINFO_FILES_VIDEO_NTSC_STILL2:
      case VCDINFO_FILES_VIDEO_PAL_STILL:
      case VCDINFO_FILES_VIDEO_PAL_STILL2:
        /* Note that we are reading a still frame but haven't
           got to the end.
        */
        p_vcdplayer->i_still = STILL_READING;
        break;
      default:
        /* */
        switch (p_vcdplayer->vcd_format) {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_VCD2:
          /* aspect ratio for VCD's is known to be 4:3 for any
             type of VCD's */
          p_vcdplayer->set_aspect_ratio(1);
          break;
        default: ;
        }
        p_vcdplayer->i_still = 0;
      }

      break;
    }

  case VCDINFO_ITEM_TYPE_TRACK:
    dbg_print(INPUT_DBG_PBC, "track %d\n", itemid.num);
    if (itemid.num < 1 || itemid.num > p_vcdplayer->i_tracks) return;
    _vcdplayer_set_track(p_vcdplayer, itemid.num);
    break;

  case VCDINFO_ITEM_TYPE_ENTRY:
    {
      unsigned int i_entries = vcdinfo_get_num_entries(p_vcdinfo);
      dbg_print(INPUT_DBG_PBC, "entry %d\n", itemid.num);
      if (itemid.num >= i_entries) return;
      _vcdplayer_set_entry(p_vcdplayer, itemid.num);
      break;
    }

  case VCDINFO_ITEM_TYPE_LID:
    LOG_ERR(p_vcdplayer, "%s\n", _("Should have converted this above"));
    break;

  case VCDINFO_ITEM_TYPE_NOTFOUND:
    dbg_print(INPUT_DBG_PBC, "play nothing\n");
    p_vcdplayer->i_lsn = p_vcdplayer->end_lsn;
    return;

  default:
    LOG_ERR(p_vcdplayer, "item type %d not implemented.\n", itemid.type);
    return;
  }

  p_vcdplayer->play_item = itemid;

  vcdplayer_update_nav(p_vcdplayer);

  /* Some players like xine, have a fifo queue of audio and video buffers
     that need to be flushed when playing a new selection. */
  /*  if (p_vcdplayer->flush_buffers)
      p_vcdplayer->flush_buffers(); */

}

/*
  Get the next play-item in the list given in the LIDs. Note play-item
  here refers to list of play-items for a single LID It shouldn't be
  confused with a user's list of favorite things to play or the
  "next" field of a LID which moves us to a different LID.
 */
static bool
_vcdplayer_inc_play_item(vcdplayer_t *p_vcdplayer)
{
  int noi;

  dbg_print(INPUT_DBG_CALL, "called pli: %d\n", p_vcdplayer->pdi);

  if ( NULL == p_vcdplayer || NULL == p_vcdplayer->pxd.pld  ) return false;

  noi = vcdinf_pld_get_noi(p_vcdplayer->pxd.pld);

  if ( noi <= 0 ) return false;

  /* Handle delays like autowait or wait here? */

  p_vcdplayer->pdi++;

  if ( p_vcdplayer->pdi < 0 || p_vcdplayer->pdi >= noi ) return false;

  else {
    uint16_t trans_itemid_num=vcdinf_pld_get_play_item(p_vcdplayer->pxd.pld,
                                                       p_vcdplayer->pdi);
    vcdinfo_itemid_t trans_itemid;

    if (VCDINFO_INVALID_ITEMID == trans_itemid_num) return false;

    vcdinfo_classify_itemid(trans_itemid_num, &trans_itemid);
    dbg_print(INPUT_DBG_PBC, "  play-item[%d]: %s\n",
              p_vcdplayer->pdi, vcdinfo_pin2str (trans_itemid_num));
    vcdplayer_play_single_item(p_vcdplayer, trans_itemid);
    return true;
  }
}

void
vcdplayer_play(vcdplayer_t *p_vcdplayer, vcdinfo_itemid_t itemid)
{
  dbg_print(INPUT_DBG_CALL, "called itemid.num: %d itemid.type: %d\n",
            itemid.num, itemid.type);

  if  (!vcdplayer_pbc_is_on(p_vcdplayer)) {
    vcdplayer_play_single_item(p_vcdplayer, itemid);
  } else {
    /* PBC on - Itemid.num is LID. */

    vcdinfo_obj_t *p_vcdinfo = p_vcdplayer->vcd;

    if (p_vcdinfo == NULL) return;

    p_vcdplayer->i_lid = itemid.num;
    vcdinfo_lid_get_pxd(p_vcdinfo, &(p_vcdplayer->pxd), itemid.num);

    switch (p_vcdplayer->pxd.descriptor_type) {

    case PSD_TYPE_SELECTION_LIST:
    case PSD_TYPE_EXT_SELECTION_LIST: {
      vcdinfo_itemid_t trans_itemid;
      uint16_t trans_itemid_num;

      if (p_vcdplayer->pxd.psd == NULL) return;
      trans_itemid_num  = vcdinf_psd_get_itemid(p_vcdplayer->pxd.psd);
      vcdinfo_classify_itemid(trans_itemid_num, &trans_itemid);
      p_vcdplayer->i_loop    = 1;
      p_vcdplayer->loop_item = trans_itemid;
      vcdplayer_play_single_item(p_vcdplayer, trans_itemid);
      break;
    }

    case PSD_TYPE_PLAY_LIST: {
      if (p_vcdplayer->pxd.pld == NULL) return;
      p_vcdplayer->pdi = -1;
      _vcdplayer_inc_play_item(p_vcdplayer);
      break;
    }

    case PSD_TYPE_END_LIST:
    case PSD_TYPE_COMMAND_LIST:

    default:
      ;
    }
  }
}

/*
   Set's start origin and size for subsequent seeks.
   input: p_vcdplayer->i_lsn, p_vcdplayer->play_item
   changed: p_vcdplayer->origin_lsn, p_vcdplayer->end_lsn
*/
static void
_vcdplayer_set_origin(vcdplayer_t *p_vcdplayer)
{
  size_t size = _vcdplayer_get_item_size(p_vcdplayer, p_vcdplayer->play_item);

  p_vcdplayer->end_lsn    = p_vcdplayer->i_lsn + size;
  p_vcdplayer->origin_lsn = p_vcdplayer->i_lsn;

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_LSN), "end LSN: %u\n",
            p_vcdplayer->end_lsn);
}

#define RETURN_NULL_STILL                       \
  p_vcdplayer->i_still = 127;                   \
  memset (p_buf, 0, M2F2_SECTOR_SIZE);          \
  p_buf[0] = 0;  p_buf[1] = 0; p_buf[2] = 0x01; \
  return READ_STILL_FRAME

/* Handles PBC navigation when reaching the end of a play item. */
static vcdplayer_read_status_t
vcdplayer_pbc_nav (vcdplayer_t *p_vcdplayer, uint8_t *p_buf)
{
  /* We are in playback control. */
  vcdinfo_itemid_t itemid;

  /* The end of an entry is really the end of the associated
     sequence (or track). */

  if ( (VCDINFO_ITEM_TYPE_ENTRY == p_vcdplayer->play_item.type) &&
       (p_vcdplayer->i_lsn < p_vcdplayer->track_end_lsn) ) {
    /* Set up to just continue to the next entry */
    p_vcdplayer->play_item.num++;
    dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC),
               "continuing into next entry: %u\n", p_vcdplayer->play_item.num);
    vcdplayer_play_single_item(p_vcdplayer, p_vcdplayer->play_item);
    p_vcdplayer->update_title();
    return READ_BLOCK;
  }

  switch (p_vcdplayer->pxd.descriptor_type) {
  case PSD_TYPE_END_LIST:
    return READ_END;
    break;
  case PSD_TYPE_PLAY_LIST: {
    int wait_time = vcdinf_get_wait_time(p_vcdplayer->pxd.pld);

    dbg_print(INPUT_DBG_PBC, "playlist wait_time: %d\n", wait_time);

    if (_vcdplayer_inc_play_item(p_vcdplayer))
      return READ_BLOCK;

    /* This needs to be improved in libvcdinfo when I get around to it.
     */
    if (-1 == wait_time) wait_time = STILL_INDEFINITE_WAIT;

    /* Set caller to handle wait time given. */
    if (STILL_READING == p_vcdplayer->i_still && wait_time > 0) {
      p_vcdplayer->i_still = wait_time;
      return READ_STILL_FRAME;
    }
    break;
  }
  case PSD_TYPE_SELECTION_LIST:     /* Selection List (+Ext. for SVCD) */
  case PSD_TYPE_EXT_SELECTION_LIST: /* Extended Selection List (VCD2.0) */
    {
      int wait_time         = vcdinf_get_timeout_time(p_vcdplayer->pxd.psd);
      uint16_t timeout_offs = vcdinf_get_timeout_offset(p_vcdplayer->pxd.psd);
      uint16_t max_loop     = vcdinf_get_loop_count(p_vcdplayer->pxd.psd);
      vcdinfo_offset_t *offset_timeout_LID =
        vcdinfo_get_offset_t(p_vcdplayer->vcd, timeout_offs);

      dbg_print(INPUT_DBG_PBC, "wait_time: %d, looped: %d, max_loop %d\n",
                wait_time, p_vcdplayer->i_loop, max_loop);

      /* Set caller to handle wait time given. */
      if (STILL_READING == p_vcdplayer->i_still && wait_time > 0) {
        p_vcdplayer->i_still = wait_time;
        return READ_STILL_FRAME;
      }

      /* Handle any looping given. */
      if ( max_loop == 0 || p_vcdplayer->i_loop < max_loop ) {
        p_vcdplayer->i_loop++;
        if (p_vcdplayer->i_loop == 0x7f) p_vcdplayer->i_loop = 0;
        vcdplayer_play_single_item(p_vcdplayer, p_vcdplayer->loop_item);
        if (p_vcdplayer->i_still) p_vcdplayer->force_redisplay();
        return READ_BLOCK;
      }

      /* Looping finished and wait finished. Move to timeout
         entry or next entry, or handle still. */

      if (NULL != offset_timeout_LID) {
        /* Handle timeout_LID */
        itemid.num  = offset_timeout_LID->lid;
        itemid.type = VCDINFO_ITEM_TYPE_LID;
        dbg_print(INPUT_DBG_PBC, "timeout to: %d\n", itemid.num);
        vcdplayer_play(p_vcdplayer, itemid);
        return READ_BLOCK;
      } else {
        int i_selections = vcdinf_get_num_selections(p_vcdplayer->pxd.psd);
        if (i_selections > 0) {
          /* Pick a random selection. */
          unsigned int bsn=vcdinf_get_bsn(p_vcdplayer->pxd.psd);
          int rand_selection=bsn +
            (int) ((i_selections+0.0)*rand()/(RAND_MAX+1.0));
          lid_t rand_lid=vcdinfo_selection_get_lid(p_vcdplayer->vcd,
                                                   p_vcdplayer->i_lid,
                                                   rand_selection);
          itemid.num = rand_lid;
          itemid.type = VCDINFO_ITEM_TYPE_LID;
          dbg_print(INPUT_DBG_PBC, "random selection %d, lid: %d\n",
                    rand_selection - bsn, rand_lid);
          vcdplayer_play(p_vcdplayer, itemid);
          return READ_BLOCK;
        } else if (p_vcdplayer->i_still > 0) {
          /* Hack: Just go back and do still again */
          RETURN_NULL_STILL ;
        }
      }

      break;
    }
  case VCDINFO_ITEM_TYPE_NOTFOUND:
    LOG_ERR(p_vcdplayer, "NOTFOUND in PBC -- not supposed to happen\n");
    break;
  case VCDINFO_ITEM_TYPE_SPAREID2:
    LOG_ERR(p_vcdplayer, "SPAREID2 in PBC -- not supposed to happen\n");
    break;
  case VCDINFO_ITEM_TYPE_LID:
    LOG_ERR(p_vcdplayer, "LID in PBC -- not supposed to happen\n");
    break;

  default:
    ;
  }
  /* FIXME: Should handle autowait ...  */
  itemid.num  = p_vcdplayer->next_entry;
  itemid.type = VCDINFO_ITEM_TYPE_LID;
  vcdplayer_play(p_vcdplayer, itemid);
  return READ_BLOCK;
}

/* Handles navigation when NOT in PBC reaching the end of a play item.
   The navigations rules here we are sort of made up, but the intent
   is to do something that's probably right or helpful.
*/
static vcdplayer_read_status_t
vcdplayer_non_pbc_nav (vcdplayer_t *p_vcdplayer, uint8_t *p_buf)
{
  /* Not in playback control. Do we advance automatically or stop? */
  switch (p_vcdplayer->play_item.type) {
  case VCDINFO_ITEM_TYPE_TRACK:
  case VCDINFO_ITEM_TYPE_ENTRY:
    if (p_vcdplayer->autoadvance
        && p_vcdplayer->next_entry != VCDINFO_INVALID_ENTRY) {
      p_vcdplayer->play_item.num=p_vcdplayer->next_entry;
      vcdplayer_update_nav(p_vcdplayer);
    } else
      return READ_END;
    break;
  case VCDINFO_ITEM_TYPE_SPAREID2:
    RETURN_NULL_STILL ;

  case VCDINFO_ITEM_TYPE_NOTFOUND:
    LOG_ERR(p_vcdplayer, "NOTFOUND outside PBC -- not supposed to happen\n");
    return READ_END;
    break;

  case VCDINFO_ITEM_TYPE_LID:
    LOG_ERR(p_vcdplayer, "LID outside PBC -- not supposed to happen\n");
    return READ_END;
    break;

  case VCDINFO_ITEM_TYPE_SEGMENT:
    /* Hack: Just go back and do still again */
    RETURN_NULL_STILL ;
  }
  return READ_BLOCK;
}


/*!
  Read i_len bytes into buf and return the status back.

  This routine is a bit complicated because on reaching the end of
  a track or entry we may automatically advance to the item, or
  interpret the next item in the playback-control list.
*/
vcdplayer_read_status_t
vcdplayer_read (vcdplayer_t *p_vcdplayer, uint8_t *p_buf,
                const off_t i_len)
{

  if ( p_vcdplayer->i_lsn >= p_vcdplayer->end_lsn ) {
    vcdplayer_read_status_t read_status;

    /* We've run off of the end of this entry. Do we continue or stop? */
    dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC),
              "end reached, cur: %u, end: %u\n",
               p_vcdplayer->i_lsn, p_vcdplayer->end_lsn);

  handle_item_continuation:
    read_status = vcdplayer_pbc_is_on(p_vcdplayer)
      ? vcdplayer_pbc_nav(p_vcdplayer, p_buf)
      : vcdplayer_non_pbc_nav(p_vcdplayer, p_buf);

    if (READ_STILL_FRAME == read_status) {
      *p_buf = p_vcdplayer->i_still;
      return READ_STILL_FRAME;
    }
    if (READ_BLOCK != read_status) return read_status;
  }

  /* Read the next block.

    Important note: we probably speed things up by removing "data"
    and the memcpy to it by extending vcd_image_source_read_mode2
    to allow a mode to do what's below in addition to its
    "raw" and "block" mode. It also would probably improve the modularity
    a little bit as well.
  */

  {
    CdIo_t *p_img = vcdinfo_get_cd_image(p_vcdplayer->vcd);
    typedef struct {
      uint8_t subheader	[CDIO_CD_SUBHEADER_SIZE];
      uint8_t data	[M2F2_SECTOR_SIZE];
      uint8_t spare     [4];
    } vcdsector_t;
    vcdsector_t vcd_sector;

    do {
      if (cdio_read_mode2_sector(p_img, &vcd_sector,
				 p_vcdplayer->i_lsn, true)!=0) {
        dbg_print(INPUT_DBG_LSN, "read error\n");
	p_vcdplayer->i_lsn++;
        return READ_ERROR;
      }
      p_vcdplayer->i_lsn++;

      if ( p_vcdplayer->i_lsn >= p_vcdplayer->end_lsn ) {
        /* We've run off of the end of this entry. Do we continue or stop? */
        dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC),
                   "end reached in reading, cur: %u, end: %u\n",
                   p_vcdplayer->i_lsn, p_vcdplayer->end_lsn);
        break;
      }

      /* Check header ID for a padding sector and simply discard
         these.  It is alleged that VCD's put these in to keep the
         bitrate constant.
      */
    } while((vcd_sector.subheader[2]&~0x01)==0x60);

    if ( p_vcdplayer->i_lsn >= p_vcdplayer->end_lsn )
      /* We've run off of the end of this entry. Do we continue or stop? */
      goto handle_item_continuation;

    memcpy (p_buf, vcd_sector.data, M2F2_SECTOR_SIZE);
    return READ_BLOCK;
  }
}

/* Do if needed */
void
vcdplayer_send_button_update(vcdplayer_t *p_vcdplayer, const int mode)
{
  /* dbg_print(INPUT_DBG_CALL, "Called\n"); */
  return;
}

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
