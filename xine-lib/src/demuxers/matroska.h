/*
 * Copyright (C) 2000-2009 the xine project
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
 * Matroska EBML stream handling
 */
#ifndef MATROSKA_H
#define MATROSKA_H

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#include "ebml.h"

/*
 * Matroska Element IDs
 */

/* Segment */
#define MATROSKA_ID_SEGMENT                       0x18538067

/* Meta Seek Information */
#define MATROSKA_ID_SEEKHEAD                      0x114D9B74
#define MATROSKA_ID_S_ENTRY                       0x4DBB
#define MATROSKA_ID_S_ID                          0x53AB
#define MATROSKA_ID_S_POSITION                    0x53AC

/* Segment Information */
#define MATROSKA_ID_INFO                          0x1549A966
#define MATROSKA_ID_I_SEGMENTUID                  0x73A4
#define MATROSKA_ID_I_FILENAME                    0x7384
#define MATROSKA_ID_I_PREVUID                     0x3CB923
#define MATROSKA_ID_I_PREVFILENAME                0x3C83AB
#define MATROSKA_ID_I_NEXTUID                     0x3EB923
#define MATROSKA_ID_I_NEXTFILENAME                0x3E83BB
#define MATROSKA_ID_I_TIMECODESCALE               0x2AD7B1
#define MATROSKA_ID_I_DURATION                    0x4489
#define MATROSKA_ID_I_DATEUTC                     0x4461
#define MATROSKA_ID_I_TITLE                       0x7BA9
#define MATROSKA_ID_I_MUXINGAPP                   0x4D80
#define MATROSKA_ID_I_WRITINGAPP                  0x5741

/* Cluster */
#define MATROSKA_ID_CLUSTER                       0x1F43B675
#define MATROSKA_ID_CL_TIMECODE                   0xE7
#define MATROSKA_ID_CL_POSITION                   0xA7
#define MATROSKA_ID_CL_PREVSIZE                   0xAB
#define MATROSKA_ID_CL_BLOCKGROUP                 0xA0
#define MATROSKA_ID_CL_BLOCK                      0xA1
#define MATROSKA_ID_CL_BLOCKVIRTUAL               0xA2
#define MATROSKA_ID_CL_SIMPLEBLOCK                0xA3
#define MATROSKA_ID_CL_BLOCKADDITIONS             0x75A1
#define MATROSKA_ID_CL_BLOCKMORE                  0xA6
#define MATROSKA_ID_CL_BLOCKADDID                 0xEE
#define MATROSKA_ID_CL_BLOCKADDITIONAL            0xA5
#define MATROSKA_ID_CL_BLOCKDURATION              0x9B
#define MATROSKA_ID_CL_REFERENCEPRIORITY          0xFA
#define MATROSKA_ID_CL_REFERENCEBLOCK             0xFB
#define MATROSKA_ID_CL_REFERENCEVIRTUAL           0xFD
#define MATROSKA_ID_CL_CODECSTATE                 0xA4
#define MATROSKA_ID_CL_SLICES                     0x8E
#define MATROSKA_ID_CL_TIMESLICE                  0xE8
#define MATROSKA_ID_CL_LACENUMBER                 0xCC
#define MATROSKA_ID_CL_FRAMENUMBER                0xCD
#define MATROSKA_ID_CL_BLOCKADDITIONID            0xCB
#define MATROSKA_ID_CL_DELAY                      0xCE
#define MATROSKA_ID_CL_DURATION                   0xCF

/* Track */
#define MATROSKA_ID_TRACKS                        0x1654AE6B
#define MATROSKA_ID_TR_ENTRY                      0xAE
#define MATROSKA_ID_TR_NUMBER                     0xD7
#define MATROSKA_ID_TR_UID                        0x73C5
#define MATROSKA_ID_TR_TYPE                       0x83
#define MATROSKA_ID_TR_FLAGENABLED                0xB9
#define MATROSKA_ID_TR_FLAGDEFAULT                0x88
#define MATROSKA_ID_TR_FLAGLACING                 0x9C
#define MATROSKA_ID_TR_MINCACHE                   0x6DE7
#define MATROSKA_ID_TR_MAXCACHE                   0x6DF8
#define MATROSKA_ID_TR_DEFAULTDURATION            0x23E383
#define MATROSKA_ID_TR_TIMECODESCALE              0x23314F
#define MATROSKA_ID_TR_NAME                       0x536E
#define MATROSKA_ID_TR_LANGUAGE                   0x22B59C
#define MATROSKA_ID_TR_CODECID                    0x86
#define MATROSKA_ID_TR_CODECPRIVATE               0x63A2
#define MATROSKA_ID_TR_CODECNAME                  0x258688
#define MATROSKA_ID_TR_CODECSETTINGS              0x3A9697
#define MATROSKA_ID_TR_CODECINFOURL               0x3B4040
#define MATROSKA_ID_TR_CODECDOWNLOADURL           0x26B240
#define MATROSKA_ID_TR_CODECDECODEALL             0xAA
#define MATROSKA_ID_TR_OVERLAY                    0x6FAB

/* Video */
#define MATROSKA_ID_TV                            0xE0
#define MATROSKA_ID_TV_FLAGINTERLACED             0x9A
#define MATROSKA_ID_TV_STEREOMODE                 0x53B9
#define MATROSKA_ID_TV_PIXELWIDTH                 0xB0
#define MATROSKA_ID_TV_PIXELHEIGHT                0xBA
#define MATROSKA_ID_TV_VIDEODISPLAYWIDTH          0x54B0
#define MATROSKA_ID_TV_VIDEODISPLAYHEIGHT         0x54BA
#define MATROSKA_ID_TV_VIDEOUNIT                  0x54B2
#define MATROSKA_ID_TV_ASPECTRATIOTYPE            0x54B3
#define MATROSKA_ID_TV_COLOURSPACE                0x2EB524
#define MATROSKA_ID_TV_GAMMAVALUE                 0x2FB523

/* Audio */
#define MATROSKA_ID_TA                            0xE1
#define MATROSKA_ID_TA_SAMPLINGFREQUENCY          0xB5
#define MATROSKA_ID_TA_OUTPUTSAMPLINGFREQUENCY    0x78B5
#define MATROSKA_ID_TA_CHANNELS                   0x9F
#define MATROSKA_ID_TA_CHANNELPOSITIONS           0x9F
#define MATROSKA_ID_TA_BITDEPTH                   0x6264

/* Content Encoding */
#define MATROSKA_ID_CONTENTENCODINGS              0x6D80
#define MATROSKA_ID_CONTENTENCODING               0x6240
#define MATROSKA_ID_CE_ORDER                      0x5031
#define MATROSKA_ID_CE_SCOPE                      0x5032
#define MATROSKA_ID_CE_TYPE                       0x5033
#define MATROSKA_ID_CE_COMPRESSION                0x5034
#define MATROSKA_ID_CE_COMPALGO                   0x4254
#define MATROSKA_ID_CE_COMPSETTINGS               0x4255
#define MATROSKA_ID_CE_ENCRYPTION                 0x5035
#define MATROSKA_ID_CE_ENCALGO                    0x47E1
#define MATROSKA_ID_CE_ENCKEYID                   0x47E2
#define MATROSKA_ID_CE_SIGNATURE                  0x47E3
#define MATROSKA_ID_CE_SIGKEYID                   0x47E4
#define MATROSKA_ID_CE_SIGALGO                    0x47E5
#define MATROSKA_ID_CE_SIGHASHALGO                0x47E6

/* Cueing Data */
#define MATROSKA_ID_CUES                          0x1C53BB6B
#define MATROSKA_ID_CU_POINT                      0xBB
#define MATROSKA_ID_CU_TIME                       0xB3
#define MATROSKA_ID_CU_TRACKPOSITION              0xB7
#define MATROSKA_ID_CU_TRACK                      0xF7
#define MATROSKA_ID_CU_CLUSTERPOSITION            0xF1
#define MATROSKA_ID_CU_BLOCKNUMBER                0x5387
#define MATROSKA_ID_CU_CODECSTATE                 0xEA
#define MATROSKA_ID_CU_REFERENCE                  0xDB
#define MATROSKA_ID_CU_REFTIME                    0x96
#define MATROSKA_ID_CU_REFCLUSTER                 0x97
#define MATROSKA_ID_CU_REFNUMBER                  0x535F
#define MATROSKA_ID_CU_REFCODECSTATE              0xEB

/* Attachements */
#define MATROSKA_ID_ATTACHMENTS                   0x1941A469
#define MATROSKA_ID_AT_FILE                       0x61A7
#define MATROSKA_ID_AT_FILEDESCRIPTION            0x467E
#define MATROSKA_ID_AT_FILENAME                   0x466E
#define MATROSKA_ID_AT_FILEMIMETYPE               0x4660
#define MATROSKA_ID_AT_FILEDATA                   0x465C
#define MATROSKA_ID_AT_FILEUID                    0x46AE

/* Chapters */
#define MATROSKA_ID_CHAPTERS                      0x1043A770
#define MATROSKA_ID_CH_EDITIONENTRY               0x45B9
#define MATROSKA_ID_CH_ED_UID                     0x45BC
#define MATROSKA_ID_CH_ED_HIDDEN                  0x45BD
#define MATROSKA_ID_CH_ED_DEFAULT                 0x45DB
#define MATROSKA_ID_CH_ED_ORDERED                 0x45DD
#define MATROSKA_ID_CH_ATOM                       0xB6
#define MATROSKA_ID_CH_UID                        0x73C4
#define MATROSKA_ID_CH_TIMESTART                  0x91
#define MATROSKA_ID_CH_TIMEEND                    0x92
#define MATROSKA_ID_CH_HIDDEN                     0x98
#define MATROSKA_ID_CH_ENABLED                    0x4598
#define MATROSKA_ID_CH_TRACK                      0x8F
#define MATROSKA_ID_CH_TRACKNUMBER                0x89
#define MATROSKA_ID_CH_DISPLAY                    0x80
#define MATROSKA_ID_CH_STRING                     0x85
#define MATROSKA_ID_CH_LANGUAGE                   0x437C
#define MATROSKA_ID_CH_COUNTRY                    0x437E

/* Tags */
#define MATROSKA_ID_TAGS                          0x1254C367

/* Chapter (used in tracks) */
typedef struct {
  uint64_t uid;
  uint64_t time_start;
  uint64_t time_end;
  /* if not 0, the chapter could e.g. be used for skipping, but not
   * be shown in the chapter list */
  int hidden;
  /* disabled chapters should be skipped during playback (using this
   * would require parsing control blocks) */
  int enabled;
  /* Tracks this chapter belongs to.
   * Remember that elements can occur in any order, so in theory the
   * chapters could become available before the tracks do.
   * TODO: currently unused
   */
  /* uint64_t* tracks; */
  /* Chapter titles and locale information
   * TODO: chapters can have multiple sets of those, i.e. several tuples
   *  (title, language, country). The current implementation picks from
   *  those by the following rules:
   *   1) remember the first element
   *   2) overwrite with an element where language=="eng"
   */
  char* title;
  char* language;
  char* country;
} matroska_chapter_t;

/* Edition */
typedef struct {
  uint64_t uid;
  unsigned int hidden;
  unsigned int is_default;
  unsigned int ordered;

  int num_chapters, cap_chapters;
  matroska_chapter_t** chapters;
} matroska_edition_t;

/* Matroska Track */
typedef struct {
  int                      flag_interlaced;
  int                      pixel_width;
  int                      pixel_height;
  int                      display_width;
  int                      display_height;
} matroska_video_track_t;

typedef struct {
  int                      sampling_freq;
  int                      output_sampling_freq;
  int                      channels;
  int                      bits_per_sample;
} matroska_audio_track_t;

typedef struct {
  char                     type;

  /* The rest is used for VobSubs (type = 'v'). */
  int                      width;
  int                      height;
  uint32_t                 palette[16];
  int                      custom_colors;
  uint32_t                 colors[4];
  int                      forced_subs_only;
} matroska_sub_track_t;

typedef struct matroska_track_s matroska_track_t;
struct matroska_track_s {
  int                      track_num;
  uint64_t uid;

  uint32_t                 track_type;
  uint64_t                 default_duration;
  char                    *language;
  char                    *codec_id;
  uint8_t                 *codec_private;
  uint32_t                 codec_private_len;
  int                      default_flag;
  uint32_t                 compress_algo;
  uint32_t                 compress_len;
  char                    *compress_settings;

  uint32_t                 buf_type;
  fifo_buffer_t           *fifo;

  matroska_video_track_t  *video_track;
  matroska_audio_track_t  *audio_track;
  matroska_sub_track_t    *sub_track;

  int64_t                  last_pts;

  void                   (*handle_content) (demux_plugin_t *this_gen,
                                            matroska_track_t *track,
		                            int decoder_flags,
                                            uint8_t *data, size_t data_len,
                                            int64_t data_pts, int data_duration,
                                            int input_normpos, int input_time);
};

/* IDs in the tags master */

/*
 * Matroska Codec IDs. Strings.
 */

#define MATROSKA_CODEC_ID_V_VFW_FOURCC   "V_MS/VFW/FOURCC"
#define MATROSKA_CODEC_ID_V_UNCOMPRESSED "V_UNCOMPRESSED"
#define MATROSKA_CODEC_ID_V_MPEG4_SP     "V_MPEG4/ISO/SP"
#define MATROSKA_CODEC_ID_V_MPEG4_ASP    "V_MPEG4/ISO/ASP"
#define MATROSKA_CODEC_ID_V_MPEG4_AP     "V_MPEG4/ISO/AP"
#define MATROSKA_CODEC_ID_V_MPEG4_AVC    "V_MPEG4/ISO/AVC"
#define MATROSKA_CODEC_ID_V_MSMPEG4V3    "V_MPEG4/MS/V3"
#define MATROSKA_CODEC_ID_V_MPEG1        "V_MPEG1"
#define MATROSKA_CODEC_ID_V_MPEG2        "V_MPEG2"
#define MATROSKA_CODEC_ID_V_MPEG2        "V_MPEG2"
#define MATROSKA_CODEC_ID_V_REAL_RV10    "V_REAL/RV10"
#define MATROSKA_CODEC_ID_V_REAL_RV20    "V_REAL/RV20"
#define MATROSKA_CODEC_ID_V_REAL_RV30    "V_REAL/RV30"
#define MATROSKA_CODEC_ID_V_REAL_RV40    "V_REAL/RV40"
#define MATROSKA_CODEC_ID_V_MJPEG        "V_MJPEG"
#define MATROSKA_CODEC_ID_V_THEORA       "V_THEORA"
#define MATROSKA_CODEC_ID_V_VP8          "V_VP8"

#define MATROSKA_CODEC_ID_A_MPEG1_L1     "A_MPEG/L1"
#define MATROSKA_CODEC_ID_A_MPEG1_L2     "A_MPEG/L2"
#define MATROSKA_CODEC_ID_A_MPEG1_L3     "A_MPEG/L3"
#define MATROSKA_CODEC_ID_A_PCM_INT_BE   "A_PCM/INT/BIG"
#define MATROSKA_CODEC_ID_A_PCM_INT_LE   "A_PCM/INT/LIT"
#define MATROSKA_CODEC_ID_A_PCM_FLOAT    "A_PCM/FLOAT/IEEE"
#define MATROSKA_CODEC_ID_A_AC3          "A_AC3"
#define MATROSKA_CODEC_ID_A_EAC3         "A_EAC3"
#define MATROSKA_CODEC_ID_A_DTS          "A_DTS"
#define MATROSKA_CODEC_ID_A_VORBIS       "A_VORBIS"
#define MATROSKA_CODEC_ID_A_ACM          "A_MS/ACM"
#define MATROSKA_CODEC_ID_A_AAC          "A_AAC"
#define MATROSKA_CODEC_ID_A_REAL_14_4    "A_REAL/14_4"
#define MATROSKA_CODEC_ID_A_REAL_28_8    "A_REAL/28_8"
#define MATROSKA_CODEC_ID_A_REAL_COOK    "A_REAL/COOK"
#define MATROSKA_CODEC_ID_A_REAL_SIPR    "A_REAL/SIPR"
#define MATROSKA_CODEC_ID_A_REAL_RALF    "A_REAL/RALF"
#define MATROSKA_CODEC_ID_A_REAL_ATRC    "A_REAL/ATRC"
#define MATROSKA_CODEC_ID_A_FLAC         "A_FLAC"

#define MATROSKA_CODEC_ID_S_TEXT_UTF8    "S_TEXT/UTF8"
#define MATROSKA_CODEC_ID_S_TEXT_SSA     "S_TEXT/SSA"
#define MATROSKA_CODEC_ID_S_TEXT_ASS     "S_TEXT/ASS"
#define MATROSKA_CODEC_ID_S_TEXT_USF     "S_TEXT/USF"
#define MATROSKA_CODEC_ID_S_UTF8         "S_UTF8"        /* deprecated */
#define MATROSKA_CODEC_ID_S_SSA          "S_SSA"         /* deprecated */
#define MATROSKA_CODEC_ID_S_ASS          "S_ASS"         /* deprecated */
#define MATROSKA_CODEC_ID_S_VOBSUB       "S_VOBSUB"
#define MATROSKA_CODEC_ID_S_HDMV_PGS     "S_HDMV/PGS"

/* block lacing */
#define MATROSKA_NO_LACING               0x0
#define MATROSKA_XIPH_LACING             0x1
#define MATROSKA_FIXED_SIZE_LACING       0x2
#define MATROSKA_EBML_LACING             0x3

/* track types */
#define MATROSKA_TRACK_VIDEO             0x01
#define MATROSKA_TRACK_AUDIO             0x02
#define MATROSKA_TRACK_COMPLEX           0x03
#define MATROSKA_TRACK_LOGO              0x10
#define MATROSKA_TRACK_SUBTITLE          0x11
#define MATROSKA_TRACK_CONTROL           0x20

/* compression algorithms */
#define MATROSKA_COMPRESS_ZLIB           0x00
#define MATROSKA_COMPRESS_BZLIB          0x01
#define MATROSKA_COMPRESS_LZO1X          0x02
#define MATROSKA_COMPRESS_HEADER_STRIP   0x03
#define MATROSKA_COMPRESS_UNKNOWN        0xFFFFFFFE  /* Xine internal type */
#define MATROSKA_COMPRESS_NONE           0xFFFFFFFF  /* Xine internal type */

#endif /* MATROSKA_H */
