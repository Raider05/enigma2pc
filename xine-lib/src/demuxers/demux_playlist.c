/*
 * Copyright (C) 2007 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 *
 * Playlist parser/demuxer by
 *        Claudio Ciccani (klan@users.sourceforge.net)
 *
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

#define LOG_MODULE "demux_playlist"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "bswap.h"
#include <xine/demux.h>

typedef enum {
  XINE_PLT_NONE = 0,
  XINE_PLT_REF  = ME_FOURCC('R','E','F',0),
  XINE_PLT_M3U  = ME_FOURCC('M','3','U',0),
  XINE_PLT_RAM  = ME_FOURCC('R','A','M',0),
  XINE_PLT_PLS  = ME_FOURCC('P','L','S',0),
  XINE_PLT_ASX  = ME_FOURCC('A','S','X',0),
  XINE_PLT_SMI  = ME_FOURCC('S','M','I',0),
  XINE_PLT_QTL  = ME_FOURCC('Q','T','L',0),
  XINE_PLT_XSPF = ME_FOURCC('X','S','P',0),
  XINE_PLT_RSS  = ME_FOURCC('R','S','S',0)
} playlist_t;

typedef struct {
  demux_plugin_t     demux_plugin;

  xine_t            *xine;
  xine_stream_t     *stream;
  input_plugin_t    *input;

  playlist_t         playlist;

  int                status;
} demux_playlist_t;

typedef struct {
  demux_class_t      demux_class;
} demux_playlist_class_t;


static playlist_t detect_by_extension (input_plugin_t *input) {
  char *ext;

  ext = strrchr (input->get_mrl (input), '.');
  if (!ext)
    return XINE_PLT_NONE;

  if (!strcasecmp (ext, ".m3u"))
    return XINE_PLT_M3U;
  if (!strcasecmp (ext, ".ram"))
    return XINE_PLT_RAM;
  if (!strcasecmp (ext, ".pls"))
    return XINE_PLT_PLS;
  if (!strcasecmp (ext, ".wax") ||
      !strcasecmp (ext, ".wvx") ||
      !strcasecmp (ext, ".asx"))
    return XINE_PLT_ASX;
  if (!strcasecmp (ext, ".smi") ||
      !strcasecmp (ext, ".smil"))
    return XINE_PLT_SMI;
  if (!strcasecmp (ext, ".qtl"))
    return XINE_PLT_QTL;
  if (!strcasecmp (ext, ".xspf"))
    return XINE_PLT_XSPF;
  if (!strcasecmp (ext, ".rss"))
    return XINE_PLT_RSS;

  return XINE_PLT_NONE;
}

static playlist_t detect_by_content (input_plugin_t *input) {
  char buf[256], *tmp;
  int  len;

  len = _x_demux_read_header (input, buf, sizeof(buf)-1);
  if (len <= 0)
    return XINE_PLT_NONE;
  buf[len] = '\0';

  tmp = buf;
  while (*tmp && isspace(*tmp))
    tmp++;

  if (!strncmp (tmp, "[Reference]", 11) ||
      !strncmp (tmp, "Ref1=", 5))
    return XINE_PLT_REF;
  if (!strncmp (tmp, "#EXTM3U", 7))
    return XINE_PLT_M3U;
  if (!strncmp (tmp, "file://", 7) ||
      !strncmp (tmp, "http://", 7) ||
      !strncmp (tmp, "rtsp://", 7) ||
      !strncmp (tmp, "pnm://", 6))
    return XINE_PLT_RAM;
  if (!strncmp (tmp, "[Playlist]", 10 ))
    return XINE_PLT_PLS;
  if (!strncasecmp (tmp, "<ASX", 4))
    return XINE_PLT_ASX;
  if (!strncmp (tmp, "<smil", 5))
    return XINE_PLT_SMI;
  if (!strncmp (tmp, "<?quicktime", 11))
    return XINE_PLT_QTL;
  if (!strncmp (tmp, "<playlist", 9))
    return XINE_PLT_XSPF;
  if (!strncmp (tmp, "<rss", 4))
    return XINE_PLT_RSS;

  if (!strncmp (tmp, "<?xml", 5)) {
    tmp += 5;
    while ((tmp = strchr (tmp, '<'))) {
      if (!strncasecmp (tmp, "<ASX", 4))
        return XINE_PLT_ASX;
      if (!strncmp (tmp, "<smil", 5))
        return XINE_PLT_SMI;
      if (!strncmp (tmp, "<?quicktime", 11))
        return XINE_PLT_QTL;
      if (!strncmp (tmp, "<playlist", 9))
        return XINE_PLT_XSPF;
      if (!strncmp (tmp, "<rss", 4))
        return XINE_PLT_RSS;
      tmp++;
    }
  }

  return XINE_PLT_NONE;
}

static char* trim (char *s) {
  char *e;

  while (*s && isspace(*s))
    s++;

  e = s + strlen(s) - 1;
  while (e > s && isspace(*e))
    *e-- = '\0';

  return s;
}

static int parse_time (const char *s) {
  int t = 0;
  int i;

  if (!s)
    return 0;

  if (!strncmp (s, "npt=", 4))
    s += 4;
  else if (!strncmp (s, "smpte=", 6))
    s += 6;

  for (i = 0; i < 3; i++) {
    t *= 60;
    t += atoi(s);
    s = strchr (s, ':');
    if (!s)
      break;
    s++;
  }

  return t*1000;
}

static void parse_ref (demux_playlist_t *this, char *data, int length) {
  char *src = data;
  char *end;
  int   alt = 0;

  while (src && *src) {
    end = strchr (src, '\n');
    if (end)
      *end = '\0';

    src = trim (src);
    if (!strncmp (src, "Ref", 3)) {
      src = strchr (src, '=');
      if (src && *(src+1)) {
        lprintf ("mrl:'%s'\n", src);
        _x_demux_send_mrl_reference (this->stream, alt++, src+1, NULL, 0, 0);
      }
    }

    src = end;
    if (src)
      src++;
  }
}

static void parse_m3u (demux_playlist_t *this, char *data, int length) {
  char *src = data;
  char *end;
  char *title = NULL;

  while (src && *src) {
    end = strchr (src, '\n');
    if (end)
      *end = '\0';

    src = trim (src);
    if (*src == '#') {
      if (!strncmp (src+1, "EXTINF:", 7)) {
        title = strchr (src+8, ',');
        if (title)
          title++;
      }
    }
    else if (*src) {
      lprintf ("mrl:'%s'\n", src);
      _x_demux_send_mrl_reference (this->stream, 0, src, title, 0, 0);
    }

    src = end;
    if (src)
      src++;
  }
}

static void parse_ram (demux_playlist_t *this, char *data, int length) {
  char *src = data;
  char *end;

  while (src && *src) {
    end = strchr (src, '\n');
    if (end)
      *end = '\0';

    src = trim (src);
    if (!strcmp (src, "--stop--"))
      break;

    if (*src && *src != '#') {
      char *title = NULL;

      if (!strncmp (src, "rtsp://", 7) || !strncmp (src, "pnm://", 7)) {
        char *tmp = strrchr (src, '?');
        if (tmp) {
          *tmp = '\0';
          title = strstr (tmp+1, "title=");
          if (title) {
            title += 6;
            tmp = strchr (title, '&');
            if (tmp)
              *tmp = '\0';
          }
        }
      }

      lprintf ("mrl:'%s'\n", src);
      _x_demux_send_mrl_reference (this->stream, 0, src, title, 0, 0);
    }

    src = end;
    if (src)
      src++;
  }
}

static void parse_pls (demux_playlist_t *this, char *data, int length) {
  char *src = data;
  char *end;

  while (src && *src) {
    end = strchr (src, '\n');
    if (end)
      *end = '\0';

    src = trim (src);
    if (!strncmp (src, "File", 4)) {
      src = strchr (src+4, '=');
      if (src && *(src+1)) {
        lprintf ("mrl:'%s'\n", src+1);
        _x_demux_send_mrl_reference (this->stream, 0, src+1, NULL, 0, 0);
      }
    }

    src = end;
    if (src)
      src++;
  }
}

static void parse_asx (demux_playlist_t *this, char *data, int length) {
  xml_node_t *root, *node, *tmp;
  int         is_asx = 0;

  xml_parser_init (data, length, XML_PARSER_CASE_INSENSITIVE);

  if (xml_parser_build_tree (&root) >= 0) {
    if (!strcasecmp (root->name, "asx")) {
      is_asx = 1;

      for (node = root->child; node; node = node->next) {
        if (!strcasecmp (node->name, "entry")) {
          const char *title    = NULL;
          const char *src      = NULL;
          const char *start    = NULL;
          const char *duration = NULL;

          for (tmp = node->child; tmp; tmp = tmp->next) {
            if (!strcasecmp (tmp->name, "title")) {
              title = tmp->data;
            }
            else if (!strcasecmp (tmp->name, "ref")) {
              src = xml_parser_get_property (tmp, "href");
            }
            else if (!strcasecmp (tmp->name, "starttime")) {
              start = xml_parser_get_property (tmp, "value");
            }
            else if (!strcasecmp (tmp->name, "duration")) {
              duration = xml_parser_get_property (tmp, "value");
            }
          }

          if (src) {
            lprintf ("mrl:'%s'\n", src);
            _x_demux_send_mrl_reference (this->stream, 0, src, title,
                                         parse_time(start), parse_time(duration));
          }
        }
      }
    }

    xml_parser_free_tree (root);
  }

  if (!is_asx) {
    /* No tags found? Might be a references list. */
    parse_ref (this, data, length);
  }
}

static void parse_smi (demux_playlist_t *this, char *data, int length) {
  xml_node_t *root, *node, *tmp;
  int         is_smi = 0;

  xml_parser_init (data, length, XML_PARSER_CASE_SENSITIVE);

  if (xml_parser_build_tree (&root) >= 0) {
    for (node = root; node; node = node->next) {
      if (!strcmp (node->name, "smil"))
        break;
    }

    if (node) {
      is_smi = 1;

      for (node = node->child; node; node = node->next) {
        if (!strcmp (node->name, "body")) {
          for (tmp = node->child; tmp; tmp = tmp->next) {
            if (!strcmp (tmp->name, "audio") || !strcmp (tmp->name, "video")) {
              const char *src, *title;
              int         start, end;

              src   = xml_parser_get_property (tmp, "src");
              title = xml_parser_get_property (tmp, "title");
              start = parse_time (xml_parser_get_property (tmp, "clipBegin") ? :
                                  xml_parser_get_property (tmp, "clip-begin"));
              end   = parse_time (xml_parser_get_property (tmp, "clipEnd") ? :
                                  xml_parser_get_property (tmp, "clip-end"));

              if (src) {
                lprintf ("mrl:'%s'\n", src);
                _x_demux_send_mrl_reference (this->stream, 0, src, title,
                                             start, end ? (end-start) : 0);
              }
            }
          }
        }
      }
    }

    xml_parser_free_tree (root);
  }

  if (!is_smi) {
    /* No tags found? Might be a RAM playlist. */
    parse_ram (this, data, length);
  }
}

static void parse_qtl (demux_playlist_t *this, char *data, int length) {
  xml_node_t *root, *node;

  xml_parser_init (data, length, XML_PARSER_CASE_SENSITIVE);

  if (xml_parser_build_tree (&root) >= 0) {
    for (node = root; node; node = node->next) {
      if (!strcmp (node->name, "embed")) {
        const char *src;

        src = xml_parser_get_property (node, "src");
        if (src) {
          lprintf ("mrl:'%s'\n", src);
          _x_demux_send_mrl_reference (this->stream, 0, src, NULL, 0, 0);
        }
      }
    }

    xml_parser_free_tree (root);
  }
}

static void parse_xspf (demux_playlist_t *this, char *data, int length) {
  xml_node_t *root, *node, *tmp;

  xml_parser_init (data, length, XML_PARSER_CASE_SENSITIVE);

  if (xml_parser_build_tree (&root) >= 0) {
    for (node = root; node; node = node->next) {
      if (!strcmp (node->name, "playlist"))
        break;
    }
    if (node) {
      for (node = node->child; node; node = node->next) {
        if (!strcmp (node->name, "trackList"))
          break;
      }
    }
    if (node) {
      for (node = node->child; node; node = node->next) {
        if (!strcmp (node->name, "track")) {
          char *src   = NULL;
          char *title = NULL;

          for (tmp = node->child; tmp; tmp = tmp->next) {
            if (!strcmp (tmp->name, "location")) {
              src = trim((char*)tmp->data);
            }
            else if (!strcmp (tmp->name, "title")) {
              title = trim((char*)tmp->data);
            }
          }

          if (src) {
            lprintf ("mrl:'%s'\n", src);
            _x_demux_send_mrl_reference (this->stream, 0, src, title, 0, 0);
          }
        }
      }
    }

    xml_parser_free_tree (root);
  }
}

static void parse_rss (demux_playlist_t *this, char *data, int length) {
  xml_node_t *root, *node, *item, *tmp;

  xml_parser_init (data, length, XML_PARSER_CASE_SENSITIVE);

  if (xml_parser_build_tree (&root) >= 0) {
    for (node = root; node; node = node->next) {
      if (!strcmp (node->name, "rss"))
        break;
    }

    if (node) {
      for (node = node->child; node; node = node->next) {
        if (strcmp (node->name, "channel"))
          continue;

        for (item = node->child; item; item = item->next) {
          if (!strcmp (item->name, "item")) {
            const char *title = NULL;
            const char *src   = NULL;

            for (tmp = item->child; tmp; tmp = tmp->next) {
              if (!strcmp (tmp->name, "title")) {
                title = tmp->data;
              }
              else if (!strcmp (tmp->name, "enclosure")) {
                src = xml_parser_get_property (tmp, "url");
              }
            }

            if (src) {
              lprintf ("mrl:'%s'\n", src);
              _x_demux_send_mrl_reference (this->stream, 0, src, title, 0, 0);
            }
          }
        }
      }
    }

    xml_parser_free_tree (root);
  }
}


static void demux_playlist_send_headers (demux_plugin_t *this_gen) {
  demux_playlist_t *this = (demux_playlist_t *) this_gen;

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);

  _x_demux_control_start (this->stream);

  this->input->seek (this->input, 0, SEEK_SET);
}


static int demux_playlist_send_chunk (demux_plugin_t *this_gen) {
  demux_playlist_t *this = (demux_playlist_t *) this_gen;
  char             *data = NULL;
  int               length;

  length = this->input->get_length (this->input);
  if (length > 0) {
    data = xine_xmalloc (length+1);
    if (data)
      this->input->read (this->input, data, length);
  }
  else {
    char buf[1024];
    int  len;

    length = 0;
    while ((len = this->input->read (this->input, buf, sizeof(buf))) > 0) {
      data = realloc (data, length+len+1);
      if (!data)
        break;

      memcpy (data+length, buf, len);
      length += len;
      data[length] = '\0';
    }
  }

  lprintf ("data:%p length:%d\n", data, length);

  if (data) {
    switch (this->playlist) {
      case XINE_PLT_REF:
        parse_ref (this, data, length);
        break;
      case XINE_PLT_M3U:
        parse_m3u (this, data, length);
        break;
      case XINE_PLT_RAM:
        parse_ram (this, data, length);
        break;
      case XINE_PLT_PLS:
        parse_pls (this, data, length);
        break;
      case XINE_PLT_ASX:
        parse_asx (this, data, length);
        break;
      case XINE_PLT_SMI:
        parse_smi (this, data, length);
        break;
      case XINE_PLT_QTL:
        parse_qtl (this, data, length);
        break;
      case XINE_PLT_XSPF:
        parse_xspf (this, data, length);
        break;
      case XINE_PLT_RSS:
        parse_rss (this, data, length);
        break;
      default:
        lprintf ("unexpected playlist type 0x%08x\n", this->playlist);
        break;
    }

    free (data);
  }

  this->status = DEMUX_FINISHED;

  return DEMUX_FINISHED;
}

static int demux_playlist_seek (demux_plugin_t *this_gen,
                                off_t start_pos, int start_time, int playing) {
  return DEMUX_OK;
}

static int demux_playlist_get_status (demux_plugin_t *this_gen) {
  demux_playlist_t *this = (demux_playlist_t *) this_gen;

  return this->status;
}

static int demux_playlist_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_playlist_get_capabilities (demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_playlist_get_optional_data (demux_plugin_t *this_gen,
                                             void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}


static demux_plugin_t *open_plugin (demux_class_t *class_gen,
                                    xine_stream_t *stream, input_plugin_t *input) {
  demux_playlist_t *this;

  this         = xine_xmalloc (sizeof (demux_playlist_t));
  this->xine   = stream->xine;
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_playlist_send_headers;
  this->demux_plugin.send_chunk        = demux_playlist_send_chunk;
  this->demux_plugin.seek              = demux_playlist_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_playlist_get_status;
  this->demux_plugin.get_stream_length = demux_playlist_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_playlist_get_capabilities;
  this->demux_plugin.get_optional_data = demux_playlist_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  switch (stream->content_detection_method) {
    case METHOD_BY_MRL:
      lprintf ("detect by extension\n");
      this->playlist = detect_by_extension (input);
      if (!this->playlist) {
        free (this);
        return NULL;
      }
      break;

    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      lprintf ("detect by content\n");
      this->playlist = detect_by_content (input);
      if (!this->playlist) {
        free (this);
        return NULL;
      }
      break;

    default:
      free (this);
      return NULL;
  }

  lprintf ("playlist:0x%08x (%s)\n", this->playlist, (char*)&this->playlist);

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_playlist_class_t     *this;

  this = xine_xmalloc (sizeof(demux_playlist_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("Playlist demux plugin");
  this->demux_class.identifier      = "playlist";
  this->demux_class.mimetypes       =
    "audio/mpegurl: m3u: M3U playlist;"
    "audio/x-mpegurl: m3u: M3U playlist;"
    //"audio/x-pn-realaudio: ram: RAM playlist;"
    //"audio/vnd.rn-realaudio: ram: RAM playlist;"
    "audio/x-scpls: pls: Winamp playlist;"
    "audio/x-ms-wax: wax, asx: WAX playlist;"
    "audio/x-ms-wvx: wvx, asx: WVX playlist;"
    "application/smil: smi, smil: SMIL playlist;"
    "application/x-quicktimeplayer: qtl: Quicktime playlist;"
    "application/xspf+xml: xspf: XSPF playlist;";
  this->demux_class.extensions      = "m3u ram pls asx wax wvx smi smil qtl xspf rss";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_flv = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "playlist", XINE_VERSION_CODE, &demux_info_flv, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
