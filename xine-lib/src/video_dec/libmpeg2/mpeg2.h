/*
 * mpeg2.h
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Structure for the mpeg2dec decoder */

#ifndef MPEG2_H
#define MPEG2_H

#include "libmpeg2_accel.h"

typedef struct mpeg2dec_s {
    xine_video_port_t * output;
    uint32_t frame_format;

    /* this is where we keep the state of the decoder */
    struct picture_s * picture;
    void *picture_base;
    
    uint32_t shift;
    int new_sequence;
    int is_sequence_needed;
    int is_wait_for_ip_frames;
    int frames_to_drop, drop_frame;
    int in_slice;
    int seek_mode, is_frame_needed;

    /* the maximum chunk size is determined by vbv_buffer_size */
    /* which is 224K for MP@ML streams. */
    /* (we make no pretenses of decoding anything more than that) */
    /* allocated in init - gcc has problems allocating such big structures */
    uint8_t * chunk_buffer;
    void *chunk_base;
    /* pointer to current position in chunk_buffer */
    uint8_t * chunk_ptr;
    /* last start code ? */
    uint8_t code;
    uint32_t chunk_size;

    int64_t pts;
    uint32_t rff_pattern; 
    int force_aspect;
    int force_pan_scan;

    /* AFD data can be found after a sequence, group or picture start code */
    /* and will be stored in afd_value_seen. Later it will be transfered to */
    /* a stream property and stored into afd_value_reported to detect changes */
    int afd_value_seen;
    int afd_value_reported;

    xine_stream_t *stream;
    
    /* a spu decoder for possible closed captions */
    spu_decoder_t *cc_dec;
    mpeg2dec_accel_t accel;

} mpeg2dec_t ;


/* initialize mpegdec with a opaque user pointer */
void mpeg2_init (mpeg2dec_t * mpeg2dec, 
		 xine_video_port_t * output);

/* destroy everything which was allocated, shutdown the output */
void mpeg2_close (mpeg2dec_t * mpeg2dec);

int mpeg2_decode_data (mpeg2dec_t * mpeg2dec,
		       uint8_t * data_start, uint8_t * data_end, 
		       uint64_t pts);

void mpeg2_find_sequence_header (mpeg2dec_t * mpeg2dec,
				 uint8_t * data_start, uint8_t * data_end);

void mpeg2_flush (mpeg2dec_t * mpeg2dec);
void mpeg2_reset (mpeg2dec_t * mpeg2dec);
void mpeg2_discontinuity (mpeg2dec_t * mpeg2dec);

/* Not needed, it is defined as static in decode.c, and no-one else called it
 * currently
 */
/* void process_userdata(mpeg2dec_t *mpeg2dec, uint8_t *buffer); */

#endif
