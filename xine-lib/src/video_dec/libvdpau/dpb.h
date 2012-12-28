/*
 * Copyright (C) 2008 Julian Scheel
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
 * dpb.h: Decoded Picture Buffer
 */

#ifndef DPB_H_
#define DPB_H_

#define MAX_DPB_COUNT 16

#include "nal.h"
#include "cpb.h"
#include <xine/video_out.h>
#include <xine/list.h>

#define USED_FOR_REF (top_is_reference || bottom_is_reference)

/**
 * ----------------------------------------------------------------------------
 * decoded picture
 * ----------------------------------------------------------------------------
 */

struct decoded_picture {
  vo_frame_t *img; /* this is the image we block, to make sure
                    * the surface is not double-used */

  /**
   * a decoded picture always contains a whole frame,
   * respective a field pair, so it can contain up to
   * 2 coded pics
   */
  struct coded_picture *coded_pic[2];

  int32_t frame_num_wrap;

  uint8_t top_is_reference;
  uint8_t bottom_is_reference;

  uint32_t lock_counter;
};

struct decoded_picture* init_decoded_picture(struct coded_picture *cpic,
    vo_frame_t *img);
void release_decoded_picture(struct decoded_picture *pic);
void lock_decoded_picture(struct decoded_picture *pic);
void decoded_pic_check_reference(struct decoded_picture *pic);
void decoded_pic_add_field(struct decoded_picture *pic,
    struct coded_picture *cpic);


/**
 * ----------------------------------------------------------------------------
 * dpb code starting here
 * ----------------------------------------------------------------------------
 */

/* Decoded Picture Buffer */
struct dpb {
  xine_list_t *reference_list;
  xine_list_t *output_list;

  int max_reorder_frames;
  int max_dpb_frames;
};

struct dpb* create_dpb(void);
void release_dpb(struct dpb *dpb);

/**
 * calculates the total number of frames in the dpb
 * when frames are used for reference and are not drawn
 * yet the result would be less then reference_list-size+
 * output_list-size
 */
int dpb_total_frames(struct dpb *dpb);

struct decoded_picture* dpb_get_next_out_picture(struct dpb *dpb, int do_flush);

struct decoded_picture* dpb_get_picture(struct dpb *dpb, uint32_t picnum);
struct decoded_picture* dpb_get_picture_by_ltpn(struct dpb *dpb, uint32_t longterm_picnum);
struct decoded_picture* dpb_get_picture_by_ltidx(struct dpb *dpb, uint32_t longterm_idx);

int dpb_set_unused_ref_picture_byltpn(struct dpb *dpb, uint32_t longterm_picnum);
int dpb_set_unused_ref_picture_bylidx(struct dpb *dpb, uint32_t longterm_idx);
int dpb_set_unused_ref_picture_lidx_gt(struct dpb *dpb, int32_t longterm_idx);

int dpb_unmark_picture_delayed(struct dpb *dpb, struct decoded_picture *pic);
int dpb_unmark_reference_picture(struct dpb *dpb, struct decoded_picture *pic);

int dpb_add_picture(struct dpb *dpb, struct decoded_picture *pic, uint32_t num_ref_frames);
int dpb_flush(struct dpb *dpb);
void dpb_free_all(struct dpb *dpb);
void dpb_clear_all_pts(struct dpb *dpb);

int fill_vdpau_reference_list(struct dpb *dpb, VdpReferenceFrameH264 *reflist);

int dp_top_field_first(struct decoded_picture *decoded_pic);

#endif /* DPB_H_ */
