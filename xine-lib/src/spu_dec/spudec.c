/*
 * Copyright (C) 2002-2004 the xine project
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * spu.c - converts DVD subtitles to an XPM image
 *
 * Mostly based on hard work by:
 *
 * Copyright (C) 2000   Samuel Hocevar <sam@via.ecp.fr>
 *                       and Michel Lespinasse <walken@via.ecp.fr>
 *
 * Lots of rearranging by:
 *	Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *	Thomas Mirlacher <dent@cosy.sbg.ac.at>
 *		implemented reassembling
 *		cleaner implementation of SPU are saving
 *		overlaying (proof of concept for now)
 *		... and yes, it works now with oms
 *		added tranparency (provided by the SPU hdr)
 *		changed structures for easy porting to MGAs DVD mode
 * This file is part of xine
 * This file was originally part of the OMS program.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/buffer.h>
#include "xine-engine/bswap.h"
#ifdef HAVE_DVDNAV
#  ifdef HAVE_DVDNAV_NAVTYPES_H
#    include <dvdnav/nav_read.h>
#    include <dvdnav/nav_print.h>
#  else
#    include <dvdread/nav_read.h>
#    include <dvdread/nav_print.h>
#  endif
#else
#  include "nav_read.h"
#  include "nav_print.h"
#endif

#include "spudec.h"

/*
#define LOG_DEBUG 1
#define LOG_BUTTON 1
#define LOG_NAV 1
*/

static void spudec_do_commands (xine_t *xine, spudec_state_t *state, spudec_seq_t* seq, vo_overlay_t *ovl);
static void spudec_draw_picture (xine_t *xine, spudec_state_t *state, spudec_seq_t* seq, vo_overlay_t *ovl);
static void spudec_discover_clut (xine_t *xine, spudec_state_t *state, vo_overlay_t *ovl);
#ifdef LOG_DEBUG
static void spudec_print_overlay( vo_overlay_t *overlay );
#endif

void spudec_decode_nav(spudec_decoder_t *this, buf_element_t *buf) {
  uint8_t                  *p;
  uint32_t                  packet_len;
  uint32_t                  stream_id;
  uint32_t                  header_len;
  pci_t                     pci;
  dsi_t                     dsi;
  video_overlay_manager_t  *ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);

  p = buf->content;
  if (p[0] || p[1] || (p[2] != 1)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "libspudec:spudec_decode_nav:nav demux error! %02x %02x %02x (should be 0x000001) \n",p[0],p[1],p[2]);
    return;
  }

  packet_len = p[4] << 8 | p[5];
  stream_id  = p[3];

  header_len = 6;
  p += header_len;

  if (stream_id == 0xbf) { /* Private stream 2 */
/*   int i;
 *   for(i=0;i<80;i++) {
 *     printf("%02x ",p[i]);
 *   }
 *   printf("\n p[0]=0x%02x\n",p[0]);
 */
    if(p[0] == 0x00) {
#ifdef LOG_NAV
      printf("libspudec:nav_PCI\n");
#endif
      navRead_PCI(&pci, p+1);
#ifdef LOG_NAV
      printf("libspudec:nav:hli_ss=%u, hli_s_ptm=%u, hli_e_ptm=%u, btn_sl_e_ptm=%u pts=%lli\n",
       pci.hli.hl_gi.hli_ss,
       pci.hli.hl_gi.hli_s_ptm,
       pci.hli.hl_gi.hli_e_ptm,
       pci.hli.hl_gi.btn_se_e_ptm,
       buf->pts);
      printf("libspudec:nav:btn_sn/ofn=%u, btn_ns=%u, fosl_btnn=%u, foac_btnn=%u\n",
       pci.hli.hl_gi.btn_ofn, pci.hli.hl_gi.btn_ns,
       pci.hli.hl_gi.fosl_btnn, pci.hli.hl_gi.foac_btnn);
      printf("btngr_ns      %d\n",  pci.hli.hl_gi.btngr_ns);
      printf("btngr%d_dsp_ty    0x%02x\n", 1, pci.hli.hl_gi.btngr1_dsp_ty);
      printf("btngr%d_dsp_ty    0x%02x\n", 2, pci.hli.hl_gi.btngr2_dsp_ty);
      printf("btngr%d_dsp_ty    0x%02x\n", 3, pci.hli.hl_gi.btngr3_dsp_ty);
      //navPrint_PCI(&pci);
      //navPrint_PCI_GI(&pci.pci_gi);
      //navPrint_NSML_AGLI(&pci.nsml_agli);
      //navPrint_HLI(&pci.hli);
      //navPrint_HL_GI(&pci.hli.hl_gi, & btngr_ns, & btn_ns);
#endif
    }

    p += packet_len;

    /* We should now have a DSI packet. */
    /* We don't need anything from the DSI packet here. */
    if(p[6] == 0x01) {
      packet_len = p[4] << 8 | p[5];
      p += 6;
#ifdef LOG_NAV
      printf("NAV DSI packet\n");
#endif
      navRead_DSI(&dsi, p+1);

//      self->vobu_start = self->dsi.dsi_gi.nv_pck_lbn;
//      self->vobu_length = self->dsi.dsi_gi.vobu_ea;
    }
  }

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
  pthread_mutex_lock(&this->nav_pci_lock);
  switch (pci.hli.hl_gi.hli_ss) {
    case 0:
      /* No Highlight information for this VOBU */
      if ( this->pci_cur.pci.hli.hl_gi.hli_ss == 1) {
        /* Hide menu spu between menus */
#ifdef LOG_BUTTON
        printf("libspudec:nav:SHOULD HIDE SPU here\n");
#endif
        if( this->menu_handle < 0 ) {
          this->menu_handle = ovl_manager->get_handle(ovl_manager,1);
        }
        if( this->menu_handle >= 0 ) {
          this->event.object.handle = this->menu_handle;
          this->event.event_type = OVERLAY_EVENT_HIDE;
	  /* hide menu right now */
	  this->event.vpts = 0;
          ovl_manager->add_event(ovl_manager, (void *)&this->event);
        } else {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libspudec: No video_overlay handles left for menu\n");
        }
      }
      spudec_clear_nav_list(this);
      xine_fast_memcpy(&this->pci_cur.pci, &pci, sizeof(pci_t));
      /* incoming SPUs will be plain subtitles */
      this->event.object.object_type = 0;
      if (this->button_filter) {
	/* we possibly had buttons before, so we update the UI info */
	xine_event_t   event;
	xine_ui_data_t data;

	event.type = XINE_EVENT_UI_NUM_BUTTONS;
	event.data = &data;
	event.data_length = sizeof(data);
	data.num_buttons = 0;

	xine_event_send(this->stream, &event);
      }
      this->button_filter=0;

      break;
    case 1:
      /* All New Highlight information for this VOBU */
      if (this->pci_cur.pci.hli.hl_gi.hli_ss != 0 &&
	  pci.hli.hl_gi.hli_s_ptm > this->pci_cur.pci.hli.hl_gi.hli_s_ptm) {
	pci_node_t *node = &this->pci_cur;
#ifdef LOG_DEBUG
	printf("libspudec: allocating new PCI node for hli_s_ptm %d\n", pci.hli.hl_gi.hli_s_ptm);
#endif
	/* append PCI at the end of the list */
	while (node->next) node = node->next;
	node->next = malloc(sizeof(pci_node_t));
	node->next->vpts = this->stream->metronom->got_spu_packet(this->stream->metronom, pci.hli.hl_gi.hli_s_ptm);
	node->next->next = NULL;
	xine_fast_memcpy(&node->next->pci, &pci, sizeof(pci_t));
      } else {
        spudec_clear_nav_list(this);
        /* menu ahead, remember PCI for later use */
        xine_fast_memcpy(&this->pci_cur.pci, &pci, sizeof(pci_t));
        spudec_process_nav(this);
      }
      break;
    case 2:
      /* Use Highlight information from previous VOBU */
      if (this->pci_cur.next) {
	/* apply changes to last enqueued NAV */
	pci_node_t *node = this->pci_cur.next;
	while (node->next) node = node->next;
	node->pci.pci_gi.vobu_s_ptm = pci.pci_gi.vobu_s_ptm;
	node->pci.pci_gi.vobu_e_ptm = pci.pci_gi.vobu_e_ptm;
	node->pci.pci_gi.vobu_se_e_ptm = pci.pci_gi.vobu_se_e_ptm;
	spudec_update_nav(this);
      } else {
	this->pci_cur.pci.pci_gi.vobu_s_ptm = pci.pci_gi.vobu_s_ptm;
	this->pci_cur.pci.pci_gi.vobu_e_ptm = pci.pci_gi.vobu_e_ptm;
	this->pci_cur.pci.pci_gi.vobu_se_e_ptm = pci.pci_gi.vobu_se_e_ptm;
      }
      break;
    case 3:
      /* Use Highlight information from previous VOBU except commands, which come from this VOBU */
      if (this->pci_cur.next) {
	/* apply changes to last enqueued NAV */
	pci_node_t *node = this->pci_cur.next;
	while (node->next) node = node->next;
	node->pci.pci_gi.vobu_s_ptm = pci.pci_gi.vobu_s_ptm;
	node->pci.pci_gi.vobu_e_ptm = pci.pci_gi.vobu_e_ptm;
	node->pci.pci_gi.vobu_se_e_ptm = pci.pci_gi.vobu_se_e_ptm;
	/* FIXME: Add command copying here */
	spudec_update_nav(this);
      } else {
	this->pci_cur.pci.pci_gi.vobu_s_ptm = pci.pci_gi.vobu_s_ptm;
	this->pci_cur.pci.pci_gi.vobu_e_ptm = pci.pci_gi.vobu_e_ptm;
	this->pci_cur.pci.pci_gi.vobu_se_e_ptm = pci.pci_gi.vobu_se_e_ptm;
	/* FIXME: Add command copying here */
      }
      break;
   default:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "libspudec: unknown pci.hli.hl_gi.hli_ss = %d\n", pci.hli.hl_gi.hli_ss );
      break;
  }
  pthread_mutex_unlock(&this->nav_pci_lock);
  return;
}

void spudec_clear_nav_list(spudec_decoder_t *this)
{
  while (this->pci_cur.next) {
    pci_node_t *node = this->pci_cur.next->next;
    free(this->pci_cur.next);
    this->pci_cur.next = node;
  }
  /* invalidate current timestamp */
  this->pci_cur.pci.hli.hl_gi.hli_s_ptm = (uint32_t)-1;
}

void spudec_update_nav(spudec_decoder_t *this)
{
  metronom_clock_t *clock = this->stream->xine->clock;

  if (this->pci_cur.next && this->pci_cur.next->vpts <= clock->get_current_time(clock)) {
    pci_node_t *node = this->pci_cur.next;
    xine_fast_memcpy(&this->pci_cur, this->pci_cur.next, sizeof(pci_node_t));
    spudec_process_nav(this);
    free(node);
  }
}

void spudec_process_nav(spudec_decoder_t *this)
{
  /* incoming SPUs will be menus */
  this->event.object.object_type = 1;
  if (!this->button_filter) {
    /* we possibly entered a menu, so we update the UI button info */
    xine_event_t   event;
    xine_ui_data_t data;

    event.type = XINE_EVENT_UI_NUM_BUTTONS;
    event.data = &data;
    event.data_length = sizeof(data);
    data.num_buttons = this->pci_cur.pci.hli.hl_gi.btn_ns;

    xine_event_send(this->stream, &event);
  }
  this->button_filter=1;
}

void spudec_reassembly (xine_t *xine, spudec_seq_t *seq, uint8_t *pkt_data, u_int pkt_len)
{
#ifdef LOG_DEBUG
  printf ("libspudec: seq->complete = %d\n", seq->complete);
  printf("libspudec:1: seq->ra_offs = %d, seq->seq_len = %d, seq->buf_len = %d, seq->buf=%p\n",
             seq->ra_offs,
             seq->seq_len,
             seq->buf_len,
             seq->buf);
#endif
  if (seq->complete) {
    seq->seq_len = (((uint32_t)pkt_data[0])<<8) | pkt_data[1];
    seq->cmd_offs = (((uint32_t)pkt_data[2])<<8) | pkt_data[3];
    if (seq->cmd_offs >= seq->seq_len) {
      xprintf(xine, XINE_VERBOSITY_DEBUG, "libspudec:faulty stream\n");
      seq->broken = 1;
    }
    if (seq->buf_len < seq->seq_len) {
      seq->buf_len = seq->seq_len;
#ifdef LOG_DEBUG
      printf ("spu: MALLOC1: seq->buf %p, len=%d\n", seq->buf,seq->buf_len);
#endif
      if (seq->buf) {
        free(seq->buf);
        seq->buf = NULL;
      }
      seq->buf = malloc(seq->buf_len);
#ifdef LOG_DEBUG
      printf ("spu: MALLOC2: seq->buf %p, len=%d\n", seq->buf,seq->buf_len);
#endif

    }
    seq->ra_offs = 0;

#ifdef LOG_DEBUG
    printf ("spu: buf_len: %d\n", seq->buf_len);
    printf ("spu: cmd_off: %d\n", seq->cmd_offs);
#endif
  }

#ifdef LOG_DEBUG
  printf("libspudec:2: seq->ra_offs = %d, seq->seq_len = %d, seq->buf_len = %d, seq->buf=%p\n",
             seq->ra_offs,
             seq->seq_len,
             seq->buf_len,
             seq->buf);
#endif
  if (seq->ra_offs < seq->seq_len) {
    if (seq->ra_offs + pkt_len > seq->seq_len)
      pkt_len = seq->seq_len - seq->ra_offs;
    memcpy (seq->buf + seq->ra_offs, pkt_data, pkt_len);
    seq->ra_offs += pkt_len;
  } else {
    xprintf(xine, XINE_VERBOSITY_DEBUG, "libspudec:faulty stream\n");
    seq->broken = 1;
  }

  if (seq->ra_offs == seq->seq_len) {
    seq->finished = 0;
    seq->complete = 1;
    return; /* sequence ready */
  }
  seq->complete = 0;
  return;
}

void spudec_process (spudec_decoder_t *this, int stream_id) {
  spudec_seq_t    *cur_seq;
  video_overlay_manager_t *ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);
  int pending = 1;
  cur_seq = &this->spudec_stream_state[stream_id].ra_seq;

#ifdef LOG_DEBUG
  printf ("spu: Found SPU from stream %d pts=%lli vpts=%lli\n",stream_id,
          this->spudec_stream_state[stream_id].pts,
          this->spudec_stream_state[stream_id].vpts);
#endif
  this->state.cmd_ptr = cur_seq->buf + cur_seq->cmd_offs;
  this->state.modified = 1; /* Only draw picture if = 1 on first event of SPU */
  this->state.visible = OVERLAY_EVENT_SHOW;
  this->state.forced_display = 0; /* 0 - No value, 1 - Forced Display. */
  this->state.delay = 0;
  cur_seq->finished=0;

  do {
    if (!(cur_seq->finished) ) {
      pci_node_t *node;

      /* spu_channel is now set based on whether we are in the menu or not. */
      /* Bit 7 is set if only forced display SPUs should be shown */
      if ( (this->stream->spu_channel & 0x1f) != stream_id  ) {
#ifdef LOG_DEBUG
        printf ("spu: Dropping SPU channel %d. Not selected stream_id\n", stream_id);
#endif
        return;
      }
      /* parse SPU command sequence, this will update forced_display, so it must come
       * before the check for it */
      spudec_do_commands(this->stream->xine, &this->state, cur_seq, &this->overlay);
      /* FIXME: Check for Forced-display or subtitle stream
       *        For subtitles, open event.
       *        For menus, store it for later.
       */
      if (cur_seq->broken) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libspudec: dropping broken SPU\n");
	cur_seq->broken = 0;
	return;
      }
      if ( (this->state.forced_display == 0) && (this->stream->spu_channel & 0x80) ) {
#ifdef LOG_DEBUG
        printf ("spu: Dropping SPU channel %d. Only allow forced display SPUs\n", stream_id);
#endif
        return;
      }

#ifdef LOG_DEBUG
      spudec_print_overlay( &this->overlay );
      printf ("spu: forced display:%s\n", this->state.forced_display ? "Yes" : "No" );
#endif
      pthread_mutex_lock(&this->nav_pci_lock);
      /* search for a PCI that matches this SPU's PTS */
      for (node = &this->pci_cur; node; node = node->next)
	if (node->pci.hli.hl_gi.hli_s_ptm == this->spudec_stream_state[stream_id].pts)
	  break;
      if (node) {
        if (this->state.visible == OVERLAY_EVENT_HIDE) {
          /* menus are hidden via nav packet decoding, not here */
	  /* FIXME: James is not sure about this solution and may want to look this over.
	   *        I'm commiting it, because I haven't found a disc it breaks, but it fixes
	   *        some instead.   Michael Roitzsch */
          pthread_mutex_unlock(&this->nav_pci_lock);
          continue;
        }
        if (node->pci.hli.hl_gi.fosl_btnn > 0) {
	  xine_event_t event;

          this->buttonN     = node->pci.hli.hl_gi.fosl_btnn;
          event.type        = XINE_EVENT_INPUT_BUTTON_FORCE;
	  event.stream      = this->stream;
	  event.data        = &this->buttonN;
	  event.data_length = sizeof(this->buttonN);
          xine_event_send(this->stream, &event);
        }
#ifdef LOG_BUTTON
        fprintf(stderr, "libspudec:Full Overlay\n");
#endif
        if (!spudec_copy_nav_to_overlay(this->stream->xine,
					&node->pci, this->state.clut,
					this->buttonN, 0, &this->overlay, &this->overlay)) {
          /* current button does not exist -> use another one */
	  xine_event_t event;

	  if (this->buttonN > node->pci.hli.hl_gi.btn_ns)
	    this->buttonN = node->pci.hli.hl_gi.btn_ns;
	  else
	    this->buttonN = 1;
          event.type        = XINE_EVENT_INPUT_BUTTON_FORCE;
	  event.stream      = this->stream;
	  event.data        = &this->buttonN;
	  event.data_length = sizeof(this->buttonN);
          xine_event_send(this->stream, &event);
	  spudec_copy_nav_to_overlay(this->stream->xine,
				     &node->pci, this->state.clut,
				     this->buttonN, 0, &this->overlay, &this->overlay);
        }
      } else {
      /* Subtitle and not a menu button */
        int i;
        for (i = 0;i < 4; i++) {
          this->overlay.hili_color[i] = this->overlay.color[i];
          this->overlay.hili_trans[i] = this->overlay.trans[i];
        }
      }
      pthread_mutex_unlock(&this->nav_pci_lock);

      if ((this->state.modified) ) {
        spudec_draw_picture(this->stream->xine, &this->state, cur_seq, &this->overlay);
      }

      if (this->state.need_clut) {
        spudec_discover_clut(this->stream->xine, &this->state, &this->overlay);
      }

      if (this->state.vobsub) {
        int width, height;
        int64_t duration;

        /*
         * vobsubs are usually played with a scaled-down stream (not full DVD
         * resolution), therefore we should try to realign it.
         */

        this->stream->video_out->status(this->stream->video_out, NULL,
                                        &width, &height, &duration );

        this->overlay.x = (width - this->overlay.width) / 2;
        this->overlay.y = height - this->overlay.height;
      }

      /* Subtitle */
      if( this->menu_handle < 0 ) {
        this->menu_handle = ovl_manager->get_handle(ovl_manager,1);
      }

      if( this->menu_handle < 0 ) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"libspudec: No video_overlay handles left for menu\n");
        return;
      }
      this->event.object.handle = this->menu_handle;
      this->event.object.pts = this->spudec_stream_state[stream_id].pts;

      xine_fast_memcpy(this->event.object.overlay,
             &this->overlay,
             sizeof(vo_overlay_t));
      this->overlay.rle=NULL;
      /* For force display menus */
      //if ( !(this->state.visible) ) {
      //  this->state.visible = OVERLAY_EVENT_SHOW;
      //}

      this->event.event_type = this->state.visible;
      /*
      printf("spu event %d handle: %d vpts: %lli\n", this->event.event_type,
         this->event.object.handle, this->event.vpts );
      */

      this->event.vpts = this->spudec_stream_state[stream_id].vpts+(this->state.delay*1000);

      /* Keep all the events in the correct order. */
      /* This corrects for errors during estimation around discontinuity */
      if( this->event.vpts < this->last_event_vpts ) {
        this->event.vpts = this->last_event_vpts + 1;
      }
      this->last_event_vpts = this->event.vpts;

#ifdef LOG_BUTTON
      fprintf(stderr, "libspudec: add_event type=%d : current time=%lld, spu vpts=%lli\n",
        this->event.event_type,
        this->stream->xine->clock->get_current_time(this->stream->xine->clock),
        this->event.vpts);
#endif
      ovl_manager->add_event(ovl_manager, (void *)&this->event);
    } else {
      pending = 0;
    }
  } while (pending);

}

#define CMD_SPU_FORCE_DISPLAY	0x00
#define CMD_SPU_SHOW		0x01
#define CMD_SPU_HIDE		0x02
#define CMD_SPU_SET_PALETTE	0x03
#define CMD_SPU_SET_ALPHA	0x04
#define CMD_SPU_SET_SIZE	0x05
#define CMD_SPU_SET_PXD_OFFSET	0x06
#define CMD_SPU_WIPE		0x07  /* Not currently implemented */
#define CMD_SPU_EOF		0xff

static void spudec_do_commands(xine_t *xine, spudec_state_t *state, spudec_seq_t* seq, vo_overlay_t *ovl)
{
  uint8_t *buf = state->cmd_ptr;
  uint8_t *next_seq;
  int32_t param_length;

#ifdef LOG_DEBUG
  printf ("spu: SPU DO COMMANDS\n");
#endif

  state->delay = (buf[0] << 8) + buf[1];
#ifdef LOG_DEBUG
  printf ("spu: \tdelay=%d\n",state->delay);
#endif
  next_seq = seq->buf + (buf[2] << 8) + buf[3];
  buf += 4;
#ifdef LOG_DEBUG
  printf ("spu: \tnext_seq=%d\n",next_seq - seq->buf);
#endif

/* if next equals current, this is the last one
 */
  if (state->cmd_ptr >= next_seq)
    next_seq = seq->buf + seq->seq_len; /* allow to run until end */

  state->cmd_ptr = next_seq;

  while (buf < next_seq && *buf != CMD_SPU_EOF) {
    switch (*buf) {
    case CMD_SPU_SHOW:		/* show subpicture */
#ifdef LOG_DEBUG
      printf ("spu: \tshow subpicture\n");
#endif
      state->visible = OVERLAY_EVENT_SHOW;
      buf++;
      break;

    case CMD_SPU_HIDE:		/* hide subpicture */
#ifdef LOG_DEBUG
      printf ("spu: \thide subpicture\n");
#endif
      state->visible = OVERLAY_EVENT_HIDE;
      buf++;
      break;

    case CMD_SPU_SET_PALETTE: {	/* CLUT */
      spudec_clut_t *clut = (spudec_clut_t *) (buf+1);

      state->cur_colors[3] = clut->entry0;
      state->cur_colors[2] = clut->entry1;
      state->cur_colors[1] = clut->entry2;
      state->cur_colors[0] = clut->entry3;

/* This is a bit out of context for now */
      ovl->color[3] = state->clut[clut->entry0];
      ovl->color[2] = state->clut[clut->entry1];
      ovl->color[1] = state->clut[clut->entry2];
      ovl->color[0] = state->clut[clut->entry3];

#ifdef LOG_DEBUG
      printf ("spu: \tclut [%x %x %x %x]\n",
	      ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
      printf ("spu: \tclut base [%x %x %x %x]\n",
	      clut->entry0, clut->entry1, clut->entry2, clut->entry3);
#endif
      state->modified = 1;
      buf += 3;
      break;
    }
    case CMD_SPU_SET_ALPHA:	{	/* transparency palette */
      spudec_clut_t *trans = (spudec_clut_t *) (buf+1);
/* This should go into state for now */

      ovl->trans[3] = trans->entry0;
      ovl->trans[2] = trans->entry1;
      ovl->trans[1] = trans->entry2;
      ovl->trans[0] = trans->entry3;

#ifdef LOG_DEBUG
      printf ("spu: \ttrans [%d %d %d %d]\n",
	       ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
#endif
      state->modified = 1;
      buf += 3;
      break;
    }

    case CMD_SPU_SET_SIZE:		/* image coordinates */
/*    state->o_left  = (buf[1] << 4) | (buf[2] >> 4);
      state->o_right = (((buf[2] & 0x0f) << 8) | buf[3]);

      state->o_top    = (buf[4]  << 4) | (buf[5] >> 4);
      state->o_bottom = (((buf[5] & 0x0f) << 8) | buf[6]);
 */
      ovl->x      = (buf[1] << 4) | (buf[2] >> 4);
      ovl->y      = (buf[4]  << 4) | (buf[5] >> 4);
      ovl->width  = (((buf[2] & 0x0f) << 8) | buf[3]) - ovl->x + 1;
      ovl->height = (((buf[5] & 0x0f) << 8) | buf[6]) - ovl->y + 1;
      ovl->hili_top    = -1;
      ovl->hili_bottom = -1;
      ovl->hili_left   = -1;
      ovl->hili_right  = -1;

#ifdef LOG_DEBUG
      printf ("spu: \tx = %d y = %d width = %d height = %d\n",
	      ovl->x, ovl->y, ovl->width, ovl->height );
#endif
      state->modified = 1;
      buf += 7;
      break;

    case CMD_SPU_SET_PXD_OFFSET:	/* image top[0] field / image bottom[1] field*/
      state->field_offs[0] = (((u_int)buf[1]) << 8) | buf[2];
      state->field_offs[1] = (((u_int)buf[3]) << 8) | buf[4];

#ifdef LOG_DEBUG
      printf ("spu: \toffset[0] = %d offset[1] = %d\n",
	       state->field_offs[0], state->field_offs[1]);
#endif

      if ((state->field_offs[0] >= seq->seq_len) ||
          (state->field_offs[1] >= seq->seq_len)) {
        xprintf(xine, XINE_VERBOSITY_DEBUG, "libspudec:faulty stream\n");
        seq->broken = 1;
      }
      state->modified = 1;
      buf += 5;
      break;

    case CMD_SPU_WIPE:
#ifdef LOG_DEBUG
      printf ("libspudec: \tSPU_WIPE not implemented yet\n");
#endif
      param_length = (buf[1] << 8) | (buf[2]);
      buf += 1 + param_length;
      break;

    case CMD_SPU_FORCE_DISPLAY:
#ifdef LOG_DEBUG
      printf ("libspudec: \tForce Display/Menu\n");
#endif
      state->forced_display = 1;
      buf++;
      break;

    default:
      xprintf(xine, XINE_VERBOSITY_DEBUG, "libspudec: unknown seqence command (%02x)\n", buf[0]);
      /* FIXME: SPU should be dropped, and buffers resynced */
      buf = next_seq;
      seq->broken = 1;
      break;
    }
  }

  if (next_seq >= seq->buf + seq->seq_len)
    seq->finished = 1;       /* last sub-sequence */
}

/* FIXME: Get rid of all these static values */
static uint8_t *bit_ptr[2];
static int field;		// which field we are currently decoding
static int put_x, put_y;

static u_int get_bits (u_int bits)
{
  static u_int data;
  static u_int bits_left;
  u_int ret = 0;

  if (!bits) {	/* for realignment to next byte */
    bits_left = 0;
  }

  while (bits) {
    if (bits > bits_left) {
      ret |= data << (bits - bits_left);
      bits -= bits_left;

      data = *bit_ptr[field]++;
      bits_left = 8;
    } else {
      bits_left -= bits;
      ret |= data >> (bits_left);
      data &= (1 << bits_left) - 1;
      bits = 0;
    }
  }

  return ret;
}

static int spudec_next_line (vo_overlay_t *spu)
{
  get_bits (0); // byte align rle data

  put_x = 0;
  put_y++;
  field ^= 1; // Toggle fields

  if (put_y >= spu->height) {
#ifdef LOG_DEBUG
    printf ("spu: put_y >= spu->height\n");
#endif
    return -1;
  }
  return 0;
}

static void spudec_draw_picture (xine_t *xine, spudec_state_t *state, spudec_seq_t* seq, vo_overlay_t *ovl)
{
  rle_elem_t *rle;
  field = 0;
  bit_ptr[0] = seq->buf + state->field_offs[0];
  bit_ptr[1] = seq->buf + state->field_offs[1];
  put_x = put_y = 0;
  get_bits (0);	/* Reset/init bit code */

/*  ovl->x      = state->o_left;
 *  ovl->y      = state->o_top;
 *  ovl->width  = state->o_right - state->o_left + 1;
 *  ovl->height = state->o_bottom - state->o_top + 1;

 *  ovl->hili_top    = 0;
 *  ovl->hili_bottom = ovl->height - 1;
 *  ovl->hili_left   = 0;
 *  ovl->hili_right  = ovl->width - 1;
 */

  /* allocate for the worst case:
   *  - both fields running to the very end
   *  - 2 RLE elements per byte meaning single pixel RLE
   */
  ovl->data_size = ((seq->cmd_offs - state->field_offs[0]) +
                    (seq->cmd_offs - state->field_offs[1])) * 2 * sizeof(rle_elem_t);

  if (ovl->rle) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "libspudec: spudec_draw_picture: ovl->rle is not empty!!!! It should be!!! "
	     "You should never see this message.\n");
    free(ovl->rle);
    ovl->rle=NULL;
  }
  ovl->rle = malloc(ovl->data_size);

  state->modified = 0; /* mark as already processed */
  rle = ovl->rle;
#ifdef LOG_DEBUG
  printf ("libspudec: Draw RLE=%p\n",rle);
#endif

  while (bit_ptr[1] < seq->buf + seq->cmd_offs) {
    u_int len;
    u_int vlc;

    vlc = get_bits (4);
    if (vlc < 0x0004) {
      vlc = (vlc << 4) | get_bits (4);
      if (vlc < 0x0010) {
	vlc = (vlc << 4) | get_bits (4);
	if (vlc < 0x0040) {
	  vlc = (vlc << 4) | get_bits (4);
	}
      }
    }

    len   = vlc >> 2;

    /* if len == 0 -> end sequence - fill to end of line */
    if (len == 0)
      len = ovl->width - put_x;

    rle->len = len;
    rle->color = vlc & 0x03;
    rle++;
    put_x += len;

    if (put_x >= ovl->width) {
      if (spudec_next_line (ovl) < 0)
        break;
    }
  }

  ovl->num_rle = rle - ovl->rle;
  ovl->rgb_clut = 0;
  ovl->unscaled = 0;
#ifdef LOG_DEBUG
  printf ("spu: Num RLE=%d\n",ovl->num_rle);
  printf ("spu: Date size=%d\n",ovl->data_size);
  printf ("spu: sizeof RLE=%d\n",sizeof(rle_elem_t));
#endif
}

/* Heuristic to discover the colors used by the subtitles
   and assign a "readable" pallete to them.
   Currently looks for sequence of border-fg-border or
   border1-border2-fg-border2-border1.
   MINFOUND is the number of ocurrences threshold.
*/
#define MINFOUND 20
static void spudec_discover_clut(xine_t *xine, spudec_state_t *state, vo_overlay_t *ovl)
{
  int bg,c;
  int seqcolor[10];
  int n,i;
  rle_elem_t *rle;

  int found[2][16] = { { 0, }, };

  static const clut_t text_clut[] = {
  CLUT_Y_CR_CB_INIT(0x80, 0x90, 0x80),
  CLUT_Y_CR_CB_INIT(0x00, 0x90, 0x00),
  CLUT_Y_CR_CB_INIT(0xff, 0x90, 0x00)
  };

  rle = ovl->rle;

  /* this seems to be a problem somewhere else,
     why rle is null? */
  if( !rle )
    return;

  /* suppose the first and last pixels are bg */
  if( rle[0].color != rle[ovl->num_rle-1].color )
    return;

  bg = rle[0].color;

  i = 0;
  for( n = 0; n < ovl->num_rle; n++ )
  {
    c = rle[n].color;

    if( c == bg )
    {
      if( i == 3 && seqcolor[1] == seqcolor[3] )
      {
        found[0][seqcolor[2]]++;
        if( found[0][seqcolor[2]] > MINFOUND )
        {
           memcpy(&state->clut[state->cur_colors[seqcolor[1]]], &text_clut[1],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[2]]], &text_clut[2],
             sizeof(clut_t));
           ovl->color[seqcolor[1]] = state->clut[state->cur_colors[seqcolor[1]]];
           ovl->color[seqcolor[2]] = state->clut[state->cur_colors[seqcolor[2]]];
           state->need_clut = 0;
           break;
        }
      }
      if( i == 5 && seqcolor[1] == seqcolor[5]
             && seqcolor[2] == seqcolor[4] )
      {
        found[1][seqcolor[3]]++;
        if( found[1][seqcolor[3]] > MINFOUND )
        {
           memcpy(&state->clut[state->cur_colors[seqcolor[1]]], &text_clut[0],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[2]]], &text_clut[1],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[3]]], &text_clut[2],
             sizeof(clut_t));
           ovl->color[seqcolor[1]] = state->clut[state->cur_colors[seqcolor[1]]];
           ovl->color[seqcolor[2]] = state->clut[state->cur_colors[seqcolor[2]]];
           ovl->color[seqcolor[3]] = state->clut[state->cur_colors[seqcolor[3]]];
           state->need_clut = 0;
           break;
        }
      }
      i = 0;
      seqcolor[i] = c;
    }
    else if ( i < 6 )
    {
      i++;
      seqcolor[i] = c;
    }
  }
}

#ifdef LOG_DEBUG
static void spudec_print_overlay( vo_overlay_t *ovl ) {
  printf ("spu: OVERLAY to show\n");
  printf ("spu: \tx = %d y = %d width = %d height = %d\n",
	  ovl->x, ovl->y, ovl->width, ovl->height );
  printf ("spu: \tclut [%x %x %x %x]\n",
	  ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
  printf ("spu: \ttrans [%d %d %d %d]\n",
	  ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
  printf ("spu: \tclip top=%d bottom=%d left=%d right=%d\n",
	  ovl->hili_top, ovl->hili_bottom, ovl->hili_left, ovl->hili_right);
  printf ("spu: \tclip_clut [%x %x %x %x]\n",
	  ovl->hili_color[0], ovl->hili_color[1], ovl->hili_color[2], ovl->hili_color[3]);
  printf ("spu: \thili_trans [%d %d %d %d]\n",
	  ovl->hili_trans[0], ovl->hili_trans[1], ovl->hili_trans[2], ovl->hili_trans[3]);
  return;
}
#endif

int spudec_copy_nav_to_overlay(xine_t *xine, pci_t* nav_pci, uint32_t* clut,
			       int32_t button, int32_t mode, vo_overlay_t * overlay, vo_overlay_t * base ) {
  btni_t *button_ptr = NULL;
  unsigned int btns_per_group;
  int i;

  if((button <= 0) || (button > nav_pci->hli.hl_gi.btn_ns))
    return 0;

  btns_per_group = 36 / nav_pci->hli.hl_gi.btngr_ns;

  /* choose button group: we can always use a normal 4:3 or widescreen button group
   * as long as xine blends the overlay before scaling the image to its aspect */
  if (!button_ptr && nav_pci->hli.hl_gi.btngr_ns >= 1 && !(nav_pci->hli.hl_gi.btngr1_dsp_ty & 6))
    button_ptr = &nav_pci->hli.btnit[0 * btns_per_group + button - 1];
  if (!button_ptr && nav_pci->hli.hl_gi.btngr_ns >= 2 && !(nav_pci->hli.hl_gi.btngr2_dsp_ty & 6))
    button_ptr = &nav_pci->hli.btnit[1 * btns_per_group + button - 1];
  if (!button_ptr && nav_pci->hli.hl_gi.btngr_ns >= 3 && !(nav_pci->hli.hl_gi.btngr3_dsp_ty & 6))
    button_ptr = &nav_pci->hli.btnit[2 * btns_per_group + button - 1];
  if (!button_ptr) {
    xprintf(xine, XINE_VERBOSITY_DEBUG,
	    "libspudec: No suitable menu button group found, using group 1.\n");
    button_ptr = &nav_pci->hli.btnit[button - 1];
  }

  /* button areas in the nav packet are in screen coordinates,
   * overlay clipping areas are in overlay coordinates;
   * therefore we must subtract the display coordinates of the underlying overlay */
  overlay->hili_left   = (button_ptr->x_start > base->x) ? (button_ptr->x_start - base->x) : 0;
  overlay->hili_top    = (button_ptr->y_start > base->y) ? (button_ptr->y_start - base->y) : 0;
  overlay->hili_right  = (button_ptr->x_end   > base->x) ? (button_ptr->x_end   - base->x) : 0;
  overlay->hili_bottom = (button_ptr->y_end   > base->y) ? (button_ptr->y_end   - base->y) : 0;
  if(button_ptr->btn_coln != 0) {
#ifdef LOG_BUTTON
    fprintf(stderr, "libspudec: normal button clut\n");
#endif
    for (i = 0;i < 4; i++) {
      overlay->hili_color[i] = clut[0xf & (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] >> (16 + 4*i))];
      overlay->hili_trans[i] = 0xf & (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] >> (4*i));
    }
  } else {
#ifdef LOG_BUTTON
    fprintf(stderr, "libspudec: abnormal button clut\n");
#endif
    for (i = 0;i < 4; i++) {
#ifdef LOG_BUTTON
      printf("libspudec:btn_coln = 0, hili_color = colour\n");
#endif
      overlay->hili_color[i] = overlay->color[i];
      overlay->hili_trans[i] = overlay->trans[i];
    }
  }

  /* spudec_print_overlay( overlay ); */
#ifdef LOG_BUTTON
  printf("libspudec:xine_decoder.c:NAV to SPU pts match!\n");
#endif

  return 1;
}
