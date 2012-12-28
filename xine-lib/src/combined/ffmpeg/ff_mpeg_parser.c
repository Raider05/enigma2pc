/*
 * Copyright (C) 2001-2004 the xine project
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
 * Simple MPEG-ES parser/framer by Thibaut Mattern (tmattern@noos.fr)
 *   based on libmpeg2 decoder.
 */
#define LOG_MODULE "mpeg_parser"
#define LOG_VERBOSE
/*
#define LOG
*/
#include "ff_mpeg_parser.h"

/* mpeg frame rate table from lavc */
static const int frame_rate_tab[][2] = {
    {    0,    0},
    {24000, 1001},
    {   24,    1},
    {   25,    1},
    {30000, 1001},
    {   30,    1},
    {   50,    1},
    {60000, 1001},
    {   60,    1},
  /* Xing's 15fps: (9) */
    {   15,    1},
  /* libmpeg3's "Unofficial economy rates": (10-13) */
    {    5,    1},
    {   10,    1},
    {   12,    1},
    {   15,    1},
    {    0,    0},
};

void mpeg_parser_init (mpeg_parser_t *parser)
{
  parser->chunk_buffer = malloc(BUFFER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
  mpeg_parser_reset(parser);
}

void mpeg_parser_dispose (mpeg_parser_t *parser)
{
  if ( parser == NULL ) return;

  free(parser->chunk_buffer);
}

void mpeg_parser_reset (mpeg_parser_t *parser)
{
  parser->shift                  = 0xffffff00;
  parser->is_sequence_needed     = 1;
  parser->in_slice               = 0;
  parser->chunk_ptr              = parser->chunk_buffer;
  parser->chunk_start            = parser->chunk_buffer;
  parser->buffer_size            = 0;
  parser->code                   = 0xb4;
  parser->picture_coding_type    = 0;
  parser->width                  = 0;
  parser->height                 = 0;
  parser->rate_code              = 0;
  parser->aspect_ratio_info      = 0;
  parser->frame_duration         = 0;
  parser->is_mpeg1               = 0;
  parser->has_sequence           = 0;
  parser->frame_aspect_ratio     = 0.0;
}

static void parse_header_picture (mpeg_parser_t *parser, uint8_t * buffer)
{
  parser->picture_coding_type = (buffer [1] >> 3) & 7;
}

static double get_aspect_ratio(mpeg_parser_t *parser)
{
  double ratio;
  double mpeg1_pel_ratio[16] = {1.0 /* forbidden */,
    1.0, 0.6735, 0.7031, 0.7615, 0.8055, 0.8437, 0.8935, 0.9157,
    0.9815, 1.0255, 1.0695, 1.0950, 1.1575, 1.2015, 1.0 /*reserved*/ };

  if( !parser->is_mpeg1 ) {
    /* these hardcoded values are defined on mpeg2 standard for
     * aspect ratio. other values are reserved or forbidden.  */
    switch (parser->aspect_ratio_info) {
    case 2:
      ratio = 4.0 / 3.0;
      break;
    case 3:
      ratio = 16.0 / 9.0;
      break;
    case 4:
      ratio = 2.11 / 1.0;
      break;
    case 1:
    default:
      ratio = (double)parser->width / (double)parser->height;
      break;
    }
  } else {
    /* mpeg1 constants refer to pixel aspect ratio */
    ratio = (double)parser->width / (double)parser->height;
    ratio /= mpeg1_pel_ratio[parser->aspect_ratio_info];
  }

  return ratio;
}

static int parse_chunk (mpeg_parser_t *parser, int code, uint8_t *buffer, int len)
{
  int is_frame_done;
  int next_code = parser->code;

  /* wait for sequence_header_code */
  if (parser->is_sequence_needed) {
    if (code != 0xb3) {
      lprintf("waiting for sequence header\n");
      parser->chunk_ptr = parser->chunk_buffer;
      return 0;
    }
  }

  is_frame_done = parser->in_slice && ((!next_code)  || (next_code == 0xb7));

  if (is_frame_done)
    parser->in_slice = 0;

  switch (code) {
  case 0x00:        /* picture_start_code */

    parse_header_picture (parser, buffer);

    parser->in_slice = 1;

    switch (parser->picture_coding_type) {
    case B_TYPE:
      lprintf ("B-Frame\n");
      break;

    case P_TYPE:
      lprintf ("P-Frame\n");
      break;

    case I_TYPE:
      lprintf ("I-Frame\n");
      break;
    }
    break;

  case 0xb2:     /* user data code */
    /* process_userdata(mpeg2dec, buffer); */
    break;

  case 0xb3:     /* sequence_header_code */
    {
      int value;
      uint16_t width, height;

      if (parser->is_sequence_needed) {
        parser->is_sequence_needed = 0;
      }

      if ((buffer[6] & 0x20) != 0x20) {
        lprintf("Invalid sequence: missing marker_bit\n");
        parser->has_sequence = 0;
        break;  /* missing marker_bit */
      }

      value = (buffer[0] << 16) |
              (buffer[1] << 8)  |
               buffer[2];
      width  = ((value >> 12) + 15) & ~15;
      height = ((value & 0xfff) + 15) & ~15;

      if ((width > 1920) || (height > 1152)) {
        lprintf("Invalid sequence: width=%d, height=%d\n", width, height);
        parser->has_sequence = 0;
        break;  /* size restrictions for MP@HL */
      }

      parser->width  = width;
      parser->height = height;
      parser->rate_code = buffer[3] & 15;
      parser->aspect_ratio_info = buffer[3] >> 4;

      if (parser->rate_code < (sizeof(frame_rate_tab)/sizeof(*frame_rate_tab))) {
        parser->frame_duration = 90000;
        parser->frame_duration *= frame_rate_tab[parser->rate_code][1];
        parser->frame_duration /= frame_rate_tab[parser->rate_code][0];
      } else {
        printf ("invalid/unknown frame rate code : %d \n",
                parser->rate_code);
        parser->frame_duration = 0;
      }

      parser->has_sequence = 1;
      parser->is_mpeg1 = 1;
    }
    break;

  case 0xb5:     /* extension_start_code */
    switch (buffer[0] & 0xf0) {
    case 0x10:     /* sequence extension */
      parser->is_mpeg1 = 0;
    }

  default:
    if (code >= 0xb9)
      lprintf ("stream not demultiplexed ?\n");

    if (code >= 0xb0)
      break;
  }
  return is_frame_done;
}

static inline uint8_t *copy_chunk (mpeg_parser_t *parser,
                                   uint8_t *current, uint8_t *end)
{
  uint32_t shift;
  uint8_t *chunk_ptr;
  uint8_t *limit;
  uint8_t byte;

  shift = parser->shift;
  chunk_ptr = parser->chunk_ptr;

  limit = current + (parser->chunk_buffer + BUFFER_SIZE - chunk_ptr);
  if (limit > end)
    limit = end;

  while (1) {

    byte = *current++;
    *chunk_ptr++ = byte;
    if (shift != 0x00000100) {
      shift = (shift | byte) << 8;
      if (current < limit)
        continue;
      if (current == end) {
        parser->chunk_ptr = chunk_ptr;
        parser->shift = shift;
        lprintf("Need more bytes\n");
        return NULL;
      } else {
        /* we filled the chunk buffer without finding a start code */
        lprintf("Buffer full\n");
        parser->code = 0xb4;        /* sequence_error_code */
        parser->chunk_ptr = parser->chunk_buffer;
        return current;
      }
    }
    lprintf("New chunk: 0x%2X\n", byte);
    parser->chunk_ptr = chunk_ptr;
    parser->shift = 0xffffff00;
    parser->code = byte;
    return current;
  }
}


uint8_t *mpeg_parser_decode_data (mpeg_parser_t *parser,
                                  uint8_t *current, uint8_t *end,
                                  int *flush)
{
  int ret;
  uint8_t code;

  ret = 0;
  *flush = 0;

  while (current != end) {
    if (parser->chunk_ptr == parser->chunk_buffer) {
      /* write the beginning of the chunk */
      parser->chunk_buffer[0] = 0x00;
      parser->chunk_buffer[1] = 0x00;
      parser->chunk_buffer[2] = 0x01;
      parser->chunk_buffer[3] = parser->code;
      parser->chunk_ptr += 4;
      parser->chunk_start = parser->chunk_ptr;
      parser->has_sequence = 0;
    }

    code = parser->code;

    current = copy_chunk (parser, current, end);
    if (current == NULL)
      return NULL;
    ret = parse_chunk (parser, code, parser->chunk_start,
                       parser->chunk_ptr - parser->chunk_start - 4);
    parser->chunk_start = parser->chunk_ptr;
    if (ret == 1) {
      if (parser->has_sequence) {
        parser->frame_aspect_ratio = get_aspect_ratio(parser);
      }
      parser->buffer_size = parser->chunk_ptr - parser->chunk_buffer - 4;
      parser->chunk_ptr = parser->chunk_buffer;

      if (parser->code == 0xb7) /* sequence end, maybe a still menu */
        *flush = 1;

      return current;
    }
  }

  return NULL;
}
