/*
 * Copyright (C) 2004 the xine project
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

/*
 * IFF header file by Manfred Tremmel (Manfred.Tremmel@iiv.de)
 * Based on the information of the Amiga Developer CD
 */

#ifndef IFFP_IFF_H
#define IFFP_IFF_H

#define IFF_OKAY                        0L
#define IFF_CLIENT_ERROR                1L
#define IFF_NOFILE                      5L

#define FOURCC_CHUNK BE_FOURCC
#define IFF_16SV_CHUNK FOURCC_CHUNK('1', '6', 'S', 'V')
#define IFF_8SVX_CHUNK FOURCC_CHUNK('8', 'S', 'V', 'X')
#define IFF_ANFI_CHUNK FOURCC_CHUNK('A', 'N', 'F', 'I')
#define IFF_ANHD_CHUNK FOURCC_CHUNK('A', 'N', 'H', 'D')
#define IFF_ANIM_CHUNK FOURCC_CHUNK('A', 'N', 'I', 'M')
#define IFF_ANNO_CHUNK FOURCC_CHUNK('A', 'N', 'N', 'O')
#define IFF_ANSQ_CHUNK FOURCC_CHUNK('A', 'N', 'S', 'Q')
#define IFF_ATAK_CHUNK FOURCC_CHUNK('A', 'T', 'A', 'K')
#define IFF_AUTH_CHUNK FOURCC_CHUNK('A', 'U', 'T', 'H')
#define IFF_BMHD_CHUNK FOURCC_CHUNK('B', 'M', 'H', 'D')
#define IFF_BODY_CHUNK FOURCC_CHUNK('B', 'O', 'D', 'Y')
#define IFF_CAMG_CHUNK FOURCC_CHUNK('C', 'A', 'M', 'G')
#define IFF_CCRT_CHUNK FOURCC_CHUNK('C', 'C', 'R', 'T')
#define IFF_CHAN_CHUNK FOURCC_CHUNK('C', 'H', 'A', 'N')
#define IFF_CMAP_CHUNK FOURCC_CHUNK('C', 'M', 'A', 'P')
#define IFF_COPY_CHUNK FOURCC_CHUNK('(', 'c', ')', ' ')
#define IFF_CRNG_CHUNK FOURCC_CHUNK('C', 'R', 'N', 'G')
#define IFF_DEST_CHUNK FOURCC_CHUNK('D', 'E', 'S', 'T')
#define IFF_DLTA_CHUNK FOURCC_CHUNK('D', 'L', 'T', 'A')
#define IFF_DPAN_CHUNK FOURCC_CHUNK('D', 'P', 'A', 'N')
#define IFF_DPI_CHUNK  FOURCC_CHUNK('D', 'P', 'I', ' ')
#define IFF_DPPS_CHUNK FOURCC_CHUNK('D', 'P', 'P', 'S')
#define IFF_DPPV_CHUNK FOURCC_CHUNK('D', 'P', 'P', 'V')
#define IFF_DRNG_CHUNK FOURCC_CHUNK('D', 'R', 'N', 'G')
#define IFF_FACE_CHUNK FOURCC_CHUNK('F', 'A', 'C', 'E')
#define IFF_FADE_CHUNK FOURCC_CHUNK('F', 'A', 'D', 'E')
#define IFF_FORM_CHUNK FOURCC_CHUNK('F', 'O', 'R', 'M')
#define IFF_FVER_CHUNK FOURCC_CHUNK('F', 'V', 'E', 'R')
#define IFF_GRAB_CHUNK FOURCC_CHUNK('G', 'R', 'A', 'B')
#define IFF_ILBM_CHUNK FOURCC_CHUNK('I', 'L', 'B', 'M')
#define IFF_INS1_CHUNK FOURCC_CHUNK('I', 'N', 'S', '1')
#define IFF_IMRT_CHUNK FOURCC_CHUNK('I', 'M', 'R', 'T')
#define IFF_JUNK_CHUNK FOURCC_CHUNK('J', 'U', 'N', 'K')
#define IFF_LIST_CHUNK FOURCC_CHUNK('L', 'I', 'S', 'T')
#define IFF_MHDR_CHUNK FOURCC_CHUNK('M', 'H', 'D', 'R')
#define IFF_NAME_CHUNK FOURCC_CHUNK('N', 'A', 'M', 'E')
#define IFF_PAN_CHUNK  FOURCC_CHUNK('P', 'A', 'N', ' ')
#define IFF_PROP_CHUNK FOURCC_CHUNK('P', 'R', 'O', 'P')
#define IFF_RLSE_CHUNK FOURCC_CHUNK('R', 'L', 'S', 'E')
#define IFF_SAMP_CHUNK FOURCC_CHUNK('S', 'A', 'M', 'P')
#define IFF_SEQN_CHUNK FOURCC_CHUNK('S', 'E', 'Q', 'N')
#define IFF_SHDR_CHUNK FOURCC_CHUNK('S', 'H', 'D', 'R')
#define IFF_SMUS_CHUNK FOURCC_CHUNK('S', 'M', 'U', 'S')
#define IFF_SPRT_CHUNK FOURCC_CHUNK('S', 'P', 'R', 'T')
#define IFF_TEXT_CHUNK FOURCC_CHUNK('T', 'E', 'X', 'T')
#define IFF_TINY_CHUNK FOURCC_CHUNK('T', 'I', 'N', 'Y')
#define IFF_TRAK_CHUNK FOURCC_CHUNK('T', 'R', 'A', 'K')
#define IFF_VHDR_CHUNK FOURCC_CHUNK('V', 'H', 'D', 'R')

/* IFF-ILBM Definitions */

/* Use this constant instead of sizeof(ColorRegister). */
#define PIC_SIZE_OF_COLOR_REGISTER      3

/* Maximum number of bitplanes storable in BitMap structure */
#define PIC_MAXAMDEPTH                  8

/* Maximum planes we can save */
#define PIC_MAXSAVEDEPTH                24

/*  Masking techniques  */
#define PIC_MASK_NONE                   0
#define PIC_MASK_HASMASK                1
#define PIC_MASK_HASTRANSPARENTMASK     2
#define PIC_MASK_LASSO                  3

/*  Compression techniques  */
#define PIC_COMPRESSION_NONE            0
#define PIC_COMPRESSION_BYTERUN1        1

#define VIDEOBUFSIZE                    128*1024

#define CAMG_LACE                       0x0004   /* Interlaced Modi */
#define CAMG_EHB                        0x0080   /* extra halfe brite */
#define CAMG_HAM                        0x0800   /* hold and modify */
#define CAMG_HIRES                      0x8000   /* Hires Modi */

#define CAMG_PAL                        0x00021000 /* Hires Modi */
#define CAMG_NTSC                       0x00011000 /* Hires Modi */

#define HAMBITS_CMAP                    0 /* take color from colormap */
#define HAMBITS_BLUE                    1 /* modify blue  component */
#define HAMBITS_RED                     2 /* modify red   component */
#define HAMBITS_GREEN                   3 /* modify green component */

static const int bitplainoffeset[] = {
                                1,       2,       4,       8,
                               16,      32,      64,     128,
                                1,       2,       4,       8,
                               16,      32,      64,     128,
                                1,       2,       4,       8,
                               16,      32,      64,     128
                        };

/* ---------- BitMapHeader ---------------------------------------------*/
/*  Required Bitmap header (BMHD) structure describes an ILBM           */
typedef struct {
  uint16_t             w;                    /* raster width in pixels  */
  uint16_t             h;                    /* raster height in pixels */
  int16_t              x;                    /* raster width in pixels  */
  int16_t              y;                    /* raster height in pixels */
  uint8_t              nplanes;              /* # source bitplanes      */
  uint8_t              masking;              /* masking technique       */
  uint8_t              compression;          /* compression algoithm    */
  uint8_t              pad1;                 /* UNUSED.  For consistency, put 0 here. */
  uint16_t             transparentColor;     /* transparent "colour number" */
  uint8_t              xaspect;              /* aspect ratio, a rational number x/y */
  uint8_t              yaspect;              /* aspect ratio, a rational number x/y */
  int16_t              pagewidth;            /* source "page" size in pixels */
  int16_t              pageheight;           /* source "page" size in pixels */
} BitMapHeader;

/* ---------- ColorRegister --------------------------------------------*/
/* A CMAP chunk is a packed array of ColorRegisters (3 bytes each).     */
typedef struct {
  uint8_t              cmap_red;             /* red color component     */
  uint8_t              cmap_green;           /* green color component   */
  uint8_t              cmap_blue;            /* blue color component    */
} ColorRegister;

/* ---------- Point2D --------------------------------------------------*/
/* A Point2D is stored in a GRAB chunk.                                 */
typedef struct {
  int16_t              x;                    /* coordinates x pixels    */
  int16_t              y;                    /* coordinates y pixels    */
} Point2D;

/* ---------- DestMerge ------------------------------------------------*/
/* A DestMerge is stored in a DEST chunk.                               */
typedef struct {
  uint8_t              depth;                /* # bitplanes in the original source */
  uint8_t              pad1;                 /* UNUSED; for consistency store 0 here */
  uint16_t             plane_pick;           /* how to scatter source bitplanes into destination */
  uint16_t             plane_onoff;          /* default bitplane data for planePick */
  uint16_t             plane_mask;           /* selects which bitplanes to store into */
} DestMerge;

/* ---------- SpritePrecedence -----------------------------------------*/
/* A SpritePrecedence is stored in a SPRT chunk.                        */
typedef uint16_t       SpritePrecedence;

/* ---------- Camg Amiga Viewport Mode Display ID ----------------------*/
/* The CAMG chunk is used to store the Amiga display mode in which
 * an ILBM is meant to be displayed.  This is very important, especially
 * for special display modes such as HAM and HALFBRITE where the
 * pixels are interpreted differently.
 * Under V37 and higher, store a 32-bit Amiga DisplayID (aka. ModeID)
 * in the ULONG ViewModes CAMG variable (from GetVPModeID(viewport)).
 * Pre-V37, instead store the 16-bit viewport->Modes.
 * See the current IFF manual for information on screening for bad CAMG
 * chunks when interpreting a CAMG as a 32-bit DisplayID or 16-bit ViewMode.
 * The chunk's content is declared as a ULONG.
 */
typedef struct {
  uint32_t             view_modes;
} CamgChunk;

/* ---------- CRange cycling chunk -------------------------------------*/
#define RNG_NORATE  36      /* Dpaint uses this rate to mean non-active */
/* A CRange is store in a CRNG chunk. */
typedef struct {
  int16_t              pad1;                 /* reserved for future use; store 0 here */
  int16_t              rate;                 /* 60/sec=16384, 30/sec=8192, 1/sec=16384/60=273 */
  int16_t              active;               /* bit0 set = active, bit 1 set = reverse */
  uint8_t              low;                  /* lower color registers selected */
  uint8_t              high;                 /* upper color registers selected */
} CRange;

/* ---------- Ccrt (Graphicraft) cycling chunk -------------------------*/
/* A Ccrt is stored in a CCRT chunk.                                    */
typedef struct {
  int16_t              direction;            /* 0=don't cycle, 1=forward, -1=backwards */
  uint8_t              start;                /* range lower             */
  uint8_t              end;                  /* range upper             */
  int32_t              seconds;              /* seconds between cycling */
  int32_t              microseconds;         /* msecs between cycling   */
  int16_t              pad;                  /* future exp - store 0 here */
} CcrtChunk;

/* ---------- DPIHeader chunk ------------------------------------------*/
/* A DPIHeader is stored in a DPI chunk.                                */
typedef struct {
  int16_t              x;
  int16_t              y;
} DPIHeader;

/* IFF-8SVX/16SV Definitions */

#define MONO                            0L
#define PAN                             1L
#define LEFT                            2L
#define RIGHT                           4L
#define STEREO                          6L

#define SND_COMPRESSION_NONE            0
#define SND_COMPRESSION_FIBONACCI       1
#define SND_COMPRESSION_EXPONENTIAL     2

#define PREAMBLE_SIZE                   8
#define IFF_JUNK_SIZE                   8
#define IFF_SIGNATURE_SIZE              12
#define PCM_BLOCK_ALIGN                 1024

#define max_volume                      65536  /* Unity = Fixed 1.0 = maximum volume */

static const int8_t fibonacci[]    = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };

static const int8_t exponential[]  = { -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64 };

typedef struct {
  uint32_t             oneShotHiSamples;     /* # samples in the high octave 1-shot part */
  uint32_t             repeatHiSamples;      /* # samples in the high octave repeat part */
  uint32_t             samplesPerHiCycle;    /* # samples/cycle in high octave, else 0 */
  uint16_t             samplesPerSec;        /* data sampling rate */
  uint8_t              ctOctave;             /* # of octaves of waveforms */
  uint8_t              sCompression;         /* data compression technique used */
  uint32_t             volume;               /* playback nominal volume from 0 to Unity
                                              * (full volume). Map this value into
                                              * the output hardware's dynamic range.
                                              */
} Voice8Header;

typedef struct {
  uint16_t             duration;             /* segment duration in milliseconds */
  uint32_t             dest;                 /* destination volume factor */
} EGPoint;

/* IFF-ANIM Definitions */

#define IFF_ANHD_ILBM                   0
#define IFF_ANHD_XOR                    1
#define IFF_ANHD_LDELTA                 2
#define IFF_ANHD_SDELTA                 3
#define IFF_ANHD_SLDELTA                4
#define IFF_ANHD_BVDELTA                5
#define IFF_ANHD_STEREOO5               6
#define IFF_ANHD_OPT7                   7
#define IFF_ANHD_OPT8                   8
#define IFF_ANHD_ASCIIJ                 74

/* ---------- AnimHeader ----------------------------------------------*/
/*  Required Anim Header (anhd) structure describes an ANIM-Frame      */
typedef struct {
  uint8_t              operation;            /* The compression method:
                                              * 0  set directly (normal ILBM BODY),
                                              * 1  XOR ILBM mode,
                                              * 2  Long Delta mode,
                                              * 3  Short Delta mode,
                                              * 4  Generalized short/long Delta mode,
                                              * 5  Byte Vertical Delta mode
                                              * 6  Stereo op 5 (third party)
                                              * 74 (ascii 'J') reserved for Eric Graham's
                                              *    compression technique
                                              */
  uint8_t              mask;                 /* (XOR mode only - plane mask where each
                                              * bit is set =1 if there is data and =0
                                              * if not.)
                                              */
  uint16_t             w;                    /* (XOR mode only - width and height of the  */
  uint16_t             h;                    /* area represented by the BODY to eliminate */
                                             /* unnecessary un-changed data)              */
  int16_t              x;                    /* (XOR mode only - position of rectangular  */
  int16_t              y;                    /* area representd by the BODY)              */
  uint32_t             abs_time;             /* (currently unused - timing for a frame    */
                                             /* relative to the time the first frame      */
                                             /* was displayed - in jiffies (1/60 sec))    */
  uint32_t             rel_time;             /* (timing for frame relative to time        */
                                             /* previous frame was displayed - in         */
                                             /* jiffies (1/60 sec))                       */
  uint8_t              interleave;           /* (unused so far - indicates how may frames */
                                             /* back this data is to modify.  =0 defaults */
                                             /* to indicate two frames back (for double   */
                                             /* buffering). =n indicates n frames back.   */
                                             /* The main intent here is to allow values   */
                                             /* of =1 for special applications where      */
                                             /* frame data would modify the immediately   */
                                             /* previous frame)                           */
  uint8_t              pad0;                 /* Pad byte, not used at present.            */
  uint32_t             bits;                 /* 32 option bits used by options=4 and 5.   */
                                             /* At present only 6 are identified, but the */
                                             /* rest are set =0 so they can be used to    */
                                             /* implement future ideas.  These are defined*/
                                             /* for option 4 only at this point.  It is   */
                                             /* recommended that all bits be set =0 for   */
                                             /* option 5 and that any bit settings        */
                                             /* used in the future (such as for XOR mode) */
                                             /* be compatible with the option 4           */
                                             /* bit settings.   Player code should check  */
                                             /*undefined bits in options 4 and 5 to assure*/
                                             /* they are zero.                            */
                                             /*                                           */
                                             /* The six bits for current use are:         */
                                             /*                                           */
                                             /* bit #      set =0               set =1    */
                                             /* ========================================= */
                                             /* 0       short data           long data    */
                                             /* 1          set                  XOR       */
                                             /* 2      separate info        one info list */
                                             /*        for each plane       for all planes*/
                                             /* 3        not RLC        RLC(run length c.)*/
                                             /* 4       horizontal           vertical     */
                                             /* 5     short info offsets   long info offs.*/
  uint8_t              pad[16];              /* This is a pad for future use for future   */
                                             /* compression modes.                        */
} AnimHeader;

/* ---------- DPAnim-Chunk ----------------------------------------------*/
/*  Deluxe Paint Anim (DPAN) Chunk      */
typedef struct {
  uint16_t             version;              /* Version */
  uint16_t             nframes;              /* number of frames in the animation.*/
  uint8_t              fps;                  /* frames per second */
  uint8_t              unused1;
  uint8_t              unused2;
  uint8_t              unused3;
} DPAnimChunk;

#endif /* IFFP_IFF_H */
