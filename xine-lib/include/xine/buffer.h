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
 *
 * contents:
 *
 * buffer_entry structure - serves as a transport encapsulation
 *   of the mpeg audio/video data through xine
 *
 * free buffer pool management routines
 *
 * FIFO buffer structures/routines
 */

#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>

#include <xine/os_types.h>
#include <xine/attributes.h>

#define BUF_MAX_CALLBACKS 5

/**
 * @defgroup buffer_types Buffer Types
 *
 * a buffer type ID describes the contents of a buffer
 * it consists of three fields:
 *
 * buf_type = 0xMMDDCCCC
 *
 * MM   : major buffer type (CONTROL, VIDEO, AUDIO, SPU)
 * DD   : decoder selection (e.g. MPEG, OPENDIVX ... for VIDEO)
 * CCCC : channel number or other subtype information for the decoder
 */
/*@{*/

#define BUF_MAJOR_MASK       0xFF000000
#define BUF_DECODER_MASK     0x00FF0000

/**
 * @defgroup buffer_ctrl Control buffer types
 */
/*@{*/
#define BUF_CONTROL_BASE            0x01000000
#define BUF_CONTROL_START           0x01000000
#define BUF_CONTROL_END             0x01010000
#define BUF_CONTROL_QUIT            0x01020000
#define BUF_CONTROL_DISCONTINUITY   0x01030000 /**< former AVSYNC_RESET */
#define BUF_CONTROL_NOP             0x01040000
#define BUF_CONTROL_AUDIO_CHANNEL   0x01050000
#define BUF_CONTROL_SPU_CHANNEL     0x01060000
#define BUF_CONTROL_NEWPTS          0x01070000
#define BUF_CONTROL_RESET_DECODER   0x01080000
#define BUF_CONTROL_HEADERS_DONE    0x01090000
#define BUF_CONTROL_FLUSH_DECODER   0x010a0000
#define BUF_CONTROL_RESET_TRACK_MAP 0x010b0000
/*@}*/

/**
 * @defgroup buffer_video Video buffer types
 * @note (please keep in sync with buffer_types.c)
 */
/*@{*/
#define BUF_VIDEO_BASE		0x02000000
#define BUF_VIDEO_UNKNOWN	0x02ff0000 /**< no decoder should handle this one */
#define BUF_VIDEO_MPEG		0x02000000
#define BUF_VIDEO_MPEG4		0x02010000
#define BUF_VIDEO_CINEPAK	0x02020000
#define BUF_VIDEO_SORENSON_V1	0x02030000
#define BUF_VIDEO_MSMPEG4_V2	0x02040000
#define BUF_VIDEO_MSMPEG4_V3	0x02050000
#define BUF_VIDEO_MJPEG		0x02060000
#define BUF_VIDEO_IV50		0x02070000
#define BUF_VIDEO_IV41		0x02080000
#define BUF_VIDEO_IV32		0x02090000
#define BUF_VIDEO_IV31		0x020a0000
#define BUF_VIDEO_ATIVCR1	0x020b0000
#define BUF_VIDEO_ATIVCR2	0x020c0000
#define BUF_VIDEO_I263		0x020d0000
#define BUF_VIDEO_RV10		0x020e0000
#define BUF_VIDEO_RGB		0x02100000
#define BUF_VIDEO_YUY2		0x02110000
#define BUF_VIDEO_JPEG		0x02120000
#define BUF_VIDEO_WMV7		0x02130000
#define BUF_VIDEO_WMV8		0x02140000
#define BUF_VIDEO_MSVC		0x02150000
#define BUF_VIDEO_DV		0x02160000
#define BUF_VIDEO_REAL	0x02170000
#define BUF_VIDEO_VP31		0x02180000
#define BUF_VIDEO_H263		0x02190000
#define BUF_VIDEO_3IVX          0x021A0000
#define BUF_VIDEO_CYUV          0x021B0000
#define BUF_VIDEO_DIVX5         0x021C0000
#define BUF_VIDEO_XVID          0x021D0000
#define BUF_VIDEO_SMC		0x021E0000
#define BUF_VIDEO_RPZA		0x021F0000
#define BUF_VIDEO_QTRLE		0x02200000
#define BUF_VIDEO_MSRLE		0x02210000
#define BUF_VIDEO_DUCKTM1	0x02220000
#define BUF_VIDEO_FLI		0x02230000
#define BUF_VIDEO_ROQ		0x02240000
#define BUF_VIDEO_SORENSON_V3	0x02250000
#define BUF_VIDEO_MSMPEG4_V1	0x02260000
#define BUF_VIDEO_MSS1		0x02270000
#define BUF_VIDEO_IDCIN		0x02280000
#define BUF_VIDEO_PGVV		0x02290000
#define BUF_VIDEO_ZYGO		0x022A0000
#define BUF_VIDEO_TSCC		0x022B0000
#define BUF_VIDEO_YVU9		0x022C0000
#define BUF_VIDEO_VQA		0x022D0000
#define BUF_VIDEO_GREY		0x022E0000
#define BUF_VIDEO_XXAN		0x022F0000
#define BUF_VIDEO_WC3		0x02300000
#define BUF_VIDEO_YV12		0x02310000
#define BUF_VIDEO_SEGA		0x02320000
#define BUF_VIDEO_RV20		0x02330000
#define BUF_VIDEO_RV30		0x02340000
#define BUF_VIDEO_MVI2		0x02350000
#define BUF_VIDEO_UCOD		0x02360000
#define BUF_VIDEO_WMV9		0x02370000
#define BUF_VIDEO_INTERPLAY	0x02380000
#define BUF_VIDEO_RV40		0x02390000
#define BUF_VIDEO_PSX_MDEC	0x023A0000
#define BUF_VIDEO_YUV_FRAMES	0x023B0000 /**< uncompressed YUV, delivered by v4l input plugin */
#define BUF_VIDEO_HUFFYUV	0x023C0000
#define BUF_VIDEO_IMAGE		0x023D0000
#define BUF_VIDEO_THEORA        0x023E0000
#define BUF_VIDEO_4XM           0x023F0000
#define BUF_VIDEO_I420		0x02400000
#define BUF_VIDEO_VP4           0x02410000
#define BUF_VIDEO_VP5           0x02420000
#define BUF_VIDEO_VP6           0x02430000
#define BUF_VIDEO_VMD		0x02440000
#define BUF_VIDEO_MSZH		0x02450000
#define BUF_VIDEO_ZLIB		0x02460000
#define BUF_VIDEO_8BPS		0x02470000
#define BUF_VIDEO_ASV1		0x02480000
#define BUF_VIDEO_ASV2		0x02490000
#define BUF_VIDEO_BITPLANE	0x024A0000 /**< Amiga typical picture and animation format */
#define BUF_VIDEO_BITPLANE_BR1	0x024B0000 /**< the same with Bytrun compression 1 */
#define BUF_VIDEO_FLV1		0x024C0000
#define BUF_VIDEO_H264		0x024D0000
#define BUF_VIDEO_MJPEG_B	0x024E0000
#define BUF_VIDEO_H261		0x024F0000
#define BUF_VIDEO_AASC		0x02500000
#define BUF_VIDEO_LOCO		0x02510000
#define BUF_VIDEO_QDRW		0x02520000
#define BUF_VIDEO_QPEG		0x02530000
#define BUF_VIDEO_ULTI		0x02540000
#define BUF_VIDEO_WNV1		0x02550000
#define BUF_VIDEO_XL		0x02560000
#define BUF_VIDEO_RT21		0x02570000
#define BUF_VIDEO_FPS1		0x02580000
#define BUF_VIDEO_DUCKTM2	0x02590000
#define BUF_VIDEO_CSCD		0x025A0000
#define BUF_VIDEO_ALGMM		0x025B0000
#define BUF_VIDEO_ZMBV		0x025C0000
#define BUF_VIDEO_AVS		0x025D0000
#define BUF_VIDEO_SMACKER	0x025E0000
#define BUF_VIDEO_NUV		0x025F0000
#define BUF_VIDEO_KMVC		0x02600000
#define BUF_VIDEO_FLASHSV	0x02610000
#define BUF_VIDEO_CAVS		0x02620000
#define BUF_VIDEO_VP6F		0x02630000
#define BUF_VIDEO_THEORA_RAW	0x02640000
#define BUF_VIDEO_VC1		0x02650000
#define BUF_VIDEO_VMNC		0x02660000
#define BUF_VIDEO_SNOW		0x02670000
#define BUF_VIDEO_VP8		0x02680000
/*@}*/

/**
 * @defgroup buffer_audio Audio buffer types
 * @note (please keep in sync with buffer_types.c)
 */
/*@{*/
#define BUF_AUDIO_BASE		0x03000000
#define BUF_AUDIO_UNKNOWN	0x03ff0000 /**< no decoder should handle this one */
#define BUF_AUDIO_A52		0x03000000
#define BUF_AUDIO_MPEG		0x03010000
#define BUF_AUDIO_LPCM_BE	0x03020000
#define BUF_AUDIO_LPCM_LE	0x03030000
#define BUF_AUDIO_WMAV1		0x03040000
#define BUF_AUDIO_DTS		0x03050000
#define BUF_AUDIO_MSADPCM	0x03060000
#define BUF_AUDIO_MSIMAADPCM	0x03070000
#define BUF_AUDIO_MSGSM		0x03080000
#define BUF_AUDIO_VORBIS        0x03090000
#define BUF_AUDIO_IMC           0x030a0000
#define BUF_AUDIO_LH            0x030b0000
#define BUF_AUDIO_VOXWARE       0x030c0000
#define BUF_AUDIO_ACELPNET      0x030d0000
#define BUF_AUDIO_AAC           0x030e0000
#define BUF_AUDIO_DNET	0x030f0000
#define BUF_AUDIO_VIVOG723      0x03100000
#define BUF_AUDIO_DK3ADPCM	0x03110000
#define BUF_AUDIO_DK4ADPCM	0x03120000
#define BUF_AUDIO_ROQ		0x03130000
#define BUF_AUDIO_QTIMAADPCM	0x03140000
#define BUF_AUDIO_MAC3		0x03150000
#define BUF_AUDIO_MAC6		0x03160000
#define BUF_AUDIO_QDESIGN1	0x03170000
#define BUF_AUDIO_QDESIGN2	0x03180000
#define BUF_AUDIO_QCLP		0x03190000
#define BUF_AUDIO_SMJPEG_IMA	0x031A0000
#define BUF_AUDIO_VQA_IMA	0x031B0000
#define BUF_AUDIO_MULAW		0x031C0000
#define BUF_AUDIO_ALAW		0x031D0000
#define BUF_AUDIO_GSM610	0x031E0000
#define BUF_AUDIO_EA_ADPCM      0x031F0000
#define BUF_AUDIO_WMAV2		0x03200000
#define BUF_AUDIO_COOK		0x03210000
#define BUF_AUDIO_ATRK		0x03220000
#define BUF_AUDIO_14_4		0x03230000
#define BUF_AUDIO_28_8		0x03240000
#define BUF_AUDIO_SIPRO		0x03250000
#define BUF_AUDIO_WMAPRO	0x03260000
#define BUF_AUDIO_WMAV3	BUF_AUDIO_WMAPRO
#define BUF_AUDIO_INTERPLAY	0x03270000
#define BUF_AUDIO_XA_ADPCM	0x03280000
#define BUF_AUDIO_WESTWOOD	0x03290000
#define BUF_AUDIO_DIALOGIC_IMA	0x032A0000
#define BUF_AUDIO_NSF		0x032B0000
#define BUF_AUDIO_FLAC		0x032C0000
#define BUF_AUDIO_DV		0x032D0000
#define BUF_AUDIO_WMAV		0x032E0000
#define BUF_AUDIO_SPEEX		0x032F0000
#define BUF_AUDIO_RAWPCM	0x03300000
#define BUF_AUDIO_4X_ADPCM	0x03310000
#define BUF_AUDIO_VMD		0x03320000
#define BUF_AUDIO_XAN_DPCM	0x03330000
#define BUF_AUDIO_ALAC		0x03340000
#define BUF_AUDIO_MPC		0x03350000
#define BUF_AUDIO_SHORTEN	0x03360000
#define BUF_AUDIO_WESTWOOD_SND1	0x03370000
#define BUF_AUDIO_WMALL		0x03380000
#define BUF_AUDIO_TRUESPEECH	0x03390000
#define BUF_AUDIO_TTA		0x033A0000
#define BUF_AUDIO_SMACKER	0x033B0000
#define BUF_AUDIO_FLVADPCM	0x033C0000
#define BUF_AUDIO_WAVPACK	0x033D0000
#define BUF_AUDIO_MP3ADU	0x033E0000
#define BUF_AUDIO_AMR_NB	0x033F0000
#define BUF_AUDIO_AMR_WB	0x03400000
#define BUF_AUDIO_EAC3		0x03410000
#define BUF_AUDIO_AAC_LATM	0x03420000
/*@}*/

/**
 * @defgroup buffer_spu SPU buffer types
 */
/*@{*/
#define BUF_SPU_BASE		0x04000000
#define BUF_SPU_DVD		0x04000000
#define BUF_SPU_TEXT            0x04010000
#define BUF_SPU_CC              0x04020000
#define BUF_SPU_DVB             0x04030000
#define BUF_SPU_SVCD            0x04040000
#define BUF_SPU_CVD             0x04050000
#define BUF_SPU_OGM             0x04060000
#define BUF_SPU_CMML            0x04070000
#define BUF_SPU_HDMV            0x04080000
/*@}*/

/**
 * @defgroup buffer_demux Demuxer block types
 */
/*@{*/
#define BUF_DEMUX_BLOCK		0x05000000
/*@}*/

/*@}*/

typedef struct extra_info_s extra_info_t;

/**
 * @brief Structure to pass information from input or demuxer plugins
 *        to output frames (past decoder).
 *
 * New data must be added after the existing fields to not break ABI
 * (backward compatibility).
 */

struct extra_info_s {

  int                   input_normpos; /**< remember where this buf came from in
                                        *   the input source (0..65535). can be
                                        *   either time or offset based. */
  int                   input_time;    /**< time offset in miliseconds from
                                        *   beginning of stream */
  uint32_t              frame_number;  /**< number of current frame if known */

  int                   seek_count;    /**< internal engine use */
  int64_t               vpts;          /**< set on output layers only */

  int                   invalid;       /**< do not use this extra info to update anything */
  int                   total_time;    /**< duration in miliseconds of the stream */
};


#define BUF_NUM_DEC_INFO 5

typedef struct buf_element_s buf_element_t;
struct buf_element_s {
  buf_element_t        *next;

  unsigned char        *mem;
  unsigned char        *content;   /**< start of raw content in mem (without header etc) */

  int32_t               size ;     /**< size of _content_                                     */
  int32_t               max_size;  /**< size of pre-allocated memory pointed to by "mem"      */
  int64_t               pts;       /**< presentation time stamp, used for a/v sync            */
  int64_t               disc_off;  /**< discontinuity offset                                  */

  extra_info_t         *extra_info; /**< extra info will be passed to frames */

  uint32_t              decoder_flags; /**< stuff like keyframe, is_header ... see below      */

  /** additional decoder flags and other dec-spec. stuff */
  uint32_t              decoder_info[BUF_NUM_DEC_INFO];
  /** pointers to dec-spec. stuff */
  void                 *decoder_info_ptr[BUF_NUM_DEC_INFO];

  void (*free_buffer) (buf_element_t *buf);

  void                 *source;   /**< pointer to source of this buffer for
                                   *   free_buffer                          */

  uint32_t              type;
} ;

/** keyframe should be set whenever possible (that is, when demuxer
 * knows about frames and keyframes).                                 */
#define BUF_FLAG_KEYFRAME    0x0001

/** frame start/end. BUF_FLAG_FRAME_END is sent on last buf of a frame */
#define BUF_FLAG_FRAME_START 0x0002
#define BUF_FLAG_FRAME_END   0x0004

/** any out-of-band data needed to initialize decoder must have
 * this flag set.                                                     */
#define BUF_FLAG_HEADER      0x0008

/** preview buffers are normal data buffers that must not produce any
 * output in decoders (may be used to sneak details about the stream
 * to come).                                                          */
#define BUF_FLAG_PREVIEW     0x0010

/** set when user stop the playback                                    */
#define BUF_FLAG_END_USER    0x0020

/** set when stream finished naturaly                                  */
#define BUF_FLAG_END_STREAM  0x0040

/** decoder_info[0] carries the frame step (1/90000).                  */
#define BUF_FLAG_FRAMERATE   0x0080

/** hint to metronom that seeking has occurred                         */
#define BUF_FLAG_SEEK        0x0100

/** special information inside, see below.                             */
#define BUF_FLAG_SPECIAL     0x0200

/** header use standard xine_bmiheader or xine_waveformatex structs.
 * xine_waveformatex is actually optional since the most important
 * information for audio init is available from decoder_info[].
 * note: BUF_FLAG_HEADER must also be set.                            */
#define BUF_FLAG_STDHEADER   0x0400

/** decoder_info[1] carries numerator for display aspect ratio
 * decoder_info[2] carries denominator for display aspect ratio       */
#define BUF_FLAG_ASPECT      0x0800

/* represent the state of gapless_switch at the time buf was enqueued */
#define BUF_FLAG_GAPLESS_SW  0x1000

/* Amount of audio padding added by encoder (mp3, aac). These empty
 * audio frames are causing a gap when switching between mp3 files.
 * decoder_info[1] carries amount of audio frames padded at the
 * beginning of the buffer
 * decoder_info[2] carries amount of audio frames padded at the end of
 * the buffer                                                         */
#define BUF_FLAG_AUDIO_PADDING 0x2000

/** decoder_info[4] has (mpeg_color_matrix << 1) | fullrange.
  * Useful for raw YUV which cannot tell this otherwise.
  * Valid until revoked or next stream.                               */
#define BUF_FLAG_COLOR_MATRIX 0x4000

/**
 * \defgroup buffer_special Special buffer types:
 * Sometimes there is a need to relay special information from a demuxer
 * to a video decoder. For example, some file types store palette data in
 * the file header independant of the video data. The special buffer type
 * offers a way to communicate this or any other custom, format-specific
 * data to the decoder.
 *
 * The interface was designed in a way that did not require an API
 * version bump. To send a special buffer type, set a buffer's flags field
 * to BUF_FLAG_SPECIAL. Set the buffer's decoder_info[1] field to a
 * number according to one of the special buffer subtypes defined below.
 * The second and third decoder_info[] fields are defined according to
 * your buffer type's requirements.
 *
 * Finally, remember to set the buffer's size to 0. This way, if a special
 * buffer is sent to a decode that does not know how to handle it, the
 * buffer will fall through to the case where the buffer's data content
 * is accumulated and no harm will be done.
 */
/*@{*/

/**
 * In a BUF_SPECIAL_PALETTE buffer:
 * decoder_info[1] = BUF_SPECIAL_PALETTE
 * decoder_info[2] = number of entries in palette table
 * decoder_info_ptr[2] = pointer to palette table
 * This buffer type is used to provide a file- and decoder-independent
 * facility to transport RGB color palettes from demuxers to decoders.
 * A palette table is an array of palette_entry_t structures. A decoder
 * should not count on this array to exist for the duration of the
 * program's execution and should copy, manipulate, and store the palette
 * data privately if it needs the palette information.
 */
#define BUF_SPECIAL_PALETTE  1


/* special buffer type 2 used to be defined but is now available for use */


/**
 * In a BUF_SPECIAL_ASPECT buffer:
 * decoder_info[1] = BUF_SPECIAL_ASPECT
 * decoder_info[2] = MPEG2 aspect ratio code
 * decoder_info[3] = stream scale prohibitions
 * This buffer is used to force mpeg decoders to use a certain aspect.
 * Currently xine-dvdnav uses this, because it has more accurate information
 * about the aspect from the dvd ifo-data.
 * The stream scale prohibitions are also delivered, with bit 0 meaning
 * "deny letterboxing" and bit 1 meaning "deny pan&scan"
 */
#define BUF_SPECIAL_ASPECT  3

/**
 * In a BUF_SPECIAL_DECODER_CONFIG buffer:
 * decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG
 * decoder_info[2] = data size
 * decoder_info_ptr[2] = pointer to data
 * This buffer is used to pass config information from  .mp4 files
 * (atom esds) to decoders. both mpeg4 and aac streams use that.
 */
#define BUF_SPECIAL_DECODER_CONFIG  4

/**
 * In a BUF_SPECIAL_STSD_ATOM buffer:
 * decoder_info[1] = BUF_SPECIAL_STSD_ATOM
 * decoder_info[2] = size of the ImageDescription atom, minus the
 *   four length bytes at the beginning
 * decoder_info_ptr[2] = pointer to ImageDescription atom, starting with
 *   the codec fourcc
 * Some Quicktime decoders need information contained within the
 * ImageDescription atom inside a Quicktime file's stsd atom. This
 * special buffer carries the ImageDescription atom from the QT demuxer
 * to an A/V decoder.
 */
#define BUF_SPECIAL_STSD_ATOM  5

/**
 * In a BUF_SPECIAL_LPCM_CONFIG buffer:
 * decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG
 * decoder_info[2] = config data
 * lpcm data encoded into mpeg2 streams have a format configuration
 * byte in every frame. this is used to detect the sample rate,
 * number of bits and channels.
 */
#define BUF_SPECIAL_LPCM_CONFIG 6

/**
 * In a BUF_SPECIAL_CHARSET_ENCODING buffer:
 * decoder_info[1] = BUF_SPECIAL_CHARSET_ENCODING
 * decoder_info[2] = size of charset encoding string
 * decoder_info_ptr[2] = pointer to charset encoding string
 * This is used mostly with subtitle buffers when encoding is
 * known at demuxer level (take precedence over xine config
 * settings such as subtitles.separate.src_encoding)
 */
#define BUF_SPECIAL_CHARSET_ENCODING 7


/**
 * In a BUF_SPECIAL_SPU_DVD_SUBTYPE:
 * decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE
 * decoder_info[2] = subtype
 * decoder_info[3] =
 * This buffer is pass SPU subtypes from DVDs
 */
#define BUF_SPECIAL_SPU_DVD_SUBTYPE 8


#define SPU_DVD_SUBTYPE_CLUT		1
#define SPU_DVD_SUBTYPE_PACKAGE		2
#define SPU_DVD_SUBTYPE_VOBSUB_PACKAGE	3
#define SPU_DVD_SUBTYPE_NAV		4

/**
 * In a BUF_SPECIAL_SPU_DVB_DESCRIPTOR
 * decoder_info[1] = BUF_SPECIAL_SPU_DVB_DESCRIPTOR
 * decoder_info[2] = size of spu_dvb_descriptor_t
 * decoder_info_ptr[2] = pointer to spu_dvb_descriptor_t, or NULL
 * decoder_info[3] =
 *
 * This buffer is used to tell a DVBSUB decoder when the stream
 * changes.  For more information on how to write a DVBSUB decoder,
 * see the comment at the top of src/demuxers/demux_ts.c
 **/
#define BUF_SPECIAL_SPU_DVB_DESCRIPTOR 9

/**
 * In a BUF_SPECIAL_RV_CHUNK_TABLE:
 * decoder_info[1] = BUF_SPECIAL_RV_CHUNK_TABLE
 * decoder_info[2] = number of entries in chunk table
 * decoder_info_ptr[2] = pointer to the chunk table
 *
 * This buffer transports the chunk table associated to each RealVideo frame.
 */
#define BUF_SPECIAL_RV_CHUNK_TABLE 10
/*@}*/

typedef struct spu_dvb_descriptor_s spu_dvb_descriptor_t;
struct spu_dvb_descriptor_s
{
  char lang[4];
  long comp_page_id;
  long aux_page_id;
} ;

typedef struct palette_entry_s palette_entry_t;
struct palette_entry_s
{
  unsigned char r, g, b;
} ;

typedef struct fifo_buffer_s fifo_buffer_t;
struct fifo_buffer_s
{
  buf_element_t  *first, *last;

  int             fifo_size;
  uint32_t        fifo_data_size;
  void            *fifo_empty_cb_data;

  pthread_mutex_t mutex;
  pthread_cond_t  not_empty;

  /*
   * functions to access this fifo:
   */

  void (*put) (fifo_buffer_t *fifo, buf_element_t *buf);

  buf_element_t *(*get) (fifo_buffer_t *fifo);

  void (*clear) (fifo_buffer_t *fifo) ;

  int (*size) (fifo_buffer_t *fifo);

  int (*num_free) (fifo_buffer_t *fifo);

  uint32_t (*data_size) (fifo_buffer_t *fifo);

  void (*dispose) (fifo_buffer_t *fifo);

  /*
   * alloc buffer for this fifo from global buf pool
   * you don't have to use this function to allocate a buffer,
   * an input plugin can decide to implement it's own
   * buffer allocation functions
   */

  buf_element_t *(*buffer_pool_alloc) (fifo_buffer_t *self);


  /*
   * special functions, not used by demuxers
   */

  /* the same as buffer_pool_alloc but may fail if none is available */
  buf_element_t *(*buffer_pool_try_alloc) (fifo_buffer_t *self);

  /* the same as put but insert at the head of the fifo */
  void (*insert) (fifo_buffer_t *fifo, buf_element_t *buf);

  /* callbacks */
  void (*register_alloc_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, void *), void *cb_data);
  void (*register_put_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, buf_element_t *buf, void *), void *cb_data);
  void (*register_get_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, buf_element_t *buf, void *), void *cb_data);
  void (*unregister_alloc_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, void *));
  void (*unregister_put_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, buf_element_t *buf, void *));
  void (*unregister_get_cb) (fifo_buffer_t *fifo, void (*cb)(fifo_buffer_t *fifo, buf_element_t *buf, void *));

  /*
   * private variables for buffer pool management
   */
  buf_element_t   *buffer_pool_top;    /* a stack actually */
  pthread_mutex_t  buffer_pool_mutex;
  pthread_cond_t   buffer_pool_cond_not_empty;
  int              buffer_pool_num_free;
  int              buffer_pool_capacity;
  int              buffer_pool_buf_size;
  void            *buffer_pool_base; /*used to free mem chunk */
  void           (*alloc_cb[BUF_MAX_CALLBACKS])(fifo_buffer_t *fifo, void *data_cb);
  void           (*put_cb[BUF_MAX_CALLBACKS])(fifo_buffer_t *fifo, buf_element_t *buf, void *data_cb);
  void           (*get_cb[BUF_MAX_CALLBACKS])(fifo_buffer_t *fifo, buf_element_t *buf, void *data_cb);
  void            *alloc_cb_data[BUF_MAX_CALLBACKS];
  void            *put_cb_data[BUF_MAX_CALLBACKS];
  void            *get_cb_data[BUF_MAX_CALLBACKS];
} ;

/**
 * @brief Allocate and initialise new (empty) FIFO buffers.
 * @param num_buffer Number of buffers to allocate.
 * @param buf_size Size of each buffer.
 * @internal Only used by video and audio decoder loops.
 */
fifo_buffer_t *_x_fifo_buffer_new (int num_buffers, uint32_t buf_size) XINE_MALLOC;

/**
 * @brief Allocate and initialise new dummy FIFO buffers.
 * @param num_buffer Number of dummy buffers to allocate.
 * @param buf_size Size of each buffer.
 * @internal Only used by video and audio decoder loops.
 */
fifo_buffer_t *_x_dummy_fifo_buffer_new (int num_buffers, uint32_t buf_size) XINE_MALLOC;


/**
 * @brief Returns the \ref buffer_video "BUF_VIDEO_xxx" for the given fourcc.
 * @param fourcc_int 32-bit FOURCC value in machine endianness
 * @sa _x_formattag_to_buf_audio
 *
 * example: fourcc_int = *(uint32_t *)fourcc_char;
 */
uint32_t _x_fourcc_to_buf_video( uint32_t fourcc_int ) XINE_PROTECTED;

/**
 * @brief Returns video codec name given the buffer type.
 * @param buf_type One of the \ref buffer_video "BUF_VIDEO_xxx" values.
 * @sa _x_buf_audio_name
 */
const char * _x_buf_video_name( uint32_t buf_type ) XINE_PROTECTED;

/**
 * @brief Returns the \ref buffer_audio "BUF_AUDIO_xxx" for the given formattag.
 * @param formattagg 32-bit format tag value in machine endianness
 * @sa _x_fourcc_to_buf_video
 */
uint32_t _x_formattag_to_buf_audio( uint32_t formattag ) XINE_PROTECTED;

/**
 * @brief Returns audio codec name given the buffer type.
 * @param buf_type One of the \ref buffer_audio "BUF_AUDIO_xxx" values.
 * @sa _x_buf_video_name
 */
const char * _x_buf_audio_name( uint32_t buf_type ) XINE_PROTECTED;


/**
 * @brief xine version of BITMAPINFOHEADER.
 * @note Should be safe to compile on 64bits machines.
 * @note Will always use machine endian format, so demuxers reading
 *       stuff from win32 formats must use the function below.
 */
typedef struct XINE_PACKED {
    int32_t        biSize;
    int32_t        biWidth;
    int32_t        biHeight;
    int16_t        biPlanes;
    int16_t        biBitCount;
    uint32_t       biCompression;
    int32_t        biSizeImage;
    int32_t        biXPelsPerMeter;
    int32_t        biYPelsPerMeter;
    int32_t        biClrUsed;
    int32_t        biClrImportant;
} xine_bmiheader;

/**
 * @brief xine version of WAVEFORMATEX.
 * @note The same comments from xine_bmiheader applies.
 */
typedef struct XINE_PACKED {
  int16_t   wFormatTag;
  int16_t   nChannels;
  int32_t   nSamplesPerSec;
  int32_t   nAvgBytesPerSec;
  int16_t   nBlockAlign;
  int16_t   wBitsPerSample;
  int16_t   cbSize;
} xine_waveformatex;

/** Convert xine_bmiheader struct from little endian */
void _x_bmiheader_le2me( xine_bmiheader *bih ) XINE_PROTECTED;

/** Convert xine_waveformatex struct from little endian */
void _x_waveformatex_le2me( xine_waveformatex *wavex ) XINE_PROTECTED;

static __inline int _x_is_fourcc(const void *ptr, const void *tag) {
  return memcmp(ptr, tag, 4) == 0;
}

#ifdef __cplusplus
}
#endif

#endif
