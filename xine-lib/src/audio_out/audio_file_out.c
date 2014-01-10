/*
 * Copyright (C) 2000-2012 the xine project
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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include "bswap.h"

#define AO_OUT_FILE_IFACE_VERSION 9

#define GAP_TOLERANCE        INT_MAX

#ifdef WIN32
#ifndef S_IWUSR
#define S_IWUSR 0x0000
#endif
#ifndef S_IRGRP
#define S_IRGRP 0x0000
#endif
#ifndef S_IROTH
#define S_IROTH 0x0000
#endif
#endif

/* Taken (hStudlyCapsAndAll) from sox's wavwritehdr */

struct wavhdr {
	unsigned char bRiffMagic[4];	// 'RIFF'
	uint32_t wRiffLength ;		// length of file minus the 8 byte riff header
	unsigned char bWaveMagic[8];	// 'WAVEfmt '
	uint32_t wFmtSize;		// length of format chunk minus 8 byte header
	uint16_t wFormatTag;		// identifies PCM, ULAW etc
	uint16_t wChannels;
	uint32_t dwSamplesPerSecond;	// samples per second per channel
	uint32_t dwAvgBytesPerSec;	// non-trivial for compressed formats
	uint16_t wBlockAlign;		// basic block size
	uint16_t wBitsPerSample;	// non-trivial for compressed formats

	// PCM formats then go straight to the data chunk:
	unsigned char bData[4];		// 'data'
	unsigned long dwDataLength;	// length of data chunk minus 8 byte header
};

typedef struct file_driver_s {
	ao_driver_t    ao_driver;

	xine_t        *xine;

	int            capabilities;
	int            mode;

	int32_t        sample_rate;
	uint32_t       num_channels;
	uint32_t       bits_per_sample;
	uint32_t       bytes_per_frame;

	const char    *fname;
	int            fd;
	size_t         bytes_written;
	struct timeval endtime;
} file_driver_t;

typedef struct {
	audio_driver_class_t  driver_class;

	config_values_t      *config;
	xine_t               *xine;
} file_class_t;

/*
 * open the audio device for writing to
 */
static int ao_file_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	struct wavhdr w;

	xprintf (this->xine, XINE_VERBOSITY_LOG,
		 "audio_file_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

	this->mode                   = mode;
	this->sample_rate            = rate;
	this->bits_per_sample        = bits;

	switch (mode) {
	case AO_CAP_MODE_MONO:
		this->num_channels = 1;
		break;
	case AO_CAP_MODE_STEREO:
		this->num_channels = 2;
		break;
	}
	this->bytes_per_frame        = (this->bits_per_sample*this->num_channels) / 8;

	this->fd = -1;
	this->fname = getenv("XINE_WAVE_OUTPUT");
	if (!this->fname)
		this->fname = "xine-out.wav";

	this->fd = xine_create_cloexec(this->fname, O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

	if (this->fd == -1) {
		xprintf (this->xine, XINE_VERBOSITY_LOG, "audio_file_out: Failed to open file '%s': %s\n",
			 this->fname, strerror(errno));
		return 0;
	}

	w.bRiffMagic[0] = 'R';
	w.bRiffMagic[1] = 'I';
	w.bRiffMagic[2] = 'F';
	w.bRiffMagic[3] = 'F';
	w.wRiffLength = le2me_32(0x7ff00024);
	w.bWaveMagic[0] = 'W';
	w.bWaveMagic[1] = 'A';
	w.bWaveMagic[2] = 'V';
	w.bWaveMagic[3] = 'E';
	w.bWaveMagic[4] = 'f';
	w.bWaveMagic[5] = 'm';
	w.bWaveMagic[6] = 't';
	w.bWaveMagic[7] = ' ';
	w.wFmtSize = le2me_32(0x10);
	w.wFormatTag = le2me_16(1); // PCM;
	w.wChannels = le2me_16(this->num_channels);
	w.dwSamplesPerSecond = le2me_32(this->sample_rate);
	w.dwAvgBytesPerSec = le2me_32(this->sample_rate * this->bytes_per_frame);
	w.wBlockAlign = le2me_16(this->bytes_per_frame);
	w.wBitsPerSample = le2me_16(this->bits_per_sample);
	w.bData[0] = 'd';
	w.bData[1] = 'a';
	w.bData[2] = 't';
	w.bData[3] = 'a';
	w.dwDataLength = le2me_32(0x7ffff000);

	this->bytes_written = 0;
	if (write(this->fd, &w, sizeof(w)) != sizeof(w)) {
		xprintf (this->xine, XINE_VERBOSITY_LOG, "audio_file_out: Failed to write WAVE header to file '%s': %s\n",
			 this->fname, strerror(errno));
		close(this->fd);
		this->fd = -1;
		return 0;
	}
	xine_monotonic_clock(&this->endtime, NULL);
	return this->sample_rate;
}


static int ao_file_num_channels(ao_driver_t *this_gen)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	return this->num_channels;
}

static int ao_file_bytes_per_frame(ao_driver_t *this_gen)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	return this->bytes_per_frame;
}

static int ao_file_get_gap_tolerance (ao_driver_t *this_gen)
{
	return GAP_TOLERANCE;
}

static int ao_file_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	size_t len = num_frames * this->bytes_per_frame;
	unsigned long usecs;

#ifdef WORDS_BIGENDIAN
	/* Eep. .WAV format is little-endian. We need to swap.
	   Remind me why I picked this output format again? */
	if (this->bits_per_sample == 16) {
		int i;
		for (i=0; i<len/2; i++)
			data[i] = bswap_16(data[i]);
	} else if (this->bits_per_sample == 32) {
		int i;
		uint32_t *d32 = (void *)data;
		for (i=0; i<len/4; i++)
			d32[i] = bswap_16(d32[i]);
	}
#endif
	while(len) {
		ssize_t thislen = write(this->fd, data, len);

		if (thislen == -1) {
			xprintf (this->xine, XINE_VERBOSITY_LOG, "audio_file_out: Failed to write data to file '%s': %s\n",
				 this->fname, strerror(errno));
			return -1;
		}
		len -= thislen;
		this->bytes_written += thislen;
	}

	/* Delay for an appropriate amount of time to prevent padding */
	usecs = ((10000 * num_frames / (this->sample_rate/100)));

	this->endtime.tv_usec += usecs;
	while (this->endtime.tv_usec > 1000000) {
		this->endtime.tv_usec -= 1000000;
		this->endtime.tv_sec++;
	}
	return 1;
}


static int ao_file_delay (ao_driver_t *this_gen)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	struct timeval now;
	unsigned long tosleep;

	/* Work out how long we need to sleep for, and how much
	   time we've already taken */
	xine_monotonic_clock(&now, NULL);

	if (now.tv_sec > this->endtime.tv_sec) {
		/* We slipped. Compensate */
		this->endtime = now;
		return 0;
	}
	if (now.tv_sec == this->endtime.tv_sec &&
	    now.tv_usec >= this->endtime.tv_usec)
		return 0;

	tosleep = this->endtime.tv_sec - now.tv_sec;
	tosleep *= 1000000;
	tosleep += this->endtime.tv_usec - now.tv_usec;

	xine_usec_sleep(tosleep);
	return 0;
}

static void ao_file_close(ao_driver_t *this_gen)
{
	file_driver_t *this = (file_driver_t *) this_gen;
	uint32_t len;

	len = le2me_32(this->bytes_written);
	xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_file_out: Close file '%s'. %zu KiB written\n",
		 this->fname, this->bytes_written / 1024);

	if (lseek(this->fd, 40, SEEK_SET) != -1) {
		if (write(this->fd, &len, 4) != 4) {
			xprintf (this->xine, XINE_VERBOSITY_LOG, "audio_file_out: Failed to write header to file '%s': %s\n",
                                 this->fname, strerror(errno));
		}

		len = le2me_32(this->bytes_written + 0x24);
		if (lseek(this->fd, 4, SEEK_SET) != -1) {
			if (write(this->fd, &len, 4) != 4) {
				xprintf (this->xine, XINE_VERBOSITY_LOG,
                                         "audio_file_out: Failed to write header to file '%s': %s\n",
                                         this->fname, strerror(errno));
                        }
		}

	}

	close(this->fd);
	this->fd = -1;
}

static uint32_t ao_file_get_capabilities (ao_driver_t *this_gen) {
	file_driver_t *this = (file_driver_t *) this_gen;
	return this->capabilities;
}

static void ao_file_exit(ao_driver_t *this_gen)
{
	file_driver_t *this = (file_driver_t *) this_gen;

	if (this->fd != -1)
		ao_file_close(this_gen);

	free (this);
}

static int ao_file_get_property (ao_driver_t *this_gen, int property) {

	return 0;
}

static int ao_file_set_property (ao_driver_t *this_gen, int property, int value) {

	return ~value;
}

static int ao_file_ctrl(ao_driver_t *this_gen, int cmd, ...) {
	/*file_driver_t *this = (file_driver_t *) this_gen;*/

	switch (cmd) {

	case AO_CTRL_PLAY_PAUSE:
		break;

	case AO_CTRL_PLAY_RESUME:
		break;

	case AO_CTRL_FLUSH_BUFFERS:
		break;
	}

	return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen,
				 const void *data) {

	file_class_t     *class = (file_class_t *) class_gen;
	/* config_values_t *config = class->config; */
	file_driver_t    *this;

	lprintf ("open_plugin called\n");

	this = calloc(1, sizeof (file_driver_t));
	if (!this)
		return NULL;

	this->xine = class->xine;
	this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;

	this->sample_rate  = 0;

	this->ao_driver.get_capabilities    = ao_file_get_capabilities;
	this->ao_driver.get_property        = ao_file_get_property;
	this->ao_driver.set_property        = ao_file_set_property;
	this->ao_driver.open                = ao_file_open;
	this->ao_driver.num_channels        = ao_file_num_channels;
	this->ao_driver.bytes_per_frame     = ao_file_bytes_per_frame;
	this->ao_driver.delay               = ao_file_delay;
	this->ao_driver.write               = ao_file_write;
	this->ao_driver.close               = ao_file_close;
	this->ao_driver.exit                = ao_file_exit;
	this->ao_driver.get_gap_tolerance   = ao_file_get_gap_tolerance;
	this->ao_driver.control	      = ao_file_ctrl;

	this->fd = -1;

	return &this->ao_driver;
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

	file_class_t        *this;

	lprintf ("init class\n");

	this = calloc(1, sizeof (file_class_t));
	if (!this)
		return NULL;

	this->driver_class.open_plugin     = open_plugin;
	this->driver_class.identifier      = "file";
	this->driver_class.description     = N_("xine file audio output plugin");
	this->driver_class.dispose         = default_audio_driver_class_dispose;

	this->config = xine->config;
	this->xine   = xine;

	return this;
}

static const ao_info_t ao_info_file = {
	-1 /* do not auto probe this one */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
	/* type, API, "name", version, special_info, init_function */
	{ PLUGIN_AUDIO_OUT, AO_OUT_FILE_IFACE_VERSION, "file", XINE_VERSION_CODE, &ao_info_file, init_class },
	{ PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

