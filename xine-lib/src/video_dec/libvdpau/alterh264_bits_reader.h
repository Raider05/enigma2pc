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

#ifndef ALTERH264_BITS_READER_H
#define ALTERH264_BITS_READER_H
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>



typedef struct {
  const uint8_t *buffer, *start;
  int offbits, length, oflow;
} bits_reader_t;



static void
bits_reader_set (bits_reader_t * br, const uint8_t * buf, int len)
{
  br->buffer = br->start = buf;
  br->offbits = 0;
  br->length = len;
  br->oflow = 0;
}



static inline uint32_t
more_rbsp_data (bits_reader_t * br)
{
  uint8_t val[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
  const uint8_t *buf = br->start + br->length;
  int bit;

  while (--buf >= br->buffer)
  {
    for (bit = 7; bit > -1; bit--)
      if (*buf & val[bit])
	return ((buf - br->buffer) * 8) - br->offbits + bit;
  }
  return 0;
}



static inline uint8_t
bits_reader_shift (bits_reader_t * br)
{
  br->offbits = 0;
  if ((br->buffer + 1) > (br->start + br->length - 1))
  {
    br->oflow = 1;
    //printf("!!!!! buffer overflow !!!!!\n");
    return 0;
  }
  ++br->buffer;
  if ((*(br->buffer) == 3) && ((br->buffer - br->start) > 2)
      && (*(br->buffer - 2) == 0) && (*(br->buffer - 1) == 0))
  {
    if ((br->buffer + 1) > (br->start + br->length - 1))
    {
      br->oflow = 1;
      //printf("!!!!! buffer overflow !!!!!\n");
      return 0;
    }
    ++br->buffer;
  }
  return 1;
}



static inline uint32_t
read_bits (bits_reader_t * br, int nbits)
{
  uint8_t val[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
  uint32_t res = 0;

  while (nbits)
  {
    res = (res << 1) + ((*br->buffer & val[br->offbits]) ? 1 : 0);
    --nbits;
    ++br->offbits;
    if (br->offbits > 7)
      if (!bits_reader_shift (br))
	return 1;
  }
  return res;
}



static inline void
skip_bits (bits_reader_t * br, int nbits)
{
  while (nbits)
  {
    --nbits;
    ++br->offbits;
    if (br->offbits > 7)
      bits_reader_shift (br);
  }
}



static inline uint32_t
read_exp_ue (bits_reader_t * br)
{
  int leading = -1;
  uint8_t b;

  for (b = 0; !b; leading++)
    b = read_bits (br, 1);

  return (1 << leading) - 1 + read_bits (br, leading);
}



static inline int32_t
read_exp_se (bits_reader_t * br)
{
  uint32_t res = read_exp_ue (br);
  return (res & 0x01) ? (res + 1) / 2 : -(res / 2);
}
#endif /* ALTERH264_BITS_READER_H */
