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

#define LOG_MODULE "real_audio_decoder"
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
  audio_decoder_class_t   decoder_class;

  /* empty so far */
} real_class_t;

typedef void * ra_codec_t;

typedef struct realdec_decoder_s {
  audio_decoder_t  audio_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *ra_handle;

  uint32_t       (*raCloseCodec)(ra_codec_t);
  uint32_t       (*raDecode)(ra_codec_t, char *, uint32_t, char *, uint32_t *, uint32_t);
  uint32_t       (*raFlush)(ra_codec_t, char *, uint32_t *);
  uint32_t       (*raFreeDecoder)(ra_codec_t);
  void *         (*raGetFlavorProperty)(ra_codec_t, uint16_t, uint16_t, uint16_t *);
  uint32_t       (*raInitDecoder)(ra_codec_t, void *);
  uint32_t       (*raOpenCodec2)(ra_codec_t *, const char *);
  uint32_t       (*raSetFlavor)(ra_codec_t, uint16_t);
  void           (*raSetDLLAccessPath)(char *);
  void           (*raSetPwd)(ra_codec_t, char *);

  ra_codec_t       context;

  int              sps, w, h;
  int              block_align;

  uint8_t         *frame_buffer;
  uint8_t         *frame_reordered;
  int              frame_size;
  int              frame_num_bytes;

  int              sample_size;

  uint64_t         pts;

  int              output_open;

  int              decoder_ok;

} realdec_decoder_t;

typedef struct {
    uint32_t  samplerate;
    uint16_t  bits;
    uint16_t  channels;
    uint16_t  quality;
    uint32_t  subpacket_size;
    uint32_t  coded_frame_size;
    uint32_t  codec_data_length;
    void      *extras;
} ra_init_t;

static int load_syms_linux (realdec_decoder_t *this, const char *const codec_name, const char *const codec_alternate) {
  cfg_entry_t* entry =
    this->stream->xine->config->lookup_entry(this->stream->xine->config,
					     "decoder.external.real_codecs_path");

  if ( (this->ra_handle = _x_real_codec_open(this->stream, entry->str_value, codec_name, codec_alternate)) == NULL )
    return 0;

  this->raCloseCodec        = dlsym (this->ra_handle, "RACloseCodec");
  this->raDecode            = dlsym (this->ra_handle, "RADecode");
  this->raFlush             = dlsym (this->ra_handle, "RAFlush");
  this->raFreeDecoder       = dlsym (this->ra_handle, "RAFreeDecoder");
  this->raGetFlavorProperty = dlsym (this->ra_handle, "RAGetFlavorProperty");
  this->raOpenCodec2        = dlsym (this->ra_handle, "RAOpenCodec2");
  this->raInitDecoder       = dlsym (this->ra_handle, "RAInitDecoder");
  this->raSetFlavor         = dlsym (this->ra_handle, "RASetFlavor");
  this->raSetDLLAccessPath  = dlsym (this->ra_handle, "SetDLLAccessPath");
  this->raSetPwd            = dlsym (this->ra_handle, "RASetPwd"); /* optional, used by SIPR */

  if (!this->raCloseCodec || !this->raDecode || !this->raFlush || !this->raFreeDecoder ||
      !this->raGetFlavorProperty || !this->raOpenCodec2 || !this->raSetFlavor ||
      /*!raSetDLLAccessPath ||*/ !this->raInitDecoder){
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	     _("libareal: (audio) Cannot resolve symbols - incompatible dll: %s\n"), codec_name);
    return 0;
  }

  if (this->raSetDLLAccessPath){

    char path[1024];

    snprintf(path, sizeof(path) - 2, "DT_Codecs=%s", entry->str_value);
    if (path[strlen(path)-1]!='/'){
      path[strlen(path)+1]=0;
      path[strlen(path)]='/';
    }
    path[strlen(path)+1]=0;

    this->raSetDLLAccessPath(path);
  }

  lprintf ("audio decoder loaded successfully\n");

  return 1;
}

static int init_codec (realdec_decoder_t *this, buf_element_t *buf) {

  int   version, result ;
  int   samples_per_sec, bits_per_sample, num_channels;
  int   subpacket_size, coded_frame_size, codec_data_length;
  int   coded_frame_size2, data_len, flavor;
  int   mode;
  void *extras;

  /*
   * extract header data
   */

  version = _X_BE_16 (buf->content+4);

  lprintf ("header buffer detected, header version %d\n", version);
#ifdef LOG
  xine_hexdump (buf->content, buf->size);
#endif

  flavor           = _X_BE_16 (buf->content+22);
  coded_frame_size = _X_BE_32 (buf->content+24);
  codec_data_length= _X_BE_16 (buf->content+40);
  coded_frame_size2= _X_BE_16 (buf->content+42);
  subpacket_size   = _X_BE_16 (buf->content+44);

  this->sps        = subpacket_size;
  this->w          = coded_frame_size2;
  this->h          = codec_data_length;

  if (version == 4) {
    samples_per_sec = _X_BE_16 (buf->content+48);
    bits_per_sample = _X_BE_16 (buf->content+52);
    num_channels    = _X_BE_16 (buf->content+54);

    /* FIXME: */
    if (buf->type==BUF_AUDIO_COOK) {

      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "libareal: audio header version 4 for COOK audio not supported.\n");
      return 0;
    }
    data_len        = 0; /* FIXME: COOK audio needs this */
    extras          = buf->content+71;

  } else {
    samples_per_sec = _X_BE_16 (buf->content+54);
    bits_per_sample = _X_BE_16 (buf->content+58);
    num_channels    = _X_BE_16 (buf->content+60);
    data_len        = _X_BE_32 (buf->content+74);
    extras          = buf->content+78;
  }

  this->block_align= coded_frame_size2;

  lprintf ("0x%04x 0x%04x 0x%04x 0x%04x data_len 0x%04x\n",
	   subpacket_size, coded_frame_size, codec_data_length,
	   coded_frame_size2, data_len);
  lprintf ("%d samples/sec, %d bits/sample, %d channels\n",
	   samples_per_sec, bits_per_sample, num_channels);

  /* load codec, resolv symbols */

  switch (buf->type) {
  case BUF_AUDIO_COOK:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Cook");
    if (!load_syms_linux (this, "cook.so", "cook.so.6.0"))
      return 0;
    this->block_align = subpacket_size;
    break;

  case BUF_AUDIO_ATRK:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Atrac");
    if (!load_syms_linux (this, "atrc.so", "atrc.so.6.0"))
      return 0;
    this->block_align = subpacket_size;
    break;

  case BUF_AUDIO_14_4:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Real 14.4");
    if (!load_syms_linux (this, "14_4.so", "14_4.so.6.0"))
      return 0;
    break;

  case BUF_AUDIO_28_8:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Real 28.8");
    if (!load_syms_linux (this, "28_8.so", "28_8.so.6.0"))
      return 0;
    break;

  case BUF_AUDIO_SIPRO:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC, "Sipro");
    if (!load_syms_linux (this, "sipr.so", "sipr.so.6.0"))
      return 0;
    /* this->block_align = 19; */
    break;

  default:
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "libareal: error, i don't handle buf type 0x%08x\n", buf->type);
    return 0;
  }

  /*
   * init codec
   */

  result = this->raOpenCodec2 (&this->context, NULL);
  if (result) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libareal: error in raOpenCodec2: %d\n", result);
    return 0;
  }

  {
    ra_init_t init_data;

    init_data.samplerate = samples_per_sec;
    init_data.bits = bits_per_sample;
    init_data.channels = num_channels;
    init_data.quality = 100; /* ??? */
    init_data.subpacket_size = subpacket_size; /* subpacket size */
    init_data.coded_frame_size = coded_frame_size; /* coded frame size */
    init_data.codec_data_length = data_len; /* codec data length */
    init_data.extras = extras; /* extras */

#ifdef LOG
    printf ("libareal: init_data:\n");
    xine_hexdump ((char *) &init_data, sizeof (ra_init_t));
    printf ("libareal: extras :\n");
    xine_hexdump (init_data.extras, data_len);
#endif

    result = this->raInitDecoder (this->context, &init_data);
    if(result){
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	       _("libareal: decoder init failed, error code: 0x%x\n"), result);
      return 0;
    }
  }

  if (this->raSetPwd){
    /* used by 'SIPR' */
    this->raSetPwd (this->context, "Ardubancel Quazanga"); /* set password... lol. */
    lprintf ("password set\n");
  }

  result = this->raSetFlavor (this->context, flavor);
  if (result){
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	     _("libareal: decoder flavor setup failed, error code: 0x%x\n"), result);
    return 0;
  }

  /*
   * alloc buffers for data reordering
   */

  if (this->sps) {

    this->frame_size      = this->w/this->sps*this->h*this->sps;
    this->frame_buffer    = calloc (1, this->frame_size);
    this->frame_reordered = calloc (1, this->frame_size);
    this->frame_num_bytes = 0;

  } else {

    this->frame_size      = this->w*this->h;
    this->frame_buffer    = calloc (this->w, this->h);
    this->frame_reordered = this->frame_buffer;
    this->frame_num_bytes = 0;

  }

  /*
   * open audio output
   */

  switch (num_channels) {
  case 1:
    mode = AO_CAP_MODE_MONO;
    break;
  case 2:
    mode = AO_CAP_MODE_STEREO;
    break;
  default:
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	     _("libareal: oups, real can do more than 2 channels ?\n"));
    return 0;
  }

  (this->stream->audio_out->open) (this->stream->audio_out,
				this->stream,
				bits_per_sample,
				samples_per_sec,
				mode) ;

  this->output_open = 1;

  this->sample_size = num_channels * (bits_per_sample>>3);

  return 1;
}

static void realdec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("decode_data %d bytes, flags=0x%08x, pts=%"PRId64" ...\n",
	   buf->size, buf->decoder_flags, buf->pts);

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {

    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/

  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {

    this->decoder_ok = init_codec (this, buf) ;
    if( !this->decoder_ok )
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);

  } else if( this->decoder_ok ) {

    int size;

    lprintf ("content buffer detected, %d bytes\n", buf->size);

    if (buf->pts && !this->pts)
      this->pts = buf->pts;

    size = buf->size;
    while (size) {
      int need;

      need = this->frame_size - this->frame_num_bytes;
      if (size < need) {
	memcpy (this->frame_buffer + this->frame_num_bytes,
		buf->content + buf->size - size, size);
	this->frame_num_bytes += size;
	size = 0;
      } else {
	audio_buffer_t *audio_buffer;
	int n, len;
	int result;

	memcpy (this->frame_buffer + this->frame_num_bytes,
		buf->content + buf->size - size, need);
	size -= need;
	this->frame_num_bytes = 0;

	n = 0;
	while (n < this->frame_size) {

	  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

	  result = this->raDecode (this->context,
				   this->frame_buffer + n,
				   this->block_align,
				   (char *) audio_buffer->mem, &len, -1);

	  lprintf ("raDecode result %d, len=%d\n", result, len);

	  audio_buffer->vpts       = this->pts;

	  this->pts = 0;

	  audio_buffer->num_frames = len/this->sample_size;;

	  this->stream->audio_out->put_buffer (this->stream->audio_out,
					       audio_buffer, this->stream);
	  n += this->block_align;
	}
      }
    }
  }

  lprintf ("decode_data...done\n");
}

static void realdec_reset (audio_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  this->frame_num_bytes = 0;
}

static void realdec_discontinuity (audio_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  this->pts = 0;
}

static void realdec_dispose (audio_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("dispose\n");

  if (this->context)
    this->raCloseCodec (this->context);

#if 0
  printf ("libareal: FreeDecoder...\n");

  if (this->context)
    this->raFreeDecoder (this->context);
#endif

  lprintf ("dlclose...\n");

  if (this->ra_handle)
    dlclose (this->ra_handle);

  if (this->output_open)
     this->stream->audio_out->close (this->stream->audio_out, this->stream);

  if (this->frame_buffer)
    free (this->frame_buffer);

  free (this);

  lprintf ("dispose done\n");
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen,
				     xine_stream_t *stream) {

  real_class_t      *cls = (real_class_t *) class_gen;
  realdec_decoder_t *this ;

  this = (realdec_decoder_t *) calloc(1, sizeof(realdec_decoder_t));

  this->audio_decoder.decode_data         = realdec_decode_data;
  this->audio_decoder.reset               = realdec_reset;
  this->audio_decoder.discontinuity       = realdec_discontinuity;
  this->audio_decoder.dispose             = realdec_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  this->output_open = 0;

  return &this->audio_decoder;
}

/*
 * real plugin class
 */
void *init_realadec (xine_t *xine, void *data) {

  real_class_t       *this;

  this = (real_class_t *) calloc(1, sizeof(real_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "realadec";
  this->decoder_class.description     = N_("real binary-only codec based audio decoder plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  _x_real_codecs_init(xine);

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t audio_types[] = {
  BUF_AUDIO_COOK, BUF_AUDIO_ATRK, /* BUF_AUDIO_14_4, BUF_AUDIO_28_8, */ BUF_AUDIO_SIPRO, 0
 };

const decoder_info_t dec_info_realaudio = {
  audio_types,         /* supported types */
  6                    /* priority        */
};
