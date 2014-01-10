/*
 * Copyright (C) 2004-2012 the xine project
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
 * IFF File Demuxer by Manfred Tremmel (Manfred.Tremmel@iiv.de)
 * Based on the AIFF demuxer and the information of the Amiga Developer CD
 *
 * currently supported iff-formats:
 * * 8SVX (uncompressed, deltacompression fibonacci and exponential)
 *   + volume rescaling is supported
 *   + multiple channels using CHAN and PAN Chunks are supported
 *   - SEQN and FADE chunks are not supported at the moment
 *     (I do understand what to do, but I hate the work behind it ;-) )
 *   - the optional data chunks ATAK and RLSE are not supported at the moment
 *     (no examples found and description isn't as clear as it should)
 * * 16SV, the same support as 8SVX
 * * ILBM (Bitmap Picturs)
 *   - simple pictures work, nothing more (most work is done in bitmap-decoder)
 * * ANIM (Animations)
 *   - Animation works fine, without seeking.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"

#include "iff.h"

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;

  xine_bmiheader       bih;

  int                  status;

  uint32_t             iff_type;                /* Type of iff-file                    */
  uint32_t             iff_sub_type;            /* Type of iff-sub-file                */

  /* Sound chunks */
  Voice8Header         *vhdr;                   /* vhdr chunk                          */
  EGPoint              *atak;                   /* atak chunk                          */
  EGPoint              *rlse;                   /* rlse chunk                          */
  uint32_t             chan_settings;           /* Mono, Stereo, Left or Right Channel */
  uint32_t             pan_sposition;           /*  */

  /* picture chunks */
  BitMapHeader         *bmhd;                   /* BITMAP-Header-Date (IFF-ILBM/ANIM   */
  ColorRegister        *cmap;                   /* colors of the bitmap picture        */
  uint32_t             cmap_num;                /* number of the bitmap colors         */
  Point2D              *grab;                   /* grab chunk                          */
  DestMerge            *dest;                   /* dest chunk                          */
  SpritePrecedence     sprt;                    /* sprt chunk                          */
  CamgChunk            *camg;                   /* camg chunk                          */
  CRange               crng[256];               /* color range infos for color cycling */
  uint32_t             crng_used;               /* number of color range fields used   */
  CcrtChunk            *ccrt;                   /* ccrt chunk                          */
  DPIHeader            *dpi;                    /* dpi  chunk                          */


  /* anim chunks */
  AnimHeader           *anhd;                   /* anhd chunk                          */
  DPAnimChunk          *dpan;                   /* dpan chunk                          */

  /* some common informations */
  char                 *title;                  /* Name of the stream from NAME-Tag*/
  char                 *copyright;              /* Copyright entry */
  char                 *author;                 /* author entry */
  char                 *annotations;            /* comment of the author, maybe authoring tool */
  char                 *version;                /* version information of the file */
  char                 *text;                   /* anny other text information */

  /* audio information */
  unsigned int         audio_type;
  unsigned int         audio_frames;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_block_align;
  unsigned int         audio_bytes_per_second;
  unsigned char        *audio_interleave_buffer;
  uint32_t             audio_interleave_buffer_size;
  unsigned char        *audio_read_buffer;
  uint32_t             audio_read_buffer_size;
  int                  audio_buffer_filled;
  uint32_t             audio_volume_left;
  uint32_t             audio_volume_right;
  uint32_t             audio_position;
  int                  audio_compression_factor;

  /* picture information */
  int                  video_send_palette;
  unsigned int         video_type;
  int64_t              video_pts;
  int64_t              video_pts_inc;

  unsigned int         running_time;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_iff_t;

typedef struct {
  demux_class_t     demux_class;
} demux_iff_class_t;


/* Decode delta encoded data from n byte source
 * buffer into double as long dest buffer, given initial data
 * value x. The value x is also returned for incrementally
 * decompression. With different decoding tables you can use
 * different decoding deltas
 */

static int8_t delta_decode_block(const int8_t *source, int32_t n, int8_t *dest, int8_t x, const int8_t *table) {
  int32_t i;
  int lim = n * 2;

  for (i=0; i < lim; i++) {
    /* Decode a data nibble, high nibble then low nibble */
    x += (i & 1) ?
         table[((uint8_t)(source[i >> 1]) & 0xf)] :
         table[((uint8_t)(source[i >> 1]) >> 4)];
    dest[i] = x;           /* store a 1 byte sample */
  }
  return(x);
}

/* Decode a complete delta encoded array */
static void delta_decode(int8_t *dest, const int8_t *source, int32_t n, const int8_t *table){
  delta_decode_block(&source[2], n-2, dest, source[1], table);
}

/* returns 1 if the IFF file was opened successfully, 0 otherwise */
static int read_iff_chunk(demux_iff_t *this) {
  unsigned char signature[IFF_SIGNATURE_SIZE];
  unsigned char buffer[512];
  unsigned int  keep_on_reading = 1;
  uint32_t      junk_size;

  while ( keep_on_reading == 1 ) {
    if (this->input->read(this->input, signature, IFF_JUNK_SIZE) == IFF_JUNK_SIZE) {
      if( signature[0] == 0 && signature[1] > 0 ) {
        signature[0]                    = signature[1];
        signature[1]                    = signature[2];
        signature[2]                    = signature[3];
        signature[3]                    = signature[4];
        signature[4]                    = signature[5];
        signature[5]                    = signature[6];
        signature[6]                    = signature[7];
        if (this->input->read(this->input, &signature[7], 1) != 1)
          return 0;
      }
      junk_size = _X_BE_32(&signature[4]);
      switch( _X_BE_32(&signature[0]) ) {
        case IFF_CMAP_CHUNK:
        case IFF_BODY_CHUNK:
        case IFF_DLTA_CHUNK:
        case IFF_FORM_CHUNK:
          /* don't read this chunks, will be done later */
          break;
        default:
          if ( junk_size < 512 ) {
            if (this->input->read(this->input, buffer, junk_size) != junk_size)
              return 0;
          } else {
            this->input->seek(this->input, junk_size, SEEK_SET);
            buffer[0]                   = 0;
          }
          break;
      }

      switch( _X_BE_32(&signature[0]) ) {
        case IFF_FORM_CHUNK:
          if (this->input->read(this->input, buffer, 4) != 4)
            return 0;
          this->iff_sub_type            = _X_BE_32(&buffer[0]);
          break;
        case IFF_VHDR_CHUNK:
          if( this->vhdr == NULL )
            this->vhdr                  = (Voice8Header *)calloc(1, sizeof(Voice8Header));
          this->vhdr->oneShotHiSamples  = _X_BE_32(&buffer[0]);
          this->vhdr->repeatHiSamples   = _X_BE_32(&buffer[4]);
          this->vhdr->samplesPerHiCycle = _X_BE_32(&buffer[8]);
          this->vhdr->samplesPerSec     = _X_BE_16(&buffer[12]);
          this->vhdr->ctOctave          = buffer[14];
          this->vhdr->sCompression      = buffer[15];
          this->audio_channels          = 1;
          this->chan_settings           = MONO;
          switch( this->vhdr->sCompression ) {
            case SND_COMPRESSION_NONE:         /* uncompressed */
            case SND_COMPRESSION_FIBONACCI:    /* Fibonacci */
            case SND_COMPRESSION_EXPONENTIAL:  /* Exponential*/
              this->audio_block_align   = PCM_BLOCK_ALIGN;
              this->audio_type          = BUF_AUDIO_LPCM_BE;
              break;
            default: /* unknown codec */
              xine_log(this->stream->xine, XINE_LOG_MSG,
                       _("iff-8svx/16sv: unknown compression: %d\n"),
                       this->vhdr->sCompression);
              return 0;
              break;
          }
          this->vhdr->volume             = _X_BE_32(&buffer[16]);
          if (this->vhdr->volume > max_volume)
            this->vhdr->volume           = max_volume;
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->oneShotHiSamples      %d\n",
                   this->vhdr->oneShotHiSamples);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->repeatHiSamples       %d\n",
                   this->vhdr->repeatHiSamples);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->samplesPerHiCycle     %d\n",
                   this->vhdr->samplesPerHiCycle);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->samplesPerSec         %d\n",
                   this->vhdr->samplesPerSec);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->ctOctave              %d\n",
                   this->vhdr->ctOctave);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->sCompression          %d\n",
                   this->vhdr->sCompression);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr->volume                %d\n",
                   this->vhdr->volume);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "chan_settings               %d\n",
                   this->chan_settings);
          break;
        case IFF_NAME_CHUNK:
          if (this->title               == NULL)
            this->title                 = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "title                      %s\n",
                   this->title);
          break;
        case IFF_COPY_CHUNK:
          if (this->copyright           == NULL)
            this->copyright             = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "copyright                  %s\n",
                   this->copyright);
          break;
        case IFF_AUTH_CHUNK:
          if (this->author              == NULL)
            this->author                = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "author                     %s\n",
                   this->author);
          break;
        case IFF_ANNO_CHUNK:
          if (this->annotations         == NULL)
            this->annotations           = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "annotations                %s\n",
                   this->annotations);
          break;
        case IFF_FVER_CHUNK:
          if (this->version             == NULL)
            this->version               = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "version                    %s\n",
                   this->version);
          break;
        case IFF_TEXT_CHUNK:
          if (this->text                == NULL)
            this->text                  = strndup( (const char *)buffer, (size_t)junk_size);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "text                       %s\n",
                   this->text);
          break;
        case IFF_ATAK_CHUNK:
          /* not yet implemented */
          break;
        case IFF_RLSE_CHUNK:
          /* not yet implemented */
          break;
        case IFF_CHAN_CHUNK:
          this->chan_settings           = _X_BE_32(&buffer[0]);
          switch( this->chan_settings ) {
            case STEREO:
              this->audio_volume_left   = this->vhdr->volume;
              this->audio_volume_right  = this->vhdr->volume;
              this->audio_channels      = 2;
              break;
            case LEFT:
              this->audio_volume_left   = this->vhdr->volume;
              this->audio_volume_right  = 0;
              this->audio_channels      = 2;
              break;
            case RIGHT:
              this->audio_volume_left   = 0;
              this->audio_volume_right  = this->vhdr->volume;
              this->audio_channels      = 2;
              break;
            default:
              this->chan_settings       = MONO;
              this->audio_channels      = 1;
              break;
          }
          break;
        case IFF_PAN_CHUNK:
          this->chan_settings           = PAN;
          this->pan_sposition           = _X_BE_32(&buffer[0]);
          this->audio_channels          = 2;
          this->audio_volume_left       = this->vhdr->volume / (max_volume / this->pan_sposition);
          this->audio_volume_right      = this->vhdr->volume - this->audio_volume_left;
          break;
        case IFF_BMHD_CHUNK:
          if( this->bmhd == NULL )
            this->bmhd                  = (BitMapHeader *)calloc(1, sizeof(BitMapHeader));
          this->bmhd->w                 = _X_BE_16(&buffer[0]);
          this->bmhd->h                 = _X_BE_16(&buffer[2]);
          this->bmhd->x                 = _X_BE_16(&buffer[4]);
          this->bmhd->y                 = _X_BE_16(&buffer[6]);
          this->bmhd->nplanes           = buffer[8];
          this->bmhd->masking           = buffer[9];
          this->bmhd->compression       = buffer[10];
          this->bmhd->pad1              = buffer[11];
          this->bmhd->transparentColor  = _X_BE_16(&buffer[12]);
          this->bmhd->xaspect           = buffer[14];
          this->bmhd->yaspect           = buffer[15];
          this->bmhd->pagewidth         = _X_BE_16(&buffer[16]);
          this->bmhd->pageheight        = _X_BE_16(&buffer[18]);

          if (this->bmhd->w > 0)
            this->bih.biWidth           = this->bmhd->w;
          else
            this->bih.biWidth           = this->bmhd->pagewidth;
          if (this->bmhd->h > 0)
            this->bih.biHeight          = this->bmhd->h;
          else
            this->bih.biHeight          = this->bmhd->pageheight;
          this->bih.biPlanes            = this->bmhd->nplanes;
          this->bih.biBitCount          = this->bmhd->nplanes;
          switch( this->bmhd->compression ) {
            case PIC_COMPRESSION_NONE:         /* uncompressed */
              this->video_type          = BUF_VIDEO_BITPLANE;
              break;
            case PIC_COMPRESSION_BYTERUN1:
              this->video_type          = BUF_VIDEO_BITPLANE_BR1;
              break;
            default:
              xine_log(this->stream->xine, XINE_LOG_MSG,
                       _("iff-ilbm: unknown compression: %d\n"),
                       this->bmhd->compression);
              return 0;
              break;
          }
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->w                     %d\n",
                   this->bmhd->w);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->h                     %d\n",
                   this->bmhd->h);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->x                     %d\n",
                   this->bmhd->x);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->y                     %d\n",
                   this->bmhd->y);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->nplanes               %d\n",
                   this->bmhd->nplanes);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->masking               %d\n",
                   this->bmhd->masking);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->compression           %d\n",
                   this->bmhd->compression);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->pad1                  %d\n",
                   this->bmhd->pad1);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->transparentColor      %d\n",
                   this->bmhd->transparentColor);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->xaspect               %d\n",
                   this->bmhd->xaspect);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->yaspect               %d\n",
                   this->bmhd->yaspect);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->pagewidth             %d\n",
                   this->bmhd->pagewidth);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "bmhd->pageheight            %d\n",
                   this->bmhd->pageheight);
          break;
        case IFF_CMAP_CHUNK:
          /* every color contains red, green and blue componente using 8Bit */
          this->cmap_num                = junk_size / PIC_SIZE_OF_COLOR_REGISTER;
          this->cmap                    = (ColorRegister *)malloc(junk_size);
          this->video_send_palette      = 1;
          if (!this->cmap || this->input->read(this->input, (char *)this->cmap, junk_size) != junk_size)
            return 0;
          break;
        case IFF_GRAB_CHUNK:
          if( this->grab == NULL )
            this->grab                  = (Point2D *)calloc(1, sizeof(Point2D));
          this->grab->x                 = _X_BE_16(&buffer[0]);
          this->grab->y                 = _X_BE_16(&buffer[2]);
          break;
        case IFF_DEST_CHUNK:
          if( this->dest == NULL )
            this->dest                  = (DestMerge *)calloc(1, sizeof(DestMerge));
          this->dest->depth             = buffer[0];
          this->dest->pad1              = buffer[1];
          this->dest->plane_pick        = _X_BE_16(&buffer[2]);
          this->dest->plane_onoff       = _X_BE_16(&buffer[4]);
          this->dest->plane_mask        = _X_BE_16(&buffer[6]);
          break;
        case IFF_SPRT_CHUNK:
          this->sprt                    = _X_BE_16(&buffer[0]);
          break;
        case IFF_CAMG_CHUNK:
          if( this->camg == NULL )
            this->camg                  = (CamgChunk *)calloc(1, sizeof(CamgChunk));
          this->camg->view_modes        = _X_BE_32(&buffer[0]);
          this->bih.biCompression       = this->camg->view_modes;
          if( this->camg->view_modes & CAMG_PAL &&
              this->video_pts_inc       == 4500 )
            this->video_pts_inc         = 5400;
          break;
        case IFF_CRNG_CHUNK:
          if (this->crng_used < 256) {
            this->crng[this->crng_used].pad1   = _X_BE_16(&buffer[0]);
            this->crng[this->crng_used].rate   = _X_BE_16(&buffer[2]);
            this->crng[this->crng_used].active = _X_BE_16(&buffer[4]);
            this->crng[this->crng_used].low    = buffer[6];
            this->crng[this->crng_used].high   = buffer[7];
            this->crng_used++;
          }
          break;
        case IFF_CCRT_CHUNK:
          if( this->ccrt == NULL )
            this->ccrt                  = (CcrtChunk *)calloc(1, sizeof(CcrtChunk));
          this->ccrt->direction         = _X_BE_16(&buffer[0]);
          this->ccrt->start             = buffer[2];
          this->ccrt->end               = buffer[3];
          this->ccrt->seconds           = _X_BE_32(&buffer[4]);
          this->ccrt->microseconds      = _X_BE_32(&buffer[8]);
          this->ccrt->pad               = _X_BE_16(&buffer[12]);
          break;
        case IFF_DPI_CHUNK:
          if( this->dpi == NULL )
            this->dpi                   = (DPIHeader *)calloc(1, sizeof(DPIHeader));
          this->dpi->x                  = _X_BE_16(&buffer[0]);
          this->dpi->y                  = _X_BE_16(&buffer[0]);
          break;
        case IFF_ANHD_CHUNK:
          if( this->anhd == NULL )
            this->anhd                  = (AnimHeader *)calloc(1, sizeof(AnimHeader));
          this->anhd->operation         = buffer[0];
          this->anhd->mask              = buffer[1];
          this->anhd->w                 = _X_BE_16(&buffer[2]);
          this->anhd->h                 = _X_BE_16(&buffer[4]);
          this->anhd->x                 = _X_BE_16(&buffer[6]);
          this->anhd->y                 = _X_BE_16(&buffer[8]);
          this->anhd->abs_time          = _X_BE_32(&buffer[10]);
          this->anhd->rel_time          = _X_BE_32(&buffer[14]);
          this->anhd->interleave        = buffer[18];
          this->anhd->pad0              = buffer[19];
          this->anhd->bits              = _X_BE_32(&buffer[20]);
          /* Using rel_time deaktivated, seems to be broken in most animations */
          /*if( this->dpan == NULL )
            this->video_pts            += this->video_pts_inc *
                                          ((this->anhd->rel_time > 0) ? this->anhd->rel_time : 1);
          else*/
            this->video_pts            += this->video_pts_inc;
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->operation             %d\n",
                   this->anhd->operation);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->mask                  %d\n",
                   this->anhd->mask);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->w                     %d\n",
                   this->anhd->w);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->h                     %d\n",
                   this->anhd->h);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->x                     %d\n",
                   this->anhd->x);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->y                     %d\n",
                   this->anhd->y);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->abs_time              %d\n",
                   this->anhd->abs_time);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->rel_time              %d\n",
                   this->anhd->rel_time);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->interleave            %d\n",
                   this->anhd->interleave);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "anhd->bits                  %d\n",
                   this->anhd->bits);
          break;
        case IFF_DPAN_CHUNK:
          if( this->dpan == NULL )
            this->dpan                  = (DPAnimChunk *)calloc(1, sizeof(DPAnimChunk));
          this->dpan->version           = _X_BE_16(&buffer[0]);
          this->dpan->nframes           = _X_BE_16(&buffer[2]);
          this->dpan->fps               = buffer[4];
          this->dpan->unused1           = buffer[5];
          this->dpan->unused2           = buffer[6];
          this->dpan->unused3           = buffer[7];
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "dpan->version               %d\n",
                   this->dpan->version);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "dpan->nframes               %d\n",
                   this->dpan->nframes);
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "dpan->fps                   %d\n",
                   this->dpan->fps);
          break;
        case IFF_DPPS_CHUNK:
          /* DPPS contains DeluxePaint specific information, no documentation available */
          break;
        case IFF_JUNK_CHUNK:
          /* JUNK contains garbage and should be ignored */
          break;
        case IFF_BODY_CHUNK:
        case IFF_DLTA_CHUNK:
          this->data_start              = this->input->get_current_pos(this->input);
          this->data_size               = junk_size;
          switch( this->iff_type )
          {
            case IFF_8SVX_CHUNK:
            case IFF_16SV_CHUNK:
              if( this->vhdr->sCompression > SND_COMPRESSION_NONE ) {
                this->audio_interleave_buffer_size = this->data_size * 2;
                this->audio_read_buffer_size       = this->data_size;
              } else {
                this->audio_interleave_buffer_size = this->data_size;
                this->audio_read_buffer_size       = 0;
              }
              if( this->chan_settings == MONO)
                this->audio_volume_left = this->vhdr->volume;
              this->audio_bytes_per_second         = this->audio_channels *
                                                     (this->audio_bits / 8) *
                                                     this->vhdr->samplesPerSec;
              this->running_time        = ((this->vhdr->oneShotHiSamples +
                                            this->vhdr->repeatHiSamples) *
                                           1000 / this->vhdr->samplesPerSec) /
                                          this->audio_channels;
              break;
            case IFF_ILBM_CHUNK:
              this->bih.biSize          = this->data_size;
              this->bih.biSizeImage     = this->data_size;
              break;
            case IFF_ANIM_CHUNK:
              this->bih.biSize          = this->data_size;
              this->bih.biSizeImage     = this->data_size;
              if( this->dpan ) {
                if( this->dpan->fps > 0 && this->dpan->fps <= 60)
                   this->video_pts_inc  = 90000 / this->dpan->fps;
                this->running_time      = (this->video_pts_inc * this->dpan->nframes) / 90;
              }
              break;
            default:
              break;
          }
          keep_on_reading               = 0;
          break;
        default:
          signature[4]                  = 0;
          xine_log(this->stream->xine, XINE_LOG_MSG, _("iff: unknown Chunk: %s\n"), signature);
          break;
      }
    } else
      return 0;
  }

  return 1;
}

/* returns 1 if the IFF file was opened successfully, 0 otherwise */
static int open_iff_file(demux_iff_t *this) {

  unsigned char signature[IFF_SIGNATURE_SIZE];

  if (_x_demux_read_header(this->input, signature, IFF_SIGNATURE_SIZE) != IFF_SIGNATURE_SIZE)
    return 0;

  this->title                           = NULL;
  this->copyright                       = NULL;
  this->author                          = NULL;
  this->annotations                     = NULL;
  this->version                         = NULL;
  this->text                            = NULL;

  this->vhdr                            = NULL;
  this->atak                            = NULL;
  this->rlse                            = NULL;
  this->chan_settings                   = 0;
  this->audio_type                      = 0;
  this->audio_frames                    = 0;
  this->audio_bits                      = 0;
  this->audio_channels                  = 0;
  this->audio_block_align               = 0;
  this->audio_bytes_per_second          = 0;
  this->running_time                    = 0;
  this->data_start                      = 0;
  this->data_size                       = 0;
  this->seek_flag                       = 0;
  this->audio_interleave_buffer         = 0;
  this->audio_interleave_buffer_size    = 0;
  this->audio_read_buffer               = 0;
  this->audio_read_buffer_size          = 0;
  this->audio_buffer_filled             = 0;
  this->audio_compression_factor        = 1;
  this->audio_position                  = 0;
  this->bmhd                            = NULL;
  this->cmap                            = NULL;
  this->cmap_num                        = 0;
  this->grab                            = NULL;
  this->dest                            = NULL;
  this->sprt                            = 0;
  this->camg                            = NULL;
  this->crng_used                       = 0;
  this->ccrt                            = NULL;
  this->dpi                             = NULL;
  this->anhd                            = NULL;
  this->dpan                            = NULL;

  this->iff_type                        = _X_BE_32(&signature[8]);
  this->iff_sub_type                    = this->iff_type;

  this->video_type                      = 0;
  this->video_pts                       = 0;
  this->video_pts_inc                   = 0;
  this->video_send_palette              = 0;

  this->bih.biSize                      = 0;
  this->bih.biWidth                     = 0;
  this->bih.biHeight                    = 0;
  this->bih.biPlanes                    = 0;
  this->bih.biBitCount                  = 0;
  this->bih.biCompression               = 0;
  this->bih.biSizeImage                 = 0;
  this->bih.biXPelsPerMeter             = 0;
  this->bih.biYPelsPerMeter             = 0;
  this->bih.biClrUsed                   = 0;
  this->bih.biClrImportant              = 0;

  /* check the signature */
  if (_X_BE_32(&signature[0]) == IFF_FORM_CHUNK)
  {
    switch( this->iff_type )
    {
      case IFF_8SVX_CHUNK:
        this->audio_bits                = 8;
        break;
      case IFF_16SV_CHUNK:
        this->audio_bits                = 16;
        break;
      case IFF_ILBM_CHUNK:
        this->video_pts_inc             = 10000000;
        break;
      case IFF_ANIM_CHUNK:
        this->video_pts_inc             = 4500;
        break;
      default:
        return 0;
        break;
    }
  } else
    return 0;

  /* file is qualified; skip over the header bytes in the stream */
  this->input->seek(this->input, IFF_SIGNATURE_SIZE, SEEK_SET);

  return read_iff_chunk( this );

}

static int demux_iff_send_chunk(demux_plugin_t *this_gen) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  buf_element_t *buf                    = NULL;
  unsigned int remaining_sample_bytes   = 0;
  off_t current_file_pos;
  int16_t *pointer16_from;
  int16_t *pointer16_to;
  int16_t zw_16;
  int32_t input_length;
  int64_t zw_pts;
  int64_t zw_rescale;
  int j, k;
  int interleave_index;
  int size;

  /* when audio is available and it's a stereo, left or right stream
   * at iff 8svx, the complete left stream at the beginning and the
   * right channel at the end of the stream. Both have to be inter-
   * leaved, the same way as in the sega film demuxer. I've copied
   * it out there
   */
  switch( this->iff_sub_type ) {
    case IFF_8SVX_CHUNK:
    case IFF_16SV_CHUNK:
      /* just load data chunks from wherever the stream happens to be
       * pointing; issue a DEMUX_FINISHED status if EOF is reached */
      current_file_pos                  = this->audio_position;

      /* load the whole chunk into the buffer */
      if (this->audio_buffer_filled == 0) {
        if (this->audio_interleave_buffer_size > 0)
        {
          this->audio_interleave_buffer =
	    calloc(1, this->audio_interleave_buffer_size);
          if (!this->audio_interleave_buffer)
            return this->status = DEMUX_FINISHED;
        }
        if (this->audio_read_buffer_size > 0)
        {
          this->audio_read_buffer       =
	    calloc(1, this->audio_read_buffer_size);
          if (!this->audio_read_buffer)
            return this->status = DEMUX_FINISHED;
        }
        if (this->audio_read_buffer) {
          if (this->input->read(this->input, this->audio_read_buffer,
              this->data_size) != this->data_size) {
            this->status                = DEMUX_FINISHED;
            return this->status;
          }

          switch( this->vhdr->sCompression ) {
            case SND_COMPRESSION_FIBONACCI:
              if (this->chan_settings == STEREO) {
                delta_decode((int8_t *)(this->audio_interleave_buffer),
                             (int8_t *)(this->audio_read_buffer),
                             (this->data_size/2),
                             fibonacci);
                delta_decode((int8_t *)(&(this->audio_interleave_buffer[this->data_size])),
                             (int8_t *)(&(this->audio_read_buffer[(this->data_size/2)])),
                             (this->data_size/2),
                             fibonacci);
              } else
                delta_decode((int8_t *)(this->audio_interleave_buffer),
                             (int8_t *)(this->audio_read_buffer),
                             this->data_size,
                             fibonacci);
              this->audio_compression_factor = 2;
              break;
            case SND_COMPRESSION_EXPONENTIAL:
              if (this->chan_settings == STEREO) {
                delta_decode((int8_t *)(this->audio_interleave_buffer),
                             (int8_t *)(this->audio_read_buffer),
                             (this->data_size/2),
                             exponential);
                delta_decode((int8_t *)(&(this->audio_interleave_buffer[this->data_size])),
                             (int8_t *)(&(this->audio_read_buffer[(this->data_size/2)])),
                             (this->data_size/2),
                             exponential);
              } else
                delta_decode((int8_t *)(this->audio_interleave_buffer),
                             (int8_t *)(this->audio_read_buffer),
                             this->data_size,
                             exponential);
              this->audio_compression_factor = 2;
              break;
            default:
              break;
          }
          free( this->audio_read_buffer );
          this->audio_read_buffer       = NULL;
        } else {
          if (this->input->read(this->input, this->audio_interleave_buffer,
              this->data_size) != this->data_size) {
            this->status                = DEMUX_FINISHED;
            return this->status;
          }
        }
        this->audio_buffer_filled       = 1;
      }

      /* proceed to de-interleave into individual buffers */
      if (this->chan_settings == STEREO) {
        remaining_sample_bytes          = ((this->data_size - current_file_pos) *
                                           this->audio_compression_factor) / 2;
        interleave_index                = (current_file_pos *
                                           this->audio_compression_factor) / 2;
      } else {
        remaining_sample_bytes          = ((this->data_size - current_file_pos) *
                                           this->audio_compression_factor);
        interleave_index                = (current_file_pos *
                                           this->audio_compression_factor);
      }

      zw_pts                            = current_file_pos;

      if (this->chan_settings == STEREO)
        input_length                    = this->data_size *
                                          this->audio_compression_factor;
      else
        input_length                    = this->data_size *
                                          this->audio_compression_factor *
                                          this->audio_channels;
      while (remaining_sample_bytes) {
        buf                             = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type                       = this->audio_type;
        if( input_length )
          buf->extra_info->input_normpos = (int)((double)zw_pts * 65535 / input_length);
        buf->pts                        = zw_pts * 90000 / this->audio_bytes_per_second;
        buf->extra_info->input_time     = buf->pts / 90;

        if (remaining_sample_bytes > buf->max_size / this->audio_channels)
          buf->size                     = buf->max_size;
        else
          buf->size                     = remaining_sample_bytes * this->audio_channels;
        remaining_sample_bytes         -= buf->size / this->audio_channels;
        zw_pts                         += buf->size;

        /* 16 bit sound */
        if (this->audio_bits == 16) {

          pointer16_from                = (int16_t *)this->audio_interleave_buffer;
          pointer16_to                  = (int16_t *)buf->content;

          if (this->chan_settings == STEREO ||
              this->chan_settings == LEFT   ||
              this->chan_settings == PAN    ||
              this->chan_settings == MONO) {
            if( this->audio_volume_left == max_volume ) {
              for (j = 0, k = (interleave_index / 2); j < (buf->size / 2); j += this->audio_channels) {
                pointer16_to[j]         = pointer16_from[k++];
              }
            } else {
              for (j = 0, k = (interleave_index / 2); j < (buf->size / 2); j += this->audio_channels) {
                zw_16                   = _X_BE_16(&pointer16_from[k]);
                k++;
                zw_rescale              = zw_16;
                zw_rescale             *= this->audio_volume_left;
                zw_rescale             /= max_volume;
                zw_16                   = (zw_rescale>32767) ? 32767 : ((zw_rescale<-32768) ? -32768 : zw_rescale);
                pointer16_to[j]         = _X_BE_16(&zw_16);
              }
            }
          } else {
            for (j = 0; j < (buf->size / 2); j += this->audio_channels) {
              pointer16_to[j]           = 0;
            }
          }

          if (this->chan_settings == STEREO ||
              this->chan_settings == RIGHT  ||
              this->chan_settings == PAN) {
            if (this->chan_settings == STEREO)
              k                         = (interleave_index +
                                           (this->data_size *
                                            this->audio_compression_factor / 2)) / 2;
            else
              k                         = interleave_index / 2;
            if( this->audio_volume_right == max_volume ) {
              for (j = 1; j < (buf->size / 2); j += this->audio_channels) {
                pointer16_to[j]         = pointer16_from[k++];
              }
            } else {
              for (j = 1; j < (buf->size / 2); j += this->audio_channels) {
                zw_16                   = _X_BE_16(&pointer16_from[k]);
                k++;
                zw_rescale              = zw_16;
                zw_rescale             *= this->audio_volume_left;
                zw_rescale             /= max_volume;
                zw_16                   = (zw_rescale>32767) ? 32767 : ((zw_rescale<-32768) ? -32768 : zw_rescale);
                pointer16_to[j]         = _X_BE_16(&zw_16);
              }
            }
          } else if (this->chan_settings == LEFT) {
            for (j = 1; j < (buf->size / 2); j += this->audio_channels) {
              pointer16_to[j]           = 0;
            }
          }
        /* 8 bit sound */
        } else {
          if (this->chan_settings == STEREO ||
              this->chan_settings == LEFT   ||
              this->chan_settings == PAN    ||
              this->chan_settings == MONO) {
            if( this->audio_volume_left == max_volume ) {
              for (j = 0, k = interleave_index; j < buf->size; j += this->audio_channels) {
                buf->content[j]         = this->audio_interleave_buffer[k++] + 0x80;
              }
            } else {
              for (j = 0, k = interleave_index; j < buf->size; j += this->audio_channels) {
                zw_rescale              = this->audio_interleave_buffer[k++];
                zw_rescale             *= this->audio_volume_left;
                zw_rescale             /= max_volume;
                zw_rescale             += 0x80;
                buf->content[j]         = (zw_rescale>255) ? 255 : ((zw_rescale<0) ? 0 : zw_rescale);
              }
            }
          } else {
            for (j = 0; j < buf->size; j += 2) {
              buf->content[j]           = 0;
            }
          }
          if (this->chan_settings == STEREO ||
              this->chan_settings == RIGHT  ||
              this->chan_settings == PAN) {
            if (this->chan_settings == STEREO)
              k                         = interleave_index +
                                          (this->data_size *
                                           this->audio_compression_factor / 2);
            else
              k                         = interleave_index;
            if( this->audio_volume_right == max_volume ) {
              for (j = 1; j < buf->size; j += this->audio_channels) {
                buf->content[j]         = this->audio_interleave_buffer[k++] + 0x80;
              }
            } else {
              for (j = 1; j < buf->size; j += this->audio_channels) {
                zw_rescale              = this->audio_interleave_buffer[k++];
                zw_rescale             *= this->audio_volume_right;
                zw_rescale             /= max_volume;
                zw_rescale             += 0x80;
                buf->content[j]         = (zw_rescale>255) ? 255 : ((zw_rescale<0) ? 0 : zw_rescale);
              }
            }
          } else if (this->chan_settings == LEFT) {
            for (j = 1; j < buf->size; j += this->audio_channels) {
              buf->content[j]           = 0;
            }
          }
        }
        interleave_index                 += buf->size / this->audio_channels;

        if (!remaining_sample_bytes)
          buf->decoder_flags           |= BUF_FLAG_FRAME_END;

        xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
                 "sending audio buf with %" PRId32 " bytes, %" PRId64 " pts, %" PRId32 " duration\n",
                 buf->size, buf->pts, buf->decoder_info[0]);
        this->audio_fifo->put(this->audio_fifo, buf);
      }
      break;
    case IFF_ILBM_CHUNK:
      /* send off the palette, if there is one */
      if ( this->video_send_palette == 1 ) {
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->decoder_flags              = BUF_FLAG_SPECIAL|BUF_FLAG_HEADER;
        buf->decoder_info[1]            = BUF_SPECIAL_PALETTE;
        buf->decoder_info[2]            = this->cmap_num;
        buf->decoder_info_ptr[2]        = this->cmap;
        buf->size                       = 0;
        buf->type                       = this->video_type;
        this->video_fifo->put (this->video_fifo, buf);
        this->video_send_palette        = 0;
      }

      /* And now let's start with the picture */
      size = this->data_size;
      while (size > 0) {
        buf                             = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->content                    = buf->mem;
        buf->type                       = this->video_type;
        buf->decoder_flags              = BUF_FLAG_FRAMERATE;
        if( this->anhd == NULL )
          buf->decoder_info[0]          = 0;
        else
          buf->decoder_info[0]          = this->video_pts_inc;
        buf->decoder_info_ptr[0]        = this->anhd;
        if( this->input->get_length (this->input) )
          buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                           65535 / this->input->get_length (this->input) );
        buf->pts                        = this->video_pts;
        buf->extra_info->input_time     = buf->pts / 90;

        if (size > buf->max_size) {
          buf->size = buf->max_size;
        } else {
          buf->size = size;
        }
        size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
        }

        if (size <= 0)
        {
          buf->decoder_flags           |= BUF_FLAG_FRAME_END;
          if (this->iff_type == IFF_ILBM_CHUNK )
            buf->decoder_info[1]        = this->video_pts_inc;  /* initial video_step */
        }


        this->video_fifo->put(this->video_fifo, buf);
      }
      break;
  }

  /* look for other multimedia parts */
  if( read_iff_chunk( this ) )
    this->status                        = DEMUX_OK;
  else
    this->status                        = DEMUX_FINISHED;

  return this->status;
}

static void demux_iff_send_headers(demux_plugin_t *this_gen) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo                      = this->stream->video_fifo;
  this->audio_fifo                      = this->stream->audio_fifo;

  this->status                          = DEMUX_OK;


  if( this->title )
    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->title);

  if( this->author )
    _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->author);

  if( this->annotations )
    _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, this->annotations);

  /* load stream information */
  switch( this->iff_type ) {
    case IFF_8SVX_CHUNK:
    case IFF_16SV_CHUNK:
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                         this->audio_channels);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                         this->vhdr->samplesPerSec);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                         this->audio_bits);

      /* send start buffers */
      _x_demux_control_start(this->stream);

      /* send init info to decoders */
      if (this->audio_fifo && this->audio_type) {
        buf                             = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type                       = this->audio_type;
        buf->decoder_flags              = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
        buf->decoder_info[0]            = 0;
        buf->decoder_info[1]            = this->vhdr->samplesPerSec;
        buf->decoder_info[2]            = this->audio_bits;
        buf->decoder_info[3]            = this->audio_channels;
        this->audio_fifo->put (this->audio_fifo, buf);
      }
      break;
    case IFF_ILBM_CHUNK:
    case IFF_ANIM_CHUNK:
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->bih.biWidth);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
                         this->video_pts_inc);

      /* send start buffers */
      _x_demux_control_start(this->stream);

      buf                               = this->video_fifo->buffer_pool_alloc(this->video_fifo);
      buf->type                         = this->video_type;
      buf->size                         = sizeof(xine_bmiheader);
      buf->decoder_flags                = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|BUF_FLAG_FRAME_END;
      buf->decoder_info[0]              = this->video_pts_inc;  /* initial video_step */
      buf->decoder_info[1]              = 0;
      buf->decoder_info[2]              = this->bmhd->xaspect;
      buf->decoder_info[3]              = this->bmhd->yaspect;
      memcpy(buf->content, &this->bih, sizeof(this->bih));

      this->video_fifo->put(this->video_fifo, buf);
      break;
    default:
      break;
  }

}

static int demux_iff_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  switch( this->iff_type ) {
    case IFF_8SVX_CHUNK:
    case IFF_16SV_CHUNK:
      this->seek_flag                   = 1;
      this->status                      = DEMUX_OK;
      _x_demux_flush_engine (this->stream);

      /* if input is non-seekable, do not proceed with the rest of this
       * seek function */
      if (!INPUT_IS_SEEKABLE(this->input))
        return this->status;

      /* check the boundary offsets */
      this->audio_position              = (start_pos < 0) ? 0 :
                                          ((start_pos >= this->data_size) ?
                                           this->data_size : start_pos);
    case IFF_ILBM_CHUNK:
    case IFF_ANIM_CHUNK:
      /* disable seeking for ILBM and ANIM */
      this->seek_flag                   = 0;
      if( !playing ) {
        this->status = DEMUX_OK;
      }
      break;
    default:
      break;
  }
  return this->status;
}

static void demux_iff_dispose (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  free(this->bmhd);
  free(this->cmap);
  free(this->grab);
  free(this->dest);
  free(this->camg);
  free(this->ccrt);
  free(this->dpi);
  free(this->vhdr);
  free(this->atak);
  free(this->rlse);
  free(this->anhd);
  free(this->dpan);

  free(this->title);
  free(this->copyright);
  free(this->author);
  free(this->annotations);
  free(this->version);
  free(this->text);

  free (this->audio_interleave_buffer);
  free (this->audio_read_buffer);

  this->audio_buffer_filled             = 0;

  free(this);
}

static int demux_iff_get_status (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_iff_get_stream_length (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  return this->running_time;
}

static uint32_t demux_iff_get_capabilities(demux_plugin_t *this_gen){
  return DEMUX_CAP_NOCAP;
}

static int demux_iff_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type){
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_iff_t   *this;

  this                                  = calloc(1, sizeof(demux_iff_t));
  this->stream                          = stream;
  this->input                           = input;

  this->demux_plugin.send_headers       = demux_iff_send_headers;
  this->demux_plugin.send_chunk         = demux_iff_send_chunk;
  this->demux_plugin.seek               = demux_iff_seek;
  this->demux_plugin.dispose            = demux_iff_dispose;
  this->demux_plugin.get_status         = demux_iff_get_status;
  this->demux_plugin.get_stream_length  = demux_iff_get_stream_length;
  this->demux_plugin.get_capabilities   = demux_iff_get_capabilities;
  this->demux_plugin.get_optional_data  = demux_iff_get_optional_data;
  this->demux_plugin.demux_class        = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

    case METHOD_BY_MRL:
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:

      if (!open_iff_file(this)) {
        free (this);
        return NULL;
      }

      break;

    default:
      free (this);
      return NULL;
  }

  return &this->demux_plugin;
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_iff_class_t     *this;

  this = calloc(1, sizeof(demux_iff_class_t));

  this->demux_class.open_plugin         = open_plugin;
  this->demux_class.description         = N_("IFF demux plugin");
  this->demux_class.identifier          = "IFF";
  this->demux_class.mimetypes           =
    "audio/x-8svx: 8svx: IFF-8SVX Audio;"
    "audio/8svx: 8svx: IFF-8SVX Audio;"
    "audio/x-16sv: 16sv: IFF-16SV Audio;"
    "audio/168sv: 16sv: IFF-16SV Audio;"
    "image/x-ilbm: ilbm: IFF-ILBM Picture;"
    "image/ilbm: ilbm: IFF-ILBM Picture;"
    "video/x-anim: anim: IFF-ANIM Video;"
    "video/anim: anim: IFF-ANIM Video;";
  this->demux_class.extensions          = "iff svx 8svx 16sv ilbm ham ham6 ham8 anim anim3 anim5 anim7 anim8";
  this->demux_class.dispose             = default_demux_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_iff = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "iff", XINE_VERSION_CODE, &demux_info_iff, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

