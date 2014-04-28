/*
 * Copyright (C) 2001-2014 the xine project
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
 * Quicktime File Demuxer by Mike Melanson (melanson@pcisys.net)
 *  based on a Quicktime parsing experiment entitled 'lazyqt'
 *
 * Atom finder, trak builder rewrite, multiaudio and ISO fragment
 *  media file support by Torsten Jager (t.jager@gmx.de)
 *
 * Ideally, more documentation is forthcoming, but in the meantime:
 * functional flow:
 *  create_qt_info
 *  open_qt_file
 *   parse_moov_atom
 *    parse_mvhd_atom
 *    parse_trak_atom
 *    build_frame_table
 *  free_qt_info
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zlib.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"

#include "qtpalette.h"

typedef unsigned int qt_atom;

#define QT_ATOM BE_FOURCC
/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')
#define FTYP_ATOM QT_ATOM('f', 't', 'y', 'p')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')

#define MVHD_ATOM QT_ATOM('m', 'v', 'h', 'd')

#define VMHD_ATOM QT_ATOM('v', 'm', 'h', 'd')
#define SMHD_ATOM QT_ATOM('s', 'm', 'h', 'd')

#define TRAK_ATOM QT_ATOM('t', 'r', 'a', 'k')
#define TKHD_ATOM QT_ATOM('t', 'k', 'h', 'd')
#define MDHD_ATOM QT_ATOM('m', 'd', 'h', 'd')
#define ELST_ATOM QT_ATOM('e', 'l', 's', 't')

/* atoms in a sample table */
#define STSD_ATOM QT_ATOM('s', 't', 's', 'd')
#define STSZ_ATOM QT_ATOM('s', 't', 's', 'z')
#define STZ2_ATOM QT_ATOM('s', 't', 'z', '2')
#define STSC_ATOM QT_ATOM('s', 't', 's', 'c')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define STTS_ATOM QT_ATOM('s', 't', 't', 's')
#define CTTS_ATOM QT_ATOM('c', 't', 't', 's')
#define STSS_ATOM QT_ATOM('s', 't', 's', 's')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ESDS_ATOM QT_ATOM('e', 's', 'd', 's')
#define WAVE_ATOM QT_ATOM('w', 'a', 'v', 'e')
#define FRMA_ATOM QT_ATOM('f', 'r', 'm', 'a')
#define AVCC_ATOM QT_ATOM('a', 'v', 'c', 'C')
#define ENDA_ATOM QT_ATOM('e', 'n', 'd', 'a')

#define IMA4_FOURCC ME_FOURCC('i', 'm', 'a', '4')
#define MAC3_FOURCC ME_FOURCC('M', 'A', 'C', '3')
#define MAC6_FOURCC ME_FOURCC('M', 'A', 'C', '6')
#define ULAW_FOURCC ME_FOURCC('u', 'l', 'a', 'w')
#define ALAW_FOURCC ME_FOURCC('a', 'l', 'a', 'w')
#define MP4A_FOURCC ME_FOURCC('m', 'p', '4', 'a')
#define SAMR_FOURCC ME_FOURCC('s', 'a', 'm', 'r')
#define ALAC_FOURCC ME_FOURCC('a', 'l', 'a', 'c')
#define DRMS_FOURCC ME_FOURCC('d', 'r', 'm', 's')
#define TWOS_FOURCC ME_FOURCC('t', 'w', 'o', 's')
#define SOWT_FOURCC ME_FOURCC('s', 'o', 'w', 't')
#define RAW_FOURCC  ME_FOURCC('r', 'a', 'w', ' ')
#define IN24_FOURCC ME_FOURCC('i', 'n', '2', '4')
#define NI42_FOURCC ME_FOURCC('4', '2', 'n', 'i')
#define AVC1_FOURCC ME_FOURCC('a', 'v', 'c', '1')
#define AC_3_FOURCC ME_FOURCC('a', 'c', '-', '3')
#define EAC3_FOURCC ME_FOURCC('e', 'c', '-', '3')
#define QCLP_FOURCC ME_FOURCC('Q', 'c', 'l', 'p')

#define UDTA_ATOM QT_ATOM('u', 'd', 't', 'a')
#define META_ATOM QT_ATOM('m', 'e', 't', 'a')
#define HDLR_ATOM QT_ATOM('h', 'd', 'l', 'r')
#define ILST_ATOM QT_ATOM('i', 'l', 's', 't')
#define NAM_ATOM QT_ATOM(0xA9, 'n', 'a', 'm')
#define CPY_ATOM QT_ATOM(0xA9, 'c', 'p', 'y')
#define DES_ATOM QT_ATOM(0xA9, 'd', 'e', 's')
#define CMT_ATOM QT_ATOM(0xA9, 'c', 'm', 't')
#define ALB_ATOM QT_ATOM(0xA9, 'a', 'l', 'b')
#define GEN_ATOM QT_ATOM(0xA9, 'g', 'e', 'n')
#define ART_ATOM QT_ATOM(0xA9, 'A', 'R', 'T')
#define TOO_ATOM QT_ATOM(0xA9, 't', 'o', 'o')
#define WRT_ATOM QT_ATOM(0xA9, 'w', 'r', 't')
#define DAY_ATOM QT_ATOM(0xA9, 'd', 'a', 'y')

#define RMRA_ATOM QT_ATOM('r', 'm', 'r', 'a')
#define RMDA_ATOM QT_ATOM('r', 'm', 'd', 'a')
#define RDRF_ATOM QT_ATOM('r', 'd', 'r', 'f')
#define RMDR_ATOM QT_ATOM('r', 'm', 'd', 'r')
#define RMVC_ATOM QT_ATOM('r', 'm', 'v', 'c')
#define QTIM_ATOM QT_ATOM('q', 't', 'i', 'm')
#define URL__ATOM QT_ATOM('u', 'r', 'l', ' ')
#define DATA_ATOM QT_ATOM('d', 'a', 't', 'a')

/* fragment stuff */
#define MVEX_ATOM QT_ATOM('m', 'v', 'e', 'x')
#define MEHD_ATOM QT_ATOM('m', 'e', 'h', 'd')
#define TREX_ATOM QT_ATOM('t', 'r', 'e', 'x')
#define MOOF_ATOM QT_ATOM('m', 'o', 'o', 'f')
#define MFHD_ATOM QT_ATOM('m', 'v', 'h', 'd')
#define TRAF_ATOM QT_ATOM('t', 'r', 'a', 'f')
#define TFHD_ATOM QT_ATOM('t', 'f', 'h', 'd')
#define TRUN_ATOM QT_ATOM('t', 'r', 'u', 'n')

/* placeholder for cutting and pasting
#define _ATOM QT_ATOM('', '', '', '')
*/

#define ATOM_PREAMBLE_SIZE 8
#define PALETTE_COUNT 256

#define MAX_PTS_DIFF 100000

/**
 * @brief Network bandwidth, cribbed from src/input/input_mms.c
 */
static const int64_t bandwidths[]={14400,19200,28800,33600,34430,57600,
                            115200,262200,393216,524300,1544000,10485800};

/* these are things that can go wrong */
typedef enum {
  QT_OK,
  QT_FILE_READ_ERROR,
  QT_NO_MEMORY,
  QT_NOT_A_VALID_FILE,
  QT_NO_MOOV_ATOM,
  QT_NO_ZLIB,
  QT_ZLIB_ERROR,
  QT_HEADER_TROUBLE,
  QT_DRM_NOT_SUPPORTED
} qt_error;

/* there are other types but these are the ones we usually care about */
typedef enum {

  MEDIA_AUDIO,
  MEDIA_VIDEO,
  MEDIA_OTHER

} media_type;

typedef struct {
  int64_t offset;
  unsigned int size;
  /* pts actually is dts for reordered video. Edit list and frame
     duration code relies on that, so keep the offset separately
     until sending to video fifo.
     Value is small enough for plain int. */
  int ptsoffs;
  int64_t pts;
  int keyframe;
  unsigned int media_id;
} qt_frame;

typedef struct {
  unsigned int track_duration;
  unsigned int media_time;
} edit_list_table_t;

typedef struct {
  unsigned int first_chunk;
  unsigned int samples_per_chunk;
  unsigned int media_id;
} sample_to_chunk_table_t;

typedef struct {
  char *url;
  int64_t data_rate;
  int qtim_version;
} reference_t;

typedef union {

  struct {
    /* the media id that corresponds to this trak */
    unsigned int media_id;

    /* offset into the stsd atom of the properties atom */
    unsigned int properties_offset;

    unsigned int codec_fourcc;
    unsigned int codec_buftype;
    unsigned int width;
    unsigned int height;
    int palette_count;
    palette_entry_t palette[PALETTE_COUNT];
    int depth;
    int edit_list_compensation;  /* special trick for edit lists */

    unsigned char *properties_atom;
    unsigned int properties_atom_size;
  } video;

  struct {
    /* the media id that corresponds to this trak */
    unsigned int media_id;

    /* offset into the stsd atom of the properties atom */
    unsigned int properties_offset;

    unsigned int codec_fourcc;
    unsigned int codec_buftype;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int bits;
    unsigned int vbr;
    unsigned int wave_size;
    xine_waveformatex *wave;

    /* special audio parameters */
    unsigned int samples_per_packet;
    unsigned int bytes_per_packet;
    unsigned int bytes_per_frame;
    unsigned int bytes_per_sample;
    unsigned int samples_per_frame;

    unsigned char *properties_atom;
    unsigned int properties_atom_size;
  } audio;

} properties_t;

typedef struct {

  /* trak description */
  media_type type;
  int id;

  /* one or more properties atoms for this trak */
  properties_t *stsd_atoms;
  int stsd_atoms_count;

  /* this is the current properties atom in use */
  properties_t *properties;

  /* internal frame table corresponding to this trak */
  qt_frame *frames;
  unsigned int frame_count;
  unsigned int current_frame;

  /* trak timescale */
  unsigned int timescale;

  /* flags that indicate how a trak is supposed to be used */
  unsigned int flags;

  /* formattag-like field that specifies codec in mp4 files */
  unsigned int object_type_id;

  /* decoder data pass information to the decoder */
  void *decoder_config;
  int decoder_config_len;

  /****************************************/
  /* temporary tables for loading a chunk */

  /* edit list table */
  unsigned int edit_list_count;
  edit_list_table_t *edit_list_table;

  /* chunk offsets */
  unsigned int chunk_offset_count;
  unsigned char *chunk_offset_table32;
  unsigned char *chunk_offset_table64;

  /* sample sizes */
  unsigned int samples;
  unsigned int sample_size;
  unsigned int sample_size_count;
  unsigned int sample_size_bits;
  unsigned char *sample_size_table;

  /* sync samples, a.k.a., keyframes */
  unsigned int sync_sample_count;
  unsigned char *sync_sample_table;

  /* sample to chunk table */
  unsigned int sample_to_chunk_count;
  sample_to_chunk_table_t *sample_to_chunk_table;

  /* time to sample table */
  unsigned int time_to_sample_count;
  unsigned char *time_to_sample_table;

  /* pts to dts timeoffset to sample table */
  unsigned int timeoffs_to_sample_count;
  unsigned char *timeoffs_to_sample_table;

  /* what to add to output buffer type */
  int audio_index;

  int lang;

  /* fragment defaults */
  int default_sample_description_index;
  int default_sample_duration;
  int default_sample_size;
  int default_sample_flags;
  /* fragment seamless dts */
  int64_t fragment_dts;
  /* fragment frame array size */
  int fragment_frames;
} qt_trak;

typedef struct {
  int compressed_header;  /* 1 if there was a compressed moov; just FYI */

  unsigned int creation_time;  /* in ms since Jan-01-1904 */
  unsigned int modification_time;
  unsigned int timescale;  /* base clock frequency is Hz */
  unsigned int duration;

  int64_t moov_first_offset;

  int               trak_count;
  qt_trak          *traks;

#define MAX_AUDIO_TRAKS 8
  int               audio_trak_count;
  int               audio_traks[MAX_AUDIO_TRAKS];

  /* the trak numbers that won their respective frame count competitions */
  int               video_trak;
  int               audio_trak;
  int seek_flag;  /* this is set to indicate that a seek has just occurred */

  /* fragment mode */
  int               fragment_count;

  char              *artist;
  char              *name;
  char              *album;
  char              *genre;
  char              *copyright;
  char              *description;
  char              *comment;
  char              *composer;
  char              *year;

  /* a QT movie may contain a number of references pointing to URLs */
  reference_t       *references;
  int                reference_count;
  int                chosen_reference;

  /* need to know base MRL to construct URLs from relative paths */
  char              *base_mrl;

  qt_error last_error;
} qt_info;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  qt_info             *qt;
  xine_bmiheader       bih;
  unsigned int         current_frame;
  unsigned int         last_frame;

  off_t                data_start;
  off_t                data_size;

  int64_t              bandwidth;

  char                 last_mrl[1024];
} demux_qt_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_qt_class_t;

/**********************************************************************
 * lazyqt special debugging functions
 **********************************************************************/

/* define DEBUG_ATOM_LOAD as 1 to get a verbose parsing of the relevant
 * atoms */
#define DEBUG_ATOM_LOAD 0

/* define DEBUG_EDIT_LIST as 1 to get a detailed look at how the demuxer is
 * handling edit lists */
#define DEBUG_EDIT_LIST 0

/* define DEBUG_FRAME_TABLE as 1 to dump the complete frame table that the
 * demuxer plans to use during file playback */
#define DEBUG_FRAME_TABLE 0

/* define DEBUG_VIDEO_DEMUX as 1 to see details about the video chunks the
 * demuxer is sending off to the video decoder */
#define DEBUG_VIDEO_DEMUX 0

/* define DEBUG_AUDIO_DEMUX as 1 to see details about the audio chunks the
 * demuxer is sending off to the audio decoder */
#define DEBUG_AUDIO_DEMUX 0

/* define DEBUG_META_LOAD as 1 to see details about the metadata chunks the
 * demuxer is reading from the file */
#define DEBUG_META_LOAD 0

/* Define DEBUG_DUMP_MOOV as 1 to dump the raw moov atom to disk. This is
 * particularly useful in debugging a file with a compressed moov (cmov)
 * atom. The atom will be dumped to the filename specified as
 * RAW_MOOV_FILENAME. */
#define DEBUG_DUMP_MOOV 0
#define RAW_MOOV_FILENAME "moovatom.raw"

#if DEBUG_ATOM_LOAD
#define debug_atom_load printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_atom_load(const char *format, ...) {}
#endif

#if DEBUG_EDIT_LIST
#define debug_edit_list printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_edit_list(const char *format, ...) {}
#endif

#if DEBUG_FRAME_TABLE
#define debug_frame_table printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_frame_table(const char *format, ...) {}
#endif

#if DEBUG_VIDEO_DEMUX
#define debug_video_demux printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_video_demux(const char *format, ...) {}
#endif

#if DEBUG_AUDIO_DEMUX
#define debug_audio_demux printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_audio_demux(const char *format, ...) {}
#endif

#if DEBUG_META_LOAD
#define debug_meta_load printf
#else
static inline void XINE_FORMAT_PRINTF(1, 2) debug_meta_load(const char *format, ...) {}
#endif

static inline void dump_moov_atom(unsigned char *moov_atom, int moov_atom_size) {
#if DEBUG_DUMP_MOOV

  FILE *f;

  f = fopen(RAW_MOOV_FILENAME, "wb");
  if (!f) {
    perror(RAW_MOOV_FILENAME);
    return;
  }

  if (fwrite(moov_atom, moov_atom_size, 1, f) != 1)
    printf ("  qt debug: could not write moov atom to disk\n");

  fclose(f);

#endif
}

/**********************************************************************
 * lazyqt functions
 **********************************************************************/

/*
 * This function traverses a file and looks for a moov atom. Returns the
 * file offset of the beginning of the moov atom (that means the offset
 * of the 4-byte length preceding the characters 'moov'). Returns -1
 * if no moov atom was found.
 *
 * Note: Do not count on the input stream being positioned anywhere in
 * particular when this function is finished.
 */
static void find_moov_atom(input_plugin_t *input, off_t *moov_offset,
  int64_t *moov_size) {

  off_t atom_size;
  qt_atom atom;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];
  int unknown_atoms = 0;

  off_t free_moov_offset = -1;
  int64_t free_moov_size = 0;

  /* init the passed variables */
  *moov_offset = *moov_size = -1;

  /* take it from the top */
  if (input->seek(input, 0, SEEK_SET) != 0)
    return;

  /* traverse through the input */
  while (*moov_offset == -1) {
    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      break;

    atom_size = _X_BE_32(&atom_preamble[0]);
    atom = _X_BE_32(&atom_preamble[4]);

    /* Special case alert: 'free' atoms sometimes masquerade as 'moov'
     * atoms. If this is a free atom, check for 'cmov' or 'mvhd' immediately
     * following. QT Player can handle it, so xine should too. */
    if (atom == FREE_ATOM) {

      /* get the next atom preamble */
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      /* if there is a cmov, qualify this free atom as the 'moov' atom
       * if no actual 'moov' atom is found. */
      if ((_X_BE_32(&atom_preamble[4]) == CMOV_ATOM) ||
          (_X_BE_32(&atom_preamble[4]) == MVHD_ATOM)) {
        /* pos = current pos minus 2 atom preambles */
        free_moov_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE * 2;
        free_moov_size = atom_size;
      }

      /* rewind the stream so we can keep looking */
      input->seek(input, -ATOM_PREAMBLE_SIZE, SEEK_CUR);
    }

    /* if the moov atom is found, log the position and break from the loop */
    if (atom == MOOV_ATOM) {
      *moov_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE;
      *moov_size = atom_size;
      break;
    }

    /* if this atom is not the moov atom, make sure that it is at least one
     * of the other top-level QT atom.
     * However, allow a configurable amount ( currently 1 ) atom be a
     * non known atom, in hopes a known atom will be found */
    if ((atom != FREE_ATOM) &&
        (atom != JUNK_ATOM) &&
        (atom != MDAT_ATOM) &&
        (atom != PNOT_ATOM) &&
        (atom != SKIP_ATOM) &&
        (atom != WIDE_ATOM) &&
        (atom != PICT_ATOM) &&
        (atom != FTYP_ATOM) ) {
      if (unknown_atoms > 1)
        break;
      else
       unknown_atoms++;
    }

    /* 0 special case-- just skip the atom */
    if (atom_size == 0)
      atom_size = 8;
    /* 64-bit length special case */
    if (atom_size == 1) {
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      atom_size = _X_BE_32(&atom_preamble[0]);
      atom_size <<= 32;
      atom_size |= _X_BE_32(&atom_preamble[4]);
      atom_size -= ATOM_PREAMBLE_SIZE * 2;
    } else
      atom_size -= ATOM_PREAMBLE_SIZE;

    input->seek(input, atom_size, SEEK_CUR);
  }

  if ((*moov_offset == -1) && (free_moov_offset != -1)) {
    *moov_offset = free_moov_offset;
    *moov_size = free_moov_size;
  }

  /* reset to the start of the stream on the way out */
  input->seek(input, 0, SEEK_SET);
}

/* create a qt_info structure or return NULL if no memory */
static qt_info *create_qt_info(void) {
  qt_info *info;

  info = (qt_info *)calloc(1, sizeof(qt_info));

  if (!info)
    return NULL;

  info->compressed_header = 0;

  info->creation_time = 0;
  info->modification_time = 0;
  info->timescale = 0;
  info->duration = 0;

  info->trak_count = 0;
  info->traks = NULL;

  info->video_trak = -1;
  info->audio_trak = -1;

  info->artist = NULL;
  info->name = NULL;
  info->album = NULL;
  info->genre = NULL;
  info->copyright = NULL;
  info->description = NULL;
  info->comment = NULL;
  info->composer = NULL;
  info->year = NULL;

  info->references = NULL;
  info->reference_count = 0;
  info->chosen_reference = -1;

  info->base_mrl = NULL;

  info->last_error = QT_OK;

  return info;
}

/* release a qt_info structure and associated data */
static void free_qt_info(qt_info *info) {

  int i, j;

  if(info) {
    if(info->traks) {
      for (i = 0; i < info->trak_count; i++) {
        free(info->traks[i].frames);
        free(info->traks[i].edit_list_table);
        free(info->traks[i].sample_to_chunk_table);
        free(info->traks[i].decoder_config);
        for (j = 0; j < info->traks[i].stsd_atoms_count; j++) {
          if (info->traks[i].type == MEDIA_AUDIO) {
            free(info->traks[i].stsd_atoms[j].audio.properties_atom);
	    free(info->traks[i].stsd_atoms[j].audio.wave);
          } else if (info->traks[i].type == MEDIA_VIDEO)
            free(info->traks[i].stsd_atoms[j].video.properties_atom);
        }
        free(info->traks[i].stsd_atoms);
      }
      free(info->traks);
    }
    if(info->references) {
      for (i = 0; i < info->reference_count; i++)
        free(info->references[i].url);
      free(info->references);
    }
    free(info->base_mrl);
    free(info->artist);
    free(info->name);
    free(info->album);
    free(info->genre);
    free(info->copyright);
    free(info->description);
    free(info->comment);
    free(info->composer);
    free(info->year);
    free(info);
    info = NULL;
  }
}

/* returns 1 if the file is determined to be a QT file, 0 otherwise */
static int is_qt_file(input_plugin_t *qt_file) {

  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;
  int i;

  /* if the input is non-seekable, be much more stringent about qualifying
   * a QT file: In this case, the moov must be the first atom in the file */
  if ((qt_file->get_capabilities(qt_file) & INPUT_CAP_SEEKABLE) == 0) {
    unsigned char preview[MAX_PREVIEW_SIZE] = { 0, };
    qt_file->get_optional_data(qt_file, preview, INPUT_OPTIONAL_DATA_PREVIEW);
    if (_X_BE_32(&preview[4]) == MOOV_ATOM)
      return 1;
    else {
      if (_X_BE_32(&preview[4]) != FTYP_ATOM)
	return 0;

      /* show some lenience if the first atom is 'ftyp'; the second atom
       * could be 'moov'
       * compute the size of the current atom plus the preamble of the
       * next atom; if the size is within the range on the preview buffer
       * then the next atom's preamble is in the preview buffer */
      uint64_t ftyp_atom_size = _X_BE_32(&preview[0]) + ATOM_PREAMBLE_SIZE;
      if (ftyp_atom_size >= MAX_PREVIEW_SIZE)
	return 0;
      return _X_BE_32(&preview[ftyp_atom_size - 4]) == MOOV_ATOM;
    }
  }

  find_moov_atom(qt_file, &moov_atom_offset, &moov_atom_size);
  if (moov_atom_offset == -1) {
    return 0;
  } else {
    unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];
    /* check that the next atom in the chunk contains alphanumeric
     * characters in the atom type field; if not, disqualify the file
     * as a QT file */
    qt_file->seek(qt_file, moov_atom_offset + ATOM_PREAMBLE_SIZE, SEEK_SET);
    if (qt_file->read(qt_file, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      return 0;

    for (i = 4; i < 8; i++)
      if (!isalnum(atom_preamble[i]))
        return 0;
    return 1;
  }
}

/* fetch interesting information from the movie header atom */
static void parse_mvhd_atom(qt_info *info, unsigned char *mvhd_atom) {

  info->creation_time = _X_BE_32(&mvhd_atom[0x0C]);
  info->modification_time = _X_BE_32(&mvhd_atom[0x10]);
  info->timescale = _X_BE_32(&mvhd_atom[0x14]);
  info->duration = _X_BE_32(&mvhd_atom[0x18]);

  debug_atom_load("  qt: timescale = %d, duration = %d (%d seconds)\n",
    info->timescale, info->duration,
    info->duration / info->timescale);
}

/* helper function from mplayer's parse_mp4.c */
static int mp4_read_descr_len(unsigned char *s, uint32_t *length) {
  uint8_t b;
  uint8_t numBytes = 0;

  *length = 0;

  do {
    b = *s++;
    numBytes++;
    *length = (*length << 7) | (b & 0x7F);
  } while ((b & 0x80) && numBytes < 4);

  return numBytes;
}

#define WRITE_BE_32(v,p) { \
  unsigned char *wp = (unsigned char *)(p); \
  uint32_t wv = (v); \
  wp[0] = wv >> 24; \
  wp[1] = wv >> 16; \
  wp[2] = wv >> 8; \
  wp[3] = wv; \
}

/* find a sub atom somewhere inside this atom */
static unsigned char *find_embedded_atom (unsigned char *atom, unsigned int type, unsigned int *size) {
  unsigned int atomsize, subtype, subsize = 0, i;

  *size = 0;
  if (!atom)
    return NULL;
  atomsize = _X_BE_32 (atom);

  for (i = 8; i + 8 <= atomsize; i++) {
    subtype = _X_BE_32 (&atom[i + 4]);
    if (subtype == type) {
      subsize = _X_BE_32 (&atom[i]);
      /* zero size means: extend to the end of parent container, */
      /* or end of file if a top level atom */
      if (subsize == 0) {
        subsize = atomsize - i;
        WRITE_BE_32 (subsize, &atom[i]);
      }
      if (i + subsize > atomsize)
        continue;
      *size = subsize;
#if DEBUG_ATOM_LOAD
      xine_hexdump (atom + i, subsize);
#endif
      return atom + i;
    }
  }

  return NULL;
}

static int atom_scan (     /** << return value: # of missing atoms. */
  unsigned char  *atom,    /** << the atom to parse. */
  int             depth,   /** << how many levels of hierarchy to examine. */
  unsigned int   *types,   /** << zero terminated list of interesting atom types. */
  unsigned char **found,   /** << list of atom pointers to fill in. */
  unsigned int   *sizes) { /** << list of atom sizes to fill in. */
  const unsigned char containers[] =
    /* look into these from "trak". */
    "edtsmdiaminfdinfstbl"
    /* look into these from "moov" (intentionally hide "trak"). */
    "udtametailstiprosinfrmrarmdardrfrmvc";
  unsigned int atomtype, atomsize, subtype = 0, subsize = 0;
  unsigned int i = 8, j, n, left;

  if (!atom || !types || !found)
    return 0;
  if (depth > 0) {
    for (n = 0; types[n]; n++) {
      found[n] = NULL;
      sizes[n] = 0;
    }
    left = n;
    depth = -depth;
  } else {
    for (left = n = 0; types[n]; n++)
      if (!(found[n]))
        left++;
  }

  atomsize = _X_BE_32 (atom);
  atomtype = _X_BE_32 (&atom[4]);
  if (atomtype == META_ATOM) {
    if ((atomsize < 12) || (atom[8] != 0))
      return left;
    i = 12;
  }
  
  for (; i + 8 <= atomsize; i += subsize) {
    subsize = _X_BE_32 (&atom[i]);
    subtype = _X_BE_32 (&atom[i + 4]);
    if (subsize == 0) {
      subsize = atomsize - i;
      WRITE_BE_32 (subsize, &atom[i]);
    }
    if ((subsize < 8) || (i + subsize > atomsize))
      break;
    for (n = 0; types[n]; n++) {
      if (found[n])
        continue;
      if (!(subtype ^ types[n])) {
#if DEBUG_ATOM_LOAD
        xine_hexdump (atom + i, subsize);
#endif
        found[n] = atom + i;
        sizes[n] = subsize;
        if (!(--left))
          return 0;
        break;
      }
    }
    if (depth > -2)
      continue;
    for (j = 0; j < sizeof (containers) - 1; j += 4) {
      if (!memcmp (atom + i + 4, containers + j, 4)) {
        if (!(left = atom_scan (atom + i, depth + 1, types, found, sizes)))
          return 0;
        break;
      }
    }
  }

  return left;
}

/*
 * This function traverses through a trak atom searching for the sample
 * table atoms, which it loads into an internal trak structure.
 */
static qt_error parse_trak_atom (qt_trak *trak,
				 unsigned char *trak_atom) {
  unsigned char *atom;
  int i, j, k;
  unsigned int atomsize, atom_pos;
  unsigned int properties_offset;
  qt_error last_error = QT_OK;

  unsigned int types[] = {
    VMHD_ATOM, SMHD_ATOM, TKHD_ATOM, ELST_ATOM,
    MDHD_ATOM, STSD_ATOM, STSZ_ATOM, STSS_ATOM,
    STCO_ATOM, CO64_ATOM, STSC_ATOM, STTS_ATOM,
    CTTS_ATOM, STZ2_ATOM, 0};
  unsigned int sizes[14];
  unsigned char *atoms[14];

  /* initialize trak structure */
  trak->id = -1;
  trak->edit_list_count = 0;
  trak->edit_list_table = NULL;
  trak->chunk_offset_count = 0;
  trak->chunk_offset_table32 = NULL;
  trak->chunk_offset_table64 = NULL;
  trak->samples = 0;
  trak->sample_size = 0;
  trak->sample_size_count = 0;
  trak->sample_size_bits = 0;
  trak->sample_size_table = NULL;
  trak->sync_sample_count = 0;
  trak->sync_sample_table = NULL;
  trak->sample_to_chunk_count = 0;
  trak->sample_to_chunk_table = NULL;
  trak->time_to_sample_count = 0;
  trak->time_to_sample_table = NULL;
  trak->timeoffs_to_sample_count = 0;
  trak->timeoffs_to_sample_table = NULL;
  trak->frames = NULL;
  trak->frame_count = 0;
  trak->current_frame = 0;
  trak->timescale = 0;
  trak->flags = 0;
  trak->object_type_id = 0;
  trak->decoder_config = NULL;
  trak->decoder_config_len = 0;
  trak->stsd_atoms_count = 0;
  trak->stsd_atoms = NULL;

  /* default type */
  trak->type = MEDIA_OTHER;

  /* search for media type atoms */
  atom_scan (trak_atom, 5, types, atoms, sizes);

  if (atoms[0]) /* VMHD_ATOM */
    trak->type = MEDIA_VIDEO;
  else if (atoms[1]) /* SMHD_ATOM */
    trak->type = MEDIA_AUDIO;

  debug_atom_load("  qt: parsing %s trak atom\n",
    (trak->type == MEDIA_VIDEO) ? "video" :
      (trak->type == MEDIA_AUDIO) ? "audio" : "other");

  /* search for the useful atoms */
  atom     = atoms[2]; /* TKHD_ATOM */
  atomsize = sizes[2];
  if (atomsize >= 12) {
    trak->flags = _X_BE_24(&atom[9]);
    if (atom[8] == 1) {
      if (atomsize >= 32)
        trak->id = _X_BE_32 (&atom[28]);
    } else {
      if (atomsize >= 24)
        trak->id = _X_BE_32 (&atom[20]);
    }
  }

  atom     = atoms[3]; /* ELST_ATOM */
  atomsize = sizes[3];
  if (atomsize >= 16) {
    trak->edit_list_count = _X_BE_32 (&atom[12]);
    if (trak->edit_list_count > (atomsize - 16) / 12)
      trak->edit_list_count = (atomsize - 16) / 12;
    debug_atom_load ("    qt elst atom (edit list atom): %d entries\n", trak->edit_list_count);
    /* dont bail on zero items */
    trak->edit_list_table = calloc (trak->edit_list_count + 1, sizeof (edit_list_table_t));
    if (!trak->edit_list_table) {
      last_error = QT_NO_MEMORY;
      goto free_trak;
    }
    /* load the edit list table */
    for (j = 0; j < trak->edit_list_count; j++) {
      trak->edit_list_table[j].track_duration = _X_BE_32 (&atom[16 + j * 12 + 0]);
      trak->edit_list_table[j].media_time     = _X_BE_32 (&atom[16 + j * 12 + 4]);
      debug_atom_load ("      %d: track duration = %d, media time = %d\n",
        j, trak->edit_list_table[j].track_duration, trak->edit_list_table[j].media_time);
    }
  }

  atom     = atoms[4]; /* MDHD_ATOM */
  atomsize = sizes[4];
  if (atomsize >= 12) {
    const int version = atom[8];
    debug_atom_load ("demux_qt: mdhd atom\n");
    if (version == 0) {
      if (atomsize >= 30) {
        trak->timescale = _X_BE_32 (&atom[20]);
        trak->lang      = _X_BE_16 (&atom[28]);
      }
    } else if (version == 1) {
      if (atomsize >= 42) {
        trak->timescale = _X_BE_32 (&atom[28]);
        trak->lang      = _X_BE_16 (&atom[40]);
      }
    }
  }

  atom     = atoms[5]; /* STSD_ATOM */
  atomsize = sizes[5];
  if (atomsize >= 16) {
    debug_atom_load ("demux_qt: stsd atom\n");
    /* allocate space for each of the properties unions */
    trak->stsd_atoms_count = _X_BE_32 (&atom[12]);
    if (trak->stsd_atoms_count <= 0) {
      last_error = QT_HEADER_TROUBLE;
      goto free_trak;
    }
    trak->stsd_atoms = calloc (trak->stsd_atoms_count, sizeof (properties_t));
    if (!trak->stsd_atoms) {
      last_error = QT_NO_MEMORY;
      goto free_trak;
    }

    atom_pos = 20;
    properties_offset = 12;
    for (k = 0; k < trak->stsd_atoms_count; k++) {
      properties_t *p = trak->stsd_atoms + k;
      const uint32_t current_stsd_atom_size = _X_BE_32(&atom[atom_pos - 4]);
      if (current_stsd_atom_size < 4) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      if (trak->type == MEDIA_VIDEO) {
        /* for palette traversal */
        int color_depth;
        int color_flag;
        int color_start;
        int color_count;
        int color_end;
        int color_index;
        int color_dec;
        int color_greyscale;
        const unsigned char *color_table;

        p->video.media_id = k + 1;
        p->video.properties_offset = properties_offset;

        /* copy the properties atom */
        p->video.properties_atom_size = current_stsd_atom_size - 4;
        p->video.properties_atom =
          xine_xmalloc (p->video.properties_atom_size);
        if (!p->video.properties_atom) {
          last_error = QT_NO_MEMORY;
          goto free_trak;
        }
        memcpy (p->video.properties_atom, &atom[atom_pos],
          p->video.properties_atom_size);

        /* initialize to sane values */
        p->video.width = 0;
        p->video.height = 0;
        p->video.depth = 0;

        /* assume no palette at first */
        p->video.palette_count = 0;

        /* fetch video parameters */
        if( _X_BE_16(&atom[atom_pos + 0x1C]) &&
            _X_BE_16(&atom[atom_pos + 0x1E]) ) {
          p->video.width =
            _X_BE_16(&atom[atom_pos + 0x1C]);
          p->video.height =
            _X_BE_16(&atom[atom_pos + 0x1E]);
        }
        p->video.codec_fourcc =
          _X_ME_32(&atom[atom_pos + 0x00]);

        /* figure out the palette situation */
        color_depth = atom[atom_pos + 0x4F];
        p->video.depth = color_depth;
        color_greyscale = color_depth & 0x20;
        color_depth &= 0x1F;

        /* if the depth is 2, 4, or 8 bpp, file is palettized */
        if ((color_depth == 2) || (color_depth == 4) || (color_depth == 8)) {
          color_flag = _X_BE_16 (&atom[atom_pos + 0x50]);

          if (color_greyscale) {

            p->video.palette_count = 1 << color_depth;

            /* compute the greyscale palette */
            color_index = 255;
            color_dec = 256 / (p->video.palette_count - 1);
            for (j = 0; j < p->video.palette_count; j++) {
              p->video.palette[j].r = color_index;
              p->video.palette[j].g = color_index;
              p->video.palette[j].b = color_index;
              color_index -= color_dec;
              if (color_index < 0)
                color_index = 0;
            }

          } else if (color_flag & 0x08) {

            /* if flag bit 3 is set, load the default palette */
            p->video.palette_count = 1 << color_depth;

            if (color_depth == 2)
              color_table = qt_default_palette_4;
            else if (color_depth == 4)
              color_table = qt_default_palette_16;
            else
              color_table = qt_default_palette_256;

            for (j = 0; j < p->video.palette_count; j++) {
              p->video.palette[j].r = color_table[j * 4 + 0];
              p->video.palette[j].g = color_table[j * 4 + 1];
              p->video.palette[j].b = color_table[j * 4 + 2];
            }

          } else {

            /* load the palette from the file */
            color_start = _X_BE_32 (&atom[atom_pos + 0x52]);
            color_count = _X_BE_16 (&atom[atom_pos + 0x56]);
            color_end   = _X_BE_16 (&atom[atom_pos + 0x58]);
            p->video.palette_count = color_end + 1;

            for (j = color_start; j <= color_end; j++) {

              color_index = _X_BE_16 (&atom[atom_pos + 0x5A + j * 8]);
              if (color_count & 0x8000)
                color_index = j;
              if (color_index < p->video.palette_count) {
                p->video.palette[color_index].r =
                  atom[atom_pos + 0x5A + j * 8 + 2];
                p->video.palette[color_index].g =
                  atom[atom_pos + 0x5A + j * 8 + 4];
                p->video.palette[color_index].b =
                  atom[atom_pos + 0x5A + j * 8 + 6];
              }
            }
          }
        } else
          p->video.palette_count = 0;

        debug_atom_load("    video properties atom #%d\n", k + 1);
        debug_atom_load("      %dx%d, video fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
          p->video.width,
          p->video.height,
          atom[atom_pos + 0x0],
          atom[atom_pos + 0x1],
          atom[atom_pos + 0x2],
          atom[atom_pos + 0x3],
          atom[atom_pos + 0x0],
          atom[atom_pos + 0x1],
          atom[atom_pos + 0x2],
          atom[atom_pos + 0x3]);
        debug_atom_load("      %d RGB colours\n",
          p->video.palette_count);
        for (j = 0; j < p->video.palette_count; j++)
          debug_atom_load("        %d: %3d %3d %3d\n",
            j,
            p->video.palette[j].r,
            p->video.palette[j].g,
            p->video.palette[j].b);

      } else if (trak->type == MEDIA_AUDIO) {

        p->audio.media_id          = k + 1;
        p->audio.properties_offset = properties_offset;

        /* copy the properties atom */
        p->audio.properties_atom_size = current_stsd_atom_size - 4;
        p->audio.properties_atom      = xine_xmalloc (p->audio.properties_atom_size);
        if (!p->audio.properties_atom) {
          last_error = QT_NO_MEMORY;
          goto free_trak;
        }
        memcpy (p->audio.properties_atom, &atom[atom_pos], p->audio.properties_atom_size);

        /* fetch audio parameters */
        p->audio.codec_fourcc = _X_ME_32 (&atom[atom_pos + 0x0]);
        p->audio.sample_rate  = _X_BE_16 (&atom[atom_pos + 0x1C]);
        p->audio.channels     = atom[atom_pos + 0x15];
        p->audio.bits         = atom[atom_pos + 0x17];

        /* 24-bit audio doesn't always declare itself properly, and can be big- or little-endian */
        if (p->audio.codec_fourcc == IN24_FOURCC) {
          p->audio.bits = 24;
          if (_X_BE_32 (&atom[atom_pos + 0x48]) == ENDA_ATOM && atom[atom_pos + 0x4D])
            p->audio.codec_fourcc = NI42_FOURCC;
        }

        /* assume uncompressed audio parameters */
        p->audio.bytes_per_sample   = p->audio.bits / 8;
        p->audio.samples_per_frame  = p->audio.channels;
        p->audio.bytes_per_frame    = p->audio.bytes_per_sample * p->audio.samples_per_frame;
        p->audio.samples_per_packet = p->audio.samples_per_frame;
        p->audio.bytes_per_packet   = p->audio.bytes_per_sample;

        /* special case time: A lot of CBR audio codecs stored in the
         * early days lacked the extra header; compensate */
        if (p->audio.codec_fourcc == IMA4_FOURCC) {
          p->audio.samples_per_packet = 64;
          p->audio.bytes_per_packet   = 34;
          p->audio.bytes_per_frame    = 34 * p->audio.channels;
          p->audio.bytes_per_sample   = 2;
          p->audio.samples_per_frame  = 64 * p->audio.channels;
        } else if (p->audio.codec_fourcc == MAC3_FOURCC) {
          p->audio.samples_per_packet = 3;
          p->audio.bytes_per_packet   = 1;
          p->audio.bytes_per_frame    = 1 * p->audio.channels;
          p->audio.bytes_per_sample   = 1;
          p->audio.samples_per_frame  = 3 * p->audio.channels;
        } else if (p->audio.codec_fourcc == MAC6_FOURCC) {
          p->audio.samples_per_packet = 6;
          p->audio.bytes_per_packet   = 1;
          p->audio.bytes_per_frame    = 1 * p->audio.channels;
          p->audio.bytes_per_sample   = 1;
          p->audio.samples_per_frame  = 6 * p->audio.channels;
        } else if (p->audio.codec_fourcc == ALAW_FOURCC) {
          p->audio.samples_per_packet = 1;
          p->audio.bytes_per_packet   = 1;
          p->audio.bytes_per_frame    = 1 * p->audio.channels;
          p->audio.bytes_per_sample   = 2;
          p->audio.samples_per_frame  = 2 * p->audio.channels;
        } else if (p->audio.codec_fourcc == ULAW_FOURCC) {
          p->audio.samples_per_packet = 1;
          p->audio.bytes_per_packet   = 1;
          p->audio.bytes_per_frame    = 1 * p->audio.channels;
          p->audio.bytes_per_sample   = 2;
          p->audio.samples_per_frame  = 2 * p->audio.channels;
        }

        /* it's time to dig a little deeper to determine the real audio
         * properties; if a the stsd compressor atom has 0x24 bytes, it
         * appears to be a handler for uncompressed data; if there are an
         * extra 0x10 bytes, there are some more useful decoding params;
         * further, do not do load these parameters if the audio is just
         * PCM ('raw ', 'twos', 'sowt' or 'in24') */
        if ((current_stsd_atom_size > 0x24) &&
            (p->audio.codec_fourcc != AC_3_FOURCC) &&
            (p->audio.codec_fourcc != EAC3_FOURCC) &&
            (p->audio.codec_fourcc != TWOS_FOURCC) &&
            (p->audio.codec_fourcc != SOWT_FOURCC) &&
            (p->audio.codec_fourcc != RAW_FOURCC)  &&
            (p->audio.codec_fourcc != IN24_FOURCC) &&
            (p->audio.codec_fourcc != NI42_FOURCC)) {

          if (_X_BE_32 (&atom[atom_pos + 0x20]))
            p->audio.samples_per_packet = _X_BE_32 (&atom[atom_pos + 0x20]);
          if (_X_BE_32 (&atom[atom_pos + 0x24]))
            p->audio.bytes_per_packet   = _X_BE_32 (&atom[atom_pos + 0x24]);
          if (_X_BE_32 (&atom[atom_pos + 0x28]))
            p->audio.bytes_per_frame    = _X_BE_32 (&atom[atom_pos + 0x28]);
          if (_X_BE_32 (&atom[atom_pos + 0x2C]))
            p->audio.bytes_per_sample   = _X_BE_32 (&atom[atom_pos + 0x2C]);
          if (p->audio.bytes_per_packet)
            p->audio.samples_per_frame =
              (p->audio.bytes_per_frame /
               p->audio.bytes_per_packet) *
               p->audio.samples_per_packet;
        }

        /* see if the trak deserves a promotion to VBR */
        if (_X_BE_16 (&atom[atom_pos + 0x18]) == 0xFFFE)
          p->audio.vbr = 1;
        else
          p->audio.vbr = 0;

        /* if this is MP4 audio, mark the trak as VBR */
        if (p->audio.codec_fourcc == MP4A_FOURCC)
          p->audio.vbr = 1;

        if (p->audio.codec_fourcc == SAMR_FOURCC)
          p->audio.vbr = 1;

        if (p->audio.codec_fourcc == AC_3_FOURCC)
          p->audio.vbr = 1;

        if (p->audio.codec_fourcc == EAC3_FOURCC)
          p->audio.vbr = 1;

        if (p->audio.codec_fourcc == ALAC_FOURCC) {
          p->audio.vbr = 1;
          /* further, FFmpeg's ALAC decoder requires 36 out-of-band bytes */
          p->audio.properties_atom_size = 36;
          p->audio.properties_atom = xine_xmalloc (p->audio.properties_atom_size);
          if (!p->audio.properties_atom) {
            last_error = QT_NO_MEMORY;
            goto free_trak;
          }
          memcpy (p->audio.properties_atom, &atom[atom_pos + 0x20], p->audio.properties_atom_size);
        }

        if (p->audio.codec_fourcc == QCLP_FOURCC)
          p->audio.vbr = 1;

        if (p->audio.codec_fourcc == DRMS_FOURCC) {
          last_error = QT_DRM_NOT_SUPPORTED;
          goto free_trak;
        }

        /* check for a MS-style WAVE format header */
        if ((atomsize >= 0x4C) &&
          (_X_BE_32 (&atom[atom_pos + 0x34]) == WAVE_ATOM) &&
          (_X_BE_32 (&atom[atom_pos + 0x3C]) == FRMA_ATOM) &&
          (_X_ME_32 (&atom[atom_pos + 0x48]) == p->audio.codec_fourcc)) {
          const int wave_size = _X_BE_32 (&atom[atom_pos + 0x44]) - 8;

          if ((wave_size >= sizeof(xine_waveformatex)) &&
              (atomsize >= (0x4C + wave_size))) {
            p->audio.wave_size = wave_size;
            p->audio.wave = xine_xmalloc (wave_size);
            if (!p->audio.wave) {
              last_error = QT_NO_MEMORY;
              goto free_trak;
            }
            memcpy (p->audio.wave, &atom[atom_pos + 0x4C], wave_size);
            _x_waveformatex_le2me(p->audio.wave);
          } else {
            p->audio.wave_size = 0;
            p->audio.wave = NULL;
          }
        } else {
          p->audio.wave_size = 0;
          p->audio.wave = NULL;
        }

        debug_atom_load("    audio properties atom #%d\n", k + 1);
        debug_atom_load("      %d Hz, %d bits, %d channels, %saudio fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
          p->audio.sample_rate,
          p->audio.bits,
          p->audio.channels,
          (p->audio.vbr) ? "vbr, " : "",
          atom[atom_pos + 0x0],
          atom[atom_pos + 0x1],
          atom[atom_pos + 0x2],
          atom[atom_pos + 0x3],
          atom[atom_pos + 0x0],
          atom[atom_pos + 0x1],
          atom[atom_pos + 0x2],
          atom[atom_pos + 0x3]);
        if (current_stsd_atom_size > 0x24) {
          debug_atom_load("      %d samples/packet, %d bytes/packet, %d bytes/frame\n",
            p->audio.samples_per_packet,
            p->audio.bytes_per_packet,
            p->audio.bytes_per_frame);
          debug_atom_load("      %d bytes/sample (%d samples/frame)\n",
            p->audio.bytes_per_sample,
            p->audio.samples_per_frame);
        }
      }
      /* use first audio properties atom for now */
      trak->properties = &trak->stsd_atoms[0];

      /* forward to the next atom */
      atom_pos += current_stsd_atom_size;
      properties_offset += current_stsd_atom_size;
    }
  }

  atom = find_embedded_atom (atoms[5], ESDS_ATOM, &atomsize);
  if (atomsize > 12) {
    debug_atom_load ("    qt/mpeg-4 esds atom\n");
    if ((trak->type == MEDIA_VIDEO) || (trak->type == MEDIA_AUDIO)) {
      uint32_t len;
      j = 12;
      if (atom[j++] == 0x03) {
        j += mp4_read_descr_len (&atom[j], &len);
        j++;
      }
      j += 2;
      if (atom[j++] == 0x04) {
        j += mp4_read_descr_len (&atom[j], &len);
        trak->object_type_id = atom[j++];
        debug_atom_load ("      object type id is %d\n", trak->object_type_id);
        j += 12;
        if (atom[j++] == 0x05) {
          j += mp4_read_descr_len (&atom[j], &len);
          debug_atom_load ("      decoder config is %d (0x%X) bytes long\n", len, len);
          if (len > atomsize - j) {
            last_error = QT_NOT_A_VALID_FILE;
            goto free_trak;
          }
          trak->decoder_config = realloc (trak->decoder_config, len);
          trak->decoder_config_len = len;
          if (!trak->decoder_config) {
            last_error = QT_NO_MEMORY;
            goto free_trak;
          }
          memcpy(trak->decoder_config,&atom[j],len);
        }
      }
    }
  }

  atom = find_embedded_atom (atoms[5], AVCC_ATOM, &atomsize);
  if (atomsize > 8) {
    debug_atom_load ("    avcC atom\n");
    trak->decoder_config_len = atomsize - 8;
    trak->decoder_config = realloc (trak->decoder_config, trak->decoder_config_len);
    if (!trak->decoder_config) {
      last_error = QT_NO_MEMORY;
      goto free_trak;
    }
    memcpy (trak->decoder_config, atom + 8, trak->decoder_config_len);
  }

  atom     = atoms[6]; /* STSZ_ATOM */
  atomsize = sizes[6];
  if (atomsize >= 20) {
    trak->sample_size       = _X_BE_32(&atom[12]);
    trak->sample_size_count = _X_BE_32(&atom[16]);
    trak->samples = trak->sample_size_count;
    if (trak->sample_size_count > (atomsize - 20) / 4)
      trak->sample_size_count = (atomsize - 20) / 4;
    debug_atom_load ("    qt stsz atom (sample size atom): sample size = %d, %d entries\n",
      trak->sample_size, trak->sample_size_count);
    /* load table only if sample size is 0 */
    /* there may be 0 items + moof fragments later */
    if (trak->sample_size == 0)
      trak->sample_size_table = atom + 20;
    trak->sample_size_bits = 32;
  } else {
    atom     = atoms[13]; /* STZ2_ATOM */
    atomsize = sizes[13];
    if (atomsize >= 20) {
      trak->sample_size_count = _X_BE_32 (&atom[16]);
      trak->sample_size_bits = atom[15];
      trak->sample_size_table = atom + 20;
      if (atom[15] == 16) {
        if (trak->sample_size_count > (atomsize - 20) / 2)
          trak->sample_size_count = (atomsize - 20) / 2;
      } else if (atom[15] == 8) {
        if (trak->sample_size_count > (atomsize - 20))
          trak->sample_size_count = atomsize - 20;
      } else {
        trak->sample_size_count = 0;
        trak->sample_size_bits = 0;
        trak->sample_size_table = NULL;
      }
    }
  }

  atom     = atoms[7]; /* STSS_ATOM */
  atomsize = sizes[7];
  if (atomsize >= 16) {
    trak->sync_sample_count = _X_BE_32 (&atom[12]);
    if (trak->sync_sample_count > (atomsize - 16) / 4)
      trak->sync_sample_count = (atomsize - 16) / 4;
    debug_atom_load ("    qt stss atom (sample sync atom): %d sync samples\n",
      trak->sync_sample_count);
    trak->sync_sample_table = atom + 16;
  }

  atom     = atoms[8]; /* STCO_ATOM */
  atomsize = sizes[8];
  if (atomsize >= 16) {
    trak->chunk_offset_count = _X_BE_32 (&atom[12]);
    debug_atom_load ("    qt stco atom (32-bit chunk offset atom): %d chunk offsets\n",
      trak->chunk_offset_count);
    if (trak->chunk_offset_count > (atomsize - 16) / 4)
      trak->chunk_offset_count = (atomsize - 16) / 4;
    trak->chunk_offset_table32 = atom + 16;
  } else {
    atom     = atoms[9]; /* CO64_ATOM */
    atomsize = sizes[9];
    if (atomsize >= 16) {
      trak->chunk_offset_count = _X_BE_32 (&atom[12]);
      if (trak->chunk_offset_count > (atomsize - 16) / 8)
        trak->chunk_offset_count = (atomsize - 16) / 8;
      debug_atom_load ("    qt co64 atom (64-bit chunk offset atom): %d chunk offsets\n",
        trak->chunk_offset_count);
      trak->chunk_offset_table64 = atom + 16;
    }
  }

  atom     = atoms[10]; /* STSC_ATOM */
  atomsize = sizes[10];
  if (atomsize >= 16) {
    trak->sample_to_chunk_count = _X_BE_32 (&atom[12]);
    if (trak->sample_to_chunk_count > (atomsize - 16) / 12)
      trak->sample_to_chunk_count = (atomsize - 16) / 12;
    debug_atom_load ("    qt stsc atom (sample-to-chunk atom): %d entries\n",
      trak->sample_to_chunk_count);
    trak->sample_to_chunk_table = calloc (trak->sample_to_chunk_count + 1, sizeof (sample_to_chunk_table_t));
    if (!trak->sample_to_chunk_table) {
      last_error = QT_NO_MEMORY;
      goto free_trak;
    }
    /* load the sample to chunk table */
    for (j = 0; j < trak->sample_to_chunk_count; j++) {
      trak->sample_to_chunk_table[j].first_chunk       = _X_BE_32 (&atom[16 + j * 12 + 0]);
      trak->sample_to_chunk_table[j].samples_per_chunk = _X_BE_32 (&atom[16 + j * 12 + 4]);
      trak->sample_to_chunk_table[j].media_id          = _X_BE_32 (&atom[16 + j * 12 + 8]);
      debug_atom_load ("      %d: %d samples/chunk starting at chunk %d (%d) for media id %d\n",
        j, trak->sample_to_chunk_table[j].samples_per_chunk,
        trak->sample_to_chunk_table[j].first_chunk,
        trak->sample_to_chunk_table[j].first_chunk - 1,
        trak->sample_to_chunk_table[j].media_id);
    }
  }

  atom     = atoms[11]; /* STTS_ATOM */
  atomsize = sizes[11];
  if (atomsize >= 16) {
    trak->time_to_sample_count = _X_BE_32 (&atom[12]);
    debug_atom_load ("    qt stts atom (time-to-sample atom): %d entries\n",
      trak->time_to_sample_count);
    if (trak->time_to_sample_count > (atomsize - 16) / 8)
      trak->time_to_sample_count = (atomsize - 16) / 8;
    trak->time_to_sample_table = atom + 16;
  }

  atom     = atoms[12]; /* CTTS_ATOM */
  atomsize = sizes[12];
  if (atomsize >= 16) {
    /* TJ. this has the same format as stts. If present, duration here
       means (pts - dts), while the corresponding stts defines dts. */
    trak->timeoffs_to_sample_count = _X_BE_32 (&atom[12]);
    debug_atom_load ("    qt ctts atom (timeoffset-to-sample atom): %d entries\n",
      trak->timeoffs_to_sample_count);
    if (trak->timeoffs_to_sample_count > (atomsize - 16) / 8)
      trak->timeoffs_to_sample_count = (atomsize - 16) / 8;
    trak->timeoffs_to_sample_table = atom + 16;
  }

  return QT_OK;

  /* jump here to make sure everything is free'd and avoid leaking memory */
free_trak:
  free(trak->edit_list_table);
  free(trak->sample_to_chunk_table);
  free(trak->decoder_config);
  if (trak->stsd_atoms) {
    for (i = 0; i < trak->stsd_atoms_count; i++)
      free(trak->stsd_atoms[i].video.properties_atom);
    free(trak->stsd_atoms);
  }
  return last_error;
}

/* Traverse through a reference atom and extract the URL and data rate. */
static qt_error parse_reference_atom (qt_info *info,
                                      unsigned char *ref_atom,
                                      char *base_mrl) {

  unsigned int sizes[4];
  reference_t ref;
  unsigned char *atoms[4];

  /* initialize reference atom */
  ref.url = NULL;
  ref.data_rate = 0;
  ref.qtim_version = 0;

  atom_scan (ref_atom, 4, (unsigned int []){
    URL__ATOM, RMDR_ATOM, QTIM_ATOM, 0}, atoms, sizes);

  if (sizes[0] > 12) {
    size_t string_size = _X_BE_32 (&atoms[0][8]);
    size_t url_offset = 0;
    int http = 0;

    if (12 + string_size > sizes[0])
      return QT_NOT_A_VALID_FILE;

    /* if the URL starts with "http://", copy it */
    if (string_size >= 7 &&
        memcmp (&atoms[0][12], "http://", 7) &&
        memcmp (&atoms[0][12], "rtsp://", 7) &&
        base_mrl) {
      /* We need a "qt" prefix hack for Apple trailers */
      http = !strncasecmp (base_mrl, "http://", 7);
      url_offset = strlen(base_mrl) + 2 * http;
    }
    if (url_offset >= 0x80000000)
      return QT_NOT_A_VALID_FILE;

    /* otherwise, append relative URL to base MRL */
    string_size += url_offset;
    ref.url = xine_xmalloc (string_size + 1);
    if (url_offset)
      sprintf (ref.url, "%s%s", http ? "qt" : "", base_mrl);
    memcpy (ref.url + url_offset, &atoms[0][12], _X_BE_32 (&atoms[0][8]));
    ref.url[string_size] = '\0';
    debug_atom_load ("    qt rdrf URL reference:\n      %s\n", ref.url);
  }

  if (sizes[1] >= 16) {
    /* load the data rate */
    ref.data_rate = _X_BE_32 (&atoms[1][12]);
    ref.data_rate *= 10;
    debug_atom_load ("    qt rmdr data rate = %"PRId64"\n", ref.data_rate);
  }

  if (sizes[2] >= 10) {
    ref.qtim_version = _X_BE_16 (&atoms[2][8]);
    debug_atom_load ("      qtim version = %04X\n", ref.qtim_version);
  }

  if (ref.url) {
    info->references = realloc (info->references, (info->reference_count + 1) * sizeof (reference_t));
    if (info->references)
      info->references[info->reference_count++] = ref;
  }

  return QT_OK;
}

/* This is a little support function used to process the edit list when
 * building a frame table. */
#define MAX_DURATION 0x7FFFFFFFFFFFFFFFLL
static void get_next_edit_list_entry(qt_trak *trak,
  unsigned int *edit_list_index,
  unsigned int *edit_list_media_time,
  int64_t *edit_list_duration,
  unsigned int global_timescale) {

  *edit_list_media_time = 0;
  *edit_list_duration = MAX_DURATION;

  /* if there is no edit list, set to max duration and get out */
  if (!trak->edit_list_table) {

    debug_edit_list("  qt: no edit list table, initial = %d, %"PRId64"\n", *edit_list_media_time, *edit_list_duration);
    return;

  } else while (*edit_list_index < trak->edit_list_count) {

    /* otherwise, find an edit list entries whose media time != -1 */
    if (trak->edit_list_table[*edit_list_index].media_time != -1) {

      *edit_list_media_time =
        trak->edit_list_table[*edit_list_index].media_time;
      *edit_list_duration =
        trak->edit_list_table[*edit_list_index].track_duration;

      /* duration is in global timescale units; convert to trak timescale */
      *edit_list_duration *= trak->timescale;
      *edit_list_duration /= global_timescale;

      *edit_list_index = *edit_list_index + 1;
      break;
    }

    *edit_list_index = *edit_list_index + 1;
  }

  /* on the way out, check if this is the last edit list entry; if so,
   * don't let the duration expire (so set it to an absurdly large value)
   */
  if (*edit_list_index == trak->edit_list_count)
    *edit_list_duration = MAX_DURATION;
  debug_edit_list("  qt: edit list table exists, initial = %d, %"PRId64"\n", *edit_list_media_time, *edit_list_duration);
}

static qt_error build_frame_table(qt_trak *trak,
				  unsigned int global_timescale) {

  int i, j, n;
  unsigned char *o, *p, *q, *s;
  unsigned int chunk_start, chunk_end;
  unsigned int samples_per_chunk;
  unsigned int samples_per_frame;
  unsigned int size_left, size_value;
  uint64_t offset_value;
  int64_t pts_value;
  unsigned int duration_left, duration_countdown, duration_value;
  unsigned int ptsoffs_left, ptsoffs_countdown;
  int ptsoffs_value;
  unsigned int audio_frame_counter = 0;
  unsigned int edit_list_media_time;
  int64_t edit_list_duration;
  int64_t frame_duration = 0;
  unsigned int edit_list_index;
  unsigned int edit_list_pts_counter;
  int atom_to_use;
  qt_frame *frame;

  /* maintain counters for each of the subtracks within the trak */
  int *media_id_counts = NULL;

  if ((trak->type != MEDIA_VIDEO) &&
      (trak->type != MEDIA_AUDIO))
    return QT_OK;

  /* AUDIO and OTHER frame types follow the same rules; VIDEO and vbr audio
   * frame types follow a different set */
  if ((trak->type == MEDIA_VIDEO) ||
      (trak->properties->audio.vbr)) {

    /* test for legacy compressed audio */
    if ((trak->type == MEDIA_AUDIO) &&
        (trak->properties->audio.samples_per_frame > 1) &&
        (trak->time_to_sample_count == 1) &&
        (_X_BE_32 (&trak->time_to_sample_table[4]) == 1))
      /* Oh dear. Old style. */
      samples_per_frame = trak->properties->audio.samples_per_frame;
    else
      samples_per_frame = 1;

    /* figure out # of samples */
    trak->frame_count = 0;
    n = trak->chunk_offset_count;
    if (!n)
      return QT_OK;
    for (i = 0; i < trak->sample_to_chunk_count - 1; i++) {
      int s = trak->sample_to_chunk_table[i].samples_per_chunk;
      if ((samples_per_frame != 1) && (s % samples_per_frame))
        return QT_OK; /* unaligned chunk, should not happen */
      j = trak->sample_to_chunk_table[i + 1].first_chunk -
        trak->sample_to_chunk_table[i].first_chunk;
      if (j < 0)
        continue;
      if (j > n)
        j = n;
      trak->frame_count += j * s;
      n -= j;
    }
    trak->frame_count += n * trak->sample_to_chunk_table[i].samples_per_chunk;
    trak->frame_count = (trak->frame_count + samples_per_frame - 1) / samples_per_frame;
    if (!trak->frame_count)
      return QT_OK;

    trak->frames = calloc(trak->frame_count, sizeof(qt_frame));
    if (!trak->frames)
      return QT_NO_MEMORY;
    frame = trak->frames;
    trak->current_frame = 0;

    /* initialize more accounting variables */
    /* file position */
    o = trak->chunk_offset_table32;
    if (!o)
      o = trak->chunk_offset_table64;
    offset_value = 0;
    /* size */
    size_left = trak->sample_size_count;
    s = trak->sample_size_table;
    size_value = trak->sample_size;
    /* sample duration */
    duration_left = trak->time_to_sample_count;
    p = trak->time_to_sample_table;
    duration_countdown = 0;
    duration_value = 1;
    pts_value = 0;
    /* used by reordered video */
    ptsoffs_left = trak->timeoffs_to_sample_count;
    q = trak->timeoffs_to_sample_table;
    ptsoffs_countdown = ptsoffs_value = 0;

    if (samples_per_frame != 1) {
      /* Old style demuxing. Tweak our frame builder.
         Treating whole chunks as frames would be faster, but unfortunately
         some ffmpeg decoders dont like multiple frames in one go. */
      size_left = 0;
      size_value = trak->properties->audio.bytes_per_frame;
      duration_left = 0;
      duration_value = samples_per_frame;
      ptsoffs_left = 0;
      trak->samples = _X_BE_32 (trak->time_to_sample_table) / samples_per_frame;
    }

    media_id_counts = xine_xcalloc(trak->stsd_atoms_count, sizeof(int));
    if (!media_id_counts)
      return QT_NO_MEMORY;

    /* iterate through each start chunk in the stsc table */
    for (i = 0; i < trak->sample_to_chunk_count; i++) {
      /* iterate from the first chunk of the current table entry to
       * the first chunk of the next table entry */
      chunk_start = trak->sample_to_chunk_table[i].first_chunk;
      if (i < trak->sample_to_chunk_count - 1)
        chunk_end =
          trak->sample_to_chunk_table[i + 1].first_chunk;
      else
        /* if the first chunk is in the last table entry, iterate to the
           final chunk number (the number of offsets in stco table) */
        chunk_end = trak->chunk_offset_count + 1;

      /* iterate through each sample in a chunk */
      for (j = chunk_start - 1; j < chunk_end - 1; j++) {

        samples_per_chunk =
          trak->sample_to_chunk_table[i].samples_per_chunk;

        if (trak->chunk_offset_table32)
          offset_value = _X_BE_32 (o), o += 4;
        else
          offset_value = _X_BE_64 (o), o += 8;

        while (samples_per_chunk > 0) {

          /* media id accounting */
          if (trak->sample_to_chunk_table[i].media_id > trak->stsd_atoms_count) {
            printf ("QT: help! media ID out of range! (%d > %d)\n",
              trak->sample_to_chunk_table[i].media_id,
              trak->stsd_atoms_count);
            frame->media_id = 0;
          } else {
            frame->media_id = trak->sample_to_chunk_table[i].media_id;
            media_id_counts[trak->sample_to_chunk_table[i].media_id - 1]++;
          }

          /* figure out the offset and size */
          if (size_left) {
            if (trak->sample_size_bits == 32)
              size_value = _X_BE_32 (s), s += 4;
            else if (trak->sample_size_bits == 16)
              size_value = _X_BE_16 (s), s += 2;
            else
              size_value = *s++;
            size_left--;
          }
          frame->offset = offset_value;
          frame->size = size_value;
          offset_value += size_value;

          /* if there is no stss (sample sync) table, make all of the frames
           * keyframes; otherwise, clear the keyframe bits for now */
          if (trak->sync_sample_table)
            frame->keyframe = 0;
          else
            frame->keyframe = 1;

          /* figure out the pts situation */
          if (duration_left && !duration_countdown) {
            duration_countdown = _X_BE_32 (p); p += 4;
            duration_value     = _X_BE_32 (p); p += 4;
            duration_left--;
          }
          frame->pts = pts_value;
          pts_value += duration_value;
          duration_countdown--;

          /* offset pts for reordered video */
          if (ptsoffs_left && !ptsoffs_countdown) {
            unsigned int v;
            ptsoffs_countdown = _X_BE_32 (q); q += 4;
            v                 = _X_BE_32 (q); q += 4;
            /* TJ. this is 32 bit signed. All casts necessary for my gcc 4.5.0 */
            ptsoffs_value = v;
            if ((sizeof (int) > 4) && (ptsoffs_value & 0x80000000))
              ptsoffs_value |= ~0xffffffffL;
            ptsoffs_value *= (int)90000;
            ptsoffs_value /= (int)trak->timescale;
            ptsoffs_left--;
          }
          frame->ptsoffs = ptsoffs_value;
          ptsoffs_countdown--;

          samples_per_chunk -= samples_per_frame;
          frame++;
        }
      }
    }

    /* was the last chunk incomplete? */
    if (trak->samples && (trak->samples < trak->frame_count))
      trak->frame_count = trak->samples;

    /* fill in the keyframe information */
    p = trak->sync_sample_table;
    for (i = 0; i < trak->sync_sample_count; i++) {
      unsigned int fr = _X_BE_32 (p) - 1; p += 4;
      if (fr < trak->frame_count)
        trak->frames[fr].keyframe = 1;
    }

    /* initialize edit list considerations */
    edit_list_index = 0;
    get_next_edit_list_entry(trak, &edit_list_index,
      &edit_list_media_time, &edit_list_duration, global_timescale);

    /* fix up pts information w.r.t. the edit list table */
    edit_list_pts_counter = 0;
    for (i = 0; i < trak->frame_count; i++) {

      debug_edit_list("    %d: (before) pts = %"PRId64"...", i, trak->frames[i].pts);

      if (trak->frames[i].pts < edit_list_media_time)
        trak->frames[i].pts = edit_list_pts_counter;
      else {
        if (i < trak->frame_count - 1)
          frame_duration =
            (trak->frames[i + 1].pts - trak->frames[i].pts);

        debug_edit_list("duration = %"PRId64"...", frame_duration);
        trak->frames[i].pts = edit_list_pts_counter;
        edit_list_pts_counter += frame_duration;
        edit_list_duration -= frame_duration;
      }

      debug_edit_list("(fixup) pts = %"PRId64"...", trak->frames[i].pts);

      /* reload media time and duration */
      if (edit_list_duration <= 0) {
        get_next_edit_list_entry(trak, &edit_list_index,
          &edit_list_media_time, &edit_list_duration, global_timescale);
      }

      debug_edit_list("(after) pts = %"PRId64"...\n", trak->frames[i].pts);
    }

    /* compute final pts values */
    for (i = 0; i < trak->frame_count; i++) {
      trak->frames[i].pts *= 90000;
      trak->frames[i].pts /= trak->timescale;
      debug_edit_list("  final pts for sample %d = %"PRId64"\n", i, trak->frames[i].pts);
    }

    /* decide which video properties atom to use */
    atom_to_use = 0;
    for (i = 1; i < trak->stsd_atoms_count; i++)
      if (media_id_counts[i] > media_id_counts[i - 1])
        atom_to_use = i;
    trak->properties = &trak->stsd_atoms[atom_to_use];

    free(media_id_counts);

  } else {

    /* in this case, the total number of frames is equal to the number of
     * chunks */
    trak->frame_count = trak->chunk_offset_count;
    trak->frames = calloc(trak->frame_count, sizeof(qt_frame));
    if (!trak->frames)
      return QT_NO_MEMORY;

    if (trak->type == MEDIA_AUDIO) {
      /* iterate through each start chunk in the stsc table */
      for (i = 0; i < trak->sample_to_chunk_count; i++) {
        /* iterate from the first chunk of the current table entry to
         * the first chunk of the next table entry */
        chunk_start = trak->sample_to_chunk_table[i].first_chunk;
        if (i < trak->sample_to_chunk_count - 1)
          chunk_end =
            trak->sample_to_chunk_table[i + 1].first_chunk;
        else
          /* if the first chunk is in the last table entry, iterate to the
             final chunk number (the number of offsets in stco table) */
          chunk_end = trak->chunk_offset_count + 1;

        /* iterate through each sample in a chunk and fill in size and
         * pts information */
        for (j = chunk_start - 1; j < chunk_end - 1; j++) {

          /* figure out the pts for this chunk */
          trak->frames[j].pts = audio_frame_counter;
          trak->frames[j].pts *= 90000;
          trak->frames[j].pts /= trak->timescale;
          trak->frames[j].ptsoffs = 0;

          /* fetch the alleged chunk size according to the QT header */
          trak->frames[j].size =
            trak->sample_to_chunk_table[i].samples_per_chunk;

          /* media id accounting */
          if (trak->sample_to_chunk_table[i].media_id > trak->stsd_atoms_count) {
            printf ("QT: help! media ID out of range! (%d > %d)\n",
              trak->sample_to_chunk_table[i].media_id,
              trak->stsd_atoms_count);
            trak->frames[j].media_id = 0;
          } else {
            trak->frames[j].media_id =
              trak->sample_to_chunk_table[i].media_id;
          }

          /* the chunk size is actually the audio frame count */
          audio_frame_counter += trak->frames[j].size;

	  /* compute the actual chunk size */
          trak->frames[j].size =
            (trak->frames[j].size *
             trak->properties->audio.channels) /
             trak->properties->audio.samples_per_frame *
             trak->properties->audio.bytes_per_frame;
        }
      }
    }

    /* fill in the rest of the information for the audio samples */
    n = trak->frame_count < trak->chunk_offset_count ?
      trak->frame_count : trak->chunk_offset_count;
    if (trak->chunk_offset_table32) {
      p = trak->chunk_offset_table32;
      for (i = 0; i < n; i++)
        trak->frames[i].offset = _X_BE_32 (p), p += 4;
    } else if (trak->chunk_offset_table64) {
      p = trak->chunk_offset_table64;
      for (i = 0; i < n; i++)
        trak->frames[i].offset = _X_BE_64 (p), p += 8;
    }
    if (trak->type != MEDIA_AUDIO) {
      for (i = 0; i < trak->frame_count; i++) {
        trak->frames[i].pts      = 0;
        trak->frames[i].ptsoffs  = 0;
        trak->frames[i].keyframe = 0;
      }
    } else {
      for (i = 0; i < trak->frame_count; i++)
        trak->frames[i].keyframe = 0;
    }
  }

  return QT_OK;
}

/************************************************************************
* Fragment stuff                                                        *
************************************************************************/

static qt_trak *find_trak_by_id (qt_info *info, int id) {
  int i;

  for (i = 0; i < info->trak_count; i++) {
    if (info->traks[i].id == id)
      return &(info->traks[i]);
  }
  return NULL;
}

static int parse_mvex_atom (qt_info *info, unsigned char *mvex_atom, int bufsize) {
  int i, j, mvex_size;
  uint32_t traknum = 0, subtype, subsize = 0;
  qt_trak *trak;

  /* limit to atom size */
  if (bufsize < 8)
    return 0;
  mvex_size = _X_BE_32 (mvex_atom);
  if (bufsize < mvex_size)
    mvex_size = bufsize;
  /* scan subatoms */
  for (i = 8; i + 8 <= mvex_size; i += subsize) {
    subsize = _X_BE_32 (&mvex_atom[i]);
    subtype = _X_BE_32 (&mvex_atom[i + 4]);
    if (subsize == 0)
      subsize = mvex_size - i;
    if ((subsize < 8) || (i + subsize > mvex_size))
      break;
    switch (subtype) {
      case MEHD_ATOM:
        break;
      case TREX_ATOM:
        if (subsize < 8 + 24)
          break;
        traknum = _X_BE_32 (&mvex_atom[i + 8 + 4]);
        trak = find_trak_by_id (info, traknum);
        if (!trak)
          break;
        trak->default_sample_description_index = _X_BE_32 (&mvex_atom[i + 8 + 8]);
        trak->default_sample_duration          = _X_BE_32 (&mvex_atom[i + 8 + 12]);
        trak->default_sample_size              = _X_BE_32 (&mvex_atom[i + 8 + 16]);
        trak->default_sample_flags             = _X_BE_32 (&mvex_atom[i + 8 + 20]);
        j = trak->frame_count;
        trak->fragment_dts = (j >= 2) ? 2 * trak->frames[j - 1].pts - trak->frames[j - 2].pts : 0;
        trak->fragment_dts *= trak->timescale;
        trak->fragment_dts /= 90000;
        trak->fragment_frames = trak->frame_count;
        info->fragment_count = -1;
        break;
      default: ;
    }
  }

  return 1;
}

static int parse_traf_atom (qt_info *info, unsigned char *traf_atom, int trafsize, off_t moofpos) {
  int i, n, done = 0, samples;
  uint32_t subtype, subsize = 0, tfhd_flags, trun_flags;
  uint32_t sample_description_index = 0;
  uint32_t default_sample_duration = 0, sample_duration;
  uint32_t default_sample_size = 0, sample_size;
  uint32_t default_sample_flags = 0, first_sample_flags, sample_flags;
  off_t base_data_offset = 0, data_pos = 0;
  int64_t sample_dts;
  unsigned char *p;
  qt_trak *trak = NULL;
  qt_frame *frame;

  for (i = 8; i + 8 <= trafsize; i += subsize) {
    subsize = _X_BE_32 (&traf_atom[i]);
    subtype = _X_BE_32 (&traf_atom[i + 4]);
    if (subsize == 0)
      subsize = trafsize - i;
    if ((subsize < 8) || (i + subsize > trafsize))
      break;
    switch (subtype) {

      case TFHD_ATOM:
        if (subsize < 8 + 8)
          break;
        p = traf_atom + i + 8;
        tfhd_flags = _X_BE_32 (p); p += 4;
        trak = find_trak_by_id (info, _X_BE_32 (p)); p += 4;
        n = 8 + 8;
        if (tfhd_flags & 1) n += 8;
        if (tfhd_flags & 2) n += 4;
        if (tfhd_flags & 8) n += 4;
        if (tfhd_flags & 0x10) n += 4;
        if (tfhd_flags & 0x20) n += 4;
        if (subsize < n)
          trak = NULL;
        if (!trak)
          break;
        if (tfhd_flags & 1)
          base_data_offset = _X_BE_64 (p), p += 8;
        else
          base_data_offset = moofpos;
        data_pos = base_data_offset;
        if (tfhd_flags & 2)
          sample_description_index = _X_BE_32 (p), p += 4;
        else
          sample_description_index = trak->default_sample_description_index;
        if (tfhd_flags & 8)
          default_sample_duration = _X_BE_32 (p), p += 4;
        else
          default_sample_duration = trak->default_sample_duration;
        if (tfhd_flags & 0x10)
          default_sample_size = _X_BE_32 (p), p += 4;
        else
          default_sample_size = trak->default_sample_size;
        if (tfhd_flags & 0x20)
          default_sample_flags = _X_BE_32 (p), p += 4;
        else
          default_sample_flags = trak->default_sample_flags;
        break;

      case TRUN_ATOM:
        /* get head */
        if (!trak)
          break;
        if (subsize < 8 + 8)
          break;
        p = traf_atom + i + 8;
        trun_flags = _X_BE_32 (p); p += 4;
        samples    = _X_BE_32 (p); p += 4;
        n = 8 + 8;
        if (trun_flags & 1) n += 4;
        if (trun_flags & 4) n += 4;
        if (subsize < n)
          break;
        if (trun_flags & 1) {
          uint32_t o = _X_BE_32 (p);
          p += 4;
          data_pos = base_data_offset + (off_t)((int32_t)o);
        }
        if (trun_flags & 4)
          first_sample_flags = _X_BE_32 (p), p += 4;
        else
          first_sample_flags = default_sample_flags;
        /* truncation paranoia */
        n = 0;
        if (trun_flags & 0x100) n += 4;
        if (trun_flags & 0x200) n += 4;
        if (trun_flags & 0x400) n += 4;
        if (trun_flags & 0x800) n += 4;
        if (n) {
          n = (i + subsize - (p - traf_atom)) / n;
          if (samples > n) samples = n;
        }
        if (!samples)
          break;
        /* enlarge frame table in steps of 64k frames, to avoid a flood of reallocations */
        frame = trak->frames;
        n = trak->frame_count + samples;
        if (n > trak->fragment_frames) {
          n = (n + 0xffff) & ~0xffff;
          frame = realloc (trak->frames, n * sizeof (*frame));
          if (!frame)
            break;
          trak->fragment_frames = n;
          trak->frames = frame;
        }
        /* get defaults */
        frame += trak->frame_count;
        sample_dts      = trak->fragment_dts;
        sample_duration = default_sample_duration;
        sample_size     = default_sample_size;
        sample_flags    = first_sample_flags;
        /* add frames */
        while (samples--) {
          frame->media_id = sample_description_index;
          frame->pts = sample_dts * 90000 / trak->timescale;
          if (trun_flags & 0x100)
            sample_duration = _X_BE_32 (p), p += 4;
          sample_dts += sample_duration;
          frame->offset = data_pos;
          if (trun_flags & 0x200)
            sample_size = _X_BE_32 (p), p += 4;
          frame->size = sample_size;
          data_pos += sample_size;
          if (trun_flags & 0x400)
            sample_flags = _X_BE_32 (p), p += 4;
          frame->keyframe = !(sample_flags & 0x10000);
          sample_flags = default_sample_flags;
          if (trun_flags & 0x800) {
            uint32_t o = _X_BE_32 (p);
            p += 4;
            frame->ptsoffs = (int32_t)90000 * (int32_t)o / (int32_t)trak->timescale;
          } else
            frame->ptsoffs = 0;
          frame++;
          (trak->frame_count)++;
        }
        trak->fragment_dts = sample_dts;
        done++;
        break;

      default: ;
    }
  }
  return done;
}

static int parse_moof_atom (qt_info *info, unsigned char *moof_atom, int moofsize, off_t moofpos) {
  int i, subtype, subsize = 0, done = 0;

  for (i = 8; i + 8 <= moofsize; i += subsize) {
    subsize = _X_BE_32 (&moof_atom[i]);
    subtype = _X_BE_32 (&moof_atom[i + 4]);
    if (subsize == 0)
      subsize = moofsize - i;
    if ((subsize < 8) || (i + subsize > moofsize))
      break;
    switch (subtype) {
      case MFHD_ATOM:
        /* TODO: check sequence # here */
        break;
      case TRAF_ATOM:
        if (parse_traf_atom (info, &moof_atom[i], subsize, moofpos))
          done++;
        break;
      default: ;
    }
  }
  return done;
}

static int fragment_scan (qt_info *info, input_plugin_t *input) {
  unsigned char *buf;
  off_t pos, fsize, atomsize = 0, bufsize = 16;
  int frags = 0, atomtype;

  /* prerequisites */
  if (info->fragment_count != -1)
    return 0;
  if (!INPUT_IS_SEEKABLE (input))
    return 0;
  fsize = input->get_length (input);
  if (fsize <= 0)
    return 0;
  buf = malloc (bufsize);
  if (!buf)
    return 0;

  for (pos = 0; pos < fsize; pos += atomsize) {
    input->seek (input, pos, SEEK_SET);
    if (input->read (input, buf, 16) != 16)
      break;
    atomsize = _X_BE_32 (buf);
    atomtype = _X_BE_32 (&buf[4]);
    if (atomsize == 0)
      atomsize = fsize - pos;
    else if (atomsize == 1) {
      atomsize = _X_BE_64 (&buf[8]);
      if (atomsize < 16)
        break;
    } else if (atomsize < 8)
      break;
    if (atomtype == MOOF_ATOM) {
      if (atomsize > (80 << 20))
        break;
      if (atomsize > bufsize) {
        unsigned char *b2;
        bufsize = atomsize + (atomsize >> 1);
        b2 = realloc (buf, bufsize);
        if (b2)
          buf = b2;
        else
          break;
      }
      if (atomsize > 16) {
        if (input->read (input, buf + 16, atomsize - 16) != atomsize - 16)
          break;
      }
      if (parse_moof_atom (info, buf, atomsize, pos))
        frags++;
    }
  }

  info->fragment_count = frags;
  free (buf);
  return frags;
}

/************************************************************************
* /Fragment stuff                                                       *
************************************************************************/

static void info_string_from_atom (unsigned char *atom, char **target) {
  uint32_t size, string_size, i;

  if (!atom)
    return;
  size = _X_BE_32 (atom);
  if ((size >= 24) && (_X_BE_32 (&atom[12]) == DATA_ATOM)) {
    if (_X_BE_32 (&atom[16]) != 1) /* # of portions */
      return;
    i = 24;
    string_size = _X_BE_32 (&atom[20]);
    if (string_size == 0)
      string_size = size - i;
  } else if (size >= 12) {
    i = 12;
    string_size = _X_BE_16 (&atom[8]);
  } else
    return;
  if (i + string_size > size)
    return;
  *target = realloc (*target, string_size + 1);
  if (*target == NULL)
    return;
  memcpy (*target, &atom[i], string_size);
  (*target)[string_size] = 0;
}

/*
 * This function takes a pointer to a qt_info structure and a pointer to
 * a buffer containing an uncompressed moov atom. When the function
 * finishes successfully, qt_info will have a list of qt_frame objects,
 * ordered by offset.
 */
static void parse_moov_atom(qt_info *info, unsigned char *moov_atom,
                            int64_t bandwidth, input_plugin_t *input) {
  int i, j;
  int error;
  unsigned int types[20], sizes[20];
  unsigned char *atoms[20];
  unsigned int max_video_frames = 0;
  unsigned int max_audio_frames = 0;
  unsigned char *mvex_atom;
  int mvex_size;

  /* make sure this is actually a moov atom (will also accept 'free' as
   * a special case) */
  if ((_X_BE_32(&moov_atom[4]) != MOOV_ATOM) &&
      (_X_BE_32(&moov_atom[4]) != FREE_ATOM)) {
    info->last_error = QT_NO_MOOV_ATOM;
    return;
  }

  /* prowl through the moov atom looking for very specific targets */
  types[0] = MVHD_ATOM;
  types[1] = MVEX_ATOM;
  for (i = 2; i < 19; i++) types[i] = TRAK_ATOM;
  types[i] = 0;
  atom_scan (moov_atom, 1, types, atoms, sizes);

  if (atoms[0]) {
    parse_mvhd_atom(info, atoms[0]);
    if (info->last_error != QT_OK)
      return;
  }

  mvex_atom = atoms[1];
  mvex_size = sizes[1];

  for (i = 2; i < 19 && atoms[i]; i++) {
    /* create a new trak structure */
    info->trak_count++;
    info->traks = realloc (info->traks, info->trak_count * sizeof (qt_trak));
    info->last_error = parse_trak_atom (&info->traks[info->trak_count - 1], atoms[i]);
    if (info->last_error != QT_OK) {
      info->trak_count--;
      return;
    }
  }

  atom_scan (moov_atom, 4, (unsigned int []){
    NAM_ATOM, CPY_ATOM, DES_ATOM, CMT_ATOM,
    ART_ATOM, ALB_ATOM, GEN_ATOM, WRT_ATOM,
    DAY_ATOM, 0}, atoms, sizes);

  info_string_from_atom (atoms[0], &info->name);
  info_string_from_atom (atoms[1], &info->copyright);
  info_string_from_atom (atoms[2], &info->description);
  info_string_from_atom (atoms[3], &info->comment);
  info_string_from_atom (atoms[4], &info->artist);
  info_string_from_atom (atoms[5], &info->album);
  info_string_from_atom (atoms[6], &info->genre);
  info_string_from_atom (atoms[7], &info->composer);
  info_string_from_atom (atoms[8], &info->year);

  for (i = 0; i < 8; i++) types[i] = RMDA_ATOM;
  types[i] = 0;
  atom_scan (moov_atom, 2, types, atoms, sizes);

  for (i = 0; i < 8 && atoms[i]; i++)
    parse_reference_atom (info, atoms[i], info->base_mrl);

  debug_atom_load("  qt: finished parsing moov atom\n");

  /* build frame tables corresponding to each trak */
  debug_frame_table("  qt: preparing to build %d frame tables\n",
    info->trak_count);
  for (i = 0; i < info->trak_count; i++) {

    debug_frame_table("    qt: building frame table #%d (%s)\n", i,
      (info->traks[i].type == MEDIA_VIDEO) ? "video" : "audio");
    error = build_frame_table(&info->traks[i], info->timescale);
    if (error != QT_OK) {
      info->last_error = error;
      return;
    }
  }

  /* must parse mvex _after_ building traks */
  if (mvex_atom) {
    parse_mvex_atom (info, mvex_atom, mvex_size);
    /* reassemble fragments, if any */
    fragment_scan (info, input);
  }

  for (i = 0; i < info->trak_count; i++) {
    /* dump the frame table in debug mode */
    for (j = 0; j < info->traks[i].frame_count; j++)
      debug_frame_table("      %d: %8X bytes @ %"PRIX64", %"PRId64" pts, media id %d%s\n",
        j,
        info->traks[i].frames[j].size,
        info->traks[i].frames[j].offset,
        info->traks[i].frames[j].pts,
        info->traks[i].frames[j].media_id,
        (info->traks[i].frames[j].keyframe) ? " (keyframe)" : "");

    /* decide which audio trak and which video trak has the most frames */
    if ((info->traks[i].type == MEDIA_VIDEO) &&
        (info->traks[i].frame_count > max_video_frames)) {

      info->video_trak = i;
      max_video_frames = info->traks[i].frame_count;

    } else if ((info->traks[i].type == MEDIA_AUDIO) &&
               (info->traks[i].frame_count > max_audio_frames)) {

      info->audio_trak = i;
      max_audio_frames = info->traks[i].frame_count;
    }
  }

  /* check for references */
  if (info->reference_count > 0) {

    /* init chosen reference to the first entry */
    info->chosen_reference = 0;

    /* iterate through 1..n-1 reference entries and decide on the right one */
    for (i = 1; i < info->reference_count; i++) {

      if (info->references[i].qtim_version >
          info->references[info->chosen_reference].qtim_version)
        info->chosen_reference = i;
      else if ((info->references[i].data_rate <= bandwidth) &&
               (info->references[i].data_rate >
                info->references[info->chosen_reference].data_rate))
        info->chosen_reference = i;
    }

    debug_atom_load("  qt: chosen reference is ref #%d, qtim version %04X, %"PRId64" bps\n      URL: %s\n",
      info->chosen_reference,
      info->references[info->chosen_reference].qtim_version,
      info->references[info->chosen_reference].data_rate,
      info->references[info->chosen_reference].url);
  }
}

static qt_error open_qt_file(qt_info *info, input_plugin_t *input,
                             int64_t bandwidth) {

  unsigned char *moov_atom = NULL;
  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;

  /* zlib stuff */
  z_stream z_state;
  int z_ret_code;
  unsigned char *unzip_buffer;

  /* extract the base MRL if this is a http MRL */
  if (strncmp(input->get_mrl(input), "http://", 7) == 0) {

    char *slash;

    /* this will copy a few bytes too many, but no big deal */
    info->base_mrl = strdup(input->get_mrl(input));
    /* terminate the string after the last slash character */
    slash = strrchr(info->base_mrl, '/');
    if (slash)
      *(slash + 1) = '\0';
  }

  if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE))
    find_moov_atom(input, &moov_atom_offset, &moov_atom_size);
  else {
    unsigned char preview[MAX_PREVIEW_SIZE] = { 0, };
    input->get_optional_data(input, preview, INPUT_OPTIONAL_DATA_PREVIEW);
    if (_X_BE_32(&preview[4]) != MOOV_ATOM) {
      /* special case if there is an ftyp atom first */
      if (_X_BE_32(&preview[4]) == FTYP_ATOM) {
        moov_atom_size = _X_BE_32(&preview[0]);
        if ((moov_atom_size + ATOM_PREAMBLE_SIZE >= MAX_PREVIEW_SIZE) ||
            (_X_BE_32(&preview[moov_atom_size + 4]) != MOOV_ATOM)) {
          info->last_error = QT_NO_MOOV_ATOM;
          return info->last_error;
        }
        moov_atom_offset = moov_atom_size;
        moov_atom_size = _X_BE_32(&preview[moov_atom_offset]);
      } else {
        info->last_error = QT_NO_MOOV_ATOM;
        return info->last_error;
      }
    } else {
      moov_atom_offset = 0;
      moov_atom_size = _X_BE_32(&preview[0]);
    }
  }

  if (moov_atom_offset == -1) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }
  info->moov_first_offset = moov_atom_offset;

  moov_atom = (unsigned char *)malloc(moov_atom_size);
  if (moov_atom == NULL) {
    info->last_error = QT_NO_MEMORY;
    return info->last_error;
  }

  /* seek to the start of moov atom */
  if (input->seek(input, info->moov_first_offset, SEEK_SET) !=
    info->moov_first_offset) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }
  if (input->read(input, moov_atom, moov_atom_size) !=
    moov_atom_size) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }

  /* check if moov is compressed */
  if (_X_BE_32(&moov_atom[12]) == CMOV_ATOM && moov_atom_size >= 0x28) {

    info->compressed_header = 1;

    z_state.next_in = &moov_atom[0x28];
    z_state.avail_in = moov_atom_size - 0x28;
    z_state.avail_out = _X_BE_32(&moov_atom[0x24]);
    unzip_buffer = (unsigned char *)malloc(_X_BE_32(&moov_atom[0x24]));
    if (!unzip_buffer) {
      free(moov_atom);
      info->last_error = QT_NO_MEMORY;
      return info->last_error;
    }

    z_state.next_out = unzip_buffer;
    z_state.zalloc = (alloc_func)0;
    z_state.zfree = (free_func)0;
    z_state.opaque = (voidpf)0;

    z_ret_code = inflateInit (&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflate(&z_state, Z_NO_FLUSH);
    if ((z_ret_code != Z_OK) && (z_ret_code != Z_STREAM_END)) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflateEnd(&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    /* replace the compressed moov atom with the decompressed atom */
    free (moov_atom);
    moov_atom = unzip_buffer;
    moov_atom_size = _X_BE_32(&moov_atom[0]);
  }

  if (!moov_atom) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }

  /* write moov atom to disk if debugging option is turned on */
  dump_moov_atom(moov_atom, moov_atom_size);

  /* take apart the moov atom */
  parse_moov_atom(info, moov_atom, bandwidth, input);
  if (info->last_error != QT_OK) {
    free(moov_atom);
    return info->last_error;
  }

  free(moov_atom);

  return QT_OK;
}

/**********************************************************************
 * xine demuxer functions
 **********************************************************************/

static int demux_qt_send_chunk(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int i, j;
  unsigned int remaining_sample_bytes;
  unsigned int frame_aligned_buf_size;
  int frame_duration;
  int first_buf;
  qt_trak *trak = NULL;
  off_t current_pos = this->input->get_current_pos (this->input);

  /* if this is DRM-protected content, finish playback before it even
   * tries to start */
  if (this->qt->last_error == QT_DRM_NOT_SUPPORTED) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if it's time to send a reference up to the UI */
  if (this->qt->chosen_reference != -1) {

    _x_demux_send_mrl_reference (this->stream, 0,
                                 this->qt->references[this->qt->chosen_reference].url,
                                 NULL, 0, 0);
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* Decide the trak from which to dispatch a frame. Policy: Dispatch
   * the frames in offset order as much as possible. If the pts difference
   * between the current frames from the audio and video traks is too
   * wide, make an exception. This exception deals with non-interleaved
   * Quicktime files. */
  do {
    int traks[MAX_AUDIO_TRAKS + 1];
    int trak_count = 0;
    int min_trak = -1, next_trak = -1;
    int64_t min_pts = 0, max_pts = 0; /* avoid warning */
    off_t next_pos = 0x7fffffffffffffffLL;
    int i;

    /* Step 1: list yet unfinished traks. */
    if (this->qt->video_trak >= 0) {
      trak = &this->qt->traks[this->qt->video_trak];
      if (trak->current_frame < trak->frame_count)
        traks[trak_count++] = this->qt->video_trak;
    }
    for (i = 0; i < this->qt->audio_trak_count; i++) {
      trak = &this->qt->traks[this->qt->audio_traks[i]];
      if (trak->current_frame < trak->frame_count)
        traks[trak_count++] = this->qt->audio_traks[i];
    }

    /* Step 2: handle trivial cases. */
    if (trak_count == 0) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    if (trak_count == 1) {
      trak = &this->qt->traks[traks[0]];
      break;
    }

    /* Step 3: find
       * The minimum pts and the trak who has it.
       * The maximum pts.
       * The forward nearest to current position and the trak thereof. */
    for (i = 0; i < trak_count; i++) {
      int64_t pts;
      off_t pos;
      trak = &this->qt->traks[traks[i]];
      pts  = trak->frames[trak->current_frame].pts;
      if (i == 0) {
        min_pts  = max_pts = pts;
        min_trak = traks[i];
      } else if (pts < min_pts) {
        min_pts  = pts;
        min_trak = traks[i];
      } else if (pts > max_pts)
        max_pts  = pts;
      pos = trak->frames[trak->current_frame].offset;
      if ((pos >= current_pos) && (pos < next_pos)) {
        next_pos = pos;
        next_trak = traks[i];
      }
    }

    /* Step 4: after seek, or if the pts scissors opened too much, send minimum pts trak next.
       Otherwise, take next one by offset. */
    i = this->qt->seek_flag || (next_trak < 0) || (max_pts - min_pts > MAX_PTS_DIFF) ?
      min_trak : next_trak;
    trak = &this->qt->traks[i];
  } while (0);

  if (this->stream->xine->verbosity == XINE_VERBOSITY_DEBUG + 1) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG + 1,
      "demux_qt: sending trak %d dts %"PRId64" pos %"PRId64"\n",
      (int)(trak - this->qt->traks),
      trak->frames[trak->current_frame].pts,
      trak->frames[trak->current_frame].offset);
  }

  /* check if it is time to seek */
  if (this->qt->seek_flag) {
    this->qt->seek_flag = 0;

    /* send min pts of all used traks, usually audio (see demux_qt_seek ()). */
    _x_demux_control_newpts (this->stream, trak->frames[trak->current_frame].pts, BUF_FLAG_SEEK);
  }

  if (trak->type == MEDIA_VIDEO) {
    i = trak->current_frame++;

    if (trak->frames[i].media_id != trak->properties->video.media_id) {
      this->status = DEMUX_OK;
      return this->status;
    }

    remaining_sample_bytes = trak->frames[i].size;
    if (trak->frames[i].offset != current_pos)
      this->input->seek (this->input, trak->frames[i].offset, SEEK_SET);

    if (i + 1 < trak->frame_count) {
      /* frame duration is the pts diff between this video frame and
       * the next video frame */
      frame_duration  = trak->frames[i + 1].pts;
      frame_duration -= trak->frames[i].pts;
    } else {
      /* give the last frame some fixed duration */
      frame_duration = 12000;
    }

    /* Due to the edit lists, some successive frames have the same pts
     * which would ordinarily cause frame_duration to be 0 which can
     * cause DIV-by-0 errors in the engine. Perform this little trick
     * to compensate. */
    if (!frame_duration) {
      frame_duration = 1;
      trak->properties->video.edit_list_compensation++;
    } else {
      frame_duration -= trak->properties->video.edit_list_compensation;
      trak->properties->video.edit_list_compensation = 0;
    }

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
                         frame_duration);

    debug_video_demux("  qt: sending off video frame %d from offset 0x%"PRIX64", %d bytes, media id %d, %"PRId64" pts\n",
      i,
      trak->frames[i].offset,
      trak->frames[i].size,
      trak->frames[i].media_id,
      trak->frames[i].pts);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = trak->properties->video.codec_buftype;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double) (trak->frames[i].offset - this->data_start)
                                                * 65535 / this->data_size);
      buf->extra_info->input_time = trak->frames[i].pts / 90;
      buf->pts = trak->frames[i].pts + (int64_t)trak->frames[i].ptsoffs;

      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = frame_duration;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        trak->current_frame = trak->frame_count;
        break;
      }

      if (trak->frames[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->video_fifo->put(this->video_fifo, buf);
    }

  } else { /* trak->type == MEDIA_AUDIO */
    /* load an audio sample and packetize it */
    i = trak->current_frame++;

    if (trak->frames[i].media_id != trak->properties->audio.media_id) {
      this->status = DEMUX_OK;
      return this->status;
    }

    /* only go through with this procedure if audio_fifo exists */
    if (!this->audio_fifo)
      return this->status;

    remaining_sample_bytes = trak->frames[i].size;

    if (trak->frames[i].offset != current_pos)
      this->input->seek (this->input, trak->frames[i].offset, SEEK_SET);

    debug_audio_demux("  qt: sending off audio frame %d from offset 0x%"PRIX64", %d bytes, media id %d, %"PRId64" pts\n",
      i,
      trak->frames[i].offset,
      trak->frames[i].size,
      trak->frames[i].media_id,
      trak->frames[i].pts);

    first_buf = 1;
    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = trak->properties->audio.codec_buftype;
      if( this->data_size )
        buf->extra_info->input_normpos = (int)( (double) (trak->frames[i].offset - this->data_start)
                                                * 65535 / this->data_size);
      /* The audio chunk is often broken up into multiple 8K buffers when
       * it is sent to the audio decoder. Only attach the proper timestamp
       * to the first buffer. This is for the linear PCM decoder which
       * turns around and sends out audio buffers as soon as they are
       * received. If 2 or more consecutive audio buffers are dispatched to
       * the audio out unit, the engine will compensate with pops. */
      if ((buf->type == BUF_AUDIO_LPCM_BE) ||
          (buf->type == BUF_AUDIO_LPCM_LE)) {
        if (first_buf) {
          buf->extra_info->input_time = trak->frames[i].pts / 90;
          buf->pts = trak->frames[i].pts;
          first_buf = 0;
        } else {
          buf->extra_info->input_time = 0;
          buf->pts = 0;
        }
      } else {
        buf->extra_info->input_time = trak->frames[i].pts / 90;
        buf->pts = trak->frames[i].pts;
      }

      /* 24-bit audio doesn't fit evenly into the default 8192-byte buffers */
      if (trak->properties->audio.bits == 24)
        frame_aligned_buf_size = 8184;
      else
        frame_aligned_buf_size = buf->max_size;

      if (remaining_sample_bytes > frame_aligned_buf_size)
        buf->size = frame_aligned_buf_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        trak->current_frame = trak->frame_count;
        break;
      }

      /* Special case alert: If this is signed, 8-bit data, transform
       * the data to unsigned. */
      if ((trak->properties->audio.bits == 8) &&
          ((trak->properties->audio.codec_fourcc == TWOS_FOURCC) ||
           (trak->properties->audio.codec_fourcc == SOWT_FOURCC)))
        for (j = 0; j < buf->size; j++)
          buf->content[j] += 0x80;

      if (!remaining_sample_bytes) {
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      }

      buf->type |= trak->audio_index;
      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }

  return this->status;
}

static void demux_qt_send_headers(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  qt_trak *video_trak = NULL;
  qt_trak *audio_trak = NULL;
  unsigned int audio_bitrate;

  int tnum;
  int audio_index = 0;

  /* for deciding data start and data size */
  int64_t first_video_offset = -1;
  int64_t  last_video_offset = -1;
  int64_t first_audio_offset = -1;
  int64_t  last_audio_offset = -1;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* figure out where the data begins and ends */
  if (this->qt->video_trak != -1) {
    video_trak = &this->qt->traks[this->qt->video_trak];
    first_video_offset = video_trak->frames[0].offset;
    last_video_offset = video_trak->frames[video_trak->frame_count - 1].size +
      video_trak->frames[video_trak->frame_count - 1].offset;
  }
  if (this->qt->audio_trak != -1) {
    audio_trak = &this->qt->traks[this->qt->audio_trak];
    first_audio_offset = audio_trak->frames[0].offset;
    last_audio_offset = audio_trak->frames[audio_trak->frame_count - 1].size +
      audio_trak->frames[audio_trak->frame_count - 1].offset;
  }

  if (first_video_offset < first_audio_offset)
    this->data_start = first_video_offset;
  else
    this->data_start = first_audio_offset;

  if (last_video_offset > last_audio_offset)
    this->data_size = last_video_offset - this->data_size;
  else
    this->data_size = last_audio_offset - this->data_size;

  /* sort out the A/V information */
  if (this->qt->video_trak != -1) {

    this->bih.biSize = sizeof(this->bih);
    this->bih.biWidth = video_trak->properties->video.width;
    this->bih.biHeight = video_trak->properties->video.height;
    this->bih.biBitCount = video_trak->properties->video.depth;

    this->bih.biCompression = video_trak->properties->video.codec_fourcc;
    video_trak->properties->video.codec_buftype =
      _x_fourcc_to_buf_video(this->bih.biCompression);

    /* hack: workaround a fourcc clash! 'mpg4' is used by MS and Sorenson
     * mpeg4 codecs (they are not compatible).
     */
    if( video_trak->properties->video.codec_buftype == BUF_VIDEO_MSMPEG4_V1 )
      video_trak->properties->video.codec_buftype = BUF_VIDEO_MPEG4;

    if( !video_trak->properties->video.codec_buftype &&
         video_trak->properties->video.codec_fourcc )
    {
      video_trak->properties->video.codec_buftype = BUF_VIDEO_UNKNOWN;
      _x_report_video_fourcc (this->stream->xine, LOG_MODULE,
			      video_trak->properties->video.codec_fourcc);
    }

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
                         this->bih.biWidth);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
                         this->bih.biHeight);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC,
                         video_trak->properties->video.codec_fourcc);

  } else {

    memset(&this->bih, 0, sizeof(this->bih));
    this->bih.biSize = sizeof(this->bih);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC, 0);

  }

  if (this->qt->audio_trak != -1) {

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
      audio_trak->properties->audio.channels);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
      audio_trak->properties->audio.sample_rate);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
      audio_trak->properties->audio.bits);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC,
      audio_trak->properties->audio.codec_fourcc);

  } else {

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS, 0);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, 0);

  }

  /* copy over the meta information like artist and title */
  if (this->qt->artist)
    _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->qt->artist);
  else if (this->qt->copyright)
    _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->qt->copyright);
  if (this->qt->name)
    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->qt->name);
  else if (this->qt->description)
    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->qt->description);
  if (this->qt->composer)
    _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, this->qt->composer);
  else if (this->qt->comment)
    _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, this->qt->comment);
  if (this->qt->album)
    _x_meta_info_set(this->stream, XINE_META_INFO_ALBUM, this->qt->album);
  if (this->qt->genre)
    _x_meta_info_set(this->stream, XINE_META_INFO_GENRE, this->qt->genre);
  if (this->qt->year)
    _x_meta_info_set(this->stream, XINE_META_INFO_YEAR, this->qt->year);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (video_trak &&
      (video_trak->properties->video.codec_buftype)) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;

    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = video_trak->properties->video.codec_buftype;
    this->video_fifo->put (this->video_fifo, buf);

    /* send header info to decoder. some mpeg4 streams need this */
    if( video_trak->decoder_config ) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = video_trak->properties->video.codec_buftype;

      if (video_trak->properties->video.codec_fourcc == AVC1_FOURCC) {
        buf->size = 0;
        buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
        buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
        buf->decoder_info[2] = video_trak->decoder_config_len;
        buf->decoder_info_ptr[2] = video_trak->decoder_config;
      } else {
        buf->size = video_trak->decoder_config_len;
        buf->content = video_trak->decoder_config;
      }

      this->video_fifo->put (this->video_fifo, buf);
    }

    /* send off the palette, if there is one */
    if (video_trak->properties->video.palette_count) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = video_trak->properties->video.palette_count;
      buf->decoder_info_ptr[2] = &video_trak->properties->video.palette;
      buf->size = 0;
      buf->type = video_trak->properties->video.codec_buftype;
      this->video_fifo->put (this->video_fifo, buf);
    }

    /* send stsd to the decoder */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
    buf->decoder_info[1] = BUF_SPECIAL_STSD_ATOM;
    buf->decoder_info[2] = video_trak->properties->video.properties_atom_size;
    buf->decoder_info_ptr[2] = video_trak->properties->video.properties_atom;
    buf->size = 0;
    buf->type = video_trak->properties->video.codec_buftype;
    this->video_fifo->put (this->video_fifo, buf);
  }

  for (tnum = 0; tnum < this->qt->trak_count; tnum++) {

    audio_trak = &this->qt->traks[tnum];
    if (audio_trak->type != MEDIA_AUDIO)
      continue;

    /* in mp4 files the audio fourcc is always 'mp4a' - the codec is
     * specified by the object type id field in the esds atom */
    if (audio_trak->properties->audio.codec_fourcc == MP4A_FOURCC) {
      switch (audio_trak->object_type_id) {
        case 221:
          audio_trak->properties->audio.codec_buftype = BUF_AUDIO_VORBIS;
          break;
        case 107:
          audio_trak->properties->audio.codec_buftype = BUF_AUDIO_MPEG;
          break;
        default:
          /* default to AAC if we have no better idea */
          audio_trak->properties->audio.codec_buftype = BUF_AUDIO_AAC;
          break;
      }
    } else {
      audio_trak->properties->audio.codec_buftype =
        _x_formattag_to_buf_audio (audio_trak->properties->audio.codec_fourcc);
    }

    if (!audio_trak->properties->audio.codec_buftype &&
         audio_trak->properties->audio.codec_fourcc) {
      audio_trak->properties->audio.codec_buftype = BUF_AUDIO_UNKNOWN;
      _x_report_audio_format_tag (this->stream->xine, LOG_MODULE,
        audio_trak->properties->audio.codec_fourcc);
    }

    if ((audio_trak->properties->audio.codec_buftype == 0) ||
        (audio_index >= MAX_AUDIO_TRAKS) ||
        (this->audio_fifo == NULL))
      continue;

    this->qt->audio_traks[audio_index] = tnum;
    audio_trak->audio_index = audio_index;

    /* set the audio bitrate field (only for CBR audio) */
    if (!audio_trak->properties->audio.vbr) {
      audio_bitrate =
        audio_trak->properties->audio.sample_rate /
        audio_trak->properties->audio.samples_per_frame *
        audio_trak->properties->audio.bytes_per_frame *
        audio_trak->properties->audio.channels *
        8;
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE,
        audio_bitrate);
    }

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = audio_trak->properties->audio.codec_buftype | audio_index;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = audio_trak->properties->audio.sample_rate;
    buf->decoder_info[2] = audio_trak->properties->audio.bits;
    buf->decoder_info[3] = audio_trak->properties->audio.channels;

    if( audio_trak->properties->audio.wave_size ) {
      if( audio_trak->properties->audio.wave_size > buf->max_size )
        buf->size = buf->max_size;
      else
        buf->size = audio_trak->properties->audio.wave_size;
      memcpy(buf->content, audio_trak->properties->audio.wave, buf->size);
    } else {
      buf->size = 0;
      buf->content = NULL;
    }

    this->audio_fifo->put (this->audio_fifo, buf);

    if( audio_trak->decoder_config ) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = audio_trak->properties->audio.codec_buftype | audio_index;
      buf->size = 0;
      buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
      buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
      buf->decoder_info[2] = audio_trak->decoder_config_len;
      buf->decoder_info_ptr[2] = audio_trak->decoder_config;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    /* send stsd to the decoder */
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
    buf->decoder_info[1] = BUF_SPECIAL_STSD_ATOM;
    buf->decoder_info[2] = audio_trak->properties->audio.properties_atom_size;
    buf->decoder_info_ptr[2] = audio_trak->properties->audio.properties_atom;
    buf->size = 0;
    buf->type = audio_trak->properties->audio.codec_buftype | audio_index;
    this->audio_fifo->put (this->audio_fifo, buf);

    this->qt->audio_trak_count = ++audio_index;
  }
}

/* support function that performs a binary seek on a trak; returns the
 * demux status */
static int binary_seek(qt_trak *trak, off_t start_pos, int start_time) {

  int best_index;
  int left, middle, right;
  int found;

  /* perform a binary search on the trak, testing the offset
   * boundaries first; offset request has precedent over time request */
  if (start_pos) {
    if (start_pos <= trak->frames[0].offset)
      best_index = 0;
    else if (start_pos >= trak->frames[trak->frame_count - 1].offset)
      best_index = trak->frame_count - 1;
    else {
      left = 0;
      right = trak->frame_count - 1;
      found = 0;

      while (!found) {
	middle = (left + right + 1) / 2;
        if ((start_pos >= trak->frames[middle].offset) &&
            (start_pos < trak->frames[middle + 1].offset)) {
          found = 1;
        } else if (start_pos < trak->frames[middle].offset) {
          right = middle - 1;
        } else {
          left = middle;
        }
      }

      best_index = middle;
    }
  } else {
    int64_t pts = 90 * start_time;

    if (pts <= trak->frames[0].pts)
      best_index = 0;
    else if (pts >= trak->frames[trak->frame_count - 1].pts)
      best_index = trak->frame_count - 1;
    else {
      left = 0;
      right = trak->frame_count - 1;
      do {
	middle = (left + right + 1) / 2;
	if (pts < trak->frames[middle].pts) {
	  right = (middle - 1);
	} else {
	  left = middle;
	}
      } while (left < right);

      best_index = left;
    }
  }

  trak->current_frame = best_index;
  return DEMUX_OK;
}

static int demux_qt_seek (demux_plugin_t *this_gen,
                          off_t start_pos, int start_time, int playing) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  qt_trak *video_trak = NULL;
  qt_trak *audio_trak = NULL;
  int i;
  int64_t keyframe_pts = -1;

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  /* short-circuit any attempts to seek in a non-seekable stream, including
   * seeking in the forward direction; this may change later */
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
    this->qt->seek_flag = 1;
    this->status = DEMUX_OK;
    return this->status;
  }

  /* if there is a video trak, position it as close as possible to the
   * requested position */
  if (this->qt->video_trak != -1) {
    video_trak = &this->qt->traks[this->qt->video_trak];
    this->status = binary_seek(video_trak, start_pos, start_time);
    if (this->status != DEMUX_OK)
      return this->status;
    /* search back in the video trak for the nearest keyframe */
    while (video_trak->current_frame) {
      if (video_trak->frames[video_trak->current_frame].keyframe) {
        break;
      }
      video_trak->current_frame--;
    }
    keyframe_pts = video_trak->frames[video_trak->current_frame].pts;
  }

  /* seek all supported audio traks */
  for (i = 0; i < this->qt->audio_trak_count; i++) {
    audio_trak = &this->qt->traks[this->qt->audio_traks[i]];
    this->status = binary_seek(audio_trak, start_pos, start_time);
    if (this->status != DEMUX_OK)
      return this->status;
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe; do not go through with this process there is
   * no video trak */
  if (keyframe_pts >= 0) for (i = 0; i < this->qt->audio_trak_count; i++) {
    audio_trak = &this->qt->traks[this->qt->audio_traks[i]];
    if (keyframe_pts > audio_trak->frames[audio_trak->frame_count - 1].pts) {
      /* whoops, this trak is too short, mark it finished */
      audio_trak->current_frame = audio_trak->frame_count;
    } else while (audio_trak->current_frame) {
      if (audio_trak->frames[audio_trak->current_frame].pts <= keyframe_pts) {
        break;
      }
      audio_trak->current_frame--;
    }
  }

  this->qt->seek_flag = 1;
  this->status = DEMUX_OK;

  /*
   * do only flush if already running (seeking).
   * otherwise decoder_config is flushed too.
   */
  if(playing)
    _x_demux_flush_engine(this->stream);

  return this->status;
}

static void demux_qt_dispose (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  free_qt_info(this->qt);
  free(this);
}

static int demux_qt_get_status (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->status;
}

static int demux_qt_get_stream_length (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  if (this->qt->timescale == 0)
    return 0;

  return (int)((int64_t) 1000 * this->qt->duration / this->qt->timescale);
}

static uint32_t demux_qt_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_AUDIOLANG;
}

static int demux_qt_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  /* be a bit paranoid */
  if (this == NULL || this->stream == NULL)
    return DEMUX_OPTIONAL_UNSUPPORTED;

  switch (data_type) {
    case DEMUX_OPTIONAL_DATA_AUDIOLANG: {
      char *str   = data;
      int channel = *((int *)data);
      if ((channel < 0) || (channel >= this->qt->audio_trak_count)) {
        strcpy (str, "none");
      } else {
        int lang = this->qt->traks[this->qt->audio_traks[channel]].lang;
        if ((lang < 0x400) || (lang == 0x7fff)) {
          sprintf (str, "%d", channel);
        } else {
          int i;
          for (i = 10; i >= 0; i -= 5)
            *str++ = 0x60 | ((lang >> i) & 0x1f);
          *str = 0;
        }
        return DEMUX_OPTIONAL_SUCCESS;
      }
    }
    break;
    default: ;
  }
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_qt_t     *this;
  xine_cfg_entry_t entry;
  qt_error last_error;

  if ((input->get_capabilities(input) & INPUT_CAP_BLOCK)) {
    return NULL;
  }

  this         = calloc(1, sizeof(demux_qt_t));
  this->stream = stream;
  this->input  = input;

  /* fetch bandwidth config */
  this->bandwidth = 0x7FFFFFFFFFFFFFFFLL;  /* assume infinite bandwidth */
  if (xine_config_lookup_entry (stream->xine, "media.network.bandwidth",
                                &entry)) {
    if ((entry.num_value >= 0) && (entry.num_value <= 11))
      this->bandwidth = bandwidths[entry.num_value];
  }

  this->demux_plugin.send_headers      = demux_qt_send_headers;
  this->demux_plugin.send_chunk        = demux_qt_send_chunk;
  this->demux_plugin.seek              = demux_qt_seek;
  this->demux_plugin.dispose           = demux_qt_dispose;
  this->demux_plugin.get_status        = demux_qt_get_status;
  this->demux_plugin.get_stream_length = demux_qt_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_qt_get_capabilities;
  this->demux_plugin.get_optional_data = demux_qt_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    last_error = open_qt_file(this->qt, this->input, this->bandwidth);
    if (last_error == QT_DRM_NOT_SUPPORTED) {

      /* special consideration for DRM-protected files */
      if (this->qt->last_error == QT_DRM_NOT_SUPPORTED)
        _x_message (this->stream, XINE_MSG_ENCRYPTED_SOURCE,
          "DRM-protected Quicktime file", NULL);

    } else if (last_error != QT_OK) {

      free_qt_info (this->qt);
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT: {

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    if (open_qt_file(this->qt, this->input, this->bandwidth) != QT_OK) {
      free_qt_info (this->qt);
      free (this);
      return NULL;
    }
  }
  break;

  default:
    free (this);
    return NULL;
  }

  if (this->qt->fragment_count > 0)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      _("demux_qt: added %d fragments\n"), this->qt->fragment_count);

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_qt_class_t     *this;

  this         = calloc(1, sizeof(demux_qt_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Apple Quicktime (MOV) and MPEG-4 demux plugin");
  this->demux_class.identifier      = "MOV/MPEG-4";
  this->demux_class.mimetypes       =
    "video/quicktime: mov,qt: Quicktime animation;"
    "video/x-quicktime: mov,qt: Quicktime animation;"
    "audio/x-m4a: m4a,m4b: MPEG-4 audio;"
    "video/mp4: f4v,mp4,mpg4: MPEG-4 video;"
    "audio/mp4: f4a,mp4,mpg4: MPEG-4 audio;";
  this->demux_class.extensions      = "mov qt mp4 m4a m4b f4a f4v";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_qt = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "quicktime", XINE_VERSION_CODE, &demux_info_qt, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

