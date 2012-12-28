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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* dxr3 spu decoder plugin.
 * Accepts the spu data from xine and sends it directly to the
 * corresponding dxr3 device. Also handles dvd menu button highlights.
 * Takes precedence over libspudec due to a higher priority.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define LOG_MODULE "dxr3_decode_spu"
/* #define LOG_VERBOSE */
/* #define LOG */

#define LOG_PTS 0
#define LOG_SPU 0
#define LOG_BTN 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/buffer.h>
#include "xine-engine/bswap.h"
#ifdef HAVE_DVDNAV
#  ifdef HAVE_DVDNAV_NAVTYPES_H
#    include <dvdnav/nav_types.h>
#    include <dvdnav/nav_read.h>
#  else
#    include <dvdread/nav_types.h>
#    include <dvdread/nav_read.h>
#  endif
#else
#  include "nav_types.h"
#  include "nav_read.h"
#endif
#include "video_out_dxr3.h"
#include "dxr3.h"

#include "compat.c"

#define MAX_SPU_STREAMS 32


/* plugin class initialization function */
static void   *dxr3_spudec_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_DVD, 0 };

static const decoder_info_t dxr3_spudec_info = {
  supported_types,     /* supported types */
  10                   /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER, 17, "dxr3-spudec", XINE_VERSION_CODE, &dxr3_spudec_info, &dxr3_spudec_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin class functions */
static spu_decoder_t *dxr3_spudec_open_plugin(spu_decoder_class_t *class_gen, xine_stream_t *stream);

/* plugin instance functions */
static void    dxr3_spudec_decode_data(spu_decoder_t *this_gen, buf_element_t *buf);
static void    dxr3_spudec_reset(spu_decoder_t *this_gen);
static void    dxr3_spudec_discontinuity(spu_decoder_t *this_gen);
static void    dxr3_spudec_dispose(spu_decoder_t *this_gen);
static int     dxr3_spudec_interact_info(spu_decoder_t *this_gen, void *data);
static void    dxr3_spudec_set_button(spu_decoder_t *this_gen, int32_t button, int32_t mode);

/* plugin structures */
typedef struct dxr3_spu_stream_state_s {
  int                      spu_length;
  int                      spu_ctrl;
  int                      spu_end;
  int                      parse;
  int                      bytes_passed; /* used to parse the spu */
} dxr3_spu_stream_state_t;

typedef struct pci_node_s pci_node_t;
struct pci_node_s {
  pci_t                    pci;
  uint64_t                 vpts;
  pci_node_t              *next;
};

typedef struct dxr3_spudec_class_s {
  spu_decoder_class_t      spu_decoder_class;

  int                      instance;     /* we allow only one instance of this plugin */
} dxr3_spudec_class_t;

typedef struct dxr3_spudec_s {
  spu_decoder_t            spu_decoder;
  dxr3_spudec_class_t     *class;
  xine_stream_t           *stream;
  dxr3_driver_t           *dxr3_vo;      /* we need to talk to the video out */
  xine_event_queue_t      *event_queue;

  int                      devnum;
  int                      fd_spu;       /* to access the dxr3 spu device */

  dxr3_spu_stream_state_t  spu_stream_state[MAX_SPU_STREAMS];
  uint32_t                 clut[16];     /* the current color lookup table */
  int                      menu;         /* are we in a menu? */
  int                      button_filter;
  pci_node_t               pci_cur;      /* a list of PCI packs, with the list head being current */
  pthread_mutex_t          pci_lock;
  uint32_t                 buttonN;      /* currently highlighted button */

  int                      anamorphic;   /* this is needed to detect anamorphic menus */
} dxr3_spudec_t;

/* helper functions */
static inline int  dxr3_present(xine_stream_t *stream);
/* the NAV functions must be called with the pci_lock held */
static inline void dxr3_spudec_clear_nav_list(dxr3_spudec_t *this);
static inline void dxr3_spudec_update_nav(dxr3_spudec_t *this);
static void        dxr3_spudec_process_nav(dxr3_spudec_t *this);
static int         dxr3_spudec_copy_nav_to_btn(dxr3_spudec_t *this, int32_t mode, em8300_button_t *btn);
static inline void dxr3_swab_clut(int* clut);

/* inline helper implementations */
static inline void dxr3_spudec_clear_nav_list(dxr3_spudec_t *this)
{
  while (this->pci_cur.next) {
    pci_node_t *node = this->pci_cur.next->next;
    free(this->pci_cur.next);
    this->pci_cur.next = node;
  }
  /* invalidate current timestamp */
  this->pci_cur.pci.hli.hl_gi.hli_s_ptm = (uint32_t)-1;
}

static inline void dxr3_spudec_update_nav(dxr3_spudec_t *this)
{
  metronom_clock_t *clock = this->stream->xine->clock;

  if (this->pci_cur.next && this->pci_cur.next->vpts <= clock->get_current_time(clock)) {
    pci_node_t *node = this->pci_cur.next;
    xine_fast_memcpy(&this->pci_cur, this->pci_cur.next, sizeof(pci_node_t));
    dxr3_spudec_process_nav(this);
    free(node);
  }
}

static inline void dxr3_swab_clut(int *clut)
{
  int i;
  for (i=0; i<16; i++)
    clut[i] = bswap_32(clut[i]);
}


static void *dxr3_spudec_init_plugin(xine_t *xine, void* data)
{
  dxr3_spudec_class_t *this;

  this = calloc(1, sizeof(dxr3_spudec_class_t));
  if (!this) return NULL;

  this->spu_decoder_class.open_plugin     = dxr3_spudec_open_plugin;
  this->spu_decoder_class.identifier      = "dxr3-spudec";
  this->spu_decoder_class.description     = N_("subtitle decoder plugin using the hardware decoding capabilities of a DXR3 decoder card");
  this->spu_decoder_class.dispose         = default_spu_decoder_class_dispose;

  this->instance                          = 0;

  return &this->spu_decoder_class;
}


static spu_decoder_t *dxr3_spudec_open_plugin(spu_decoder_class_t *class_gen, xine_stream_t *stream)
{
  dxr3_spudec_t *this;
  dxr3_spudec_class_t *class = (dxr3_spudec_class_t *)class_gen;
  char tmpstr[128];

  if (class->instance) return NULL;
  if (!dxr3_present(stream)) return NULL;

  this = calloc(1, sizeof(dxr3_spudec_t));
  if (!this) return NULL;

  this->spu_decoder.decode_data       = dxr3_spudec_decode_data;
  this->spu_decoder.reset             = dxr3_spudec_reset;
  this->spu_decoder.discontinuity     = dxr3_spudec_discontinuity;
  this->spu_decoder.dispose           = dxr3_spudec_dispose;
  this->spu_decoder.get_interact_info = dxr3_spudec_interact_info;
  this->spu_decoder.set_button        = dxr3_spudec_set_button;

  this->class                         = class;
  this->stream                        = stream;
  /* We need to talk to dxr3 video out to coordinate spus and overlays */
  this->dxr3_vo                       = (dxr3_driver_t *)stream->video_driver;
  this->event_queue                   = xine_event_new_queue(stream);

  this->devnum = stream->xine->config->register_num(stream->xine->config,
    CONF_KEY, 0, CONF_NAME, CONF_HELP, 10, NULL, NULL);

  pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
  if (this->dxr3_vo->fd_spu)
    this->fd_spu = this->dxr3_vo->fd_spu;
  else {
    /* open dxr3 spu device */
    snprintf(tmpstr, sizeof(tmpstr), "/dev/em8300_sp-%d", this->devnum);
    if ((this->fd_spu = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("dxr3_decode_spu: Failed to open spu device %s (%s)\n"), tmpstr, strerror(errno));
      pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
      free(this);
      return NULL;
    }
    llprintf(LOG_SPU, "init: SPU_FD = %i\n",this->fd_spu);
    /* We are talking directly to the dxr3 video out to allow concurrent
     * access to the same spu device */
    this->dxr3_vo->fd_spu = this->fd_spu;
  }
  pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);

  this->menu                          = 0;
  this->button_filter                 = 1;
  this->pci_cur.pci.hli.hl_gi.hli_ss  = 0;
  this->pci_cur.next                  = NULL;
  this->buttonN                       = 1;

  this->anamorphic                    = 0;

  pthread_mutex_init(&this->pci_lock, NULL);

  class->instance                     = 1;

  return &this->spu_decoder;
}

static void dxr3_spudec_decode_data(spu_decoder_t *this_gen, buf_element_t *buf)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  ssize_t written;
  uint32_t stream_id = buf->type & 0x1f;
  dxr3_spu_stream_state_t *state = &this->spu_stream_state[stream_id];
  uint32_t spu_channel = this->stream->spu_channel;
  xine_event_t *event;

  /* handle queued events */
  while ((event = xine_event_get(this->event_queue))) {
    llprintf(LOG_SPU, "event caught: SPU_FD = %i\n",this->fd_spu);

    switch (event->type) {
    case XINE_EVENT_FRAME_FORMAT_CHANGE:
      /* we are in anamorphic mode, if the frame is 16:9, but not pan&scan'ed */
      this->anamorphic =
	(((xine_format_change_data_t *)event->data)->aspect == 3) &&
	(((xine_format_change_data_t *)event->data)->pan_scan == 0);
      llprintf(LOG_BTN, "anamorphic mode %s\n", this->anamorphic ? "on" : "off");
      break;
    }

    xine_event_free(event);
  }

  /* check, if we need to process the next PCI from the list */
  pthread_mutex_lock(&this->pci_lock);
  dxr3_spudec_update_nav(this);
  pthread_mutex_unlock(&this->pci_lock);

  if ( (buf->type & 0xffff0000) != BUF_SPU_DVD ||
       !(buf->decoder_flags & BUF_FLAG_SPECIAL) ||
       buf->decoder_info[1] != BUF_SPECIAL_SPU_DVD_SUBTYPE )
    return;

  if (buf->decoder_info[2] == SPU_DVD_SUBTYPE_CLUT) {
    llprintf(LOG_SPU, "BUF_SPU_CLUT\n");
    if (buf->content[0] == 0)  /* cheap endianess detection */
      dxr3_swab_clut((int *)buf->content);
    pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
    if (dxr3_spu_setpalette(this->fd_spu, buf->content))
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_decode_spu: failed to set CLUT (%s)\n", strerror(errno));
    /* remember clut, when video out places some overlay we may need to restore it */
    memcpy(this->clut, buf->content, 16 * sizeof(uint32_t));
    this->dxr3_vo->clut_cluttered = 0;
    pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
    return;
  }
  if (buf->decoder_info[2] == SPU_DVD_SUBTYPE_NAV) {
    uint8_t *p = buf->content;

    llprintf(LOG_BTN, "got NAV packet\n");
    pthread_mutex_lock(&this->pci_lock);

    /* just watch out for menus */
    if (p[3] == 0xbf && p[6] == 0x00) { /* Private stream 2 */
      pci_t pci;

      navRead_PCI(&pci, p + 7);
      llprintf(LOG_BTN, "PCI packet hli_ss is %d\n", pci.hli.hl_gi.hli_ss);

      if (pci.hli.hl_gi.hli_ss == 1) {
	/* menu ahead */

	/* NAV packets contain start and end presentation timestamps, which tell the
	 * application, when the highlight information in the NAV is supposed to be valid.
	 * We handle these timestamps only in a very stripped-down way: We keep a list
	 * of NAV packets (or better: the PCI part of them), tagged with a VPTS timestamp
	 * telling, when the NAV should be processed. However, we only enqueue a new node
	 * into this list, when we receive new highlight information during an already
	 * showing menu. This happens very rarerly on common DVDs, so it is of low impact.
	 * And we only check for processing of queued entries at some prominent
	 * locations in this SPU decoder. Since presentation timestamps rarely solve a real
	 * purpose on most DVDs, this is ok compared to the full-blown solution, which would
	 * require a separate thread managing the queue all the time. */
	if (this->pci_cur.pci.hli.hl_gi.hli_ss != 0 &&
	    pci.hli.hl_gi.hli_s_ptm > this->pci_cur.pci.hli.hl_gi.hli_s_ptm) {
	  pci_node_t *node = &this->pci_cur;
	  printf("dxr3_decode_spu: DEBUG: allocating new PCI node for hli_s_ptm %d\n", pci.hli.hl_gi.hli_s_ptm);
	  /* append PCI at the end of the list */
	  while (node->next) node = node->next;
	  node->next = calloc(1, sizeof(pci_node_t));
	  node->next->vpts = this->stream->metronom->got_spu_packet(this->stream->metronom, pci.hli.hl_gi.hli_s_ptm);
	  node->next->next = NULL;
	  xine_fast_memcpy(&node->next->pci, &pci, sizeof(pci_t));
        } else {
	  dxr3_spudec_clear_nav_list(this);
	  /* menu ahead, remember PCI for later use */
	  xine_fast_memcpy(&this->pci_cur.pci, &pci, sizeof(pci_t));
	  dxr3_spudec_process_nav(this);
	}
      }

      if ((pci.hli.hl_gi.hli_ss == 0) && (this->pci_cur.pci.hli.hl_gi.hli_ss == 1)) {
        /* this is (or: should be, I hope I got this right) a
           subpicture plane, that hides all menu buttons */
        uint8_t empty_spu[] = {
          0x00, 0x26, 0x00, 0x08, 0x80, 0x00, 0x00, 0x80,
          0x00, 0x00, 0x00, 0x20, 0x01, 0x03, 0x00, 0x00,
          0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00,
          0x00, 0x01, 0x06, 0x00, 0x04, 0x00, 0x07, 0xFF,
          0x00, 0x01, 0x00, 0x20, 0x02, 0xFF };
        /* leaving menu */
	dxr3_spudec_clear_nav_list(this);
	this->pci_cur.pci.hli.hl_gi.hli_ss = 0;
	this->menu = 0;
	this->button_filter = 1;
	pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
        dxr3_spu_button(this->fd_spu, NULL);
        write(this->fd_spu, empty_spu, sizeof(empty_spu));
	pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
      }
    }
    pthread_mutex_unlock(&this->pci_lock);
    return;
  }

  /* We parse the SPUs command and end sequence here for two reasons:
   * 1. Look for the display duration entry in the spu packets.
   *    If the spu is a menu button highlight pane, this entry must not exist,
   *    because the spu is hidden, when the menu is left, not by timeout.
   *    Some broken dvds do not respect this and therefore confuse the spu
   *    decoding pipeline of the card. We fix this here.
   * 2. We need to handle SPU forcing here. When we only display forced
   *    SPUs, we have to prevent normal unforced SPUs from being displayed.
   *    But since that decision is only possible after parts of the SPU
   *    have already been written to the card, we have to manipulate the
   *    SPU's command sequence to prevent it from being displayed.
   */
  if (!state->spu_length) {
    state->spu_length   =  buf->content[0] << 8 | buf->content[1];
    state->spu_ctrl     = (buf->content[2] << 8 | buf->content[3]) + 2;
    state->spu_end      = 0;
    state->parse        = 0;
    state->bytes_passed = 0;
  }
  if (state->spu_length) {
    if (!state->parse) {
      int offset_in_buffer = state->spu_ctrl - state->bytes_passed;
      if (offset_in_buffer >= 0 && offset_in_buffer < buf->size)
	state->spu_end = buf->content[offset_in_buffer] << 8;
      offset_in_buffer++;
      if (offset_in_buffer >= 0 && offset_in_buffer < buf->size) {
	state->spu_end |= buf->content[offset_in_buffer];
	state->parse = 2;
      }
    }
    if (state->parse > 1) {
      int offset_in_buffer;
      do {
	offset_in_buffer = state->spu_ctrl + state->parse - state->bytes_passed;
	if (offset_in_buffer >= 0 && offset_in_buffer < buf->size) {
	  switch (buf->content[offset_in_buffer]) {
	  case 0x00:  /* force display */
	    state->parse++;
	    break;
	  case 0x01:  /* show */
	    /* when only forced SPUs are allowed, change show to hide */
	    if (spu_channel & 0x80) buf->content[offset_in_buffer] = 0x02;
	    /* falling through intended */
	  case 0x02:  /* hide */
	    state->parse++;
	    break;
	  case 0x03:  /* colour lookup table */
	  case 0x04:  /* transparency palette */
	    state->parse += 3;
	    break;
	  case 0x05:  /* position and size */
	    state->parse += 7;
	    break;
	  case 0x06:  /* field offsets */
	    state->parse += 5;
	    break;
	  case 0x07:  /* wipe */
	  case 0xff:  /* end */
	  default:
	    state->parse = 1;  /* bail out */
	  }
	}
      } while (offset_in_buffer < buf->size && state->parse > 1);
    }
    if (state->parse && this->menu) {
      int offset_in_buffer = state->spu_end - state->bytes_passed;
      if (offset_in_buffer >= 0 && offset_in_buffer < buf->size)
	buf->content[offset_in_buffer] = 0x00;
      offset_in_buffer++;
      if (offset_in_buffer >= 0 && offset_in_buffer < buf->size)
	buf->content[offset_in_buffer] = 0x00;
      offset_in_buffer += 3;
      if (offset_in_buffer >= 0 && offset_in_buffer < buf->size &&
	  buf->content[offset_in_buffer] == 0x02)
	buf->content[offset_in_buffer] = 0x00;
    }
    state->spu_length -= buf->size;
    if (state->spu_length < 0) state->spu_length = 0;
    state->bytes_passed += buf->size;
  }

  /* filter unwanted streams */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    llprintf(LOG_SPU, "Dropping SPU channel %d. Preview data\n", stream_id);
    return;
  }
  if (this->anamorphic && !this->dxr3_vo->widescreen_enabled &&
      this->stream->spu_channel_user == -1 && this->stream->spu_channel_letterbox >= 0) {
    /* Use the letterbox version of the subpicture for letterboxed display. */
    spu_channel = this->stream->spu_channel_letterbox;
  }
  if ((spu_channel & 0x1f) != stream_id) {
    llprintf(LOG_SPU, "Dropping SPU channel %d. Not selected stream_id\n", stream_id);
    return;
  }
  /* We used to filter for SPU forcing here as well, but this does not work
   * this way with the DXR3, because we have to evaluate the SPU command sequence
   * to detect, if a particular SPU is forced or not. See the parsing code above. */

  pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);

  /* write sync timestamp to the card */
  if (buf->pts) {
    int64_t vpts;
    uint32_t vpts32;

    vpts = this->stream->metronom->got_spu_packet(this->stream->metronom, buf->pts);
    llprintf(LOG_PTS, "pts = %" PRId64 " vpts = %" PRIu64 "\n", buf->pts, vpts);
    vpts32 = vpts;
    if (dxr3_spu_setpts(this->fd_spu, &vpts32))
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_decode_spu: spu setpts failed (%s)\n", strerror(errno));
  }

  /* has video out tampered with our palette */
  if (this->dxr3_vo->clut_cluttered) {
    if (dxr3_spu_setpalette(this->fd_spu, this->clut))
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_decode_spu: failed to set CLUT (%s)\n", strerror(errno));
    this->dxr3_vo->clut_cluttered = 0;
  }

  /* write spu data to the card */
  llprintf(LOG_SPU, "write: SPU_FD = %i\n",this->fd_spu);
  written = write(this->fd_spu, buf->content, buf->size);
  if (written < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_decode_spu: spu device write failed (%s)\n", strerror(errno));
    pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
    return;
  }
  if (written != buf->size)
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_decode_spu: Could only write %zd of %d spu bytes.\n", written, buf->size);

  pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
}

static void dxr3_spudec_reset(spu_decoder_t *this_gen)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  int i;

  for (i = 0; i < MAX_SPU_STREAMS; i++)
    this->spu_stream_state[i].spu_length = 0;
  pthread_mutex_lock(&this->pci_lock);
  dxr3_spudec_clear_nav_list(this);
  pthread_mutex_unlock(&this->pci_lock);
}

static void dxr3_spudec_discontinuity(spu_decoder_t *this_gen)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;

  pthread_mutex_lock(&this->pci_lock);
  dxr3_spudec_clear_nav_list(this);
  pthread_mutex_unlock(&this->pci_lock);
}

static void dxr3_spudec_dispose(spu_decoder_t *this_gen)
{
  static const uint8_t empty_spu[] = {
    0x00, 0x26, 0x00, 0x08, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x20, 0x01, 0x03, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x06, 0x00, 0x04, 0x00, 0x07, 0xFF,
    0x00, 0x01, 0x00, 0x20, 0x02, 0xFF };
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;

  llprintf(LOG_SPU, "close: SPU_FD = %i\n",this->fd_spu);
  pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
  /* clear any remaining spu */
  dxr3_spu_button(this->fd_spu, NULL);
  write(this->fd_spu, empty_spu, sizeof(empty_spu));
  close(this->fd_spu);
  this->fd_spu = 0;
  this->dxr3_vo->fd_spu = 0;
  pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);

  dxr3_spudec_clear_nav_list(this);
  xine_event_dispose_queue(this->event_queue);
  pthread_mutex_destroy(&this->pci_lock);
  this->class->instance = 0;
  free (this);
}

static int dxr3_spudec_interact_info(spu_decoder_t *this_gen, void *data)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;

  pthread_mutex_lock(&this->pci_lock);
  dxr3_spudec_update_nav(this);
  memcpy(data, &this->pci_cur.pci, sizeof(pci_t));
  pthread_mutex_unlock(&this->pci_lock);
  return 1;
}

static void dxr3_spudec_set_button(spu_decoder_t *this_gen, int32_t button, int32_t mode)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  em8300_button_t btn;

  llprintf(LOG_BTN, "setting button\n");
  this->buttonN = button;
  pthread_mutex_lock(&this->pci_lock);
  dxr3_spudec_update_nav(this);
  if (mode > 0 && !this->button_filter &&
      (dxr3_spudec_copy_nav_to_btn(this, mode - 1, &btn ) > 0)) {
    pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
    if (dxr3_spu_button(this->fd_spu, &btn))
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "dxr3_decode_spu: failed to set spu button (%s)\n", strerror(errno));
    pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
  }
  pthread_mutex_unlock(&this->pci_lock);
  if (mode == 2) this->button_filter = 1;
  llprintf(LOG_BTN, "buttonN = %u\n", this->buttonN);
}




static void dxr3_spudec_process_nav(dxr3_spudec_t *this)
{
  em8300_button_t btn;

  this->menu = 1;
  this->button_filter = 0;
  if (this->pci_cur.pci.hli.hl_gi.fosl_btnn > 0) {
    /* a button is forced here, inform nav plugin */
    xine_event_t event;
    this->buttonN      = this->pci_cur.pci.hli.hl_gi.fosl_btnn;
    event.type         = XINE_EVENT_INPUT_BUTTON_FORCE;
    event.stream       = this->stream;
    event.data         = &this->buttonN;
    event.data_length  = sizeof(this->buttonN);
    xine_event_send(this->stream, &event);
  }
  if ((dxr3_spudec_copy_nav_to_btn(this, 0, &btn ) > 0)) {
    pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
    if (dxr3_spu_button(this->fd_spu, &btn))
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
        "dxr3_decode_spu: failed to set spu button (%s)\n", strerror(errno));
    pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
  } else {
    /* current button does not exist -> use another one */
    xine_event_t event;

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("requested button not available\n"));

    if (this->buttonN > this->pci_cur.pci.hli.hl_gi.btn_ns)
      this->buttonN = this->pci_cur.pci.hli.hl_gi.btn_ns;
    else
      this->buttonN = 1;
    event.type         = XINE_EVENT_INPUT_BUTTON_FORCE;
    event.stream       = this->stream;
    event.data         = &this->buttonN;
    event.data_length  = sizeof(this->buttonN);
    xine_event_send(this->stream, &event);

    if ((dxr3_spudec_copy_nav_to_btn(this, 0, &btn ) > 0)) {
      pthread_mutex_lock(&this->dxr3_vo->spu_device_lock);
      if (dxr3_spu_button(this->fd_spu, &btn))
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	 "dxr3_decode_spu: failed to set spu button (%s)\n", strerror(errno));
      pthread_mutex_unlock(&this->dxr3_vo->spu_device_lock);
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "no working menu button found\n");
    }
  }
}

static int dxr3_spudec_copy_nav_to_btn(dxr3_spudec_t *this, int32_t mode, em8300_button_t *btn)
{
  btni_t *button_ptr = NULL;

  if ((this->buttonN <= 0) || (this->buttonN > this->pci_cur.pci.hli.hl_gi.btn_ns))
    return -1;

  /* choosing a button from a matching button group */
  if (this->anamorphic &&
      !this->dxr3_vo->widescreen_enabled &&
      this->stream->spu_channel_user == -1 &&
      this->stream->spu_channel_letterbox != this->stream->spu_channel &&
      this->stream->spu_channel_letterbox >= 0) {
    unsigned int btns_per_group = 36 / this->pci_cur.pci.hli.hl_gi.btngr_ns;

    /* use a letterbox button group for letterboxed anamorphic menus on tv out */
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 1 && (this->pci_cur.pci.hli.hl_gi.btngr1_dsp_ty & 2))
      button_ptr = &this->pci_cur.pci.hli.btnit[0 * btns_per_group + this->buttonN - 1];
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 2 && (this->pci_cur.pci.hli.hl_gi.btngr2_dsp_ty & 2))
      button_ptr = &this->pci_cur.pci.hli.btnit[1 * btns_per_group + this->buttonN - 1];
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 3 && (this->pci_cur.pci.hli.hl_gi.btngr3_dsp_ty & 2))
      button_ptr = &this->pci_cur.pci.hli.btnit[2 * btns_per_group + this->buttonN - 1];

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "No suitable letterbox button group found.\n");
    _x_assert(button_ptr);

  } else {
    unsigned int btns_per_group = 36 / this->pci_cur.pci.hli.hl_gi.btngr_ns;

    /* otherwise use a normal 4:3 or widescreen button group */
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 1 && !(this->pci_cur.pci.hli.hl_gi.btngr1_dsp_ty & 6))
      button_ptr = &this->pci_cur.pci.hli.btnit[0 * btns_per_group + this->buttonN - 1];
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 2 && !(this->pci_cur.pci.hli.hl_gi.btngr2_dsp_ty & 6))
      button_ptr = &this->pci_cur.pci.hli.btnit[1 * btns_per_group + this->buttonN - 1];
    if (!button_ptr && this->pci_cur.pci.hli.hl_gi.btngr_ns >= 3 && !(this->pci_cur.pci.hli.hl_gi.btngr3_dsp_ty & 6))
      button_ptr = &this->pci_cur.pci.hli.btnit[2 * btns_per_group + this->buttonN - 1];

  }
  if (!button_ptr) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_decode_spu: No suitable menu button group found, using group 1.\n");
    button_ptr = &this->pci_cur.pci.hli.btnit[this->buttonN - 1];
  }

  if(button_ptr->btn_coln != 0) {
    llprintf(LOG_BTN, "normal button clut, mode %d\n", mode);
    btn->color = (this->pci_cur.pci.hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] >> 16);
    btn->contrast = (this->pci_cur.pci.hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode]);
    btn->left = button_ptr->x_start;
    btn->top  = button_ptr->y_start;
    btn->right = button_ptr->x_end;
    btn->bottom = button_ptr->y_end;
    return 1;
  }
  return -1;
}

