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

#include <sys/types.h>



typedef struct {
  const uint8_t *buffer, *start;
  int      offbits, length, oflow;
} bits_reader_t;



static void bits_reader_set( bits_reader_t *br, const uint8_t *buf, int len )
{
  br->buffer = br->start = buf;
  br->offbits = 0;
  br->length = len;
  br->oflow = 0;
}



static void skip_bits( bits_reader_t *br, int nbits )
{
  br->offbits += nbits;
  br->buffer += br->offbits / 8;
  br->offbits %= 8;
  if ( br->buffer > (br->start + br->length) ) {
    br->oflow = 1;
  }
}



static uint32_t get_bits( bits_reader_t *br, int nbits )
{
  int i, nbytes;
  uint32_t ret = 0;
  const uint8_t *buf;

  buf = br->buffer;
  nbytes = (br->offbits + nbits)/8;
  if ( ((br->offbits + nbits) %8 ) > 0 )
    nbytes++;
  if ( (buf + nbytes) > (br->start + br->length) ) {
    br->oflow = 1;
    return 0;
  }
  for ( i=0; i<nbytes; i++ )
    ret += buf[i]<<((nbytes-i-1)*8);
  i = (4-nbytes)*8+br->offbits;
  ret = ((ret<<i)>>i)>>((nbytes*8)-nbits-br->offbits);

  return ret;
}



static uint32_t read_bits( bits_reader_t *br, int nbits )
{
  uint32_t ret = get_bits(br, nbits);

  br->offbits += nbits;
  br->buffer += br->offbits / 8;
  br->offbits %= 8;

  return ret;
}
