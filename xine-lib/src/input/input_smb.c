/*
 * Copyright (C) 2008 the xine project
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

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>

#include <libsmbclient.h>
#include <sys/types.h>
#include <errno.h>

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#define MAXFILES      65535

typedef struct {
	input_class_t input_class;
	xine_t *xine;

	int mrls_allocated_entries;
	xine_mrl_t **mrls;
} smb_input_class_t;

typedef struct {
	input_plugin_t input_plugin;
	xine_stream_t *stream;

	/* File */
	char *mrl;
	int fd;
} smb_input_t;


static uint32_t
smb_plugin_get_capabilities (input_plugin_t *this_gen)
{
	return INPUT_CAP_SEEKABLE; // | INPUT_CAP_SPULANG;
}


static off_t
smb_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len)
{
	smb_input_t *this = (smb_input_t *) this_gen;
	char *buf = (char *)buf_gen;
	off_t n, num_bytes;

	if (len < 0)
		return -1;
	num_bytes = 0;

	while (num_bytes < len)
	{
		n = smbc_read( this->fd, buf+num_bytes, len-num_bytes );
		if (n<0) return -1;
		if (!n) return num_bytes;
		num_bytes += n;
	}

	return num_bytes;
}

static buf_element_t*
smb_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
		off_t todo)
{
	off_t total_bytes;
	buf_element_t *buf = fifo->buffer_pool_alloc (fifo);

	if (todo > buf->max_size)
	  todo = buf->max_size;
	if (todo < 0) {
		buf->free_buffer (buf);
		return NULL;
	}

	buf->content = buf->mem;
	buf->type = BUF_DEMUX_BLOCK;

	total_bytes = smb_plugin_read (this_gen, (char*)buf->content, todo);

	if (total_bytes == todo) buf->size = todo;
	else
	{
		buf->free_buffer (buf);
		buf = NULL;
	}

	return buf;
}

static off_t
smb_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd<0) return 0;
	return smbc_lseek(this->fd,offset,origin);
}

static off_t
smb_plugin_get_current_pos (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd<0) return 0;
	return smbc_lseek(this->fd,0,SEEK_CUR);
}

static off_t
smb_plugin_get_length (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	int e;
	struct stat st;

	if (this->fd>=0) e = smbc_fstat(this->fd,&st);
	else e = smbc_stat(this->mrl,&st);

	if (e) return 0;

	return st.st_size;
}

static const char*
smb_plugin_get_mrl (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	return this->mrl;
}

static uint32_t smb_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
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


static xine_mrl_t **smb_class_get_dir (input_class_t *this_gen,
          const char *filename, int *nFiles) {

	smb_input_class_t   *this = (smb_input_class_t *) this_gen;
	int                 (*func) ()        = _sortfiles_default;
	int        dir;
	int i;
	struct smbc_dirent  *pdirent;
	char current_path [XINE_PATH_MAX + 1];
	char current_path_smb [XINE_PATH_MAX + 1];
	int num_files=0;
	if (filename != NULL && strlen(filename)>6){
		snprintf(current_path, XINE_PATH_MAX, "%s",filename);
		snprintf(current_path_smb, XINE_PATH_MAX, "%s/",current_path);
	}else{
		snprintf(current_path, XINE_PATH_MAX, "smb:/");
		snprintf(current_path_smb, XINE_PATH_MAX, "smb://");
	}

	if ((dir = smbc_opendir(current_path_smb)) >= 0){
		xine_mrl_t *dir_files  = (xine_mrl_t *) calloc(MAXFILES, sizeof(xine_mrl_t));
		xine_mrl_t *norm_files = (xine_mrl_t *) calloc(MAXFILES, sizeof(xine_mrl_t));
		int num_dir_files=0;
		int num_norm_files=0;
		while ((pdirent = smbc_readdir(dir)) != NULL){
			if (pdirent->smbc_type == SMBC_WORKGROUP){
				dir_files[num_dir_files].link   = NULL;
				dir_files[num_dir_files].type = mrl_file | mrl_file_directory;
				dir_files[num_dir_files].origin = strdup(current_path);
				dir_files[num_dir_files].mrl    = _x_asprintf("%s/%s", current_path, pdirent->name);
				dir_files[num_dir_files].size   = pdirent->dirlen;
				num_dir_files ++;
			}else if (pdirent->smbc_type == SMBC_SERVER){
				if (num_dir_files == 0) {
					dir_files[num_dir_files].link   = NULL;
					dir_files[num_dir_files].type = mrl_file | mrl_file_directory;
					dir_files[num_dir_files].origin = strdup("smb:/");
					dir_files[num_dir_files].mrl    = strdup("smb://..");
					dir_files[num_dir_files].size   = pdirent->dirlen;
					num_dir_files ++;
				}
				dir_files[num_dir_files].link   = NULL;
				dir_files[num_dir_files].type   = mrl_file | mrl_file_directory;
				dir_files[num_dir_files].origin = strdup("smb:/");
				dir_files[num_dir_files].mrl    = _x_asprintf("smb://%s", pdirent->name);
				dir_files[num_dir_files].size   = pdirent->dirlen;
				num_dir_files ++;
			} else if (pdirent->smbc_type == SMBC_FILE_SHARE){
				if (num_dir_files == 0) {
					dir_files[num_dir_files].link   = NULL;
					dir_files[num_dir_files].type   = mrl_file | mrl_file_directory;
					dir_files[num_dir_files].origin = strdup(current_path);
					dir_files[num_dir_files].mrl    = _x_asprintf("%s/..", current_path);
					dir_files[num_dir_files].type   |= mrl_file_directory;
					dir_files[num_dir_files].size   = pdirent->dirlen;
					num_dir_files ++;
				}
				if (pdirent->name[strlen(pdirent->name)-1]!='$'){
					dir_files[num_dir_files].link   = NULL;
					dir_files[num_dir_files].type   = mrl_file | mrl_file_directory;
					dir_files[num_dir_files].origin = strdup(current_path);
					dir_files[num_dir_files].mrl    = _x_asprintf("%s/%s", current_path, pdirent->name);
					dir_files[num_dir_files].size   = pdirent->dirlen;
					num_dir_files ++;
				}
			} else if (pdirent->smbc_type == SMBC_DIR){
				dir_files[num_dir_files].link   = NULL;
				dir_files[num_dir_files].type   = mrl_file | mrl_file_directory;
				dir_files[num_dir_files].origin = strdup(current_path);
				dir_files[num_dir_files].mrl    = _x_asprintf("%s/%s", current_path, pdirent->name);
				dir_files[num_dir_files].size   = pdirent->dirlen;
				num_dir_files ++;
			}else if (pdirent->smbc_type == SMBC_FILE){
				norm_files[num_norm_files].link   = NULL;
				norm_files[num_norm_files].type   = mrl_file | mrl_file_normal;
				norm_files[num_norm_files].origin = strdup(current_path);
				norm_files[num_norm_files].mrl    = _x_asprintf("%s/%s", current_path, pdirent->name);
				norm_files[num_norm_files].size   = pdirent->dirlen;
				num_norm_files ++;
			}
		}
		smbc_closedir(dir);

		if (num_dir_files == 0) {
			dir_files[num_dir_files].link   = NULL;
			dir_files[num_dir_files].origin = strdup(current_path);
			dir_files[num_dir_files].mrl    = _x_asprintf("%s/..", current_path);
			dir_files[num_dir_files].type = mrl_file | mrl_file_directory;
			dir_files[num_dir_files].size   = 0;
			num_dir_files ++;
		}

		/*
		 * Sort arrays
		 */
		if(num_dir_files)
			qsort(dir_files, num_dir_files, sizeof(xine_mrl_t), func);

		if(num_norm_files)
			qsort(norm_files, num_norm_files, sizeof(xine_mrl_t), func);

		/*
		 * Add directories entries
		 */
		for(i = 0; i < num_dir_files; i++) {
			if (num_files >= this->mrls_allocated_entries) {
				++this->mrls_allocated_entries;
				this->mrls = realloc(this->mrls,
					(this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
				this->mrls[num_files] = calloc(1, sizeof(xine_mrl_t));
			}else
				MRL_ZERO(this->mrls[num_files]);

			MRL_DUPLICATE(&dir_files[i], this->mrls[num_files]);

			num_files++;
		}

		/*
		 * Add other files entries
		 */
		for(i = 0; i < num_norm_files; i++) {
			if(num_files >= this->mrls_allocated_entries) {
				++this->mrls_allocated_entries;
				this->mrls = realloc(this->mrls,
					(this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
				this->mrls[num_files] = calloc(1, sizeof(xine_mrl_t));
			}else
				MRL_ZERO(this->mrls[num_files]);

			MRL_DUPLICATE(&norm_files[i], this->mrls[num_files]);

			num_files++;
		}

		/* Some cleanups before leaving */
		for(i = num_dir_files; i == 0; i--)
			MRL_ZERO(&dir_files[i]);
		free(dir_files);

		for(i = num_norm_files; i == 0; i--)
			MRL_ZERO(&norm_files[i]);
		free(norm_files);
	}else {
		xprintf (this->xine, XINE_VERBOSITY_DEBUG,
			"input_smb: smbc_opendir(\"%s\") failed: %d - %s\n",
			current_path, errno, strerror(errno));
		*nFiles = 0;
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

	return this->mrls;
}

static int
smb_plugin_get_optional_data (input_plugin_t *this_gen,
		void *data, int data_type)
{
	return INPUT_OPTIONAL_UNSUPPORTED;
}

static void
smb_plugin_dispose (input_plugin_t *this_gen )
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd>=0)
		smbc_close(this->fd);
	if (this->mrl)
		free (this->mrl);
	free (this);
}

static int
smb_plugin_open (input_plugin_t *this_gen )
{
	smb_input_t *this = (smb_input_t *) this_gen;
	smb_input_class_t *class = (smb_input_class_t *) this_gen->input_class;

	this->fd = smbc_open(this->mrl,O_RDONLY,0);
	xprintf(class->xine, XINE_VERBOSITY_DEBUG,
	        "input_smb: open failed for %s: %s\n",
	        this->mrl, strerror(errno));
	if (this->fd<0) return 0;

	return 1;
}

static void
smb_class_dispose (input_class_t *this_gen)
{
	smb_input_class_t *this = (smb_input_class_t *) this_gen;

	while(this->mrls_allocated_entries) {
		MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
		free(this->mrls[this->mrls_allocated_entries--]);
	}
	free(this->mrls);

	free (this);
}

static input_plugin_t *
smb_class_get_instance (input_class_t *class_gen, xine_stream_t *stream,
		const char *mrl)
{
	smb_input_t *this;

	if (mrl == NULL)
		return NULL;
	if (strncmp (mrl, "smb://",6))
		return NULL;

	this = calloc(1, sizeof(smb_input_t));
	this->stream = stream;
	this->mrl = strdup (mrl);
	this->fd = -1;

	this->input_plugin.open              = smb_plugin_open;
	this->input_plugin.get_capabilities  = smb_plugin_get_capabilities;
	this->input_plugin.read              = smb_plugin_read;
	this->input_plugin.read_block        = smb_plugin_read_block;
	this->input_plugin.seek              = smb_plugin_seek;
	this->input_plugin.get_current_pos   = smb_plugin_get_current_pos;
	this->input_plugin.get_length        = smb_plugin_get_length;
	this->input_plugin.get_blocksize     = smb_plugin_get_blocksize;
	this->input_plugin.get_mrl           = smb_plugin_get_mrl;
	this->input_plugin.get_optional_data =
		smb_plugin_get_optional_data;
	this->input_plugin.dispose           = smb_plugin_dispose;
	this->input_plugin.input_class       = class_gen;

	return &this->input_plugin;
}

static void smb_auth(const char *srv, const char *shr, char *wg, int wglen, char *un, int unlen, char *pw, int pwlen)
{
	wglen = unlen = pwlen = 0;
}

static void
*init_input_class (xine_t *xine, void *data)
{
	smb_input_class_t *this = NULL;
	/* libsmbclient seems to mess up with locale. Workaround: save and restore locale */
#ifdef HAVE_SETLOCALE
	char *lcl = strdup(setlocale(LC_MESSAGES, NULL));
#endif

	if (smbc_init(smb_auth,(xine->verbosity >= XINE_VERBOSITY_DEBUG)))
	  goto _exit_error;

	this = calloc(1, sizeof(smb_input_class_t));
	this->xine = xine;

	this->input_class.get_instance       = smb_class_get_instance;
	this->input_class.identifier         = "smb";
	this->input_class.description        = N_("CIFS/SMB input plugin based on libsmbclient");
	this->input_class.get_dir            = smb_class_get_dir;
	this->input_class.get_autoplay_list  = NULL;
	this->input_class.dispose            = smb_class_dispose;
	this->input_class.eject_media        = NULL;

 _exit_error:

#ifdef HAVE_SETLOCALE
	setlocale(LC_MESSAGES, lcl);
	free(lcl);
#endif

	return (input_class_t *) this;
}

static const input_info_t input_info_smb = {
  0                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
	{ PLUGIN_INPUT, 18, "smb", XINE_VERSION_CODE, &input_info_smb,
		init_input_class },
	{ PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

