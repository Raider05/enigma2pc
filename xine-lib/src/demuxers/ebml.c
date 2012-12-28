/*
 * Copyright (C) 2000-2003 the xine project
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
 * EBML parser
 * a lot of ideas from the gstreamer parser
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#define LOG_MODULE "ebml"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "bswap.h"

#include "ebml.h"


ebml_parser_t *new_ebml_parser (xine_t *xine, input_plugin_t *input) {
  ebml_parser_t *ebml;

  ebml = xine_xmalloc(sizeof(ebml_parser_t));
  ebml->xine                 = xine;
  ebml->input                = input;

  return ebml;
}


void dispose_ebml_parser(ebml_parser_t *ebml) {
  free(ebml);
}


uint32_t ebml_get_next_level(ebml_parser_t *ebml, ebml_elem_t *elem) {
  ebml_elem_t *parent_elem;

  if (ebml->level > 0) {
    parent_elem = &ebml->elem_stack[ebml->level - 1];
    while ((elem->start + elem->len) >= (parent_elem->start + parent_elem->len)) {
      lprintf("parent: %" PRIdMAX ", %" PRIu64 "; elem: %" PRIdMAX  ", %" PRIu64 "\n",
              (intmax_t)parent_elem->start, parent_elem->len, (intmax_t)elem->start, elem->len);
      ebml->level--;
      if (ebml->level == 0) break;
      parent_elem = &ebml->elem_stack[ebml->level - 1];
    }
  }
  lprintf("id: 0x%x, len: %" PRIu64 ", next_level: %d\n", elem->id, elem->len, ebml->level);
  return ebml->level;
}


static int ebml_read_elem_id(ebml_parser_t *ebml, uint32_t *id) {
  uint8_t   data[4];
  uint32_t  mask = 0x80;
  uint32_t  value;
  int       size = 1;
  int       i;

  if (ebml->input->read(ebml->input, data, 1) != 1) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: read error\n");
    return 0;
  }
  value = data[0];

  /* compute the size of the ID (1-4 bytes)*/
  while (size <= 4 && !(value & mask)) {
    size++;
    mask >>= 1;
  }
  if (size > 4) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: invalid EBML ID size (0x%x) at position %" PRIdMAX "\n",
            data[0], (intmax_t)pos);
    return 0;
  }

  /* read the rest of the id */
  if (ebml->input->read(ebml->input, data + 1, size - 1) != (size - 1)) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: read error at position %" PRIdMAX "\n", (intmax_t)pos);
    return 0;
  }
  for(i = 1; i < size; i++) {
    value = (value << 8) | data[i];
  }
  *id = value;

  return 1;
}


static int ebml_read_elem_len(ebml_parser_t *ebml, uint64_t *len) {
  uint8_t data[8];
  uint32_t mask = 0x80;
  int size = 1;
  int ff_bytes;
  uint64_t value;
  int i;

  if (ebml->input->read(ebml->input, data, 1) != 1) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: read error at position %" PRIdMAX "\n", (intmax_t)pos);
    return 0;
  }
  value = data[0];

  /* compute the size of the "data len" (1-8 bytes) */
  while (size <= 8 && !(value & mask)) {
    size++;
    mask >>= 1;
  }
  if (size > 8) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: Invalid EBML length size (0x%x) at position %" PRIdMAX "\n",
             data[0], (intmax_t)pos);
    return 0;
  }

  /* remove size bits */
  value &= mask - 1;

  /* check if the first byte is full */
  if (value == (mask - 1))
    ff_bytes = 1;
  else
    ff_bytes = 0;

  /* read the rest of the len */
  if (ebml->input->read(ebml->input, data + 1, size - 1) != (size - 1)) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: read error at position %" PRIdMAX "\n", (intmax_t)pos);
    return 0;
  }
  for (i = 1; i < size; i++) {
    if (data[i] == 0xff)
      ff_bytes++;
    value = (value << 8) | data[i];
  }

  if (ff_bytes == size)
    *len = -1;
  else
    *len = value;

  return 1;
}


static int ebml_read_elem_data(ebml_parser_t *ebml, void *buf, int64_t len) {

  if (ebml->input->read(ebml->input, buf, len) != len) {
    off_t pos = ebml->input->get_current_pos(ebml->input);
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: read error at position %" PRIdMAX "\n", (intmax_t)pos);
    return 0;
  }

  return 1;
}


int ebml_skip(ebml_parser_t *ebml, ebml_elem_t *elem) {
  if (ebml->input->seek(ebml->input, elem->len, SEEK_CUR) < 0) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: seek error\n");
    return 0;
  }

  return 1;
}


int ebml_read_elem_head(ebml_parser_t *ebml, ebml_elem_t *elem) {

  int ret_id  = ebml_read_elem_id(ebml, &elem->id);

  int ret_len = ebml_read_elem_len(ebml, &elem->len);

  elem->start = ebml->input->get_current_pos(ebml->input);

  return (ret_id && ret_len);
}


int ebml_read_uint(ebml_parser_t *ebml, ebml_elem_t *elem, uint64_t *num) {
  uint8_t  data[8];
  uint64_t size = elem->len;

  if ((elem->len < 1) || (elem->len > 8)) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: Invalid integer element size %" PRIu64 "\n", size);
    return 0;
  }

  if (!ebml_read_elem_data (ebml, data, size))
    return 0;

  *num = 0;
  while (size > 0) {
    *num = (*num << 8) | data[elem->len - size];
    size--;
  }

  return 1;
}

#if 0
int ebml_read_sint (ebml_parser_t *ebml, ebml_elem_t  *elem, int64_t *num) {
  uint8_t  data[8];
  uint64_t size = elem->len;

  if ((elem->len < 1) || (elem->len > 8)) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: Invalid integer element size %" PRIu64 "\n", size);
    return 0;
  }

  if (!ebml_read_elem_data(ebml, data, size))
    return 0;

  /* propagate negative bit */
  if (data[0] & 80)
    *num = -1;
  else
    *num = 0;

  while (size > 0) {
    *num = (*num << 8) | data[elem->len - size];
    size--;
  }

  return 1;
}
#endif


int ebml_read_float (ebml_parser_t *ebml, ebml_elem_t *elem, double *num) {
  uint8_t  data[10];
  uint64_t size = elem->len;

  if ((size != 4) && (size != 8) && (size != 10)) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: Invalid float element size %" PRIu64 "\n", size);
    return 0;
  }

  if (!ebml_read_elem_data(ebml, data, size))
    return 0;

  if (size == 10) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: FIXME! 10-byte floats unimplemented\n");
    return 0;
  }

  if (size == 4) {
    float f;

    *((uint32_t *) &f) = _X_BE_32(data);
    *num = f;
  } else {
    double d;

    *((uint64_t *) &d) = _X_BE_64(data);
    *num = d;
  }
  return 1;
}

int ebml_read_ascii(ebml_parser_t *ebml, ebml_elem_t *elem, char *str) {
  uint64_t size = elem->len;

  if (!ebml_read_elem_data(ebml, str, size))
    return 0;

  return 1;
}

#if 0
int ebml_read_utf8 (ebml_parser_t *ebml, ebml_elem_t *elem, char *str) {
  return ebml_read_ascii (ebml, elem, str);
}
#endif

char *ebml_alloc_read_ascii (ebml_parser_t *ebml, ebml_elem_t *elem)
{
  char *text;
  if (elem->len >= 4096)
    return NULL;
  text = malloc(elem->len + 1);
  if (text)
  {
    text[elem->len] = '\0';
    if (ebml_read_ascii (ebml, elem, text))
      return text;
    free (text);
  }
  return NULL;
}

#if 0
int ebml_read_date (ebml_parser_t *ebml, ebml_elem_t *elem, int64_t *date) {
  return ebml_read_sint (ebml, elem, date);
}
#endif

int ebml_read_master (ebml_parser_t *ebml, ebml_elem_t *elem) {
  ebml_elem_t *top_elem;

  if (ebml->level < 0) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: invalid current level\n");
    return 0;
  }

  top_elem = &ebml->elem_stack[ebml->level];
  top_elem->start = elem->start;
  top_elem->len = elem->len;
  top_elem->id = elem->id;

  ebml->level++;
  lprintf("id: 0x%x, len: %" PRIu64 ", level: %d\n", elem->id, elem->len, ebml->level);
  if (ebml->level >= EBML_STACK_SIZE) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
	    "ebml: max level exceeded\n");
    return 0;
  }
  return 1;
}

int ebml_read_binary(ebml_parser_t *ebml, ebml_elem_t *elem, void *binary) {
  return !!ebml_read_elem_data(ebml, binary, elem->len);
}

int ebml_check_header(ebml_parser_t *ebml) {
  uint32_t next_level;
  ebml_elem_t master;

  if (!ebml_read_elem_head(ebml, &master)) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: invalid master element\n");
    return 0;
  }

  if (master.id != EBML_ID_EBML) {
    xprintf(ebml->xine, XINE_VERBOSITY_LOG,
            "ebml: invalid master element\n");
    return 0;
  }

  if (!ebml_read_master (ebml, &master))
    return 0;

  next_level = 1;
  while (next_level == 1) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case EBML_ID_EBMLVERSION: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("ebml_version: %" PRIu64 "\n", num);
        ebml->version = num;
        break;
      }

      case EBML_ID_EBMLREADVERSION: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("ebml_read_version: %" PRIu64 "\n", num);
        if (num != EBML_VERSION)
          return 0;
        ebml->read_version = num;
        break;
      }

      case EBML_ID_EBMLMAXIDLENGTH: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("ebml_max_id_length: %" PRIu64 "\n", num);
        ebml->max_id_len = num;
        break;
      }

      case EBML_ID_EBMLMAXSIZELENGTH: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("ebml_max_size_length: %" PRIu64 "\n", num);
        ebml->max_size_len = num;
        break;
      }

      case EBML_ID_DOCTYPE: {
        char *text = ebml_alloc_read_ascii (ebml, &elem);
        if (!text)
          return 0;

        lprintf("doctype: %s\n", text);
        if (ebml->doctype)
          free (ebml->doctype);
        ebml->doctype = text;
        break;
      }

      case EBML_ID_DOCTYPEVERSION: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("doctype_version: %" PRIu64 "\n", num);
        ebml->doctype_version = num;
        break;
      }

      case EBML_ID_DOCTYPEREADVERSION: {
        uint64_t num;

        if (!ebml_read_uint (ebml, &elem, &num))
          return 0;
        lprintf("doctype_read_version: %" PRIu64 "\n", num);
        ebml->doctype_read_version = num;
        break;
      }

      default:
        xprintf(ebml->xine, XINE_VERBOSITY_LOG,
                "ebml: Unknown data type 0x%x in EBML header (ignored)\n", elem.id);
	ebml_skip(ebml, &elem);
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  return 1;
}
