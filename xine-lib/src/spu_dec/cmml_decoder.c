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
 */

#define LOG_MODULE "libspucmml"
#define LOG_VERBOSE
/*
#define LOG
*/
#define LOG_OSD 0
#define LOG_SCHEDULING 0
#define LOG_WIDTH 0

#define SUB_BUFSIZE 1024
#define SUB_MAX_TEXT  5

#include <xine/xine_internal.h>

typedef enum {
  SUBTITLE_SIZE_SMALL = 0,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;


typedef struct spucmml_class_s {
  spu_decoder_class_t class;
  char              *src_encoding;  /* encoding of subtitle file */
  xine_t            *xine;

} spucmml_class_t;


typedef struct cmml_anchor_s {
  char *text;
  char *href;
} cmml_anchor_t;


typedef struct spucmml_decoder_s {
  spu_decoder_t      spu_decoder;

  spucmml_class_t   *class;
  xine_stream_t     *stream;

  xine_event_queue_t *event_queue;

  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  int                cached_width;          /* frame width */
  int                cached_height;         /* frame height */
  int64_t            cached_img_duration;
  int                font_size;
  int                line_height;
  int                master_started;
  int                slave_started;

  char              *font;          /* subtitle font */
  subtitle_size      subtitle_size; /* size of subtitles */
  int                vertical_offset;

  osd_object_t      *osd;

  cmml_anchor_t      current_anchor;
} spucmml_decoder_t;


static void video_frame_format_change_callback (void *user_data, const xine_event_t *event);


static void update_font_size (spucmml_decoder_t *this) {
  static const int sizes[SUBTITLE_SIZE_NUM][4] = {
    { 16, 16, 16, 20 }, /* SUBTITLE_SIZE_SMALL  */
    { 16, 16, 20, 24 }, /* SUBTITLE_SIZE_NORMAL */
    { 16, 20, 24, 32 }, /* SUBTITLE_SIZE_LARGE  */
  };

  const int *const vec = sizes[this->subtitle_size];

  if( this->cached_width >= 512 )
    this->font_size = vec[3];
  else if( this->cached_width >= 384 )
    this->font_size = vec[2];
  else if( this->cached_width >= 320 )
    this->font_size = vec[1];
  else
    this->font_size = vec[0];

  this->line_height = this->font_size + 10;

  int y = this->cached_height - (SUB_MAX_TEXT * this->line_height) - 5;

  if(((y - this->vertical_offset) >= 0) && ((y - this->vertical_offset) <= this->cached_height))
    y -= this->vertical_offset;

  /* TODO: we should move this stuff below into another function */

  if (this->osd)
    this->stream->osd_renderer->free_object (this->osd);

  llprintf (LOG_OSD,
      "pre new_object: osd=%p, osd_renderer=%p, width=%d, height=%d\n",
      this->osd,
      this->stream->osd_renderer,
      this->cached_width,
      SUB_MAX_TEXT * this->line_height);

  this->osd = this->stream->osd_renderer->new_object (this->stream->osd_renderer,
          this->cached_width, SUB_MAX_TEXT * this->line_height);

  llprintf (LOG_OSD, "post new_object: osd is %p\n", this->osd);

  if(this->stream->osd_renderer) {
    this->stream->osd_renderer->set_font (this->osd, this->font, this->font_size);
    this->stream->osd_renderer->set_position (this->osd, 0, y);
  }
}

static int get_width(spucmml_decoder_t *this, char* text) {
  int width=0;

  while (1)
    switch (*text) {
    case '\0':
      llprintf(LOG_WIDTH, "get_width returning width of %d\n", width);
      return width;

    case '<':
      if (!strncmp("<b>", text, 3)) {
        /*Do somethink to enable BOLD typeface*/
	text += 3;
        break;
      } else if (!strncmp("</b>", text, 3)) {
        /*Do somethink to disable BOLD typeface*/
	text += 4;
        break;
      } else if (!strncmp("<i>", text, 3)) {
        /*Do somethink to enable italics typeface*/
	text += 3;
        break;
      } else if (!strncmp("</i>", text, 3)) {
        /*Do somethink to disable italics typeface*/
	text += 4;
        break;
      } else if (!strncmp("<font>", text, 3)) {
        /*Do somethink to disable typing
          fixme - no teststreams*/
	text += 6;
        break;
      } else if (!strncmp("</font>", text, 3)) {
        /*Do somethink to enable typing
          fixme - no teststreams*/
	text += 7;
        break;
      }
    default:
      {
	int w, dummy;
	const char letter[2] = { *text, '\0' };
	this->stream->osd_renderer->get_text_size(this->osd, letter, &w, &dummy);
	width += w;
	text++;
      }
    }
}

static void render_line(spucmml_decoder_t *this, int x, int y, char* text) {
  while (*text != '\0') {
    int w, dummy;
    const char letter[2] = { *text, '\0' };

    this->stream->osd_renderer->render_text(this->osd, x, y, letter, OSD_TEXT1);
    this->stream->osd_renderer->get_text_size(this->osd, letter, &w, &dummy);
    x += w;
    text++;
  }
}

static void draw_subtitle(spucmml_decoder_t *this, int64_t sub_start) {

  this->stream->osd_renderer->filled_rect (this->osd, 0, 0,
      this->cached_width-1, this->line_height * SUB_MAX_TEXT - 1, 0);

  const int y = (SUB_MAX_TEXT - this->lines) * this->line_height;
  int font_size = this->font_size;
  this->stream->osd_renderer->set_encoding(this->osd, this->class->src_encoding);

  int line;

  for (line=0; line<this->lines; line++) {
    int x;
    while(1) {
      const int w = get_width( this, this->text[line]);
      x = (this->cached_width - w) / 2;

      if( w > this->cached_width && font_size > 16 ) {
        font_size -= 4;
        this->stream->osd_renderer->set_font (this->osd, this->font, font_size);
      } else {
        break;
      }
    }
    render_line(this, x, y + line*this->line_height, this->text[line]);
  }

  if( font_size != this->font_size )
    this->stream->osd_renderer->set_font (this->osd, this->font, this->font_size);


  this->stream->osd_renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
  this->stream->osd_renderer->show (this->osd, sub_start);

  llprintf (LOG_SCHEDULING,
      "spucmml: scheduling subtitle >%s< at %"PRId64", current time is %"PRId64"\n",
      this->text[0], sub_start,
      this->stream->xine->clock->get_current_time (this->stream->xine->clock));
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  spucmml_decoder_t *this = (spucmml_decoder_t *) this_gen;

  xml_parser_t *xml_parser;
  xml_node_t *packet_xml_root;
  char * anchor_text = NULL;

  lprintf("CMML packet seen\n");

  char *str = (char *) buf->content;

  /* parse the CMML */

  xml_parser = xml_parser_init_r (str, strlen (str), XML_PARSER_CASE_INSENSITIVE);
  if (xml_parser_build_tree_r(xml_parser, &packet_xml_root) != XML_PARSER_OK) {
    lprintf ("warning: invalid XML packet detected in CMML track\n");
    xml_parser_finalize_r(xml_parser);
    return;
  }

  xml_parser_finalize_r(xml_parser);

  if (strcasecmp(packet_xml_root->name, "head") == 0) {
    /* found a <head>...</head> packet: need to parse the title */

    xml_node_t *title_node;

    /* iterate through children trying to find the title node */

    for (title_node = packet_xml_root->child; title_node != NULL; title_node = title_node->next) {

      if (title_node->data &&
	  strcasecmp (title_node->name, "title") == 0) {
        /* found a title node */

	xine_ui_data_t data = {
	  .str_len = strlen(title_node->data) + 1
	};
	xine_event_t uevent = {
	  .type = XINE_EVENT_UI_SET_TITLE,
	  .stream = this->stream,
	  .data = &data,
	  .data_length = sizeof(data),
	};
	strncpy(data.str, title_node->data, sizeof(data.str)-1);

	/* found a non-empty title */
	lprintf ("found title: \"%s\"\n", data.str);

	/* set xine meta-info */
	_x_meta_info_set(this->stream, XINE_META_INFO_TITLE, strdup(data.str));

	/* and push out a new event signifying the title update on the event
	 * queue */
	xine_event_send(this->stream, &uevent);
      }
    }
  } else if (strcasecmp(packet_xml_root->name, "clip") == 0) {
    /* found a <clip>...</clip> packet: search for the <a href="..."> in it */
    xml_node_t *clip_node;

    /* iterate through each tag contained in the <clip> tag to look for <a> */

    for (clip_node = packet_xml_root->child; clip_node != NULL; clip_node = clip_node->next) {

      if (strcasecmp (clip_node->name, "a") == 0) {
        xml_property_t *href_property;

        /* found the <a> tag: grab its value and its href property */

        if (clip_node->data)
          anchor_text = strdup (clip_node->data);

        for (href_property = clip_node->props; href_property != NULL; href_property = href_property->next) {
          if (strcasecmp (href_property->name, "href") == 0) {
            /* found the href property */
            char *href = href_property->value;

            if (href) {
              lprintf ("found href: \"%s\"\n", href);
              this->current_anchor.href = strdup(href);
            }
          }
        }
      }
    }
  }

  /* finish here if we don't have to process any anchor text */
  if (!anchor_text)
    return;

  /* how many lines does the anchor text take up? */
  this->lines=0;
  {
    int i = 0, index = 0;
    while (anchor_text[index]) {
      if (anchor_text[index] == '\r' || anchor_text[index] == '\n') {
        if (i) {
          /* match a newline and there are chars on the current line ... */
          this->text[ this->lines ][i] = '\0';
          this->lines++;
          i = 0;
        }
      } else {
        /* found a normal (non-line-ending) character */
        this->text[ this->lines ][i] = anchor_text[index];
        if (i<SUB_BUFSIZE-1)
          i++;
      }
      index++;
    }

    /* always NULL-terminate the string */
    if (i) {
      this->text[ this->lines ][i] = '\0';
      this->lines++;
    }
  }
  free (anchor_text);

  /* initialize decoder if needed */
  if( !this->cached_width || !this->cached_height || !this->cached_img_duration || !this->osd ) {
    if( this->stream->video_out->status(this->stream->video_out, NULL,
          &this->cached_width, &this->cached_height, &this->cached_img_duration )) {
      if( this->cached_width && this->cached_height && this->cached_img_duration ) {
        lprintf("this->stream->osd_renderer is %p\n", this->stream->osd_renderer);
      }
    }
  }

  update_font_size (this);

  if( this->osd ) {
    draw_subtitle(this, buf->pts);
    return;
  } else {
    lprintf ("libspucmml: no osd\n");
  }

  return;
}

static void video_frame_format_change_callback (void *user_data, const xine_event_t *event)
{
  /* this doesn't do anything for now: it's a start at attempting to display
   * CMML clips which occur at 0 seconds into the track.  see
   *
   *   http://marc.theaimsgroup.com/?l=xine-devel&m=109202443013890&w=2
   *
   * for a description of the problem. */

  switch (event->type) {
    case XINE_EVENT_FRAME_FORMAT_CHANGE:
      lprintf("video_frame_format_change_callback called!\n");
      break;
    default:
      lprintf("video_frame_format_change_callback called with unknown event %d\n", event->type);
      break;
  }
}

static void spudec_reset (spu_decoder_t *this_gen) {
  spucmml_decoder_t *this = (spucmml_decoder_t *) this_gen;

  this->cached_width = this->cached_height = 0;
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  /* do nothing */
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spucmml_decoder_t *this = (spucmml_decoder_t *) this_gen;

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  if (this->osd) {
    this->stream->osd_renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_vertical_offset(void *this_gen, xine_cfg_entry_t *entry)
{
  spucmml_decoder_t *this = (spucmml_decoder_t *)this_gen;

  this->vertical_offset = entry->num_value;
  update_font_size(this);
}

static void update_osd_font(void *this_gen, xine_cfg_entry_t *entry)
{
  spucmml_decoder_t *this = (spucmml_decoder_t *)this_gen;

  this->font = entry->str_value;

  if( this->stream->osd_renderer )
    this->stream->osd_renderer->set_font (this->osd, this->font, this->font_size);
}

static spu_decoder_t *spucmml_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {
  spucmml_class_t *class = (spucmml_class_t *)class_gen;
  spucmml_decoder_t *this = (spucmml_decoder_t *) calloc(1, sizeof(spucmml_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;
  this->spu_decoder.dispose             = spudec_dispose;

  this->class  = class;
  this->stream = stream;

  this->event_queue = xine_event_new_queue (this->stream);
  xine_event_create_listener_thread (this->event_queue,
                                     video_frame_format_change_callback,
                                     this);

  this->font_size = 24;
  this->subtitle_size = 1;

  this->font             = class->xine->config->register_string(class->xine->config,
                              "subtitles.separate.font",
                              "sans",
                              _("font for external subtitles"),
                              NULL, 0, update_osd_font, this);

  this->vertical_offset  = class->xine->config->register_num(class->xine->config,
                              "subtitles.separate.vertical_offset",
                              0,
                              _("subtitle vertical offset (relative window size)"),
                              NULL, 0, update_vertical_offset, this);

  this->current_anchor.href = NULL;

  lprintf ("video_out is at %p\n", this->stream->video_out);

  return (spu_decoder_t *) this;
}

static void update_src_encoding(void *this_gen, xine_cfg_entry_t *entry)
{
  spucmml_class_t *this = (spucmml_class_t *)this_gen;

  this->src_encoding = entry->str_value;
  printf("libspucmml: spu_src_encoding = %s\n", this->src_encoding );
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {
  spucmml_class_t *this = (spucmml_class_t *) calloc(1, sizeof(spucmml_class_t));

  this->class.open_plugin      = spucmml_class_open_plugin;
  this->class.identifier       = "spucmml";
  this->class.description      = N_("CMML subtitle decoder plugin");
  this->class.dispose          = default_spu_decoder_class_dispose;

  this->xine                   = xine;

  this->src_encoding  = xine->config->register_string(xine->config,
                                "subtitles.separate.src_encoding",
                                "iso-8859-1",
                                _("encoding of subtitles"),
                                NULL, 10, update_src_encoding, this);

  return &this->class;
}


/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_CMML, 0 };

static const decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_SPU_DECODER, 17, "spucmml", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

