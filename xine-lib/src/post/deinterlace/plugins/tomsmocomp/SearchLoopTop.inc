// -*- c++ -*-

unsigned char* pDest;
const unsigned char* pSrcP;
const unsigned char* pSrc;
const unsigned char* pBob;
const unsigned char* pBobP;

// long is int32 on ARCH_368, int64 on ARCH_AMD64. Declaring it this way
// saves a lot of xor's to delete 64bit garbage.

#if defined(DBL_RESIZE) || defined(USE_FOR_DSCALER)
long	    src_pitch2 = src_pitch;			// even & odd lines are not longerleaved in DScaler
#else
long	    src_pitch2 = 2 * src_pitch;		// even & odd lines are longerleaved in Avisynth
#endif

long	    dst_pitch2 = 2 * dst_pitch;
long     y;

#ifdef IS_SSE2
long     Last8 = (rowsize-16);			// ofs to last 16 bytes in row for SSE2
#else
long     Last8 = (rowsize-8);			// ofs to last 8 bytes in row
#endif

long		dst_pitchw = dst_pitch; // local stor so asm can ref
	pSrc  = pWeaveSrc;			// polongs 1 weave line above
	pSrcP = pWeaveSrcP;			// " 

#ifdef DBL_RESIZE
	        
#ifdef USE_VERTICAL_FILTER
	pDest = pWeaveDest + dst_pitch2;
#else
	pDest = pWeaveDest + 3*dst_pitch;
#endif

#else

#ifdef USE_VERTICAL_FILTER
	pDest = pWeaveDest + dst_pitch;
#else
	pDest = pWeaveDest + dst_pitch2;
#endif

#endif

	if (TopFirst)
	{
		pBob = pCopySrc + src_pitch2;      // remember one weave line just copied previously
		pBobP = pCopySrcP + src_pitch2;
	}
	else
	{
		pBob =  pCopySrc;
		pBobP =  pCopySrcP;
	}

#ifndef _pBob
#define _pBob       "%0"
#define _src_pitch2 "%1"
#define _pDest      "%2"
#define _dst_pitchw "%3"
#define _Last8      "%4"
#define _pSrc       "%5"
#define _pSrcP      "%6"
#define _pBobP      "%7"
#define _olddx      "%8"
#define _UVMask     "%9"
#define _ShiftMask  "%10"
#define _FOURS      "%11"
#define _TENS       "%12"
#define _Max_Vals   "%13"
#define _Min_Vals   "%14"
#define _YMask      "%15"
#define _Max_Mov    "%16"
#define _ONES       "%17"
#define _DiffThres  "%18"
#endif

	for (y=1; y < FldHeight-1; y++)
	{
          // pretend it's indented -->>
        __asm__ __volatile__
            (
             // Loop general reg usage
             //
             // XAX - pBobP, then pDest 
             // XDX - pBob
             // XCX - src_pitch2
             // _olddx - current offset
             // XDI - prev weave pixels, 1 line up
             // XSI - next weave pixels, 1 line up

#ifdef IS_SSE2
             
             // sse2 code deleted for now

#else
             // simple bob first 8 bytes
             MOVX"	"_pBob",        %%"XDX"\n\t"
             MOVX"	"_src_pitch2",  %%"XCX"\n\t"

#ifdef USE_VERTICAL_FILTER
             "movq	    (%%"XDX"),        %%mm0\n\t"
             "movq	    (%%"XDX", %%"XCX"), %%mm1\n\t" //, qword ptr["XDX"+"XCX"]
             "movq	    %%mm0,          %%mm2\n\t"
             V_PAVGB ("%%mm2", "%%mm1", "%%mm3", _ShiftMask)		// halfway between
             V_PAVGB ("%%mm0", "%%mm2", "%%mm3", _ShiftMask)		// 1/4 way
             V_PAVGB ("%%mm1", "%%mm2", "%%mm3", _ShiftMask)		// 3/4 way
             MOVX"		"_pDest",       %%"XDI"\n\t"
             MOVX"		"_dst_pitchw",  %%"XAX"\n\t"
             V_MOVNTQ	("(%%"XDI")", "%%mm0")
             V_MOVNTQ	("(%%"XDI", %%"XAX")", "%%mm1") // qword ptr["XDI"+"XAX"], mm1

             // simple bob last 8 bytes
             MOVX"		"_Last8", %%"XSI"\n\t"
             MOVX"		%%"XSI", "_olddx"\n\t"
             ADDX"		%%"XDX", %%"XSI"\n\t"  // ["XDX"+"_olddx"]
             "movq	    (%%"XSI"), %%mm0\n\t"
             "movq	    (%%"XSI", %%"XCX"), %%mm1\n\t"    // qword ptr["XSI"+"XCX"]
             "movq	    %%mm0, %%mm2\n\t"
             V_PAVGB ("%%mm2", "%%mm1", "%%mm3", _ShiftMask)		// halfway between
             V_PAVGB ("%%mm0", "%%mm2", "%%mm3", _ShiftMask)		// 1/4 way
             V_PAVGB ("%%mm1", "%%mm2", "%%mm3", _ShiftMask)		// 3/4 way
             ADDX"		"_olddx", %%"XDI"\n\t"						// last 8 bytes of dest
             V_MOVNTQ	("%%"XDI"", "%%mm0")
             V_MOVNTQ	("(%%"XDI", %%"XAX")", "%%mm1") // qword ptr["XDI"+"XAX"], mm1)

#else
             "movq	(%%"XDX"), %%mm0\n\t"
             //		pavgb	mm0, qword ptr["XDX"+"XCX"]
             V_PAVGB ("%%mm0", "(%%"XDX", %%"XCX")", "%%mm2", _ShiftMask) // qword ptr["XDX"+"XCX"], mm2, ShiftMask)
             MOVX"		"_pDest", %%"XDI"\n\t"
             V_MOVNTQ	("(%%"XDI")", "%%mm0")

             // simple bob last 8 bytes
             MOVX"		"_Last8", %%"XSI"\n\t"
             MOVX"		%%"XSI", "_olddx"\n\t"
             ADDX"		%%"XDX", %%"XSI"\n\t" //"XSI", ["XDX"+"_olddx"]
             "movq	    (%%"XSI"), %%mm0\n\t"
             //		pavgb	mm0, qword ptr["XSI"+"XCX"]
             V_PAVGB	("%%mm0", "(%%"XSI", %%"XCX")", "%%mm2", _ShiftMask) // qword ptr["XSI"+"XCX"], mm2, ShiftMask)
             ADDX"		"_olddx", %%"XDI"\n\t"
             V_MOVNTQ	("(%%"XDI")", "%%mm0") // qword ptr["XDI"+"_olddx"], mm0)
#endif
             // now loop and get the middle qwords
             MOVX"		"_pSrc", %%"XSI"\n\t"
             MOVX"		"_pSrcP", %%"XDI"\n\t"
             MOVX"		$8, "_olddx"\n\t"				// curr offset longo all lines

             "1:\n\t"	
             MOVX"		"_pBobP", %%"XAX"\n\t"
             ADDX"		$8, %%"XDI"\n\t"
             ADDX"		$8, %%"XSI"\n\t"
             ADDX"		$8, %%"XDX"\n\t"
             ADDX"		"_olddx", %%"XAX"\n\t"

#ifdef USE_STRANGE_BOB
#include "StrangeBob.inc"
#else
#include "WierdBob.inc"
#endif

             // For non-SSE2:
             // through out most of the rest of this loop we will malongain
             //	mm4		our min bob value
             //	mm5		best weave pixels so far
             // mm6		our max Bob value 
             //	mm7		best weighted pixel ratings so far
             
             // We will keep a slight bias to using the weave pixels
             // from the current location, by rating them by the min distance
             // from the Bob value instead of the avg distance from that value.
             // our best and only rating so far
             "pcmpeqb	%%mm7, %%mm7\n\t"			// ffff, say we didn't find anything good yet

#endif
