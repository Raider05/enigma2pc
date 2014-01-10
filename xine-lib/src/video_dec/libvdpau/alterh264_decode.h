/* kate: tab-indent on; indent-width 4; mixedindent off; indent-mode cstyle; remove-trailing-space on; */
/*
 * Copyright (C) 2008-2013 the xine project
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
 */

#ifndef ALTERH264_DECODE_H
#define ALTERH264_DECODE_H

//#define LOG
#define LOG_MODULE "vdpau_h264"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "accel_vdpau.h"
#include <vdpau/vdpau.h>

#include "alterh264_bits_reader.h"



enum aspect_ratio
{
  ASPECT_UNSPECIFIED = 0,
  ASPECT_1_1,
  ASPECT_12_11,
  ASPECT_10_11,
  ASPECT_16_11,
  ASPECT_40_33,
  ASPECT_24_11,
  ASPECT_20_11,
  ASPECT_32_11,
  ASPECT_80_33,
  ASPECT_18_11,
  ASPECT_15_11,
  ASPECT_64_33,
  ASPECT_160_99,
  ASPECT_4_3,
  ASPECT_3_2,
  ASPECT_2_1,
  ASPECT_RESERVED,
  ASPECT_EXTENDED_SAR = 255
};



static const uint8_t zigzag_4x4[16] = {
  0, 1, 4, 8,
  5, 2, 3, 6,
  9, 12, 13, 10,
  7, 11, 14, 15
};

static const uint8_t zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t default_4x4_intra[16] = {
  6, 13, 13, 20,
  20, 20, 28, 28,
  28, 28, 32, 32,
  32, 37, 37, 42
};

static const uint8_t default_4x4_inter[16] = {
  10, 14, 14, 20,
  20, 20, 24, 24,
  24, 24, 27, 27,
  27, 30, 30, 34
};

static const uint8_t default_8x8_intra[64] = {
  6, 10, 10, 13, 11, 13, 16, 16,
  16, 16, 18, 18, 18, 18, 18, 23,
  23, 23, 23, 23, 23, 25, 25, 25,
  25, 25, 25, 25, 27, 27, 27, 27,
  27, 27, 27, 27, 29, 29, 29, 29,
  29, 29, 29, 31, 31, 31, 31, 31,
  31, 33, 33, 33, 33, 33, 36, 36,
  36, 36, 38, 38, 38, 40, 40, 42
};

static const uint8_t default_8x8_inter[64] = {
  9, 13, 13, 15, 13, 15, 17, 17,
  17, 17, 19, 19, 19, 19, 19, 21,
  21, 21, 21, 21, 21, 22, 22, 22,
  22, 22, 22, 22, 24, 24, 24, 24,
  24, 24, 24, 24, 25, 25, 25, 25,
  25, 25, 25, 27, 27, 27, 27, 27,
  27, 28, 28, 28, 28, 28, 30, 30,
  30, 30, 32, 32, 32, 33, 33, 35
};



typedef struct
{
  uint8_t aspect_ratio_info;
  uint8_t aspect_ratio_idc;
  uint16_t sar_width;
  uint16_t sar_height;
  uint8_t colour_desc;
  uint8_t colour_primaries;
  uint8_t timing_info;
  uint32_t num_units_in_tick;
  uint32_t time_scale;
} vui_param_t;



typedef struct
{
  uint8_t profile_idc;
  uint8_t level_idc;
  uint8_t seq_parameter_set_id;
  uint8_t constraint_set0_flag;
  uint8_t constraint_set1_flag;
  uint8_t constraint_set2_flag;
  uint8_t constraint_set3_flag;
  uint8_t chroma_format_idc;
  uint8_t separate_colour_plane_flag;
  uint8_t bit_depth_luma_minus8;
  uint8_t bit_depth_chroma_minus8;
  uint8_t qpprime_y_zero_transform_bypass_flag;
  uint8_t seq_scaling_matrix_present_flag;
  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];
  uint8_t log2_max_frame_num_minus4;
  uint8_t pic_order_cnt_type;
  uint8_t log2_max_pic_order_cnt_lsb_minus4;
  uint8_t delta_pic_order_always_zero_flag;
  int32_t offset_for_non_ref_pic;
  int32_t offset_for_top_to_bottom_field;
  uint8_t num_ref_frames_in_pic_order_cnt_cycle;
  int32_t offset_for_ref_frame[256];
  uint8_t num_ref_frames;
  uint8_t gaps_in_frame_num_value_allowed_flag;
  uint8_t pic_width_in_mbs_minus1;
  uint8_t pic_height_in_map_units_minus1;
  uint8_t frame_mbs_only_flag;
  uint8_t mb_adaptive_frame_field_flag;
  uint8_t direct_8x8_inference_flag;
  uint8_t frame_cropping_flag;
  uint16_t frame_crop_left_offset;
  uint16_t frame_crop_right_offset;
  uint16_t frame_crop_top_offset;
  uint16_t frame_crop_bottom_offset;
  uint8_t vui_parameters_present_flag;
  vui_param_t vui;
} seq_param_t;



typedef struct
{
  uint8_t pic_parameter_set_id;
  uint8_t seq_parameter_set_id;
  uint8_t entropy_coding_mode_flag;
  uint8_t pic_order_present_flag;
  /*uint8_t num_slice_groups_minus1;
     uint8_t slice_group_map_type;
     uint16_t run_length_minus1[64];
     uint16_t top_left[64];
     uint16_t bottom_right[64];
     uint8_t slice_group_change_direction_flag;
     uint16_t slice_group_change_rate_minus1;
     uint16_t pic_size_in_map_units_minus1;
     uint8_t slice_group_id[64]; */
  uint8_t num_ref_idx_l0_active_minus1;
  uint8_t num_ref_idx_l1_active_minus1;
  uint8_t weighted_pred_flag;
  uint8_t weighted_bipred_idc;
  int8_t pic_init_qp_minus26;
  int8_t pic_init_qs_minus26;
  int8_t chroma_qp_index_offset;
  uint8_t deblocking_filter_control_present_flag;
  uint8_t constrained_intra_pred_flag;
  uint8_t redundant_pic_cnt_present_flag;
  uint8_t transform_8x8_mode_flag;
  uint8_t pic_scaling_matrix_present_flag;
  uint8_t pic_scaling_list_present_flag[8];
  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];
  int8_t second_chroma_qp_index_offset;
} pic_param_t;



typedef struct
{
  uint8_t nal_ref_idc;
  uint8_t nal_unit_type;
  uint8_t slice_type;
  uint8_t pic_parameter_set_id;
  uint16_t frame_num;
  uint32_t MaxFrameNum;
  uint8_t field_pic_flag;
  uint8_t bottom_field_flag;
  uint16_t idr_pic_id;
  uint16_t pic_order_cnt_lsb;
  int32_t delta_pic_order_cnt_bottom;
  int32_t delta_pic_order_cnt[2];
  uint8_t redundant_pic_cnt;
  uint8_t num_ref_idx_l0_active_minus1;
  uint8_t num_ref_idx_l1_active_minus1;
} slice_param_t;


#define PICTURE_TOP_DONE    1
#define PICTURE_BOTTOM_DONE 2
#define PICTURE_DONE        3

#define SHORT_TERM_REF 1
#define LONG_TERM_REF  2

typedef struct
{
  uint8_t used;
  uint8_t missing_header;
  int64_t pts;
  uint8_t drop_pts;
  uint8_t completed;
  uint8_t top_field_first;
  uint16_t FrameNum;
  int32_t FrameNumWrap;
  int32_t PicNum[2];		/* 0:top, 1:bottom */
  uint8_t is_reference[2];	/* 0:top, 1:bottom, short or long term */
  uint8_t field_pic_flag;
  int32_t PicOrderCntMsb;
  int32_t TopFieldOrderCnt;
  int32_t BottomFieldOrderCnt;
  uint16_t pic_order_cnt_lsb;
  uint8_t mmc5;

  vo_frame_t *videoSurface;
} dpb_frame_t;



typedef struct
{
  uint32_t buf_offset;
  uint32_t len;
} slice_t;



typedef struct
{
  uint32_t coded_width;
  uint32_t reported_coded_width;
  uint32_t coded_height;
  uint32_t reported_coded_height;
  uint64_t video_step;		/* frame duration in pts units */
  uint64_t reported_video_step;	/* frame duration in pts units */
  double ratio;
  double reported_ratio;

  slice_t slices[68];
  int slices_count;
  int slice_mode;

  seq_param_t *seq_param[32];
  pic_param_t *pic_param[256];
  slice_param_t slice_param;

  dpb_frame_t *dpb[16];
  dpb_frame_t cur_pic;
  uint16_t prevFrameNum;
  uint16_t prevFrameNumOffset;
  uint8_t prevMMC5;

  int chroma;
  int top_field_first;
  VdpDecoderProfile profile;

  uint8_t *buf;			/* accumulate data */
  int bufseek;
  uint32_t bufsize;
  uint32_t bufpos;
  int start;

  int64_t pic_pts;

  bits_reader_t br;

  int vdp_runtime_nr;
  vdpau_accel_t *accel_vdpau;

  int reset;
  int startup_frame;

  uint8_t mode_frame;
  uint8_t flag_header;
  uint32_t frame_header_size;

  int color_matrix;

} sequence_t;



typedef struct
{
  video_decoder_class_t decoder_class;
} vdpau_h264_alter_class_t;



typedef struct vdpau_mpeg12_decoder_s
{
  video_decoder_t video_decoder;	/* parent video decoder structure */

  vdpau_h264_alter_class_t *class;
  xine_stream_t *stream;

  sequence_t sequence;

  VdpDecoder decoder;
  VdpDecoderProfile decoder_profile;
  uint32_t decoder_width;
  uint32_t decoder_height;

} vdpau_h264_alter_decoder_t;

#endif /* ALTERH264_DECODE_H */
