/*
 * header.c
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/*
#define LOG_PAN_SCAN
*/

#include "config.h"

#include <stdio.h>  /* For printf debugging */
#include <inttypes.h>

#include "mpeg2_internal.h"
#include <xine/attributes.h>

/* default intra quant matrix, in zig-zag order */
static const uint8_t default_intra_quantizer_matrix[64] ATTR_ALIGN(16) = {
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

uint8_t mpeg2_scan_norm[64] ATTR_ALIGN(16) =
{
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

uint8_t mpeg2_scan_alt[64] ATTR_ALIGN(16) =
{
    /* Alternate scan pattern */
    0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,
    41,33,26,18,3,11,4,12,19,27,34,42,50,58,35,43,
    51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,
    53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63
};

/* count must be between 1 and 32 */
static uint32_t get_bits(uint8_t *buffer, uint32_t count, uint32_t *bit_position) {
  uint32_t byte_offset;
  uint32_t bit_offset;
  uint32_t bit_mask;
  uint32_t bit_bite;
  uint32_t result=0;
  if (count == 0) return 0; 
  do {
    byte_offset = *bit_position >> 3;  /* Div 8 */
    bit_offset = 8 - (*bit_position & 0x7); /* Bits got 87654321 */
    bit_mask = ((1 << (bit_offset)) - 1);
    bit_bite = bit_offset;
    if (count < bit_offset) {
      bit_mask ^=  ((1 << (bit_offset-count)) - 1);
      bit_bite = count;
    }
    /*
    printf("Byte=0x%02x Bitmask=0x%04x byte_offset=%u bit_offset=%u bit_byte=%u count=%u\n",buffer[byte_offset], bit_mask, byte_offset, bit_offset, bit_bite,count);
    */
    result = (result << bit_bite) | ((buffer[byte_offset] & bit_mask) >> (bit_offset-bit_bite));
    *bit_position+=bit_bite;
    count-=bit_bite;
  } while ((count > 0) && (byte_offset<50) ); 
  return result;
}

static int32_t get_bits_signed(uint8_t *buffer, uint32_t count, uint32_t *bit_position) {
  uint32_t value = get_bits(buffer, count, bit_position);
  uint32_t sign_mask = (uint32_t)(-1 << (count - 1));
  if (value & sign_mask)
    value |= sign_mask; /* sign-extend value */
  return (int32_t)value;
}

void mpeg2_header_state_init (picture_t * picture)
{
    picture->scan = mpeg2_scan_norm;
    picture->load_intra_quantizer_matrix = 1;
    picture->load_non_intra_quantizer_matrix = 1;
}

int mpeg2_header_sequence (picture_t * picture, uint8_t * buffer)
{
    int width, height;
    int i;

    if ((buffer[6] & 0x20) != 0x20)
	return 1;	/* missing marker_bit */

    height = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];

    picture->display_width = width = (height >> 12);
    picture->display_height = height = (height & 0xfff);
    
    width = (width + 15) & ~15;
    height = (height + 15) & ~15;

    if ((width > 1920) || (height > 1152))
	return 1;	/* size restrictions for MP@HL */

    picture->coded_picture_width = width;
    picture->coded_picture_height = height;

    /* this is not used by the decoder */
    picture->aspect_ratio_information = buffer[3] >> 4;
    picture->frame_rate_code = buffer[3] & 15;
    picture->bitrate = (buffer[4]<<10)|(buffer[5]<<2)|(buffer[6]>>6);

    if (buffer[7] & 2) {
	for (i = 0; i < 64; i++)
	    picture->intra_quantizer_matrix[mpeg2_scan_norm[i]] =
		(buffer[i+7] << 7) | (buffer[i+8] >> 1);
	buffer += 64;
    } else
	for (i = 0; i < 64; i++)
	    picture->intra_quantizer_matrix[mpeg2_scan_norm[i]] =
		default_intra_quantizer_matrix [i];

    if (buffer[7] & 1)
	for (i = 0; i < 64; i++)
	    picture->non_intra_quantizer_matrix[mpeg2_scan_norm[i]] =
		buffer[i+8];
    else
	for (i = 0; i < 64; i++)
	    picture->non_intra_quantizer_matrix[i] = 16;
    picture->load_intra_quantizer_matrix = 1;
    picture->load_non_intra_quantizer_matrix = 1;
    /* MPEG1 - for testing only */
    picture->mpeg1 = 1;
    picture->intra_dc_precision = 0;
    picture->frame_pred_frame_dct = 1;
    picture->q_scale_type = 0;
    picture->concealment_motion_vectors = 0;
    /* picture->alternate_scan = 0; */
    picture->picture_structure = FRAME_PICTURE;
    /* picture->second_field = 0; */

    return 0;
}

static int sequence_extension (picture_t * picture, uint8_t * buffer)
{
    /* check chroma format, size extensions, marker bit */
    if (((buffer[1] & 0x07) != 0x02) || (buffer[2] & 0xe0) ||
	((buffer[3] & 0x01) != 0x01))
	return 1;

    /* this is not used by the decoder */
    picture->progressive_sequence = (buffer[1] >> 3) & 1;

    picture->low_delay = buffer[5] & 0x80;

    if (!picture->progressive_sequence)
	picture->coded_picture_height =
	    (picture->coded_picture_height + 31) & ~31;
    
    
    /* printf ("libmpeg2: low_delay : %d\n", picture->low_delay); */

/*
    printf ("libmpeg2: sequence extension+5 : %08x (%d)\n",
	    buffer[5], buffer[5] % 0x80);
 */

    picture->frame_rate_ext_n = buffer[5] & 0x31;
    picture->frame_rate_ext_d = (buffer[5] >> 2) & 0x03;
    
    /* MPEG1 - for testing only */
    picture->mpeg1 = 0;

    return 0;
}

static int quant_matrix_extension (picture_t * picture, uint8_t * buffer)
{
    int i;

    if (buffer[0] & 8) {
	for (i = 0; i < 64; i++)
	    picture->intra_quantizer_matrix[mpeg2_scan_norm[i]] =
		(buffer[i] << 5) | (buffer[i+1] >> 3);
	buffer += 64;
    }

    if (buffer[0] & 4)
	for (i = 0; i < 64; i++)
	    picture->non_intra_quantizer_matrix[mpeg2_scan_norm[i]] =
		(buffer[i] << 6) | (buffer[i+1] >> 2);

    return 0;
}

static int picture_coding_extension (picture_t * picture, uint8_t * buffer)
{
    /* pre subtract 1 for use later in compute_motion_vector */
    picture->f_motion.f_code[0] = (buffer[0] & 15) - 1;
    picture->f_motion.f_code[1] = (buffer[1] >> 4) - 1;
    picture->b_motion.f_code[0] = (buffer[1] & 15) - 1;
    picture->b_motion.f_code[1] = (buffer[2] >> 4) - 1;

    picture->intra_dc_precision = (buffer[2] >> 2) & 3;
    picture->picture_structure = buffer[2] & 3;
    picture->frame_pred_frame_dct = (buffer[3] >> 6) & 1;
    picture->concealment_motion_vectors = (buffer[3] >> 5) & 1;
    picture->q_scale_type = (buffer[3] >> 4) & 1;
    picture->intra_vlc_format = (buffer[3] >> 3) & 1;

    if (buffer[3] & 4)	/* alternate_scan */
	picture->scan = mpeg2_scan_alt;
    else
	picture->scan = mpeg2_scan_norm;

    /* these are not used by the decoder */
    picture->top_field_first = buffer[3] >> 7;
    picture->repeat_first_field = (buffer[3] >> 1) & 1;
    picture->progressive_frame = buffer[4] >> 7;

    return 0;
}

static int sequence_display_extension (picture_t * picture, uint8_t * buffer) {
  /* FIXME: implement. */
  uint32_t bit_position;
  /*uint32_t padding;*/
  
  bit_position = 0; 
  /*padding = get_bits(buffer, 4, &bit_position);*/ bit_position += 4;
  picture->video_format = get_bits(buffer, 3, &bit_position);
  picture->colour_description = get_bits(buffer, 1, &bit_position);
  if(picture->colour_description) {
  picture->colour_primatives = get_bits(buffer, 8, &bit_position);
  picture->transfer_characteristics = get_bits(buffer, 8, &bit_position);
  picture->matrix_coefficients = get_bits(buffer, 8, &bit_position);
  }
  picture->display_horizontal_size = get_bits(buffer, 14, &bit_position);
  /*padding = get_bits(buffer, 1, &bit_position);*/ bit_position++;
  picture->display_vertical_size = get_bits(buffer, 14, &bit_position);

#ifdef LOG_PAN_SCAN
  printf("Sequence_display_extension\n");
  printf("     video_format: %u\n", picture->video_format);
  printf("     colour_description: %u\n", picture->colour_description);
  if(picture->colour_description) {
  printf("     colour_primatives: %u\n", picture->colour_primatives);
  printf("     transfer_characteristics %u\n", picture->transfer_characteristics);
  printf("     matrix_coefficients %u\n", picture->matrix_coefficients);
  }
  printf("     display_horizontal_size %u\n", picture->display_horizontal_size);
  printf("     display_vertical_size %u\n", picture->display_vertical_size);
#endif

  return 0;
}

static int picture_display_extension (picture_t * picture, uint8_t * buffer) {
  uint32_t bit_position;
  /*uint32_t padding;*/

#ifdef LOG_PAN_SCAN     
    printf ("libmpeg2: picture_display_extension\n");
#endif
  
  bit_position = 0; 
  /*padding = get_bits(buffer, 4, &bit_position);*/ bit_position += 4;
  picture->frame_centre_horizontal_offset = get_bits_signed(buffer, 16, &bit_position);
  /*padding = get_bits(buffer, 1, &bit_position);*/ bit_position++;
  picture->frame_centre_vertical_offset = get_bits_signed(buffer, 16, &bit_position);
  /*padding = get_bits(buffer, 1, &bit_position);*/ bit_position++;

#ifdef LOG_PAN_SCAN
  printf("Pan & Scan centre (x,y) = (%d, %d)\n",  
    picture->frame_centre_horizontal_offset,
    picture->frame_centre_vertical_offset);
#endif

  return 0;
}

int mpeg2_header_extension (picture_t * picture, uint8_t * buffer)
{
    switch (buffer[0] & 0xf0) {
    case 0x00:	/* reserved */
        return 0;

    case 0x10:	/* sequence extension */
	return sequence_extension (picture, buffer);

    case 0x20:	/* sequence display extension for Pan & Scan */
	return sequence_display_extension (picture, buffer);

    case 0x30:	/* quant matrix extension */
	return quant_matrix_extension (picture, buffer);

    case 0x40:	/* copyright extension */
        return 0;

    case 0x50:	/* sequence scalable extension */
        return 0;

    case 0x60:	/* reserved */
        return 0;

    case 0x70:	/* picture display extension for Pan & Scan */
	return picture_display_extension (picture, buffer);

    case 0x80:	/* picture coding extension */
	return picture_coding_extension (picture, buffer);

    case 0x90:	/* picture spacial scalable extension */
        return 0;

    case 0xA0:	/* picture temporal scalable extension */
        return 0;

    case 0xB0:	/* camera parameters extension */
        return 0;

    case 0xC0:	/* ITU-T extension */
        return 0;

    case 0xD0:	/* reserved */
        return 0;

    case 0xE0:	/* reserved */
        return 0;

    case 0xF0:	/* reserved */
        return 0;
    }

    return 0;
}

int mpeg2_header_group_of_pictures (picture_t * picture, uint8_t * buffer) {
  uint32_t bit_position;
  /*uint32_t padding;*/
  bit_position = 0;
  
  picture->drop_frame_flag = get_bits(buffer, 1, &bit_position);
  picture->time_code_hours = get_bits(buffer, 5, &bit_position);
  picture->time_code_minutes = get_bits(buffer, 6, &bit_position);
  /*padding = get_bits(buffer, 1, &bit_position);*/ bit_position++;
  picture->time_code_seconds = get_bits(buffer, 6, &bit_position);
  picture->time_code_pictures = get_bits(buffer, 6, &bit_position);
  picture->closed_gop = get_bits(buffer, 1, &bit_position);
  picture->broken_link = get_bits(buffer, 1, &bit_position);

#ifdef LOG_PAN_SCAN     
  printf("Group of pictures\n");
  printf("     drop_frame_flag: %u\n", picture->drop_frame_flag);
  printf("     time_code: HH:MM:SS:Pictures %02u:%02u:%02u:%02u\n", 
         picture->time_code_hours,
         picture->time_code_minutes,
         picture->time_code_seconds,
         picture->time_code_pictures);
  printf("     closed_gop: %u\n", picture->closed_gop);
  printf("     bloken_link: %u\n", picture->broken_link);
#endif

  return 0;
}

int mpeg2_header_picture (picture_t * picture, uint8_t * buffer)
{
    picture->picture_coding_type = (buffer [1] >> 3) & 7;
    picture->vbv_delay = ((buffer[1] << 13) | (buffer[2] << 5) |
			  (buffer[3] >> 3)) & 0xffff;

    /* forward_f_code and backward_f_code - used in mpeg1 only */
    picture->f_motion.f_code[1] = (buffer[3] >> 2) & 1;
    picture->f_motion.f_code[0] =
	(((buffer[3] << 1) | (buffer[4] >> 7)) & 7) - 1;
    picture->b_motion.f_code[1] = (buffer[4] >> 6) & 1;
    picture->b_motion.f_code[0] = ((buffer[4] >> 3) & 7) - 1;

    /* move in header_process_picture_header */
        picture->second_field =
            (picture->picture_structure != FRAME_PICTURE) &&
            !(picture->second_field);

    return 0;
}
