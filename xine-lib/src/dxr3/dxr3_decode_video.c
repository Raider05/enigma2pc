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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* dxr3 video decoder plugin.
 * Accepts the video data from xine and sends it directly to the
 * corresponding dxr3 device. Takes precedence over the libmpeg2
 * due to a higher priority.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define LOG_MODULE "dxr3_decode_video"
/* #define LOG_VERBOSE */
/* #define LOG */

#define LOG_VID 0
#define LOG_PTS 0

#include <xine/xine_internal.h>
#include <xine/buffer.h>
#include "video_out_dxr3.h"
#include "dxr3.h"

#include "compat.c"

/* once activated, we wait for this amount of missing pan&scan info
 * before disabling it again */
#define PAN_SCAN_WINDOW_SIZE 50

/* the number of frames to pass after an out-of-sync situation
 * before locking the stream again */
#define RESYNC_WINDOW_SIZE 50

/* we adjust vpts_offset in metronom, when skip_count reaches this value */
#define SKIP_TOLERANCE 200

/* the number of frames to pass before we stop duration correction */
#define FORCE_DURATION_WINDOW_SIZE 100


/* plugin class initialization function */
static void     *dxr3_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static const decoder_info_t dxr3_video_decoder_info = {
  supported_types,     /* supported types */
  10                   /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "dxr3-mpeg2", XINE_VERSION_CODE, &dxr3_video_decoder_info, &dxr3_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin class functions */
static video_decoder_t *dxr3_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream);

/* plugin instance functions */
static void dxr3_decode_data(video_decoder_t *this_gen, buf_element_t *buf);
static void dxr3_reset(video_decoder_t *this_gen);
static void dxr3_discontinuity(video_decoder_t *this_gen);
static void dxr3_flush(video_decoder_t *this_gen);
static void dxr3_dispose(video_decoder_t *this_gen);

/* plugin structures */
typedef struct dxr3_decoder_class_s {
  video_decoder_class_t  video_decoder_class;

  int                    instance;             /* we allow only one instance of this plugin */

  metronom_clock_t      *clock;                /* used for syncing */
} dxr3_decoder_class_t;

typedef struct dxr3_decoder_s {
  video_decoder_t        video_decoder;
  dxr3_decoder_class_t  *class;
  xine_stream_t         *stream;
  dxr3_scr_t            *scr;                  /* shortcut to the scr plugin in the dxr3 video out */

  int                    devnum;
  int                    fd_control;
  int                    fd_video;             /* to access the dxr3 devices */

  int                    have_header_info;
  int                    sequence_open;
  int                    width;
  int                    height;
  double                 ratio;
  int                    aspect_code;
  int                    frame_rate_code;
  int                    repeat_first_field;   /* mpeg stream header data */

  int                    force_aspect;         /* when input plugin has better info, we are forced */
  int                    force_pan_scan;       /* to use a certain aspect or to do pan&scan */

  int                    use_panscan;
  int                    panscan_smart_change;
  int                    afd_smart_change;
  int                    afd_code;             /* use pan&scan info if present in stream */

  int                    last_width;
  int                    last_height;
  int                    last_aspect_code;     /* used to detect changes for event sending */

  unsigned int           dts_offset[3];
  int                    sync_every_frame;
  int                    sync_retry;
  int                    enhanced_mode;
  int                    resync_window;
  int                    skip_count;           /* syncing parameters */

  int                    correct_durations;
  int64_t                last_vpts;
  int                    force_duration_window;
  int                    avg_duration;         /* logic to correct broken frame rates */
} dxr3_decoder_t;

/* helper functions */
static inline int  dxr3_mvcommand(int fd_control, int command);
static        void parse_mpeg_header(dxr3_decoder_t *this, uint8_t *buffer);
static        int  get_duration(dxr3_decoder_t *this);
static        void frame_format_change(dxr3_decoder_t *this);

/* config callbacks */
static void      dxr3_update_panscan(void *this_gen, xine_cfg_entry_t *entry);
static void      dxr3_update_sync_mode(void *this_gen, xine_cfg_entry_t *entry);
static void      dxr3_update_enhanced_mode(void *this_gen, xine_cfg_entry_t *entry);
static void      dxr3_update_correct_durations(void *this_gen, xine_cfg_entry_t *entry);

/* inline helper implementations */
static inline int dxr3_mvcommand(int fd_control, int command)
{
  em8300_register_t reg;

  reg.microcode_register = 1;
  reg.reg = 0;
  reg.val = command;

  return ioctl(fd_control, EM8300_IOCTL_WRITEREG, &reg);
}


static void *dxr3_init_plugin(xine_t *xine, void *data)
{
  dxr3_decoder_class_t *this;

  this = calloc(1, sizeof (dxr3_decoder_class_t));
  if (!this) return NULL;

  this->video_decoder_class.open_plugin     = dxr3_open_plugin;
  this->video_decoder_class.identifier      = "dxr3-mpeg2";
  this->video_decoder_class.description     = N_("MPEGI/II decoder plugin using the hardware decoding capabilities of a DXR3 decoder card.");
  this->video_decoder_class.dispose         = default_video_decoder_class_dispose;

  this->instance                            = 0;

  this->clock                               = xine->clock;

  return &this->video_decoder_class;
}


static video_decoder_t *dxr3_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  static const char *const panscan_types[] = { "only when forced", "use MPEG hint", "use DVB hint", NULL };
  dxr3_decoder_t *this;
  dxr3_decoder_class_t *class = (dxr3_decoder_class_t *)class_gen;
  config_values_t *cfg;
  char tmpstr[128];

  if (class->instance) return NULL;
  if (!dxr3_present(stream)) return NULL;

  this = calloc(1, sizeof (dxr3_decoder_t));
  if (!this) return NULL;

  cfg = stream->xine->config;

  this->video_decoder.decode_data   = dxr3_decode_data;
  this->video_decoder.reset         = dxr3_reset;
  this->video_decoder.discontinuity = dxr3_discontinuity;
  this->video_decoder.flush         = dxr3_flush;
  this->video_decoder.dispose       = dxr3_dispose;

  this->class                       = class;
  this->stream                      = stream;
  this->scr                         = NULL;

  this->devnum = cfg->register_num(cfg, CONF_KEY, 0, CONF_NAME, CONF_HELP, 10, NULL, NULL);

  snprintf(tmpstr, sizeof(tmpstr), "/dev/em8300-%d", this->devnum);
  llprintf(LOG_VID, "Entering video init, devname=%s.\n",tmpstr);

  /* open later, because dxr3_video_out might have it open until we request a frame */
  this->fd_video = -1;

  if ((this->fd_control = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_decode_video: Failed to open control device %s (%s)\n"), tmpstr, strerror(errno));
    free(this);
    return NULL;
  }

  this->use_panscan           = cfg->register_enum(cfg,
    "dxr3.use_panscan", 0, panscan_types, _("use Pan & Scan info"),
    _("\"Pan & Scan\" is a special display mode which is sometimes used in MPEG "
      "encoded material. You can specify here, how to handle such content.\n\n"
      "only when forced\n"
      "Use Pan & Scan only, when the content you are playing enforces it.\n\n"
      "use MPEG hint\n"
      "Enable Pan & Scan based on information embedded in the MPEG video stream.\n\n"
      "use DVB hint\n"
      "Enable Pan & Scan based on information embedded in DVB streams. This makes "
      "use of the Active Format Descriptor (AFD) used in some European DVB channels."),
    10, dxr3_update_panscan, this);

  this->dts_offset[0]         = 21600;
  this->dts_offset[1]         = 21600;
  this->dts_offset[2]         = 21600;

  this->force_duration_window = -FORCE_DURATION_WINDOW_SIZE;
  this->last_vpts             = this->class->clock->get_current_time(this->class->clock);

  this->sync_every_frame      = cfg->register_bool(cfg,
    "dxr3.playback.sync_every_frame", 0, _("try to sync video every frame"),
    _("Tries to set a synchronization timestamp for every frame. "
      "Normally this is not necessary, because sync is sufficent "
      "even when the timestamp is set only every now and then.\n"
      "This is relevant for progressive video only (most PAL films)."),
    20, dxr3_update_sync_mode, this);
  this->enhanced_mode         = cfg->register_bool(cfg,
    "dxr3.playback.alt_play_mode", 1, _("use smooth play mode"),
    _("Enabling this option will utilise a smoother play mode."),
    20, dxr3_update_enhanced_mode, this);
  this->correct_durations     = cfg->register_bool(cfg,
    "dxr3.playback.correct_durations", 0, _("correct frame durations in broken streams"),
    _("Enables a small logic that corrects the frame durations of "
      "some mpeg streams with wrong framerate codes. Currently a "
      "correction for NTSC streams erroneously labeled as PAL "
      "streams is implemented. Enable only, when you encounter such streams."),
    0, dxr3_update_correct_durations, this);

  /* the dxr3 needs a longer prebuffering to have time for its internal decoding */
  this->stream->metronom->set_option(this->stream->metronom, METRONOM_PREBUFFER, 90000);

  (stream->video_out->open) (stream->video_out, stream);

  class->instance = 1;

  return &this->video_decoder;
}

static void dxr3_decode_data(video_decoder_t *this_gen, buf_element_t *buf)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  ssize_t written;
  int64_t vpts;
  int i, skip;
  vo_frame_t *img;
  uint8_t *buffer, byte;
  uint32_t shift;

  vpts = 0;

  /* handle aspect hints from xine-dvdnav */
  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_ASPECT) {
      this->aspect_code = this->force_aspect = buf->decoder_info[2];
      if (buf->decoder_info[3] == 0x1 && this->force_aspect == 3)
	/* letterboxing is denied, we have to do pan&scan */
	this->force_pan_scan = 1;
      else
	this->force_pan_scan = 0;

      frame_format_change(this);

      this->last_aspect_code = this->aspect_code;
    }
    return;
  }

  /* parse frames in the buffer handed in, evaluate headers,
   * send frames to video_out and handle some syncing
   */
  buffer = buf->content;
  shift = 0xffffff00;
  for (i = 0; i < buf->size; i++) {
    byte = *buffer++;
    if (shift != 0x00000100) {
      shift = (shift | byte) << 8;
      continue;
    }
    /* header code of some kind found */
    shift = 0xffffff00;

    if (byte == 0xb2) {
      /* check for AFD data */
      if (buffer + 5 < buf->content + buf->size) {
	if (buffer[0] == 0x44 && buffer[1] == 0x54 && buffer[2] == 0x47) {
	  this->afd_code = buffer[5] & 0x0f;
	  if (this->aspect_code == 3)
	    /* 4:3 image in 16:9 frame -> zoomit! */
	    this->afd_smart_change = PAN_SCAN_WINDOW_SIZE;
	}
      }
      continue;
    }
    if (byte == 0xb3) {
      /* sequence data */
      if (buffer + 3 < buf->content + buf->size)
        parse_mpeg_header(this, buffer);
      this->sequence_open = 1;
      continue;
    }
    if (byte == 0xb5) {
      /* extension data */
      /* parse the extension type and use what is necessary...
       * types are: sequence(1), sequence_display(2), quant_matrix(3),
       * copyright(4), picture_display(7), picture_coding(8), ... */
      if (buffer + 4 < buf->content + buf->size) {
        switch (buffer[0] >> 4) {
	case 2:
	case 7:
	  /* picture_display and sequence_display are pan&scan info */
	  if (this->use_panscan) this->panscan_smart_change = PAN_SCAN_WINDOW_SIZE;
	  break;
	case 8:
	  this->repeat_first_field = (buffer[3] >> 1) & 1;
	  /* clearing the progessive flag gets rid of the slight shaking with
	   * TV-out in the lower third of the image; but we have to set this
	   * flag, when a still frame is coming along, otherwise the card will
	   * drop one of the fields; therefore we check for the fifo size */
	  if (!((dxr3_driver_t *)this->stream->video_driver)->overlay_enabled) {
	    if (this->stream->video_fifo->fifo_size > this->stream->video_fifo->buffer_pool_capacity / 2)
	      buffer[4] &= ~(1 << 7);
	    else
	      buffer[4] |=  (1 << 7);
	  }
	  break;
	}
      }
      /* check if we can keep syncing */
      if (this->repeat_first_field && this->sync_retry)  /* reset counter */
        this->sync_retry = 500;
      if (this->repeat_first_field && this->sync_every_frame) {
        llprintf(LOG_VID, "non-progressive video detected. disabling sync_every_frame.\n");
        this->sync_every_frame = 0;
        this->sync_retry = 500; /* see you later */
      }
      /* check for pan&scan state */
      if (this->use_panscan && (this->panscan_smart_change > 0 || this->afd_smart_change > 0)) {
	this->panscan_smart_change--;
	this->afd_smart_change--;
	if (this->panscan_smart_change > 0 || this->afd_smart_change > 0) {
	  /* only pan&scan if source is anamorphic */
	  if (this->aspect_code == 3) {
	    if (this->afd_smart_change && this->use_panscan == 2 && this->afd_code == 9)
	      this->force_pan_scan = 1; /* panscan info available -> zoom */
	    else if (this->afd_smart_change && this->use_panscan == 2 && this->afd_code != 9)
	      this->force_pan_scan = 0; /* force no panscan - image is 16:9 */
	    else if (this->use_panscan == 1 && this->panscan_smart_change)
	      this->force_pan_scan = 1; /* panscan info available, ignore AFD mode */
	    else if (!this->afd_smart_change && this->panscan_smart_change)
	      this->force_pan_scan = 1;
	    frame_format_change(this);
	  }
	} else {
	  this->force_pan_scan = 0;
	  frame_format_change(this);
	}
      }
      continue;
    }
    if (byte == 0xb7)
      /* sequence end */
      this->sequence_open = 0;
    if (byte != 0x00)  /* Don't care what it is. It's not a new frame */
      continue;
    /* we have a code for a new frame */
    if (!this->have_header_info)  /* this->width et al may still be undefined */
      continue;
    if (buf->decoder_flags & BUF_FLAG_PREVIEW)
      continue;

    /* pretend like we have decoded a frame */
    img = this->stream->video_out->get_frame(this->stream->video_out,
      this->width, this->height, this->ratio,
      XINE_IMGFMT_DXR3, VO_BOTH_FIELDS | (this->force_pan_scan ? VO_PAN_SCAN_FLAG : 0));
    img->pts       = buf->pts;
    img->bad_frame = 0;
    img->duration  = get_duration(this);

    skip = img->draw(img, this->stream);

    if (skip <= 0) { /* don't skip */
      vpts = img->vpts; /* copy so we can free img */

      if (this->correct_durations) {
        /* calculate an average frame duration from metronom's vpts values */
        this->avg_duration = this->avg_duration * 0.9 + (vpts - this->last_vpts) * 0.1;
        llprintf(LOG_PTS, "average frame duration %d\n", this->avg_duration);
      }

      if (this->skip_count) this->skip_count--;

      if (this->resync_window == 0 && this->scr && this->enhanced_mode &&
	  !this->scr->scanning) {
        /* we are in sync, so we can lock the stream now */
        llprintf(LOG_VID, "in sync, stream locked\n");
        dxr3_mvcommand(this->fd_control, MVCOMMAND_SYNC);
        this->resync_window = -RESYNC_WINDOW_SIZE;
	pthread_mutex_lock(&this->scr->mutex);
	this->scr->sync = 1;
	pthread_mutex_unlock(&this->scr->mutex);
      }
      if (this->resync_window != 0 && this->resync_window > -RESYNC_WINDOW_SIZE)
        this->resync_window--;
    } else { /* metronom says skip, so don't set vpts */
      llprintf(LOG_VID, "%d frames to skip\n", skip);
      vpts = 0;
      this->avg_duration = 0;

      /* handle frame skip conditions */
      if (this->scr && !this->scr->scanning) this->skip_count += skip;
      if (this->skip_count > SKIP_TOLERANCE) {
        /* we have had enough skipping messages now, let's react */
        int64_t vpts_adjust = skip * (int64_t)img->duration / 2;
        if (vpts_adjust > 90000) vpts_adjust = 90000;
        this->stream->metronom->set_option(this->stream->metronom,
          METRONOM_ADJ_VPTS_OFFSET, vpts_adjust);
        this->skip_count = 0;
        this->resync_window = 0;
      }

      if (this->scr && this->scr->scanning) this->resync_window = 0;
      if (this->resync_window == 0 && this->scr && this->enhanced_mode &&
	  !this->scr->scanning) {
        /* switch off sync mode in the card to allow resyncing */
        llprintf(LOG_VID, "out of sync, allowing stream resync\n");
        dxr3_mvcommand(this->fd_control, MVCOMMAND_START);
        this->resync_window = RESYNC_WINDOW_SIZE;
	pthread_mutex_lock(&this->scr->mutex);
	this->scr->sync = 0;
	pthread_mutex_unlock(&this->scr->mutex);
      }
      if (this->resync_window != 0 && this->resync_window < RESYNC_WINDOW_SIZE)
        this->resync_window++;
    }
    this->last_vpts = img->vpts;
    img->free(img);

    /* if sync_every_frame was disabled, decrease the counter
     * for a retry
     * (it might be due to crappy studio logos and stuff
     * so we should give the main movie a chance)
     */
    if (this->sync_retry) {
      if (!--this->sync_retry) {
        llprintf(LOG_VID, "retrying sync_every_frame");
        this->sync_every_frame = 1;
      }
    }
  }
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) return;

  /* ensure video device is open
   * (we open it late because on occasion the dxr3 video out driver
   * wants to open it)
   * also ensure the scr is running
   */
  if (this->fd_video < 0) {
    metronom_clock_t *clock = this->class->clock;
    char tmpstr[128];
    int64_t time;

    /* open the device for the decoder */
    snprintf (tmpstr, sizeof(tmpstr), "/dev/em8300_mv-%d", this->devnum);
    if ((this->fd_video = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("dxr3_decode_video: Failed to open video device %s (%s)\n"), tmpstr, strerror(errno));
      return;
    }

    /* We may want to issue a SETPTS, so make sure the scr plugin
     * is running and registered. Unfortuantely wa cannot do this
     * earlier, because the dxr3's internal scr gets confused
     * when started with a closed video device. Maybe this is a
     * driver bug and gets fixed somewhen. FIXME: We might then
     * want to do this entirely in the video out.
     */
    this->scr = ((dxr3_driver_t *)this->stream->video_driver)->class->scr;
    time = clock->get_current_time(clock);
    this->scr->scr_plugin.start(&this->scr->scr_plugin, time);
    clock->register_scr(clock, &this->scr->scr_plugin);
  }

  /* update the pts timestamp in the card, which tags the data we write to it */
  if (vpts) {
    int64_t delay;

    /* The PTS values written to the DXR3 must be modified based on the difference
     * between stream's PTS and DTS (decoder timestamp). We receive this
     * difference via decoder_info */
    buf->decoder_info[0] <<= 1;
    if (buf->pts) {
      if ((this->dts_offset[0] == buf->decoder_info[0]) &&
	  (this->dts_offset[1] == buf->decoder_info[0]))
	this->dts_offset[2] = buf->decoder_info[0];
      else {
	this->dts_offset[1] = this->dts_offset[0];
	this->dts_offset[0] = buf->decoder_info[0];
      }
      llprintf(LOG_PTS, "PTS to DTS correction: %d\n", this->dts_offset[1]);
    }
    vpts -= this->dts_offset[2];

    delay = vpts - this->class->clock->get_current_time(
      this->class->clock);
    llprintf(LOG_PTS, "SETPTS got %" PRId64 "\n", vpts);
    /* SETPTS only if less then one second in the future and
     * either buffer has pts or sync_every_frame is set */
    if ((delay > 0) && (delay < 90000) &&
      (this->sync_every_frame || buf->pts)) {
      uint32_t vpts32 = vpts;
      /* update the dxr3's current pts value */
      if (dxr3_video_setpts(this->fd_video, &vpts32))
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"dxr3_decode_video: set video pts failed (%s)\n", strerror(errno));
    }

    if (delay >= 90000)   /* frame more than 1 sec ahead */
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_decode_video: WARNING: vpts %" PRId64 " is %.02f seconds ahead of time!\n",
	      vpts, delay/90000.0);
    if (delay < 0)
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "dxr3_decode_video: WARNING: overdue frame.\n");
  }
  else if (buf->pts)
    llprintf(LOG_PTS, "skip buf->pts = %" PRId64 " (no vpts)\n", buf->pts);

  /* now write the content to the dxr3 mpeg device and, in a dramatic
   * break with open source tradition, check the return value
   */
  written = write(this->fd_video, buf->content, buf->size);
  if (written < 0) {
    if (errno == EAGAIN) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("dxr3_decode_video: write to device would block. flushing\n"));
      dxr3_flush(this_gen);
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("dxr3_decode_video: video device write failed (%s)\n"), strerror(errno));
    }
    return;
  }
  if (written != buf->size)
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_decode_video: Could only write %zd of %d video bytes.\n", written, buf->size);
}

static void dxr3_reset(video_decoder_t *this_gen)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;

  this->sequence_open = 0;
}

static void dxr3_discontinuity(video_decoder_t *this_gen)
{
}

static void dxr3_flush(video_decoder_t *this_gen)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;

  if (this->sequence_open && ++this->sequence_open > 5 &&
      _x_stream_info_get(this->stream, XINE_STREAM_INFO_VIDEO_HAS_STILL)) {
    /* The dxr3 needs a sequence end code for still menus to work correctly
     * (the highlights won't move without), but some dvds have stills
     * with no sequence end code. Since it is very likely that flush() is called
     * in still situations, we send one here. */
    static const uint8_t end_buffer[4] = { 0x00, 0x00, 0x01, 0xb7 };
    write(this->fd_video, &end_buffer, 4);
    this->sequence_open = 0;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "dxr3_decode_video: WARNING: added missing end sequence\n");
  }
}

static void dxr3_dispose(video_decoder_t *this_gen)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  metronom_clock_t *clock = this->class->clock;

  if (this->scr)
    clock->unregister_scr(clock, &this->scr->scr_plugin);

  dxr3_mvcommand(this->fd_control, MVCOMMAND_FLUSHBUF);

  if (this->fd_video >= 0) close(this->fd_video);
  close(this->fd_control);

  this->stream->video_out->close(this->stream->video_out, this->stream);
  this->class->instance  = 0;

  free(this);
}


static void parse_mpeg_header(dxr3_decoder_t *this, uint8_t * buffer)
{
  this->frame_rate_code = buffer[3] & 15;
  this->height          = (buffer[0] << 16) |
                          (buffer[1] <<  8) |
                          (buffer[2] <<  0);
  this->width           = ((this->height >> 12) + 15) & ~15;
  this->height          = ((this->height & 0xfff) + 15) & ~15;
  this->aspect_code     = buffer[3] >> 4;

  this->have_header_info = 1;

  if (this->force_aspect) this->aspect_code = this->force_aspect;

  /* when width, height or aspect changes,
   * we have to send an event for dxr3 spu decoder */
  if (!this->last_width || !this->last_height || !this->last_aspect_code ||
      (this->last_width != this->width) ||
      (this->last_height != this->height) ||
      (this->last_aspect_code != this->aspect_code)) {
    frame_format_change(this);
    this->last_width = this->width;
    this->last_height = this->height;
    this->last_aspect_code = this->aspect_code;
  }
}

static int get_duration(dxr3_decoder_t *this)
{
  int duration;

  switch (this->frame_rate_code) {
  case 1: /* 23.976 */
    duration = 3754;  /* actually it's 3753.75 */
    break;
  case 2: /* 24.000 */
    duration = 3750;
    break;
  case 3: /* 25.000 */
    duration = this->repeat_first_field ? 5400 : 3600;
    break;
  case 4: /* 29.970 */
    duration = this->repeat_first_field ? 4505 : 3003;
    break;
  case 5: /* 30.000 */
    duration = 3000;
    break;
  case 6: /* 50.000 */
    duration = 1800;
    break;
  case 7: /* 59.940 */
    duration = 1502;  /* actually it's 1501.5 */
    break;
  case 8: /* 60.000 */
    duration = 1500;
    break;
  default:
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("dxr3_decode_video: WARNING: unknown frame rate code %d\n"), this->frame_rate_code);
    duration = 0;
    break;
  }

  /* update stream metadata */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, duration);

  if (this->correct_durations && duration) {
    /* we set an initial average frame duration here */
    if (!this->avg_duration) this->avg_duration = duration;

    /* Apply a correction to the framerate-code if metronom
     * insists on a different frame duration.
     * The code below is for NTCS streams labeled as PAL streams.
     * (I have seen such things even on dvds!)
     */
    if (this->avg_duration && this->avg_duration < 3300 && duration == 3600) {
      if (this->force_duration_window > 0) {
        /* we are already in a force_duration window, so we force duration */
        this->force_duration_window = FORCE_DURATION_WINDOW_SIZE;
        return 3000;
      }
      if (this->force_duration_window <= 0 && (this->force_duration_window += 10) > 0) {
        /* we just entered a force_duration window, so we start the correction */
        metronom_t *metronom = this->stream->metronom;
        int64_t cur_offset;
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
		_("dxr3_decode_video: WARNING: correcting frame rate code from PAL to NTSC\n"));
        /* those weird streams need an offset, too */
        cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
        metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset - 28800);
        this->force_duration_window = FORCE_DURATION_WINDOW_SIZE;
        return 3000;
      }
    }

    if (this->force_duration_window == -FORCE_DURATION_WINDOW_SIZE)
      /* we are far from a force_duration window */
      return duration;
    if (--this->force_duration_window == 0) {
      /* we have just left a force_duration window */
      metronom_t *metronom = this->stream->metronom;
      int64_t cur_offset;
      cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
      metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset + 28800);
      this->force_duration_window = -FORCE_DURATION_WINDOW_SIZE;
    }
  }

  return duration;
}

static void frame_format_change(dxr3_decoder_t *this)
{
  /* inform the dxr3 SPU decoder about the current format,
   * so that it can choose the correctly matching SPU */
  xine_event_t event;
  xine_format_change_data_t data;
  event.type        = XINE_EVENT_FRAME_FORMAT_CHANGE;
  event.stream      = this->stream;
  event.data        = &data;
  event.data_length = sizeof(data);
  data.width        = this->width;
  data.height       = this->height;
  data.aspect       = this->aspect_code;
  data.pan_scan     = this->force_pan_scan;
  xine_event_send(this->stream, &event);

  /* update ratio */
  switch (this->aspect_code) {
  case 2:
    this->ratio = 4.0 / 3.0;
    break;
  case 3:
    this->ratio = 16.0 / 9.0;
    break;
  case 4:
    this->ratio = 2.11;
    break;
  default:
    if (this->have_header_info)
      this->ratio = (double)this->width / (double)this->height;
  }

  /* update stream metadata */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  10000 * this->ratio);

  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "MPEG (DXR3)");
}

static void dxr3_update_panscan(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->use_panscan = entry->num_value;
}

static void dxr3_update_sync_mode(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->sync_every_frame = entry->num_value;
}

static void dxr3_update_enhanced_mode(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->enhanced_mode = entry->num_value;
}

static void dxr3_update_correct_durations(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->correct_durations = entry->num_value;
}
