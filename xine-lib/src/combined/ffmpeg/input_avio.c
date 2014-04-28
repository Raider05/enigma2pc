/*
 * Copyright (C) 2013 the xine project
 * Copyright (C) 2013 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <libavformat/avio.h>

#define LOG_MODULE "libavio"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#include "ffmpeg_decoder.h"

/*
 * avio input plugin
 */

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;

  char            *mrl;         /* 'public' mrl without authentication credentials */
  char            *mrl_private; /* 'private' mrl with authentication credentials */
  AVIOContext     *pb;

  /* preview support */
  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;
  off_t            curpos;

} avio_input_plugin_t;

static off_t input_avio_read (input_plugin_t *this_gen, void *buf_gen, off_t len) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;
  off_t total = 0;

  if (len < 0)
    return -1;

  if (this->curpos < this->preview_size) {
    off_t n = this->preview_size - this->curpos;
    if (n > (len - total))
      n = len - total;

    memcpy (&buf[total], &this->preview[this->curpos], n);
    this->curpos += n;
    total += n;
    len -= n;
  }

  if (len > 0 && this->pb) {
    off_t n = avio_read(this->pb, buf + total, len);
    if (n < 0) {
      return n;
    }
    this->curpos += n;
    total += n;
  }

  return total;
}

static buf_element_t *input_avio_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  return NULL;
}

static off_t input_avio_get_length (input_plugin_t *this_gen) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  if (this->pb) {
    return avio_size(this->pb);
  }

  return -1;
}

static uint32_t input_avio_get_capabilities (input_plugin_t *this_gen) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  if (this->pb && this->pb->seekable) {
    return INPUT_CAP_SEEKABLE | INPUT_CAP_PREVIEW;
  }

  return INPUT_CAP_PREVIEW;
}

static off_t input_avio_seek_time (input_plugin_t *this_gen, int time_offset, int origin) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  if (origin == SEEK_SET && this->pb && this->pb->seekable) {
    int64_t ts     = (int64_t)time_offset * AV_TIME_BASE / 1000;
    off_t   result = avio_seek_time(this->pb, -1, ts, 0);
    if (result >= 0) {
      this->preview_size = 0;
      this->curpos = result;
      return this->curpos;
    }
  }

  return -1;
}

static uint32_t input_avio_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t input_avio_get_current_pos (input_plugin_t *this_gen) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  if (this->pb && this->curpos >= this->preview_size) {
    this->curpos = avio_tell(this->pb);
  }

  return this->curpos;
}

static off_t input_avio_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;
  off_t size;
  off_t newpos;

  if (!this->pb || !this->pb->seekable) {
    return -1;
  }

  /* convert relative seeks to absolute */
  switch (origin) {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      offset += this->curpos;
      break;
    case SEEK_END:
      size = avio_size(this->pb);
      if (size < 1) {
        return -1;
      }
      offset = size + offset;
      if (offset < 0)
        offset = 0;
      if (offset > size)
        offset = size;
      break;
  }

  /* seek, take care of preview buffer */

  newpos = offset;
  if (offset < this->preview_size) {
    offset = this->preview_size;
  }

  if (offset != avio_seek(this->pb, offset, SEEK_SET)) {
    return -1;
  }

  this->curpos = newpos;
  return this->curpos;
}


static const char* input_avio_get_mrl (input_plugin_t *this_gen) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  return this->mrl;
}

static int input_avio_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
      memcpy (data, this->preview, this->preview_size);
      return this->preview_size;

    case INPUT_OPTIONAL_DATA_pb:
      *((AVIOContext **)data) = this->pb;
      this->pb = NULL;
      return INPUT_OPTIONAL_SUCCESS;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int input_avio_open (input_plugin_t *this_gen) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;
  int toread = MAX_PREVIEW_SIZE;
  int trycount = 0;

  if (!this->pb) {

    /* try to open libavio protocol */
    if (avio_open2(&this->pb, this->mrl_private, AVIO_FLAG_READ, NULL, NULL) < 0) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": failed to open avio protocol for '%s'\n", this->mrl);
      _x_freep (&this->mrl_private);

      return 0;
    }

    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE": opened avio protocol for '%s'\n", this->mrl);
  }
  _x_freep (&this->mrl_private);

  while ((toread > 0) && (trycount < 10)) {
    off_t n = avio_read (this->pb, this->preview + this->preview_size, toread);
    if (n > 0) {
      this->preview_size += n;
    }
    trycount++;
    toread = MAX_PREVIEW_SIZE - this->preview_size;
  }

  return 1;
}

static void input_avio_dispose (input_plugin_t *this_gen ) {
  avio_input_plugin_t *this = (avio_input_plugin_t *) this_gen;

  avio_close(this->pb);
  _x_freep (&this->mrl);
  _x_freep (&this->mrl_private);
  free (this_gen);
}

/*
 * avio input class
 */

static int is_avio_supported_protocol(xine_t *xine, const char *mrl)
{
  char *mrl_protocol = strdup(mrl);
  char *pt = strchr(mrl_protocol, ':');
  int   result = 0;

  if (pt) {
    const char *protocol;
    void       *iter;

    *pt = 0;

    for (iter = NULL; NULL != (protocol = avio_enum_protocols(&iter, 0)); ) {
      if (!strcmp(mrl_protocol, protocol)) {
        xprintf (xine, XINE_VERBOSITY_LOG, LOG_MODULE": using avio protocol '%s' for '%s'\n", protocol, mrl);
        result = 1;
      }
    }
  }

  if (!result) {
    xprintf (xine, XINE_VERBOSITY_LOG, LOG_MODULE": no avio protocol for '%s'\n", mrl);
  }

  free(mrl_protocol);
  return result;
}

static input_plugin_t *input_avio_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {
  avio_input_plugin_t *this;
  const int            proto_len = strlen(INPUT_AVIO_ID"+");

  if (!mrl || !*mrl) {
    return NULL;
  }

  /* accept only mrls with protocol part */
  if (!strchr(mrl, ':') || (strchr(mrl, '/') < strchr(mrl, ':'))) {
    return NULL;
  }

  /* always accept own protocol */
  /* avio+http:// ... --> use avio instead of xine native http plugin */
  if (!strncasecmp (mrl, INPUT_AVIO_ID"+", proto_len)) {
    mrl += proto_len;
  }

  if (!is_avio_supported_protocol(stream->xine, mrl)) {
    return NULL;
  }

  this = calloc(1, sizeof(avio_input_plugin_t));
  this->stream = stream;
  this->mrl    = _x_mrl_remove_auth(mrl);
  this->mrl_private = strdup(mrl);

  this->input_plugin.open              = input_avio_open;
  this->input_plugin.get_capabilities  = input_avio_get_capabilities;
  this->input_plugin.read              = input_avio_read;
  this->input_plugin.read_block        = input_avio_read_block;
  this->input_plugin.seek              = input_avio_seek;
  this->input_plugin.seek_time         = input_avio_seek_time;
  this->input_plugin.get_current_pos   = input_avio_get_current_pos;
  this->input_plugin.get_length        = input_avio_get_length;
  this->input_plugin.get_blocksize     = input_avio_get_blocksize;
  this->input_plugin.get_mrl           = input_avio_get_mrl;
  this->input_plugin.get_optional_data = input_avio_get_optional_data;
  this->input_plugin.dispose           = input_avio_dispose;
  this->input_plugin.input_class       = cls_gen;

  /* do not expose authentication credentials in title (if title is not set, it defaults to mrl in xine-ui) */
  _x_meta_info_set(stream, XINE_META_INFO_TITLE, this->mrl);

  return &this->input_plugin;
}

void *init_avio_input_plugin (xine_t *xine, void *data) {
  input_class_t  *this;
  const char     *protocol;
  void           *iter;

  for (iter = NULL; NULL != (protocol = avio_enum_protocols(&iter, 0)); ) {
    xprintf (xine, XINE_VERBOSITY_DEBUG, LOG_MODULE": found avio protocol '%s'\n", protocol);
  }

  this = calloc(1, sizeof(input_class_t));

  pthread_once( &once_control, init_once_routine );

  this->get_instance      = input_avio_get_instance;
  this->description       = N_("libavio input plugin");
  this->identifier        = INPUT_AVIO_ID;
  this->get_dir           = NULL;
  this->get_autoplay_list = NULL;
  this->dispose           = default_input_class_dispose;
  this->eject_media       = NULL;

  return this;
}

const input_info_t input_info_avio = {
  -1   /* priority */
};
