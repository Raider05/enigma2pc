/*
 * Copyright (C) 2000-2012 the xine project
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
 * FFT code by Steve Haehnichen, originally licensed under GPL v1
 * modified by Thibaut Mattern (tmattern@noos.fr) to remove global vars
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "fft.h"

/**************************************************************************
 * fft specific decode functions
 *************************************************************************/

# define                SINE(x)         (fft->SineTable[(x)])
# define                COSINE(x)       (fft->CosineTable[(x)])
# define                WINDOW(x)       (fft->WinTable[(x)])

#define PERMUTE(x, y)   reverse((x), (y))

/* Number of samples in one "frame" */
#define SAMPLES         (1 << bits)

#define REAL(x)         wave[(x)].re
#define IMAG(x)         wave[(x)].im
#define ALPHA           0.54

/*
 *  Bit reverser for unsigned ints
 *  Reverses 'bits' bits.
 */
static inline const unsigned int
reverse (unsigned int val, int bits)
{
  unsigned int retn = 0;

  while (bits--)
    {
      retn <<= 1;
      retn |= (val & 1);
      val >>= 1;
    }
  return (retn);
}

/*
 *  Here is the real work-horse.
 *  It's a generic FFT, so nothing is lost or approximated.
 *  The samples in wave[] should be in order, and they
 *  will be decimated when fft() returns.
 */
void fft_compute (fft_t *fft, complex_t wave[])
{
  register int  loop, loop1, loop2;
  unsigned      i1;             /* going to right shift this */
  int           i2, i3, i4, y;
  double         a1, a2, b1, b2, z1, z2;
  int bits = fft->bits;

  i1 = SAMPLES / 2;
  i2 = 1;

  /* perform the butterflys */

  for (loop = 0; loop < bits; loop++)
    {
      i3 = 0;
      i4 = i1;

      for (loop1 = 0; loop1 < i2; loop1++)
        {
          y  = PERMUTE(i3 / (int)i1, bits);
          z1 = COSINE(y);
          z2 = -SINE(y);

          for (loop2 = i3; loop2 < i4; loop2++)
            {
              a1 = REAL(loop2);
              a2 = IMAG(loop2);

              b1 = z1 * REAL(loop2+i1) - z2 * IMAG(loop2+i1);
              b2 = z2 * REAL(loop2+i1) + z1 * IMAG(loop2+i1);

              REAL(loop2) = a1 + b1;
              IMAG(loop2) = a2 + b2;

              REAL(loop2+i1) = a1 - b1;
              IMAG(loop2+i1) = a2 - b2;
            }

          i3 += (i1 << 1);
          i4 += (i1 << 1);
        }

      i1 >>= 1;
      i2 <<= 1;
    }
}

/*
 *  Initializer for FFT routines.  Currently only sets up tables.
 *  - Generates scaled lookup tables for sin() and cos()
 *  - Fills a table for the Hamming window function
 */
fft_t *fft_new (int bits)
{
  fft_t *fft;
  int i;
  const double   TWOPIoN   = (atan(1.0) * 8.0) / (double)SAMPLES;
  const double   TWOPIoNm1 = (atan(1.0) * 8.0) / (double)(SAMPLES - 1);

  /* printf("fft_new: bits=%d\n", bits); */

  fft = (fft_t*)malloc(sizeof(fft_t));
  if (!fft)
    return NULL;

  fft->bits = bits;
  fft->SineTable   = malloc (sizeof(double) * SAMPLES);
  fft->CosineTable = malloc (sizeof(double) * SAMPLES);
  fft->WinTable    = malloc (sizeof(double) * SAMPLES);
  for (i=0; i < SAMPLES; i++)
    {
      fft->SineTable[i]   = sin((double) i * TWOPIoN);
      fft->CosineTable[i] = cos((double) i * TWOPIoN);
      /*
       * Generalized Hamming window function.
       * Set ALPHA to 0.54 for a hanning window. (Good idea)
       */
      fft->WinTable[i] = ALPHA + ((1.0 - ALPHA)
				  * cos (TWOPIoNm1 * (i - SAMPLES/2)));
    }
  return fft;
}

void fft_dispose(fft_t *fft)
{
  if (fft)
  {
    free(fft->SineTable);
    free(fft->CosineTable);
    free(fft->WinTable);
    free(fft);
  }
}

/*
 *  Apply some windowing function to the samples.
 */
void fft_window (fft_t *fft, complex_t wave[])
{
  int i;
  int bits = fft->bits;

  for (i = 0; i < SAMPLES; i++)
    {
      REAL(i) *= WINDOW(i);
      IMAG(i) *= WINDOW(i);
    }
}

/*
 *  Calculate amplitude of component n in the decimated wave[] array.
 */
double fft_amp (int n, complex_t wave[], int bits)
{
  n = PERMUTE (n, bits);
  return (hypot (REAL(n), IMAG(n)));
}

/*
 *  Scale sampled values.
 *  Do this *before* the fft.
 */
void fft_scale (complex_t wave[], int bits)
{
  int i;

  for (i = 0; i < SAMPLES; i++)  {
    wave[i].re /= SAMPLES;
    wave[i].im /= SAMPLES;
  }
}
