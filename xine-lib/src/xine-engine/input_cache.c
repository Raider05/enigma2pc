/*
 * Copyright (C) 2000-2004 the xine project
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
 * Buffered Input Plugin (request optimizer).
 *
 * The goal of this input plugin is to reduce
 * the number of calls to the real input plugin.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LOG_MODULE "input_cache"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include "xine_private.h"
#include <assert.h>

#define DEFAULT_BUFFER_SIZE 1024

typedef struct {
  input_plugin_t    input_plugin;      /* inherited structure */

  input_plugin_t   *main_input_plugin; /* original input plugin */
  xine_stream_t    *stream;

  char             *buf;
  size_t            buf_size;          /* allocated size */
  int               buf_len;           /* data size */
  int               buf_pos;

  /* Statistics */
  int               read_call;
  int               main_read_call;
  int               seek_call;
  int               main_seek_call;

} cache_input_plugin_t;


/*
 * read data from input plugin and write it into file
 */
static off_t cache_plugin_read(input_plugin_t *this_gen, void *buf_gen, off_t len) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  char *buf = (char *)buf_gen;
  off_t read_len = 0;
  off_t main_read;

  lprintf("cache_plugin_read: len=%"PRId64"\n", len);
  this->read_call++;

  /* optimized for common cases */
  if (len <= (this->buf_len - this->buf_pos)) {
    /* all bytes are in the buffer */
    switch (len) {
#if defined(__i386__) || defined(__x86_64__)
      /* These are restricted to x86 and amd64. Some other architectures don't
       * handle unaligned accesses in the same way, quite possibly requiring
       * extra code over and above simple byte copies.
       */
      case 8:
        *((uint64_t *)buf) = *(uint64_t *)(&(this->buf[this->buf_pos]));
        break;
      case 7:
        buf[6] = (char)this->buf[this->buf_pos + 6];
        /* fallthru */
      case 6:
        *((uint32_t *)buf) = *(uint32_t *)(&(this->buf[this->buf_pos]));
        *((uint16_t *)&buf[4]) = *(uint16_t *)(&(this->buf[this->buf_pos + 4]));
        break;
      case 5:
        buf[4] = (char)this->buf[this->buf_pos + 4];
        /* fallthru */
      case 4:
        *((uint32_t *)buf) = *(uint32_t *)(&(this->buf[this->buf_pos]));
        break;
      case 3:
        buf[2] = (char)this->buf[this->buf_pos + 2];
        /* fallthru */
      case 2:
        *((uint16_t *)buf) = *(uint16_t *)(&(this->buf[this->buf_pos]));
        break;
#endif
      case 1:
        *buf = (char)this->buf[this->buf_pos];
        break;
      default:
        xine_fast_memcpy(buf, this->buf + this->buf_pos, len);
    }
    this->buf_pos += len;
    read_len += len;

  } else {
    int in_buf_len;

    /* copy internal buffer bytes */
    in_buf_len = this->buf_len - this->buf_pos;
    if (in_buf_len > 0) {
      xine_fast_memcpy(buf, this->buf + this->buf_pos, in_buf_len);
      len -= in_buf_len;
      read_len += in_buf_len;
    }
    this->buf_len = 0;
    this->buf_pos = 0;

    /* read the rest */
    if (len < this->buf_size) {
      /* readahead bytes */
      main_read = this->main_input_plugin->read(this->main_input_plugin, this->buf, this->buf_size);
      this->main_read_call++;

      if( main_read >= 0 ) {
        this->buf_len = main_read;

        if (len > this->buf_len)
          len = this->buf_len;

        if (len) {
          xine_fast_memcpy(buf + read_len, this->buf, len);
          this->buf_pos = len;
          read_len += len;
        }
      } else {
        /* read error: report return value to caller */
        read_len = main_read;
      }
    } else {
      /* direct read */
      main_read = this->main_input_plugin->read(this->main_input_plugin, buf + read_len, len);
      this->main_read_call++;

      if( main_read >= 0 )
        read_len += main_read;
      else
        /* read error: report return value to caller */
        read_len = main_read;
    }
  }

  return read_len;
}

/*
 * open should never be called
 */
static int cache_plugin_open(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  xine_log(this->stream->xine, XINE_LOG_MSG,
	   _(LOG_MODULE": open() function should never be called\n"));
  return 0;
}

static uint32_t cache_plugin_get_capabilities(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_capabilities(this->main_input_plugin);
}

static buf_element_t *cache_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  buf_element_t *buf;
  int in_buf_len;

  in_buf_len = this->buf_len - this->buf_pos;
  if (in_buf_len > 0) {
    off_t read_len;

    /* hmmm, the demuxer mixes read and read_block */
    buf = fifo->buffer_pool_alloc (fifo);
    if (buf) {
      buf->type = BUF_DEMUX_BLOCK;

      assert(todo <= buf->max_size);
      read_len = cache_plugin_read (this_gen, buf->content, todo);
      buf->size = read_len;
    }
  } else {
    buf = this->main_input_plugin->read_block(this->main_input_plugin, fifo, todo);
    this->read_call++;
    this->main_read_call++;
  }
  return buf;
}

static off_t cache_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  off_t cur_pos;
  off_t rel_offset;
  off_t new_buf_pos;

  lprintf("offset: %"PRId64", origin: %d\n", offset, origin);
  this->seek_call++;

  if( !this->buf_len ) {
    cur_pos = this->main_input_plugin->seek(this->main_input_plugin, offset, origin);
    this->main_seek_call++;
  } else {
    cur_pos = this->main_input_plugin->get_current_pos(this->main_input_plugin);
    if( cur_pos >= (this->buf_len - this->buf_pos) )
      cur_pos -= (this->buf_len - this->buf_pos);
    else
      cur_pos = 0;

    switch (origin) {
    case SEEK_CUR:
      rel_offset = offset;
      break;

    case SEEK_SET:
      rel_offset = offset - cur_pos;
      break;

    default:
      /* invalid origin - main input should know better */
      cur_pos = this->main_input_plugin->seek(this->main_input_plugin, offset, origin);
      this->buf_len = this->buf_pos = 0;
      this->main_seek_call++;
      return cur_pos;
    }

    new_buf_pos = (off_t)this->buf_pos + rel_offset;
    lprintf("buf_len: %d, rel_offset=%"PRId64", new_buf_pos=%"PRId64"\n",
	  this->buf_len, rel_offset, new_buf_pos);

    if ((new_buf_pos < 0) || (new_buf_pos >= this->buf_len)) {
      if( origin == SEEK_SET )
        cur_pos = this->main_input_plugin->seek(this->main_input_plugin, offset, origin);
      else
        cur_pos = this->main_input_plugin->seek(this->main_input_plugin,
                  offset - (this->buf_len - this->buf_pos), origin);
      this->buf_len = this->buf_pos = 0;
      this->main_seek_call++;
    } else {
      this->buf_pos = (int)new_buf_pos;
      cur_pos += rel_offset;
    }
  }
  return cur_pos;
}

static off_t cache_plugin_seek_time(input_plugin_t *this_gen, int time_offset, int origin) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  off_t cur_pos;

  lprintf("time_offset: %d, origin: %d\n", time_offset, origin);
  this->seek_call++;

  cur_pos = this->main_input_plugin->seek_time(this->main_input_plugin, time_offset, origin);
  this->buf_len = this->buf_pos = 0;
  this->main_seek_call++;
  return cur_pos;
}

static off_t cache_plugin_get_current_pos(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  off_t cur_pos;

  cur_pos = this->main_input_plugin->get_current_pos(this->main_input_plugin);
  if( this->buf_len ) {
    if( cur_pos >= (this->buf_len - this->buf_pos) )
      cur_pos -= (this->buf_len - this->buf_pos);
    else
      cur_pos = 0;
  }

  return cur_pos;
}

static int cache_plugin_get_current_time(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;
  int cur_time;

  cur_time = this->main_input_plugin->get_current_time(this->main_input_plugin);

  return cur_time;
}

static off_t cache_plugin_get_length (input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_length(this->main_input_plugin);
}

static uint32_t cache_plugin_get_blocksize(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_blocksize(this->main_input_plugin);
}

static const char* cache_plugin_get_mrl (input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_mrl(this->main_input_plugin);
}

static int cache_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_optional_data(
    this->main_input_plugin, data, data_type);
}

/*
 * dispose main input plugin and self
 */
static void cache_plugin_dispose(input_plugin_t *this_gen) {
  cache_input_plugin_t *this = (cache_input_plugin_t *)this_gen;

  lprintf("cache_plugin_dispose\n");

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE": read calls: %d, main input read calls: %d\n", this->read_call, this->main_read_call);
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE": seek_calls: %d, main input seek calls: %d\n", this->seek_call, this->main_seek_call);

  _x_free_input_plugin(this->stream, this->main_input_plugin);
  free(this->buf);
  free(this);
}


/*
 * create self instance,
 */
input_plugin_t *_x_cache_plugin_get_instance (xine_stream_t *stream) {
  cache_input_plugin_t *this;
  input_plugin_t *main_plugin = stream->input_plugin;

  /* check given input plugin */
  if (!stream->input_plugin) {
    xine_log(stream->xine, XINE_LOG_MSG, _(LOG_MODULE": input plugin not defined!\n"));
    return NULL;
  }

  lprintf("mrl: %s\n", main_plugin->get_mrl(main_plugin));

  this = calloc(1, sizeof(cache_input_plugin_t));
  if (!this)
    return NULL;

  this->main_input_plugin = main_plugin;
  this->stream            = stream;

  this->input_plugin.open                = cache_plugin_open;
  this->input_plugin.get_capabilities    = cache_plugin_get_capabilities;
  this->input_plugin.read                = cache_plugin_read;
  this->input_plugin.read_block          = cache_plugin_read_block;
  this->input_plugin.seek                = cache_plugin_seek;
  if(this->main_input_plugin->seek_time)
    this->input_plugin.seek_time         = cache_plugin_seek_time;
  this->input_plugin.get_current_pos     = cache_plugin_get_current_pos;
  if(this->main_input_plugin->get_current_time)
    this->input_plugin.get_current_time  = cache_plugin_get_current_time;
  this->input_plugin.get_length          = cache_plugin_get_length;
  this->input_plugin.get_blocksize       = cache_plugin_get_blocksize;
  this->input_plugin.get_mrl             = cache_plugin_get_mrl;
  this->input_plugin.get_optional_data   = cache_plugin_get_optional_data;
  this->input_plugin.dispose             = cache_plugin_dispose;
  this->input_plugin.input_class         = main_plugin->input_class;

  /* use main input block size */
  this->buf_size = this->main_input_plugin->get_blocksize(this->main_input_plugin);
  if (this->buf_size < DEFAULT_BUFFER_SIZE) {
    this->buf_size = DEFAULT_BUFFER_SIZE;
  }

  this->buf = calloc(1, this->buf_size);
  if (!this->buf) {
    free (this);
    return NULL;
  }

  return &this->input_plugin;
}

