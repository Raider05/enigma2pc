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
 * demultiplexer for matroska streams: shared header
 */

#ifndef _DEMUX_MATROSKA_H_
#define _DEMUX_MATROSKA_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

#include <xine/xine_internal.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"

#include "ebml.h"
#include "matroska.h"

#define NUM_PREVIEW_BUFFERS      10

#define MAX_STREAMS             128
#define MAX_FRAMES               32

#define WRAP_THRESHOLD        90000

typedef struct {
  int                  track_num;
  off_t               *pos;
  uint64_t            *timecode;
  int                  num_entries;

} matroska_index_t;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  input_plugin_t      *input;

  int                  status;

  ebml_parser_t       *ebml;

  /* segment element */
  ebml_elem_t          segment;
  uint64_t             timecode_scale;
  int                  duration;            /* in millis */
  int                  preview_sent;
  int                  preview_mode;
  char                *title;

  /* meta seek info */
  int                  has_seekhead;
  int                  seekhead_handled;

  /* seek info */
  matroska_index_t    *indexes;
  int                  num_indexes;
  int                  first_cluster_found;
  int                  skip_to_timecode;
  int                  skip_for_track;

  /* tracks */
  int                  num_tracks;
  int                  num_video_tracks;
  int                  num_audio_tracks;
  int                  num_sub_tracks;

  matroska_track_t    *tracks[MAX_STREAMS];
  size_t               compress_maxlen;

  /* maintain editions, number and capacity */
  int                  num_editions, cap_editions;
  matroska_edition_t **editions;

  /* block */
  uint8_t             *block_data;
  size_t               block_data_size;

  /* current tracks */
  matroska_track_t    *video_track;   /* to remove */
  matroska_track_t    *audio_track;   /* to remove */
  matroska_track_t    *sub_track;     /* to remove */
  uint64_t             last_timecode;

  int                  send_newpts;
  int                  buf_flag_seek;

  /* seekhead parsing */
  int                  top_level_list_size;
  int                  top_level_list_max_size;
  off_t               *top_level_list;

  /* event handling (chapter navigation) */
  xine_event_queue_t  *event_queue;
} demux_matroska_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;

} demux_matroska_class_t;

/* "entry points" for chapter handling.
 * The parser descends into "Chapters" elements at the _parse_ function,
 * and editions care about cleanup internally. */
int matroska_parse_chapters(demux_matroska_t*);
void matroska_free_editions(demux_matroska_t*);

/* Search an edition for the chapter matching a given timecode.
 *
 * Return: chapter index, or -1 if none is found.
 *
 * TODO: does not handle chapter end times yet.
 */
int matroska_get_chapter(demux_matroska_t*, uint64_t, matroska_edition_t**);

#endif /* _DEMUX_MATROSKA_H_ */
