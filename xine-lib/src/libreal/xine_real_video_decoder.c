/*
 * Copyright (C) 2000-2008 the xine project
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
 * thin layer to use real binary-only codecs in xine
 *
 * code inspired by work from Florian Schneider for the MPlayer Project
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#define LOG_MODULE "real_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/
#include "bswap.h"
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#include "real_common.h"

typedef struct {
  video_decoder_class_t   decoder_class;

  /* empty so far */
} real_class_t;

#define BUF_SIZE       65536

typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *rv_handle;

  uint32_t        (*rvyuv_custom_message)(void*, void*);
  uint32_t        (*rvyuv_free)(void*);
  uint32_t        (*rvyuv_hive_message)(uint32_t, void*);
  uint32_t        (*rvyuv_init)(void*, void*); /* initdata,context */
  uint32_t        (*rvyuv_transform)(char*, char*, void*, void*, void*);

  void            *context;

  uint32_t         width, height;
  double           ratio;
  double           fps;

  uint8_t         *chunk_buffer;
  int              chunk_buffer_size;
  int              chunk_buffer_max;

  int64_t          pts;
  int              duration;

  uint8_t         *frame_buffer;
  int              frame_size;
  int              decoder_ok;

} realdec_decoder_t;

/* we need exact positions */
typedef struct {
  int16_t  unk1;
  int16_t  w;
  int16_t  h;
  int16_t  unk3;
  int32_t  unk2;
  int32_t  subformat;
  int32_t  unk5;
  int32_t  format;
} rv_init_t;

/*
 * Structures for data packets.  These used to be tables of unsigned ints, but
 * that does not work on 64 bit platforms (e.g. Alpha).  The entries that are
 * pointers get truncated.  Pointers on 64 bit platforms are 8 byte longs.
 * So we have to use structures so the compiler will assign the proper space
 * for the pointer.
 */
typedef struct cmsg_data_s {
        uint32_t data1;
        uint32_t data2;
        uint32_t* dimensions;
} cmsg_data_t;

typedef struct transform_in_s {
        uint32_t len;
        uint32_t interpolate;
        uint32_t nsegments;
        void    *segments;
        uint32_t flags;
        uint32_t timestamp;
} transform_in_t;

typedef struct {
        uint32_t frames;
        uint32_t notes;
        uint32_t timestamp;
        uint32_t width;
        uint32_t height;
} transform_out_t;

/*
 * real codec loader
 */

static int load_syms_linux (realdec_decoder_t *this, const char *codec_name, const char *const codec_alternate) {
  cfg_entry_t* entry =
    this->stream->xine->config->lookup_entry(this->stream->xine->config,
					     "decoder.external.real_codecs_path");

  if ( (this->rv_handle = _x_real_codec_open(this->stream, entry->str_value, codec_name, codec_alternate)) == NULL )
    return 0;

  this->rvyuv_custom_message = dlsym (this->rv_handle, "RV20toYUV420CustomMessage");
  this->rvyuv_free           = dlsym (this->rv_handle, "RV20toYUV420Free");
  this->rvyuv_hive_message   = dlsym (this->rv_handle, "RV20toYUV420HiveMessage");
  this->rvyuv_init           = dlsym (this->rv_handle, "RV20toYUV420Init");
  this->rvyuv_transform      = dlsym (this->rv_handle, "RV20toYUV420Transform");

  if (this->rvyuv_custom_message &&
      this->rvyuv_free &&
      this->rvyuv_hive_message &&
      this->rvyuv_init &&
      this->rvyuv_transform)
    return 1;

  this->rvyuv_custom_message = dlsym (this->rv_handle, "RV40toYUV420CustomMessage");
  this->rvyuv_free           = dlsym (this->rv_handle, "RV40toYUV420Free");
  this->rvyuv_hive_message   = dlsym (this->rv_handle, "RV40toYUV420HiveMessage");
  this->rvyuv_init           = dlsym (this->rv_handle, "RV40toYUV420Init");
  this->rvyuv_transform      = dlsym (this->rv_handle, "RV40toYUV420Transform");

  if (this->rvyuv_custom_message &&
      this->rvyuv_free &&
      this->rvyuv_hive_message &&
      this->rvyuv_init &&
      this->rvyuv_transform)
    return 1;

  xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	   _("libreal: Error resolving symbols! (version incompatibility?)\n"));
  return 0;
}

static int init_codec (realdec_decoder_t *this, buf_element_t *buf) {

  /* unsigned int* extrahdr = (unsigned int*) (buf->content+28); */
  int           result;
  rv_init_t     init_data = {11, 0, 0, 0, 0, 0, 1, 0}; /* rv30 */

  switch (buf->type) {
  case BUF_VIDEO_RV20:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 2.0");
    if (!load_syms_linux (this, "drv2.so", "drv2.so.6.0"))
      return 0;
    break;
  case BUF_VIDEO_RV30:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 3.0");
    if (!load_syms_linux (this, "drvc.so", "drv3.so.6.0"))
      return 0;
    break;
  case BUF_VIDEO_RV40:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 4.0");
    if (!load_syms_linux(this, "drvc.so", "drv3.so.6.0"))
      return 0;
    break;
  default:
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "libreal: error, i don't handle buf type 0x%08x\n", buf->type);
    _x_abort();
  }

  init_data.w = _X_BE_16(&buf->content[12]);
  init_data.h = _X_BE_16(&buf->content[14]);

  this->width  = (init_data.w + 1) & (~1);
  this->height = (init_data.h + 1) & (~1);

  if(buf->decoder_flags & BUF_FLAG_ASPECT)
    this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
  else
    this->ratio  = (double)this->width / (double)this->height;

  /* While the framerate is stored in the header it sometimes doesn't bear
   * much resemblence to the actual frequency of frames in the file. Hence
   * it's better to just let the engine estimate the frame duration for us */
#if 0
  this->fps      = (double) _X_BE_16(&buf->content[22]) +
                   ((double) _X_BE_16(&buf->content[24]) / 65536.0);
  this->duration = 90000.0 / this->fps;
#endif

  lprintf("this->ratio=%f\n", this->ratio);

  lprintf ("init_data.w=%d(0x%x), init_data.h=%d(0x%x),"
	   "this->width=%d(0x%x), this->height=%d(0x%x)\n",
	   init_data.w, init_data.w,
	   init_data.h, init_data.h,
	   this->width, this->width, this->height, this->height);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->duration);

  init_data.subformat = _X_BE_32(&buf->content[26]);
  init_data.format    = _X_BE_32(&buf->content[30]);

#ifdef LOG
  printf ("libreal: init_data for rvyuv_init:\n");
  xine_hexdump ((char *) &init_data, sizeof (init_data));

  printf ("libreal: buf->content\n");
  xine_hexdump (buf->content, buf->size);
#endif
  lprintf ("init codec %dx%d... %x %x\n",
	   init_data.w, init_data.h,
	   init_data.subformat, init_data.format );

  this->context = NULL;

  result = this->rvyuv_init (&init_data, &this->context);

  lprintf ("init result: %d\n", result);

  /* setup rv30 codec (codec sub-type and image dimensions): */
  if ((init_data.format>=0x20200002) && (buf->type != BUF_VIDEO_RV40)) {
    int       i, j;
    uint32_t  cmsg24[(buf->size - 34 + 2) * sizeof(uint32_t)];
    cmsg_data_t cmsg_data = { 0x24, 1 + ((init_data.subformat >> 16) & 7), &cmsg24[0] };

    cmsg24[0] = this->width;
    cmsg24[1] = this->height;
    for(i = 2, j = 34; j < buf->size; i++, j++)
      cmsg24[i] = 4 * buf->content[j];

#ifdef LOG
    printf ("libreal: CustomMessage cmsg_data:\n");
    xine_hexdump ((uint8_t *) &cmsg_data, sizeof (cmsg_data));
    printf ("libreal: cmsg24:\n");
    xine_hexdump ((uint8_t *) cmsg24, (buf->size - 34 + 2) * sizeof(uint32_t));
#endif

    this->rvyuv_custom_message (&cmsg_data, this->context);
  }

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  this->frame_size   = this->width * this->height;
  this->frame_buffer = xine_xmalloc (this->width * this->height * 3 / 2);

  this->chunk_buffer = calloc(1, BUF_SIZE);
  this->chunk_buffer_max = BUF_SIZE;

  return 1;
}

static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("decode_data, flags=0x%08x, len=%d, pts=%"PRId64" ...\n",
           buf->decoder_flags, buf->size, buf->pts);

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->duration = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
                         this->duration);
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    this->decoder_ok = init_codec (this, buf);
    if( !this->decoder_ok )
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);

  } else if (this->decoder_ok && this->context) {

    /* Each frame starts with BUF_FLAG_FRAME_START and ends with
     * BUF_FLAG_FRAME_END.
     * The last buffer contains the chunk offset table.
     */

    if (!(buf->decoder_flags & BUF_FLAG_SPECIAL)) {

      lprintf ("buffer (%d bytes)\n", buf->size);

      if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
        /* new frame starting */

        this->chunk_buffer_size = 0;
        this->pts = buf->pts;
        lprintf ("new frame starting, pts=%"PRId64"\n", this->pts);
      }

      if ((this->chunk_buffer_size + buf->size) > this->chunk_buffer_max) {
        lprintf("increasing chunk buffer size\n");

        this->chunk_buffer_max *= 2;
        this->chunk_buffer = realloc(this->chunk_buffer, this->chunk_buffer_max);
      }

      xine_fast_memcpy (this->chunk_buffer + this->chunk_buffer_size,
                        buf->content,
                        buf->size);

      this->chunk_buffer_size += buf->size;

    } else {
      /* end of frame, chunk table */

      lprintf ("special buffer (%d bytes)\n", buf->size);

      if (buf->decoder_info[1] == BUF_SPECIAL_RV_CHUNK_TABLE) {

        int            result;
        vo_frame_t    *img;

        transform_out_t transform_out;
	transform_in_t transform_in = {
	  this->chunk_buffer_size,
	    /* length of the packet (sub-packets appended) */
	  0,
	    /* unknown, seems to be unused  */
	  buf->decoder_info[2],
	    /* number of sub-packets - 1 */
	  buf->decoder_info_ptr[2],
	    /* table of sub-packet offsets */
	  0,
	    /* unknown, seems to be unused  */
	  this->pts / 90
	    /* timestamp (the integer value from the stream) */
	};

        lprintf ("chunk table\n");


#ifdef LOG
        printf ("libreal: got %d chunks\n",
                buf->decoder_info[2] + 1);

        printf ("libreal: decoding %d bytes:\n", this->chunk_buffer_size);
        xine_hexdump (this->chunk_buffer, this->chunk_buffer_size);

        printf ("libreal: transform_in:\n");
        xine_hexdump ((uint8_t *) &transform_in, sizeof(transform_in_t));

        printf ("libreal: chunk_table:\n");
        xine_hexdump ((uint8_t *) buf->decoder_info_ptr[2],
                      2*(buf->decoder_info[2]+1)*sizeof(uint32_t));
#endif

        result = this->rvyuv_transform (this->chunk_buffer,
                                        this->frame_buffer,
                                        &transform_in,
                                        &transform_out,
                                        this->context);

        lprintf ("transform result: %08x\n", result);
        lprintf ("transform_out:\n");
#ifdef LOG
        xine_hexdump ((uint8_t *) &transform_out, 5 * 4);
#endif

        /* Sometimes the stream contains video of a different size
         * to that specified in the realmedia header */
        if(transform_out.frames && ((transform_out.width != this->width) ||
                                    (transform_out.height != this->height))) {
          this->width  = transform_out.width;
          this->height = transform_out.height;

          this->frame_size = this->width * this->height;

          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width);
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
        }

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                                  /* this->av_picture.linesize[0],  */
                                                  this->width,
                                                  this->height,
                                                  this->ratio,
                                                  XINE_IMGFMT_YV12,
                                                  VO_BOTH_FIELDS);

        img->pts       = this->pts;
        img->duration  = this->duration;
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->duration);
        img->bad_frame = 0;

        yv12_to_yv12(
         /* Y */
          this->frame_buffer, this->width,
          img->base[0], img->pitches[0],
         /* U */
          this->frame_buffer + this->frame_size, this->width/2,
          img->base[1], img->pitches[1],
         /* V */
          this->frame_buffer + this->frame_size * 5/4, this->width/2,
          img->base[2], img->pitches[2],
         /* width x height */
          this->width, this->height);

        img->draw(img, this->stream);
        img->free(img);

      } else {
        /* unsupported special buf */
      }
    }
  }

  lprintf ("decode_data...done\n");
}

static void realdec_flush (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

  lprintf ("flush\n");
}

static void realdec_reset (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  this->chunk_buffer_size = 0;
}

static void realdec_discontinuity (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  this->pts = 0;
}

static void realdec_dispose (video_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("dispose\n");

  if (this->context)
    this->stream->video_out->close(this->stream->video_out, this->stream);

  if (this->rvyuv_free && this->context)
    this->rvyuv_free (this->context);

  if (this->rv_handle)
    dlclose (this->rv_handle);

  if (this->frame_buffer)
    free (this->frame_buffer);

  if (this->chunk_buffer)
    free (this->chunk_buffer);

  free (this);

  lprintf ("dispose done\n");
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen,
				     xine_stream_t *stream) {

  real_class_t      *cls = (real_class_t *) class_gen;
  realdec_decoder_t *this ;

  this = (realdec_decoder_t *) calloc(1, sizeof(realdec_decoder_t));

  this->video_decoder.decode_data         = realdec_decode_data;
  this->video_decoder.flush               = realdec_flush;
  this->video_decoder.reset               = realdec_reset;
  this->video_decoder.discontinuity       = realdec_discontinuity;
  this->video_decoder.dispose             = realdec_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  this->context    = 0;
  this->pts        = 0;

  this->duration   = 0;

  return &this->video_decoder;
}

/*
 * real plugin class
 */
void *init_realvdec (xine_t *xine, void *data) {

  real_class_t       *this;

  this = (real_class_t *) calloc(1, sizeof(real_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "realvdec";
  this->decoder_class.description     = N_("real binary-only codec based video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  _x_real_codecs_init(xine);

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_RV30,
                                      BUF_VIDEO_RV40,
                                      0 };

const decoder_info_t dec_info_realvideo = {
  supported_types,     /* supported types */
  7                    /* priority        */
};
