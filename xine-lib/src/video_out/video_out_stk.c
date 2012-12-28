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
 *
 * video_out_stk.c, Libstk Surface Video Driver
 * more info on Libstk at http://www.libstk.org
 *
 * based on video_out_sdl from
 *   Miguel Freitas
 *
 * based on mpeg2dec code from
 *   Ryan C. Gordon <icculus@lokigames.com> and
 *   Dominik Schnitzer <aeneas@linuxvideo.org>
 *
 * (SDL) xine version by Miguel Freitas (Jan/2002)
 *   Missing features:
 *    - event handling
 *    - fullscreen
 *    - surface locking (for Xlib error noted below)
 *    - stability, testing, etc?? ;)
 *   Known bugs:
 *    - using stk SDL backend, a window move is sometimes needed for the video
 *      to show up, alghough the audio is playing
 *    - using stk SDL backend an XLib unexpected async response error sometimes
 *      occurs
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include <libstk/stk_c_wrapper.h>

#define LOG_MODULE "video_out_stk"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/xineutils.h>
#include <xine/vo_scale.h>

/* Extend the video frame class with stk private data */
typedef struct stk_frame_s {
    /* public interface */
    vo_frame_t vo_frame;

    /* stk private data */
    int width, height, format;
    double ratio;
    overlay_t* overlay;
} stk_frame_t;


/* Extend the video output class with stk private data */
typedef struct stk_driver_s {
    /* public interface */
    vo_driver_t        vo_driver;

    /* stk private data */
    config_values_t*   config;
    surface_t*         surface;
    xine_panel_t*      xine_panel;
    uint8_t            bpp;              /* do we need this ? */
    pthread_mutex_t    mutex;
    uint32_t           capabilities;
    vo_scale_t         sc;
    xine_t            *xine;

    alphablend_t       alphablend_extra_data;
} stk_driver_t;


typedef struct {
    video_driver_class_t  driver_class;
    config_values_t*      config;
    xine_t               *xine;
} stk_class_t;

static uint32_t stk_get_capabilities (vo_driver_t *this_gen) {
    stk_driver_t* this = (stk_driver_t *)this_gen;
    //printf("video_out_stk: get_capabilities()\n");
    return this->capabilities;
}

/* copy YUV to RGB data (see fb driver)*/
static void stk_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src)
{
    /* not needed by SDL, we may need it for other stk backends */
    //printf("video_out_stk: frame_copy()\n");
}

/* set up the rgb_dst and strides (see fb driver) */
static void stk_frame_field (vo_frame_t *vo_img, int which_field) {
    /* not needed for SDL, maybe not for libstk ? */
    //printf("video_out_stk: frame_field()\n");
}

static void stk_frame_dispose (vo_frame_t *vo_img) {
    stk_frame_t* frame = (stk_frame_t *)vo_img;
    //printf("video_out_stk: frame_dispose()\n");
    if (frame->overlay) stk_overlay_free(frame->overlay);
    free (frame);
}

static vo_frame_t *stk_alloc_frame(vo_driver_t *this_gen) {
    stk_driver_t* this = (stk_driver_t*)this_gen;
    /* allocate the frame */
    stk_frame_t* frame;

    //printf("video_out_stk: alloc_frame()\n");
    frame = calloc(1, sizeof(stk_frame_t));
    if (!frame)
      return NULL;

    /* populate the frame members*/
    pthread_mutex_init (&frame->vo_frame.mutex, NULL);

    /* map the frame function pointers */
    frame->vo_frame.proc_slice = stk_frame_proc_slice;
    frame->vo_frame.proc_frame = NULL;
    frame->vo_frame.field      = stk_frame_field;
    frame->vo_frame.dispose    = stk_frame_dispose;

    return (vo_frame_t *) frame;
}

static void stk_compute_ideal_size (stk_driver_t* this) {
    //printf("video_out_stk: compute_ideal_size()\n");
    _x_vo_scale_compute_ideal_size(&this->sc);
}

static void stk_compute_output_size (stk_driver_t *this) {
    //printf("video_out_stk: compute_output_size()\n");
    _x_vo_scale_compute_output_size( &this->sc );

    lprintf ("frame source %d x %d => screen output %d x %d\n",
	     this->sc.delivered_width, this->sc.delivered_height,
	     this->sc.output_width, this->sc.output_height);
}


static void stk_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
        uint32_t width, uint32_t height, double ratio, int format, int flags) {
    stk_driver_t* this = (stk_driver_t*)this_gen;
    stk_frame_t* frame = (stk_frame_t*)frame_gen;
    //printf("video_out_stk: update_frame_format()\n");

    if ((frame->width != width) || (frame->height != height) || (frame->format != format)) {
      lprintf("update_frame_format - %d=%d, %d=%d, %d=%d\n",
	      frame->width, width, frame->height, height, frame->format, format);
      lprintf("vo_frame data - width, height, format: %d, %d, %d\n",
	     frame->vo_frame.width, frame->vo_frame.height, frame->vo_frame.format);

        /* (re-) allocate image */
        if (frame->overlay) {
            stk_overlay_free(frame->overlay);
            frame->overlay = NULL;
        }

        if (format == XINE_IMGFMT_YV12) {
	  lprintf ("format YV12\n");
	  frame->overlay = stk_surface_create_overlay(this->surface, width, height,
						      STK_FORMAT_YV12);

        } else if (format == XINE_IMGFMT_YUY2) {
	  lprintf("format YUY2\n");
	  frame->overlay = stk_surface_create_overlay(this->surface, width, height,
						      STK_FORMAT_YUY2);
        }

        if (frame->overlay == NULL)
            return;

        /* From the SDL driver:
         * This needs to be done becuase I have found that
         * pixels isn't setup until this is called.
         */
        stk_overlay_lock(frame->overlay);

        frame->vo_frame.pitches[0] = stk_overlay_pitches(frame->overlay, 0);
        frame->vo_frame.pitches[1] = stk_overlay_pitches(frame->overlay, 2);
        frame->vo_frame.pitches[2] = stk_overlay_pitches(frame->overlay, 1);
        frame->vo_frame.base[0] = stk_overlay_pixels(frame->overlay, 0);
        frame->vo_frame.base[1] = stk_overlay_pixels(frame->overlay, 2);
        frame->vo_frame.base[2] = stk_overlay_pixels(frame->overlay, 1);

        frame->width  = width;
        frame->height = height;
        frame->format = format;
    } else {
        stk_overlay_lock(frame->overlay);
    }

    frame->ratio = ratio;
}


static void stk_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
    stk_driver_t* this = (stk_driver_t*)this_gen;
    stk_frame_t* frame = (stk_frame_t*)frame_gen;
    //printf("video_out_stk: overlay_blend()\n");

    this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
    this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;

    if (overlay->rle) {
        if (frame->format == XINE_IMGFMT_YV12)
            _x_blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches, &this->alphablend_extra_data);
        else
            _x_blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0], &this->alphablend_extra_data);
    }

}

static void stk_check_events (stk_driver_t* this) {
    /* SDL checks for a resize, our video panels aren't resizeable... */
    //printf("video_out_stk: check_events()\n");
}

/* when is this called ? */
static int stk_redraw_needed (vo_driver_t* this_gen) {
    stk_driver_t* this = (stk_driver_t*)this_gen;
    int ret = 0;
    static int last_gui_width, last_gui_height;

    //printf("video_out_stk: redraw_needed()\n");

    if( last_gui_width != this->sc.gui_width ||
            last_gui_height != this->sc.gui_height ||
            this->sc.force_redraw ) {

        last_gui_width = this->sc.gui_width;
        last_gui_height = this->sc.gui_height;

        stk_compute_output_size (this);

        ret = 1;
    }

    this->sc.force_redraw = 0;

    return ret;
}


static void stk_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
    stk_driver_t* this = (stk_driver_t*)this_gen;
    stk_frame_t* frame = (stk_frame_t*)frame_gen;

    //printf("video_out_stk: display_frame()\n");

    pthread_mutex_lock(&this->mutex);

    if ( (frame->width != this->sc.delivered_width)
            || (frame->height != this->sc.delivered_height)
            || (frame->ratio != this->sc.delivered_ratio) ) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG, "video_out_stk: change frame format\n");

        this->sc.delivered_width      = frame->width;
        this->sc.delivered_height     = frame->height;
        this->sc.delivered_ratio      = frame->ratio;

        stk_compute_ideal_size(this);

        this->sc.force_redraw = 1;
    }

    /*
     * tell gui that we are about to display a frame,
     * ask for offset and output size
     */
    stk_check_events (this);
    stk_redraw_needed (this_gen);

    stk_overlay_unlock(frame->overlay);
    stk_overlay_display(frame->overlay, this->sc.output_xoffset, this->sc.output_yoffset,
            this->sc.output_width, this->sc.output_height);

    frame->vo_frame.free (&frame->vo_frame);

    pthread_mutex_unlock(&this->mutex);
}

static int stk_get_property (vo_driver_t* this_gen, int property) {
    stk_driver_t* this = (stk_driver_t *)this_gen;

    //printf("video_out_stk: get_property()\n");

    if (property == VO_PROP_ASPECT_RATIO)
        return this->sc.user_ratio;

    return 0;
}

static int stk_set_property (vo_driver_t* this_gen, int property, int value) {
    stk_driver_t* this = (stk_driver_t*)this_gen;

    //printf("video_out_stk: set_property()\n");

    if ( property == VO_PROP_ASPECT_RATIO) {
        if (value>=XINE_VO_ASPECT_NUM_RATIOS)
            value = XINE_VO_ASPECT_AUTO;
        this->sc.user_ratio = value;
        xprintf(this->xine, XINE_VERBOSITY_DEBUG,
		"video_out_stk: aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name_table[value]);

        stk_compute_ideal_size (this);
        this->sc.force_redraw = 1;
    }

    return value;
}

static void stk_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max) {
    /*  stk_driver_t* this = (stk_driver_t*)this_gen; */
    //printf("video_out_stk: get_property_min_max()\n");
}

static int stk_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data) {
    stk_driver_t *this = (stk_driver_t*)this_gen;

    switch (data_type)
    {
        case XINE_GUI_SEND_COMPLETION_EVENT:
            break;

        case XINE_GUI_SEND_EXPOSE_EVENT:
            break;

        case XINE_GUI_SEND_DRAWABLE_CHANGED:
            this->xine_panel = (xine_panel_t*)data;
            this->surface = stk_xine_panel_surface(this->xine_panel);
            this->sc.gui_x      = stk_xine_panel_x(this->xine_panel);
            this->sc.gui_y      = stk_xine_panel_y(this->xine_panel);
            this->sc.gui_width  = stk_xine_panel_width(this->xine_panel);
            this->sc.gui_height = stk_xine_panel_height(this->xine_panel);
            this->sc.force_redraw = 1;
            break;

        case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
            break;
    }

    return 0;
}

static void stk_dispose (vo_driver_t * this_gen) {
    stk_driver_t* this = (stk_driver_t*)this_gen;

    //printf("video_out_stk: dispose()\n");

    /* FIXME: any libstk deleting must be done in the app or library
     * since we didn't create the surface */

    _x_alphablend_free(&this->alphablend_extra_data);

    free(this);
}

static vo_driver_t *open_plugin(video_driver_class_t *class_gen, const void *visual_gen) {
    stk_class_t * class = (stk_class_t *) class_gen;
    /* allocate the video output driver class */
    stk_driver_t*        this;

    //printf("video_out_stk: open_plugin()\n");

    this = calloc(1, sizeof (stk_driver_t));
    if (!this)
      return NULL;

    _x_alphablend_init(&this->alphablend_extra_data, class->xine);

    /* populate the video output driver members */
    this->config     = class->config;
    this->xine       = class->xine;
    this->xine_panel = (xine_panel_t*)visual_gen;
    this->surface    = stk_xine_panel_surface(this->xine_panel);
    /* FIXME: provide a way to get bpp from stk surfaces */
    /* this->bpp = stk_surface_bpp(this->surface); */
    this->bpp        = 32;
    pthread_mutex_init(&this->mutex, NULL);
    /* FIXME: provide a way to get YUV formats from stk surfaces */
    /* this->capabilities = stk_surface_formats(this->surface); */
    this->capabilities = VO_CAP_YUY2 | VO_CAP_YV12;
    /* FIXME: what does this do ? */
    _x_vo_scale_init( &this->sc, 0, 0, this->config );
    this->sc.gui_x      = stk_xine_panel_x(this->xine_panel);
    this->sc.gui_y      = stk_xine_panel_y(this->xine_panel);
    this->sc.gui_width  = stk_xine_panel_width(this->xine_panel);
    this->sc.gui_height = stk_xine_panel_height(this->xine_panel);

    /* map the video output driver function pointers */
    this->vo_driver.get_capabilities     = stk_get_capabilities;
    this->vo_driver.alloc_frame          = stk_alloc_frame;
    this->vo_driver.update_frame_format  = stk_update_frame_format;
    this->vo_driver.overlay_begin        = NULL; /* not used */
    this->vo_driver.overlay_blend        = stk_overlay_blend;
    this->vo_driver.overlay_end          = NULL; /* not used */
    this->vo_driver.display_frame        = stk_display_frame;
    this->vo_driver.get_property         = stk_get_property;
    this->vo_driver.set_property         = stk_set_property;
    this->vo_driver.get_property_min_max = stk_get_property_min_max;
    this->vo_driver.gui_data_exchange    = stk_gui_data_exchange;
    this->vo_driver.dispose              = stk_dispose;
    this->vo_driver.redraw_needed        = stk_redraw_needed;

    /* FIXME: move this to the stk SDL driver code */
    xine_setenv("SDL_VIDEO_YUV_HWACCEL", "1", 1);
    xine_setenv("SDL_VIDEO_X11_NODIRECTCOLOR", "1", 1);

    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     "video_out_stk: warning, xine's STK driver is EXPERIMENTAL\n");
    return &this->vo_driver;
}


/**
 * Class Functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
    stk_class_t* this;

    //printf("video_out_stk: init_class()\n");

    this = calloc(1, sizeof(stk_class_t));

    this->driver_class.open_plugin      = open_plugin;
    this->driver_class.identifier       = "stk";
    this->driver_class.description      = N_("xine video output plugin using the Libstk Surface Set-top Toolkit");
    this->driver_class.dispose          = default_video_driver_class_dispose;

    this->config                        = xine->config;
    this->xine                          = xine;

    return this;
}

/* what priority should we be (what is low), what vistype should we declare ? */
static const vo_info_t vo_info_stk = {
    4,                    /* priority */
    XINE_VISUAL_TYPE_FB,  /* visual type supported by this plugin */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
    /* type, API, "name", version, special_info, init_function */
    { PLUGIN_VIDEO_OUT, 22, "stk", XINE_VERSION_CODE, &vo_info_stk, init_class },
    { PLUGIN_NONE, 0, "" , 0 , NULL, NULL}
};

