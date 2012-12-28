// -*- c++ -*-
/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2002 Tom Barry  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
//
//  This file is subject to the terms of the GNU General Public License as
//  published by the Free Software Foundation.  A copy of this license is
//  included with this software distribution in the file COPYING.  If you
//  do not have a copy, you may obtain a copy by writing to the Free
//  Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details
//
//  Also, this program is "Philanthropy-Ware".  That is, if you like it and 
//  feel the need to reward or inspire the author then please feel free (but
//  not obligated) to consider joining or donating to the Electronic Frontier
//  Foundation. This will help keep cyber space free of barbed wire and bullsh*t.  
//  See www.eff.org for details
/////////////////////////////////////////////////////////////////////////////

#if !defined(MASKS_DEFINED)
#define MASKS_DEFINED
static const int64_t __attribute__((__used__)) Max_Mov   = 0x0404040404040404ull;
static const int64_t __attribute__((__used__)) DiffThres = 0x0f0f0f0f0f0f0f0full;
static const int64_t __attribute__((__used__)) YMask     = 0x00ff00ff00ff00ffull; // keeps only luma
static const int64_t __attribute__((__used__)) UVMask    = 0xff00ff00ff00ff00ull; // keeps only chroma
static const int64_t __attribute__((__used__)) TENS      = 0x0a0a0a0a0a0a0a0aull;
static const int64_t __attribute__((__used__)) FOURS     = 0x0404040404040404ull;
static const int64_t __attribute__((__used__)) ONES      = 0x0101010101010101ull;
static const int64_t __attribute__((__used__)) ShiftMask = 0xfefffefffefffeffull;
static int64_t Min_Vals  = 0x0000000000000000ull;
static int64_t Max_Vals  = 0x0000000000000000ull;
#endif

#ifndef TopFirst
#define TopFirst IsOdd
#endif

#ifdef SEFUNC
#undef SEFUNC
#endif

#if defined(IS_SSE)
#define SEFUNC(x) Search_Effort_SSE_##x
#elif defined(IS_3DNOW)
#define SEFUNC(x) Search_Effort_3DNOW_##x
#else
#define SEFUNC(x) Search_Effort_MMX_##x
#endif

static void FUNCT_NAME(uint8_t *output, int outstride,
                  deinterlace_frame_data_t *data,
                  int bottom_field, int second_field, int width, int height )
{
    int IsOdd;
    const unsigned char* pWeaveSrc;
    const unsigned char* pWeaveSrcP;
    unsigned char* pWeaveDest;
    const unsigned char* pCopySrc;
    const unsigned char* pCopySrcP;
    unsigned char* pCopyDest;
    int src_pitch;
    int dst_pitch;
    int rowsize;
    int FldHeight;
    int stride = (width*2);
    long olddx;


    src_pitch = stride*2;
    dst_pitch = outstride;
    rowsize = stride;
    FldHeight = height / 2;
       
    if( second_field ) {
        pWeaveSrc = data->f0;
        pCopySrc = data->f0;
        pWeaveSrcP = data->f1;
        pCopySrcP = data->f1;
    } else {
        pWeaveSrc = data->f0;
        pCopySrc = data->f1;
        pWeaveSrcP = data->f1;
        pCopySrcP = data->f2;
    }

    if( bottom_field ) {
        pWeaveSrc += stride;
        pCopySrc += 0;
        pWeaveSrcP += stride;
        pCopySrcP += 0;
    } else {
        pWeaveSrc += 0;
        pCopySrc += stride;
        pWeaveSrcP += 0;
        pCopySrcP += stride;
    }

    IsOdd = bottom_field;

#ifdef IS_SSE2
// SSE2 support temporarily deleted

#endif

    if(IsOdd) {
		// if we have an odd field we copy an even field and weave an odd field
		pCopyDest = output;
		pWeaveDest = output + dst_pitch;
	} else {
		// if we have an ever field we copy an odd field and weave an even field
		pCopyDest = output + dst_pitch;
		pWeaveDest = output;
	}
	// copy 1st and last weave lines 
	Fieldcopy(pWeaveDest, pCopySrc, rowsize,		
              1, dst_pitch*2, src_pitch);
	Fieldcopy(pWeaveDest+(FldHeight-1)*dst_pitch*2,
              pCopySrc+(FldHeight-1)*src_pitch, rowsize, 
              1, dst_pitch*2, src_pitch);

#ifdef USE_VERTICAL_FILTER
// Vertical Filter currently not implemented for DScaler !!
	// copy 1st and last lines the copy field
	Fieldcopy(pCopyDest, pCopySrc, rowsize, 
              1, dst_pitch*2, src_pitch);
	Fieldcopy(pCopyDest+(FldHeight-1)*dst_pitch*2,
              pCopySrc+(FldHeight-1)*src_pitch, rowsize, 
              1, dst_pitch*2, src_pitch);
#else

	// copy all of the copy field
	Fieldcopy(pCopyDest, pCopySrc, rowsize, 
              FldHeight, dst_pitch*2, src_pitch);
#endif	
	// then go fill in the hard part, being variously lazy depending upon
	// SearchEffort

    if(UseStrangeBob == 0) {
		if (SearchEffort == 0)
		{
		        goto SEFUNC(0);
		}
		else if (SearchEffort <= 1)
		{
			goto SEFUNC(1);
		}
	/*	else if (SearchEffort <= 2)
		{
			goto SEFUNC(2);
		}
	*/
		else if (SearchEffort <= 3)
		{
			goto SEFUNC(3);
		}
		else if (SearchEffort <= 5)
		{
			goto SEFUNC(5);
		}
		else if (SearchEffort <= 9)
		{
			goto SEFUNC(9);
		}
		else if (SearchEffort <= 11)
		{
			goto SEFUNC(11);
		}
		else if (SearchEffort <= 13)
		{
			goto SEFUNC(13);
		}
		else if (SearchEffort <= 15)
		{
			goto SEFUNC(15);
		}
		else if (SearchEffort <= 19)
		{
			goto SEFUNC(19);
		}
		else if (SearchEffort <= 21)
		{
			goto SEFUNC(21);
		}
		else 
		{
			goto SEFUNC(Max);
		}
	}
	else
	{
		if (SearchEffort == 0)
		{
			goto SEFUNC(0_SB);
		}
		else if (SearchEffort <= 1)
		{
			goto SEFUNC(1_SB);
		}
	/*	else if (SearchEffort <= 2)
		{
			goto SEFUNC(2_SB);
		}
	*/
		else if (SearchEffort <= 3)
		{
			goto SEFUNC(3_SB);
		}
		else if (SearchEffort <= 5)
		{
			goto SEFUNC(5_SB);
		}
		else if (SearchEffort <= 9)
		{
			goto SEFUNC(9_SB);
		}
		else if (SearchEffort <= 11)
		{
			goto SEFUNC(11_SB);
		}
		else if (SearchEffort <= 13)
		{
			goto SEFUNC(13_SB);
		}
		else if (SearchEffort <= 15)
		{
			goto SEFUNC(15_SB);
		}
		else if (SearchEffort <= 19)
		{
			goto SEFUNC(19_SB);
		}
		else if (SearchEffort <= 21)
		{
			goto SEFUNC(21_SB);
		}
		else 
		{
			goto SEFUNC(Max_SB);
		}
	}

end:
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    __asm__ __volatile__("emms");
#endif
    return;
     
#include "TomsMoCompAll2.inc"

#define USE_STRANGE_BOB

#include "TomsMoCompAll2.inc"

#undef USE_STRANGE_BOB
}
