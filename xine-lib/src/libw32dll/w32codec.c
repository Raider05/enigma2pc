/*
 * Copyright (C) 2000-2006 the xine project
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
 * routines for using w32 codecs
 * DirectShow support by Miguel Freitas (Nov/2001)
 * DMO support (Dez/2002)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "wine/msacm.h"
#include "wine/driver.h"
#include "wine/avifmt.h"
#include "wine/vfw.h"
#include "wine/mmreg.h"
#include "wine/ldt_keeper.h"
#include "wine/win32.h"
#include "wine/wineacm.h"
#include "wine/loader.h"

#define NOAVIFILE_HEADERS
#include "DirectShow/guids.h"
#include "DirectShow/DS_AudioDecoder.h"
#include "DirectShow/DS_VideoDecoder.h"
#include "dmo/DMO_AudioDecoder.h"
#include "dmo/DMO_VideoDecoder.h"

#define LOG_MODULE "w32codec"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

#include "common.c"

static GUID CLSID_Voxware =
{
     0x73f7a062, 0x8829, 0x11d1,
     { 0xb5, 0x50, 0x00, 0x60, 0x97, 0x24, 0x2d, 0x8d }
};

static GUID CLSID_Acelp =
{
     0x4009f700, 0xaeba, 0x11d1,
     { 0x83, 0x44, 0x00, 0xc0, 0x4f, 0xb9, 0x2e, 0xb7 }
};

static GUID wmv1_clsid =
{
	0x4facbba1, 0xffd8, 0x4cd7,
	{0x82, 0x28, 0x61, 0xe2, 0xf6, 0x5c, 0xb1, 0xae}
};

static GUID wmv2_clsid =
{
	0x521fb373, 0x7654, 0x49f2,
	{0xbd, 0xb1, 0x0c, 0x6e, 0x66, 0x60, 0x71, 0x4f}
};

static GUID wmv3_clsid =
{
	0x724bb6a4, 0xe526, 0x450f,
	{0xaf, 0xfa, 0xab, 0x9b, 0x45, 0x12, 0x91, 0x11}
};

static GUID wmvdmo_clsid =
{
	0x82d353df, 0x90bd, 0x4382,
	{0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34}
};

static GUID dvsd_clsid =
{
	0xB1B77C00, 0xC3E4, 0x11CF,
	{0xAF, 0x79, 0x00, 0xAA, 0x00, 0xB6, 0x7A, 0x42}
};

static GUID msmpeg4_clsid =
{
	0x82CCd3E0, 0xF71A, 0x11D0,
	{ 0x9f, 0xe5, 0x00, 0x60, 0x97, 0x78, 0xea, 0x66}
};

static GUID mss1_clsid =
{
	0x3301a7c4, 0x0a8d, 0x11d4,
	{ 0x91, 0x4d, 0x00, 0xc0, 0x4f, 0x61, 0x0d, 0x24 }
};

static GUID wma3_clsid =
{
	0x27ca0808, 0x01f5, 0x4e7a,
	{ 0x8b, 0x05, 0x87, 0xf8, 0x07, 0xa2, 0x33, 0xd1 }
};

static GUID wmav_clsid =
{
	0x874131cb, 0x4ecc, 0x443b,
        { 0x89, 0x48, 0x74, 0x6b, 0x89, 0x59, 0x5d, 0x20 }
};


/* some data is shared inside wine loader.
 * this mutex seems to avoid some segfaults
 */
static pthread_mutex_t win32_codec_mutex;
static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static char*   win32_codec_name;

#define VIDEOBUFSIZE 128*1024

#define DRIVER_STD 0
#define DRIVER_DS  1
#define DRIVER_DMO 2

typedef struct w32v_decoder_s {
  video_decoder_t   video_decoder;

  xine_stream_t    *stream;

  int64_t           video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  *bih, o_bih;
  double            ratio;
  char              scratch1[16]; /* some codecs overflow o_bih */
  HIC               hic;
  int               yuv_supported ;
  int		    yuv_hack_needed ;
  int               flipped ;
  unsigned char    *buf;
  int               bufsize;
  void             *img_buffer;
  int               size;
  long		    outfmt;

  int               ex_functions;
  int               driver_type;
  GUID             *guid;
  DS_VideoDecoder  *ds_dec;
  DMO_VideoDecoder *dmo_dec;

  int               stream_id;
  int               skipframes;

  ldt_fs_t *ldt_fs;
} w32v_decoder_t;

typedef struct {
  video_decoder_class_t   decoder_class;
} w32v_class_t;

typedef struct w32a_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *stream;

  int               output_open;
  int               decoder_ok;

  unsigned char    *buf;
  int               size;
  int64_t           pts;

  /* these are used for pts estimation */
  int64_t           lastpts, sumpts, sumsize;
  double            byterate;

  unsigned char    *outbuf;
  int               outsize;

  HACMSTREAM        srcstream;
  int               rec_audio_src_size;
  int               max_audio_src_size;
  int               num_channels;
  int               rate;

  int               driver_type;
  GUID             *guid;
  DS_AudioDecoder  *ds_dec;
  DMO_AudioDecoder *dmo_dec;

  ldt_fs_t *ldt_fs;
} w32a_decoder_t;

typedef struct {
  audio_decoder_class_t   decoder_class;
} w32a_class_t;


/*
 * RGB->YUY2 conversion, we need is for xine video-codec ->
 * video-output interface
 *
 * YCbCr is defined per CCIR 601-1, except that Cb and Cr are
 * normalized to the range 0..MAXSAMPLE rather than -0.5 .. 0.5.
 * The conversion equations to be implemented are therefore
 *      Y  =  0.29900 * R + 0.58700 * G + 0.11400 * B
 *      Cb = -0.16874 * R - 0.33126 * G + 0.50000 * B  + CENTERSAMPLE
 *      Cr =  0.50000 * R - 0.41869 * G - 0.08131 * B  + CENTERSAMPLE
 * (These numbers are derived from TIFF 6.0 section 21, dated 3-June-92.)
 *
 * To avoid floating-point arithmetic, we represent the fractional
 * constants as integers scaled up by 2^16 (about 4 digits precision);
 * we have to divide the products by 2^16, with appropriate rounding,
 * to get the correct answer.
 *
 * FIXME: For the XShm video-out driver, this conversion is a huge
 * waste of time (converting from RGB->YUY2 here and converting back
 * from YUY2->RGB in the XShm driver).
 */
#define	MAXSAMPLE	255
#define	CENTERSAMPLE	128

#define	SCALEBITS	16
#define	FIX(x)		( (int32_t) ( (x) * (1<<SCALEBITS) + 0.5 ) )
#define	ONE_HALF	( (int32_t) (1<< (SCALEBITS-1)) )
#define	CBCR_OFFSET	(CENTERSAMPLE << SCALEBITS)

#define R_Y_OFF         0                       /* offset to R => Y section */
#define G_Y_OFF         (1*(MAXSAMPLE+1))       /* offset to G => Y section */
#define B_Y_OFF         (2*(MAXSAMPLE+1))       /* etc. */
#define R_CB_OFF        (3*(MAXSAMPLE+1))
#define G_CB_OFF        (4*(MAXSAMPLE+1))
#define B_CB_OFF        (5*(MAXSAMPLE+1))
#define R_CR_OFF        B_CB_OFF                /* B=>Cb, R=>Cr are the same */
#define G_CR_OFF        (6*(MAXSAMPLE+1))
#define B_CR_OFF        (7*(MAXSAMPLE+1))
#define TABLE_SIZE      (8*(MAXSAMPLE+1))


/*
 * HAS_SLOW_MULT:
 * 0: use integer multiplication in inner loop of rgb2yuv conversion
 * 1: use precomputed tables (avoids slow integer multiplication)
 *
 * (On a P-II/Athlon, the version using the precomputed tables is
 * slightly faster)
 */
#define	HAS_SLOW_MULT	1

#if	HAS_SLOW_MULT
static int32_t *rgb_ycc_tab;
#endif

static void w32v_init_rgb_ycc(void)
{
#if	HAS_SLOW_MULT
  /*
   * System has slow integer multiplication, so we precompute
   * the YCbCr constants times R,G,B for all possible values.
   */
  int i;

  if (rgb_ycc_tab) return;

  rgb_ycc_tab = malloc(TABLE_SIZE * sizeof(int32_t));

  for (i = 0; i <= MAXSAMPLE; i++) {
    rgb_ycc_tab[i+R_Y_OFF] = FIX(0.29900) * i;
    rgb_ycc_tab[i+G_Y_OFF] = FIX(0.58700) * i;
    rgb_ycc_tab[i+B_Y_OFF] = FIX(0.11400) * i     + ONE_HALF;
    rgb_ycc_tab[i+R_CB_OFF] = (-FIX(0.16874)) * i;
    rgb_ycc_tab[i+G_CB_OFF] = (-FIX(0.33126)) * i;
    /*
     * We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
     * This ensures that the maximum output will round to MAXJSAMPLE
     * not MAXJSAMPLE+1, and thus that we don't have to range-limit.
     */
    rgb_ycc_tab[i+B_CB_OFF] = FIX(0.50000) * i    + CBCR_OFFSET + ONE_HALF-1;
    /*
     * B=>Cb and R=>Cr tables are the same
    rgb_ycc_tab[i+R_CR_OFF] = FIX(0.50000) * i    + CBCR_OFFSET + ONE_HALF-1;
     */
    rgb_ycc_tab[i+G_CR_OFF] = (-FIX(0.41869)) * i;
    rgb_ycc_tab[i+B_CR_OFF] = (-FIX(0.08131)) * i;
  }
#endif
}

static int get_vids_codec_n_name(w32v_decoder_t *this, int buf_type)
{
  buf_type &= 0xffff0000;

  switch (buf_type) {
  case BUF_VIDEO_MSMPEG4_V1:
  case BUF_VIDEO_MSMPEG4_V2:
  case BUF_VIDEO_MSMPEG4_V3:
  case BUF_VIDEO_IV50:
  case BUF_VIDEO_IV41:
  case BUF_VIDEO_IV32:
  case BUF_VIDEO_IV31:
  case BUF_VIDEO_CINEPAK:
  case BUF_VIDEO_ATIVCR2:
  case BUF_VIDEO_I263:
  case BUF_VIDEO_MSVC:
  case BUF_VIDEO_DV:
  case BUF_VIDEO_VP31:
  case BUF_VIDEO_VP4:
  case BUF_VIDEO_VP5:
  case BUF_VIDEO_VP6:
  case BUF_VIDEO_MSS1:
  case BUF_VIDEO_TSCC:
  case BUF_VIDEO_UCOD:
    return 1;
  case BUF_VIDEO_WMV7:
  case BUF_VIDEO_WMV8:
  case BUF_VIDEO_WMV9:
    return 2;
  }

  return 0;
}

static char* get_vids_codec_name(w32v_decoder_t *this,
				 int buf_type, int n) {

  this->yuv_supported=0;
  this->yuv_hack_needed=0;
  this->flipped=0;
  this->driver_type = DRIVER_STD;
  this->ex_functions = 0;

  buf_type &= 0xffff0000;

  switch (buf_type) {
  case BUF_VIDEO_MSMPEG4_V1:
  case BUF_VIDEO_MSMPEG4_V2:
    /* Microsoft MPEG-4 v1/v2 */
    /* old dll is disabled now due segfaults
     * (using directshow instead)
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    return "mpg4c32.dll";
    */
    this->yuv_supported=1;
    this->driver_type = DRIVER_DS;
    this->guid=&msmpeg4_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS MPEG-4 V1/V2 (win32)");
    return "mpg4ds32.ax";

  case BUF_VIDEO_MSMPEG4_V3:
    /* Microsoft MPEG-4 v3 */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS MPEG-4 V3 (win32)");
    return "divxc32.dll";

  case BUF_VIDEO_IV50:
    /* Video in Indeo Video 5 format */
    this->yuv_supported=1;   /* YUV pic is upside-down :( */
    this->flipped=0;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Indeo Video 5 (win32)");
    return "ir50_32.dll";

  case BUF_VIDEO_IV41:
    /* Video in Indeo Video 4.1 format */
    this->flipped=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Indeo Video 4.1 (win32)");
    return "ir41_32.dll";

  case BUF_VIDEO_IV32:
    /* Video in Indeo Video 3.2 format */
    this->flipped=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Indeo Video 3.2 (win32)");
    return "ir32_32.dll";

  case BUF_VIDEO_IV31:
    /* Video in Indeo Video 3.1 format */
    this->flipped=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Indeo Video 3.1 (win32)");
    return "ir32_32.dll";

  case BUF_VIDEO_CINEPAK:
    /* Video in Cinepak format */
    this->flipped=1;
    this->yuv_supported=0;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Cinepak (win32)");
    return "iccvid.dll";

    /*** Only 16bit .DLL available (can't load under linux) ***
	 case mmioFOURCC('V', 'C', 'R', '1'):
	 printf("Video in ATI VCR1 format\n");
	 return "ativcr1.dll";
    */

  case BUF_VIDEO_ATIVCR2:
    /* Video in ATI VCR2 format */
    this->yuv_supported=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "ATI VCR2 (win32)");
    return "ativcr2.dll";

  case BUF_VIDEO_I263:
    /* Video in I263 format */
    this->flipped=1;
    this->yuv_supported=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "I263 (win32)");
    return "i263_32.drv";

  case BUF_VIDEO_MSVC:
    /* Video in Windows Video 1 */
    /* note: can't play streams with 8bpp */
    this->flipped=1;
    this->yuv_supported=0;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS Windows Video 1 (win32)");
    return "msvidc32.dll";

  case BUF_VIDEO_DV:
    /* Sony DV Codec (not working yet) */
    this->yuv_supported=1;
    this->driver_type = DRIVER_DS;
    this->guid=&dvsd_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Sony DV (win32)");
    return "qdv.dll";

  case BUF_VIDEO_WMV7:
    this->yuv_supported=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS WMV 7 (win32)");
    if (n == 2) {
      this->driver_type = DRIVER_DMO;
      this->guid=&wmvdmo_clsid;
      return "wmvdmod.dll";
    }
    this->driver_type = DRIVER_DS;
    this->guid=&wmv1_clsid;
    return "wmvds32.ax";

  case BUF_VIDEO_WMV8:
    this->yuv_supported=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS WMV 8 (win32)");
    if (n == 2) {
      this->driver_type = DRIVER_DMO;
      this->guid=&wmvdmo_clsid;
      return "wmvdmod.dll";
    }
    this->driver_type = DRIVER_DS;
    this->guid=&wmv2_clsid;
    return "wmv8ds32.ax";

  case BUF_VIDEO_WMV9:
    this->yuv_supported=1;
    this->driver_type = DRIVER_DMO;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "MS WMV 9 (win32)");
    if (n == 2) {
      this->guid=&wmvdmo_clsid;
      return "wmvdmod.dll";
    }
    this->guid=&wmv3_clsid;
    return "wmv9dmod.dll";

  case BUF_VIDEO_VP31:
    this->yuv_supported=1;
    this->ex_functions=1;
    this->flipped=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "On2 VP3.1 (win32)");
    return "vp31vfw.dll";

  case BUF_VIDEO_VP4:
    this->yuv_supported=1;
    this->ex_functions=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "On2 VP4 (win32)");
    return "vp4vfw.dll";

  case BUF_VIDEO_VP5:
    this->yuv_supported=1;
    this->ex_functions=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "On2 VP5 (win32)");
    return "vp5vfw.dll";

  case BUF_VIDEO_VP6:
    this->yuv_supported=1;
    this->ex_functions=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "On2 VP6 (win32)");
    return "vp6vfw.dll";

  case BUF_VIDEO_MSS1:
    this->driver_type = DRIVER_DS;
    this->guid=&mss1_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "Windows Screen Video (win32)");
    return "msscds32.ax";

  case BUF_VIDEO_TSCC:
    this->flipped=1;
    this->yuv_supported=0;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "TechSmith Screen Capture Codec (win32)");
    return "tsccvid.dll";

  case BUF_VIDEO_UCOD:
    this->yuv_supported=1;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
      "ClearVideo (win32)");
    return "clrviddd.dll";

  }

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	   "w32codec: this didn't happen: unknown video buf type %08x\n", buf_type);


  return NULL;
}

#undef IMGFMT_YUY2
#undef IMGFMT_YV12
#define IMGFMT_YUY2  mmioFOURCC('Y','U','Y','2')
#define IMGFMT_YV12  mmioFOURCC('Y','V','1','2')
#define IMGFMT_32RGB mmioFOURCC( 32,'R','G','B')
#define IMGFMT_24RGB mmioFOURCC( 24,'R','G','B')
#define IMGFMT_16RGB mmioFOURCC( 16,'R','G','B')
#define IMGFMT_15RGB mmioFOURCC( 15,'R','G','B')

static void w32v_init_codec (w32v_decoder_t *this, int buf_type) {

  HRESULT  ret;
  uint32_t vo_cap;
  int outfmt;

  lprintf ("init codec...\n");

  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);

  this->ldt_fs = Setup_LDT_Keeper();

  outfmt = IMGFMT_15RGB;
  if (this->yuv_supported) {
    vo_cap = this->stream->video_out->get_capabilities (this->stream->video_out);
    if (vo_cap & VO_CAP_YUY2)
      outfmt = IMGFMT_YUY2;
  }

  this->hic = ICOpen ((int)win32_codec_name,
		      this->bih->biCompression,
		      ICMODE_FASTDECOMPRESS);

  if(!this->hic){
    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("w32codec: ICOpen failed! unknown codec %08lx / wrong parameters?\n"),
              this->bih->biCompression);
    this->decoder_ok = 0;
    return;
  }

  ret = ICDecompressGetFormat(this->hic, this->bih, &this->o_bih);
  if(ret){
    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("w32codec: ICDecompressGetFormat (%.4s %08lx/%d) failed: Error %ld\n"),
              (char*)&this->o_bih.biCompression, this->bih->biCompression,
              this->bih->biBitCount, (long)ret);
    this->decoder_ok = 0;
    return;
  }

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	   "w32codec: video output format: %.4s %08lx\n",
	   (char*)&this->o_bih.biCompression, this->o_bih.biCompression);

  if(outfmt==IMGFMT_YUY2 || outfmt==IMGFMT_15RGB)
    this->o_bih.biBitCount=16;
  else
    this->o_bih.biBitCount=outfmt&0xFF;

  this->o_bih.biSizeImage = this->o_bih.biWidth * this->o_bih.biHeight
      * this->o_bih.biBitCount / 8;

  if (this->flipped)
    this->o_bih.biHeight=-this->bih->biHeight;

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');
  else
    this->o_bih.biCompression = 0;

  ret = (!this->ex_functions)
        ?ICDecompressQuery(this->hic, this->bih, &this->o_bih)
        :ICDecompressQueryEx(this->hic, this->bih, &this->o_bih);

  if(ret){
    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("w32codec: ICDecompressQuery failed: Error %ld\n"), (long)ret);
    this->decoder_ok = 0;
    return;
  }

  ret = (!this->ex_functions)
        ?ICDecompressBegin(this->hic, this->bih, &this->o_bih)
        :ICDecompressBeginEx(this->hic, this->bih, &this->o_bih);

  if(ret){
    xine_log (this->stream->xine, XINE_LOG_MSG,
              _("w32codec: ICDecompressBegin failed: Error %ld\n"), (long)ret);
    this->decoder_ok = 0;
    return;
  }

  if (outfmt==IMGFMT_YUY2 && this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');

  this->size = 0;

  if ( this->img_buffer )
    free (this->img_buffer);
  this->img_buffer = malloc (this->o_bih.biSizeImage);

  if ( this->buf )
    free (this->buf);
  this->bufsize = VIDEOBUFSIZE;
  this->buf = malloc(this->bufsize);

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  this->outfmt = outfmt;
  this->decoder_ok = 1;
}

static void w32v_init_ds_dmo_codec (w32v_decoder_t *this, int buf_type) {
  uint32_t vo_cap;
  int outfmt;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "w32codec: init DirectShow/DMO video codec...\n");

  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);

  this->ldt_fs = Setup_LDT_Keeper();

  /* hack: dvsd is the only fourcc accepted by qdv.dll */
  if( buf_type ==  BUF_VIDEO_DV )
    this->bih->biCompression = mmioFOURCC('d','v','s','d');

  if( this->driver_type == DRIVER_DS ) {
    this->ds_dec = DS_VideoDecoder_Open(win32_codec_name, this->guid,
                                          this->bih, this->flipped, 0);

    if(!this->ds_dec){
      xine_log (this->stream->xine, XINE_LOG_MSG,
                _("w32codec: DS_VideoDecoder failed! unknown codec %08lx / wrong parameters?\n"),
                this->bih->biCompression);
      this->decoder_ok = 0;
      return;
    }
  } else {
    this->dmo_dec = DMO_VideoDecoder_Open(win32_codec_name, this->guid,
                                          this->bih, this->flipped, 0);

    if(!this->dmo_dec){
      xine_log (this->stream->xine, XINE_LOG_MSG,
                _("w32codec: DMO_VideoDecoder failed! unknown codec %08lx / wrong parameters?\n"),
                this->bih->biCompression);
      this->decoder_ok = 0;
      return;
    }
  }

  outfmt = IMGFMT_15RGB;
  if (this->yuv_supported) {
    vo_cap = this->stream->video_out->get_capabilities (this->stream->video_out);
    if (vo_cap & VO_CAP_YUY2)
      outfmt = IMGFMT_YUY2;
  }

  if(outfmt==IMGFMT_YUY2 || outfmt==IMGFMT_15RGB )
    this->o_bih.biBitCount=16;
  else
    this->o_bih.biBitCount=outfmt&0xFF;

  this->o_bih.biWidth = this->bih->biWidth;
  this->o_bih.biHeight = this->bih->biHeight;

  this->o_bih.biSizeImage = this->o_bih.biWidth * this->o_bih.biHeight
      * this->o_bih.biBitCount / 8;

  if (this->flipped)
    this->o_bih.biHeight=-this->bih->biHeight;

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');
  else
    this->o_bih.biCompression = 0;

  if( this->driver_type == DRIVER_DS )
    DS_VideoDecoder_SetDestFmt(this->ds_dec, this->o_bih.biBitCount, this->o_bih.biCompression);
  else
    DMO_VideoDecoder_SetDestFmt(this->dmo_dec, this->o_bih.biBitCount, this->o_bih.biCompression);

  if (outfmt==IMGFMT_YUY2 && this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');

  if( this->driver_type == DRIVER_DS )
    DS_VideoDecoder_StartInternal(this->ds_dec);
  else
    DMO_VideoDecoder_StartInternal(this->dmo_dec);

  this->size = 0;

  if ( this->img_buffer )
    free (this->img_buffer);
  this->img_buffer = malloc (this->o_bih.biSizeImage);

  if ( this->buf )
    free (this->buf);
  this->bufsize = VIDEOBUFSIZE;
  this->buf = malloc(this->bufsize);

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  this->outfmt = outfmt;
  this->decoder_ok = 1;
}


static void w32v_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  lprintf ("processing packet type = %08x, buf->decoder_flags=%08x\n",
	   buf->type, buf->decoder_flags);

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);

    lprintf ("video_step is %lld\n", this->video_step);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    int num_decoders;

    if ( buf->type & 0xff )
      return;

    lprintf ("processing header ...\n");

    /* init package containing bih */
    if( this->bih )
      free( this->bih );
    this->bih = malloc(buf->size);
    memcpy ( this->bih, buf->content, buf->size );

    this->ratio = (double)this->bih->biWidth/(double)this->bih->biHeight;

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,    this->bih->biWidth);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,   this->bih->biHeight);

    pthread_mutex_lock(&win32_codec_mutex);
    num_decoders = get_vids_codec_n_name (this, buf->type);
    if (num_decoders != 0) {
      int i;

      for (i = 1; i <= num_decoders; i++) {
        win32_codec_name = get_vids_codec_name (this, buf->type, i);

        if( this->driver_type == DRIVER_STD )
          w32v_init_codec (this, buf->type);
        else if( this->driver_type == DRIVER_DS
		|| this->driver_type == DRIVER_DMO )
          w32v_init_ds_dmo_codec (this, buf->type);

	if (this->decoder_ok)
          break;
      }
    }

    if( !this->decoder_ok ) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("w32codec: decoder failed to start. Is '%s' installed?\n"),
              win32_codec_name );
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
      _x_message(this->stream, XINE_MSG_LIBRARY_LOAD_ERROR,
                   win32_codec_name, NULL);
    }

    pthread_mutex_unlock(&win32_codec_mutex);

    this->stream_id = -1;
    this->skipframes = 0;

  } else if (this->decoder_ok) {

    lprintf ("processing packet ...\n");

    if( (int) buf->size <= 0 )
        return;

    if( this->stream_id < 0 )
       this->stream_id = buf->type & 0xff;

    if( this->stream_id != (buf->type & 0xff) )
       return;

    if( this->size + buf->size > this->bufsize ) {
      this->bufsize = this->size + 2 * buf->size;
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "w32codec: increasing source buffer to %d to avoid overflow.\n", this->bufsize);
      this->buf = realloc( this->buf, this->bufsize );
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

      HRESULT     ret = 0;
      int         flags;
      vo_frame_t *img;
      uint8_t    *img_buffer = this->img_buffer;

      Check_FS_Segment(this->ldt_fs);

      /* decoder video frame */

      this->bih->biSizeImage = this->size;

      img = this->stream->video_out->get_frame (this->stream->video_out,
					this->bih->biWidth,
					this->bih->biHeight,
					this->ratio,
					IMGFMT_YUY2,
					VO_BOTH_FIELDS);

      img->duration = this->video_step;

      lprintf ("frame duration is %lld\n", this->video_step);

      if (this->outfmt==IMGFMT_YUY2)
         img_buffer = img->base[0];

      flags = 0;
      if( !(buf->decoder_flags & BUF_FLAG_KEYFRAME) )
        flags |= ICDECOMPRESS_NOTKEYFRAME;
      if( this->skipframes )
        flags |= ICDECOMPRESS_HURRYUP|ICDECOMPRESS_PREROL;

      if( this->skipframes && (buf->type & ~0xff) != BUF_VIDEO_IV32 )
        img_buffer = NULL;

      pthread_mutex_lock(&win32_codec_mutex);
      if( this->driver_type == DRIVER_STD )
        ret = (!this->ex_functions)
              ?ICDecompress(this->hic, flags,
			    this->bih, this->buf, &this->o_bih,
			    img_buffer)
              :ICDecompressEx(this->hic, flags,
			    this->bih, this->buf, &this->o_bih,
			    img_buffer);
      else if( this->driver_type == DRIVER_DS ) {
        ret = DS_VideoDecoder_DecodeInternal(this->ds_dec, this->buf, this->size,
                            buf->decoder_flags & BUF_FLAG_KEYFRAME,
                            img_buffer);
      } else if( this->driver_type == DRIVER_DMO ) {
        ret = DMO_VideoDecoder_DecodeInternal(this->dmo_dec, this->buf, this->size,
                            1,
                            img_buffer);
      }
      pthread_mutex_unlock(&win32_codec_mutex);

      if (!this->skipframes) {
        if (this->outfmt==IMGFMT_YUY2) {
	  /* already decoded into YUY2 format by DLL */
	  /*
	  xine_fast_memcpy(img->base[0], this->img_buffer,
	                   this->bih.biHeight*this->bih.biWidth*2);
	  */
        } else {
	  /* now, convert rgb to yuv */
	  int row, col;
#if	HAS_SLOW_MULT
	  int32_t *ctab = rgb_ycc_tab;
#endif

	  for (row=0; row<this->bih->biHeight; row++) {

	    uint16_t *pixel, *out;

	    pixel = (uint16_t *) ( (uint8_t *)this->img_buffer + 2 * row * this->o_bih.biWidth );
	    out = (uint16_t *) (img->base[0] + row * img->pitches[0] );

	    for (col=0; col<this->o_bih.biWidth; col++, pixel++, out++) {

	      uint8_t   r,g,b;
	      uint8_t   y,u,v;

	      b = (*pixel & 0x001F) << 3;
	      g = (*pixel & 0x03E0) >> 5 << 3;
	      r = (*pixel & 0x7C00) >> 10 << 3;

#if	HAS_SLOW_MULT
	      y = (ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF]) >> SCALEBITS;
	      if (!(col & 0x0001)) {
	        /* even pixel, do u */
	        u = (ctab[r+R_CB_OFF] + ctab[g+G_CB_OFF] + ctab[b+B_CB_OFF]) >> SCALEBITS;
	        *out = ( (uint16_t) u << 8) | (uint16_t) y;
	      } else {
	        /* odd pixel, do v */
	        v = (ctab[r+R_CR_OFF] + ctab[g+G_CR_OFF] + ctab[b+B_CR_OFF]) >> SCALEBITS;
	        *out = ( (uint16_t) v << 8) | (uint16_t) y;
	      }
#else
	      y = (FIX(0.299) * r + FIX(0.587) * g + FIX(0.114) * b + ONE_HALF) >> SCALEBITS;
	      if (!(col & 0x0001)) {
	        /* even pixel, do u */
	        u = (- FIX(0.16874) * r - FIX(0.33126) * g + FIX(0.5) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	        *out = ( (uint16_t) u << 8) | (uint16_t) y;
	      } else {
	        /* odd pixel, do v */
	        v = (FIX(0.5) * r - FIX(0.41869) * g - FIX(0.08131) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	        *out = ( (uint16_t) v << 8) | (uint16_t) y;
	      }
#endif
	      //printf("r %02x g %02x b %02x y %02x u %02x v %02x\n",r,g,b,y,u,v);
	    }
	  }
        }
      }

      img->pts = buf->pts;
      if (ret || this->skipframes) {
        if (!this->skipframes)
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "w32codec: Error decompressing frame, err=%ld\n", (long)ret);
	img->bad_frame = 1;
	lprintf ("BAD FRAME, duration is %d\n", img->duration);
      } else {
	img->bad_frame = 0;
	lprintf ("GOOD FRAME, duration is %d\n\n", img->duration);
      }

      this->skipframes = img->draw(img, this->stream);

      lprintf ("skipframes is %d\n", this->skipframes);

      if (this->skipframes < 0)
        this->skipframes = 0;
      img->free(img);

      this->size = 0;
    }

    /* printf ("w32codec: processing packet done\n"); */
  }
}

static void w32v_flush (video_decoder_t *this_gen) {
}

static void w32v_reset (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  /* FIXME: need to improve this function. currently it
     doesn't avoid artifacts when seeking. */

  pthread_mutex_lock(&win32_codec_mutex);
  if( this->driver_type == DRIVER_STD ) {
    if( this->hic )
    {
      if (!this->ex_functions)
        ICDecompressBegin(this->hic, this->bih, &this->o_bih);
      else
        ICDecompressBeginEx(this->hic, this->bih, &this->o_bih);
    }
  } else if ( this->driver_type == DRIVER_DS ) {
  }
  this->size = 0;
  pthread_mutex_unlock(&win32_codec_mutex);
}

static void w32v_discontinuity (video_decoder_t *this_gen) {
}


static void w32v_dispose (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  pthread_mutex_lock(&win32_codec_mutex);
  if ( this->driver_type == DRIVER_STD ) {
    if( this->hic ) {
      ICDecompressEnd(this->hic);
      ICClose(this->hic);
    }
  } else if ( this->driver_type == DRIVER_DS ) {
    if( this->ds_dec )
      DS_VideoDecoder_Destroy(this->ds_dec);
    this->ds_dec = NULL;
  } else if ( this->driver_type == DRIVER_DMO ) {
    if( this->dmo_dec )
      DMO_VideoDecoder_Destroy(this->dmo_dec);
    this->dmo_dec = NULL;
  }
  Restore_LDT_Keeper( this->ldt_fs );
  pthread_mutex_unlock(&win32_codec_mutex);

  if ( this->img_buffer ) {
    free (this->img_buffer);
    this->img_buffer = NULL;
  }

  if ( this->buf ) {
    free (this->buf);
    this->buf = NULL;
  }

  if( this->bih ) {
    free (this->bih);
    this->bih = NULL;
  }

  if( this->decoder_ok )  {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this);
}

/*
 * audio stuff
 */

static char* get_auds_codec_name(w32a_decoder_t *this, int buf_type) {

  buf_type = buf_type & 0xFFFF0000;
  this->driver_type = DRIVER_STD;

  switch (buf_type) {
  case BUF_AUDIO_WMAV1:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Windows Media Audio v1 (win32)");
    return "divxa32.acm";
  case BUF_AUDIO_WMAV2:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Windows Media Audio v2 (win32)");
    return "divxa32.acm";
  case BUF_AUDIO_WMAPRO:
    this->driver_type = DRIVER_DMO;
    this->guid=&wma3_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Windows Media Audio Professional (win32)");
    return "wma9dmod.dll";
  case BUF_AUDIO_WMALL:
    this->driver_type = DRIVER_DMO;
    this->guid=&wma3_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Windows Media Audio Lossless (win32)");
    return "wma9dmod.dll";
  case BUF_AUDIO_WMAV:
    this->driver_type = DRIVER_DMO;
    this->guid=&wmav_clsid;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Windows Media Audio Voice (win32)");
    return "wmspdmod.dll";
  case BUF_AUDIO_MSADPCM:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "MS ADPCM (win32)");
    return "msadp32.acm";
  case BUF_AUDIO_MSIMAADPCM:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "MS IMA ADPCM (win32)");
    return "imaadp32.acm";
  case BUF_AUDIO_MSGSM:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "MS GSM (win32)");
    return "msgsm32.acm";
  case BUF_AUDIO_IMC:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Intel Music Coder (win32)");
    return "imc32.acm";
  case BUF_AUDIO_LH:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Lernout & Hauspie (win32)");
    return "lhacm.acm";
  case BUF_AUDIO_VOXWARE:
    this->driver_type = DRIVER_DS;
    this->guid=&CLSID_Voxware;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Voxware Metasound (win32)");
    return "voxmsdec.ax";
  case BUF_AUDIO_ACELPNET:
    this->driver_type = DRIVER_DS;
    this->guid=&CLSID_Acelp;
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "ACELP.net (win32)");
    return "acelpdec.ax";
  case BUF_AUDIO_VIVOG723:
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_AUDIOCODEC,
      "Vivo G.723/Siren Audio Codec (win32)");
    return "vivog723.acm";
  }

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	   "w32codec: this didn't happen: unknown audio buf type %08x\n", buf_type);
  return NULL;
}

static void w32a_reset (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->size = 0;
}

static void w32a_discontinuity (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->pts = this->lastpts = this->sumpts = this->sumsize = 0;
}

static int w32a_init_audio (w32a_decoder_t *this,
                            uint8_t *buf, int bufsize, int buftype) {

  HRESULT ret;
  WAVEFORMATEX wf;
  WAVEFORMATEX *in_fmt;
  unsigned long in_size;
  unsigned long out_size;
  audio_buffer_t *audio_buffer;
  int audio_buffer_mem_size;

  in_fmt = (WAVEFORMATEX *)buf;
  in_size=in_fmt->nBlockAlign;

  this->srcstream = 0;
  this->num_channels  = (in_fmt->nChannels >= 2)?2:1;
  this->rate = in_fmt->nSamplesPerSec;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  this->output_open = (this->stream->audio_out->open) ( this->stream->audio_out, this->stream,
					      16, in_fmt->nSamplesPerSec,
					      _x_ao_channels2mode(in_fmt->nChannels));
  if (!this->output_open) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: (ACM_Decoder) Cannot open audio output device\n");
    return 0;
  }

  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
  audio_buffer_mem_size = audio_buffer->mem_size;
  audio_buffer->num_frames = 0;
  audio_buffer->vpts       = 0;
  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

  wf.nChannels       = (in_fmt->nChannels >= 2)?2:1;
  wf.nSamplesPerSec  = in_fmt->nSamplesPerSec;
  wf.nAvgBytesPerSec = 2*wf.nSamplesPerSec*wf.nChannels;
  wf.wFormatTag      = WAVE_FORMAT_PCM;
  wf.nBlockAlign     = 2*in_fmt->nChannels;
  wf.wBitsPerSample  = 16;
  wf.cbSize          = 0;

  this->ldt_fs = Setup_LDT_Keeper();
  win32_codec_name = get_auds_codec_name (this, buftype);

  if( this->driver_type == DRIVER_STD ) {

    MSACM_RegisterDriver(win32_codec_name, in_fmt->wFormatTag, 0);

    ret=acmStreamOpen(&this->srcstream,(HACMDRIVER)NULL,
                      in_fmt,
                      &wf,
                      NULL,0,0,0);
    if(ret){
      if(ret==ACMERR_NOTPOSSIBLE)
        xine_log (this->stream->xine, XINE_LOG_MSG,
                  _("w32codec: (ACM_Decoder) Unappropriate audio format\n"));
      else
        xine_log (this->stream->xine, XINE_LOG_MSG,
                  _("w32codec: (ACM_Decoder) acmStreamOpen error %d\n"), (int) ret);
      this->srcstream = 0;
      return 0;
    }

    acmStreamSize(this->srcstream, in_size, &out_size, ACM_STREAMSIZEF_SOURCE);
    out_size*=2;
    if(out_size < audio_buffer_mem_size)
      out_size=audio_buffer_mem_size;

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: Audio buffer min. size: %d\n",(int)out_size);

    acmStreamSize(this->srcstream, out_size, (LPDWORD) &this->rec_audio_src_size,
      ACM_STREAMSIZEF_DESTINATION);
  } else if( this->driver_type == DRIVER_DS ) {
    this->ds_dec=DS_AudioDecoder_Open(win32_codec_name,this->guid, in_fmt);

    if( this->ds_dec == NULL ) {
      xine_log (this->stream->xine, XINE_LOG_MSG, _("w32codec: Error initializing DirectShow Audio\n"));
      this->srcstream = 0;
      return 0;
    }

    out_size = audio_buffer_mem_size;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: output buffer size: %d\n",(int)out_size);
    this->rec_audio_src_size=DS_AudioDecoder_GetSrcSize(this->ds_dec,out_size);

    /* somehow DS_Filters seems to eat more than rec_audio_src_size if the output
       buffer is big enough. Doubling rec_audio_src_size should make this
       impossible */
    this->rec_audio_src_size*=2;
  } else if( this->driver_type == DRIVER_DMO ) {
    this->dmo_dec=DMO_AudioDecoder_Open(win32_codec_name,this->guid, in_fmt, wf.nChannels);

    if( this->dmo_dec == NULL ) {
      xine_log (this->stream->xine, XINE_LOG_MSG, _("w32codec: Error initializing DMO Audio\n"));
      this->srcstream = 0;
      return 0;
    }

    out_size = audio_buffer_mem_size;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: output buffer size: %d\n",(int)out_size);
    this->rec_audio_src_size=DMO_AudioDecoder_GetSrcSize(this->dmo_dec,out_size);

    /* i don't know if DMO has the same problem as above. so, just in case... */
    this->rec_audio_src_size*=2;
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "w32codec: Recommended source buffer size: %d\n", this->rec_audio_src_size);

  if( this->rec_audio_src_size < in_fmt->nBlockAlign ) {
    this->rec_audio_src_size = in_fmt->nBlockAlign;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: adjusting source buffer size to %d\n", this->rec_audio_src_size);
  }

  if( this->buf )
    free(this->buf);

  if( this->outbuf )
    free(this->outbuf);

  this->max_audio_src_size = 2 * this->rec_audio_src_size;

  this->buf = malloc( this->max_audio_src_size );

  out_size += 32768;
  this->outbuf = malloc( out_size );
  this->outsize = out_size;

  this->size = 0;
  this->pts = this->lastpts = this->sumpts = this->sumsize = 0;

  return 1;
}


static void w32a_ensure_buffer_size(w32a_decoder_t *this, int size) {
  if( this->size + size > this->max_audio_src_size ) {
    this->max_audio_src_size = this->size + 2 * size;
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "w32codec: increasing source buffer to %d to avoid overflow.\n",
      this->max_audio_src_size);
    this->buf = realloc( this->buf, this->max_audio_src_size );
  }
}


static void w32a_decode_audio (w32a_decoder_t *this,
			       unsigned char *data,
			       uint32_t size,
			       int frame_end,
			       int64_t pts) {

  static ACMSTREAMHEADER ash;
  HRESULT hr = 0;
  int size_read, size_written;
  int delay;
  /* DWORD srcsize=0; */

  /* This code needs more testing */
  /* bitrate computing (byterate: byte per pts) */
  if (pts && (pts != this->lastpts)) {
    this->pts = pts;
    if (!this->lastpts) {
      this->byterate = 0.0;
    } else {
      this->sumpts = pts - this->lastpts;
      if (this->byterate == 0.0) {
        this->byterate = (double)this->sumsize / (double)this->sumpts;
      } else {
        /* smooth the bitrate */
        this->byterate = (9 * this->byterate +
                         (double)this->sumsize / (double)this->sumpts) / 10;
      }
    }
    this->lastpts = pts;
    this->sumsize = 0;
  }
  /* output_buf->pts = this->pts - delay */
  if (this->byterate)
    delay = (int)((double)this->size / this->byterate);
  else
    delay = 0;

  this->sumsize += size;

  w32a_ensure_buffer_size(this, this->size + size);
  xine_fast_memcpy (&this->buf[this->size], data, size);

  lprintf("w32a_decode_audio: demux pts=%lld, this->size=%d, d=%d, rate=%lf\n",
	  pts, this->size, delay, this->byterate);

  this->size += size;

  while (this->size >= this->rec_audio_src_size) {
    memset(&ash, 0, sizeof(ash));
    ash.cbStruct=sizeof(ash);
    ash.fdwStatus=0;
    ash.dwUser=0;
    ash.pbSrc=this->buf;
    ash.cbSrcLength=this->rec_audio_src_size;
    ash.pbDst=this->outbuf;
    ash.cbDstLength=this->outsize;

    lprintf ("decoding %d of %d bytes (%02x %02x %02x %02x ... %02x %02x)\n",
	     this->rec_audio_src_size, this->size,
	     this->buf[0], this->buf[1], this->buf[2], this->buf[3],
	     this->buf[this->rec_audio_src_size-2], this->buf[this->rec_audio_src_size-1]);

    pthread_mutex_lock(&win32_codec_mutex);
    if( this->driver_type == DRIVER_STD ) {
      hr=acmStreamPrepareHeader(this->srcstream,&ash,0);
      if(hr){
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"w32codec: (ACM_Decoder) acmStreamPrepareHeader error %d\n",(int)hr);
        pthread_mutex_unlock(&win32_codec_mutex);
        return;
      }

      hr=acmStreamConvert(this->srcstream,&ash,0);
    } else if( this->driver_type == DRIVER_DS ){
      hr=DS_AudioDecoder_Convert(this->ds_dec, ash.pbSrc, ash.cbSrcLength,
			     ash.pbDst, ash.cbDstLength,
		             &size_read, &size_written );
       ash.cbSrcLengthUsed = size_read;
       ash.cbDstLengthUsed = size_written;
    } else if( this->driver_type == DRIVER_DMO ){
      hr=DMO_AudioDecoder_Convert(this->dmo_dec, ash.pbSrc, ash.cbSrcLength,
			     ash.pbDst, ash.cbDstLength,
		             &size_read, &size_written );
       ash.cbSrcLengthUsed = size_read;
       ash.cbDstLengthUsed = size_written;
    }
    pthread_mutex_unlock(&win32_codec_mutex);

    if(hr){
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "w32codec: stream convert error %d, used %d bytes\n", (int)hr,(int)ash.cbSrcLengthUsed);
      this->size-=ash.cbSrcLength;
    } else {
      int DstLengthUsed, bufsize;
      audio_buffer_t *audio_buffer;
      char *p;

      lprintf ("acmStreamConvert worked, used %d bytes, generated %d bytes\n",
	       ash.cbSrcLengthUsed, ash.cbDstLengthUsed);

      DstLengthUsed = ash.cbDstLengthUsed;
      p = this->outbuf;

      while( DstLengthUsed )
      {
        audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

	if( DstLengthUsed < audio_buffer->mem_size )
	  bufsize = DstLengthUsed;
	else
	  bufsize = audio_buffer->mem_size;

        xine_fast_memcpy( audio_buffer->mem, p, bufsize );

	audio_buffer->num_frames = bufsize / (this->num_channels*2);
        if (this->pts)
          audio_buffer->vpts = this->pts - delay;
        else
          audio_buffer->vpts = 0;

        lprintf("w32a_decode_audio: decoder pts=%lld\n", audio_buffer->vpts);

	this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

	this->pts = 0;
        DstLengthUsed -= bufsize;
	p += bufsize;
      }
    }
    if(ash.cbSrcLengthUsed>=this->size){
      this->size=0;
    } else {
      this->size-=ash.cbSrcLengthUsed;
      xine_fast_memcpy( this->buf, &this->buf [ash.cbSrcLengthUsed], this->size);
    }

    pthread_mutex_lock(&win32_codec_mutex);
    if( this->driver_type == DRIVER_STD ) {
      hr=acmStreamUnprepareHeader(this->srcstream,&ash,0);
      if(hr){
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"w32codec: (ACM_Decoder) acmStreamUnprepareHeader error %d\n",(int)hr);
      }
    }
    pthread_mutex_unlock(&win32_codec_mutex);
  }
}

static void w32a_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    lprintf ("preview data ignored.\n");
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    lprintf ("got audio header\n");

    /* accumulate init data */
    w32a_ensure_buffer_size(this, this->size + buf->size);
    memcpy(this->buf + this->size, buf->content, buf->size);
    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
      pthread_mutex_lock(&win32_codec_mutex);
      this->decoder_ok = w32a_init_audio (this, this->buf, this->size, buf->type);

      if( !this->decoder_ok ) {
        xine_log (this->stream->xine, XINE_LOG_MSG,
                  _("w32codec: decoder failed to start. Is '%s' installed?\n"), win32_codec_name );
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_HANDLED, 0);
      }
      pthread_mutex_unlock(&win32_codec_mutex);
    }

  } else if (this->decoder_ok) {
    lprintf ("decoding %d data bytes...\n", buf->size);

    if( (int)buf->size <= 0 )
      return;

    Check_FS_Segment(this->ldt_fs);

    w32a_decode_audio (this, buf->content, buf->size,
		       buf->decoder_flags & BUF_FLAG_FRAME_END,
		       buf->pts);
  }
}


static void w32a_dispose (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  pthread_mutex_lock(&win32_codec_mutex);
  if( this->driver_type == DRIVER_STD ) {
      if( this->srcstream ) {
      acmStreamClose(this->srcstream, 0);
      this->srcstream = 0;
    }
  } else if( this->driver_type == DRIVER_DS ) {
    if( this->ds_dec )
      DS_AudioDecoder_Destroy(this->ds_dec);
    this->ds_dec = NULL;
  } else if( this->driver_type == DRIVER_DMO ) {
    if( this->dmo_dec )
      DMO_AudioDecoder_Destroy(this->dmo_dec);
    this->dmo_dec = NULL;
  }

  Restore_LDT_Keeper(this->ldt_fs);
  pthread_mutex_unlock(&win32_codec_mutex);

  if( this->buf ) {
    free(this->buf);
    this->buf = NULL;
  }

  if( this->outbuf ) {
    free(this->outbuf);
    this->outbuf = NULL;
  }

  this->decoder_ok = 0;

  if (this->output_open) {
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
    this->output_open = 0;
  }

  free (this);
}

static video_decoder_t *open_video_decoder_plugin (video_decoder_class_t *class_gen,
						   xine_stream_t *stream) {

  w32v_decoder_t *this ;

  this = (w32v_decoder_t *) calloc(1, sizeof(w32v_decoder_t));

  this->video_decoder.decode_data         = w32v_decode_data;
  this->video_decoder.flush               = w32v_flush;
  this->video_decoder.reset               = w32v_reset;
  this->video_decoder.discontinuity       = w32v_discontinuity;
  this->video_decoder.dispose             = w32v_dispose;

  this->stream      = stream;
  this->decoder_ok  = 0;

  return &this->video_decoder;
}

/*
 * video decoder class
 */

static void init_routine(void) {
  pthread_mutex_init (&win32_codec_mutex, NULL);
  w32v_init_rgb_ycc();
}

static void *init_video_decoder_class (xine_t *xine, void *data) {

  w32v_class_t   *this;
  config_values_t *cfg;

  cfg = xine->config;
  if ((win32_def_path = get_win32_codecs_path(cfg)) == NULL) return NULL;

  this = (w32v_class_t *) calloc(1, sizeof(w32v_class_t));

  this->decoder_class.open_plugin     = open_video_decoder_plugin;
  this->decoder_class.identifier      = "w32v";
  this->decoder_class.description     = N_("win32 binary video codec plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  pthread_once (&once_control, init_routine);

  return this;
}

/********************************************************
 * audio part
 */

static audio_decoder_t *open_audio_decoder_plugin (audio_decoder_class_t *class_gen,
						   xine_stream_t *stream) {

  w32a_decoder_t *this ;

  this = (w32a_decoder_t *) calloc(1, sizeof(w32a_decoder_t));

  this->audio_decoder.decode_data         = w32a_decode_data;
  this->audio_decoder.reset               = w32a_reset;
  this->audio_decoder.discontinuity       = w32a_discontinuity;
  this->audio_decoder.dispose             = w32a_dispose;

  this->stream      = stream;
  this->output_open = 0;
  this->decoder_ok  = 0;

  this->buf         = NULL;
  this->outbuf      = NULL;

  return &this->audio_decoder;
}

/*
 * audio decoder plugin class
 */
static void *init_audio_decoder_class (xine_t *xine, void *data) {

  w32a_class_t    *this;
  config_values_t *cfg;

  cfg = xine->config;
  if ((win32_def_path = get_win32_codecs_path(cfg)) == NULL) return NULL;

  this = (w32a_class_t *) calloc(1, sizeof(w32a_class_t));

  this->decoder_class.open_plugin     = open_audio_decoder_plugin;
  this->decoder_class.identifier      = "win32 audio";
  this->decoder_class.description     = N_("win32 binary audio codec plugin");
  this->decoder_class.dispose         = default_audio_decoder_class_dispose;

  pthread_once (&once_control, init_routine);

  return this;
}


/*
 * exported plugin catalog entry
 */

static const uint32_t video_types[] = {
  BUF_VIDEO_MSMPEG4_V1, BUF_VIDEO_MSMPEG4_V2, BUF_VIDEO_MSMPEG4_V3,
  BUF_VIDEO_IV50, BUF_VIDEO_IV41, BUF_VIDEO_IV32, BUF_VIDEO_IV31,
  BUF_VIDEO_CINEPAK, /* BUF_VIDEO_ATIVCR1, */
  BUF_VIDEO_ATIVCR2, BUF_VIDEO_I263, BUF_VIDEO_MSVC,
  BUF_VIDEO_DV, BUF_VIDEO_WMV7, BUF_VIDEO_WMV8, BUF_VIDEO_WMV9,
  BUF_VIDEO_VP31, BUF_VIDEO_MSS1, BUF_VIDEO_TSCC, BUF_VIDEO_UCOD,
  BUF_VIDEO_VP4, BUF_VIDEO_VP5, BUF_VIDEO_VP6,
  0
 };

static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

static const uint32_t audio_types[] = {
  BUF_AUDIO_WMAV1, BUF_AUDIO_WMAV2, BUF_AUDIO_WMAPRO, BUF_AUDIO_MSADPCM,
  BUF_AUDIO_MSIMAADPCM, BUF_AUDIO_MSGSM, BUF_AUDIO_IMC, BUF_AUDIO_LH,
  BUF_AUDIO_VOXWARE, BUF_AUDIO_ACELPNET, BUF_AUDIO_VIVOG723, BUF_AUDIO_WMAV,
  BUF_AUDIO_WMALL,
  0
 };

static const decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "win32v", XINE_VERSION_CODE, &dec_info_video, init_video_decoder_class },
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 16, "win32a", XINE_VERSION_CODE, &dec_info_audio, init_audio_decoder_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
