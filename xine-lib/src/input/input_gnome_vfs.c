/*
 * Copyright (C) 2000-2003 the xine project
 * 2002 Bastien Nocera <hadess@hadess.net>
 *
 * This file is part of totem,
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
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"

#include <libgnomevfs/gnome-vfs.h>

#ifdef  __GNUC__
#define D(...)
#else
#define D(__VA_ARGS__)
#endif
/* #define D(...) g_message (__VA_ARGS__) */
/* #define LOG */

typedef struct {
	input_class_t input_class;
	xine_t *xine;
} gnomevfs_input_class_t;

typedef struct {
	input_plugin_t input_plugin;
	xine_stream_t *stream;
	nbc_t *nbc;

	/* File */
	GnomeVFSHandle *fh;
	off_t curpos;
	char *mrl;
	GnomeVFSURI *uri;

	/* Preview */
	char preview[MAX_PREVIEW_SIZE];
	off_t preview_size;
	off_t preview_pos;
} gnomevfs_input_t;

static off_t gnomevfs_plugin_get_current_pos (input_plugin_t *this_gen);


static uint32_t
gnomevfs_plugin_get_capabilities (input_plugin_t *this_gen)
{
	return INPUT_CAP_SEEKABLE | INPUT_CAP_SPULANG;
}

#define SSH_BUFFER_SIZE 256 * 1024

static off_t
gnomevfs_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len)
{
  char *buf = (char *)buf_gen;
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	off_t n, num_bytes;

	D("gnomevfs_plugin_read: %ld", (long int) len);

	num_bytes = 0;

	while (num_bytes < len)
	{
		GnomeVFSResult res;

		res = gnome_vfs_read (this->fh, &buf[num_bytes],
				(GnomeVFSFileSize) MIN (len - num_bytes, SSH_BUFFER_SIZE),
				(GnomeVFSFileSize *)&n);

		D("gnomevfs_plugin_read: read %ld from gnome-vfs",
				(long int) n);
		if (res != GNOME_VFS_OK && res != GNOME_VFS_ERROR_EOF)
		{
			D("gnomevfs_plugin_read: gnome_vfs_read returns %s",
					gnome_vfs_result_to_string (res));
			return -1;
		} else if (res == GNOME_VFS_ERROR_EOF) {
			D("gnomevfs_plugin_read: GNOME_VFS_ERROR_EOF");
			return num_bytes;
		}

		if (n <= 0)
		{
			g_warning ("input_gnomevfs: read error");
		}

		num_bytes += n;
		this->curpos += n;
	}

	return num_bytes;
}

static buf_element_t*
gnomevfs_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
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

	total_bytes = gnomevfs_plugin_read (this_gen, buf->content, todo);

	if (total_bytes == todo) buf->size = todo;
	else
	{
		buf->free_buffer (buf);
		buf = NULL;
	}

	return buf;
}

static off_t
gnomevfs_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	if (gnome_vfs_seek (this->fh, origin, offset) == GNOME_VFS_OK)
	{
		D ("gnomevfs_plugin_seek: %d", (int) (origin + offset));
		return (off_t) (origin + offset);
	} else
		return (off_t) gnomevfs_plugin_get_current_pos (this_gen);
}

static off_t
gnomevfs_plugin_get_current_pos (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	GnomeVFSFileSize offset;

	if (this->fh == NULL)
	{
		D ("gnomevfs_plugin_get_current_pos: (this->fh == NULL)");
		return 0;
	}

	if (gnome_vfs_tell (this->fh, &offset) == GNOME_VFS_OK)
	{
		D ("gnomevfs_plugin_get_current_pos: %d", (int) offset);
		return (off_t) offset;
	} else
		return 0;
}

static off_t
gnomevfs_plugin_get_length (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	GnomeVFSFileInfo *info;
	off_t length;

	if (this->fh == NULL)
	{
		D ("gnomevfs_plugin_get_length: (this->fh == NULL)");
		return 0;
	}

	info = gnome_vfs_file_info_new ();
	if (gnome_vfs_get_file_info (this->mrl, info,
				GNOME_VFS_FILE_INFO_DEFAULT) == GNOME_VFS_OK)
	{
		length = info->size;
		gnome_vfs_file_info_unref (info);
		D ("gnomevfs_plugin_get_length: %lld", length);
		return length;
	}

	gnome_vfs_file_info_unref (info);
	return 0;
}

static uint32_t
gnomevfs_plugin_get_blocksize (input_plugin_t *this_gen)
{
	return 8 * 1024;
}

static const char*
gnomevfs_plugin_get_mrl (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	return this->mrl;
}

static int
gnomevfs_plugin_get_optional_data (input_plugin_t *this_gen,
		void *data, int data_type)
{
	D ("input_gnomevfs: get optional data, type %08x\n", data_type);

	return INPUT_OPTIONAL_UNSUPPORTED;
}

static void
gnomevfs_plugin_dispose (input_plugin_t *this_gen )
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	if (this->nbc)
	{
		nbc_close (this->nbc);
		this->nbc = NULL;
	}
	if (this->fh)
		gnome_vfs_close (this->fh);
	if (this->mrl)
		g_free (this->mrl);
	if (this->uri)
		gnome_vfs_uri_unref (this->uri);
	g_free (this);
}

static int
gnomevfs_plugin_open (input_plugin_t *this_gen )
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	GnomeVFSResult res;

	D("gnomevfs_klass_open: opening '%s'", this->mrl);
	res = gnome_vfs_open_uri (&this->fh, this->uri, GNOME_VFS_OPEN_READ | GNOME_VFS_OPEN_RANDOM);
	if (res != GNOME_VFS_OK)
	{
		int err;

		D("gnomevfs_klass_open: failed to open '%s': %s (%d)", this->mrl, gnome_vfs_result_to_string (res), res);
		switch (res) {
		case GNOME_VFS_ERROR_HOST_NOT_FOUND:
			err = XINE_MSG_UNKNOWN_HOST;
			break;
		case GNOME_VFS_ERROR_NOT_FOUND:
			err = XINE_MSG_FILE_NOT_FOUND;
			break;
		case GNOME_VFS_ERROR_ACCESS_DENIED:
			err = XINE_MSG_PERMISSION_ERROR;
			break;
		default:
			err = XINE_MSG_NO_ERROR;
		}

		if (err != XINE_MSG_NO_ERROR) {
			D("gnomevfs_klass_open: sending error %d", err);
			_x_message(this->stream, err, this->mrl, NULL);
		}
		return 0;
	}

	if (gnomevfs_plugin_get_length (this_gen) == 0) {
		_x_message(this->stream, XINE_MSG_FILE_EMPTY, this->mrl, NULL);
		xine_log (this->stream->xine, XINE_LOG_MSG,
				_("input_file: File empty: >%s<\n"), this->mrl);
		return 0;
	}

	return 1;
}

static void
gnomevfs_klass_dispose (input_class_t *this_gen)
{
	gnomevfs_input_class_t *this = (gnomevfs_input_class_t *) this_gen;

	g_free (this);
}

static const char ignore_scheme[][8] = { "cdda", "file", "http" };

static input_plugin_t *
gnomevfs_klass_get_instance (input_class_t *klass_gen, xine_stream_t *stream,
		const char *mrl)
{
	gnomevfs_input_t *this;
	GnomeVFSURI *uri;
	int i;

	if (mrl == NULL)
		return NULL;
	else if (strstr (mrl, "://") == NULL)
		return NULL;

	D("gnomevfs_klass_get_instance: %s", mrl);

	for (i = 0; i < G_N_ELEMENTS (ignore_scheme); i++) {
		if (strncmp (ignore_scheme[i], mrl, strlen (ignore_scheme[i])) == 0)
				return NULL;
	}

	uri = gnome_vfs_uri_new (mrl);
	if (uri == NULL)
		return NULL;

	D("Creating the structure for stream '%s'", mrl);
	this = g_new0 (gnomevfs_input_t, 1);
	this->stream = stream;
	this->fh = NULL;
	this->mrl = g_strdup (mrl);
	this->uri = uri;
	this->nbc = nbc_init (this->stream);

	this->input_plugin.open              = gnomevfs_plugin_open;
	this->input_plugin.get_capabilities  = gnomevfs_plugin_get_capabilities;
	this->input_plugin.read              = gnomevfs_plugin_read;
	this->input_plugin.read_block        = gnomevfs_plugin_read_block;
	this->input_plugin.seek              = gnomevfs_plugin_seek;
	this->input_plugin.get_current_pos   = gnomevfs_plugin_get_current_pos;
	this->input_plugin.get_length        = gnomevfs_plugin_get_length;
	this->input_plugin.get_blocksize     = gnomevfs_plugin_get_blocksize;
	this->input_plugin.get_mrl           = gnomevfs_plugin_get_mrl;
	this->input_plugin.get_optional_data =
		gnomevfs_plugin_get_optional_data;
	this->input_plugin.dispose           = gnomevfs_plugin_dispose;
	this->input_plugin.input_class       = klass_gen;

	return &this->input_plugin;
}

static void
*init_input_class (xine_t *xine, void *data)
{
	gnomevfs_input_class_t *this;

	xprintf (xine, XINE_VERBOSITY_DEBUG, "gnome_vfs init_input_class\n");

	/* Don't initialise gnome-vfs, only gnome-vfs enabled applications
	 * should be using it */
	if (gnome_vfs_initialized () == FALSE) {
		xprintf (xine, XINE_VERBOSITY_DEBUG, "gnome-vfs not initialised\n");
		return NULL;
	}

	if (!g_thread_supported ())
		g_thread_init (NULL);

	this = g_new0 (gnomevfs_input_class_t, 1);
	this->xine = xine;

	this->input_class.get_instance       = gnomevfs_klass_get_instance;
	this->input_class.identifier         = "gnomevfs";
	this->input_class.description        = N_("gnome-vfs input plugin as shipped with xine");
	this->input_class.get_dir            = NULL;
	this->input_class.get_autoplay_list  = NULL;
	this->input_class.dispose            = gnomevfs_klass_dispose;

	return (input_class_t *) this;
}

static input_info_t input_info_gnomevfs = {
	100                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
	{ PLUGIN_INPUT | PLUGIN_NO_UNLOAD, 18, "gnomevfs", XINE_VERSION_CODE,
		&input_info_gnomevfs, init_input_class },
	{ PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

