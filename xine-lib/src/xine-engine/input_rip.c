/*
 * Copyright (C) 2000-2006 the xine project
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
 * Rip Input Plugin for catching streams
 *
 * It saves raw data into file as go from input plugins.
 *
 * Usage:
 *
 * - activation:
 *     xine stream_mrl#save:file.raw
 *
 * - it's possible speeder saving streams in the xine without playing:
 *     xine stream_mrl#save:file.raw\;noaudio\;novideo
 */

/* TODO:
 *   - resume feature (via #append)
 *   - gui activation (after restarting playback)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOG_MODULE "input_rip"
#define LOG_VERBOSE
/*
#define LOG
*/

#ifdef WIN32
#  define CLR_FAIL ""
#  define CLR_RST ""
#else
#  define CLR_FAIL "\e[1;31m"
#  define CLR_RST "\e[0;39m"
#endif

#include <xine/xine_internal.h>
#include "xine_private.h"

#ifndef HAVE_FSEEKO
#  define fseeko fseek
#endif

#define SCRATCH_SIZE 1024
#define MAX_TARGET_LEN 256
#define SEEK_TIMEOUT 2.5

typedef struct {
  input_plugin_t    input_plugin;      /* inherited structure */

  input_plugin_t   *main_input_plugin; /* original input plugin */

  xine_stream_t    *stream;
  FILE             *file;              /* destination file */

  char             *preview;           /* preview data */
  off_t             preview_size;      /* size of read preview data */
  off_t             curpos;            /* current position */
  off_t             savepos;           /* amount of already saved data */

  int               regular;           /* permit reading from the file */
} rip_input_plugin_t;


static off_t min_off(off_t a, off_t b) {
  return a <= b ? a : b;
}

/*
 * read data from input plugin and write it into file
 */
static off_t rip_plugin_read(input_plugin_t *this_gen, void *buf_gen, off_t len) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  char *buf = (char *)buf_gen;
  off_t retlen, npreview, nread, nwrite, nread_orig, nread_file;

  lprintf("reading %"PRId64" bytes (curpos = %"PRId64", savepos = %"PRId64")\n", len, this->curpos, this->savepos);

  if (len < 0) return -1;

  /* compute sizes and copy data from preview */
  if (this->curpos < this->preview_size && this->preview) {
    npreview = this->preview_size - this->curpos;
    if (npreview > len) {
      npreview = len;
      nread = 0;
    } else {
      nread = min_off(this->savepos - this->preview_size, len - npreview);
    }

    lprintf(" => get %"PRId64" bytes from preview (%"PRId64" bytes)\n", npreview, this->preview_size);

    memcpy(buf, &this->preview[this->curpos], npreview);
  } else {
    npreview = 0;
    nread = min_off(this->savepos - this->curpos, len);
  }

  /* size to write into file */
  nwrite = len - npreview - nread;
  /* size to read from file */
  nread_file = this->regular ? nread : 0;
  /* size to read from original input plugin */
  nread_orig = this->regular ? 0 : nread;

  /* re-reading from file */
  if (nread_file) {
    lprintf(" => read %"PRId64" bytes from file\n", nread_file);
    if (fread(&buf[npreview], nread_file, 1, this->file) != 1) {
      xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: reading of saved data failed: %s\n"), strerror(errno));
      return -1;
    }
  }

  /* really to read/catch */
  if (nread_orig + nwrite) {
    lprintf(" => read %"PRId64" bytes from input plugin\n", nread_orig + nwrite);

    /* read from main input plugin */
    retlen = this->main_input_plugin->read(this->main_input_plugin, &buf[npreview + nread_file], nread_orig + nwrite);
    lprintf("%s => returned %"PRId64"" CLR_RST "\n", retlen == nread_orig + nwrite ? "" : CLR_FAIL, retlen);

    if (retlen < 0) {
      xine_log(this->stream->xine, XINE_LOG_MSG,
               _("input_rip: reading by input plugin failed\n"));
      return -1;
    }

    /* write to file (only successfully read data) */
    if (retlen > nread_orig) {
      nwrite = retlen - nread_orig;
      if (fwrite(buf + this->savepos - this->curpos, nwrite, 1, this->file) != 1) {
        xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"), (intmax_t)(retlen - nread_orig), strerror(errno));
        return -1;
      }
      this->savepos += nwrite;
      lprintf(" => saved %"PRId64" bytes\n", nwrite);
    } else
      nwrite = 0;
  }

  this->curpos += (npreview + nread + nwrite);

  return npreview + nread + nwrite;
}

/*
 * open should never be called
 */
static int rip_plugin_open(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  xine_log(this->stream->xine, XINE_LOG_MSG,
           _("input_rip: open() function should never be called\n"));
  return 0;
}

/*
 * set preview and/or seek capability when it's implemented by RIP
 */
static uint32_t rip_plugin_get_capabilities(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t caps;

  caps = this->main_input_plugin->get_capabilities(this->main_input_plugin);

  if (this->regular)
    caps |= INPUT_CAP_SEEKABLE;

  if (this->preview) caps |= INPUT_CAP_PREVIEW;
  return caps;
}

/*
 * read a block of data from input plugin and write it into file
 *
 * This rip plugin returns block unchanged from main input plugin. But special
 * cases are reading over preview or reading already saved data - it returns
 * own allocated block.
 */
static buf_element_t *rip_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  buf_element_t *buf = NULL;
  off_t retlen, npreview, nread, nwrite, nread_orig, nread_file;

  lprintf("reading %"PRId64" bytes (curpos = %"PRId64", savepos = %"PRId64") (block)\n", todo, this->curpos, this->savepos);

  if (todo <= 0) return NULL;

  /* compute sizes and copy data from preview */
  if (this->curpos < this->preview_size && this->preview) {
    npreview = this->preview_size - this->curpos;
    if (npreview > todo) {
      npreview = todo;
      nread = 0;
    } else {
      nread = min_off(this->savepos - this->preview_size, todo - npreview);
    }

    lprintf(" => get %"PRId64" bytes from preview (%"PRId64" bytes) (block)\n", npreview, this->preview_size);
  } else {
    npreview = 0;
    nread = min_off(this->savepos - this->curpos, todo);
  }

  /* size to write into file */
  nwrite = todo - npreview - nread;
  /* size to read from file */
  nread_file = this->regular ? nread : 0;
  /* size to read from original input plugin */
  nread_orig = this->regular ? 0 : nread;

  /* create own block by RIP if needed */
  if (npreview + nread_file) {
    buf = fifo->buffer_pool_alloc(fifo);
    buf->content = buf->mem;
    buf->type = BUF_DEMUX_BLOCK;

    /* get data from preview */
    if (npreview) {
      lprintf(" => get %"PRId64" bytes from the preview (block)\n", npreview);
      memcpy(buf->content, &this->preview[this->curpos], npreview);
    }

    /* re-reading from the file */
    if (nread_file) {
      lprintf(" => read %"PRId64" bytes from the file (block)\n", nread_file);
      if (fread(&buf->content[npreview], nread_file, 1, this->file) != 1) {
        xine_log(this->stream->xine, XINE_LOG_MSG,
                 _("input_rip: reading of saved data failed: %s\n"),
                 strerror(errno));
        return NULL;
      }
    }
  }

  /* really to read/catch */
  if (nread_orig + nwrite) {
    /* read from main input plugin */
    if (buf) {
      lprintf(" => read %"PRId64" bytes from input plugin (block)\n", nread_orig + nwrite);
      retlen = this->main_input_plugin->read(this->main_input_plugin, &buf->content[npreview + nread_file], nread_orig + nwrite);
    } else {
      lprintf(" => read block of %"PRId64" bytes from input plugin (block)\n", nread_orig + nwrite);
      buf = this->main_input_plugin->read_block(this->main_input_plugin, fifo, nread_orig + nwrite);
      if (buf) retlen = buf->size;
      else {
        lprintf(CLR_FAIL " => returned NULL" CLR_RST "\n");
        return NULL;
      }
    }
    if (retlen != nread_orig + nwrite) {
      lprintf(CLR_FAIL " => returned %"PRId64"" CLR_RST "\n", retlen);
      return NULL;
    }

    /* write to file (only successfully read data) */
    if (retlen > nread_orig) {
      nwrite = retlen - nread_orig;
      if (fwrite(buf->content + this->savepos - this->curpos, nwrite, 1, this->file) != 1) {
        xine_log(this->stream->xine, XINE_LOG_MSG,
                 _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
                 (intmax_t)(retlen - nread_orig), strerror(errno));
        return NULL;
      }
      this->savepos += nwrite;
      lprintf(" => saved %"PRId64" bytes\n", nwrite);
    } else
      nwrite = 0;
  }

  this->curpos += (npreview + nread + nwrite);
  buf->size = npreview + nread + nwrite;

  return buf;
}

static off_t rip_seek_original(rip_input_plugin_t *this, off_t reqpos) {
  off_t pos;

  lprintf(" => seeking original input plugin to %"PRId64"\n", reqpos);

  pos = this->main_input_plugin->seek(this->main_input_plugin, reqpos, SEEK_SET);
  if (pos == -1) {
    xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed\n"));
    return -1;
  }
#ifdef LOG
  if (pos != reqpos) {
    lprintf(CLR_FAIL " => reqested position %"PRId64" differs from result position %"PRId64"" CLR_RST "\n", reqpos, pos);
  }
#endif

  this->curpos = pos;

  return pos;
}

/*
 * seek in RIP
 *
 * If we are seeking back and we can read from saved file,
 * position of original input plugin isn't changed.
 */
static off_t rip_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  char buffer[SCRATCH_SIZE];
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t blocksize;
  off_t newpos, reqpos, pos;
  struct timeval time1, time2;
  double interval = 0;

  lprintf("seek, offset %"PRId64", origin %d (curpos %"PRId64", savepos %"PRId64")\n", offset, origin, this->curpos, this->savepos);

  switch (origin) {
    case SEEK_SET: newpos = offset; break;
    case SEEK_CUR: newpos = this->curpos + offset; break;
    default: newpos = this->curpos;
  }

  /* align the new position down to block sizes */
  if( this_gen->get_capabilities(this_gen) & INPUT_CAP_BLOCK ) {
    blocksize = this_gen->get_blocksize(this_gen);
    newpos = (newpos / blocksize) * blocksize;
  } else
    blocksize = 0;

  if (newpos < this->savepos) {
    lprintf(" => virtual seeking from %"PRId64" to %"PRId64"\n", this->curpos, newpos);

    /* don't seek into preview area */
    if (this->preview && newpos < this->preview_size) {
      reqpos = this->preview_size;
    } else  {
      reqpos = newpos;
    }

    if (this->regular) {
      if (reqpos != this->savepos) {
        lprintf(" => seeking file to %"PRId64"\n", reqpos);
        if (fseeko(this->file, reqpos, SEEK_SET) != 0) {
          xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed: %s\n"), strerror(errno));
          return -1;
        }
      }
      this->curpos = newpos;
    } else {
      if ((pos = rip_seek_original(this, reqpos)) == -1) return -1;
      if (pos == reqpos) this->curpos = newpos;
    }

    return this->curpos;
  }

  if (this->curpos < this->savepos) {
    lprintf(" => seeking to end: %"PRId64"\n", this->savepos);
    if (this->regular) {
      lprintf(" => seeking file to end: %"PRId64"\n", this->savepos);
      if (fseeko(this->file, this->savepos, SEEK_SET) != 0) {
        xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed: %s\n"), strerror(errno));
        return -1;
      }
      this->curpos = this->savepos;
    } else {
      if ((pos = rip_seek_original(this, this->savepos)) == -1) return -1;
      if (pos > this->savepos)
        xine_log(this->stream->xine, XINE_LOG_MSG,
                 _("input_rip: %" PRIdMAX " bytes dropped\n"),
                 (intmax_t)(pos - this->savepos));
    }
  }

  /* read and catch remaining data after this->savepos */
  xine_monotonic_clock(&time1, NULL);
  while (this->curpos < newpos && interval < SEEK_TIMEOUT) {
    if( blocksize ) {
      buf_element_t *buf;

      buf = rip_plugin_read_block(this_gen, this->stream->video_fifo, blocksize);
      if (buf)
        buf->free_buffer(buf);
      else
        break;
    } else {
      size_t toread = newpos - this->curpos;
      if( toread > sizeof(buffer) )
        toread = sizeof(buffer);

      if( rip_plugin_read(this_gen, buffer, toread) <= 0 ) {
        xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed\n"));
        break;
      }
    }
    xine_monotonic_clock(&time2, NULL);
    interval = (double)(time2.tv_sec - time1.tv_sec)
               + (double)(time2.tv_usec - time1.tv_usec) / 1000000;
  }

  lprintf(" => new position %"PRId64"\n", this->curpos);

  return this->curpos;
}

static off_t rip_plugin_seek_time(input_plugin_t *this_gen, int time_offset, int origin) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("seek_time, time_offset: %d, origin: %d\n", time_offset, origin);

  return this->main_input_plugin->seek_time(this->main_input_plugin, time_offset, origin);
}

/*
 * return current position,
 * check values for debug build
 */
static off_t rip_plugin_get_current_pos(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
#ifdef DEBUG
  off_t pos;

  pos = this->main_input_plugin->get_current_pos(this->main_input_plugin);
  if (pos != this->curpos) {
    lprintf(CLR_FAIL "position: computed = %"PRId64", input plugin = %"PRId64"" CLR_RST "\n", this->curpos, pos);
  }
#endif

  return this->curpos;
}

static int rip_plugin_get_current_time(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_current_time(this->main_input_plugin);
}

static off_t rip_plugin_get_length (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  off_t length;

  length = this->main_input_plugin->get_length(this->main_input_plugin);
  if(length <= 0)
    length = this->savepos;

  return length;
}

static uint32_t rip_plugin_get_blocksize(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_blocksize(this->main_input_plugin);
}

static const char* rip_plugin_get_mrl (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_mrl(this->main_input_plugin);
}

static int rip_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("get optional data\n");
  if (this->preview && data_type == INPUT_OPTIONAL_DATA_PREVIEW) {
    memcpy(data, this->preview, this->preview_size);
    return this->preview_size;
  } else
    return this->main_input_plugin->get_optional_data(
	this->main_input_plugin, data, data_type);
}

/*
 * dispose main input plugin and self
 */
static void rip_plugin_dispose(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("rip_plugin_dispose\n");

  _x_free_input_plugin(this->stream, this->main_input_plugin);
  fclose(this->file);
  if (this->preview) free(this->preview);
  free(this);
}


/*
 * concat name of directory and name of file,
 * returns non-zero, if there was enough space
 */
static int dir_file_concat(char *target, size_t maxlen, const char *dir, const char *name) {
  size_t len_name = strlen(name);
  size_t len_dir = strlen(dir);
  size_t pos_name = 0;

  /* remove slashes */
  if (dir[len_dir - 1] == '/') len_dir--;
  if (name[0] == '/') {
    pos_name = 1;
    len_name--;
  }

  /* test and perform copy */
  if (len_dir + len_name + 2 > maxlen) {
    target[0] = '\0';
    return 0;
  }
  if (len_dir) memcpy(target, dir, len_dir);
  target[len_dir] = '/';
  strcpy(&target[len_dir + 1], name + pos_name);
  return 1;
}


/*
 * create self instance,
 * target file for writing stream is specified in 'data'
 */
input_plugin_t *_x_rip_plugin_get_instance (xine_stream_t *stream, const char *filename) {
  rip_input_plugin_t *this;
  input_plugin_t *main_plugin = stream->input_plugin;
  struct stat pstat;
  const char *mode;
  char target[MAX_TARGET_LEN], target_no[MAX_TARGET_LEN];
  char *fnc, *target_basename;
  int i;

  lprintf("catch file = %s, path = %s\n", filename, stream->xine->save_path);

  /* check given input plugin */
  if (!stream->input_plugin) {
    xine_log(stream->xine, XINE_LOG_MSG, _("input_rip: input plugin not defined!\n"));
    return NULL;
  }

  if (!stream->xine->save_path[0]) {
    xine_log(stream->xine, XINE_LOG_MSG,
	     _("input_rip: target directory wasn't specified, please fill out the option 'media.capture.save_dir'\n"));
    _x_message(stream, XINE_MSG_SECURITY,
	       _("The stream save feature is disabled until you set media.capture.save_dir in the configuration."), NULL);
    return NULL;
  }

#ifndef SAVING_ALWAYS_PERMIT
  if ( main_plugin->get_capabilities(main_plugin) & INPUT_CAP_RIP_FORBIDDEN ) {
    xine_log(stream->xine, XINE_LOG_MSG,
	     _("input_rip: ripping/caching of this source is not permitted!\n"));
    _x_message(stream, XINE_MSG_SECURITY,
	       _("xine is not allowed to save from this source. (possibly copyrighted material?)"), NULL);
    return NULL;
  }
#endif

  if (!filename || !filename[0]) {
    xine_log(stream->xine, XINE_LOG_MSG, _("input_rip: file name not given!\n"));
    return NULL;
  }

  this = calloc(1, sizeof(rip_input_plugin_t));
  this->main_input_plugin = main_plugin;
  this->stream            = stream;
  this->curpos  = 0;
  this->savepos = 0;

  fnc = strdup(filename);
  target_basename = basename(fnc);
  dir_file_concat(target, MAX_TARGET_LEN, stream->xine->save_path,
                  target_basename);
  strcpy(target_no, target);

  i = 1;
  mode = "wb+";
  do {
    /* find out kind of target */
    if (stat(target_no, &pstat) < 0) break;
#ifndef _MSC_VER
    if (S_ISFIFO(pstat.st_mode)) this->regular = 0;
    else this->regular = 1;
#else
    /* no fifos under MSVC */
    this->regular = 1;
#endif
    /* we want write into fifos */
    if (!this->regular) {
      mode = "wb";
      break;
    }

    snprintf(target_no, MAX_TARGET_LEN, "%s.%d", target, i);
    i++;
  } while(1);
  free(fnc);
  lprintf("target file: %s\n", target_no);

  if ((this->file = fopen(target_no, mode)) == NULL) {
    xine_log(this->stream->xine, XINE_LOG_MSG,
	     _("input_rip: error opening file %s: %s\n"), target_no, strerror(errno));
    free(this);
    return NULL;
  }

  /* fill preview memory */
  if ( (main_plugin->get_capabilities(main_plugin) & INPUT_CAP_SEEKABLE) == 0) {
    if ( main_plugin->get_capabilities(main_plugin) & INPUT_CAP_BLOCK ) {
      buf_element_t *buf;
      uint32_t blocksize;

      blocksize = main_plugin->get_blocksize(main_plugin);
      buf = main_plugin->read_block(main_plugin, stream->video_fifo, blocksize);

      this->preview_size = buf->size;
      this->preview = malloc(this->preview_size);
      memcpy(this->preview, buf->content, this->preview_size);

      buf->free_buffer(buf);
    } else {
      this->preview = malloc(MAX_PREVIEW_SIZE);
      this->preview_size = main_plugin->read(main_plugin, this->preview, MAX_PREVIEW_SIZE);
    }
  } else {
    this->preview = NULL;
  }

  if (this->preview && this->preview_size) {
    if (fwrite(this->preview, this->preview_size, 1, this->file) != 1) {
      xine_log(this->stream->xine, XINE_LOG_MSG,
               _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
               (intmax_t)(this->preview_size), strerror(errno));
      fclose(this->file);
      free(this);
      return NULL;
    }
    lprintf(" => saved %"PRId64" bytes (preview)\n", this->preview_size);
    this->savepos = this->preview_size;
  }

  this->input_plugin.open                = rip_plugin_open;
  this->input_plugin.get_capabilities    = rip_plugin_get_capabilities;
  this->input_plugin.read                = rip_plugin_read;
  this->input_plugin.read_block          = rip_plugin_read_block;
  this->input_plugin.seek                = rip_plugin_seek;
  if(this->main_input_plugin->seek_time)
    this->input_plugin.seek_time         = rip_plugin_seek_time;
  this->input_plugin.get_current_pos     = rip_plugin_get_current_pos;
  if(this->main_input_plugin->get_current_time)
    this->input_plugin.get_current_time  = rip_plugin_get_current_time;
  this->input_plugin.get_length          = rip_plugin_get_length;
  this->input_plugin.get_blocksize       = rip_plugin_get_blocksize;
  this->input_plugin.get_mrl             = rip_plugin_get_mrl;
  this->input_plugin.get_optional_data   = rip_plugin_get_optional_data;
  this->input_plugin.dispose             = rip_plugin_dispose;
  this->input_plugin.input_class         = main_plugin->input_class;

  return &this->input_plugin;
}
