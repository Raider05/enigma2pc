/*
 * Copyright (C) 2000-2004 the xine project
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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
 * This file was originally part of the OMS program.
 */

#ifndef __SPU_H__
#define __SPU_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <xine/video_out.h>
#include <xine/video_overlay.h>
#ifdef HAVE_DVDNAV
#  ifdef HAVE_DVDNAV_NAVTYPES_H
#    include <dvdnav/nav_types.h>
#  else
#    include <dvdread/nav_types.h>
#  endif
#else
#  include "nav_types.h"
#endif

#define NUM_SEQ_BUFFERS 50
#define MAX_STREAMS 32

typedef struct spudec_clut_struct {
#ifdef WORDS_BIGENDIAN
	uint8_t	entry0	: 4;
	uint8_t	entry1	: 4;
	uint8_t	entry2	: 4;
	uint8_t	entry3	: 4;
#else
	uint8_t	entry1	: 4;
	uint8_t	entry0	: 4;
	uint8_t	entry3	: 4;
	uint8_t	entry2	: 4;
#endif
} spudec_clut_t;

typedef struct {
  uint8_t   *buf;
  uint32_t   ra_offs;     /* reassembly offset */
  uint32_t   seq_len;
  uint32_t   buf_len;
  uint32_t   cmd_offs;
  int64_t    pts;        /* Base PTS of this sequence */
  int32_t    finished;   /* Has this control sequence been finished? */
  uint32_t   complete;   /* Has this reassembly been finished? */
  uint32_t   broken;     /* this SPU is broken and should be dropped */
} spudec_seq_t;

typedef struct {
  uint8_t *cmd_ptr;

  uint32_t field_offs[2];
  int32_t b_top,    o_top;
  int32_t b_bottom, o_bottom;
  int32_t b_left,   o_left;
  int32_t b_right,  o_right;

  int32_t modified;     /* Was the sub-picture modified? */
  int32_t visible;      /* Must the sub-picture be shown? */
  int32_t forced_display; /* This overlay is a menu */
  int32_t delay;        /* Delay in 90Khz / 1000 */
  int32_t need_clut;    /* doesn't have the right clut yet */
  int32_t cur_colors[4];/* current 4 colors been used */
  int32_t vobsub;       /* vobsub must be aligned to bottom */

  uint32_t clut[16];
} spudec_state_t;

typedef struct spudec_stream_state_s {
  spudec_seq_t        ra_seq;
  spudec_state_t      state;
  int64_t          vpts;
  int64_t          pts;
  int32_t          overlay_handle;
} spudec_stream_state_t;

typedef struct {
  spu_decoder_class_t   decoder_class;
} spudec_class_t;

typedef struct pci_node_s pci_node_t;
struct pci_node_s {
  pci_t         pci;
  uint64_t      vpts;
  pci_node_t   *next;
};

typedef struct spudec_decoder_s {
  spu_decoder_t    spu_decoder;

  spudec_class_t  *class;
  xine_stream_t   *stream;
  spudec_stream_state_t spudec_stream_state[MAX_STREAMS];

  video_overlay_event_t      event;
  video_overlay_object_t     object;
  int32_t          menu_handle;

  spudec_state_t      state;

  vo_overlay_t     overlay;
  int              ovl_caps;
  int              output_open;
  pthread_mutex_t  nav_pci_lock;
  pci_node_t       pci_cur;
  uint32_t         buttonN;  /* Current button number for highlights */
  int32_t          button_filter; /* Allow highlight changes or not */
  int64_t          last_event_vpts;
} spudec_decoder_t;

void spudec_reassembly (xine_t *xine, spudec_seq_t *seq, uint8_t *pkt_data, u_int pkt_len);
void spudec_process( spudec_decoder_t *this, int stream_id);
/* the nav functions must be called with the nav_pci_lock held */
void spudec_decode_nav( spudec_decoder_t *this, buf_element_t *buf);
void spudec_clear_nav_list(spudec_decoder_t *this);
void spudec_update_nav(spudec_decoder_t *this);
void spudec_process_nav(spudec_decoder_t *this);
int  spudec_copy_nav_to_overlay(xine_t *xine, pci_t* nav_pci, uint32_t* clut, int32_t button, int32_t mode,
                                vo_overlay_t * overlay, vo_overlay_t * base );

#endif
