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
 * dpb.c: Implementing Decoded Picture Buffer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpb.h"
#include "dpb.h"
#include "nal.h"

#include "h264_parser.h"

#include "accel_vdpau.h"

#include <xine/video_out.h>

//#define DEBUG_DPB

int dp_top_field_first(struct decoded_picture *decoded_pic)
{
  int top_field_first = 1;


  if (decoded_pic->coded_pic[1] != NULL) {
    if (!decoded_pic->coded_pic[0]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[1]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[0]->top_field_order_cnt !=
            decoded_pic->coded_pic[1]->bottom_field_order_cnt) {
      top_field_first = decoded_pic->coded_pic[0]->top_field_order_cnt < decoded_pic->coded_pic[1]->bottom_field_order_cnt;
    } else if (decoded_pic->coded_pic[0]->slc_nal->slc.bottom_field_flag &&
        !decoded_pic->coded_pic[1]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[0]->bottom_field_order_cnt !=
            decoded_pic->coded_pic[1]->top_field_order_cnt) {
      top_field_first = decoded_pic->coded_pic[0]->bottom_field_order_cnt > decoded_pic->coded_pic[1]->top_field_order_cnt;
    }
  }

  if (decoded_pic->coded_pic[0]->flag_mask & PIC_STRUCT_PRESENT && decoded_pic->coded_pic[0]->sei_nal != NULL) {
    uint8_t pic_struct = decoded_pic->coded_pic[0]->sei_nal->sei.pic_timing.pic_struct;
    if(pic_struct == DISP_TOP_BOTTOM ||
        pic_struct == DISP_TOP_BOTTOM_TOP) {
      top_field_first = 1;
    } else if (pic_struct == DISP_BOTTOM_TOP ||
        pic_struct == DISP_BOTTOM_TOP_BOTTOM) {
      top_field_first = 0;
    } else if (pic_struct == DISP_FRAME) {
      top_field_first = 1;
    }
  }

  return top_field_first;
}

/**
 * ----------------------------------------------------------------------------
 * decoded picture
 * ----------------------------------------------------------------------------
 */

void free_decoded_picture(struct decoded_picture *pic);

struct decoded_picture* init_decoded_picture(struct coded_picture *cpic, vo_frame_t *img)
{
  struct decoded_picture *pic = calloc(1, sizeof(struct decoded_picture));

  pic->coded_pic[0] = cpic;

  decoded_pic_check_reference(pic);
  pic->img = img;
  pic->lock_counter = 1;

  return pic;
}

void decoded_pic_check_reference(struct decoded_picture *pic)
{
  int i;
  for(i = 0; i < 2; i++) {
    struct coded_picture *cpic = pic->coded_pic[i];
    if(cpic && (cpic->flag_mask & REFERENCE)) {
      // FIXME: this assumes Top Field First!
      if(i == 0) {
        pic->top_is_reference = cpic->slc_nal->slc.field_pic_flag
                    ? (cpic->slc_nal->slc.bottom_field_flag ? 0 : 1) : 1;
      }

      pic->bottom_is_reference = cpic->slc_nal->slc.field_pic_flag
                    ? (cpic->slc_nal->slc.bottom_field_flag ? 1 : 0) : 1;
    }
  }
}

void decoded_pic_add_field(struct decoded_picture *pic,
    struct coded_picture *cpic)
{
  pic->coded_pic[1] = cpic;

  decoded_pic_check_reference(pic);
}

void release_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  pic->lock_counter--;
  //printf("release decoded picture: %p (%d)\n", pic, pic->lock_counter);

  if(pic->lock_counter <= 0) {
    free_decoded_picture(pic);
  }
}

void lock_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  pic->lock_counter++;
  //printf("lock decoded picture: %p (%d)\n", pic, pic->lock_counter);
}

void free_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  if(pic->img != NULL) {
    pic->img->free(pic->img);
  }

  free_coded_picture(pic->coded_pic[1]);
  free_coded_picture(pic->coded_pic[0]);
  pic->coded_pic[0] = NULL;
  pic->coded_pic[1] = NULL;
  free(pic);
}




/**
 * ----------------------------------------------------------------------------
 * dpb code starting here
 * ----------------------------------------------------------------------------
 */

struct dpb* create_dpb(void)
{
    struct dpb *dpb = calloc(1, sizeof(struct dpb));

    dpb->output_list = xine_list_new();
    dpb->reference_list = xine_list_new();

    dpb->max_reorder_frames = MAX_DPB_COUNT;
    dpb->max_dpb_frames = MAX_DPB_COUNT;

    return dpb;
}

int dpb_total_frames(struct dpb *dpb)
{
  int num_frames = xine_list_size(dpb->output_list);

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while(ite) {
    struct decoded_picture *pic = xine_list_get_value(dpb->reference_list, ite);
    if (xine_list_find(dpb->output_list, pic) == NULL) {
      num_frames++;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return num_frames;
}

void release_dpb(struct dpb *dpb)
{
  if(!dpb)
    return;

  dpb_free_all(dpb);

  xine_list_delete(dpb->output_list);
  xine_list_delete(dpb->reference_list);

  free(dpb);
}

struct decoded_picture* dpb_get_next_out_picture(struct dpb *dpb, int do_flush)
{
  struct decoded_picture *pic = NULL;;
  struct decoded_picture *outpic = NULL;

  if(!do_flush &&
      xine_list_size(dpb->output_list) < dpb->max_reorder_frames &&
      dpb_total_frames(dpb) < dpb->max_dpb_frames) {
    return NULL;
  }

  xine_list_iterator_t ite = xine_list_back(dpb->output_list);
  while (ite) {
    pic = xine_list_get_value(dpb->output_list, ite);

    int32_t out_top_field_order_cnt = outpic != NULL ?
        outpic->coded_pic[0]->top_field_order_cnt : 0;
    int32_t top_field_order_cnt = pic->coded_pic[0]->top_field_order_cnt;

    int32_t out_bottom_field_order_cnt = outpic != NULL ?
        (outpic->coded_pic[1] != NULL ?
          outpic->coded_pic[1]->bottom_field_order_cnt :
          outpic->coded_pic[0]->top_field_order_cnt) : 0;
    int32_t bottom_field_order_cnt = pic->coded_pic[1] != NULL ?
              pic->coded_pic[1]->bottom_field_order_cnt :
              pic->coded_pic[0]->top_field_order_cnt;

    if (outpic == NULL ||
            (top_field_order_cnt <= out_top_field_order_cnt &&
                bottom_field_order_cnt <= out_bottom_field_order_cnt) ||
            (out_top_field_order_cnt <= 0 && top_field_order_cnt > 0 &&
               out_bottom_field_order_cnt <= 0 && bottom_field_order_cnt > 0) ||
            outpic->coded_pic[0]->flag_mask & IDR_PIC) {
      outpic = pic;
    }

    ite = xine_list_prev(dpb->output_list, ite);
  }

  return outpic;
}

struct decoded_picture* dpb_get_picture(struct dpb *dpb, uint32_t picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if ((pic->coded_pic[0]->pic_num == picnum ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->pic_num == picnum))) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

struct decoded_picture* dpb_get_picture_by_ltpn(struct dpb *dpb,
    uint32_t longterm_picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if (pic->coded_pic[0]->long_term_pic_num == longterm_picnum ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->long_term_pic_num == longterm_picnum)) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

struct decoded_picture* dpb_get_picture_by_ltidx(struct dpb *dpb,
    uint32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if (pic->coded_pic[0]->long_term_frame_idx == longterm_idx ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->long_term_frame_idx == longterm_idx)) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

int dpb_set_unused_ref_picture_byltpn(struct dpb *dpb, uint32_t longterm_picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if (pic->coded_pic[0]->long_term_pic_num == longterm_picnum) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->long_term_pic_num == longterm_picnum)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    if (found)
      return 0;

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}

int dpb_set_unused_ref_picture_bylidx(struct dpb *dpb, uint32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if (pic->coded_pic[0]->long_term_frame_idx == longterm_idx) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->long_term_frame_idx == longterm_idx)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    if (found)
      return 0;

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}

int dpb_set_unused_ref_picture_lidx_gt(struct dpb *dpb, int32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if (pic->coded_pic[0]->long_term_frame_idx >= longterm_idx) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->long_term_frame_idx >= longterm_idx)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}


int dpb_unmark_picture_delayed(struct dpb *dpb, struct decoded_picture *pic)
{
  if(!pic)
    return -1;

  xine_list_iterator_t ite = xine_list_find(dpb->output_list, pic);
  if (ite) {
    xine_list_remove(dpb->output_list, ite);
    release_decoded_picture(pic);

    return 0;
  }

  return -1;
}

int dpb_unmark_reference_picture(struct dpb *dpb, struct decoded_picture *pic)
{
  if(!pic)
    return -1;

  xine_list_iterator_t ite = xine_list_find(dpb->reference_list, pic);
  if (ite) {
    xine_list_remove(dpb->reference_list, ite);
    release_decoded_picture(pic);

    return 0;
  }

  return -1;
}

/*static int dpb_remove_picture_by_img(struct dpb *dpb, vo_frame_t *remimg)
{
  int retval = -1;
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while (ite) {
    pic = xine_list_get_value(dpb->output_list, ite);

    if (pic->img == remimg) {
      dpb_unmark_picture_delayed(dpb, pic);
      dpb->used--;
      retval = 0;
    }

    ite = xine_list_next(dpb->output_list, ite);
  }

  return retval;
}*/


int dpb_add_picture(struct dpb *dpb, struct decoded_picture *pic, uint32_t num_ref_frames)
{
#if 0
  /* this should never happen */
  pic->img->lock(pic->img);
  if (0 == dpb_remove_picture_by_img(dpb, pic->img))
    lprintf("H264/DPB broken stream: current img was already in dpb -- freed it\n");
  else
    pic->img->free(pic->img);
#endif

  /* add the pic to the output picture list, as no
   * pic would be immediately drawn.
   * acquire a lock for this list
   */
  lock_decoded_picture(pic);
  xine_list_push_back(dpb->output_list, pic);


  /* check if the pic is a reference pic,
   * if it is it should be added to the reference
   * list. another lock has to be acquired in that case
   */
  if (pic->coded_pic[0]->flag_mask & REFERENCE ||
      (pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->flag_mask & REFERENCE)) {
    lock_decoded_picture(pic);
    xine_list_push_back(dpb->reference_list, pic);

    /*
     * always apply the sliding window reference removal, if more reference
     * frames than expected are in the list. we will always remove the oldest
     * reference frame
     */
    if(xine_list_size(dpb->reference_list) > num_ref_frames) {
      struct decoded_picture *discard = xine_list_get_value(dpb->reference_list, xine_list_front(dpb->reference_list));
      dpb_unmark_reference_picture(dpb, discard);
    }
  }

#if DEBUG_DPB
  printf("DPB list sizes: Total: %2d, Output: %2d, Reference: %2d\n",
      dpb_total_frames(dpb), xine_list_size(dpb->output_list),
      xine_list_size(dpb->reference_list));
#endif

  return 0;
}

int dpb_flush(struct dpb *dpb)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    dpb_unmark_reference_picture(dpb, pic);

    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->reference_list);
  }

  return 0;
}

void dpb_free_all(struct dpb *dpb)
{
  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while(ite) {
    dpb_unmark_picture_delayed(dpb, xine_list_get_value(dpb->output_list, ite));
    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->output_list);
  }

  ite = xine_list_front(dpb->reference_list);
  while(ite) {
    dpb_unmark_reference_picture(dpb, xine_list_get_value(dpb->reference_list, ite));
    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->reference_list);
  }
}

void dpb_clear_all_pts(struct dpb *dpb)
{
  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while(ite) {
    struct decoded_picture *pic = xine_list_get_value(dpb->output_list, ite);
    pic->img->pts = 0;

    ite = xine_list_next(dpb->output_list, ite);
  }
}

int fill_vdpau_reference_list(struct dpb *dpb, VdpReferenceFrameH264 *reflist)
{
  struct decoded_picture *pic = NULL;

  int i = 0;
  int used_refframes = 0;

  xine_list_iterator_t ite = xine_list_back(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);
    reflist[i].surface = ((vdpau_accel_t*)pic->img->accel_data)->surface;
    reflist[i].is_long_term = pic->coded_pic[0]->used_for_long_term_ref ||
        (pic->coded_pic[1] != NULL && pic->coded_pic[1]->used_for_long_term_ref);

    reflist[i].frame_idx = pic->coded_pic[0]->used_for_long_term_ref ?
        pic->coded_pic[0]->long_term_pic_num :
        pic->coded_pic[0]->slc_nal->slc.frame_num;
    reflist[i].top_is_reference = pic->top_is_reference;
    reflist[i].bottom_is_reference = pic->bottom_is_reference;
    reflist[i].field_order_cnt[0] = pic->coded_pic[0]->top_field_order_cnt;
    reflist[i].field_order_cnt[1] = pic->coded_pic[1] != NULL ?
        pic->coded_pic[1]->bottom_field_order_cnt :
        pic->coded_pic[0]->bottom_field_order_cnt;
    i++;

    ite = xine_list_prev(dpb->reference_list, ite);
  }

  used_refframes = i;

  // fill all other frames with invalid handles
  while(i < 16) {
    reflist[i].bottom_is_reference = VDP_FALSE;
    reflist[i].top_is_reference = VDP_FALSE;
    reflist[i].frame_idx = 0;
    reflist[i].is_long_term = VDP_FALSE;
    reflist[i].surface = VDP_INVALID_HANDLE;
    reflist[i].field_order_cnt[0] = 0;
    reflist[i].field_order_cnt[1] = 0;
    i++;
  }

  return used_refframes;
}
