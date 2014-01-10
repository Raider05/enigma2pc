/*
 * Copyright (C) 2000-2013 the xine project
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
 * public xine-lib (libxine) interface and documentation
 *
 *
 * some programming guidelines about this api:
 * -------------------------------------------
 *
 * (1) libxine has (per stream instance) a fairly static memory
 *     model
 * (2) as a rule of thumb, never free() or realloc() any pointers
 *     returned by the xine engine (unless stated otherwise)
 *     or, in other words:
 *     do not free() stuff you have not malloc()ed
 * (3) xine is multi-threaded, make sure your programming environment
 *     can handle this.
 *     for x11-related stuff this means that you either have to properly
 *     use xlockdisplay() or use two seperate connections to the x-server
 *
 */
/*_x_ Lines formatted like this one are xine-lib developer comments.  */
/*_x_ They will be removed from the installed version of this header. */

#ifndef HAVE_XINE_H
#define HAVE_XINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>

#ifdef WIN32
#include <windows.h>
#include <windowsx.h>
#endif

#include <xine/os_types.h>
#include <xine/attributes.h>
#include <xine/version.h>

/* This enables some experimental features. These are not part of the
 * official libxine API, so use them only, if you absolutely need them.
 * Although we make efforts to keep even this part of the API as stable
 * as possible, this is not guaranteed. Incompatible changes can occur.
 */
/* #define XINE_ENABLE_EXPERIMENTAL_FEATURES */

/* This disables some deprecated features. These are still part of the
 * official libxine API and you may still use them. During the current
 * major release series, these will always be available and will stay
 * compatible. However, removal is likely to occur as soon as possible.
 */
/* #define XINE_DISABLE_DEPRECATED_FEATURES */


/*********************************************************************
 * xine opaque data types                                            *
 *********************************************************************/

typedef struct xine_s xine_t;
typedef struct xine_stream_s xine_stream_t;
typedef struct xine_audio_port_s xine_audio_port_t;
typedef struct xine_video_port_s xine_video_port_t;

/*********************************************************************
 * global engine handling                                            *
 *********************************************************************/

/*
 * version information
 */

/* dynamic info from actually linked libxine */
const char *xine_get_version_string (void) XINE_PROTECTED;
void xine_get_version (int *major, int *minor, int *sub) XINE_PROTECTED;

/* compare given version to libxine version,
   return 1 if compatible, 0 otherwise */
int  xine_check_version (int major, int minor, int sub) XINE_PROTECTED;

/*
 * pre-init the xine engine
 *
 * will first malloc and init a xine_t, create an empty config
 * system, then scan through all installed plugins and add them
 * to an internal list for later use.
 *
 * to fully init the xine engine, you have to load config values
 * (either using your own storage method and calling
 * xine_config_register_entry, or by using the xine_load_config
 * utility function - see below) and then call xine_init
 *
 * the only proper way to shut down the xine engine is to
 * call xine_exit() - do not try to free() the xine pointer
 * yourself and do not try to access any internal data structures
 */
xine_t *xine_new (void) XINE_PROTECTED;

/* allow the setting of some flags before xine_init
 * FIXME-ABI: this is currently GLOBAL
 */
void xine_set_flags (xine_t *, int) XINE_PROTECTED XINE_WEAK;
#define XINE_FLAG_NO_WRITE_CACHE		1

/*
 * post_init the xine engine
 */
void xine_init (xine_t *self) XINE_PROTECTED;

/*
 * helper functions to find and init audio/video drivers
 * from xine's plugin collection
 *
 * id    : identifier of the driver, may be NULL for auto-detection
 * data  : special data struct for ui/driver communications, depends
 *         on driver
 * visual: video driver flavor selector, constants see below
 *
 * both functions may return NULL if driver failed to load, was not
 * found ...
 *
 * use xine_close_audio/video_driver() to close loaded drivers
 * and free resources allocated by them
 */
xine_audio_port_t *xine_open_audio_driver (xine_t *self, const char *id,
					   void *data) XINE_PROTECTED;
xine_video_port_t *xine_open_video_driver (xine_t *self, const char *id,
					   int visual, void *data) XINE_PROTECTED;

void xine_close_audio_driver (xine_t *self, xine_audio_port_t  *driver) XINE_PROTECTED;
void xine_close_video_driver (xine_t *self, xine_video_port_t  *driver) XINE_PROTECTED;

/* valid visual types */
#define XINE_VISUAL_TYPE_NONE              0
#define XINE_VISUAL_TYPE_X11               1
#define XINE_VISUAL_TYPE_X11_2            10
#define XINE_VISUAL_TYPE_AA                2
#define XINE_VISUAL_TYPE_FB                3
#define XINE_VISUAL_TYPE_GTK               4
#define XINE_VISUAL_TYPE_DFB               5
#define XINE_VISUAL_TYPE_PM                6 /* used by the OS/2 port */
#define XINE_VISUAL_TYPE_DIRECTX           7 /* used by the win32/msvc port */
#define XINE_VISUAL_TYPE_CACA              8
#define XINE_VISUAL_TYPE_MACOSX            9
#define XINE_VISUAL_TYPE_XCB              11
#define XINE_VISUAL_TYPE_RAW              12

/*
 * free all resources, close all plugins, close engine.
 * self pointer is no longer valid after this call.
 */
void xine_exit (xine_t *self) XINE_PROTECTED;


/*********************************************************************
 * stream handling                                                   *
 *********************************************************************/

/*
 * create a new stream for media playback/access
 *
 * returns xine_stream_t* if OK,
 *         NULL on error (use xine_get_error for details)
 *
 * the only proper way to free the stream pointer returned by this
 * function is to call xine_dispose() on it. do not try to access any
 * fields in xine_stream_t, they're all private and subject to change
 * without further notice.
 */
xine_stream_t *xine_stream_new (xine_t *self,
				xine_audio_port_t *ao, xine_video_port_t *vo) XINE_PROTECTED;

/*
 * Make one stream the slave of another.
 * This establishes a binary master slave relation on streams, where
 * certain operations (specified by parameter "affection") on the master
 * stream are also applied to the slave stream.
 * If you want more than one stream to react to one master, you have to
 * apply the calls in a top down way:
 *  xine_stream_master_slave(stream1, stream2, 3);
 *  xine_stream_master_slave(stream2, stream3, 3);
 * This will make stream1 affect stream2 and stream2 affect stream3, so
 * effectively, operations on stream1 propagate to stream2 and 3.
 *
 * Please note that subsequent master_slave calls on the same streams
 * will overwrite their previous master/slave setting.
 * Be sure to not mess around.
 *
 * returns 1 on success, 0 on failure
 */
int xine_stream_master_slave(xine_stream_t *master, xine_stream_t *slave,
                             int affection) XINE_PROTECTED;

/* affection is some of the following ORed together: */
/* playing the master plays the slave */
#define XINE_MASTER_SLAVE_PLAY     (1<<0)
/* slave stops on master stop */
#define XINE_MASTER_SLAVE_STOP     (1<<1)
/* slave is synced to master's speed */
#define XINE_MASTER_SLAVE_SPEED    (1<<2)

/*
 * open a stream
 *
 * look for input / demux / decoder plugins, find out about the format
 * see if it is supported, set up internal buffers and threads
 *
 * returns 1 if OK, 0 on error (use xine_get_error for details)
 */
int xine_open (xine_stream_t *stream, const char *mrl) XINE_PROTECTED;

/*
 * play a stream from a given position
 *
 * start_pos:  0..65535
 * start_time: milliseconds
 * if both start position parameters are != 0 start_pos will be used
 * for non-seekable streams both values will be ignored
 *
 * returns 1 if OK, 0 on error (use xine_get_error for details)
 */
int  xine_play (xine_stream_t *stream, int start_pos, int start_time) XINE_PROTECTED;

/*
 * stop stream playback
 * xine_stream_t stays valid for new xine_open or xine_play
 */
void xine_stop (xine_stream_t *stream) XINE_PROTECTED;

/*
 * stop stream playback, free all stream-related resources
 * xine_stream_t stays valid for new xine_open
 */
void xine_close (xine_stream_t *stream) XINE_PROTECTED;

/*
 * ask current/recent input plugin to eject media - may or may not work,
 * depending on input plugin capabilities
 */
int  xine_eject (xine_stream_t *stream) XINE_PROTECTED;

/*
 * stop playback, dispose all stream-related resources
 * xine_stream_t no longer valid when after this
 */
void xine_dispose (xine_stream_t *stream) XINE_PROTECTED;

/*
 * set/get engine parameters.
 */
void xine_engine_set_param(xine_t *self, int param, int value) XINE_PROTECTED;
int xine_engine_get_param(xine_t *self, int param) XINE_PROTECTED;

#define XINE_ENGINE_PARAM_VERBOSITY        1

/*
 * set/get xine stream parameters
 * e.g. playback speed, constants see below
 */
void xine_set_param (xine_stream_t *stream, int param, int value) XINE_PROTECTED;
int  xine_get_param (xine_stream_t *stream, int param) XINE_PROTECTED;

/*
 * xine stream parameters
 */
#define XINE_PARAM_SPEED                   1 /* see below                   */
#define XINE_PARAM_AV_OFFSET               2 /* unit: 1/90000 sec           */
#define XINE_PARAM_AUDIO_CHANNEL_LOGICAL   3 /* -1 => auto, -2 => off       */
#define XINE_PARAM_SPU_CHANNEL             4
#define XINE_PARAM_VIDEO_CHANNEL           5
#define XINE_PARAM_AUDIO_VOLUME            6 /* 0..100                      */
#define XINE_PARAM_AUDIO_MUTE              7 /* 1=>mute, 0=>unmute          */
#define XINE_PARAM_AUDIO_COMPR_LEVEL       8 /* <100=>off, % compress otherw*/
#define XINE_PARAM_AUDIO_AMP_LEVEL         9 /* 0..200, 100=>100% (default) */
#define XINE_PARAM_AUDIO_REPORT_LEVEL     10 /* 1=>send events, 0=> don't   */
#define XINE_PARAM_VERBOSITY              11 /* control console output      */
#define XINE_PARAM_SPU_OFFSET             12 /* unit: 1/90000 sec           */
#define XINE_PARAM_IGNORE_VIDEO           13 /* disable video decoding      */
#define XINE_PARAM_IGNORE_AUDIO           14 /* disable audio decoding      */
#define XINE_PARAM_IGNORE_SPU             15 /* disable spu decoding        */
#define XINE_PARAM_BROADCASTER_PORT       16 /* 0: disable, x: server port  */
#define XINE_PARAM_METRONOM_PREBUFFER     17 /* unit: 1/90000 sec           */
#define XINE_PARAM_EQ_30HZ                18 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_60HZ                19 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_125HZ               20 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_250HZ               21 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_500HZ               22 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_1000HZ              23 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_2000HZ              24 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_4000HZ              25 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_8000HZ              26 /* equalizer gains -100..100   */
#define XINE_PARAM_EQ_16000HZ             27 /* equalizer gains -100..100   */
#define XINE_PARAM_AUDIO_CLOSE_DEVICE     28 /* force closing audio device  */
#define XINE_PARAM_AUDIO_AMP_MUTE         29 /* 1=>mute, 0=>unmute */
#define XINE_PARAM_FINE_SPEED             30 /* 1.000.000 => normal speed   */
#define XINE_PARAM_EARLY_FINISHED_EVENT   31 /* send event when demux finish*/
#define XINE_PARAM_GAPLESS_SWITCH         32 /* next stream only gapless swi*/
#define XINE_PARAM_DELAY_FINISHED_EVENT   33 /* 1/10sec,0=>disable,-1=>forev*/

/*
 * speed values for XINE_PARAM_SPEED parameter.
 *
 * alternatively, one may use XINE_PARAM_FINE_SPEED for greater
 * control of the speed value, where:
 * XINE_PARAM_SPEED / 4 <-> XINE_PARAM_FINE_SPEED / 1000000
 */
#define XINE_SPEED_PAUSE                   0
#define XINE_SPEED_SLOW_4                  1
#define XINE_SPEED_SLOW_2                  2
#define XINE_SPEED_NORMAL                  4
#define XINE_SPEED_FAST_2                  8
#define XINE_SPEED_FAST_4                  16

/* normal speed value for XINE_PARAM_FINE_SPEED parameter */
#define XINE_FINE_SPEED_NORMAL             1000000

/* video parameters */
#define XINE_PARAM_VO_DEINTERLACE          0x01000000 /* bool               */
#define XINE_PARAM_VO_ASPECT_RATIO         0x01000001 /* see below          */
#define XINE_PARAM_VO_HUE                  0x01000002 /* 0..65535           */
#define XINE_PARAM_VO_SATURATION           0x01000003 /* 0..65535           */
#define XINE_PARAM_VO_CONTRAST             0x01000004 /* 0..65535           */
#define XINE_PARAM_VO_BRIGHTNESS           0x01000005 /* 0..65535           */
#define XINE_PARAM_VO_GAMMA                0x0100000c /* 0..65535           */
#define XINE_PARAM_VO_ZOOM_X               0x01000008 /* percent            */
#define XINE_PARAM_VO_ZOOM_Y               0x0100000d /* percent            */
#define XINE_PARAM_VO_PAN_SCAN             0x01000009 /* bool               */
#define XINE_PARAM_VO_TVMODE               0x0100000a /* ???                */
#define XINE_PARAM_VO_WINDOW_WIDTH         0x0100000f /* readonly           */
#define XINE_PARAM_VO_WINDOW_HEIGHT        0x01000010 /* readonly           */
#define XINE_PARAM_VO_SHARPNESS            0x01000018 /* 0..65535           */
#define XINE_PARAM_VO_NOISE_REDUCTION      0x01000019 /* 0..65535           */
#define XINE_PARAM_VO_CROP_LEFT            0x01000020 /* crop frame pixels  */
#define XINE_PARAM_VO_CROP_RIGHT           0x01000021 /* crop frame pixels  */
#define XINE_PARAM_VO_CROP_TOP             0x01000022 /* crop frame pixels  */
#define XINE_PARAM_VO_CROP_BOTTOM          0x01000023 /* crop frame pixels  */

#define XINE_VO_ZOOM_STEP                  100
#define XINE_VO_ZOOM_MAX                   400
#define XINE_VO_ZOOM_MIN                   -85

/* possible ratios for XINE_PARAM_VO_ASPECT_RATIO */
#define XINE_VO_ASPECT_AUTO                0
#define XINE_VO_ASPECT_SQUARE              1 /* 1:1    */
#define XINE_VO_ASPECT_4_3                 2 /* 4:3    */
#define XINE_VO_ASPECT_ANAMORPHIC          3 /* 16:9   */
#define XINE_VO_ASPECT_DVB                 4 /* 2.11:1 */
#define XINE_VO_ASPECT_NUM_RATIOS          5
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
#define XINE_VO_ASPECT_PAN_SCAN            41
#define XINE_VO_ASPECT_DONT_TOUCH          42
#endif

/* stream format detection strategies */

/* recognize stream type first by content then by extension. */
#define XINE_DEMUX_DEFAULT_STRATEGY        0
/* recognize stream type first by extension then by content. */
#define XINE_DEMUX_REVERT_STRATEGY         1
/* recognize stream type by content only.                    */
#define XINE_DEMUX_CONTENT_STRATEGY        2
/* recognize stream type by extension only.                  */
#define XINE_DEMUX_EXTENSION_STRATEGY      3

/* verbosity settings */
#define XINE_VERBOSITY_NONE                0
#define XINE_VERBOSITY_LOG                 1
#define XINE_VERBOSITY_DEBUG               2

/*
 * snapshot function
 *
 * image format can be YUV 4:2:0 or 4:2:2
 * will copy the image data into memory that <img> points to
 * (interleaved for yuv 4:2:2 or planary for 4:2:0)
 *
 * xine_get_current_frame() requires that <img> must be able
 * to hold the image data. Use a NULL pointer to retrieve the
 * necessary parameters for calculating the buffer size. Be
 * aware that the image can change between two successive calls
 * so you better pause the stream.
 *
 * xine_get_current_frame_s() requires to specify the buffer
 * size and it returns the needed / used size. It won't copy
 * image data into a too small buffer.
 *
 * xine_get_current_frame_alloc() takes care of allocating
 * a buffer on its own, so image data can be retrieved by
 * a single call without the need to pause the stream.
 *
 * xine_get_current_frame_data() passes the parameters of the
 * previously mentioned functions plus further information in
 * a structure and can work like the _s or _alloc function
 * respectively depending on the passed flags.
 *
 * all functions return 1 on success, 0 failure.
 */
int  xine_get_current_frame (xine_stream_t *stream,
			     int *width, int *height,
			     int *ratio_code, int *format,
			     uint8_t *img) XINE_PROTECTED;

int  xine_get_current_frame_s (xine_stream_t *stream,
			     int *width, int *height,
			     int *ratio_code, int *format,
			     uint8_t *img, int *img_size) XINE_PROTECTED;

int  xine_get_current_frame_alloc (xine_stream_t *stream,
			     int *width, int *height,
			     int *ratio_code, int *format,
			     uint8_t **img, int *img_size) XINE_PROTECTED;

typedef struct {

  int      width;
  int      height;
  int      crop_left;
  int      crop_right;
  int      crop_top;
  int      crop_bottom;
  int      ratio_code;
  int      interlaced;
  int      format;
  int      img_size;
  uint8_t *img;
} xine_current_frame_data_t;

#define XINE_FRAME_DATA_ALLOCATE_IMG (1<<0)

int  xine_get_current_frame_data (xine_stream_t *stream,
			     xine_current_frame_data_t *data,
			     int flags) XINE_PROTECTED;

/* xine image formats */
#define XINE_IMGFMT_YV12 (('2'<<24)|('1'<<16)|('V'<<8)|'Y')
#define XINE_IMGFMT_YUY2 (('2'<<24)|('Y'<<16)|('U'<<8)|'Y')
#define XINE_IMGFMT_XVMC (('C'<<24)|('M'<<16)|('v'<<8)|'X')
#define XINE_IMGFMT_XXMC (('C'<<24)|('M'<<16)|('x'<<8)|'X')
#define XINE_IMGFMT_VDPAU (('A'<<24)|('P'<<16)|('D'<<8)|'V')
#define XINE_IMGFMT_VAAPI (('P'<<24)|('A'<<16)|('A'<<8)|'V')

/* get current xine's virtual presentation timestamp (1/90000 sec)
 * note: this is mostly internal data.
 * one can use vpts with xine_osd_show() and xine_osd_hide().
 */
int64_t xine_get_current_vpts(xine_stream_t *stream) XINE_PROTECTED;


/*
 * Continuous video frame grabbing feature.
 *
 * In opposite to the 'xine_get_current_frame' based snapshot function this grabbing
 * feature allow continuous grabbing of last or next displayed video frame.
 * Grabbed video frames are returned in simple three byte RGB format.
 *
 * Depending on the capabilities of the used video output driver video image data is
 * taken as close as possible at the end of the video processing chain. Thus a returned
 * video image could contain the blended OSD data, is deinterlaced, cropped and scaled
 * and video properties like hue, sat could be applied.
 * If a video output driver does not have a decent grabbing implementation then there
 * is a generic fallback feature that grabs the video frame as they are taken from the video
 * display queue (like the xine_get_current_frame' function).
 * In this case color correct conversation to a RGB image incorporating source cropping
 * and scaling to the requested grab size is also supported.
 *
 * The caller must first request a new video grab frame using the public 'xine_new_grab_video_frame'
 * function. Then the caller should populate the frame with the wanted source cropping, grab image
 * size and control flags. After that grab requests could be done by calling the supplied grab() feature
 * of the frame. At the end a call to the supplied dispose() feature of the frame releases all needed
 * resources.
 * The caller should have acquired a port ticket while calling these features.
 *
 */
#define HAVE_XINE_GRAB_VIDEO_FRAME      1

/*
 * frame structure used for grabbing video frames of format RGB.
 */
typedef struct xine_grab_video_frame_s xine_grab_video_frame_t;
struct xine_grab_video_frame_s {
  /*
   *  grab last/next displayed image.
   *  returns 0 if grab is successful, 1 on timeout and -1 on error
   */
  int (*grab) (xine_grab_video_frame_t *self);

  /*
   *  free all resources.
   */
  void (*dispose) (xine_grab_video_frame_t *self);

  /*
   *  Cropping of source image. Has to be specified by caller.
   */
  int crop_left;
  int crop_right;
  int crop_top;
  int crop_bottom;

  /*
   * Parameters of returned RGB image.
   * Caller can specify wanted frame size giving width and/or height a value > 0.
   * In this case the grabbed image is scaled to the requested size.
   * Otherwise the grab function returns the actual size of the grabbed image
   * in width/height without scaling the image.
   */
  int width, height; /* requested/returned size of image */
  uint8_t *img;      /* returned RGB image data taking three bytes per pixel */
  int64_t vpts;      /* virtual presentation timestamp (1/90000 sec) of returned frame */

  int timeout;       /* Max. time to wait for next displayed frame in milliseconds */
  int flags;         /* Controlling flags. See XINE_GRAB_VIDEO_FRAME_FLAGS_* definitions */
};

#define XINE_GRAB_VIDEO_FRAME_FLAGS_CONTINUOUS  0x01    /* optimize resource allocation for continuous frame grabbing */
#define XINE_GRAB_VIDEO_FRAME_FLAGS_WAIT_NEXT   0x02    /* wait for next display frame instead of using last displayed frame */

#define XINE_GRAB_VIDEO_FRAME_DEFAULT_TIMEOUT   500

/*
 * Allocate new grab video frame. Returns NULL on error.
 */
xine_grab_video_frame_t*  xine_new_grab_video_frame (xine_stream_t *stream) XINE_PROTECTED;


/*********************************************************************
 * media processing                                                  *
 *********************************************************************/

#ifdef XINE_ENABLE_EXPERIMENTAL_FEATURES

/*
 * access to decoded audio and video frames from a stream
 * these functions are intended to provide the basis for
 * re-encoding and other video processing applications
 *
 * note that the xine playback engine will block when
 * rendering to a framegrab port: to unblock the stream,
 * you must fetch the frames manually with the
 * xine_get_next_* functions.  this ensures that a
 * framegrab port is guaranteed to never miss a frame.
 *
 */

xine_video_port_t *xine_new_framegrab_video_port (xine_t *self) XINE_PROTECTED;

typedef struct {

  int64_t  vpts;       /* timestamp 1/90000 sec for a/v sync */
  int64_t  duration;
  double   aspect_ratio;
  int      width, height;
  int      colorspace; /* XINE_IMGFMT_* */

  int      pos_stream; /* bytes from stream start */
  int      pos_time;   /* milliseconds */

  int      frame_number; /* frame number (may be unknown) */

  uint8_t *data;
  void    *xine_frame; /* used internally by xine engine */
} xine_video_frame_t;

int xine_get_next_video_frame (xine_video_port_t *port,
			       xine_video_frame_t *frame) XINE_PROTECTED;

void xine_free_video_frame (xine_video_port_t *port, xine_video_frame_t *frame) XINE_PROTECTED;

xine_audio_port_t *xine_new_framegrab_audio_port (xine_t *self) XINE_PROTECTED;

typedef struct {

  int64_t  vpts;       /* timestamp 1/90000 sec for a/v sync */
  int      num_samples;
  int      sample_rate;
  int      num_channels;
  int      bits_per_sample; /* per channel */

  uint8_t *data;
  void    *xine_frame; /* used internally by xine engine */

  off_t    pos_stream; /* bytes from stream start */
  int      pos_time;   /* milliseconds */
} xine_audio_frame_t;

int xine_get_next_audio_frame (xine_audio_port_t *port,
			       xine_audio_frame_t *frame) XINE_PROTECTED;

void xine_free_audio_frame (xine_audio_port_t *port, xine_audio_frame_t *frame) XINE_PROTECTED;

#endif


/*********************************************************************
 * post plugin handling                                              *
 *********************************************************************/

/*
 * post effect plugin functions
 *
 * after the data leaves the decoder it can pass an arbitrary tree
 * of post plugins allowing for effects to be applied to the video
 * frames/audio buffers before they reach the output stage
 */

typedef struct xine_post_s xine_post_t;

struct xine_post_s {

  /* a NULL-terminated array of audio input ports this post plugin
   * provides; you can hand these to other post plugin's outputs or
   * pass them to the initialization of streams
   */
  xine_audio_port_t **audio_input;

  /* a NULL-terminated array of video input ports this post plugin
   * provides; you can hand these to other post plugin's outputs or
   * pass them to the initialization of streams
   */
  xine_video_port_t **video_input;

  /* the type of the post plugin
   * one of XINE_POST_TYPE_* can be used here
   */
  int type;

};

/*
 * initialize a post plugin
 *
 * returns xine_post_t* on success, NULL on failure
 *
 * Initializes the post plugin with the given name and connects its
 * outputs to the NULL-terminated arrays of audio and video ports.
 * Some plugins also care about the number of inputs you request
 * (e.g. mixer plugins), others simply ignore this number.
 */
xine_post_t *xine_post_init(xine_t *xine, const char *name,
			    int inputs,
			    xine_audio_port_t **audio_target,
			    xine_video_port_t **video_target) XINE_PROTECTED;

/* get a list of all available post plugins */
const char *const *xine_list_post_plugins(xine_t *xine) XINE_PROTECTED;

/* get a list of all post plugins of one type */
const char *const *xine_list_post_plugins_typed(xine_t *xine, uint32_t type) XINE_PROTECTED;

/*
 * post plugin input/output
 *
 * These structures encapsulate inputs/outputs for post plugins
 * to transfer arbitrary data. Frontends can also provide inputs
 * and outputs and connect them to post plugins to exchange data
 * with them.
 */

typedef struct xine_post_in_s  xine_post_in_t;
typedef struct xine_post_out_s xine_post_out_t;

struct xine_post_in_s {

  /* the name identifying this input */
  const char   *name;

  /* the data pointer; input is directed to this memory location,
   * so you simply access the pointer to access the input data */
  void         *data;

  /* the datatype of this input, use one of XINE_POST_DATA_* here */
  int           type;

};

struct xine_post_out_s {

  /* the name identifying this output */
  const char   *name;

  /* the data pointer; output should be directed to this memory location,
   * so in the easy case you simply write through the pointer */
  void         *data;

  /* this function is called, when the output should be redirected
   * to another input, you sould set the data pointer to direct
   * any output to this new input;
   * a special situation is, when this function is called with a NULL
   * argument: in this case you should disconnect the data pointer
   * from any output and if necessary to avoid writing to some stray
   * memory you should make it point to some dummy location,
   * returns 1 on success, 0 on failure;
   * if you do not implement rewiring, set this to NULL */
  int (*rewire) (xine_post_out_t *self, void *data);

  /* the datatype of this output, use one of XINE_POST_DATA_* here */
  int           type;

};

/* get a list of all inputs of a post plugin */
const char *const *xine_post_list_inputs(xine_post_t *self) XINE_PROTECTED;

/* get a list of all outputs of a post plugin */
const char *const *xine_post_list_outputs(xine_post_t *self) XINE_PROTECTED;

/* retrieve one specific input of a post plugin */
xine_post_in_t *xine_post_input(xine_post_t *self, const char *name) XINE_PROTECTED;

/* retrieve one specific output of a post plugin */
xine_post_out_t *xine_post_output(xine_post_t *self, const char *name) XINE_PROTECTED;

/*
 * wire an input to an output
 * returns 1 on success, 0 on failure
 */
int xine_post_wire(xine_post_out_t *source, xine_post_in_t *target) XINE_PROTECTED;

/*
 * wire a video port to a video output
 * This can be used to rewire different post plugins to the video output
 * plugin layer. The ports you hand in at xine_post_init() will already
 * be wired with the post plugin, so you need this function for
 * _re_connecting only.
 *
 * returns 1 on success, 0 on failure
 */
int xine_post_wire_video_port(xine_post_out_t *source, xine_video_port_t *vo) XINE_PROTECTED;

/*
 * wire an audio port to an audio output
 * This can be used to rewire different post plugins to the audio output
 * plugin layer. The ports you hand in at xine_post_init() will already
 * be wired with the post plugin, so you need this function for
 * _re_connecting only.
 *
 * returns 1 on success, 0 on failure
 */
int xine_post_wire_audio_port(xine_post_out_t *source, xine_audio_port_t *ao) XINE_PROTECTED;

/*
 * Extracts an output for a stream. Use this to rewire the outputs of streams.
 */
xine_post_out_t * xine_get_video_source(xine_stream_t *stream) XINE_PROTECTED;
xine_post_out_t * xine_get_audio_source(xine_stream_t *stream) XINE_PROTECTED;

/*
 * disposes the post plugin
 * please make sure that no other post plugin and no stream is
 * connected to any of this plugin's inputs
 */
void xine_post_dispose(xine_t *xine, xine_post_t *self) XINE_PROTECTED;


/* post plugin types */
#define XINE_POST_TYPE_VIDEO_FILTER		0x010000
#define XINE_POST_TYPE_VIDEO_VISUALIZATION	0x010001
#define XINE_POST_TYPE_VIDEO_COMPOSE		0x010002
#define XINE_POST_TYPE_AUDIO_FILTER		0x020000
#define XINE_POST_TYPE_AUDIO_VISUALIZATION	0x020001


/* post plugin data types */

/* video port data
 * input->data is a xine_video_port_t*
 * output->data usually is a xine_video_port_t**
 */
#define XINE_POST_DATA_VIDEO          0

/* audio port data
 * input->data is a xine_audio_port_t*
 * output->data usually is a xine_audio_port_t**
 */
#define XINE_POST_DATA_AUDIO          1

/* integer data
 * input->data is a int*
 * output->data usually is a int*
 */
#define XINE_POST_DATA_INT            3

/* double precision floating point data
 * input->data is a double*
 * output->data usually is a double*
 */
#define XINE_POST_DATA_DOUBLE         4

/* parameters api (used by frontends)
 * input->data is xine_post_api_t*  (see below)
 */
#define XINE_POST_DATA_PARAMETERS     5

/* defines a single parameter entry. */
typedef struct {
  int              type;             /* POST_PARAM_TYPE_xxx             */
  const char      *name;             /* name of this parameter          */
  int              size;             /* sizeof(parameter)               */
  int              offset;           /* offset in bytes from struct ptr */
  char           **enum_values;      /* enumeration (first=0) or NULL   */
  double           range_min;        /* minimum value                   */
  double           range_max;        /* maximum value                   */
  int              readonly;         /* 0 = read/write, 1=read-only     */
  const char      *description;      /* user-friendly description       */
} xine_post_api_parameter_t;

/* description of parameters struct (params). */
typedef struct {
  int struct_size;                       /* sizeof(params)     */
  xine_post_api_parameter_t *parameter;  /* list of parameters */
} xine_post_api_descr_t;

typedef struct {
  /*
   * method to set all the read/write parameters.
   * params is a struct * defined by xine_post_api_descr_t
   */
  int (*set_parameters) (xine_post_t *self, void *params);

  /*
   * method to get all parameters.
   */
  int (*get_parameters) (xine_post_t *self, void *params);

  /*
   * method to get params struct definition
   */
  xine_post_api_descr_t * (*get_param_descr) (void);

  /*
   * method to get plugin and parameters help (UTF-8)
   * the help string must be word wrapped by the frontend.
   * it might contain \n to mark paragraph breaks.
   */
  char * (*get_help) (void);
} xine_post_api_t;

/* post parameter types */
#define POST_PARAM_TYPE_LAST       0  /* terminator of parameter list       */
#define POST_PARAM_TYPE_INT        1  /* integer (or vector of integers)    */
#define POST_PARAM_TYPE_DOUBLE     2  /* double (or vector of doubles)      */
#define POST_PARAM_TYPE_CHAR       3  /* char (or vector of chars = string) */
#define POST_PARAM_TYPE_STRING     4  /* (char *), ASCIIZ                   */
#define POST_PARAM_TYPE_STRINGLIST 5  /* (char **) list, NULL terminated    */
#define POST_PARAM_TYPE_BOOL       6  /* integer (0 or 1)                   */


/*********************************************************************
 * information retrieval                                             *
 *********************************************************************/

/*
 * xine log functions
 *
 * frontends can display xine log output using these functions
 */
int    xine_get_log_section_count(xine_t *self) XINE_PROTECTED;

/* return a NULL terminated array of log sections names */
const char *const *xine_get_log_names(xine_t *self) XINE_PROTECTED;

/* print some log information to <buf> section */
void   xine_log (xine_t *self, int buf,
		 const char *format, ...) XINE_FORMAT_PRINTF(3, 4) XINE_PROTECTED;
void   xine_vlog(xine_t *self, int buf,
		  const char *format, va_list args) XINE_FORMAT_PRINTF(3, 0) XINE_PROTECTED;

/* get log messages of specified section */
char *const *xine_get_log (xine_t *self, int buf) XINE_PROTECTED;

/* log callback will be called whenever something is logged */
typedef void (*xine_log_cb_t) (void *user_data, int section);
void   xine_register_log_cb (xine_t *self, xine_log_cb_t cb,
			     void *user_data) XINE_PROTECTED;

/*
 * error handling / engine status
 */

/* return last error  */
int  xine_get_error (xine_stream_t *stream) XINE_PROTECTED;

/* get current xine engine status (constants see below) */
int  xine_get_status (xine_stream_t *stream) XINE_PROTECTED;

/*
 * engine status codes
 */
#define XINE_STATUS_IDLE                   0 /* no mrl assigned */
#define XINE_STATUS_STOP                   1
#define XINE_STATUS_PLAY                   2
#define XINE_STATUS_QUIT                   3

/*
 * xine error codes
 */
#define XINE_ERROR_NONE                    0
#define XINE_ERROR_NO_INPUT_PLUGIN         1
#define XINE_ERROR_NO_DEMUX_PLUGIN         2
#define XINE_ERROR_DEMUX_FAILED            3
#define XINE_ERROR_MALFORMED_MRL           4
#define XINE_ERROR_INPUT_FAILED            5

/*
 * try to find out audio/spu language of given channel
 * (use -1 for current channel)
 *
 * lang must point to a buffer of at least XINE_LANG_MAX bytes
 *
 * returns 1 on success, 0 on failure
 */
int xine_get_audio_lang (xine_stream_t *stream, int channel,
			 char *lang) XINE_PROTECTED;
int xine_get_spu_lang   (xine_stream_t *stream, int channel,
			 char *lang) XINE_PROTECTED;
/*_x_ increasing this number means an incompatible ABI breakage! */
#define XINE_LANG_MAX                     32

/*
 * get position / length information
 *
 * depending of the nature and system layer of the stream,
 * some or all of this information may be unavailable or incorrect
 * (e.g. live network streams may not have a valid length)
 *
 * returns 1 on success, 0 on failure (data was not updated,
 * probably because it's not known yet... try again later)
 */
int  xine_get_pos_length (xine_stream_t *stream,
			  int *pos_stream,  /* 0..65535     */
			  int *pos_time,    /* milliseconds */
			  int *length_time) /* milliseconds */
  XINE_PROTECTED;

/*
 * get information about the stream such as
 * video width/height, codecs, audio format, title, author...
 * strings are UTF-8 encoded.
 *
 * constants see below
 */
uint32_t    xine_get_stream_info (xine_stream_t *stream, int info) XINE_PROTECTED;
const char *xine_get_meta_info   (xine_stream_t *stream, int info) XINE_PROTECTED;

/* xine_get_stream_info */
#define XINE_STREAM_INFO_BITRATE           0
#define XINE_STREAM_INFO_SEEKABLE          1
#define XINE_STREAM_INFO_VIDEO_WIDTH       2
#define XINE_STREAM_INFO_VIDEO_HEIGHT      3
#define XINE_STREAM_INFO_VIDEO_RATIO       4 /* *10000 */
#define XINE_STREAM_INFO_VIDEO_CHANNELS    5
#define XINE_STREAM_INFO_VIDEO_STREAMS     6
#define XINE_STREAM_INFO_VIDEO_BITRATE     7
#define XINE_STREAM_INFO_VIDEO_FOURCC      8
#define XINE_STREAM_INFO_VIDEO_HANDLED     9  /* codec available? */
#define XINE_STREAM_INFO_FRAME_DURATION    10 /* 1/90000 sec */
#define XINE_STREAM_INFO_AUDIO_CHANNELS    11
#define XINE_STREAM_INFO_AUDIO_BITS        12
#define XINE_STREAM_INFO_AUDIO_SAMPLERATE  13
#define XINE_STREAM_INFO_AUDIO_BITRATE     14
#define XINE_STREAM_INFO_AUDIO_FOURCC      15
#define XINE_STREAM_INFO_AUDIO_HANDLED     16 /* codec available? */
#define XINE_STREAM_INFO_HAS_CHAPTERS      17
#define XINE_STREAM_INFO_HAS_VIDEO         18
#define XINE_STREAM_INFO_HAS_AUDIO         19
#define XINE_STREAM_INFO_IGNORE_VIDEO      20
#define XINE_STREAM_INFO_IGNORE_AUDIO      21
#define XINE_STREAM_INFO_IGNORE_SPU        22
#define XINE_STREAM_INFO_VIDEO_HAS_STILL   23
#define XINE_STREAM_INFO_MAX_AUDIO_CHANNEL 24
#define XINE_STREAM_INFO_MAX_SPU_CHANNEL   25
#define XINE_STREAM_INFO_AUDIO_MODE        26
#define XINE_STREAM_INFO_SKIPPED_FRAMES    27 /* for 1000 frames delivered */
#define XINE_STREAM_INFO_DISCARDED_FRAMES  28 /* for 1000 frames delivered */
#define XINE_STREAM_INFO_VIDEO_AFD         29
#define XINE_STREAM_INFO_DVD_TITLE_NUMBER   30
#define XINE_STREAM_INFO_DVD_TITLE_COUNT    31
#define XINE_STREAM_INFO_DVD_CHAPTER_NUMBER 32
#define XINE_STREAM_INFO_DVD_CHAPTER_COUNT  33
#define XINE_STREAM_INFO_DVD_ANGLE_NUMBER   34
#define XINE_STREAM_INFO_DVD_ANGLE_COUNT    35

/* possible values for XINE_STREAM_INFO_VIDEO_AFD */
#define XINE_VIDEO_AFD_NOT_PRESENT         -1
#define XINE_VIDEO_AFD_RESERVED_0          0
#define XINE_VIDEO_AFD_RESERVED_1          1
#define XINE_VIDEO_AFD_BOX_16_9_TOP        2
#define XINE_VIDEO_AFD_BOX_14_9_TOP        3
#define XINE_VIDEO_AFD_BOX_GT_16_9_CENTRE  4
#define XINE_VIDEO_AFD_RESERVED_5          5
#define XINE_VIDEO_AFD_RESERVED_6          6
#define XINE_VIDEO_AFD_RESERVED_7          7
#define XINE_VIDEO_AFD_SAME_AS_FRAME       8
#define XINE_VIDEO_AFD_4_3_CENTRE          9
#define XINE_VIDEO_AFD_16_9_CENTRE         10
#define XINE_VIDEO_AFD_14_9_CENTRE         11
#define XINE_VIDEO_AFD_RESERVED_12         12
#define XINE_VIDEO_AFD_4_3_PROTECT_14_9    13
#define XINE_VIDEO_AFD_16_9_PROTECT_14_9   14
#define XINE_VIDEO_AFD_16_9_PROTECT_4_3    15

/* xine_get_meta_info */
#define XINE_META_INFO_TITLE               0
#define XINE_META_INFO_COMMENT             1
#define XINE_META_INFO_ARTIST              2
#define XINE_META_INFO_GENRE               3
#define XINE_META_INFO_ALBUM               4
#define XINE_META_INFO_YEAR                5 /* may be full date */
#define XINE_META_INFO_VIDEOCODEC          6
#define XINE_META_INFO_AUDIOCODEC          7
#define XINE_META_INFO_SYSTEMLAYER         8
#define XINE_META_INFO_INPUT_PLUGIN        9
#define XINE_META_INFO_CDINDEX_DISCID      10
#define XINE_META_INFO_TRACK_NUMBER        11
#define XINE_META_INFO_COMPOSER            12
/* post-1.1.17; taken from the list at http://age.hobba.nl/audio/mirroredpages/ogg-tagging.html on 2009-12-11 */
#define XINE_META_INFO_PUBLISHER	   13
#define XINE_META_INFO_COPYRIGHT	   14
#define XINE_META_INFO_LICENSE		   15
#define XINE_META_INFO_ARRANGER		   16
#define XINE_META_INFO_LYRICIST		   17
#define XINE_META_INFO_AUTHOR		   18
#define XINE_META_INFO_CONDUCTOR	   19
#define XINE_META_INFO_PERFORMER	   20
#define XINE_META_INFO_ENSEMBLE		   21
#define XINE_META_INFO_OPUS		   22
#define XINE_META_INFO_PART		   23
#define XINE_META_INFO_PARTNUMBER	   24
#define XINE_META_INFO_LOCATION		   25
/* post-1.1.18.1 */
#define XINE_META_INFO_DISCNUMBER	   26


/*********************************************************************
 * plugin management / autoplay / mrl browsing                       *
 *********************************************************************/

/*
 * note: the pointers to strings or string arrays returned
 *       by some of these functions are pointers to statically
 *       alloced internal xine memory chunks.
 *       they're only valid between xine function calls
 *       and should never be free()d.
 */

typedef struct {
  char      *origin; /* file plugin: path */
  char      *mrl;    /* <type>://<location> */
  char      *link;
  off_t      size;   /* size of this source, may be 0 */
  uint32_t   type;   /* see below */
} xine_mrl_t;

/* mrl types */
#define XINE_MRL_TYPE_unknown        (0 << 0)
#define XINE_MRL_TYPE_dvd            (1 << 0)
#define XINE_MRL_TYPE_vcd            (1 << 1)
#define XINE_MRL_TYPE_net            (1 << 2)
#define XINE_MRL_TYPE_rtp            (1 << 3)
#define XINE_MRL_TYPE_stdin          (1 << 4)
#define XINE_MRL_TYPE_cda            (1 << 5)
#define XINE_MRL_TYPE_file           (1 << 6)
#define XINE_MRL_TYPE_file_fifo      (1 << 7)
#define XINE_MRL_TYPE_file_chardev   (1 << 8)
#define XINE_MRL_TYPE_file_directory (1 << 9)
#define XINE_MRL_TYPE_file_blockdev  (1 << 10)
#define XINE_MRL_TYPE_file_normal    (1 << 11)
#define XINE_MRL_TYPE_file_symlink   (1 << 12)
#define XINE_MRL_TYPE_file_sock      (1 << 13)
#define XINE_MRL_TYPE_file_exec      (1 << 14)
#define XINE_MRL_TYPE_file_backup    (1 << 15)
#define XINE_MRL_TYPE_file_hidden    (1 << 16)

/* get a list of browsable input plugin ids */
const char *const *xine_get_browsable_input_plugin_ids (xine_t *self)  XINE_PROTECTED;

/*
 * ask input plugin named <plugin_id> to return
 * a list of available MRLs in domain/directory <start_mrl>.
 *
 * <start_mrl> may be NULL indicating the toplevel domain/dir
 * returns <start_mrl> if <start_mrl> is a valid MRL, not a directory
 * returns NULL if <start_mrl> is an invalid MRL, not even a directory.
 */
xine_mrl_t **xine_get_browse_mrls (xine_t *self,
				   const char *plugin_id,
				   const char *start_mrl,
				   int *num_mrls) XINE_PROTECTED;

/* get a list of plugins that support the autoplay feature */
const char *const *xine_get_autoplay_input_plugin_ids (xine_t *self) XINE_PROTECTED;

/* get autoplay MRL list from input plugin named <plugin_id> */
const char * const *xine_get_autoplay_mrls (xine_t *self,
					    const char *plugin_id,
					    int *num_mrls) XINE_PROTECTED;

/* get a list of file extensions for file types supported by xine
 * the list is separated by spaces
 *
 * the pointer returned can be free()ed when no longer used */
char *xine_get_file_extensions (xine_t *self) XINE_PROTECTED;

/* get a list of mime types supported by xine
 *
 * the pointer returned can be free()ed when no longer used */
char *xine_get_mime_types (xine_t *self) XINE_PROTECTED;

/* get the demuxer identifier that handles a given mime type
 *
 * the pointer returned can be free()ed when no longer used
 * returns NULL if no demuxer is available to handle this. */
char *xine_get_demux_for_mime_type (xine_t *self, const char *mime_type) XINE_PROTECTED;

/* get a description string for a plugin */
const char *xine_get_input_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_demux_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_spu_plugin_description   (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_audio_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_video_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_audio_driver_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_video_driver_plugin_description (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;
const char *xine_get_post_plugin_description  (xine_t *self,
					       const char *plugin_id) XINE_PROTECTED;

/* get lists of available audio and video output plugins */
const char *const *xine_list_audio_output_plugins (xine_t *self) XINE_PROTECTED;
const char *const *xine_list_video_output_plugins (xine_t *self) XINE_PROTECTED;
/* typemask is (1ULL << XINE_VISUAL_TYPE_FOO) | ... */
const char *const *xine_list_video_output_plugins_typed (xine_t *self, uint64_t typemask) XINE_PROTECTED;

/* get list of available demultiplexor plugins */
const char *const *xine_list_demuxer_plugins(xine_t *self) XINE_PROTECTED;

/* get list of available input plugins */
const char *const *xine_list_input_plugins(xine_t *self) XINE_PROTECTED;

/* get list of available subpicture plugins */
const char *const *xine_list_spu_plugins(xine_t *self) XINE_PROTECTED;

/* get list of available audio and video decoder plugins */
const char *const *xine_list_audio_decoder_plugins(xine_t *self) XINE_PROTECTED;
const char *const *xine_list_video_decoder_plugins(xine_t *self) XINE_PROTECTED;

/* unload unused plugins */
void xine_plugins_garbage_collector(xine_t *self) XINE_PROTECTED;


/*********************************************************************
 * visual specific gui <-> xine engine communication                 *
 *********************************************************************/

/* new (preferred) method to talk to video driver. */
int    xine_port_send_gui_data (xine_video_port_t *vo,
			        int type, void *data) XINE_PROTECTED;

typedef struct {

  /* area of that drawable to be used by video */
  int      x,y,w,h;

} x11_rectangle_t;

/*
 * this is the visual data struct any x11 gui
 * must supply to the xine_open_video_driver call
 * ("data" parameter)
 */
typedef struct {

  /* some information about the display */
  void             *display; /* Display* */
  int               screen;

  /* drawable to display the video in/on */
  unsigned long     d; /* Drawable */

  void             *user_data;

  /*
   * dest size callback
   *
   * this will be called by the video driver to find out
   * how big the video output area size will be for a
   * given video size. The ui should _not_ adjust its
   * video out area, just do some calculations and return
   * the size. This will be called for every frame, ui
   * implementation should be fast.
   * dest_pixel_aspect should be set to the used display pixel aspect.
   * NOTE: Semantics has changed: video_width and video_height
   * are no longer pixel aspect corrected. Get the old semantics
   * in the UI with
   *   *dest_pixel_aspect = display_pixel_aspect;
   *   if (video_pixel_aspect >= display_pixel_aspect)
   *     video_width  = video_width * video_pixel_aspect / display_pixel_aspect + .5;
   *   else
   *     video_height = video_height * display_pixel_aspect / video_pixel_aspect + .5;
   */
  void (*dest_size_cb) (void *user_data,
			int video_width, int video_height,
			double video_pixel_aspect,
			int *dest_width, int *dest_height,
			double *dest_pixel_aspect);

  /*
   * frame output callback
   *
   * this will be called by the video driver for every frame
   * it's about to draw. ui can adapt its size if necessary
   * here.
   * note: the ui doesn't have to adjust itself to this
   * size, this is just to be taken as a hint.
   * ui must return the actual size of the video output
   * area and the video output driver will do its best
   * to adjust the video frames to that size (while
   * preserving aspect ratio and stuff).
   *    dest_x, dest_y: offset inside window
   *    dest_width, dest_height: available drawing space
   *    dest_pixel_aspect: display pixel aspect
   *    win_x, win_y: window absolute screen position
   * NOTE: Semantics has changed: video_width and video_height
   * are no longer pixel aspect corrected. Get the old semantics
   * in the UI with
   *   *dest_pixel_aspect = display_pixel_aspect;
   *   if (video_pixel_aspect >= display_pixel_aspect)
   *     video_width  = video_width * video_pixel_aspect / display_pixel_aspect + .5;
   *   else
   *     video_height = video_height * display_pixel_aspect / video_pixel_aspect + .5;
   */
  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   double video_pixel_aspect,
			   int *dest_x, int *dest_y,
			   int *dest_width, int *dest_height,
			   double *dest_pixel_aspect,
			   int *win_x, int *win_y);

  /*
   * lock display callback
   *
   * this callback is called when the video driver
   * needs access to the x11 display connection
   *
   * note: to enable this you MUST use XINE_VISUAL_TYPE_X11_2
   * note: if display_lock is NULL, the fallback is used
   * note: fallback for this function is XLockDisplay(display)
   */
   void (*lock_display) (void *user_data);

  /*
   * unlock display callback
   *
   * this callback is called when the video driver
   * doesn't need access to the x11 display connection anymore
   *
   * note: to enable this you MUST use XINE_VISUAL_TYPE_X11_2
   * note: if display_unlock is NULL, the fallback is used
   * note: fallback for this function is XUnlockDisplay(display)
   */
   void (*unlock_display) (void *user_data);

} x11_visual_t;

/*
 * this is the visual data struct any xcb gui
 * must supply to the xine_open_video_driver call
 * ("data" parameter)
 */
typedef struct {

  /* some information about the display */
  void        *connection; /* xcb_connection_t */
  void        *screen;     /* xcb_screen_t     */

  /* window to display the video in / on */
  unsigned int window; /* xcb_window_t */

  void        *user_data;

  /*
   * dest size callback
   *
   * this will be called by the video driver to find out
   * how big the video output area size will be for a
   * given video size. The ui should _not_ adjust its
   * video out area, just do some calculations and return
   * the size. This will be called for every frame, ui
   * implementation should be fast.
   * dest_pixel_aspect should be set to the used display pixel aspect.
   * NOTE: Semantics has changed: video_width and video_height
   * are no longer pixel aspect corrected. Get the old semantics
   * in the UI with
   *   *dest_pixel_aspect = display_pixel_aspect;
   *   if (video_pixel_aspect >= display_pixel_aspect)
   *     video_width  = video_width * video_pixel_aspect / display_pixel_aspect + .5;
   *   else
   *     video_height = video_height * display_pixel_aspect / video_pixel_aspect + .5;
   */
  void (*dest_size_cb) (void *user_data,
			int video_width, int video_height,
			double video_pixel_aspect,
			int *dest_width, int *dest_height,
			double *dest_pixel_aspect);

  /*
   * frame output callback
   *
   * this will be called by the video driver for every frame
   * it's about to draw. ui can adapt its size if necessary
   * here.
   * note: the ui doesn't have to adjust itself to this
   * size, this is just to be taken as a hint.
   * ui must return the actual size of the video output
   * area and the video output driver will do its best
   * to adjust the video frames to that size (while
   * preserving aspect ratio and stuff).
   *    dest_x, dest_y: offset inside window
   *    dest_width, dest_height: available drawing space
   *    dest_pixel_aspect: display pixel aspect
   *    win_x, win_y: window absolute screen position
   * NOTE: Semantics has changed: video_width and video_height
   * are no longer pixel aspect corrected. Get the old semantics
   * in the UI with
   *   *dest_pixel_aspect = display_pixel_aspect;
   *   if (video_pixel_aspect >= display_pixel_aspect)
   *     video_width  = video_width * video_pixel_aspect / display_pixel_aspect + .5;
   *   else
   *     video_height = video_height * display_pixel_aspect / video_pixel_aspect + .5;
   */
  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   double video_pixel_aspect,
			   int *dest_x, int *dest_y,
			   int *dest_width, int *dest_height,
			   double *dest_pixel_aspect,
			   int *win_x, int *win_y);

} xcb_visual_t;

/**************************************************
 * XINE_VO_RAW struct definitions
 *************************************************/
/*  frame_format definitions */
#define XINE_VORAW_YV12 1
#define XINE_VORAW_YUY2 2
#define XINE_VORAW_RGB 4

/*  maximum number of overlays the raw driver can handle */
#define XINE_VORAW_MAX_OVL 16

/* raw_overlay_t struct used in raw_overlay_cb callback */
typedef struct {
  uint8_t *ovl_rgba;
  int ovl_w, ovl_h; /* overlay's width and height */
  int ovl_x, ovl_y; /* overlay's top-left display position */
} raw_overlay_t;

/* this is the visual data struct any raw gui
 * must supply to the xine_open_video_driver call
 * ("data" parameter)
 */
typedef struct {
  void *user_data;

  /* OR'ed frame_format
   * Unsupported frame formats are converted to rgb.
   * XINE_VORAW_RGB is always assumed by the driver, even if not set.
   * So a frontend must at least support rgb.
   * Be aware that rgb requires more cpu than yuv,
   * so avoid its usage for video playback.
   * However, it's useful for single frame capture (e.g. thumbs)
   */
  int supported_formats;

  /* raw output callback
   * this will be called by the video driver for every frame
   *
   * If frame_format==XINE_VORAW_YV12, data0 points to frame_width*frame_height Y values
   *                                   data1 points to (frame_width/2)*(frame_height/2) U values
   *                                   data2 points to (frame_width/2)*(frame_height/2) V values
   *
   * If frame_format==XINE_VORAW_YUY2, data0 points to frame_width*frame_height*2 YU/Y²V values
   *                                   data1 is NULL
   *                                   data2 is NULL
   *
   * If frame_format==XINE_VORAW_RGB, data0 points to frame_width*frame_height*3 RGB values
   *                                  data1 is NULL
   *                                  data2 is NULL
   */
  void (*raw_output_cb) (void *user_data, int frame_format,
                        int frame_width, int frame_height,
			double frame_aspect,
			void *data0, void *data1, void *data2);

  /* raw overlay callback
   * this will be called by the video driver for every new overlay state
   * overlays_array points to an array of num_ovl raw_overlay_t
   * Note that num_ovl can be 0, meaning "end of overlay display"
   * num_ovl is at most XINE_VORAW_MAX_OVL */
  void (*raw_overlay_cb) (void *user_data, int num_ovl,
                         raw_overlay_t *overlays_array);
} raw_visual_t;
/**********************************************
 * end of vo_raw defs
 *********************************************/

/*
 * this is the visual data struct any fb gui
 * may supply to the xine_open_video_driver call
 * ("data" parameter) to get frame_output_cd calls
 */

typedef struct {

 void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   double video_pixel_aspect,
			   int *dest_x, int *dest_y,
			   int *dest_width, int *dest_height,
			   double *dest_pixel_aspect,
			   int *win_x, int *win_y);

  void             *user_data;

} fb_visual_t;

#ifdef WIN32
/*
 * this is the visual data struct any win32 gui should supply
 * (pass this to init_video_out_plugin or the xine_load_video_output_plugin
 * utility function)
 */

typedef struct {

  HWND      WndHnd;     /* handle of window associated with primary surface */
  HINSTANCE HInst;      /* handle of windows application instance */
  RECT      WndRect;    /* rect of window client points translated to screen
                         * cooridnates */
  int       FullScreen; /* is window fullscreen */
  HBRUSH    Brush;      /* window brush for background color */
  COLORREF  ColorKey;   /* window brush color key */

} win32_visual_t;

/*
 * constants for gui_data_exchange's data_type parameter
 */

#define GUI_WIN32_MOVED_OR_RESIZED	0

#endif /* WIN32 */

/*
 * "type" constants for xine_port_send_gui_data(...)
 */

#ifndef XINE_DISABLE_DEPRECATED_FEATURES
/* xevent *data */
#define XINE_GUI_SEND_COMPLETION_EVENT       1 /* DEPRECATED */
#endif

/* Drawable data */
#define XINE_GUI_SEND_DRAWABLE_CHANGED       2

/* xevent *data */
#define XINE_GUI_SEND_EXPOSE_EVENT           3

/* x11_rectangle_t *data */
#define XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO 4

/* int data */
#define XINE_GUI_SEND_VIDEOWIN_VISIBLE	     5

/* *data contains chosen visual, select a new one or change it to NULL
 * to indicate the visual to use or that no visual will work */
/* XVisualInfo **data */
#define XINE_GUI_SEND_SELECT_VISUAL          8

/* Gui is about to destroy drawable */
#define XINE_GUI_SEND_WILL_DESTROY_DRAWABLE  9


/*********************************************************************
 * xine health check stuff                                           *
 *********************************************************************/

#define XINE_HEALTH_CHECK_OK            0
#define XINE_HEALTH_CHECK_FAIL          1
#define XINE_HEALTH_CHECK_UNSUPPORTED   2
#define XINE_HEALTH_CHECK_NO_SUCH_CHECK 3

#define CHECK_KERNEL    0
#define CHECK_MTRR      1
#define CHECK_CDROM     2
#define CHECK_DVDROM    3
#define CHECK_DMA       4
#define CHECK_X         5
#define CHECK_XV        6

struct xine_health_check_s {
  const char* cdrom_dev;
  const char* dvd_dev;
  const char* msg;
  const char* title;
  const char* explanation;
  int         status;
};

typedef struct xine_health_check_s xine_health_check_t;
xine_health_check_t* xine_health_check(xine_health_check_t*, int check_num) XINE_PROTECTED;


/*********************************************************************
 * configuration system                                              *
 *********************************************************************/

/*
 * config entry data types
 */

#define XINE_CONFIG_TYPE_UNKNOWN 0
#define XINE_CONFIG_TYPE_RANGE   1
#define XINE_CONFIG_TYPE_STRING  2
#define XINE_CONFIG_TYPE_ENUM    3
#define XINE_CONFIG_TYPE_NUM     4
#define XINE_CONFIG_TYPE_BOOL    5

/* For the string type (1.1.4 and later). These are stored in num_value. */
#define XINE_CONFIG_STRING_IS_STRING		0
#define XINE_CONFIG_STRING_IS_FILENAME		1
#define XINE_CONFIG_STRING_IS_DEVICE_NAME	2
#define XINE_CONFIG_STRING_IS_DIRECTORY_NAME	3

typedef struct xine_cfg_entry_s xine_cfg_entry_t;

typedef void (*xine_config_cb_t) (void *user_data,
				  xine_cfg_entry_t *entry);
struct xine_cfg_entry_s {
  const char      *key;     /* unique id (example: gui.logo_mrl) */

  int              type;

  /* user experience level */
  int              exp_level; /* 0 => beginner,
			        10 => advanced user,
			        20 => expert */

  /* type unknown */
  char            *unknown_value;

  /* type string */
  char            *str_value;
  char            *str_default;

  /* common to range, enum, num, bool;
   * num_value is also used by string to indicate what's required:
   * plain string, file name, device name, directory name
   */
  int              num_value;
  int              num_default;

  /* type range specific: */
  int              range_min;
  int              range_max;

  /* type enum specific: */
  char           **enum_values;

  /* help info for the user (UTF-8)
   * the help string must be word wrapped by the frontend.
   * it might contain \n to mark paragraph breaks.
   */
  const char      *description;
  const char      *help;

  /* callback function and data for live changeable values */
  /* some config entries will take effect immediately, although they
   * do not have a callback registered; such values will have some
   * non-NULL dummy value in callback_data; so if you want to check,
   * if a config change will require restarting xine, check for
   * callback_data == NULL */
  xine_config_cb_t callback;
  void            *callback_data;

};

const char *xine_config_register_string (xine_t *self,
					 const char *key,
					 const char *def_value,
					 const char *description,
					 const char *help,
					 int   exp_level,
					 xine_config_cb_t changed_cb,
					 void *cb_data) XINE_PROTECTED;

const char *xine_config_register_filename (xine_t *self,
					   const char *key,
					   const char *def_value,
					   int req_type, /* XINE_CONFIG_STRING_IS_* */
					   const char *description,
					   const char *help,
					   int   exp_level,
					   xine_config_cb_t changed_cb,
					   void *cb_data) XINE_PROTECTED;

int   xine_config_register_range  (xine_t *self,
				   const char *key,
				   int def_value,
				   int min, int max,
				   const char *description,
				   const char *help,
				   int   exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) XINE_PROTECTED;

int   xine_config_register_enum   (xine_t *self,
				   const char *key,
				   int def_value,
				   char **values,
				   const char *description,
				   const char *help,
				   int   exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) XINE_PROTECTED;

int   xine_config_register_num    (xine_t *self,
				   const char *key,
				   int def_value,
				   const char *description,
				   const char *help,
				   int   exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) XINE_PROTECTED;

int   xine_config_register_bool   (xine_t *self,
				   const char *key,
				   int def_value,
				   const char *description,
				   const char *help,
				   int   exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) XINE_PROTECTED;

/*
 * the following functions will copy data from the internal xine_config
 * data database to the xine_cfg_entry_t *entry you provide
 *
 * they return 1 on success, 0 on failure
 */

/* get first config item */
int  xine_config_get_first_entry (xine_t *self, xine_cfg_entry_t *entry) XINE_PROTECTED;

/* get next config item (iterate through the items) */
int  xine_config_get_next_entry (xine_t *self, xine_cfg_entry_t *entry) XINE_PROTECTED;

/* search for a config entry by key */
int  xine_config_lookup_entry (xine_t *self, const char *key,
			       xine_cfg_entry_t *entry) XINE_PROTECTED;

/*
 * update a config entry (which was returned from lookup_entry() )
 *
 * xine will make a deep copy of the data in the entry into its internal
 * config database.
 */
void xine_config_update_entry (xine_t *self,
			       const xine_cfg_entry_t *entry) XINE_PROTECTED;

/*
 * translation of old configuration entry names
 */
typedef struct {
  const char *old_name, *new_name;
} xine_config_entry_translation_t;

void xine_config_set_translation_user (const xine_config_entry_translation_t *) XINE_PROTECTED;

/*
 * load/save config data from/to afile (e.g. $HOME/.xine/config)
 */
void xine_config_load  (xine_t *self, const char *cfg_filename) XINE_PROTECTED;
void xine_config_save  (xine_t *self, const char *cfg_filename) XINE_PROTECTED;
void xine_config_reset (xine_t *self) XINE_PROTECTED;


/*********************************************************************
 * asynchroneous xine event mechanism                                *
 *********************************************************************/

/*
 * to receive events you have to register an event queue with
 * the xine engine (xine_event_new_queue, see below).
 *
 * then you can either
 * 1) check for incoming events regularly (xine_event_get/wait),
 *    process them and free them using xine_event_free
 * 2) use xine_event_create_listener_thread and specify a callback
 *    which will then be called for each event
 *
 * to send events to every module listening you don't need
 * to register an event queue but simply call xine_event_send.
 *
 * front ends should listen for one of MRL_REFERENCE and MRL_REFERENCE_EXT
 * since both will be sent for compatibility reasons
 */

/* event types */
#define XINE_EVENT_UI_PLAYBACK_FINISHED   1 /* frontend can e.g. move on to next playlist entry */
#define XINE_EVENT_UI_CHANNELS_CHANGED    2 /* inform ui that new channel info is available */
#define XINE_EVENT_UI_SET_TITLE           3 /* request title display change in ui */
#define XINE_EVENT_UI_MESSAGE             4 /* message (dialog) for the ui to display */
#define XINE_EVENT_FRAME_FORMAT_CHANGE    5 /* e.g. aspect ratio change during dvd playback */
#define XINE_EVENT_AUDIO_LEVEL            6 /* report current audio level (l/r/mute) */
#define XINE_EVENT_QUIT                   7 /* last event sent when stream is disposed */
#define XINE_EVENT_PROGRESS               8 /* index creation/network connections */
#define XINE_EVENT_MRL_REFERENCE          9 /* (deprecated) demuxer->frontend: MRL reference(s) for the real stream */
#define XINE_EVENT_UI_NUM_BUTTONS        10 /* number of buttons for interactive menus */
#define XINE_EVENT_SPU_BUTTON            11 /* the mouse pointer enter/leave a button */
#define XINE_EVENT_DROPPED_FRAMES        12 /* number of dropped frames is too high */
#define XINE_EVENT_MRL_REFERENCE_EXT     13 /* demuxer->frontend: MRL reference(s) for the real stream */
#define XINE_EVENT_AUDIO_AMP_LEVEL       14 /* report current audio amp level (l/r/mute) */
#define XINE_EVENT_NBC_STATS             15 /* nbc buffer status */
#define XINE_EVENT_FRAMERATE_CHANGE      16


/* input events coming from frontend */
#define XINE_EVENT_INPUT_MOUSE_BUTTON   101
#define XINE_EVENT_INPUT_MOUSE_MOVE     102
#define XINE_EVENT_INPUT_MENU1          103
#define XINE_EVENT_INPUT_MENU2          104
#define XINE_EVENT_INPUT_MENU3          105
#define XINE_EVENT_INPUT_MENU4          106
#define XINE_EVENT_INPUT_MENU5          107
#define XINE_EVENT_INPUT_MENU6          108
#define XINE_EVENT_INPUT_MENU7          109
#define XINE_EVENT_INPUT_UP             110
#define XINE_EVENT_INPUT_DOWN           111
#define XINE_EVENT_INPUT_LEFT           112
#define XINE_EVENT_INPUT_RIGHT          113
#define XINE_EVENT_INPUT_SELECT         114
#define XINE_EVENT_INPUT_NEXT           115
#define XINE_EVENT_INPUT_PREVIOUS       116
#define XINE_EVENT_INPUT_ANGLE_NEXT     117
#define XINE_EVENT_INPUT_ANGLE_PREVIOUS 118
#define XINE_EVENT_INPUT_BUTTON_FORCE   119
#define XINE_EVENT_INPUT_NUMBER_0       120
#define XINE_EVENT_INPUT_NUMBER_1       121
#define XINE_EVENT_INPUT_NUMBER_2       122
#define XINE_EVENT_INPUT_NUMBER_3       123
#define XINE_EVENT_INPUT_NUMBER_4       124
#define XINE_EVENT_INPUT_NUMBER_5       125
#define XINE_EVENT_INPUT_NUMBER_6       126
#define XINE_EVENT_INPUT_NUMBER_7       127
#define XINE_EVENT_INPUT_NUMBER_8       128
#define XINE_EVENT_INPUT_NUMBER_9       129
#define XINE_EVENT_INPUT_NUMBER_10_ADD  130

/* specific event types */
#define XINE_EVENT_SET_V4L2             200
#define XINE_EVENT_PVR_SAVE             201
#define XINE_EVENT_PVR_REPORT_NAME      202
#define XINE_EVENT_PVR_REALTIME         203
#define XINE_EVENT_PVR_PAUSE            204
#define XINE_EVENT_SET_MPEG_DATA        205

/* VDR specific event types */
#define XINE_EVENT_VDR_RED              300
#define XINE_EVENT_VDR_GREEN            301
#define XINE_EVENT_VDR_YELLOW           302
#define XINE_EVENT_VDR_BLUE             303
#define XINE_EVENT_VDR_PLAY             304
#define XINE_EVENT_VDR_PAUSE            305
#define XINE_EVENT_VDR_STOP             306
#define XINE_EVENT_VDR_RECORD           307
#define XINE_EVENT_VDR_FASTFWD          308
#define XINE_EVENT_VDR_FASTREW          309
#define XINE_EVENT_VDR_POWER            310
#define XINE_EVENT_VDR_CHANNELPLUS      311
#define XINE_EVENT_VDR_CHANNELMINUS     312
#define XINE_EVENT_VDR_SCHEDULE         313
#define XINE_EVENT_VDR_CHANNELS         314
#define XINE_EVENT_VDR_TIMERS           315
#define XINE_EVENT_VDR_RECORDINGS       316
#define XINE_EVENT_VDR_SETUP            317
#define XINE_EVENT_VDR_COMMANDS         318
#define XINE_EVENT_VDR_BACK             319
#define XINE_EVENT_VDR_USER1            320
#define XINE_EVENT_VDR_USER2            321
#define XINE_EVENT_VDR_USER3            322
#define XINE_EVENT_VDR_USER4            323
#define XINE_EVENT_VDR_USER5            324
#define XINE_EVENT_VDR_USER6            325
#define XINE_EVENT_VDR_USER7            326
#define XINE_EVENT_VDR_USER8            327
#define XINE_EVENT_VDR_USER9            328
#define XINE_EVENT_VDR_VOLPLUS          329
#define XINE_EVENT_VDR_VOLMINUS         330
#define XINE_EVENT_VDR_MUTE             331
#define XINE_EVENT_VDR_AUDIO            332
#define XINE_EVENT_VDR_INFO             333
#define XINE_EVENT_VDR_CHANNELPREVIOUS  334
#define XINE_EVENT_VDR_SUBTITLES        335
#define XINE_EVENT_VDR_USER0            336
/* some space for further keys */
#define XINE_EVENT_VDR_SETVIDEOWINDOW   350
#define XINE_EVENT_VDR_FRAMESIZECHANGED 351
#define XINE_EVENT_VDR_SELECTAUDIO      352
#define XINE_EVENT_VDR_TRICKSPEEDMODE   353
#define XINE_EVENT_VDR_PLUGINSTARTED    354
#define XINE_EVENT_VDR_DISCONTINUITY    355

/* events generated from post plugins */
#define XINE_EVENT_POST_TVTIME_FILMMODE_CHANGE   400

#define XINE_EVENT_SET_VIDEO_STREAMTYPE 501
#define XINE_EVENT_SET_AUDIO_STREAMTYPE 502


/*
 * xine event struct
 */
typedef struct {
  xine_stream_t                   *stream; /* stream this event belongs to     */

  void                            *data;   /* contents depending on type */
  int                              data_length;

  int                              type;   /* event type (constants see above) */

  /* you do not have to provide this, it will be filled in by xine_event_send() */
  struct timeval                   tv;     /* timestamp of event creation */
} xine_event_t;

/*
 * input event dynamic data
 */
typedef struct {
  xine_event_t        event;
  uint8_t             button; /* Generally 1 = left, 2 = mid, 3 = right */
  uint16_t            x,y;    /* In Image space */
} xine_input_data_t;

/*
 * UI event dynamic data - send information to/from UI.
 */
typedef struct {
  int                 num_buttons;
  int                 str_len;
  char                str[256]; /* might be longer */
} xine_ui_data_t;

typedef struct {
  int                 pid;
  int                 streamtype;
} xine_streamtype_data_t;

/*
 * Send messages to UI. used mostly to report errors.
 */
typedef struct {
  /*
   * old xine-ui versions expect xine_ui_data_t type.
   * this struct is added for compatibility.
   */
  xine_ui_data_t      compatibility;

  /* See XINE_MSG_xxx for defined types. */
  int                 type;

  /* defined types are provided with a standard explanation.
   * note: zero means no explanation.
   */
  int                 explanation; /* add to struct address to get a valid (char *) */

  /* parameters are zero terminated strings */
  int                 num_parameters;
  int                 parameters;  /* add to struct address to get a valid (char *) */

  /* where messages are stored, will be longer
   *
   * this field begins with the message text itself (\0-terminated),
   * followed by (optional) \0-terminated parameter strings
   * the end marker is \0 \0
   */
  char                messages[1];
} xine_ui_message_data_t;


/*
 * notify frame format change
 */
typedef struct {
  int                 width;
  int                 height;
  /* these are aspect codes as defined in MPEG2, because this
   * is only used for DVD playback, pan_scan is a boolean flag */
  int                 aspect;
  int                 pan_scan;
} xine_format_change_data_t;

/*
 * audio level for left/right channel
 */
typedef struct {
  int                 left;
  int                 right;  /* 0..100 % */
  int                 mute;
} xine_audio_level_data_t;

/*
 * index generation / buffering
 */
typedef struct {
  const char         *description; /* e.g. "connecting..." */
  int                 percent;
} xine_progress_data_t;

/*
 * nbc buffer status
 */
typedef struct {
  int                 v_percent;    /* fill of video buffer */
  int64_t             v_remaining;  /* remaining time in ms till underrun */
  int64_t             v_bitrate;    /* current bitrate */
  int                 v_in_disc;    /* in discontinuity */
  int                 a_percent;    /* like video, but for audio */
  int64_t             a_remaining;
  int64_t             a_bitrate;
  int                 a_in_disc;
  int                 buffering;    /* currently filling buffer */
  int                 enabled;      /* buffer disabled by engine */
  int                 type;         /* 0=buffer put, 1=buffer get */
} xine_nbc_stats_data_t;

typedef struct {
  int64_t             framerate;
} xine_framerate_data_t;

/*
 * mrl reference data is sent by demuxers when a reference stream is found.
 * this stream just contains pointers (urls) to the real data, which are
 * passed to frontend using this event type. (examples: .asx, .mov and .ram)
 *
 * ideally, frontends should add these mrls to a "hierarchical playlist".
 * that is, instead of the original file, the ones provided here should be
 * played instead. on pratice, just using a simple playlist should work.
 *
 * mrl references should be played in the same order they are received, just
 * after the current stream finishes.
 * alternative entries may be provided and should be used in case of
 * failure of the primary stream (the one with alternative=0).
 *
 * sample playlist:
 *   1) http://something/something.ram
 *     1a) rtsp://something/realsomething1.rm (alternative=0)
 *     1b) pnm://something/realsomething1.rm (alternative=1)
 *   2) http://another/another.avi
 *
 * 1 and 2 are the original items on this playlist. 1a and 1b were received
 * by events (they are the mrl references enclosed in 1). 1a is played after
 * receiving the finished event from 1. note: 1b is usually ignored, it should
 * only be used in case 1a fails to open.
 *
 * An event listener which accepts XINE_EVENT_MRL_REFERENCE_EXT events
 * *must* ignore XINE_EVENT_MRL_REFERENCE events.
 */
typedef struct {
  int                 alternative; /* alternative playlist number, usually 0 */
  char                mrl[1]; /* might (will) be longer */
} xine_mrl_reference_data_t XINE_DEPRECATED;

typedef struct {
  int                 alternative; /* as above */
  uint32_t            start_time, duration; /* milliseconds */
  uint32_t            spare[20]; /* for future expansion */
  const char          mrl[1]; /* might (will) be longer */
/*const char          title[]; ** immediately follows MRL's terminating NUL */
} xine_mrl_reference_data_ext_t;

/*
 * configuration options for video4linux-like input plugins
 */
typedef struct {
  /* input selection */
  int input;                    /* select active input from card */
  int channel;                  /* channel number */
  int radio;                    /* ask for a radio channel */
  uint32_t frequency;           /* frequency divided by 62.5KHz or 62.5 Hz */
  uint32_t transmission;        /* The transmission standard. */

  /* video parameters */
  uint32_t framerate_numerator; /* framerate as numerator/denominator */
  uint32_t framerate_denominator;
  uint32_t framelines;          /* Total lines per frame including blanking */
  uint64_t standard_id;         /* One of the V4L2_STD_* values */
  uint32_t colorstandard;       /* One of the V4L2_COLOR_STD_* values */
  uint32_t colorsubcarrier;     /* The color subcarrier frequency */
  int frame_width;              /* scaled frame width */
  int frame_height;             /* scaled frame height */

  /* let some spare space so we can add new fields without breaking
   * binary api compatibility.
   */
  uint32_t spare[20];

  /* used by pvr plugin */
  int32_t session_id;           /* -1 stops pvr recording */

} xine_set_v4l2_data_t;

/*
 * configuration options for plugins that can do a kind of mpeg encoding
 * note: highly experimental api :)
 */
typedef struct {

  /* mpeg2 parameters */
  int bitrate_vbr;              /* 1 = vbr, 0 = cbr */
  int bitrate_mean;             /* mean (target) bitrate in kbps*/
  int bitrate_peak;             /* peak (max) bitrate in kbps */
  int gop_size;                 /* GOP size in frames */
  int gop_closure;              /* open/closed GOP */
  int b_frames;                 /* number of B frames to use */
  int aspect_ratio;             /* XINE_VO_ASPECT_xxx */

  /* let some spare space so we can add new fields without breaking
   * binary api compatibility.
   */
  uint32_t spare[20];

} xine_set_mpeg_data_t;

typedef struct {
  int                 direction; /* 0 leave, 1 enter */
  int32_t             button; /* button number */
} xine_spu_button_t;

#ifdef XINE_ENABLE_EXPERIMENTAL_FEATURES

/*
 * ask pvr to save (ie. do not discard) the current session
 * see comments on input_pvr.c to understand how it works.
 */
typedef struct {
  /* mode values:
   * -1 = do nothing, just set the name
   * 0 = truncate current session and save from now on
   * 1 = save from last sync point
   * 2 = save everything on current session
   */
  int mode;
  int id;
  char name[256]; /* name for saving, might be longer */
} xine_pvr_save_data_t;

typedef struct {
  /* mode values:
   * 0 = non realtime
   * 1 = realtime
   */
  int mode;
} xine_pvr_realtime_t;

typedef struct {
  /* mode values:
   * 0 = playing
   * 1 = paused
   */
  int mode;
} xine_pvr_pause_t;

#endif

/* event XINE_EVENT_DROPPED_FRAMES is generated if libxine detects a
 * high number of dropped frames (above configured thresholds). it can
 * be used by the front end to warn about performance problems.
 */
typedef struct {
  /* these values are given for 1000 frames delivered */
  /* (that is, divide by 10 to get percentages) */
  int skipped_frames;
  int skipped_threshold;
  int discarded_frames;
  int discarded_threshold;
} xine_dropped_frames_t;


/*
 * Defined message types for XINE_EVENT_UI_MESSAGE
 * This is the mechanism to report async errors from engine.
 *
 * If frontend knows about the XINE_MSG_xxx type it may safely
 * ignore the 'explanation' field and provide its own custom
 * dialogue to the 'parameters'.
 *
 * right column specifies the usual parameters.
 */

#define XINE_MSG_NO_ERROR		0  /* (messages to UI)   */
#define XINE_MSG_GENERAL_WARNING	1  /* (warning message)  */
#define XINE_MSG_UNKNOWN_HOST		2  /* (host name)        */
#define XINE_MSG_UNKNOWN_DEVICE		3  /* (device name)      */
#define XINE_MSG_NETWORK_UNREACHABLE	4  /* none               */
#define XINE_MSG_CONNECTION_REFUSED	5  /* (host name)        */
#define XINE_MSG_FILE_NOT_FOUND		6  /* (file name or mrl) */
#define XINE_MSG_READ_ERROR		7  /* (device/file/mrl)  */
#define XINE_MSG_LIBRARY_LOAD_ERROR	8  /* (library/decoder)  */
#define XINE_MSG_ENCRYPTED_SOURCE	9  /* none               */
#define XINE_MSG_SECURITY		10 /* (security message) */
#define XINE_MSG_AUDIO_OUT_UNAVAILABLE	11 /* none               */
#define XINE_MSG_PERMISSION_ERROR       12 /* (file name or mrl) */
#define XINE_MSG_FILE_EMPTY             13 /* file is empty      */
#define XINE_MSG_AUTHENTICATION_NEEDED	14 /* (mrl, likely http) */

/* opaque xine_event_queue_t */
typedef struct xine_event_queue_s xine_event_queue_t;

/*
 * register a new event queue
 *
 * you have to receive messages from this queue regularly
 *
 * use xine_event_dispose_queue to unregister and free the queue
 */
xine_event_queue_t *xine_event_new_queue (xine_stream_t *stream) XINE_PROTECTED;
void xine_event_dispose_queue (xine_event_queue_t *queue) XINE_PROTECTED;

/*
 * receive events (poll)
 *
 * use xine_event_free on the events received from these calls
 * when they're no longer needed
 */
xine_event_t *xine_event_get  (xine_event_queue_t *queue) XINE_PROTECTED;
xine_event_t *xine_event_wait (xine_event_queue_t *queue) XINE_PROTECTED;
void          xine_event_free (xine_event_t *event) XINE_PROTECTED;

/*
 * receive events (callback)
 *
 * a thread is created which will receive all events from
 * the specified queue, call your callback on each of them
 * and will then free the event when your callback returns
 *
 */
typedef void (*xine_event_listener_cb_t) (void *user_data,
					  const xine_event_t *event);
void xine_event_create_listener_thread (xine_event_queue_t *queue,
					xine_event_listener_cb_t callback,
					void *user_data) XINE_PROTECTED;

/*
 * send an event to all queues
 *
 * the event will be copied so you can free or reuse
 * *event as soon as xine_event_send returns.
 */
void xine_event_send (xine_stream_t *stream, const xine_event_t *event) XINE_PROTECTED;


/*********************************************************************
 * OSD (on screen display)                                           *
 *********************************************************************/

#define XINE_TEXT_PALETTE_SIZE 11

#define XINE_OSD_TEXT1  (0 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT2  (1 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT3  (2 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT4  (3 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT5  (4 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT6  (5 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT7  (6 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT8  (7 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT9  (8 * XINE_TEXT_PALETTE_SIZE)
#define XINE_OSD_TEXT10 (9 * XINE_TEXT_PALETTE_SIZE)

/* white text, black border, transparent background  */
#define XINE_TEXTPALETTE_WHITE_BLACK_TRANSPARENT    0
/* white text, noborder, transparent background      */
#define XINE_TEXTPALETTE_WHITE_NONE_TRANSPARENT     1
/* white text, no border, translucid background      */
#define XINE_TEXTPALETTE_WHITE_NONE_TRANSLUCID      2
/* yellow text, black border, transparent background */
#define XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT   3

#define XINE_OSD_CAP_FREETYPE2     0x0001 /* freetype2 support compiled in     */
#define XINE_OSD_CAP_UNSCALED      0x0002 /* unscaled overlays supp. by vo drv */
#define XINE_OSD_CAP_CUSTOM_EXTENT 0x0004 /* hardware scaled to match video output window */
#define XINE_OSD_CAP_ARGB_LAYER    0x0008 /* supports ARGB true color pixmaps */
#define XINE_OSD_CAP_VIDEO_WINDOW  0x0010 /* can scale video to an area within osd extent */

typedef struct xine_osd_s xine_osd_t;

xine_osd_t *xine_osd_new           (xine_stream_t *self, int x, int y,
				    int width, int height) XINE_PROTECTED;
uint32_t    xine_osd_get_capabilities (xine_osd_t *self) XINE_PROTECTED;
void        xine_osd_draw_point    (xine_osd_t *self, int x, int y, int color) XINE_PROTECTED;

void        xine_osd_draw_line     (xine_osd_t *self, int x1, int y1,
				    int x2, int y2, int color) XINE_PROTECTED;
void        xine_osd_draw_rect     (xine_osd_t *self, int x1, int y1,
				    int x2, int y2,
				    int color, int filled ) XINE_PROTECTED;
/* x1 and y1 specifies the upper left corner of the text to be rendered */
void        xine_osd_draw_text     (xine_osd_t *self, int x1, int y1,
				    const char *text, int color_base) XINE_PROTECTED;
void        xine_osd_draw_bitmap   (xine_osd_t *self, uint8_t *bitmap,
				    int x1, int y1, int width, int height,
				    uint8_t *palette_map) XINE_PROTECTED;
/* for freetype2 fonts the height is the maximum height for the whole font and not
 * only for the specified text */
void        xine_osd_get_text_size (xine_osd_t *self, const char *text,
				    int *width, int *height) XINE_PROTECTED;
/* with freetype2 support compiled in, you can also specify a font file
   as 'fontname' here */
int         xine_osd_set_font      (xine_osd_t *self, const char *fontname,
				    int size) XINE_PROTECTED;
/*
 * specifying encoding of texts
 *   ""   ... means current locale encoding (default)
 *   NULL ... means latin1
 */
void        xine_osd_set_encoding(xine_osd_t *self, const char *encoding) XINE_PROTECTED;
/* set position were overlay will be blended */
void        xine_osd_set_position  (xine_osd_t *self, int x, int y) XINE_PROTECTED;
void        xine_osd_show          (xine_osd_t *self, int64_t vpts) XINE_PROTECTED;
void        xine_osd_show_unscaled (xine_osd_t *self, int64_t vpts) XINE_PROTECTED;
void        xine_osd_show_scaled   (xine_osd_t *self, int64_t vpts) XINE_PROTECTED;
void        xine_osd_hide          (xine_osd_t *self, int64_t vpts) XINE_PROTECTED;
/* empty drawing area */
void        xine_osd_clear         (xine_osd_t *self) XINE_PROTECTED;
/*
 * set on existing text palette
 * (-1 to set used specified palette)
 *
 * color_base specifies the first color index to use for this text
 * palette. The OSD palette is then modified starting at this
 * color index, up to the size of the text palette.
 *
 * Use OSD_TEXT1, OSD_TEXT2, ... for some preassigned color indices.
 *
 * These palettes are not working well with the true type fonts.
 * First thing is that these fonts cannot have a border. So you get
 * the best results by loading a linearly blending palette from the
 * background (at index 0) to the forground color (at index 10).
 */
void        xine_osd_set_text_palette (xine_osd_t *self,
				       int palette_number,
				       int color_base ) XINE_PROTECTED;
/* get palette (color and transparency) */
void        xine_osd_get_palette   (xine_osd_t *self, uint32_t *color,
				    uint8_t *trans) XINE_PROTECTED;
void        xine_osd_set_palette   (xine_osd_t *self,
				    const uint32_t *const color,
				    const uint8_t *const trans ) XINE_PROTECTED;

/*
 * Set an ARGB buffer to be blended into video.
 * The buffer must stay valid while the OSD is on screen.
 * Pass a NULL pointer to safely remove the buffer from
 * the OSD layer. Only the dirty area  will be
 * updated on screen. For convenience the whole
 * OSD object will be considered dirty when setting
 * a different buffer pointer.
 * see also XINE_OSD_CAP_ARGB_LAYER
 */
void xine_osd_set_argb_buffer(xine_osd_t *self, uint32_t *argb_buffer,
                              int dirty_x, int dirty_y, int dirty_width, int dirty_height) XINE_PROTECTED;

/*
 * define extent of reference coordinate system
 * for video resolution independent osds.
 * see also XINE_OSD_CAP_CUSTOM_EXTENT
 */
void xine_osd_set_extent(xine_osd_t *self, int extent_width, int extent_height) XINE_PROTECTED;

/*
 * define area within osd extent to output
 * video to while osd is on screen
 * see also XINE_OSD_CAP_VIDEO_WINDOW
 */
void xine_osd_set_video_window(xine_osd_t *self, int window_x, int window_y, int window_width, int window_height) XINE_PROTECTED;

/*
 * close osd rendering engine
 * loaded fonts are unloaded
 * osd objects are closed
 */
void        xine_osd_free          (xine_osd_t *self) XINE_PROTECTED;

#ifdef __cplusplus
}
#endif

#endif
