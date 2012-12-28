/*
 * Copyright (C) 2006 the xine project
 * Based on the FLAC File Demuxer by Mike Melanson
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

#ifndef __FLACUTILS_H__
#define __FLACUTILS_H__

typedef struct {
  off_t offset;
  int64_t sample_number;
  int64_t pts;
  int size;
} flac_seekpoint_t;

#define FLAC_SIGNATURE_SIZE 4
#define FLAC_STREAMINFO_SIZE 34
#define FLAC_SEEKPOINT_SIZE 18

enum {
  FLAC_BLOCKTYPE_STREAMINFO,
  FLAC_BLOCKTYPE_PADDING,
  FLAC_BLOCKTYPE_APPLICATION,
  FLAC_BLOCKTYPE_SEEKTABLE,
  FLAC_BLOCKTYPE_VORBIS_COMMENT,
  FLAC_BLOCKTYPE_CUESHEET,
  FLAC_BLOCKTYPE_INVALID = 127
};

/*
 * WARNING: These structures are *not* using the same format
 *          used by FLAC files, bitwise.
 *
 * Using bitfields to read the whole data is unfeasible because
 * of endianness problems with non-byte-aligned values.
 */

typedef struct {
  uint8_t last;
  uint8_t blocktype;
  uint32_t length;
} xine_flac_metadata_header;

typedef struct {
  uint16_t blocksize_min;
  uint16_t blocksize_max;
  uint32_t framesize_min;
  uint32_t framesize_max;
  uint32_t samplerate;
  uint8_t channels;
  uint8_t bits_per_sample;
  uint64_t total_samples;
  uint8_t md5[16];
} xine_flac_streaminfo_block;

static inline void _x_parse_flac_metadata_header(uint8_t *buffer, xine_flac_metadata_header *parsed) {
  parsed->last = buffer[0] & 0x80 ? 1 : 0;
  parsed->blocktype = buffer[0] & 0x7f;

  parsed->length = _X_BE_24(&buffer[1]);
}

static inline void _x_parse_flac_streaminfo_block(uint8_t *buffer, xine_flac_streaminfo_block *parsed) {
  parsed->blocksize_min = _X_BE_16(&buffer[0]);
  parsed->blocksize_max = _X_BE_16(&buffer[2]);
  parsed->framesize_min = _X_BE_24(&buffer[4]);
  parsed->framesize_max = _X_BE_24(&buffer[7]);
  parsed->samplerate = _X_BE_32(&buffer[10]);
  parsed->channels = ((parsed->samplerate >> 9) & 0x07) + 1;
  parsed->bits_per_sample = ((parsed->samplerate >> 4) & 0x1F) + 1;
  parsed->samplerate >>= 12;
  parsed->total_samples = _X_BE_64(&buffer[10]) & UINT64_C(0x0FFFFFFFFF);  /* 36 bits */
}

#endif
