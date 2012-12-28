/*
 * Copyright (C) 2000-2004 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * stuff needed to turn libmpeg2 into a xine decoder plugin
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
#include <inttypes.h>
#include <assert.h>

#include "./include/mpeg2.h"
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>

/*
#define LOG
#define LOG_FRAME_ALLOC_FREE
#define LOG_ENTRY
#define LOG_FRAME_COUNTER
*/

#define _x_abort() do {} while (0)

typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;

typedef struct {
  uint32_t id;
  vo_frame_t * img;
} img_state_t;

typedef struct mpeg2_video_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2dec_t      *mpeg2dec;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
  int32_t         force_aspect;
  int             force_pan_scan;
  double          ratio;
  img_state_t     img_state[30];
  uint32_t	  frame_number;
  uint32_t        rff_pattern;

} mpeg2_video_decoder_t;

#ifndef LOG_FRAME_ALLOC_FREE
inline static void mpeg2_video_print_bad_state(img_state_t * img_state) {}
#else
static void mpeg2_video_print_bad_state(img_state_t * img_state) {
  int32_t n,m;
  m=0;
  for(n=0;n<30;n++) {
    if (img_state[n].id>0) {
      printf("%d = %u\n",n, img_state[n].id);
      m++;
    }
  }
  if (m > 3) _x_abort();
  if (m == 0) printf("NO FRAMES\n");
}
#endif

static void mpeg2_video_free_all(img_state_t * img_state) {
  int32_t n,m;
  vo_frame_t * img;
  printf("libmpeg2new:free_all\n");
  for(n=0;n<30;n++) {
    if (img_state[n].id>0) {
      img = img_state[n].img;
      img->free(img);
      img_state[n].id = 0;
    }
  }
}


static void mpeg2_video_print_fbuf(const mpeg2_fbuf_t * fbuf) {
  printf("%p",fbuf);
  vo_frame_t * img;
  if (fbuf) {
    img = (vo_frame_t *) fbuf->id;
    if (img) {
      printf (", img=%p, (id=%d)\n",
             img, img->id);
    } else {
      printf (", img=NULL\n");
    }
  } else {
    printf ("\n");
  }
}

static void mpeg2_video_decode_data (video_decoder_t *this_gen, buf_element_t *buf_element) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;
  uint8_t * current = buf_element->content;
  uint8_t * end = buf_element->content + buf_element->size;
  const mpeg2_info_t * info;
  mpeg2_state_t state;
  vo_frame_t * img;
  uint32_t picture_structure;
  int32_t frame_skipping;

  /* handle aspect hints from xine-dvdnav */
  if (buf_element->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf_element->decoder_info[1] == BUF_SPECIAL_ASPECT) {
      this->force_aspect = buf_element->decoder_info[2];
      if (buf_element->decoder_info[3] == 0x1 && buf_element->decoder_info[2] == 3)
	/* letterboxing is denied, we have to do pan&scan */
	this->force_pan_scan = 1;
      else
	this->force_pan_scan = 0;
    }

    return;
  }

  if (buf_element->decoder_flags != 0) return;

#ifdef LOG_ENTRY
  printf ("libmpeg2: decode_data: enter\n");
#endif

  mpeg2_buffer (this->mpeg2dec, current, end);

  info = mpeg2_info (this->mpeg2dec);

  while ((state = mpeg2_parse (this->mpeg2dec)) != STATE_BUFFER) {
    switch (state) {
      case STATE_SEQUENCE:
        /* might set nb fbuf, convert format, stride */
        /* might set fbufs */
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_BITRATE,   info->sequence->byte_rate * 8);
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,     info->sequence->picture_width);
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,    info->sequence->picture_height);
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,  info->sequence->frame_period / 300);
        if (this->force_aspect) ((mpeg2_sequence_t *)info->sequence)->pixel_width = this->force_aspect; /* ugly... */
        switch (info->sequence->pixel_width) {
	case 3:
	  this->ratio = 16.0 / 9.0;
	  break;
	case 4:
	  this->ratio = 2.11;
	  break;
	case 2:
	  this->ratio = 4.0 / 3.0;
	  break;
	case 1:
	default:
	  this->ratio = (double)info->sequence->picture_width/(double)info->sequence->picture_height;
	  break;
        }
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO, (int)(10000*this->ratio));

        if (info->sequence->flags & SEQ_FLAG_MPEG2) {
          _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "MPEG 2 (libmpeg2new)");
        } else {
          _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "MPEG 1 (libmpeg2new)");
        }

        break;
      case STATE_PICTURE:
        /* might skip */
        /* might set fbuf */
        if (info->current_picture->nb_fields == 1) {
          picture_structure = info->current_picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? VO_TOP_FIELD : VO_BOTTOM_FIELD;
        } else {
          picture_structure = VO_BOTH_FIELDS;
        }

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                              info->sequence->picture_width,
                                              info->sequence->picture_height,
                                              this->ratio,
                                              XINE_IMGFMT_YV12,
                                              picture_structure);
        this->frame_number++;
#ifdef LOG_FRAME_COUNTER
        printf("libmpeg2:frame_number=%d\n",this->frame_number);
#endif
        img->top_field_first = info->current_picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? 1 : 0;
        img->repeat_first_field = (info->current_picture->nb_fields > 2) ? 1 : 0;
        img->duration=info->sequence->frame_period / 300;
        if( ((this->rff_pattern & 0xff) == 0xaa ||
             (this->rff_pattern & 0xff) == 0x55) ) {
          /* special case for ntsc 3:2 pulldown */
            img->duration += img->duration/4;
        } else {
          if( img->repeat_first_field ) {
            img->duration = (img->duration * info->current_picture->nb_fields) / 2;
          }
        }

        if ((info->current_picture->flags & 7) == 1) {
          img->pts=buf_element->pts; /* If an I frame, use PTS */
        } else {
          img->pts=0;
        }


#ifdef LOG_FRAME_ALLOC_FREE
        printf ("libmpeg2:decode_data:get_frame xine=%p (id=%d)\n", img,img->id);
#endif
        if (this->img_state[img->id].id != 0) {
          printf ("libmpeg2:decode_data:get_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id].id);
          _x_abort();
        }

        this->img_state[img->id].id = 1;
        this->img_state[img->id].img = img;

        mpeg2_set_buf (this->mpeg2dec, img->base, img);
        break;
      case STATE_SLICE:
      case STATE_END:
#if 0
    printf("libmpeg2:decode_data:current_fbuf=");
    mpeg2_video_print_fbuf(info->current_fbuf);
    printf("libmpeg2:decode_data:display_fbuf=");
    mpeg2_video_print_fbuf(info->display_fbuf);
    printf("libmpeg2:decode_data:discard_fbuf=");
    mpeg2_video_print_fbuf(info->discard_fbuf);
#endif
        /* draw current picture */
        /* might free frame buffer */
        if (info->display_fbuf && info->display_fbuf->id) {
          img = (vo_frame_t *) info->display_fbuf->id;
          /* this should be used to detect any special rff pattern */
          this->rff_pattern = this->rff_pattern << 1;
          this->rff_pattern |= img->repeat_first_field;

#ifdef LOG_FRAME_ALLOC_FREE
          printf ("libmpeg2:decode_data:draw_frame xine=%p, fbuf=%p, id=%d \n", img, info->display_fbuf, img->id);
#endif
          if (this->img_state[img->id].id != 1) {
            printf ("libmpeg2:decode_data:draw_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id].id);
            _x_abort();
          }
          if (this->img_state[img->id].id == 1) {
            frame_skipping = img->draw (img, this->stream);
            /* FIXME: Handle skipping */
            this->img_state[img->id].id = 2;
          }

        }
        if (info->discard_fbuf && !info->discard_fbuf->id) {
          printf ("libmpeg2:decode_data:BAD free_frame discard: xine=%p, fbuf=%p\n", info->discard_fbuf->id, info->discard_fbuf);
          //_x_abort();
        }
        if (info->discard_fbuf && info->discard_fbuf->id) {
          img = (vo_frame_t *) info->discard_fbuf->id;
#ifdef LOG_FRAME_ALLOC_FREE
          printf ("libmpeg2:decode_data:free_frame xine=%p, fbuf=%p,id=%d\n", img, info->discard_fbuf, img->id);
#endif
          if (this->img_state[img->id].id != 2) {
            printf ("libmpeg2:decode_data:free_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id].id);
            _x_abort();
          }
          if (this->img_state[img->id].id == 2) {
            img->free(img);
            this->img_state[img->id].id = 0;
          }
        }
#ifdef LOG_FRAME_ALLOC_FREE
        mpeg2_video_print_bad_state(this->img_state);
#endif
        break;
      case STATE_GOP:
        break;
      default:
	printf("libmpeg2new: STATE unknown %d\n",state);
        break;
   }

 }
#ifdef LOG_ENTRY
  printf ("libmpeg2: decode_data: exit\n");
#endif

}

static void mpeg2_video_flush (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG_ENTRY
  printf ("libmpeg2: flush\n");
#endif

/*  mpeg2_flush (&this->mpeg2); */
}

static void mpeg2_video_reset (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;
  int32_t state;
  const mpeg2_info_t * info;
  vo_frame_t * img;
  int32_t frame_skipping;

#ifdef LOG_ENTRY
  printf ("libmpeg2: reset\n");
#endif
  mpeg2_reset (this->mpeg2dec, 1); /* 1 for full reset */
  mpeg2_video_free_all(this->img_state);


#if 0  /* This bit of code does not work yet. */
  info = mpeg2_info (this->mpeg2dec);
  state = mpeg2_reset (this->mpeg2dec);
  printf("reset state1:%d\n",state);
  if (info->display_fbuf && info->display_fbuf->id) {
    img = (vo_frame_t *) info->display_fbuf->id;

    if (this->img_state[img->id] != 1) {
      printf ("libmpeg2:decode_data:draw_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 1) {
      frame_skipping = img->draw (img, this->stream);
      /* FIXME: Handle skipping */
      this->img_state[img->id] = 2;
    }
  }

  if (info->discard_fbuf && !info->discard_fbuf->id) {
    printf ("libmpeg2:decode_data:BAD free_frame discard_fbuf=%p\n", info->discard_fbuf);
    _x_abort();
  }
  if (info->discard_fbuf && info->discard_fbuf->id) {
    img = (vo_frame_t *) info->discard_fbuf->id;
    if (this->img_state[img->id] != 2) {
      printf ("libmpeg2:decode_data:free_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 2) {
      img->free(img);
      this->img_state[img->id] = 0;
    }
  }
  state = mpeg2_parse (this->mpeg2dec);
  printf("reset state2:%d\n",state);
  if (info->display_fbuf && info->display_fbuf->id) {
    img = (vo_frame_t *) info->display_fbuf->id;

    if (this->img_state[img->id] != 1) {
      printf ("libmpeg2:decode_data:draw_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 1) {
      frame_skipping = img->draw (img, this->stream);
      /* FIXME: Handle skipping */
      this->img_state[img->id] = 2;
    }
  }

  if (info->discard_fbuf && !info->discard_fbuf->id) {
    printf ("libmpeg2:decode_data:BAD free_frame discard_fbuf=%p\n", info->discard_fbuf);
    _x_abort();
  }
  if (info->discard_fbuf && info->discard_fbuf->id) {
    img = (vo_frame_t *) info->discard_fbuf->id;
    if (this->img_state[img->id] != 2) {
      printf ("libmpeg2:decode_data:free_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 2) {
      img->free(img);
      this->img_state[img->id] = 0;
    }
  }
  state = mpeg2_parse (this->mpeg2dec);
  printf("reset state3:%d\n",state);
  if (info->display_fbuf && info->display_fbuf->id) {
    img = (vo_frame_t *) info->display_fbuf->id;

    if (this->img_state[img->id] != 1) {
      printf ("libmpeg2:decode_data:draw_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 1) {
      frame_skipping = img->draw (img, this->stream);
      /* FIXME: Handle skipping */
      this->img_state[img->id] = 2;
    }
  }

  if (info->discard_fbuf && !info->discard_fbuf->id) {
    printf ("libmpeg2:decode_data:BAD free_frame discard_fbuf=%p\n", info->discard_fbuf);
    _x_abort();
  }
  if (info->discard_fbuf && info->discard_fbuf->id) {
    img = (vo_frame_t *) info->discard_fbuf->id;
    if (this->img_state[img->id] != 2) {
      printf ("libmpeg2:decode_data:free_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
      _x_abort();
    }
    if (this->img_state[img->id] == 2) {
      img->free(img);
      this->img_state[img->id] = 0;
    }
  }
#endif

}

static void mpeg2_video_discontinuity (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG_ENTRY
  printf ("libmpeg2: dicontinuity\n");
#endif
/*  mpeg2_discontinuity (&this->mpeg2dec); */
}

static void mpeg2_video_dispose (video_decoder_t *this_gen) {

  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG_ENTRY
  printf ("libmpeg2: close\n");
#endif

  mpeg2_close (this->mpeg2dec);

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  mpeg2_video_decoder_t *this ;
  int32_t n;

  this = (mpeg2_video_decoder_t *) calloc(1, sizeof(mpeg2_video_decoder_t));

  this->video_decoder.decode_data         = mpeg2_video_decode_data;
  this->video_decoder.flush               = mpeg2_video_flush;
  this->video_decoder.reset               = mpeg2_video_reset;
  this->video_decoder.discontinuity       = mpeg2_video_discontinuity;
  this->video_decoder.dispose             = mpeg2_video_dispose;
  this->stream                            = stream;
  this->class                             = (mpeg2_class_t *) class_gen;
  this->frame_number=0;
  this->rff_pattern=0;

  this->mpeg2dec = mpeg2_init ();
  mpeg2_custom_fbuf (this->mpeg2dec, 1);  /* <- Force libmpeg2 to use xine frame buffers. */
  (stream->video_out->open) (stream->video_out, stream);
  this->force_aspect = this->force_pan_scan = 0;
  for(n=0;n<30;n++) this->img_state[n].id=0;

  return &this->video_decoder;
}

/*
 * mpeg2 plugin class
 */
static void *init_plugin (xine_t *xine, void *data) {

  mpeg2_class_t *this;

  this = (mpeg2_class_t *) calloc(1, sizeof(mpeg2_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "mpeg2new";
  this->decoder_class.description     = N_("mpeg2 based video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  return this;
}
/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static const decoder_info_t dec_info_mpeg2 = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "mpeg2new", XINE_VERSION_CODE, &dec_info_mpeg2, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
