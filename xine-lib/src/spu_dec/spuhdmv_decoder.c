/*
 * Copyright (C) 2000-2009 the xine project
 *
 * Copyright (C) 2009 Petri Hintukainen <phintuka@users.sourceforge.net>
 *
 * This file is part of xine, a unix video player.
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
 * Decoder for HDMV/BluRay bitmap subtitles
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <xine/xine_internal.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/video_out.h>
#include <xine/video_overlay.h>

#define XINE_HDMV_TRACE(x...) printf(x)
/*#define XINE_HDMV_TRACE(x...) */
#define XINE_HDMV_ERROR(x...) fprintf(stderr, "spuhdmv: " x)
/*#define XINE_HDMV_ERROR(x...) lprintf(x) */

#ifndef MAX
#  define MAX(a,b) (a>b)?(a):(b)
#endif

enum {
  SEGTYPE_PALETTE              = 0x14,
  SEGTYPE_OBJECT               = 0x15,
  SEGTYPE_PRESENTATION_SEGMENT = 0x16,
  SEGTYPE_WINDOW_DEFINITION    = 0x17,
  SEGTYPE_INTERACTIVE          = 0x18,
  SEGTYPE_END_OF_DISPLAY       = 0x80,
} eSegmentType;

/*
 * cached palette (xine-lib format)
 */
typedef struct subtitle_clut_s subtitle_clut_t;
struct subtitle_clut_s {
  uint8_t          id;
  uint32_t         color[256];
  uint8_t          trans[256];
  subtitle_clut_t *next;

  int shown;
};

/*
 * cached RLE image (xine-lib format)
 */
typedef struct subtitle_object_s subtitle_object_t;
struct subtitle_object_s {
  uint16_t    id;
  uint16_t    xpos, ypos;
  uint16_t    width, height;

  /* xine format */
  rle_elem_t *rle;
  unsigned int num_rle;
  size_t      data_size;

  /* HDMV format (used when object does not fit to single segment) */
  uint32_t    data_len;      /* size of complete object */
  uint8_t    *raw_data;      /* partial RLE data in HDMV format */
  size_t      raw_data_len;  /* bytes buffered */
  size_t      raw_data_size; /* allocated size */

  subtitle_object_t *next;

  int shown;
};

/*
 * Window definition
 */
typedef struct window_def_s window_def_t;
struct window_def_s {
  uint8_t   id;
  uint16_t  xpos, ypos;
  uint16_t  width, height;

  window_def_t *next;

  int shown;
};


/*
 * decoded SPU
 */
typedef struct composition_object_s composition_object_t;
struct composition_object_s {
  uint8_t     window_id_ref;
  uint16_t    object_id_ref;

  uint16_t    xpos, ypos;

  uint8_t     forced_flag;
  uint8_t     cropped_flag;
  uint16_t    crop_horiz_pos, crop_vert_pos;
  uint16_t    crop_width, crop_height;

  composition_object_t *next;

  int shown;
};

typedef struct composition_descriptor_s composition_descriptor_t;
struct composition_descriptor_s {
  uint16_t number;
  uint8_t  state;
};

typedef struct presentation_segment_s presentation_segment_t;
struct presentation_segment_s {
  composition_descriptor_t comp_descr;

  uint8_t palette_update_flag;
  uint8_t palette_id_ref;
  uint8_t object_number;

  composition_object_t *comp_objs;

  presentation_segment_t *next;

  int64_t pts;
  int     shown;
};

/*
 * list handling
 */

#define LIST_REPLACE(list, obj, FREE_FUNC)      \
  do {                                          \
    unsigned int id = obj->id;                  \
                                                \
    /* insert to list */                        \
    obj->next = list;                           \
    list = obj;                                 \
                                                \
    /* remove old */                            \
    while (obj->next && obj->next->id != id)    \
      obj = obj->next;                          \
    if (obj->next) {                            \
      void *tmp = (void*)obj->next;             \
      obj->next = obj->next->next;              \
      FREE_FUNC(tmp);                           \
    }                                           \
  } while (0);

#define LIST_DESTROY(list, FREE_FUNC) \
  while (list) {               \
    void *tmp = (void*)list;   \
    list = list->next;         \
    FREE_FUNC(tmp);            \
  }

static void free_subtitle_object(void *ptr)
{
  if (ptr) {
    free(((subtitle_object_t*)ptr)->rle);
    free(((subtitle_object_t*)ptr)->raw_data);
    free(ptr);
  }
}
static void free_presentation_segment(void *ptr)
{
  if (ptr) {
    presentation_segment_t *seg = (presentation_segment_t*)ptr;
    LIST_DESTROY(seg->comp_objs, free);
    free(ptr);
  }
}


/*
 * segment_buffer_t
 *
 * assemble and decode segments
 */

typedef struct {
  /* current segment */
  int      segment_len;  /* length of current segment (without 3-byte header) */
  uint8_t  segment_type; /* current segment type */
  uint8_t *segment_data; /* pointer to current segment payload */
  uint8_t *segment_end;  /* pointer to last byte + 1 of current segment */
  uint8_t  error;        /* boolean: buffer overflow etc. */

  /* accumulated data */
  uint8_t *buf;          /* */
  size_t   len;          /* count of unprocessed bytes */
  size_t   data_size;    /* allocated buffer size */
} segment_buffer_t;

/*
 * mgmt
 */

static segment_buffer_t *segbuf_init(void)
{
  segment_buffer_t *buf = calloc(1, sizeof(segment_buffer_t));
  return buf;
}

static void segbuf_dispose(segment_buffer_t *buf)
{
  if (buf->buf)
    free (buf->buf);
  free (buf);
}

static void segbuf_reset(segment_buffer_t *buf)
{
  buf->segment_end = buf->segment_data = buf->buf;
  buf->len          = 0;
  buf->segment_len  = -1;
  buf->segment_type = 0;
  buf->error        = 0;
}

/*
 * assemble, parse
 */

static void segbuf_parse_segment_header(segment_buffer_t *buf)
{
  if (buf->len > 2) {
    buf->segment_type = buf->buf[0];
    buf->segment_len  = (buf->buf[1] << 8) | buf->buf[2];
    buf->segment_data = buf->buf + 3;
    buf->segment_end  = buf->segment_data + buf->segment_len;
    buf->error        = 0;

    if ( buf->segment_type < 0x14 ||
         ( buf->segment_type > 0x18 &&
           buf->segment_type != 0x80)) {
      XINE_HDMV_ERROR("unknown segment type 0x%02x, resetting\n", buf->segment_type);
      segbuf_reset(buf);
    }
  } else {
    buf->segment_len = -1;
    buf->error       = 1;
  }
}

static void segbuf_fill(segment_buffer_t *buf, uint8_t *data, size_t len)
{
  if (buf->len + len > buf->data_size) {
    buf->data_size = buf->len + len;
    if (buf->buf)
      buf->buf = realloc(buf->buf, buf->data_size);
    else
      buf->buf = malloc(buf->data_size);
  }

  memcpy(buf->buf + buf->len, data, len);
  buf->len += len;

  segbuf_parse_segment_header(buf);
}

static int segbuf_segment_complete(segment_buffer_t *buf)
{
  return (buf->segment_len >= 0) && (buf->len >= (unsigned)buf->segment_len + 3);
}

static void segbuf_skip_segment(segment_buffer_t *buf)
{
  if (segbuf_segment_complete (buf)) {
    buf->len -= buf->segment_len + 3;
    if (buf->len > 0)
      memmove(buf->buf, buf->buf + buf->segment_len + 3, buf->len);

    segbuf_parse_segment_header(buf);

    XINE_HDMV_TRACE("  skip_segment: %zd bytes left\n", buf->len);
  } else {
    XINE_HDMV_ERROR("  skip_segment: ERROR - %zd bytes queued, %d required\n",
                    buf->len, buf->segment_len);
    segbuf_reset (buf);
  }
}

/*
 * access segment data
 */

static uint8_t segbuf_segment_type(segment_buffer_t *buf)
{
  return buf->segment_type;
}

static size_t segbuf_data_length(segment_buffer_t *buf)
{
  ssize_t val = buf->segment_end - buf->segment_data;
  if (val < 0) val = 0;
  return (size_t)val;
}

static uint8_t segbuf_get_u8(segment_buffer_t *buf)
{
  if (!(buf->error = ++buf->segment_data > buf->segment_end))
    return buf->segment_data[-1];
  XINE_HDMV_ERROR("segbuf_get_u8: read failed (end of segment reached) !\n");
  return 0;
}

static uint16_t segbuf_get_u16(segment_buffer_t *buf)
{
  return (segbuf_get_u8(buf) << 8) | segbuf_get_u8(buf);
}

static uint32_t segbuf_get_u24(segment_buffer_t *buf)
{
  return (segbuf_get_u8(buf) << 16) | (segbuf_get_u8(buf) << 8) | segbuf_get_u8(buf);
}

/*
 * decode segments
 */

static subtitle_clut_t *segbuf_decode_palette(segment_buffer_t *buf)
{
  uint8_t palette_id             = segbuf_get_u8 (buf);
  uint8_t palette_version_number = segbuf_get_u8 (buf);

  size_t  len     = segbuf_data_length(buf);
  size_t  entries = len / 5;
  size_t  i;

  if (buf->error)
    return NULL;

  if (len % 5) {
    XINE_HDMV_ERROR("  decode_palette: segment size error (%zd ; expected %zd for %zd entries)\n",
                    len, (5 * entries), entries);
    return NULL;
  }
  XINE_HDMV_TRACE("decode_palette: %zd items (id %d, version %d)\n",
                  entries, palette_id, palette_version_number);

  /* convert to xine-lib clut */
  subtitle_clut_t *clut = calloc(1, sizeof(subtitle_clut_t));
  clut->id = palette_id;

  for (i = 0; i < entries; i++) {
    uint8_t index = segbuf_get_u8 (buf);
    uint8_t Y     = segbuf_get_u8 (buf);
    uint8_t Cr    = segbuf_get_u8 (buf);
    uint8_t Cb    = segbuf_get_u8 (buf);
    uint8_t alpha = segbuf_get_u8 (buf);
    clut->color[index] = (Y << 16) | (Cr << 8) | Cb;
    clut->trans[index] = alpha >> 4;
  }

  return clut;
}

static int segbuf_decode_rle(segment_buffer_t *buf, subtitle_object_t *obj)
{
  int x = 0, y = 0;
  int rle_size = sizeof(rle_elem_t) * obj->width / 16 * obj->height + 1;
  rle_elem_t *rlep = malloc(rle_size);

  free (obj->rle);
  obj->rle       = rlep;
  obj->data_size = rle_size;
  obj->num_rle   = 0;

  /* convert to xine-lib rle format */
  while (y < obj->height && !buf->error) {

    /* decode RLE element */
    uint8_t byte = segbuf_get_u8 (buf);
    if (byte != 0) {
      rlep->color = byte;
      rlep->len   = 1;
    } else {
      byte = segbuf_get_u8 (buf);
      if (!(byte & 0x80)) {
        rlep->color = 0;
        if (!(byte & 0x40))
          rlep->len   = byte & 0x3f;
        else
          rlep->len   = ((byte & 0x3f) << 8) | segbuf_get_u8 (buf);
      } else {
        if (!(byte & 0x40))
          rlep->len   = byte & 0x3f;
        else
          rlep->len   = ((byte & 0x3f) << 8) | segbuf_get_u8 (buf);
        rlep->color = segbuf_get_u8 (buf);
      }
    }

    /* move to next element */
    if (rlep->len > 0) {
      x += rlep->len;
      rlep++;
      obj->num_rle ++;
    } else {
      /* end of line marker (00 00) */
      if (x < obj->width) {
        rlep->len = obj->width - x;
        rlep->color = 0xff;
        rlep++;
        obj->num_rle ++;
      }
      x = 0;
      y++;
    }

    /* grow allocated RLE data size ? */
    if (obj->data_size <= (obj->num_rle + 1) * sizeof(rle_elem_t)) {
      obj->data_size *= 2;
      obj->rle = realloc(obj->rle, obj->data_size);
      rlep = obj->rle + obj->num_rle;
    }
  }

  return buf->error;
}

static subtitle_object_t *segbuf_decode_object(segment_buffer_t *buf, subtitle_object_t *objects)
{
  uint16_t object_id = segbuf_get_u16(buf);
  uint8_t  version   = segbuf_get_u8 (buf);
  uint8_t  seq_desc  = segbuf_get_u8 (buf);

  XINE_HDMV_TRACE("  decode_object: object_id %d, version %d, seq 0x%x\n",
                  object_id, version, seq_desc);

  if (seq_desc & 0x80) {
    /* new object (first-in-sequence flag set) */

    subtitle_object_t *obj = calloc(1, sizeof(subtitle_object_t));

    obj->id       = object_id;
    obj->data_len = segbuf_get_u24(buf);
    obj->width    = segbuf_get_u16(buf);
    obj->height   = segbuf_get_u16(buf);

    if (buf->error) {
      XINE_HDMV_TRACE("    decode error at object header\n");
      free_subtitle_object(obj);
      return NULL;
    }

    obj->data_len -= 4; /* width, height parsed */

    XINE_HDMV_TRACE("    object length %d bytes, size %dx%d\n", obj->data_len, obj->width, obj->height);

    if (obj->data_len > segbuf_data_length(buf)) {
      XINE_HDMV_TRACE("    object length %d bytes, have only %zd bytes -> missing %d bytes\n",
                      obj->data_len, segbuf_data_length(buf), obj->data_len - (int)segbuf_data_length(buf));

      if (obj->raw_data)
        free(obj->raw_data);

      /* store partial RLE data in HDMV format */
      obj->raw_data_len  = segbuf_data_length(buf);
      obj->raw_data_size = MAX(obj->data_len, obj->raw_data_len);
      obj->raw_data      = malloc(obj->raw_data_size);
      memcpy(obj->raw_data, buf->segment_data, obj->raw_data_len);

      return obj;
    }

    segbuf_decode_rle (buf, obj);

    if (buf->error) {
      XINE_HDMV_TRACE("    decode error at RLE data\n");
      free_subtitle_object(obj);
      return NULL;
    }

    return obj;
  }

  /* not first-of-sequence --> append data to already existing objct */

  /* search for object */
  while (objects && objects->id != object_id)
    objects = objects->next;

  if (!objects) {
    XINE_HDMV_TRACE("    object not found from list, discarding segment\n");
    return NULL;
  }

  /* store partial RLE data in HDMV format */
  if (objects->raw_data_size < objects->raw_data_len + segbuf_data_length(buf)) {
    XINE_HDMV_ERROR("object larger than object size !\n");
    return NULL;
  }
  memcpy(objects->raw_data + objects->raw_data_len, buf->segment_data, segbuf_data_length(buf));
  objects->raw_data_len  += segbuf_data_length(buf);

  /* if complete, decode RLE data */
  if (objects->raw_data_len >= objects->data_len) {
    /* create dummy buffer for segbuf_decode_rle */
    segment_buffer_t tmpbuf = {
      .segment_data = objects->raw_data,
      .segment_end  = objects->raw_data + objects->raw_data_len,
    };

    /* decode RLE data */
    segbuf_decode_rle (&tmpbuf, objects);

    if (tmpbuf.error) {
      XINE_HDMV_TRACE("    error decoding multi-segment object\n");
    }

    /* free decode buffer */
    free(objects->raw_data);
    objects->raw_data      = NULL;
    objects->raw_data_len  = 0;
    objects->raw_data_size = 0;
  }

  return NULL;
}

static window_def_t *segbuf_decode_window_definition(segment_buffer_t *buf)
{
  window_def_t *wnd = calloc(1, sizeof(window_def_t));

  uint8_t a   = segbuf_get_u8 (buf);
  wnd->id     = segbuf_get_u8 (buf);
  wnd->xpos   = segbuf_get_u16 (buf);
  wnd->ypos   = segbuf_get_u16 (buf);
  wnd->width  = segbuf_get_u16 (buf);
  wnd->height = segbuf_get_u16 (buf);

  XINE_HDMV_TRACE("  window: [%02x %d]  %d,%d %dx%d\n", a,
                  wnd->id, wnd->xpos, wnd->ypos, wnd->width, wnd->height);

  if (buf->error) {
    free(wnd);
    return NULL;
  }

  return wnd;
}

static int segbuf_decode_video_descriptor(segment_buffer_t *buf)
{
  uint16_t width      = segbuf_get_u16(buf);
  uint16_t height     = segbuf_get_u16(buf);
  uint8_t  frame_rate = segbuf_get_u8 (buf);

  XINE_HDMV_TRACE("  video_descriptor: %dx%d fps %d\n", width, height, frame_rate);

  return buf->error;
}

static int segbuf_decode_composition_descriptor(segment_buffer_t *buf, composition_descriptor_t *descr)
{
  descr->number = segbuf_get_u16(buf);
  descr->state  = segbuf_get_u8 (buf) & 0xc0;

  XINE_HDMV_TRACE("  composition_descriptor: number %d, state %d\n", descr->number, descr->state);
  return buf->error;
}

static composition_object_t *segbuf_decode_composition_object(segment_buffer_t *buf)
{
  composition_object_t *cobj = calloc(1, sizeof(composition_object_t));

  cobj->object_id_ref    = segbuf_get_u16 (buf);
  cobj->window_id_ref    = segbuf_get_u8 (buf);
  uint8_t tmp            = segbuf_get_u8 (buf);
  cobj->cropped_flag     = !!(tmp & 0x80);
  cobj->forced_flag      = !!(tmp & 0x40);
  cobj->xpos             = segbuf_get_u16 (buf);
  cobj->ypos             = segbuf_get_u16 (buf);
  if (cobj->cropped_flag) {
    /* x,y where to take the image from */
    cobj->crop_horiz_pos = segbuf_get_u8 (buf);
    cobj->crop_vert_pos  = segbuf_get_u8 (buf);
    /* size of the cropped image */
    cobj->crop_width     = segbuf_get_u8 (buf);
    cobj->crop_height    = segbuf_get_u8 (buf);
  }

  if (buf->error) {
    free(cobj);
    return NULL;
  }

  XINE_HDMV_TRACE("  composition_object: id: %d, win: %d, position %d,%d crop %d forced %d\n",
                  cobj->object_id_ref, cobj->window_id_ref, cobj->xpos, cobj->ypos,
                  cobj->cropped_flag, cobj->forced_flag);

  return cobj;
}

static presentation_segment_t *segbuf_decode_presentation_segment(segment_buffer_t *buf)
{
  presentation_segment_t *seg = calloc(1, sizeof(presentation_segment_t));
  int                     index;

  segbuf_decode_video_descriptor (buf);
  segbuf_decode_composition_descriptor (buf, &seg->comp_descr);

  seg->palette_update_flag  = !!((segbuf_get_u8(buf)) & 0x80);
  seg->palette_id_ref       = segbuf_get_u8 (buf);
  seg->object_number        = segbuf_get_u8 (buf);

  XINE_HDMV_TRACE("  presentation_segment: object_number %d, palette %d\n",
                  seg->object_number, seg->palette_id_ref);

  for (index = 0; index < seg->object_number; index++) {
    composition_object_t *cobj = segbuf_decode_composition_object (buf);
    cobj->next = seg->comp_objs;
    seg->comp_objs = cobj;
  }

  if (buf->error) {
    free_presentation_segment(seg);
    return NULL;
  }

  return seg;
}

static rle_elem_t *copy_crop_rle(subtitle_object_t *obj, composition_object_t *cobj)
{
  /* TODO: cropping (w,h sized image from pos x,y) */

  rle_elem_t *rle = calloc (obj->num_rle, sizeof(rle_elem_t));
  memcpy (rle, obj->rle, obj->num_rle * sizeof(rle_elem_t));
  return rle;
}


/*
 * xine plugin
 */

typedef struct {
  spu_decoder_class_t decoder_class;
} spuhdmv_class_t;

typedef struct spuhdmv_decoder_s {
  spu_decoder_t    spu_decoder;

  spuhdmv_class_t  *class;
  xine_stream_t    *stream;

  segment_buffer_t *buf;

  subtitle_clut_t        *cluts;
  subtitle_object_t      *objects;
  window_def_t           *windows;
  presentation_segment_t *segments;

  int overlay_handles[MAX_OBJECTS];

  int64_t               pts;

} spuhdmv_decoder_t;

static void free_objs(spuhdmv_decoder_t *this)
{
  LIST_DESTROY (this->cluts,    free);
  LIST_DESTROY (this->objects,  free_subtitle_object);
  LIST_DESTROY (this->windows,  free);
  LIST_DESTROY (this->segments, free_presentation_segment);
}

static int decode_palette(spuhdmv_decoder_t *this)
{
  /* decode */
  subtitle_clut_t *clut = segbuf_decode_palette(this->buf);
  if (!clut)
    return 1;

  LIST_REPLACE (this->cluts, clut, free);

  return 0;
}

static int decode_object(spuhdmv_decoder_t *this)
{
  /* decode */
  subtitle_object_t *obj = segbuf_decode_object(this->buf, this->objects);
  if (!obj)
    return 1;

  LIST_REPLACE (this->objects, obj, free_subtitle_object);

  return 0;
}

static int decode_window_definition(spuhdmv_decoder_t *this)
{
  /* decode */
  window_def_t *wnd = segbuf_decode_window_definition (this->buf);
  if (!wnd)
    return 1;

  LIST_REPLACE (this->windows, wnd, free);

  return 0;
}

static int decode_presentation_segment(spuhdmv_decoder_t *this)
{
  /* decode */
  presentation_segment_t *seg = segbuf_decode_presentation_segment(this->buf);
  if (!seg)
    return 1;

  seg->pts = this->pts;

  /* epoch start or acquistion point -> drop cached objects */
  if (seg->comp_descr.state) {
    free_objs(this);
  }

  /* replace */
  if (this->segments)
    LIST_DESTROY(this->segments, free_presentation_segment);
  this->segments = seg;

  return 0;
}

static int show_overlay(spuhdmv_decoder_t *this, composition_object_t *cobj, unsigned int palette_id_ref,
                        int overlay_index, int64_t pts, int force_update)
{
  video_overlay_manager_t *ovl_manager = this->stream->video_out->get_overlay_manager(this->stream->video_out);
  metronom_t              *metronom    = this->stream->metronom;
  video_overlay_event_t    event       = {0};
  vo_overlay_t             overlay     = {0};

  /* find palette */
  subtitle_clut_t *clut = this->cluts;
  while (clut && clut->id != palette_id_ref)
    clut = clut->next;
  if (!clut) {
    XINE_HDMV_TRACE("  show_overlay: clut %d not found !\n", palette_id_ref);
    return -1;
  }

  /* find RLE image */
  subtitle_object_t *obj = this->objects;
  while (obj && obj->id != cobj->object_id_ref)
    obj = obj->next;
  if (!obj) {
    XINE_HDMV_TRACE("  show_overlay: object %d not found !\n", cobj->object_id_ref);
    return -1;
  }
  if (!obj->rle) {
    XINE_HDMV_TRACE("  show_overlay: object %d RLE data not decoded !\n", cobj->object_id_ref);
    return -1;
  }

  /* find window */
  window_def_t *wnd = this->windows;
  while (wnd && wnd->id != cobj->window_id_ref)
    wnd = wnd->next;
  if (!wnd) {
    XINE_HDMV_TRACE("  show_overlay: window %d not found !\n", cobj->window_id_ref);
    return -1;
  }

  /* do not show again if all elements are unchanged */
  if (!force_update && clut->shown && obj->shown && wnd->shown && cobj->shown)
    return 0;
  clut->shown = obj->shown = wnd->shown = cobj->shown = 1;

  /* copy palette to xine overlay */
  overlay.rgb_clut = 0;
  memcpy(overlay.color, clut->color, sizeof(uint32_t) * 256);
  memcpy(overlay.trans, clut->trans, sizeof(uint8_t)  * 256);

  /* copy and crop RLE image to xine overlay */
  overlay.width     = obj->width;
  overlay.height    = obj->height;

  overlay.rle       = copy_crop_rle (obj, cobj);
  overlay.num_rle   = obj->num_rle;
  overlay.data_size = obj->num_rle * sizeof(rle_elem_t);

  /* */

  overlay.x = /*wnd->xpos +*/ cobj->xpos;
  overlay.y = /*wnd->ypos +*/ cobj->ypos;

  overlay.unscaled    = 0;
  overlay.hili_top    = -1;
  overlay.hili_bottom = -1;
  overlay.hili_left   = -1;
  overlay.hili_right  = -1;

  XINE_HDMV_TRACE("    -> overlay: %d,%d %dx%d\n",
                  overlay.x, overlay.y, overlay.width, overlay.height);


  /* set timings */

  if (pts > 0)
    event.vpts = metronom->got_spu_packet (metronom, pts);
  else
    event.vpts = 0;


  /* generate SHOW event */

  this->stream->video_out->enable_ovl(this->stream->video_out, 1);

  if (this->overlay_handles[overlay_index] < 0)
    this->overlay_handles[overlay_index] = ovl_manager->get_handle(ovl_manager, 0);

  event.event_type     = OVERLAY_EVENT_SHOW;
  event.object.handle  = this->overlay_handles[overlay_index];
  event.object.overlay = &overlay;
  event.object.object_type = 0; /* subtitle */

  ovl_manager->add_event (ovl_manager, (void *)&event);

  return 0;
}

static void hide_overlays(spuhdmv_decoder_t *this, int64_t pts)
{
  video_overlay_event_t event = {0};
  int i = 0;

  while (this->overlay_handles[i] >= 0) {
    XINE_HDMV_TRACE("    -> HIDE %d\n", i);

    video_overlay_manager_t *ovl_manager = this->stream->video_out->get_overlay_manager(this->stream->video_out);
    metronom_t              *metronom    = this->stream->metronom;

    event.object.handle = this->overlay_handles[i];
    if (this)
      event.vpts = metronom->got_spu_packet (metronom, pts);
    else
      event.vpts = 0;
    event.event_type = OVERLAY_EVENT_HIDE;
    event.object.overlay = NULL;
    ovl_manager->add_event (ovl_manager, (void *)&event);

    //this->overlay_handles[i] = -1;
    i++;
  }
}

static void update_overlays(spuhdmv_decoder_t *this)
{
  presentation_segment_t *pseg = this->segments;

  while (pseg) {

    if (!pseg->object_number) {

      /* HIDE */
      if (!pseg->shown)
        hide_overlays (this, pseg->pts);

    } else {

      /* SHOW */
      composition_object_t *cobj = pseg->comp_objs;
      int i;

      for (i = 0; i < pseg->object_number; i++) {
        if (!cobj) {
          XINE_HDMV_ERROR("show_overlays: composition object %d missing !\n", i);
        } else {
          show_overlay(this, cobj, pseg->palette_id_ref, i, pseg->pts, !pseg->shown);
          cobj = cobj->next;
        }
      }
    }

    pseg->shown = 1;

    pseg = pseg->next;
  }
}

static void decode_segment(spuhdmv_decoder_t *this)
{
  XINE_HDMV_TRACE("*** new segment, pts %010"PRId64": 0x%02x (%8d bytes)\n",
                  this->pts, this->buf->segment_type, this->buf->segment_len);

  switch (segbuf_segment_type(this->buf)) {
  case SEGTYPE_PALETTE:
    XINE_HDMV_TRACE("  segment: PALETTE\n");
    decode_palette(this);
    break;
  case SEGTYPE_OBJECT:
    XINE_HDMV_TRACE("  segment: OBJECT\n");
    decode_object(this);
    break;
  case SEGTYPE_PRESENTATION_SEGMENT:
    XINE_HDMV_TRACE("  segment: PRESENTATION SEGMENT\n");
    decode_presentation_segment(this);
    break;
  case SEGTYPE_WINDOW_DEFINITION:
    XINE_HDMV_TRACE("  segment: WINDOW DEFINITION\n");
    decode_window_definition(this);
    break;
  case SEGTYPE_INTERACTIVE:
    XINE_HDMV_TRACE("  segment: INTERACTIVE\n");
    break;
  case SEGTYPE_END_OF_DISPLAY:
    XINE_HDMV_TRACE("  segment: END OF DISPLAY\n");
#if 0
    /* drop all cached objects */
    free_objs(this);
#endif
    break;
  default:
    XINE_HDMV_ERROR("  segment type 0x%x unknown, skipping\n", segbuf_segment_type(this->buf));
    break;
  }
  if (this->buf->error) {
    XINE_HDMV_ERROR("*** DECODE ERROR ***\n");
  }

  update_overlays (this);
}

static void close_osd(spuhdmv_decoder_t *this)
{
  video_overlay_manager_t *ovl_manager = this->stream->video_out->get_overlay_manager (this->stream->video_out);

  int i = 0;
  while (this->overlay_handles[i] >= 0) {
    ovl_manager->free_handle(ovl_manager, this->overlay_handles[i]);
    this->overlay_handles[i] = -1;
    i++;
  }
}

static void spudec_decode_data (spu_decoder_t * this_gen, buf_element_t * buf)
{
  spuhdmv_decoder_t *this = (spuhdmv_decoder_t *) this_gen;

  if ((buf->type & 0xffff0000) != BUF_SPU_HDMV)
    return;

  if (buf->size < 1)
    return;

  if (buf->pts)
    this->pts = buf->pts;

#ifdef DUMP_SPU_DATA
  int i;
  for(i = 0; i < buf->size; i++)
    printf(" %02x", buf->content[i]);
  printf("\n");
#endif

  segbuf_fill(this->buf, buf->content, buf->size);

  while (segbuf_segment_complete(this->buf)) {
    decode_segment(this);
    segbuf_skip_segment(this->buf);
  }
}

static void spudec_reset (spu_decoder_t * this_gen)
{
  spuhdmv_decoder_t *this = (spuhdmv_decoder_t *) this_gen;

  if (this->buf)
    segbuf_reset(this->buf);

  free_objs(this);

  close_osd(this);
}

static void spudec_discontinuity (spu_decoder_t *this_gen)
{
  spuhdmv_decoder_t *this = (spuhdmv_decoder_t *) this_gen;

  close_osd(this);
}

static void spudec_dispose (spu_decoder_t *this_gen)
{
  spuhdmv_decoder_t  *this = (spuhdmv_decoder_t *) this_gen;

  close_osd (this);
  segbuf_dispose (this->buf);

  free_objs(this);

  free (this);
}

static spu_decoder_t *open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream)
{
  spuhdmv_decoder_t *this;

  this = (spuhdmv_decoder_t *) calloc(1, sizeof (spuhdmv_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;
  this->stream                          = stream;
  this->class                           = (spuhdmv_class_t *) class_gen;

  this->buf = segbuf_init();

  memset(this->overlay_handles, 0xff, sizeof(this->overlay_handles)); /* --> -1 */

  return &this->spu_decoder;
}

static void *init_plugin (xine_t *xine, void *data)
{
  spuhdmv_class_t *this;

  this = calloc(1, sizeof (spuhdmv_class_t));

  this->decoder_class.open_plugin = open_plugin;
  this->decoder_class.identifier  = "spuhdmv";
  this->decoder_class.description = "HDMV/BluRay bitmap SPU decoder plugin";
  this->decoder_class.dispose     = default_spu_decoder_class_dispose;

  return this;
}

/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_HDMV, 0 };

static const decoder_info_t dec_info_data = {
  supported_types,     /* supported types */
  5                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER, 17, "spuhdmv", XINE_VERSION_CODE, &dec_info_data, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
