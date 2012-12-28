/*
 * Copyright (C) 2000-2007 the xine project
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
 * ID3 tag parser
 *
 * Supported versions: v1, v1.1, v2.2, v2.3, v2.4
 */

#ifndef ID3_H
#define ID3_H

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "bswap.h"

/* id3v2 */
#define ID3V22_TAG        ME_FOURCC('I', 'D', '3', 2) /* id3 v2.2 header tag */
#define ID3V23_TAG        ME_FOURCC('I', 'D', '3', 3) /* id3 v2.3 header tag */
#define ID3V24_TAG        ME_FOURCC('I', 'D', '3', 4) /* id3 v2.4 header tag */
#define ID3V24_FOOTER_TAG ME_FOURCC('3', 'D', 'I', 0) /* id3 v2.4 footer tag */

#define ID3V2X_TAG        ME_FOURCC('I', 'D', '3', 0) /* id3 v2.x header tag */
#define ID3V2X_MASK      ~ME_FOURCC( 0 ,  0 ,  0 , 0xFF) /* id3 v2.x header mask */

/*
 *  ID3 v2.2
 */
/* tag header */
#define ID3V22_UNSYNCH_FLAG               0x80
#define ID3V22_COMPRESS_FLAG              0x40
#define ID3V22_ZERO_FLAG                  0x3F

/* frame header */
#define ID3V22_FRAME_HEADER_SIZE             6

/*
 *  ID3 v2.3
 */
/* tag header */
#define ID3V23_UNSYNCH_FLAG               0x80
#define ID3V23_EXT_HEADER_FLAG            0x40
#define ID3V23_EXPERIMENTAL_FLAG          0x20
#define ID3V23_ZERO_FLAG                  0x1F

/* frame header */
#define ID3V23_FRAME_HEADER_SIZE            10
#define ID3V23_FRAME_TAG_PRESERV_FLAG   0x8000
#define ID3V23_FRAME_FILE_PRESERV_FLAG  0x4000
#define ID3V23_FRAME_READ_ONLY_FLAG     0x2000
#define ID3V23_FRAME_COMPRESS_FLAG      0x0080
#define ID3V23_FRAME_ENCRYPT_FLAG       0x0040
#define ID3V23_FRAME_GROUP_ID_FLAG      0x0020
#define ID3V23_FRAME_ZERO_FLAG          0x1F1F

/*
 *  ID3 v2.4
 */
/* tag header */
#define ID3V24_UNSYNCH_FLAG               0x80
#define ID3V24_EXT_HEADER_FLAG            0x40
#define ID3V24_EXPERIMENTAL_FLAG          0x20
#define ID3V24_FOOTER_FLAG                0x10
#define ID3V24_ZERO_FLAG                  0x0F

/* extended header */
#define ID3V24_EXT_UPDATE_FLAG            0x40
#define ID3V24_EXT_CRC_FLAG               0x20
#define ID3V24_EXT_RESTRICTIONS_FLAG      0x10
#define ID3V24_EXT_ZERO_FLAG              0x8F

/* frame header */
#define ID3V24_FRAME_HEADER_SIZE            10
#define ID3V24_FRAME_TAG_PRESERV_FLAG   0x4000
#define ID3V24_FRAME_FILE_PRESERV_FLAG  0x2000
#define ID3V24_FRAME_READ_ONLY_FLAG     0x1000
#define ID3V24_FRAME_GROUP_ID_FLAG      0x0040
#define ID3V24_FRAME_COMPRESS_FLAG      0x0008
#define ID3V24_FRAME_ENCRYPT_FLAG       0x0004
#define ID3V24_FRAME_UNSYNCH_FLAG       0x0002
#define ID3V24_FRAME_DATA_LEN_FLAG      0x0001
#define ID3V24_FRAME_ZERO_FLAG          0x8FB0

/* footer */
#define ID3V24_FOOTER_SIZE                  10


typedef struct {
  uint32_t  id;
  uint8_t   revision;
  uint8_t   flags;
  size_t    size;
} id3v2_header_t;

typedef struct {
  uint32_t  id;
  size_t    size;
} id3v22_frame_header_t;

typedef struct {
  uint32_t  id;
  size_t    size;
  uint16_t  flags;
} id3v23_frame_header_t;

typedef struct {
  size_t    size;
  uint16_t  flags;
  uint32_t  padding_size;
  uint32_t  crc;
} id3v23_frame_ext_header_t;

typedef id3v2_header_t id3v24_footer_t;

typedef struct {
  uint32_t  id;
  size_t    size;
  uint16_t  flags;
} id3v24_frame_header_t;

typedef struct {
  size_t    size;
  uint8_t   flags;
  uint32_t  crc;
  uint8_t   restrictions;
} id3v24_frame_ext_header_t;

typedef struct {
  char    tag[3];
  char    title[30];
  char    artist[30];
  char    album[30];
  char    year[4];
  char    comment[30];
  uint8_t genre;
} id3v1_tag_t;

int id3v1_parse_tag (input_plugin_t *input, xine_stream_t *stream);

/**
 * @brief Generic function for ID3v2 tags parsing.
 * @param input Pointer to the input plugin used by the demuxer, used
 *              to access the tag's data.
 * @param stream Pointer to the xine stream currently being read.
 * @param mp3_frame_header Header of the MP3 frame carrying the tag.
 *
 * @note This function will take care of calling the proper function for
 *       parsing ID3v2.2, ID3v2.3 or ID3v2.4 tags.
 */
int id3v2_parse_tag(input_plugin_t *input,
		    xine_stream_t *stream,
		    uint32_t id3_signature);

/**
 * @brief Checks if the given buffer is an ID3 tag preamble
 * @param ptr Pointer to the first 10 bytes of the ID3 tag
 */
static inline int id3v2_istag(uint32_t id3_signature) {
  return (id3_signature & ID3V2X_MASK) == ID3V2X_TAG;
}

#if 0
/* parse an unsynchronized 16bits integer */
static inline uint16_t _X_BE_16_synchsafe(uint8_t buf[2]) {
  return ((uint16_t)(buf[0] & 0x7F) << 7) |
          (uint16_t)(buf[1] & 0x7F);
}
#endif

/* parse an unsynchronized 24bits integer */
static inline uint32_t _X_BE_24_synchsafe(uint8_t buf[3]) {
  return ((uint32_t)(buf[0] & 0x7F) << 14) |
         ((uint32_t)(buf[1] & 0x7F) << 7) |
          (uint32_t)(buf[2] & 0x7F);
}

/* parse an unsynchronized 32bits integer */
static inline uint32_t _X_BE_32_synchsafe(uint8_t buf[4]) {
  return ((uint32_t)(buf[0] & 0x7F) << 21) |
         ((uint32_t)(buf[1] & 0x7F) << 14) |
         ((uint32_t)(buf[2] & 0x7F) << 7) |
          (uint32_t)(buf[3] & 0x7F);
}

/* parse an unsynchronized 35bits integer */
static inline uint32_t BE_35_synchsafe(uint8_t buf[5]) {
  return ((uint32_t)(buf[0] & 0x07) << 28) |
         ((uint32_t)(buf[1] & 0x7F) << 21) |
         ((uint32_t)(buf[2] & 0x7F) << 14) |
         ((uint32_t)(buf[3] & 0x7F) << 7) |
          (uint32_t)(buf[4] & 0x7F);
}

#endif /* ID3_H */
