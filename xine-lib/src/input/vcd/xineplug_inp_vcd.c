/*

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

/*
  These are plugin routines called by the xine engine. See
  Chapter 4. Extending xine's input
  http://www.xine-project.org/hackersguide#INPUT
  and the comments in input_plugin.h

  This is what is referred to below a "the xine plugin spec"

  Please don't add any OS-specific code in here - #if defined(__sun)
  or or #if defined(__linux__) are harbingers of such stuff. It took a
  great deal of effort to get it *out* of here (or most of it); If you
  feel the need to do so, you are doing something wrong and breaking
  modularity.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define SHORT_PLUGIN_NAME "VCD"
#define MRL_PREFIX "vcd://"
#define MRL_PREFIX_LEN (sizeof(MRL_PREFIX) - 1)
#define MAX_DEVICE_LEN 1024

#define xine_config_entry_t xine_cfg_entry_t

/* Xine includes */
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#include "xine-extra.h"

#ifdef HAVE_VCDNAV
#include <cdio/logging.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>
#include <cdio/version.h>

/* libvcd includes */
#include <libvcd/files.h>
#include <libvcd/logging.h>
#else
#include "cdio/logging.h"
#include "cdio/iso9660.h"
#include "cdio/cd_types.h"
/* libvcd includes */
#include "libvcd/files.h"
#include "libvcd/logging.h"
#endif

#include "vcdplayer.h"
#include "vcdio.h"

/* A xine define. */
#ifndef BUF_DEMUX_BLOCK
#define BUF_DEMUX_BLOCK 0x05000000
#endif

/*
   Convert an autoplay enumeration into an vcdinfo itemtype enumeration.
   See definitions in vcdplayer.h and vcdinfo.h to get the below correct.
*/
static const vcdinfo_item_enum_t autoplay2itemtype[]={
  VCDINFO_ITEM_TYPE_TRACK,   /* VCDPLAYER_AUTOPLAY_TRACK */
  VCDINFO_ITEM_TYPE_ENTRY,   /* VCDPLAYER_AUTOPLAY_ENTRY */
  VCDINFO_ITEM_TYPE_SEGMENT, /* VCDPLAYER_AUTOPLAY_SEGMENT */
  VCDINFO_ITEM_TYPE_LID      /* VCDPLAYER_AUTOPLAY_PBC */
};

typedef struct vcd_config_s
{
  char         *title_format;        /* Format string of GUI display title */
  char         *comment_format;      /* Format string of stream comment meta */
} vcd_config_t;

typedef struct vcd_input_plugin_tag vcd_input_plugin_t;

typedef struct vcd_input_class_s {
  input_class_t        input_class;
  xine_t              *xine;
  config_values_t     *config;     /* Pointer to XineRC config file. */
  vcd_input_plugin_t  *ip;

  vcd_config_t         v_config;  /* config stuff passed to child */
  xine_mrl_t           **mrls;    /* list of mrl entries for medium */
  int                  num_mrls;  /* count of above */
  char                *vcd_device;/* Device name to use when
				     none specified in MRL    */
  /*--------------------------------------------------------------
    Media resource locator (MRL) info.

    For the below offsets, use play_item + mrl_xxx_offset to get index
    into "mrls" array
   ---------------------------------------------------------------*/
  int        mrl_track_offset;    /* perhaps -1 for tracks staring with 1*/
  int        mrl_entry_offset;    /* i_tracks for entries starting with 0 */
  int        mrl_play_offset;     /* i_tracks for entries starting with 0 */
  int        mrl_segment_offset;  /* i_tracks + i_entries if segs start 1*/


} vcd_input_class_t;

vcd_input_class_t *vcd_class;

struct vcd_input_plugin_tag {
  input_plugin_t    input_plugin; /* input plugin interface as defined by
                                     by player. For xine it contains a
                                     structure of functions that need
                                     to be implemented.
                                  */
  xine_stream_t      *stream;
  xine_event_queue_t *event_queue;

  time_t	      pause_end_time;
  int                 i_old_still; /* Value of player-i_still before next read.
                                      See also i_still in vcdplayer structure.
                                    */
  int                 i_old_deinterlace; /* value of deinterlace before
                                            entering a still. */

  vcd_input_class_t  *class;
  vcd_config_t        v_config;    /* Config stuff initially inherited   */
  char               *mrl;

  int32_t             i_mouse_button; /* The "button" number associated
                                         with the region that the mouse
                                         is currently located in. If
                                         the mouse is not in any "button"
                                         region then this has value -1.
                                      */
  bool                b_mouse_in;     /* True if mouse is inside a "button"
                                         region; false otherwise */

  vcdplayer_t         player ;
  char               *player_device;
};

vcd_input_plugin_t  my_vcd;

/* Prototype definitions */
static bool vcd_handle_events (void);
static void vcd_close(vcd_input_class_t *class);
#if LIBVCD_VERSION_NUM >= 23
static void send_mouse_enter_leave_event(vcd_input_plugin_t *p_this,
                                         bool b_mouse_in);
#endif

/*
   If class->vcd_device is NULL or the empty string,
   Use libcdio to find a CD drive with a VCD in it.
*/
static bool
vcd_get_default_device(vcd_input_class_t *class, bool log_msg_if_fail)
{
  dbg_print(INPUT_DBG_CALL, "Called with %s\n",
            log_msg_if_fail ? "True" : "False");

  if (NULL == class->vcd_device || strlen(class->vcd_device)==0) {
    char **cd_drives=NULL;
    cd_drives = cdio_get_devices_with_cap(NULL,
(CDIO_FS_ANAL_SVCD|CDIO_FS_ANAL_CVD|CDIO_FS_ANAL_VIDEOCD|CDIO_FS_UNKNOWN),
					true);
    if (NULL == cd_drives || NULL == cd_drives[0]) {
      LOG_MSG("%s", _("failed to find a device with a VCD"));
      return false;
    }
    class->vcd_device = strdup(cd_drives[0]);
    cdio_free_device_list(cd_drives);
#if LIBCDIO_VERSION_NUM <= 72
    free(cd_drives);
#endif
  }
  return true;
}


static void
meta_info_assign(int field, xine_stream_t *stream, const char * info)
{
  if (NULL != info) {
    dbg_print(INPUT_DBG_META, "meta[%d]: %s\n", field, info);
    _x_meta_info_set(stream, field, info);
  }
}

#define stream_info_assign(field, stream, info) \
  _x_stream_info_set(stream, field, info);

/* Set stream information. */
static void
vcd_set_meta_info (vcd_input_plugin_t *xine_vcd)
{
  vcdinfo_obj_t *p_vcdinfo= xine_vcd->player.vcd;
  meta_info_assign(XINE_META_INFO_ALBUM, xine_vcd->stream,
                   vcdinfo_get_album_id(p_vcdinfo));
  meta_info_assign(XINE_META_INFO_ARTIST,  xine_vcd->stream,
                   vcdinfo_get_preparer_id(p_vcdinfo));
  meta_info_assign(XINE_META_INFO_COMMENT, xine_vcd->stream,
                   vcdplayer_format_str(&xine_vcd->player,
                                   xine_vcd->v_config.comment_format));
  meta_info_assign(XINE_META_INFO_GENRE, xine_vcd->stream,
                   vcdinfo_get_format_version_str(p_vcdinfo));
}

static void
vcd_force_redisplay (void)
{
#if 1
    my_vcd.stream->xine->clock->adjust_clock(my_vcd.stream->xine->clock,
    my_vcd.stream->xine->clock->get_current_time(my_vcd.stream->xine->clock)
     + 30 * 90000 );
#else
    /* Alternate method that causes too much disruption... */
    xine_set_param(my_vcd.stream, XINE_PARAM_VO_ASPECT_RATIO,
		 (xine_get_param(my_vcd.stream, XINE_PARAM_VO_ASPECT_RATIO)));
#endif
}

static void
vcd_set_aspect_ratio (int i_aspect_ratio)
{
  /* Alternate method that causes too much disruption... */
  xine_set_param(my_vcd.stream, XINE_PARAM_VO_ASPECT_RATIO, i_aspect_ratio);
}

/*! Add another MRL to the MRL list inside "this" to be displayed.
   mrl is the string name to add; size is the size of the entry in bytes.
   The number of mrls in "this" is incremented.
*/
static void
vcd_add_mrl_slot(vcd_input_class_t *this, const char *mrl, off_t size,
                 unsigned int *i)
{
  dbg_print(INPUT_DBG_MRL, "called to add slot %d: %s, size %u\n",
              *i, mrl, (unsigned int) size);

  this->mrls[*i] = malloc(sizeof(xine_mrl_t));
  if (NULL==this->mrls[*i]) {
    LOG_ERR("Can't malloc %zu bytes for MRL slot %u (%s)",
            sizeof(xine_mrl_t), *i, mrl);
    return;
  }
  this->mrls[*i]->link   = NULL;
  this->mrls[*i]->origin = NULL;
  this->mrls[*i]->type   = mrl_vcd;
  this->mrls[*i]->size   = size * M2F2_SECTOR_SIZE;

  this->mrls[*i]->mrl = strdup(mrl);
  if (NULL==this->mrls[*i]->mrl) {
    LOG_ERR("Can't strndup %zu bytes for MRL name %s", strlen(mrl), mrl);
  }
  (*i)++;
}

/*!
  Return the associated mrl_offset for the given type.
*/
static int
vcd_get_mrl_type_offset(vcd_input_plugin_t *inp,
                        vcdinfo_item_enum_t type, int *size)
{
  switch (type) {
  case VCDINFO_ITEM_TYPE_ENTRY:
    *size = inp->class->mrl_play_offset - inp->class->mrl_entry_offset + 1;
    return inp->class->mrl_entry_offset;
    break;
  case VCDINFO_ITEM_TYPE_SEGMENT:
    *size = inp->class->num_mrls - inp->class->mrl_segment_offset - 1;
    return inp->class->mrl_segment_offset;
  case VCDINFO_ITEM_TYPE_TRACK:
    *size = inp->class->mrl_entry_offset;
    return inp->class->mrl_track_offset;
  case VCDINFO_ITEM_TYPE_LID:
    /* Play list number (LID) */
    *size = (inp->player.i_lids > 0) ? 1 : 0;
    return inp->class->mrl_play_offset;
  case VCDINFO_ITEM_TYPE_NOTFOUND:
  case VCDINFO_ITEM_TYPE_SPAREID2:
  default:
    return -2;
  }
}

/*!
   Create a MRL list inside "class". Any existing MRL list is freed.
 */
static bool
vcd_build_mrl_list(vcd_input_class_t *class, char *vcd_device)
{

  char mrl[MRL_PREFIX_LEN+MAX_DEVICE_LEN+(sizeof("@E")-1)+12];
  vcdplayer_t *vcdplayer;
  unsigned int n, i=0;
  unsigned int i_entries;
  vcdinfo_obj_t *p_vcdinfo;
  int was_open;

  if (NULL == class) {
    LOG_MSG("%s", _("was passed a null class parameter"));
    return false;
  }

  vcdplayer = &(my_vcd.player);

  /* If VCD already open, we gotta close and stop it. */
  if ((was_open = vcdplayer->b_opened)) {
    vcd_close(class);
  }

  if (NULL == vcd_device) {
    if (!vcd_get_default_device(class, true)) return false;
    vcd_device = class->vcd_device;
  }

  if (!vcdio_open(vcdplayer, vcd_device)) {
    /* Error should have been logged  in vcdio_open. If not do the below:
    LOG_ERR(vcdplayer, "%s: %s.\n", _("unable to open"),
            class->vcd_device, strerror(errno));
    */
    return false;
  }

  free (my_vcd.player_device);
  my_vcd.player_device    = strdup (vcd_device);
  p_vcdinfo               = vcdplayer->vcd;
  i_entries               = vcdplayer->i_entries;
  class->mrl_track_offset = -1;

  xine_free_mrls(&(class->num_mrls), class->mrls);

  /* Figure out number of MRLs. Calculation would be real simple if
     didn't have to possibly remove rejected LIDs from list done in the
     loop below.
   */
  class->num_mrls = vcdplayer->i_tracks + vcdplayer->i_entries
    + vcdplayer->i_segments + vcdplayer->i_lids;

  if (!vcdplayer->show_rejected && vcdinfo_get_lot(vcdplayer->vcd)) {
    /* Remove rejected LIDs from count. */
    for (n=0; n<vcdplayer->i_lids; n++) {
      if ( vcdinf_get_lot_offset(vcdinfo_get_lot(vcdplayer->vcd), n)
           == PSD_OFS_DISABLED )
        class->num_mrls--;
    }
  }

  class->mrls = calloc(class->num_mrls, sizeof(xine_mrl_t *));
  if (NULL == class->mrls) {
    LOG_ERR("Can't calloc %d MRL entries", class->num_mrls);
    class->num_mrls = 0;
    if (!was_open)
      vcdio_close(vcdplayer);
    return false;
  }

  /* Record MRL's for tracks */
  for (n=1; n<=vcdplayer->i_tracks; n++) {
    memset(&mrl, 0, sizeof (mrl));
    snprintf(mrl, sizeof(mrl), "%s%s@T%u", MRL_PREFIX, vcd_device, n);
    vcd_add_mrl_slot(class, mrl, vcdplayer->track[n-1].size, &i);
  }

  class->mrl_entry_offset = vcdplayer->i_tracks;
  class->mrl_play_offset  = class->mrl_entry_offset + i_entries - 1;

  /* Record MRL's for entries */
  if (i_entries > 0) {
    for (n=0; n<i_entries; n++) {
      memset(&mrl, 0, sizeof (mrl));
      snprintf(mrl, sizeof(mrl), "%s%s@E%u", MRL_PREFIX, vcd_device, n);
      vcd_add_mrl_slot(class, mrl, vcdplayer->entry[n].size, &i);
    }
  }

  /* Record MRL's for LID entries or selection entries*/
  class->mrl_segment_offset = class->mrl_play_offset;
  if (vcdinfo_get_lot(vcdplayer->vcd)) {
    for (n=0; n<vcdplayer->i_lids; n++) {
      uint16_t ofs = vcdinf_get_lot_offset(vcdinfo_get_lot(vcdplayer->vcd), n);
      if (ofs != PSD_OFS_DISABLED || vcdplayer->show_rejected) {
        memset(&mrl, 0, sizeof (mrl));
        snprintf(mrl, sizeof(mrl), "%s%s@P%u%s", MRL_PREFIX, vcd_device, n+1,
                ofs == PSD_OFS_DISABLED ? "*" : "");
        vcd_add_mrl_slot(class, mrl, 0, &i);
        class->mrl_segment_offset++;
      }
    }
  }

  /* Record MRL's for segments */
  {
    segnum_t i_segments = vcdplayer->i_segments;
    for (n=0; n<i_segments; n++) {
      vcdinfo_video_segment_type_t segtype
        = vcdinfo_get_video_type(p_vcdinfo, n);
      char c='S';
      switch (segtype) {
        {
        case VCDINFO_FILES_VIDEO_NTSC_STILL:
        case VCDINFO_FILES_VIDEO_NTSC_STILL2:
        case VCDINFO_FILES_VIDEO_NTSC_MOTION:
          c='s';
          break;
        case VCDINFO_FILES_VIDEO_PAL_STILL:
        case VCDINFO_FILES_VIDEO_PAL_STILL2:
        case VCDINFO_FILES_VIDEO_PAL_MOTION:
          c='S';
          break;
        default: ;
        }
      }

      memset(&mrl, 0, sizeof (mrl));
      snprintf(mrl, sizeof(mrl), "%s%s@%c%u", MRL_PREFIX, vcd_device, c, n);
      vcd_add_mrl_slot(class, mrl, vcdplayer->segment[n].size, &i);
    }
  }

  dbg_print(INPUT_DBG_MRL,
            "offsets are track: %d, entry: %d, play: %d seg: %d\n",
            class->mrl_track_offset, class->mrl_entry_offset,
            class->mrl_play_offset,  class->mrl_segment_offset);

  return true;
}

/*!
  parses a MRL which has the format

  vcd://[vcd_path][@[EPTS]?number]\*?

  Examples
    vcd://                    - Play (navigate) default device: /dev/cdrom
    vcd://@                   - same as above
    vcd:///dev/cdrom          - probably same as above
    vcd:///dev/cdrom2         - Play (navigate) /dev/cdrom2
    vcd:///dev/cdrom2@        - same as above
    vcd:///dev/cdrom2@T1      - Play Track 1 from /dev/cdrom2
    vcd:///dev/cdrom@S1       - Play selection id 1 from /dev/cdrom
    vcd://dev/cdrom@E0        - Play Entry id 0 from default device
    vcd://@P1                 - probably same as above.
                                 If there is no playback control, MRL will
			         get converted into vcd://@E0
    vcd://@P1*                - probably same as above.
    vcd://@S0                 - Play segment 0 from default device
    vcd://@3                  - Play track 3 from default device
    vcd:///dev/cdrom2@1       - Play track 1 from /dev/cdrom2
    vcd:///tmp/ntsc.bin@      - Play default item from /tmp/ntsc.bin
    vcd:///tmp/ntsc.bin/@E0   - Play entry 0 of /tmp/ntsc.bin

parameters:
  mrl               : mrl to parse
  default_vcd_device: name of device to use when none given
  auto_type         : type of selection (entry, track, LID) when none given
  used_default      : true iff auto_type was used.

 */
static bool
vcd_parse_mrl(/*in*/ const char *default_vcd_device, /*in*/ char *mrl,
              /*out*/ char *device_str, /*out*/ vcdinfo_itemid_t *itemid,
              /*in */ vcdplayer_autoplay_t auto_type,
              /*out*/ bool *used_default)
{
  char type_str[2];
  int count;
  char *p;
  unsigned int num = 0;

  dbg_print(INPUT_DBG_CALL, "called mrl %s\n", mrl);

  type_str[0]   ='\0';
  itemid->type  = (vcdinfo_item_enum_t) auto_type;
  *used_default = false;

  if ( NULL == mrl || strncasecmp(mrl, MRL_PREFIX, MRL_PREFIX_LEN) )
    return false;
  p = &mrl[MRL_PREFIX_LEN - 2];
  while (*p == '/')
    ++p;

  device_str[0] = '/';
  device_str[1] = 0;
  count = sscanf (p, "%1023[^@]@%1[EePpSsTt]%u",
		  device_str + 1, type_str, &num);
  itemid->num = num;

  switch (count) {
  case 1:
    /* Matched device, but nothing beyond that */
    if (strlen(device_str)!=0 && device_str[0] != ':') {
      /* See if we have old-style MRL with no type specifier.
         If so, we assume "track". */
      count = sscanf (p, "%u", &num);
      itemid->num = num;
      if (1==count) {
        type_str[0] = 'T';
        if (default_vcd_device)
          strncpy(device_str, default_vcd_device, MAX_DEVICE_LEN);
        else
          *device_str = 0;
      }
      else
        _x_mrl_unescape (device_str);
      break;
    }
  case 2 ... 9:
    _x_mrl_unescape (device_str);

  case 0:
  case EOF:
    {
      /* No device/file given, so use the default device and try again. */
      if (NULL == default_vcd_device) return false;
      strncpy(device_str, default_vcd_device, MAX_DEVICE_LEN);
      if (p[0] == '@') p++;
      count = sscanf (p, "%1[EePpSsTt]%u", type_str, &num);
      type_str[0] = toupper(type_str[0]);
      itemid->num = num;

      switch (count) {
      case EOF:
	/* Default PBC navigation. */
        return true;
      case 0:
        /* See if we have old-style MRL with no type specifier.
           If so, we assume "track". */
        count = sscanf (p, "%u", &num);
        if (1==count) {
          type_str[0] = 'T';
          break;
        }
	/* Default PBC navigation. */
	return true;
      case 1:
        /* Type given, but no number. Entries start at 0, other things
           start at 1 */
	if (type_str[0] == 'P' || type_str[0] == 'T') itemid->num = 1;
      }
    }
  }

  /* We have some sort of track/selection/entry number */
  switch (type_str[0]) {
  case 'E':
    itemid->type = VCDINFO_ITEM_TYPE_ENTRY;
    break;
  case '\0':
    /* None specified, use config value. */
    itemid->type = (vcdinfo_item_enum_t) auto_type;
    *used_default = true;
    break;
  case 'P':
    itemid->type = VCDINFO_ITEM_TYPE_LID;
    break;
  case 'S':
    itemid->type = VCDINFO_ITEM_TYPE_SEGMENT;
    break;
  case 'T':
    itemid->type = VCDINFO_ITEM_TYPE_TRACK;
    break;
  default: ;
  }

  if ( 0==itemid->num
       && ( (VCDINFO_ITEM_TYPE_LID == itemid->type)
            || (VCDINFO_ITEM_TYPE_TRACK == itemid->type) ) )
    itemid->num = 1;

  return true;
}

/*!
  From xine plugin spec:

  return capabilities of input source
*/
static uint32_t
vcd_plugin_get_capabilities (input_plugin_t *this_gen)
{

  uint32_t ret =
    INPUT_CAP_AUDIOLANG | INPUT_CAP_BLOCK     |
    INPUT_CAP_CHAPTERS  | INPUT_CAP_PREVIEW   |
    (my_vcd.player.i_still ? 0: INPUT_CAP_SEEKABLE) |
    INPUT_CAP_SPULANG;

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "returning %d\n", ret);
  vcd_handle_events();
  return ret;
}

# if FINISHED
/* If needed, will fill out later... */
static void
vcd_read_ahead_cb(void *this_gen, xine_cfg_entry_t *entry)
{
   return;
}
#endif

static void
vcd_flush_buffers(void)
{
  _x_demux_flush_engine(my_vcd.stream);
}

/*!
  From xine plugin spec:

  read nlen bytes, return number of bytes read.
*/
static off_t
vcd_plugin_read (input_plugin_t *this_gen, char *buf, const off_t nlen)
{

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT),
            "Called with nlen %u\n", (unsigned int) nlen);

  /* FIXME: Tricking the demux_mpeg_block plugin */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0x01;
  buf[3] = 0xba;
  return (off_t) 1;
}

/* Allocate and return a no-op buffer. This signals the outside
   to do nothing, but in contrast to returning NULL, it doesn't
   mean the stream has ended. We use this say for still frames.
 */
#define RETURN_NOOP_BUF                                    \
  p_buf = fifo->buffer_pool_alloc (fifo);                  \
  p_buf->type = BUF_CONTROL_NOP;                           \
  return p_buf

/* Handle keyboard events and if there were non which might affect
   playback, then sleep a little bit and return;
 */
#define SLEEP_AND_HANDLE_EVENTS                          \
  xine_usec_sleep(50000);                                \
  if (vcd_handle_events()) goto read_block;              \
  RETURN_NOOP_BUF

/*!
  From xine plugin spec:

  read one block, return newly allocated block (or NULL on failure)
  for blocked input sources len must be == blocksize the fifo
  parameter is only used to get access to the buffer_pool_alloc
  function
*/
static buf_element_t *
vcd_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
                       const off_t i_len)
{
  vcd_input_plugin_t *vcd_input_plugin= (vcd_input_plugin_t *) this_gen;
  vcdplayer_t   *p_vcdplayer = &my_vcd.player;
  buf_element_t *p_buf;
  uint8_t        data[M2F2_SECTOR_SIZE] = {0};

  if (fifo == NULL) {
    dbg_print(INPUT_DBG_CALL, "NULL fifo");
    return NULL;
  }

  dbg_print(INPUT_DBG_CALL, "Called with i_len %u\n", (unsigned int) i_len);

  /* Should we change this to <= instead of !=? */
  if (i_len != M2F2_SECTOR_SIZE) return NULL;

  /* If VCD isn't open, we need to open it now. */
  if (!p_vcdplayer->b_opened) {
    if (!vcdio_open(p_vcdplayer, my_vcd.player_device)) {
      return NULL;
    }
  }

  if (vcd_handle_events()) goto read_block;

  if (p_vcdplayer->i_still > 0) {
    if ( time(NULL) >= vcd_input_plugin->pause_end_time ) {
      if (STILL_INDEFINITE_WAIT == p_vcdplayer->i_still) {
        dbg_print(INPUT_DBG_STILL, "Continuing still indefinite wait time\n");
        vcd_input_plugin->pause_end_time = time(NULL) + p_vcdplayer->i_still;
        SLEEP_AND_HANDLE_EVENTS;
      } else {
        dbg_print(INPUT_DBG_STILL, "Still time ended\n");
        p_vcdplayer->i_still = 0;
      }
    } else {
      SLEEP_AND_HANDLE_EVENTS;
    }
  }


 read_block:
  switch (vcdplayer_read(p_vcdplayer, data, i_len)) {
  case READ_END:
    /* End reached. Return NULL to indicated this. */
    return NULL;
  case READ_ERROR:
    /* Some sort of error. */
    return NULL;
  case READ_STILL_FRAME:
    {
      dbg_print(INPUT_DBG_STILL, "Handled still event wait time %u\n",
                p_vcdplayer->i_still);
      vcd_input_plugin->pause_end_time = time(NULL) + p_vcdplayer->i_still;
      RETURN_NOOP_BUF;
    }

  default:
  case READ_BLOCK:
    /* Read buffer */
    p_buf = fifo->buffer_pool_alloc (fifo);
    p_buf->type = BUF_DEMUX_BLOCK;
  }

  p_buf->content = p_buf->mem;

  if (STILL_READING == p_vcdplayer->i_still && 0 == my_vcd.i_old_still) {
    my_vcd.i_old_deinterlace = xine_get_param(my_vcd.stream,
                                              XINE_PARAM_VO_DEINTERLACE);
    xine_set_param(my_vcd.stream, XINE_PARAM_VO_DEINTERLACE, 0);
    dbg_print(INPUT_DBG_STILL, "going into still, saving deinterlace %d\n",
              my_vcd.i_old_deinterlace);
  } else if (0 == p_vcdplayer->i_still && 0 != my_vcd.i_old_still) {
    dbg_print(INPUT_DBG_STILL,
              "going out of still, restoring deinterlace\n");
    xine_set_param(my_vcd.stream, XINE_PARAM_VO_DEINTERLACE,
                   my_vcd.i_old_deinterlace);
  }
  my_vcd.i_old_still = p_vcdplayer->i_still;

  /* Ideally this should probably be i_len.  */
  memcpy (p_buf->mem, data, M2F2_SECTOR_SIZE);

  return p_buf;
}

/*!
  From xine plugin spec:

  seek position, return new position

  if seeking failed, -1 is returned
*/
static off_t
vcd_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
  return vcdio_seek (&my_vcd.player, offset, origin);
}


/*!
  From xine plugin spec:
  return length of input (-1 => unlimited, e.g. stream)

  length size is bytes.
*/

static vcdinfo_itemid_t old_play_item = {VCDINFO_ITEM_TYPE_NOTFOUND, 0};

static off_t old_get_length = 0;
static vcdplayer_slider_length_t old_slider_length;

/* This routine is called a bit. Make reasonably fast. */
static off_t
vcd_plugin_get_length (input_plugin_t *this_gen) {

  vcd_input_plugin_t *ip= (vcd_input_plugin_t *) this_gen;
  vcdplayer_t        *vcdplayer = &(ip->player);

  int n = vcdplayer->play_item.num;

  if (vcdplayer->play_item.num == old_play_item.num
      && vcdplayer->play_item.type == old_play_item.type
      && vcdplayer->slider_length == old_slider_length)
    return old_get_length;

  old_slider_length = vcdplayer->slider_length;
  old_play_item     = vcdplayer->play_item;

  switch (vcdplayer->play_item.type) {
  case VCDINFO_ITEM_TYPE_ENTRY:
    switch (vcdplayer->slider_length) {
    case VCDPLAYER_SLIDER_LENGTH_AUTO:
    case VCDPLAYER_SLIDER_LENGTH_ENTRY:
      n += ip->class->mrl_entry_offset;
      break;
    case VCDPLAYER_SLIDER_LENGTH_TRACK:
      n = vcdinfo_get_track(vcdplayer->vcd, n) + ip->class->mrl_track_offset;
      break;
    default:
      /* FIXME? */
      return -1;
    }
    break;
  case VCDINFO_ITEM_TYPE_TRACK:
    n += ip->class->mrl_track_offset;
    break;
  case VCDINFO_ITEM_TYPE_SEGMENT:
    n += ip->class->mrl_segment_offset;
    break;
  case VCDINFO_ITEM_TYPE_LID:
    /* This is the only situation where the size of the current play item
       is not static. It depends what the current play-item is.
     */
    old_get_length = (vcdplayer->end_lsn - vcdplayer->origin_lsn) *
      M2F2_SECTOR_SIZE;
    return old_get_length;
    break;
  case VCDINFO_ITEM_TYPE_NOTFOUND:
  case VCDINFO_ITEM_TYPE_SPAREID2:
  default:
    /* FIXME? */
    return -1;
  }

  if (n >= 0 && n < ip->class->num_mrls) {
    old_get_length = ip->class->mrls[n]->size;
    dbg_print(INPUT_DBG_MRL, "item: %u, slot %u, size %ld\n",
              vcdplayer->play_item.num,
              (unsigned int) n,  (long int) old_get_length);
  }
  return old_get_length;
}

/*!
 * From xine plugin spec:
 * get current position in stream.
 *
 */
static off_t
vcd_plugin_get_current_pos (input_plugin_t *this_gen){
  // trace_print("Called\n");
  return (vcd_plugin_seek (this_gen, 0, SEEK_CUR));
}


/*!
 * From xine plugin spec:
 * return block size of input source (if supported, 0 otherwise)
 */
static uint32_t
vcd_plugin_get_blocksize (input_plugin_t *this_gen) {
  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called\n");

  return M2F2_SECTOR_SIZE;
}

/*!
  From xine plugin spec:
  ls function
  return value: NULL => filename is a file, **char=> filename is a dir

-- This list returned forms the entries of the GUI MRL "browser".
*/

static xine_mrl_t **
vcd_class_get_dir (input_class_t *this_gen, const char *filename,
                    int *num_files) {

  char             intended_vcd_device[MAX_DEVICE_LEN+1]= { '\0', };
  vcdinfo_itemid_t itemid;

  vcd_input_class_t *class = (vcd_input_class_t *) this_gen;
  vcdplayer_t       *vcdplayer= &my_vcd.player;

  bool used_default;

  if (filename == NULL) {
    dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT),
              "called with NULL\n");
    if ( class->mrls != NULL && NULL != class->mrls[0] ) goto have_mrls;

    if ( !vcd_build_mrl_list(class, vcdplayer->psz_source) ) {
      goto no_mrls;
    }
  } else {
    char *mrl = strdup(filename);
    dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT),
              "called with %s\n", filename);
    if (!vcd_get_default_device(class, true)) goto no_mrls;
    if (!vcd_parse_mrl(class->vcd_device, mrl,
                       intended_vcd_device, &itemid,
                       vcdplayer->default_autoplay, &used_default)) {
      free (mrl);
      goto no_mrls;
    }
    free (mrl);
  }

 have_mrls:
  *num_files = class->num_mrls;
  return class->mrls;

 no_mrls:
  *num_files = 0;
  return NULL;
}

#define FREE_AND_NULL(ptr) if (NULL != ptr) free(ptr); ptr = NULL;

static void
vcd_close(vcd_input_class_t *class)
{
  xine_free_mrls(&(class->num_mrls), class->mrls);
  FREE_AND_NULL(my_vcd.mrl);
  if (my_vcd.player.b_opened)
    vcdio_close(&my_vcd.player);
}


/*!
 * From plugin xine spec:
 * eject/load the media (if it's possible)
 *
 * returns 0 for temporary failures
 */
static int
vcd_class_eject_media (input_class_t *this_gen)
{
  int ret;
  CdIo_t *cdio=vcdinfo_get_cd_image(my_vcd.player.vcd);

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called\n");
  if (NULL == cdio) return 0;

  ret = cdio_eject_media(&cdio);
  if ((ret == 0) || (ret == 2)) {
     if (my_vcd.player.b_opened)
       vcdio_close(&my_vcd.player);
    return 1;
  } else return 0;
}

/*!
 * From spec:
 * return current MRL
 */
static const char *
vcd_plugin_get_mrl (input_plugin_t *this_gen)
{
  vcd_input_plugin_t *t         = (vcd_input_plugin_t *) this_gen;
  vcdplayer_t        *vcdplayer = &my_vcd.player;
  int n;
  int size; /* need something to feed get_mrl_type_offset */
  int offset;

  if (vcdplayer_pbc_is_on(vcdplayer)) {
    n = vcdplayer->i_lid;
    offset = vcd_get_mrl_type_offset(t, VCDINFO_ITEM_TYPE_LID, &size);
  } else {
    n = vcdplayer->play_item.num;
    offset = vcd_get_mrl_type_offset(t, vcdplayer->play_item.type, &size);
  }

  if (-2 == offset) {
    /* Bad type. */
    LOG_ERR("%s %d", _("Invalid current entry type"),
                  vcdplayer->play_item.type);
    return "";
  } else {
    n += offset;
    if (n < t->class->num_mrls) {
      dbg_print(INPUT_DBG_CALL, "Called, returning %s\n",
                t->class->mrls[n]->mrl);
      return t->class->mrls[n]->mrl;
    } else {
      return "";
    }
  }
}

/*
   Handle all queued keyboard/mouse events. Return TRUE if this causes
   a change in the play item.
*/
static bool
vcd_handle_events (void)
{
  vcdplayer_t  *p_vcdplayer = &my_vcd.player;
  xine_event_t *p_event;
  int digit_entered=0;

  /* What you add to the last input number entry. It accumulates all of
     the 10_ADD keypresses */
  static unsigned int number_addend = 0;

  while ((p_event = xine_event_get(my_vcd.event_queue))) {

    dbg_print( (INPUT_DBG_CALL), "processing %d\n", p_event->type );
    digit_entered=0;

    switch(p_event->type) {

    case XINE_EVENT_INPUT_NUMBER_10_ADD:
      number_addend += 10;
      dbg_print(INPUT_DBG_EVENT, "10 added to number. Is now: %d\n",
                number_addend);
      break;

    /* The method used below is oblivious to XINE_EVENT_INPUT encodings
       In particular, it does not assume XINE_EVENT_INPUT_NUMBE_9 =
       XINE_EVENT_INPUT_NUMBER_0 + 9.
     */
    case XINE_EVENT_INPUT_NUMBER_9:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_8:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_7:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_6:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_5:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_4:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_3:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_2:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_1:
      digit_entered++;
    case XINE_EVENT_INPUT_NUMBER_0:
      {
        number_addend *= 10;
        number_addend += digit_entered;
        dbg_print(INPUT_DBG_EVENT,
                  "digit added number is now: %d\n", number_addend);
        break;
      }
    case XINE_EVENT_INPUT_MENU3:
      dbg_print(INPUT_DBG_EVENT, "menu3 setting debug: %d\n", number_addend);
      vcdplayer_debug = number_addend;
      number_addend = 0;
      break;
    case XINE_EVENT_INPUT_MENU1:
    case XINE_EVENT_INPUT_MENU2:
    case XINE_EVENT_INPUT_NEXT:
    case XINE_EVENT_INPUT_PREVIOUS:
      {
        int num = number_addend;
        vcdinfo_itemid_t itemid;

        number_addend = 0;

        /* If no number was given it's really the same as 1, not 0. */
        if (num == 0) num++;

        dbg_print(INPUT_DBG_EVENT,
                  "RETURN/NEXT/PREV/DEFAULT (%d) iteration count %d\n",
                  p_event->type, num);

        for ( ; num > 0; num--) {
          itemid = p_vcdplayer->play_item;
          switch (p_event->type) {
          case XINE_EVENT_INPUT_MENU1:
            if (p_vcdplayer->return_entry == VCDINFO_INVALID_ENTRY) {
              LOG_MSG("%s\n", _("selection has no RETURN entry"));
              return false;
            }
            itemid.num = p_vcdplayer->return_entry;
            dbg_print((INPUT_DBG_PBC|INPUT_DBG_EVENT),
                      "RETURN to %d\n", itemid.num);
            /* Don't loop around -- doesn't make sense to loop a return*/
            num = 0;
            break;
          case XINE_EVENT_INPUT_MENU2:
            if  (vcdplayer_pbc_is_on(p_vcdplayer)) {
              lid_t lid=vcdinfo_get_multi_default_lid(p_vcdplayer->vcd,
                                                      p_vcdplayer->i_lid,
                                                      p_vcdplayer->i_lsn);
              if (VCDINFO_INVALID_LID != lid) {
                itemid.num = lid;
                dbg_print((INPUT_DBG_PBC|INPUT_DBG_EVENT),
                          "DEFAULT to %d\n", itemid.num);
              } else {
                dbg_print((INPUT_DBG_PBC|INPUT_DBG_EVENT),
                          "no DEFAULT for LID %d\n",
                          p_vcdplayer->i_lid);
              }

              /* Don't loop around -- doesn't make sense to loop a return*/
              num = 0;
            } else {
              /* PBC is not on. "default" selection beginning of current
                 selection . Alternative: */
              LOG_MSG("%s\n", _("DEFAULT selected, but PBC is not on."));
            }
            break;
          case XINE_EVENT_INPUT_NEXT:
            if (p_vcdplayer->next_entry == VCDINFO_INVALID_ENTRY) {
              LOG_MSG("%s\n", _("selection has no NEXT entry"));
              return false;
            }
            itemid.num = p_vcdplayer->next_entry;
            dbg_print(INPUT_DBG_PBC, "NEXT to %d\n", itemid.num);
            break;
          case XINE_EVENT_INPUT_PREVIOUS:
            if (p_vcdplayer->prev_entry == VCDINFO_INVALID_ENTRY) {
              LOG_MSG("%s\n", _("selection has no PREVIOUS entry"));
              return false;
            }
            itemid.num = p_vcdplayer->prev_entry;
            dbg_print(INPUT_DBG_PBC, "PREVIOUS to %d\n", itemid.num);
            break;
          default:
            LOG_MSG("%s %d\n", _("Unknown event type: "), p_event->type);
          }
          _x_demux_flush_engine(my_vcd.stream);
          vcdplayer_play(p_vcdplayer, itemid);
          return true;
        }
        break;
      }
    case XINE_EVENT_INPUT_SELECT:
      {
        /* In the future will have to test to see if we are in a menu
           selection. But if not... */
        vcdinfo_itemid_t itemid = p_vcdplayer->play_item;

        itemid.num  = number_addend;
        number_addend = 0;

        if (vcdplayer_pbc_is_on(p_vcdplayer)) {
          lid_t i_next=vcdinfo_selection_get_lid(p_vcdplayer->vcd,
                                                 p_vcdplayer->i_lid,
                                                 itemid.num);
          if (VCDINFO_INVALID_LID != i_next) {
            itemid.num = i_next;
            _x_demux_flush_engine(my_vcd.stream);
            vcdplayer_play(p_vcdplayer, itemid);
            return true;
          }
        }
        break;
      }
    case XINE_EVENT_INPUT_MOUSE_BUTTON:
      if (my_vcd.stream)
      {
        xine_input_data_t *p_input = p_event->data;
        if (p_input->button == 1)
        {
#if LIBVCD_VERSION_NUM >= 23
          int i_selection;
#endif

          dbg_print(INPUT_DBG_EVENT,
                    "Button to x: %d, y: %d, scaled x: %d, scaled y %d\n",
                    p_input->x, p_input->y,
                    p_input->x * 255 / p_vcdplayer->max_x,
                    p_input->y * 255 / p_vcdplayer->max_y);

#if LIBVCD_VERSION_NUM >= 23
          /* xine_dvd_send_button_update(this, 1); */

          if (my_vcd.b_mouse_in)
            send_mouse_enter_leave_event(&my_vcd, false);

          i_selection = vcdinfo_get_area_selection(p_vcdplayer->vcd,
                                                   p_vcdplayer->i_lid,
                                                   p_input->x,
                                                   p_input->y,
                                                   p_vcdplayer->max_x,
                                                   p_vcdplayer->max_y);
          dbg_print(INPUT_DBG_EVENT, "Selection is: %d\n", i_selection);

          if (vcdplayer_pbc_is_on(p_vcdplayer)) {
            vcdinfo_itemid_t itemid = p_vcdplayer->play_item;
            lid_t i_next=vcdinfo_selection_get_lid(p_vcdplayer->vcd,
                                                   p_vcdplayer->i_lid,
                                                   i_selection);
            if (VCDINFO_INVALID_LID != i_next) {
              itemid.num = i_next;
              _x_demux_flush_engine(my_vcd.stream);
              vcdplayer_play(p_vcdplayer, itemid);
              return true;
            }
          }
#endif
        }
      }
      break;
    case XINE_EVENT_INPUT_BUTTON_FORCE:
      break;
    case XINE_EVENT_INPUT_MOUSE_MOVE:
      if (my_vcd.stream)
      {
        xine_input_data_t *p_input = p_event->data;
#if LIBVCD_VERSION_NUM >= 23
        int32_t i_selection = vcdinfo_get_area_selection(p_vcdplayer->vcd,
                                                         p_vcdplayer->i_lid,
                                                         p_input->x,
                                                         p_input->y,
                                                         p_vcdplayer->max_x,
                                                         p_vcdplayer->max_y);
        dbg_print(INPUT_DBG_EVENT, "Move to x: %d, y: %d\n",
                  p_input->x, p_input->y);

        if (my_vcd.i_mouse_button != i_selection) {
          dbg_print(INPUT_DBG_EVENT, "Old selection: %d, selection: %d\n",
                    my_vcd.i_mouse_button, i_selection);
          my_vcd.i_mouse_button = i_selection;
          if (i_selection < 0)
            send_mouse_enter_leave_event(&my_vcd, false);
          else
            send_mouse_enter_leave_event(&my_vcd, true);
        }
#else
      dbg_print(INPUT_DBG_EVENT, "Move to x: %d, y: %d\n",
                p_input->x, p_input->y);
#endif
      }
      break;
    case XINE_EVENT_INPUT_UP:
      dbg_print(INPUT_DBG_EVENT, "Called with up\n");
      vcdplayer_send_button_update(p_vcdplayer, 0);
      break;
    case XINE_EVENT_INPUT_DOWN:
      dbg_print(INPUT_DBG_EVENT, "Called with down\n");
      vcdplayer_send_button_update(p_vcdplayer, 0);
      break;
    case XINE_EVENT_INPUT_LEFT:
      dbg_print(INPUT_DBG_EVENT, "Called with left\n");
      vcdplayer_send_button_update(p_vcdplayer, 0);
      break;
    case XINE_EVENT_INPUT_RIGHT:
      dbg_print(INPUT_DBG_EVENT, "Called with right\n");
      vcdplayer_send_button_update(p_vcdplayer, 0);
      break;
    }
  }
  return false;
}

/*!
  From xine plugin spec:

  request optional data from input plugin.
*/
static int
vcd_get_optional_data (input_plugin_t *this_gen,
                        void *data, int data_type) {

  dbg_print( (INPUT_DBG_CALL|INPUT_DBG_EXT),
             "called with %d\n", data_type);

  if (NULL == my_vcd.stream) return INPUT_OPTIONAL_UNSUPPORTED;

  /* Fill this out more fully... */
  switch(data_type) {

  case INPUT_OPTIONAL_DATA_AUDIOLANG:
    {
      uint8_t   channel;
      channel = _x_get_audio_channel(my_vcd.stream);

      dbg_print(INPUT_DBG_EXT, "AUDIO CHANNEL = %d\n", channel);
      if (channel == (uint8_t)-1) {
        strcpy(data, "auto");
      } else {
        const vcdinfo_obj_t *p_vcdinfo= my_vcd.player.vcd;
        unsigned int audio_type;
        unsigned int num_channels;
        unsigned int track_num = my_vcd.player.i_track;
        audio_type = vcdinfo_get_track_audio_type(p_vcdinfo, track_num);
        num_channels = vcdinfo_audio_type_num_channels(p_vcdinfo, audio_type);

        if (channel >= num_channels) {
          sprintf(data, "%d ERR", channel);
        } else {
          sprintf(data, "%1d", channel);
        }
      }
      return INPUT_OPTIONAL_SUCCESS;
    }

  case INPUT_OPTIONAL_DATA_SPULANG:
    {
      /*uint16_t lang;*/
      int8_t   channel;
      channel = (int8_t) _x_get_spu_channel(my_vcd.stream);
      dbg_print(INPUT_DBG_EXT, "SPU CHANNEL = %d\n", channel);
      if (-1 == channel) {
        strcpy(data, "auto");
      } else {
        sprintf(data, "%1d", channel);
      }

    }

  default: ;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/* Array to hold MRLs returned by get_autoplay_list */
#define MAX_DIR_ENTRIES 250

/*!
  From xine plugin spec:

  generate autoplay list
  return value: list of MRLs

-- The list of MRLs returned goes into the playlist.
   This is called when the SHORT_PLUGIN_NAME button is pressed.
*/

static char **
vcd_class_get_autoplay_list (input_class_t *this_gen, int *num_files)
{
  vcd_input_class_t *class = (vcd_input_class_t *) this_gen;
  static char *filelist[MAX_DIR_ENTRIES];

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called\n");

  if ( !vcd_build_mrl_list(class, my_vcd.player.psz_source) ) {
    *num_files = 0;
    return NULL;
  } else {
    int i;
    int size = 0;
    vcdinfo_item_enum_t itemtype =
      autoplay2itemtype[my_vcd.player.default_autoplay];

    int offset = vcd_get_mrl_type_offset(&my_vcd, itemtype, &size);

    /* A VCD is not required to have PBC or LID's, default to entry if
       this is the case...
     */
    if (VCDINFO_ITEM_TYPE_LID == itemtype && size==0) {
      itemtype=VCDINFO_ITEM_TYPE_ENTRY;
      offset = vcd_get_mrl_type_offset(&my_vcd, itemtype, &size);
    }

    /* This is because entries start at 0 while other playable units
       start at 1. Can remove the below when everything has the same
       origin.
     */
    if (VCDINFO_ITEM_TYPE_ENTRY != itemtype) offset++;

    for (i=0; i<size; i++) {
      if (class->mrls[offset+i] != NULL) {
        filelist[i] = class->mrls[offset+i]->mrl;
        dbg_print((INPUT_DBG_MRL), "filelist[%d]: %s\n", i, filelist[i]);
      } else {
        filelist[i] = NULL;
        dbg_print((INPUT_DBG_MRL), "filelist[%d]: NULL\n", i);
      }
    }
    *num_files = i;
    return filelist;
  }
}

/*!
  Things that need to be done when a stream is closed.
*/
static void
vcd_plugin_dispose(input_plugin_t *this_gen)
{

  /* Not sure there's much more to do here...
     In open_plugin we are given a stream which
     we save...
  */
  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called\n");

  my_vcd.stream = NULL;

  if (my_vcd.player.b_opened)
    vcdio_close(&my_vcd.player);
}

/* Pointer to vcdimager default log handler. Set by init_input_plugin
   routine. Perhaps can remove. */
static vcd_log_handler_t  gl_default_vcd_log_handler = NULL;
static cdio_log_handler_t gl_default_cdio_log_handler = NULL;

/*! This routine is called by libvcd routines on error.
   Setup is done by init_input_plugin.
*/
static void
vcd_log_handler (vcd_log_level_t level, const char message[])
{
  switch (level) {
  case VCD_LOG_DEBUG:
  case VCD_LOG_INFO:
    if (!(vcdplayer_debug & INPUT_DBG_VCDINFO))
      return;
    /* Fall through if to warn case */
  case VCD_LOG_WARN:
    LOG_MSG("%s", message);
    break;
  case VCD_LOG_ERROR:
  case VCD_LOG_ASSERT:
    LOG_ERR("%s", message);
    break;
  default:
    LOG_ERR("%s\n%s %d",
            message,
            _("The above message had unknown vcdimager log level"),
            level);
  }

  /* gl_default_vcd_log_handler (level, message); */
}

/*! This routine is called by libcdio routines on error.
   Setup is done by init_input_plugin.
*/
static void
cdio_log_handler (cdio_log_level_t level, const char message[])
{
  switch (level) {
  case CDIO_LOG_DEBUG:
  case CDIO_LOG_INFO:
    if (!(vcdplayer_debug & INPUT_DBG_CDIO)) return;
    /* Fall through if to warn case */
  default:
    vcd_log_handler (level, message);
  }
}

/*! This routine is when xine is not around.
   Setup is done by vcd_class_dispose.
*/
static void
uninit_log_handler (vcd_log_level_t level, const char message[])
{
  switch (level) {
  case VCD_LOG_DEBUG:
  case VCD_LOG_INFO:
    if (!(vcdplayer_debug & (INPUT_DBG_VCDINFO|INPUT_DBG_CDIO)))
      return;
    /* Fall through if to warn case */
  case VCD_LOG_WARN:
    fprintf(stderr, "WARN: %s\n", message);
    break;
  case VCD_LOG_ERROR:
    fprintf(stderr, "ERROR: %s\n", message);
    break;
  case VCD_LOG_ASSERT:
    fprintf(stderr, "ASSERT ERROR: %s\n", message);
    break;
  default:
    fprintf(stderr, "UNKNOWN ERROR: %s\n%s %d",
            message,
            _("The above message had unknown vcdimager log level"),
            level);
  }

  /* gl_default_vcd_log_handler (level, message); */
}

/*!
  Things that need to be done the vcd plugin is closed.
*/
static void
vcd_class_dispose (input_class_t *this_gen) {
  vcd_input_class_t  *class = (vcd_input_class_t *) this_gen;

  class->xine->config->unregister_callback(class->xine->config,
					   "media.vcd.device");
  gl_default_vcd_log_handler  = vcd_log_set_handler (uninit_log_handler);
  gl_default_cdio_log_handler =
    cdio_log_set_handler ((cdio_log_handler_t) uninit_log_handler);

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called\n");

  vcd_close(class);
  free(class->vcd_device);
  free(my_vcd.v_config.title_format);
  free(my_vcd.v_config.comment_format);
  free(class);
}

/* Update the xine player title text. */
static void
vcd_update_title_display(void)
{
  xine_event_t uevent;
  xine_ui_data_t data;

  char *title_str;

  title_str = vcdplayer_format_str(&my_vcd.player,
                                   my_vcd.v_config.title_format);

  meta_info_assign(XINE_META_INFO_TITLE, my_vcd.stream, title_str);
  meta_info_assign(XINE_META_INFO_COMMENT, my_vcd.stream,
                   vcdplayer_format_str(&my_vcd.player,
                                        my_vcd.v_config.comment_format));
  stream_info_assign(XINE_STREAM_INFO_VIDEO_HAS_STILL, my_vcd.stream,
                     my_vcd.player.i_still);

  /* Set_str title/chapter display */
  dbg_print((INPUT_DBG_MRL|INPUT_DBG_CALL),
            "Changing title to read '%s'\n", title_str);
  uevent.type        = XINE_EVENT_UI_SET_TITLE;
  uevent.stream      = my_vcd.stream;
  uevent.data        = &data;
  uevent.data_length = sizeof(data);

  memcpy(data.str, title_str, strlen(title_str) + 1);
  data.str_len = strlen(title_str) + 1;

  xine_event_send(my_vcd.stream, &uevent);
}

#if LIBVCD_VERSION_NUM >= 23
static void
send_mouse_enter_leave_event(vcd_input_plugin_t *p_this, bool b_mouse_in)
{
  if (b_mouse_in && p_this->b_mouse_in) {
    /* Set up to enter the following "if" statement. */
    p_this->b_mouse_in = false;
  }

  if (b_mouse_in != p_this->b_mouse_in) {
    xine_event_t        event;
    xine_spu_button_t   spu_event;

    spu_event.direction = b_mouse_in ? 1 : 0;
    spu_event.button    = p_this->i_mouse_button;

    event.type        = XINE_EVENT_SPU_BUTTON;
    event.stream      = p_this->stream;
    event.data        = &spu_event;
    event.data_length = sizeof(spu_event);
    xine_event_send(p_this->stream, &event);

    p_this->b_mouse_in = b_mouse_in;
  }

  if (!b_mouse_in)
    p_this->i_mouse_button = -1;
}
#endif

/*
   Not much special initialization needed here. All of the initialization
   is either done in the class or when we have an actual MRL we want
   to deal with.
*/
static int
vcd_plugin_open (input_plugin_t *this_gen ) {
  vcd_input_class_t  *class = (vcd_input_class_t *) this_gen->input_class;

  gl_default_vcd_log_handler  = vcd_log_set_handler (vcd_log_handler);
  gl_default_cdio_log_handler = cdio_log_set_handler (cdio_log_handler);

  /* actually, this is also done by class initialization. But just in
     case... */
  class->ip          = &my_vcd;
  my_vcd.i_old_still = 0;

  return 1;
}

/*!
  This basically sets up stream specified by MRL for playing. After this
  routine is called, xine-lib can read blocks from the thing
  specified by the MRL, set the position of the thing specified by the
  MRL, get its size or read its current position...

  See vcdplayer_parses_mrl for the for the format that a valid MRL can take.

  Return values:
     pointer to input plugin
     NULL on failure

*/
static input_plugin_t *
vcd_class_get_instance (input_class_t *class_gen, xine_stream_t *stream,
                        const char *mrl)
{
  vcd_input_class_t  *class = (vcd_input_class_t *) class_gen;

  char intended_vcd_device[MAX_DEVICE_LEN+1]= { '\0', };
  vcdinfo_itemid_t itemid;
  char *check_mrl=NULL;
  bool used_default;

  if (mrl == NULL)
    check_mrl = strdup(MRL_PREFIX);
  else
    check_mrl = strdup(mrl);

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_EXT), "called with %s\n", mrl);

  if ( strncasecmp(check_mrl, MRL_PREFIX, MRL_PREFIX_LEN) ) {
    goto free_and_return;
  }

  vcd_get_default_device(class, false);

  if (!vcd_parse_mrl(class->vcd_device, check_mrl,
                     intended_vcd_device, &itemid,
                     my_vcd.player.default_autoplay, &used_default)) {
    dbg_print(INPUT_DBG_MRL, "parsing MRL %s failed\n", check_mrl);
    goto free_and_return;
  }

  free(my_vcd.mrl);
  my_vcd.mrl         = strdup(check_mrl);
  my_vcd.stream      = stream;
  my_vcd.event_queue = xine_event_new_queue (stream);
  class->ip          = &my_vcd; /* Can probably get rid of this... */

  if (!vcd_build_mrl_list(class, intended_vcd_device)) {
    goto free_and_return;
  }

  my_vcd.player.user_data   = (void *) class;

  /* Do we set PBC (via LID) on? */
  my_vcd.player.i_lid =
    ( VCDINFO_ITEM_TYPE_LID == itemid.type
      && my_vcd.player.i_lids > itemid.num )
    ? itemid.num
    :  VCDINFO_INVALID_ENTRY;

  if ( VCDINFO_ITEM_TYPE_LID == itemid.type && used_default) {
    /* LID was selected automatically but we don't have PBC for this VCD.
       So silently change LID to track and continue.
     */
    itemid.type=VCDINFO_ITEM_TYPE_TRACK;
  }

  if ( 0==itemid.num
       && ( (VCDINFO_ITEM_TYPE_LID == itemid.type)
            || (VCDINFO_ITEM_TYPE_TRACK == itemid.type) ) )
    itemid.num = 1;

  dbg_print(INPUT_DBG_PBC, "Jumping to NUM >%i<, type >%i<\n",
            itemid.num, itemid.type);

  vcd_set_meta_info(&my_vcd);
  vcdplayer_play(&my_vcd.player, itemid);


  dbg_print(INPUT_DBG_MRL, "Successfully opened MRL %s.\n",
            my_vcd.mrl);

  free (check_mrl);
  return &(my_vcd.input_plugin);

 free_and_return:
  free (check_mrl);
  return NULL;
}

#define VCD_NUM_CALLBACK(fn_name, var) \
 static void fn_name(void *this_gen, xine_cfg_entry_t *entry) \
{ \
\
  dbg_print(INPUT_DBG_CALL, "Called setting %d\n", entry->num_value); \
\
  my_vcd.player.var = entry->num_value; \
}

#define VCD_ENUM_CALLBACK(fn_name, enum_type, var) \
 static void fn_name(void *this_gen, xine_cfg_entry_t *entry) \
{ \
  dbg_print(INPUT_DBG_CALL, "Called setting %d\n", entry->num_value); \
\
  my_vcd.player.var = (enum_type) entry->num_value; \
}

#define VCD_STR_CALLBACK(fn_name, var) \
 static void fn_name(void *this_gen, xine_cfg_entry_t *entry) \
{ \
\
  dbg_print(INPUT_DBG_CALL, "Called setting %s\n", entry->str_value); \
  if ( NULL == entry->str_value ) return; \
\
  if ( var ) free(var); \
  var = strdup(entry->str_value); \
}

VCD_STR_CALLBACK(vcd_default_dev_changed_cb,
                  my_vcd.class->vcd_device)

VCD_STR_CALLBACK(vcd_title_format_changed_cb,
                  my_vcd.v_config.title_format)

VCD_STR_CALLBACK(vcd_comment_format_changed_cb,
                  my_vcd.v_config.comment_format)

VCD_NUM_CALLBACK(vcd_show_rejected_cb, show_rejected)

VCD_NUM_CALLBACK(vcd_autoadvance_cb, autoadvance)

VCD_ENUM_CALLBACK(vcd_slider_length_cb, vcdplayer_slider_length_t,
                   slider_length)

VCD_ENUM_CALLBACK(vcd_default_autoplay_cb, vcdinfo_item_enum_t,
                   default_autoplay)

static void
vcd_debug_cb(void *this_gen, xine_cfg_entry_t *entry)
{
  dbg_print(INPUT_DBG_CALL, "Called setting %d\n", entry->num_value);
  vcdplayer_debug = entry->num_value;
}

static void *
vcd_init (xine_t *xine, void *data)
{
  vcd_input_class_t  *class;
  config_values_t    *config;

  dbg_print(INPUT_DBG_CALL, "Called\n");

  class = calloc(1, sizeof (vcd_input_class_t));

  class->xine   = xine;
  class->config = config = xine->config;

  vcd_class     = class;
  class->mrls   = NULL;

  class->input_class.get_instance        = vcd_class_get_instance;
  class->input_class.identifier          = SHORT_PLUGIN_NAME;
  class->input_class.description         = N_("Video CD plugin with PBC and support for: (X)VCD, (X)SVCD, HQVCD, CVD ... ");
  class->input_class.get_dir             = vcd_class_get_dir;
  class->input_class.get_autoplay_list   = vcd_class_get_autoplay_list;
  class->input_class.dispose		 = vcd_class_dispose;
  class->input_class.eject_media         = vcd_class_eject_media;

  memset(&my_vcd, 0, sizeof(my_vcd));

  /*------------------------------------------------------------------
    Callback functions.
  ---------------------------------------------------------------------*/
  my_vcd.player.flush_buffers          = &vcd_flush_buffers;
  my_vcd.player.update_title           = &vcd_update_title_display;
  my_vcd.player.log_err                = (generic_fn) &xine_log_err;
  my_vcd.player.log_msg                = (generic_fn) &xine_log_msg;
  my_vcd.player.force_redisplay        = &vcd_force_redisplay;
  my_vcd.player.set_aspect_ratio       = &vcd_set_aspect_ratio;

  /*-------------------------------------------------------------
     Playback control-specific fields
   --------------------------------------------------------------*/

  my_vcd.player.i_lid                  = VCDINFO_INVALID_ENTRY;
  my_vcd.player.end_lsn                = VCDINFO_NULL_LSN;

  my_vcd.player.pdi                    = -1;
  my_vcd.player.pxd.psd                = NULL;

  /*-----------------------------------
     Navigation fields
   ------------------------------------*/
  my_vcd.player.next_entry             = -1;
  my_vcd.player.prev_entry             = -1;
  my_vcd.player.return_entry           = -1;
  my_vcd.player.default_entry          = -1;

  /*--------------------------------------------------------------
    Configuration variables
   ---------------------------------------------------------------*/

  {
    /*Note: these labels have to be listed in the same order as the
      enumeration vcdplayer_autoplay_t in vcdplayer.h.
    */
    static const char *const autoplay_modes[] =
      { "MPEG track", "entry", "segment",  "playback-control item", NULL };

    /*Note: these labels have to be listed in the same order as the
      enumeration vcdplayer_slider_length_t in vcdplayer.h.
    */
    static const char *const length_reporting_modes[] =
      { "auto", "track", "entry", NULL };

    my_vcd.player.default_autoplay =
      config->register_enum(config,
                            "media.vcd.autoplay",
                            VCDPLAYER_AUTOPLAY_PBC,
                            (char **) autoplay_modes,
                            _("VCD default type to use on autoplay"),
_("The VCD play unit to use when none is specified in an MRL, e.g. "
                            "vcd:// or vcd:///dev/dvd:"),
                            10,
                            vcd_default_autoplay_cb, class);


    class->vcd_device =
      strdup (config->register_filename(config,
                              "media.vcd.device",
                              "", XINE_CONFIG_STRING_IS_DEVICE_NAME,
          _("CD-ROM drive used for VCD when none given"),
_("What to use if no drive specified. If the setting is empty, xine will scan for CD drives."),
                              20,
                              vcd_default_dev_changed_cb,
                              (void *) class));

    my_vcd.player.slider_length =
      config->register_enum(config,
                            "media.vcd.length_reporting",
                            VCDPLAYER_SLIDER_LENGTH_AUTO,
                            (char **) length_reporting_modes,
                            _("VCD position slider range"),
_("range that the stream playback position slider represents playing a VCD."),
                            10,
                            vcd_slider_length_cb, NULL);

#if READAHEAD_FINISHED
    my_vcd.player.readahead =
      config->register_bool(config, "vcd.use_readahead",
                            (int) false,
                            _("VCD read-ahead caching?"),
                            _("Class "
                              "may lead to jerky playback on low-end "
                              "machines."),
                            vcd_read_ahead_cb, NULL);
#endif

  my_vcd.player.autoadvance =
    config->register_bool(config,
                        "media.vcd.autoadvance",
                        (int) true,
                        _("automatically advance VCD track/entry"),
_("If enabled, we should automatically advance to the next entry or track. Used only when playback control (PBC) is disabled."),
                        10,
                        vcd_autoadvance_cb,
                        NULL);

  my_vcd.player.show_rejected =
    config->register_bool(config,
                        "media.vcd.show_rejected",
                        (int) false,
                        _("show 'rejected' VCD LIDs"),
_("Some playback list IDs (LIDs) are marked not showable, "
"but you can see them in the MRL list if this is set. Rejected entries "
"are marked with an asterisk (*) appended to the MRL."),
                        10,
                        vcd_show_rejected_cb,
                        NULL);

  my_vcd.v_config.title_format =
    strdup(config->register_string(config,
                          "media.vcd.title_format",
                          "%F - %I %N%L%S, disk %c of %C - %v %A",
                          _("VCD format string for display banner"),
_("VCD format used in the GUI Title. Similar to the Unix date "
"command. Format specifiers start with a percent sign. Specifiers are:\n"
" %A : The album information\n"
" %C : The VCD volume count - the number of CD's in the collection.\n"
" %c : The VCD volume num - the number of the CD in the collection.\n"
" %F : The VCD Format, e.g. VCD 1.0, VCD 1.1, VCD 2.0, or SVCD\n"
" %I : The current entry/segment/playback type, e.g. ENTRY, TRACK, ...\n"
" %L : The playlist ID prefixed with \" LID\" if it exists\n"
" %N : The current number of the above - a decimal number\n"
" %P : The publisher ID\n"
" %p : The preparer ID\n"
" %S : If we are in a segment (menu), the kind of segment\n"
" %T : The track number\n"
" %V : The volume set ID\n"
" %v : The volume ID\n"
"      A number between 1 and the volume count.\n"
" %% : a %\n"),
                          20,
                          vcd_title_format_changed_cb,
                          NULL));

  my_vcd.v_config.comment_format =
    strdup(config->register_string(config,
                          "media.vcd.comment_format",
                          "%P - Track %T",
                          _("VCD format string for stream comment field"),
_("VCD format used in the GUI Title. Similar to the Unix date "
"command. Format specifiers start with a percent sign. Specifiers are "
"%A, %C, %c, %F, %I, %L, %N, %P, %p, %S, %T, %V, %v, and %%.\n"
"See the help for the title_format for the meanings of these."),
                          20,
                          vcd_comment_format_changed_cb,
                          NULL));

  vcdplayer_debug =
  config->register_num(config,
                       "media.vcd.debug",
                       0,
                       _("VCD debug flag mask"),
_("For tracking down bugs in the VCD plugin. Mask values are:\n"
"   1: Meta information\n"
"   2: input (keyboard/mouse) events\n"
"   4: MRL parsing\n"
"   8: Calls from external routines\n"
"  16: routine calls\n"
"  32: LSN changes\n"
"  64: Playback control\n"
" 128: Debugging from CDIO\n"
" 256: Seeks to set location\n"
" 512: Seeks to find current location\n"
"1024: Still-frame\n"
"2048: Debugging from VCDINFO\n"
),
                        20,
                        vcd_debug_cb,
                        class);
  }

  gl_default_vcd_log_handler  = vcd_log_set_handler (uninit_log_handler);
  gl_default_cdio_log_handler =
    cdio_log_set_handler ((cdio_log_handler_t) uninit_log_handler);

  my_vcd.input_plugin.open               = vcd_plugin_open;
  my_vcd.input_plugin.get_capabilities   = vcd_plugin_get_capabilities;
  my_vcd.input_plugin.read               = vcd_plugin_read;
  my_vcd.input_plugin.read_block         = vcd_plugin_read_block;
  my_vcd.input_plugin.seek               = vcd_plugin_seek;
  my_vcd.input_plugin.get_current_pos    = vcd_plugin_get_current_pos;
  my_vcd.input_plugin.get_length         = vcd_plugin_get_length;
  my_vcd.input_plugin.get_blocksize      = vcd_plugin_get_blocksize;
  my_vcd.input_plugin.get_mrl            = vcd_plugin_get_mrl;
  my_vcd.input_plugin.get_optional_data  = vcd_get_optional_data;
  my_vcd.input_plugin.dispose            = vcd_plugin_dispose;
  my_vcd.input_plugin.input_class        = (input_class_t *) class;

  my_vcd.stream                          = NULL;
  my_vcd.class                           = class;
  my_vcd.i_mouse_button                  = -1;
  my_vcd.b_mouse_in                      = false;

  my_vcd.player.psz_source               = NULL;

  my_vcd.player.b_opened                 = false;
  my_vcd.player.play_item.num            = VCDINFO_INVALID_ENTRY;
  my_vcd.player.play_item.type           = VCDINFO_ITEM_TYPE_ENTRY;

  my_vcd.player_device                   = NULL;

  return class;
}

/*
   Exported plugin catalog entries.

   All plugins listing only the current API number break when the API
   number is increased. This is by design.

   Sometimes in the rush to get out a buggy release, the API number is
   increased without communication let alone a concern for whether it
   is necessary or how many plugins it might break. And that is
   precisely when what happened between API release 12 and API
   13. Input plugin API numbers 12 and 13 are functionally identical.

   Because of problems like this, we'll just put in a future API
   release. If the number was increased for a reason that doesn't
   affect us (such as for nor reason at all), then this plugin will
   work unmodified that future APIs. If on the other hand there was
   incompatible change, we are no worse off than if we hadn't entered
   the next API number since in both cases the plugin is broken.
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, (char *) SHORT_PLUGIN_NAME,
    XINE_VERSION_CODE, NULL, vcd_init },
  { PLUGIN_NONE, 0, (char *) "", 0, NULL, NULL }
};

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
