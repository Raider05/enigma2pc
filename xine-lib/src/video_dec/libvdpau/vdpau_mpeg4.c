/*
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 *
 * Copyright (C) 2010-2013 the xine project
 * Copyright (C) 2010 Christophe Thommeret <hftom@free.fr>
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
 * vdpau_mpeg4.c, a mpeg4-part-2 video stream parser using VDPAU hardware decoder
 *
 */

/*#define LOG*/
#define LOG_MODULE "vdpau_mpeg4"


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

#define begin_vo_start_code         0x00
#define end_vo_start_code           0x1f
#define begin_vol_start_code        0x20
#define end_vol_start_code          0x2f
#define viso_sequence_start_code    0xb0
#define viso_sequence_end_code      0xb1
#define viso_start_code             0xb5
#define group_start_code            0xb3
#define user_data_start_code        0xb2
#define vop_start_code              0xb6

#define I_FRAME   0
#define P_FRAME   1
#define B_FRAME   2

#define PICTURE_TOP     1
#define PICTURE_BOTTOM  2
#define PICTURE_FRAME   3

#define SHAPE_RECT    0
#define SHAPE_BIN     1
#define SHAPE_BINONLY 2
#define SHAPE_GRAY    3

#define SPRITE_STATIC 1
#define SPRITE_GMC    2

static int nframe;

/*#define MAKE_DAT*/ /*do NOT define this, unless you know what you do */
#ifdef MAKE_DAT
static int nframes;
static FILE *outfile;
#endif



/* default intra quant matrix, in zig-zag order */
static const uint8_t default_intra_quantizer_matrix[64] = {
    8,
    17, 17,
    20, 18, 18,
    19, 19, 21, 21,
    22, 22, 22, 21, 21,
    23, 23, 23, 23, 23, 23,
    25, 24, 24, 24, 24, 25, 25,
    27, 27, 26, 26, 26, 26, 26, 27,
    28, 28, 28, 28, 28, 28, 28,
    30, 30, 30, 30, 30, 30,
    32, 32, 32, 32, 32,
    35, 35, 35, 35,
    38, 38, 38,
    41, 41,
    45
};

/* default non intra quant matrix, in zig-zag order */
static const uint8_t default_non_intra_quantizer_matrix[64] = {
    16,
    17, 17,
    18, 18, 18,
    19, 19, 19, 19,
    20, 20, 20, 20, 20,
    21, 21, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22,
    23, 23, 23, 23, 23, 23, 23, 23,
    24, 24, 24, 25, 24, 24, 24,
    25, 26, 26, 26, 26, 25,
    27, 27, 27, 27, 27,
    28, 28, 28, 28,
    30, 30, 30,
    31, 31,
    33
};

uint8_t mpeg_scan_norm[64] = {
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
  VdpPictureInfoMPEG4Part2  vdp_infos; /* first field, also used for frame */

  int                     viso_verid;
  int                     newpred_enable;
  int                     reduced_resolution_vop_enable;
  int                     vol_shape;
  int                     complexity_estimation_disable;
  int                     sprite_enable;
  int                     quant_precision;

  int                     progressive_frame;
} picture_t;



typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  uint64_t    video_step; /* frame duration in pts units */
  double      ratio;
  VdpDecoderProfile profile;
  int         chroma;
  int         top_field_first;

  int         have_header;

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

  int         have_codec_name;
  char        codec_name[256];

  int         fixed_vop_time_increment;
  int         time_increment_bits;
  int         last_time_base;
  int         time_base;
  int         time;
  int         last_non_b_time;
  int         t_frame;

  int         color_matrix;

} sequence_t;



typedef struct {
  video_decoder_class_t   decoder_class;
} vdpau_mpeg4_class_t;



typedef struct vdpau_mpeg4_decoder_s {
  video_decoder_t         video_decoder;  /* parent video decoder structure */

  vdpau_mpeg4_class_t    *class;
  xine_stream_t           *stream;

  sequence_t              sequence;

  VdpDecoder              decoder;
  VdpDecoderProfile       decoder_profile;
  uint32_t                decoder_width;
  uint32_t                decoder_height;

} vdpau_mpeg4_decoder_t;



static void reset_picture( picture_t *pic )
{
  lprintf( "reset_picture\n" );
  pic->vdp_infos.vop_coding_type = 0;
  pic->vdp_infos.alternate_vertical_scan_flag = 0;
  pic->vdp_infos.quant_type = 0;
  pic->vdp_infos.vop_time_increment_resolution = 0;
  pic->vdp_infos.vop_fcode_forward = 1;
  pic->vdp_infos.vop_fcode_backward = 1;
  pic->vdp_infos.resync_marker_disable = 0;
  pic->vdp_infos.interlaced = 0;
  pic->vdp_infos.quarter_sample = 0;
  pic->vdp_infos.short_video_header = 0;
  pic->vdp_infos.rounding_control = 0;
  pic->vdp_infos.top_field_first = 1;
  pic->progressive_frame = 1;
  pic->viso_verid = 1;
  pic->newpred_enable = 0;
  pic->reduced_resolution_vop_enable = 0;
  pic->complexity_estimation_disable = 1;
  pic->vol_shape = SHAPE_RECT;
  pic->quant_precision = 5;
  pic->vdp_infos.trd[0] = pic->vdp_infos.trd[1] = 0;
  pic->vdp_infos.trb[0] = pic->vdp_infos.trb[1] = 0;
}



static void init_picture( picture_t *pic )
{
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

  sequence->last_time_base = 0;
  sequence->time_base = 0;
  sequence->time = 0;
  sequence->last_non_b_time = 0;
  sequence->t_frame = 0;
}



static void free_sequence( sequence_t *sequence )
{
  lprintf( "init_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = VDP_DECODER_PROFILE_MPEG4_PART2_ASP;
  sequence->chroma = 0;
  sequence->video_step = 3600;
  sequence->have_codec_name = 0;
  strcpy( sequence->codec_name, "MPEG4 / XviD / DivX (vdpau)" );
  reset_sequence( sequence, 1 );
}



static void update_metadata( vdpau_mpeg4_decoder_t *this_gen )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  
  _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_WIDTH, sequence->coded_width );
  _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, sequence->coded_height );
  _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*sequence->ratio) );
  _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_FRAME_DURATION, sequence->video_step );
  _x_meta_info_set_utf8( this_gen->stream, XINE_META_INFO_VIDEOCODEC, sequence->codec_name );
  xine_event_t event;
  xine_format_change_data_t data;
  event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
  event.stream = this_gen->stream;
  event.data = &data;
  event.data_length = sizeof(data);
  data.width = sequence->coded_width;
  data.height = sequence->coded_height;

  if (fabs(sequence->ratio-1.0)<0.1)
    data.aspect = XINE_VO_ASPECT_SQUARE;
  else if (fabs(sequence->ratio-1.33)<0.1)
    data.aspect = XINE_VO_ASPECT_4_3;
  else if (fabs(sequence->ratio-1.77)<0.1)
    data.aspect = XINE_VO_ASPECT_ANAMORPHIC;
  else if (fabs(sequence->ratio-2.11)<0.1)
    data.aspect = XINE_VO_ASPECT_DVB;
  else
    data.aspect = XINE_VO_ASPECT_AUTO;

  xine_event_send( this_gen->stream, &event );
}



static void visual_object( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *picture = (picture_t*)&sequence->picture;
  int xine_color_matrix = 4; /* undefined, mpeg range */
  bits_reader_set( &sequence->br, buf, len );

  if ( read_bits( &sequence->br, 1 ) ) {
    picture->viso_verid = read_bits( &sequence->br, 4 );
    lprintf("visual_object_verid: %d\n", picture->viso_verid);
    skip_bits( &sequence->br, 3 );
  }
  if ( read_bits( &sequence->br, 4 ) == 1 ) {
    if ( read_bits( &sequence->br, 1 ) ) {
      skip_bits (&sequence->br, 3); /* video_format */
      xine_color_matrix |= read_bits (&sequence->br, 1); /* full range */
      if ( read_bits( &sequence->br, 1 ) ) {
        skip_bits (&sequence->br, 16);
        xine_color_matrix = (xine_color_matrix & 1)
          | (read_bits (&sequence->br, 8) << 1);  /* matrix_coefficients */
      }
    }
  }
  VO_SET_FLAGS_CM (xine_color_matrix, sequence->color_matrix);
}



static void video_object_layer( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *picture = (picture_t*)&sequence->picture;
  bits_reader_set( &sequence->br, buf, len );

  int vol_verid = 1;

  picture->vdp_infos.short_video_header = 0;
  sequence->t_frame = 0;

  skip_bits( &sequence->br, 9 );
  if ( read_bits( &sequence->br, 1 ) ) {
    vol_verid = read_bits( &sequence->br, 4 );
    lprintf("video_object_layer_verid: %d\n", vol_verid);
    skip_bits( &sequence->br, 3 );
  }
  double parw=1, parh=1;
  int ar = read_bits( &sequence->br, 4 );
  lprintf("aspect_ratio_info: %d\n", ar);
  switch ( ar ) {
    case 1: parw = parh = 1; break;
    case 2: parw = 12; parh = 11; break;
    case 3: parw = 10; parh = 11; break;
    case 4: parw = 16; parh = 11; break;
    case 5: parw = 40; parh = 33; break;
    case 15: {
      parw = read_bits( &sequence->br, 8 );
      parh = read_bits( &sequence->br, 8 );
      break;
    }
  }
  lprintf("parw: %f, parh: %f\n", parw, parh);
  if ( read_bits( &sequence->br, 1 ) ) {
    skip_bits( &sequence->br, 3 );
    if ( read_bits( &sequence->br, 1 ) ) {
      read_bits( &sequence->br, 16 );
      read_bits( &sequence->br, 16 );
      read_bits( &sequence->br, 16 );
      read_bits( &sequence->br, 15 );
      read_bits( &sequence->br, 16 );
    }
  }

  picture->vol_shape = read_bits( &sequence->br, 2 );
  if ( (picture->vol_shape == SHAPE_GRAY) && (vol_verid != 1) ) {
    skip_bits( &sequence->br, 4 );
    fprintf(stderr, "vdpau_mpeg4: unsupported SHAPE_GRAY!\n");
  }
  skip_bits( &sequence->br, 1 );
  picture->vdp_infos.vop_time_increment_resolution = read_bits( &sequence->br, 16 );
  lprintf("vop_time_increment_resolution: %d\n", picture->vdp_infos.vop_time_increment_resolution);
  int length=1, max=2;
  while ( (max - 1) < picture->vdp_infos.vop_time_increment_resolution ) {
    ++length;
    max *= 2;
  }
  sequence->time_increment_bits = length;
  if ( sequence->time_increment_bits < 1 )
    sequence->time_increment_bits = 1;
  skip_bits( &sequence->br, 1 );

  if ( read_bits( &sequence->br, 1 ) ) {
    sequence->fixed_vop_time_increment = read_bits( &sequence->br, sequence->time_increment_bits );
  }
  else
    sequence->fixed_vop_time_increment = 1;

  sequence->video_step = 90000 / (picture->vdp_infos.vop_time_increment_resolution / sequence->fixed_vop_time_increment);
  lprintf("fixed_vop_time_increment: %d\n", sequence->fixed_vop_time_increment);
  lprintf("video_step: %d\n", (int)sequence->video_step);

  if ( picture->vol_shape != SHAPE_BINONLY ) {
    if ( picture->vol_shape == SHAPE_RECT ) {
      skip_bits( &sequence->br, 1 );
      sequence->coded_width = read_bits( &sequence->br, 13 );
      lprintf("vol_width: %d\n", sequence->coded_width);
      skip_bits( &sequence->br, 1 );
      sequence->coded_height = read_bits( &sequence->br, 13 );
      lprintf("vol_height: %d\n", sequence->coded_height);
      skip_bits( &sequence->br, 1 );
    }
    sequence->ratio = ((double)sequence->coded_width * parw) / ((double)sequence->coded_height * parh);
    lprintf("aspect_ratio: %f\n", sequence->ratio);
    picture->vdp_infos.interlaced = read_bits( &sequence->br, 1 );
    skip_bits( &sequence->br, 1 );

    picture->sprite_enable = 0;
    if ( vol_verid == 1 )
      picture->sprite_enable = read_bits( &sequence->br, 1 );
    else
      picture->sprite_enable = read_bits( &sequence->br, 2 );

    if ( (picture->sprite_enable == SPRITE_STATIC) || (picture->sprite_enable == SPRITE_GMC) ) {
      if ( picture->sprite_enable != SPRITE_GMC ) {
        skip_bits( &sequence->br, 14 );
        skip_bits( &sequence->br, 14 );
        skip_bits( &sequence->br, 14 );
        skip_bits( &sequence->br, 14 );
      }
      skip_bits( &sequence->br, 9 );
      if ( picture->sprite_enable != SPRITE_GMC )
        skip_bits( &sequence->br, 1 );
    }
    if ( (vol_verid != 1) && (picture->vol_shape != SHAPE_RECT) )
      skip_bits( &sequence->br, 1 );

    if ( read_bits( &sequence->br, 1 ) ) {
      picture->quant_precision = read_bits( &sequence->br, 4 );
      skip_bits( &sequence->br, 4 );
    }
    else
      picture->quant_precision = 5;

    if ( picture->vol_shape == SHAPE_GRAY )
      skip_bits( &sequence->br, 3 );

    picture->vdp_infos.quant_type = read_bits( &sequence->br, 1 );

    /* load default matrices */
    int j;
    for ( j=0; j<64; ++j ) {
      sequence->picture.vdp_infos.intra_quantizer_matrix[mpeg_scan_norm[j]] = default_intra_quantizer_matrix[j];
      sequence->picture.vdp_infos.non_intra_quantizer_matrix[mpeg_scan_norm[j]] = default_non_intra_quantizer_matrix[j];
    }
    if ( picture->vdp_infos.quant_type ) {
      int val, last = 0;
      if ( read_bits( &sequence->br, 1 ) ) { /* load_intra_quant_matrix */
        lprintf("load_intra_quant_matrix\n");
        for ( j=0; j<64; ++j ) {
          val = read_bits( &sequence->br, 8 );
          if ( !val )
            break;
          last = sequence->picture.vdp_infos.intra_quantizer_matrix[j] = val;
        }
        for ( ; j<64; ++j )
          sequence->picture.vdp_infos.intra_quantizer_matrix[j] = last;
      }
      if ( read_bits( &sequence->br, 1 ) ) { /* load_non_intra_quant_matrix */
        lprintf("load_non_intra_quant_matrix\n");
        for ( j=0; j<64; ++j ) {
          val = read_bits( &sequence->br, 8 );
          if ( !val )
            break;
          last = sequence->picture.vdp_infos.non_intra_quantizer_matrix[j] = val;
        }
        for ( ; j<64; ++j )
          sequence->picture.vdp_infos.non_intra_quantizer_matrix[j] = last;
      }
      if ( picture->vol_shape == SHAPE_GRAY ) { /* FIXME */
        fprintf(stderr, "vdpau_mpeg4: grayscale shape not supported!\n");
        return;
      }
    }
    if ( vol_verid != 1 )
      sequence->picture.vdp_infos.quarter_sample = read_bits( &sequence->br, 1 );
    else
      sequence->picture.vdp_infos.quarter_sample = 0;

    picture->complexity_estimation_disable = read_bits( &sequence->br, 1 );
    if ( !picture->complexity_estimation_disable ) { /* define_vop_complexity_estimation_header */
      int estimation_method = read_bits( &sequence->br, 2 );
      if ( (estimation_method == 0) || (estimation_method == 1) ){
        if ( !read_bits( &sequence->br, 1 ) )
          skip_bits( &sequence->br, 6 );
        if ( !read_bits( &sequence->br, 1 ) )
          skip_bits( &sequence->br, 4 );
        skip_bits( &sequence->br, 1 );
        if ( !read_bits( &sequence->br, 1 ) )
          skip_bits( &sequence->br, 4 );
        if ( !read_bits( &sequence->br, 1 ) )
          skip_bits( &sequence->br, 6 );
        skip_bits( &sequence->br, 1 );
        if ( estimation_method == 1 ) {
          if ( !read_bits( &sequence->br, 1 ) )
            skip_bits( &sequence->br, 2 );
        }
      }
    }

    picture->vdp_infos.resync_marker_disable = read_bits( &sequence->br, 1 );

    if ( read_bits( &sequence->br, 1 ) )
      skip_bits( &sequence->br, 1 );
    if ( vol_verid != 1 ) {
      picture->newpred_enable = read_bits( &sequence->br, 1 );
      if ( picture->newpred_enable )
        skip_bits( &sequence->br, 3 );
      picture->reduced_resolution_vop_enable = read_bits( &sequence->br, 1 );
    }
    else {
      picture->newpred_enable = 0;
      picture->reduced_resolution_vop_enable = 0;
    }
    /* .... */
  }
  else {
    if ( vol_verid != 1 ) {
      if ( read_bits( &sequence->br, 1 ) )
        skip_bits( &sequence->br, 24 );
    }
    picture->vdp_infos.resync_marker_disable = read_bits( &sequence->br, 1 );
  }

  if ( !sequence->have_header ) {
    update_metadata( this_gen );
    sequence->have_header = 1;
  }
}


#define ROUNDED_DIV(a,b) (((a)>0 ? (a) + ((b)>>1) : (a) - ((b)>>1))/(b))

static void video_object_plane( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *picture = (picture_t*)&sequence->picture;
  bits_reader_set( &sequence->br, buf, len );
  int time_inc=0, time_increment;

  sequence->seq_pts = sequence->cur_pts;
  sequence->cur_pts = 0;

  picture->vdp_infos.vop_coding_type = read_bits( &sequence->br, 2 );
  while ( read_bits( &sequence->br, 1 ) )
    ++time_inc;

  skip_bits( &sequence->br, 1 );

  if ( sequence->time_increment_bits == 0 || !(get_bits( &sequence->br, sequence->time_increment_bits + 1) & 1) ) {
    for ( sequence->time_increment_bits = 1; sequence->time_increment_bits < 16; ++sequence->time_increment_bits ) {
      if ( picture->vdp_infos.vop_coding_type == P_FRAME ) {
        if ( (get_bits( &sequence->br, sequence->time_increment_bits + 6 ) & 0x37) == 0x30 )
          break;
      }
      else {
        if ( (get_bits( &sequence->br, sequence->time_increment_bits + 5 ) & 0x1f) == 0x18 )
          break;
      }
      fprintf(stderr, "Headers are not complete, guessing time_increment_bits: %d\n", sequence->time_increment_bits);
    }
  }

  time_increment = read_bits( &sequence->br, sequence->time_increment_bits );

  if ( picture->vdp_infos.vop_coding_type != B_FRAME ) {
    sequence->last_time_base = sequence->time_base;
    sequence->time_base += time_inc;
    sequence->time = sequence->time_base * picture->vdp_infos.vop_time_increment_resolution + time_increment;
    if ( sequence->time < sequence->last_non_b_time ) {
      ++sequence->time_base;
      sequence->time += picture->vdp_infos.vop_time_increment_resolution;
    }
    picture->vdp_infos.trd[0] = sequence->time - sequence->last_non_b_time;
    sequence->last_non_b_time = sequence->time;
  }
  else {
    sequence->time = (sequence->last_time_base + time_inc) * picture->vdp_infos.vop_time_increment_resolution + time_increment;
    picture->vdp_infos.trb[0] = picture->vdp_infos.trd[0] - (sequence->last_non_b_time - sequence->time);
    if ( (picture->vdp_infos.trd[0] <= picture->vdp_infos.trb[0] ) || (picture->vdp_infos.trd[0] <= (picture->vdp_infos.trd[0] - picture->vdp_infos.trb[0])) || (picture->vdp_infos.trd[0] <= 0) ) {
      /* FIXME */
    }
    if ( sequence->t_frame == 0 )
      sequence->t_frame = picture->vdp_infos.trb[0];
    if ( sequence->t_frame == 0 )
      sequence->t_frame = 1;
    picture->vdp_infos.trd[1] = (  ROUNDED_DIV(sequence->last_non_b_time, sequence->t_frame) - ROUNDED_DIV(sequence->last_non_b_time - picture->vdp_infos.trd[0], sequence->t_frame));
    picture->vdp_infos.trb[1] = (  ROUNDED_DIV(sequence->time, sequence->t_frame) - ROUNDED_DIV(sequence->last_non_b_time - picture->vdp_infos.trd[0], sequence->t_frame));
    if ( picture->vdp_infos.interlaced ) {
      /* FIXME */
    }
  }

  /*if ( sequence->fixed_vop_time_increment )
    sequence->seq_pts = ( sequence->time + sequence->fixed_vop_time_increment/2 ) / sequence->fixed_vop_time_increment;*/
  
  skip_bits( &sequence->br, 1 );
  if ( !read_bits( &sequence->br, 1 ) )
    return; /* vop_coded == 0 */

  if ( picture->newpred_enable ) { /* FIXME */
    fprintf(stderr, "vdpau_mpeg4: newpred_enable, dunno what to do !!!\n");
    return;
  }

  if ( (picture->vol_shape != SHAPE_BINONLY) && (picture->vdp_infos.vop_coding_type == P_FRAME) )
    picture->vdp_infos.rounding_control = read_bits( &sequence->br, 1 );
  else
    picture->vdp_infos.rounding_control = 0;

  if ( picture->reduced_resolution_vop_enable && (picture->vol_shape == SHAPE_RECT) && (picture->vdp_infos.vop_coding_type != B_FRAME) )
    skip_bits( &sequence->br, 1 );
  if ( picture->vol_shape != SHAPE_RECT ) { /* FIXME */
    fprintf(stderr, "vdpau_mpeg4: vol_shape != SHAPE_RECT, return\n");
    return;
  }

  if ( picture->vol_shape != SHAPE_BINONLY ) {
    if ( !picture->complexity_estimation_disable ) { /* FIXME */
      fprintf(stderr, "vdpau_mpeg4: TODO: read_vop_complexity_estimation_header\n");
      return;
    }
  }

  if ( picture->vol_shape != SHAPE_BINONLY ) {
    skip_bits( &sequence->br, 3 );
    if ( picture->vdp_infos.interlaced ) {
      picture->vdp_infos.top_field_first = read_bits( &sequence->br, 1 );
      picture->vdp_infos.alternate_vertical_scan_flag = read_bits( &sequence->br, 1 );
    }
  }

  if ( picture->vol_shape != SHAPE_BINONLY ) {
    skip_bits( &sequence->br, picture->quant_precision );
    if ( picture->vol_shape == SHAPE_GRAY ) { /* FIXME */
      fprintf(stderr, "vdpau_mpeg4: unsupported SHAPE_GRAY!\n");
      return;
    }
    if ( picture->vdp_infos.vop_coding_type != I_FRAME )
      picture->vdp_infos.vop_fcode_forward = read_bits( &sequence->br, 3 );
    if ( picture->vdp_infos.vop_coding_type == B_FRAME )
      picture->vdp_infos.vop_fcode_backward = read_bits( &sequence->br, 3 );
  }
}



static void gop_header( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buf, int len )
{
  int h, m, s;

  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  bits_reader_set( &sequence->br, buf, len );

  h = read_bits( &sequence->br, 5 );
  m = read_bits( &sequence->br, 6 );
  skip_bits( &sequence->br, 1 );
  s = read_bits( &sequence->br, 6 );

  sequence->time_base = s + (60 * (m + (60 * h)));
}



static void user_data( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buffer, int len )
{
  /* code from ffmpeg's mpeg4videodec.c */

  char buf[256];
  int i, e, ver = 0, build = 0, ver2 = 0, ver3 = 0;
  char last;

  if ( this_gen->sequence.have_codec_name )
    return;

  for( i=0; i<255 && i<len; i++ ) {
    if ( buffer[i] == 0 )
      break;
    buf[i]= buffer[i];
  }
  buf[i]=0;

  /* divx detection */
  e = sscanf(buf, "DivX%dBuild%d%c", &ver, &build, &last);
  if ( e < 2 )
    e=sscanf(buf, "DivX%db%d%c", &ver, &build, &last);
  if ( e >= 2 ) {
    strcpy( this_gen->sequence.codec_name, "MPEG4 / DivX " );
    sprintf( buf, "%d", ver );
    strcat( this_gen->sequence.codec_name, " (vdpau)" );
    this_gen->sequence.have_codec_name = 1;
  }

  /* ffmpeg detection */
  e = sscanf(buf, "FFmpe%*[^b]b%d", &build) + 3;
  if ( e != 4 )
    e=sscanf(buf, "FFmpeg v%d.%d.%d / libavcodec build: %d", &ver, &ver2, &ver3, &build);
  if ( e != 4 ) {
    e=sscanf(buf, "Lavc%d.%d.%d", &ver, &ver2, &ver3)+1;
    if ( e > 1 )
      build= (ver<<16) + (ver2<<8) + ver3;
  }
  if ( e == 4 ) {
    strcpy( this_gen->sequence.codec_name, "MPEG4 / FFmpeg " );
    sprintf( buf, "%d", build );
    strcat( this_gen->sequence.codec_name, " (vdpau)" );
    this_gen->sequence.have_codec_name = 1;
  }
  else {
    if(strcmp(buf, "ffmpeg")==0) {
      strcpy( this_gen->sequence.codec_name, "MPEG4 / FFmpeg " );
      strcpy( this_gen->sequence.codec_name, "4600" );
      strcat( this_gen->sequence.codec_name, " (vdpau)" );
      this_gen->sequence.have_codec_name = 1;
    }
  }

  /* Xvid detection */
  e = sscanf(buf, "XviD%d", &build);
  if ( e == 1 ) {
    strcpy( this_gen->sequence.codec_name, "MPEG4 / XviD " );
    sprintf( buf, "%d", build );
    strcat( this_gen->sequence.codec_name, " (vdpau)" );
    this_gen->sequence.have_codec_name = 1;
  }

  update_metadata( this_gen );
}



static int parse_code( vdpau_mpeg4_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( (buf[3] >= begin_vo_start_code) && (buf[3] <= end_vo_start_code) ) {
    lprintf( " ----------- vo_start_code\n" );
    return 0;
  }

  if ( (buf[3] >= begin_vol_start_code) && (buf[3] <= end_vol_start_code) ) {
    lprintf( " ----------- vol_start_code\n" );
    video_object_layer( this_gen, buf+4, len-4);
    return 0;
  }

  switch ( buf[3] ) {
    case viso_sequence_start_code:
      lprintf( " ----------- viso_sequence_start_code\n" );
      break;
    case viso_sequence_end_code:
      lprintf( " ----------- viso_sequence_end_code\n" );
      break;
    case viso_start_code:
      lprintf( " ----------- viso_start_code\n" );
      visual_object( this_gen, buf+4, len-4 );
      break;
  }

  if ( !sequence->have_header )
    return 0;

  switch ( buf[3] ) {
    case group_start_code:
      lprintf( " ----------- group_start_code\n" );
      gop_header( this_gen, buf+4, len-4 );
      break;
    case user_data_start_code:
      lprintf( " ----------- user_data_start_code\n" );
      user_data( this_gen, buf+4, len-4 );
      break;
    case vop_start_code:
      lprintf( " ----------- vop_start_code\n" );
      video_object_plane( this_gen, buf+4, len-4 );
      return 1;
      break;
  }
  return 0;
}



static void decode_render( vdpau_mpeg4_decoder_t *vd, vdpau_accel_t *accel, uint8_t *buf, int len )
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;

  VdpStatus st;
  if ( vd->decoder==VDP_INVALID_HANDLE || vd->decoder_profile!=seq->profile || vd->decoder_width!=seq->coded_width || vd->decoder_height!=seq->coded_height ) {
    if ( vd->decoder!=VDP_INVALID_HANDLE ) {
      accel->vdp_decoder_destroy( vd->decoder );
      vd->decoder = VDP_INVALID_HANDLE;
    }
    st = accel->vdp_decoder_create( accel->vdp_device, seq->profile, seq->coded_width, seq->coded_height, 2, &vd->decoder);
    if ( st!=VDP_STATUS_OK )
      fprintf(stderr, "vdpau_mpeg4: failed to create decoder !! %s\n", accel->vdp_get_error_string( st ) );
    else {
      lprintf( "decoder created.\n" );
      vd->decoder_profile = seq->profile;
      vd->decoder_width = seq->coded_width;
      vd->decoder_height = seq->coded_height;
      seq->vdp_runtime_nr = accel->vdp_runtime_nr;
    }
  }

  VdpPictureInfoMPEG4Part2 *infos = (VdpPictureInfoMPEG4Part2*)&pic->vdp_infos;
  printf("%d: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", ++nframe, infos->vop_coding_type,infos->vop_time_increment_resolution, infos->vop_fcode_forward, infos->vop_fcode_backward, infos->resync_marker_disable, infos->interlaced, infos->quant_type, infos->quarter_sample, infos->short_video_header, infos->rounding_control, infos->alternate_vertical_scan_flag, len, infos->trd[0], infos->trd[1], infos->trb[0], infos->trb[1]);

  VdpBitstreamBuffer vbit;
  vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit.bitstream = buf;
  vbit.bitstream_bytes = len;
  st = accel->vdp_decoder_render( vd->decoder, accel->surface, (VdpPictureInfo*)&pic->vdp_infos, 1, &vbit );
  if ( st!=VDP_STATUS_OK )
    fprintf(stderr, "vdpau_mpeg4: decoder failed : %d!! %s\n", st, accel->vdp_get_error_string( st ) );
  else {
    lprintf( "DECODER SUCCESS : vop_coding_type=%d, bytes=%d, current=%d, forwref:%d, backref:%d, pts:%lld\n",
              pic->vdp_infos.vop_coding_type, vbit.bitstream_bytes, accel->surface, pic->vdp_infos.forward_reference, pic->vdp_infos.backward_reference, seq->seq_pts );
  }
}



static void decode_picture( vdpau_mpeg4_decoder_t *vd )
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;
  vdpau_accel_t *ref_accel;

  uint8_t *buf = seq->buf;
  int len = seq->bufpos;

  pic->vdp_infos.forward_reference = VDP_INVALID_HANDLE;
  pic->vdp_infos.backward_reference = VDP_INVALID_HANDLE;

  if ( pic->vdp_infos.vop_coding_type == P_FRAME ) {
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else {
      /* reset_picture( &seq->picture ); */
      return;
    }
  }
  else if ( pic->vdp_infos.vop_coding_type == B_FRAME ) {
    if ( seq->forward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->forward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else {
      /* reset_picture( &seq->picture ); */
      return;
    }
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.backward_reference = ref_accel->surface;
    }
    else {
      /* reset_picture( &seq->picture );*/
      return;
    }
  }

  vo_frame_t *img = vd->stream->video_out->get_frame( vd->stream->video_out, seq->coded_width, seq->coded_height, seq->ratio, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS | seq->color_matrix );
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

  decode_render( vd, accel, buf, len );


#ifdef MAKE_DAT
  if ( nframes==0 ) {
    fwrite( &seq->coded_width, 1, sizeof(seq->coded_width), outfile );
    fwrite( &seq->coded_height, 1, sizeof(seq->coded_height), outfile );
    fwrite( &seq->ratio, 1, sizeof(seq->ratio), outfile );
    fwrite( &seq->profile, 1, sizeof(seq->profile), outfile );
  }

  if ( nframes++ < 25 ) {
    fwrite( &pic->vdp_infos, 1, sizeof(pic->vdp_infos), outfile );
    fwrite( &len, 1, sizeof(len), outfile );
    fwrite( buf, 1, len, outfile );
    printf( "picture_type = %d\n", pic->vdp_infos.picture_type);
  }
#endif

  if ( pic->vdp_infos.interlaced ) {
    img->progressive_frame = 0;
    img->top_field_first = pic->vdp_infos.top_field_first;
  }
  else {
    img->progressive_frame = -1; /* set to -1 to let the vo know that it MUST NOT deinterlace */
    img->top_field_first = 1;
  }
  img->pts = seq->seq_pts;
  img->bad_frame = 0;
  if ( seq->video_step > 900 ) /* some buggy streams */
    img->duration = seq->video_step;

  if ( pic->vdp_infos.vop_coding_type < B_FRAME ) {
    if ( pic->vdp_infos.vop_coding_type == I_FRAME && !seq->backward_ref ) {
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




/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void vdpau_mpeg4_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  vdpau_mpeg4_decoder_t *this = (vdpau_mpeg4_decoder_t *) this_gen;
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
        if ( parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start ) ) {
          decode_picture( this );
        }
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
}

/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_mpeg4_flush (video_decoder_t *this_gen) {

  lprintf( "vdpau_mpeg4_flush\n" );
}

/*
 * This function resets the video decoder.
 */
static void vdpau_mpeg4_reset (video_decoder_t *this_gen) {
  vdpau_mpeg4_decoder_t *this = (vdpau_mpeg4_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg4_reset\n" );
  reset_sequence( &this->sequence, 1 );
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_mpeg4_discontinuity (video_decoder_t *this_gen) {
  vdpau_mpeg4_decoder_t *this = (vdpau_mpeg4_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg4_discontinuity\n" );
  reset_sequence( &this->sequence, 0 );
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_mpeg4_dispose (video_decoder_t *this_gen) {

  vdpau_mpeg4_decoder_t *this = (vdpau_mpeg4_decoder_t *) this_gen;

  lprintf( "vdpau_mpeg4_dispose\n" );

  if ( this->decoder!=VDP_INVALID_HANDLE && this->sequence.accel_vdpau ) {
      this->sequence.accel_vdpau->vdp_decoder_destroy( this->decoder );
      this->decoder = VDP_INVALID_HANDLE;
    }

  free_sequence( &this->sequence );

  this->stream->video_out->close( this->stream->video_out, this->stream );

  free( this->sequence.buf );
  free( this_gen );
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  vdpau_mpeg4_decoder_t  *this ;

  lprintf( "open_plugin\n" );

  /* the videoout must be vdpau-capable to support this decoder */
  if ( !(stream->video_driver->get_capabilities(stream->video_driver) & VO_CAP_VDPAU_MPEG4) )
    return NULL;

  /* now check if vdpau has free decoder resource */
  vo_frame_t *img = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS );
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  int runtime_nr = accel->vdp_runtime_nr;
  img->free(img);
  VdpDecoder decoder;
  VdpStatus st = accel->vdp_decoder_create( accel->vdp_device, VDP_DECODER_PROFILE_MPEG4_PART2_ASP, 1920, 1080, 2, &decoder );
  if ( st!=VDP_STATUS_OK ) {
    lprintf( "can't create vdpau decoder.\n" );
    return NULL;
  }

  accel->vdp_decoder_destroy( decoder );

  this = (vdpau_mpeg4_decoder_t *) calloc(1, sizeof(vdpau_mpeg4_decoder_t));

  this->video_decoder.decode_data         = vdpau_mpeg4_decode_data;
  this->video_decoder.flush               = vdpau_mpeg4_flush;
  this->video_decoder.reset               = vdpau_mpeg4_reset;
  this->video_decoder.discontinuity       = vdpau_mpeg4_discontinuity;
  this->video_decoder.dispose             = vdpau_mpeg4_dispose;

  this->stream                            = stream;
  this->class                             = (vdpau_mpeg4_class_t *) class_gen;

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
  outfile = fopen( "/tmp/mpeg4.dat","w");
  nframes = 0;
#endif
  nframe = 0;

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  vdpau_mpeg4_class_t *this;

  this = (vdpau_mpeg4_class_t *) calloc(1, sizeof(vdpau_mpeg4_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "vdpau_mpeg4";
  this->decoder_class.description     =
	N_("vdpau_mpeg4: mpeg4 part 2 decoder plugin using VDPAU hardware decoding.\n"
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
  BUF_VIDEO_MPEG4,
  BUF_VIDEO_XVID,
  BUF_VIDEO_DIVX5,
  BUF_VIDEO_3IVX,
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
  0                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER, 19, "vdpau_mpeg4", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
