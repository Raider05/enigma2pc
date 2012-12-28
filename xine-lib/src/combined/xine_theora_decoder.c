/*
 * Copyright (C) 2001-2003 the xine project
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
 * xine decoder plugin using libtheora
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>
#include <theora/theora.h>

#define LOG_MODULE "theora_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/metronom.h>
#include <xine/xineutils.h>

typedef struct theora_class_s {
  video_decoder_class_t   decoder_class;
} theora_class_t;

typedef struct theora_decoder_s {
  video_decoder_t    theora_decoder;
  theora_class_t     *class;
  theora_info        t_info;
  theora_comment     t_comment;
  theora_state       t_state;
  ogg_packet         op;
  yuv_buffer         yuv;
  xine_stream_t*     stream;
  int                reject;
  int                op_max_size;
  unsigned char*     packet;
  int                done;
  int                width, height;
  double             ratio;
  int                offset_x, offset_y;
  int                frame_duration;
  int                skipframes;
  int                hp_read;
  int                initialized;
} theora_decoder_t;

static void readin_op (theora_decoder_t *this, unsigned char* src, int size) {
  if ( this->done+size > this->op_max_size) {
    while (this->op_max_size < this->done+size)
      this->op_max_size=this->op_max_size*2;
    this->packet=realloc(this->packet, this->op_max_size);
    this->op.packet=this->packet;
  }
  xine_fast_memcpy ( this->packet+this->done, src, size);
  this->done=this->done+size;
}

static void yuv2frame(yuv_buffer *yuv, vo_frame_t *frame, int offset_x, int offset_y) {
  int i;
  int crop_offset;

  /* fixme - direct rendering (exchaning pointers) may be possible.
   * frame->base[0] = yuv->y could work if one could change the
   * pitches[0,1,2] values, and rely on the drawing routine using
   * the new pitches. With cropping and offsets, it's a bit trickier,
   * but it would still be possible.
   * Attempts at doing this have yielded nothing but SIGSEVs so far.
   */

  /* Copy yuv data onto the frame. Cropping and offset as specified
   * by the frame_width, frame_height, offset_x and offset_y fields
   * in the theora header is carried out.
   */

  crop_offset=offset_x+yuv->y_stride*offset_y;
  for(i=0;i<frame->height;i++)
    xine_fast_memcpy(frame->base[0]+frame->pitches[0]*i,
		     yuv->y+crop_offset+yuv->y_stride*i,
		     frame->width);

  crop_offset=(offset_x/2)+(yuv->uv_stride)*(offset_y/2);
  for(i=0;i<frame->height/2;i++){
    xine_fast_memcpy(frame->base[1]+frame->pitches[1]*i,
		     yuv->u+crop_offset+yuv->uv_stride*i,
		     frame->width/2);
    xine_fast_memcpy(frame->base[2]+frame->pitches[2]*i,
		     yuv->v+crop_offset+yuv->uv_stride*i,
		     frame->width/2);

  }
}

static int collect_data (theora_decoder_t *this, buf_element_t *buf ) {
  /* Assembles an ogg_packet which was sent with send_ogg_packet over xinebuffers */
  /* this->done, this->rejected, this->op and this->decoder->flags are needed*/

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    this->done=0;  /*start from the beginnig*/
    this->reject=0;/*new packet - new try*/

    /*copy the ogg_packet struct and the sum, correct the adress of the packet*/
    xine_fast_memcpy (&this->op, buf->content, sizeof(ogg_packet));
    this->op.packet=this->packet;

    readin_op (this, buf->content + sizeof(ogg_packet), buf->size - sizeof(ogg_packet) );
    /*read the rest of the data*/

  } else {
    if (this->done==0 || this->reject) {
      /*we are starting to collect an packet without the beginnig
	reject the rest*/
      printf ("libtheora: rejecting packet\n");
      this->reject=1;
      return 0;
    }
    readin_op (this, buf->content, buf->size );
  }

  if ((buf->decoder_flags & BUF_FLAG_FRAME_END) && !this->reject) {
    if ( this->done != this->op.bytes ) {
      printf ("libtheora: A packet changed its size during transfer - rejected\n");
      printf ("           size %d    should be %ld\n", this->done , this->op.bytes);
      this->op.bytes=this->done;
    }
    return 1;
  }
  return 0;
}

static void theora_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  /*
   * decode data from buf and feed decoded frames to
   * video output
   */
  theora_decoder_t *this = (theora_decoder_t *) this_gen;
  vo_frame_t *frame;
  yuv_buffer yuv;
  int ret;

  if (!collect_data(this, buf)) return;
  /*return, until a entire packets is collected*/

  if ( (buf->decoder_flags & BUF_FLAG_HEADER) &&
       !(buf->decoder_flags & BUF_FLAG_STDHEADER) ) {
    /*get the first 3 packets and decode the header during preview*/

    if (this->hp_read==0) {
      /*decode first hp*/
      if (theora_decode_header(&this->t_info, &this->t_comment, &this->op)>=0) {
	this->hp_read++;
	return;
      }
    }

    if (this->hp_read==1) {
      /*decode three header packets*/
      if (theora_decode_header(&this->t_info, &this->t_comment,&this->op)) {
	printf ("libtheora: Was unable to decode header #%d, corrupt stream?\n",this->hp_read);
      } else {
	this->hp_read++;
	return;
      }
    }

    if (this->hp_read==2) {
      if (theora_decode_header(&this->t_info, &this->t_comment,&this->op)) {
	printf ("libtheora: Was unable to decode header #%d, corrupt stream?\n",this->hp_read);
      }
      /*headers are now decoded. initialize the decoder*/
      theora_decode_init (&this->t_state, &this->t_info);

      lprintf("theora stream is Theora %dx%d %.02f fps video.\n"
	      "           frame content is %dx%d with offset (%d,%d).\n"
	      "           pixel aspect is %d:%d.\n",
	      this->t_info.width,this->t_info.height,
	      (double)this->t_info.fps_numerator/this->t_info.fps_denominator,
	      this->t_info.frame_width, this->t_info.frame_height,
	      this->t_info.offset_x, this->t_info.offset_y,
	      this->t_info.aspect_numerator, this->t_info.aspect_denominator);

      this->frame_duration=((int64_t)90000*this->t_info.fps_denominator)/this->t_info.fps_numerator;
      this->width=this->t_info.frame_width;
      this->height=this->t_info.frame_height;
      if (this->t_info.aspect_numerator==0 || this->t_info.aspect_denominator==0)
	/* 0-values are undefined, so don't do any scaling.  */
	this->ratio=(double)this->width/(double)this->height;
      else
	/* Yes, this video needs to be scaled.  */
	this->ratio=(double)(this->width*this->t_info.aspect_numerator) /
	  (double)(this->height*this->t_info.aspect_denominator);
      this->offset_x=this->t_info.offset_x;
      this->offset_y=this->t_info.offset_y;
      this->initialized=1;
      this->hp_read++;
    }

  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {
    /*ignore headerpackets*/

    return;

  } else {
    /*decode videodata*/

    if (!this->initialized) {
      printf ("libtheora: cannot decode stream without header\n");
      return;
    }

    ret=theora_decode_packetin( &this->t_state, &this->op);

    if ( ret!=0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, "libtheora:Received an bad packet\n");
    } else if (!this->skipframes) {

      theora_decode_YUVout(&this->t_state,&yuv);

      /*fixme - aspectratio from theora is not considered*/
      frame = this->stream->video_out->get_frame( this->stream->video_out,
						  this->width, this->height,
						  this->ratio,
						  XINE_IMGFMT_YV12,
						  VO_BOTH_FIELDS);
      yuv2frame(&yuv, frame, this->offset_x, this->offset_y);

      frame->pts = buf->pts;
      frame->duration=this->frame_duration;
      this->skipframes=frame->draw(frame, this->stream);
      frame->free(frame);
    } else {
      this->skipframes=this->skipframes-1;
    }
  }
}


static void theora_flush (video_decoder_t *this_gen) {
  /*
   * flush out any frames that are still stored in the decoder
   */
  theora_decoder_t *this = (theora_decoder_t *) this_gen;
  this->skipframes=0;
}

static void theora_reset (video_decoder_t *this_gen) {
  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
  theora_decoder_t *this = (theora_decoder_t *) this_gen;
  this->skipframes=0;
}

static void theora_discontinuity (video_decoder_t *this_gen) {
  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  theora_decoder_t *this = (theora_decoder_t *) this_gen;
  this->skipframes=0;
}

static void theora_dispose (video_decoder_t *this_gen) {
  /*
   * close down, free all resources
   */

  theora_decoder_t *this = (theora_decoder_t *) this_gen;

  lprintf ("dispose \n");

  theora_clear (&this->t_state);
  theora_comment_clear (&this->t_comment);
  theora_info_clear (&this->t_info);
  this->stream->video_out->close(this->stream->video_out, this->stream);
  free (this->packet);
  free (this);
}

static video_decoder_t *theora_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  /*
   * open a new instance of this plugin class
   */

  theora_decoder_t  *this ;

  this = (theora_decoder_t *) calloc(1, sizeof(theora_decoder_t));

  this->theora_decoder.decode_data   = theora_decode_data;
  this->theora_decoder.flush         = theora_flush;
  this->theora_decoder.reset         = theora_reset;
  this->theora_decoder.discontinuity = theora_discontinuity;
  this->theora_decoder.dispose       = theora_dispose;

  this->stream                       = stream;
  this->class                        = (theora_class_t *) class_gen;

  this->op_max_size                  = 4096;
  this->packet                       = malloc(this->op_max_size);

  this->done                         = 0;

  this->stream                       = stream;

  this->initialized                  = 0;

  theora_comment_init (&this->t_comment);
  theora_info_init (&this->t_info);
  (stream->video_out->open) (stream->video_out, stream);

  return &this->theora_decoder;

}

/*
 * theora plugin class
 */
void *theora_init_plugin (xine_t *xine, void *data) {
  /*initialize our plugin*/
  theora_class_t *this;

  this = (theora_class_t *) calloc(1, sizeof(theora_class_t));

  this->decoder_class.open_plugin     = theora_open_plugin;
  this->decoder_class.identifier      = "theora video";
  this->decoder_class.description     = N_("theora video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_THEORA, 0 };

const decoder_info_t dec_info_theora = {
  supported_types,   /* supported types */
  5                        /* priority        */
};
