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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define LOG_MODULE "input_enigma"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/input_plugin.h>
#include "net_buf_ctrl.h"
#include "combined_enigma.h"

#define ENIGMA_ABS_FIFO_DIR     "/tmp"
#define DEFAULT_PTS_START       150000
#define BUFSIZE                 768
#define FILE_FLAGS O_RDONLY
#define FIFO_PUT                0

typedef struct enigma_input_plugin_s enigma_input_plugin_t;

struct enigma_input_plugin_s {
  input_plugin_t      input_plugin;
  xine_stream_t      *stream;
  int                 fh;
  char               *mrl;
  off_t               curpos;
  char                seek_buf[BUFSIZE];
  xine_t             *xine;
  int                 last_disc_type;
  nbc_t              *nbc;
};

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
} enigma_input_class_t;


/* Put callback the fifo mutex is locked */
static void enigma_nbc_put_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;
  int64_t progress = 0;
  int64_t video_p = 0;
  int64_t audio_p = 0;
  int force_dvbspeed = 0;
  int has_video, has_audio;
  force_dvbspeed = 0;
  xine_t *xine = this->stream->xine;

  cfg_entry_t *entry;
	config_values_t *cfg;
	cfg = xine->config;
	entry = cfg->lookup_entry(cfg, "input.buffer.dynamic");
  if  (strdup(entry->unknown_value) == "1");
  			force_dvbspeed = 1;

  lprintf("enter enigma_nbc_put_cb\n");
  pthread_mutex_lock(&this->mutex);

  if ((buf->type & BUF_MAJOR_MASK) != BUF_CONTROL_BASE) {

    if (this->enabled) {
      if (this->dvbspeed)
        dvbspeed_put (this, fifo, buf);
      else {
      nbc_compute_fifo_length(this, fifo, buf, FIFO_PUT);

      if (this->buffering) {

        has_video = _x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_VIDEO);
        has_audio = _x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_AUDIO);
        /* restart playing if high_water_mark is reached by all fifos
         * do not restart if has_video and has_audio are false to avoid
         * a yoyo effect at the beginning of the stream when these values
         * are not yet known.
         *
         * be sure that the next buffer_pool_alloc() call will not deadlock,
         * we need at least 2 buffers (see buffer.c)
         */
//printf("this->video_last_pts %lld   this->audio_last_pts %lld\n", this->video_last_pts, this->audio_last_pts);
        int64_t first_pts = this->video_first_pts>this->audio_first_pts?this->video_first_pts:this->audio_first_pts;
        int64_t last_pts = this->video_last_pts<this->audio_last_pts?this->video_last_pts:this->audio_last_pts;
//printf("AAA first_pts %lld  last_pts %lld\n", first_pts, last_pts);
        if ( has_video && has_audio && (last_pts-first_pts)>DEFAULT_PTS_START ) {
          this->progress = 100;
          //report_progress (this->stream, 100);
          this->buffering = 0;
          nbc_set_speed_normal(this);
        }
        else if ((((!has_video) || (this->video_fifo_length > this->high_water_mark)) &&
             ((!has_audio) || (this->audio_fifo_length > this->high_water_mark)) &&
             (has_video || has_audio))) {

          this->progress = 100;
          //report_progress (this->stream, 100);
          this->buffering = 0;

          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: enigma_nbc_put_cb: stops buffering\n");

          nbc_set_speed_normal(this);

          this->high_water_mark += this->high_water_mark / 2;

        } else {
          /*  compute the buffering progress
           *    50%: video
           *    50%: audio */
          video_p = ((this->video_fifo_length * 50) / this->high_water_mark);
          if (video_p > 50) video_p = 50;
          audio_p = ((this->audio_fifo_length * 50) / this->high_water_mark);
          if (audio_p > 50) audio_p = 50;

          if ((has_video) && (has_audio)) {
            progress = video_p + audio_p;
          } else if (has_video) {
            progress = 2 * video_p;
          } else {
            progress = 2 * audio_p;
          }

          /* if the progress can't be computed using the fifo length,
             use the number of buffers */
          if (!progress) {
            video_p = this->video_fifo_fill;
            audio_p = this->audio_fifo_fill;
            progress = (video_p > audio_p) ? video_p : audio_p;
          }

          if (progress > this->progress) {
            //report_progress (this->stream, progress);
            this->progress = progress;
          }
        }
      }
      //if(this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
      //  display_stats(this);

      //report_stats(this, 0);
      }
  	}
  } else {

    switch (buf->type) {
      case BUF_CONTROL_START:
        lprintf("BUF_CONTROL_START\n");
        if (!this->enabled) {
          /* a new stream starts */
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: enigma_nbc_put_cb: starts buffering\n");
          this->enabled           = 1;
          this->buffering         = 1;
          this->video_first_pts   = 0;
          this->video_last_pts    = 0;
          this->audio_first_pts   = 0;
          this->audio_last_pts    = 0;
          this->video_fifo_length = 0;
          this->audio_fifo_length = 0;
          dvbspeed_init (this, force_dvbspeed);
          if (!this->dvbspeed) nbc_set_speed_pause(this);
/*          this->progress = 0;
          report_progress (this->stream, progress);*/
        }
        break;
      case BUF_CONTROL_NOP:
        if (!(buf->decoder_flags & BUF_FLAG_END_USER) &&
            !(buf->decoder_flags & BUF_FLAG_END_STREAM)) {
          break;
        }
        /* fall through */
      case BUF_CONTROL_END:
      case BUF_CONTROL_QUIT:
        lprintf("BUF_CONTROL_END\n");
        dvbspeed_close (this);
        if (this->enabled) {
          /* end of stream :
           *   - disable the nbc
           *   - unpause the engine if buffering
           */
          this->enabled = 0;

          lprintf("DISABLE netbuf\n");

          if (this->buffering) {
            this->buffering = 0;
            this->progress = 100;
            //report_progress (this->stream, this->progress);

            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: enigma_nbc_put_cb: stops buffering\n");

            nbc_set_speed_normal(this);
          }
        }
        break;

      case BUF_CONTROL_NEWPTS:
        /* discontinuity management */
        if (fifo == this->video_fifo) {
          this->video_in_disc++;
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "\nnet_buf_ctrl: enigma_nbc_put_cb video disc %d\n", this->video_in_disc);
        } else {
          this->audio_in_disc++;
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "\nnet_buf_ctrl: enigma_nbc_put_cb audio disc %d\n", this->audio_in_disc);
        }
        break;
    }

    if (fifo == this->video_fifo) {
      this->video_fifo_free = fifo->buffer_pool_num_free;
      this->video_fifo_size = fifo->fifo_data_size;
    } else {
      this->audio_fifo_free = fifo->buffer_pool_num_free;
      this->audio_fifo_size = fifo->fifo_data_size;
    }
  }
  pthread_mutex_unlock(&this->mutex);
  lprintf("exit enigma_nbc_put_cb\n");
}

nbc_t *enigma_nbc_init (xine_stream_t *stream) {

  nbc_t *this = calloc(1, sizeof (nbc_t));
  fifo_buffer_t *video_fifo = stream->video_fifo;
  fifo_buffer_t *audio_fifo = stream->audio_fifo;
  
  
  double video_fifo_factor, audio_fifo_factor;
  cfg_entry_t *entry;

  lprintf("enigma_nbc_init\n");
  pthread_mutex_init (&this->mutex, NULL);

  this->stream              = stream;
  this->video_fifo          = video_fifo;
  this->audio_fifo          = audio_fifo;

  /* when the FIFO sizes are increased compared to the default configuration,
   * apply a factor to the high water mark */
  entry = stream->xine->config->lookup_entry(stream->xine->config, "engine.buffers.video_num_buffers");
  /* No entry when no video output */
  if (entry)
    video_fifo_factor = (double)video_fifo->buffer_pool_capacity / (double)entry->num_default;
  else
    video_fifo_factor = 1.0;
  entry = stream->xine->config->lookup_entry(stream->xine->config, "engine.buffers.audio_num_buffers");
  /* When there's no audio output, there's no entry */
  if (entry)
    audio_fifo_factor = (double)audio_fifo->buffer_pool_capacity / (double)entry->num_default;
  else
    audio_fifo_factor = 1.0;
  /* use the smaller factor */
  if (video_fifo_factor < audio_fifo_factor)
    this->high_water_mark = (double)DEFAULT_HIGH_WATER_MARK * video_fifo_factor;
  else
    this->high_water_mark = (double)DEFAULT_HIGH_WATER_MARK * audio_fifo_factor;

  video_fifo->register_alloc_cb(video_fifo, nbc_alloc_cb, this);
  video_fifo->register_put_cb(video_fifo, enigma_nbc_put_cb, this);
  video_fifo->register_get_cb(video_fifo, nbc_get_cb, this);

  audio_fifo->register_alloc_cb(audio_fifo, nbc_alloc_cb, this);
  audio_fifo->register_put_cb(audio_fifo, enigma_nbc_put_cb, this);
  audio_fifo->register_get_cb(audio_fifo, nbc_get_cb, this);

  return this;
}

void enigma_nbc_close (nbc_t *this) {
  fifo_buffer_t *video_fifo = this->stream->video_fifo;
  fifo_buffer_t *audio_fifo = this->stream->audio_fifo;
  xine_t        *xine       = this->stream->xine;

  xprintf(xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: enigma_nbc_close\n");

  /* unregister all fifo callbacks */
  /* do not lock the mutex to avoid deadlocks if a decoder calls fifo->get() */
  video_fifo->unregister_alloc_cb(video_fifo, nbc_alloc_cb);
  video_fifo->unregister_put_cb(video_fifo, enigma_nbc_put_cb);
  video_fifo->unregister_get_cb(video_fifo, nbc_get_cb);

  audio_fifo->unregister_alloc_cb(audio_fifo, nbc_alloc_cb);
  audio_fifo->unregister_put_cb(audio_fifo, enigma_nbc_put_cb);
  audio_fifo->unregister_get_cb(audio_fifo, nbc_get_cb);

  /* now we are sure that nobody will call a callback */
  this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);

  pthread_mutex_destroy(&this->mutex);
  free (this);
  xprintf(xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: enigma_nbc_close: done\n");
}

static off_t enigma_read_abort(xine_stream_t *stream, int fd, char *buf, off_t todo)
{
  off_t ret;

  while (1)
  {
    /*
     * System calls are not a thread cancellation point in Linux
     * pthreads.  However, the RT signal sent to cancel the thread
     * will cause recv() to return with EINTR, and we can manually
     * check cancellation.
     */
    pthread_testcancel();
    ret = _x_read_abort(stream, fd, buf, todo);
    pthread_testcancel();

    if (ret < 0
        && (errno == EINTR
          || errno == EAGAIN))
    {
      continue;
    }

    break;
  }

  return ret;
}

static off_t enigma_plugin_read (input_plugin_t *this_gen,
				void *buf_gen, off_t len) {

  enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen;
  uint8_t *buf = (uint8_t *)buf_gen;
  off_t n, total = 0;
#ifdef LOG_READ
  lprintf ("reading %lld bytes...\n", len);
#endif

  if( len > 0 )
  {
    int retries = 0;
    do
    {
      n = enigma_read_abort (this->stream, this->fh, (char *)&buf[total], len-total);
      //n = _x_io_file_read (this->stream, this->fh, &buf[total], len - total);
      if (0 == n)
        lprintf("read 0, retries: %d\n", retries);
    }
    while (0 == n
           && _x_continue_stream_processing(this->stream)
           && 200 > retries++); // 200 * 50ms
#ifdef LOG_READ
    lprintf ("got %lld bytes (%lld/%lld bytes read)\n", n, total, len);
#endif
    if (n < 0)
    {
      _x_message(this->stream, XINE_MSG_READ_ERROR, NULL);
      return 0;
    }

    this->curpos += n;
    total += n;
  }

  return total;

}

static buf_element_t *enigma_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
					       off_t todo) {

  off_t                 total_bytes;
  /* enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen; */
  buf_element_t         *buf = fifo->buffer_pool_alloc (fifo);

  if (todo > buf->max_size)
    todo = buf->max_size;
  if (todo < 0) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = enigma_plugin_read (this_gen, (char*)buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

/* forward reference */
static off_t enigma_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {

  enigma_input_plugin_t  *this = (enigma_input_plugin_t *) this_gen;

  printf ("seek %"PRId64" offset, %d origin...\n", offset, origin);

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
        return this->curpos;
    }

    this_gen->read (this_gen, this->seek_buf, offset);
  }

  if (origin == SEEK_SET) {

    if (offset < this->curpos) {

      //if( this->curpos <= this->preview_size )
      //  this->curpos = offset;
      //else
        xprintf (this->xine, XINE_VERBOSITY_LOG,
                 _("stdin: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n"),
                 (intmax_t)this->curpos, (intmax_t)offset);
        printf ("stdin: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n",
                 (intmax_t)this->curpos, (intmax_t)offset);

    } else {
      offset -= this->curpos;

      for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
        if( this_gen->read (this_gen, this->seek_buf, BUFSIZE) <= 0 )
          return this->curpos;
      }

      this_gen->read (this_gen, this->seek_buf, offset);
    }
  }

  return this->curpos;
}

static off_t enigma_plugin_get_length(input_plugin_t *this_gen) {
  return 0;
}

static uint32_t enigma_plugin_get_capabilities(input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

static uint32_t enigma_plugin_get_blocksize(input_plugin_t *this_gen) {

  return 0;
}

static off_t enigma_plugin_get_current_pos (input_plugin_t *this_gen){
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  return this->curpos;
}

static const char* enigma_plugin_get_mrl (input_plugin_t *this_gen) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  return this->mrl;
}

static void enigma_plugin_dispose (input_plugin_t *this_gen ) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  if (this->nbc) {
    enigma_nbc_close (this->nbc);
  }

  if (this->fh != -1)
    close(this->fh);

  free (this->mrl);
  free (this);
}

static int enigma_plugin_get_optional_data (input_plugin_t *this_gen,
					   void *data, int data_type) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  switch (data_type)
  {
  case INPUT_OPTIONAL_DATA_PREVIEW:
    /* just fake what mpeg_pes demuxer expects */
    memcpy (data, "\x00\x00\x01\xe0\x00\x03\x80\x00\x00", 9);
    return 9;
  case INPUT_OPTIONAL_DATA_DEMUXER:
    {
      char **tmp = (char**)data;
      *tmp = "mpeg-ts";
    }
    return 0;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int enigma_plugin_open (input_plugin_t *this_gen ) {
  enigma_input_plugin_t *this = (enigma_input_plugin_t *) this_gen;

  printf ("trying to open '%s'...\n", this->mrl);

  if (this->fh == -1) {
    int err = 0;
    char *filename = (char *)ENIGMA_ABS_FIFO_DIR "/ENIGMA_FIFO";
    this->fh = open (filename, FILE_FLAGS);

    printf("filename '%s'\n", filename);

    if (this->fh == -1) {
      xprintf (this->xine, XINE_VERBOSITY_LOG, _("enigma_fifo: failed to open '%s'\n"), filename);
      printf ("enigma_fifo: failed to open '%s'\n", filename);
      return 0;
    }

  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this->curpos          = 0;

  return 1;
}

static input_plugin_t *enigma_class_get_instance (input_class_t *class_gen,
						 xine_stream_t *stream, const char *data) {

  enigma_input_class_t  *class = (enigma_input_class_t *) class_gen;
  enigma_input_plugin_t *this;
  char                 *mrl = strdup(data);
  int                   fh;

  if (!strncasecmp(mrl, "enigma:/", 8)) {
    lprintf("Enigma plugin\n");
  } else {
    free(mrl);
    return NULL;
  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this       = calloc(1, sizeof(enigma_input_plugin_t));

  this->stream = stream;
  this->curpos = 0;
  this->mrl    = mrl;
  this->fh     = -1;
  this->xine   = class->xine;

  this->input_plugin.open              = enigma_plugin_open;
  this->input_plugin.get_capabilities  = enigma_plugin_get_capabilities;
  this->input_plugin.read              = enigma_plugin_read;
  this->input_plugin.read_block        = enigma_plugin_read_block;
  this->input_plugin.seek              = enigma_plugin_seek;
  this->input_plugin.get_current_pos   = enigma_plugin_get_current_pos;
  this->input_plugin.get_length        = enigma_plugin_get_length;
  this->input_plugin.get_blocksize     = enigma_plugin_get_blocksize;
  this->input_plugin.get_mrl           = enigma_plugin_get_mrl;
  this->input_plugin.dispose           = enigma_plugin_dispose;
  this->input_plugin.get_optional_data = enigma_plugin_get_optional_data;
  this->input_plugin.input_class       = class_gen;

  /*
   * buffering control
   */
  this->nbc    = enigma_nbc_init (this->stream);

  return &this->input_plugin;
}


//static void *init_class (xine_t *xine, void *data) {
void *init_class (xine_t *xine, void *data) {

  enigma_input_class_t  *this;

  this = calloc(1, sizeof (enigma_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = enigma_class_get_instance;
  this->input_class.identifier         = "ENIGMA";
  this->input_class.description        = N_("ENIGMA2PC display device plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = default_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

/*
const plugin_info_t xine_plugin_info[] EXPORTED = {
//  type, API, "name", version, special_info, init_function 
  { PLUGIN_INPUT, 18, "enigma", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
*/
