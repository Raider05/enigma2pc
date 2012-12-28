/*
 * Copyright (C) 2000-2006 the xine project
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
 * demultiplexer for asf streams
 *
 * based on ffmpeg's
 * ASF compatible encoder and decoder.
 * Copyright (c) 2000, 2001 Gerard Lantau.
 *
 * GUID list from avifile
 * some other ideas from MPlayer
 */

#ifndef ASFHEADER_H
#define ASFHEADER_H

#include <inttypes.h>

/*
 * define asf GUIDs (list from avifile)
 */
#define GUID_ERROR                              0

    /* base ASF objects */
#define GUID_ASF_HEADER                         1
#define GUID_ASF_DATA                           2
#define GUID_ASF_SIMPLE_INDEX                   3
#define GUID_INDEX                              4
#define GUID_MEDIA_OBJECT_INDEX                 5
#define GUID_TIMECODE_INDEX                     6

    /* header ASF objects */
#define GUID_ASF_FILE_PROPERTIES                7
#define GUID_ASF_STREAM_PROPERTIES              8
#define GUID_ASF_HEADER_EXTENSION               9
#define GUID_ASF_CODEC_LIST                    10
#define GUID_ASF_SCRIPT_COMMAND                11
#define GUID_ASF_MARKER                        12
#define GUID_ASF_BITRATE_MUTUAL_EXCLUSION      13
#define GUID_ASF_ERROR_CORRECTION              14
#define GUID_ASF_CONTENT_DESCRIPTION           15
#define GUID_ASF_EXTENDED_CONTENT_DESCRIPTION  16
#define GUID_ASF_STREAM_BITRATE_PROPERTIES     17
#define GUID_ASF_EXTENDED_CONTENT_ENCRYPTION   18
#define GUID_ASF_PADDING                       19
    
    /* stream properties object stream type */
#define GUID_ASF_AUDIO_MEDIA                   20
#define GUID_ASF_VIDEO_MEDIA                   21
#define GUID_ASF_COMMAND_MEDIA                 22
#define GUID_ASF_JFIF_MEDIA                    23
#define GUID_ASF_DEGRADABLE_JPEG_MEDIA         24
#define GUID_ASF_FILE_TRANSFER_MEDIA           25
#define GUID_ASF_BINARY_MEDIA                  26

    /* stream properties object error correction type */
#define GUID_ASF_NO_ERROR_CORRECTION           27
#define GUID_ASF_AUDIO_SPREAD                  28

    /* mutual exclusion object exlusion type */
#define GUID_ASF_MUTEX_BITRATE                 29
#define GUID_ASF_MUTEX_UKNOWN                  30

    /* header extension */
#define GUID_ASF_RESERVED_1                    31
    
    /* script command */
#define GUID_ASF_RESERVED_SCRIPT_COMMNAND      32

    /* marker object */
#define GUID_ASF_RESERVED_MARKER               33

    /* various */
#define GUID_ASF_AUDIO_CONCEAL_NONE            34
#define GUID_ASF_CODEC_COMMENT1_HEADER         35
#define GUID_ASF_2_0_HEADER                    36

#define GUID_EXTENDED_STREAM_PROPERTIES        37
#define GUID_ADVANCED_MUTUAL_EXCLUSION         38
#define GUID_GROUP_MUTUAL_EXCLUSION            39
#define GUID_STREAM_PRIORITIZATION             40
#define GUID_BANDWIDTH_SHARING                 41
#define GUID_LANGUAGE_LIST                     42
#define GUID_METADATA                          43
#define GUID_METADATA_LIBRARY                  44
#define GUID_INDEX_PARAMETERS                  45
#define GUID_MEDIA_OBJECT_INDEX_PARAMETERS     46
#define GUID_TIMECODE_INDEX_PARAMETERS         47
#define GUID_ADVANCED_CONTENT_ENCRYPTION       48
#define GUID_COMPATIBILITY                     49
#define GUID_END                               50


/* asf stream types */
#define ASF_STREAM_TYPE_UNKNOWN           0
#define ASF_STREAM_TYPE_AUDIO             1
#define ASF_STREAM_TYPE_VIDEO             2
#define ASF_STREAM_TYPE_CONTROL           3
#define ASF_STREAM_TYPE_JFIF              4
#define ASF_STREAM_TYPE_DEGRADABLE_JPEG   5
#define ASF_STREAM_TYPE_FILE_TRANSFER     6
#define ASF_STREAM_TYPE_BINARY            7

#define ASF_MAX_NUM_STREAMS     23

#if !defined(GUID_DEFINED) && !defined(_GUID_DEFINED)
#define GUID_DEFINED
#define _GUID_DEFINED

typedef struct _GUID {          /* size is 16 */
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t  Data4[8];
} GUID;

#endif /* !GUID_DEFINED */

static const struct
{
    const char* name;
    const GUID  guid;
} guids[] =
{
    { "error",
    { 0x0,} },


    /* base ASF objects */
    { "header",
    { 0x75b22630, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c }} },

    { "data",
    { 0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c }} },

    { "simple index",
    { 0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb }} },

    { "index",
    { 0xd6e229d3, 0x35da, 0x11d1, { 0x90, 0x34, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xbe }} },

    { "media object index",
    { 0xfeb103f8, 0x12ad, 0x4c64, { 0x84, 0x0f, 0x2a, 0x1d, 0x2f, 0x7a, 0xd4, 0x8c }} },

    { "timecode index",
    { 0x3cb73fd0, 0x0c4a, 0x4803, { 0x95, 0x3d, 0xed, 0xf7, 0xb6, 0x22, 0x8f, 0x0c }} },

    /* header ASF objects */
    { "file properties",
    { 0x8cabdca1, 0xa947, 0x11cf, { 0x8e, 0xe4, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 }} },

    { "stream header",
    { 0xb7dc0791, 0xa9b7, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 }} },

    { "header extension",
    { 0x5fbf03b5, 0xa92e, 0x11cf, { 0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 }} },

    { "codec list",
    { 0x86d15240, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 }} },

    { "script command",
    { 0x1efb1a30, 0x0b62, 0x11d0, { 0xa3, 0x9b, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 }} },

    { "marker",
    { 0xf487cd01, 0xa951, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 }} },

    { "bitrate mutual exclusion",
    { 0xd6e229dc, 0x35da, 0x11d1, { 0x90, 0x34, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xbe }} },

    { "error correction",
    { 0x75b22635, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c }} },

    { "content description",
    { 0x75b22633, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c }} },

    { "extended content description",
    { 0xd2d0a440, 0xe307, 0x11d2, { 0x97, 0xf0, 0x00, 0xa0, 0xc9, 0x5e, 0xa8, 0x50 }} },

    { "stream bitrate properties", /* (http://get.to/sdp) */
    { 0x7bf875ce, 0x468d, 0x11d1, { 0x8d, 0x82, 0x00, 0x60, 0x97, 0xc9, 0xa2, 0xb2 }} },

    { "extended content encryption",
    { 0x298ae614, 0x2622, 0x4c17, { 0xb9, 0x35, 0xda, 0xe0, 0x7e, 0xe9, 0x28, 0x9c }} },

    { "padding",
    { 0x1806d474, 0xcadf, 0x4509, { 0xa4, 0xba, 0x9a, 0xab, 0xcb, 0x96, 0xaa, 0xe8 }} },


    /* stream properties object stream type */
    { "audio media",
    { 0xf8699e40, 0x5b4d, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b }} },

    { "video media",
    { 0xbc19efc0, 0x5b4d, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b }} },

    { "command media",
    { 0x59dacfc0, 0x59e6, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 }} },

    { "JFIF media (JPEG)",
    { 0xb61be100, 0x5b4e, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b }} },

    { "Degradable JPEG media",
    { 0x35907de0, 0xe415, 0x11cf, { 0xa9, 0x17, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b }} },

    { "File Transfer media",
    { 0x91bd222c, 0xf21c, 0x497a, { 0x8b, 0x6d, 0x5a, 0xa8, 0x6b, 0xfc, 0x01, 0x85 }} },

    { "Binary media",
    { 0x3afb65e2, 0x47ef, 0x40f2, { 0xac, 0x2c, 0x70, 0xa9, 0x0d, 0x71, 0xd3, 0x43 }} },

    /* stream properties object error correction */
    { "no error correction",
    { 0x20fb5700, 0x5b55, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b }} },

    { "audio spread",
    { 0xbfc3cd50, 0x618f, 0x11cf, { 0x8b, 0xb2, 0x00, 0xaa, 0x00, 0xb4, 0xe2, 0x20 }} },


    /* mutual exclusion object exlusion type */
    { "mutex bitrate",
    { 0xd6e22a01, 0x35da, 0x11d1, { 0x90, 0x34, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xbe }} },

    { "mutex unknown", 
    { 0xd6e22a02, 0x35da, 0x11d1, { 0x90, 0x34, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xbe }} },


    /* header extension */
    { "reserved_1",
    { 0xabd3d211, 0xa9ba, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 }} },


    /* script command */
    { "reserved script command",
    { 0x4B1ACBE3, 0x100B, 0x11D0, { 0xA3, 0x9B, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 }} },

    /* marker object */
    { "reserved marker",
    { 0x4CFEDB20, 0x75F6, 0x11CF, { 0x9C, 0x0F, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xCB }} },

    /* various */
    { "audio conceal none",
    { 0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 }} },

    { "codec comment1 header",
    { 0x86d15241, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 }} },

    { "asf 2.0 header",
    { 0xd6e229d1, 0x35da, 0x11d1, { 0x90, 0x34, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xbe }} },


    /* header extension GUIDs */ 
    { "extended stream properties",
    { 0x14E6A5CB, 0xC672, 0x4332, { 0x83, 0x99, 0xA9, 0x69, 0x52, 0x6, 0x5B, 0x5A }} },

    { "advanced mutual exclusion",
    { 0xA08649CF, 0x4775, 0x4670, { 0x8a, 0x16, 0x6e, 0x35, 0x35, 0x75, 0x66, 0xcd }} },

    { "group mutual exclusion",
    { 0xD1465A40, 0x5A79, 0x4338, { 0xb7, 0x1b, 0xe3, 0x6b, 0x8f, 0xd6, 0xc2, 0x49 }} },

    { "stream prioritization",
    { 0xD4FED15B, 0x88D3, 0x454F, { 0x81, 0xf0, 0xed, 0x5c, 0x45, 0x99, 0x9e, 0x24 }} },

    { "bandwidth sharing",
    { 0xA69609E6, 0x517B, 0x11D2, { 0xb6, 0xaf, 0x00, 0xc0, 0x4f, 0xd9, 0x08, 0xe9 }} },

    { "language list",
    { 0x7C4346A9, 0xEFE0, 0x4BFC, {0xB2, 0x29, 0x39, 0x3E, 0xDE, 0x41, 0x5C, 0x85}} },

    { "metadata",
    { 0xC5F8CBEA, 0x5BAF, 0x4877, {0x84, 0x67, 0xAA, 0x8C, 0x44, 0xFA, 0x4C, 0xCA}} },

    { "metadata library",
    { 0x44231C94, 0x9498, 0x49D1, {0xA1, 0x41, 0x1D, 0x13, 0x4E, 0x45, 0x70, 0x54}} },

    { "index parameters",
    { 0xD6E229DF, 0x35DA, 0x11D1, {0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE}} },

    { "media object index parameters",
    { 0x6B203BAD, 0x3F11, 0x48E4, {0xAC, 0xA8, 0xD7, 0x61, 0x3D, 0xE2, 0xCF, 0xA7}} },

    { "timecode index parameters",
    { 0xF55E496D, 0x9797, 0x4B5D, {0x8C, 0x8B, 0x60, 0x4D, 0xF9, 0x9B, 0xFB, 0x24}} },

    { "advanced content encryption",
    { 0x43058533, 0x6981, 0x49E6, {0x9B, 0x74, 0xAD, 0x12, 0xCB, 0x86, 0xD5, 0x8C}} },

    /* exotic stuff */
    { "compatibility",
    { 0x26F18B5D, 0x4584, 0x47EC, {0x9F, 0x5F, 0xE,0x65, 0x1F, 0x4, 0x52, 0xC9}} }
};

typedef struct asf_header_s asf_header_t;
typedef struct asf_file_s asf_file_t;
typedef struct asf_content_s asf_content_t;
typedef struct asf_stream_s asf_stream_t;
typedef struct asf_stream_extension_s asf_stream_extension_t;

struct asf_header_s {
  asf_file_t             *file;
  asf_content_t          *content;
  int                     stream_count;

  asf_stream_t           *streams[ASF_MAX_NUM_STREAMS];
  asf_stream_extension_t *stream_extensions[ASF_MAX_NUM_STREAMS];
  uint32_t                bitrates[ASF_MAX_NUM_STREAMS];
  struct { uint32_t x, y; } aspect_ratios[ASF_MAX_NUM_STREAMS];
};

struct asf_file_s {
  GUID     file_id;
  uint64_t file_size;              /* in bytes */
  uint64_t data_packet_count;
  uint64_t play_duration;          /* in 100 nanoseconds unit */
  uint64_t send_duration;          /* in 100 nanoseconds unit */
  uint64_t preroll;                /* in 100 nanoseconds unit */

  uint32_t packet_size;
  uint32_t max_bitrate;

  uint8_t  broadcast_flag;
  uint8_t  seekable_flag;
};

/* ms unicode strings */
struct asf_content_s {
  char     *title;
  char     *author;
  char     *copyright;
  char     *description;
  char     *rating;
};

struct asf_stream_s {
  uint16_t  stream_number;
  int       stream_type;
  int       error_correction_type;
  uint64_t  time_offset;

  uint32_t  private_data_length;
  uint8_t  *private_data;

  uint32_t  error_correction_data_length;
  uint8_t  *error_correction_data;

  uint8_t   encrypted_flag;
};

struct asf_stream_extension_s {
  uint64_t start_time;
  uint64_t end_time;
  uint32_t data_bitrate;
  uint32_t buffer_size;
  uint32_t initial_buffer_fullness;
  uint32_t alternate_data_bitrate;
  uint32_t alternate_buffer_size;
  uint32_t alternate_initial_buffer_fullness;
  uint32_t max_object_size;

  uint8_t  reliable_flag;
  uint8_t  seekable_flag;
  uint8_t  no_cleanpoints_flag;
  uint8_t  resend_live_cleanpoints_flag;

  uint16_t language_id;
  uint64_t average_time_per_frame;

  uint16_t stream_name_count;
  uint16_t payload_extension_system_count;

  char   **stream_names;
};

int asf_find_object_id (GUID *g);
void asf_get_guid (uint8_t *buffer, GUID *value);

asf_header_t *asf_header_new (uint8_t *buffer, int buffer_len) XINE_MALLOC;
void asf_header_choose_streams (asf_header_t *header, uint32_t bandwidth,
                                int *video_id, int *audio_id);
void asf_header_disable_streams (asf_header_t *header,
                                 int video_id, int audio_id);
void asf_header_delete (asf_header_t *header);


#endif
