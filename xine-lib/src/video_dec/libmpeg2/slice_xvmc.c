/*
 * slice_xvmc.c
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

#include "config.h"

#include <stdio.h>
#include <string.h>	/* memcpy/memset, try to remove */
#include <stdlib.h>
#include <inttypes.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include "mpeg2_internal.h"
#include <xine/xineutils.h>

#include <xine/attributes.h>
#include "accel_xvmc.h"
#include "xvmc.h"


#define MOTION_ACCEL   XINE_VO_MOTION_ACCEL
#define IDCT_ACCEL     XINE_VO_IDCT_ACCEL
#define SIGNED_INTRA   XINE_VO_SIGNED_INTRA
#define ACCEL          (MOTION_ACCEL | IDCT_ACCEL)

#include "vlc.h"
/* original (non-patched) scan tables */

static const uint8_t mpeg2_scan_norm_orig[64] ATTR_ALIGN(16) =
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

static const uint8_t mpeg2_scan_alt_orig[64] ATTR_ALIGN(16) =
{
    /* Alternate scan pattern */
    0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,
    41,33,26,18,3,11,4,12,19,27,34,42,50,58,35,43,
    51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,
    53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63
};

static uint8_t mpeg2_scan_alt_ptable[64] ATTR_ALIGN(16);
static uint8_t mpeg2_scan_norm_ptable[64] ATTR_ALIGN(16);
static uint8_t mpeg2_scan_orig_ptable[64] ATTR_ALIGN(16);

void xvmc_setup_scan_ptable( void )
{
    int i;
    for (i=0; i<64; ++i) {
	mpeg2_scan_norm_ptable[mpeg2_scan_norm_orig[i]] = mpeg2_scan_norm[i];
	mpeg2_scan_alt_ptable[mpeg2_scan_alt_orig[i]] = mpeg2_scan_alt[i];
	mpeg2_scan_orig_ptable[i] = i;
    }
}
    

static const int non_linear_quantizer_scale [] = {
    0,  1,  2,  3,  4,  5,   6,   7,
    8, 10, 12, 14, 16, 18,  20,  22,
    24, 28, 32, 36, 40, 44,  48,  52,
    56, 64, 72, 80, 88, 96, 104, 112
};

static inline int get_xvmc_macroblock_modes (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int macroblock_modes;
    const MBtab * tab;

    switch (picture->picture_coding_type) {
    case I_TYPE:

	tab = MB_I + UBITS (bit_buf, 1);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if ((! (picture->frame_pred_frame_dct)) &&
	    (picture->picture_structure == FRAME_PICTURE)) {
	    macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
	    DUMPBITS (bit_buf, bits, 1);
	}

	return macroblock_modes;

    case P_TYPE:

	tab = MB_P + UBITS (bit_buf, 5);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if (picture->picture_structure != FRAME_PICTURE) {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD) {
		macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
		DUMPBITS (bit_buf, bits, 2);
	    }
	    return macroblock_modes;
	} else if (picture->frame_pred_frame_dct) {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
		macroblock_modes |= MC_FRAME;
	    return macroblock_modes;
	} else {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD) {
		macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
		DUMPBITS (bit_buf, bits, 2);
	    }
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
		macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
		DUMPBITS (bit_buf, bits, 1);
	    }
	    return macroblock_modes;
	}

    case B_TYPE:

	tab = MB_B + UBITS (bit_buf, 6);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if (picture->picture_structure != FRAME_PICTURE) {
	    if (! (macroblock_modes & MACROBLOCK_INTRA)) {
		macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
		DUMPBITS (bit_buf, bits, 2);
	    }
	    return macroblock_modes;
	} else if (picture->frame_pred_frame_dct) {
	    /* if (! (macroblock_modes & MACROBLOCK_INTRA)) */
	    macroblock_modes |= MC_FRAME;
	    return macroblock_modes;
	} else {
	    if (macroblock_modes & MACROBLOCK_INTRA)
		goto intra;
	    macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
	    DUMPBITS (bit_buf, bits, 2);
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
	    intra:
		macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
		DUMPBITS (bit_buf, bits, 1);
	    }
	    return macroblock_modes;
	}

    case D_TYPE:

	DUMPBITS (bit_buf, bits, 1);
	return MACROBLOCK_INTRA;

    default:
	return 0;
    }
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int get_xvmc_quantizer_scale (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)

    int quantizer_scale_code;

    quantizer_scale_code = UBITS (bit_buf, 5);
    DUMPBITS (bit_buf, bits, 5);

    if (picture->q_scale_type)
	return non_linear_quantizer_scale [quantizer_scale_code];
    else
	return quantizer_scale_code << 1;
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int get_xvmc_motion_delta (picture_t * picture, int f_code)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)

    int delta;
    int sign;
    const MVtab * tab;

    if (bit_buf & 0x80000000) {
	DUMPBITS (bit_buf, bits, 1);
	return 0;
    } else if (bit_buf >= 0x0c000000) {

	tab = MV_4 + UBITS (bit_buf, 4);
	delta = (tab->delta << f_code) + 1;
	bits += tab->len + f_code + 1;
	bit_buf <<= tab->len;

	sign = SBITS (bit_buf, 1);
	bit_buf <<= 1;

	if (f_code)
	    delta += UBITS (bit_buf, f_code);
	bit_buf <<= f_code;

	return (delta ^ sign) - sign;

    } else {

	tab = MV_10 + UBITS (bit_buf, 10);
	delta = (tab->delta << f_code) + 1;
	bits += tab->len + 1;
	bit_buf <<= tab->len;

	sign = SBITS (bit_buf, 1);
	bit_buf <<= 1;

	if (f_code) {
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    delta += UBITS (bit_buf, f_code);
	    DUMPBITS (bit_buf, bits, f_code);
	}

	return (delta ^ sign) - sign;

    }
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int bound_motion_vector (int vec, int f_code)
{
#if 1
    unsigned int limit;
    int sign;

    limit = 16 << f_code;

    if ((unsigned int)(vec + limit) < 2 * limit)
	return vec;
    else {
	sign = ((int32_t)vec) >> 31;
	return vec - ((2 * limit) ^ sign) + sign;
    }
#else
    return ((int32_t)vec << (27 - f_code)) >> (27 - f_code);
#endif
}

static inline int get_xvmc_dmv (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)

    const DMVtab * tab;

    tab = DMV_2 + UBITS (bit_buf, 2);
    DUMPBITS (bit_buf, bits, tab->len);
    return tab->dmv;
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int get_xvmc_coded_block_pattern (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)

    const CBPtab * tab;

    NEEDBITS (bit_buf, bits, bit_ptr);

    if (bit_buf >= 0x20000000) {

	tab = CBP_7 + (UBITS (bit_buf, 7) - 16);
	DUMPBITS (bit_buf, bits, tab->len);
	return tab->cbp;

    } else {

	tab = CBP_9 + UBITS (bit_buf, 9);
	DUMPBITS (bit_buf, bits, tab->len);
	return tab->cbp;
    }

#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int get_xvmc_luma_dc_dct_diff (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    const DCtab * tab;
    int size;
    int dc_diff;

    if (bit_buf < 0xf8000000) {
	tab = DC_lum_5 + UBITS (bit_buf, 5);
	size = tab->size;
	if (size) {
	    bits += tab->len + size;
	    bit_buf <<= tab->len;
	    dc_diff =
		UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	    bit_buf <<= size;
	    return dc_diff;
	} else {
	    DUMPBITS (bit_buf, bits, 3);
	    return 0;
	}
    } else {
	tab = DC_long + (UBITS (bit_buf, 9) - 0x1e0);
	size = tab->size;
	DUMPBITS (bit_buf, bits, tab->len);
	NEEDBITS (bit_buf, bits, bit_ptr);
	dc_diff = UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	DUMPBITS (bit_buf, bits, size);
	return dc_diff;
    }
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline int get_xvmc_chroma_dc_dct_diff (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    const DCtab * tab;
    int size;
    int dc_diff;

    if (bit_buf < 0xf8000000) {
	tab = DC_chrom_5 + UBITS (bit_buf, 5);
	size = tab->size;
	if (size) {
	    bits += tab->len + size;
	    bit_buf <<= tab->len;
	    dc_diff =
		UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	    bit_buf <<= size;
	    return dc_diff;
	} else {
	    DUMPBITS (bit_buf, bits, 2);
	    return 0;
	}
    } else {
	tab = DC_long + (UBITS (bit_buf, 10) - 0x3e0);
	size = tab->size;
	DUMPBITS (bit_buf, bits, tab->len + 1);
	NEEDBITS (bit_buf, bits, bit_ptr);
	dc_diff = UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	DUMPBITS (bit_buf, bits, size);
	return dc_diff;
    }
#undef bit_buf
#undef bits
#undef bit_ptr
}

#define SATURATE(val)			\
do {					\
    if ((uint32_t)(val + 2048) > 4095)	\
	val = (val > 0) ? 2047 : -2048;	\
} while (0)

static void get_xvmc_intra_block_B14 (picture_t * picture)
{
    int i;
    int j;
    int l;
    int val;
    const uint8_t * scan = picture->scan;
    uint8_t * scan_ptable = mpeg2_scan_orig_ptable;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = picture->quantizer_scale;
    int mismatch;
    const DCTtab * tab;
    uint32_t bit_buf;
    int bits;
    uint8_t * bit_ptr;
    int16_t * dest;

    dest = picture->mc->blockptr;
    
    if( picture->mc->xvmc_accel & IDCT_ACCEL ) {
	if ( scan == mpeg2_scan_norm ) {
	    scan =  mpeg2_scan_norm_orig; 
	    scan_ptable = mpeg2_scan_norm_ptable;
	} else {
	    scan = mpeg2_scan_alt_orig;
	    scan_ptable = mpeg2_scan_alt_ptable;
	}
    }
	    
    i = 0;
    mismatch = ~dest[0];

    bit_buf = picture->bitstream_buf;
    bits = picture->bitstream_bits;
    bit_ptr = picture->bitstream_ptr;

    NEEDBITS (bit_buf, bits, bit_ptr);

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bit_buf, 5) - 5);

	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    l = scan_ptable[j = scan[i]];
	    
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = (tab->level * quantizer_scale * quant_matrix[l]) >> 4;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bit_buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    l = scan_ptable[j = scan[i]];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    val = (SBITS (bit_buf, 12) *
		   quantizer_scale * quant_matrix[l]) / 16;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bit_buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bit_buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bit_buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    GETWORD (bit_buf, bits + 16, bit_ptr);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }

    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 2);	/* dump end of block code */
    picture->bitstream_buf = bit_buf;
    picture->bitstream_bits = bits;
    picture->bitstream_ptr = bit_ptr;
}

static void get_xvmc_intra_block_B15 (picture_t * picture)
{
    int i;
    int j;
    int l;
    int val;
    const uint8_t * scan = picture->scan;
    uint8_t * scan_ptable = mpeg2_scan_orig_ptable;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = picture->quantizer_scale;
    int mismatch;
    const DCTtab * tab;
    uint32_t bit_buf;
    int bits;
    uint8_t * bit_ptr;
    int16_t * dest;

    dest = picture->mc->blockptr;

    if( picture->mc->xvmc_accel & IDCT_ACCEL ) {
	if ( scan == mpeg2_scan_norm ) {
	    scan =  mpeg2_scan_norm_orig; 
	    scan_ptable = mpeg2_scan_norm_ptable;
	} else {
	    scan = mpeg2_scan_alt_orig;
	    scan_ptable = mpeg2_scan_alt_ptable;
	}
    }
	    	    
    i = 0;
    mismatch = ~dest[0];

    bit_buf = picture->bitstream_buf;
    bits = picture->bitstream_bits;
    bit_ptr = picture->bitstream_ptr;

    NEEDBITS (bit_buf, bits, bit_ptr);

    while (1) {
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B15_8 + (UBITS (bit_buf, 8) - 4);

	    i += tab->run;
	    if (i < 64) {

	    normal_code:
		l = scan_ptable[j = scan[i]];
		bit_buf <<= tab->len;
		bits += tab->len + 1;
		val = (tab->level * quantizer_scale * quant_matrix[l]) >> 4;

		/* if (bitstream_get (1)) val = -val; */
		val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		bit_buf <<= 1;
		NEEDBITS (bit_buf, bits, bit_ptr);

		continue;

	    } else {

		/* end of block. I commented out this code because if we */
		/* dont exit here we will still exit at the later test :) */

		/* if (i >= 128) break;	*/	/* end of block */

		/* escape code */

                i += UBITS (bit_buf << 6, 6) - 64;
		if (i >= 64)
		    break;	/* illegal, check against buffer overflow */

		l = scan_ptable[j = scan[i]];

		DUMPBITS (bit_buf, bits, 12);
		NEEDBITS (bit_buf, bits, bit_ptr);
		val = (SBITS (bit_buf, 12) *
		       quantizer_scale * quant_matrix[l]) / 16;

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		DUMPBITS (bit_buf, bits, 12);
		NEEDBITS (bit_buf, bits, bit_ptr);

		continue;

	    }
	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B15_10 + (UBITS (bit_buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bit_buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bit_buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    GETWORD (bit_buf, bits + 16, bit_ptr);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }

    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 4);	/* dump end of block code */
    picture->bitstream_buf = bit_buf;
    picture->bitstream_bits = bits;
    picture->bitstream_ptr = bit_ptr;
}

static void get_xvmc_non_intra_block (picture_t * picture)
{
    int i;
    int j;
    int l;
    int val;
    const uint8_t * scan = picture->scan;
    uint8_t * scan_ptable = mpeg2_scan_orig_ptable;
    uint8_t * quant_matrix = picture->non_intra_quantizer_matrix;
    int quantizer_scale = picture->quantizer_scale;
    int mismatch;
    const DCTtab * tab;
    uint32_t bit_buf;
    int bits;
    uint8_t * bit_ptr;
    int16_t * dest;

    i = -1;
    mismatch = 1;

    dest = picture->mc->blockptr;

    if( picture->mc->xvmc_accel & IDCT_ACCEL ) {
	if ( scan == mpeg2_scan_norm ) {
	    scan =  mpeg2_scan_norm_orig; 
	    scan_ptable = mpeg2_scan_norm_ptable;
	} else {
	    scan = mpeg2_scan_alt_orig;
	    scan_ptable = mpeg2_scan_alt_ptable;
	}
    }
	    
    bit_buf = picture->bitstream_buf;
    bits = picture->bitstream_bits;
    bit_ptr = picture->bitstream_ptr;

    NEEDBITS (bit_buf, bits, bit_ptr);
    if (bit_buf >= 0x28000000) {
	tab = DCT_B14DC_5 + (UBITS (bit_buf, 5) - 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bit_buf, 5) - 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    l = scan_ptable[j = scan[i]];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = ((2*tab->level+1) * quantizer_scale * quant_matrix[l]) >> 5;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	}

    entry_2:
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bit_buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

            i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    l = scan_ptable[j = scan[i]];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    val = 2 * (SBITS (bit_buf, 12) + SBITS (bit_buf, 1)) + 1;
	    val = (val * quantizer_scale * quant_matrix[l]) / 32;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bit_buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bit_buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bit_buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    GETWORD (bit_buf, bits + 16, bit_ptr);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 2);	/* dump end of block code */
    picture->bitstream_buf = bit_buf;
    picture->bitstream_bits = bits;
    picture->bitstream_ptr = bit_ptr;
}

static void get_xvmc_mpeg1_intra_block (picture_t * picture)
{
    int i;
    int j;
    int l;
    int val;
    const uint8_t * scan = picture->scan;
    uint8_t * scan_ptable = mpeg2_scan_orig_ptable;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = picture->quantizer_scale;
    const DCTtab * tab;
    uint32_t bit_buf;
    int bits;
    uint8_t * bit_ptr;
    int16_t * dest;

    i = 0;

    dest = picture->mc->blockptr;

    if( picture->mc->xvmc_accel & IDCT_ACCEL ) {
	if ( scan == mpeg2_scan_norm ) {
	    scan =  mpeg2_scan_norm_orig; 
	    scan_ptable = mpeg2_scan_norm_ptable;
	} else {
	    scan = mpeg2_scan_alt_orig;
	    scan_ptable = mpeg2_scan_alt_ptable;
	}
    }
	    
    bit_buf = picture->bitstream_buf;
    bits = picture->bitstream_bits;
    bit_ptr = picture->bitstream_ptr;

    NEEDBITS (bit_buf, bits, bit_ptr);

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bit_buf, 5) - 5);

	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    l = scan_ptable[j = scan[i]];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = (tab->level * quantizer_scale * quant_matrix[l]) >> 4;

	    /* oddification */
	    val = (val - 1) | 1;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bit_buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

            i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    l = scan_ptable[j = scan[i]];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    val = SBITS (bit_buf, 8);
	    if (! (val & 0x7f)) {
		DUMPBITS (bit_buf, bits, 8);
		val = UBITS (bit_buf, 8) + 2 * val;
	    }
	    val = (val * quantizer_scale * quant_matrix[l]) / 16;

	    /* oddification */
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    DUMPBITS (bit_buf, bits, 8);
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bit_buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bit_buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bit_buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    GETWORD (bit_buf, bits + 16, bit_ptr);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    DUMPBITS (bit_buf, bits, 2);	/* dump end of block code */
    picture->bitstream_buf = bit_buf;
    picture->bitstream_bits = bits;
    picture->bitstream_ptr = bit_ptr;
}

static void get_xvmc_mpeg1_non_intra_block (picture_t * picture)
{
    int i;
    int j;
    int l;
    int val;
    const uint8_t * scan = picture->scan;
    uint8_t * scan_ptable = mpeg2_scan_orig_ptable;
    uint8_t * quant_matrix = picture->non_intra_quantizer_matrix;
    int quantizer_scale = picture->quantizer_scale;
    const DCTtab * tab;
    uint32_t bit_buf;
    int bits;
    uint8_t * bit_ptr;
    int16_t * dest;

    i = -1;

    dest = picture->mc->blockptr;

    if( picture->mc->xvmc_accel & IDCT_ACCEL ) {
	if ( scan == mpeg2_scan_norm ) {
	    scan =  mpeg2_scan_norm_orig; 
	    scan_ptable = mpeg2_scan_norm_ptable;
	} else {
	    scan = mpeg2_scan_alt_orig;
	    scan_ptable = mpeg2_scan_alt_ptable;
	}
    }
	    
    bit_buf = picture->bitstream_buf;
    bits = picture->bitstream_bits;
    bit_ptr = picture->bitstream_ptr;

    NEEDBITS (bit_buf, bits, bit_ptr);
    if (bit_buf >= 0x28000000) {
	tab = DCT_B14DC_5 + (UBITS (bit_buf, 5) - 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 + (UBITS (bit_buf, 5) - 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	/* end of block */

	normal_code:
	    l = scan_ptable[j = scan[i]];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = ((2*tab->level+1) * quantizer_scale * quant_matrix[l]) >> 5;

	    /* oddification */
	    val = (val - 1) | 1;

	    /* if (bitstream_get (1)) val = -val; */
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	}

    entry_2:
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 + (UBITS (bit_buf, 8) - 4);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    /* escape code */

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	/* illegal, check needed to avoid buffer overflow */

	    l = scan_ptable[j = scan[i]];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    val = SBITS (bit_buf, 8);
	    if (! (val & 0x7f)) {
		DUMPBITS (bit_buf, bits, 8);
		val = UBITS (bit_buf, 8) + 2 * val;
	    }
	    val = 2 * (val + SBITS (val, 1)) + 1;
	    val = (val * quantizer_scale * quant_matrix[l]) / 32;

	    /* oddification */
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    DUMPBITS (bit_buf, bits, 8);
	    NEEDBITS (bit_buf, bits, bit_ptr);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 + (UBITS (bit_buf, 10) - 8);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 + (UBITS (bit_buf, 13) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 + (UBITS (bit_buf, 15) - 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    GETWORD (bit_buf, bits + 16, bit_ptr);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	/* illegal, check needed to avoid buffer overflow */
    }
    DUMPBITS (bit_buf, bits, 2);	/* dump end of block code */
    picture->bitstream_buf = bit_buf;
    picture->bitstream_bits = bits;
    picture->bitstream_ptr = bit_ptr;
}

static inline void slice_xvmc_intra_DCT (picture_t * picture, int cc,
				    uint8_t * dest, int stride)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)  
#define bit_ptr (picture->bitstream_ptr)
    NEEDBITS (bit_buf, bits, bit_ptr);
    /* Get the intra DC coefficient and inverse quantize it */

    //    printf("slice: slice_xvmc_intra_DCT cc=%d pred[0]=%d\n",cc,picture->dc_dct_pred[0]);
    if (cc == 0)
	picture->dc_dct_pred[0] += get_xvmc_luma_dc_dct_diff (picture);
    else
	picture->dc_dct_pred[cc] += get_xvmc_chroma_dc_dct_diff (picture);
    //TODO conversion to signed format 
    //    printf("slice:  pred[0]=%d presision=%d\n",picture->dc_dct_pred[0],
    //       picture->intra_dc_precision);

    mpeg2_zero_block(picture->mc->blockptr);

    picture->mc->blockptr[0] = picture->dc_dct_pred[cc] << (3 - picture->intra_dc_precision);

    if (picture->mpeg1) {
	if (picture->picture_coding_type != D_TYPE)
	    get_xvmc_mpeg1_intra_block (picture);
    } else if (picture->intra_vlc_format)
	get_xvmc_intra_block_B15 (picture);
    else
	get_xvmc_intra_block_B14 (picture);

    if((picture->mc->xvmc_accel & ACCEL) == MOTION_ACCEL) {
        //motion_comp only no idct acceleration so do it in software
        mpeg2_idct (picture->mc->blockptr);
    }
    picture->mc->blockptr += 64;
#undef bit_buf
#undef bits
#undef bit_ptr
}

static inline void slice_xvmc_non_intra_DCT (picture_t * picture, uint8_t * dest,
					int stride)
{
  mpeg2_zero_block(picture->mc->blockptr);

    if (picture->mpeg1)
	get_xvmc_mpeg1_non_intra_block (picture);
    else
	get_xvmc_non_intra_block (picture);

    if((picture->mc->xvmc_accel & ACCEL) == MOTION_ACCEL) {
      // motion comp only no idct acceleration so do it in sw
      mpeg2_idct (picture->mc->blockptr);
    }
    picture->mc->blockptr += 64;
}

static void motion_mp1 (picture_t * picture, motion_t * motion,
			void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_x = (motion->pmv[0][0] +
		(get_xvmc_motion_delta (picture,
				   motion->f_code[0]) << motion->f_code[1]));
    motion_x = bound_motion_vector (motion_x,
				    motion->f_code[0] + motion->f_code[1]);
    motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = (motion->pmv[0][1] +
		(get_xvmc_motion_delta (picture,
				   motion->f_code[0]) << motion->f_code[1]));
    motion_y = bound_motion_vector (motion_y,
				    motion->f_code[0] + motion->f_code[1]);
    motion->pmv[0][1] = motion_y;

#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fr_frame (picture_t * picture, motion_t * motion,
			     void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = motion->pmv[0][1] + get_xvmc_motion_delta (picture,
						     motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;

#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fr_field (picture_t * picture, motion_t * motion,
			     void (** table) (uint8_t *, uint8_t *, int, int),
			     int dir)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y, field;
    //    unsigned int pos_x, pos_y, xy_half;

    NEEDBITS (bit_buf, bits, bit_ptr);
    field = UBITS (bit_buf, 1);
    picture->XvMC_mv_field_sel[0][dir] = field;
    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = (motion->pmv[0][1] >> 1) + get_xvmc_motion_delta (picture,
							    motion->f_code[1]);
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */
    motion->pmv[0][1] = motion_y << 1;

    NEEDBITS (bit_buf, bits, bit_ptr);
    field = UBITS (bit_buf, 1);
    //TODO look at field select need bob  (weave ok)
    picture->XvMC_mv_field_sel[1][dir] = field;
    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[1][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = (motion->pmv[1][1] >> 1) + get_xvmc_motion_delta (picture,
							    motion->f_code[1]);
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */
    motion->pmv[1][1] = motion_y << 1;

#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fr_dmv (picture_t * picture, motion_t * motion,
			   void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;

    // TODO field select ?? possible need to be 0
    picture->XvMC_mv_field_sel[0][0] = picture->XvMC_mv_field_sel[1][0] = 0;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;
    NEEDBITS (bit_buf, bits, bit_ptr);

    motion_y = (motion->pmv[0][1] >> 1) + get_xvmc_motion_delta (picture,
							    motion->f_code[1]);
    /* motion_y = bound_motion_vector (motion_y, motion->f_code[1]); */
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y << 1;

#undef bit_buf
#undef bits
#undef bit_ptr
}

#if 0
static void motion_reuse (picture_t * picture, motion_t * motion,
			  void (** table) (uint8_t *, uint8_t *, int, int))
{
    int motion_x, motion_y;

    motion_x = motion->pmv[0][0];
    motion_y = motion->pmv[0][1];

}
#endif

/* like motion_frame, but parsing without actual motion compensation */
static void motion_fr_conceal (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int tmp;

    NEEDBITS (bit_buf, bits, bit_ptr);
    tmp = (picture->f_motion.pmv[0][0] +
	   get_xvmc_motion_delta (picture, picture->f_motion.f_code[0]));
    tmp = bound_motion_vector (tmp, picture->f_motion.f_code[0]);
    picture->f_motion.pmv[1][0] = picture->f_motion.pmv[0][0] = tmp;

    NEEDBITS (bit_buf, bits, bit_ptr);
    tmp = (picture->f_motion.pmv[0][1] +
	   get_xvmc_motion_delta (picture, picture->f_motion.f_code[1]));
    tmp = bound_motion_vector (tmp, picture->f_motion.f_code[1]);
    picture->f_motion.pmv[1][1] = picture->f_motion.pmv[0][1] = tmp;

    DUMPBITS (bit_buf, bits, 1); /* remove marker_bit */
#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fi_field (picture_t * picture, motion_t * motion,
			     void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;
    /*uint8_t ** ref_field;*/

    NEEDBITS (bit_buf, bits, bit_ptr);
    /*ref_field = motion->ref2[UBITS (bit_buf, 1)];*/

    // TODO field select may need to do something here for bob (weave ok)
    picture->XvMC_mv_field_sel[0][0] = picture->XvMC_mv_field_sel[1][0] = 0;

    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = motion->pmv[0][1] + get_xvmc_motion_delta (picture,
						     motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;

#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fi_16x8 (picture_t * picture, motion_t * motion,
			    void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;
    /*uint8_t ** ref_field;*/

    NEEDBITS (bit_buf, bits, bit_ptr);
    /*ref_field = motion->ref2[UBITS (bit_buf, 1)];*/

    // TODO field select may need to do something here bob  (weave ok)
    picture->XvMC_mv_field_sel[0][0] = picture->XvMC_mv_field_sel[1][0] = 0;

    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = motion->pmv[0][1] + get_xvmc_motion_delta (picture,
						     motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[0][1] = motion_y;


    NEEDBITS (bit_buf, bits, bit_ptr);
    /*ref_field = motion->ref2[UBITS (bit_buf, 1)];*/

    // TODO field select may need to do something here for bob (weave ok)
    picture->XvMC_mv_field_sel[0][0] = picture->XvMC_mv_field_sel[1][0] = 0;

    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[1][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion_x;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_y = motion->pmv[1][1] + get_xvmc_motion_delta (picture,
						     motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion_y;

#undef bit_buf
#undef bits
#undef bit_ptr
}

static void motion_fi_dmv (picture_t * picture, motion_t * motion,
			   void (** table) (uint8_t *, uint8_t *, int, int))
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int motion_x, motion_y;

    NEEDBITS (bit_buf, bits, bit_ptr);
    motion_x = motion->pmv[0][0] + get_xvmc_motion_delta (picture,
						     motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;
    NEEDBITS (bit_buf, bits, bit_ptr);

    motion_y = motion->pmv[0][1] + get_xvmc_motion_delta (picture,
						     motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;

    // TODO field select may need to do something here for bob  (weave ok)
    picture->XvMC_mv_field_sel[0][0] = picture->XvMC_mv_field_sel[1][0] = 0;

#undef bit_buf
#undef bits
#undef bit_ptr
}


static void motion_fi_conceal (picture_t * picture)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int tmp;

    NEEDBITS (bit_buf, bits, bit_ptr);
    DUMPBITS (bit_buf, bits, 1); /* remove field_select */

    tmp = (picture->f_motion.pmv[0][0] +
	   get_xvmc_motion_delta (picture, picture->f_motion.f_code[0]));
    tmp = bound_motion_vector (tmp, picture->f_motion.f_code[0]);
    picture->f_motion.pmv[1][0] = picture->f_motion.pmv[0][0] = tmp;

    NEEDBITS (bit_buf, bits, bit_ptr);
    tmp = (picture->f_motion.pmv[0][1] +
	   get_xvmc_motion_delta (picture, picture->f_motion.f_code[1]));
    tmp = bound_motion_vector (tmp, picture->f_motion.f_code[1]);
    picture->f_motion.pmv[1][1] = picture->f_motion.pmv[0][1] = tmp;

    DUMPBITS (bit_buf, bits, 1); /* remove marker_bit */
#undef bit_buf
#undef bits
#undef bit_ptr
}

#define MOTION_CALL(routine,direction)				\
do {								\
    if ((direction) & MACROBLOCK_MOTION_FORWARD)		\
	routine (picture, &(picture->f_motion), mpeg2_mc.put);	\
    if ((direction) & MACROBLOCK_MOTION_BACKWARD)		\
	routine (picture, &(picture->b_motion),			\
		 ((direction) & MACROBLOCK_MOTION_FORWARD ?	\
		  mpeg2_mc.avg : mpeg2_mc.put));		\
} while (0)

#define NEXT_MACROBLOCK							    \
do {									    \
    picture->offset += 16;						    \
    if (picture->offset == picture->coded_picture_width) {		    \
	do { /* just so we can use the break statement */		    \
	    if (picture->current_frame->proc_slice) {			    \
		picture->current_frame->proc_slice (picture->current_frame, \
					      picture->dest);		    \
		if (picture->picture_coding_type == B_TYPE)		    \
		    break;						    \
	    }								    \
	    picture->dest[0] += 16 * picture->pitches[0];		    \
	    picture->dest[1] += 8 * picture->pitches[1];		    \
	    picture->dest[2] += 8 * picture->pitches[2];		    \
	} while (0);							    \
	picture->v_offset += 16;					    \
	if (picture->v_offset > picture->limit_y) {			    \
	    if (mpeg2_cpu_state_restore)				    \
		mpeg2_cpu_state_restore (&cpu_state);			    \
	    return;							    \
	}								    \
	picture->offset = 0;						    \
    }									    \
} while (0)

static inline int slice_xvmc_init (picture_t * picture, int code)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    int offset, height;
    struct vo_frame_s * forward_reference_frame;
    struct vo_frame_s * backward_reference_frame;
    const MBAtab * mba;

    offset = picture->picture_structure == BOTTOM_FIELD;
    picture->pitches[0] = picture->current_frame->pitches[0];
    picture->pitches[1] = picture->current_frame->pitches[1];
    picture->pitches[2] = picture->current_frame->pitches[2];

    if( picture->forward_reference_frame ) {
        forward_reference_frame = picture->forward_reference_frame;
    }
    else {
        /* return 1; */
        forward_reference_frame = picture->current_frame;
    }
    
    if( picture->backward_reference_frame ) {
        backward_reference_frame = picture->backward_reference_frame;
    }
    else {
        /* return 1; */
        backward_reference_frame = picture->current_frame;
    }
    
    picture->f_motion.ref[0][0] =
        forward_reference_frame->base[0] + (offset ? picture->pitches[0] : 0);
    picture->f_motion.ref[0][1] =
        forward_reference_frame->base[1] + (offset ? picture->pitches[1] : 0);
    picture->f_motion.ref[0][2] =
        forward_reference_frame->base[2] + (offset ? picture->pitches[2] : 0);
    
    picture->b_motion.ref[0][0] =
	backward_reference_frame->base[0] + (offset ? picture->pitches[0] : 0);
    picture->b_motion.ref[0][1] =
	backward_reference_frame->base[1] + (offset ? picture->pitches[1] : 0);
    picture->b_motion.ref[0][2] =
	backward_reference_frame->base[2] + (offset ? picture->pitches[2] : 0);
    
    if (picture->picture_structure != FRAME_PICTURE) {
	uint8_t ** forward_ref;
	int bottom_field;

	bottom_field = (picture->picture_structure == BOTTOM_FIELD);
	picture->dmv_offset = bottom_field ? 1 : -1;
	picture->f_motion.ref2[0] = picture->f_motion.ref[bottom_field];
	picture->f_motion.ref2[1] = picture->f_motion.ref[!bottom_field];
	picture->b_motion.ref2[0] = picture->b_motion.ref[bottom_field];
	picture->b_motion.ref2[1] = picture->b_motion.ref[!bottom_field];

	forward_ref = forward_reference_frame->base;
	if (picture->second_field && (picture->picture_coding_type != B_TYPE))
	    forward_ref = picture->current_frame->base;

	picture->f_motion.ref[1][0] = forward_ref[0] + (bottom_field ? 0 : picture->pitches[0]);
	picture->f_motion.ref[1][1] = forward_ref[1] + (bottom_field ? 0 : picture->pitches[1]);
	picture->f_motion.ref[1][2] = forward_ref[2] + (bottom_field ? 0 : picture->pitches[2]);

	picture->b_motion.ref[1][0] =
	    backward_reference_frame->base[0] + (bottom_field ? 0 : picture->pitches[0]);
	picture->b_motion.ref[1][1] =
	    backward_reference_frame->base[1] + (bottom_field ? 0 : picture->pitches[1]);
	picture->b_motion.ref[1][2] =
	    backward_reference_frame->base[2] + (bottom_field ? 0 : picture->pitches[2]);
    }

    picture->f_motion.pmv[0][0] = picture->f_motion.pmv[0][1] = 0;
    picture->f_motion.pmv[1][0] = picture->f_motion.pmv[1][1] = 0;
    picture->b_motion.pmv[0][0] = picture->b_motion.pmv[0][1] = 0;
    picture->b_motion.pmv[1][0] = picture->b_motion.pmv[1][1] = 0;

    picture->v_offset = (code - 1) * 16;
    offset = (code - 1);
    if (picture->current_frame->proc_slice && picture->picture_coding_type == B_TYPE)
	offset = 0;
    else if (picture->picture_structure != FRAME_PICTURE)
	offset = 2 * offset;

    picture->dest[0] = picture->current_frame->base[0] + picture->pitches[0] * offset * 16;
    picture->dest[1] = picture->current_frame->base[1] + picture->pitches[1] * offset * 8;
    picture->dest[2] = picture->current_frame->base[2] + picture->pitches[2] * offset * 8;

    height = picture->coded_picture_height;
    switch (picture->picture_structure) {
    case BOTTOM_FIELD:
	picture->dest[0] += picture->pitches[0];
	picture->dest[1] += picture->pitches[1];
	picture->dest[2] += picture->pitches[2];
	/* follow thru */
    case TOP_FIELD:
	picture->pitches[0] <<= 1;
	picture->pitches[1] <<= 1;
	picture->pitches[2] <<= 1;
	height >>= 1;
    }
    picture->limit_x = 2 * picture->coded_picture_width - 32;
    picture->limit_y_16 = 2 * height - 32;
    picture->limit_y_8 = 2 * height - 16;
    picture->limit_y = height - 16;

    //TODO conversion to signed format signed format
    if((picture->mc->xvmc_accel & ACCEL) == MOTION_ACCEL &&
       !(picture->mc->xvmc_accel & SIGNED_INTRA)) {
      //Motion Comp only unsigned intra
      // original:
      picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
	picture->dc_dct_pred[2] = 1 << (picture->intra_dc_precision + 7);
    } else {
      //Motion Comp only signed intra  MOTION_ACCEL+SIGNED_INTRA
      picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
	picture->dc_dct_pred[2] = 0;
    }

    picture->quantizer_scale = get_xvmc_quantizer_scale (picture);

    /* ignore intra_slice and all the extra data */
    while (bit_buf & 0x80000000) {
	DUMPBITS (bit_buf, bits, 9);
	NEEDBITS (bit_buf, bits, bit_ptr);
    }

    /* decode initial macroblock address increment */
    offset = 0;
    while (1) {
	if (bit_buf >= 0x08000000) {
	    mba = MBA_5 + (UBITS (bit_buf, 6) - 2);
	    break;
	} else if (bit_buf >= 0x01800000) {
	    mba = MBA_11 + (UBITS (bit_buf, 12) - 24);
	    break;
	} else switch (UBITS (bit_buf, 12)) {
	case 8:		/* macroblock_escape */
	    offset += 33;
	    DUMPBITS (bit_buf, bits, 11);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    continue;
	case 15:	/* macroblock_stuffing (MPEG1 only) */
	    bit_buf &= 0xfffff;
	    DUMPBITS (bit_buf, bits, 11);
	    NEEDBITS (bit_buf, bits, bit_ptr);
	    continue;
	default:	/* error */
	    return 1;
	}
    }
    DUMPBITS (bit_buf, bits, mba->len + 1);
    picture->offset = (offset + mba->mba) << 4;

    while (picture->offset - picture->coded_picture_width >= 0) {
	picture->offset -= picture->coded_picture_width;
	if ((picture->current_frame->proc_slice == NULL) ||
	    (picture->picture_coding_type != B_TYPE)) {
	    picture->dest[0] += 16 * picture->pitches[0];
	    picture->dest[1] += 8 * picture->pitches[1];
	    picture->dest[2] += 8 * picture->pitches[2];
	}
	picture->v_offset += 16;
    }
    if (picture->v_offset > picture->limit_y)
	return 1;

    return 0;
#undef bit_buf
#undef bits
#undef bit_ptr
}

void mpeg2_xvmc_slice (mpeg2dec_accel_t *accel, picture_t * picture, int code, uint8_t * buffer)
{
#define bit_buf (picture->bitstream_buf)
#define bits (picture->bitstream_bits)
#define bit_ptr (picture->bitstream_ptr)
    cpu_state_t cpu_state;
    xine_xvmc_t *xvmc = (xine_xvmc_t *) picture->current_frame->accel_data;

    if (1 == code) {
      accel->xvmc_last_slice_code = 0;
    }
    if ((code != accel->xvmc_last_slice_code + 1) &&
	(code != accel->xvmc_last_slice_code))
	return;
    
    bitstream_init (picture, buffer);

    if (slice_xvmc_init (picture, code))
	return;

    if (mpeg2_cpu_state_save)
	mpeg2_cpu_state_save (&cpu_state);

    while (1) {
	int macroblock_modes;
	int mba_inc;
	const MBAtab * mba;

	NEEDBITS (bit_buf, bits, bit_ptr);

	macroblock_modes = get_xvmc_macroblock_modes (picture); //macroblock_modes()
	picture->XvMC_mb_type = macroblock_modes & 0x1F;
	picture->XvMC_dct_type = (macroblock_modes & DCT_TYPE_INTERLACED)>>5;
	picture->XvMC_motion_type = (macroblock_modes & MOTION_TYPE_MASK)>>6;

	picture->XvMC_x = picture->offset/16;
	picture->XvMC_y = picture->v_offset/16;

	if((picture->XvMC_x == 0) && (picture->XvMC_y == 0)) {
	  picture->XvMC_mv_field_sel[0][0] = 
	    picture->XvMC_mv_field_sel[1][0] = 
	    picture->XvMC_mv_field_sel[0][1] = 
	    picture->XvMC_mv_field_sel[1][1] = 0;
	}

	picture->XvMC_cbp = 0x3f;  //TODO set for intra 4:2:0 6 blocks yyyyuv all enabled

	/* maybe integrate MACROBLOCK_QUANT test into get_xvmc_macroblock_modes ? */
	if (macroblock_modes & MACROBLOCK_QUANT)
	    picture->quantizer_scale = get_xvmc_quantizer_scale (picture);
	if (macroblock_modes & MACROBLOCK_INTRA) {

	    int DCT_offset, DCT_stride;
	    int offset;
	    uint8_t * dest_y;

	    if (picture->concealment_motion_vectors) {
		if (picture->picture_structure == FRAME_PICTURE)
		    motion_fr_conceal (picture);
		else
		    motion_fi_conceal (picture);
	    } else {
		picture->f_motion.pmv[0][0] = picture->f_motion.pmv[0][1] = 0;
		picture->f_motion.pmv[1][0] = picture->f_motion.pmv[1][1] = 0;
		picture->b_motion.pmv[0][0] = picture->b_motion.pmv[0][1] = 0;
		picture->b_motion.pmv[1][0] = picture->b_motion.pmv[1][1] = 0;
	    }

	    if (macroblock_modes & DCT_TYPE_INTERLACED) {
		DCT_offset = picture->pitches[0];
		DCT_stride = picture->pitches[0] * 2;
	    } else {
		DCT_offset = picture->pitches[0] * 8;
		DCT_stride = picture->pitches[0];
	    }
	    offset = picture->offset;
	    dest_y = picture->dest[0] + offset;
	    // unravaled loop of 6 block(i) calls in macroblock()
	    slice_xvmc_intra_DCT (picture, 0, dest_y, DCT_stride);
	    slice_xvmc_intra_DCT (picture, 0, dest_y + 8, DCT_stride);
	    slice_xvmc_intra_DCT (picture, 0, dest_y + DCT_offset, DCT_stride);
	    slice_xvmc_intra_DCT (picture, 0, dest_y + DCT_offset + 8, DCT_stride);
	    slice_xvmc_intra_DCT (picture, 1, picture->dest[1] + (offset >> 1),
			     picture->pitches[1]);
	    slice_xvmc_intra_DCT (picture, 2, picture->dest[2] + (offset >> 1),
			     picture->pitches[2]);

	    if (picture->picture_coding_type == D_TYPE) {
		NEEDBITS (bit_buf, bits, bit_ptr);
		DUMPBITS (bit_buf, bits, 1);
	    }
	} else {
	    picture->XvMC_cbp = 0;

	    if (picture->picture_structure == FRAME_PICTURE)
		switch (macroblock_modes & MOTION_TYPE_MASK) {
		case MC_FRAME:
		    if (picture->mpeg1) {
			MOTION_CALL (motion_mp1, macroblock_modes);
		    } else {
			MOTION_CALL (motion_fr_frame, macroblock_modes);
		    }
		    break;

		case MC_FIELD:
		    //MOTION_CALL (motion_fr_field, macroblock_modes);

		    if ((macroblock_modes) & MACROBLOCK_MOTION_FORWARD)
		      motion_fr_field(picture, &(picture->f_motion),
				       mpeg2_mc.put,0);
		    if ((macroblock_modes) & MACROBLOCK_MOTION_BACKWARD)
		      motion_fr_field(picture, &(picture->b_motion),
			     ((macroblock_modes) & MACROBLOCK_MOTION_FORWARD ?
				mpeg2_mc.avg : mpeg2_mc.put),1);

		    break;

		case MC_DMV:
		    MOTION_CALL (motion_fr_dmv, MACROBLOCK_MOTION_FORWARD);
		    break;

		case 0:
		    /* non-intra mb without forward mv in a P picture */
		    picture->f_motion.pmv[0][0] = 0;
		    picture->f_motion.pmv[0][1] = 0;
		    picture->f_motion.pmv[1][0] = 0;
		    picture->f_motion.pmv[1][1] = 0;
		    //	 MOTION_CALL (motion_zero, MACROBLOCK_MOTION_FORWARD);
		    break;
		}
	    else
		switch (macroblock_modes & MOTION_TYPE_MASK) {
		case MC_FIELD:
		    MOTION_CALL (motion_fi_field, macroblock_modes);
		    break;

		case MC_16X8:
		    MOTION_CALL (motion_fi_16x8, macroblock_modes);
		    break;

		case MC_DMV:
		    MOTION_CALL (motion_fi_dmv, MACROBLOCK_MOTION_FORWARD);
		    break;

		case 0:
		    /* non-intra mb without forward mv in a P picture */
		    picture->f_motion.pmv[0][0] = 0;
		    picture->f_motion.pmv[0][1] = 0;
		    picture->f_motion.pmv[1][0] = 0;
		    picture->f_motion.pmv[1][1] = 0;
		    //	 MOTION_CALL (motion_zero, MACROBLOCK_MOTION_FORWARD);
		    break;
		}

	    if (macroblock_modes & MACROBLOCK_PATTERN) {
		int coded_block_pattern;
		int DCT_offset, DCT_stride;
		int offset;
		uint8_t * dest_y;

		if (macroblock_modes & DCT_TYPE_INTERLACED) {
		    DCT_offset = picture->pitches[0];
		    DCT_stride = picture->pitches[0] * 2;
		} else {
		    DCT_offset = picture->pitches[0] * 8;
		    DCT_stride = picture->pitches[0];
		}

		picture->XvMC_cbp = coded_block_pattern = get_xvmc_coded_block_pattern (picture);
		offset = picture->offset;
		dest_y = picture->dest[0] + offset;
		// TODO  optimize not fully used for idct accel only mc.
		if (coded_block_pattern & 0x20)
		    slice_xvmc_non_intra_DCT (picture, dest_y, DCT_stride); //  cc0  luma 0
		if (coded_block_pattern & 0x10)
		    slice_xvmc_non_intra_DCT (picture, dest_y + 8, DCT_stride); // cc0 luma 1
		if (coded_block_pattern & 0x08)
		    slice_xvmc_non_intra_DCT (picture, dest_y + DCT_offset,
					 DCT_stride); // cc0 luma 2
		if (coded_block_pattern & 0x04)
		    slice_xvmc_non_intra_DCT (picture, dest_y + DCT_offset + 8,
					 DCT_stride); // cc0 luma 3
		if (coded_block_pattern & 0x2)
		    slice_xvmc_non_intra_DCT (picture,
					 picture->dest[1] + (offset >> 1),
					 picture->pitches[1]); // cc1 croma 
		if (coded_block_pattern & 0x1)
		    slice_xvmc_non_intra_DCT (picture,
					 picture->dest[2] + (offset >> 1),
					 picture->pitches[2]); // cc2 croma
	    }

            if((picture->mc->xvmc_accel & ACCEL) == MOTION_ACCEL &&
	       !(picture->mc->xvmc_accel & SIGNED_INTRA)) {
	        // original:
	        picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
		    picture->dc_dct_pred[2] = 128 << picture->intra_dc_precision;

	    } else { // MOTION_ACCEL+SIGNED_INTRA
	        picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
		    picture->dc_dct_pred[2] = 0;
	    }

	}
        xvmc->proc_macro_block(picture->XvMC_x, picture->XvMC_y,
					 picture->XvMC_mb_type,
					 picture->XvMC_motion_type,
					 picture->XvMC_mv_field_sel,
					 picture->XvMC_dmvector,
					 picture->XvMC_cbp,
					 picture->XvMC_dct_type,
					 picture->current_frame,
					 picture->forward_reference_frame,
					 picture->backward_reference_frame,
					 picture->picture_structure,
					 picture->second_field,
				         picture->f_motion.pmv,
				         picture->b_motion.pmv);


	NEXT_MACROBLOCK;

	NEEDBITS (bit_buf, bits, bit_ptr);
	mba_inc = 0;
	while (1) {
	    if (bit_buf >= 0x10000000) {
		mba = MBA_5 + (UBITS (bit_buf, 5) - 2);
		break;
	    } else if (bit_buf >= 0x03000000) {
		mba = MBA_11 + (UBITS (bit_buf, 11) - 24);
		break;
	    } else switch (UBITS (bit_buf, 11)) {
	    case 8:		/* macroblock_escape */
		mba_inc += 33;
		/* pass through */
	    case 15:	/* macroblock_stuffing (MPEG1 only) */
		DUMPBITS (bit_buf, bits, 11);
		NEEDBITS (bit_buf, bits, bit_ptr);
		continue;
	    default:	/* end of slice, or error */
		if (mpeg2_cpu_state_restore)
		    mpeg2_cpu_state_restore (&cpu_state);
		accel->xvmc_last_slice_code = code;
		return;
	    }
	}
	DUMPBITS (bit_buf, bits, mba->len);
	mba_inc += mba->mba;
	if (mba_inc) {
	    //TODO  conversion to signed format signed format
          if((picture->mc->xvmc_accel & ACCEL) == MOTION_ACCEL &&
	     !(picture->mc->xvmc_accel & SIGNED_INTRA)) {
	    // original:
	    picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
	      picture->dc_dct_pred[2] = 128 << picture->intra_dc_precision;
	  } else { // MOTION_ACCEL+SIGNED_INTRA
	    picture->dc_dct_pred[0] = picture->dc_dct_pred[1] =
	      picture->dc_dct_pred[2] = 0;
	  }

	    picture->XvMC_cbp = 0; 
	    if (picture->picture_coding_type == P_TYPE) {
		picture->f_motion.pmv[0][0] = picture->f_motion.pmv[0][1] = 0;
		picture->f_motion.pmv[1][0] = picture->f_motion.pmv[1][1] = 0;

		do {
		    if(picture->mc->xvmc_accel) {

		        /* derive motion_type */
		        if(picture->picture_structure == FRAME_PICTURE) {
			  picture->XvMC_motion_type = XINE_MC_FRAME;
			} else {
			  picture->XvMC_motion_type = XINE_MC_FIELD;
			  /* predict from field of same parity */
			  picture->XvMC_mv_field_sel[0][0] =
			    picture->XvMC_mv_field_sel[0][1] =
			      (picture->picture_structure==BOTTOM_FIELD);
			}
			picture->XvMC_mb_type = macroblock_modes & 0x1E;
			picture->XvMC_x = picture->offset/16;
			picture->XvMC_y = picture->v_offset/16;

			xvmc->proc_macro_block(picture->XvMC_x,picture->XvMC_y,
					 picture->XvMC_mb_type,
					 picture->XvMC_motion_type,
					 picture->XvMC_mv_field_sel,
					 picture->XvMC_dmvector,
					 picture->XvMC_cbp,
					 picture->XvMC_dct_type,
					 picture->current_frame,
					 picture->forward_reference_frame,
					 picture->backward_reference_frame,
					 picture->picture_structure,
					 picture->second_field,
				         picture->f_motion.pmv,
				         picture->b_motion.pmv);
		    } else {
		      // MOTION_CALL (motion_zero, MACROBLOCK_MOTION_FORWARD);
		    }
		    NEXT_MACROBLOCK;
		} while (--mba_inc);
	    } else {
		do {
		    if(picture->mc->xvmc_accel) {

		        /* derive motion_type */
		        if(picture->picture_structure == FRAME_PICTURE) {
			  picture->XvMC_motion_type = XINE_MC_FRAME;
			} else {
			  picture->XvMC_motion_type = XINE_MC_FIELD;
			  /* predict from field of same parity */
			  picture->XvMC_mv_field_sel[0][0] =
			    picture->XvMC_mv_field_sel[0][1] =
			      (picture->picture_structure==BOTTOM_FIELD);
			}

			picture->XvMC_mb_type = macroblock_modes & 0x1E;
			picture->XvMC_x = picture->offset/16;
			picture->XvMC_y = picture->v_offset/16;

			xvmc->proc_macro_block(picture->XvMC_x,picture->XvMC_y,
					 picture->XvMC_mb_type,
					 picture->XvMC_motion_type,
					 picture->XvMC_mv_field_sel,
					 picture->XvMC_dmvector,
					 picture->XvMC_cbp,
					 picture->XvMC_dct_type,
					 picture->current_frame,
					 picture->forward_reference_frame,
					 picture->backward_reference_frame,
					 picture->picture_structure,
					 picture->second_field,
				         picture->f_motion.pmv,
				         picture->b_motion.pmv);
		    } else {
                        //MOTION_CALL (motion_reuse, macroblock_modes);
		    }
		    NEXT_MACROBLOCK;
		} while (--mba_inc);
	    }
	}
    }
    accel->xvmc_last_slice_code = code;
#undef bit_buf
#undef bits
#undef bit_ptr
}

