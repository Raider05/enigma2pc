/*
 * Copyright (C) 2000-2014 the xine project,
 *                         Rich Wareham <richwareham@users.sourceforge.net>
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

/* This file was origninally part of the xine-dvdnav project
 * at http://dvd.sf.net/.
 */

/* TODO:
 *
 *  - Proper internationalisation of strings.
 *  - Failure dialogue.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#ifndef WIN32
#include <sys/param.h>
#endif /* WIN32 */

#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#ifndef WIN32
#if ! defined(__GNU__)
#include <sys/mount.h>
#endif
#include <sys/wait.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#endif /* WIN32 */


#if defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_DVDIO_H)
#include <sys/dvdio.h>
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
#elif defined(WIN32)
#include <io.h>                                                 /* read() */
#else
#warning "This might not compile due to missing cdrom ioctls"
#endif

/* DVDNAV includes */
#ifdef HAVE_DVDNAV
#  include <dvdnav/dvdnav.h>
#  ifdef HAVE_DVDNAV_NAVTYPES_H
#    include <dvdnav/nav_types.h>
#    include <dvdnav/nav_read.h>
#  else
#    include <dvdread/nav_types.h>
#    include <dvdread/nav_read.h>
#  endif
#else
#  define DVDNAV_COMPILE
#  include "dvdnav.h"
#  include "nav_read.h"
#endif

/* Xine includes */
#include <xine/xineutils.h>
#include <xine/buffer.h>
#include <xine/xine_internal.h>
#include "media_helper.h"

/* Print debug messages? */
/* #define INPUT_DEBUG */

/* Print trace messages? */
/* #define INPUT_DEBUG_TRACE */

/* Print debug of eject */
/* #define LOG_DVD_EJECT */

/* Current play mode (title only or menus?) */
#define MODE_FAIL	0
#define MODE_NAVIGATE	1
#define MODE_TITLE	2

/* Is seeking enabled? 1 - Yes, 0 - No */
#define CAN_SEEK 1

/* The default DVD device on Solaris is not /dev/dvd */
#if defined(__sun)
#define DVD_PATH "/vol/dev/aliases/cdrom0"
#define RDVD_PATH ""
#elif WIN32
/* There really isn't a default on Windows! */
#define DVD_PATH "d:\\"
#define RDVD_PATH "d:\\"
#elif defined(__OpenBSD__)
#define DVD_PATH "/dev/rcd0c"
#define RDVD_PATH "/dev/rcd0c"
#else
#define DVD_PATH "/dev/dvd"
#define RDVD_PATH "/dev/rdvd"
#endif

/* Some misc. defines */
#ifdef DVD_VIDEO_LB_LEN
#  define DVD_BLOCK_SIZE DVD_VIDEO_LB_LEN
#else
#  define DVD_BLOCK_SIZE 2048
#endif

/* Debugging macros */
#ifdef __GNUC__
# ifdef INPUT_DEBUG_TRACE
#  define trace_print(s, args...) printf("input_dvd: %s : " s, __FUNCTION__, ##args);
# else
#  define trace_print(s, args...) /* Nothing */
# endif
#else
#  ifndef _MSC_VER
#    define trace_print(s, ...) /* Nothing */
#  else
#    ifdef INPUT_DEBUG_TRACE
#      define trace_print printf
#    else
#      define trace_print() /* Nothing */
#    endif /* INPUT_DEBUG_TRACE */
#  endif /* _MSC_VER */
#endif

#if defined (__FreeBSD__)
# define off64_t off_t
# define lseek64 lseek
#endif

static const char *const dvdnav_menu_table[] = {
  NULL,
  NULL,
  "Title",
  "Root",
  "Subpicture",
  "Audio",
  "Angle",
  "Part"
};

typedef struct {
  input_plugin_t    input_plugin; /* Parent input plugin type        */

  xine_stream_t    *stream;
  xine_event_queue_t *event_queue;

  int               pause_timer;  /* Cell still-time timer            */
  int               pause_counter;
  time_t	    pause_end_time;
  int64_t           pg_length;
  int64_t           pgc_length;
  int64_t           cell_start;
  int64_t           pg_start;
  int32_t           buttonN;
  int               typed_buttonN;/* for XINE_EVENT_INPUT_NUMBER_* */

  int32_t           mouse_buttonN;
  int               mouse_in;

  /* Flags */
  int               opened;       /* 1 if the DVD device is already open */
  int               seekable;     /* are we seekable? */
  int               mode;         /* MODE_NAVIGATE / MODE_TITLE */
  int               tt, pr;       /* title / chapter */

  /* xine specific variables */
  const char       *current_dvd_device; /* DVD device currently open */
  char             *mrl;          /* Current MRL                     */
  dvdnav_t         *dvdnav;       /* Handle for libdvdnav            */
  const char       *dvd_name;

  /* special buffer handling for libdvdnav caching */
  pthread_mutex_t   buf_mutex;
  void             *source;
  void            (*free_buffer)(buf_element_t *);
  int               mem_stack;
  int               mem_stack_max;
  unsigned char   **mem;
  int               freeing;
} dvd_input_plugin_t;

typedef struct {

  input_class_t       input_class;

  xine_t             *xine;
  config_values_t    *config;

  char               *dvd_device;    /* default DVD device */
  char               *eject_device;  /* the device last opened is remembered for eject */

  dvd_input_plugin_t *ip;

  int32_t             read_ahead_flag;
  int32_t             seek_mode;
  int32_t             language;
  int32_t             region;
  int32_t             play_single_chapter;

} dvd_input_class_t;

static void dvd_handle_events(dvd_input_plugin_t *this);
static void xine_dvd_send_button_update(dvd_input_plugin_t *this, int mode);

/* Callback on device name change */
static void device_change_cb(void *data, xine_cfg_entry_t *cfg) {
  dvd_input_class_t *class = (dvd_input_class_t *) data;

  class->dvd_device = cfg->str_value;
}

static uint32_t dvd_plugin_get_capabilities (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;

  trace_print("Called\n");

  return INPUT_CAP_BLOCK |
  /* TODO: figure out if there is any "allow copying" flag on DVD.
   *       maybe set INPUT_CAP_RIP_FORBIDDEN only for encrypted media?
   */
    INPUT_CAP_RIP_FORBIDDEN |
#if CAN_SEEK
    (this->seekable ? INPUT_CAP_SEEKABLE : 0) |
#endif
    INPUT_CAP_AUDIOLANG | INPUT_CAP_SPULANG | INPUT_CAP_CHAPTERS;
}

static void read_ahead_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->read_ahead_flag = entry->num_value;

  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_readahead_flag(this->dvdnav, entry->num_value);
  }
}

static void seek_mode_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->seek_mode = entry->num_value;

  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_PGC_positioning_flag(this->dvdnav, !entry->num_value);
  }
}

static void region_changed_cb (void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->region = entry->num_value;

  if(class->ip && ((entry->num_value >= 1) && (entry->num_value <= 8))) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_region_mask(this->dvdnav, 1<<(entry->num_value-1));
  }
}

static void language_changed_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->language = entry->str_value[0] << 8 | entry->str_value[1];

  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_menu_language_select(this->dvdnav, entry->str_value);
    dvdnav_audio_language_select(this->dvdnav, entry->str_value);
    dvdnav_spu_language_select(this->dvdnav, entry->str_value);
  }
}

static void play_single_chapter_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->play_single_chapter = entry->num_value;
}

static void send_mouse_enter_leave_event(dvd_input_plugin_t *this, int direction) {

  if(direction && this->mouse_in)
    this->mouse_in = !this->mouse_in;

  if(direction != this->mouse_in) {
    xine_spu_button_t spu_event = {
      .direction = direction,
      .button    = this->mouse_buttonN
    };
    xine_event_t event;
    event.type        = XINE_EVENT_SPU_BUTTON;
    event.stream      = this->stream;
    event.data        = &spu_event;
    event.data_length = sizeof(spu_event);
    xine_event_send(this->stream, &event);

    this->mouse_in = direction;
  }

  if(!direction)
    this->mouse_buttonN = -1;
}

static int update_title_display(dvd_input_plugin_t *this) {
  xine_ui_data_t data;
  xine_event_t uevent = {
    .type = XINE_EVENT_UI_SET_TITLE,
    .stream = this->stream,
    .data = &data,
    .data_length = sizeof(data)
  };
  int tt=-1, pr=-1;
  int num_tt = 0;

  if(!this || !(this->stream))
   return 0;

  /* Set title/chapter display */

  dvdnav_current_title_info(this->dvdnav, &tt, &pr);
  if( this->mode == MODE_TITLE ) {
    if( (((dvd_input_class_t *)this->input_plugin.input_class)->play_single_chapter ) ) {
      if( (this->tt && this->tt != tt) ||
          (this->pr && this->pr != pr) )
        return 0;
    }
    this->tt = tt;
    this->pr = pr;
  }

  dvdnav_get_number_of_titles(this->dvdnav, &num_tt );


  if(tt >= 1) {
    int num_angle = 0, cur_angle = 0;
    int num_part = 0;
    /* no menu here */
    /* Reflect angle info if appropriate */
    dvdnav_get_number_of_parts(this->dvdnav, tt, &num_part);
    dvdnav_get_angle_info(this->dvdnav, &cur_angle, &num_angle);
    if(num_angle > 1) {
      data.str_len = snprintf(data.str, sizeof(data.str),
			       "Title %i, Chapter %i, Angle %i of %i",
			       tt,pr,cur_angle, num_angle);
       _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_NUMBER,cur_angle);
       _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_COUNT,num_angle);
    } else {
      data.str_len = snprintf(data.str, sizeof(data.str),
			       "Title %i, Chapter %i",
			       tt,pr);
       _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_NUMBER,0);
       _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_COUNT,0);
    }
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_NUMBER,tt);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_COUNT,num_tt);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_NUMBER,pr);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_COUNT,num_part);
  } else if (tt == 0 && dvdnav_menu_table[pr]) {
    data.str_len = snprintf(data.str, sizeof(data.str),
			     "DVD %s Menu",
			     dvdnav_menu_table[pr]);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_NUMBER,tt);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_COUNT,num_tt);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_NUMBER,0);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_COUNT,0);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_NUMBER,0);
     _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_COUNT,0);
  } else {
    strcpy(data.str, "DVD Menu");
    data.str_len = strlen(data.str);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_NUMBER,0);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_TITLE_COUNT,num_tt);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_NUMBER,0);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_CHAPTER_COUNT,0);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_NUMBER,0);
    _x_stream_info_set(this->stream,XINE_STREAM_INFO_DVD_ANGLE_COUNT,0);
  }

  if (this->dvd_name && this->dvd_name[0] &&
      (data.str_len + strlen(this->dvd_name) < sizeof(data.str))) {
    data.str_len += snprintf(data.str+data.str_len, sizeof(data.str) - data.str_len,
			      ", %s", this->dvd_name);
  }
#ifdef INPUT_DEBUG
  printf("input_dvd: Changing title to read '%s'\n", data.str);
#endif
  xine_event_send(this->stream, &uevent);

  return 1;
}

static void dvd_plugin_dispose (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;

  trace_print("Called\n");

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  ((dvd_input_class_t *)this_gen->input_class)->ip = NULL;
  if (this->dvdnav)
    dvdnav_close(this->dvdnav);

  pthread_mutex_lock(&this->buf_mutex);
  if (this->mem_stack) {
    /* raise the freeing flag, so that the plugin will be freed as soon
     * as all buffers have returned to the libdvdnav read ahead cache */
    this->freeing = 1;
    pthread_mutex_unlock(&this->buf_mutex);
  } else {
    pthread_mutex_unlock(&this->buf_mutex);
    pthread_mutex_destroy(&this->buf_mutex);
    free(this->mem);
    free(this->mrl);
    free(this);
  }
}


/* Align pointer |p| to alignment |align| */
#define	PTR_ALIGN(p, align)	((void*) (((long)(p) + (align) - 1) & ~((align)-1)) )


/* FIXME */
#if 0
static void dvd_build_mrl_list(dvd_input_plugin_t *this) {
  int num_titles, *num_parts;

  /* skip DVD if already open */
  if (this->opened) return;
  if (this->class->mrls) {
    free(this->class->mrls);
    this->class->mrls = NULL;
    this->class->num_mrls = 0;
  }

  if (dvdnav_open(&(this->dvdnav),
		  this->dvd_device) == DVDNAV_STATUS_ERR) {
    return;
  }

  this->current_dvd_device = this->dvd_device;
  this->opened = 1;

  dvdnav_get_number_of_titles(this->dvdnav, &num_titles);
  if ((num_parts = (int *) calloc(num_titles, sizeof(int)))) {
    struct xine_mrl_align_s {
      char dummy;
      xine_mrl_t mrl;
    };
    int xine_mrl_alignment = offsetof(struct xine_mrl_align_s, mrl);
    int num_mrls = 1, i;
    /* for each title, count the number of parts */
    for (i = 1; i <= num_titles; i++) {
      num_parts[i-1] = 0;
      /* dvdnav_title_play(this->dvdnav, i); */
      dvdnav_get_number_of_parts(this->dvdnav, i, &num_parts[i-1]);
      num_mrls += num_parts[i-1]; /* num_mrls = total number of programs */
    }

    /* allocate enough memory for:
     * - a list of pointers to mrls       sizeof(xine_mrl_t *)     * (num_mrls+1)
     * - possible alignment of the mrl array
     * - an array of mrl structures       sizeof(xine_mrl_t)       * num_mrls
     * - enough chars for every filename  sizeof(char)*25     * num_mrls
     *   - "dvd:/000000.000000\0" = 25 chars
     */
    if ((this->mrls = (xine_mrl_t **) malloc(sizeof(xine_mrl_t *) + num_mrls *
	(sizeof(xine_mrl_t*) + sizeof(xine_mrl_t) + 25*sizeof(char)) +
	xine_mrl_alignment))) {

      /* the first mrl struct comes after the pointer list */
      xine_mrl_t *mrl = PTR_ALIGN(&this->mrls[num_mrls+1], xine_mrl_alignment);

      /* the chars for filenames come after the mrl structs */
      char *name = (char *) &mrl[num_mrls];
      int pos = 0, j;
      this->num_mrls = num_mrls;

      for (i = 1; i <= num_titles; i++) {
	for (j = (i == 1 ? 0 : 1); j <= num_parts[i-1]; j++) {
	  this->class->mrls[pos++] = mrl;
	  mrl->origin = NULL;
	  mrl->mrl = name;
	  mrl->link = NULL;
	  mrl->type = mrl_dvd;
	  mrl->size = 0;
	  snprintf(name, 25, (j == 0) ? "dvd:/" :
		             (j == 1) ? "dvd:/%d" :
		                        "dvd:/%d.%d", i, j);
	  name = &name[25];
	  mrl++;
	}
      }
      this->class->mrls[pos] = NULL; /* terminate list */
    }
    free(num_parts);
  }
}
#endif

static void dvd_plugin_free_buffer(buf_element_t *buf) {
  dvd_input_plugin_t *this = buf->source;

  pthread_mutex_lock(&this->buf_mutex);
  /* give this buffer back to libdvdnav */
  dvdnav_free_cache_block(this->dvdnav, buf->mem);
  /* reconstruct the original xine buffer */
  buf->free_buffer = this->free_buffer;
  buf->source = this->source;
  buf->mem = this->mem[--this->mem_stack];
  pthread_mutex_unlock(&this->buf_mutex);
  /* give this buffer back to xine's pool */
  buf->free_buffer(buf);
  if (this->freeing && !this->mem_stack) {
    /* all buffers returned, we can free the plugin now */
    pthread_mutex_destroy(&this->buf_mutex);
    free(this->mem);
    free(this->mrl);
    free(this);
  }
}

static buf_element_t *dvd_plugin_read_block (input_plugin_t *this_gen,
						fifo_buffer_t *fifo, off_t nlen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  buf_element_t      *buf;
  dvdnav_status_t     result;
  int                 event, len;
  int                 finished = 0;
  unsigned char      *block;

  if(fifo == NULL) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("input_dvd: values of \\beta will give rise to dom!\n"));
    return NULL;
  }

  /* Read buffer */
  buf = fifo->buffer_pool_alloc (fifo);
  block = buf->mem;

  while(!finished) {
    dvd_handle_events(this);

    if (block != buf->mem) {
      /* if we already have a dvdnav cache block, give it back first */
      dvdnav_free_cache_block(this->dvdnav, block);
      block = buf->mem;
    }
    result = dvdnav_get_next_cache_block (this->dvdnav, &block, &event, &len);
    if(result == DVDNAV_STATUS_ERR) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("input_dvd: Error getting next block from DVD (%s)\n"), dvdnav_err_to_string(this->dvdnav));
      _x_message(this->stream, XINE_MSG_READ_ERROR,
                   dvdnav_err_to_string(this->dvdnav), NULL);
      if (block != buf->mem) dvdnav_free_cache_block(this->dvdnav, block);
      buf->free_buffer(buf);
      return NULL;
    }

    switch(event) {
    case DVDNAV_BLOCK_OK:
      {
	buf->content = block;
	buf->type = BUF_DEMUX_BLOCK;

	/* Make sure we don't think we are still paused */
	this->pause_timer = 0;

	/* we got a block, so we might be seekable here */
	this->seekable = 1;

	finished = 1;
      }
      break;
    case DVDNAV_NOP:
      break;
    case DVDNAV_STILL_FRAME:
      {
        dvdnav_still_event_t *still_event =
          (dvdnav_still_event_t*)block;
        buf->type = BUF_CONTROL_NOP;
        finished = 1;

        /* stills are not seekable */
        this->seekable = 0;

        /* Xine's method of doing still-frames */
        if (this->pause_timer == 0) {
#ifdef INPUT_DEBUG
          printf("input_dvd: Stillframe! (pause time = 0x%02x)\n",
                 still_event->length);
#endif
          this->pause_timer = still_event->length;
          this->pause_end_time = time(NULL) + this->pause_timer;
          this->pause_counter = 0;
          break;
        }

        if(this->pause_timer == 0xff) {
          this->pause_counter++;
          xine_usec_sleep(50000);
          break;
        }
        if ((this->pause_timer != 0xff) &&
            (time(NULL) >= this->pause_end_time)) {
          this->pause_timer = 0;
          this->pause_end_time = 0;
          dvdnav_still_skip(this->dvdnav);
          break;
        }
        if(this->pause_timer) {
          this->pause_counter++;
#ifdef INPUT_DEBUG
          printf("input_dvd: Stillframe! (pause_timer = 0x%02x) counter=%d\n",
                 still_event->length, this->pause_counter);
#endif
          xine_usec_sleep(50000);
          break;
        }
      }
      break;
    case DVDNAV_SPU_STREAM_CHANGE:
      {
	dvdnav_spu_stream_change_event_t *stream_event =
	  (dvdnav_spu_stream_change_event_t*) (block);
        buf->content = block;
        buf->type = BUF_CONTROL_SPU_CHANNEL;
        buf->decoder_info[0] = stream_event->physical_wide;
	buf->decoder_info[1] = stream_event->physical_letterbox;
	buf->decoder_info[2] = stream_event->physical_pan_scan;
#ifdef INPUT_DEBUG
	printf("input_dvd: SPU stream wide %d, letterbox %d, pan&scan %d\n",
	  stream_event->physical_wide,
	  stream_event->physical_letterbox,
	  stream_event->physical_pan_scan);
#endif
	finished = 1;
      }
      break;
    case DVDNAV_AUDIO_STREAM_CHANGE:
      {
	dvdnav_audio_stream_change_event_t *stream_event =
	 (dvdnav_audio_stream_change_event_t*) (block);
        buf->content = block;
        buf->type = BUF_CONTROL_AUDIO_CHANNEL;
        buf->decoder_info[0] = stream_event->physical;
#ifdef INPUT_DEBUG
	printf("input_dvd: AUDIO stream %d\n", stream_event->physical);
#endif
	finished = 1;
      }
      break;
    case DVDNAV_HIGHLIGHT:
      xine_dvd_send_button_update(this, 0);
      break;
    case DVDNAV_VTS_CHANGE:
      {
	int aspect, permission;
#ifdef INPUT_DEBUG
	printf("input_dvd: VTS change\n");
#endif
	/* Check for video aspect change and scaling permissions */
	aspect = dvdnav_get_video_aspect(this->dvdnav);
	permission = dvdnav_get_video_scale_permission(this->dvdnav);

	buf->type = BUF_VIDEO_MPEG;
	buf->decoder_flags = BUF_FLAG_SPECIAL;
	buf->decoder_info[1] = BUF_SPECIAL_ASPECT;
	buf->decoder_info[2] = aspect;
	buf->decoder_info[3] = permission;
	finished = 1;
      }
      break;
    case DVDNAV_CELL_CHANGE:
      {
	dvdnav_cell_change_event_t *cell_event =
	 (dvdnav_cell_change_event_t*) (block);

	/* Tell xine to update the UI */
        xine_event_t event = {
	  .type = XINE_EVENT_UI_CHANNELS_CHANGED,
	  .stream = this->stream,
	  .data = NULL,
	  .data_length = 0
	};
	xine_event_send(this->stream, &event);

	if( !update_title_display(this) ) {
	  if (buf->mem != block) dvdnav_free_cache_block(this->dvdnav, block);
	  buf->free_buffer(buf);
	  /* return NULL to indicate end of stream */
	  return NULL;
        }

	this->pg_length  = cell_event->pg_length;
	this->pgc_length = cell_event->pgc_length;
	this->cell_start = cell_event->cell_start;
	this->pg_start   = cell_event->pg_start;
      }
      break;
    case DVDNAV_HOP_CHANNEL:
      _x_demux_flush_engine(this->stream);
      break;
    case DVDNAV_NAV_PACKET:
      {
	buf->content = block;
	buf->type = BUF_DEMUX_BLOCK;
	finished = 1;
      }
      break;
    case DVDNAV_SPU_CLUT_CHANGE:
      {
	buf->content = block;
	buf->type = BUF_SPU_DVD;
	buf->decoder_flags |= BUF_FLAG_SPECIAL;
	buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
	buf->decoder_info[2] = SPU_DVD_SUBTYPE_CLUT;
	finished = 1;
      }
      break;
    case DVDNAV_STOP:
      {
	if (buf->mem != block) dvdnav_free_cache_block(this->dvdnav, block);
	buf->free_buffer(buf);
	/* return NULL to indicate end of stream */
	return NULL;
      }
    case DVDNAV_WAIT:
      {
	int buffers = this->stream->video_fifo->size(this->stream->video_fifo);
	if (this->stream->audio_fifo)
	  buffers += this->stream->audio_fifo->size(this->stream->audio_fifo);
	/* we wait until the fifos are empty, ... well, we allow one remaining buffer,
	 * because a flush might be in progress. */
	if (buffers <= 1)
	  dvdnav_wait_skip(this->dvdnav);
	else
	  xine_usec_sleep(50000);
      }
      break;
    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: FIXME: Unknown event (%i)\n", event);
      break;
    }
  }

  if (block != buf->mem) {
    /* we have received a buffer from the libdvdnav cache, store all
     * necessary values to reconstruct xine's buffer and modify it according to
     * our needs. */
    pthread_mutex_lock(&this->buf_mutex);
    if (this->mem_stack >= this->mem_stack_max) {
      void *mem;

      mem = realloc(this->mem, sizeof(unsigned char *) * (this->mem_stack_max + 1024));
      if (mem) {
        this->mem_stack_max += 1024;
        this->mem = mem;
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: Memory stack increased to %d entries.\n", this->mem_stack_max);
      } else {
        xprintf(this->stream->xine, XINE_VERBOSITY_NONE, "%s: realloc() failed: %s.\n",
          __XINE_FUNCTION__, strerror(errno));
      }
    }
    if (this->mem_stack < this->mem_stack_max) {
      this->mem[this->mem_stack++] = buf->mem;
      this->free_buffer = buf->free_buffer;
      this->source = buf->source;
      buf->mem = block;
      buf->free_buffer = dvd_plugin_free_buffer;
      buf->source = this;
    } else {
      /* the stack for storing the memory chunks from xine is full, we cannot
       * modify the buffer, because we would not be able to reconstruct it.
       * Therefore we copy the data and give the buffer back. */
      memcpy(buf->mem, block, DVD_BLOCK_SIZE);
      dvdnav_free_cache_block(this->dvdnav, block);
      buf->content = buf->mem;
    }
    pthread_mutex_unlock(&this->buf_mutex);
  }

  if (this->pg_length && this->pgc_length) {
    switch (((dvd_input_class_t *)this->input_plugin.input_class)->seek_mode) {
    case 0: /* PGC based seeking */
      buf->extra_info->total_time = this->pgc_length / 90;
      buf->extra_info->input_time = this->cell_start / 90;
      break;
    case 1: /* PG based seeking */
      buf->extra_info->total_time = this->pg_length  / 90;
      buf->extra_info->input_time = (this->cell_start - this->pg_start) / 90;
      break;
    }
  }

  return buf;
}

static off_t dvd_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len) {
/*  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen; */
  char *ch_buf = (char *)buf_gen;

  if (len < 4)
    return -1;

  /* FIXME: Tricking the demux_mpeg_block plugin */
  ch_buf[0] = 0;
  ch_buf[1] = 0;
  ch_buf[2] = 0x01;
  ch_buf[3] = 0xba;
  return 1;
}

static off_t dvd_plugin_get_current_pos (input_plugin_t *this_gen){
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  uint32_t pos=0;
  uint32_t length=1;
  /*dvdnav_status_t result;*/
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return 0;
  }
  /*result =*/ dvdnav_get_position(this->dvdnav, &pos, &length);
  return (off_t)pos * (off_t)DVD_BLOCK_SIZE;
}

static off_t dvd_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;

  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return -1;
  }

  dvdnav_sector_search(this->dvdnav, offset / DVD_BLOCK_SIZE , origin);
  return dvd_plugin_get_current_pos(this_gen);
}

static off_t dvd_plugin_seek_time (input_plugin_t *this_gen, int time_offset, int origin) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;

  trace_print("Called\n");

  if(!this || !this->dvdnav || origin != SEEK_SET) {
    return -1;
  }

  dvdnav_time_search(this->dvdnav, time_offset * 90);

  return dvd_plugin_get_current_pos(this_gen);
}

static off_t dvd_plugin_get_length (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  uint32_t pos=0;
  uint32_t length=1;
  /*dvdnav_status_t result;*/

  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return 0;
  }
  /*result =*/ dvdnav_get_position(this->dvdnav, &pos, &length);
  return (off_t)length * (off_t)DVD_BLOCK_SIZE;
}

static uint32_t dvd_plugin_get_blocksize (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return DVD_BLOCK_SIZE;
}

static const char* dvd_plugin_get_mrl (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;

  trace_print("Called\n");

  return this->mrl;
}

static void xine_dvd_send_button_update(dvd_input_plugin_t *this, int mode) {
  int32_t button;
  int32_t show;

  if (!this || !this->stream || _x_stream_info_get(this->stream,XINE_STREAM_INFO_IGNORE_SPU))
    return;

  if (!this->stream->spu_decoder_plugin ||
      this->stream->spu_decoder_streamtype != ((BUF_SPU_DVD >> 16) & 0xFF)) {
    /* the proper SPU decoder has not been initialized yet,
     * so we send a dummy buffer to trigger this */
    buf_element_t *buf = this->stream->video_fifo->buffer_pool_alloc(this->stream->video_fifo);

    buf->size = 0;
    buf->type = BUF_SPU_DVD;
    this->stream->video_fifo->insert(this->stream->video_fifo, buf);

    while (!this->stream->spu_decoder_plugin ||
	this->stream->spu_decoder_streamtype != ((BUF_SPU_DVD >> 16) & 0xFF))
      xine_usec_sleep(50000);
  }

  dvdnav_get_current_highlight(this->dvdnav, &button);

  if (button == this->buttonN && (mode == 0) ) return;

  this->buttonN = button; /* Avoid duplicate sending of button info */

#ifdef INPUT_DEBUG
  printf("input_dvd: sending_button_update button=%d mode=%d\n", button, mode);
#endif
  /* Do we want to show or hide the button? */
  /* libspudec will control hiding */
  show = mode + 1; /* mode=0 select, 1 activate. */
  this->stream->spu_decoder_plugin->set_button (this->stream->spu_decoder_plugin, button, show);
}

static void dvd_handle_events(dvd_input_plugin_t *this) {

  dvd_input_class_t  *class = (dvd_input_class_t*)this->input_plugin.input_class;
  config_values_t  *config = class->config;       /* Pointer to XineRC config file   */
  xine_event_t *event;

  while ((event = xine_event_get(this->event_queue))) {

    if(!this->dvdnav) {
      xine_event_free(event);
      return;
    }

    switch(event->type) {
    case XINE_EVENT_INPUT_MENU1:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU1 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Escape);
      break;
    case XINE_EVENT_INPUT_MENU2:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU2 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Title);
      break;
    case XINE_EVENT_INPUT_MENU3:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU3 key hit.\n");
      if (dvdnav_menu_call(this->dvdnav, DVD_MENU_Root) == DVDNAV_STATUS_ERR)
        dvdnav_menu_call(this->dvdnav, DVD_MENU_Title);
      break;
    case XINE_EVENT_INPUT_MENU4:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU4 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Subpicture);
      break;
    case XINE_EVENT_INPUT_MENU5:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU5 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Audio);
      break;
    case XINE_EVENT_INPUT_MENU6:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU6 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Angle);
      break;
    case XINE_EVENT_INPUT_MENU7:
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_dvd: MENU7 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Part);
      break;
    case XINE_EVENT_INPUT_NEXT:
      {
        cfg_entry_t* entry = config->lookup_entry(config, "media.dvd.skip_behaviour");
	int title = 0, part = 0;
	switch (entry->num_value) {
	case 0: /* skip by program */
	  dvdnav_next_pg_search(this->dvdnav);
	  break;
	case 1: /* skip by part */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, title, ++part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, ++title, 1);
	  break;
	}
      }
      break;
    case XINE_EVENT_INPUT_PREVIOUS:
      {
        cfg_entry_t *entry = config->lookup_entry(config, "media.dvd.skip_behaviour");
	int title = 0, part = 0;
	switch (entry->num_value) {
	case 0: /* skip by program */
	  dvdnav_prev_pg_search(this->dvdnav);
	  break;
	case 1: /* skip by part */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, title, --part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, --title, 1);
	  break;
	}
      }
      break;
    case XINE_EVENT_INPUT_ANGLE_NEXT:
      {
        int num = 0, current = 0;
        dvdnav_get_angle_info(this->dvdnav, &current, &num);

        if(num != 0) {
          current ++;
          if(current > num)
            current = 1;
        }
        dvdnav_angle_change(this->dvdnav, current);
#ifdef INPUT_DEBUG
        printf("input_dvd: Changing to angle %i\n", current);
#endif
        update_title_display(this);
      }
      break;
    case XINE_EVENT_INPUT_ANGLE_PREVIOUS:
      {
        int num = 0, current = 0;
        dvdnav_get_angle_info(this->dvdnav, &current, &num);

        if(num != 0) {
          current --;
          if(current <= 0)
            current = num;
        }
        dvdnav_angle_change(this->dvdnav, current);
#ifdef INPUT_DEBUG
        printf("input_dvd: Changing to angle %i\n", current);
#endif
        update_title_display(this);
      }
      break;
    case XINE_EVENT_INPUT_SELECT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
          if (dvdnav_button_activate(this->dvdnav, &nav_pci) == DVDNAV_STATUS_OK)
            xine_dvd_send_button_update(this, 1);
        }
      }
      break;
    case XINE_EVENT_INPUT_MOUSE_BUTTON:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  xine_input_data_t *input = event->data;
          if((input->button == 1) && dvdnav_mouse_activate(this->dvdnav,
							   &nav_pci, input->x, input->y) == DVDNAV_STATUS_OK) {
            xine_dvd_send_button_update(this, 1);

	    if(this->mouse_in)
	      send_mouse_enter_leave_event(this, 0);

	    this->mouse_buttonN = -1;

	  }
        }
      }
      break;
    case XINE_EVENT_INPUT_BUTTON_FORCE:  /* For libspudec to feedback forced button select from NAV PCI packets. */
      {
        pci_t nav_pci;
        int *but = event->data;
#ifdef INPUT_DEBUG
        printf("input_dvd: BUTTON_FORCE %d\n", *but);
#endif
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) )
          dvdnav_button_select(this->dvdnav, &nav_pci, *but);
      }
      break;
    case XINE_EVENT_INPUT_MOUSE_MOVE:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  xine_input_data_t *input = event->data;
	  /* printf("input_dvd: Mouse move (x,y) = (%i,%i)\n", input->x, input->y); */
	  if(dvdnav_mouse_select(this->dvdnav, &nav_pci, input->x, input->y) == DVDNAV_STATUS_OK) {
	    int32_t button;

	    dvdnav_get_current_highlight(this->dvdnav, &button);

	    if(this->mouse_buttonN != button) {
	      this->mouse_buttonN = button;
	      send_mouse_enter_leave_event(this, 1);
	    }

	  }
	  else {
	    if(this->mouse_in)
	      send_mouse_enter_leave_event(this, 0);

	  }
        }
      }
      break;
    case XINE_EVENT_INPUT_UP:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_upper_button_select(this->dvdnav, &nav_pci);

	  if(this->mouse_in)
	    send_mouse_enter_leave_event(this, 0);

	}
        break;
      }
    case XINE_EVENT_INPUT_DOWN:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_lower_button_select(this->dvdnav, &nav_pci);

	  if(this->mouse_in)
	    send_mouse_enter_leave_event(this, 0);

	}
        break;
      }
    case XINE_EVENT_INPUT_LEFT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_left_button_select(this->dvdnav, &nav_pci);

	  if(this->mouse_in)
	    send_mouse_enter_leave_event(this, 0);

	}
        break;
      }
    case XINE_EVENT_INPUT_RIGHT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_right_button_select(this->dvdnav, &nav_pci);

	  if(this->mouse_in)
	    send_mouse_enter_leave_event(this, 0);

	}
        break;
      }
    case XINE_EVENT_INPUT_NUMBER_9:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_8:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_7:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_6:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_5:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_4:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_3:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_2:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_1:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_0:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_interact_info(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  if (dvdnav_button_select_and_activate(this->dvdnav, &nav_pci, this->typed_buttonN) == DVDNAV_STATUS_OK) {
            xine_dvd_send_button_update(this, 1);

	    if(this->mouse_in)
	      send_mouse_enter_leave_event(this, 0);
	  }

          this->typed_buttonN = 0;
        }
        break;
      }
    case XINE_EVENT_INPUT_NUMBER_10_ADD:
      this->typed_buttonN += 10;
    }

    xine_event_free(event);
  }
  return;
}

static int dvd_plugin_get_optional_data (input_plugin_t *this_gen,
					    void *data, int data_type) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  switch(data_type) {

  case INPUT_OPTIONAL_DATA_AUDIOLANG: {
    uint16_t lang;
    int      channel = *((int *)data);
    int8_t   dvd_channel;

    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	strcpy(data, "menu");
	if (channel <= 0)
	  return INPUT_OPTIONAL_SUCCESS;
	else
	  return INPUT_OPTIONAL_UNSUPPORTED;
      }

      if (channel == -1)
        dvd_channel = dvdnav_get_audio_logical_stream(this->dvdnav, this->stream->audio_channel_auto);
      else
        dvd_channel = dvdnav_get_audio_logical_stream(this->dvdnav, channel);

      if(dvd_channel != -1) {
	lang = dvdnav_audio_stream_to_lang(this->dvdnav, dvd_channel);

	if(lang != 0xffff)
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	  /* TODO: provide long version in XINE_META_INFO_FULL_LANG */
	else
	  strcpy(data, " ??");
	return INPUT_OPTIONAL_SUCCESS;
      } else {
        if (channel == -1) {
	  strcpy(data, "none");
	  return INPUT_OPTIONAL_SUCCESS;
	}
      }
    }
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;


  case INPUT_OPTIONAL_DATA_SPULANG: {
    uint16_t lang;
    int      channel = *((int *)data);
    int8_t   dvd_channel;

    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	strcpy(data, "menu");
	if (channel <= 0)
	  return INPUT_OPTIONAL_SUCCESS;
	else
	  return INPUT_OPTIONAL_UNSUPPORTED;
      }

      if(channel == -1)
	dvd_channel = dvdnav_get_spu_logical_stream(this->dvdnav, this->stream->spu_channel_auto);
      else
	dvd_channel = dvdnav_get_spu_logical_stream(this->dvdnav, channel);

      if(dvd_channel != -1) {
	lang = dvdnav_spu_stream_to_lang(this->dvdnav, dvd_channel);

	if(lang != 0xffff)
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	  /* TODO: provide long version in XINE_META_INFO_FULL_LANG */
	else
	  sprintf(data, " %c%c", '?', '?');
	return INPUT_OPTIONAL_SUCCESS;
      } else {
	if(channel == -1) {
	  strcpy(data, "none");
	  return INPUT_OPTIONAL_SUCCESS;
	}
      }
    }
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;

  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

#ifdef	__sun
/*
 * Check the environment, if we're running under sun's
 * vold/rmmount control.
 */
static void
check_solaris_vold_device(dvd_input_class_t *this)
{
  char *volume_device;
  char *volume_name;
  char *volume_action;
  char *device;
  struct stat stb;

  if ((volume_device = getenv("VOLUME_DEVICE")) != NULL &&
      (volume_name   = getenv("VOLUME_NAME"))   != NULL &&
      (volume_action = getenv("VOLUME_ACTION")) != NULL &&
      strcmp(volume_action, "insert") == 0) {

    device = _x_asprintf("%s/%s", volume_device, volume_name);
    if (!device || stat(device, &stb) != 0 || !S_ISCHR(stb.st_mode)) {
      free(device);
      return;
    }
    this->dvd_device = device;
  }
}
#endif


static int dvd_parse_try_open(dvd_input_plugin_t *this, const char *locator)
{
  const char *intended_dvd_device;

  /* FIXME: we temporarily special-case "dvd:/" for compatibility;
   * actually "dvd:/" should play a DVD image stored in /, but for
   * now we have it use the default device */
#if 0
  if (strlen(locator)) {
#else
  if (strlen(locator) && !(locator[0] == '/' && locator[1] == '\0')) {
#endif
    /* we have an alternative dvd_path */
    intended_dvd_device = locator;
    /* do not use the raw device for the alternative */
    xine_setenv("DVDCSS_RAW_DEVICE", "", 1);
  } else {
    /* use default DVD device */
    dvd_input_class_t *class = (dvd_input_class_t*)this->input_plugin.input_class;
    xine_cfg_entry_t raw_device;
    if (xine_config_lookup_entry(this->stream->xine,
	"media.dvd.raw_device", &raw_device))
      xine_setenv("DVDCSS_RAW_DEVICE", raw_device.str_value, 1);
    intended_dvd_device = class->dvd_device;
  }

  /* attempt to open DVD */
  if (this->opened) {
    if (intended_dvd_device == this->current_dvd_device) {
      /* Already open, so skip opening */
      dvdnav_reset(this->dvdnav);
    } else {
      /* Changing DVD device */
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      this->opened = 0;
    }
  }
  if (!this->opened) {
    if (dvdnav_open(&this->dvdnav, intended_dvd_device) == DVDNAV_STATUS_OK) {
      this->opened = 1;
      this->current_dvd_device = intended_dvd_device;
    }
  }

  return this->opened;
}

static int dvd_parse_mrl(dvd_input_plugin_t *this, char **locator, char **title_part)
{
  *title_part      = NULL;

  if (dvd_parse_try_open(this, *locator)) {
    return MODE_NAVIGATE;
  } else {
    /* opening failed, but we can still try cutting off <title>.<part> */
    char *last_slash;
    for (last_slash = *locator + strlen(*locator) - 1; last_slash >= *locator; last_slash--) {
      if (*last_slash == '.') continue;
      if (*last_slash >= '0' && *last_slash <= '9') continue;
      break;
    }
    if (last_slash > *locator && *last_slash == '/') {
      *title_part = last_slash + 1;
      *last_slash = '\0';
    } else if (last_slash == *locator && *last_slash == '/') {
      /* we must never delete the very first slash, since this will turn an
       * absolute into a relative URL and overthrow further opening */
      *title_part = last_slash + 1;
      *locator = "/";
    } else if (last_slash < *locator) {
      /* there could be a dvd:<title>.<part> MRL without any path */
      *title_part = *locator;
      *locator = "";
    } else
      return MODE_FAIL;

    if (dvd_parse_try_open(this, *locator))
      if (strlen(*title_part))
	return MODE_TITLE;
      else
	return MODE_NAVIGATE;
    else
      return MODE_FAIL;
  }
}

static int dvd_plugin_open (input_plugin_t *this_gen) {
  dvd_input_plugin_t    *this = (dvd_input_plugin_t*)this_gen;
  dvd_input_class_t     *class = (dvd_input_class_t*)this_gen->input_class;

  char                  *locator, *locator_orig;
  char                  *title_part;
  xine_cfg_entry_t       region_entry, lang_entry, cfg_entry;

  trace_print("Called\n");

  /* we already checked the "dvd:/" MRL before */
  locator_orig = locator = strdup (this->mrl + (sizeof("dvd:") - 1));

  /* FIXME: call a generic xine-lib MRL parser here to pre-parse
   * the MRL for ?title=<title>&part=<part> stuff and to expand
   * escaped characters properly */

  _x_mrl_unescape (locator);

  this->mode = dvd_parse_mrl(this, &locator, &title_part);

  if (this->mode == MODE_FAIL) {
    /* opening failed and we have nothing left to try */
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("input_dvd: Error opening DVD device\n"));
    _x_message(this->stream, XINE_MSG_READ_ERROR,
      /* FIXME: see FIXME in dvd_parse_try_open() */
      (strlen(locator) && !(locator[0] == '/' && locator[1] == '\0')) ? locator : class->dvd_device, NULL);
    free (locator_orig);
    return 0;
  }

  dvdnav_get_title_string(this->dvdnav, &this->dvd_name);
  if(this->dvd_name)
    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->dvd_name);

  /* Set region code */
  if (xine_config_lookup_entry (this->stream->xine, "media.dvd.region",
				&region_entry))
    region_changed_cb (class, &region_entry);

  /* Set languages */
  if (xine_config_lookup_entry (this->stream->xine, "media.dvd.language",
				&lang_entry))
    language_changed_cb (class, &lang_entry);

  /* Set cache usage */
  if (xine_config_lookup_entry(this->stream->xine, "media.dvd.readahead",
			       &cfg_entry))
    read_ahead_cb(class, &cfg_entry);

  /* Set seek mode */
  if (xine_config_lookup_entry(this->stream->xine, "media.dvd.seek_behaviour",
			       &cfg_entry))
    seek_mode_cb(class, &cfg_entry);

  /* Set single chapter mode */
  if (xine_config_lookup_entry(this->stream->xine, "media.dvd.play_single_chapter",
			       &cfg_entry))
    play_single_chapter_cb(class, &cfg_entry);

  if (this->mode == MODE_TITLE) {
    char *delimiter;
    int tt, pr;
    int titles, parts;

    /* a <title>.<part> was specified -> resume parsing */

    /* See if there is a period. */
    delimiter = strchr(title_part, '.');
    if (delimiter) *delimiter = '\0';

    tt = strtol(title_part, NULL, 10);
    dvdnav_get_number_of_titles(this->dvdnav, &titles);
    if((tt < 0) || (tt > titles)) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_dvd: Title %i is out of range (1 to %i).\n", tt, titles);
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      free (locator_orig);
      return 0;
    }

    /* If there was a part specified, get that too. */
    pr = -1;
    if(delimiter) {
      pr = strtol(delimiter+1, NULL, 10);
      dvdnav_get_number_of_parts(this->dvdnav, tt, &parts);
      if ((pr < 0) || (pr > parts)) {
	xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"input_dvd: Part %i is out of range (1 to %i).\n", pr, parts);
	dvdnav_close(this->dvdnav);
	this->dvdnav = NULL;
	free (locator_orig);
	return 0;
      }
    }

#ifdef INPUT_DEBUG
    printf("input_dvd: Jumping to TT >%i<, PTT >%i<\n", tt, pr);
#endif
    if (tt > 0) {
      if (pr > 0)
        dvdnav_part_play(this->dvdnav, tt, pr);
      else
        dvdnav_title_play(this->dvdnav, tt);
    } else
      this->mode = MODE_NAVIGATE;
  }
#ifdef INPUT_DEBUG
  printf("input_dvd: DVD device successfully opened.\n");
#endif

  /* remember the last successfully opened device for ejecting */
  free(class->eject_device);
  class->eject_device = strdup(this->current_dvd_device);

  { /* Tell Xine to update the UI */
    const xine_event_t event = {
      .type = XINE_EVENT_UI_CHANNELS_CHANGED,
      .stream = this->stream,
      .data = NULL,
      .data_length = 0
    };
    xine_event_send(this->stream, &event);
  }

  update_title_display(this);

  free (locator_orig);
  return 1;
}

/* dvdnav CLASS functions */

/*
 * Opens the DVD plugin. The MRL takes the following form:
 *
 * dvd:[dvd_path]/[vts[.program]]
 *
 * e.g.
 *   dvd:/                    - Play (navigate)
 *   dvd:/1                   - Play Title 1
 *   dvd:/1.3                 - Play Title 1, program 3
 *   dvd:/dev/dvd2/           - Play (navigate) from /dev/dvd2
 *   dvd:/dev/dvd2/1.3        - Play Title 1, program 3 from /dev/dvd2
 */
static input_plugin_t *dvd_class_get_instance (input_class_t *class_gen, xine_stream_t *stream, const char *data) {
  dvd_input_plugin_t    *this;
  dvd_input_class_t     *class = (dvd_input_class_t*)class_gen;
  static const char handled_mrl[] = "dvd:/";

  trace_print("Called\n");

  /* Check we can handle this MRL */
  if (strncasecmp (data, handled_mrl, sizeof(handled_mrl)-1 ) != 0)
    return NULL;

  this = calloc(1, sizeof (dvd_input_plugin_t));
  if (!this) {
    return NULL;
  }

  this->mem_stack     = 0;
  this->mem_stack_max = 1024;
  this->mem           = calloc(this->mem_stack_max, sizeof(unsigned char *));
  if (!this->mem) {
    free(this);
    return NULL;
  }

  this->input_plugin.open               = dvd_plugin_open;
  this->input_plugin.get_capabilities   = dvd_plugin_get_capabilities;
  this->input_plugin.read               = dvd_plugin_read;
  this->input_plugin.read_block         = dvd_plugin_read_block;
  this->input_plugin.seek               = dvd_plugin_seek;
  this->input_plugin.seek_time          = dvd_plugin_seek_time;
  this->input_plugin.get_current_pos    = dvd_plugin_get_current_pos;
  this->input_plugin.get_length         = dvd_plugin_get_length;
  this->input_plugin.get_blocksize      = dvd_plugin_get_blocksize;
  this->input_plugin.get_mrl            = dvd_plugin_get_mrl;
  this->input_plugin.get_optional_data  = dvd_plugin_get_optional_data;
  this->input_plugin.dispose            = dvd_plugin_dispose;
  this->input_plugin.input_class        = class_gen;

  this->stream = stream;
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HAS_STILL, 1);

  this->dvdnav                 = NULL;
  this->opened                 = 0;
  this->seekable               = 0;
  this->buttonN                = 0;
  this->mouse_buttonN          = -1;
  this->mouse_in               = 0;
  this->typed_buttonN          = 0;
  this->pause_timer            = 0;
  this->pg_length              = 0;
  this->pgc_length             = 0;
  this->dvd_name               = NULL;
  this->mrl                    = strdup(data);

  pthread_mutex_init(&this->buf_mutex, NULL);
  this->freeing                = 0;

  this->event_queue = xine_event_new_queue (this->stream);

  /* config callbacks may react now */
  class->ip = this;

  return &this->input_plugin;
}

/* FIXME: adapt to new api. */
#if 0
static xine_mrl_t **dvd_class_get_dir (input_class_t *this_gen,
						       const char *filename, int *nFiles) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;

  trace_print("Called\n");
  if (filename) { *nFiles = 0; return NULL; }

/*
  dvd_build_mrl_list(this);
  *nFiles = this->num_mrls;
  return this->mrls;
*/
  *nFiles = 0;
   return NULL;
}
#endif

static const char * const *dvd_class_get_autoplay_list (input_class_t *this_gen,
					    int *num_files) {

  static const char * const filelist[] = {"dvd:/", NULL};

  trace_print("get_autoplay_list entered\n");

  *num_files = 1;

  return filelist;
}

static void dvd_class_dispose(input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;
  config_values_t *config = this->xine->config;

  config->unregister_callback(config, "media.dvd.device");
  config->unregister_callback(config, "media.dvd.region");
  config->unregister_callback(config, "media.dvd.language");
  config->unregister_callback(config, "media.dvd.readahead");
  config->unregister_callback(config, "media.dvd.seek_behaviour");
  config->unregister_callback(config, "media.dvd.play_single_chapter");

  free(this->eject_device);
  free(this);
}

static int dvd_class_eject_media (input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;

  return media_eject_media (this->xine, this->eject_device);
}

static void *init_class (xine_t *xine, void *data) {
  dvd_input_class_t   *this;
  config_values_t     *config = xine->config;
  void                *dvdcss;
  static const char *const skip_modes[] = {"skip program", "skip part", "skip title", NULL};
  static const char *const seek_modes[] = {"seek in program chain", "seek in program", NULL};
  static const char *const play_single_chapter_modes[] = {"entire dvd", "one chapter", NULL};

  trace_print("Called\n");
#ifdef INPUT_DEBUG
  printf("input_dvd.c: init_class called.\n");
  printf("input_dvd.c: config = %p\n", config);
#endif

  this = (dvd_input_class_t *) calloc(1, sizeof (dvd_input_class_t));
  if (!this)
    return NULL;

  this->input_class.get_instance       = dvd_class_get_instance;
  this->input_class.identifier         = "DVD";
  this->input_class.description        = N_("DVD Navigator");
/*
  this->input_class.get_dir            = dvd_class_get_dir;
*/
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = dvd_class_get_autoplay_list;
  this->input_class.dispose            = dvd_class_dispose;
  this->input_class.eject_media        = dvd_class_eject_media;

  this->config                         = config;
  this->xine                           = xine;

  this->ip                             = NULL;

  this->dvd_device = config->register_filename(config,
					     "media.dvd.device",
					     DVD_PATH, XINE_CONFIG_STRING_IS_DEVICE_NAME,
					     _("device used for DVD playback"),
					     _("The path to the device, usually a "
					       "DVD drive, which you intend to use for playing DVDs."),
					     10, device_change_cb, (void *)this);

#ifdef HOST_OS_DARWIN
  if ((dvdcss = dlopen("libdvdcss.2.dylib", RTLD_LAZY)) != NULL)
#else
  if ((dvdcss = dlopen("libdvdcss.so.2", RTLD_LAZY)) != NULL)
#endif
  {
    /* we have found libdvdcss, enable the specific config options */
    char *raw_device;
    static const char *const decrypt_modes[] = { "key", "disc", "title", NULL };
    int mode;

    raw_device = config->register_filename(config, "media.dvd.raw_device",
					 RDVD_PATH, XINE_CONFIG_STRING_IS_DEVICE_NAME,
					 _("raw device set up for DVD access"),
					 _("If this points to a raw device connected to your "
					   "DVD device, xine will use the raw device for playback. "
					   "This has the advantage of being slightly faster and "
					   "of bypassing the block device cache, which avoids "
					   "throwing away important cache content by keeping DVD "
					   "data cached. Using the block device cache for DVDs "
					   "is useless, because almost all DVD data will be used "
					   "only once.\nSee the documentation on raw device setup "
					   "(man raw) for further information."),
					 10, NULL, NULL);
    if (raw_device) xine_setenv("DVDCSS_RAW_DEVICE", raw_device, 0);

    mode = config->register_enum(config, "media.dvd.css_decryption_method", 0,
				 (char **)decrypt_modes, _("CSS decryption method"),
				 _("Selects the decryption method libdvdcss will use to descramble "
				   "copy protected DVDs. Try the various methods, if you have problems "
				   "playing scrambled DVDs."), 20, NULL, NULL);
    xine_setenv("DVDCSS_METHOD", decrypt_modes[mode], 0);

    if(xine->verbosity > XINE_VERBOSITY_NONE)
      xine_setenv("DVDCSS_VERBOSE", "2", 0);
    else
      xine_setenv("DVDCSS_VERBOSE", "0", 0);

    dlclose(dvdcss);
  }

  config->register_num(config, "media.dvd.region",
		       1,
		       _("region the DVD player claims to be in (1 to 8)"),
		       _("This only needs to be changed if your DVD jumps to a screen "
		         "complaining about a wrong region code. It has nothing to do with "
		         "the region code set in DVD drives, this is purely software."),
		       0, region_changed_cb, this);
  config->register_string(config, "media.dvd.language",
			  "en",
			  _("default language for DVD playback"),
			  _("xine tries to use this language as a default for DVD playback. "
			    "As far as the DVD supports it, menus and audio tracks will be presented "
			    "in this language.\nThe value must be a two character ISO639 language code."),
			  0, language_changed_cb, this);
  config->register_bool(config, "media.dvd.readahead",
			1,
			_("read-ahead caching"),
			_("xine can use a read ahead cache for DVD drive access.\n"
			  "This may lead to jerky playback on slow drives, but it improves the impact "
			  "of the DVD layer change on faster drives."),
			10, read_ahead_cb, this);
  config->register_enum(config, "media.dvd.skip_behaviour", 0,
			(char **)skip_modes,
			_("unit for the skip action"),
			_("You can configure the behaviour when issuing a skip command (using the skip "
			  "buttons for example). The individual values mean:\n\n"
			  "skip program\n"
			  "will skip a DVD program, which is a navigational unit similar to the "
			  "index marks on an audio CD; this is the normal behaviour for DVD players\n\n"
			  "skip part\n"
			  "will skip a DVD part, which is a structural unit similar to the "
			  "track marks on an audio CD; parts usually coincide with programs, but parts "
			  "can be larger than programs\n\n"
			  "skip title\n"
			  "will skip a DVD title, which is a structural unit representing entire "
			  "features on the DVD"),
			20, NULL, NULL);
  config->register_enum(config, "media.dvd.seek_behaviour", 0,
			(char **)seek_modes,
			_("unit for seeking"),
			_("You can configure the domain spanned by the seek slider. The individual "
			  "values mean:\n\n"
			  "seek in program chain\n"
			  "seeking will span an entire DVD program chain, which is a navigational "
			  "unit representing the entire video stream of the current feature\n\n"
			  "seek in program\n"
			  "seeking will span a DVD program, which is a navigational unit representing "
			  "a chapter of the current feature"),
			20, seek_mode_cb, this);
  config->register_enum(config, "media.dvd.play_single_chapter", 0,
			(char **)play_single_chapter_modes,
			_("play mode when title/chapter is given"),
			_("You can configure the behaviour when playing a dvd from a given "
			  "title/chapter (eg. using MRL 'dvd:/1.2'). The individual values mean:\n\n"
			  "entire dvd\n"
			  "play the entire dvd starting on the specified position.\n\n"
			  "one chapter\n"
			  "play just the specified title/chapter and then stop"),
			20, play_single_chapter_cb, this);

#ifdef __sun
  check_solaris_vold_device(this);
#endif
#ifdef INPUT_DEBUG
  printf("input_dvd.c: init_class finished.\n");
#endif
  return this;
}


const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "DVD", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
