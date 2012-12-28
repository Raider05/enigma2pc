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
#ifndef EBML_H
#define EBML_H

#define EBML_STACK_SIZE 10
#define EBML_VERSION 1

/* EBML IDs */
#define EBML_ID_EBML                0x1A45DFA3
#define EBML_ID_EBMLVERSION         0x4286
#define EBML_ID_EBMLREADVERSION     0x42F7
#define EBML_ID_EBMLMAXIDLENGTH     0x42F2
#define EBML_ID_EBMLMAXSIZELENGTH   0x42F3
#define EBML_ID_DOCTYPE             0x4282
#define EBML_ID_DOCTYPEVERSION      0x4287
#define EBML_ID_DOCTYPEREADVERSION  0x4285


typedef struct ebml_elem_s {
  uint32_t  id;
  off_t     start;
  uint64_t  len;
} ebml_elem_t;

typedef struct ebml_parser_s {

  /* xine stuff */
  xine_t                *xine;
  input_plugin_t        *input;

  /* EBML Parser Stack Management */
  ebml_elem_t            elem_stack[EBML_STACK_SIZE];
  int                    level;

  /* EBML Header Infos */
  uint64_t               version;
  uint64_t               read_version;
  uint64_t               max_id_len;
  uint64_t               max_size_len;
  char                  *doctype;
  uint64_t               doctype_version;
  uint64_t               doctype_read_version;

} ebml_parser_t;


ebml_parser_t *new_ebml_parser (xine_t *xine, input_plugin_t *input) XINE_MALLOC;

void dispose_ebml_parser (ebml_parser_t *ebml);

/* check EBML header */
int ebml_check_header(ebml_parser_t *read);


/* Element Header */
int ebml_read_elem_head(ebml_parser_t *ebml, ebml_elem_t *elem);

uint32_t ebml_get_next_level(ebml_parser_t *ebml, ebml_elem_t *elem);

int ebml_skip(ebml_parser_t *ebml, ebml_elem_t *elem);

/* EBML types */
int ebml_read_uint(ebml_parser_t *ebml, ebml_elem_t *elem, uint64_t *val);

#if 0
int ebml_read_sint(ebml_parser_t *ebml, ebml_elem_t *elem, int64_t *val);
#endif

int ebml_read_float(ebml_parser_t *ebml, ebml_elem_t  *elem, double *val);

int ebml_read_ascii(ebml_parser_t *ebml, ebml_elem_t *elem, char *str);

#if 0
int ebml_read_utf8(ebml_parser_t *ebml, ebml_elem_t *elem, char *str);
#endif

char *ebml_alloc_read_ascii(ebml_parser_t *ebml, ebml_elem_t *elem);

#if 0
int ebml_read_date(ebml_parser_t *ebml, ebml_elem_t *elem, int64_t *date);
#endif

int ebml_read_master(ebml_parser_t *ebml, ebml_elem_t *elem);

int ebml_read_binary(ebml_parser_t *ebml, ebml_elem_t *elem, void *binary);

#endif /* EBML_H */
