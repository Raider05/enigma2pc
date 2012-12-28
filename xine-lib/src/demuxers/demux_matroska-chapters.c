/*
 * Copyright (C) 2009 the xine project
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
 * demultiplexer for matroska streams: chapter handling
 *
 * TODO:
 *  - nested chapters
 *
 * Authors:
 *  Nicos Gollan <gtdev@spearhead.de>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LOG_MODULE "demux_matroska_chapters"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#include "ebml.h"
#include "matroska.h"
#include "demux_matroska.h"

/* TODO: this only handles one single (title, language, country) tuple.
 *  See the header for information. */
static int parse_chapter_display(demux_matroska_t *this, matroska_chapter_t *chap, int level) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = level+1;
  char* tmp_name = NULL;
  char* tmp_lang = NULL;
  char* tmp_country = NULL;

  while (next_level == level+1) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {

      case MATROSKA_ID_CH_STRING:
        tmp_name = ebml_alloc_read_ascii(ebml, &elem);
        break;

      case MATROSKA_ID_CH_LANGUAGE:
        tmp_lang = ebml_alloc_read_ascii(ebml, &elem);
        break;

      case MATROSKA_ID_CH_COUNTRY:
        tmp_country = ebml_alloc_read_ascii(ebml, &elem);
        break;

      default:
        lprintf("Unhandled ID (inside ChapterDisplay): 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }

    next_level = ebml_get_next_level(ebml, &elem);
  }

  if (NULL != chap->title) {
    chap->title = tmp_name;

    free(chap->language);
    chap->language = tmp_lang;

    free(chap->country);
    chap->country = tmp_country;
  } else if (tmp_lang != NULL && !strcmp("eng", tmp_lang) && (chap->language == NULL || strcmp("eng", chap->language))) {
    free(chap->title);
    chap->title = tmp_name;

    free(chap->language);
    chap->language = tmp_lang;

    free(chap->country);
    chap->country = tmp_country;
  } else {
    free(tmp_name);
    free(tmp_lang);
    free(tmp_country);
  }

  return 1;
}

static int parse_chapter_atom(demux_matroska_t *this, matroska_chapter_t *chap, int level) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = level+1;
  uint64_t num;

  chap->time_start = 0;
  chap->time_end = 0;
  chap->hidden = 0;
  chap->enabled = 1;

  while (next_level == level+1) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem)) {
      lprintf("invalid head\n");
      return 0;
    }

    switch (elem.id) {
      case MATROSKA_ID_CH_UID:
        if (!ebml_read_uint(ebml, &elem, &chap->uid)) {
          lprintf("invalid UID\n");
          return 0;
        }
        break;

      case MATROSKA_ID_CH_TIMESTART:
        if (!ebml_read_uint(ebml, &elem, &chap->time_start)) {
          lprintf("invalid start time\n");
          return 0;
        }
        /* convert to xine timing: Matroska timestamps are in nanoseconds,
         * xine's PTS are in 1/90,000s */
        chap->time_start /= 100000;
        chap->time_start *= 9;
        break;

      case MATROSKA_ID_CH_TIMEEND:
        if (!ebml_read_uint(ebml, &elem, &chap->time_end)) {
          lprintf("invalid end time\n");
          return 0;
        }
        /* convert to xine timing */
        chap->time_end /= 100000;
        chap->time_end *= 9;
        break;

      case MATROSKA_ID_CH_DISPLAY:
        if (!ebml_read_master(ebml, &elem))
          return 0;

        lprintf("ChapterDisplay\n");
        if(!parse_chapter_display(this, chap, level+1)) {
          lprintf("invalid display information\n");
          return 0;
        }
        break;

      case MATROSKA_ID_CH_HIDDEN:
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        chap->hidden = (int)num;
        break;

      case MATROSKA_ID_CH_ENABLED:
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        chap->enabled = (int)num;
        break;

      case MATROSKA_ID_CH_ATOM: /* TODO */
        xprintf(this->stream->xine, XINE_VERBOSITY_NONE,
            LOG_MODULE ": Warning: Nested chapters are not supported, playback may suffer!\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;

      case MATROSKA_ID_CH_TRACK: /* TODO */
        xprintf(this->stream->xine, XINE_VERBOSITY_NONE,
            LOG_MODULE ": Warning: Specific track information in chapters is not supported, playback may suffer!\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;

      default:
        lprintf("Unhandled ID (inside ChapterAtom): 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }

    next_level = ebml_get_next_level(ebml, &elem);
  }

  /* fallback information */
  /* FIXME: check allocations! */
  if (NULL == chap->title) {
    chap->title = malloc(9);
    if (chap->title != NULL)
      strncpy(chap->title, "No title", 9);
  }

  if (NULL == chap->language) {
    chap->language = malloc(4);
    if (chap->language != NULL)
      strncpy(chap->language, "unk", 4);
  }

  if (NULL == chap->country) {
    chap->country = malloc(3);
    if (chap->country != NULL)
      strncpy(chap->country, "XX", 3);
  }

  lprintf( "Chapter 0x%" PRIx64 ": %" PRIu64 "-%" PRIu64 "(pts), %s (%s). %shidden, %senabled.\n",
      chap->uid, chap->time_start, chap->time_end,
      chap->title, chap->language,
      (chap->hidden ? "" : "not "),
      (chap->enabled ? "" : "not "));

  return 1;
}

static void free_chapter(demux_matroska_t *this, matroska_chapter_t *chap) {
  free(chap->title);
  free(chap->language);
  free(chap->country);

  free(chap);
}

static int parse_edition_entry(demux_matroska_t *this, matroska_edition_t *ed) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;
  uint64_t num;
  int i;

  ed->hidden = 0;
  ed->is_default = 0;
  ed->ordered = 0;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CH_ED_UID:
        if (!ebml_read_uint(ebml, &elem, &ed->uid))
          return 0;
        break;

      case MATROSKA_ID_CH_ED_HIDDEN:
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        ed->hidden = (int)num;
        break;

      case MATROSKA_ID_CH_ED_DEFAULT:
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        ed->is_default = (int)num;
        break;

      case MATROSKA_ID_CH_ED_ORDERED:
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        ed->ordered = (int)num;
        break;

      case MATROSKA_ID_CH_ATOM:
        {
          matroska_chapter_t *chapter = calloc(1, sizeof(matroska_chapter_t));
          if (NULL == chapter)
            return 0;

          lprintf("ChapterAtom\n");
          if (!ebml_read_master(ebml, &elem))
            return 0;

          if (!parse_chapter_atom(this, chapter, next_level))
            return 0;

          /* resize chapters array if necessary */
          if (ed->num_chapters >= ed->cap_chapters) {
            matroska_chapter_t** old_chapters = ed->chapters;
            ed->cap_chapters += 10;
            ed->chapters = realloc(ed->chapters, ed->cap_chapters * sizeof(matroska_chapter_t*));

            if (NULL == ed->chapters) {
              ed->chapters = old_chapters;
              ed->cap_chapters -= 10;
              return 0;
            }
          }

          ed->chapters[ed->num_chapters] = chapter;
          ++ed->num_chapters;

          break;
        }

      default:
        lprintf("Unhandled ID (inside EditionEntry): 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }

    next_level = ebml_get_next_level(ebml, &elem);
  }

  xprintf( this->stream->xine, XINE_VERBOSITY_LOG,
      LOG_MODULE ": Edition 0x%" PRIx64 ": %shidden, %sdefault, %sordered. %d chapters:\n",
      ed->uid,
      (ed->hidden ? "" : "not "),
      (ed->is_default ? "" : "not "),
      (ed->ordered ? "" : "not "),
      ed->num_chapters );

  for (i=0; i<ed->num_chapters; ++i) {
    matroska_chapter_t* chap = ed->chapters[i];
    xprintf( this->stream->xine, XINE_VERBOSITY_LOG,
        LOG_MODULE ":  Chapter %d: %" PRIu64 "-%" PRIu64 "(pts), %s (%s). %shidden, %senabled.\n",
        i+1, chap->time_start, chap->time_end,
        chap->title, chap->language,
        (chap->hidden ? "" : "not "),
        (chap->enabled ? "" : "not "));
  }

  return 1;
}

static void free_edition(demux_matroska_t *this, matroska_edition_t *ed) {
  int i;

  for(i=0; i<ed->num_chapters; ++i) {
    free_chapter(this, ed->chapters[i]);
  }
  free(ed->chapters);
  free(ed);
}

int matroska_parse_chapters(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CH_EDITIONENTRY:
        {
          matroska_edition_t *edition = calloc(1, sizeof(matroska_edition_t));
          if (NULL == edition)
            return 0;

          lprintf("EditionEntry\n");
          if (!ebml_read_master(ebml, &elem))
            return 0;

          if (!parse_edition_entry(this, edition))
            return 0;

          /* resize editions array if necessary */
          if (this->num_editions >= this->cap_editions) {
            matroska_edition_t** old_editions = this->editions;
            this->cap_editions += 10;
            this->editions = realloc(this->editions, this->cap_editions * sizeof(matroska_edition_t*));

            if (NULL == this->editions) {
              this->editions = old_editions;
              this->cap_editions -= 10;
              return 0;
            }
          }

          this->editions[this->num_editions] = edition;
          ++this->num_editions;

          break;
        }

      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }

    next_level = ebml_get_next_level(ebml, &elem);
  }

  return 1;
}

void matroska_free_editions(demux_matroska_t *this) {
  int i;

  for(i=0; i<this->num_editions; ++i) {
    free_edition(this, this->editions[i]);
  }
  free(this->editions);
  this->num_editions = 0;
  this->cap_editions = 0;
}

int matroska_get_chapter(demux_matroska_t *this, uint64_t tc, matroska_edition_t** ed) {
  uint64_t block_pts = (tc * this->timecode_scale) / 100000 * 9;
  int chapter_idx = 0;

  if (this->num_editions < 1)
    return -1;

  while (chapter_idx < (*ed)->num_chapters && block_pts > (*ed)->chapters[chapter_idx]->time_start)
    ++chapter_idx;

  if (chapter_idx > 0)
    --chapter_idx;

  return chapter_idx;
}
