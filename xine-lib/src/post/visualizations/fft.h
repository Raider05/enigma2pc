/*
 * Copyright (C) 2000-2003 the xine project
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
#ifndef FFT_H
#define FFT_H

struct complex_s
{
  double re;
  double im;
};
typedef struct complex_s complex_t;


struct fft_s {
  int bits;
  double *SineTable;
  double *CosineTable;
  double *WinTable;
};
typedef struct fft_s fft_t;

fft_t  *fft_new (int bits);
void    fft_dispose(fft_t *fft);

void    fft_compute (fft_t *fft, complex_t wave[]);
void    fft_window (fft_t *fft, complex_t wave[]);

double  fft_amp (int n, complex_t wave[], int bits);
void    fft_scale (complex_t wave[], int bits);

#endif /* FFT_H */
