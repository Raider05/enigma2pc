/*
 * Copyright (C) 2000-2005 the xine project
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#define LOG_MODULE "input_file"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>

#define MAXFILES      65535

#ifndef WIN32
/* MS needs O_BINARY to open files, for everyone else,
 * make sure it doesn't get in the way */
#  define O_BINARY  0
#endif

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

  char             *origin_path;
  int               show_hidden_files;

  int               mrls_allocated_entries;
  xine_mrl_t      **mrls;

} file_input_class_t;

typedef struct {
  input_plugin_t    input_plugin;

  xine_stream_t    *stream;

  int               fh;
#ifdef HAVE_MMAP
  int               mmap_on;
  uint8_t          *mmap_base;
  uint8_t          *mmap_curr;
  off_t             mmap_len;
#endif
  char             *mrl;

} file_input_plugin_t;


static uint32_t file_plugin_get_capabilities (input_plugin_t *this_gen) {

  struct stat          buf ;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->fh <0)
    return 0;

#ifdef _MSC_VER
    /*return INPUT_CAP_SEEKABLE | INPUT_CAP_GET_DIR;*/
	return INPUT_CAP_SEEKABLE;
#else
  if (fstat (this->fh, &buf) == 0) {
    if (S_ISREG(buf.st_mode))
      return INPUT_CAP_SEEKABLE;
    else
      return 0;
  } else
    perror ("system call fstat");
  return 0;
#endif /* _MSC_VER */
}

#ifdef HAVE_MMAP
/**
 * @brief Check if the file can be read through mmap().
 * @param this The instance of the input plugin to check
 *             with
 * @return 1 if the file can still be mmapped, 0 if the file
 *         changed size
 */
static int check_mmap_file(file_input_plugin_t *this) {
  struct stat          sbuf;

  if ( ! this->mmap_on ) return 0;

  if ( fstat (this->fh, &sbuf) != 0 ) {
    return 0;
  }

  /* If the file grew, we're most likely dealing with a timeshifting recording
   * so switch to normal access. */
  if ( this->mmap_len != sbuf.st_size ) {
    this->mmap_on = 0;

    lseek(this->fh, this->mmap_curr - this->mmap_base, SEEK_SET);
    return 0;
  }

  return 1;
}
#endif

static off_t file_plugin_read (input_plugin_t *this_gen, void *buf, off_t len) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (len < 0)
    return -1;

#ifdef HAVE_MMAP
  if ( check_mmap_file(this) ) {
    off_t l = len;
    if ( (this->mmap_curr + len) > (this->mmap_base + this->mmap_len) )
      l = (this->mmap_base + this->mmap_len) - this->mmap_curr;

    memcpy(buf, this->mmap_curr, l);
    this->mmap_curr += l;

    return l;
  }
#endif

  return read (this->fh, buf, len);
}

static buf_element_t *file_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->type = BUF_DEMUX_BLOCK;

#ifdef HAVE_MMAP
  if ( check_mmap_file(this) ) {
    off_t len = todo;

    if ( (this->mmap_curr + len) > (this->mmap_base + this->mmap_len) )
      len = (this->mmap_base + this->mmap_len) - this->mmap_curr;

    /* We use the still-mmapped file rather than copying it */
    buf->size = len;
    buf->content = this->mmap_curr;

    /* FIXME: it's completely illegal to free buffer->mem here
     * - buffer->mem has not been allocated by malloc
     * - demuxers expect buffer->mem != NULL
     */
    /* free(buf->mem); buf->mem = NULL; */

    this->mmap_curr += len;
  } else
#endif
  {
    off_t num_bytes, total_bytes = 0;

    buf->content = buf->mem;

    while (total_bytes < todo) {
      num_bytes = read (this->fh, buf->mem + total_bytes, todo-total_bytes);
      if (num_bytes <= 0) {
	if (num_bytes < 0) {
	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_file: read error (%s)\n"), strerror(errno));
	  _x_message(this->stream, XINE_MSG_READ_ERROR,
                     this->mrl, NULL);
	}
	buf->free_buffer (buf);
	buf = NULL;
	break;
      }
      total_bytes += num_bytes;
    }

    if( buf != NULL )
      buf->size = total_bytes;
  }

  return buf;
}

static off_t file_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

#ifdef HAVE_MMAP /* Simulate f*() library calls */
  if ( check_mmap_file(this) ) {
    uint8_t *new_point = this->mmap_curr;
    switch(origin) {
    case SEEK_SET: new_point = this->mmap_base + offset; break;
    case SEEK_CUR: new_point = this->mmap_curr + offset; break;
    case SEEK_END: new_point = this->mmap_base + this->mmap_len + offset; break;
    default:
      errno = EINVAL;
      return (off_t)-1;
    }
    if ( new_point < this->mmap_base || new_point > (this->mmap_base + this->mmap_len) ) {
      errno = EINVAL;
      return (off_t)-1;
    }

    this->mmap_curr = new_point;
    return (this->mmap_curr - this->mmap_base);
  }
#endif

  return lseek (this->fh, offset, origin);
}

static off_t file_plugin_get_current_pos (input_plugin_t *this_gen){
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->fh <0)
    return 0;

#ifdef HAVE_MMAP
  if ( check_mmap_file(this) )
    return (this->mmap_curr - this->mmap_base);
#endif

  return lseek (this->fh, 0, SEEK_CUR);
}

static off_t file_plugin_get_length (input_plugin_t *this_gen) {

  struct stat          buf ;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->fh <0)
    return 0;

#ifdef HAVE_MMAP
  if ( check_mmap_file(this) )
    return this->mmap_len;
#endif

  if (fstat (this->fh, &buf) == 0) {
    return buf.st_size;
  } else
    perror ("system call fstat");
  return 0;
}

static uint32_t file_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

/*
 * Return 1 if filepathname is a directory, otherwise 0
 */
static int is_a_dir(char *filepathname) {
  struct stat  pstat;

  stat(filepathname, &pstat);

  return (S_ISDIR(pstat.st_mode));
}

static const char* file_plugin_get_mrl (input_plugin_t *this_gen) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return this->mrl;
}

static int file_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void file_plugin_dispose (input_plugin_t *this_gen ) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

#ifdef HAVE_MMAP
  /* Check for mmap_base rather than mmap_on because the file might have
   * started as a mmap() and now might be changed to descriptor-based
   * access
   */
  if ( this->mmap_base )
    munmap(this->mmap_base, this->mmap_len);
#endif

  if (this->fh != -1)
    close(this->fh);

  free (this->mrl);

  free (this);
}

static char *decode_uri (char *uri) {
  uri = strdup(uri);
  _x_mrl_unescape (uri);
  return uri;
}

static int file_plugin_open (input_plugin_t *this_gen ) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;
  char                *filename;
  struct stat          sbuf;

  lprintf("file_plugin_open\n");

  if (strncasecmp (this->mrl, "file:/", 6) == 0)
  {
    if (strncasecmp (this->mrl, "file://localhost/", 16) == 0)
      filename = decode_uri(&(this->mrl[16]));
    else if (strncasecmp (this->mrl, "file://127.0.0.1/", 16) == 0)
      filename = decode_uri(&(this->mrl[16]));
    else
      filename = decode_uri(&(this->mrl[5]));
  }
  else
    filename = strdup(this->mrl); /* NEVER unescape plain file names! */

  this->fh = xine_open_cloexec(filename, O_RDONLY|O_BINARY);

  if (this->fh == -1) {
    if (errno == EACCES) {
      _x_message(this->stream, XINE_MSG_PERMISSION_ERROR, this->mrl, NULL);
      xine_log (this->stream->xine, XINE_LOG_MSG,
                _("input_file: Permission denied: >%s<\n"), this->mrl);
    } else if (errno == ENOENT) {
      _x_message(this->stream, XINE_MSG_FILE_NOT_FOUND, this->mrl, NULL);
      xine_log (this->stream->xine, XINE_LOG_MSG,
                _("input_file: File not found: >%s<\n"), this->mrl);
    }

    free(filename);
    return -1;
  }

  free(filename);

#ifdef HAVE_MMAP
  this->mmap_on = 0;
  this->mmap_base = NULL;
  this->mmap_curr = NULL;
  this->mmap_len = 0;
#endif

  /* don't check length of fifo or character device node */
  if (fstat (this->fh, &sbuf) == 0) {
    if (!S_ISREG(sbuf.st_mode))
      return 1;
  }

#ifdef HAVE_MMAP
  {
    size_t tmp_size = sbuf.st_size; /* may cause truncation - if it does, DON'T mmap! */
    if ((tmp_size == sbuf.st_size) &&
	( (this->mmap_base = mmap(NULL, tmp_size, PROT_READ, MAP_SHARED, this->fh, 0)) != (void*)-1 )) {
      this->mmap_on = 1;
      this->mmap_curr = this->mmap_base;
      this->mmap_len = sbuf.st_size;
    } else {
      this->mmap_base = NULL;
    }
  }
#endif

  if (file_plugin_get_length (this_gen) == 0) {
      _x_message(this->stream, XINE_MSG_FILE_EMPTY, this->mrl, NULL);
      close (this->fh);
      this->fh = -1;
      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("input_file: File empty: >%s<\n"), this->mrl);
      return -1;
  }

  return 1;
}

static input_plugin_t *file_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *data) {

  /* file_input_class_t  *cls = (file_input_class_t *) cls_gen; */
  file_input_plugin_t *this;
  char                *mrl = strdup(data);

  lprintf("file_class_get_instance\n");

  if ((strncasecmp (mrl, "file:", 5)) && strstr (mrl, ":/") && (strstr (mrl, ":/") < strchr(mrl, '/'))) {
    free (mrl);
    return NULL;
  }

  this = (file_input_plugin_t *) calloc(1, sizeof (file_input_plugin_t));
  this->stream = stream;
  this->mrl    = mrl;
  this->fh     = -1;

  this->input_plugin.open               = file_plugin_open;
  this->input_plugin.get_capabilities   = file_plugin_get_capabilities;
  this->input_plugin.read               = file_plugin_read;
  this->input_plugin.read_block         = file_plugin_read_block;
  this->input_plugin.seek               = file_plugin_seek;
  this->input_plugin.get_current_pos    = file_plugin_get_current_pos;
  this->input_plugin.get_length         = file_plugin_get_length;
  this->input_plugin.get_blocksize      = file_plugin_get_blocksize;
  this->input_plugin.get_mrl            = file_plugin_get_mrl;
  this->input_plugin.get_optional_data  = file_plugin_get_optional_data;
  this->input_plugin.dispose            = file_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  return &this->input_plugin;
}


/*
 * plugin class functions
 */

#ifndef S_ISLNK
#define S_ISLNK(mode)  0
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(mode) 0
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(mode) 0
#endif
#ifndef S_ISCHR
#define S_ISCHR(mode)  0
#endif
#ifndef S_ISBLK
#define S_ISBLK(mode)  0
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  0
#endif
#if !S_IXUGO
#define S_IXUGO        (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

/*
 * Callback for config changes.
 */
static void hidden_bool_cb(void *data, xine_cfg_entry_t *cfg) {
  file_input_class_t *this = (file_input_class_t *) data;

  this->show_hidden_files = cfg->num_value;
}
static void origin_change_cb(void *data, xine_cfg_entry_t *cfg) {
  file_input_class_t *this = (file_input_class_t *) data;

  this->origin_path = cfg->str_value;
}

/*
 * Sorting function, it comes from GNU fileutils package.
 */
#define S_N        0x0
#define S_I        0x4
#define S_F        0x8
#define S_Z        0xC
#define CMP          2
#define LEN          3
#define ISDIGIT(c)   ((unsigned) (c) - '0' <= 9)
static int _strverscmp(const char *s1, const char *s2) {
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  unsigned char c1, c2;
  int state;
  int diff;
  static const unsigned int next_state[] = {
    S_N, S_I, S_Z, S_N,
    S_N, S_I, S_I, S_I,
    S_N, S_F, S_F, S_F,
    S_N, S_F, S_Z, S_Z
  };
  static const int result_type[] = {
    CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
    CMP,  -1,  -1, CMP,   1, LEN, LEN, CMP,
      1, LEN, LEN, CMP, CMP, CMP, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
    CMP,   1,   1, CMP,  -1, CMP, CMP, CMP,
     -1, CMP, CMP, CMP
  };

  if(p1 == p2)
    return 0;

  c1 = *p1++;
  c2 = *p2++;

  state = S_N | ((c1 == '0') + (ISDIGIT(c1) != 0));

  while((diff = c1 - c2) == 0 && c1 != '\0') {
    state = next_state[state];
    c1 = *p1++;
    c2 = *p2++;
    state |= (c1 == '0') + (ISDIGIT(c1) != 0);
  }

  state = result_type[state << 2 | ((c2 == '0') + (ISDIGIT(c2) != 0))];

  switch(state) {
  case CMP:
    return diff;

  case LEN:
    while(ISDIGIT(*p1++))
      if(!ISDIGIT(*p2++))
	return 1;

    return ISDIGIT(*p2) ? -1 : diff;

  default:
    return state;
  }
}

/*
 * Wrapper to _strverscmp() for qsort() calls, which sort mrl_t type array.
 */
static int _sortfiles_default(const xine_mrl_t *s1, const xine_mrl_t *s2) {
  return(_strverscmp(s1->mrl, s2->mrl));
}

/*
 * Return the type (OR'ed) of the given file *fully named*
 */
static uint32_t get_file_type(char *filepathname, char *origin, xine_t *xine) {
  struct stat  pstat;
  int          mode;
  uint32_t     file_type = 0;
  char         buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if((lstat(filepathname, &pstat)) < 0) {
    snprintf(buf, sizeof(buf), "%s/%s", origin, filepathname);
    if((lstat(buf, &pstat)) < 0) {
      lprintf ("lstat failed for %s{%s}\n", filepathname, origin);
      file_type |= mrl_unknown;
      return file_type;
    }
  }

  file_type |= mrl_file;

  mode = pstat.st_mode;

  if(S_ISLNK(mode))
    file_type |= mrl_file_symlink;
  else if(S_ISDIR(mode))
    file_type |= mrl_file_directory;
  else if(S_ISCHR(mode))
    file_type |= mrl_file_chardev;
  else if(S_ISBLK(mode))
    file_type |= mrl_file_blockdev;
  else if(S_ISFIFO(mode))
    file_type |= mrl_file_fifo;
  else if(S_ISSOCK(mode))
    file_type |= mrl_file_sock;
  else {
    if(S_ISREG(mode)) {
      file_type |= mrl_file_normal;
    }
    if(mode & S_IXUGO)
      file_type |= mrl_file_exec;
  }

  if(filepathname[strlen(filepathname) - 1] == '~')
    file_type |= mrl_file_backup;

  return file_type;
}

/*
 * Return the file size of the given file *fully named*
 */
static off_t get_file_size(char *filepathname, char *origin) {
  struct stat  pstat;
  char         buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if((lstat(filepathname, &pstat)) < 0) {
    snprintf(buf, sizeof(buf), "%s/%s", origin, filepathname);
    if((lstat(buf, &pstat)) < 0)
      return (off_t) 0;
  }

  return pstat.st_size;
}

static xine_mrl_t **file_class_get_dir (input_class_t *this_gen,
					const char *filename, int *nFiles) {

  /* FIXME: this code needs cleanup badly */

  file_input_class_t   *this = (file_input_class_t *) this_gen;
  struct dirent        *pdirent;
  DIR                  *pdir;
  xine_mrl_t           *hide_files, *dir_files, *norm_files;
  char                  current_dir[XINE_PATH_MAX + 1];
  char                  current_dir_slashed[XINE_PATH_MAX + 1];
  char                  fullfilename[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  int                   num_hide_files  = 0;
  int                   num_dir_files   = 0;
  int                   num_norm_files  = 0;
  int                   num_files       = -1;
  int                 (*func) ()        = _sortfiles_default;
  int                   already_tried   = 0;

  *nFiles = 0;
  memset(current_dir, 0, sizeof(current_dir));

  /*
   * No origin location, so got the content of the current directory
   */
  if(!filename) {
    snprintf(current_dir, XINE_PATH_MAX, "%s", this->origin_path);
  }
  else {
    snprintf(current_dir, XINE_PATH_MAX, "%s", filename);

    /* Remove exceed '/' */
    while((current_dir[strlen(current_dir) - 1] == '/') && strlen(current_dir) > 1)
      current_dir[strlen(current_dir) - 1] = '\0';
  }

  /* Store new origin path */
 try_again_from_home:

  this->config->update_string(this->config, "media.files.origin_path", current_dir);

  if(strcasecmp(current_dir, "/"))
    snprintf(current_dir_slashed, sizeof(current_dir_slashed), "%s/", current_dir);
  else
    sprintf(current_dir_slashed, "/");

  /*
   * Ooch!
   */
  if((pdir = opendir(current_dir)) == NULL) {

    if(!already_tried) {
      /* Try one more time with user homedir */
      snprintf(current_dir, XINE_PATH_MAX, "%s", xine_get_homedir());
      already_tried++;
      goto try_again_from_home;
    }

    return NULL;
  }

  dir_files  = (xine_mrl_t *) calloc(MAXFILES, sizeof(xine_mrl_t));
  hide_files = (xine_mrl_t *) calloc(MAXFILES, sizeof(xine_mrl_t));
  norm_files = (xine_mrl_t *) calloc(MAXFILES, sizeof(xine_mrl_t));

  while((pdirent = readdir(pdir)) != NULL) {

    memset(fullfilename, 0, sizeof(fullfilename));
    snprintf(fullfilename, sizeof(fullfilename), "%s/%s", current_dir, pdirent->d_name);

    if(is_a_dir(fullfilename)) {

      /* if user don't want to see hidden files, ignore them */
      if(this->show_hidden_files == 0 &&
	 ((strlen(pdirent->d_name) > 1)
	  && (pdirent->d_name[0] == '.' &&  pdirent->d_name[1] != '.'))) {
	;
      }
      else {

	dir_files[num_dir_files].origin = strdup(current_dir);
	dir_files[num_dir_files].mrl    = _x_asprintf("%s%s", current_dir_slashed, pdirent->d_name);
	dir_files[num_dir_files].link   = NULL;
	dir_files[num_dir_files].type   = get_file_type(fullfilename, current_dir, this->xine);
	dir_files[num_dir_files].size   = get_file_size(fullfilename, current_dir);

	/* The file is a link, follow it */
	if(dir_files[num_dir_files].type & mrl_file_symlink) {
	  char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	  int linksize;

	  memset(linkbuf, 0, sizeof(linkbuf));
	  linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);

	  if(linksize < 0)
	    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		     "input_file: readlink() failed: %s\n", strerror(errno));
	  else {
	    dir_files[num_dir_files].link =
	      strndup(linkbuf, linksize);

	    dir_files[num_dir_files].type |= get_file_type(dir_files[num_dir_files].link, current_dir, this->xine);
	  }
	}

	num_dir_files++;
      }

    } /* Hmmmm, an hidden file ? */
    else if((strlen(pdirent->d_name) > 1)
	    && (pdirent->d_name[0] == '.' &&  pdirent->d_name[1] != '.')) {

      /* if user don't want to see hidden files, ignore them */
      if(this->show_hidden_files) {

	hide_files[num_hide_files].origin = strdup(current_dir);
	hide_files[num_hide_files].mrl    = _x_asprintf("%s%s", current_dir_slashed, pdirent->d_name);
	hide_files[num_hide_files].link   = NULL;
	hide_files[num_hide_files].type   = get_file_type(fullfilename, current_dir, this->xine);
	hide_files[num_hide_files].size   = get_file_size(fullfilename, current_dir);

	/* The file is a link, follow it */
	if(hide_files[num_hide_files].type & mrl_file_symlink) {
	  char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	  int linksize;

	  memset(linkbuf, 0, sizeof(linkbuf));
	  linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);

	  if(linksize < 0) {
	    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		     "input_file: readlink() failed: %s\n", strerror(errno));
	  }
	  else {
	    hide_files[num_hide_files].link =
	      strndup(linkbuf, linksize);
	    hide_files[num_hide_files].type |= get_file_type(hide_files[num_hide_files].link, current_dir, this->xine);
	  }
	}

	num_hide_files++;
      }

    } /* So a *normal* one. */
    else {

      norm_files[num_norm_files].origin = strdup(current_dir);
      norm_files[num_norm_files].mrl    = _x_asprintf("%s%s", current_dir_slashed, pdirent->d_name);
      norm_files[num_norm_files].link   = NULL;
      norm_files[num_norm_files].type   = get_file_type(fullfilename, current_dir, this->xine);
      norm_files[num_norm_files].size   = get_file_size(fullfilename, current_dir);

      /* The file is a link, follow it */
      if(norm_files[num_norm_files].type & mrl_file_symlink) {
	char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	int linksize;

	memset(linkbuf, 0, sizeof(linkbuf));
	linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);

	if(linksize < 0) {
	  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		   "input_file: readlink() failed: %s\n", strerror(errno));
	}
	else {
	  norm_files[num_norm_files].link =
	    strndup(linkbuf, linksize);
	  norm_files[num_norm_files].type |= get_file_type(norm_files[num_norm_files].link, current_dir, this->xine);
	}
      }

      num_norm_files++;
    }

    num_files++;
  }

  closedir(pdir);

  /*
   * Ok, there are some files here, so sort
   * them then store them into global mrls array.
   */
  if(num_files > 0) {
    int i;

    num_files = 0;

    /*
     * Sort arrays
     */
    if(num_dir_files)
      qsort(dir_files, num_dir_files, sizeof(xine_mrl_t), func);

    if(num_hide_files)
      qsort(hide_files, num_hide_files, sizeof(xine_mrl_t), func);

    if(num_norm_files)
      qsort(norm_files, num_norm_files, sizeof(xine_mrl_t), func);

    /*
     * Add directories entries
     */
    for(i = 0; i < num_dir_files; i++) {

      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = calloc(1, sizeof(xine_mrl_t));
      }
      else
	MRL_ZERO(this->mrls[num_files]);

      MRL_DUPLICATE(&dir_files[i], this->mrls[num_files]);

      num_files++;
    }

    /*
     * Add hidden files entries
     */
    for(i = 0; i < num_hide_files; i++) {

      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = calloc(1, sizeof(xine_mrl_t));
      }
      else
	MRL_ZERO(this->mrls[num_files]);

      MRL_DUPLICATE(&hide_files[i], this->mrls[num_files]);

      num_files++;
    }

    /*
     * Add other files entries
     */
    for(i = 0; i < num_norm_files; i++) {

      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = calloc(1, sizeof(xine_mrl_t));
      }
      else
	MRL_ZERO(this->mrls[num_files]);

      MRL_DUPLICATE(&norm_files[i], this->mrls[num_files]);

      num_files++;
    }

    /* Some cleanups before leaving */
    for(i = num_dir_files; i == 0; i--)
      MRL_ZERO(&dir_files[i]);
    free(dir_files);

    for(i = num_hide_files; i == 0; i--)
      MRL_ZERO(&hide_files[i]);
    free(hide_files);

    for(i = num_norm_files; i == 0; i--)
      MRL_ZERO(&norm_files[i]);
    free(norm_files);

  }
  else {
    free(hide_files);
    free(dir_files);
    free(norm_files);
    return NULL;
  }

  /*
   * Inform caller about files found number.
   */
  *nFiles = num_files;

  /*
   * Freeing exceeded mrls if exists.
   */
  while(this->mrls_allocated_entries > num_files) {
    MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
    free(this->mrls[this->mrls_allocated_entries--]);
  }

  /*
   * This is useful to let UI know where it should stops ;-).
   */
  this->mrls[num_files] = NULL;

  /*
   * Some debugging info
   */
  /*
  {
    int j = 0;
    while(this->mrls[j]) {
      printf("mrl[%d] = '%s'\n", j, this->mrls[j]->mrl);
      j++;
    }
  }
  */

  return this->mrls;
}

static void file_class_dispose (input_class_t *this_gen) {
  file_input_class_t  *this = (file_input_class_t *) this_gen;
  config_values_t     *config = this->xine->config;

  config->unregister_callback(config, "media.files.origin_path");

  while(this->mrls_allocated_entries) {
    MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
    free(this->mrls[this->mrls_allocated_entries--]);
  }
  free (this->mrls);

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  file_input_class_t  *this;
  config_values_t     *config;

  this = (file_input_class_t *) calloc(1, sizeof (file_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = file_class_get_instance;
  this->input_class.identifier         = "file";
  this->input_class.description        = N_("file input plugin");
  this->input_class.get_dir            = file_class_get_dir;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = file_class_dispose;
  this->input_class.eject_media        = NULL;

  this->mrls = (xine_mrl_t **) calloc(1, sizeof(xine_mrl_t*));
  this->mrls_allocated_entries = 0;

  {
    char current_dir[XINE_PATH_MAX + 1];

    if(getcwd(current_dir, sizeof(current_dir)) == NULL)
      strcpy(current_dir, ".");

    this->origin_path = config->register_filename(config, "media.files.origin_path",
						current_dir, XINE_CONFIG_STRING_IS_DIRECTORY_NAME,
						_("file browsing start location"),
						_("The browser to select the file to play will "
						  "start at this location."),
						0, origin_change_cb, (void *) this);
  }

  this->show_hidden_files = config->register_bool(config,
						  "media.files.show_hidden_files",
						  0, _("list hidden files"),
						  _("If enabled, the browser to select the file to "
						    "play will also show hidden files."),
						  10, hidden_bool_cb, (void *) this);

  return this;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "FILE", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
