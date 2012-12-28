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

#ifndef HAVE_VIDEO_OVERLAY_H
#define HAVE_VIDEO_OVERLAY_H

#include <xine/xine_internal.h>

#ifdef	__GNUC__
#define CLUT_Y_CR_CB_INIT(_y,_cr,_cb)	{y: (_y), cr: (_cr), cb: (_cb)}
#else
#define CLUT_Y_CR_CB_INIT(_y,_cr,_cb)	{ (_cb), (_cr), (_y) }
#endif

#define MAX_OBJECTS   50
#define MAX_EVENTS    50
#define MAX_SHOWING   (5 + 16)

#define OVERLAY_EVENT_NULL             0
#define OVERLAY_EVENT_SHOW             1
#define OVERLAY_EVENT_HIDE             2
#define OVERLAY_EVENT_MENU_BUTTON      3
#define OVERLAY_EVENT_FREE_HANDLE      8 /* Frees a handle, previous allocated via get_handle */

typedef struct video_overlay_object_s {
  int32_t	 handle;       /* Used to match Show and Hide events. */
  uint32_t	 object_type;  /* 0=Subtitle, 1=Menu */
  int64_t        pts;          /* Needed for Menu button compares */
  vo_overlay_t  *overlay;      /* The image data. */
  uint32_t	*palette;      /* If NULL, no palette contained in this event. */
  uint32_t       palette_type; /* 1 Y'CrCB, 2 R'G'B' */
} video_overlay_object_t;

/* This will hold all details of an event item, needed for event queue to function */
typedef struct video_overlay_event_s {
  int64_t	 vpts;        /* Time when event will action. 0 means action now */
/* Once video_out blend_yuv etc. can take rle_elem_t with Colour, blend and length information.
 * we can remove clut and blend from this structure.
 * This will allow for many more colours for OSD.
 */
  uint32_t	 event_type;  /* Show SPU, Show OSD, Hide etc. */
  video_overlay_object_t   object; /* The image data. */
} video_overlay_event_t;

video_overlay_manager_t *_x_video_overlay_new_manager(xine_t *) XINE_MALLOC XINE_PROTECTED;

#endif
