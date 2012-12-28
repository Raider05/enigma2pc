/*
 * Copyright (C) 2008 the xine project
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
 */

static inline void demux_real_sipro_swap (char buffer[], int bs)
{
  /* bs = nybbles per subpacket */
  static const unsigned char sipr_swaps[38][2] = {
    {0, 63}, {1, 22}, {2, 44}, {3, 90}, {5, 81}, {7, 31}, {8, 86}, {9, 58},
    {10, 36}, {12, 68}, {13, 39}, {14, 73}, {15, 53}, {16, 69}, {17, 57},
    {19, 88}, {20, 34}, {21, 71}, {24, 46}, {25, 94}, {26, 54}, {28, 75},
    {29, 50}, {32, 70}, {33, 92}, {35, 74}, {38, 85}, {40, 56}, {42, 87},
    {43, 65}, {45, 59}, {48, 79}, {49, 93}, {51, 89}, {55, 95}, {61, 76},
    {67, 83}, {77, 80}
  };
  int n;

  for (n = 0; n < 38; ++n)
  {
    int j;
    int i = bs * sipr_swaps[n][0];
    int o = bs * sipr_swaps[n][1];
    /* swap nibbles of block 'i' with 'o'      TODO: optimize */
    for (j = 0; j < bs; ++j)
    {
      int x = (i & 1) ? (buffer[i >> 1] >> 4) : (buffer[i >> 1] & 0x0F);
      int y = (o & 1) ? (buffer[o >> 1] >> 4) : (buffer[o >> 1] & 0x0F);
      if (o & 1)
	buffer[o >> 1] = (buffer[o >> 1] & 0x0F) | (x << 4);
      else
	buffer[o >> 1] = (buffer[o >> 1] & 0xF0) | x;
      if (i & 1)
	buffer[i >> 1] = (buffer[i >> 1] & 0x0F) | (y << 4);
      else
	buffer[i >> 1] = (buffer[i >> 1] & 0xF0) | y;
      ++i;
      ++o;
    }
  }
}
