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
#ifndef HAVE_MPEG_PARSER_H
#define HAVE_MPEG_PARSER_H

#include <xine/xine_internal.h>
#include "ffmpeg_decoder.h"

#define BUFFER_SIZE (1194 * 1024) /* libmpeg2's buffer size */

/* picture coding type (mpeg2 header) */
#define I_TYPE 1
#define P_TYPE 2
#define B_TYPE 3
#define D_TYPE 4

typedef struct mpeg_parser_s {
  uint8_t        *chunk_buffer;
  uint8_t        *chunk_ptr;
  uint8_t        *chunk_start;
  uint32_t        shift;
  int             buffer_size;
  uint8_t         code;
  uint8_t         picture_coding_type;

  uint8_t         is_sequence_needed:1;
  uint8_t         is_mpeg1:1;     /* public */
  uint8_t         has_sequence:1; /* public */
  uint8_t         in_slice:1;

  uint8_t         rate_code:4;

  int             aspect_ratio_info;

  /* public properties */
  uint16_t        width;
  uint16_t        height;
  int             frame_duration;
  double          frame_aspect_ratio;

} mpeg_parser_t;

/* parser initialization */
void mpeg_parser_init (mpeg_parser_t *parser);

/* parser disposal */
void mpeg_parser_dispose (mpeg_parser_t *parser);

/* read a frame
 *   return a pointer to the first byte of the next frame
 *   or NULL if more bytes are needed
 *   *flush is set to 1 if the decoder must be flushed (needed for still menus)
 */
uint8_t *mpeg_parser_decode_data (mpeg_parser_t *parser,
                                  uint8_t *current, uint8_t *end,
                                  int *flush);

/* reset the parser */
void mpeg_parser_reset (mpeg_parser_t *parser);

#endif /* HAVE_MPEG_PARSER_H */
