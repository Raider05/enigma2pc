/*
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 * Copyright (C) 2008-2013 the xine project
 * Copyright (C) 2008 Christophe Thommeret <hftom@free.fr>
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
 * vdpau_mpeg12.c, a mpeg1/2 video stream parser using VDPAU hardware decoder
 *
 */

/*#define LOG*/
#define LOG_MODULE "vdpau_mpeg12"

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
#include "bits_reader.h"

#include <vdpau/vdpau.h>

#define sequence_header_code    0xb3
#define sequence_error_code     0xb4
#define sequence_end_code       0xb7
#define group_start_code        0xb8
#define extension_start_code    0xb5
#define user_data_start_code    0xb2
#define picture_start_code      0x00
#define begin_slice_start_code  0x01
#define end_slice_start_code    0xaf

#define sequence_ext_sc         1
#define quant_matrix_ext_sc     3
#define picture_coding_ext_sc   8
#define sequence_display_ext_sc 2

#define I_FRAME   1
#define P_FRAME   2
#define B_FRAME   3

#define PICTURE_TOP     1
#define PICTURE_BOTTOM  2
#define PICTURE_FRAME   3

/*#define MAKE_DAT*/ /*do NOT define this, unless you know what you do */
#ifdef MAKE_DAT
static int nframes;
static FILE *outfile;
#endif



/* default intra quant matrix, in zig-zag order */
static const uint8_t default_intra_quantizer_matrix[64] = {
    8,
    16, 16,
    19, 16, 19,
    22, 22, 22, 22,
    22, 22, 26, 24, 26,
    27, 27, 27, 26, 26, 26,
    26, 27, 27, 27, 29, 29, 29,
    34, 34, 34, 29, 29, 29, 27, 27,
    29, 29, 32, 32, 34, 34, 37,
    38, 37, 35, 35, 34, 35,
    38, 38, 40, 40, 40,
    48, 48, 46, 46,
    56, 56, 58,
    69, 69,
    83
};

uint8_t mpeg2_scan_norm[64] = {
    /* Zig-Zag scan pattern */
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};



typedef struct {
  VdpPictureInfoMPEG1Or2  vdp_infos; /* first field, also used for frame */
  VdpPictureInfoMPEG1Or2  vdp_infos2; /* second field */
  int                     slices_count, slices_count2;
  uint8_t                 *slices;
  int                     slices_size;
  int                     slices_pos, slices_pos_top;

  int                     progressive_frame;
  int                     repeat_first_field;
} picture_t;



typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  double      video_step; /* frame duration in pts units */
  double      reported_video_step; /* frame duration in pts units */
  double      ratio;
   
  VdpDecoderProfile profile;
  int         horizontal_size_value;
  int         vertical_size_value;
  int         aspect_ratio_information;
  int         frame_rate_code;
  int         progressive_sequence;
  int         chroma;
  int         horizontal_size_extension;
  int         vertical_size_extension;
  int         frame_rate_extension_n;
  int         frame_rate_extension_d;
  int         display_horizontal_size;
  int         display_vertical_size;
  int         top_field_first;

  int         have_header;
  int         have_display_extension;

  uint8_t     *buf; /* accumulate data */
  int         bufseek;
  uint32_t    bufsize;
  uint32_t    bufpos;
  int         start;

  picture_t   picture;
  vo_frame_t  *forward_ref;
  vo_frame_t  *backward_ref;

  int64_t    cur_pts, seq_pts;

  vdpau_accel_t *accel_vdpau;

  bits_reader_t  br;

  int         vdp_runtime_nr;
  int         reset;

} sequence_t;



typedef struct {
  video_decoder_class_t   decoder_class;
} vdpau_mpeg12_class_t;



typedef struct vdpau_mpeg12_decoder_s {
  video_decoder_t         video_decoder;  /* parent video decoder structure */

  vdpau_mpeg12_class_t    *class;
  xine_stream_t           *stream;

  sequence_t              sequence;

  VdpDecoder              decoder;
  VdpDecoderProfile       decoder_profile;
  uint32_t                decoder_width;
  uint32_t                decoder_height;

} vdpau_mpeg12_decoder_t;


static void picture_ready( vdpau_mpeg12_decoder_t *vd, uint8_t end_of_sequence );



static void reset_picture( picture_t *pic )
{
  lprintf( "reset_picture\n" );
  pic->vdp_infos.picture_structure = pic->vdp_infos2.picture_structure = 0;
  pic->vdp_infos2.intra_dc_precision = pic->vdp_infos.intra_dc_precision = 0;
  pic->vdp_infos2.frame_pred_frame_dct = pic->vdp_infos.frame_pred_frame_dct = 1;
  pic->vdp_infos2.concealment_motion_vectors = pic->vdp_infos.concealment_motion_vectors = 0;
  pic->vdp_infos2.intra_vlc_format = pic->vdp_infos.intra_vlc_format = 0;
  pic->vdp_infos2.alternate_scan = pic->vdp_infos.alternate_scan = 0;
  pic->vdp_infos2.q_scale_type = pic->vdp_infos.q_scale_type = 0;
  pic->vdp_infos2.top_field_first = pic->vdp_infos.top_field_first = 1;
  pic->slices_count = 0;
  pic->slices_count2 = 0;
  pic->slices_pos = 0;
  pic->slices_pos_top = 0;
  pic->progressive_frame = 0;
  pic->repeat_first_field = 0;
}



static void init_picture( picture_t *pic )
{
  pic->slices_size = 2048;
  pic->slices = (uint8_t*)malloc(pic->slices_size);
  reset_picture( pic );
}



static void reset_sequence( sequence_t *sequence, int free_refs )
{
  sequence->cur_pts = sequence->seq_pts = 0;
  if ( sequence->forward_ref )
    sequence->forward_ref->pts = 0;
  if ( sequence->backward_ref )
    sequence->backward_ref->pts = 0;

  if ( !free_refs )
    return;

  sequence->bufpos = 0;
  sequence->bufseek = 0;
  sequence->start = -1;
  if ( sequence->forward_ref )
    sequence->forward_ref->free( sequence->forward_ref );
  sequence->forward_ref = NULL;
  if ( sequence->backward_ref )
    sequence->backward_ref->free( sequence->backward_ref );
  sequence->backward_ref = NULL;
  sequence->top_field_first = 0;
  sequence->reset = VO_NEW_SEQUENCE_FLAG;
}



static void free_sequence( sequence_t *sequence )
{
  lprintf( "init_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = VDP_DECODER_PROFILE_MPEG1;
  sequence->chroma = 0;
  sequence->video_step = 3600;
  reset_sequence( sequence, 1 );
}



static void sequence_header( vdpau_mpeg12_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  int i, j;

  if ( !sequence->have_header )
    sequence->have_header = 1;

  sequence->profile = VDP_DECODER_PROFILE_MPEG1;
  sequence->horizontal_size_extension = 0;
  sequence->vertical_size_extension = 0;
  sequence->have_display_extension = 0;

  bits_reader_set( &sequence->br, buf, len );
  sequence->horizontal_size_value = read_bits( &sequence->br, 12 );
  lprintf( "horizontal_size_value: %d\n", sequence->horizontal_size_value );
  sequence->vertical_size_value = read_bits( &sequence->br, 12 );
  lprintf( "vertical_size_value: %d\n", sequence->vertical_size_value );
  sequence->aspect_ratio_information = read_bits( &sequence->br, 4 );
  lprintf( "aspect_ratio_information: %d\n", sequence->aspect_ratio_information );
  sequence->frame_rate_code = read_bits( &sequence->br, 4 );
  lprintf( "frame_rate_code: %d\n", sequence->frame_rate_code );
#ifdef LOG
  int tmp;
  tmp = read_bits( &sequence->br, 18 );
  lprintf( "bit_rate_value: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "marker_bit: %d\n", tmp );
  tmp = read_bits( &sequence->br, 10 );
  lprintf( "vbv_buffer_size_value: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "constrained_parameters_flag: %d\n", tmp );
#else
  skip_bits(&sequence->br, 30);
#endif
  i = read_bits( &sequence->br, 1 );
  lprintf( "load_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.intra_quantizer_matrix[mpeg2_scan_norm[j]] = read_bits( &sequence->br, 8 );
    }
  }
  else {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.intra_quantizer_matrix[mpeg2_scan_norm[j]] = default_intra_quantizer_matrix[j];
    }
  }

  i = read_bits( &sequence->br, 1 );
  lprintf( "load_non_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.non_intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.non_intra_quantizer_matrix[mpeg2_scan_norm[j]] = read_bits( &sequence->br, 8 );
    }
  }
  else {
    memset( sequence->picture.vdp_infos.non_intra_quantizer_matrix, 16, 64 );
    memset( sequence->picture.vdp_infos2.non_intra_quantizer_matrix, 16, 64 );
  }
}



static void process_sequence_mpeg12_dependent_data( vdpau_mpeg12_decoder_t *this_gen )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  int frame_rate_value_n, frame_rate_value_d;

  sequence->coded_width  = sequence->horizontal_size_value | (sequence->horizontal_size_extension << 14);
  sequence->coded_height = sequence->vertical_size_value   | (sequence->vertical_size_extension   << 14);

  switch ( sequence->frame_rate_code ) {
    case 1:  frame_rate_value_n = 24; frame_rate_value_d = 1001; break; /* 23.976.. */
    case 2:  frame_rate_value_n = 24; frame_rate_value_d = 1000; break; /* 24 */
    case 3:  frame_rate_value_n = 25; frame_rate_value_d = 1000; break; /* 25 */
    case 4:  frame_rate_value_n = 30; frame_rate_value_d = 1001; break; /* 29.97.. */
    case 5:  frame_rate_value_n = 30; frame_rate_value_d = 1000; break; /* 30 */
    case 6:  frame_rate_value_n = 50; frame_rate_value_d = 1000; break; /* 50 */
    case 7:  frame_rate_value_n = 60; frame_rate_value_d = 1001; break; /* 59.94.. */
    case 8:  frame_rate_value_n = 60; frame_rate_value_d = 1000; break; /* 60 */
    default: frame_rate_value_n = 50; frame_rate_value_d = 1000; /* assume 50 */
  }

  sequence->video_step = 90.0 * (frame_rate_value_d * (sequence->frame_rate_extension_d + 1))
                              / (frame_rate_value_n * (sequence->frame_rate_extension_n + 1));

  if ( sequence->profile==VDP_DECODER_PROFILE_MPEG1 ) {
    double pel_aspect_ratio; /* height / width */

    switch ( sequence->aspect_ratio_information ) {
      case  1: pel_aspect_ratio = 1.0000;
      case  2: pel_aspect_ratio = 0.6735;
      case  3: pel_aspect_ratio = 0.7031;
      case  4: pel_aspect_ratio = 0.7615;
      case  5: pel_aspect_ratio = 0.8055;
      case  6: pel_aspect_ratio = 0.8437;
      case  7: pel_aspect_ratio = 0.8935;
      case  8: pel_aspect_ratio = 0.9157;
      case  9: pel_aspect_ratio = 0.9815;
      case 10: pel_aspect_ratio = 1.0255;
      case 11: pel_aspect_ratio = 1.0695;
      case 12: pel_aspect_ratio = 1.0950;
      case 13: pel_aspect_ratio = 1.1575;
      case 14: pel_aspect_ratio = 1.2015;
      default: pel_aspect_ratio = 1.0000; /* fallback */
    }

    sequence->ratio = ((double)sequence->coded_width/(double)sequence->coded_height)/pel_aspect_ratio;
  }
  else {
    switch ( sequence->aspect_ratio_information ) {
      case 1:  sequence->ratio = sequence->have_display_extension
                               ? ((double)sequence->display_horizontal_size/(double)sequence->display_vertical_size)/1.0
                               : ((double)sequence->coded_width/(double)sequence->coded_height)/1.0;
                               break;
      case 2:  sequence->ratio = 4.0/3.0;  break;
      case 3:  sequence->ratio = 16.0/9.0; break;
      case 4:  sequence->ratio = 2.21;     break;
      default: sequence->ratio = ((double)sequence->coded_width/(double)sequence->coded_height)/1.0;
    }
  }

  if ( sequence->have_header == 1 ) {
    sequence->have_header = 2;
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_WIDTH, sequence->coded_width );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, sequence->coded_height );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*sequence->ratio) );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_FRAME_DURATION, (sequence->reported_video_step = sequence->video_step) );
    _x_meta_info_set_utf8( this_gen->stream, XINE_META_INFO_VIDEOCODEC, "MPEG1/2 (vdpau)" );
    xine_event_t event;
    xine_format_change_data_t data;
    event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
    event.stream = this_gen->stream;
    event.data = &data;
    event.data_length = sizeof(data);
    data.width = sequence->coded_width;
    data.height = sequence->coded_height;
    //data.aspect = sequence->ratio;
    data.aspect = sequence->aspect_ratio_information;
    xine_event_send( this_gen->stream, &event );
  }
  else if ( sequence->have_header == 2 && sequence->reported_video_step != sequence->video_step ) {
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_FRAME_DURATION, (sequence->reported_video_step = sequence->video_step) );
  }
}



static void picture_header( vdpau_mpeg12_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  process_sequence_mpeg12_dependent_data(this_gen);

  if ( sequence->profile==VDP_DECODER_PROFILE_MPEG1 )
    sequence->picture.vdp_infos.picture_structure = PICTURE_FRAME;

  VdpPictureInfoMPEG1Or2 *infos = &sequence->picture.vdp_infos;

  if ( sequence->picture.vdp_infos.picture_structure==PICTURE_FRAME ) {
	picture_ready( this_gen, 0 );
    reset_picture( &sequence->picture );
  }
  else if ( sequence->picture.vdp_infos.picture_structure && sequence->picture.vdp_infos2.picture_structure ) {
	picture_ready( this_gen, 0 );
    reset_picture( &sequence->picture );
  }
  else if ( sequence->picture.vdp_infos.picture_structure ) {
    infos = &sequence->picture.vdp_infos2;
	sequence->picture.slices_pos_top = sequence->picture.slices_pos;

    sequence->cur_pts = 0; /* ignore pts of second field */
  }

  /* take over pts for next issued image */ 
  if ( sequence->cur_pts ) {
    sequence->seq_pts = sequence->cur_pts;
    sequence->cur_pts = 0;
  }

  bits_reader_set( &sequence->br, buf, len );
#ifdef LOG
  int tmp = read_bits( &sequence->br, 10 );
  lprintf( "temporal_reference: %d\n", tmp );
#else
  skip_bits( &sequence->br, 10 );
#endif
  infos->picture_coding_type = read_bits( &sequence->br, 3 );
  lprintf( "picture_coding_type: %d\n", infos->picture_coding_type );
  infos->forward_reference = VDP_INVALID_HANDLE;
  infos->backward_reference = VDP_INVALID_HANDLE;
  skip_bits( &sequence->br, 16 );
  if ( infos->picture_coding_type > I_FRAME ) {
    infos->full_pel_forward_vector = read_bits( &sequence->br, 1 );
    infos->f_code[0][0] = infos->f_code[0][1] = read_bits( &sequence->br, 3 );
    if ( infos->picture_coding_type==B_FRAME ) {
      infos->full_pel_backward_vector = read_bits( &sequence->br, 1 );
      infos->f_code[1][0] = infos->f_code[1][1] = read_bits( &sequence->br, 3 );
    }
  }
  else {
    infos->full_pel_forward_vector = 0;
    infos->full_pel_backward_vector = 0;
  }
}



static void sequence_extension( sequence_t *sequence, uint8_t *buf, int len )
{
  bits_reader_set( &sequence->br, buf, len );
#ifdef LOG
  int tmp = read_bits( &sequence->br, 4 );
  lprintf( "extension_start_code_identifier: %d\n", tmp );
  skip_bits( &sequence->br, 1 );
#else
  skip_bits( &sequence->br, 5 );
#endif
  switch ( read_bits( &sequence->br, 3 ) ) {
    case 5: sequence->profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE; break;
    default: sequence->profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
  }
  skip_bits( &sequence->br, 4 );
  sequence->progressive_sequence = read_bits( &sequence->br, 1 );
  lprintf( "progressive_sequence: %d\n", sequence->progressive_sequence );
  if ( read_bits( &sequence->br, 2 ) == 2 )
    sequence->chroma = VO_CHROMA_422;
#ifdef LOG
  tmp = read_bits( &sequence->br, 2 );
  lprintf( "horizontal_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 2 );
  lprintf( "vertical_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 12 );
  lprintf( "bit_rate_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "marker_bit: %d\n", tmp );
  tmp = read_bits( &sequence->br, 8 );
  lprintf( "vbv_buffer_size_extension: %d\n", tmp );
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "low_delay: %d\n", tmp );
#else
  skip_bits( &sequence->br, 26 );
#endif
  sequence->frame_rate_extension_n = read_bits( &sequence->br, 2 );
  lprintf( "frame_rate_extension_n: %d\n", sequence->frame_rate_extension_n );
  sequence->frame_rate_extension_d = read_bits( &sequence->br, 5 );
  lprintf( "frame_rate_extension_d: %d\n", sequence->frame_rate_extension_d );
}



static void picture_coding_extension( sequence_t *sequence, uint8_t *buf, int len )
{
  VdpPictureInfoMPEG1Or2 *infos = &sequence->picture.vdp_infos;
  if ( infos->picture_structure && infos->picture_structure!=PICTURE_FRAME )
    infos = &sequence->picture.vdp_infos2;

  bits_reader_set( &sequence->br, buf, len );
#ifdef LOG
  int tmp = read_bits( &sequence->br, 4 );
  lprintf( "extension_start_code_identifier: %d\n", tmp );
#else
  skip_bits( &sequence->br, 4 );
#endif
  infos->f_code[0][0] = read_bits( &sequence->br, 4 );
  infos->f_code[0][1] = read_bits( &sequence->br, 4 );
  infos->f_code[1][0] = read_bits( &sequence->br, 4 );
  infos->f_code[1][1] = read_bits( &sequence->br, 4 );
  lprintf( "f_code_0_0: %d\n", infos->f_code[0][0] );
  lprintf( "f_code_0_1: %d\n", infos->f_code[0][1] );
  lprintf( "f_code_1_0: %d\n", infos->f_code[1][0] );
  lprintf( "f_code_1_1: %d\n", infos->f_code[1][1] );
  infos->intra_dc_precision = read_bits( &sequence->br, 2 );
  lprintf( "intra_dc_precision: %d\n", infos->intra_dc_precision );
  infos->picture_structure = read_bits( &sequence->br, 2 );
  lprintf( "picture_structure: %d\n", infos->picture_structure );
  infos->top_field_first = read_bits( &sequence->br, 1 );
  lprintf( "top_field_first: %d\n", infos->top_field_first );
  infos->frame_pred_frame_dct = read_bits( &sequence->br, 1 );
  lprintf( "frame_pred_frame_dct: %d\n", infos->frame_pred_frame_dct );
  infos->concealment_motion_vectors = read_bits( &sequence->br, 1 );
  lprintf( "concealment_motion_vectors: %d\n", infos->concealment_motion_vectors );
  infos->q_scale_type = read_bits( &sequence->br, 1 );
  lprintf( "q_scale_type: %d\n", infos->q_scale_type );
  infos->intra_vlc_format = read_bits( &sequence->br, 1 );
  lprintf( "intra_vlc_format: %d\n", infos->intra_vlc_format );
  infos->alternate_scan = read_bits( &sequence->br, 1 );
  lprintf( "alternate_scan: %d\n", infos->alternate_scan );
  sequence->picture.repeat_first_field = read_bits( &sequence->br, 1 );
  lprintf( "repeat_first_field: %d\n", sequence->picture.repeat_first_field );
#ifdef LOG
  tmp = read_bits( &sequence->br, 1 );
  lprintf( "chroma_420_type: %d\n", tmp );
#else
  skip_bits( &sequence->br, 1 );
#endif
  sequence->picture.progressive_frame = read_bits( &sequence->br, 1 );
  lprintf( "progressive_frame: %d\n", sequence->picture.progressive_frame );
}



static void quant_matrix_extension( sequence_t *sequence, uint8_t *buf, int len )
{
  int i, j;

  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 4 );
  i = read_bits( &sequence->br, 1 );
  lprintf( "load_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.intra_quantizer_matrix[mpeg2_scan_norm[j]] = read_bits( &sequence->br, 8 );
    }
  }
  else {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.intra_quantizer_matrix[mpeg2_scan_norm[j]] = default_intra_quantizer_matrix[j];
    }
  }

  i = read_bits( &sequence->br, 1 );
  lprintf( "load_non_intra_quantizer_matrix: %d\n", i );
  if ( i ) {
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos2.non_intra_quantizer_matrix[mpeg2_scan_norm[j]] = sequence->picture.vdp_infos.non_intra_quantizer_matrix[mpeg2_scan_norm[j]] = read_bits( &sequence->br, 8 );
    }
  }
  else {
    memset( sequence->picture.vdp_infos.non_intra_quantizer_matrix, 16, 64 );
    memset( sequence->picture.vdp_infos2.non_intra_quantizer_matrix, 16, 64 );
  }
}



static void copy_slice( sequence_t *sequence, uint8_t *buf, int len )
{
  int size = sequence->picture.slices_pos+len;
  if ( sequence->picture.slices_size < size ) {
    sequence->picture.slices_size = size+1024;
    sequence->picture.slices = realloc( sequence->picture.slices, sequence->picture.slices_size );
  }
  xine_fast_memcpy( sequence->picture.slices+sequence->picture.slices_pos, buf, len );
  sequence->picture.slices_pos += len;
  if ( sequence->picture.slices_pos_top )
    sequence->picture.slices_count2++;
  else
    sequence->picture.slices_count++;
}



static int parse_code( vdpau_mpeg12_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( !sequence->have_header && buf[3]!=sequence_header_code ) {
    lprintf( " ----------- no sequence header yet.\n" );
    return 0;
  }

  if ( (buf[3] >= begin_slice_start_code) && (buf[3] <= end_slice_start_code) ) {
    lprintf( " ----------- slice_start_code\n" );
    copy_slice( sequence, buf, len );
    return 0;
  }

  switch ( buf[3] ) {
    case sequence_header_code:
      lprintf( " ----------- sequence_header_code\n" );
      sequence_header( this_gen, buf+4, len-4 );
      break;
    case extension_start_code: {
      switch ( buf[4]>>4 ) {
        case sequence_ext_sc:
          lprintf( " ----------- sequence_extension_start_code\n" );
          sequence_extension( sequence, buf+4, len-4 );
          break;
        case quant_matrix_ext_sc:
          lprintf( " ----------- quant_matrix_extension_start_code\n" );
          quant_matrix_extension( sequence, buf+4, len-4 );
          break;
        case picture_coding_ext_sc:
          lprintf( " ----------- picture_coding_extension_start_code\n" );
          picture_coding_extension( sequence, buf+4, len-4 );
          break;
        case sequence_display_ext_sc:
          lprintf( " ----------- sequence_display_extension_start_code\n" );
          break;
      }
      break;
      }
    case user_data_start_code:
      lprintf( " ----------- user_data_start_code\n" );
      break;
    case group_start_code:
      lprintf( " ----------- group_start_code\n" );
      break;
    case picture_start_code:
      lprintf( " ----------- picture_start_code\n" );
      picture_header( this_gen, buf+4, len-4 );
      break;
    case sequence_error_code:
      lprintf( " ----------- sequence_error_code\n" );
      break;
    case sequence_end_code:
      lprintf( " ----------- sequence_end_code\n" );
      break;
  }
  return 0;
}



static void decode_render( vdpau_mpeg12_decoder_t *vd, vdpau_accel_t *accel )
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;

  pic->vdp_infos.slice_count = pic->slices_count;
  pic->vdp_infos2.slice_count = pic->slices_count2;

  VdpStatus st;
  if ( vd->decoder==VDP_INVALID_HANDLE || vd->decoder_profile!=seq->profile || vd->decoder_width!=seq->coded_width || vd->decoder_height!=seq->coded_height ) {
    if ( vd->decoder!=VDP_INVALID_HANDLE ) {
      accel->vdp_decoder_destroy( vd->decoder );
      vd->decoder = VDP_INVALID_HANDLE;
    }
    st = accel->vdp_decoder_create( accel->vdp_device, seq->profile, seq->coded_width, seq->coded_height, 2, &vd->decoder);
    if ( st!=VDP_STATUS_OK )
      lprintf( "failed to create decoder !! %s\n", accel->vdp_get_error_string( st ) );
    else {
      vd->decoder_profile = seq->profile;
      vd->decoder_width = seq->coded_width;
      vd->decoder_height = seq->coded_height;
      seq->vdp_runtime_nr = accel->vdp_runtime_nr;
    }
  }

  VdpBitstreamBuffer vbit;
  vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit.bitstream = pic->slices;
  vbit.bitstream_bytes = (pic->vdp_infos.picture_structure==PICTURE_FRAME)? pic->slices_pos : pic->slices_pos_top;
  st = accel->vdp_decoder_render( vd->decoder, accel->surface, (VdpPictureInfo*)&pic->vdp_infos, 1, &vbit );
#ifdef LOG
  if ( st!=VDP_STATUS_OK )
    lprintf( "decoder failed : %d!! %s\n", st, accel->vdp_get_error_string( st ) );
  else {
    lprintf( "DECODER SUCCESS : frame_type:%d, slices=%d, slices_bytes=%d, current=%d, forwref:%d, backref:%d, pts:%lld\n",
      pic->vdp_infos.picture_coding_type, pic->vdp_infos.slice_count, vbit.bitstream_bytes, accel->surface, pic->vdp_infos.forward_reference, pic->vdp_infos.backward_reference, seq->cur_pts );
    VdpPictureInfoMPEG1Or2 *info = &pic->vdp_infos;
    lprintf("%d %d %d %d %d %d %d %d %d %d %d %d %d\n", info->intra_dc_precision, info->frame_pred_frame_dct, info->concealment_motion_vectors,
      info->intra_vlc_format, info->alternate_scan, info->q_scale_type, info->top_field_first, info->full_pel_forward_vector,
      info->full_pel_backward_vector, info->f_code[0][0], info->f_code[0][1], info->f_code[1][0], info->f_code[1][1] );
  }
#endif

  if ( pic->vdp_infos.picture_structure != PICTURE_FRAME ) {
    pic->vdp_infos2.backward_reference = VDP_INVALID_HANDLE;
    pic->vdp_infos2.forward_reference = VDP_INVALID_HANDLE;
    if ( pic->vdp_infos2.picture_coding_type==P_FRAME ) {
      if ( pic->vdp_infos.picture_coding_type==I_FRAME )
        pic->vdp_infos2.forward_reference = accel->surface;
      else
        pic->vdp_infos2.forward_reference = pic->vdp_infos.forward_reference;
    }
    else if ( pic->vdp_infos.picture_coding_type==B_FRAME ) {
      pic->vdp_infos2.forward_reference = pic->vdp_infos.forward_reference;
      pic->vdp_infos2.backward_reference = pic->vdp_infos.backward_reference;
    }
    vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
    vbit.bitstream = pic->slices+pic->slices_pos_top;
    vbit.bitstream_bytes = pic->slices_pos-pic->slices_pos_top;
    st = accel->vdp_decoder_render( vd->decoder, accel->surface, (VdpPictureInfo*)&pic->vdp_infos2, 1, &vbit );
    if ( st!=VDP_STATUS_OK )
      lprintf( "decoder failed : %d!! %s\n", st, accel->vdp_get_error_string( st ) );
    else
      lprintf( "DECODER SUCCESS : frame_type:%d, slices=%d, current=%d, forwref:%d, backref:%d, pts:%lld\n",
        pic->vdp_infos2.picture_coding_type, pic->vdp_infos2.slice_count, accel->surface, pic->vdp_infos2.forward_reference, pic->vdp_infos2.backward_reference, seq->cur_pts );
  }
}



static void decode_picture( vdpau_mpeg12_decoder_t *vd, uint8_t end_of_sequence )
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;
  vdpau_accel_t *ref_accel;

  if ( seq->profile == VDP_DECODER_PROFILE_MPEG1 )
    pic->vdp_infos.picture_structure=PICTURE_FRAME;

  if ( pic->vdp_infos.picture_coding_type==P_FRAME ) {
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else
      return;
  }
  else if ( pic->vdp_infos.picture_coding_type==B_FRAME ) {
    if ( seq->forward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->forward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else
      return;
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.backward_reference = ref_accel->surface;
    }
    else
      return;
  }

  int still_image = (end_of_sequence) ? VO_STILL_IMAGE : 0;
  /* no sequence display extension parser yet so at least enable autoselection */
  int color_matrix = 0;
  VO_SET_FLAGS_CM (4, color_matrix);
  vo_frame_t *img = vd->stream->video_out->get_frame( vd->stream->video_out, seq->coded_width, seq->coded_height,
                                                      seq->ratio, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS | seq->chroma | seq->reset | still_image | color_matrix );
  seq->reset = 0;                                                      
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  if ( !seq->accel_vdpau )
    seq->accel_vdpau = accel;

  if( seq->vdp_runtime_nr != *(seq->accel_vdpau->current_vdp_runtime_nr) ) {
    seq->accel_vdpau = accel;
    if ( seq->forward_ref )
      seq->forward_ref->free( seq->forward_ref );
    seq->forward_ref = NULL;
    if ( seq->backward_ref )
      seq->backward_ref->free( seq->backward_ref );
    seq->backward_ref = NULL;
    vd->decoder = VDP_INVALID_HANDLE;
  }

  decode_render( vd, accel );

#ifdef MAKE_DAT
  if ( nframes==0 ) {
    fwrite( &seq->coded_width, 1, sizeof(seq->coded_width), outfile );
    fwrite( &seq->coded_height, 1, sizeof(seq->coded_height), outfile );
    fwrite( &seq->ratio, 1, sizeof(seq->ratio), outfile );
    fwrite( &seq->profile, 1, sizeof(seq->profile), outfile );
  }

  if ( nframes++ < 25 ) {
    fwrite( &pic->vdp_infos, 1, sizeof(pic->vdp_infos), outfile );
    fwrite( &pic->slices_pos, 1, sizeof(pic->slices_pos), outfile );
    fwrite( pic->slices, 1, pic->slices_pos, outfile );
  }
#endif

  img->drawn = 0;
  img->pts = seq->seq_pts;
  seq->seq_pts = 0; /* reset */
  img->bad_frame = 0;

  if ( end_of_sequence ) {
    if ( seq->backward_ref )
      seq->backward_ref->free( seq->backward_ref );
    seq->backward_ref = NULL;
  }

#if 0
  /* trying to deal with (french) buggy streams that randomly set bottom_field_first
     while stream is top_field_first. So we assume that when top_field_first
     is set one time, the stream _is_ top_field_first. */
  lprintf("pic->vdp_infos.top_field_first = %d\n", pic->vdp_infos.top_field_first);
  if ( pic->vdp_infos.top_field_first )
    seq->top_field_first = 1;
  img->top_field_first = seq->top_field_first;
#else
  img->top_field_first = pic->vdp_infos.top_field_first;
#endif

  /* progressive_frame is unreliable with most mpeg2 streams */
  if ( pic->vdp_infos.picture_structure!=PICTURE_FRAME )
    img->progressive_frame = 0;
  else
    img->progressive_frame = pic->progressive_frame;

  img->repeat_first_field = pic->repeat_first_field;

  double duration = seq->video_step;

  if ( img->repeat_first_field ) {
    if( !seq->progressive_sequence && pic->progressive_frame ) {
      /* decoder should output 3 fields, so adjust duration to
         count on this extra field time */
      duration *= 3;
      duration /= 2;
    } else if ( seq->progressive_sequence ) {
      /* for progressive sequences the output should repeat the
         frame 1 or 2 times depending on top_field_first flag. */
      duration *= (pic->vdp_infos.top_field_first ? 3 : 2);
    }
  }

  img->duration = (int)(duration + .5);

  if ( pic->vdp_infos.picture_coding_type!=B_FRAME ) {
    if ( pic->vdp_infos.picture_coding_type==I_FRAME && !seq->backward_ref ) {
      img->pts = 0;
      img->draw( img, vd->stream );
      ++img->drawn;
    }
    if ( seq->forward_ref ) {
      seq->forward_ref->drawn = 0;
      seq->forward_ref->free( seq->forward_ref );
    }
    seq->forward_ref = seq->backward_ref;
    if ( seq->forward_ref && !seq->forward_ref->drawn ) {
      seq->forward_ref->draw( seq->forward_ref, vd->stream );
    }
    seq->backward_ref = img;
  }
  else {
    img->draw( img, vd->stream );
    img->free( img );
  }
}



static void picture_ready( vdpau_mpeg12_decoder_t *vd, uint8_t end_of_sequence )
{
	picture_t *pic = (picture_t*)&vd->sequence.picture;
	if ( !pic->slices_count )
		return;
	if ( pic->vdp_infos2.picture_structure && !pic->slices_count2 )
		return;
	decode_picture( vd, end_of_sequence );
}



/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void vdpau_mpeg12_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  vdpau_mpeg12_decoder_t *this = (vdpau_mpeg12_decoder_t *) this_gen;
  sequence_t *seq = (sequence_t*)&this->sequence;

  /* preview buffers shall not be decoded and drawn -- use them only to supply stream information */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ( !buf->size )
    return;

  if ( buf->pts )
    seq->cur_pts = buf->pts;

  int size = seq->bufpos+buf->size;
  if ( seq->bufsize < size ) {
    seq->bufsize = size+1024;
    seq->buf = realloc( seq->buf, seq->bufsize );
  }
  xine_fast_memcpy( seq->buf+seq->bufpos, buf->content, buf->size );
  seq->bufpos += buf->size;

  while ( seq->bufseek <= seq->bufpos-4 ) {
    uint8_t *buffer = seq->buf+seq->bufseek;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      if ( seq->start<0 ) {
        seq->start = seq->bufseek;
      }
      else {
        parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
        uint8_t *tmp = (uint8_t*)malloc(seq->bufsize);
        xine_fast_memcpy( tmp, seq->buf+seq->bufseek, seq->bufpos-seq->bufseek );
        seq->bufpos -= seq->bufseek;
        seq->start = -1;
        seq->bufseek = -1;
        free( seq->buf );
        seq->buf = tmp;
      }
    }
    ++seq->bufseek;
  }

  /* still image detection -- don't wait for further data if buffer ends in sequence end code */
  if (seq->start >= 0 && seq->buf[seq->start + 3] == sequence_end_code) {
    decode_picture(this, 1);
	parse_code(this, seq->buf+seq->start, 4);
    seq->start = -1;
  }
}

/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_mpeg12_flush (video_decoder_t *this_gen) {

  lprintf( "vdpau_mpeg12_flush\n" );
}

/*
 * This function resets the video decoder.
 */
static void vdpau_mpeg12_reset (video_decoder_t *this_gen) {
  vdpau_mpeg12_decoder_t *this = (vdpau_mpeg12_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg12_reset\n" );
  reset_sequence( &this->sequence, 1 );
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_mpeg12_discontinuity (video_decoder_t *this_gen) {
  vdpau_mpeg12_decoder_t *this = (vdpau_mpeg12_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg12_discontinuity\n" );
  reset_sequence( &this->sequence, 0 );
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_mpeg12_dispose (video_decoder_t *this_gen) {

  vdpau_mpeg12_decoder_t *this = (vdpau_mpeg12_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg12_dispose\n" );

  if ( this->decoder!=VDP_INVALID_HANDLE && this->sequence.accel_vdpau ) {
      this->sequence.accel_vdpau->vdp_decoder_destroy( this->decoder );
      this->decoder = VDP_INVALID_HANDLE;
    }

  free_sequence( &this->sequence );

  this->stream->video_out->close( this->stream->video_out, this->stream );

  free( this->sequence.picture.slices );
  free( this->sequence.buf );
  free( this_gen );
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  vdpau_mpeg12_decoder_t  *this ;

  lprintf( "open_plugin\n" );

  /* the videoout must be vdpau-capable to support this decoder */
  if ( !(stream->video_driver->get_capabilities(stream->video_driver) & VO_CAP_VDPAU_MPEG12) )
    return NULL;

  /* now check if vdpau has free decoder resource */
  vo_frame_t *img = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS );
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  int runtime_nr = accel->vdp_runtime_nr;
  img->free(img);
  VdpDecoder decoder;
  VdpStatus st = accel->vdp_decoder_create( accel->vdp_device, VDP_DECODER_PROFILE_MPEG2_MAIN, 1920, 1080, 2, &decoder );
  if ( st!=VDP_STATUS_OK ) {
    lprintf( "can't create vdpau decoder.\n" );
    return NULL;
  }

  accel->vdp_decoder_destroy( decoder );

  this = (vdpau_mpeg12_decoder_t *) calloc(1, sizeof(vdpau_mpeg12_decoder_t));

  this->video_decoder.decode_data         = vdpau_mpeg12_decode_data;
  this->video_decoder.flush               = vdpau_mpeg12_flush;
  this->video_decoder.reset               = vdpau_mpeg12_reset;
  this->video_decoder.discontinuity       = vdpau_mpeg12_discontinuity;
  this->video_decoder.dispose             = vdpau_mpeg12_dispose;

  this->stream                            = stream;
  this->class                             = (vdpau_mpeg12_class_t *) class_gen;

  this->sequence.bufsize = 1024;
  this->sequence.buf = (uint8_t*)malloc(this->sequence.bufsize);
  this->sequence.forward_ref = 0;
  this->sequence.backward_ref = 0;
  this->sequence.vdp_runtime_nr = runtime_nr;
  free_sequence( &this->sequence );
  this->sequence.ratio = 1;
  this->sequence.reset = VO_NEW_SEQUENCE_FLAG;

  init_picture( &this->sequence.picture );

  this->decoder = VDP_INVALID_HANDLE;
  this->sequence.accel_vdpau = NULL;

  (stream->video_out->open)(stream->video_out, stream);

#ifdef MAKE_DAT
  outfile = fopen( "/tmp/mpg.dat","w");
  nframes = 0;
#endif

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  vdpau_mpeg12_class_t *this;

  this = (vdpau_mpeg12_class_t *) calloc(1, sizeof(vdpau_mpeg12_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "vdpau_mpeg12";
  this->decoder_class.description     =
	N_("vdpau_mpeg12: mpeg1/2 decoder plugin using VDPAU hardware decoding.\n"
	   "Must be used along with video_out_vdpau.");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
static const uint32_t video_types[] = {
  BUF_VIDEO_MPEG,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  8                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER, 19, "vdpau_mpeg12", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
