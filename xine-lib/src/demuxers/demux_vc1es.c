/*
 * Copyright (C) 2008 the xine project
 * Copyright (C) 2008 Christophe Thommeret <hftom@free.fr>
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

/*
 * demultiplexer for wmv9/vc1 elementary streams
 *
 *
 *    SMP (.rcv) format:
 *
 *    ** header ***
 *    le24 number of frames
 *    C5 04 00 00 00
 *    4 bytes sequence header
 *    le32 height
 *    le32 width
 *    0C 00 00 00
 *    8 bytes unknown
 *    le32 fps
 *    ************
 *    le24 frame_size
 *    80
 *    le32 pts (ms)
 *    frame_size bytes of picture data
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* #define LOG */
#define LOG_MODULE "demux_vc1es"
#define LOG_VERBOSE

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"

#define SCRATCH_SIZE 36
#define PRIVATE_SIZE 44

#define MODE_SMP 1
#define MODE_AP  2



typedef struct {
  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;
  int                  mode;
  int                  first_chunk;
  uint8_t              private[PRIVATE_SIZE];
  uint32_t             video_step;

  uint32_t             blocksize;
} demux_vc1_es_t ;



typedef struct {
  demux_class_t     demux_class;
} demux_vc1_es_class_t;



static int demux_vc1_es_next_smp( demux_vc1_es_t *this )
{
  buf_element_t *buf;
  uint32_t pts=0, frame_size=0;
  off_t done;
  uint8_t head[SCRATCH_SIZE];
  int start_flag = 1;

  if ( this->first_chunk ) {
    this->input->read( this->input, head, SCRATCH_SIZE );
    this->first_chunk = 0;
  }

  done = this->input->read( this->input, head, 8 );
  frame_size = _X_LE_24( head );
  pts = _X_LE_32( head+4 );

  done = 0;
  while ( frame_size>0 ) {
    buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    off_t read = (frame_size>buf->max_size) ? buf->max_size : frame_size;
    done = this->input->read( this->input, buf->mem, read );
    if ( done<=0 ) {
      buf->free_buffer( buf );
      this->status = DEMUX_FINISHED;
      return 0;
    }
    buf->size = done;
    buf->content = buf->mem;
    buf->type = BUF_VIDEO_WMV9;
    buf->pts = pts*90;
    frame_size -= done;
    if ( start_flag ) {
      buf->decoder_flags = BUF_FLAG_FRAME_START;
      start_flag = 0;
    }
    if ( !(frame_size>0) )
      buf->decoder_flags = BUF_FLAG_FRAME_END;
    this->video_fifo->put(this->video_fifo, buf);
  }

  return 1;
}



static int demux_vc1_es_next_ap( demux_vc1_es_t *this )
{
  buf_element_t *buf;
  uint32_t blocksize;
  off_t done;

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  blocksize = (this->blocksize ? this->blocksize : buf->max_size);
  done = this->input->read(this->input, buf->mem, blocksize);

  if (done <= 0) {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->size = done;
  buf->content = buf->mem;
  buf->pts = 0;
  buf->type = BUF_VIDEO_VC1;

  if( this->input->get_length (this->input) )
    buf->extra_info->input_normpos = (int)( (double)this->input->get_current_pos( this->input )*65535/this->input->get_length( this->input ) );

  this->video_fifo->put(this->video_fifo, buf);

  return 1;
}



static int demux_vc1_es_send_chunk( demux_plugin_t *this_gen )
{
  demux_vc1_es_t *this = (demux_vc1_es_t *) this_gen;

  if ( this->mode==MODE_SMP ) {
    if (!demux_vc1_es_next_smp(this))
      this->status = DEMUX_FINISHED;
    return this->status;
  }

  if (!demux_vc1_es_next_ap(this))
    this->status = DEMUX_FINISHED;
  return this->status;
}



static int demux_vc1_es_get_status( demux_plugin_t *this_gen )
{
  demux_vc1_es_t *this = (demux_vc1_es_t *) this_gen;

  return this->status;
}



static void demux_vc1_es_send_headers( demux_plugin_t *this_gen )
{
  demux_vc1_es_t *this = (demux_vc1_es_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
  _x_demux_control_start(this->stream);
  this->blocksize = this->input->get_blocksize(this->input);
  this->status = DEMUX_OK;

  if ( this->mode==MODE_SMP ) {
    buf_element_t *buf;
    buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    xine_fast_memcpy( buf->mem, this->private, PRIVATE_SIZE );
    buf->size = PRIVATE_SIZE;
    buf->content = buf->mem;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
    if ( this->video_step ) {
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = 90000/this->video_step;
    }
    buf->type = BUF_VIDEO_WMV9;
    this->video_fifo->put(this->video_fifo, buf);
  }
}



static int demux_vc1_es_seek( demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing )
{
  demux_vc1_es_t *this = (demux_vc1_es_t *) this_gen;

  if ( this->mode==MODE_SMP ) {
    this->status = DEMUX_OK;
    return this->status;
  }

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  this->status = DEMUX_OK;

  if (playing)
    _x_demux_flush_engine(this->stream);

  if (INPUT_IS_SEEKABLE(this->input)) {

    /* FIXME: implement time seek */

    if (start_pos != this->input->seek (this->input, start_pos, SEEK_SET)) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    lprintf ("seeking to %"PRId64"\n", start_pos);
  }

  /*
   * now start demuxing
   */
  this->status = DEMUX_OK;

  return this->status;
}



static void demux_vc1_es_dispose( demux_plugin_t *this )
{
  free (this);
}



static int demux_vc1_es_get_stream_length( demux_plugin_t *this_gen )
{
  return 0 ; /*FIXME: implement */
}



static uint32_t demux_vc1_es_get_capabilities( demux_plugin_t *this_gen )
{
  return DEMUX_CAP_NOCAP;
}



static int demux_vc1_es_get_optional_data( demux_plugin_t *this_gen, void *data, int data_type )
{
  return DEMUX_OPTIONAL_UNSUPPORTED;
}



static demux_plugin_t *open_plugin( demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input )
{

  demux_vc1_es_t *this;
  uint8_t scratch[SCRATCH_SIZE];
  int i, read, found=0;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    read = _x_demux_read_header(input, scratch, SCRATCH_SIZE);
    if (!read)
      return NULL;
    lprintf("read size =%d\n",read);

    /* simple and main profiles */
    if ( read>=SCRATCH_SIZE ) {
      lprintf("searching for rcv format..\n");
      if ( scratch[3]==0xc5 && scratch[4]==4 && scratch[5]==0 && scratch[6]==0 && scratch[7]==0 && scratch[20]==0x0c && scratch[21]==0 && scratch[22]==0 && scratch[23]==0 ) {
        lprintf("rcv format found\n");
        found = MODE_SMP;
      }
    }

    if ( found==0 ) {
      /* advanced profile */
      for (i = 0; i < read-4; i++) {
        lprintf ("%02x %02x %02x %02x\n", scratch[i], scratch[i+1], scratch[i+2], scratch[i+3]);
        if ((scratch[i] == 0x00) && (scratch[i+1] == 0x00) && (scratch[i+2] == 0x01)) {
          if (scratch[i+3] == 0x0f) {
            found = MODE_AP;
            lprintf ("found header at offset 0x%x\n", i);
            break;
          }
        }
      }
    }

    if (found == 0)
      return NULL;
    lprintf ("input accepted.\n");
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  this = calloc(1, sizeof(demux_vc1_es_t));
  this->mode = found;
  this->first_chunk = 1;
  if ( found==MODE_SMP ) {
    xine_fast_memcpy( this->private+8, scratch+12, 4 ); /* height */
    xine_fast_memcpy( this->private+4, scratch+16, 4 ); /* width */
    xine_fast_memcpy( this->private+40, scratch+8, 4 ); /* sequence header */
    this->video_step = _X_LE_32( scratch+32 );
  }
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_vc1_es_send_headers;
  this->demux_plugin.send_chunk        = demux_vc1_es_send_chunk;
  this->demux_plugin.seek              = demux_vc1_es_seek;
  this->demux_plugin.dispose           = demux_vc1_es_dispose;
  this->demux_plugin.get_status        = demux_vc1_es_get_status;
  this->demux_plugin.get_stream_length = demux_vc1_es_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_vc1_es_get_capabilities;
  this->demux_plugin.get_optional_data = demux_vc1_es_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  return &this->demux_plugin;
}



static void *init_plugin( xine_t *xine, void *data )
{
  demux_vc1_es_class_t     *this;

  this = calloc(1, sizeof(demux_vc1_es_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("VC1 elementary stream demux plugin");
  this->demux_class.identifier      = "VC1_ES";
  this->demux_class.mimetypes       = NULL;
  this->demux_class.extensions      = "";
  this->demux_class.dispose         = default_demux_class_dispose;

  return this;
}


/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_vc1es = {
  0                       /* priority */
};



const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "vc1es", XINE_VERSION_CODE, &demux_info_vc1es, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
