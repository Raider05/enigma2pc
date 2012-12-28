/*
 * Copyright (C) 2002-2003 the xine project
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
 * rtsp input plugin
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_MODULE "input_rtsp"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "bswap.h"
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#include "librtsp/rtsp_session.h"
#include "net_buf_ctrl.h"

#define BUFSIZE 1025

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} rtsp_input_class_t;

typedef struct {
  input_plugin_t   input_plugin;

  rtsp_session_t  *rtsp;

  xine_stream_t   *stream;

  char            *mrl;
  char            *public_mrl;

  off_t            curpos;

  nbc_t           *nbc;

  char             scratch[BUFSIZE];

} rtsp_input_plugin_t;


static off_t rtsp_plugin_read (input_plugin_t *this_gen,
                              void *buf, off_t len) {
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;
  off_t               n;

  lprintf ("rtsp_plugin_read: %"PRId64" bytes ...\n", len);

  n = rtsp_session_read (this->rtsp, buf, len);
  if (n > 0)
    this->curpos += n;

  return n;
}

static buf_element_t *rtsp_plugin_read_block (input_plugin_t *this_gen,
                                             fifo_buffer_t *fifo, off_t todo) {
  /*rtsp_input_plugin_t   *this = (rtsp_input_plugin_t *) this_gen; */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int                   total_bytes;

  lprintf ("rtsp_plugin_read_block: %"PRId64" bytes...\n", todo);

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = rtsp_plugin_read (this_gen, (char*)buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t rtsp_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {

  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  lprintf ("seek %"PRId64" bytes, origin %d\n", offset, origin);

  /* only realtive forward-seeking is implemented */

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      off_t n = rtsp_plugin_read (this_gen, this->scratch, BUFSIZE);
      if (n <= 0)
	return this->curpos;
      this->curpos += n;
    }

    off_t n = rtsp_plugin_read (this_gen, this->scratch, offset);
    if (n <= 0)
      return this->curpos;
    this->curpos += n;
  }

  return this->curpos;
}

static off_t rtsp_plugin_seek_time (input_plugin_t *this_gen, int time_offset, int origin) {

  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  lprintf ("seek_time %d msec, origin %d\n", time_offset, origin);

  if (origin == SEEK_SET)
    rtsp_session_set_start_time (this->rtsp, time_offset);

  return this->curpos;
}

static off_t rtsp_plugin_get_length (input_plugin_t *this_gen) {

  /*
  rtsp_input_plugin_t   *this = (rtsp_input_plugin_t *) this_gen;
  off_t                 length;
  */

  return -1;
}

static uint32_t rtsp_plugin_get_capabilities (input_plugin_t *this_gen) {
  return INPUT_CAP_PREVIEW | INPUT_CAP_RIP_FORBIDDEN;
}

static uint32_t rtsp_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t rtsp_plugin_get_current_pos (input_plugin_t *this_gen){
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  /*
  printf ("current pos is %"PRId64"\n", this->curpos);
  */

  return this->curpos;
}

static void rtsp_plugin_dispose (input_plugin_t *this_gen) {
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  if (this->rtsp) {
    rtsp_session_end (this->rtsp);
    this->rtsp = NULL;
  }

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if(this->mrl)
    free(this->mrl);

  if(this->public_mrl)
    free(this->public_mrl);

  free (this);
}

static const char* rtsp_plugin_get_mrl (input_plugin_t *this_gen) {
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  return this->public_mrl;
}

static int rtsp_plugin_get_optional_data (input_plugin_t *this_gen,
                                         void *data, int data_type) {
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    return rtsp_session_peek_header(this->rtsp, data, MAX_PREVIEW_SIZE);

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int rtsp_plugin_open (input_plugin_t *this_gen) {
  rtsp_input_plugin_t *this = (rtsp_input_plugin_t *) this_gen;

  rtsp_session_t      *rtsp;

  lprintf ("trying to open '%s'\n", this->mrl);

  rtsp = rtsp_session_start(this->stream, this->mrl);

  if (!rtsp) {
    lprintf ("returning null.\n");

    return 0;
  }

  this->rtsp = rtsp;

  return 1;
}

static input_plugin_t *rtsp_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *mrl) {

  /* rtsp_input_class_t  *cls = (rtsp_input_class_t *) cls_gen; */
  rtsp_input_plugin_t *this;

  if (strncasecmp (mrl, "rtsp://", 6))
    return NULL;

  this = calloc(1, sizeof (rtsp_input_plugin_t));

  this->stream  = stream;
  this->rtsp    = NULL;
  this->mrl     = strdup (mrl);
  /* since we handle only real streams yet, we can savely add
   * an .rm extention to force handling by demux_real.
   */
  this->public_mrl = _x_asprintf("%s.rm", this->mrl);

  this->nbc     = nbc_init (stream);

  this->input_plugin.open              = rtsp_plugin_open;
  this->input_plugin.get_capabilities  = rtsp_plugin_get_capabilities;
  this->input_plugin.read              = rtsp_plugin_read;
  this->input_plugin.read_block        = rtsp_plugin_read_block;
  this->input_plugin.seek              = rtsp_plugin_seek;
  this->input_plugin.seek_time         = rtsp_plugin_seek_time;
  this->input_plugin.get_current_pos   = rtsp_plugin_get_current_pos;
  this->input_plugin.get_length        = rtsp_plugin_get_length;
  this->input_plugin.get_blocksize     = rtsp_plugin_get_blocksize;
  this->input_plugin.get_mrl           = rtsp_plugin_get_mrl;
  this->input_plugin.dispose           = rtsp_plugin_dispose;
  this->input_plugin.get_optional_data = rtsp_plugin_get_optional_data;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

/*
 * rtsp input plugin class stuff
 */
static void *init_class (xine_t *xine, void *data) {

  rtsp_input_class_t  *this;

  this = calloc(1, sizeof (rtsp_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = rtsp_class_get_instance;
  this->input_class.identifier         = "rtsp";
  this->input_class.description        = N_("rtsp streaming input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 18, "rtsp", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

