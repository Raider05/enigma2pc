/*
 * Copyright (C) 2000-2012 the xine project
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
#ifndef HAVE_AUDIO_OUT_H
#define HAVE_AUDIO_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xine/os_types.h>
#include <xine/metronom.h>
#include <xine/configfile.h>
#include <xine/xineutils.h>

#ifdef XINE_COMPILE
#  include <xine/plugin_catalog.h>
#endif

#define AUDIO_OUT_IFACE_VERSION  9

/*
 * ao_driver_s contains the driver every audio output
 * driver plugin has to implement.
 */

typedef struct ao_driver_s ao_driver_t;

struct ao_driver_s {

  /*
   *
   * find out what output modes + capatilities are supported by
   * this plugin (constants for the bit vector to return see above)
   *
   * See AO_CAP_* bellow.
   */
  uint32_t (*get_capabilities) (ao_driver_t *);

  /*
   * open the driver and make it ready to receive audio data
   * buffers may be flushed(!)
   *
   * return value: 0 : failure, >0 : output sample rate
   */
  int (*open)(ao_driver_t *, uint32_t bits, uint32_t rate, int mode);

  /* return the number of audio channels
   */
  int (*num_channels)(ao_driver_t *self_gen);

  /* return the number of bytes per frame.
   * A frame is equivalent to one sample being output on every audio channel.
   */
  int (*bytes_per_frame)(ao_driver_t *self_gen);

  /* return the delay is frames measured by
   * looking at pending samples in the audio output device
   */
  int (*delay)(ao_driver_t *self_gen);

  /*
   * return gap tolerance (in pts) needed for this driver
   */
  int (*get_gap_tolerance) (ao_driver_t *self_gen);

  /*
   * write audio data to audio output device
   * return value:
   *  >0 => audio samples were processed ok
   *   0 => audio samples were not yet processed,
   *        call write_audio_data with the _same_ samples again
   */
  int (*write)(ao_driver_t *,
	       int16_t* audio_data, uint32_t num_samples);

  /*
   * this is called when the decoder no longer uses the audio
   * output driver - the driver should get ready to get opened() again
   */
  void (*close)(ao_driver_t *);

  /*
   * shut down this audio output driver plugin and
   * free all resources allocated
   */
  void (*exit) (ao_driver_t *);

  /*
   * Get, Set a property of audio driver.
   *
   * get_property() return 1 in success, 0 on failure.
   * set_property() return value on success, ~value on failure.
   *
   * See AO_PROP_* below for available properties.
   */
  int (*get_property) (ao_driver_t *, int property);

  int (*set_property) (ao_driver_t *, int property, int value);


  /*
   * misc control operations on the audio device.
   *
   * See AO_CTRL_* below.
   */
  int (*control) (ao_driver_t *, int cmd, /* arg */ ...);

  /**
   * @brief Pointer to the loaded plugin node.
   *
   * Used by the plugins loader. It's an opaque type when using the
   * structure outside of xine's build.
   */
#ifdef XINE_COMPILE
  plugin_node_t *node;
#else
  void *node;
#endif
};

typedef struct ao_format_s ao_format_t;

struct ao_format_s {
  uint32_t bits;
  uint32_t rate;
  int mode;
};

typedef struct audio_fifo_s audio_fifo_t;

typedef struct audio_buffer_s audio_buffer_t;

struct audio_buffer_s {

  audio_buffer_t    *next;

  int16_t           *mem;
  int                mem_size;
  int                num_frames;

  int64_t            vpts;
  uint32_t           frame_header_count;
  uint32_t           first_access_unit;

  /* extra info coming from input or demuxers */
  extra_info_t      *extra_info;

  xine_stream_t     *stream; /* stream that send that buffer */

  ao_format_t        format; /* let each buffer carry it's own format info */
};

/*
 * xine_audio_port_s contains the port every audio decoder talks to
 *
 * Remember that adding new functions to this structure requires
 * adaption of the post plugin decoration layer. Be sure to look into
 * src/xine-engine/post.[ch].
 */

struct xine_audio_port_s {
  uint32_t (*get_capabilities) (xine_audio_port_t *); /* for constants see below */

  /*   * Get/Set audio property
   *
   * See AO_PROP_* bellow
   */
  int (*get_property) (xine_audio_port_t *, int property);
  int (*set_property) (xine_audio_port_t *, int property, int value);

  /* open audio driver for audio output
   * return value: 0:failure, >0:output sample rate
   */
  /* when you are not a full-blown stream, but still need to open the port
   * (e.g. you are a post plugin) it is legal to pass an anonymous stream */
  int (*open) (xine_audio_port_t *, xine_stream_t *stream,
	       uint32_t bits, uint32_t rate, int mode);

  /*
   * get a piece of memory for audio data
   */
  audio_buffer_t * (*get_buffer) (xine_audio_port_t *);

  /*
   * append a buffer filled with audio data to the audio fifo
   * for output
   */
  /* when the frame does not originate from a stream, it is legal to pass an anonymous stream */
  void (*put_buffer) (xine_audio_port_t *, audio_buffer_t *buf, xine_stream_t *stream);

  /* audio driver is no longer used by decoder => close */
  /* when you are not a full-blown stream, but still need to close the port
   * (e.g. you are a post plugin) it is legal to pass an anonymous stream */
  void (*close) (xine_audio_port_t *self, xine_stream_t *stream);

  /* called on xine exit */
  void (*exit) (xine_audio_port_t *);

  /*
   * misc control operations on the audio device.
   *
   * See AO_CTRL_* below.
   */
  int (*control) (xine_audio_port_t *, int cmd, /* arg */ ...);

  /*
   * Flush audio_out fifo.
   */
  void (*flush) (xine_audio_port_t *);

  /*
   * Check if port is opened for this stream and get parameters.
   * The stream can be anonymous.
   */
  int (*status) (xine_audio_port_t *, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode);

};

typedef struct audio_driver_class_s audio_driver_class_t;

struct audio_driver_class_s {

  /*
   * open a new instance of this plugin class
   */
  ao_driver_t* (*open_plugin) (audio_driver_class_t *, const void *data);

  /**
   * @brief short human readable identifier for this plugin class
   */
  const char *identifier;

  /**
   * @brief human readable (verbose = 1 line) description for this plugin class
   *
   * The description is passed to gettext() to internationalise.
   */
  const char *description;

  /**
   * @brief Optional non-standard catalog to use with dgettext() for description.
   */
  const char *text_domain;

  /*
   * free all class-related resources
   */

  void (*dispose) (audio_driver_class_t *);
};

#define default_audio_driver_class_dispose (void (*) (audio_driver_class_t *this))free

/**
 * @brief Initialise the audio_out sync routines
 *
 * @internal
 */
xine_audio_port_t *_x_ao_new_port (xine_t *xine, ao_driver_t *driver, int grab_only) XINE_MALLOC;

/*
 * audio output modes + capabilities
 */

#define AO_CAP_NOCAP            0x00000000 /* driver has no capabilities    */
#define AO_CAP_MODE_A52         0x00000001 /* driver supports A/52 output   */
#define AO_CAP_MODE_AC5         0x00000002 /* driver supports AC5 output    */
/* 1 sample ==  2 bytes (C)               */
#define AO_CAP_MODE_MONO        0x00000004 /* driver supports mono output   */
/* 1 sample ==  4 bytes (L,R)             */
#define AO_CAP_MODE_STEREO      0x00000008 /* driver supports stereo output */
/* 1 sample ==  8 bytes (L,R,LR,RR)       */
#define AO_CAP_MODE_4CHANNEL    0x00000010 /* driver supports 4 channels    */
/*
 * Sound cards generally support, 1,2,4,6 channels, but rarely 5.
 * So xine will take 4.1, 5 and 6 channel a52 streams and
 * down or upmix it correctly to fill the 6 output channels.
 * Are there any requests for 2.1 out there?
 */
/* 1 sample == 12 bytes (L,R,LR,RR,Null,LFE)   */
#define AO_CAP_MODE_4_1CHANNEL  0x00000020 /* driver supports 4.1 channels  */
/* 1 sample == 12 bytes (L,R,LR,RR,C, Null)     */
#define AO_CAP_MODE_5CHANNEL    0x00000040 /* driver supports 5 channels    */
/* 1 sample == 12 bytes (L,R,LR,RR,C,LFE) */
#define AO_CAP_MODE_5_1CHANNEL  0x00000080 /* driver supports 5.1 channels  */

/*
 * converts the audio output mode into the number of channels
 */
int _x_ao_mode2channels( int mode ) XINE_PROTECTED;
/*
 * converts the number of channels into the audio output mode
 */
int _x_ao_channels2mode( int channels ) XINE_PROTECTED;

#define AO_CAP_MIXER_VOL        0x00000100 /* driver supports mixer control */
#define AO_CAP_PCM_VOL          0x00000200 /* driver supports pcm control   */
#define AO_CAP_MUTE_VOL         0x00000400 /* driver can mute volume        */
#define AO_CAP_8BITS            0x00000800 /* driver support 8-bit samples  */
#define AO_CAP_16BITS           0x00001000 /* driver support 16-bit samples  */
#define AO_CAP_24BITS           0x00002000 /* driver support 24-bit samples  */
#define AO_CAP_FLOAT32          0x00004000 /* driver support 32-bit samples. i.e. Floats  */

/* properties supported by get/set_property() */
#define AO_PROP_MIXER_VOL       0
#define AO_PROP_PCM_VOL         1
#define AO_PROP_MUTE_VOL        2
#define AO_PROP_COMPRESSOR      3
#define AO_PROP_DISCARD_BUFFERS 4
#define AO_PROP_BUFS_IN_FIFO    5 /* read-only */
#define AO_PROP_AMP             6 /* amplifier */
#define AO_PROP_EQ_30HZ         7 /* equalizer */
#define AO_PROP_EQ_60HZ         8 /* equalizer */
#define AO_PROP_EQ_125HZ        9 /* equalizer */
#define AO_PROP_EQ_250HZ       10 /* equalizer */
#define AO_PROP_EQ_500HZ       11 /* equalizer */
#define AO_PROP_EQ_1000HZ      12 /* equalizer */
#define AO_PROP_EQ_2000HZ      13 /* equalizer */
#define AO_PROP_EQ_4000HZ      14 /* equalizer */
#define AO_PROP_EQ_8000HZ      15 /* equalizer */
#define AO_PROP_EQ_16000HZ     16 /* equalizer */
#define AO_PROP_CLOSE_DEVICE   17 /* force closing audio device */
#define AO_PROP_AMP_MUTE       18 /* amplifier mute */
#define AO_PROP_NUM_STREAMS    19 /* read-only */
#define AO_PROP_CLOCK_SPEED    20 /* inform audio_out that speed has changed */
#define AO_PROP_BUFS_TOTAL     21 /* read-only */
#define AO_PROP_BUFS_FREE      22 /* read-only */
#define AO_PROP_DRIVER_DELAY   23 /* read-only */
#define AO_NUM_PROPERTIES      24

/* audio device control ops */
#define AO_CTRL_PLAY_PAUSE	0
#define AO_CTRL_PLAY_RESUME	1
#define AO_CTRL_FLUSH_BUFFERS	2

/* above that value audio frames are discarded */
#define AO_MAX_GAP              15000

#ifdef __cplusplus
}
#endif

#endif
