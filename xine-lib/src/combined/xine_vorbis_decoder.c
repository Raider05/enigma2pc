/*
 * Copyright (C) 2000-2003 the xine project
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
 * (ogg/)vorbis audio decoder plugin (libvorbis wrapper) for xine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "vorbis_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define MAX_NUM_SAMPLES 4096
#define INIT_BUFSIZE    8192

typedef struct {
  audio_decoder_class_t   decoder_class;
} vorbis_class_t;

typedef struct vorbis_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  ogg_packet        op; /* we must use this struct to sent data to libvorbis */

  /* vorbis stuff */
  vorbis_info       vi; /* stores static vorbis bitstream settings */
  vorbis_comment    vc;
  vorbis_dsp_state  vd; /* central working state for packet->PCM decoder */
  vorbis_block      vb; /* local working state for packet->PCM decoder */

  int16_t           convbuffer[MAX_NUM_SAMPLES];
  int               convsize;

  int               header_count;

  xine_stream_t    *stream;

  /* data accumulation stuff */
  unsigned char    *buf;
  int               bufsize;
  int               size;

} vorbis_decoder_t;


static void vorbis_reset (audio_decoder_t *this_gen) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;

  if( this->header_count ) return;
  this->size = 0;

  /* clear block first, as it might contain allocated data */
  vorbis_block_clear(&this->vb);
  vorbis_block_init(&this->vd,&this->vb);
}

static void vorbis_discontinuity (audio_decoder_t *this_gen) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;

  this->pts=0;
}

/* Known vorbis comment keys from ogg123 sources*/
static const struct {
  const char *key;         /* includes the '=' for programming convenience */
  int   xine_metainfo_index;
} vorbis_comment_keys[] = {
  {"ARTIST=", XINE_META_INFO_ARTIST},
  {"ALBUM=", XINE_META_INFO_ALBUM},
  {"TITLE=", XINE_META_INFO_TITLE},
  {"GENRE=", XINE_META_INFO_GENRE},
  {"DESCRIPTION=", XINE_META_INFO_COMMENT},
  {"COMMENT=", XINE_META_INFO_COMMENT},
  {"DATE=", XINE_META_INFO_YEAR},
  {"TRACKNUMBER=", XINE_META_INFO_TRACK_NUMBER},
  {NULL, 0}
};

static void get_metadata (vorbis_decoder_t *this) {

  char **ptr=this->vc.user_comments;
  while(*ptr){

    char *comment = *ptr;
    int i;

    lprintf("%s\n", comment);

    for (i = 0; vorbis_comment_keys[i].key != NULL; i++) {

      if ( !strncasecmp (vorbis_comment_keys[i].key, comment,
			 strlen(vorbis_comment_keys[i].key)) ) {

	lprintf ("known metadata %d %d\n",
		 i, vorbis_comment_keys[i].xine_metainfo_index);

        _x_meta_info_set_utf8(this->stream, vorbis_comment_keys[i].xine_metainfo_index,
	  comment + strlen(vorbis_comment_keys[i].key));

      }
    }
    ++ptr;
  }

  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "vorbis");
}

static void vorbis_check_bufsize (vorbis_decoder_t *this, int size) {
  if (size > this->bufsize) {
    this->bufsize = size + size / 2;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("vorbis: increasing buffer to %d to avoid overflow.\n"),
	    this->bufsize);
    this->buf = realloc(this->buf, this->bufsize);
  }
}

static void vorbis_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;

  memset( &this->op, 0, sizeof(this->op) );

  /* data accumulation */
  vorbis_check_bufsize(this, this->size + buf->size);
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
    this->op.packet = this->buf;
    this->op.bytes = this->size;

    /* reset accumultaion buffer */
    this->size = 0;

    if ( (buf->decoder_flags & BUF_FLAG_HEADER) &&
        !(buf->decoder_flags & BUF_FLAG_STDHEADER) ) {

      lprintf ("%d headers to go\n", this->header_count);

      if (this->header_count) {
        int res = 0;

        if (this->header_count == 3)
          this->op.b_o_s = 1;

        if ( (res = vorbis_synthesis_headerin(&this->vi,&this->vc,&this->op)) < 0 ) {
          /* error case; not a vorbis header */
          xine_log(this->stream->xine, XINE_LOG_MSG, "libvorbis: this bitstream does not contain vorbis audio data. Following first 64 bytes (return: %d).\n", res);
          xine_hexdump((char *)this->op.packet, this->op.bytes < 64 ? this->op.bytes : 64);
          return;
        }

        this->header_count--;

        if (!this->header_count) {

          int mode = AO_CAP_MODE_MONO;

          get_metadata (this);

          mode = _x_ao_channels2mode(this->vi.channels);

          this->convsize=MAX_NUM_SAMPLES/this->vi.channels;

          if (!this->output_open) {
            this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
                                                      this->stream,
                                                      16,
                                                      this->vi.rate,
                                                      mode) ;

            _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE,
              this->vi.bitrate_nominal);

          }

          /* OK, got and parsed all three headers. Initialize the Vorbis
                  * packet->PCM decoder. */
          lprintf("all three headers parsed. initializing decoder.\n");
          /* initialize central decode state */
          vorbis_synthesis_init(&this->vd,&this->vi);
          /* initialize local state for most of the decode so multiple
            * block decodes can proceed in parallel. We could init
            * multiple vorbis_block structures for vd here */
          vorbis_block_init(&this->vd,&this->vb);
        }
      }

    } else if (this->output_open) {

      float **pcm;
      int samples;

      if(vorbis_synthesis(&this->vb,&this->op)==0)
        vorbis_synthesis_blockin(&this->vd,&this->vb);

      if (buf->pts!=0)
        this->pts=buf->pts;

      while ((samples=vorbis_synthesis_pcmout(&this->vd,&pcm))>0){

        /* **pcm is a multichannel float vector. In stereo, for
        * example, pcm[0][...] is left, and pcm[1][...] is right.
        * samples is the size of each channel. Convert the float
        * values (-1.<=range<=1.) to whatever PCM format and write
        * it out
        */

        int i,j;
        int bout=(samples<this->convsize?samples:this->convsize);
        audio_buffer_t *audio_buffer;

        audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

        /* convert floats to 16 bit signed ints (host order) and
          interleave */
        for(i=0;i<this->vi.channels;i++){
          ogg_int16_t *ptr=audio_buffer->mem+i;
          float  *mono=pcm[i];
          for(j=0;j<bout;j++){
            int val=(mono[j] + 1.0f) * 32768.f;
            val -= 32768;
            /* might as well guard against clipping */
            if(val>32767){
              val=32767;
            } else if(val<-32768){
              val=-32768;
            }
            *ptr=val;
            ptr+=this->vi.channels;
          }
        }

        audio_buffer->vpts       = this->pts;
        this->pts=0;
        audio_buffer->num_frames = bout;

        this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

        buf->pts=0;

        /* tell libvorbis how many samples we actually consumed */
        vorbis_synthesis_read(&this->vd,bout);
      }
    } else {
      lprintf("output not open\n");
    }
  }
}

static void vorbis_dispose (audio_decoder_t *this_gen) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;

  if( !this->header_count ) {
    lprintf("deinitializing decoder\n");

    vorbis_block_clear(&this->vb);
    vorbis_dsp_clear(&this->vd);
  }

  vorbis_comment_clear(&this->vc);

  vorbis_info_clear(&this->vi);  /* must be called last */

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  lprintf("libvorbis instance destroyed\n");

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen,
				     xine_stream_t *stream) {

  vorbis_decoder_t *this ;

  this = (vorbis_decoder_t *) calloc(1, sizeof(vorbis_decoder_t));

  this->audio_decoder.decode_data         = vorbis_decode_data;
  this->audio_decoder.reset               = vorbis_reset;
  this->audio_decoder.discontinuity       = vorbis_discontinuity;
  this->audio_decoder.dispose             = vorbis_dispose;
  this->stream                            = stream;

  this->output_open     = 0;
  this->header_count    = 3;
  this->convsize        = 0;

  this->bufsize         = INIT_BUFSIZE;
  this->buf             = calloc(1, INIT_BUFSIZE);
  this->size            = 0;

  vorbis_info_init(&this->vi);
  vorbis_comment_init(&this->vc);

  lprintf("libvorbis decoder instance created\n");

  return (audio_decoder_t *) this;
}

/*
 * vorbis plugin class
 */
void *vorbis_init_plugin (xine_t *xine, void *data) {

  vorbis_class_t *this;

  this = (vorbis_class_t *) calloc(1, sizeof(vorbis_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "vorbis";
  this->decoder_class.description     = N_("vorbis audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_VORBIS, 0
 };

const decoder_info_t dec_info_vorbis = {
  audio_types,         /* supported types */
  5                    /* priority        */
};
