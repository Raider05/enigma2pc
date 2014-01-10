#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>

#include <jack/jack.h>

#include "speakers.h"

#define AO_OUT_JACK_IFACE_VERSION 9

#define GAP_TOLERANCE        AO_MAX_GAP
/* maximum number of channels supported, avoids lots of mallocs */
#define MAX_CHANS 6

typedef struct jack_driver_s
{
  ao_driver_t	ao_driver;
  xine_t	*xine;

  int		capabilities;
  int		mode;
  int		paused;
  int		underrun;

  int32_t	output_sample_rate, input_sample_rate;
  uint32_t	num_channels;
  uint32_t	bits_per_sample;
  uint32_t	bytes_per_frame;
  uint32_t	bytes_in_buffer; /* number of bytes writen to audio hardware */
  uint32_t	fragment_size;

  jack_client_t	*client;
  jack_port_t	*ports[MAX_CHANS];

  /*! buffer for audio data */
  unsigned char	*buffer;

  /*! buffer read position, may only be modified by playback thread or while it is stopped */
  uint32_t	read_pos;
  /*! buffer write position, may only be modified by MPlayer's thread */
  uint32_t	write_pos;

  struct
  {
    int		volume;
    int		mute;
  } mixer;

} jack_driver_t;

typedef struct
{
  audio_driver_class_t	driver_class;
  config_values_t	*config;
  xine_t		*xine;
} jack_class_t;


/**************************************************************
 *
 * Simple ringbuffer implementation
 * Lifted from mplayer ao_jack.c
 *
**************************************************************/


/*! size of one chunk, if this is too small Xine will start to "stutter" */
/*! after a short time of playback */
#define CHUNK_SIZE (16 * 1024)
/*! number of "virtual" chunks the buffer consists of */
#define NUM_CHUNKS 8
/* This type of ring buffer may never fill up completely, at least */
/* one byte must always be unused. */
/* For performance reasons (alignment etc.) one whole chunk always stays */
/* empty, not only one byte. */
#define BUFFSIZE ((NUM_CHUNKS + 1) * CHUNK_SIZE)

/**
 * \brief get the number of free bytes in the buffer
 * \return number of free bytes in buffer
 *
 * may only be called by Xine's thread
 * return value may change between immediately following two calls,
 * and the real number of free bytes might be larger!
 */
static int buf_free (jack_driver_t *this)
{
  int free = this->read_pos - this->write_pos - CHUNK_SIZE;
  if (free < 0)
    free += BUFFSIZE;
  return free;
}

/**
 * \brief get amount of data available in the buffer
 * \return number of bytes available in buffer
 *
 * may only be called by the playback thread
 * return value may change between immediately following two calls,
 * and the real number of buffered bytes might be larger!
 */
static int buf_used (jack_driver_t *this)
{
  int used = this->write_pos - this->read_pos;
  if (used < 0)
    used += BUFFSIZE;
  return used;
}

/**
 * \brief insert len bytes into buffer
 * \param data data to insert
 * \param len length of data
 * \return number of bytes inserted into buffer
 *
 * If there is not enough room, the buffer is filled up
 *
 * TODO: Xine should really pass data as float, perhaps in V1.2?
 */
static int write_buffer_32 (jack_driver_t *this, unsigned char *data, int len)
{
  int first_len = BUFFSIZE - this->write_pos;
  int free = buf_free (this);
  if (len > free)
    len = free;
  if (first_len > len)
    first_len = len;

  /* copy from current write_pos to end of buffer */
  memcpy (&(this->buffer[this->write_pos]), data, first_len);
  if (len > first_len) {	/* we have to wrap around */
    /* remaining part from beginning of buffer */
    memcpy (this->buffer, &data[first_len], len - first_len);
  }
  this->write_pos = (this->write_pos + len) % BUFFSIZE;

  return len;
}

static int write_buffer_16 (jack_driver_t *this, unsigned char *data, int len)
{
  int samples_free = buf_free (this) / (sizeof (float));
  int samples = len / 2;
  if (samples > samples_free)
    samples = samples_free;

  /* Rename some pointers so that the next bit of gymnastics is easier to read */
  uint32_t write_pos = this->write_pos;
  float *p_write;
  int16_t *p_read = (int16_t *) data;
  int i;
  for (i = 0; i < samples; i++) {
    /* Read in 16bits, write out floats */
    p_write = (float *) (&(this->buffer[write_pos]));
    *p_write = ((float) (p_read[i])) / 32768.0f;
    write_pos = (write_pos + sizeof (float)) % BUFFSIZE;
  }
  this->write_pos = write_pos;

  return samples * 2;
}



/**
 * \brief read data from buffer and splitting it into channels
 * \param bufs num_bufs float buffers, each will contain the data of one channel
 * \param cnt number of samples to read per channel
 * \param num_bufs number of channels to split the data into
 * \return number of samples read per channel, equals cnt unless there was too
 *         little data in the buffer
 *
 * Assumes the data in the buffer is of type float, the number of bytes
 * read is res * num_bufs * sizeof(float), where res is the return value.
 * If there is not enough data in the buffer remaining parts will be filled
 * with silence.
 */
static int read_buffer (jack_driver_t *this, float **bufs, int cnt,
			int num_bufs, float gain)
{
  int buffered = buf_used (this);
  int i, j;
  int orig_cnt = cnt;
  if (cnt * sizeof (float) * num_bufs > buffered)
    cnt = buffered / (sizeof (float) * num_bufs);

  uint32_t read_pos = this->read_pos;
  unsigned char *buffer = this->buffer;
  for (i = 0; i < cnt; i++) {
    for (j = 0; j < num_bufs; j++) {
      bufs[j][i] = *((float *) (&(buffer[read_pos]))) * gain;
      read_pos = (read_pos + sizeof (float)) % BUFFSIZE;
    }
  }
  this->read_pos = read_pos;
  for (i = cnt; i < orig_cnt; i++)
    for (j = 0; j < num_bufs; j++)
      bufs[j][i] = 0;

  return cnt;
}

/**
 * \brief fill the buffers with silence
 * \param bufs num_bufs float buffers, each will contain the data of one channel
 * \param cnt number of samples in each buffer
 * \param num_bufs number of buffers
 */
static void silence (float **bufs, int cnt, int num_bufs)
{
  int i, j;
  for (i = 0; i < cnt; i++)
    for (j = 0; j < num_bufs; j++)
      bufs[j][i] = 0;
}


/**************************************************************
 *
 * Jack interface functions
 *
**************************************************************/


/**
 * \brief stop playing and empty buffers (for seeking/pause)
 */
static void jack_reset (jack_driver_t *this)
{
  this->paused = 1;
  this->read_pos = this->write_pos = 0;
  this->paused = 0;
}

static int jack_callback (jack_nframes_t nframes, void *arg)
{
  jack_driver_t *this = (jack_driver_t *) arg;

  if (!this->client) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "jack_callback: called without a client parameter? silently trying to continue...\n");
    return 0;
  }

  float gain = 0;
  if (!this->mixer.mute) {
    gain = (float) this->mixer.volume / 100.0;
    gain *= gain;		/* experiment with increasing volume range */
  }

  float *bufs[MAX_CHANS];
  int i;
  for (i = 0; i < this->num_channels; i++)
    bufs[i] = jack_port_get_buffer (this->ports[i], nframes);

  if (this->paused || this->underrun) {
    silence (bufs, nframes, this->num_channels);
  } else {
    int frames_read = read_buffer (this, bufs, nframes, this->num_channels, gain);
    if (frames_read < nframes) {
      xprintf (this->xine, XINE_VERBOSITY_LOG,
	       "jack_callback: underrun - frames read: %d\n", frames_read);
      this->underrun = 1;
    }
  }

  return 0;
}

#if 0
static void jack_shutdown (void *arg)
{
  jack_driver_t *this = (jack_driver_t *) arg;
  this->client = NULL;
}
#endif

/*
 * Open the Jack audio device
 * Return 1 on success, 0 on failure
 * All error handling rests with the caller, we just try to open the device here
 */
static int jack_open_device (ao_driver_t *this_gen, char *jack_device,
			     int32_t *poutput_sample_rate, int num_channels)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  const char **matching_ports = NULL;
  jack_client_t *client = this->client;

  int port_flags = JackPortIsInput;
  int i;
  int num_ports;

  if (num_channels > MAX_CHANS) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "jack_open_device: Invalid number of channels: %i\n",
	     num_channels);
    goto err_out;
  }
  /* Try to create a client called "xine[-NN]" */
  if ((client = jack_client_open ("xine", JackNullOption, NULL)) == 0) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "\njack_open_device: Error: Failed to connect to JACK server\n");
    goto err_out;
  }

  /* Save the new client */
  this->client = client;

  jack_reset (this);
  jack_set_process_callback (client, jack_callback, this);

  /* list matching ports */
  if (!jack_device)
    port_flags |= JackPortIsPhysical;
  matching_ports = jack_get_ports (client, jack_device, NULL, port_flags);
  for (num_ports = 0; matching_ports && matching_ports[num_ports];
       num_ports++);
  if (!num_ports) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "jack_open_device: no physical ports available\n");
    goto err_out;
  }
  if (num_ports < num_channels) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "jack_open_device: not enough physical ports available\n");
    goto err_out;
  }

  /* create output ports */
  for (i = 0; i < num_channels; i++) {
    char pname[50];
    snprintf (pname, 50, "out_%d", i);
    this->ports[i] =
      jack_port_register (client, pname, JACK_DEFAULT_AUDIO_TYPE,
			  JackPortIsOutput, 0);
    if (!this->ports[i]) {
      xprintf (this->xine, XINE_VERBOSITY_LOG,
	       "jack_open_device: could not create output ports?  Why not?\n");
      goto err_out;
    }
  }
  if (jack_activate (client)) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "jack_open_device: jack_activate() failed\n");
    goto err_out;
  }
  for (i = 0; i < num_channels; i++) {
    if (jack_connect
	(client, jack_port_name (this->ports[i]), matching_ports[i])) {
      xprintf (this->xine, XINE_VERBOSITY_LOG,
	       "jack_open_device: jack_connect() failed\n");
      goto err_out;
    }
  }
  *poutput_sample_rate = jack_get_sample_rate (client);

  free (matching_ports);
  return 1;

err_out:
  free (matching_ports);
  if (client) {
    jack_client_close (client);
    this->client = NULL;
  }
  return 0;
}


/**************************************************************
 *
 * Xine interface functions
 *
**************************************************************/


/**
 * close the device and reset the play position
 */
static void ao_jack_close (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "ao_jack_close: closing\n");

  jack_reset (this);
  if (this->client) {
    jack_client_close (this->client);
    this->client = NULL;
  }
}

/*
 * open the audio device for writing to
 */
static int ao_jack_open (ao_driver_t *this_gen, uint32_t bits, uint32_t rate,
			 int mode)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  config_values_t *config = this->xine->config;
  char *jack_device;

  jack_device =
    config->lookup_entry (config, "audio.device.jack_device_name")->str_value;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   "ao_jack_open: ao_open rate=%d, mode=%d, bits=%d dev=%s\n", rate,
	   mode, bits, jack_device);

  if ((bits != 16) && (bits != 32)) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "ao_jack_open: bits=%u expected 16 or 32bit only\n", bits);
    return 0;
  }

  if ((mode & this->capabilities) == 0) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "ao_jack_open: unsupported mode %08x\n", mode);
    return 0;
  }

  /* If device open already then either re-use it or close it */
  if (this->client) {
    if ((mode == this->mode) && (rate == this->input_sample_rate)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "ao_jack_open: device already open, reusing it\n");
      return this->output_sample_rate;
    }

    ao_jack_close (this_gen);
  }


  this->mode = mode;
  this->input_sample_rate = rate;
  this->bits_per_sample = bits;
  this->bytes_in_buffer = 0;
  this->read_pos = this->write_pos = 0;
  this->paused = 0;
  this->underrun = 0;

  /*
   * set number of channels / a52 passthrough
   */
  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  case AO_CAP_MODE_4CHANNEL:
    this->num_channels = 4;
    break;
  case AO_CAP_MODE_4_1CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    this->num_channels = 6;
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    /* FIXME: Is this correct...? */
    this->num_channels = 2;
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     "ao_jack_open: AO_CAP_MODE_A52\n");
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "ao_jack_open: JACK Driver does not support the requested mode: 0x%X\n",
	     mode);
    return 0;
  }

  xprintf (this->xine, XINE_VERBOSITY_LOG,
	   "ao_jack_open: %d channels output\n", this->num_channels);
  this->bytes_per_frame = (this->bits_per_sample * this->num_channels) / 8;

  /*
   * open audio device
   */
  if (!jack_open_device (this_gen, jack_device, &(this->output_sample_rate),
			 this->num_channels))
    return 0;

  if (this->input_sample_rate != this->output_sample_rate) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     "ao_jack_open: audio rate : %d requested, %d provided by device\n",
	     this->input_sample_rate, this->output_sample_rate);
  }

  return this->output_sample_rate;
}


static int ao_jack_num_channels (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_jack_bytes_per_frame (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_jack_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

/*
 * Return the number of outstanding frames in all output buffers
 * need to account for ring buffer plus Jack, plus soundcard
 */
static int ao_jack_delay (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  int frames_played = jack_frames_since_cycle_start (this->client);

  int delay = 0;
  /* Ring Buffer always stores floats */
  /* TODO: Unsure if the delay should be fragment_size*2 or *3? */
  delay = buf_used (this) / (sizeof (float) * this->num_channels) +
	  this->fragment_size * 3 - frames_played;

  return delay;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  * MUST SIMULATE BLOCKING WRITES
  */
static int ao_jack_write (ao_driver_t *this_gen, int16_t *frame_buffer,
			  uint32_t num_frames)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  int written = 0;
  int num_bytes = num_frames * this->bytes_per_frame;

  /* First try and write all the bytes in one go */
  this->underrun = 0;
  /* TODO: In the future Xine should pass only floats to us, so no conversion needed */
  if (this->bits_per_sample == 16)
    written = write_buffer_16 (this, (char *) frame_buffer, num_bytes);
  else if (this->bits_per_sample == 32)
    written = write_buffer_32 (this, (char *) frame_buffer, num_bytes);

  /* If this fails then need to spin and keep trying until everything written */
  int spin_count = 0;
  while ((written < num_bytes) && (spin_count < 40)) {
    num_bytes -= written;
    frame_buffer += written / 2;

    /* Sleep to save CPU */
    int until_callback =
      this->fragment_size - jack_frames_since_cycle_start (this->client);
    if ((until_callback < 0) || (until_callback > this->fragment_size)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "ao_jack_write: Invalid until_callback %d\n", until_callback);
      until_callback = this->fragment_size;
    }
    xine_usec_sleep (((until_callback +
		       100) * 1000.0 * 1000.0) / this->output_sample_rate);

    if (this->bits_per_sample == 16)
      written = write_buffer_16 (this, (char *) frame_buffer, num_bytes);
    else if (this->bits_per_sample == 32)
      written = write_buffer_32 (this, (char *) frame_buffer, num_bytes);

    if (written == 0)
      spin_count++;
    else
      spin_count = 0;

    if (written == 0)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       "ao_jack_write: unusual, couldn't write anything\n");
  };

  if (spin_count)
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     "Nonzero spin_count...%d\n", spin_count);

  return spin_count ? 0 : 1;	/* return 1 on success, 0 if we got stuck for some reason */
}

static uint32_t ao_jack_get_capabilities (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_jack_exit (ao_driver_t *this_gen)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;

  ao_jack_close (this_gen);
  if (this->buffer)
    free (this->buffer);
  free (this);
}

static int ao_jack_get_property (ao_driver_t *this_gen, int property)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;

  switch (property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    return this->mixer.volume;
    break;
  case AO_PROP_MUTE_VOL:
    return this->mixer.mute;
    break;
  }

  return 0;
}

static int ao_jack_set_property (ao_driver_t *this_gen, int property, int value)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;

  switch (property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    this->mixer.volume = value;
    return value;
    break;
  case AO_PROP_MUTE_VOL:
    this->mixer.mute = value;
    return value;
    break;
  }

  return -1;
}

static int ao_jack_ctrl (ao_driver_t *this_gen, int cmd, ...)
{
  jack_driver_t *this = (jack_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    this->paused = 1;
    break;

  case AO_CTRL_PLAY_RESUME:
    this->paused = 0;
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    jack_reset (this);
    break;
  }

  return 0;
}

static void jack_speaker_arrangement_cb (void *user_data,
					 xine_cfg_entry_t *entry);

static ao_driver_t *open_jack_plugin (audio_driver_class_t *class_gen,
				      const void *data)
{
  jack_class_t *class = (jack_class_t *) class_gen;
  config_values_t *config = class->config;
  jack_driver_t *this;

  jack_client_t *client;
  uint32_t rate;
  char *jack_device;
  const char **matching_ports = NULL;

  AUDIO_DEVICE_SPEAKER_ARRANGEMENT_TYPES;
  int speakers;

  /* Try to create a client called "xine[-NN]" */
  if ((client = jack_client_open ("xine", JackNullOption, NULL)) == 0) {
    xprintf (class->xine, XINE_VERBOSITY_LOG,
	     "\nopen_jack_plugin: Error: Failed to connect to JACK server\n");
    return 0;
  }

  this = calloc(1, sizeof (jack_driver_t));

  rate = jack_get_sample_rate (client);
  xprintf (class->xine, XINE_VERBOSITY_DEBUG,
	   "open_jack_plugin: JACK sample rate is %u\n", rate);

  /* devname_val is offset used to select auto, /dev/dsp, or /dev/sound/dsp */
  jack_device = config->register_string (config,
					 "audio.device.jack_device_name",
					 "",
					 _("JACK audio device name"),
					 _("Specifies the jack audio device name, "
					   "leave blank for the default physical output port."),
					 10, NULL, NULL);

  this->capabilities = 0;


  /* for usability reasons, keep this in sync with audio_alsa_out.c */
  speakers =
    config->register_enum (config, "audio.output.speaker_arrangement",
			   STEREO, speaker_arrangement,
                           AUDIO_DEVICE_SPEAKER_ARRANGEMENT_HELP,
			   0, jack_speaker_arrangement_cb, this);

  int port_flags = JackPortIsInput;
  int num_ports;
  /* list matching ports */
  if (!jack_device)
    port_flags |= JackPortIsPhysical;
  /* Find all the ports matching either the desired device regexp or physical output ports */
  matching_ports = jack_get_ports (client, jack_device, NULL, port_flags);
  /* Count 'em */
  for (num_ports = 0; matching_ports && matching_ports[num_ports];
       num_ports++)
    /**/;
  if (!num_ports) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "open_jack_plugin: no physical ports available\n");
    goto err_out;
  }


/* TODO: We deliberately don't offer mono, let Xine upsample instead? */
/*  if (num_ports >= 1) { */
/*    this->capabilities |= AO_CAP_MODE_MONO; */
/*    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "mono "); */
/*  } */

  if (num_ports >= 2) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    xprintf (class->xine, XINE_VERBOSITY_DEBUG, "stereo ");
  }

  if (num_ports >= 4) {
    if (speakers == SURROUND4) {
      this->capabilities |= AO_CAP_MODE_4CHANNEL;
      xprintf (class->xine, XINE_VERBOSITY_DEBUG, "4-channel ");
    } else
      xprintf (class->xine, XINE_VERBOSITY_DEBUG,
	       "(4-channel not enabled in xine config) ");
  }

  if (num_ports >= 5) {
    if (speakers == SURROUND5) {
      this->capabilities |= AO_CAP_MODE_5CHANNEL;
      xprintf (class->xine, XINE_VERBOSITY_DEBUG, "5-channel ");
    } else
      xprintf (class->xine, XINE_VERBOSITY_DEBUG,
	       "(5-channel not enabled in xine config) ");
  }

  if (num_ports >= 6) {
    if (speakers == SURROUND51) {
      this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
      xprintf (class->xine, XINE_VERBOSITY_DEBUG, "5.1-channel ");
    } else
      xprintf (class->xine, XINE_VERBOSITY_DEBUG,
	       "(5.1-channel not enabled in xine config) ");
  }

  this->buffer = (unsigned char *) malloc (BUFFSIZE);
  jack_reset (this);

  this->capabilities |= AO_CAP_MIXER_VOL;
  this->capabilities |= AO_CAP_MUTE_VOL;
  /* TODO: Currently not respected by Xine, perhaps v1.2? */
  this->capabilities |= AO_CAP_FLOAT32;


  this->mixer.mute = 0;
  this->mixer.volume = 100;

  this->output_sample_rate = jack_get_sample_rate (client);
  this->fragment_size = jack_get_buffer_size (client);

  /* Close our JACK client */
  jack_client_close (client);

  this->xine = class->xine;

  this->ao_driver.get_capabilities	= ao_jack_get_capabilities;
  this->ao_driver.get_property		= ao_jack_get_property;
  this->ao_driver.set_property		= ao_jack_set_property;
  this->ao_driver.open			= ao_jack_open;
  this->ao_driver.num_channels		= ao_jack_num_channels;
  this->ao_driver.bytes_per_frame	= ao_jack_bytes_per_frame;
  this->ao_driver.delay			= ao_jack_delay;
  this->ao_driver.write			= ao_jack_write;
  this->ao_driver.close			= ao_jack_close;
  this->ao_driver.exit			= ao_jack_exit;
  this->ao_driver.get_gap_tolerance	= ao_jack_get_gap_tolerance;
  this->ao_driver.control		= ao_jack_ctrl;

  return &this->ao_driver;

err_out:
  free (matching_ports);
  if (client)
    jack_client_close (client);
  return 0;
}

static void jack_speaker_arrangement_cb (void *user_data,
					 xine_cfg_entry_t *entry)
{
  jack_driver_t *this = (jack_driver_t *) user_data;
  int32_t value = entry->num_value;
  if (value == SURROUND4) {
    this->capabilities |= AO_CAP_MODE_4CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_4CHANNEL;
  }
  if (value == SURROUND41) {
    this->capabilities |= AO_CAP_MODE_4_1CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_4_1CHANNEL;
  }
  if (value == SURROUND5) {
    this->capabilities |= AO_CAP_MODE_5CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_5CHANNEL;
  }
  if (value >= SURROUND51) {
    this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
  } else {
    this->capabilities &= ~AO_CAP_MODE_5_1CHANNEL;
  }
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *data) {

    jack_class_t        *this;

    this = calloc(1, sizeof (jack_class_t));

    this->driver_class.open_plugin     = open_jack_plugin;
    this->driver_class.identifier      = "jack";
    this->driver_class.description     = N_("xine output plugin for JACK Audio Connection Kit");
    this->driver_class.dispose         = default_audio_driver_class_dispose;

    this->config = xine->config;
    this->xine   = xine;

    fprintf(stderr, "jack init_class returning %p\n", (void *)this);

    return this;
}

static ao_info_t ao_info_jack = { 6 };

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
    /* type, API, "name", version, special_info, init_function */
    { PLUGIN_AUDIO_OUT, AO_OUT_JACK_IFACE_VERSION, "jack", XINE_VERSION_CODE /* XINE_VERSION_CODE */, &ao_info_jack, init_class },
    { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
