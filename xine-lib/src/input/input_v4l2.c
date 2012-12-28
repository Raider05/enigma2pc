/*
* Copyright (C) 2010 the xine project
* Copyright (C) 2010 Trever Fischer <tdfischer@fedoraproject.org>
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
* v4l2 input plugin
*/

#define LOG_MODULE "v4l2"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
/*
#define LOG
*/
#include <xine/input_plugin.h>
#include <xine/xine_plugin.h>
#include <xine/xine_internal.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_SYS_VIDEOIO_H
# include <sys/videoio.h>
#elif defined(HAVE_SYS_VIDEODEV2_H)
# include <sys/videodev2.h>
#else
# include <linux/videodev2.h>
#endif
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>

#ifdef HAVE_LIBV4L2_H
# include <libv4l2.h>
#else
# include <unistd.h>
# include <sys/ioctl.h>
# define v4l2_open(f,d)		open(f,d)
# define v4l2_ioctl(f,c,a)	ioctl(f,c,a)
# define v4l2_mmap(p,l,d,m,f,o)	mmap(p,l,d,m,f,o)
# define v4l2_munmap(s,l)	munmap(s,l)
# define v4l2_close(f)		close(f)
#endif

typedef struct  {
    void *start;
    size_t length;
} buffer_data;

typedef struct {
    int width;
    int height;
} resolution_t;

typedef struct {
    buffer_data *buffers;
    int bufcount;
    resolution_t resolution;
    struct v4l2_buffer inbuf;
    off_t index;
    int headerSent;
} v4l2_video_t;

typedef struct {
    buffer_data *buffers;
    int bufcount;
} v4l2_radio_t;

typedef struct {
    input_plugin_t input_plugin;

    int fd;
    char* mrl;
    struct v4l2_capability cap;
    xine_stream_t *stream;

    xine_event_queue_t *events;
    v4l2_video_t* video;
    v4l2_radio_t* radio;
} v4l2_input_plugin_t;

static int v4l2_input_enqueue_video_buffer(v4l2_input_plugin_t *this, int idx);
static int v4l2_input_dequeue_video_buffer(v4l2_input_plugin_t *this, buf_element_t *input);
static int v4l2_input_setup_video_streaming(v4l2_input_plugin_t *this);


static int v4l2_input_open(input_plugin_t *this_gen) {
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*) this_gen;
    int ret;
    lprintf("Opening %s\n", this->mrl);
	this->fd = v4l2_open(this->mrl, O_RDWR);
	if (this->fd) {
        /* TODO: Clean up this mess */
        this->events = xine_event_new_queue(this->stream);
	ret = v4l2_ioctl(this->fd, VIDIOC_QUERYCAP, &(this->cap));
	if (ret < 0)
	{
	  xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
		   LOG_MODULE": capability query failed: %s\n", strerror (-ret));
	  return 0;
	}
        if (this->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
            this->video = malloc(sizeof(v4l2_video_t));
            this->video->headerSent = 0;
            this->video->bufcount = 0;
        }
        if (this->cap.capabilities & V4L2_CAP_STREAMING) {
            lprintf("Supports streaming. Allocating buffers...\n");
            if (this->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                if (v4l2_input_setup_video_streaming(this)) {
                    lprintf("Video streaming ready.\n");
                    return 1;
                } else {
                    /* TODO: Fallbacks */
		    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
			     LOG_MODULE": video streaming setup failed\n");
                    return 0;
                }
            } else {
                /* TODO: Radio streaming */
		xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
			 LOG_MODULE": sorry, only video is supported for now\n");
                return 0;
            }
        } else {
	    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
		     LOG_MODULE": device doesn't support streaming - prod the author to support the other methods\n");
            return 0;
        }
    } else {
        return 0;
    }
}

static int v4l2_input_setup_video_streaming(v4l2_input_plugin_t *this) {
    this->video->bufcount = 0;
    struct v4l2_requestbuffers reqbuf;
    unsigned int i;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 25;

    if (-1 == v4l2_ioctl(this->fd, VIDIOC_REQBUFS, &reqbuf)) {
        lprintf("Buffer request failed. Is streaming supported?\n");
        return 0;
    }

    this->video->bufcount = reqbuf.count;
    lprintf("Got %i buffers for stremaing.\n", reqbuf.count);

    this->video->buffers = calloc(this->video->bufcount, sizeof(buffer_data));
    _x_assert(this->video->buffers);
    for (i = 0;i < this->video->bufcount;i++) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = reqbuf.type;
        buffer.memory = reqbuf.memory;
        buffer.index = i;

        if (-1 == v4l2_ioctl(this->fd, VIDIOC_QUERYBUF, &buffer)) {
            lprintf("Couldn't allocate buffer %i\n", i);
            return 0;
        }

        this->video->buffers[i].length = buffer.length;
        this->video->buffers[i].start = (void*)v4l2_mmap(NULL, buffer.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            this->fd, buffer.m.offset);
        if (MAP_FAILED == this->video->buffers[i].start) {
            lprintf("Couldn't mmap buffer %i\n", i);
            int j;
            for(j = 0;j<i;j++) {
                v4l2_munmap(this->video->buffers[i].start, this->video->buffers[i].length);
            }
            free(this->video->buffers);
            this->video->bufcount = 0;
            return 0;
        }
	if (v4l2_input_enqueue_video_buffer(this, i) < 0)
	  goto fail;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /* TODO: Other formats? MPEG support? */
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if (-1 == v4l2_ioctl(this->fd, VIDIOC_S_FMT, &fmt))
	goto fail;
    this->video->resolution.width = fmt.fmt.pix.width;
    this->video->resolution.height = fmt.fmt.pix.height;
    if (-1 == v4l2_ioctl(this->fd, VIDIOC_STREAMON, &reqbuf.type))
	goto fail;

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);

    return 1;

  fail:
    lprintf("Couldn't start streaming: %s\n", strerror(errno));
    return 0;
}


static buf_element_t* v4l2_input_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t len) {
    lprintf("Reading block\n");
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*)this_gen;
    buf_element_t *buf = fifo->buffer_pool_alloc(fifo);
    if (!this->video->headerSent) {
        struct timeval tv;
        xine_monotonic_clock(&tv, NULL);
        buf->pts = (int64_t) tv.tv_sec * 90000 + (int64_t) tv.tv_usec * 9 / 100;

        lprintf("Sending video header\n");
        xine_bmiheader bih;
        bih.biSize = sizeof(xine_bmiheader);
	/* HACK: Why do I need to do this and why is it magic? */
        bih.biWidth = this->video->resolution.width*2;
        bih.biHeight = this->video->resolution.height*2;
        lprintf("Getting size of %ix%i\n", this->video->resolution.width, this->video->resolution.height);
        buf->size = sizeof(xine_bmiheader);
        buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_START;
        memcpy(buf->content, &bih, sizeof(xine_bmiheader));
        this->video->headerSent = 1;
        this->video->index = 0;
        buf->type = BUF_VIDEO_YUY2;
    } else {
        lprintf("Sending video frame (sent %zd of %zd)\n", this->video->index, this->video->buffers[this->video->inbuf.index].length);
        /* TODO: Add audio support */
        this->video->headerSent = v4l2_input_dequeue_video_buffer(this, buf);
	if (this->video->headerSent < 0)
	{
	  buf->free_buffer (buf);
	  buf = NULL;
	}
    }
    return buf;
}

static uint32_t v4l2_input_blocksize(input_plugin_t *this_gen) {
    /* HACK */
    return 0;
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*)this_gen;
    if (this->video->headerSent) {
        lprintf("Returning block size of %zu\n",this->video->buffers[0].length);
        return this->video->buffers[0].length;
    } else {
        lprintf("Returning block size of %zu\n",sizeof(xine_bmiheader));
        return sizeof(xine_bmiheader);
    }
}

static int v4l2_input_dequeue_video_buffer(v4l2_input_plugin_t *this, buf_element_t *output)
{
    int ret;

    if (!this->video->index)
    {
	memset (&this->video->inbuf, 0, sizeof (this->video->inbuf));
	this->video->inbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	this->video->inbuf.memory = V4L2_MEMORY_MMAP;
	ret = v4l2_ioctl(this->fd, VIDIOC_DQBUF, &this->video->inbuf);
	if (ret < 0)
	  return -1; /* failure */
	output->decoder_flags = BUF_FLAG_FRAME_START;
    }
    else
	output->decoder_flags = 0;

    output->content = output->mem;
    output->type = BUF_VIDEO_YUY2;

    output->size = this->video->buffers[this->video->inbuf.index].length - this->video->index;
    if (output->size > output->max_size)
	output->size = output->max_size;

    xine_fast_memcpy (output->content, (char *)this->video->buffers[this->video->inbuf.index].start + this->video->index, output->size);

    this->video->index += output->size;
    if (this->video->index == this->video->buffers[this->video->inbuf.index].length)
    {
	output->decoder_flags |= BUF_FLAG_FRAME_END;
	ret = v4l2_input_enqueue_video_buffer(this, this->video->inbuf.index);
	return -(ret < 0);
    }

    return 1;
}

static int v4l2_input_enqueue_video_buffer(v4l2_input_plugin_t *this, int idx) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.index = idx;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    return v4l2_ioctl(this->fd, VIDIOC_QBUF, &buf);
}

static void v4l2_input_dispose(input_plugin_t *this_gen) {
    lprintf("Disposing of myself.\n");
    v4l2_input_plugin_t* this = (v4l2_input_plugin_t*)this_gen;

    if (this->video != NULL) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == v4l2_ioctl(this->fd, VIDIOC_STREAMOFF, &type)) {
            lprintf("Couldn't stop streaming. Uh oh.\n");
        }
        if (this->video->bufcount > 0) {
            int i;
            for(i = 0;i<this->video->bufcount;i++) {
                v4l2_munmap(this->video->buffers[i].start, this->video->buffers[i].length);
            }
            free(this->video->buffers);
        }
        free(this->video);
    }
    v4l2_close(this->fd);
    free(this->mrl);
    free(this);
}

static off_t v4l2_input_read(input_plugin_t *this_gen, void *buf, off_t nlen) {
    /* Only block reads are supported. */
    return 0;
}

static uint32_t v4l2_input_get_capabilities(input_plugin_t* this_gen) {
    return INPUT_CAP_BLOCK;
}

static const char* v4l2_input_get_mrl(input_plugin_t* this_gen) {
    /*v4l2_input_plugin_t* this = (v4l2_input_plugin_t*)this_gen;*/
    /* HACK HACK HACK HACK */
    /* So far, the only way to get the yuv_frames demuxer to work with this */
    return "v4l:/";
    //return this->mrl;
}

static int v4l2_input_get_optional_data(input_plugin_t *this_gen, void *data, int data_type) {
    return INPUT_OPTIONAL_UNSUPPORTED;
}

/* Seeking not supported. */
static off_t v4l2_input_seek(input_plugin_t *this_gen, off_t offset, int origin) {
    return -1;
}

static off_t v4l2_input_seek_time(input_plugin_t *this_gen, int time_offset, int origin) {
    return -1;
}

static off_t v4l2_input_pos(input_plugin_t *this_gen) {
    /* TODO */
    return 0;
}

static int v4l2_input_time(input_plugin_t *this_gen) {
    /* TODO */
    return 0;
}

static off_t v4l2_input_length(input_plugin_t *this_gen) {
    return -1;
}

typedef struct {
    input_class_t input_class;
} v4l2_input_class_t;

static input_plugin_t *v4l2_class_get_instance(input_class_t *gen_cls, xine_stream_t *stream, const char *mrl) {
    v4l2_input_plugin_t *this;
    if (strncasecmp (mrl, "v4l2:/", 6))
	return NULL;
    mrl += 5;
    while (*++mrl == '/') /**/;
    --mrl; /* point at the last slash */
    /* TODO: Radio devices */
    /* FIXME: Don't require devices to be of /dev/videoXXX */
    if (strncmp(mrl, "/dev/video", 10) != 0)
        return NULL;
    lprintf("We can handle %s!\n", mrl);

    this = calloc(1, sizeof(v4l2_input_plugin_t));
    _x_assert(this);
    this->mrl = strdup(mrl);
    this->input_plugin.open = v4l2_input_open;
    this->input_plugin.get_capabilities = v4l2_input_get_capabilities;
    this->input_plugin.get_blocksize = v4l2_input_blocksize;
    this->input_plugin.get_mrl = v4l2_input_get_mrl;
    this->input_plugin.dispose = v4l2_input_dispose;
    this->input_plugin.read = v4l2_input_read;
    this->input_plugin.read_block = v4l2_input_read_block;
    this->input_plugin.seek = v4l2_input_seek;
    this->input_plugin.seek_time = v4l2_input_seek_time;
    this->input_plugin.get_current_pos = v4l2_input_pos;
    this->input_plugin.get_current_time = v4l2_input_time;
    this->input_plugin.get_length = v4l2_input_length;
    this->input_plugin.get_optional_data = v4l2_input_get_optional_data;
    this->input_plugin.input_class = gen_cls;
    this->stream = stream;

    this->video = NULL;
    this->radio = NULL;
    lprintf("Ready to read!\n");

    xprintf (this->stream->xine, XINE_VERBOSITY_NONE,
	     LOG_MODULE": WARNING: this plugin is not of release quality\n");

    return &this->input_plugin;
}

static void *v4l2_init_class(xine_t *xine, void *data) {
    v4l2_input_class_t *this;
    this = calloc(1, sizeof(v4l2_input_class_t));
    this->input_class.get_instance = v4l2_class_get_instance;
    this->input_class.description = N_("v4l2 input plugin");
    this->input_class.identifier = "v4l2";
    this->input_class.get_dir = NULL;
    this->input_class.get_autoplay_list = NULL;
    this->input_class.dispose = default_input_class_dispose;
    this->input_class.eject_media = NULL;
    return &this->input_class;
}

const input_info_t input_info_v4l2 = {
    4000
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
    /* type, API, "name", version, special_info, init_function */
    { PLUGIN_INPUT, 18, "v4l2", XINE_VERSION_CODE, &input_info_v4l2, v4l2_init_class },
    { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
