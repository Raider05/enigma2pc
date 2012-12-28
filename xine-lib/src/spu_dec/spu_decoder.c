/*
 * Copyright (C) 2000-2008 the xine project
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * This file is part of xine, a unix video player.
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
 * stuff needed to turn libspu into a xine decoder plugin
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <xine/xine_internal.h>
#include <xine/buffer.h>
#include "xine-engine/bswap.h"
#include <xine/xineutils.h>
#ifdef HAVE_DVDNAV
#  ifdef HAVE_DVDNAV_NAVTYPES_H
#    include <dvdnav/nav_types.h>
#    include <dvdnav/nav_read.h>
#  else
#    include <dvdread/nav_types.h>
#    include <dvdread/nav_read.h>
#  endif
#else
#  include "nav_read.h"
#  include "nav_types.h"
#endif

#include "spudec.h"

/*
#define LOG_DEBUG 1
#define LOG_BUTTON 1
*/

static const clut_t default_clut[] = {
  CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x51, 0xef, 0x5a),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x36, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x51, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x5c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x1c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef)
};

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  const uint8_t stream_id = buf->type & 0x1f ;

#ifdef LOG_DEBUG
  printf("libspudec:got buffer type = %x\n", buf->type);
#endif

  /* check, if we need to process the next PCI from the list */
  pthread_mutex_lock(&this->nav_pci_lock);
  spudec_update_nav(this);
  pthread_mutex_unlock(&this->nav_pci_lock);

  if ( (buf->type & 0xffff0000) != BUF_SPU_DVD ||
       !(buf->decoder_flags & BUF_FLAG_SPECIAL) ||
       buf->decoder_info[1] != BUF_SPECIAL_SPU_DVD_SUBTYPE )
    return;

  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_CLUT ) {
#ifdef LOG_DEBUG
    printf("libspudec: SPU CLUT\n");
#endif
    if (buf->content[0]) { /* cheap endianess detection */
      xine_fast_memcpy(this->state.clut, buf->content, sizeof(uint32_t)*16);
    } else {
      int i;
      uint32_t *clut = (uint32_t*) buf->content;
      for (i = 0; i < 16; i++)
        this->state.clut[i] = bswap_32(clut[i]);
    }
    this->state.need_clut = 0;
    return;
  }

  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_NAV ) {
#ifdef LOG_DEBUG
    printf("libspudec:got nav packet 1\n");
#endif
    spudec_decode_nav(this,buf);
    return;
  }

  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_VOBSUB_PACKAGE ) {
    this->state.vobsub = 1;
  }

#ifdef LOG_DEBUG
  printf("libspudec:got buffer type = %x\n", buf->type);
#endif
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)  /* skip preview data */
    return;

  if (buf->pts) {
    metronom_t *metronom = this->stream->metronom;
    int64_t vpts = metronom->got_spu_packet(metronom, buf->pts);

    this->spudec_stream_state[stream_id].vpts = vpts; /* Show timer */
    this->spudec_stream_state[stream_id].pts = buf->pts; /* Required to match up with NAV packets */
  }

  spudec_reassembly(this->stream->xine,
		    &this->spudec_stream_state[stream_id].ra_seq, buf->content, buf->size);
  if(this->spudec_stream_state[stream_id].ra_seq.complete == 1) {
    if(this->spudec_stream_state[stream_id].ra_seq.broken) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libspudec: dropping broken SPU\n");
      this->spudec_stream_state[stream_id].ra_seq.broken = 0;
    } else
      spudec_process(this,stream_id);
  }
}

static void spudec_reset (spu_decoder_t *this_gen) {
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  video_overlay_manager_t *ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);
  int i;

  if( this->menu_handle >= 0 )
    ovl_manager->free_handle(ovl_manager,
			     this->menu_handle);
  this->menu_handle = -1;

  for (i=0; i < MAX_STREAMS; i++) {
    if( this->spudec_stream_state[i].overlay_handle >= 0 )
      ovl_manager->free_handle(ovl_manager,
			       this->spudec_stream_state[i].overlay_handle);
    this->spudec_stream_state[i].overlay_handle = -1;
    this->spudec_stream_state[i].ra_seq.complete = 1;
    this->spudec_stream_state[i].ra_seq.broken = 0;
  }

  pthread_mutex_lock(&this->nav_pci_lock);
  spudec_clear_nav_list(this);
  pthread_mutex_unlock(&this->nav_pci_lock);
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;

  pthread_mutex_lock(&this->nav_pci_lock);
  spudec_clear_nav_list(this);
  pthread_mutex_unlock(&this->nav_pci_lock);
}


static void spudec_dispose (spu_decoder_t *this_gen) {

  spudec_decoder_t         *this = (spudec_decoder_t *) this_gen;
  video_overlay_manager_t  *ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);

  if( this->menu_handle >= 0 )
    ovl_manager->free_handle(ovl_manager,
			     this->menu_handle);
  this->menu_handle = -1;

  int i;
  for (i=0; i < MAX_STREAMS; i++) {
    if( this->spudec_stream_state[i].overlay_handle >= 0 )
      ovl_manager->free_handle(ovl_manager,
			       this->spudec_stream_state[i].overlay_handle);
    this->spudec_stream_state[i].overlay_handle = -1;
    free (this->spudec_stream_state[i].ra_seq.buf);
  }

  spudec_clear_nav_list(this);
  pthread_mutex_destroy(&this->nav_pci_lock);

  free (this->event.object.overlay);
  free (this);
}

/* gets the current already correctly processed nav_pci info */
/* This is not perfectly in sync with the display, but all the same, */
/* much closer than doing it at the input stage. */
/* returns a bool for error/success.*/
static int spudec_get_interact_info (spu_decoder_t *this_gen, void *data) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  /*printf("get_interact_info() called\n");*/
  if (!this || !data)
    return 0;

  /*printf("get_interact_info() coping nav_pci\n");*/
  pthread_mutex_lock(&this->nav_pci_lock);
  spudec_update_nav(this);
  memcpy(data, &this->pci_cur.pci, sizeof(pci_t) );
  pthread_mutex_unlock(&this->nav_pci_lock);
  return 1;

}

static void spudec_set_button (spu_decoder_t *this_gen, int32_t button, int32_t show) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  /* This function will move to video_overlay
  * when video_overlay does menus */

  video_overlay_manager_t *ovl_manager;
  video_overlay_event_t *overlay_event = calloc(1, sizeof(video_overlay_event_t));
  vo_overlay_t        *overlay = calloc(1, sizeof(vo_overlay_t));

  /* FIXME: Watch out for threads. We should really put a lock on this
   * because events is a different thread than decode_data */

  if( this->menu_handle < 0 ) {
    if (this->stream->video_out) {
      ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);
      this->menu_handle = ovl_manager->get_handle(ovl_manager,1);
    }
  }
#ifdef LOG_BUTTON
  printf ("libspudec:xine_decoder.c:spudec_event_listener:this=%p\n",this);
  printf ("libspudec:xine_decoder.c:spudec_event_listener:this->menu_handle=%d\n",this->menu_handle);
#endif
  if(this->menu_handle < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "Menu handle alloc failed. No more overlays objects available. Only %d at once please.",
	    MAX_OBJECTS);
    free(overlay_event);
    free(overlay);
    return;
  }

  if (show > 0) {
#ifdef LOG_NAV
    fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:buttonN = %u show=%d\n",
             button,
             show);
#endif
    this->buttonN = button;
    if (this->button_filter != 1) {
#ifdef LOG_BUTTON
      fprintf (stdout,"libspudec:xine_decoder.c:spudec_event_listener:buttonN updates not allowed\n");
#endif
      /* Only update highlight is the menu will let us */
      free(overlay_event);
      free(overlay);
      return;
    }
    if (show == 2) {
      this->button_filter = 2;
    }
    pthread_mutex_lock(&this->nav_pci_lock);
    spudec_update_nav(this);
    overlay_event->object.handle = this->menu_handle;
    overlay_event->object.pts = this->pci_cur.pci.hli.hl_gi.hli_s_ptm;
    overlay_event->object.overlay=overlay;
    overlay_event->event_type = OVERLAY_EVENT_MENU_BUTTON;
#ifdef LOG_BUTTON
    fprintf(stderr, "libspudec:Button Overlay\n");
#endif
    spudec_copy_nav_to_overlay(this->stream->xine, &this->pci_cur.pci, this->state.clut,
			       this->buttonN, show-1, overlay, &this->overlay );
    pthread_mutex_unlock(&this->nav_pci_lock);
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "libspudec:xine_decoder.c:spudec_event_listener:HIDE ????\n");
    printf("We dropped out here for some reason");
    _x_abort();
    overlay_event->object.handle = this->menu_handle;
    overlay_event->event_type = OVERLAY_EVENT_HIDE;
  }
  overlay_event->vpts = 0;
  if (this->stream->video_out) {
    ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);
#ifdef LOG_BUTTON
    fprintf(stderr, "libspudec: add_event type=%d : current time=%lld, spu vpts=%lli\n",
            overlay_event->event_type,
            this->stream->xine->clock->get_current_time(this->stream->xine->clock),
            overlay_event->vpts);
#endif
    ovl_manager->add_event (ovl_manager, (void *)overlay_event);
    free(overlay_event);
    free(overlay);
  } else {
    free(overlay_event);
    free(overlay);
  }
  return;
}

static spu_decoder_t *open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  spudec_decoder_t *this ;

  this = (spudec_decoder_t *) calloc(1, sizeof (spudec_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_interact_info   = spudec_get_interact_info;
  this->spu_decoder.set_button          = spudec_set_button;
  this->stream                          = stream;
  this->class                           = (spudec_class_t *) class_gen;

  this->menu_handle = -1;
  this->buttonN = 1;
  this->event.object.overlay = calloc(1, sizeof(vo_overlay_t));

  pthread_mutex_init(&this->nav_pci_lock, NULL);
  this->pci_cur.pci.hli.hl_gi.hli_ss  = 0;
  this->pci_cur.next                  = NULL;

  this->ovl_caps    = stream->video_out->get_capabilities(stream->video_out);
  this->output_open = 0;
  this->last_event_vpts = 0;

  int i;
  for (i=0; i < MAX_STREAMS; i++) {
    this->spudec_stream_state[i].ra_seq.complete = 1;
    this->spudec_stream_state[i].overlay_handle = -1;
  }

/* FIXME:Do we really need a default clut? */
  xine_fast_memcpy(this->state.clut, default_clut, sizeof(this->state.clut));
  this->state.need_clut = 1;
  this->state.vobsub = 0;

  return &this->spu_decoder;
}

static void *init_plugin (xine_t *xine, void *data) {

  spudec_class_t *this;

  this = calloc(1, sizeof (spudec_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "spudec";
  this->decoder_class.description     = N_("DVD/VOB SPU decoder plugin");
  this->decoder_class.dispose         = default_spu_decoder_class_dispose;

  lprintf ("libspudec:init_plugin called\n");
  return this;
}

/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_DVD, 0 };

static const decoder_info_t dec_info_data = {
  supported_types,     /* supported types */
  5                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER, 17, "spudec", XINE_VERSION_CODE, &dec_info_data, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
