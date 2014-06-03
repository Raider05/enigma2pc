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
 *
 *
 * Input plugin for Digital TV (Digital Video Broadcast - DVB) devices,
 * e.g. Hauppauge WinTV Nova supported by DVB drivers from Convergence.
 *
 *
 * MODIFICATION HISTORY
 *
 * Date        Author
 * ----        ------
 *
 * 01-Feb-2005 Pekka Jääskeläinen <poj@iki.fi>
 *
 *             - This history log started.
 *             - Disabled the automatic EPG updater thread until EPG demuxer
 *               is done (it caused pausing of video stream), now EPG is
 *               updated only on demand when the EPG OSD is displayed and
 *               no data is in cache.
 *             - Tried to stabilize the EPG updater thread.
 *             - Fixed a tuning problem I had with Linux 2.6.11-rc2.
 *             - Now tuning to an erroneus channel shouldn't hang but stop
 *               the playback and output a log describing the error.
 *             - Style cleanups here and there.
 *
 * 06-Apr-2006 Jack Steven Kelliher
 *	       - Add ATSC support
 *
 * TODO/Wishlist: (not in any order)
 * - Parse all Administrative PIDs - NIT,SDT,CAT etc
 * - As per James' suggestion, we need a way for the demuxer
 *   to request PIDs from the input plugin.
 * - Timeshift ability.
 * - Pipe teletext infomation to a named fifo so programs such as
 *   Alevtd can read it.
 * - Allow the user to view one set of PIDs (channel) while
 *   recording another on the same transponder - this will require either remuxing or
 *   perhaps bypassing the TS demuxer completely - we could easily have access to the
 *   individual audio/video streams via seperate read calls, so send them to the decoders
 *   and save the TS output to disk instead of passing it to the demuxer.
 *   This also gives us full control over the streams being played..hmm..control...
 * - Parse and use full EIT for programming info.
 * - Allow the user to find and tune new stations from within xine, and
 *   do away with the need for dvbscan & channels.conf file.
 * - Enable use of Conditional Access devices for scrambled content.
 * - if multiple cards are available, optionally use these to record/gather si info,
 *   and leave primary card for viewing.
 * - allow for handing off of EPG data to specialised frontends, instead of displaying via
 *   OSD - this will allow for filtering/searching of epg data - useful for automatic recording :)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* pthread.h must be included first so rest of the headers are imported
   thread safely (on some systems).
   However, including it before config.h causes problems with asprintf not
   being declared (glibc 2.3.6)
*/
#include <pthread.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <poll.h>
#ifdef __sun
#include <sys/ioccom.h>
#endif
#include <sys/poll.h>
#include <time.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <ctype.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <crc.h>
#else
#  include <libavutil/crc.h>
#endif

/* XDG */
#include <basedir.h>

#ifdef HAVE_DEV_DTV_DTVIO_H
#include <dev/dtv/dtvio.h>
#else
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#endif

#define LOG_MODULE "input_dvb"
#define LOG_VERBOSE
/*
#define LOG
#define LOG_READS
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"

#define BUFSIZE 16384

#define DVB_NOPID 0xffff

/* define stream types
 * administrative/system PIDs first */
#define INTERNAL_FILTER 0
#define PATFILTER 1
#define PMTFILTER 2
#define EITFILTER 3
#define PCRFILTER 4
#define VIDFILTER 5
#define AUDFILTER 6
#define AC3FILTER 7
#define TXTFILTER 8

#define MAX_FILTERS 9

#define MAX_AUTOCHANNELS 200

#define MAX_SUBTITLES 4

/* define for alternate non-buffered mode */
/*
#define DVB_NO_BUFFERING
*/
/* Mouse button codes. */
#define MOUSE_BUTTON_LEFT   1
#define MOUSE_BUTTON_MIDDLE 2
#define MOUSE_BUTTON_RIGHT  3
#define MOUSE_WHEEL_UP      4
#define MOUSE_WHEEL_DOWN    5

/* The "thumb button" of my Intellimouse. */
#define MOUSE_SIDE_LEFT     6
#define MOUSE_SIDE_RIGHT    7

/* EPG settings. */

/* define to have EPG come up on left mouse click */
/*
#define LEFT_MOUSE_DOES_EPG
*/

/* define to make EPG data updated in background in a separate thread */
/*#define EPG_UPDATE_IN_BACKGROUND */

/* Delay between EPG data updates in the EPG updater thread in seconds. */
#define EPG_UPDATE_DELAY 60

/* Width of the EPG OSD area. */
#define EPG_WIDTH 520

/* Height of the EPG OSD area. */
#define EPG_HEIGHT 620

/* Minimum top margin of the EPG in the video window. */
#define EPG_TOP 50

/* Font size of the channel name text. */
#define EPG_CHANNEL_FONT_SIZE 32

/* Font size of the clock. */
#define EPG_CLOCK_FONT_SIZE 18

/* Font size of the header text (duration and program name). */
#define EPG_TITLE_FONT_SIZE 24

/* Font size of the content type and rating text. */
#define EPG_CONTENT_FONT_SIZE 18

/* Font size of the program description text. */
#define EPG_DESCRIPTION_FONT_SIZE 18

#define EPG_PIXELS_BETWEEN_TEXT_ROWS 2
#define EPG_PIXELS_BETWEEN_PROGRAM_ENTRIES 2

/* How many pixels the background of the OSD is bigger than the text area?
   The margin is for each side of the background box. */
#define EPG_BACKGROUND_MARGIN 5

#define MAX_EPG_PROGRAM_NAME_LENGTH 255
#define MAX_EPG_PROGRAM_DESCRIPTION_LENGTH 255
#define MAX_EPG_CONTENT_TYPE_LENGTH 20
#define MAX_EPG_ENTRIES_PER_CHANNEL 10

/* How many seconds an EPG entry with the running flag on can be "late"
   according to the system time before discarding it as an old program?

   This margin is needed because in channel list OSD some EPG entries of
   some channels may be updated a very long ago (if user has watched another
   channel in different mux) so we have to resort to system clock for
   figuring out the current program. */
#define MAX_EPG_ENTRY_LATENESS 5*60.0

/*
#define DEBUG_EPG
*/

/* Channel selector OSD settings. */
#define CHSEL_WIDTH 600
#define CHSEL_HEIGHT 400
#define CHSEL_CHANNEL_FONT_SIZE 26
#define CHSEL_PROGRAM_NAME_FONT_SIZE 12

#define bcdtoint(i) ((((i & 0xf0) >> 4) * 10) + (i & 0x0f))

typedef struct {
  int                            fd_frontend;
  int                            fd_pidfilter[MAX_FILTERS];
  int                            fd_subfilter[MAX_SUBTITLES];

  struct dvb_frontend_info       feinfo;

  int				 adapter_num;

  char				*dvr_device;
  char				*demux_device;

  struct dmx_pes_filter_params   pesFilterParams[MAX_FILTERS];
  struct dmx_pes_filter_params   subFilterParams[MAX_SUBTITLES];
  struct dmx_sct_filter_params	 sectFilterParams[MAX_FILTERS];
  xine_t                        *xine;
} tuner_t;


/* EPG entry. */
typedef struct {

  /* Program's name. */
  char		     *progname;

  /* Textual description of the program. */
  char		     *description;

  /* The content type string. */
  char		     *content;

  /* Age recommendation. 0 if not available. */
  int		      rating;

  time_t             starttime;

  /* Program's duration in hours + minutes. */
  char		     duration_hours;
  char               duration_minutes;

  /* Is this program running currently according to EPG data? */
  char               running;

} epg_entry_t;

typedef struct {
  char                            *name;
  struct dvb_frontend_parameters   front_param;
  int                              pid[MAX_FILTERS];
  int				   subpid[MAX_SUBTITLES];
  int				   service_id;
  int                              sat_no;
  int                              tone;
  int                              pol;
  int				   pmtpid;
  int                              epg_count;
  epg_entry_t			   *epg[MAX_EPG_ENTRIES_PER_CHANNEL];
} channel_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;

  const char       *mrls[6];

  int		    numchannels;

  char		   *autoplaylist[MAX_AUTOCHANNELS];

  const AVCRC      *av_crc;
} dvb_input_class_t;

typedef struct {
  input_plugin_t      input_plugin;

  dvb_input_class_t  *class;

  xine_stream_t      *stream;

  char               *mrl;

  off_t               curpos;

  nbc_t              *nbc;

  tuner_t            *tuner;
  channel_t          *channels;
  int                 fd;

/* Is channel tuned in correctly, i.e., can we read program stream? */
  int                 tuned_in;
  int                 num_channels;
  int                 channel;
  pthread_mutex_t     channel_change_mutex;

  osd_object_t       *osd;
  osd_object_t       *rec_osd;
  osd_object_t	     *name_osd;
  osd_object_t	     *paused_osd;
  osd_object_t	     *proginfo_osd;
  osd_object_t	     *channel_osd;
  osd_object_t	     *background;

  xine_event_queue_t *event_queue;

  /* scratch buffer for forward seeking */
  char                seek_buf[BUFSIZE];

  /* Is the GUI enabled at all? */
  int                 dvb_gui_enabled;
  /* simple vcr-like functionality */
  int                 record_fd;
  int		      record_paused;
  /* centre cutout zoom */
  int		      zoom_ok;
  /* Is EPG displaying? */
  int                 epg_displaying;

  /* This is set to non-zero if the updater thread is wanted to stop. */
  int                 epg_updater_stop;
  pthread_t           epg_updater_thread;

  /* buffer for EIT data */
    /*char		     *eitbuffer;*/
  int		      num_streams_in_this_ts;
  /* number of timedout reads in plugin_read */
  int		      read_failcount;
#ifdef DVB_NO_BUFFERING
  int newchannel;
#endif
} dvb_input_plugin_t;

typedef struct {
	const char *name;
	int value;
} Param;

static const Param inversion_list [] = {
	{ "INVERSION_OFF", INVERSION_OFF },
	{ "INVERSION_ON", INVERSION_ON },
	{ "INVERSION_AUTO", INVERSION_AUTO },
        { NULL, 0 }
};

static const Param bw_list [] = {
	{ "BANDWIDTH_6_MHZ", BANDWIDTH_6_MHZ },
	{ "BANDWIDTH_7_MHZ", BANDWIDTH_7_MHZ },
	{ "BANDWIDTH_8_MHZ", BANDWIDTH_8_MHZ },
	{ "BANDWIDTH_AUTO", BANDWIDTH_AUTO },
        { NULL, 0 }
};

static const Param fec_list [] = {
	{ "FEC_1_2", FEC_1_2 },
	{ "FEC_2_3", FEC_2_3 },
	{ "FEC_3_4", FEC_3_4 },
	{ "FEC_4_5", FEC_4_5 },
	{ "FEC_5_6", FEC_5_6 },
	{ "FEC_6_7", FEC_6_7 },
	{ "FEC_7_8", FEC_7_8 },
	{ "FEC_8_9", FEC_8_9 },
	{ "FEC_AUTO", FEC_AUTO },
	{ "FEC_NONE", FEC_NONE },
        { NULL, 0 }
};

static const Param guard_list [] = {
	{"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16},
	{"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32},
	{"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4},
	{"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8},
	{"GUARD_INTERVAL_AUTO", GUARD_INTERVAL_AUTO},
        { NULL, 0 }
};

static const Param hierarchy_list [] = {
	{ "HIERARCHY_1", HIERARCHY_1 },
	{ "HIERARCHY_2", HIERARCHY_2 },
	{ "HIERARCHY_4", HIERARCHY_4 },
	{ "HIERARCHY_NONE", HIERARCHY_NONE },
	{ "HIERARCHY_AUTO", HIERARCHY_AUTO },
        { NULL, 0 }
};

static const Param atsc_list [] = {
	{ "8VSB", VSB_8 },
	{ "QAM_256", QAM_256 },
	{ "QAM_64", QAM_64 },
	{ "QAM", QAM_AUTO },
        { NULL, 0 }
};

static const Param qam_list [] = {
	{ "QPSK", QPSK },
	{ "QAM_128", QAM_128 },
	{ "QAM_16", QAM_16 },
	{ "QAM_256", QAM_256 },
	{ "QAM_32", QAM_32 },
	{ "QAM_64", QAM_64 },
	{ "QAM_AUTO", QAM_AUTO },
        { NULL, 0 }
};

static const Param transmissionmode_list [] = {
	{ "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
	{ "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
	{ "TRANSMISSION_MODE_AUTO", TRANSMISSION_MODE_AUTO },
        { NULL, 0 }
};


static time_t dvb_mjdtime (uint8_t *buf);
static void load_epg_data(dvb_input_plugin_t *this);
static void show_eit(dvb_input_plugin_t *this);

/* Utility Functions */

static void print_error(const char* estring) {
    printf("input_dvb: ERROR: %s\n", estring);
}

#ifdef DEBUG_EPG
static void print_info(const char* estring) {
    printf("input_dvb: %s\n", estring);
}
#endif


static unsigned int getbits(unsigned char *buffer, unsigned int bitpos, unsigned int bitcount)
{
    unsigned int i;
    unsigned int val = 0;

    for (i = bitpos; i < bitcount + bitpos; i++) {
      val = val << 1;
      val = val + ((buffer[i >> 3] & (0x80 >> (i & 7))) ? 1 : 0);
    }
    return val;
}


static int find_descriptor(uint8_t tag, const unsigned char *buf, int descriptors_loop_len,
                                                const unsigned char **desc, int *desc_len)
{

  while (descriptors_loop_len > 0) {
    unsigned char descriptor_tag = buf[0];
    unsigned char descriptor_len = buf[1] + 2;

    if (!descriptor_len) {
      break;
    }

    if (tag == descriptor_tag) {
      if (desc)
        *desc = buf;

      if (desc_len)
        *desc_len = descriptor_len;
	 return 1;
    }

    buf += descriptor_len;
    descriptors_loop_len -= descriptor_len;
  }
  return 0;
}

/* Extract UTC time and date encoded in modified julian date format and return it as a time_t.
 */
static time_t dvb_mjdtime (uint8_t *buf)
{
  int i;
  unsigned int year, month, day, hour, min, sec;
  unsigned long int mjd;
  struct tm *tma = calloc(1, sizeof(struct tm));
  time_t t;

  _x_assert(tma != NULL);

  mjd =	(unsigned int)(buf[0] & 0xff) << 8;
  mjd +=(unsigned int)(buf[1] & 0xff);
  hour =(unsigned char)bcdtoint(buf[2] & 0xff);
  min = (unsigned char)bcdtoint(buf[3] & 0xff);
  sec = (unsigned char)bcdtoint(buf[4] & 0xff);
  year =(unsigned long)((mjd - 15078.2)/365.25);
  month=(unsigned long)((mjd - 14956.1 - (unsigned long)(year * 365.25))/30.6001);
  day = mjd - 14956 - (unsigned long)(year * 365.25) - (unsigned long)(month * 30.6001);

  if (month == 14 || month == 15)
    i = 1;
  else
    i = 0;
  year += i;
  month = month - 1 - i * 12;

  tma->tm_sec=sec;
  tma->tm_min=min;
  tma->tm_hour=hour;
  tma->tm_mday=day;
  tma->tm_mon=month-1;
  tma->tm_year=year;


  t = timegm(tma);

  free(tma);
  return t;
}


static void tuner_dispose(tuner_t * this)
{
    int x;

    if (this->fd_frontend >= 0)
      close(this->fd_frontend);

    /* close all pid filter filedescriptors */
    for (x = 0; x < MAX_FILTERS; x++)
      if (this->fd_pidfilter[x] >= 0)
        close(this->fd_pidfilter[x]);

    /* close all pid filter filedescriptors */
    for (x = 0; x < MAX_SUBTITLES; x++)
      if (this->fd_subfilter[x] >= 0)
        close(this->fd_subfilter[x]);

    free(this->dvr_device);
    free(this->demux_device);
    free(this);
}


static tuner_t *XINE_MALLOC tuner_init(xine_t * xine, int adapter)
{

    tuner_t *this;
    int x;
    int test_video;
    char *video_device = NULL;
    char *frontend_device = NULL;

    this = (tuner_t *) xine_xmalloc(sizeof(tuner_t));

    _x_assert(this != NULL);

    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "tuner_init adapter=%d\n", adapter);
    this->fd_frontend = -1;
    memset(this->fd_pidfilter, 0, sizeof(this->fd_pidfilter));

    this->xine = xine;
    this->adapter_num = adapter;

    this->demux_device = _x_asprintf("/dev/dvb/adapter%i/demux0",this->adapter_num);
    this->dvr_device = _x_asprintf("/dev/dvb/adapter%i/dvr0",this->adapter_num);
    video_device = _x_asprintf("/dev/dvb/adapter%i/video0",this->adapter_num);

    frontend_device = _x_asprintf("/dev/dvb/adapter%i/frontend0",this->adapter_num);
    if ((this->fd_frontend = xine_open_cloexec(frontend_device, O_RDWR)) < 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "FRONTEND DEVICE: %s\n", strerror(errno));
      tuner_dispose(this);
      this = NULL;
      goto exit;
    }
    free(frontend_device); frontend_device = NULL;

    if ((ioctl(this->fd_frontend, FE_GET_INFO, &this->feinfo)) < 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "FE_GET_INFO: %s\n", strerror(errno));
      tuner_dispose(this);
      this = NULL;
      goto exit;
    }

    for (x = 0; x < MAX_FILTERS; x++) {
      this->fd_pidfilter[x] = xine_open_cloexec(this->demux_device, O_RDWR);
      if (this->fd_pidfilter[x] < 0) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG, "DEMUX DEVICE PIDfilter: %s\n", strerror(errno));
        tuner_dispose(this);
	this = NULL;
	goto exit;
      }
   }
    for (x = 0; x < MAX_SUBTITLES; x++) {
      this->fd_subfilter[x] = xine_open_cloexec(this->demux_device, O_RDWR);
      if (this->fd_subfilter[x] < 0) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG, "DEMUX DEVICE Subtitle filter: %s\n", strerror(errno));
      }
   }

   /* open EIT with NONBLOCK */
   if(fcntl(this->fd_pidfilter[EITFILTER], F_SETFL, O_NONBLOCK)<0)
     xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: couldn't set EIT to nonblock: %s\n",strerror(errno));
    /* and the internal filter used for PAT & PMT */
   if(fcntl(this->fd_pidfilter[INTERNAL_FILTER], F_SETFL, O_NONBLOCK)<0)
     xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: couldn't set INTERNAL to nonblock: %s\n",strerror(errno));
    /* and the frontend */
    fcntl(this->fd_frontend, F_SETFL, O_NONBLOCK);

   xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Frontend is <%s> ",this->feinfo.name);
   if(this->feinfo.type==FE_QPSK) xprintf(this->xine,XINE_VERBOSITY_DEBUG,"SAT Card\n");
   if(this->feinfo.type==FE_QAM) xprintf(this->xine,XINE_VERBOSITY_DEBUG,"CAB Card\n");
   if(this->feinfo.type==FE_OFDM) xprintf(this->xine,XINE_VERBOSITY_DEBUG,"TER Card\n");
   if(this->feinfo.type==FE_ATSC) xprintf(this->xine,XINE_VERBOSITY_DEBUG,"US Card\n");

   if ((test_video=xine_open_cloexec(video_device, O_RDWR)) < 0) {
       xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Card has no hardware decoder\n");
   }else{
       xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Card HAS HARDWARE DECODER\n");
       close(test_video);
  }

 exit:
  free(video_device);
  free(frontend_device);

  return this;
}


static int dvb_set_pidfilter(dvb_input_plugin_t * this, int filter, ushort pid, int pidtype, int taptype)
{
    tuner_t *tuner = this->tuner;

   if(this->channels[this->channel].pid [filter] !=DVB_NOPID) {
      ioctl(tuner->fd_pidfilter[filter], DMX_STOP);
    }

    this->channels[this->channel].pid [filter] = pid;
    tuner->pesFilterParams[filter].pid = pid;
    tuner->pesFilterParams[filter].input = DMX_IN_FRONTEND;
    tuner->pesFilterParams[filter].output = taptype;
    tuner->pesFilterParams[filter].pes_type = pidtype;
    tuner->pesFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_PES_FILTER, &tuner->pesFilterParams[filter]) < 0)
    {
	   xprintf(tuner->xine, XINE_VERBOSITY_DEBUG, "input_dvb: set_pid: %s\n", strerror(errno));
	   return 0;
    }
    return 1;
}


static int dvb_set_sectfilter(dvb_input_plugin_t * this, int filter, ushort pid, int pidtype, char table, char mask)
{
    tuner_t *tuner = this->tuner;

    if(this->channels[this->channel].pid [filter] !=DVB_NOPID) {
      ioctl(tuner->fd_pidfilter[filter], DMX_STOP);
    }

    this->channels[this->channel].pid [filter] = pid;
    tuner->sectFilterParams[filter].pid = pid;
    memset(&tuner->sectFilterParams[filter].filter.filter,0,DMX_FILTER_SIZE);
    memset(&tuner->sectFilterParams[filter].filter.mask,0,DMX_FILTER_SIZE);
    tuner->sectFilterParams[filter].timeout = 0;
    tuner->sectFilterParams[filter].filter.filter[0] = table;
    tuner->sectFilterParams[filter].filter.mask[0] = mask;
    tuner->sectFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_FILTER, &tuner->sectFilterParams[filter]) < 0){
	   xprintf(tuner->xine, XINE_VERBOSITY_DEBUG, "input_dvb: set_sectionfilter: %s\n", strerror(errno));
	   return 0;
    }
    return 1;
}


static int find_param(const Param *list, const char *name)
{
  while (list->name && strcmp(list->name, name))
    list++;
  return list->value;;
}

static int extract_channel_from_string_internal(channel_t * channel,char * str,fe_type_t fe_type)
{
	/*
		try to extract channel data from a string in the following format
		(DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>:<vpid>:<apid>
		(DVBC) QAM: <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>:<vpid>:<apid>
		(DVBT) OFDM: <channel name>:<frequency>:<inversion>:
						<bw>:<fec_hp>:<fec_lp>:<qam>:
						<transmissionm>:<guardlist>:<hierarchinfo>:<vpid>:<apid>
		(DVBA) ATSC: <channel name>:<frequency>:<qam>:<vpid>:<apid>

		<channel name> = any string not containing ':'
		<frequency>    = unsigned long
		<polarisation> = 'v' or 'h'
		<sat_no>       = unsigned long, usually 0 :D
		<sym_rate>     = symbol rate in MSyms/sec


		<inversion>    = INVERSION_ON | INVERSION_OFF | INVERSION_AUTO
		<fec>          = FEC_1_2, FEC_2_3, FEC_3_4 .... FEC_AUTO ... FEC_NONE
		<qam>          = QPSK, QAM_128, QAM_16, ATSC ...

		<bw>           = BANDWIDTH_6_MHZ, BANDWIDTH_7_MHZ, BANDWIDTH_8_MHZ
		<fec_hp>       = <fec>
		<fec_lp>       = <fec>
		<transmissionm> = TRANSMISSION_MODE_2K, TRANSMISSION_MODE_8K
		<vpid>         = video program id
		<apid>         = audio program id

	*/
	unsigned long freq;
	char *field, *tmp;

	tmp = str;

	/* find the channel name */
	if(!(field = strsep(&tmp,":")))return -1;
	channel->name = strdup(field);

	/* find the frequency */
	if(!(field = strsep(&tmp, ":")))return -1;
	freq = strtoul(field,NULL,0);

	switch(fe_type)
	{
		case FE_QPSK:
			if(freq > 11700)
			{
				channel->front_param.frequency = (freq - 10600)*1000;
				channel->tone = 1;
			} else {
				channel->front_param.frequency = (freq - 9750)*1000;
				channel->tone = 0;
			}
			channel->front_param.inversion = INVERSION_AUTO;

			/* find out the polarisation */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->pol = (field[0] == 'h' ? 0 : 1);

			/* satellite number */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->sat_no = strtoul(field, NULL, 0);

			/* symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qpsk.symbol_rate = strtoul(field, NULL, 0) * 1000;

			channel->front_param.u.qpsk.fec_inner = FEC_AUTO;
		break;
		case FE_QAM:
			channel->front_param.frequency = freq;

			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.symbol_rate = strtoul(field, NULL, 0);

			/* find out the fec */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.fec_inner = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.modulation = find_param(qam_list, field);
		break;
		case FE_OFDM:
		        /* DVB-T frequency is in kHz - workaround broken channels.confs */
		        if (freq < 1000000)
		          freq*=1000;

		        channel->front_param.frequency = freq;

			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the bandwidth */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.bandwidth = find_param(bw_list, field);

			/* find out the fec_hp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_HP = find_param(fec_list, field);

			/* find out the fec_lp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_LP = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.constellation = find_param(qam_list, field);

			/* find out the transmission mode */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.transmission_mode = find_param(transmissionmode_list, field);

			/* guard list */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.guard_interval = find_param(guard_list, field);

			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.hierarchy_information = find_param(hierarchy_list, field);
		break;
		case FE_ATSC:
			channel->front_param.frequency = freq;

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.vsb.modulation = find_param(atsc_list, field);
		break;

	}

   /* Video PID - not used but we'll take it anyway */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[VIDFILTER] = strtoul(field, NULL, 0);

    /* Audio PID - it's only for mpegaudio so we don't use it anymore */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[AUDFILTER] = strtoul(field, NULL, 0);

    /* service ID */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->service_id = strtoul(field, NULL, 0);

    /* some channel.conf files are generated with the service ID 1 to the right
       this needs investigation */
    if ((field = strsep(&tmp, ":")))
      if(strtoul(field,NULL,0)>0)
        channel->service_id = strtoul(field, NULL, 0);

	return 0;
}

static int extract_channel_from_string(channel_t *channel, char *str, fe_type_t fe_type)
{
  channel->name = NULL;
  if (!extract_channel_from_string_internal(channel, str, fe_type))
    return 0;
  free (channel->name); /* without this, we have a possible memleak */
  return -1;
}

static channel_t *load_channels(xine_t *xine, xine_stream_t *stream, int *num_ch, fe_type_t fe_type) {

  FILE      *f;
  char       str[BUFSIZE];
  char       filename[BUFSIZE];
  channel_t *channels = NULL;
  int        num_channels = 0;
  int        num_alloc = 0;
  struct stat st;

  snprintf(filename, BUFSIZE, "%s/"PACKAGE"/channels.conf", xdgConfigHome(&xine->basedir_handle));

  f = fopen(filename, "r");
  if (!f) {
    xprintf(xine, XINE_VERBOSITY_LOG, _("input_dvb: failed to open dvb channel file '%s': %s\n"), filename, strerror (errno));
    if (!f && stream)
      _x_message(stream, XINE_MSG_FILE_NOT_FOUND, filename, "Please run the dvbscan utility.", NULL);
    return NULL;
  }
  if (fstat(fileno(f), &st) || !S_ISREG (st.st_mode)) {
    xprintf(xine, XINE_VERBOSITY_LOG, _("input_dvb: dvb channel file '%s' is not a plain file\n"), filename);
    fclose(f);
    return NULL;
  }

  /*
   * load channel list
   */

  while ( fgets (str, BUFSIZE, f)) {
    channel_t channel = {0};

    /* lose trailing spaces & control characters */
    size_t i = strlen (str);
    while (i && str[i - 1] <= ' ')
      --i;
    if (i == 0)
        continue;
    str[i] = 0;

    if (extract_channel_from_string(&channel,str,fe_type) < 0)
	continue;

    if (num_channels >= num_alloc) {
      channel_t *new_channels = calloc((num_alloc += 32), sizeof (channel_t));
      _x_assert(new_channels != NULL);
      memcpy(new_channels, channels, num_channels * sizeof (channel_t));
      free(channels);
      channels = new_channels;
    }

    channels[num_channels] = channel;

    /* Initially there's no EPG data in the EPG structs. */
    channels[num_channels].epg_count = 0;
    memset(channels[num_channels].epg, 0, sizeof(channels[num_channels].epg));

    num_channels++;
  }
  fclose(f);

  /* free any trailing unused entries */
  channels = realloc (channels, num_channels * sizeof (channel_t));

  if(num_channels > 0)
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: found %d channels...\n", num_channels);
  else {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: no channels found in the file: giving up.\n");
    free(channels);
    return NULL;
  }

  *num_ch = num_channels;
  return channels;
}

static void free_channel_list (channel_t *channels, int num_channels)
{
  if (channels)
    while (--num_channels >= 0)
      free(channels[num_channels].name);
  free(channels);
}

static int tuner_set_diseqc(tuner_t *this, channel_t *c)
{
   struct dvb_diseqc_master_cmd cmd =
      {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

   cmd.msg[3] = 0xf0 | ((c->sat_no * 4) & 0x0f) |
      (c->tone ? 1 : 0) | (c->pol ? 0 : 2);

   if (ioctl(this->fd_frontend, FE_SET_TONE, SEC_TONE_OFF) < 0)
      return 0;
   if (ioctl(this->fd_frontend, FE_SET_VOLTAGE,
	     c->pol ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_BURST,
	     (c->sat_no / 4) % 2 ? SEC_MINI_B : SEC_MINI_A) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_SET_TONE,
	     c->tone ? SEC_TONE_ON : SEC_TONE_OFF) < 0)
      return 0;

   return 1;
}


/* Tune to the requested freq. etc, wait for frontend to lock for a few seconds.
 * if frontend can't lock, retire. */
static int tuner_tune_it (tuner_t *this, struct dvb_frontend_parameters
			  *front_param) {
  fe_status_t status = 0;
/*  fe_status_t festatus; */
  struct dvb_frontend_event event;
  unsigned int strength;
  struct pollfd pfd[1];
  xine_cfg_entry_t config_tuning_timeout;
  struct timeval time_now;
  struct timeval tuning_timeout;

  /* discard stale events */
  while (ioctl(this->fd_frontend, FE_GET_EVENT, &event) != -1);

  if (ioctl(this->fd_frontend, FE_SET_FRONTEND, front_param) <0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: setfront front: %s\n", strerror(errno));
    return 0;
  }

  pfd[0].fd = this->fd_frontend;
  pfd[0].events = POLLIN;

  if (poll(pfd,1,3000)){
      if (pfd[0].revents & POLLIN){
#ifdef EOVERFLOW
	  if (ioctl(this->fd_frontend, FE_GET_EVENT, &event) == -EOVERFLOW) {
	      print_error("EOVERFLOW");
#else
	  if (ioctl(this->fd_frontend, FE_GET_EVENT, &event) == -EINVAL) {
	      print_error("EINVAL");
#endif
	      return 0;
	  }
	  if (event.parameters.frequency <= 0)
	      return 0;
      }
  }

  xine_config_lookup_entry(this->xine, "media.dvb.tuning_timeout", &config_tuning_timeout);
  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: media.dvb.tuning_timeout is %d\n", config_tuning_timeout.num_value );

  if( config_tuning_timeout.num_value != 0 ) {
    gettimeofday( &tuning_timeout, NULL );
    if( config_tuning_timeout.num_value < 5 )
        tuning_timeout.tv_sec += 5;
    else
        tuning_timeout.tv_sec += config_tuning_timeout.num_value;
  }

  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: tuner_tune_it - waiting for lock...\n" );

  do {
    status = 0;
    if (ioctl(this->fd_frontend, FE_READ_STATUS, &status) < 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: fe get event: %s\n", strerror(errno));
      return 0;
    }

    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: status: %x\n", status);
    if (status & FE_HAS_LOCK) {
      break;
    }

    /* FE_TIMEDOUT does not happen in a no signal condition.
     * Use the tuning_timeout config to prevent a hang in this loop
     */
    if( config_tuning_timeout.num_value != 0 ) {
      gettimeofday( &time_now, NULL );
      if( time_now.tv_sec > tuning_timeout.tv_sec ) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG, "input_dvb: No FE_HAS_LOCK before timeout\n");
        break;
      }
    }

    usleep(10000);
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "Trying to get lock...");
  } while (!(status & FE_TIMEDOUT));

  /* inform the user of frontend status */
  xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Tuner status:  ");
/*  if (ioctl(this->fd_frontend, FE_READ_STATUS, &status) >= 0){ */
    if (status & FE_HAS_SIGNAL)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_HAS_SIGNAL");
    if (status & FE_TIMEDOUT)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_TIMEDOUT");
    if (status & FE_HAS_LOCK)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_HAS_LOCK");
    if (status & FE_HAS_CARRIER)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_HAS_CARRIER");
    if (status & FE_HAS_VITERBI)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_HAS_VITERBI");
    if (status & FE_HAS_SYNC)
	xprintf(this->xine,XINE_VERBOSITY_LOG," FE_HAS_SYNC");
/*  } */
  xprintf(this->xine,XINE_VERBOSITY_LOG,"\n");

  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_BER,&strength) >= 0)
    xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Bit error rate: %i\n",strength);

  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_SIGNAL_STRENGTH,&strength) >= 0)
    xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Signal strength: %u\n",strength);

  strength=0;
  if(ioctl(this->fd_frontend,FE_READ_SNR,&strength) >= 0)
    xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Signal/Noise Ratio: %u\n",strength);

  if (status & FE_HAS_LOCK && !(status & FE_TIMEDOUT)) {
    xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Lock achieved at %lu Hz\n",(unsigned long)front_param->frequency);
    return 1;
  } else {
    xprintf(this->xine,XINE_VERBOSITY_LOG,"input_dvb: Unable to achieve lock at %lu Hz\n",(unsigned long)front_param->frequency);
    return 0;
  }

}

/* Parse the PMT, and add filters for all stream types associated with
 * the 'channel'. We leave it to the demuxer to sort out which PIDs to
 * use. to simplify things slightly, (and because the demuxer can't handle it)
 * allow only one of each media type */
static void parse_pmt(dvb_input_plugin_t *this, const unsigned char *buf, int section_length)
{

  int program_info_len;
  int pcr_pid;
  int has_video=0;
  int has_audio=0;
  int has_ac3=0;
  int has_subs=0;
  int has_text=0;

  dvb_set_pidfilter(this, PMTFILTER, this->channels[this->channel].pmtpid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, PATFILTER, 0, DMX_PES_OTHER,DMX_OUT_TS_TAP);

  pcr_pid = ((buf[0] & 0x1f) << 8) | buf[1];
  if(pcr_pid!=0x1FFF) /* don't waste time if the PCR is invalid */
    dvb_set_pidfilter(this, PCRFILTER, pcr_pid, DMX_PES_PCR,DMX_OUT_TS_TAP);

  program_info_len = ((buf[2] & 0x0f) << 8) | buf[3];
  buf += program_info_len + 4;
  section_length -= program_info_len + 4;

  while (section_length >= 5) {
    int elementary_pid = ((buf[1] & 0x1f) << 8) | buf[2];
    int descriptor_len = ((buf[3] & 0x0f) << 8) | buf[4];
    switch (buf[0]) {
      case 0x01:
      case 0x02:
      case 0x10:
      case 0x1b:
        if(!has_video) {
          xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding VIDEO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, VIDFILTER, elementary_pid, DMX_PES_VIDEO, DMX_OUT_TS_TAP);
	  has_video=1;
	}
	break;

      case 0x03:
      case 0x04:
      case 0x0f:
      case 0x11:
        if(!has_audio) {
	  xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding AUDIO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, AUDFILTER, elementary_pid, DMX_PES_AUDIO, DMX_OUT_TS_TAP);
	  has_audio=1;
	}
        break;

      case 0x06:
        if (find_descriptor(0x56, buf + 5, descriptor_len, NULL, NULL)) {
	  if(!has_text) {
	     xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding TELETEXT  : PID 0x%04x\n", elementary_pid);
	     dvb_set_pidfilter(this,TXTFILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
             has_text=1;
          }
	  break;
	} else if (find_descriptor (0x59, buf + 5, descriptor_len, NULL, NULL)) {
           /* Note: The subtitling descriptor can also signal
	    * teletext subtitling, but then the teletext descriptor
	    * will also be present; so we can be quite confident
	    * that we catch DVB subtitling streams only here, w/o
	    * parsing the descriptor. */
	    if(has_subs <= MAX_SUBTITLES) {
              xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding SUBTITLES: PID 0x%04x\n", elementary_pid);
               if(this->channels[this->channel].subpid [has_subs] !=DVB_NOPID) {
                  ioctl(this->tuner->fd_subfilter[has_subs], DMX_STOP);
               }
               this->channels[this->channel].subpid [has_subs] = elementary_pid;
               this->tuner->subFilterParams[has_subs].pid = elementary_pid;
               this->tuner->subFilterParams[has_subs].input = DMX_IN_FRONTEND;
               this->tuner->subFilterParams[has_subs].output = DMX_OUT_TS_TAP;
               this->tuner->subFilterParams[has_subs].pes_type = DMX_PES_OTHER;
               this->tuner->subFilterParams[has_subs].flags = DMX_IMMEDIATE_START;
               if (ioctl(this->tuner->fd_subfilter[has_subs], DMX_SET_PES_FILTER, &this->tuner->subFilterParams[has_subs]) < 0)
               {
	   xprintf(this->tuner->xine, XINE_VERBOSITY_DEBUG, "input_dvb: set_pid: %s\n", strerror(errno));
                   break;
               }
               has_subs++;
            }
	    break;
        } else if (find_descriptor (0x6a, buf + 5, descriptor_len, NULL, NULL)) {
            if(!has_ac3) {
	      dvb_set_pidfilter(this, AC3FILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
              xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding AC3       : PID 0x%04x\n", elementary_pid);
              has_ac3=1;
        }
	break;
	}
        break;
      case 0x81: /* AC3 audio */
	fprintf(stderr, "  pid type 0x%x,  has audio %d\n",buf[0],has_audio);
        if(!has_audio) {
	  xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Adding AUDIO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, AUDFILTER, elementary_pid, DMX_PES_AUDIO, DMX_OUT_TS_TAP);
	  has_audio=1;
	}
        break;

      };

    buf += descriptor_len + 5;
    section_length -= descriptor_len + 5;
  };
}

static void dvb_parse_si(dvb_input_plugin_t *this) {

  uint8_t *tmpbuffer;
  uint8_t *bufptr;
  int	service_id;
  int	result;
  int	section_len;
  int	x;
  struct pollfd pfd;

  tuner_t *tuner = this->tuner;
  tmpbuffer = calloc(1, 8192);

  _x_assert(tmpbuffer != NULL);

  bufptr = tmpbuffer;

  pfd.fd=tuner->fd_pidfilter[INTERNAL_FILTER];
  pfd.events = POLLPRI;

  xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Setting up Internal PAT filter\n");

  xine_usec_sleep(500000);

  /* first - the PAT. retrieve the entire section...*/
  dvb_set_sectfilter(this, INTERNAL_FILTER, 0, DMX_PES_OTHER, 0, 0xff);

  /* wait for up to 15 seconds */
  if(poll(&pfd,1,12000)<1) /* PAT timed out - weird, but we'll default to using channels.conf info */
  {
    xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: Error setting up Internal PAT filter - reverting to rc6 hehaviour\n");
    dvb_set_pidfilter (this,VIDFILTER,this->channels[this->channel].pid[VIDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    dvb_set_pidfilter (this,AUDFILTER,this->channels[this->channel].pid[AUDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    goto done;
  }
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer, 3);

  if(result!=3)
    xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: error reading PAT table - no data!\n");

  section_len = getbits(tmpbuffer,12,12);
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer+5,section_len);

  if(result!=section_len)
    xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: error reading in the PAT table\n");

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);

  bufptr+=10;
  this->num_streams_in_this_ts=0;
  section_len-=5;

  while(section_len>4){
    service_id = getbits (bufptr,0,16);
    for (x=0;x<this->num_channels;x++){
      if(this->channels[x].service_id==service_id) {
        this->channels[x].pmtpid = getbits (bufptr, 19, 13);
      }
    }
    section_len-=4;
    bufptr+=4;
    if(service_id>0) /* ignore NIT table for now */
      this->num_streams_in_this_ts++;
  }

  bufptr = tmpbuffer;

    /* next - the PMT */
  xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Setting up Internal PMT filter for pid %x\n",this->channels[this->channel].pmtpid);

  dvb_set_sectfilter(this, INTERNAL_FILTER, this->channels[this->channel].pmtpid, DMX_PES_OTHER, 2, 0xff);

  if((poll(&pfd,1,15000)<1) || this->channels[this->channel].pmtpid==0) /* PMT timed out or couldn't be found - default to using channels.conf info */
  {
    xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"input_dvb: PMT scan timed out. Using video & audio PID info from channels.conf.\n");
    dvb_set_pidfilter (this,VIDFILTER,this->channels[this->channel].pid[VIDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    dvb_set_pidfilter (this,AUDFILTER,this->channels[this->channel].pid[AUDFILTER], DMX_PES_OTHER, DMX_OUT_TS_TAP);
    goto done;
  }
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer,3);

  section_len = getbits (bufptr, 12, 12);
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer+3,section_len);

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);

  parse_pmt(this,tmpbuffer+8,section_len);

/*
  dvb_set_pidfilter(this, TSDTFILTER, 0x02,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, RSTFILTER, 0x13,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, TDTFILTER, 0x14,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, DITFILTER, 0x1e,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, CATFILTER, 0x01,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, NITFILTER, 0x10,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, SDTFILTER, 0x11, DMX_PES_OTHER, DMX_OUT_TS_TAP);
*/

  /* we use the section filter for EIT because we are guarenteed a complete section */
  if(ioctl(tuner->fd_pidfilter[EITFILTER],DMX_SET_BUFFER_SIZE,8192*this->num_streams_in_this_ts)<0)
    xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: couldn't increase buffer size for EIT: %s \n",strerror(errno));
  dvb_set_sectfilter(this, EITFILTER, 0x12,DMX_PES_OTHER,0x4e, 0xff);

  xprintf(this->stream->xine,XINE_VERBOSITY_DEBUG,"input_dvb: Setup of PID filters complete\n");

done:
  free(tmpbuffer);
}

/* Helper function for finding the channel index in the channels struct
   given the service_id. If channel is not found, -1 is returned. */
static int channel_index(dvb_input_plugin_t* this, int service_id) {
  int n;
  for (n=0; n < this->num_channels; n++)
    if (this->channels[n].service_id == service_id)
	return n;

  return -1;
}

static int compare_epg_by_starttime(const void* a, const void* b) {
    const epg_entry_t **epg_a, **epg_b;
    epg_a = (const epg_entry_t**)a;
    epg_b = (const epg_entry_t**)b;

    if ((*epg_a)->starttime < (*epg_b)->starttime) {
	return -1;
    } else if ((*epg_a)->starttime > (*epg_b)->starttime) {
	return 1;
    }
    return 0;
}

/* Finds the index of EPG entry with given starting time. If not found, returns -1. */
static int epg_with_starttime(channel_t* channel, time_t starttime) {
    int i;

    for (i = 0; i < channel->epg_count; i++) {
	if (channel->epg[i]->starttime == starttime)
	    return i;
    }
    return -1;
}

#ifdef EPG_UPDATE_IN_BACKGROUND
/* Sleep routine for pthread (hackish). */
static void pthread_sleep(int seconds) {
    pthread_mutex_t dummy_mutex;
    static pthread_cond_t dummy_cond = PTHREAD_COND_INITIALIZER;
    struct timespec timeout;

    /* Create a dummy mutex which doesn't unlock for sure while waiting. */
    pthread_mutex_init(&dummy_mutex, NULL);
    pthread_mutex_lock(&dummy_mutex);

    /* Create a dummy condition variable. */
/*    pthread_cond_init(&dummy_cond, NULL); */

    timeout.tv_sec = time(NULL) + seconds;
    timeout.tv_nsec = 0;

    pthread_cond_timedwait(&dummy_cond, &dummy_mutex, &timeout);

/*    pthread_cond_destroy(&dummy_cond); */
    pthread_mutex_unlock(&dummy_mutex);
    pthread_mutex_destroy(&dummy_mutex);
}

/* Thread routine that updates the EPG data periodically. */
static void* epg_data_updater(void *t) {
    dvb_input_plugin_t* this = (dvb_input_plugin_t*)t;
    while (!this->epg_updater_stop) {
#ifdef DEBUG_EPG
	print_info("EPG  epg_data_updater() updating...");
#endif
	load_epg_data(this);

	/* Update the EPG OSD if it's visible. */
	if (this->epg_displaying) {
	    this->epg_displaying = 0;
	    show_eit(this);
	}

	pthread_sleep(EPG_UPDATE_DELAY);
    }
#ifdef DEBUG_EPG
    print_info("EPG  epg_data_updater() returning...");
#endif
    return NULL;
}
#endif

/* This function parses the EIT table and saves the data used in
   EPG OSD of all channels found in the currently tuned stream. */
static void load_epg_data(dvb_input_plugin_t *this)
{
  int table_id;
  char skip_byte;
  int descriptor_id;
  int section_len = 0;
  unsigned int service_id=-1;
  int n;
  uint8_t *eit = NULL;
  uint8_t *foo = NULL;
  char *seen_channels = NULL;
  int text_len;
  struct pollfd fd;
  int loops;
  int current_channel_index;
  epg_entry_t* current_epg = NULL;
  channel_t* current_channel = NULL;
  int i;

  pthread_mutex_lock(&this->channel_change_mutex);

  /* seen_channels array is used to store information of channels that were
     already "found" in the stream. This information is used to initialize the
     channel's EPG structs when the EPG information for the channel is seen in
     the stream the first time. */
  seen_channels = calloc(this->num_channels, sizeof(char));
  _x_assert(seen_channels != NULL);

  foo = calloc(1, 8192);
  _x_assert(foo != NULL);

  fd.fd = this->tuner->fd_pidfilter[EITFILTER];
  fd.events = POLLPRI;

  for (loops = 0; loops <= this->num_streams_in_this_ts*2; loops++) {
    eit = foo;

    if (poll(&fd,1,2000)<1) {
       xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"(Timeout in EPG loop!! Quitting\n");
       pthread_mutex_unlock(&this->channel_change_mutex);
       free(seen_channels);
       free(foo);
       return;
    }
    n = read(this->tuner->fd_pidfilter[EITFILTER], eit, 3);
    if (n != 3) {
       xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"Error reading EPG section length\n");
       break;
    }
    table_id = getbits(eit, 0, 8);
    section_len = (unsigned int)getbits(eit, 12, 12);
    n = read(this->tuner->fd_pidfilter[EITFILTER], eit + 3, section_len);
    if (n != section_len) {
       xprintf(this->stream->xine,XINE_VERBOSITY_LOG,"Error reading EPG section data\n");
       break;
    }

    service_id = (unsigned int)getbits(eit, 24, 16);

    if ((current_channel_index = channel_index(this, service_id)) == -1) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,"input_dvb: load_epg_data(): unknown service_id: %d!\n", service_id);
      continue;
    }


    if (section_len > 15) {

      current_channel = &this->channels[current_channel_index];

      /* Reset the EPG struct if this channel is seen the first time in the stream. */
      if (!seen_channels[current_channel_index]) {
	  current_channel->epg_count = 0;
	  seen_channels[current_channel_index] = 1;
      }

      if (current_channel->epg_count >= MAX_EPG_ENTRIES_PER_CHANNEL) {

	  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		   "input_dvb: load_epg_data(): MAX_EPG_ENTRIES_PER_CHANNEL reached!\n");
	  continue;
      }

      /* Initialize the EPG struct if there's not one we can reuse.
         Allocate space for the strings. */
      if (current_channel->epg[current_channel->epg_count] == NULL) {
	  current_channel->epg[current_channel->epg_count] =
	    calloc(1, sizeof(epg_entry_t));
	  _x_assert(current_channel->epg[current_channel->epg_count] != NULL);

	  current_channel->epg[current_channel->epg_count]->progname =
	    malloc(MAX_EPG_PROGRAM_NAME_LENGTH + 1);
	  _x_assert(current_channel->epg[current_channel->epg_count]->progname != NULL);

	  current_channel->epg[current_channel->epg_count]->description =
	    malloc(MAX_EPG_PROGRAM_DESCRIPTION_LENGTH + 1);
	  _x_assert(current_channel->epg[current_channel->epg_count]->description != NULL);

	  current_channel->epg[current_channel->epg_count]->content =
	    malloc(MAX_EPG_CONTENT_TYPE_LENGTH + 1);
	  _x_assert(current_channel->epg[current_channel->epg_count]->content != NULL);
	  current_channel->epg[current_channel->epg_count]->running = 0;

      }

      current_epg = current_channel->epg[current_channel->epg_count];
      current_epg->starttime = dvb_mjdtime(eit+16);

      /* running_status:
	 0 undefined
	 1 not running
	 2 starts in a few seconds
	 3 pausing
	 4 running
       */
      if (getbits(foo,192,3) == 4){
	  current_epg->running = 1;
      } else {
	  current_epg->running = 0;
      }


      if (epg_with_starttime(current_channel, current_epg->starttime) != -1) {
	  /* Found already an entry with this starttime, let's not add it! */
	  continue;
      }

      current_epg->duration_hours = (char)bcdtoint(eit[21] & 0xff);
      current_epg->duration_minutes = (char)bcdtoint(eit[22] & 0xff);

      descriptor_id = eit[26];
      eit += 27;
      section_len -= 27;
      /* run the descriptor loop for the length of section_len */
      while (section_len > 1)
      {
        switch(descriptor_id) {
          case 0x4D: { /* simple program info descriptor */
              int name_len;
              /*int desc_len;*/
	      xine_cfg_entry_t language;

              /*desc_len =*/ getbits(eit, 0, 8);

	      /* Let's get the EPG data only in the wanted language. */

	      if (xine_config_lookup_entry(
		      this->stream->xine,
		      "media.dvd.language", &language) &&
		  language.str_value && strlen(language.str_value) >= 2 &&
		  strncasecmp(language.str_value, &eit[1], 2)) {

#ifdef DEBUG_EPG

		  printf("input_dvb: EPG  Skipping language: %C%C%C\n",
			 eit[1],eit[2],eit[3]);
		  printf("input_dvb: EPG  language.str_value: %s\n",
			 language.str_value);
#endif
		break;
	      }

              /* program name */
              name_len = (unsigned char)eit[4];
	      if (name_len == 0) {
		  current_epg->progname[0] = '\0';
		  break;
	      }

	      /* the first char of the string contains sometimes the character
		 encoding information, which should not be copied to the
		 string. (FIXME - we ought to be using this byte to change charsets)*/

	      if (!isalnum(*(eit + 5)))
		  skip_byte = 1;
	      else
		  skip_byte = 0;

              memcpy(current_epg->progname, eit + 5 + skip_byte,
		     name_len - skip_byte);
	      current_epg->progname[name_len - skip_byte] = '\0';

              /* detailed program information (max 256 chars)*/
              text_len = (unsigned char)eit[5 + name_len];
	      if (text_len == 0) {
		  current_epg->description[0] = '\0';
		  break;
	      }

	      if (!isalnum(*(eit + 6 + name_len)))
		  skip_byte = 1;
	      else
		  skip_byte = 0;

              memcpy(current_epg->description, eit + 6 + name_len + skip_byte,
		     text_len - skip_byte);
	      current_epg->description[text_len - skip_byte] = '\0';
            }
            break;

          case 0x54: {  /* Content Descriptor, riveting stuff */
              int content_bits = getbits(eit, 8, 4);
              static const char *const content[] = {
		  "UNKNOWN","MOVIE","NEWS","ENTERTAINMENT","SPORT",
		  "CHILDRENS","MUSIC","ARTS/CULTURE","CURRENT AFFAIRS",
		  "EDUCATIONAL","INFOTAINMENT","SPECIAL","COMEDY","DRAMA",
		  "DOCUMENTARY","UNK"};
              snprintf(current_epg->content, MAX_EPG_CONTENT_TYPE_LENGTH, "%s", content[content_bits]);
            }
            break;
          case 0x55: {  /* Parental Rating descriptor describes minimum recommened age -3 */

	      /* A rating value of 0 means that there is no rating defined. Ratings
		 greater than 0xF are "defined by broadcaster", which is not supported
		 for now. */
	      if (eit[4] > 0 && eit[4] <= 0xF)
		  current_epg->rating = eit[4] + 3;
	      else
		  current_epg->rating = 0;
            }
            break;
          default:
            break;
        }

        section_len -= getbits(eit, 0, 8) + 2;
        eit += getbits(eit, 0, 8);
        descriptor_id = eit[1];
        eit += 2;
      }
    /* Store the entry if we got enough data. */
    if (current_epg->progname && strlen(current_epg->progname))
	current_channel->epg_count++;

    }
  }
  /* Sort the EPG arrays by starttime. */
  for (i = 0; i < this->num_channels; ++i) {
      if (!seen_channels[i])
	  continue;
      qsort(this->channels[i].epg, this->channels[i].epg_count,
	    sizeof(epg_entry_t*), compare_epg_by_starttime);
  }
  free(seen_channels);
  free(foo);
  pthread_mutex_unlock(&this->channel_change_mutex);
}

/* Prints text to an area, tries to cut the lines in between words. */
static void render_text_area(osd_renderer_t* renderer, osd_object_t* osd, const char* text,
			     int x, int y, int row_space,
			     int max_x, int max_y, int* height, int color_base) {

  /* The position of the text to be printed. */
  const char* cursor = text;
  const char *const text_end = text + strlen(text);

  /* The line to be printed next. */
  char text_line[512];
  int text_width, text_height;
  size_t old_line_length, line_cursor;
  const char* bound, *old_bound;

  *height = 0;
  while (cursor < text_end) {
    bound = cursor;
    line_cursor = 0;
    text_line[0] = '\0';
    /* Find out how much fits in a row. */
    do {
      /* Find out the next word boundary. */
      old_bound = bound;
      old_line_length = strlen(text_line);
      line_cursor = old_line_length;

      /* Strip leading white space. */
      while (isspace(*bound))
	bound++;

      /* Copy text to the text_line until end of word or end of string. */
      while (!isspace(*bound) && *bound != '\0') {
	text_line[line_cursor] = *bound;
	bound++;
	line_cursor++;
      }

      text_line[line_cursor++] = ' ';
      text_line[line_cursor] = '\0';

      /* Try if the line with a new word still fits to the given area. */
      renderer->get_text_size(osd, text_line, &text_width, &text_height);
      if (x + text_width > max_x) {
	/* It didn't fit, restore the old line and stop trying to fit more.*/
	text_line[old_line_length] = '\0';

	/* If no words did fit to the line, fit as many characters as possible in it. */
	if (old_line_length == 0) {
	    text_width = 0;
	    bound = bound - line_cursor + 1; /* rewind to the beginning of the word */
	    line_cursor = 0;
	    while (!isspace(*bound) &&
		   *bound != '\0') {
		text_line[line_cursor++] = *bound++;
		text_line[line_cursor] = '\0';
		renderer->get_text_size(osd, text_line, &text_width, &text_height);

		/* The last character did not fit. */
		if (x + text_width >= max_x) {
		    text_line[line_cursor - 1] = '\0';
		    bound--;
		    break;
		}
	    }
	    /* The line is now filled with chars from the word. */
	    break;
	}
	bound = old_bound;
	break;
      }

      /* OK, it did fit, let's try to fit some more. */
    } while (bound < text_end);

    if (y + text_height + row_space > max_y) {
	break;
    }
    renderer->render_text(osd, x, y, text_line, color_base);
    *height += text_height + row_space;
    y += text_height + row_space;
    cursor = bound;
  }
}

/* Finds the EPG of the ith next program. 0 means the current program, 1 next.
   If not found, returns NULL. All these functions expect the EPG entries
   are sorted by starting time. */
static epg_entry_t* ith_next_epg(channel_t* channel, int count) {
    time_t current_time = time(NULL);
    int counter = 0;

    /* Discard the entries of past programs. */
    while (counter + 1 < channel->epg_count &&
	   difftime(channel->epg[counter + 1]->starttime, current_time) < 0.0)
	counter++;

    /* Check whether the previous program has still the running bit on,
       and if it's not more late than the given margin, assume it's still
       running. */
    if (counter >= 1 && channel->epg[counter - 1]->running &&
	difftime(current_time, channel->epg[counter]->starttime) < MAX_EPG_ENTRY_LATENESS) {
	counter--;
    }

    counter += count;

    if (counter >= channel->epg_count)
	return NULL;

    /* Check if the EPG to be returned is the last program in the EPG list and
       its duration info says that it should have ended more than n minutes
       ago. In that case do not return any EPG. This fixes the "very last
       program of the day sticking until morning" bug. */
    if (counter == channel->epg_count - 1) {
	if (difftime(current_time,
		     channel->epg[counter]->starttime +
		     channel->epg[counter]->duration_hours*60*60 +
		     channel->epg[counter]->duration_minutes*60) > MAX_EPG_ENTRY_LATENESS) {
	    return NULL;
	}
    }

    return channel->epg[counter];
}

/* Finds the EPG of the current program. If not found, returns NULL. */
static epg_entry_t* current_epg(channel_t* channel) {
    epg_entry_t* next = ith_next_epg(channel, 0);
#ifdef DEBUG_EPG
    if (next != NULL)
	printf("input_dvb: EPG  current: %s (%d)\n", next->progname, next->running);
#endif
    return next;
}

/* Finds the EPG of the next program. If not found, returns NULL. */
static epg_entry_t* next_epg(channel_t* channel) {
    epg_entry_t* next = ith_next_epg(channel, 1);
#ifdef DEBUG_EPG
    if (next != NULL)
	printf("input_dvb: EPG  next: %s (%d)\n", next->progname, next->running);
#endif
    return next;
}


/* Displays the program info of an EPG entry in OSD.

   x,y          The upper left coordinates of the program information area.
   max_x, max_y The maximum right coordinate of the program information area.
   last_y       The position of y after printing the entry.
   data         The EPG entry to display.

   Returns the height of the entry in the OSD in pixels.
*/
static void show_program_info(int x, int y, int max_x, int max_y, int* last_y,
			      epg_entry_t* epg_data, osd_renderer_t* renderer,
			      osd_object_t* osd) {
  char* buffer;
  int time_width, text_width, dummy;
  int content_width = 0;
  int text_height = 0;
  int time_height = 0;
  int prog_rating;
  struct tm* starttime = NULL;

  *last_y = y;

  if (epg_data == NULL || epg_data->progname == NULL)
    return;

  buffer = calloc(1, 512);

  _x_assert(buffer != NULL);

  if (!renderer->set_font(osd, "sans", EPG_TITLE_FONT_SIZE)) {
      print_error("Setting title font failed.");
  }

  starttime = localtime(&epg_data->starttime);
  strftime(buffer, 7, "%H:%M ", starttime);

  /* Print the starting time. */
  renderer->render_text(osd, x, y, buffer, OSD_TEXT3);
  renderer->get_text_size(osd, buffer, &time_width, &time_height);

  /*Content type and rating, if any. */
  if (strlen(epg_data->content) > 3) {
    strncpy(buffer, epg_data->content, 94-1);

    prog_rating = epg_data->rating;
    if (prog_rating > 0) {
      snprintf(buffer + strlen(buffer), 7, " (%i+)", prog_rating);
    }
    if (!renderer->set_font(osd, "sans", EPG_CONTENT_FONT_SIZE)) {
	print_error("Setting content type font failed.");
    }
    renderer->get_text_size(osd, buffer, &content_width, &dummy);
    renderer->render_text(osd, max_x - 2 - content_width, y, buffer, OSD_TEXT3);
  }

  text_width = max_x - x - time_width - content_width - 2;

  renderer->set_font(osd, "sans", EPG_TITLE_FONT_SIZE);

  render_text_area(renderer, osd, epg_data->progname,
		   x + time_width, y, EPG_PIXELS_BETWEEN_TEXT_ROWS,
		   x + text_width + time_width, max_y, &text_height,
		   OSD_TEXT4);

  if (text_height == 0)
      *last_y = y + time_height;
  else
      *last_y = y + text_height;

  /* Print the description. */
  if (epg_data->description && strlen(epg_data->description) > 0) {
    renderer->set_font(osd, "sans", EPG_DESCRIPTION_FONT_SIZE);
    strcpy(buffer, epg_data->description);
    /* If the description is not complete (i.e., there is no comma at the end),
       add "..." to the end. In my locale they often seem to send incomplete description
       texts :( */
    if (buffer[strlen(buffer)-1] != '.' && buffer[strlen(buffer)-1] != '?' &&
	buffer[strlen(buffer)-1] != '!') {
      strcat(buffer, "...");
    }

    /* If duration_hours is zero, do not print them. */
    if (epg_data->duration_hours > 0)
      sprintf(buffer + strlen(buffer), " (%dh%02dmin)",
	      epg_data->duration_hours, epg_data->duration_minutes);
    else if (epg_data->duration_minutes > 0)
      sprintf(buffer + strlen(buffer), " (%dmin)",
	      epg_data->duration_minutes);

    render_text_area(renderer, osd, buffer, x + time_width,
		     *last_y + EPG_PIXELS_BETWEEN_TEXT_ROWS,
		     EPG_PIXELS_BETWEEN_TEXT_ROWS,
		     max_x, max_y, &text_height, OSD_TEXT3);

    *last_y += EPG_PIXELS_BETWEEN_TEXT_ROWS + text_height;
  }

  free(buffer);
}

/* Shows the EPG listing for the current channel. */
static void show_eit(dvb_input_plugin_t *this) {
  int y;
  int centered_x, centered_y;
  int y_pos = 0;
  int window_width, window_height, stream_width, stream_height;
  int temp1, temp2;
  time_t ct;
  char clock[6];

  if (!this->epg_displaying) {

#ifndef EPG_UPDATE_IN_BACKGROUND
    if (current_epg(&this->channels[this->channel]) == NULL ||
	next_epg(&this->channels[this->channel]) == NULL) {
	load_epg_data(this);
    }
#endif

    this->epg_displaying = 1;
    this->stream->osd_renderer->hide(this->proginfo_osd, 0);
    this->stream->osd_renderer->clear(this->proginfo_osd);

    /* Channel Name */
    if (!this->stream->osd_renderer->set_font(
	    this->proginfo_osd, "sans", EPG_CHANNEL_FONT_SIZE)) {
	print_error("Error setting channel name font.");
    }

    this->stream->osd_renderer->render_text(
      this->proginfo_osd, 0, 0, this->channels[this->channel].name, OSD_TEXT4);

    time(&ct);
    strftime(clock, sizeof(clock), "%H:%M", localtime(&ct));
    clock[5] = '\0';

    /* Clock - align it to right. */
    if (!this->stream->osd_renderer->set_font(
	    this->proginfo_osd, "sans", EPG_CLOCK_FONT_SIZE)) {
	print_error("Error setting clock font.");
    }

    this->stream->osd_renderer->get_text_size(
      this->proginfo_osd, this->channels[this->channel].name, &temp1, &temp2);

    this->stream->osd_renderer->render_text(
      this->proginfo_osd, EPG_WIDTH - 45,
      EPG_CHANNEL_FONT_SIZE - EPG_CLOCK_FONT_SIZE, clock, OSD_TEXT4);

    show_program_info(0, EPG_CHANNEL_FONT_SIZE + 2, EPG_WIDTH, EPG_HEIGHT, &y_pos,
		      current_epg(&this->channels[this->channel]),
		      this->stream->osd_renderer,
		      this->proginfo_osd);
    y = y_pos;

    show_program_info(0, y, EPG_WIDTH, EPG_HEIGHT, &y_pos,
		      next_epg(&this->channels[this->channel]),
		      this->stream->osd_renderer,
		      this->proginfo_osd);
    y = y_pos;

    window_width =
	this->stream->video_out->get_property(
	    this->stream->video_out, VO_PROP_WINDOW_WIDTH);

    window_height =
	this->stream->video_out->get_property(
	    this->stream->video_out, VO_PROP_WINDOW_HEIGHT);

    stream_width =
	xine_get_stream_info(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH);

    stream_height =
	xine_get_stream_info(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT);

    /* Resize the background to be the size of the OSD texts + margins. */
    this->stream->osd_renderer->clear(this->background);
    this->stream->osd_renderer->set_font(this->background, "cetus", 32);
    this->stream->osd_renderer->set_encoding(this->background, NULL);
    this->stream->osd_renderer->set_text_palette(
	this->background, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);
    this->stream->osd_renderer->filled_rect(
	this->background, 0, 0,
	EPG_WIDTH + EPG_BACKGROUND_MARGIN*2,
	y + EPG_BACKGROUND_MARGIN*2, 4);

    /* In case video is downscaled and the EPG fits, show it unscaled to make it
       appear bigger thus more readable. NOT FULLY TESTED. */
    if (stream_width > window_width && window_width > EPG_WIDTH) {

      centered_x = (window_width - EPG_WIDTH) / 2;
      centered_x = (centered_x > 0)?(centered_x):(0);

      centered_y = (window_height - y) / 3;
      centered_y = (centered_y > 0)?(centered_y):(EPG_TOP);

      this->stream->osd_renderer->set_position(
	  this->proginfo_osd,
	  centered_x + EPG_BACKGROUND_MARGIN,
	  centered_y + EPG_BACKGROUND_MARGIN);

      this->stream->osd_renderer->set_position(this->background, centered_x, centered_y);
      this->stream->osd_renderer->show_unscaled(this->background, 0);
      this->stream->osd_renderer->show_unscaled(this->proginfo_osd, 0);
    } else {
      /* Otherwise make it scaled. */
      centered_x = (stream_width - EPG_WIDTH) / 2;
      centered_x = (centered_x > 0)?(centered_x):(0);

      centered_y = (stream_height - y) / 3;
      centered_y = (centered_y > 0)?(centered_y):(EPG_TOP);

      /* Center the OSD to stream. */
      this->stream->osd_renderer->set_position(
	  this->proginfo_osd,
	  centered_x + EPG_BACKGROUND_MARGIN,
	  centered_y + EPG_BACKGROUND_MARGIN);

      this->stream->osd_renderer->set_position(this->background, centered_x, centered_y);
      this->stream->osd_renderer->show(this->background, 0);
      this->stream->osd_renderer->show(this->proginfo_osd, 0);
    }

  } else {
    this->epg_displaying = 0;
    this->stream->osd_renderer->hide (this->proginfo_osd,0);
    this->stream->osd_renderer->hide (this->background,0);
  }

  return;
}

static int tuner_set_channel (dvb_input_plugin_t *this, channel_t *c) {
  tuner_t *tuner=this->tuner;
  xine_cfg_entry_t lastchannel;
  config_values_t *config = this->stream->xine->config;

  if (tuner->feinfo.type==FE_QPSK) {
    if(!(tuner->feinfo.caps & FE_CAN_INVERSION_AUTO))
      c->front_param.inversion = INVERSION_OFF;
    if (!tuner_set_diseqc(tuner, c))
      return 0;
  }

  if (!tuner_tune_it (tuner, &c->front_param)){
    return 0;
  }

  if (xine_config_lookup_entry(this->stream->xine, "media.dvb.remember_channel", &lastchannel))
    if (lastchannel.num_value){
      /* Remember last watched channel. never show this entry*/
      config->update_num(config, "media.dvb.last_channel", this->channel+1);
    }
#ifdef DVB_NO_BUFFERING
    this->newchannel=1;
#endif
  return 1; /* fixme: error handling */
}

static void osd_show_channel (dvb_input_plugin_t *this, int channel) {

  int i, channel_to_print, temp;
  epg_entry_t* current_program = NULL;

  this->stream->osd_renderer->clear(this->channel_osd);
  this->stream->osd_renderer->filled_rect (this->channel_osd, 0, 0, CHSEL_WIDTH, CHSEL_HEIGHT, 2);

  channel_to_print = channel - 5;

  for (i=0; i<11; i++) {

      if ((channel_to_print >= 0) && (channel_to_print < this->num_channels)) {
	  this->stream->osd_renderer->set_font(this->channel_osd, "cetus", CHSEL_CHANNEL_FONT_SIZE);
	  this->stream->osd_renderer->set_text_palette(
	      this->channel_osd, XINE_TEXTPALETTE_WHITE_NONE_TRANSLUCID, OSD_TEXT3);
	  this->stream->osd_renderer->set_text_palette(
	      this->channel_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT4);

	  this->stream->osd_renderer->render_text(
	      this->channel_osd, 110, 10+i*35,
	      this->channels[channel_to_print].name,
	      (channel_to_print == channel)?(OSD_TEXT4):(OSD_TEXT3));

	  if ((current_program = current_epg(&this->channels[channel_to_print])) &&
	      current_program->progname && strlen(current_program->progname) > 0) {

	      this->stream->osd_renderer->set_font(this->channel_osd, "sans", 16);

	      render_text_area(this->stream->osd_renderer, this->channel_osd,
			       current_program->progname, 400, 10+i*35,
			       -5, CHSEL_WIDTH, 10+i*35+CHSEL_CHANNEL_FONT_SIZE+2,
			       &temp, (channel_to_print == channel)?(OSD_TEXT4):(OSD_TEXT3));
	  }
      }

    channel_to_print++;
  }

  this->stream->osd_renderer->line (this->channel_osd, 105, 183, 390, 183, 10);
  this->stream->osd_renderer->line (this->channel_osd, 105, 183, 105, 219, 10);
  this->stream->osd_renderer->line (this->channel_osd, 105, 219, 390, 219, 10);
  this->stream->osd_renderer->line (this->channel_osd, 390, 183, 390, 219, 10);

  this->stream->osd_renderer->show (this->channel_osd, 0);
  /* hide eit if showing */
  if (this->epg_displaying==1) {
    this->stream->osd_renderer->hide (this->proginfo_osd,0);
    this->stream->osd_renderer->hide (this->background,0);
  }
}

static int switch_channel(dvb_input_plugin_t *this, int channel) {

  int x;
  xine_event_t     event;
  xine_pids_data_t data;
  xine_ui_data_t   ui_data;

  /* control_nop appears to stop an occasional (quite long) pause between
     channel-changes, which the user may see as a lockup. */
  _x_demux_control_nop(this->stream, BUF_FLAG_END_STREAM);
  _x_demux_flush_engine(this->stream);

  pthread_mutex_lock (&this->channel_change_mutex);

  close (this->fd);
  this->tuned_in = 0;

  for (x = 0; x < MAX_FILTERS; x++) {
    close(this->tuner->fd_pidfilter[x]);
    this->tuner->fd_pidfilter[x] = xine_open_cloexec(this->tuner->demux_device, O_RDWR);
   }

  if (!tuner_set_channel (this, &this->channels[channel])) {
    xprintf (this->class->xine, XINE_VERBOSITY_LOG,
	     _("input_dvb: tuner_set_channel failed\n"));
    pthread_mutex_unlock (&this->channel_change_mutex);
    return 0;
  }

  event.type = XINE_EVENT_PIDS_CHANGE;
  data.vpid = this->channels[channel].pid[VIDFILTER];
  data.apid = this->channels[channel].pid[AUDFILTER];
  event.data = &data;
  event.data_length = sizeof (xine_pids_data_t);

  xprintf (this->class->xine, XINE_VERBOSITY_DEBUG, "input_dvb: sending event\n");

  xine_event_send (this->stream, &event);

  snprintf (ui_data.str, strlen(this->channels[channel].name)+1, "%s", this->channels[channel].name);
  ui_data.str_len = strlen (ui_data.str);
  _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, ui_data.str);

  event.type        = XINE_EVENT_UI_SET_TITLE;
  event.stream      = this->stream;
  event.data        = &ui_data;
  event.data_length = sizeof(ui_data);
  xine_event_send(this->stream, &event);

  xprintf(this->class->xine,XINE_VERBOSITY_DEBUG,"ui title event sent\n");

  this->channel = channel;

  this->fd = xine_open_cloexec(this->tuner->dvr_device, O_RDONLY | O_NONBLOCK);
  this->tuned_in = 1;

  pthread_mutex_unlock (&this->channel_change_mutex);

  /* now read the pat, find all accociated PIDs and add them to the stream */
  dvb_parse_si(this);

  this->stream->osd_renderer->hide(this->channel_osd, 0);

  /* if there is no EPG data, start loading it immediately. */
  if (current_epg(&this->channels[channel]) == NULL)
      load_epg_data(this);

  /* show eit for this channel if necessary */
  if (this->epg_displaying==1){
      this->epg_displaying=0;
      show_eit(this);
  }
  return 1;
}

static void do_record (dvb_input_plugin_t *this) {

 struct tm *tma;
 time_t *t;
 char filename [256];
 char dates[64];
 int x=0;
 xine_cfg_entry_t savedir;
 DIR *dir;

 if (this->record_fd > -1) {

    /* stop recording */
    close (this->record_fd);
    this->record_fd = -1;

    this->stream->osd_renderer->hide (this->rec_osd, 0);
    this->stream->osd_renderer->hide (this->paused_osd, 0);
    this->record_paused=0;
  } else {
   t=calloc(1, sizeof(time_t));

    _x_assert(t != NULL);

    time(t);
    tma=localtime(t);
    free(t);
    t = NULL;
    strftime(dates,63,"%Y-%m-%d_%H%M",tma);

    if (xine_config_lookup_entry(this->stream->xine, "media.capture.save_dir", &savedir)){
      if(strlen(savedir.str_value)>1){
        if((dir = opendir(savedir.str_value))==NULL){
          snprintf (filename, 256, "%s/%s_%s.ts",xine_get_homedir(),this->channels[this->channel].name, dates);
          xprintf(this->class->xine,XINE_VERBOSITY_LOG,"savedir is wrong... saving to home directory\n");
        } else {
          closedir(dir);
          snprintf (filename, 256, "%s/%s_%s.ts",savedir.str_value,this->channels[this->channel].name, dates);
          xprintf(this->class->xine,XINE_VERBOSITY_LOG,"saving to savedir\n");
        }
      } else {
        snprintf (filename, 256, "%s/%s_%s.ts",xine_get_homedir(),this->channels[this->channel].name, dates);
        xprintf(this->class->xine,XINE_VERBOSITY_LOG,"Saving to HomeDir\n");
      }
    }
    /* remove spaces from name */
    while((filename[x]!=0) && x<255){
      if(filename[x]==' ') filename[x]='_';
      x++;
    }

    /* start recording */
    this->record_fd = xine_create_cloexec(filename, O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    this->stream->osd_renderer->clear (this->rec_osd);

    this->stream->osd_renderer->render_text (this->rec_osd, 10, 10, "Recording to:",
					     OSD_TEXT3);

    this->stream->osd_renderer->render_text (this->rec_osd, 160, 10, filename,
					     OSD_TEXT3);

    this->stream->osd_renderer->show_unscaled (this->rec_osd, 0);

  }
}

static void dvb_event_handler (dvb_input_plugin_t *this) {

  xine_event_t *event;
  static int channel_menu_visible = 0;
  static int next_channel = -1;

  while ((event = xine_event_get (this->event_queue))) {

    xprintf(this->class->xine,XINE_VERBOSITY_DEBUG,"got event %08x\n", event->type);

    if (this->fd < 0) {
      xine_event_free (event);
      return;
    }

    switch (event->type) {

    case XINE_EVENT_INPUT_MOUSE_BUTTON: {
      xine_input_data_t *input = (xine_input_data_t*)event->data;
      switch (input->button) {

      case MOUSE_BUTTON_LEFT:
	if (channel_menu_visible) {
	  channel_menu_visible = 0;
	  if (next_channel != this->channel){
	      this->channel = next_channel;
	      switch_channel (this, next_channel);
          }
	  else
	      this->stream->osd_renderer->hide(this->channel_osd, 0);
	}
#ifdef LEFT_MOUSE_DOES_EPG
	else {		/* show EPG on left click of videowindow */
	  show_eit(this);
	}
#endif
	break;

      case MOUSE_WHEEL_UP:
	if (!channel_menu_visible)
	    next_channel = this->channel;

	if (next_channel > 0)
	    next_channel--;

	channel_menu_visible = 1;
	osd_show_channel(this, next_channel);
	break;

      case MOUSE_WHEEL_DOWN:

	if (!channel_menu_visible)
	    next_channel = this->channel;

	if (next_channel < (this->num_channels-1))
	    next_channel++;

	channel_menu_visible = 1;
	osd_show_channel(this, next_channel);
	break;

      case MOUSE_SIDE_LEFT:
	if (this->channel > 0) {
	  this->channel--;
	  channel_menu_visible = 0;
	  switch_channel(this, this->channel);
	}
	break;

      case MOUSE_SIDE_RIGHT:
	if (this->channel < (this->num_channels-1)) {
	  this->channel++;
	  channel_menu_visible = 0;
	  switch_channel (this, this->channel);
	}
	break;

      default:
	  /* Unused mouse event. */
	  break;
      }
      break;
    }

    case XINE_EVENT_INPUT_DOWN:
      if (!channel_menu_visible)
	next_channel = this->channel;

      if (next_channel < (this->num_channels-1))
	next_channel++;

      channel_menu_visible = 1;
      osd_show_channel(this, next_channel);

      break;

    case XINE_EVENT_INPUT_UP:
      if (!channel_menu_visible)
	next_channel = this->channel;

      if (next_channel > 0)
	next_channel--;

      channel_menu_visible = 1;
      osd_show_channel(this, next_channel);
      break;

    case XINE_EVENT_INPUT_NEXT:
      if (this->channel < (this->num_channels-1)) {
	channel_menu_visible = 0;
	switch_channel (this, this->channel + 1);
      }
      break;

    case XINE_EVENT_INPUT_SELECT:
      channel_menu_visible = 0;
      if (next_channel != this->channel){
	switch_channel (this, next_channel);
	this->channel = next_channel;
      }
      else
	this->stream->osd_renderer->hide(this->channel_osd, 0);
      break;

    case XINE_EVENT_INPUT_PREVIOUS:
      if (this->channel>0) {
	channel_menu_visible = 0;
	switch_channel (this, this->channel - 1);
      }
      break;

    case XINE_EVENT_INPUT_MENU1:
      if (this->osd != NULL)
	this->stream->osd_renderer->hide (this->osd, 0);
      channel_menu_visible = 0;
      break;

    case XINE_EVENT_INPUT_MENU2:
      do_record (this);
      break;

    case XINE_EVENT_INPUT_MENU3:
      /* zoom for cropped 4:3 in a 16:9 window */
      if (!this->zoom_ok) {
       this->zoom_ok = 1;
       this->stream->video_out->set_property(
	   this->stream->video_out, VO_PROP_ZOOM_X, 133);
       this->stream->video_out->set_property(
	   this->stream->video_out, VO_PROP_ZOOM_Y, 133);
      } else {
       this->zoom_ok=0;
       this->stream->video_out->set_property(
	   this->stream->video_out, VO_PROP_ZOOM_X, 100);
       this->stream->video_out->set_property(
	   this->stream->video_out, VO_PROP_ZOOM_Y, 100);
      }
      break;

    case XINE_EVENT_INPUT_MENU4:
      /* Pause recording.. */
      if ((this->record_fd>-1) && (!this->record_paused)) {
       this->record_paused = 1;
       this->stream->osd_renderer->render_text (this->paused_osd, 15, 10, "Recording Paused",OSD_TEXT3);
       this->stream->osd_renderer->show_unscaled (this->paused_osd, 0);
      } else {
       this->record_paused=0;
       this->stream->osd_renderer->hide (this->paused_osd, 0);
      }
      break;

    case XINE_EVENT_INPUT_MENU7:
	 channel_menu_visible = 0;
         show_eit(this);
        break;

#if 0
   default:
      printf ("input_dvb: got an event, type 0x%08x\n", event->type);
#endif
    }

    xine_event_free (event);
  }
}

/* parse TS and re-write PAT to contain only our pmt */
static void ts_rewrite_packets (dvb_input_plugin_t *this, unsigned char * originalPkt, int len) {

#define PKT_SIZE 188
#define BODY_SIZE (188-4)
  unsigned int  sync_byte;
  unsigned int  data_offset;
  unsigned int  data_len;
  unsigned int	pid;

  while(len>0){

    sync_byte                      = originalPkt[0];
    pid                            = ((originalPkt[1] << 8) | originalPkt[2]) & 0x1fff;

    /*
     * Discard packets that are obviously bad.
     */

    data_offset = 4;
    originalPkt+=data_offset;

    if (pid == 0 && sync_byte==0x47) {
      unsigned long crc;

      originalPkt[3]=13; /* section length including CRC - first 3 bytes */
      originalPkt[2]=0x80;
      originalPkt[7]=0; /* section number */
      originalPkt[8]=0; /* last section number */
      originalPkt[9]=(this->channels[this->channel].service_id >> 8) & 0xff;
      originalPkt[10]=this->channels[this->channel].service_id & 0xff;
      originalPkt[11]=(this->channels[this->channel].pmtpid >> 8) & 0xff;
      originalPkt[12]=this->channels[this->channel].pmtpid & 0xff;

      crc = av_crc(this->class->av_crc, 0xffffffff, originalPkt+1, 12);

      originalPkt[13]=(crc    ) & 0xff;
      originalPkt[14]=(crc>> 8) & 0xff;
      originalPkt[15]=(crc>>16) & 0xff;
      originalPkt[16]=(crc>>24) & 0xff;
      memset(originalPkt+17,0xFF,PKT_SIZE-21); /* stuff the remainder */

    }

  data_len = PKT_SIZE - data_offset;
  originalPkt+=data_len;
  len-=data_len;

  }
}

static off_t dvb_plugin_read (input_plugin_t *this_gen,
			      void *buf_gen, off_t len) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
  uint8_t *buf = buf_gen;

  off_t n=0, total=0;
  struct pollfd pfd;

  if (!this->tuned_in)
      return 0;
  if (this->dvb_gui_enabled)
      dvb_event_handler (this);
#ifdef LOG_READS
  xprintf(this->class->xine,XINE_VERBOSITY_DEBUG,
	  "input_dvb: reading %" PRIdMAX " bytes...\n", (intmax_t)len);
#endif

  /* protect against channel changes */
  pthread_mutex_lock(&this->channel_change_mutex);
  total=0;

  while (total<len){
      pfd.fd = this->fd;
      pfd.events = POLLPRI | POLLIN | POLLERR;
      pfd.revents = 0;

      if (!this->tuned_in) {
	  pthread_mutex_unlock( &this->channel_change_mutex );
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG,
		  "input_dvb: Channel \"%s\" could not be tuned in. "
		  "Possibly erroneus settings in channels.conf "
		  "(frequency changed?).\n",
		  this->channels[this->channel].name);
	  return 0;
      }

      if (poll(&pfd, 1, 1500) < 1) {
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG,
		  "input_dvb:  No data available.  Signal Lost??  \n");
	  _x_demux_control_end(this->stream, BUF_FLAG_END_USER);
	  this->read_failcount++;
	  break;
      }

      if (this->read_failcount) {
      /* signal/stream regained after loss -
	 kick the net_buf_control layer. */
	  this->read_failcount=0;
	  xprintf(this->class->xine,XINE_VERBOSITY_LOG,
		  "input_dvb: Data resumed...\n");
	  _x_demux_control_start(this->stream);
      }

      if (pfd.revents & POLLPRI || pfd.revents & POLLIN) {
	  n = read (this->fd, &buf[total], len-total);
      } else
	  if (pfd.revents & POLLERR) {
	      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
		      "input_dvb:  No data available.  Signal Lost??  \n");
	      _x_demux_control_end(this->stream, BUF_FLAG_END_USER);
	      this->read_failcount++;
	      break;
	  }

#ifdef LOG_READS
      xprintf(this->class->xine,XINE_VERBOSITY_DEBUG,
	      "input_dvb: got %" PRIdMAX " bytes (%" PRIdMAX "/%" PRIdMAX " bytes read)\n",
	      (intmax_t)n, (intmax_t)total, (intmax_t)len);
#endif

      if (n > 0){
	  this->curpos += n;
	  total += n;
      } else if (n < 0 && errno!=EAGAIN) {
	  break;
      }
  }

  ts_rewrite_packets (this, buf,total);

  if ((this->record_fd > -1) && (!this->record_paused))
    if (write (this->record_fd, buf, total) != total) {
      do_record(this);
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      "input_dvb: Recording failed\n");
    }

  pthread_mutex_unlock( &this->channel_change_mutex );

  /* no data for several seconds - tell the user a possible reason */
  if(this->read_failcount==5){
    _x_message(this->stream,1,"DVB Signal Lost.  Please check connections.", NULL);
  }
#ifdef DVB_NO_BUFFERING
  if(this->newchannel){
    this->newchannel = 0;
    xine_usec_sleep(1200000);
  }
#endif
  return total;
}

static buf_element_t *dvb_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t todo) {
  /* dvb_input_plugin_t   *this = (dvb_input_plugin_t *) this_gen;  */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int                   total_bytes;

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type    = BUF_DEMUX_BLOCK;

  total_bytes = dvb_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t dvb_plugin_seek (input_plugin_t *this_gen, off_t offset,
			      int origin) {

  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  xprintf(this->class->xine,XINE_VERBOSITY_DEBUG,"seek %" PRIdMAX " bytes, origin %d\n", (intmax_t)offset, origin);

  /* only relative forward-seeking is implemented */

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      this->curpos += dvb_plugin_read (this_gen, this->seek_buf, BUFSIZE);
    }

    this->curpos += dvb_plugin_read (this_gen, this->seek_buf, offset);
  }

  return this->curpos;
}

static off_t dvb_plugin_get_length (input_plugin_t *this_gen) {
  return 0;
}

static uint32_t dvb_plugin_get_capabilities (input_plugin_t *this_gen) {
  return 0; /* INPUT_CAP_CHAPTERS */ /* where did INPUT_CAP_AUTOPLAY go ?!? */
}

static uint32_t dvb_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t dvb_plugin_get_current_pos (input_plugin_t *this_gen){
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  return this->curpos;
}

static void dvb_plugin_dispose (input_plugin_t *this_gen) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
  int i, j;

  if (this->fd != -1) {
    close(this->fd);
    this->fd = -1;
  }

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  if(this->mrl)
    free (this->mrl);

  /* Free the EPG data. */
  for (i = 0; i < this->num_channels; ++i) {
      for (j = 0; j < MAX_EPG_ENTRIES_PER_CHANNEL && this->channels[i].epg[j]; ++j) {
        if(this->channels[i].epg[j]->description)
	  free(this->channels[i].epg[j]->description);
        if(this->channels[i].epg[j]->progname)
	  free(this->channels[i].epg[j]->progname);
	if(this->channels[i].epg[j]->content)
          free(this->channels[i].epg[j]->content);
	if(this->channels[i].epg[j])
          free(this->channels[i].epg[j]);
	this->channels[i].epg[j] = NULL;
      }
  }
  if (this->channels)
    free_channel_list (this->channels, this->num_channels);


  /* Make the EPG updater thread return. */
  this->epg_updater_stop = 1;

  if (this->tuner)
    tuner_dispose (this->tuner);

  if(this->proginfo_osd)
    this->stream->osd_renderer->hide (this->proginfo_osd,0);

  if(this->background)
    this->stream->osd_renderer->hide (this->background,0);

  /* free all memory associated with our OSD */
  if(this->rec_osd)
    this->stream->osd_renderer->free_object(this->rec_osd);
  if(this->channel_osd)
    this->stream->osd_renderer->free_object(this->channel_osd);
  if(this->name_osd)
    this->stream->osd_renderer->free_object(this->name_osd);
  if(this->paused_osd)
    this->stream->osd_renderer->free_object(this->paused_osd);
  if(this->proginfo_osd)
    this->stream->osd_renderer->free_object(this->proginfo_osd);
  if(this->background)
    this->stream->osd_renderer->free_object(this->background);

  free (this);
}

static const char* dvb_plugin_get_mrl (input_plugin_t *this_gen) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  return this->mrl;
}

static int dvb_plugin_get_optional_data (input_plugin_t *this_gen,
					 void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/* allow center cutout zoom for dvb content */
static void
dvb_zoom_cb (void *this_gen, xine_cfg_entry_t *cfg)
{
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  if (!this)
    return;

  this->zoom_ok = cfg->num_value;

  if (this->zoom_ok) {
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 133);
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 133);
  } else {
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 100);
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 100);
  }
}


static int dvb_plugin_open(input_plugin_t * this_gen)
{
    dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
    tuner_t *tuner;
    channel_t *channels;
    int num_channels = 0;
    config_values_t *config = this->stream->xine->config;
    char str[256];
    char *ptr;
    int x;
    char dummy=0;
    xine_cfg_entry_t zoomdvb;
    xine_cfg_entry_t adapter;
    xine_cfg_entry_t lastchannel;
    xine_cfg_entry_t gui_enabled;

    xine_config_lookup_entry(this->stream->xine, "media.dvb.gui_enabled", &gui_enabled);
    this->dvb_gui_enabled = gui_enabled.num_value;
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: DVB GUI %s\n"), this->dvb_gui_enabled ? "enabled" : "disabled");

    xine_config_lookup_entry(this->stream->xine, "media.dvb.adapter", &adapter);

    if (!(tuner = tuner_init(this->class->xine,adapter.num_value))) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: cannot open dvb device\n"));
      return 0;
    }

    if (strncasecmp(this->mrl, "dvb://", 6) == 0) {
     /*
      * This is either dvb://<number>
      * or the "magic" dvb://<channel name>
      * We load the channels from ~/.xine/channels.conf
      * and assume that its format is valid for our tuner type
      */

      if (!(channels = load_channels(this->class->xine, this->stream, &num_channels, tuner->feinfo.type)))
      {
        /* failed to load the channels */
	 tuner_dispose(tuner);
	 return 0;
      }

      if ((sscanf(this->mrl, "dvb://%d%1c", &this->channel, &dummy) >0 ) && ((isalpha(dummy)==0) && (isspace(dummy)==0)))
      {
        /* dvb://<number> format: load channels from ~/.xine/channels.conf */
	if (this->channel >= num_channels) {
          xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	            _("input_dvb: channel %d out of range, defaulting to 0\n"),
		    this->channel);
          this->channel = 0;
	}
      } else {
        /* dvb://<channel name> format ? */
        char *channame = this->mrl + 6;
	if (*channame) {
	  /* try to find the specified channel */
	  int idx = 0;
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	          _("input_dvb: searching for channel %s\n"), channame);

	  while (idx < num_channels) {
	    if (strcasecmp(channels[idx].name, channame) == 0)
	      break;
            idx++;
          }

	 if (idx < num_channels) {
	   this->channel = idx;
         } else {
           /*
            * try a partial match too
	    * be smart and compare starting from the first char, then from
	    * the second etc..
	    * Yes, this is expensive, but it happens really often
	    * that the channels have really ugly names, sometimes prefixed
	    * by numbers...
	    */
	    size_t chanlen = strlen(channame);
	    size_t offset = 0;

	    xprintf(this->class->xine, XINE_VERBOSITY_LOG,
		     _("input_dvb: exact match for %s not found: trying partial matches\n"), channame);

            do {
	      idx = 0;
	      while (idx < num_channels) {
	        if (strlen(channels[idx].name) > offset) {
		  if (strncasecmp(channels[idx].name + offset, channame, chanlen) == 0) {
                     xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: found matching channel %s\n"), channels[idx].name);
                     break;
                  }
		}
		idx++;
              }
	      offset++;
	      xprintf(this->class->xine,XINE_VERBOSITY_LOG,"%zd,%d,%d\n", offset, idx, num_channels);
            }
            while ((offset < 6) && (idx == num_channels));
              if (idx < num_channels) {
                this->channel = idx;
              } else {
                xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: channel %s not found in channels.conf, defaulting.\n"), channame);
                this->channel = 0;
              }
            }
	  } else {
	    /* just default to channel 0 */
	    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: invalid channel specification, defaulting to last viewed channel.\n"));
                xine_config_lookup_entry(this->class->xine, "media.dvb.remember_channel", &lastchannel);
                if (lastchannel.num_value) {
                  if (xine_config_lookup_entry(this->class->xine, "media.dvb.last_channel", &lastchannel)){
                    this->channel = lastchannel.num_value -1;
                    if (this->channel < 0 || this->channel >= num_channels)
                      this->channel = 0; /* out of range? default */
                  }else{
                    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: invalid channel specification, defaulting to channel 0\n"));
                    this->channel = 0;
                  }
                }
	  }
        }

    } else if (strncasecmp(this->mrl, "dvbs://", 7) == 0) {
	/*
	 * This is dvbs://<channel name>:<qpsk tuning parameters>
	 */
	if (tuner->feinfo.type != FE_QPSK) {
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: dvbs mrl specified but the tuner doesn't appear to be QPSK (DVB-S)\n"));
	  tuner_dispose(tuner);
	  return 0;
	}
	ptr = this->mrl;
	ptr += 7;
	channels = calloc(1, sizeof(channel_t));
	_x_assert(channels != NULL);
	if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0) {
          free(channels);
	  channels = NULL;
	  tuner_dispose(tuner);
	  return 0;
	}
	this->channel = 0;
    } else if (strncasecmp(this->mrl, "dvbt://", 7) == 0) {
	/*
	 * This is dvbt://<channel name>:<ofdm tuning parameters>
	 */
	 if (tuner->feinfo.type != FE_OFDM) {
	   xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: dvbt mrl specified but the tuner doesn't appear to be OFDM (DVB-T)\n"));
	   tuner_dispose(tuner);
	   return 0;
         }
	   ptr = this->mrl;
	   ptr += 7;
	   channels = calloc(1, sizeof(channel_t));
	   _x_assert(channels != NULL);
	   if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0) {
              free(channels);
	      channels = NULL;
              tuner_dispose(tuner);
              return 0;
	   }
	   this->channel = 0;
    } else if (strncasecmp(this->mrl, "dvbc://", 7) == 0)
    {
      /*
       * This is dvbc://<channel name>:<qam tuning parameters>
       */
       if (tuner->feinfo.type != FE_QAM)
       {
         xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	 _("input_dvb: dvbc mrl specified but the tuner doesn't appear to be QAM (DVB-C)\n"));
         tuner_dispose(tuner);
         return 0;
      }
      ptr = this->mrl;
      ptr += 7;
      channels = calloc(1, sizeof(channel_t));
      _x_assert(channels != NULL);
      if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0)
      {
        free(channels);
	channels = NULL;
        tuner_dispose(tuner);
        return 0;
      }
      this->channel = 0;
    } else if (strncasecmp(this->mrl, "dvba://", 7) == 0)
    {
      fprintf(stderr,"input_dvb: 2a %x\n",tuner->feinfo.type);
      /*
       * This is dvba://<channel name>:<atsc tuning parameters>
       */
       if (tuner->feinfo.type != FE_ATSC)
       {
	 fprintf(stderr,"input_dvb: FAILED 1\n");
         xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	 _("input_dvb: dvba mrl specified but the tuner doesn't appear to be ATSC (DVB-A)\n"));
         tuner_dispose(tuner);
         return 0;
      }
      ptr = this->mrl;
      ptr += 7;
      channels = calloc(1, sizeof(channel_t));
      _x_assert(channels != NULL);
      if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0)
      {
	fprintf(stderr,"input_dvb: FAILED 2\n");
        free(channels);
	channels = NULL;
        tuner_dispose(tuner);
        return 0;
      }
      this->channel = 0;
    }else {
	   /* not our mrl */
	   tuner_dispose(tuner);
	   return 0;
    }

    this->tuner = tuner;
    this->channels = channels;
    this->num_channels = num_channels;

    if (!tuner_set_channel(this, &this->channels[this->channel])) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	   _("input_dvb: tuner_set_channel failed\n"));
      return 0;
    }

    if ((this->fd = xine_open_cloexec(this->tuner->dvr_device, O_RDONLY |O_NONBLOCK)) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
             _("input_dvb: cannot open dvr device '%s'\n"), this->tuner->dvr_device);
      return 0;
    }
    this->tuned_in = 1;

    /* now read the pat, find all accociated PIDs and add them to the stream */
    dvb_parse_si(this);

    this->curpos = 0;
    this->osd = NULL;

    pthread_mutex_init(&this->channel_change_mutex, NULL);

    this->event_queue = xine_event_new_queue(this->stream);

#ifdef EPG_UPDATE_IN_BACKGROUND
    if (this->dvb_gui_enabled) {
      /* Start the EPG updater thread. */
      this->epg_updater_stop = 0;
      if (pthread_create(&this->epg_updater_thread, NULL,
		         epg_data_updater, this) != 0) {
	  xprintf(
	      this->class->xine, XINE_VERBOSITY_LOG,
	      _("input_dvb: cannot create EPG updater thread\n"));
	  return 0;
      }
    }
#endif
    /*
     * this osd is used to draw the "recording" sign
     */
    this->rec_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 900, 61);
    this->stream->osd_renderer->set_position(this->rec_osd, 20, 10);
    this->stream->osd_renderer->set_font(this->rec_osd, "cetus", 26);
    this->stream->osd_renderer->set_encoding(this->rec_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->rec_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /*
     * this osd is used to draw the channel switching OSD
     */
    this->channel_osd = this->stream->osd_renderer->new_object(
	this->stream->osd_renderer, CHSEL_WIDTH, CHSEL_HEIGHT);
    this->stream->osd_renderer->set_position(this->channel_osd, 20, 10);
    this->stream->osd_renderer->set_encoding(this->channel_osd, NULL);

    /*
     * this osd is for displaying currently shown channel name
     */
    this->name_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 301, 61);
    this->stream->osd_renderer->set_position(this->name_osd, 20, 10);
    this->stream->osd_renderer->set_font(this->name_osd, "cetus", 40);
    this->stream->osd_renderer->set_encoding(this->name_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->name_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /*
     * this osd is for displaying Recording Paused
     */
    this->paused_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 301, 161);
    this->stream->osd_renderer->set_position(this->paused_osd, 10, 50);
    this->stream->osd_renderer->set_font(this->paused_osd, "cetus", 40);
    this->stream->osd_renderer->set_encoding(this->paused_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->paused_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /*
     * This osd is for displaying Program Information (EIT), i.e., EPG.
     */
    this->proginfo_osd =
	this->stream->osd_renderer->new_object(
	    this->stream->osd_renderer, EPG_WIDTH, EPG_HEIGHT);

    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);
    this->stream->osd_renderer->set_encoding(this->proginfo_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->proginfo_osd, XINE_TEXTPALETTE_WHITE_NONE_TRANSLUCID, OSD_TEXT3);
    this->stream->osd_renderer->set_text_palette(this->proginfo_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT4);

    this->background =
	this->stream->osd_renderer->new_object(
	    this->stream->osd_renderer,
	    EPG_WIDTH + EPG_BACKGROUND_MARGIN*2,
	    EPG_HEIGHT + EPG_BACKGROUND_MARGIN*2);

    this->epg_displaying = 0;
    /* zoom for 4:3 in a 16:9 window */
    config->register_bool(config, "media.dvb.zoom",
				 0,
				 _("use DVB 'center cutout' (zoom)"),
				 _("This will allow fullscreen "
				   "playback of 4:3 content "
				   "transmitted in a 16:9 frame."),
				 0, &dvb_zoom_cb, (void *) this);

    if (xine_config_lookup_entry(this->stream->xine, "media.dvb.zoom", &zoomdvb))
      dvb_zoom_cb((input_plugin_t *) this, &zoomdvb);

    if (xine_config_lookup_entry(this->stream->xine, "media.dvb.remember_channel", &lastchannel))
      if (lastchannel.num_value){
      /* Remember last watched channel. never show this entry*/
        config->update_num(config, "media.dvb.last_channel", this->channel+1);
      }

    /*
     * init metadata (channel title)
     */
    snprintf(str, 256, "%s", this->channels[this->channel].name);

    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, str);

    /* Clear all pids, the pmt will tell us which to use */
    for (x = 0; x < MAX_FILTERS; x++){
      this->channels[this->channel].pid[x] = DVB_NOPID;
    }


    return 1;
}


static input_plugin_t *dvb_class_get_instance (input_class_t *class_gen,
				    xine_stream_t *stream,
				    const char *data) {

  dvb_input_class_t  *class = (dvb_input_class_t *) class_gen;
  dvb_input_plugin_t *this;
  char               *mrl = (char *) data;

  if(strncasecmp (mrl, "dvb://",6))
    if(strncasecmp(mrl,"dvbs://",7))
      if(strncasecmp(mrl,"dvbt://",7))
        if(strncasecmp(mrl,"dvbc://",7))
	  if(strncasecmp(mrl,"dvba://",7))
	    return NULL;

  fprintf(stderr, "input_dvb: continuing in get_instance\n");

  this = calloc(1, sizeof(dvb_input_plugin_t));

  _x_assert(this != NULL);

  this->stream       = stream;
  this->mrl          = strdup(mrl);
  this->class        = class;
  this->tuner        = NULL;
  this->channels     = NULL;
  this->fd           = -1;
  this->tuned_in     = 0;
#ifndef DVB_NO_BUFFERING
  this->nbc          = nbc_init (this->stream);
#else
  this->nbc	= NULL;
#endif
  this->osd          = NULL;
  this->event_queue  = NULL;
  this->record_fd    = -1;
  this->read_failcount = 0;
  this->epg_updater_stop = 0;

  this->input_plugin.open              = dvb_plugin_open;
  this->input_plugin.get_capabilities  = dvb_plugin_get_capabilities;
  this->input_plugin.read              = dvb_plugin_read;
  this->input_plugin.read_block        = dvb_plugin_read_block;
  this->input_plugin.seek              = dvb_plugin_seek;
  this->input_plugin.get_current_pos   = dvb_plugin_get_current_pos;
  this->input_plugin.get_length        = dvb_plugin_get_length;
  this->input_plugin.get_blocksize     = dvb_plugin_get_blocksize;
  this->input_plugin.get_mrl           = dvb_plugin_get_mrl;
  this->input_plugin.get_optional_data = dvb_plugin_get_optional_data;
  this->input_plugin.dispose           = dvb_plugin_dispose;
  this->input_plugin.input_class       = class_gen;

  return &this->input_plugin;
}

/*
 * dvb input plugin class stuff
 */

static void dvb_class_dispose(input_class_t * this_gen)
{
    dvb_input_class_t *class = (dvb_input_class_t *) this_gen;
    int x;

    for(x=0;x<class->numchannels;x++)
       free(class->autoplaylist[x]);

    free(class);
}

static int dvb_class_eject_media (input_class_t *this_gen) {
  return 1;
}


static const char * const *dvb_class_get_autoplay_list(input_class_t * this_gen,
						  int *num_files)
{
    dvb_input_class_t *class = (dvb_input_class_t *) this_gen;
    channel_t *channels=NULL;
    int ch, apch, num_channels = 0;
    int default_channel = -1;
    xine_cfg_entry_t lastchannel_enable = {0};
    xine_cfg_entry_t lastchannel;

    /* need to probe card here to get fe_type to read in channels.conf */
    tuner_t *tuner;
    xine_cfg_entry_t adapter;

    xine_config_lookup_entry(class->xine, "media.dvb.adapter", &adapter);

    if (!(tuner = tuner_init(class->xine,adapter.num_value))) {
       xprintf(class->xine, XINE_VERBOSITY_LOG, _("input_dvb: cannot open dvb device\n"));
       class->mrls[0]="Sorry, No DVB input device found.";
       *num_files=1;
       return class->mrls;
    }

    if (!(channels = load_channels(class->xine, NULL, &num_channels, tuner->feinfo.type))) {
       /* channels.conf not found in .xine */
       class->mrls[0]="Sorry, No valid channels.conf found";
       class->mrls[1]="for the selected DVB device.";
       class->mrls[2]="Please run the dvbscan utility";
       class->mrls[3]="from the dvb drivers apps package";
       class->mrls[4]="and place the file in ~/.xine/";
       *num_files=5;
       tuner_dispose(tuner);
       return class->mrls;
    }

    tuner_dispose(tuner);

    if (xine_config_lookup_entry(class->xine, "media.dvb.remember_channel", &lastchannel_enable)
        && lastchannel_enable.num_value
        && xine_config_lookup_entry(class->xine, "media.dvb.last_channel", &lastchannel))
    {
      default_channel = lastchannel.num_value - 1;
      if (default_channel < 0 || default_channel >= num_channels)
        default_channel = -1;
    }

    for (ch = 0, apch = !!lastchannel_enable.num_value;
         ch < num_channels && ch < MAX_AUTOCHANNELS;
         ++ch, ++apch) {
      free(class->autoplaylist[apch]);
      class->autoplaylist[apch] = _x_asprintf("dvb://%s", channels[ch].name);
      _x_assert(class->autoplaylist[apch] != NULL);
    }

    if (lastchannel_enable.num_value){
      free(class->autoplaylist[0]);
      if (default_channel != -1)
	/* plugin has been used before - channel is valid */
	class->autoplaylist[0] = _x_asprintf("dvb://%s", channels[default_channel].name);
      else
	/* set a reasonable default - the first channel */
	class->autoplaylist[0] = _x_asprintf("dvb://%s", num_channels ? channels[0].name : "0");
    }

    free_channel_list(channels, num_channels);

    *num_files = num_channels + lastchannel_enable.num_value;
    class->numchannels = *num_files;

    return (const char * const *)class->autoplaylist;
}

static void *init_class (xine_t *xine, void *data) {

  dvb_input_class_t  *this;
  config_values_t *config = xine->config;

  this = calloc(1, sizeof (dvb_input_class_t));
  _x_assert(this != NULL);

  this->xine   = xine;

  this->input_class.get_instance       = dvb_class_get_instance;
  this->input_class.identifier         = "dvb";
  this->input_class.description        = N_("DVB (Digital TV) input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = dvb_class_get_autoplay_list;
  this->input_class.dispose            = dvb_class_dispose;
  this->input_class.eject_media        = dvb_class_eject_media;

  this->mrls[0] = "dvb://";
  this->mrls[1] = "dvbs://";
  this->mrls[2] = "dvbc://";
  this->mrls[3] = "dvbt://";
  this->mrls[4] = "dvba://";
  this->mrls[5] = 0;

  this->av_crc = av_crc_get_table(AV_CRC_32_IEEE);

  xprintf(this->xine,XINE_VERBOSITY_DEBUG,"init class succeeded\n");

  /* Enable remembering of last watched channel */
  config->register_bool(config, "media.dvb.remember_channel",
			1,
			_("Remember last DVB channel watched"),
			_("On autoplay, xine will remember and "
			  "switch to the channel indicated in media.dvb.last_channel. "),
			0, NULL, NULL);

  /* Enable remembering of last watched channel never show this entry*/
  config->register_num(config, "media.dvb.last_channel",
		       -1,
		       _("Last DVB channel viewed"),
		       _("If enabled xine will remember and switch to this channel. "),
		       21, NULL, NULL);

  config->register_num(config, "media.dvb.tuning_timeout",
		       0,
		       _("Number of seconds until tuning times out."),
		       _("Leave at 0 means try forever. "
			 "Greater than 0 means wait that many seconds to get a lock. Minimum is 5 seconds."),
		       0, NULL, (void *) this);

  /* set to 0 to turn off the GUI built into this input plugin */
  config->register_bool(config, "media.dvb.gui_enabled",
			1,
			_("Enable the DVB GUI"),
			_("Enable the DVB GUI, mouse controlled recording and channel switching."),
			21, NULL, NULL);

  config->register_num(config, "media.dvb.adapter",
		       0,
		       _("Number of dvb card to use."),
		       _("Leave this at zero unless you "
			 "really have more than 1 card "
			 "in your system."),
		       0, NULL, (void *) this);


  return this;
}


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "DVB", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
