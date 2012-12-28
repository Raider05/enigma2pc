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

/* mpeg1 encoding video out plugin for the dxr3.
 *
 * modifications to the original dxr3 video out plugin by
 * Mike Lampard <mlampard at users.sourceforge.net>
 * this first standalone version by
 * Harm van der Heijden <hrm at users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#if defined(__sun)
#include <sys/ioccom.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#ifdef HAVE_X11
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#  include <X11/Xutil.h>
#endif
#ifdef HAVE_XINERAMA
#  include <X11/extensions/Xinerama.h>
#endif

#define LOG_MODULE "video_out_dxr3"
/* #define LOG_VERBOSE */
/* #define LOG */

#define LOG_VID 0
#define LOG_OVR 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/video_out.h>
#include "dxr3.h"
#include "video_out_dxr3.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#include "compat.c"

/* the amount of extra time we give the card for decoding */
#define DECODE_PIPE_PREBUFFER 10000


/* plugin class initialization functions */
static void                *dxr3_x11_init_plugin(xine_t *xine, void *visual_gen);
static void                *dxr3_aa_init_plugin(xine_t *xine, void *visual_gen);
static dxr3_driver_class_t *dxr3_vo_init_plugin(xine_t *xine, void *visual_gen);


/* plugin catalog information */
#ifdef HAVE_X11
static const vo_info_t   vo_info_dxr3_x11 = {
  10,                  /* priority        */
  XINE_VISUAL_TYPE_X11 /* visual type     */
};
#endif

static const vo_info_t   vo_info_dxr3_aa = {
  10,                  /* priority        */
  XINE_VISUAL_TYPE_AA  /* visual type     */
};

const plugin_info_t      xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
#ifdef HAVE_X11
  { PLUGIN_VIDEO_OUT, 22, "dxr3", XINE_VERSION_CODE, &vo_info_dxr3_x11, &dxr3_x11_init_plugin },
#endif
  { PLUGIN_VIDEO_OUT, 22, "aadxr3", XINE_VERSION_CODE, &vo_info_dxr3_aa, &dxr3_aa_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin class functions */
static vo_driver_t *dxr3_vo_open_plugin(video_driver_class_t *class_gen, const void *visual);
static void         dxr3_vo_class_dispose(video_driver_class_t *class_gen);

/* plugin instance functions */
static uint32_t    dxr3_get_capabilities(vo_driver_t *this_gen);
static vo_frame_t *dxr3_alloc_frame(vo_driver_t *this_gen);
static void        dxr3_frame_proc_frame(vo_frame_t *frame_gen);
static void        dxr3_frame_proc_slice(vo_frame_t *frame_gen, uint8_t **src);
static void        dxr3_frame_field(vo_frame_t *vo_img, int which_field);
static void        dxr3_frame_dispose(vo_frame_t *frame_gen);
static void        dxr3_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                            uint32_t width, uint32_t height,
                                            double ratio, int format, int flags);
static void        dxr3_overlay_begin(vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed);
static void        dxr3_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                      vo_overlay_t *overlay);
static void        dxr3_overlay_end(vo_driver_t *this_gen, vo_frame_t *frame_gen);
static void        dxr3_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen);
static int         dxr3_redraw_needed(vo_driver_t *this_gen);
static int         dxr3_get_property(vo_driver_t *this_gen, int property);
static int         dxr3_set_property(vo_driver_t *this_gen, int property, int value);
static void        dxr3_get_property_min_max(vo_driver_t *this_gen, int property,
                                             int *min, int *max);
static int         dxr3_gui_data_exchange(vo_driver_t *this_gen,
                                          int data_type, void *data);
static void        dxr3_dispose(vo_driver_t *this_gen);

/* overlay helper functions only called once during plugin init */
static void        gather_screen_vars(dxr3_driver_t *this, const x11_visual_t *vis);
static int         dxr3_overlay_read_state(dxr3_overlay_t *this);
static int         dxr3_overlay_set_keycolor(dxr3_overlay_t *this);
static int         dxr3_overlay_set_attributes(dxr3_overlay_t *this);

/* overlay helper functions */
static void        dxr3_overlay_update(dxr3_driver_t *this);
static void        dxr3_zoomTV(dxr3_driver_t *this);

/* config callbacks */
static void        dxr3_update_add_bars(void *data, xine_cfg_entry_t *entry);
static void        dxr3_update_swap_fields(void *data, xine_cfg_entry_t *entry);
static void        dxr3_update_enhanced_mode(void *this_gen, xine_cfg_entry_t *entry);


#ifdef HAVE_X11
static void *dxr3_x11_init_plugin(xine_t *xine, void *visual_gen)
{
  dxr3_driver_class_t *this = dxr3_vo_init_plugin(xine, visual_gen);

  if (!this) return NULL;
  this->visual_type = XINE_VISUAL_TYPE_X11;
  return &this->video_driver_class;
}
#endif

static void *dxr3_aa_init_plugin(xine_t *xine, void *visual_gen)
{
  dxr3_driver_class_t *this = dxr3_vo_init_plugin(xine, visual_gen);

  if (!this) return NULL;
  this->visual_type = XINE_VISUAL_TYPE_AA;
  return &this->video_driver_class;
}

static dxr3_driver_class_t *dxr3_vo_init_plugin(xine_t *xine, void *visual_gen)
{
  dxr3_driver_class_t *this;

  this = calloc(1, sizeof(dxr3_driver_class_t));
  if (!this) return NULL;

  this->devnum = xine->config->register_num(xine->config,
    CONF_KEY, 0, CONF_NAME, CONF_HELP, 10, NULL, NULL);

  this->video_driver_class.open_plugin     = dxr3_vo_open_plugin;
  this->video_driver_class.identifier      = DXR3_VO_ID;
  this->video_driver_class.description     = N_("video output plugin displaying images through your DXR3 decoder card");
  this->video_driver_class.dispose         = dxr3_vo_class_dispose;

  this->xine                               = xine;

  this->instance                           = 0;

  this->scr                                = dxr3_scr_init(xine);

  return this;
}

static void dxr3_vo_class_dispose(video_driver_class_t *class_gen)
{
  dxr3_driver_class_t *class = (dxr3_driver_class_t *)class_gen;

  if(class->scr)
    class->scr->scr_plugin.exit(&class->scr->scr_plugin);
  free(class_gen);
}


static vo_driver_t *dxr3_vo_open_plugin(video_driver_class_t *class_gen, const void *visual_gen)
{
  dxr3_driver_t *this;
  dxr3_driver_class_t *class = (dxr3_driver_class_t *)class_gen;
  config_values_t *config = class->xine->config;
  char tmpstr[100];
  const char *confstr;
  int encoder, confnum;
  static char *available_encoders[SUPPORTED_ENCODER_COUNT + 2];
  plugin_node_t *node;

  static const char *const videoout_modes[] = {
    "letterboxed tv", "widescreen tv",
#ifdef HAVE_X11
    "letterboxed overlay", "widescreen overlay",
#endif
    NULL
  };
  static const char *const tv_modes[] = { "ntsc", "pal", "pal60" , "default", NULL };
  int list_id, list_size;
  xine_sarray_t *plugin_list;

  if (class->instance) return NULL;

  this = calloc(1, sizeof(dxr3_driver_t));
  if (!this) return NULL;

  this->vo_driver.get_capabilities     = dxr3_get_capabilities;
  this->vo_driver.alloc_frame          = dxr3_alloc_frame;
  this->vo_driver.update_frame_format  = dxr3_update_frame_format;
  this->vo_driver.overlay_begin        = dxr3_overlay_begin;
  this->vo_driver.overlay_blend        = dxr3_overlay_blend;
  this->vo_driver.overlay_end          = dxr3_overlay_end;
  this->vo_driver.display_frame        = dxr3_display_frame;
  this->vo_driver.redraw_needed        = dxr3_redraw_needed;
  this->vo_driver.get_property         = dxr3_get_property;
  this->vo_driver.set_property         = dxr3_set_property;
  this->vo_driver.get_property_min_max = dxr3_get_property_min_max;
  this->vo_driver.gui_data_exchange    = dxr3_gui_data_exchange;
  this->vo_driver.dispose              = dxr3_dispose;

  pthread_mutex_init(&this->video_device_lock, NULL);
  pthread_mutex_init(&this->spu_device_lock, NULL);

  _x_vo_scale_init(&this->scale, 0, 0, config);
  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->class                          = class;
  this->swap_fields                    = config->register_bool(config,
    "dxr3.encoding.swap_fields", 0, _("swap odd and even lines"),
    _("Swaps the even and odd field of the image.\nEnable this option for "
      "non-MPEG material which produces a vertical jitter on screen."),
    10, dxr3_update_swap_fields, this);
  this->add_bars                       = config->register_bool(config,
    "dxr3.encoding.add_bars", 1, _("add black bars to correct aspect ratio"),
    _("Adds black bars when the image has an aspect ratio the card cannot "
      "handle natively. This is needed to maintain proper image proportions."),
    20, dxr3_update_add_bars, this);
  this->enhanced_mode                  = config->register_bool(config,
    "dxr3.encoding.alt_play_mode", 1,
    _("use smooth play mode for mpeg encoder playback"),
    _("Enabling this option will utilise a smoother play mode for non-MPEG content."),
    20, dxr3_update_enhanced_mode, this);

  snprintf(tmpstr, sizeof(tmpstr), "/dev/em8300-%d", class->devnum);
  llprintf(LOG_VID, "Entering video init, devname = %s.\n", tmpstr);
  if ((this->fd_control = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {

    xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	    _("video_out_dxr3: Failed to open control device %s (%s)\n"), tmpstr, strerror(errno));

    return 0;
  }

  snprintf (tmpstr, sizeof(tmpstr), "/dev/em8300_mv-%d", class->devnum);
  if ((this->fd_video = xine_open_cloexec(tmpstr, O_WRONLY | O_SYNC )) < 0) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	    _("video_out_dxr3: Failed to open video device %s (%s)\n"), tmpstr, strerror(errno));
    return 0;
  }
  /* close now and and let the decoder/encoder reopen if they want */
  close(this->fd_video);
  this->fd_video = -1;

  /* which encoder to use? Whadda we got? */
  encoder = 0;
#if LOG_VID
  printf("video_out_dxr3: Supported mpeg encoders: ");
#endif
  available_encoders[encoder++] = "libavcodec";
  printf("libavcodec, ");
#ifdef HAVE_LIBFAME
  available_encoders[encoder++] = "fame";
#if LOG_VID
  printf("fame, ");
#endif
#endif
#ifdef HAVE_LIBRTE
  available_encoders[encoder++] = "rte";
#if LOG_VID
  printf("rte, ");
#endif
#endif
  available_encoders[encoder] = "none";
  available_encoders[encoder + 1] = NULL;
#if LOG_VID
  printf("none\n");
#endif
  if (encoder) {
    encoder = config->register_enum(config, "dxr3.encoding.encoder", 0,
      available_encoders, _("encoder for non mpeg content"),
      _("Content other than MPEG has to pass an additional reencoding stage, "
	"because the dxr3 handles only MPEG.\nDepending on what is supported by your xine, "
	"this setting can be \"fame\", \"rte\", \"libavcodec\" or \"none\".\n"
	"The \"libavcodec\" encoder makes use of the ffmpeg plugin that already ships with xine, "
	"so you do not need to install any additional library for that. Even better is that "
	"libavcodec also provides high quality with low CPU usage. Using \"libavcodec\" is "
	"therefore strongly suggested.\n\"fame\" and \"rte\" are still there, "
	"but xine support for them is outdated, so these might fail to work."),
      0, NULL, NULL);
    if ((strcmp(available_encoders[encoder], "libavcodec") == 0) && !dxr3_lavc_init(this, node)) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: Mpeg encoder libavcodec failed to init.\n"));
      return 0;
    }
#ifdef HAVE_LIBRTE
    if ((strcmp(available_encoders[encoder], "rte") == 0) && !dxr3_rte_init(this)) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: Mpeg encoder rte failed to init.\n"));
      return 0;
    }
#endif
#ifdef HAVE_LIBFAME
    if ((strcmp(available_encoders[encoder], "fame") == 0) && !dxr3_fame_init(this)) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: Mpeg encoder fame failed to init.\n"));
      return 0;
    }
#endif
    if (strcmp(available_encoders[encoder], "none") == 0)
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: Mpeg encoding disabled.\n"
		"video_out_dxr3: that's ok, you don't need it for mpeg video like DVDs, but\n"
		"video_out_dxr3: you will not be able to play non-mpeg content using this video out\n"
		"video_out_dxr3: driver. See the README.dxr3 for details on configuring an encoder.\n"));
  } else
    xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	    _("video_out_dxr3: No mpeg encoder compiled in.\n"
	      "video_out_dxr3: that's ok, you don't need it for mpeg video like DVDs, but\n"
	      "video_out_dxr3: you will not be able to play non-mpeg content using this video out\n"
	      "video_out_dxr3: driver. See the README.dxr3 for details on configuring an encoder.\n"));

  /* init aspect */
  this->aspect = dxr3_set_property(&this->vo_driver, VO_PROP_ASPECT_RATIO, XINE_VO_ASPECT_4_3);

  /* init brightness/contrast/saturation */
  dxr3_set_property(&this->vo_driver, VO_PROP_BRIGHTNESS, 500);
  dxr3_set_property(&this->vo_driver, VO_PROP_CONTRAST  , 500);
  dxr3_set_property(&this->vo_driver, VO_PROP_SATURATION, 500);

  /* overlay or tvout? */
  confnum = config->register_enum(config, "dxr3.output.mode", 0, videoout_modes,
    _("video output mode (TV or overlay)"),
    _("The way the DXR3 outputs the final video can be set here. The individual values are:\n\n"
      "letterboxed tv\n"
      "Send video to the TV out connector only. This is the mode used for the standard 4:3 "
      "television set. Anamorphic (16:9) video will be displayed letterboxed, pan&scan "
      "material will have the image cropped at the left and right side. This is the common "
      "setting for TV viewing and acts like a standalone DVD player.\n\n"
      "widescreen tv\n"
      "Send video to the tv out connector only. This mode is intended for 16:9 widescreen TV sets. "
      "Anamorphic and pan&scan content will fill the entire screen, but you have to set the "
      "TV's aspect ratio manually to 16:9 using your.\n\n"
      "letterboxed overlay\n"
      "Overlay Video output on the computer screen with the option of on-the-fly switching "
      "to TV out by hiding the video window. The overlay will be displayed with black borders "
      "if it is anamorphic (16:9).\n"
      "This setting is only useful in the rare case of a DVD subtitle channel that would "
      "only display properly in letterbox mode. A good example for that are the animated "
      "commentator's silhouettes on \"Ghostbusters\".\n\n"
      "widescreen overlay\n"
      "Overlay Video output on the computer screen with the option of on-the-fly switching "
      "to TV out by hiding the video window. This is the common variant of DXR3 overlay."),
    0, NULL, NULL);
  if (!(class->visual_type == XINE_VISUAL_TYPE_X11) && confnum > 1)
    /* no overlay modes when not using X11 -> switch to letterboxed tv */
    confnum = 0;
  llprintf(LOG_VID, "videomode = %s\n", videoout_modes[confnum]);
  switch (confnum) {
  case 0: /* letterboxed tv mode */
    this->overlay_enabled = 0;
    this->tv_switchable = 0;  /* don't allow on-the-fly switching */
    this->widescreen_enabled = 0;
    break;
  case 1: /* widescreen tv mode */
    this->overlay_enabled = 0;
    this->tv_switchable = 0;  /* don't allow on-the-fly switching */
    this->widescreen_enabled = 1;
    break;
#ifdef HAVE_X11
  case 2: /* letterboxed overlay mode */
  case 3: /* widescreen overlay mode */
    llprintf(LOG_VID, "setting up overlay mode\n");
    gather_screen_vars(this, visual_gen);
    this->overlay.xine = this->class->xine;
    if (dxr3_overlay_read_state(&this->overlay) == 0) {
      this->overlay_enabled = 1;
      this->tv_switchable = 1;
      this->widescreen_enabled = confnum - 2;
      confstr = config->register_string(config, "dxr3.output.keycolor", "0x80a040",
	_("overlay colour key value"), _("Hexadecimal RGB value of the key colour.\n"
	"You can try different values, if you experience windows becoming transparent "
	"when using DXR3 overlay mode."), 20, NULL, NULL);
      sscanf(confstr, "%x", &this->overlay.colorkey);
      confstr = config->register_string(config, "dxr3.output.keycolor_interval", "50.0",
	_("overlay colour key tolerance"), _("A greater value widens the tolerance for "
	"the overlay key colour.\nYou can try lower values, if you experience windows "
	"becoming transparent when using DXR3 overlay mode, but parts of the image borders may "
	"disappear when using a too low setting."), 20, NULL, NULL);
      sscanf(confstr, "%f", &this->overlay.color_interval);
      this->overlay.shrink = config->register_num(config, "dxr3.output.shrink_overlay_area", 0,
	_("crop the overlay area at top and bottom"),
        _("Removes one pixel line from the top and bottom of the overlay. Enable this, if "
	"you see green lines at the top or bottom of the overlay."), 10, NULL, NULL);
    } else {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: please run autocal, overlay disabled\n"));
      this->overlay_enabled = 0;
      this->tv_switchable = 0;
      this->widescreen_enabled = 0;
    }
#endif
  }

  /* init tvmode */
  confnum = config->register_enum(config, "dxr3.output.tvmode", 3, tv_modes,
    _("preferred tv mode"), _("Selects the TV mode to be used by the DXR3. The values mean:\n\n"
    "ntsc: NTSC at 60Hz\npal: PAL at 50Hz\npal60: PAL at 60Hz\ndefault: keep the card's setting"),
    0, NULL, NULL);
  switch (confnum) {
  case 0: /* ntsc */
    this->tv_mode = EM8300_VIDEOMODE_NTSC;
    llprintf(LOG_VID, "setting tv_mode to NTSC\n");
    break;
  case 1: /* pal */
    this->tv_mode = EM8300_VIDEOMODE_PAL;
    llprintf(LOG_VID, "setting tv_mode to PAL 50Hz\n");
    break;
  case 2: /* pal60 */
    this->tv_mode = EM8300_VIDEOMODE_PAL60;
    llprintf(LOG_VID, "setting tv_mode to PAL 60Hz\n");
    break;
  default:
    this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
  }
  if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
    if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: setting video mode failed.\n"));

#ifdef HAVE_X11
  /* initialize overlay */
  if (this->overlay_enabled) {
    em8300_overlay_screen_t scr;
    int value;
    XColor dummy;

    this->overlay.fd_control = this->fd_control;

    /* allocate keycolor */
    this->key.red   = ((this->overlay.colorkey >> 16) & 0xff) * 256;
    this->key.green = ((this->overlay.colorkey >>  8) & 0xff) * 256;
    this->key.blue  = ((this->overlay.colorkey      ) & 0xff) * 256;
    XAllocColor(this->display, DefaultColormap(this->display, 0), &this->key);

    /* allocate black for output area borders */
    XAllocNamedColor(this->display, DefaultColormap(this->display, 0),
      "black", &this->black, &dummy);

    /* set the screen */
    scr.xsize = this->overlay.screen_xres;
    scr.ysize = this->overlay.screen_yres;
    if (ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETSCREEN, &scr))
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: setting the overlay screen failed.\n");

    if (dxr3_overlay_set_keycolor(&this->overlay) != 0)
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: setting the overlay key colour failed.\n");
    if (dxr3_overlay_set_attributes(&this->overlay) != 0)
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: setting an overlay attribute failed.\n");

    /* finally switch to overlay mode */
    value = EM8300_OVERLAY_MODE_OVERLAY;
    if (ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &value) != 0)
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: switching to overlay mode failed.\n");
  }
#endif

  return &this->vo_driver;
}


static uint32_t dxr3_get_capabilities(vo_driver_t *this_gen)
{
  return VO_CAP_YV12 | VO_CAP_YUY2;
}

static vo_frame_t *dxr3_alloc_frame(vo_driver_t *this_gen)
{
  dxr3_frame_t *frame;
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  frame = calloc(1, sizeof(dxr3_frame_t));

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  if (this->enc && this->enc->on_frame_copy) {
    frame->vo_frame.proc_frame = NULL;
    frame->vo_frame.proc_slice = dxr3_frame_proc_slice;
  } else {
    frame->vo_frame.proc_frame = dxr3_frame_proc_frame;
    frame->vo_frame.proc_slice = NULL;
  }
  frame->vo_frame.field   = dxr3_frame_field;
  frame->vo_frame.dispose = dxr3_frame_dispose;
  frame->vo_frame.driver  = this_gen;

  return &frame->vo_frame;
}

static void dxr3_frame_proc_frame(vo_frame_t *frame_gen)
{
  /* we reduce the vpts to give the card some extra decoding time */
  if (frame_gen->format != XINE_IMGFMT_DXR3 && !frame_gen->proc_called)
    frame_gen->vpts -= DECODE_PIPE_PREBUFFER;

  frame_gen->proc_called = 1;
}

static void dxr3_frame_proc_slice(vo_frame_t *frame_gen, uint8_t **src)
{
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;
  dxr3_driver_t *this = (dxr3_driver_t *)frame_gen->driver;

  /* we reduce the vpts to give the card some extra decoding time */
  if (frame_gen->format != XINE_IMGFMT_DXR3 && !frame_gen->proc_called)
    frame_gen->vpts -= DECODE_PIPE_PREBUFFER;

  frame_gen->proc_called = 1;

  if (frame_gen->format != XINE_IMGFMT_DXR3 && this->enc && this->enc->on_frame_copy)
    this->enc->on_frame_copy(this, frame, src);
}

static void dxr3_frame_field(vo_frame_t *vo_img, int which_field)
{
  /* dummy function */
}

static void dxr3_frame_dispose(vo_frame_t *frame_gen)
{
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;

  av_free(frame->mem);
  pthread_mutex_destroy(&frame_gen->mutex);
  free(frame);
}

static void dxr3_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen,
  uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;
  uint32_t oheight;

  if (format == XINE_IMGFMT_DXR3) { /* talking to dxr3 decoder */
    /* a bit of a hack. we must release the em8300_mv fd for
     * the dxr3 decoder plugin */
    pthread_mutex_lock(&this->video_device_lock);
    if (this->fd_video >= 0) {
      metronom_clock_t *clock = this->class->xine->clock;

      clock->unregister_scr(clock, &this->class->scr->scr_plugin);
      close(this->fd_video);
      this->fd_video = -1;
      /* inform the encoder on next frame's arrival */
      this->need_update = 1;
    }
    pthread_mutex_unlock(&this->video_device_lock);

    /* for mpeg source, we don't have to do much. */
    this->video_width   = 0;

    frame->vo_frame.width   = width;
    frame->vo_frame.height  = height;
    frame->vo_frame.ratio   = ratio;
    frame->oheight          = height;
    if (ratio < 1.5)
      frame->aspect         = XINE_VO_ASPECT_4_3;
    else
      frame->aspect         = XINE_VO_ASPECT_ANAMORPHIC;
    frame->pan_scan         = flags & VO_PAN_SCAN_FLAG;

    av_freep(&frame->mem);
    frame->real_base[0] = frame->real_base[1] = frame->real_base[2] = NULL;
    frame_gen->base[0] = frame_gen->base[1] = frame_gen->base[2] = NULL;

    return;
  }

  /* the following is for the mpeg encoding part only */

  if (!this->add_bars)
    /* don't add black bars; assume source is in 4:3 */
    ratio = 4.0/3.0;

  frame->vo_frame.ratio = ratio;
  frame->pan_scan       = 0;
  frame->aspect         = this->video_aspect;
  oheight               = this->video_oheight;

  pthread_mutex_lock(&this->video_device_lock);
  if (this->fd_video < 0) { /* decoder should have released it */
    metronom_clock_t *clock = this->class->xine->clock;
    char tmpstr[128];
    int64_t time;

    /* open the device for the encoder */
    snprintf(tmpstr, sizeof(tmpstr), "/dev/em8300_mv-%d", this->class->devnum);
    if ((this->fd_video = xine_open_cloexec(tmpstr, O_WRONLY)) < 0)
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: Failed to open video device %s (%s)\n", tmpstr, strerror(errno));

    /* start the scr plugin */
    time = clock->get_current_time(clock);
    this->class->scr->scr_plugin.start(&this->class->scr->scr_plugin, time);
    clock->register_scr(clock, &this->class->scr->scr_plugin);

    this->scale.force_redraw = 1;
  }
  pthread_mutex_unlock(&this->video_device_lock);

  if ((this->video_width != width) || (this->video_iheight != height) ||
      (fabs(this->video_ratio - ratio) > 0.01)) {

    /* try anamorphic */
    frame->aspect = XINE_VO_ASPECT_ANAMORPHIC;
    oheight = (double)height * (ratio / (16.0 / 9.0)) + .5;
    if (oheight < height) {
      /* frame too high, try 4:3 */
      frame->aspect = XINE_VO_ASPECT_4_3;
      oheight = (double)height * (ratio / (4.0 / 3.0)) + .5;
    }
    if (oheight < height) {
      /* still too high, use full height */
      oheight = height;
    }

    /* use next multiple of 16 */
    oheight = ((oheight - 1) | 15) + 1;

    /* Tell the viewers about the aspect ratio stuff. */
    if (oheight - height > 0)
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: adding %d black lines to get %s aspect ratio.\n",
	      oheight - height, frame->aspect == XINE_VO_ASPECT_4_3 ? "4:3" : "16:9");

    /* make top black bar multiple of 16,
     * so old and new macroblocks overlap */
    this->top_bar            = ((oheight - height) / 32) * 16;

    this->video_width        = width;
    this->video_iheight      = height;
    this->video_oheight      = oheight;
    this->video_ratio        = ratio;
    this->video_aspect       = frame->aspect;
    this->scale.force_redraw = 1;
    this->need_update        = 1;

    if (!this->enc) {
      /* no encoder plugin! Let's bug the user! */
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	      _("video_out_dxr3: Need an mpeg encoder to play non-mpeg videos on dxr3\n"
		"video_out_dxr3: Read the README.dxr3 for details.\n"));
    }
  }

  /* if dimensions changed, we need to re-allocate frame memory */
  if ((frame->vo_frame.width != width) || (frame->vo_frame.height != height) ||
      (frame->oheight != oheight) || (frame->vo_frame.format != format)) {
    av_freep(&frame->mem);

    if (format == XINE_IMGFMT_YUY2) {
      int i, image_size;

      /* calculate pitch and size including black bars */
      frame->vo_frame.pitches[0] = 32 * ((width + 15) / 16);
      image_size = frame->vo_frame.pitches[0] * oheight;

      /* planar format, only base[0] */
      /* add one extra line for field swap stuff */
      frame->real_base[0] = frame->mem = av_mallocz(image_size + frame->vo_frame.pitches[0]);

      /* don't use first line */
      frame->real_base[0] += frame->vo_frame.pitches[0];
      frame->real_base[1] = frame->real_base[2] = 0;

      /* fix offset, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + frame->vo_frame.pitches[0] * this->top_bar;
      frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;

      /* fill with black (yuy2 16,128,16,128,...) */
      memset(frame->real_base[0], 128, image_size); /* U and V */
      for (i = 0; i < image_size; i += 2) /* Y */
        *(frame->real_base[0] + i) = 16;

    } else { /* XINE_IMGFMT_YV12 */
      int image_size_y, image_size_u, image_size_v;

      /* calculate pitches and sizes including black bars */
      frame->vo_frame.pitches[0] = 16*((width + 15) / 16);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      image_size_y = frame->vo_frame.pitches[0] * oheight;
      image_size_u = frame->vo_frame.pitches[1] * ((oheight + 1) / 2);
      image_size_v = frame->vo_frame.pitches[2] * ((oheight + 1) / 2);

      /* add one extra line for field swap stuff */
      frame->real_base[0] = frame->mem = av_mallocz(image_size_y + frame->vo_frame.pitches[0] +
						    image_size_u + image_size_v);

      /* don't use first line */
      frame->real_base[0] += frame->vo_frame.pitches[0];
      frame->real_base[1] = frame->real_base[0] + image_size_y;
      frame->real_base[2] = frame->real_base[1] + image_size_u;

      /* fix offsets, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + frame->vo_frame.pitches[0] * this->top_bar;
      frame->vo_frame.base[1] = frame->real_base[1] + frame->vo_frame.pitches[1] * this->top_bar / 2;
      frame->vo_frame.base[2] = frame->real_base[2] + frame->vo_frame.pitches[2] * this->top_bar / 2;

      /* fill with black (yuv 16,128,128) */
      memset(frame->real_base[0], 16, image_size_y);
      memset(frame->real_base[1], 128, image_size_u);
      memset(frame->real_base[2], 128, image_size_v);
    }
  }

  if (this->swap_fields != frame->swap_fields) {
    if (this->swap_fields)
      frame->vo_frame.base[0] -= frame->vo_frame.pitches[0];
    else
      frame->vo_frame.base[0] += frame->vo_frame.pitches[0];
  }

  frame->vo_frame.width  = width;
  frame->vo_frame.height = height;
  frame->oheight         = oheight;
  frame->swap_fields     = this->swap_fields;
}

static void dxr3_overlay_begin(vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  /* special treatment is only necessary for mpeg frames */
  if (frame_gen->format != XINE_IMGFMT_DXR3) return;

  if (!this->spu_enc) this->spu_enc = dxr3_spu_encoder_init();

  if (!changed) {
    this->spu_enc->need_reencode = 0;
    return;
  }

  this->spu_enc->need_reencode = 1;
  this->spu_enc->overlay = NULL;

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void dxr3_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen,
  vo_overlay_t *overlay)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  if (frame_gen->format != XINE_IMGFMT_DXR3) {
    dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;

    if (overlay->rle) {
      if (frame_gen->format == XINE_IMGFMT_YV12)
        _x_blend_yuv(frame->vo_frame.base, overlay,
                  frame->vo_frame.width, frame->vo_frame.height,
                  frame->vo_frame.pitches, &this->alphablend_extra_data);
      else
        _x_blend_yuy2(frame->vo_frame.base[0], overlay,
                   frame->vo_frame.width, frame->vo_frame.height,
                   frame->vo_frame.pitches[0], &this->alphablend_extra_data);
    }
  } else { /* XINE_IMGFMT_DXR3 */
    if (!this->spu_enc->need_reencode) return;
    /* FIXME: we only handle the last overlay because previous ones are simply overwritten */
    this->spu_enc->overlay = overlay;
  }
}

static void dxr3_overlay_end(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  em8300_button_t btn;
  char tmpstr[128];
  ssize_t written;

  if (frame_gen->format != XINE_IMGFMT_DXR3) return;
  if (!this->spu_enc->need_reencode) return;

  dxr3_spu_encode(this->spu_enc);

  pthread_mutex_lock(&this->spu_device_lock);

  /* try to open the dxr3 spu device */
  if (!this->fd_spu) {
    snprintf (tmpstr, sizeof(tmpstr), "/dev/em8300_sp-%d", this->class->devnum);
    if ((this->fd_spu = xine_open_cloexec(tmpstr, O_WRONLY)) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: Failed to open spu device %s (%s)\n"
	      "video_out_dxr3: Overlays are not available\n", tmpstr, strerror(errno));
      pthread_mutex_unlock(&this->spu_device_lock);
      return;
    }
  }

  if (!this->spu_enc->overlay) {
    uint8_t empty_spu[] = {
      0x00, 0x26, 0x00, 0x08, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x00, 0x00, 0x20, 0x01, 0x03, 0x00, 0x00,
      0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x01, 0x06, 0x00, 0x04, 0x00, 0x07, 0xFF,
      0x00, 0x01, 0x00, 0x20, 0x02, 0xFF };
    /* just clear any previous spu */
    dxr3_spu_button(this->fd_spu, NULL);
    write(this->fd_spu, empty_spu, sizeof(empty_spu));
    pthread_mutex_unlock(&this->spu_device_lock);
    return;
  }

  /* copy clip palette */
  this->spu_enc->color[4] = this->spu_enc->hili_color[0];
  this->spu_enc->color[5] = this->spu_enc->hili_color[1];
  this->spu_enc->color[6] = this->spu_enc->hili_color[2];
  this->spu_enc->color[7] = this->spu_enc->hili_color[3];
  /* set palette */
  if (dxr3_spu_setpalette(this->fd_spu, this->spu_enc->color))
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: failed to set CLUT (%s)\n", strerror(errno));
  this->clut_cluttered = 1;
  /* write spu */
  written = write(this->fd_spu, this->spu_enc->target, this->spu_enc->size);
  if (written < 0)
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: spu device write failed (%s)\n", strerror(errno));
  else if (written != this->spu_enc->size)
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: Could only write %zd of %d spu bytes.\n", written, this->spu_enc->size);
  /* set clipping */
  btn.color = 0x7654;
  btn.contrast =
    ((this->spu_enc->hili_trans[3] << 12) & 0xf000) |
    ((this->spu_enc->hili_trans[2] <<  8) & 0x0f00) |
    ((this->spu_enc->hili_trans[1] <<  4) & 0x00f0) |
    ((this->spu_enc->hili_trans[0]      ) & 0x000f);
  btn.left   = this->spu_enc->overlay->x + this->spu_enc->overlay->hili_left;
  btn.right  = this->spu_enc->overlay->x + this->spu_enc->overlay->hili_right - 1;
  btn.top    = this->spu_enc->overlay->y + this->spu_enc->overlay->hili_top;
  btn.bottom = this->spu_enc->overlay->y + this->spu_enc->overlay->hili_bottom - 2;
  if (dxr3_spu_button(this->fd_spu, &btn))
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "dxr3_decode_spu: failed to set spu button (%s)\n", strerror(errno));

  pthread_mutex_unlock(&this->spu_device_lock);
}

static void dxr3_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;

  /* widescreen display does not need any aspect handling */
  if (!this->widescreen_enabled) {
    if (frame->aspect != this->aspect)
      this->aspect = dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, frame->aspect);
    if (frame->pan_scan && !this->pan_scan) {
#if 0
      /* the real pan&scan mode does not work, since when placed here, it
       * arrives too late for stills */
      em8300_register_t reg;
      reg.microcode_register = 1;
      reg.reg = 64;
      reg.val = 8;  /* pan&scan mode */
      if (!this->overlay_enabled) reg.val |= 1;  /* interlaced :( */
      ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &reg);
#else
      /* the card needs a break before enabling zoom mode, otherwise it fails
       * sometimes (like in the initial menu of "Breakfast at Tiffany's" RC2) */
      xine_usec_sleep(50000);
      dxr3_set_property(this_gen, VO_PROP_ZOOM_X, 1);
#endif
      this->pan_scan = 1;
    }
    if (!frame->pan_scan && this->pan_scan) {
      this->pan_scan = 0;
      dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
    }
  }

#ifdef HAVE_X11
  if (this->overlay_enabled) {
    if (this->scale.force_redraw                         ||
	this->scale.delivered_width  != frame_gen->width ||
	this->scale.delivered_height != frame->oheight   ||
	this->scale.delivered_ratio  != frame_gen->ratio ||
	this->scale.user_ratio       != (this->widescreen_enabled ? frame->aspect : XINE_VO_ASPECT_4_3)) {

      this->scale.delivered_width  = frame_gen->width;
      this->scale.delivered_height = frame->oheight;
      this->scale.delivered_ratio  = frame_gen->ratio;
      this->scale.user_ratio       = (this->widescreen_enabled ? frame->aspect : XINE_VO_ASPECT_4_3);
      this->scale.force_redraw     = 1;

      _x_vo_scale_compute_ideal_size(&this->scale);

      /* prepare the overlay window */
      dxr3_overlay_update(this);
    }
  }
#endif

  if (frame_gen->format != XINE_IMGFMT_DXR3 && this->enc && this->enc->on_display_frame) {

    pthread_mutex_lock(&this->video_device_lock);
    if (this->fd_video < 0) {
      /* no need to encode, when the device is already reserved for the decoder */
      frame_gen->free(frame_gen);
    } else {
      uint32_t vpts32 = (uint32_t)(frame_gen->vpts + DECODE_PIPE_PREBUFFER);

      if (this->need_update) {
	/* we cannot do this earlier, because vo_frame.duration is only valid here */
	if (this->enc && this->enc->on_update_format) {
	  /* set the dxr3 playmode */
	  if (this->enc->on_update_format(this, frame) && this->enhanced_mode) {
	    em8300_register_t reg;
	    reg.microcode_register = 1;
	    reg.reg = 0;
	    reg.val = MVCOMMAND_SYNC;
	    ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &reg);
	    pthread_mutex_lock(&this->class->scr->mutex);
	    this->class->scr->sync = 1;
	    pthread_mutex_unlock(&this->class->scr->mutex);
	  }
	}
	this->need_update = 0;
      }

      /* inform the card on the timing */
      if (dxr3_video_setpts(this->fd_video, &vpts32))
	xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
		"video_out_dxr3: set video pts failed (%s)\n", strerror(errno));
      /* for non-mpeg, the encoder plugin is responsible for calling
       * frame_gen->free(frame_gen) ! */
      this->enc->on_display_frame(this, frame);
    }
    pthread_mutex_unlock(&this->video_device_lock);

  } else {

    if (this->need_update) {
      /* we do not need the mpeg encoders any more */
      if (this->enc && this->enc->on_unneeded)
        this->enc->on_unneeded(this);
      this->need_update = 0;
    }
    frame_gen->free(frame_gen);

  }
}

static int dxr3_redraw_needed(vo_driver_t *this_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

#ifdef HAVE_X11
  if (this->overlay_enabled)
    dxr3_overlay_update(this);
#endif

  return 0;
}

static int dxr3_get_property(vo_driver_t *this_gen, int property)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  switch (property) {
  case VO_PROP_SATURATION:
    return this->bcs.saturation;
  case VO_PROP_CONTRAST:
    return this->bcs.contrast;
  case VO_PROP_BRIGHTNESS:
    return this->bcs.brightness;
  case VO_PROP_ASPECT_RATIO:
    return this->aspect;
  case VO_PROP_COLORKEY:
    return this->overlay.colorkey;
  case VO_PROP_ZOOM_X:
  case VO_PROP_ZOOM_Y:
  case VO_PROP_TVMODE:
    return 0;
  case VO_PROP_WINDOW_WIDTH:
    return this->scale.gui_width;
  case VO_PROP_WINDOW_HEIGHT:
    return this->scale.gui_height;
  }
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	  "video_out_dxr3: property %d not implemented.\n", property);
  return 0;
}

static int dxr3_set_property(vo_driver_t *this_gen, int property, int value)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  int val, bcs_changed = 0;

  switch (property) {
  case VO_PROP_SATURATION:
    this->bcs.saturation = value;
    bcs_changed = 1;
    break;
  case VO_PROP_CONTRAST:
    this->bcs.contrast = value;
    bcs_changed = 1;
    break;
  case VO_PROP_BRIGHTNESS:
    this->bcs.brightness = value;
    bcs_changed = 1;
    break;
  case VO_PROP_ASPECT_RATIO:
    /* xine-ui increments the value, so we make
     * just a two value "loop" */
    if (this->pan_scan) break;
    if (this->widescreen_enabled)
      /* We should send an anamorphic hint to widescreen tvs, so they
       * can switch to 16:9 mode. But the dxr3 cannot do this. */
      break;

    switch(value) {
    case XINE_VO_ASPECT_SQUARE:
    case XINE_VO_ASPECT_4_3:
      llprintf(LOG_VID, "setting aspect ratio to full\n");
      val = EM8300_ASPECTRATIO_4_3;
      value = XINE_VO_ASPECT_4_3;
      break;
    case XINE_VO_ASPECT_ANAMORPHIC:
    case XINE_VO_ASPECT_DVB:
      llprintf(LOG_VID, "setting aspect ratio to anamorphic\n");
      val = EM8300_ASPECTRATIO_16_9;
      value = XINE_VO_ASPECT_ANAMORPHIC;
    }

    if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: failed to set aspect ratio (%s)\n", strerror(errno));

    this->scale.force_redraw = 1;
    break;
  case VO_PROP_COLORKEY:
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: VO_PROP_COLORKEY not implemented!");
    this->overlay.colorkey = value;
    break;
  case VO_PROP_ZOOM_X:
    if (value == 1) {
      llprintf(LOG_VID, "enabling 16:9 zoom\n");
      if (!this->widescreen_enabled) {
	dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
	if (!this->overlay_enabled) dxr3_zoomTV(this);
      } else {
	/* We should send an anamorphic hint to widescreen tvs, so they
	 * can switch to 16:9 mode. But the dxr3 cannot do this. */
      }
    } else if (value == -1) {
      llprintf(LOG_VID, "disabling 16:9 zoom\n");
      dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
    }
    break;
  case VO_PROP_TVMODE:
    if (++this->tv_mode > EM8300_VIDEOMODE_LAST) this->tv_mode = EM8300_VIDEOMODE_PAL;
#ifdef LOG
    switch (this->tv_mode) {
    case EM8300_VIDEOMODE_PAL:
      llprintf(LOG_VID, "Changing TVMode to PAL\n");
      break;
    case EM8300_VIDEOMODE_PAL60:
      llprintf(LOG_VID, "Changing TVMode to PAL60\n");
      break;
    case EM8300_VIDEOMODE_NTSC:
      llprintf(LOG_VID, "Changing TVMode to NTSC\n");
      break;
    }
#endif
    if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: setting video mode failed (%s)\n", strerror(errno));
    break;
  }

  if (bcs_changed)
    if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	      "video_out_dxr3: bcs set failed (%s)\n", strerror(errno));

  return value;
}

static void dxr3_get_property_min_max(vo_driver_t *this_gen, int property,
  int *min, int *max)
{
  switch (property) {
  case VO_PROP_SATURATION:
  case VO_PROP_CONTRAST:
  case VO_PROP_BRIGHTNESS:
    *min = 0;
    *max = 1000;
    break;
  default:
    *min = 0;
    *max = 0;
  }
}

static int dxr3_gui_data_exchange(vo_driver_t *this_gen, int data_type, void *data)
{
#ifdef HAVE_X11
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  if (!this->overlay_enabled && !this->tv_switchable) return 0;

  switch (data_type) {
  case XINE_GUI_SEND_EXPOSE_EVENT:
    this->scale.force_redraw = 1;
    break;
  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->win = (Drawable)data;
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC(this->display, this->win, 0, NULL);
    this->aspect = dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
    break;
  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;
      _x_vo_scale_translate_gui2video(&this->scale, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->scale, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1 - this->top_bar;
      rect->w = x2 - x1;
      rect->h = y2 - y1;
      if (this->overlay_enabled && this->pan_scan) {
	/* in this mode, the image is distorted, so we have to modify */
	rect->x = rect->x * 3 / 4 + this->scale.delivered_width / 8;
	rect->w = rect->w * 3 / 4;
      }
    }
    break;
  case XINE_GUI_SEND_VIDEOWIN_VISIBLE:
    {
      long window_showing = (long)data;
      int val;
      if (!window_showing) {
        llprintf(LOG_VID, "Hiding video window and diverting video to TV\n");
        val = EM8300_OVERLAY_MODE_OFF;
        this->overlay_enabled = 0;
      } else {
        llprintf(LOG_VID, "Using video window for overlaying video\n");
        val = EM8300_OVERLAY_MODE_OVERLAY;
        this->overlay_enabled = 1;
        this->scale.force_redraw = 1;
      }
      ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &val);
      this->aspect = dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
      if (this->pan_scan) dxr3_set_property(this_gen, VO_PROP_ZOOM_X, 1);
    }
    break;
  default:
    return -1;
  }
#endif
  return 0;
}

static void dxr3_dispose(vo_driver_t *this_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  int val = EM8300_OVERLAY_MODE_OFF;

  llprintf(LOG_VID, "vo exit called\n");
  if (this->enc && this->enc->on_close)
    this->enc->on_close(this);
  if(this->overlay_enabled)
    ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &val);
  close(this->fd_control);
  pthread_mutex_lock(&this->spu_device_lock);
  if (this->fd_spu) {
    static const uint8_t empty_spu[] = {
      0x00, 0x26, 0x00, 0x08, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x00, 0x00, 0x20, 0x01, 0x03, 0x00, 0x00,
      0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x01, 0x06, 0x00, 0x04, 0x00, 0x07, 0xFF,
      0x00, 0x01, 0x00, 0x20, 0x02, 0xFF };
    /* clear any remaining spu */
    dxr3_spu_button(this->fd_spu, NULL);
    write(this->fd_spu, empty_spu, sizeof(empty_spu));
    close(this->fd_spu);
  }
  pthread_mutex_unlock(&this->spu_device_lock);
  pthread_mutex_destroy(&this->video_device_lock);
  pthread_mutex_destroy(&this->spu_device_lock);

  _x_alphablend_free(&this->alphablend_extra_data);

  free(this);
}


#ifdef HAVE_X11
static void gather_screen_vars(dxr3_driver_t *this, const x11_visual_t *vis)
{
  int scrn;
#ifdef HAVE_XINERAMA
  int screens;
  int dummy_a, dummy_b;
  XineramaScreenInfo *screeninfo = NULL;
#endif

  this->win             = vis->d;
  this->display         = vis->display;
  this->scale.user_data = vis->user_data;
  this->gc              = XCreateGC(this->display, this->win, 0, NULL);
  scrn                  = DefaultScreen(this->display);

#ifdef HAVE_XINERAMA
  if (XineramaQueryExtension(this->display, &dummy_a, &dummy_b) &&
      (screeninfo = XineramaQueryScreens(this->display, &screens)) &&
      XineramaIsActive(this->display)) {
    this->overlay.screen_xres = screeninfo[0].width;
    this->overlay.screen_yres = screeninfo[0].height;
  } else
#endif
  {
    this->overlay.screen_xres = DisplayWidth(this->display, scrn);
    this->overlay.screen_yres = DisplayHeight(this->display, scrn);
  }

  this->overlay.screen_depth  = DisplayPlanes(this->display, scrn);
  this->scale.frame_output_cb = (void *)vis->frame_output_cb;

  llprintf(LOG_OVR, "xres: %d, yres: %d, depth: %d\n",
    this->overlay.screen_xres, this->overlay.screen_yres, this->overlay.screen_depth);
}

/* dxr3_overlay_read_state helper structure */
#define TYPE_INT 1
#define TYPE_XINT 2
#define TYPE_COEFF 3
#define TYPE_FLOAT 4

struct lut_entry {
    char *name;
    int type;
    void *ptr;
};

/* dxr3_overlay_read_state helper function */
static int lookup_parameter(struct lut_entry *lut, char *name,
  void **ptr, int *type)
{
  int i;

  for (i = 0; lut[i].name; i++)
    if (strcmp(name, lut[i].name) == 0) {
      *ptr  = lut[i].ptr;
      *type = lut[i].type;
      llprintf(LOG_OVR, "found parameter \"%s\"\n", name);
      return 1;
    }
  llprintf(LOG_OVR, "WARNING: unknown parameter \"%s\"\n", name);
  return 0;
}

static int dxr3_overlay_read_state(dxr3_overlay_t *this)
{
  char *loc;
  char *fname, line[256];
  FILE *fp;
  struct lut_entry lut[] = {
    {"xoffset",        TYPE_INT,   &this->xoffset},
    {"yoffset",        TYPE_INT,   &this->yoffset},
    {"xcorr",          TYPE_INT,   &this->xcorr},
    {"jitter",         TYPE_INT,   &this->jitter},
    {"stability",      TYPE_INT,   &this->stability},
    {"keycolor",       TYPE_XINT,  &this->colorkey},
    {"colcal_upper",   TYPE_COEFF, &this->colcal_upper[0]},
    {"colcal_lower",   TYPE_COEFF, &this->colcal_lower[0]},
    {"color_interval", TYPE_FLOAT, &this->color_interval},
    {0,0,0}
  };
  char *tok;
  void *ptr;
  int type;
  int j;

  /* store previous locale */
  loc = setlocale(LC_NUMERIC, NULL);
  /* set C locale for floating point values
   * (used by .overlay/res file) */
  setlocale(LC_NUMERIC, "C");

  fname = _x_asprintf("%s/.overlay/res_%dx%dx%d", getenv("HOME"),
    this->screen_xres, this->screen_yres, this->screen_depth);
  llprintf(LOG_OVR, "attempting to open %s\n", fname);
  if (!(fp = fopen(fname, "r"))) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
	    _("video_out_dxr3: ERROR Reading overlay init file. Run autocal!\n"));
    free(fname);
    return -1;
  }
  free(fname);

  while (!feof(fp)) {
    if (!fgets(line, 256, fp))
      break;
    tok = strtok(line, " ");
    if (lookup_parameter(lut, tok, &ptr, &type)) {
      tok = strtok(NULL, " \n");
      switch(type) {
      case TYPE_INT:
        sscanf(tok, "%d", (int *)ptr);
        llprintf(LOG_OVR, "value \"%s\" = %d\n", tok, *(int *)ptr);
        break;
      case TYPE_XINT:
        sscanf(tok, "%x", (int *)ptr);
        llprintf(LOG_OVR, "value \"%s\" = %d\n", tok, *(int *)ptr);
        break;
      case TYPE_FLOAT:
        sscanf(tok, "%f", (float *)ptr);
        llprintf(LOG_OVR, "value \"%s\" = %f\n", tok, *(float *)ptr);
        break;
      case TYPE_COEFF:
        for(j = 0; j < 3; j++) {
          sscanf(tok, "%f", &((struct coeff *)ptr)[j].k);
          llprintf(LOG_OVR, "value (%d,k) \"%s\" = %f\n", j, tok, ((struct coeff *)ptr)[j].k);
          tok = strtok(NULL, " \n");
          sscanf(tok, "%f", &((struct coeff *)ptr)[j].m);
          llprintf(LOG_OVR, "value (%d,m) \"%s\" = %f\n", j, tok, ((struct coeff *)ptr)[j].m);
          tok = strtok(NULL, " \n");
        }
        break;
      }
    }
  }

  fclose(fp);
  /* restore original locale */
  setlocale(LC_NUMERIC, loc);

  return 0;
}

/* dxr3_overlay_set_keycolor helper function */
static int col_interp(float x, struct coeff c)
{
  int y;
  y = rint(x * c.k + c.m);
  if (y > 255) y = 255;
  if (y <   0) y =   0;
  return y;
}

static int dxr3_overlay_set_keycolor(dxr3_overlay_t *this)
{
  em8300_attribute_t attr;
  float r = (this->colorkey & 0xff0000) >> 16;
  float g = (this->colorkey & 0x00ff00) >>  8;
  float b = (this->colorkey & 0x0000ff);
  float interval = this->color_interval;
  int32_t overlay_limit;
  int ret;

  llprintf(LOG_OVR, "set_keycolor: r = %f, g = %f, b = %f, interval = %f\n",
    r, g, b, interval);

  overlay_limit =  /* lower limit */
    col_interp(r - interval, this->colcal_lower[0]) << 16 |
    col_interp(g - interval, this->colcal_lower[1]) <<  8 |
    col_interp(b - interval, this->colcal_lower[2]);
  llprintf(LOG_OVR, "lower overlay_limit = %d\n", overlay_limit);
  attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_LOWER;
  attr.value     = overlay_limit;
  if ((ret = ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr)) < 0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: WARNING: error setting overlay lower limit attribute\n");
    return ret;
  }

  overlay_limit =  /* upper limit */
    col_interp(r + interval, this->colcal_upper[0]) << 16 |
    col_interp(g + interval, this->colcal_upper[1]) <<  8 |
    col_interp(b + interval, this->colcal_upper[2]);
  llprintf(LOG_OVR, "upper overlay_limit = %d\n", overlay_limit);
  attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_UPPER;
  attr.value     = overlay_limit;
  if ((ret = ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr)) < 0)
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	    "video_out_dxr3: WARNING: error setting overlay upper limit attribute\n");
  return ret;
}

static int dxr3_overlay_set_attributes(dxr3_overlay_t *this)
{
  em8300_attribute_t attr;

  attr.attribute = EM9010_ATTRIBUTE_XOFFSET;
  attr.value     = this->xoffset;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_YOFFSET;
  attr.value     = this->yoffset;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_XCORR;
  attr.value     = this->xcorr;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_STABILITY;
  attr.value     = this->stability;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_JITTER;
  attr.value     = this->jitter;
  return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr);
}


static void dxr3_overlay_update(dxr3_driver_t *this)
{
  if (_x_vo_scale_redraw_needed(&this->scale)) {
    em8300_overlay_window_t win;

    _x_vo_scale_compute_output_size(&this->scale);

    /* fill video window with keycolor */
    XLockDisplay(this->display);
    XSetForeground(this->display, this->gc, this->black.pixel);
    XFillRectangle(this->display, this->win, this->gc,
      this->scale.gui_x, this->scale.gui_y,
      this->scale.gui_width, this->scale.gui_height);
    XSetForeground(this->display, this->gc, this->key.pixel);
    XFillRectangle(this->display, this->win, this->gc,
      this->scale.output_xoffset, this->scale.output_yoffset + this->overlay.shrink,
      this->scale.output_width, this->scale.output_height - 2 * this->overlay.shrink);
    XFlush(this->display);
    XUnlockDisplay(this->display);

    win.xpos   = this->scale.output_xoffset + this->scale.gui_win_x;
    win.ypos   = this->scale.output_yoffset + this->scale.gui_win_y;
    win.width  = this->scale.output_width;
    win.height = this->scale.output_height;

    if (this->pan_scan) {
      win.xpos  -= win.width / 6;
      win.width *= 4;
      win.width /= 3;
    }

    /* is some part of the picture visible? */
    if (win.xpos + win.width  < 0) return;
    if (win.ypos + win.height < 0) return;
    if (win.xpos > this->overlay.screen_xres) return;
    if (win.ypos > this->overlay.screen_yres) return;

    ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETWINDOW, &win);
  }
}
#endif

static void dxr3_zoomTV(dxr3_driver_t *this)
{
  em8300_register_t frame, visible, update;

  /* change left bound */
  frame.microcode_register   = 1;
  frame.reg                  = 93;   // dicom frame left
  frame.val                  = 0x10;

  visible.microcode_register = 1;
  visible.reg                = 97;   // dicom visible left
  visible.val                = 0x10;

  update.microcode_register  = 1;
  update.reg                 = 65;   // dicom_update
  update.val                 = 1;

  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);

  /* change right bound */
  frame.microcode_register   = 1;
  frame.reg                  = 94;   // dicom frame right
  frame.val                  = 0x10;

  visible.microcode_register = 1;
  visible.reg                = 98;   // dicom visible right
  visible.val                = 968;

  update.microcode_register  = 1;
  update.reg                 = 65;   // dicom_update
  update.val                 = 1;

  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);
}


static void dxr3_update_add_bars(void *data, xine_cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->add_bars = entry->num_value;
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	  "video_out_dxr3: setting add_bars to correct aspect ratio to %s\n", (this->add_bars ? "on" : "off"));
}

static void dxr3_update_swap_fields(void *data, xine_cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->swap_fields = entry->num_value;
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	  "video_out_dxr3: setting swap fields to %s\n", (this->swap_fields ? "on" : "off"));
}

static void dxr3_update_enhanced_mode(void *data, xine_cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->enhanced_mode = entry->num_value;
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
	  "video_out_dxr3: setting enhanced encoding playback to %s\n", (this->enhanced_mode ? "on" : "off"));
}
