/*
 * Copyright (C) 2008 the xine project
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
 * vdpau_vc1.c, a vc1 video stream parser using VDPAU hardware decoder
 *
 */

/*#define LOG*/
#define LOG_MODULE "vdpau_vc1"

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

#define sequence_header_code    0x0f
#define sequence_end_code       0x0a
#define entry_point_code        0x0e
#define frame_start_code        0x0d
#define field_start_code        0x0c
#define slice_start_code        0x0b

#define PICTURE_FRAME            0
#define PICTURE_FRAME_INTERLACE  2
#define PICTURE_FIELD_INTERLACE  3

#define I_FRAME   0
#define P_FRAME   1
#define B_FRAME   3
#define BI_FRAME  4

#define FIELDS_I_I    0
#define FIELDS_I_P    1
#define FIELDS_P_I    2
#define FIELDS_P_P    3
#define FIELDS_B_B    4
#define FIELDS_B_BI   5
#define FIELDS_BI_B   6
#define FIELDS_BI_BI  7

#define MODE_STARTCODE  0
#define MODE_FRAME      1

/*#define MAKE_DAT*/ /*do NOT define this, unless you know what you do */
#ifdef MAKE_DAT
static int nframes;
static FILE *outfile;
#endif



const double aspect_ratio[] = {
  0.0,
  1.0,
  12./11.,
  10./11.,
  16./11.,
  40./33.,
  24./11.,
  20./11.,
  32./11.,
  80./33.,
  18./11.,
  15./11.,
  64./33.,
  160./99.
};



typedef struct {
  VdpPictureInfoVC1       vdp_infos;
  int                     slices;
  int                     fptype;
  int                     field;
  int                     header_size;
  int                     hrd_param_flag;
  int                     hrd_num_leaky_buckets;
  int                     repeat_first_field;
  int                     top_field_first;
  int                     skipped;
} picture_t;



typedef struct {
  uint32_t    coded_width;
  uint32_t    coded_height;

  uint64_t    video_step; /* frame duration in pts units */
  uint64_t    reported_video_step; /* frame duration in pts units */
  double      ratio;
  VdpDecoderProfile profile;

  int         mode;
  int         have_header;

  uint8_t     *buf; /* accumulate data */
  int         bufseek;
  int         start;
  int         code_start, current_code;
  uint32_t    bufsize;
  uint32_t    bufpos;

  picture_t   picture;
  vo_frame_t  *forward_ref;
  vo_frame_t  *backward_ref;

  int64_t    seq_pts;
  int64_t    cur_pts;

  vdpau_accel_t *accel_vdpau;

  bits_reader_t br;

  int         vdp_runtime_nr;

  int         color_matrix;
} sequence_t;



typedef struct {
  video_decoder_class_t   decoder_class;
} vdpau_vc1_class_t;



typedef struct vdpau_vc1_decoder_s {
  video_decoder_t         video_decoder;  /* parent video decoder structure */

  vdpau_vc1_class_t    *class;
  xine_stream_t           *stream;

  sequence_t              sequence;

  VdpDecoder              decoder;
  VdpDecoderProfile       decoder_profile;
  uint32_t                decoder_width;
  uint32_t                decoder_height;

} vdpau_vc1_decoder_t;



static void init_picture( picture_t *pic )
{
  memset( pic, 0, sizeof( picture_t ) );
}



static void reset_picture( picture_t *pic )
{
  pic->slices = 1;
}



static void reset_sequence( sequence_t *sequence )
{
  lprintf( "reset_sequence\n" );
  sequence->bufpos = 0;
  sequence->bufseek = 0;
  sequence->start = -1;
  sequence->code_start = sequence->current_code = 0;
  sequence->seq_pts = sequence->cur_pts = 0;
  if ( sequence->forward_ref )
    sequence->forward_ref->free( sequence->forward_ref );
  sequence->forward_ref = NULL;
  if ( sequence->backward_ref )
    sequence->backward_ref->free( sequence->backward_ref );
  sequence->backward_ref = NULL;
  reset_picture( &sequence->picture );
}



static void init_sequence( sequence_t *sequence )
{
  lprintf( "init_sequence\n" );
  sequence->have_header = 0;
  sequence->profile = VDP_DECODER_PROFILE_VC1_SIMPLE;
  sequence->ratio = 0;
  sequence->video_step = 0;
  sequence->picture.hrd_param_flag = 0;
  reset_sequence( sequence );
}



static void update_metadata( vdpau_vc1_decoder_t *this_gen )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( !sequence->have_header ) {
    sequence->have_header = 1;
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_WIDTH, sequence->coded_width );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, sequence->coded_height );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*sequence->ratio) );
    _x_stream_info_set( this_gen->stream, XINE_STREAM_INFO_FRAME_DURATION, (sequence->reported_video_step = sequence->video_step) );
    _x_meta_info_set_utf8( this_gen->stream, XINE_META_INFO_VIDEOCODEC, "VC1/WMV9 (vdpau)" );
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
}



static void sequence_header_advanced( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  lprintf( "sequence_header_advanced\n" );
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  int xine_color_matrix = 4; /* undefined, mpeg range */

  if ( len < 5 )
    return;

  sequence->profile = VDP_DECODER_PROFILE_VC1_ADVANCED;
  lprintf("VDP_DECODER_PROFILE_VC1_ADVANCED\n");
  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 15 );
  sequence->picture.vdp_infos.postprocflag = read_bits( &sequence->br, 1 );
  sequence->coded_width = (read_bits( &sequence->br, 12 )+1)<<1;
  sequence->coded_height = (read_bits( &sequence->br, 12 )+1)<<1;
  sequence->picture.vdp_infos.pulldown = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.interlace = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.tfcntrflag = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.finterpflag = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.psf = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.maxbframes = 7;
  if ( read_bits( &sequence->br, 1 ) ) {
    double w, h;
    int ar=0;
    w = read_bits( &sequence->br, 14 )+1;
    h = read_bits( &sequence->br, 14 )+1;
    if ( read_bits( &sequence->br, 1 ) ) {
      ar = read_bits( &sequence->br, 4 );
    }
    if ( ar==15 ) {
      w = read_bits( &sequence->br, 8 );
      h = read_bits( &sequence->br, 8 );
      sequence->ratio = w/h;
      lprintf("aspect_ratio (w/h) = %f\n", sequence->ratio);
    }
    else if ( ar && ar<14 ) {
      sequence->ratio = sequence->coded_width*aspect_ratio[ar]/sequence->coded_height;
      lprintf("aspect_ratio = %f\n", sequence->ratio);
    }

    if ( read_bits( &sequence->br, 1 ) ) {
      if ( read_bits( &sequence->br, 1 ) ) {
#ifdef LOG
        int exp = read_bits( &sequence->br, 16 );
        lprintf("framerate exp = %d\n", exp);
#else
        skip_bits( &sequence->br, 16 );
#endif
      }
      else {
        double nr = read_bits( &sequence->br, 8 );
        switch ((int)nr) {
          case 1: nr = 24000; break;
          case 2: nr = 25000; break;
          case 3: nr = 30000; break;
          case 4: nr = 50000; break;
          case 5: nr = 60000; break;
          default: nr = 0;
        }
        double dr = read_bits( &sequence->br, 4 );
        switch ((int)dr) {
          case 2: dr = 1001; break;
          default: dr = 1000;
        }
        sequence->video_step = 90000/(nr/dr);
        lprintf("framerate = %f video_step = %d\n", nr/dr, sequence->video_step);
      }
    }
    if ( read_bits( &sequence->br, 1 ) ) {
      skip_bits( &sequence->br, 16 );
      xine_color_matrix = read_bits (&sequence->br, 8) << 1; /* VC1 is always mpeg range?? */
    }
  }
  VO_SET_FLAGS_CM (xine_color_matrix, sequence->color_matrix);
  sequence->picture.hrd_param_flag = read_bits( &sequence->br, 1 );
  if ( sequence->picture.hrd_param_flag )
    sequence->picture.hrd_num_leaky_buckets = read_bits( &sequence->br, 5 );

  update_metadata( this_gen );
}



static void sequence_header( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  lprintf( "sequence_header\n" );
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( len < 4 )
    return;

  bits_reader_set( &sequence->br, buf, len );
  switch ( read_bits( &sequence->br, 2 ) ) {
    case 0: sequence->profile = VDP_DECODER_PROFILE_VC1_SIMPLE; lprintf("VDP_DECODER_PROFILE_VC1_SIMPLE\n"); break;
    case 1: sequence->profile = VDP_DECODER_PROFILE_VC1_MAIN; lprintf("VDP_DECODER_PROFILE_VC1_MAIN\n"); break;
    case 2: sequence->profile = VDP_DECODER_PROFILE_VC1_MAIN; fprintf(stderr, "vc1_complex profile not supported by vdpau, forcing vc1_main, expect corruption!.\n"); break;
    case 3: return sequence_header_advanced( this_gen, buf, len ); break;
    default: return; /* illegal value, broken header? */
  }
  skip_bits( &sequence->br, 10 );
  sequence->picture.vdp_infos.loopfilter = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.multires = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.fastuvmc = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.extended_mv = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.dquant = read_bits( &sequence->br, 2 );
  sequence->picture.vdp_infos.vstransform = read_bits( &sequence->br, 1 );
  skip_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.overlap = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.syncmarker = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.rangered = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.maxbframes = read_bits( &sequence->br, 3 );
  sequence->picture.vdp_infos.quantizer = read_bits( &sequence->br, 2 );
  sequence->picture.vdp_infos.finterpflag = read_bits( &sequence->br, 1 );

  VO_SET_FLAGS_CM (4, sequence->color_matrix);

  update_metadata( this_gen );
}



static void entry_point( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  lprintf( "entry_point\n" );
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 2 );
  sequence->picture.vdp_infos.panscan_flag = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.refdist_flag = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.loopfilter = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.fastuvmc = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.extended_mv = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.dquant = read_bits( &sequence->br, 2 );
  sequence->picture.vdp_infos.vstransform = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.overlap = read_bits( &sequence->br, 1 );
  sequence->picture.vdp_infos.quantizer = read_bits( &sequence->br, 2 );

  if ( sequence->picture.hrd_param_flag ) {
    int i;
    for ( i=0; i<sequence->picture.hrd_num_leaky_buckets; ++i )
      skip_bits( &sequence->br, 8 );
  }

  if ( read_bits( &sequence->br, 1 ) ) {
    sequence->coded_width = (read_bits( &sequence->br, 12 )+1)<<1;
    sequence->coded_height = (read_bits( &sequence->br, 12 )+1)<<1;
  }

  if ( sequence->picture.vdp_infos.extended_mv )
    sequence->picture.vdp_infos.extended_dmv = read_bits( &sequence->br, 1 );

  sequence->picture.vdp_infos.range_mapy_flag = read_bits( &sequence->br, 1 );
  if ( sequence->picture.vdp_infos.range_mapy_flag ) {
    sequence->picture.vdp_infos.range_mapy = read_bits( &sequence->br, 3 );
  }
  sequence->picture.vdp_infos.range_mapuv_flag = read_bits( &sequence->br, 1 );
  if ( sequence->picture.vdp_infos.range_mapuv_flag ) {
    sequence->picture.vdp_infos.range_mapuv = read_bits( &sequence->br, 3 );
  }
}



static void picture_header( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  //picture_t *pic = (picture_t*)&sequence->picture;
  VdpPictureInfoVC1 *info = &(sequence->picture.vdp_infos);
  int tmp;

  lprintf("picture_header\n");

  bits_reader_set( &sequence->br, buf, len );
  skip_bits( &sequence->br, 2 );

  if ( info->finterpflag )
    skip_bits( &sequence->br, 1 );
  if ( info->rangered ) {
    /*info->rangered &= ~2;
    info->rangered |= get_bits( buf,off++,1 ) << 1;*/
    info->rangered = (read_bits( &sequence->br, 1 ) << 1) +1;
  }
  if ( !info->maxbframes ) {
    if ( read_bits( &sequence->br, 1 ) )
      info->picture_type = P_FRAME;
    else
      info->picture_type = I_FRAME;
  }
  else {
    if ( read_bits( &sequence->br, 1 ) )
      info->picture_type = P_FRAME;
    else {
      if ( read_bits( &sequence->br, 1 ) )
        info->picture_type = I_FRAME;
      else
        info->picture_type = B_FRAME;
    }
  }
  if ( info->picture_type == B_FRAME ) {
    tmp = read_bits( &sequence->br, 3 );
    if ( tmp==7 ) {
      tmp = (tmp<<4) | read_bits( &sequence->br, 4 );
      if ( tmp==127 )
        info->picture_type = BI_FRAME;
    }
  }
}



static void picture_header_advanced( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  picture_t *pic = (picture_t*)&sequence->picture;
  VdpPictureInfoVC1 *info = &(sequence->picture.vdp_infos);

  lprintf("picture_header_advanced\n");

  bits_reader_set( &sequence->br, buf, len );

  if ( info->interlace ) {
    lprintf("frame->interlace=1\n");
    if ( !read_bits( &sequence->br, 1 ) ) {
      lprintf("progressive frame\n");
      info->frame_coding_mode = PICTURE_FRAME;
    }
    else {
      if ( !read_bits( &sequence->br, 1 ) ) {
        lprintf("frame interlaced\n");
        info->frame_coding_mode = PICTURE_FRAME_INTERLACE;
      }
      else {
        lprintf("field interlaced\n");
        info->frame_coding_mode = PICTURE_FIELD_INTERLACE;
      }
    }
  }
  if ( info->interlace && info->frame_coding_mode == PICTURE_FIELD_INTERLACE ) {
    pic->fptype = read_bits( &sequence->br, 3 );
    switch ( pic->fptype ) {
      case FIELDS_I_I:
      case FIELDS_I_P:
        info->picture_type = I_FRAME; break;
      case FIELDS_P_I:
      case FIELDS_P_P:
        info->picture_type = P_FRAME; break;
      case FIELDS_B_B:
      case FIELDS_B_BI:
        info->picture_type = B_FRAME; break;
      default:
        info->picture_type = BI_FRAME;
    }
  }
  else {
    if ( !read_bits( &sequence->br, 1 ) )
      info->picture_type = P_FRAME;
    else {
      if ( !read_bits( &sequence->br, 1 ) )
        info->picture_type = B_FRAME;
      else {
        if ( !read_bits( &sequence->br, 1 ) )
          info->picture_type = I_FRAME;
        else {
          if ( !read_bits( &sequence->br, 1 ) )
            info->picture_type = BI_FRAME;
          else {
            info->picture_type = P_FRAME;
            pic->skipped = 1;
          }
        }
      }
    }
  }
  if ( info->tfcntrflag ) {
    lprintf("tfcntrflag=1\n");
    skip_bits( &sequence->br, 8 );
  }
  if ( info->pulldown && info->interlace ) {
    pic->top_field_first = read_bits( &sequence->br, 1 );
    pic->repeat_first_field = read_bits( &sequence->br, 1 );
  }
}



static void parse_header( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;
  int off=0;

  while ( off < (len-4) ) {
    uint8_t *buffer = buf+off;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      switch ( buffer[3] ) {
        case sequence_header_code: sequence_header( this_gen, buf+off+4, len-off-4 ); break;
        case entry_point_code: entry_point( this_gen, buf+off+4, len-off-4 ); break;
      }
    }
    ++off;
  }
  if ( !sequence->have_header )
    sequence_header( this_gen, buf, len );
}



static void remove_emulation_prevention( uint8_t *src, uint8_t *dst, int src_len, int *dst_len )
{
  int i;
  int len = 0;
  int removed = 0;

  for ( i=0; i<src_len-3; ++i ) {
    if ( src[i]==0 && src[i+1]==0 && src[i+2]==3 ) {
      lprintf("removed emulation prevention byte\n");
      dst[len++] = src[i];
      dst[len++] = src[i+1];
      i += 2;
      ++removed;
    }
    else {
      memcpy( dst+len, src+i, 4 );
      ++len;
    }
  }
  for ( ; i<src_len; ++i )
    dst[len++] = src[i];
  *dst_len = src_len-removed;
}



static int parse_code( vdpau_vc1_decoder_t *this_gen, uint8_t *buf, int len )
{
  sequence_t *sequence = (sequence_t*)&this_gen->sequence;

  if ( !sequence->have_header && buf[3]!=sequence_header_code )
    return 0;

  if ( sequence->code_start == frame_start_code ) {
    if ( sequence->current_code==field_start_code || sequence->current_code==slice_start_code ) {
	  sequence->picture.slices++;
      return -1;
	}
    return 1; /* frame complete, decode */
  }

  switch ( buf[3] ) {
    int dst_len;
    uint8_t *tmp;
    case sequence_header_code:
      lprintf("sequence_header_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      sequence_header( this_gen, tmp+4, dst_len-4 );
      free( tmp );
      break;
    case entry_point_code:
      lprintf("entry_point_code\n");
      tmp = malloc( len );
      remove_emulation_prevention( buf, tmp, len, &dst_len );
      entry_point( this_gen, tmp+4, dst_len-4 );
      free( tmp );
      break;
    case sequence_end_code:
      lprintf("sequence_end_code\n");
      break;
    case frame_start_code:
      lprintf("frame_start_code, len=%d\n", len);
      break;
    case field_start_code:
      lprintf("field_start_code\n");
      break;
    case slice_start_code:
      lprintf("slice_start_code, len=%d\n", len);
      break;
  }
  return 0;
}



static void decode_render( vdpau_vc1_decoder_t *vd, vdpau_accel_t *accel, uint8_t *buf, int len )
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
      fprintf(stderr, "vdpau_vc1: failed to create decoder !! %s\n", accel->vdp_get_error_string( st ) );
    else {
      lprintf( "decoder created.\n" );
      vd->decoder_profile = seq->profile;
      vd->decoder_width = seq->coded_width;
      vd->decoder_height = seq->coded_height;
      seq->vdp_runtime_nr = accel->vdp_runtime_nr;
    }
  }

  VdpBitstreamBuffer vbit;
  vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit.bitstream = buf;
  vbit.bitstream_bytes = len;
  if ( pic->field )
    vbit.bitstream_bytes = pic->field;
  st = accel->vdp_decoder_render( vd->decoder, accel->surface, (VdpPictureInfo*)&pic->vdp_infos, 1, &vbit );
  if ( st!=VDP_STATUS_OK )
    fprintf(stderr, "vdpau_vc1: decoder failed : %d!! %s\n", st, accel->vdp_get_error_string( st ) );
  else {
    lprintf( "DECODER SUCCESS : slices=%d, slices_bytes=%d, current=%d, forwref:%d, backref:%d, pts:%lld\n",
              pic->vdp_infos.slice_count, vbit.bitstream_bytes, accel->surface, pic->vdp_infos.forward_reference, pic->vdp_infos.backward_reference, seq->seq_pts );
  }
#ifdef LOG
  VdpPictureInfoVC1 *info = &(seq->picture.vdp_infos);
  lprintf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", info->slice_count, info->picture_type, info->frame_coding_mode,
           info->postprocflag, info->pulldown, info->interlace, info->tfcntrflag, info->finterpflag, info->psf, info->dquant, info->panscan_flag, info->refdist_flag,
           info->quantizer, info->extended_mv, info->extended_dmv, info->overlap, info->vstransform, info->loopfilter, info->fastuvmc, info->range_mapy_flag, info->range_mapy,
           info->range_mapuv_flag, info->range_mapuv, info->multires, info->syncmarker, info->rangered, info->maxbframes, info->deblockEnable, info->pquant );
#endif

  if ( pic->field ) {
    int old_type = pic->vdp_infos.picture_type;
    switch ( pic->fptype ) {
      case FIELDS_I_I:
      case FIELDS_P_I:
        pic->vdp_infos.picture_type = I_FRAME;
        pic->vdp_infos.backward_reference = VDP_INVALID_HANDLE;
        pic->vdp_infos.forward_reference = VDP_INVALID_HANDLE;
        break;
      case FIELDS_I_P:
        pic->vdp_infos.forward_reference = accel->surface;
        pic->vdp_infos.picture_type = P_FRAME;
        break;
      case FIELDS_P_P:
        if ( seq->backward_ref )
          pic->vdp_infos.forward_reference = ((vdpau_accel_t*)seq->backward_ref->accel_data)->surface;
        pic->vdp_infos.picture_type = P_FRAME;
        break;
      case FIELDS_B_B:
      case FIELDS_BI_B:
        pic->vdp_infos.picture_type = B_FRAME;
        break;
      default:
        pic->vdp_infos.picture_type = BI_FRAME;
    }
    vbit.bitstream = buf+pic->field+4;
    vbit.bitstream_bytes = len-pic->field-4;
    st = accel->vdp_decoder_render( vd->decoder, accel->surface, (VdpPictureInfo*)&pic->vdp_infos, 1, &vbit );
    if ( st!=VDP_STATUS_OK )
      fprintf(stderr, "vdpau_vc1: decoder failed : %d!! %s\n", st, accel->vdp_get_error_string( st ) );
    else {
      lprintf( "DECODER SUCCESS (second field): slices=%d, slices_bytes=%d, current=%d, forwref:%d, backref:%d, pts:%lld\n",
                pic->vdp_infos.slice_count, vbit.bitstream_bytes, accel->surface, pic->vdp_infos.forward_reference, pic->vdp_infos.backward_reference, seq->seq_pts );
    }
#ifdef LOG
    VdpPictureInfoVC1 *info = &(seq->picture.vdp_infos);
    lprintf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", info->slice_count, info->picture_type, info->frame_coding_mode,
             info->postprocflag, info->pulldown, info->interlace, info->tfcntrflag, info->finterpflag, info->psf, info->dquant, info->panscan_flag, info->refdist_flag,
             info->quantizer, info->extended_mv, info->extended_dmv, info->overlap, info->vstransform, info->loopfilter, info->fastuvmc, info->range_mapy_flag, info->range_mapy,
             info->range_mapuv_flag, info->range_mapuv, info->multires, info->syncmarker, info->rangered, info->maxbframes, info->deblockEnable, info->pquant );
#endif

    pic->vdp_infos.picture_type = old_type;
  }
}



static int search_field( vdpau_vc1_decoder_t *vd, uint8_t *buf, int len )
{
  int i;
  lprintf("search_fields, len=%d\n", len);
  for ( i=0; i<len-4; ++i ) {
    if ( buf[i]==0 && buf[i+1]==0 && buf[i+2]==1 && buf[i+3]==field_start_code ) {
      lprintf("found field_start_code at %d\n", i);
      return i;
    }
  }
  return 0;
}



static void decode_picture( vdpau_vc1_decoder_t *vd )
{
  sequence_t *seq = (sequence_t*)&vd->sequence;
  picture_t *pic = (picture_t*)&seq->picture;
  vdpau_accel_t *ref_accel;
  int field;

  uint8_t *buf;
  int len;

  pic->skipped = 0;
  pic->field = 0;

  if ( seq->mode == MODE_FRAME ) {
    buf = seq->buf;
    len = seq->bufpos;
    if ( seq->profile==VDP_DECODER_PROFILE_VC1_ADVANCED )
      picture_header_advanced( vd, buf, len );
    else
      picture_header( vd, buf, len );

    if ( len < 2 )
      pic->skipped = 1;
  }
  else {
    seq->picture.vdp_infos.slice_count = seq->picture.slices;
    buf = seq->buf+seq->start+4;
    len = seq->bufseek-seq->start-4;
    if ( seq->profile==VDP_DECODER_PROFILE_VC1_ADVANCED ) {
      int tmplen = (len>50) ? 50 : len;
      uint8_t *tmp = malloc( tmplen );
      remove_emulation_prevention( buf, tmp, tmplen, &tmplen );
      picture_header_advanced( vd, tmp, tmplen );
      free( tmp );
    }
    else
      picture_header( vd, buf, len );

    if ( len < 2 )
      pic->skipped = 1;
  }

  if ( pic->skipped )
    pic->vdp_infos.picture_type = P_FRAME;

  if ( pic->vdp_infos.interlace && pic->vdp_infos.frame_coding_mode == PICTURE_FIELD_INTERLACE ) {
    if ( !(field = search_field( vd, buf, len )) )
      lprintf("error, no fields found!\n");
    else
      pic->field = field;
  }

  pic->vdp_infos.forward_reference = VDP_INVALID_HANDLE;
  pic->vdp_infos.backward_reference = VDP_INVALID_HANDLE;

  if ( pic->vdp_infos.picture_type==P_FRAME ) {
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else {
      reset_picture( &seq->picture );
      return;
    }
  }
  else if ( pic->vdp_infos.picture_type>=B_FRAME ) {
    if ( seq->forward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->forward_ref->accel_data;
      pic->vdp_infos.forward_reference = ref_accel->surface;
    }
    else {
      reset_picture( &seq->picture );
      return;
    }
    if ( seq->backward_ref ) {
      ref_accel = (vdpau_accel_t*)seq->backward_ref->accel_data;
      pic->vdp_infos.backward_reference = ref_accel->surface;
    }
    else {
      reset_picture( &seq->picture );
      return;
    } 
  }

  vo_frame_t *img = vd->stream->video_out->get_frame( vd->stream->video_out, seq->coded_width, seq->coded_height,
    seq->ratio, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS | seq->color_matrix );
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

  if ( pic->vdp_infos.interlace && pic->vdp_infos.frame_coding_mode ) {
    img->progressive_frame = 0;
    img->top_field_first = pic->top_field_first;
  }
  else {
    img->progressive_frame = 1;
    img->top_field_first = 1;
  }
  img->pts = seq->seq_pts;
  img->bad_frame = 0;
  img->duration = seq->video_step;

  if ( pic->vdp_infos.picture_type<B_FRAME ) {
    if ( pic->vdp_infos.picture_type==I_FRAME && !seq->backward_ref ) {
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

  seq->seq_pts +=seq->video_step;

  reset_picture( &seq->picture );
}



/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void vdpau_vc1_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  vdpau_vc1_decoder_t *this = (vdpau_vc1_decoder_t *) this_gen;
  sequence_t *seq = (sequence_t*)&this->sequence;

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    lprintf("BUF_FLAG_PREVIEW\n");
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    lprintf("BUF_FLAG_FRAMERATE=%d\n", buf->decoder_info[0]);
    if ( buf->decoder_info[0] > 0 ) {
      this->sequence.video_step = buf->decoder_info[0];
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->sequence.video_step);
    }
  }
  
  if (this->sequence.reported_video_step != this->sequence.video_step){
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->sequence.reported_video_step = this->sequence.video_step));
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    lprintf("BUF_FLAG_HEADER\n");
  }

  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    lprintf("BUF_FLAG_ASPECT\n");
    seq->ratio = (double)buf->decoder_info[1]/(double)buf->decoder_info[2];
    lprintf("arx=%d ary=%d ratio=%f\n", buf->decoder_info[1], buf->decoder_info[2], seq->ratio);
  }

  if ( !buf->size )
    return;

  seq->cur_pts = buf->pts;

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    lprintf("BUF_FLAG_STDHEADER\n");
    xine_bmiheader *bih = (xine_bmiheader *) buf->content;
    int bs = sizeof( xine_bmiheader );
    seq->coded_width = bih->biWidth;
    seq->coded_height = bih->biHeight;
    lprintf( "width=%d height=%d\n", bih->biWidth, bih->biHeight );
    if ( buf->size > bs ) {
      seq->mode = MODE_FRAME;
      parse_header( this, buf->content+bs, buf->size-bs );
    }
    return;
  }

  int size = seq->bufpos+buf->size;
  if ( seq->bufsize < size ) {
    seq->bufsize = size+10000;
    seq->buf = realloc( seq->buf, seq->bufsize );
    lprintf("sequence buffer realloced = %d\n", seq->bufsize );
  }
  xine_fast_memcpy( seq->buf+seq->bufpos, buf->content, buf->size );
  seq->bufpos += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    lprintf("BUF_FLAG_FRAME_START\n");
    seq->seq_pts = buf->pts;
    seq->mode = MODE_FRAME;
    if ( seq->bufpos > 3 ) {
      if ( seq->buf[0]==0 && seq->buf[1]==0 && seq->buf[2]==1 ) {
        seq->mode = MODE_STARTCODE;
      }
    }
  }

  if ( seq->mode == MODE_FRAME ) {
    if ( buf->decoder_flags & BUF_FLAG_FRAME_END ) {
      lprintf("BUF_FLAG_FRAME_END\n");
      decode_picture( this );
      seq->bufpos = 0;
    }
    return;
  }

  int res;
  while ( seq->bufseek <= seq->bufpos-4 ) {
    uint8_t *buffer = seq->buf+seq->bufseek;
    if ( buffer[0]==0 && buffer[1]==0 && buffer[2]==1 ) {
      seq->current_code = buffer[3];
      lprintf("current_code = %d\n", seq->current_code);
      if ( seq->start<0 ) {
        seq->start = seq->bufseek;
        seq->code_start = buffer[3];
        lprintf("code_start = %d\n", seq->code_start);
        if ( seq->cur_pts )
          seq->seq_pts = seq->cur_pts;
      }
      else {
        res = parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
        if ( res==1 ) {
          seq->mode = MODE_STARTCODE;
          decode_picture( this );
          parse_code( this, seq->buf+seq->start, seq->bufseek-seq->start );
        }
        if ( res!=-1 ) {
          uint8_t *tmp = (uint8_t*)malloc(seq->bufsize);
          xine_fast_memcpy( tmp, seq->buf+seq->bufseek, seq->bufpos-seq->bufseek );
          seq->bufpos -= seq->bufseek;
          seq->start = -1;
          seq->bufseek = -1;
          free( seq->buf );
          seq->buf = tmp;
        }
      }
    }
    ++seq->bufseek;
  }
}



/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_vc1_flush (video_decoder_t *this_gen) {

  lprintf( "vdpau_vc1_flush\n" );
}

/*
 * This function resets the video decoder.
 */
static void vdpau_vc1_reset (video_decoder_t *this_gen) {
  vdpau_vc1_decoder_t *this = (vdpau_vc1_decoder_t *) this_gen;

  lprintf( "vdpau_vc1_reset\n" );
  reset_sequence( &this->sequence );
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_vc1_discontinuity (video_decoder_t *this_gen) {

  lprintf( "vdpau_vc1_discontinuity\n" );
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_vc1_dispose (video_decoder_t *this_gen) {

  vdpau_vc1_decoder_t *this = (vdpau_vc1_decoder_t *) this_gen;

  lprintf( "vdpau_vc1_dispose\n" );

  if ( this->decoder!=VDP_INVALID_HANDLE && this->sequence.accel_vdpau ) {
      this->sequence.accel_vdpau->vdp_decoder_destroy( this->decoder );
      this->decoder = VDP_INVALID_HANDLE;
    }

  reset_sequence( &this->sequence );

  this->stream->video_out->close( this->stream->video_out, this->stream );

  free( this->sequence.buf );
  free( this_gen );
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  vdpau_vc1_decoder_t  *this ;

  lprintf( "open_plugin\n" );

  /* the videoout must be vdpau-capable to support this decoder */
  if ( !(stream->video_driver->get_capabilities(stream->video_driver) & VO_CAP_VDPAU_VC1) )
    return NULL;

  /* now check if vdpau has free decoder resource */
  vo_frame_t *img = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS );
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  int runtime_nr = accel->vdp_runtime_nr;
  img->free(img);
  VdpDecoder decoder;
  VdpStatus st = accel->vdp_decoder_create( accel->vdp_device, VDP_DECODER_PROFILE_VC1_MAIN, 1920, 1080, 2, &decoder );
  if ( st!=VDP_STATUS_OK ) {
    lprintf( "can't create vdpau decoder.\n" );
    return NULL;
  }

  accel->vdp_decoder_destroy( decoder );

  this = (vdpau_vc1_decoder_t *) calloc(1, sizeof(vdpau_vc1_decoder_t));

  this->video_decoder.decode_data         = vdpau_vc1_decode_data;
  this->video_decoder.flush               = vdpau_vc1_flush;
  this->video_decoder.reset               = vdpau_vc1_reset;
  this->video_decoder.discontinuity       = vdpau_vc1_discontinuity;
  this->video_decoder.dispose             = vdpau_vc1_dispose;

  this->stream                            = stream;
  this->class                             = (vdpau_vc1_class_t *) class_gen;

  this->sequence.bufsize = 10000;
  this->sequence.buf = (uint8_t*)malloc(this->sequence.bufsize);
  this->sequence.forward_ref = 0;
  this->sequence.backward_ref = 0;
  this->sequence.vdp_runtime_nr = runtime_nr;
  init_sequence( &this->sequence );

  init_picture( &this->sequence.picture );

  this->decoder = VDP_INVALID_HANDLE;
  this->sequence.accel_vdpau = NULL;
  this->sequence.mode = MODE_STARTCODE;

  (stream->video_out->open)(stream->video_out, stream);

#ifdef MAKE_DAT
  outfile = fopen( "/tmp/vc1.dat","w");
  nframes = 0;
#endif

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  vdpau_vc1_class_t *this;

  this = (vdpau_vc1_class_t *) calloc(1, sizeof(vdpau_vc1_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "vdpau_vc1";
  this->decoder_class.description     =
	N_("vdpau_vc1: vc1 decoder plugin using VDPAU hardware decoding.\n"
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
  BUF_VIDEO_VC1, BUF_VIDEO_WMV9,
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
  { PLUGIN_VIDEO_DECODER, 19, "vdpau_vc1", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
