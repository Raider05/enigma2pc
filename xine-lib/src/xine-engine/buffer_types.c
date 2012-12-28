/*
 * Copyright (C) 2000-2008 the xine project
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
 *
 * contents:
 *
 * buffer types management.
 * convert FOURCC and audioformattag to BUF_xxx defines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/xine_internal.h>
#include "bswap.h"

typedef struct video_db_s {
   uint32_t fourcc[20];
   uint32_t buf_type;
   const char *name;
} video_db_t;

typedef struct audio_db_s {
   uint32_t formattag[10];
   uint32_t buf_type;
   const char *name;
} audio_db_t;


static const video_db_t video_db[] = {
{
  {
    ME_FOURCC('m', 'p', 'e', 'g'),
    ME_FOURCC('M', 'P', 'E', 'G'),
    ME_FOURCC('P', 'I', 'M', '1'),
    ME_FOURCC('m', 'p', 'g', '2'),
    ME_FOURCC('m', 'p', 'g', '1'),
    ME_FOURCC(0x02, 0, 0, 0x10),
    0
  },
  BUF_VIDEO_MPEG,
  "MPEG 1/2"
},
{
  {
    ME_FOURCC('D', 'I', 'V', 'X'),
    ME_FOURCC('d', 'i', 'v', 'x'),
    ME_FOURCC('D', 'i', 'v', 'x'),
    ME_FOURCC('D', 'i', 'v', 'X'),
    ME_FOURCC('M', 'P', '4', 'S'),
    ME_FOURCC('m', 'p', '4', 'v'),
    ME_FOURCC('M', '4', 'S', '2'),
    ME_FOURCC('m', '4', 's', '2'),
    ME_FOURCC('F', 'M', 'P', '4'),
    0
  },
  BUF_VIDEO_MPEG4,
  "ISO-MPEG4/OpenDivx"
},
{
  {
    ME_FOURCC('X', 'V', 'I', 'D'),
    ME_FOURCC('x', 'v', 'i', 'd'),
    0
  },
  BUF_VIDEO_XVID,
  "XviD"
},
{
  {
    ME_FOURCC('D', 'X', '5', '0'),
    0
  },
  BUF_VIDEO_DIVX5,
  "DivX 5"
},
{
  {
    ME_FOURCC('c', 'v', 'i', 'd'),
    0
  },
  BUF_VIDEO_CINEPAK,
  "Cinepak"
},
{
  {
    ME_FOURCC('S', 'V', 'Q', '1'),
    ME_FOURCC('s', 'v', 'q', '1'),
    ME_FOURCC('s', 'v', 'q', 'i'),
    0
  },
  BUF_VIDEO_SORENSON_V1,
  "Sorenson Video 1"
},
{
  {
    ME_FOURCC('S', 'V', 'Q', '3'),
    ME_FOURCC('s', 'v', 'q', '3'),
    0
  },
  BUF_VIDEO_SORENSON_V3,
  "Sorenson Video 3"
},
{
  {
    ME_FOURCC('M', 'P', '4', '1'),
    ME_FOURCC('m', 'p', '4', '1'),
    ME_FOURCC('M', 'P', 'G', '4'),
    ME_FOURCC('m', 'p', 'g', '4'),
    0
  },
  BUF_VIDEO_MSMPEG4_V1,
  "Microsoft MPEG-4 v1"
},
{
  {
    ME_FOURCC('M', 'P', '4', '1'),
    ME_FOURCC('m', 'p', '4', '1'),
    ME_FOURCC('M', 'P', '4', '2'),
    ME_FOURCC('m', 'p', '4', '2'),
    ME_FOURCC('D', 'I', 'V', '2'),
    ME_FOURCC('d', 'i', 'v', '2'),
    0
  },
  BUF_VIDEO_MSMPEG4_V2,
  "Microsoft MPEG-4 v2"
},
{
  {
    ME_FOURCC('M', 'P', '4', '3'),
    ME_FOURCC('m', 'p', '4', '3'),
    ME_FOURCC('D', 'I', 'V', '3'),
    ME_FOURCC('d', 'i', 'v', '3'),
    ME_FOURCC('D', 'I', 'V', '4'),
    ME_FOURCC('d', 'i', 'v', '4'),
    ME_FOURCC('D', 'I', 'V', '5'),
    ME_FOURCC('d', 'i', 'v', '5'),
    ME_FOURCC('D', 'I', 'V', '6'),
    ME_FOURCC('d', 'i', 'v', '6'),
    ME_FOURCC('A', 'P', '4', '1'),
    ME_FOURCC('M', 'P', 'G', '3'),
    ME_FOURCC('C', 'O', 'L', '1'),
    ME_FOURCC('3', 'I', 'V', 'D'),
    0
  },
  BUF_VIDEO_MSMPEG4_V3,
  "Microsoft MPEG-4 v3"
},
{
  {
    ME_FOURCC('3', 'I', 'V', '1'),
    ME_FOURCC('3', 'I', 'V', '2'),
    0
  },
  BUF_VIDEO_3IVX,
  "3ivx MPEG-4"
},
{
  {
    ME_FOURCC('d', 'm', 'b', '1'),
    ME_FOURCC('M', 'J', 'P', 'G'),
    ME_FOURCC('m', 'j', 'p', 'a'),
    ME_FOURCC('A', 'V', 'R', 'n'),
    ME_FOURCC('A', 'V', 'D', 'J'),
    0
  },
  BUF_VIDEO_MJPEG,
  "Motion JPEG"
},
{
  {
    ME_FOURCC('m', 'j', 'p', 'b'),
    0
  },
  BUF_VIDEO_MJPEG_B,
  "Motion JPEG B"
},
{
  {
    ME_FOURCC('I', 'V', '5', '0'),
    ME_FOURCC('i', 'v', '5', '0'),
    0
  },
  BUF_VIDEO_IV50,
  "Indeo Video 5.0"
},
{
  {
    ME_FOURCC('I', 'V', '4', '1'),
    ME_FOURCC('i', 'v', '4', '1'),
    0
  },
  BUF_VIDEO_IV41,
  "Indeo Video 4.1"
},
{
  {
    ME_FOURCC('I', 'V', '3', '2'),
    ME_FOURCC('i', 'v', '3', '2'),
    0
  },
  BUF_VIDEO_IV32,
  "Indeo Video 3.2"
},
{
  {
    ME_FOURCC('I', 'V', '3', '1'),
    ME_FOURCC('i', 'v', '3', '1'),
    0
  },
  BUF_VIDEO_IV31,
  "Indeo Video 3.1"
},
{
  {
    ME_FOURCC('V', 'C', 'R', '1'),
    0
  },
  BUF_VIDEO_ATIVCR1,
  "ATI VCR1"
},
{
  {
    ME_FOURCC('V', 'C', 'R', '2'),
    0
  },
  BUF_VIDEO_ATIVCR2,
  "ATI VCR2"
},
{
  {
    ME_FOURCC('I', '2', '6', '3'),
    ME_FOURCC('i', '2', '6', '3'),
    ME_FOURCC('V', 'I', 'V', 'O'),
    ME_FOURCC('v', 'i', 'v', 'o'),
    ME_FOURCC('v', 'i', 'v', '1'),
    0
  },
  BUF_VIDEO_I263,
  "I263"
},
{
  {
    ME_FOURCC('D','I','B',' '),  /* device-independent bitmap */
    ME_FOURCC('r','a','w',' '),
    0
  },
  BUF_VIDEO_RGB,
  "Raw RGB"
},
{
  {
    /* is this right? copied from demux_qt:
    else if (!strncasecmp (video, "yuv2", 4))
    this->video_type = BUF_VIDEO_YUY2;
    */
    ME_FOURCC('y', 'u', 'v', '2'),
    ME_FOURCC('Y', 'U', 'Y', '2'),
    0
  },
  BUF_VIDEO_YUY2,
  ""
},
{
  {
    ME_FOURCC('j','p','e','g'),
    ME_FOURCC('J','F','I','F'),
    0
  },
  BUF_VIDEO_JPEG,
  "JPEG"
},
{
  {
    ME_FOURCC('W','M','V','1'),
    0
  },
  BUF_VIDEO_WMV7,
  "Windows Media Video 7"
},
{
  {
    ME_FOURCC('W','M','V','2'),
    0
  },
  BUF_VIDEO_WMV8,
  "Windows Media Video 8"
},
{
  {
    ME_FOURCC('W','M','V','3'),
    ME_FOURCC('W','M','V','P'),
    0
  },
  BUF_VIDEO_WMV9,
  "Windows Media Video 9"
},
{
  {
    ME_FOURCC('W','V','C','1'),
    ME_FOURCC('W','M','V','A'),
    ME_FOURCC('v','c','-','1'),
    0
  },
  BUF_VIDEO_VC1,
  "Windows Media Video VC-1"
},
{
  {
    ME_FOURCC('c','r','a','m'),
    ME_FOURCC('C','R','A','M'),
    ME_FOURCC('M','S','V','C'),
    ME_FOURCC('m','s','v','c'),
    ME_FOURCC('W','H','A','M'),
    ME_FOURCC('w','h','a','m'),
    0
  },
  BUF_VIDEO_MSVC,
  "Microsoft Video 1"
},
{
  {
    ME_FOURCC('D','V','S','D'),
    ME_FOURCC('d','v','s','d'),
    ME_FOURCC('d','v','c','p'),
    0
  },
  BUF_VIDEO_DV,
  "Sony Digital Video (DV)"
},
{
  {
    ME_FOURCC('V','P','3',' '),
    ME_FOURCC('V','P','3','0'),
    ME_FOURCC('v','p','3','0'),
    ME_FOURCC('V','P','3','1'),
    ME_FOURCC('v','p','3','1'),
    0
  },
  BUF_VIDEO_VP31,
  "On2 VP3.1"
},
{
  {
    ME_FOURCC('V','P','4','0'),
    0,
  },
  BUF_VIDEO_VP4,
  "On2 VP4"
},
{
  {
    ME_FOURCC('H', '2', '6', '3'),
    ME_FOURCC('h', '2', '6', '3'),
    ME_FOURCC('U', '2', '6', '3'),
    ME_FOURCC('s', '2', '6', '3'),
    0
  },
  BUF_VIDEO_H263,
  "H263"
},
{
  {
    ME_FOURCC('c', 'y', 'u', 'v'),
    ME_FOURCC('C', 'Y', 'U', 'V'),
    0
  },
  BUF_VIDEO_CYUV,
  "Creative YUV"
},
{
  {
    ME_FOURCC('s', 'm', 'c', ' '),
    0
  },
  BUF_VIDEO_SMC,
  "Apple Quicktime Graphics (SMC)"
},
{
  {
    ME_FOURCC('r', 'p', 'z', 'a'),
    ME_FOURCC('a', 'z', 'p', 'r'),
    0
  },
  BUF_VIDEO_RPZA,
  "Apple Quicktime (RPZA)"
},
{
  {
    ME_FOURCC('r', 'l', 'e', ' '),
    0
  },
  BUF_VIDEO_QTRLE,
  "Apple Quicktime Animation (RLE)"
},
{
  {
    1, 2, 0  /* MS RLE format identifiers */
  },
  BUF_VIDEO_MSRLE,
  "Microsoft RLE"
},
{
  {
    ME_FOURCC('D', 'U', 'C', 'K'),
    0
  },
  BUF_VIDEO_DUCKTM1,
  "Duck Truemotion v1"
},
{
  {
    ME_FOURCC('M', 'S', 'S', '1'),
    0
  },
  BUF_VIDEO_MSS1,
  "Windows Screen Video"
},
{
  {
    ME_FOURCC('P', 'G', 'V', 'V'),
    0
  },
  BUF_VIDEO_PGVV,
  "Radius Studio"
},
{
  {
    ME_FOURCC('Z', 'y', 'G', 'o'),
    0
  },
  BUF_VIDEO_ZYGO,
  "ZyGo Video"
},
{
  {
    ME_FOURCC('t', 's', 'c', 'c'),
    0
  },
  BUF_VIDEO_TSCC,
  "TechSmith Screen Capture Codec"
},
{
  {
    ME_FOURCC('Y', 'V', 'U', '9'),
    0
  },
  BUF_VIDEO_YVU9,
  "Raw YVU9 Planar Data"
},
{
  {
    ME_FOURCC('G', 'R', 'E', 'Y'),
    0
  },
  BUF_VIDEO_GREY,
  "Raw Greyscale"
},
{
  {
    ME_FOURCC('X', 'x', 'a', 'n'),
    ME_FOURCC('X', 'X', 'A', 'N'),
    ME_FOURCC('x', 'x', 'a', 'n'),
    0
  },
  BUF_VIDEO_XXAN,
  "Wing Commander IV Video Codec"
},
{
  {
    ME_FOURCC('Y', 'V', '1', '2'),
    ME_FOURCC('y', 'v', '1', '2'),
    0
  },
  BUF_VIDEO_YV12,
  "Raw Planar YV12"
},
{
  {
    ME_FOURCC('I', '4', '2', '0'),
    ME_FOURCC('I', 'Y', 'U', 'V'),
    0
  },
  BUF_VIDEO_I420,
  "Raw Planar I420"
},
{
  {
    ME_FOURCC('S', 'E', 'G', 'A'),
    ME_FOURCC('s', 'e', 'g', 'a'),
    0
  },
  BUF_VIDEO_SEGA,
  "Cinepak for Sega"
},
{
  {
    ME_FOURCC('m', 'v', 'i', '2'),
    ME_FOURCC('M', 'V', 'I', '2'),
    0
  },
  BUF_VIDEO_MVI2,
  "Motion Pixels"
},
{
  {
    ME_FOURCC('u', 'c', 'o', 'd'),
    ME_FOURCC('U', 'C', 'O', 'D'),
    0
  },
  BUF_VIDEO_UCOD,
  "ClearVideo"
},
{
  {
    ME_FOURCC('R', 'V', '1', '0'),
    0
  },
  BUF_VIDEO_RV10,
  "Real Video 1.0"
},
{
  {
    ME_FOURCC('R', 'V', '2', '0'),
    0
  },
  BUF_VIDEO_RV20,
  "Real Video 2.0"
},
{
  {
    ME_FOURCC('R', 'V', '3', '0'),
    0
  },
  BUF_VIDEO_RV30,
  "Real Video 3.0"
},
{
  {
    ME_FOURCC('R', 'V', '4', '0'),
    0
  },
  BUF_VIDEO_RV40,
  "Real Video 4.0"
},
{
  {
    ME_FOURCC('H', 'F', 'Y', 'U'),
    0,
  },
  BUF_VIDEO_HUFFYUV,
  "HuffYUV"
},
{
  {
    ME_FOURCC('I', 'M', 'G', ' '),
    ME_FOURCC('g', 'i', 'f', ' '),
    0,
  },
  BUF_VIDEO_IMAGE,
  "Image"
},
{
  {
    0,
  },
  BUF_VIDEO_THEORA,
  "Ogg Theora"
},
{
  {
    ME_FOURCC('V','P','5','0'),
    0
  },
  BUF_VIDEO_VP5,
  "On2 VP5"
},
{
  {
    ME_FOURCC('V','P','6','0'),
    ME_FOURCC('V','P','6','1'),
    ME_FOURCC('V','P','6','2'),
    0
  },
  BUF_VIDEO_VP6,
  "On2 VP6"
},
{
  {
    ME_FOURCC('V','P','6','F'),
    0
  },
  BUF_VIDEO_VP6F,
  "On2 VP6"
},
{
  {
    ME_FOURCC('8','B', 'P','S'),
    0
  },
  BUF_VIDEO_8BPS,
  "Planar RGB"
},
{
  {
    ME_FOURCC('Z','L','I','B'),
    0
  },
  BUF_VIDEO_ZLIB,
  "ZLIB Video"
},
{
  {
    ME_FOURCC('M','S','Z','H'),
    0
  },
  BUF_VIDEO_MSZH,
  "MSZH Video"
},
{
  {
    ME_FOURCC('A','S','V','1'),
    0
  },
  BUF_VIDEO_ASV1,
  "ASV v1 Video"
},
{
  {
    ME_FOURCC('A','S','V','2'),
    0
  },
  BUF_VIDEO_ASV2,
  "ASV v2 Video"
},
{
  {
    ME_FOURCC('a','v','c','1'),
    ME_FOURCC('h','2','6','4'),
    ME_FOURCC('H','2','6','4'),
    ME_FOURCC('x','2','6','4'),
    ME_FOURCC('X','2','6','4'),
    0
  },
  BUF_VIDEO_H264,
  "Advanced Video Coding (H264)"
},
{
  {
    ME_FOURCC('A','A','S','C'),
    0
  },
  BUF_VIDEO_AASC,
  "Autodesk Animator Studio Codec"
},
{
  {
    ME_FOURCC('q','d','r','w'),
    0
  },
  BUF_VIDEO_QDRW,
  "QuickDraw"
},
{
  {
    ME_FOURCC('L','O','C','O'),
    0
  },
  BUF_VIDEO_LOCO,
  "LOCO"
},
{
  {
    ME_FOURCC('U','L','T','I'),
    0
  },
  BUF_VIDEO_ULTI,
  "IBM UltiMotion"
},
{
  {
    ME_FOURCC('W','N','V','1'),
    0
  },
  BUF_VIDEO_WNV1,
  "Winnow Video"
},
{
  {
    ME_FOURCC('P','I','X','L'),
    ME_FOURCC('X','I','X','L'),
    0
  },
  BUF_VIDEO_XL,
  "Miro/Pinnacle VideoXL"
},
{
  {
    ME_FOURCC('Q','P','E','G'),
    ME_FOURCC('Q','1','.','0'),
    ME_FOURCC('Q','1','.','1'),
    0
  },
  BUF_VIDEO_QPEG,
  "Q-Team QPEG Video"
},
{
  {
    ME_FOURCC('R','T','2','1'),
    0
  },
  BUF_VIDEO_RT21,
  "Winnow Video"
},
{
  {
    ME_FOURCC('F','P','S','1'),
    0
  },
  BUF_VIDEO_FPS1,
  "Fraps FPS1"
},
{
  {
    ME_FOURCC('T','M','2','0'),
    0
  },
  BUF_VIDEO_DUCKTM2,
  "Duck TrueMotion 2"
},
{
  {
    ME_FOURCC('C','S','C','D'),
    0
  },
  BUF_VIDEO_CSCD,
  "CamStudio"
},
{
  {
    ME_FOURCC('Z','M','B','V'),
    0
  },
  BUF_VIDEO_ZMBV,
  "Zip Motion Blocks Video"
},
{
  {
    ME_FOURCC('K','M','V','C'),
    0
  },
  BUF_VIDEO_KMVC,
  "Karl Morton's Video Codec"
},
{
  {
    ME_FOURCC('V','M','n','c'),
    0
  },
  BUF_VIDEO_VMNC,
  "VMware Screen Codec"
},
{
  {
    ME_FOURCC('S','N','O','W'),
    0
  },
  BUF_VIDEO_SNOW,
  "Snow"
},
{
  {
    ME_FOURCC('V','P','8','0'),
    0
  },
  BUF_VIDEO_VP8,
  "On2 VP8"
},
{ { 0 }, 0, "last entry" }
};


static const audio_db_t audio_db[] = {
{
  {
    0x2000,
    ME_FOURCC('m', 's', 0x20, 0x00),
    0
  },
  BUF_AUDIO_A52,
  "AC3"
},
{
  {
    0x50, 0x55,
    ME_FOURCC('.','m','p','3'),
    ME_FOURCC('m', 's', 0, 0x55),
    ME_FOURCC('M','P','3',' '),
    0
  },
  BUF_AUDIO_MPEG,
  "MPEG layer 2/3"
},
{
  {
    ME_FOURCC('a', 'd', 'u', 0x55),
    0
  },
  BUF_AUDIO_MP3ADU,
  "MPEG layer-3 adu"
},
{
  {
    ME_FOURCC('t','w','o','s'),
    ME_FOURCC('i','n','2','4'),
    0
  },
  BUF_AUDIO_LPCM_BE,
  "Uncompressed PCM big endian"
},
{
  {
    0x01,
    ME_FOURCC('r','a','w',' '),
    ME_FOURCC('s','o','w','t'),
    ME_FOURCC('4','2','n','i'),
    0
  },
  BUF_AUDIO_LPCM_LE,
  "Uncompressed PCM little endian"
},
{
  {
    0x160, 0
  },
  BUF_AUDIO_WMAV1,
  "Windows Media Audio v1"
},
{
  {
    0x161, 0
  },
  BUF_AUDIO_WMAV2,
  "Windows Media Audio v2"
},
{
  {
    0x162, 0
  },
  BUF_AUDIO_WMAPRO,
  "Windows Media Audio Professional"
},
{
  {
    0x163, 0
  },
  BUF_AUDIO_WMALL,
  "Windows Media Audio Lossless"
},
{
  {
    0xA, 0
  },
  BUF_AUDIO_WMAV,
  "Windows Media Audio Voice"
},
{
  {
    0x2001, 0
  },
  BUF_AUDIO_DTS,
  "DTS"
},
{
  {
    0x02,
    ME_FOURCC('m', 's', 0, 0x02),
    0
  },
  BUF_AUDIO_MSADPCM,
  "MS ADPCM"
},
{
  {
    0x11,
    ME_FOURCC('m', 's', 0, 0x11),
    0
  },
  BUF_AUDIO_MSIMAADPCM,
  "MS IMA ADPCM"
},
{
  {
    0x31, 0x32, 0
  },
  BUF_AUDIO_MSGSM,
  "MS GSM"
},
{
  {
    /* these formattags are used by Vorbis ACM encoder and
       supported by NanDub, a variant of VirtualDub. */
    0x674f, 0x676f, 0x6750, 0x6770, 0x6751, 0x6771,
    ME_FOURCC('O','g','g','S'),
    ME_FOURCC('O','g','g','V'),
    0
  },
  BUF_AUDIO_VORBIS,
  "OggVorbis Audio"
},
{
  {
    0x401, 0
  },
  BUF_AUDIO_IMC,
  "Intel Music Coder"
},
{
  {
    0x1101, 0x1102, 0x1103, 0x1104, 0
  },
  BUF_AUDIO_LH,
  "Lernout & Hauspie"
},
{
  {
    0x75, 0
  },
  BUF_AUDIO_VOXWARE,
  "Voxware Metasound"
},
{
  {
    0x130, 0
  },
  BUF_AUDIO_ACELPNET,
  "ACELP.net"
},
{
  {
    0x111, 0x112, 0
  },
  BUF_AUDIO_VIVOG723,
  "Vivo G.723/Siren Audio Codec"
},
{
  {
    0x61, 0
  },
  BUF_AUDIO_DK4ADPCM,
  "Duck DK4 ADPCM (rogue format number)"
},
{
  {
    0x62, 0
  },
  BUF_AUDIO_DK3ADPCM,
  "Duck DK3 ADPCM (rogue format number)"
},
{
  {
    ME_FOURCC('i', 'm', 'a', '4'),
    0
  },
  BUF_AUDIO_QTIMAADPCM,
  "QT IMA ADPCM"
},
{
  {
    ME_FOURCC('m', 'a', 'c', '3'),
    ME_FOURCC('M', 'A', 'C', '3'),
    0
  },
  BUF_AUDIO_MAC3,
  "Apple MACE 3:1 Audio"
},
{
  {
    ME_FOURCC('m', 'a', 'c', '6'),
    ME_FOURCC('M', 'A', 'C', '6'),
    0
  },
  BUF_AUDIO_MAC6,
  "Apple MACE 6:1 Audio"
},
{
  {
    ME_FOURCC('Q', 'D', 'M', 'C'),
    0
  },
  BUF_AUDIO_QDESIGN1,
  "QDesign Audio v1"
},
{
  {
    ME_FOURCC('Q', 'D', 'M', '2'),
    0
  },
  BUF_AUDIO_QDESIGN2,
  "QDesign Audio v2"
},
{
  {
    0xFF,
    ME_FOURCC('m', 'p', '4', 'a'),
    ME_FOURCC('M', 'P', '4', 'A'),
    ME_FOURCC('r', 'a', 'a', 'c'),
    ME_FOURCC('r', 'a', 'c', 'p'),
    ME_FOURCC('A', 'A', 'C', ' '),
    0
  },
  BUF_AUDIO_AAC,
  "Advanced Audio Coding (MPEG-4 AAC)"
},
{
  {
    ME_FOURCC('d', 'n', 'e', 't'),
    0
  },
  BUF_AUDIO_DNET,
  "RealAudio DNET"
},
{
  {
    ME_FOURCC('s', 'i', 'p', 'r'),
    0
  },
  BUF_AUDIO_SIPRO,
  "RealAudio SIPRO"
},
{
  {
    ME_FOURCC('c', 'o', 'o', 'k'),
    0
  },
  BUF_AUDIO_COOK,
  "RealAudio COOK"
},
{
  {
    ME_FOURCC('a', 't', 'r', 'c'),
    0
  },
  BUF_AUDIO_ATRK,
  "RealAudio ATRK"
},
{
  {
    ME_FOURCC('Q', 'c', 'l', 'p'),
    0
  },
  BUF_AUDIO_QCLP,
  "Qualcomm PureVoice"
},
{
  {
    0x7,
    ME_FOURCC('u', 'l', 'a', 'w'),
    0
  },
  BUF_AUDIO_MULAW,
  "mu-law logarithmic PCM"
},
{
  {
    0x6,
    ME_FOURCC('a', 'l', 'a', 'w'),
    0
  },
  BUF_AUDIO_ALAW,
  "A-law logarithmic PCM"
},
{
  {
    ME_FOURCC('a', 'g', 's', 'm'),
    0
  },
  BUF_AUDIO_GSM610,
  "GSM 6.10"
},
{
  {
    0
  },
  BUF_AUDIO_FLAC,
  "Free Lossless Audio Codec (FLAC)"
},
{
  {
    0
  },
  BUF_AUDIO_DV,
  "DV Audio"
},
{
  {
    ME_FOURCC('l', 'p', 'c', 'J'),
    0
  },
  BUF_AUDIO_14_4,
  "Real 14.4"
},
{
  {
    ME_FOURCC('2', '8', '_', '8'),
    0
  },
  BUF_AUDIO_28_8,
  "Real 28.8"
},
{
  {
    0
  },
  BUF_AUDIO_SPEEX,
  "Speex"
},
{
  {
    ME_FOURCC('a', 'l', 'a', 'c'),
  },
  BUF_AUDIO_ALAC,
  "Apple Lossless Audio Codec"
},
{
  {
    0x22,
  },
  BUF_AUDIO_TRUESPEECH,
  "Truespeech"
},
{
  {
    0
  },
  BUF_AUDIO_MPC,
  "Musepack"
},
{
  {
    ME_FOURCC('W', 'V', 'P', 'K'),
  },
  BUF_AUDIO_WAVPACK,
  "Wavpack"
},
{
  {
    ME_FOURCC('s', 'a', 'm', 'r'),
  },
  BUF_AUDIO_AMR_NB,
  "AMR narrow band"
},
{
  {
    ME_FOURCC('s', 'a', 'w', 'b'),
  },
  BUF_AUDIO_AMR_WB,
  "AMR wide band"
},
{
  {
    ME_FOURCC('T', 'T', 'A', '1'),
  },
  BUF_AUDIO_TTA,
  "True Audio Lossless"
},
{
  {
    ME_FOURCC('E', 'A', 'C', '3'),
    ME_FOURCC('e', 'c', '-', '3'),
    0
  },
  BUF_AUDIO_EAC3,
  "E-AC-3"
},
{
  {
    ME_FOURCC('M', 'P', '4', 'L'),
    0
  },
  BUF_AUDIO_AAC_LATM,
  "AAC LATM"
},
{ { 0 }, 0, "last entry" }
};


uint32_t _x_fourcc_to_buf_video( uint32_t fourcc_int ) {
int i, j;
static uint32_t cached_fourcc=0;
static uint32_t cached_buf_type=0;

  if( fourcc_int == cached_fourcc )
    return cached_buf_type;

  for( i = 0; video_db[i].buf_type; i++ ) {
    for( j = 0; video_db[i].fourcc[j]; j++ ) {
      if( fourcc_int == video_db[i].fourcc[j] ) {
        cached_fourcc = fourcc_int;
        cached_buf_type = video_db[i].buf_type;
        return video_db[i].buf_type;
      }
    }
  }
  return 0;
}

const char * _x_buf_video_name( uint32_t buf_type ) {
int i;

  buf_type &= 0xffff0000;

  for( i = 0; video_db[i].buf_type; i++ ) {
    if( buf_type == video_db[i].buf_type ) {
        return video_db[i].name;
    }
  }

  return "";
}

uint32_t _x_formattag_to_buf_audio( uint32_t formattag ) {
int i, j;
static uint16_t cached_formattag=0;
static uint32_t cached_buf_type=0;

  if( formattag == cached_formattag )
    return cached_buf_type;

  for( i = 0; audio_db[i].buf_type; i++ ) {
    for( j = 0; audio_db[i].formattag[j]; j++ ) {
      if( formattag == audio_db[i].formattag[j] ) {
        cached_formattag = formattag;
        cached_buf_type = audio_db[i].buf_type;
        return audio_db[i].buf_type;
      }
    }
  }
  return 0;
}

const char * _x_buf_audio_name( uint32_t buf_type ) {
int i;

  buf_type &= 0xffff0000;

  for( i = 0; audio_db[i].buf_type; i++ ) {
    if( buf_type == audio_db[i].buf_type ) {
        return audio_db[i].name;
    }
  }

  return "";
}


static void code_to_text (char ascii[5], uint32_t code)
{
  int i;
  for (i = 0; i < 4; ++i)
  {
    int byte = code & 0xFF;
    ascii[i] = (byte < ' ') ? ' ' : (byte >= 0x7F) ? '.' : (char) byte;
    code >>= 8;
  }
  ascii[4] = 0;
}

void _x_report_video_fourcc (xine_t *xine, const char *module, uint32_t code)
{
  if (code)
  {
    char ascii[5];
    code_to_text (ascii, code);
    xprintf (xine, XINE_VERBOSITY_LOG,
             _("%s: unknown video FourCC code %#x \"%s\"\n"),
             module, code, ascii);
  }
}

void _x_report_audio_format_tag (xine_t *xine, const char *module, uint32_t code)
{
  if (code)
  {
    char ascii[5];
    code_to_text (ascii, code);
    xprintf (xine, XINE_VERBOSITY_LOG,
             _("%s: unknown audio format tag code %#x \"%s\"\n"),
             module, code, ascii);
  }
}


void _x_bmiheader_le2me( xine_bmiheader *bih ) {
  /* OBS: fourcc must be read using machine endianness
   *      so don't play with biCompression here!
   */

  bih->biSize = le2me_32(bih->biSize);
  bih->biWidth = le2me_32(bih->biWidth);
  bih->biHeight = le2me_32(bih->biHeight);
  bih->biPlanes = le2me_16(bih->biPlanes);
  bih->biBitCount = le2me_16(bih->biBitCount);
  bih->biSizeImage = le2me_32(bih->biSizeImage);
  bih->biXPelsPerMeter = le2me_32(bih->biXPelsPerMeter);
  bih->biYPelsPerMeter = le2me_32(bih->biYPelsPerMeter);
  bih->biClrUsed = le2me_32(bih->biClrUsed);
  bih->biClrImportant = le2me_32(bih->biClrImportant);
}

void _x_waveformatex_le2me( xine_waveformatex *wavex ) {

  wavex->wFormatTag = le2me_16(wavex->wFormatTag);
  wavex->nChannels = le2me_16(wavex->nChannels);
  wavex->nSamplesPerSec = le2me_32(wavex->nSamplesPerSec);
  wavex->nAvgBytesPerSec = le2me_32(wavex->nAvgBytesPerSec);
  wavex->nBlockAlign = le2me_16(wavex->nBlockAlign);
  wavex->wBitsPerSample = le2me_16(wavex->wBitsPerSample);
  wavex->cbSize = le2me_16(wavex->cbSize);
}

