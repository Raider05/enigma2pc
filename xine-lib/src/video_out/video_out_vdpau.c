/*
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 * Copyright (C) 2008-2013 the xine project
 * Copyright (C) 2008 Christophe Thommeret <hftom@free.fr>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * video_out_vdpau.c, a video output plugin
 * using VDPAU (Video Decode and Presentation Api for Unix)
 *
 *
 */

/* #define LOG */
#define LOG_MODULE "video_out_vdpau"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <xine.h>
#include <xine/video_out.h>
#include <xine/vo_scale.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#include <vdpau/vdpau_x11.h>
#include "accel_vdpau.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#define NUM_FRAMES_BACK 1

#ifndef HAVE_THREAD_SAFE_X11
#define LOCKDISPLAY /*define this if you have a buggy libX11/xcb*/
#endif

#define DEINT_BOB                    1
#define DEINT_HALF_TEMPORAL          2
#define DEINT_HALF_TEMPORAL_SPATIAL  3
#define DEINT_TEMPORAL               4
#define DEINT_TEMPORAL_SPATIAL       5

#define NUMBER_OF_DEINTERLACERS 5

static const char *const vdpau_deinterlacer_name[] = {
  "bob",
  "half temporal",
  "half temporal_spatial",
  "temporal",
  "temporal_spatial",
  NULL
};

static const char *const vdpau_deinterlacer_description [] = {
  "bob\nBasic deinterlacing, doing 50i->50p.\n\n",
  "half temporal\nDisplays first field only, doing 50i->25p\n\n",
  "half temporal_spatial\nDisplays first field only, doing 50i->25p\n\n",
  "temporal\nVery good, 50i->50p\n\n",
  "temporal_spatial\nThe best, but very GPU intensive.\n\n",
  NULL
};


static const char *const vdpau_sd_only_properties[] = {
  "none",
  "noise",
  "sharpness",
  "noise+sharpness",
  NULL
};

static const VdpOutputSurfaceRenderBlendState blend = {
  VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
  { 0 }
};



VdpDevice vdp_device;
VdpPresentationQueue vdp_queue;
VdpPresentationQueueTarget vdp_queue_target;

VdpDeviceDestroy *vdp_device_destroy;

VdpGetProcAddress *vdp_get_proc_address;

VdpGetApiVersion *vdp_get_api_version;
VdpGetInformationString *vdp_get_information_string;
VdpGetErrorString *vdp_get_error_string;

VdpVideoSurfaceQueryCapabilities *vdp_video_surface_query_capabilities;
VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *vdp_video_surface_query_get_put_bits_ycbcr_capabilities;
VdpVideoSurfaceCreate *vdp_video_surface_create;
VdpVideoSurfaceDestroy *vdp_video_surface_destroy;
VdpVideoSurfacePutBitsYCbCr *vdp_video_surface_putbits_ycbcr;
VdpVideoSurfaceGetBitsYCbCr *vdp_video_surface_getbits_ycbcr;
VdpVideoSurfaceGetParameters *vdp_video_surface_get_parameters;

VdpOutputSurfaceQueryCapabilities *vdp_output_surface_query_capabilities;
VdpOutputSurfaceQueryGetPutBitsNativeCapabilities  *vdp_output_surface_query_get_put_bits_native_capabilities;
VdpOutputSurfaceQueryPutBitsYCbCrCapabilities *vdp_output_surface_query_put_bits_ycbcr_capabilities;
VdpOutputSurfaceCreate *vdp_output_surface_create;
VdpOutputSurfaceDestroy *vdp_output_surface_destroy;
VdpOutputSurfaceRenderOutputSurface *vdp_output_surface_render_output_surface;
VdpOutputSurfaceGetBitsNative *vdp_output_surface_get_bits;
VdpOutputSurfacePutBitsNative *vdp_output_surface_put_bits;
VdpOutputSurfacePutBitsYCbCr *vdp_output_surface_put_bits_ycbcr;

VdpVideoMixerCreate *vdp_video_mixer_create;
VdpVideoMixerDestroy *vdp_video_mixer_destroy;
VdpVideoMixerRender *vdp_video_mixer_render;
VdpVideoMixerSetAttributeValues *vdp_video_mixer_set_attribute_values;
VdpVideoMixerSetFeatureEnables *vdp_video_mixer_set_feature_enables;
VdpVideoMixerGetFeatureEnables *vdp_video_mixer_get_feature_enables;
VdpVideoMixerQueryFeatureSupport *vdp_video_mixer_query_feature_support;
VdpVideoMixerQueryParameterSupport *vdp_video_mixer_query_parameter_support;
VdpVideoMixerQueryAttributeSupport *vdp_video_mixer_query_attribute_support;
VdpVideoMixerQueryParameterValueRange *vdp_video_mixer_query_parameter_value_range;
VdpVideoMixerQueryAttributeValueRange *vdp_video_mixer_query_attribute_value_range;

VdpGenerateCSCMatrix *vdp_generate_csc_matrix;

VdpPresentationQueueTargetCreateX11 *vdp_queue_target_create_x11;
VdpPresentationQueueTargetDestroy *vdp_queue_target_destroy;
VdpPresentationQueueCreate *vdp_queue_create;
VdpPresentationQueueDestroy *vdp_queue_destroy;
VdpPresentationQueueDisplay *vdp_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle *vdp_queue_block;
VdpPresentationQueueSetBackgroundColor *vdp_queue_set_background_color;
VdpPresentationQueueGetTime *vdp_queue_get_time;
VdpPresentationQueueQuerySurfaceStatus *vdp_queue_query_surface_status;

VdpDecoderQueryCapabilities *vdp_decoder_query_capabilities;
VdpDecoderCreate *vdp_decoder_create;
VdpDecoderDestroy *vdp_decoder_destroy;
VdpDecoderRender *vdp_decoder_render;

VdpPreemptionCallbackRegister *vdp_preemption_callback_register;

static void vdp_preemption_callback( VdpDevice device, void *context );
static void vdpau_reinit( vo_driver_t *this_gen );

static VdpVideoSurfaceCreate *orig_vdp_video_surface_create;
static VdpVideoSurfaceDestroy *orig_vdp_video_surface_destroy;

static VdpOutputSurfaceCreate *orig_vdp_output_surface_create;
static VdpOutputSurfaceDestroy *orig_vdp_output_surface_destroy;

static VdpVideoSurfacePutBitsYCbCr *orig_vdp_video_surface_putbits_ycbcr;

static VdpDecoderCreate *orig_vdp_decoder_create;
static VdpDecoderDestroy *orig_vdp_decoder_destroy;
static VdpDecoderRender *orig_vdp_decoder_render;

#ifdef LOCKDISPLAY
#define DO_LOCKDISPLAY          if (guarded_display) XLockDisplay(guarded_display);
#define DO_UNLOCKDISPLAY        if (guarded_display) XUnlockDisplay(guarded_display);
static Display *guarded_display;
#else
#define DO_LOCKDISPLAY
#define DO_UNLOCKDISPLAY
#endif

static VdpStatus guarded_vdp_video_surface_putbits_ycbcr(VdpVideoSurface surface, VdpYCbCrFormat source_ycbcr_format, void const *const *source_data, uint32_t const *source_pitches)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_video_surface_putbits_ycbcr(surface, source_ycbcr_format, source_data, source_pitches);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_video_surface_create(VdpDevice device, VdpChromaType chroma_type, uint32_t width, uint32_t height,VdpVideoSurface *surface)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_video_surface_create(device, chroma_type, width, height, surface);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_video_surface_destroy(VdpVideoSurface surface)
{
  VdpStatus r;
  /*XLockDisplay(guarded_display);*/
  r = orig_vdp_video_surface_destroy(surface);
  /*XUnlockDisplay(guarded_display);*/
  return r;
}

static VdpStatus guarded_vdp_output_surface_create(VdpDevice device, VdpChromaType chroma_type, uint32_t width, uint32_t height,VdpVideoSurface *surface)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_output_surface_create(device, chroma_type, width, height, surface);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_output_surface_destroy(VdpVideoSurface surface)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_output_surface_destroy(surface);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_decoder_create(VdpDevice device, VdpDecoderProfile profile, uint32_t width, uint32_t height, uint32_t max_references, VdpDecoder *decoder)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_decoder_create(device, profile, width, height, max_references, decoder);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_decoder_destroy(VdpDecoder decoder)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_decoder_destroy(decoder);
  DO_UNLOCKDISPLAY;
  return r;
}

static VdpStatus guarded_vdp_decoder_render(VdpDecoder decoder, VdpVideoSurface target, VdpPictureInfo const *picture_info, uint32_t bitstream_buffer_count, VdpBitstreamBuffer const *bitstream_buffers)
{
  VdpStatus r;
  DO_LOCKDISPLAY;
  r = orig_vdp_decoder_render(decoder, target, picture_info, bitstream_buffer_count, bitstream_buffers);
  DO_UNLOCKDISPLAY;
  return r;
}



typedef struct {
  VdpOutputSurface  surface;
  uint32_t          width;
  uint32_t          height;
  uint32_t          size;
} vdpau_output_surface_t;


typedef struct {
  xine_grab_video_frame_t grab_frame;

  vo_driver_t *vo_driver;
  vdpau_output_surface_t render_surface;
  int width, height;
  uint32_t *rgba;
} vdpau_grab_video_frame_t;


typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;

  int                surface_cleared_nr;

  vdpau_accel_t     vdpau_accel_data;
} vdpau_frame_t;


typedef struct {

  int               x;             /* x start of subpicture area       */
  int               y;             /* y start of subpicture area       */
  int               width;         /* width of subpicture area         */
  int               height;        /* height of subpicture area        */

  /* area within osd extent to scale video to */
  int               video_window_x;
  int               video_window_y;
  int               video_window_width;
  int               video_window_height;

  /* extent of reference coordinate system */
  int               extent_width;
  int               extent_height;

  int               unscaled;       /* true if it should be blended unscaled */
  int               use_dirty_rect; /* true if update of dirty rect only is possible */

  vo_overlay_t      *ovl;

  vdpau_output_surface_t render_surface;
} vdpau_overlay_t;


typedef struct {

  vo_driver_t        vo_driver;
  vo_scale_t         sc;

  Display           *display;
  int                screen;
  Drawable           drawable;
  pthread_mutex_t    drawable_lock;
  uint32_t           display_width;
  uint32_t           display_height;

  config_values_t   *config;

  int                 ovl_changed;
  int                 num_ovls;
  int                 old_num_ovls;
  vdpau_overlay_t     overlays[XINE_VORAW_MAX_OVL];
  uint32_t           *ovl_pixmap;
  int                 ovl_pixmap_size;
  VdpOutputSurface    ovl_layer_surface;
  VdpRect             ovl_src_rect;
  VdpRect             ovl_dest_rect;
  VdpRect             ovl_video_dest_rect;
  vdpau_output_surface_t ovl_main_render_surface;

  VdpVideoSurface      soft_surface;
  uint32_t             soft_surface_width;
  uint32_t             soft_surface_height;
  int                  soft_surface_format;

#define NOUTPUTSURFACEBUFFER  25
  vdpau_output_surface_t output_surface_buffer[NOUTPUTSURFACEBUFFER];
  int                  output_surface_buffer_size;
  int                  num_big_output_surfaces_created;

#define NOUTPUTSURFACE 8
  VdpOutputSurface     output_surface[NOUTPUTSURFACE];
  uint8_t              current_output_surface;
  uint32_t             output_surface_width[NOUTPUTSURFACE];
  uint32_t             output_surface_height[NOUTPUTSURFACE];
  uint8_t              init_queue;
  uint8_t              queue_length;

  vdpau_grab_video_frame_t *pending_grab_request;
  pthread_mutex_t      grab_lock;
  pthread_cond_t       grab_cond;

  VdpVideoMixer        video_mixer;
  VdpChromaType        video_mixer_chroma;
  uint32_t             video_mixer_width;
  uint32_t             video_mixer_height;
  VdpBool              temporal_spatial_is_supported;
  VdpBool              temporal_is_supported;
  VdpBool              noise_reduction_is_supported;
  VdpBool              sharpness_is_supported;
  VdpBool              inverse_telecine_is_supported;
  VdpBool              skip_chroma_is_supported;
  VdpBool              background_is_supported;

  const char*          deinterlacers_name[NUMBER_OF_DEINTERLACERS+1];
  int                  deinterlacers_method[NUMBER_OF_DEINTERLACERS];

  int                  scaling_level_max;
  int                  scaling_level_current;

  VdpColor             back_color;

  vdpau_frame_t        *back_frame[ NUM_FRAMES_BACK ];

  uint32_t          capabilities;
  xine_t            *xine;

  int               hue;
  int               saturation;
  int               brightness;
  int               contrast;
  int               sharpness;
  int               noise;
  int               deinterlace;
  int               deinterlace_method_hd;
  int               deinterlace_method_sd;
  int               enable_inverse_telecine;
  int               honor_progressive;
  int               skip_chroma;
  int               sd_only_properties;
  int               background;

  int               vdp_runtime_nr;
  int               reinit_needed;

  int               surface_cleared_nr;

  int               allocated_surfaces;
  int		            zoom_x;
  int		            zoom_y;

  int               color_matrix;
  int               update_csc;
  int               cm_state;
} vdpau_driver_t;

/* import common color matrix stuff */
#define CM_HAVE_YCGCO_SUPPORT 1
#define CM_DRIVER_T vdpau_driver_t
#include "color_matrix.c"


typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
} vdpau_class_t;



static VdpStatus vdpau_get_output_surface (vdpau_driver_t *this, uint32_t width, uint32_t height, vdpau_output_surface_t *r)
{
  int i, full = 1;
  vdpau_output_surface_t *smallest = NULL, *best = NULL;
  vdpau_output_surface_t *l = &this->output_surface_buffer[0];
  VdpStatus st = VDP_STATUS_OK;

  for (i = this->output_surface_buffer_size; i; --i, ++l) {
    if (l->surface == VDP_INVALID_HANDLE)
      full = 0;
    else if ((l->width >= width && l->height >= height) && (best == NULL || l->size < best->size))
      best = l;
    else if (smallest == NULL || l->size < smallest->size)
      smallest = l;
  }

  if (best != NULL) {
    *r = *best;
    best->surface = VDP_INVALID_HANDLE;
  } else if (full) {
    *r = *smallest;
    smallest->surface = VDP_INVALID_HANDLE;
  } else
    r->surface = VDP_INVALID_HANDLE;

  if (r->surface != VDP_INVALID_HANDLE && (r->width < width || r->height < height)) {
    lprintf("destroy output surface %d\n", (int)r->surface);
    st = vdp_output_surface_destroy(r->surface);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vdpau_get_output_surface: vdp_output_surface_destroy failed : %s\n", vdp_get_error_string(st));
    r->surface = VDP_INVALID_HANDLE;
  }

  if (r->surface == VDP_INVALID_HANDLE) {
    if (this->num_big_output_surfaces_created < this->output_surface_buffer_size) {
        /* We create big output surfaces which should fit for many output buffer requests as long
         * as the reuse buffer can hold them */
      if (width < this->video_mixer_width)
        width = this->video_mixer_width;
      if (height < this->video_mixer_height)
        height = this->video_mixer_height;

      if (width < this->display_width)
        width = this->display_width;
      if (height < this->display_height)
        height = this->display_height;

      ++this->num_big_output_surfaces_created;
    }

    st = vdp_output_surface_create(vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, width, height, &r->surface);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vdpau_get_output_surface: vdp_output_surface_create failed : %s\n", vdp_get_error_string(st));
    r->width = width;
    r->height = height;
    r->size = width * height;
    lprintf("create output surface %dx%d -> %d\n", (int)r->width, (int)r->height, (int)r->surface);
  }

  return st;
}



static void vdpau_free_output_surface (vdpau_driver_t *this, vdpau_output_surface_t *os)
{
  if (os->surface == VDP_INVALID_HANDLE)
    return;

  vdpau_output_surface_t *smallest = NULL;
  vdpau_output_surface_t *l = &this->output_surface_buffer[0];
  int i;
  for (i = this->output_surface_buffer_size; i; --i, ++l) {
    if (l->surface == VDP_INVALID_HANDLE) {
      *l = *os;
      os->surface = VDP_INVALID_HANDLE;
      return;
    } else if (smallest == NULL || l->size < smallest->size)
      smallest = l;
  }

  VdpOutputSurface surface;
  if (smallest && smallest->size < os->size) {
    surface = smallest->surface;
    *smallest = *os;
  } else
    surface = os->surface;

  lprintf("destroy output surface %d\n", (int)surface);
  VdpStatus st = vdp_output_surface_destroy (surface);
  if (st != VDP_STATUS_OK)
    fprintf(stderr, "vdpau_free_output_surface: vdp_output_surface_destroy failed : %s\n", vdp_get_error_string(st));

  os->surface = VDP_INVALID_HANDLE;
}


static void vdpau_overlay_begin (vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  this->ovl_changed = changed;
  if ( changed ) {
    this->old_num_ovls = this->num_ovls;
    this->num_ovls = 0;
    lprintf("overlay begin\n");
  }
}


static void vdpau_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *voovl)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  if (!this->ovl_changed)
    return;

  int i = this->num_ovls;
  if (i >= XINE_VORAW_MAX_OVL)
    return;

  if (voovl->width <= 0 || voovl->height <= 0 || (!voovl->rle && (!voovl->argb_layer || !voovl->argb_layer->buffer)))
    return;

  if (voovl->rle)
    lprintf("overlay[%d] rle %s%s %dx%d@%d,%d  extend %dx%d  hili %d,%d-%d,%d  video %dx%d@%d,%d\n", i,
                  voovl->unscaled ? " unscaled ": " scaled ",
                  (voovl->rgb_clut > 0 || voovl->hili_rgb_clut > 0) ? " rgb ": " ycbcr ",
                  voovl->width, voovl->height, voovl->x, voovl->y,
                  voovl->extent_width, voovl->extent_height,
                  voovl->hili_left, voovl->hili_top,
                  voovl->hili_right, voovl->hili_bottom,
                  voovl->video_window_width,voovl->video_window_height,
                  voovl->video_window_x,voovl->video_window_y);
  if (voovl->argb_layer && voovl->argb_layer->buffer)
    lprintf("overlay[%d] argb %s %dx%d@%d,%d  extend %dx%d, dirty %d,%d-%d,%d  video %dx%d@%d,%d\n", i,
                  voovl->unscaled ? " unscaled ": " scaled ",
                  voovl->width, voovl->height, voovl->x, voovl->y,
                  voovl->extent_width, voovl->extent_height,
                  voovl->argb_layer->x1, voovl->argb_layer->y1,
                  voovl->argb_layer->x2, voovl->argb_layer->y2,
                  voovl->video_window_width,voovl->video_window_height,
                  voovl->video_window_x,voovl->video_window_y);

  vdpau_overlay_t *ovl = &this->overlays[i];

  if (i >= this->old_num_ovls ||
      (ovl->use_dirty_rect &&
        (ovl->render_surface.surface == VDP_INVALID_HANDLE ||
        voovl->rle ||
        ovl->x != voovl->x || ovl->y != voovl->y ||
        ovl->width != voovl->width || ovl->height != voovl->height)))
    ovl->use_dirty_rect = 0;

  ovl->ovl = voovl;
  ovl->x = voovl->x;
  ovl->y = voovl->y;
  ovl->width = voovl->width;
  ovl->height = voovl->height;
  ovl->extent_width = voovl->extent_width;
  ovl->extent_height = voovl->extent_height;
  ovl->unscaled = voovl->unscaled;
  ovl->video_window_x = voovl->video_window_x;
  ovl->video_window_y = voovl->video_window_y;
  ovl->video_window_width = voovl->video_window_width;
  ovl->video_window_height = voovl->video_window_height;

  this->num_ovls = i + 1;
}


static void vdpau_overlay_end (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  if (!this->ovl_changed)
    return;

  int i;
  for (i = 0; i < this->old_num_ovls; ++i) {
    vdpau_overlay_t *ovl = &this->overlays[i];
    if (i >= this->num_ovls || !ovl->use_dirty_rect) {
      lprintf("overlay[%d] free render surface %d\n", i, (int)ovl->render_surface.surface);
      vdpau_free_output_surface(this, &ovl->render_surface);
    }
  }
  if (this->ovl_main_render_surface.surface != VDP_INVALID_HANDLE) {
    lprintf("overlay free main render surface %d\n", (int)this->ovl_main_render_surface.surface);
    vdpau_free_output_surface(this, &this->ovl_main_render_surface);
  }

  for (i = 0; i < this->num_ovls; ++i) {
    vdpau_overlay_t *ovl = &this->overlays[i];
    vo_overlay_t *voovl = ovl->ovl;

    if (!ovl->use_dirty_rect) {
      vdpau_get_output_surface(this, ovl->width, ovl->height, &ovl->render_surface);
      lprintf("overlay[%d] get render surface %dx%d -> %d\n", i, ovl->width, ovl->height, (int)ovl->render_surface.surface);
    }

    uint32_t *pixmap;
    int is_argb = 1;
    if (voovl->rle) {
      if ((ovl->width * ovl->height) > this->ovl_pixmap_size) {
        this->ovl_pixmap_size = ovl->width * ovl->height;
        free(this->ovl_pixmap);
        this->ovl_pixmap = calloc(this->ovl_pixmap_size, sizeof(uint32_t));
      }

      pixmap = this->ovl_pixmap;
      rle_elem_t *rle = voovl->rle;
      int num_rle = voovl->num_rle;
      int pos = 0;
      while (num_rle > 0) {
        int x = pos % ovl->width;
        int y = pos / ovl->width;
        clut_t *colors;
        uint8_t *trans;
        if (x >= voovl->hili_left && x <= voovl->hili_right && y >= voovl->hili_top && y <= voovl->hili_bottom) {
          colors = (clut_t*)voovl->hili_color;
          trans = voovl->hili_trans;
          is_argb = voovl->hili_rgb_clut;
        } else {
          colors = (clut_t*)voovl->color;
          trans = voovl->trans;
          is_argb = voovl->rgb_clut;
        }

        int clr = rle->color;
        uint32_t pixel;
        if ( trans[clr] == 0 )
          pixel = 0;
        else if (is_argb)
          pixel = (((uint32_t)trans[clr] * 255 / 15) << 24) | (((uint32_t)colors[clr].y) << 16) | (((uint32_t)colors[clr].cr) << 8) | ((uint32_t)colors[clr].cb);
        else
          pixel = (((uint32_t)trans[clr] * 255 / 15) << 24) | (((uint32_t)colors[clr].y) << 16) | (((uint32_t)colors[clr].cb) << 8) | ((uint32_t)colors[clr].cr);

        int rlelen = rle->len;
        pos += rlelen;
        while (rlelen > 0) {
          *pixmap++ = pixel;
          --rlelen;
        }
        ++rle;
        --num_rle;
      }

      int n = ovl->width * ovl->height - pos;
      if (n > 0)
        memset(pixmap, 0, n * sizeof(uint32_t));

      pixmap = this->ovl_pixmap;
    } else {
      pthread_mutex_lock(&voovl->argb_layer->mutex);
      pixmap = voovl->argb_layer->buffer;
    }

    VdpRect put_rect;
    if (ovl->use_dirty_rect) {
      put_rect.x0 = voovl->argb_layer->x1;
      put_rect.y0 = voovl->argb_layer->y1;
      put_rect.x1 = voovl->argb_layer->x2;
      put_rect.y1 = voovl->argb_layer->y2;
    } else {
      put_rect.x0 = 0;
      put_rect.y0 = 0;
      put_rect.x1 = ovl->width;
      put_rect.y1 = ovl->height;
    }

    VdpStatus st;
    uint32_t pitch = ovl->width * sizeof(uint32_t);
    if (is_argb) {
      lprintf("overlay[%d] put %s %d,%d:%d,%d\n", i, ovl->use_dirty_rect ? "dirty argb": "argb", put_rect.x0, put_rect.y0, put_rect.x1, put_rect.y1);
      st = vdp_output_surface_put_bits(ovl->render_surface.surface, &pixmap, &pitch, &put_rect);
      if ( st != VDP_STATUS_OK )
          fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_put_bits_native failed : %s\n", vdp_get_error_string(st));
    } else {
      lprintf("overlay[%d] put ycbcr %d,%d:%d,%d\n", i, put_rect.x0, put_rect.y0, put_rect.x1, put_rect.y1);
      st = vdp_output_surface_put_bits_ycbcr(ovl->render_surface.surface, VDP_YCBCR_FORMAT_V8U8Y8A8, &pixmap, &pitch, &put_rect, NULL);
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_put_bits_ycbcr failed : %s\n", vdp_get_error_string(st));
    }

    if (voovl->rle)
      ovl->use_dirty_rect = 0;
    else {
      pthread_mutex_unlock(&voovl->argb_layer->mutex);
      ovl->use_dirty_rect = 1;
    }
  }
}


static void vdpau_process_overlays (vdpau_driver_t *this)
{
  int novls = this->num_ovls;
  if (!novls) {
    this->ovl_changed = 0;
    return;
  }

  int zoom = (this->sc.delivered_width > this->sc.displayed_width || this->sc.delivered_height > this->sc.displayed_height);

  VdpRect vid_src_rect;
  if (zoom) {
    /* compute displayed video window coordinates */
    vid_src_rect.x0 = this->sc.displayed_xoffset;
    vid_src_rect.y0 = this->sc.displayed_yoffset;
    vid_src_rect.x1 = this->sc.displayed_width + this->sc.displayed_xoffset;
    vid_src_rect.y1 = this->sc.displayed_height + this->sc.displayed_yoffset;
  }

  /* compute video window output coordinates */
  VdpRect vid_rect;
  vid_rect.x0 = this->sc.output_xoffset;
  vid_rect.y0 = this->sc.output_yoffset;
  vid_rect.x1 = this->sc.output_xoffset + this->sc.output_width;
  vid_rect.y1 = this->sc.output_yoffset + this->sc.output_height;
  this->ovl_video_dest_rect = vid_rect;

  VdpRect ovl_rects[XINE_VORAW_MAX_OVL], ovl_src_rects[XINE_VORAW_MAX_OVL];
  int i, first_visible = 0, nvisible = 0;
  for (i = 0; i < novls; ++i) {
    vdpau_overlay_t *ovl = &this->overlays[i];

    /* compute unscaled displayed overlay window coordinates */
    VdpRect ovl_src_rect;
    ovl_src_rect.x0 = 0;
    ovl_src_rect.y0 = 0;
    ovl_src_rect.x1 = ovl->width;
    ovl_src_rect.y1 = ovl->height;

    /* compute unscaled overlay window output coordinates */
    VdpRect ovl_rect;
    ovl_rect.x0 = ovl->x;
    ovl_rect.y0 = ovl->y;
    ovl_rect.x1 = ovl->x + ovl->width;
    ovl_rect.y1 = ovl->y + ovl->height;

    /* Note: Always coordinates of last overlay osd video window is taken into account */
    if (ovl->video_window_width > 0 && ovl->video_window_height > 0) {
      /* compute unscaled osd video window output coordinates */
      vid_rect.x0 = ovl->video_window_x;
      vid_rect.y0 = ovl->video_window_y;
      vid_rect.x1 = ovl->video_window_x + ovl->video_window_width;
      vid_rect.y1 = ovl->video_window_y + ovl->video_window_height;
      this->ovl_video_dest_rect = vid_rect;
    }

    if (ovl->unscaled==2) {
      ovl_rect.x0 = 0;
      ovl_rect.y0 = 0;
      ovl_rect.x1 = this->sc.gui_width;
      ovl_rect.y1 = this->sc.gui_height;
      this->ovl_changed = 1;
    }
    else if (ovl->unscaled==0) {
      double rx, ry;
      VdpRect clip_rect;
      if (ovl->extent_width > 0 && ovl->extent_height > 0) {
        if (zoom) {
          /* compute frame size to extend size scaling factor */
          rx = (double)ovl->extent_width / (double)this->sc.delivered_width;
          ry = (double)ovl->extent_height / (double)this->sc.delivered_height;

          /* scale displayed video window coordinates to extend coordinates */
          clip_rect.x0 = vid_src_rect.x0 * rx + 0.5;
          clip_rect.y0 = vid_src_rect.y0 * ry + 0.5;
          clip_rect.x1 = vid_src_rect.x1 * rx + 0.5;
          clip_rect.y1 = vid_src_rect.y1 * ry + 0.5;

          /* compute displayed size to output size scaling factor */
          rx = (double)this->sc.output_width / (double)(clip_rect.x1 - clip_rect.x0);
          ry = (double)this->sc.output_height / (double)(clip_rect.y1 - clip_rect.y0);

        } else {
          /* compute extend size to output size scaling factor */
          rx = (double)this->sc.output_width / (double)ovl->extent_width;
          ry = (double)this->sc.output_height / (double)ovl->extent_height;
        }
      } else {
        if (zoom)
          clip_rect = vid_src_rect;

        /* compute displayed size to output size scaling factor */
        rx = (double)this->sc.output_width / (double)this->sc.displayed_width;
        ry = (double)this->sc.output_height / (double)this->sc.displayed_height;
      }
      if (zoom) {
        /* clip overlay window to margins of displayed video window */
        if (ovl_rect.x0 < clip_rect.x0) {
          ovl_src_rect.x0 = clip_rect.x0 - ovl_rect.x0;
          ovl_rect.x0 = clip_rect.x0;
        }
        if (ovl_rect.y0 < clip_rect.y0) {
          ovl_src_rect.y0 = clip_rect.y0 - ovl_rect.y0;
          ovl_rect.y0 = clip_rect.y0;
        }
        if (ovl_rect.x1 > clip_rect.x1) {
          ovl_src_rect.x1 -= (ovl_rect.x1 - clip_rect.x1);
          ovl_rect.x1 = clip_rect.x1;
        }
        if (ovl_rect.y1 > clip_rect.y1) {
          ovl_src_rect.y1 -= (ovl_rect.y1 - clip_rect.y1);
          ovl_rect.y1 = clip_rect.y1;
        }

        ovl_rect.x0 -= clip_rect.x0;
        ovl_rect.y0 -= clip_rect.y0;
        ovl_rect.x1 -= clip_rect.x0;
        ovl_rect.y1 -= clip_rect.y0;
      }

      /* scale overlay window coordinates to output window coordinates */
      ovl_rect.x0 = (double)ovl_rect.x0 * rx + 0.5;
      ovl_rect.y0 = (double)ovl_rect.y0 * ry + 0.5;
      ovl_rect.x1 = (double)ovl_rect.x1 * rx + 0.5;
      ovl_rect.y1 = (double)ovl_rect.y1 * ry + 0.5;

      ovl_rect.x0 += this->sc.output_xoffset;
      ovl_rect.y0 += this->sc.output_yoffset;
      ovl_rect.x1 += this->sc.output_xoffset;
      ovl_rect.y1 += this->sc.output_yoffset;

      if (ovl->video_window_width > 0 && ovl->video_window_height > 0) {
        if (zoom) {
          /* clip osd video window to margins of displayed video window */
          if (vid_rect.x0 < clip_rect.x0)
            vid_rect.x0 = clip_rect.x0;
          if (vid_rect.y0 < clip_rect.y0)
            vid_rect.y0 = clip_rect.y0;
          if (vid_rect.x1 > clip_rect.x1)
            vid_rect.x1 = clip_rect.x1;
          if (vid_rect.y1 > clip_rect.y1)
            vid_rect.y1 = clip_rect.y1;

          vid_rect.x0 -= clip_rect.x0;
          vid_rect.y0 -= clip_rect.y0;
          vid_rect.x1 -= clip_rect.x0;
          vid_rect.y1 -= clip_rect.y0;
        }

        /* scale osd video window coordinates to output window coordinates */
        vid_rect.x0 = (double)vid_rect.x0 * rx + 0.5;
        vid_rect.y0 = (double)vid_rect.y0 * ry + 0.5;
        vid_rect.x1 = (double)vid_rect.x1 * rx + 0.5;
        vid_rect.y1 = (double)vid_rect.y1 * ry + 0.5;

        vid_rect.x0 += this->sc.output_xoffset;
        vid_rect.y0 += this->sc.output_yoffset;
        vid_rect.x1 += this->sc.output_xoffset;
        vid_rect.y1 += this->sc.output_yoffset;

        /* take only visible osd video windows into account */
        if (vid_rect.x0 < vid_rect.x1 && vid_rect.y0 < vid_rect.y1)
          this->ovl_video_dest_rect = vid_rect;
      }

      this->ovl_changed = 1;
    }

    ovl_rects[i] = ovl_rect;
    ovl_src_rects[i] = ovl_src_rect;

    /* take only visible overlays into account */
    if (ovl_rect.x0 < ovl_rect.x1 && ovl_rect.y0 < ovl_rect.y1) {
      /* compute overall output window size */
      if (nvisible == 0) {
        first_visible = i;
        this->ovl_dest_rect = ovl_rect;
      } else {
        if (ovl_rect.x0 < this->ovl_dest_rect.x0)
          this->ovl_dest_rect.x0 = ovl_rect.x0;
        if (ovl_rect.y0 < this->ovl_dest_rect.y0)
          this->ovl_dest_rect.y0 = ovl_rect.y0;
        if (ovl_rect.x1 > this->ovl_dest_rect.x1)
          this->ovl_dest_rect.x1 = ovl_rect.x1;
        if (ovl_rect.y1 > this->ovl_dest_rect.y1)
          this->ovl_dest_rect.y1 = ovl_rect.y1;
      }
      ++nvisible;
    }
  }

  if (nvisible == 0) {
    this->ovl_layer_surface = VDP_INVALID_HANDLE;
    this->ovl_changed = 0;
    lprintf("overlays not visible\n");
    return;
  } else if (nvisible == 1) {
    /* we have only one visible overlay object so we can use it directly as overlay layer surface */
    this->ovl_src_rect = ovl_src_rects[first_visible];
    this->ovl_layer_surface = this->overlays[first_visible].render_surface.surface;
  } else {
    this->ovl_src_rect.x0 = 0;
    this->ovl_src_rect.y0 = 0;
    this->ovl_src_rect.x1 = this->ovl_dest_rect.x1 - this->ovl_dest_rect.x0;
    this->ovl_src_rect.y1 = this->ovl_dest_rect.y1 - this->ovl_dest_rect.y0;
    this->ovl_layer_surface = this->ovl_main_render_surface.surface;
  }

  lprintf("overlay output %d,%d:%d,%d -> %d,%d:%d,%d  video window %d,%d:%d,%d\n",
                  this->ovl_src_rect.x0, this->ovl_src_rect.y0, this->ovl_src_rect.x1, this->ovl_src_rect.y1,
                  this->ovl_dest_rect.x0, this->ovl_dest_rect.y0, this->ovl_dest_rect.x1, this->ovl_dest_rect.y1,
                  this->ovl_video_dest_rect.x0, this->ovl_video_dest_rect.y0, this->ovl_video_dest_rect.x1, this->ovl_video_dest_rect.y1);

  if (!this->ovl_changed)
    return;

  if (nvisible == 1) {
    this->ovl_changed = 0;
    return;
  }

  if (this->ovl_main_render_surface.surface != VDP_INVALID_HANDLE) {
    lprintf("overlay free main render surface %d\n", (int)this->ovl_main_render_surface.surface);
    vdpau_free_output_surface(this, &this->ovl_main_render_surface);
  }

  vdpau_get_output_surface(this, this->ovl_src_rect.x1, this->ovl_src_rect.y1, &this->ovl_main_render_surface);
  lprintf("overlay get main render surface %dx%d -> %d\n", this->ovl_src_rect.x1, this->ovl_src_rect.y1, (int)this->ovl_main_render_surface.surface);

  this->ovl_layer_surface = this->ovl_main_render_surface.surface;

  /* Clear main render surface if first overlay does not cover hole output window */
  if (this->ovl_dest_rect.x0 != ovl_rects[first_visible].x0 ||
                  this->ovl_dest_rect.x1 != ovl_rects[first_visible].x1 ||
                  this->ovl_dest_rect.y0 != ovl_rects[first_visible].y0 ||
                  this->ovl_dest_rect.y1 != ovl_rects[first_visible].y1) {
    lprintf("overlay clear main render output surface %dx%d\n", this->ovl_src_rect.x1, this->ovl_src_rect.y1);

    if (this->ovl_src_rect.x1 > this->ovl_pixmap_size) {
      this->ovl_pixmap_size = this->ovl_src_rect.x1;
      free(this->ovl_pixmap);
      this->ovl_pixmap = calloc(this->ovl_pixmap_size, sizeof(uint32_t));
    } else {
      memset(this->ovl_pixmap, 0, (this->ovl_src_rect.x1 * sizeof(uint32_t)));
    }

    uint32_t pitch = 0;
    VdpStatus st = vdp_output_surface_put_bits(this->ovl_layer_surface, &this->ovl_pixmap, &pitch, &this->ovl_src_rect);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vdpau_process_overlays: vdp_output_surface_put_bits (clear) failed : %s\n", vdp_get_error_string(st));
  }

  /* Render all visible overlays into main render surface */
  for (i = 0; i < novls; ++i) {
    vdpau_overlay_t *ovl = &this->overlays[i];

    if (ovl_rects[i].x0 < ovl_rects[i].x1 && ovl_rects[i].y0 < ovl_rects[i].y1) {
      /* compensate overall output offset of main render surface */
      VdpRect render_rect;
      render_rect.x0 = ovl_rects[i].x0 - this->ovl_dest_rect.x0;
      render_rect.x1 = ovl_rects[i].x1 - this->ovl_dest_rect.x0;
      render_rect.y0 = ovl_rects[i].y0 - this->ovl_dest_rect.y0;
      render_rect.y1 = ovl_rects[i].y1 - this->ovl_dest_rect.y0;

      lprintf("overlay[%d] render %d,%d:%d,%d -> %d,%d:%d,%d\n",
                      i, ovl_rects[i].x0, ovl_rects[i].y0, ovl_rects[i].x1, ovl_rects[i].y1, render_rect.x0, render_rect.y0, render_rect.x1, render_rect.y1);

      VdpOutputSurfaceRenderBlendState *bs = (i > first_visible) ? &blend: NULL;
      VdpStatus st = vdp_output_surface_render_output_surface(this->ovl_layer_surface, &render_rect, ovl->render_surface.surface, &ovl_src_rects[i], 0, bs, 0 );
      if (st != VDP_STATUS_OK)
        fprintf(stderr, "vdpau_process_overlays: vdp_output_surface_render_output_surface failed : %s\n", vdp_get_error_string(st));
    }
  }
  this->ovl_changed = 0;
}



static void vdpau_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src)
{
  /*vdpau_frame_t  *frame = (vdpau_frame_t *) vo_img;*/

  vo_img->proc_called = 1;
}



static void vdpau_frame_field (vo_frame_t *vo_img, int which_field)
{
}



static void vdpau_frame_dispose (vo_frame_t *vo_img)
{
  vdpau_frame_t  *frame = (vdpau_frame_t *) vo_img ;

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);
  if ( frame->vdpau_accel_data.surface != VDP_INVALID_HANDLE )
    vdp_video_surface_destroy( frame->vdpau_accel_data.surface );
  free (frame);
}



static vo_frame_t *vdpau_alloc_frame (vo_driver_t *this_gen)
{
  vdpau_frame_t  *frame;
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;

  lprintf( "vo_vdpau: vdpau_alloc_frame\n" );

  frame = (vdpau_frame_t *) calloc(1, sizeof(vdpau_frame_t));

  if (!frame)
    return NULL;

  frame->vo_frame.base[0] = frame->vo_frame.base[1] = frame->vo_frame.base[2] = NULL;
  frame->width = frame->height = frame->format = frame->flags = 0;

  frame->vo_frame.accel_data = &frame->vdpau_accel_data;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_duplicate_frame_data = NULL;
  frame->vo_frame.proc_slice = vdpau_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = vdpau_frame_field;
  frame->vo_frame.dispose    = vdpau_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  frame->surface_cleared_nr = 0;

  frame->vdpau_accel_data.vo_frame = &frame->vo_frame;
  frame->vdpau_accel_data.vdp_device = vdp_device;
  frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
  frame->vdpau_accel_data.chroma = VDP_CHROMA_TYPE_420;
  frame->vdpau_accel_data.vdp_decoder_create = vdp_decoder_create;
  frame->vdpau_accel_data.vdp_decoder_destroy = vdp_decoder_destroy;
  frame->vdpau_accel_data.vdp_decoder_render = vdp_decoder_render;
  frame->vdpau_accel_data.vdp_get_error_string = vdp_get_error_string;
  frame->vdpau_accel_data.vdp_runtime_nr = this->vdp_runtime_nr;
  frame->vdpau_accel_data.current_vdp_runtime_nr = &this->vdp_runtime_nr;

  return (vo_frame_t *) frame;
}



static void vdpau_provide_standard_frame_data (vo_frame_t *this, xine_current_frame_data_t *data)
{
  VdpStatus st;
  VdpYCbCrFormat format;
  uint32_t pitches[3];
  void *base[3];

  if (this->format != XINE_IMGFMT_VDPAU) {
    fprintf(stderr, "vdpau_provide_standard_frame_data: unexpected frame format 0x%08x!\n", this->format);
    return;
  }

  vdpau_accel_t *accel = (vdpau_accel_t *) this->accel_data;

  if (accel->vdp_runtime_nr != *(accel->current_vdp_runtime_nr))
    return;

  this = accel->vo_frame;

  if (accel->chroma == VDP_CHROMA_TYPE_420) {
    data->format = XINE_IMGFMT_YV12;
    data->img_size = this->width * this->height
                   + ((this->width + 1) / 2) * ((this->height + 1) / 2)
                   + ((this->width + 1) / 2) * ((this->height + 1) / 2);
    if (data->img) {
      pitches[0] = this->width;
      pitches[2] = this->width / 2;
      pitches[1] = this->width / 2;
      base[0] = data->img;
      base[2] = data->img + this->width * this->height;
      base[1] = data->img + this->width * this->height + this->width * this->height / 4;
      format = VDP_YCBCR_FORMAT_YV12;
    }
  } else {
    data->format = XINE_IMGFMT_YUY2;
    data->img_size = this->width * this->height
                   + ((this->width + 1) / 2) * this->height
                   + ((this->width + 1) / 2) * this->height;
    if (data->img) {
      pitches[0] = this->width * 2;
      base[0] = data->img;
      format = VDP_YCBCR_FORMAT_YUYV;
    }
  }

  if (data->img) {
    st = vdp_video_surface_getbits_ycbcr(accel->surface, format, base, pitches);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vo_vdpau: failed to get surface bits !! %s\n", vdp_get_error_string(st));
  }
}



static void vdpau_duplicate_frame_data (vo_frame_t *this_gen, vo_frame_t *original)
{
  vdpau_frame_t *this = (vdpau_frame_t *)this_gen;
  vdpau_frame_t *orig = (vdpau_frame_t *)original;
  VdpStatus st;
  VdpYCbCrFormat format;

  if (orig->vo_frame.format != XINE_IMGFMT_VDPAU) {
    fprintf(stderr, "vdpau_duplicate_frame_data: unexpected frame format 0x%08x!\n", orig->vo_frame.format);
    return;
  }

  if(orig->vdpau_accel_data.vdp_runtime_nr != this->vdpau_accel_data.vdp_runtime_nr) {
    fprintf(stderr, "vdpau_duplicate_frame_data: called with invalid frame\n");
    return;
  }

  if (!(orig->flags & VO_CHROMA_422)) {
    this->vo_frame.pitches[0] = 8*((orig->vo_frame.width + 7) / 8);
    this->vo_frame.pitches[1] = 8*((orig->vo_frame.width + 15) / 16);
    this->vo_frame.pitches[2] = 8*((orig->vo_frame.width + 15) / 16);
    this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * orig->vo_frame.height);
    this->vo_frame.base[1] = av_mallocz(this->vo_frame.pitches[1] * ((orig->vo_frame.height+1)/2));
    this->vo_frame.base[2] = av_mallocz(this->vo_frame.pitches[2] * ((orig->vo_frame.height+1)/2));
    format = VDP_YCBCR_FORMAT_YV12;
  } else {
    this->vo_frame.pitches[0] = 8*((orig->vo_frame.width + 3) / 4);
    this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * orig->vo_frame.height);
    format = VDP_YCBCR_FORMAT_YUYV;
  }

  st = vdp_video_surface_getbits_ycbcr(orig->vdpau_accel_data.surface, format, this->vo_frame.base, this->vo_frame.pitches);
  if (st != VDP_STATUS_OK)
    fprintf(stderr, "vo_vdpau: failed to get surface bits !! %s\n", vdp_get_error_string(st));

  st = vdp_video_surface_putbits_ycbcr(this->vdpau_accel_data.surface, format, this->vo_frame.base, this->vo_frame.pitches);
  if (st != VDP_STATUS_OK)
    fprintf(stderr, "vo_vdpau: failed to put surface bits !! %s\n", vdp_get_error_string(st));

  av_freep (&this->vo_frame.base[0]);
  av_freep (&this->vo_frame.base[1]);
  av_freep (&this->vo_frame.base[2]);
}



static void vdpau_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
      uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;
  vdpau_frame_t *frame = (vdpau_frame_t *) frame_gen;
  uint32_t requested_width = width;
  uint32_t requested_height = height;

  int clear = 0;

  if ( flags & VO_NEW_SEQUENCE_FLAG )
    ++this->surface_cleared_nr;

  VdpChromaType chroma = (flags & VO_CHROMA_422) ? VDP_CHROMA_TYPE_422 : VDP_CHROMA_TYPE_420;

  /* adjust width and height to meet xine and VDPAU constraints */
  width = (width + ((flags & VO_CHROMA_422) ? 3 : 15)) & ~((flags & VO_CHROMA_422) ? 3 : 15); /* xine constraint */
  height = (height + 3) & ~3; /* VDPAU constraint */
  /* any excess pixels from the adjustment will be cropped away */
  frame->vo_frame.width = width;
  frame->vo_frame.height = height;
  frame->vo_frame.crop_right += width - requested_width;
  frame->vo_frame.crop_bottom += height - requested_height;

  /* Check frame size and format and reallocate if necessary */
  if ( (frame->width != width) || (frame->height != height) || (frame->format != format) || (frame->format==XINE_IMGFMT_VDPAU && frame->vdpau_accel_data.chroma!=chroma) ||
        (frame->vdpau_accel_data.vdp_runtime_nr != this->vdp_runtime_nr)) {

    /* (re-) allocate render space */
    av_freep (&frame->vo_frame.base[0]);
    av_freep (&frame->vo_frame.base[1]);
    av_freep (&frame->vo_frame.base[2]);

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz (frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz (frame->vo_frame.pitches[2] * ((height+1)/2));
    } else if (format == XINE_IMGFMT_YUY2){
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
    }

    if ( frame->vdpau_accel_data.vdp_runtime_nr != this->vdp_runtime_nr ) {
      frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
      frame->vdpau_accel_data.vdp_runtime_nr = this->vdp_runtime_nr;
      frame->vdpau_accel_data.vdp_device = vdp_device;
      frame->vo_frame.proc_duplicate_frame_data = NULL;
      frame->vo_frame.proc_provide_standard_frame_data = NULL;
    }

    if ( frame->vdpau_accel_data.surface != VDP_INVALID_HANDLE  ) {
      if ( (frame->width != width) || (frame->height != height) || (format != XINE_IMGFMT_VDPAU) || frame->vdpau_accel_data.chroma != chroma ) {
        lprintf("vo_vdpau: update_frame - destroy surface\n");
        vdp_video_surface_destroy( frame->vdpau_accel_data.surface );
        frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
        --this->allocated_surfaces;
        frame->vo_frame.proc_duplicate_frame_data = NULL;
        frame->vo_frame.proc_provide_standard_frame_data = NULL;
      }
    }

    if ( (format == XINE_IMGFMT_VDPAU) && (frame->vdpau_accel_data.surface == VDP_INVALID_HANDLE) ) {
      VdpStatus st = vdp_video_surface_create( vdp_device, chroma, width, height, &frame->vdpau_accel_data.surface );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to create surface !! %s\n", vdp_get_error_string( st ) );
      else {
        clear = 1;
        frame->vdpau_accel_data.chroma = chroma;
        ++this->allocated_surfaces;
        frame->vo_frame.proc_duplicate_frame_data = vdpau_duplicate_frame_data;
        frame->vo_frame.proc_provide_standard_frame_data = vdpau_provide_standard_frame_data;

        /* check whether allocated surface matches constraints */
        {
          VdpChromaType ct = (VdpChromaType)-1;
          int w = -1;
          int h = -1;

          st = vdp_video_surface_get_parameters(frame->vdpau_accel_data.surface, &ct, &w, &h);
          if (st != VDP_STATUS_OK)
            fprintf(stderr, "vo_vdpau: failed to get parameters !! %s\n", vdp_get_error_string(st));
          else if (w != width || h != height) {
   
            fprintf(stderr, "vo_vdpau: video surface doesn't match size contraints (%d x %d) -> (%d x %d) != (%d x %d). Segfaults ahead!\n"
              , requested_width, requested_height, width, height, w, h);
          }
        }
      }
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->flags = flags;

    vdpau_frame_field ((vo_frame_t *)frame, flags);
  }

  if ( (format == XINE_IMGFMT_VDPAU) && (clear || (frame->surface_cleared_nr != this->surface_cleared_nr)) ) {
    lprintf( "clear surface: %d\n", frame->vdpau_accel_data.surface );
    if ( frame->vdpau_accel_data.chroma == VDP_CHROMA_TYPE_422 ) {
      uint8_t *cb = malloc( frame->width * 2 );
      memset( cb, 127, frame->width * 2 );
      uint32_t pitches[] = { 0 };
      void* data[] = { cb };
      VdpStatus st = vdp_video_surface_putbits_ycbcr( frame->vdpau_accel_data.surface, VDP_YCBCR_FORMAT_YUYV, &data, pitches );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to clear surface: %s\n", vdp_get_error_string( st ) );
      free( cb );
    }
    else {
      uint8_t *cb = malloc( frame->width );
      memset( cb, 127, frame->width );
      uint32_t pitches[] = { 0, 0, 0 };
      void* data[] = { cb, cb, cb };
      VdpStatus st = vdp_video_surface_putbits_ycbcr( frame->vdpau_accel_data.surface, VDP_YCBCR_FORMAT_YV12, &data, pitches );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to clear surface: %s\n", vdp_get_error_string( st ) );
      free( cb );
    }
    if ( frame->surface_cleared_nr != this->surface_cleared_nr )
      frame->surface_cleared_nr = this->surface_cleared_nr;
  }

  frame->ratio = ratio;
  frame->vo_frame.future_frame = NULL;
}



static int vdpau_redraw_needed (vo_driver_t *this_gen)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  _x_vo_scale_compute_ideal_size( &this->sc );
  if ( _x_vo_scale_redraw_needed( &this->sc ) ) {
    _x_vo_scale_compute_output_size( &this->sc );
    return 1;
  }
  return this->update_csc;
}



static void vdpau_release_back_frames( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  int i;

  for ( i=0; i<NUM_FRAMES_BACK; ++i ) {
    if ( this->back_frame[ i ])
      this->back_frame[ i ]->vo_frame.free( &this->back_frame[ i ]->vo_frame );
    this->back_frame[ i ] = NULL;
  }
}



static void vdpau_backup_frame( vo_driver_t *this_gen, vo_frame_t *frame_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  vdpau_frame_t   *frame = (vdpau_frame_t *) frame_gen;

  int i;
  if ( this->back_frame[NUM_FRAMES_BACK-1]) {
    this->back_frame[NUM_FRAMES_BACK-1]->vo_frame.free (&this->back_frame[NUM_FRAMES_BACK-1]->vo_frame);
  }
  for ( i=NUM_FRAMES_BACK-1; i>0; i-- )
    this->back_frame[i] = this->back_frame[i-1];
  this->back_frame[0] = frame;
}



static void vdpau_set_deinterlace( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  VdpVideoMixerFeature features[2];
  VdpBool feature_enables[2];
  int features_count = 0;
  int deinterlace_method;
  
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }

  if ( !features_count )
    return;

  if ( this->deinterlace ) {
    if ( this->video_mixer_width < 800 )
      deinterlace_method = this->deinterlace_method_sd;
    else
      deinterlace_method = this->deinterlace_method_hd;
  
    switch ( this->deinterlacers_method[deinterlace_method] ) {
      case DEINT_BOB:
        feature_enables[0] = feature_enables[1] = 0;
        fprintf(stderr, "vo_vdpau: deinterlace: bob\n" );
        break;
      case DEINT_HALF_TEMPORAL:
        feature_enables[0] = 1; feature_enables[1] = 0;
        fprintf(stderr, "vo_vdpau: deinterlace: half_temporal\n" );
        break;
      case DEINT_TEMPORAL:
        feature_enables[0] = 1; feature_enables[1] = 0;
        fprintf(stderr, "vo_vdpau: deinterlace: temporal\n" );
        break;
      case DEINT_HALF_TEMPORAL_SPATIAL:
        feature_enables[0] = feature_enables[1] = 1;
        fprintf(stderr, "vo_vdpau: deinterlace: half_temporal_spatial\n" );
        break;
      case DEINT_TEMPORAL_SPATIAL:
        feature_enables[0] = feature_enables[1] = 1;
        fprintf(stderr, "vo_vdpau: deinterlace: temporal_spatial\n" );
        break;
    }
  }
  else {
    feature_enables[0] = feature_enables[1] = 0;
    fprintf(stderr, "vo_vdpau: deinterlace: none\n" );
  }

  vdp_video_mixer_set_feature_enables( this->video_mixer, features_count, features, feature_enables );
}



static void vdpau_set_inverse_telecine( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( !this->inverse_telecine_is_supported )
    return;

  VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE };
  VdpBool feature_enables[1];
  if ( this->deinterlace && this->enable_inverse_telecine )
    feature_enables[0] = 1;
  else
    feature_enables[0] = 0;

  vdp_video_mixer_set_feature_enables( this->video_mixer, 1, features, feature_enables );
  vdp_video_mixer_get_feature_enables( this->video_mixer, 1, features, feature_enables );
  fprintf(stderr, "vo_vdpau: enabled features: inverse_telecine=%d\n", feature_enables[0] );
}



static void vdpau_update_deinterlace_method_sd( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->deinterlace_method_sd = entry->num_value;
  fprintf(stderr,  "vo_vdpau: deinterlace_method_sd=%d\n", this->deinterlace_method_sd );
  vdpau_set_deinterlace( (vo_driver_t*)this_gen );
}



static void vdpau_update_deinterlace_method_hd( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->deinterlace_method_hd = entry->num_value;
  fprintf(stderr,  "vo_vdpau: deinterlace_method_hd=%d\n", this->deinterlace_method_hd );
  vdpau_set_deinterlace( (vo_driver_t*)this_gen );
}



static void vdpau_set_scaling_level( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  int i;
  VdpVideoMixerFeature features[9];
  VdpBool feature_enables[9];
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[i] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    feature_enables[i] = 0;
  }
  vdp_video_mixer_set_feature_enables( this->video_mixer, this->scaling_level_max, features, feature_enables );

  if ( this->scaling_level_current ) {
    features[0] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 - 1 + this->scaling_level_current;
    feature_enables[0] = 1;
    vdp_video_mixer_set_feature_enables( this->video_mixer, 1, features, feature_enables );
  }

  fprintf(stderr,  "vo_vdpau: set_scaling_level=%d\n", this->scaling_level_current );
#endif
}



static void vdpau_update_scaling_level( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->scaling_level_current = entry->num_value;
  fprintf(stderr,  "vo_vdpau: scaling_quality=%d\n", this->scaling_level_current );
  vdpau_set_scaling_level( (vo_driver_t*)this_gen );
}



static void vdpau_update_enable_inverse_telecine( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->enable_inverse_telecine = entry->num_value;
  fprintf(stderr, "vo_vdpau: enable inverse_telecine=%d\n", this->enable_inverse_telecine );
  vdpau_set_inverse_telecine( (vo_driver_t*)this_gen );
}



static void vdpau_honor_progressive_flag( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->honor_progressive = entry->num_value;
  fprintf(stderr, "vo_vdpau: honor_progressive=%d\n", this->honor_progressive );
}



static void vdpau_update_noise( vdpau_driver_t *this_gen )
{
  if ( !this_gen->noise_reduction_is_supported )
    return;

  float value = this_gen->noise/100.0;
  if ( value==0 || ((this_gen->sd_only_properties & 1) && this_gen->video_mixer_width >= 800)) {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION };
    VdpBool feature_enables[] = { 0 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: disable noise reduction.\n" );
    return;
  }
  else {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION };
    VdpBool feature_enables[] = { 1 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: enable noise reduction.\n" );
  }

  const VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL };
  const void * const attribute_values[] = { &value };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set noise reduction level !!\n" );
}



static void vdpau_update_sharpness( vdpau_driver_t *this_gen )
{
  if ( !this_gen->sharpness_is_supported )
    return;

  float value = this_gen->sharpness/100.0;
  if ( value==0 || (this_gen->sd_only_properties >= 2 && this_gen->video_mixer_width >= 800)) {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_SHARPNESS  };
    VdpBool feature_enables[] = { 0 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: disable sharpness.\n" );
    return;
  }
  else {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_SHARPNESS  };
    VdpBool feature_enables[] = { 1 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: enable sharpness.\n" );
  }

  const VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL };
  const void * const attribute_values[] = { &value };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set sharpness level !!\n" );
}



static void vdpau_update_sd_only_properties( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->sd_only_properties = entry->num_value;
  printf( "vo_vdpau: enable sd only noise=%d, sd only sharpness %d\n", ((this->sd_only_properties & 1) != 0), (this->sd_only_properties >= 2) );
  vdpau_update_noise(this);
  vdpau_update_sharpness(this);
}



static void vdpau_update_csc_matrix (vdpau_driver_t *that, vdpau_frame_t *frame) {
  int color_matrix;

  color_matrix = cm_from_frame (&frame->vo_frame);

  if ( that->update_csc || that->color_matrix != color_matrix ) {
    VdpStatus st;
    VdpCSCMatrix matrix;
    float hue = (float)that->hue * 3.14159265359 / 128.0;
    float saturation = (float)that->saturation / 128.0;
    float contrast = (float)that->contrast / 128.0;
    float brightness = that->brightness;
    float uvcos = saturation * cos( hue );
    float uvsin = saturation * sin( hue );
    int i;

    if ((color_matrix >> 1) == 8) {
      /* YCgCo. This is really quite simple. */
      uvsin *= contrast;
      uvcos *= contrast;
      /* matrix[rgb][yuv1] */
      matrix[0][1] = -1.0 * uvcos - 1.0 * uvsin;
      matrix[0][2] =  1.0 * uvcos - 1.0 * uvsin;
      matrix[1][1] =  1.0 * uvcos;
      matrix[1][2] =                1.0 * uvsin;
      matrix[2][1] = -1.0 * uvcos + 1.0 * uvsin;
      matrix[2][2] = -1.0 * uvcos - 1.0 * uvsin;
      for (i = 0; i < 3; i++) {
        matrix[i][0] = contrast;
        matrix[i][3] = (brightness * contrast - 128.0 * (matrix[i][1] + matrix[i][2])) / 255.0;
      }
    } else {
      /* YCbCr */
      float kb, kr;
      float vr, vg, ug, ub;
      float ygain, yoffset;

      switch (color_matrix >> 1) {
        case 1:  kb = 0.0722; kr = 0.2126; break; /* ITU-R 709 */
        case 4:  kb = 0.1100; kr = 0.3000; break; /* FCC */
        case 7:  kb = 0.0870; kr = 0.2120; break; /* SMPTE 240 */
        default: kb = 0.1140; kr = 0.2990;        /* ITU-R 601 */
      }
      vr = 2.0 * (1.0 - kr);
      vg = -2.0 * kr * (1.0 - kr) / (1.0 - kb - kr);
      ug = -2.0 * kb * (1.0 - kb) / (1.0 - kb - kr);
      ub = 2.0 * (1.0 - kb);

      if (color_matrix & 1) {
        /* fullrange mode */
        yoffset = brightness;
        ygain = contrast;
        uvcos *= contrast * 255.0 / 254.0;
        uvsin *= contrast * 255.0 / 254.0;
      } else {
        /* mpeg range */
        yoffset = brightness - 16.0;
        ygain = contrast * 255.0 / 219.0;
        uvcos *= contrast * 255.0 / 224.0;
        uvsin *= contrast * 255.0 / 224.0;
      }

      /* matrix[rgb][yuv1] */
      matrix[0][1] = -uvsin * vr;
      matrix[0][2] = uvcos * vr;
      matrix[1][1] = uvcos * ug - uvsin * vg;
      matrix[1][2] = uvcos * vg + uvsin * ug;
      matrix[2][1] = uvcos * ub;
      matrix[2][2] = uvsin * ub;
      for (i = 0; i < 3; i++) {
        matrix[i][0] = ygain;
        matrix[i][3] = (yoffset * ygain - 128.0 * (matrix[i][1] + matrix[i][2])) / 255.0;
      }
    }

    that->color_matrix = color_matrix;
    that->update_csc = 0;

    const VdpVideoMixerAttribute attributes [] = {VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX};
    const void * const attribute_values[] = {&matrix};
    st = vdp_video_mixer_set_attribute_values (that->video_mixer, 1, attributes, attribute_values);
    if (st != VDP_STATUS_OK)
      fprintf (stderr, "vo_vdpau: error, can't set csc matrix !!\n");

    xprintf (that->xine, XINE_VERBOSITY_LOG,"video_out_vdpau: b %d c %d s %d h %d [%s]\n",
      that->brightness, that->contrast, that->saturation, that->hue, cm_names[color_matrix]);
  }
}



static void vdpau_update_skip_chroma( vdpau_driver_t *this_gen )
{
  if ( !this_gen->skip_chroma_is_supported )
    return;

  const VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE };
  const void* attribute_values[] = { &(this_gen->skip_chroma) };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set skip_chroma !!\n" );
  else
    fprintf(stderr, "vo_vdpau: skip_chroma = %d\n", this_gen->skip_chroma );
}



static void vdpau_set_skip_chroma( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  this->skip_chroma = entry->num_value;
  vdpau_update_skip_chroma( this );
}



static void vdpau_update_background( vdpau_driver_t *this_gen )
{
  if ( !this_gen->background_is_supported )
    return;

  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR };
  const VdpColor bg = { (this_gen->background >> 16) / 255.f, ((this_gen->background >> 8) & 0xff) / 255.f, (this_gen->background & 0xff) / 255.f, 1 };
  const void* attribute_values[] = { &bg };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    printf( "vo_vdpau: error, can't set background_color !!\n" );
  else
    printf( "vo_vdpau: background_color = %d\n", this_gen->background );
}



static void vdpau_set_background( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  entry->num_value &= 0xffffff;
  this->background = entry->num_value;
  vdpau_update_background( this );
}



static void vdpau_shift_queue( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( this->init_queue < this->queue_length )
    ++this->init_queue;
  ++this->current_output_surface;
  if ( this->current_output_surface >= this->queue_length )
    this->current_output_surface = 0;
}



static void vdpau_check_output_size( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( (this->sc.gui_width > this->output_surface_width[this->current_output_surface]) || (this->sc.gui_height > this->output_surface_height[this->current_output_surface]) ) {
    /* recreate output surface to match window size */
    lprintf( "vo_vdpau: output_surface size update\n" );
    this->output_surface_width[this->current_output_surface] = this->sc.gui_width;
    this->output_surface_height[this->current_output_surface] = this->sc.gui_height;

    VdpStatus st = vdp_output_surface_destroy( this->output_surface[this->current_output_surface] );
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vo_vdpau: Can't destroy output surface: %s\n", vdp_get_error_string (st));

    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[this->current_output_surface], this->output_surface_height[this->current_output_surface], &this->output_surface[this->current_output_surface] );
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vo_vdpau: Can't create output surface: %s\n", vdp_get_error_string (st));
  }
}


static void vdpau_grab_current_output_surface (vdpau_driver_t *this, int64_t vpts)
{
  pthread_mutex_lock(&this->grab_lock);

  vdpau_grab_video_frame_t *frame = this->pending_grab_request;
  if (frame) {
    VdpStatus st;

    this->pending_grab_request = NULL;
    frame->grab_frame.vpts = -1;

    VdpOutputSurface grab_surface = this->output_surface[this->current_output_surface];
    int width = this->sc.gui_width;
    int height = this->sc.gui_height;

    /* take cropping parameters into account */
    width = width - frame->grab_frame.crop_left - frame->grab_frame.crop_right;
    height = height - frame->grab_frame.crop_top - frame->grab_frame.crop_bottom;
    if (width < 1)
      width = 1;
    if (height < 1)
      height = 1;

    /* if caller does not specify frame size we return the actual size of grabbed frame */
    if (frame->grab_frame.width <= 0)
      frame->grab_frame.width = width;
    if (frame->grab_frame.height <= 0)
      frame->grab_frame.height = height;

    if (frame->grab_frame.width != frame->width || frame->grab_frame.height != frame->height) {
      free(frame->rgba);
      free(frame->grab_frame.img);
      frame->rgba = NULL;
      frame->grab_frame.img = NULL;

      frame->width = frame->grab_frame.width;
      frame->height = frame->grab_frame.height;
    }

    if (frame->rgba == NULL) {
      frame->rgba = (uint32_t *) calloc(frame->width * frame->height, sizeof(uint32_t));
      if (frame->rgba == NULL) {
        pthread_cond_broadcast(&this->grab_cond);
        pthread_mutex_unlock(&this->grab_lock);
        return;
      }
    }

    if (frame->grab_frame.img == NULL) {
      frame->grab_frame.img = (uint8_t *) calloc(frame->width * frame->height, 3);
      if (frame->grab_frame.img == NULL) {
        pthread_cond_broadcast(&this->grab_cond);
        pthread_mutex_unlock(&this->grab_lock);
        return;
      }
    }

    uint32_t pitches = frame->width * sizeof(uint32_t);
    VdpRect src_rect = { frame->grab_frame.crop_left, frame->grab_frame.crop_top, width+frame->grab_frame.crop_left, height+frame->grab_frame.crop_top };
    if (frame->width != width || frame->height != height) {
      st = vdpau_get_output_surface(this, frame->width, frame->height, &frame->render_surface);
      if (st == VDP_STATUS_OK) {
        lprintf("grab got render output surface %dx%d -> %d\n", frame->width, frame->height, (int)frame->render_surface.surface);

        VdpRect dst_rect = { 0, 0, frame->width, frame->height };
        st = vdp_output_surface_render_output_surface(frame->render_surface.surface, &dst_rect, grab_surface, &src_rect, NULL, NULL, VDP_OUTPUT_SURFACE_RENDER_ROTATE_0);
        if (st == VDP_STATUS_OK) {
          st = vdp_output_surface_get_bits(frame->render_surface.surface, &dst_rect, &frame->rgba, &pitches);
          if (st != VDP_STATUS_OK)
            fprintf(stderr, "vo_vdpau: Can't get output surface bits for raw frame grabbing: %s\n", vdp_get_error_string (st));
        } else
          fprintf(stderr, "vo_vdpau: Can't render output surface for raw frame grabbing: %s\n", vdp_get_error_string (st));

        vdpau_free_output_surface(this, &frame->render_surface);
      }
    } else {
      st = vdp_output_surface_get_bits(grab_surface, &src_rect, &frame->rgba, &pitches);
      if (st != VDP_STATUS_OK)
        fprintf(stderr, "vo_vdpau: Can't get output surface bits for raw frame grabbing: %s\n", vdp_get_error_string (st));
    }

    if (st == VDP_STATUS_OK)
      frame->grab_frame.vpts = vpts;

    pthread_cond_broadcast(&this->grab_cond);
  }

  pthread_mutex_unlock(&this->grab_lock);
}


static void vdpau_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  vdpau_frame_t   *frame = (vdpau_frame_t *) frame_gen;
  VdpStatus st;
  VdpVideoSurface surface;
  VdpChromaType chroma = this->video_mixer_chroma;
  uint32_t mix_w = this->video_mixer_width;
  uint32_t mix_h = this->video_mixer_height;
  VdpTime stream_speed;

  if ( (frame->width != this->sc.delivered_width) ||
                  (frame->height != this->sc.delivered_height) ||
                  (frame->ratio != this->sc.delivered_ratio) ||
                  (frame->vo_frame.crop_left != this->sc.crop_left) ||
                  (frame->vo_frame.crop_right != this->sc.crop_right) ||
                  (frame->vo_frame.crop_top != this->sc.crop_top) ||
                  (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  this->sc.delivered_height = frame->height;
  this->sc.delivered_width  = frame->width;
  this->sc.delivered_ratio  = frame->ratio;
  this->sc.crop_left        = frame->vo_frame.crop_left;
  this->sc.crop_right       = frame->vo_frame.crop_right;
  this->sc.crop_top         = frame->vo_frame.crop_top;
  this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

  int redraw_needed = vdpau_redraw_needed( this_gen );

  pthread_mutex_lock(&this->drawable_lock); /* protect drawble from being changed */

  if(this->reinit_needed)
    vdpau_reinit(this_gen);

  if ( (frame->format == XINE_IMGFMT_YV12) || (frame->format == XINE_IMGFMT_YUY2) ) {
    chroma = ( frame->format==XINE_IMGFMT_YV12 )? VDP_CHROMA_TYPE_420 : VDP_CHROMA_TYPE_422;
    if ( (frame->width != this->soft_surface_width) || (frame->height != this->soft_surface_height) || (frame->format != this->soft_surface_format) ) {
      lprintf( "vo_vdpau: soft_surface size update\n" );
      /* recreate surface to match frame changes */
      this->soft_surface_width = frame->width;
      this->soft_surface_height = frame->height;
      this->soft_surface_format = frame->format;
      vdp_video_surface_destroy( this->soft_surface );
      this->soft_surface = VDP_INVALID_HANDLE;
      vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
    }
    /* FIXME: have to swap U and V planes to get correct colors !! */
    uint32_t pitches[] = { frame->vo_frame.pitches[0], frame->vo_frame.pitches[2], frame->vo_frame.pitches[1] };
    void* data[] = { frame->vo_frame.base[0], frame->vo_frame.base[2], frame->vo_frame.base[1] };
    if ( frame->format==XINE_IMGFMT_YV12 ) {
      st = vdp_video_surface_putbits_ycbcr( this->soft_surface, VDP_YCBCR_FORMAT_YV12, &data, pitches );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_surface_putbits_ycbcr YV12 error : %s\n", vdp_get_error_string( st ) );
    }
    else {
      st = vdp_video_surface_putbits_ycbcr( this->soft_surface, VDP_YCBCR_FORMAT_YUYV, &data, pitches );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_surface_putbits_ycbcr YUY2 error : %s\n", vdp_get_error_string( st ) );
    }
    surface = this->soft_surface;
    mix_w = this->soft_surface_width;
    mix_h = this->soft_surface_height;
  }
  else if (frame->format == XINE_IMGFMT_VDPAU) {
    surface = frame->vdpau_accel_data.surface;
    mix_w = frame->width;
    mix_h = frame->height;
    chroma = (frame->vo_frame.flags & VO_CHROMA_422) ? VDP_CHROMA_TYPE_422 : VDP_CHROMA_TYPE_420;
  }
  else {
    /* unknown format */
    fprintf(stderr, "vo_vdpau: got an unknown image -------------\n" );
    frame->vo_frame.free( &frame->vo_frame );
    pthread_mutex_unlock(&this->drawable_lock); /* allow changing drawable again */
    return;
  }

  if ( (mix_w != this->video_mixer_width) || (mix_h != this->video_mixer_height) || (chroma != this->video_mixer_chroma)) {
    vdpau_release_back_frames( this_gen ); /* empty past frames array */
    lprintf("vo_vdpau: recreate mixer to match frames: width=%d, height=%d, chroma=%d\n", mix_w, mix_h, chroma);
    vdp_video_mixer_destroy( this->video_mixer );
    this->video_mixer = VDP_INVALID_HANDLE;
    VdpVideoMixerFeature features[15];
    int features_count = 0;
    if ( this->noise_reduction_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
      ++features_count;
    }
    if ( this->sharpness_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
      ++features_count;
    }
    if ( this->temporal_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
      ++features_count;
    }
    if ( this->temporal_spatial_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
      ++features_count;
    }
    if ( this->inverse_telecine_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
      ++features_count;
    }
    int i;
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
    for ( i=0; i<this->scaling_level_max; ++i ) {
     features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
      ++features_count;
    }
#endif
    VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
          VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
    int num_layers = 1;
    void const *param_values[] = { &mix_w, &mix_h, &chroma, &num_layers };
    vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
    this->video_mixer_chroma = chroma;
    this->video_mixer_width = mix_w;
    this->video_mixer_height = mix_h;
    vdpau_set_deinterlace( this_gen );
    vdpau_set_scaling_level( this_gen );
    vdpau_set_inverse_telecine( this_gen );
    vdpau_update_noise( this );
    vdpau_update_sharpness( this );
    this->update_csc = 1;
    vdpau_update_skip_chroma( this );
    vdpau_update_background( this );
  }

  vdpau_update_csc_matrix (this, frame);

  if (this->ovl_changed || redraw_needed)
    vdpau_process_overlays(this);

  uint32_t layer_count;
  VdpLayer *layer, ovl_layer;
  VdpRect *vid_dest, vid_dest_rect;
  if (this->num_ovls && this->ovl_layer_surface != VDP_INVALID_HANDLE) {
    ovl_layer.struct_version = VDP_LAYER_VERSION;
    ovl_layer.source_surface = this->ovl_layer_surface;
    ovl_layer.source_rect = &this->ovl_src_rect;
    ovl_layer.destination_rect = &this->ovl_dest_rect;
    layer = &ovl_layer;
    layer_count = 1;
    vid_dest = &this->ovl_video_dest_rect;
  } else {
    layer = NULL;
    layer_count = 0;
    vid_dest_rect.x0 = this->sc.output_xoffset;
    vid_dest_rect.y0 = this->sc.output_yoffset;
    vid_dest_rect.x1 = this->sc.output_xoffset + this->sc.output_width;
    vid_dest_rect.y1 = this->sc.output_yoffset + this->sc.output_height;
    vid_dest = &vid_dest_rect;
  }

  VdpRect vid_source, out_dest;
  vid_source.x0 = this->sc.displayed_xoffset; vid_source.y0 = this->sc.displayed_yoffset;
  vid_source.x1 = this->sc.displayed_width+this->sc.displayed_xoffset; vid_source.y1 = this->sc.displayed_height+this->sc.displayed_yoffset;
  out_dest.x0 = out_dest.y0 = 0;
  out_dest.x1 = this->sc.gui_width; out_dest.y1 = this->sc.gui_height;

  stream_speed = frame->vo_frame.stream ? xine_get_param(frame->vo_frame.stream, XINE_PARAM_FINE_SPEED) : 0;

  /* try to get frame duration from previous img->pts when frame->duration is 0 */
  int frame_duration = frame->vo_frame.duration;
  if ( !frame_duration && this->back_frame[0] ) {
    int duration = frame->vo_frame.pts - this->back_frame[0]->vo_frame.pts;
    if ( duration>0 && duration<4000 )
      frame_duration = duration;
  }
  int non_progressive;
  if ( frame->vo_frame.progressive_frame < 0 )
    non_progressive = 0;
  else
    non_progressive = (this->honor_progressive && !frame->vo_frame.progressive_frame) || !this->honor_progressive;

  VdpTime last_time;

  if ( this->init_queue>1 )
    vdp_queue_block( vdp_queue, this->output_surface[this->current_output_surface], &last_time );

  DO_LOCKDISPLAY;

  vdpau_check_output_size( this_gen );

  if ( frame->format==XINE_IMGFMT_VDPAU && this->deinterlace && non_progressive && !(frame->vo_frame.flags & VO_STILL_IMAGE) && frame_duration>2500 ) {
    VdpTime current_time = 0;
    VdpVideoSurface past[2];
    VdpVideoSurface future[1];
    VdpVideoMixerPictureStructure picture_structure;

    past[1] = past[0] = (this->back_frame[0] && (this->back_frame[0]->format==XINE_IMGFMT_VDPAU)) ? this->back_frame[0]->vdpau_accel_data.surface : VDP_INVALID_HANDLE;
    future[0] = surface;
    picture_structure = ( frame->vo_frame.top_field_first ) ? VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD : VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD;

    st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, picture_structure,
                               2, past, surface, 1, future, &vid_source, this->output_surface[this->current_output_surface], &out_dest, vid_dest, layer_count, layer );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

    vdpau_grab_current_output_surface( this, frame->vo_frame.vpts );
    vdp_queue_get_time( vdp_queue, &current_time );
    vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], this->sc.gui_width, this->sc.gui_height, 0 ); /* display _now_ */
    vdpau_shift_queue( this_gen );

    int dm;
    if ( this->video_mixer_width < 800 )
      dm = this->deinterlacers_method[this->deinterlace_method_sd];
    else
      dm = this->deinterlacers_method[this->deinterlace_method_hd];
    
    if ( (dm != DEINT_HALF_TEMPORAL) && (dm != DEINT_HALF_TEMPORAL_SPATIAL) && frame->vo_frame.future_frame ) {  /* process second field */
      if ( this->init_queue >= this->queue_length ) {
        DO_UNLOCKDISPLAY;
        vdp_queue_block( vdp_queue, this->output_surface[this->current_output_surface], &last_time );
        DO_LOCKDISPLAY;
      }

      vdpau_check_output_size( this_gen );

      picture_structure = ( frame->vo_frame.top_field_first ) ? VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD : VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD;
      past[0] = surface;
      if ( frame->vo_frame.future_frame!=NULL && ((vdpau_frame_t*)(frame->vo_frame.future_frame))->format==XINE_IMGFMT_VDPAU )
        future[0] = ((vdpau_frame_t*)(frame->vo_frame.future_frame))->vdpau_accel_data.surface;
      else
        future[0] = VDP_INVALID_HANDLE;

      st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, picture_structure,
                               2, past, surface, 1, future, &vid_source, this->output_surface[this->current_output_surface], &out_dest, vid_dest, layer_count, layer );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

      if ( stream_speed > 0 )
        current_time += frame->vo_frame.duration * 1000000ull * XINE_FINE_SPEED_NORMAL / (180 * stream_speed);

      vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], this->sc.gui_width, this->sc.gui_height, current_time );
      vdpau_shift_queue( this_gen );
    }
  }
  else {
    if ( frame->vo_frame.flags & VO_STILL_IMAGE )
      lprintf( "vo_vdpau: VO_STILL_IMAGE\n");
    st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
                               0, 0, surface, 0, 0, &vid_source, this->output_surface[this->current_output_surface], &out_dest, vid_dest, layer_count, layer );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

    vdpau_grab_current_output_surface( this, frame->vo_frame.vpts );
    vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], this->sc.gui_width, this->sc.gui_height, 0 );
    vdpau_shift_queue( this_gen );
  }

  DO_UNLOCKDISPLAY;

  if ( stream_speed ) 
    vdpau_backup_frame( this_gen, frame_gen );
  else /* do not release past frame if paused, it will be used for redrawing */
    frame->vo_frame.free( &frame->vo_frame );

  pthread_mutex_unlock(&this->drawable_lock); /* allow changing drawable again */
}



static int vdpau_get_property (vo_driver_t *this_gen, int property)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  switch (property) {
    case VO_PROP_MAX_NUM_FRAMES:
      return 30;
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    case VO_PROP_OUTPUT_WIDTH:
      return this->sc.output_width;
    case VO_PROP_OUTPUT_HEIGHT:
      return this->sc.output_height;
    case VO_PROP_OUTPUT_XOFFSET:
      return this->sc.output_xoffset;
    case VO_PROP_OUTPUT_YOFFSET:
      return this->sc.output_yoffset;
    case VO_PROP_HUE:
      return this->hue;
    case VO_PROP_SATURATION:
      return this->saturation;
    case VO_PROP_CONTRAST:
      return this->contrast;
    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    case VO_PROP_SHARPNESS:
      return this->sharpness;
    case VO_PROP_NOISE_REDUCTION:
      return this->noise;
    case VO_PROP_ZOOM_X:
      return this->zoom_x;
    case VO_PROP_ZOOM_Y:
      return this->zoom_y;
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;
  }

  return -1;
}



static int vdpau_set_property (vo_driver_t *this_gen, int property, int value)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  fprintf(stderr,"vdpau_set_property: property=%d, value=%d\n", property, value );

  switch (property) {
    case VO_PROP_INTERLACED:
      this->deinterlace = value;
      vdpau_set_deinterlace( this_gen );
      break;
    case VO_PROP_DEINTERLACE_SD:
      this->deinterlace_method_sd = value;
      break;
    case VO_PROP_DEINTERLACE_HD:
      this->deinterlace_method_hd = value;
      break;
    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_x = value;
        this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_y = value;
        this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ASPECT_RATIO:
      if ( value>=XINE_VO_ASPECT_NUM_RATIOS )
        value = XINE_VO_ASPECT_AUTO;
      this->sc.user_ratio = value;
      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      break;
    case VO_PROP_HUE: this->hue = value; this->update_csc = 1; break;
    case VO_PROP_SATURATION: this->saturation = value; this->update_csc = 1; break;
    case VO_PROP_CONTRAST: this->contrast = value; this->update_csc = 1; break;
    case VO_PROP_BRIGHTNESS: this->brightness = value; this->update_csc = 1; break;
    case VO_PROP_SHARPNESS: this->sharpness = value; vdpau_update_sharpness( this ); break;
    case VO_PROP_NOISE_REDUCTION: this->noise = value; vdpau_update_noise( this ); break;
  }

  return value;
}



static void vdpau_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max)
{
  switch ( property ) {
    case VO_PROP_HUE:
      *max = 127; *min = -128; break;
    case VO_PROP_SATURATION:
      *max = 255; *min = 0; break;
    case VO_PROP_CONTRAST:
      *max = 255; *min = 0; break;
    case VO_PROP_BRIGHTNESS:
      *max = 127; *min = -128; break;
    case VO_PROP_SHARPNESS:
      *max = 100; *min = -100; break;
    case VO_PROP_NOISE_REDUCTION:
      *max = 100; *min = 0; break;
    default:
      *max = 0; *min = 0;
  }
}


/*
 * functions for grabbing RGB images from displayed frames
 */
static void vdpau_dispose_grab_video_frame(xine_grab_video_frame_t *frame_gen)
{
  vdpau_grab_video_frame_t *frame = (vdpau_grab_video_frame_t *) frame_gen;

  free(frame->grab_frame.img);
  free(frame->rgba);
  free(frame);
}


/*
 * grab next displayed output surface.
 * Note: This feature only supports grabbing of next displayed frame (implicit VO_GRAB_FRAME_FLAGS_WAIT_NEXT)
 */
static int vdpau_grab_grab_video_frame (xine_grab_video_frame_t *frame_gen) {
  vdpau_grab_video_frame_t *frame = (vdpau_grab_video_frame_t *) frame_gen;
  vdpau_driver_t *this = (vdpau_driver_t *) frame->vo_driver;
  struct timeval tvnow, tvdiff, tvtimeout;
  struct timespec ts;

  /* calculate absolute timeout time */
  tvdiff.tv_sec = frame->grab_frame.timeout / 1000;
  tvdiff.tv_usec = frame->grab_frame.timeout % 1000;
  tvdiff.tv_usec *= 1000;
  gettimeofday(&tvnow, NULL);
  timeradd(&tvnow, &tvdiff, &tvtimeout);
  ts.tv_sec  = tvtimeout.tv_sec;
  ts.tv_nsec = tvtimeout.tv_usec;
  ts.tv_nsec *= 1000;

  pthread_mutex_lock(&this->grab_lock);

  /* wait until other pending grab request is finished */
  while (this->pending_grab_request) {
    if (pthread_cond_timedwait(&this->grab_cond, &this->grab_lock, &ts) == ETIMEDOUT) {
      pthread_mutex_unlock(&this->grab_lock);
      return 1;   /* no frame available */
    }
  }

  this->pending_grab_request = frame;

  /* wait until our request is finished */
  while (this->pending_grab_request) {
    if (pthread_cond_timedwait(&this->grab_cond, &this->grab_lock, &ts) == ETIMEDOUT) {
      this->pending_grab_request = NULL;
      pthread_mutex_unlock(&this->grab_lock);
      return 1;   /* no frame available */
    }
  }

  pthread_mutex_unlock(&this->grab_lock);

  if (frame->grab_frame.vpts == -1)
    return -1; /* error happened */

  /* convert ARGB image to RGB image */
  uint32_t *src = frame->rgba;
  uint8_t *dst = frame->grab_frame.img;
  int n = frame->width * frame->height;
  while (n--) {
    uint32_t rgba = *src++;
    *dst++ = (uint8_t)(rgba >> 16);  /*R*/
    *dst++ = (uint8_t)(rgba >> 8);   /*G*/
    *dst++ = (uint8_t)(rgba);        /*B*/
  }

  return 0;
}


static xine_grab_video_frame_t * vdpau_new_grab_video_frame(vo_driver_t *this)
{
  vdpau_grab_video_frame_t *frame = calloc(1, sizeof(vdpau_grab_video_frame_t));
  if (frame) {
    frame->grab_frame.dispose = vdpau_dispose_grab_video_frame;
    frame->grab_frame.grab = vdpau_grab_grab_video_frame;
    frame->grab_frame.vpts = -1;
    frame->grab_frame.timeout = XINE_GRAB_VIDEO_FRAME_DEFAULT_TIMEOUT;
    frame->vo_driver = this;
    frame->render_surface.surface = VDP_INVALID_HANDLE;
  }

  return (xine_grab_video_frame_t *) frame;
}


static int vdpau_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
    case XINE_GUI_SEND_COMPLETION_EVENT:
      break;
#endif

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      if ( this->init_queue ) {
        pthread_mutex_lock(&this->drawable_lock); /* wait for other thread which is currently displaying */
        DO_LOCKDISPLAY;
        int previous;
        if ( this->current_output_surface )
          previous = this->current_output_surface - 1;
        else
          previous = this->queue_length - 1;
        vdp_queue_display( vdp_queue, this->output_surface[previous], 0, 0, 0 );
        DO_UNLOCKDISPLAY;
        pthread_mutex_unlock(&this->drawable_lock);
      }
      break;
    }

    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      VdpStatus st;
      pthread_mutex_lock(&this->drawable_lock); /* wait for other thread which is currently displaying */
      DO_LOCKDISPLAY;
      this->drawable = (Drawable) data;
      vdp_queue_destroy( vdp_queue );
      vdp_queue_target_destroy( vdp_queue_target );
      st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
      if ( st != VDP_STATUS_OK ) {
        fprintf(stderr, "vo_vdpau: FATAL !! Can't recreate presentation queue target after drawable change !!\n" );
        DO_UNLOCKDISPLAY;
        pthread_mutex_unlock(&this->drawable_lock);
        break;
      }
      st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
      if ( st != VDP_STATUS_OK ) {
        fprintf(stderr, "vo_vdpau: FATAL !! Can't recreate presentation queue after drawable change !!\n" );
        DO_UNLOCKDISPLAY;
        pthread_mutex_unlock(&this->drawable_lock);
        break;
      }
      vdp_queue_set_background_color( vdp_queue, &this->back_color );
      DO_UNLOCKDISPLAY;
      pthread_mutex_unlock(&this->drawable_lock);
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
      break;
    }

    default:
      return -1;
  }

  return 0;
}



static uint32_t vdpau_get_capabilities (vo_driver_t *this_gen)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;

  return this->capabilities;
}



static void vdpau_dispose (vo_driver_t *this_gen)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;
  int i;

  cm_close (this);

  if ( vdp_queue != VDP_INVALID_HANDLE )
    vdp_queue_destroy( vdp_queue );

  if ( vdp_queue_target != VDP_INVALID_HANDLE )
    vdp_queue_target_destroy( vdp_queue_target );

  if ( this->video_mixer!=VDP_INVALID_HANDLE )
    vdp_video_mixer_destroy( this->video_mixer );

  if ( this->soft_surface != VDP_INVALID_HANDLE )
    vdp_video_surface_destroy( this->soft_surface );

  if ( vdp_output_surface_destroy ) {
    if (this->ovl_main_render_surface.surface != VDP_INVALID_HANDLE)
      vdp_output_surface_destroy( this->ovl_main_render_surface.surface );
    for (i = 0; i < this->num_ovls; ++i) {
      vdpau_overlay_t *ovl = &this->overlays[i];
      if (ovl->render_surface.surface != VDP_INVALID_HANDLE)
        vdp_output_surface_destroy( ovl->render_surface.surface );
    }
    for ( i=0; i<this->queue_length; ++i ) {
      if ( this->output_surface[i] != VDP_INVALID_HANDLE )
        vdp_output_surface_destroy( this->output_surface[i] );
    }
    for ( i=0; i<this->output_surface_buffer_size; ++i ) {
      if ( this->output_surface_buffer[i].surface != VDP_INVALID_HANDLE )
        vdp_output_surface_destroy( this->output_surface_buffer[i].surface );
    }
  }

  for ( i=0; i<NUM_FRAMES_BACK; i++ )
    if ( this->back_frame[i] )
      this->back_frame[i]->vo_frame.dispose( &this->back_frame[i]->vo_frame );

  if ( (vdp_device != VDP_INVALID_HANDLE) && vdp_device_destroy )
    vdp_device_destroy( vdp_device );

  pthread_mutex_destroy(&this->grab_lock);
  pthread_cond_destroy(&this->grab_cond);
  pthread_mutex_destroy(&this->drawable_lock);
  free(this->ovl_pixmap);
  free (this);
}



static void vdpau_update_display_dimension (vdpau_driver_t *this)
{
  XLockDisplay (this->display);

  this->display_width  = DisplayWidth(this->display, this->screen);
  this->display_height = DisplayHeight(this->display, this->screen);

  XUnlockDisplay(this->display);
}



static int vdpau_reinit_error( VdpStatus st, const char *msg )
{
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: %s : %s\n", msg, vdp_get_error_string( st ) );
    return 1;
  }
  return 0;
}



static void vdpau_reinit( vo_driver_t *this_gen )
{
  fprintf(stderr,"vo_vdpau: VDPAU was pre-empted. Reinit.\n");
  vdpau_driver_t *this = (vdpau_driver_t *)this_gen;

  DO_LOCKDISPLAY;
  vdpau_release_back_frames(this_gen);

  VdpStatus st = vdp_device_create_x11( this->display, this->screen, &vdp_device, &vdp_get_proc_address );

  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: Can't create vdp device : " );
    if ( st == VDP_STATUS_NO_IMPLEMENTATION )
      fprintf(stderr, "No vdpau implementation.\n" );
    else
      fprintf(stderr, "unsupported GPU?\n" );
    DO_UNLOCKDISPLAY;
    return;
  }

  st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
  if ( vdpau_reinit_error( st, "Can't create presentation queue target !!" ) ) {
    DO_UNLOCKDISPLAY;
    return;
  }
  st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
  if ( vdpau_reinit_error( st, "Can't create presentation queue !!" ) ) {
    DO_UNLOCKDISPLAY;
    return;
  }
  vdp_queue_set_background_color( vdp_queue, &this->back_color );


  VdpChromaType chroma = VDP_CHROMA_TYPE_420;
  st = orig_vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
  if ( vdpau_reinit_error( st, "Can't create video surface !!" ) ) {
    DO_UNLOCKDISPLAY;
    return;
  }

  vdpau_update_display_dimension(this);
  this->current_output_surface = 0;
  this->init_queue = 0;
  int i;
  for ( i=0; i<this->queue_length; ++i ) {
    this->output_surface_width[i] = this->display_width;
    this->output_surface_height[i] = this->display_height;
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[i], this->output_surface_height[i], &this->output_surface[i] );
    if ( vdpau_reinit_error( st, "Can't create output surface !!" ) ) {
      int j;
      for ( j=0; j<i; ++j )
        vdp_output_surface_destroy( this->output_surface[j] );
      vdp_video_surface_destroy( this->soft_surface );
      DO_UNLOCKDISPLAY;
      return;
    }
  }

  this->num_big_output_surfaces_created = 0;
  for (i = 0; i < this->output_surface_buffer_size; ++i)
    this->output_surface_buffer[i].surface = VDP_INVALID_HANDLE;

  this->ovl_layer_surface = VDP_INVALID_HANDLE;
  this->ovl_main_render_surface.surface = VDP_INVALID_HANDLE;
  for (i = 0; i < this->num_ovls; ++i)
    this->overlays[i].render_surface.surface = VDP_INVALID_HANDLE;
  this->num_ovls = 0;
  this->ovl_changed = 1;

  VdpVideoMixerFeature features[15];
  int features_count = 0;
  if ( this->noise_reduction_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
    ++features_count;
  }
  if ( this->sharpness_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
	++features_count;
  }
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }
  if ( this->inverse_telecine_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
    ++features_count;
  }
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    ++features_count;
  }
#endif
  VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT, VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
  int num_layers = 1;
  void const *param_values[] = { &this->video_mixer_width, &this->video_mixer_height, &chroma, &num_layers };
  st = vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
  if ( vdpau_reinit_error( st, "Can't create video mixer !!" ) ) {
    orig_vdp_video_surface_destroy( this->soft_surface );
    for ( i=0; i<this->queue_length; ++i )
      vdp_output_surface_destroy( this->output_surface[i] );
    DO_UNLOCKDISPLAY;
    return;
  }
  this->video_mixer_chroma = chroma;
  vdpau_set_deinterlace( this_gen );
  vdpau_set_scaling_level( this_gen );
  vdpau_set_inverse_telecine( this_gen );
  vdpau_update_noise( this );
  vdpau_update_sharpness( this );
  this->update_csc = 1;
  vdpau_update_skip_chroma( this );
  vdpau_update_background( this );

  vdp_preemption_callback_register(vdp_device, &vdp_preemption_callback, (void*)this);

  this->vdp_runtime_nr++;
  this->reinit_needed = 0;
  DO_UNLOCKDISPLAY;
  fprintf(stderr,"vo_vdpau: Reinit done.\n");
}



static void vdp_preemption_callback(VdpDevice device, void *context)
{
  fprintf(stderr,"vo_vdpau: VDPAU preemption callback\n");
  vdpau_driver_t *this = (vdpau_driver_t *)context;
  this->reinit_needed = 1;
}



static int vdpau_init_error( VdpStatus st, const char *msg, vo_driver_t *driver, int error_string )
{
  if ( st != VDP_STATUS_OK ) {
    if ( error_string )
      fprintf(stderr, "vo_vdpau: %s : %s\n", msg, vdp_get_error_string( st ) );
    else
      fprintf(stderr, "vo_vdpau: %s\n", msg );
    vdpau_dispose( driver );
    return 1;
  }
  return 0;
}



static vo_driver_t *vdpau_open_plugin (video_driver_class_t *class_gen, const void *visual_gen)
{
  vdpau_class_t       *class   = (vdpau_class_t *) class_gen;
  x11_visual_t        *visual  = (x11_visual_t *) visual_gen;
  vdpau_driver_t      *this;
  config_values_t      *config  = class->xine->config;
  int i;

  this = (vdpau_driver_t *) calloc(1, sizeof(vdpau_driver_t));

  if (!this)
    return NULL;

#ifdef LOCKDISPLAY
  int buggy_xcb_workaround = config->register_bool( config, "video.output.vdpau_enable_buggy_xcb_workaround", 1,
    _("vdpau: Use lock display synchronization for some vdpau calls (workaround for buggy libX11/xcb)"),
    _("Enable this if you have a buggy libX11/xcb."),
      10, NULL, this );
  guarded_display     = buggy_xcb_workaround ? visual->display: NULL;
  fprintf( stderr, "vo_vdpau: %s lock display synchronization for some vdpau calls\n", buggy_xcb_workaround ? "Use": "Do not use" );
#endif

  this->display       = visual->display;
  this->screen        = visual->screen;
  this->drawable      = visual->d;
  pthread_mutex_init(&this->drawable_lock, 0);

  _x_vo_scale_init(&this->sc, 1, 0, config);
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  this->zoom_x              = 100;
  this->zoom_y              = 100;

  this->xine                    = class->xine;
  this->config                  = config;

  this->vo_driver.get_capabilities     = vdpau_get_capabilities;
  this->vo_driver.alloc_frame          = vdpau_alloc_frame;
  this->vo_driver.update_frame_format  = vdpau_update_frame_format;
  this->vo_driver.overlay_begin        = vdpau_overlay_begin;
  this->vo_driver.overlay_blend        = vdpau_overlay_blend;
  this->vo_driver.overlay_end          = vdpau_overlay_end;
  this->vo_driver.display_frame        = vdpau_display_frame;
  this->vo_driver.get_property         = vdpau_get_property;
  this->vo_driver.set_property         = vdpau_set_property;
  this->vo_driver.get_property_min_max = vdpau_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vdpau_gui_data_exchange;
  this->vo_driver.dispose              = vdpau_dispose;
  this->vo_driver.redraw_needed        = vdpau_redraw_needed;
  this->vo_driver.new_grab_video_frame = vdpau_new_grab_video_frame;

  this->surface_cleared_nr = 0;

  this->video_mixer = VDP_INVALID_HANDLE;
  for ( i=0; i<NOUTPUTSURFACE; ++i )
    this->output_surface[i] = VDP_INVALID_HANDLE;
  this->soft_surface = VDP_INVALID_HANDLE;
  vdp_queue = VDP_INVALID_HANDLE;
  vdp_queue_target = VDP_INVALID_HANDLE;
  vdp_device = VDP_INVALID_HANDLE;

  vdp_output_surface_destroy = NULL;
  vdp_device_destroy = NULL;

  this->sharpness_is_supported = 0;
  this->noise_reduction_is_supported = 0;
  this->temporal_is_supported = 0;
  this->temporal_spatial_is_supported = 0;
  this->inverse_telecine_is_supported = 0;
  this->skip_chroma_is_supported = 0;
  this->background_is_supported = 0;

  this->ovl_changed = 0;
  this->num_ovls = 0;
  this->old_num_ovls = 0;
  this->ovl_layer_surface = VDP_INVALID_HANDLE;
  this->ovl_main_render_surface.surface = VDP_INVALID_HANDLE;
  this->ovl_pixmap = NULL;
  this->ovl_pixmap_size = 0;
  this->ovl_src_rect.x0 = 0;
  this->ovl_src_rect.y0 = 0;

  VdpStatus st = vdp_device_create_x11( visual->display, visual->screen, &vdp_device, &vdp_get_proc_address );
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: Can't create vdp device : " );
    if ( st == VDP_STATUS_NO_IMPLEMENTATION )
      fprintf(stderr, "No vdpau implementation.\n" );
    else
      fprintf(stderr, "unsupported GPU?\n" );
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_ERROR_STRING , (void*)&vdp_get_error_string );
  if ( vdpau_init_error( st, "Can't get GET_ERROR_STRING proc address !!", &this->vo_driver, 0 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_API_VERSION , (void*)&vdp_get_api_version );
  if ( vdpau_init_error( st, "Can't get GET_API_VERSION proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  uint32_t tmp;
  vdp_get_api_version( &tmp );
  fprintf(stderr, "vo_vdpau: vdpau API version : %d\n", tmp );
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_INFORMATION_STRING , (void*)&vdp_get_information_string );
  if ( vdpau_init_error( st, "Can't get GET_INFORMATION_STRING proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  const char *s;
  st = vdp_get_information_string( &s );
  fprintf(stderr, "vo_vdpau: vdpau implementation description : %s\n", s );
  VdpBool ok;
  uint32_t max_surface_width, max_surface_height;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES , (void*)&vdp_video_surface_query_capabilities );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_QUERY_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_video_surface_query_capabilities( vdp_device, VDP_CHROMA_TYPE_422, &ok, &max_surface_width, &max_surface_height );
  if ( vdpau_init_error( st, "Failed to check vdpau chroma type 4:2:2 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support chroma type 4:2:2, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  fprintf(stderr, "vo_vdpau: maximum video surface size for chroma type 4:2:2 is %dx%d\n", (int)max_surface_width, (int)max_surface_height );
  st = vdp_video_surface_query_capabilities( vdp_device, VDP_CHROMA_TYPE_420, &ok, &max_surface_width, &max_surface_height );
  if ( vdpau_init_error( st, "Failed to check vdpau chroma type 4:2:0 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support chroma type 4:2:0, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  fprintf(stderr, "vo_vdpau: maximum video surface size for chroma type 4:2:0 is %dx%d\n", (int)max_surface_width, (int)max_surface_height );
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES , (void*)&vdp_video_surface_query_get_put_bits_ycbcr_capabilities );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_video_surface_query_get_put_bits_ycbcr_capabilities( vdp_device, VDP_CHROMA_TYPE_422, VDP_YCBCR_FORMAT_YUYV, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau yuy2 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support yuy2, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_video_surface_query_get_put_bits_ycbcr_capabilities( vdp_device, VDP_CHROMA_TYPE_420, VDP_YCBCR_FORMAT_YV12, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau yv12 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support yv12, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES , (void*)&vdp_output_surface_query_capabilities );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_QUERY_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_output_surface_query_capabilities( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, &ok, &max_surface_width, &max_surface_height );
  if ( vdpau_init_error( st, "Failed to check vdpau rgba capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: OutputSurface doesn't support rgba, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  fprintf(stderr, "vo_vdpau: maximum output surface size is %dx%d\n", (int)max_surface_width, (int)max_surface_height );
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_GET_PUT_BITS_NATIVE_CAPABILITIES , (void*)&vdp_output_surface_query_get_put_bits_native_capabilities );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_QUERY_GET_PUT_BITS_NATIVE_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_output_surface_query_get_put_bits_native_capabilities( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau get/put bits native capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: OutputSurface doesn't support get/put bits native, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_Y_CB_CR_CAPABILITIES , (void*)&vdp_output_surface_query_put_bits_ycbcr_capabilities );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_QUERY_PUT_BITS_Y_CB_CR_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_output_surface_query_put_bits_ycbcr_capabilities( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, VDP_YCBCR_FORMAT_V8U8Y8A8, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau put bits ycbcr capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: OutputSurface doesn't support put bits ycbcr, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DEVICE_DESTROY , (void*)&vdp_device_destroy );
  if ( vdpau_init_error( st, "Can't get DEVICE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_CREATE , (void*)&orig_vdp_video_surface_create ); vdp_video_surface_create = guarded_vdp_video_surface_create;
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_DESTROY , (void*)&orig_vdp_video_surface_destroy ); vdp_video_surface_destroy = guarded_vdp_video_surface_destroy;
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR , (void*)&orig_vdp_video_surface_putbits_ycbcr ); vdp_video_surface_putbits_ycbcr = guarded_vdp_video_surface_putbits_ycbcr;
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_PUT_BITS_Y_CB_CR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR , (void*)&vdp_video_surface_getbits_ycbcr );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_GET_BITS_Y_CB_CR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS , (void*)&vdp_video_surface_get_parameters );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_GET_PARAMETERS proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_CREATE , (void*)&orig_vdp_output_surface_create ); vdp_output_surface_create = guarded_vdp_output_surface_create;
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY , (void*)&orig_vdp_output_surface_destroy ); vdp_output_surface_destroy = guarded_vdp_output_surface_destroy;
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE , (void*)&vdp_output_surface_render_output_surface );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE , (void*)&vdp_output_surface_put_bits );
  if ( vdpau_init_error( st, "Can't get VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_Y_CB_CR , (void*)&vdp_output_surface_put_bits_ycbcr );
  if ( vdpau_init_error( st, "Can't get VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_Y_CB_CR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE , (void*)&vdp_output_surface_get_bits );
  if ( vdpau_init_error( st, "Can't get VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_CREATE , (void*)&vdp_video_mixer_create );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_DESTROY , (void*)&vdp_video_mixer_destroy );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_RENDER , (void*)&vdp_video_mixer_render );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_RENDER proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES , (void*)&vdp_video_mixer_set_attribute_values );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_SET_ATTRIBUTE_VALUES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES , (void*)&vdp_video_mixer_set_feature_enables );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_SET_FEATURE_ENABLES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES , (void*)&vdp_video_mixer_get_feature_enables );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_GET_FEATURE_ENABLES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT , (void*)&vdp_video_mixer_query_feature_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_FEATURE_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_SUPPORT , (void*)&vdp_video_mixer_query_parameter_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_PARAMETER_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT , (void*)&vdp_video_mixer_query_attribute_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE , (void*)&vdp_video_mixer_query_parameter_value_range );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE , (void*)&vdp_video_mixer_query_attribute_value_range );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GENERATE_CSC_MATRIX , (void*)&vdp_generate_csc_matrix );
  if ( vdpau_init_error( st, "Can't get GENERATE_CSC_MATRIX proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11 , (void*)&vdp_queue_target_create_x11 );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_TARGET_CREATE_X11 proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY , (void*)&vdp_queue_target_destroy );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_TARGET_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE , (void*)&vdp_queue_create );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY , (void*)&vdp_queue_destroy );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY , (void*)&vdp_queue_display );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_DISPLAY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE , (void*)&vdp_queue_block );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR , (void*)&vdp_queue_set_background_color );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_SET_BACKGROUND_COLOR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME , (void*)&vdp_queue_get_time );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_GET_TIME proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS , (void*)&vdp_queue_query_surface_status );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_QUERY_SURFACE_STATUS proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES , (void*)&vdp_decoder_query_capabilities );
  if ( vdpau_init_error( st, "Can't get DECODER_QUERY_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_CREATE , (void*)&orig_vdp_decoder_create ); vdp_decoder_create = guarded_vdp_decoder_create;
  if ( vdpau_init_error( st, "Can't get DECODER_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_DESTROY , (void*)&orig_vdp_decoder_destroy ); vdp_decoder_destroy = guarded_vdp_decoder_destroy;
  if ( vdpau_init_error( st, "Can't get DECODER_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_RENDER , (void*)&orig_vdp_decoder_render ); vdp_decoder_render = guarded_vdp_decoder_render;
  if ( vdpau_init_error( st, "Can't get DECODER_RENDER proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER, (void*)&vdp_preemption_callback_register );
  if ( vdpau_init_error( st, "Can't get PREEMPTION_CALLBACK_REGISTER proc address !!", &this->vo_driver, 1 ) )
    return NULL;

  st = vdp_preemption_callback_register(vdp_device, &vdp_preemption_callback, (void*)this);
  if ( vdpau_init_error( st, "Can't register preemption callback !!", &this->vo_driver, 1 ) )
    return NULL;

  st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
  if ( vdpau_init_error( st, "Can't create presentation queue target !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
  if ( vdpau_init_error( st, "Can't create presentation queue !!", &this->vo_driver, 1 ) )
    return NULL;

  /* choose almost black as backcolor for color keying */
  this->back_color.red = 0.02;
  this->back_color.green = 0.01;
  this->back_color.blue = 0.03;
  this->back_color.alpha = 1;
  vdp_queue_set_background_color( vdp_queue, &this->back_color );

  this->soft_surface_width = 320;
  this->soft_surface_height = 240;
  this->soft_surface_format = XINE_IMGFMT_YV12;
  VdpChromaType chroma = VDP_CHROMA_TYPE_420;
  st = vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
  if ( vdpau_init_error( st, "Can't create video surface !!", &this->vo_driver, 1 ) )
    return NULL;

  this->output_surface_buffer_size = config->register_num (config, "video.output.vdpau_output_surface_buffer_size", 10, /* default */
       _("maximum number of output surfaces buffered for reuse"),
       _("The maximum number of video output surfaces buffered for reuse"),
      20, NULL, this);
  if (this->output_surface_buffer_size < 2)
    this->output_surface_buffer_size = 2;
  if (this->output_surface_buffer_size > NOUTPUTSURFACEBUFFER)
    this->output_surface_buffer_size = NOUTPUTSURFACEBUFFER;
  fprintf(stderr, "vo_vdpau: hold a maximum of %d video output surfaces for reuse\n", this->output_surface_buffer_size);

  this->num_big_output_surfaces_created = 0;
  for (i = 0; i < this->output_surface_buffer_size; ++i)
    this->output_surface_buffer[i].surface = VDP_INVALID_HANDLE;

  vdpau_update_display_dimension(this);

  this->queue_length = config->register_num (config, "video.output.vdpau_display_queue_length", 3, /* default */
       _("default length of display queue"),
       _("The default number of video output surfaces to create for the display queue"),
      20, NULL, this);
  if (this->queue_length < 2)
    this->queue_length = 2;
  if (this->queue_length > NOUTPUTSURFACE)
    this->queue_length = NOUTPUTSURFACE;
  fprintf(stderr, "vo_vdpau: using %d output surfaces of size %dx%d for display queue\n", this->queue_length, this->display_width, this->display_height);

  this->current_output_surface = 0;
  this->init_queue = 0;
  for ( i=0; i<this->queue_length; ++i ) {
    this->output_surface_width[i] = this->display_width;
    this->output_surface_height[i] = this->display_height;
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[i], this->output_surface_height[i], &this->output_surface[i] );
    if ( vdpau_init_error( st, "Can't create output surface !!", &this->vo_driver, 1 ) ) {
      int j;
      for ( j=0; j<i; ++j )
        vdp_output_surface_destroy( this->output_surface[j] );
      vdp_video_surface_destroy( this->soft_surface );
      return NULL;
    }
  }

  this->scaling_level_max = this->scaling_level_current = 0;
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  VdpBool hqscaling;
  for ( i=0; i<9; ++i ) {
    st = vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i, &hqscaling );
    if ( ( st != VDP_STATUS_OK ) || !hqscaling ) {
      /*printf("unsupported scaling quality=%d\n", i);*/
      break;
    }
    else {
      /*printf("supported scaling quality=%d\n", i);*/
      ++this->scaling_level_max;
    }
  }
#endif

  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL, &this->temporal_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL, &this->temporal_spatial_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION, &this->noise_reduction_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_SHARPNESS, &this->sharpness_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE, &this->inverse_telecine_is_supported );
  vdp_video_mixer_query_attribute_support( vdp_device, VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE, &this->skip_chroma_is_supported );
  vdp_video_mixer_query_attribute_support( vdp_device, VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR, &this->background_is_supported );

  this->video_mixer_chroma = chroma;
  this->video_mixer_width = this->soft_surface_width;
  this->video_mixer_height = this->soft_surface_height;
  VdpVideoMixerFeature features[15];
  int features_count = 0;
  if ( this->noise_reduction_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
    ++features_count;
  }
  if ( this->sharpness_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
    ++features_count;
  }
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }
  if ( this->inverse_telecine_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
    ++features_count;
  }
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    ++features_count;
  }
#endif
  VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
        VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
  int num_layers = 1;
  void const *param_values[] = { &this->video_mixer_width, &this->video_mixer_height, &chroma, &num_layers };
  st = vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
  if ( vdpau_init_error( st, "Can't create video mixer !!", &this->vo_driver, 1 ) ) {
    vdp_video_surface_destroy( this->soft_surface );
    for ( i=0; i<this->queue_length; ++i )
      vdp_output_surface_destroy( this->output_surface[i] );
    return NULL;
  }

  char deinterlacers_description[1024];
  memset( deinterlacers_description, 0, 1024 );
  int deint_count = 0;
  int deint_default = 0;
  this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[0];
  this->deinterlacers_method[deint_count] = DEINT_BOB;
  strcat( deinterlacers_description, vdpau_deinterlacer_description[0] );
  ++deint_count;
  if ( this->temporal_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[1];
    this->deinterlacers_method[deint_count] = DEINT_HALF_TEMPORAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[1] );
    ++deint_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[2];
    this->deinterlacers_method[deint_count] = DEINT_HALF_TEMPORAL_SPATIAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[2] );
    ++deint_count;
  }
  if ( this->temporal_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[3];
    this->deinterlacers_method[deint_count] = DEINT_TEMPORAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[3] );
    deint_default = deint_count;
    ++deint_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[4];
    this->deinterlacers_method[deint_count] = DEINT_TEMPORAL_SPATIAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[4] );
    ++deint_count;
  }
  this->deinterlacers_name[deint_count] = NULL;

  if ( this->scaling_level_max ) {
    this->scaling_level_current = config->register_range( config, "video.output.vdpau_scaling_quality", 0,
           0, this->scaling_level_max, _("vdpau: Scaling Quality"),
           _("Scaling Quality Level"),
           10, vdpau_update_scaling_level, this );
  }

  this->deinterlace_method_hd = config->register_enum( config, "video.output.vdpau_hd_deinterlace_method", deint_default,
         this->deinterlacers_name, _("vdpau: HD deinterlace method"),
         deinterlacers_description,
         10, vdpau_update_deinterlace_method_hd, this );

  this->deinterlace_method_sd = config->register_enum( config, "video.output.vdpau_sd_deinterlace_method", deint_default,
         this->deinterlacers_name, _("vdpau: SD deinterlace method"),
         deinterlacers_description,
         10, vdpau_update_deinterlace_method_sd, this );

  if ( this->inverse_telecine_is_supported ) {
    this->enable_inverse_telecine = config->register_bool( config, "video.output.vdpau_enable_inverse_telecine", 1,
      _("vdpau: Try to recreate progressive frames from pulldown material"),
      _("Enable this to detect bad-flagged progressive content to which\n"
        "a 2:2 or 3:2 pulldown was applied.\n\n"),
        10, vdpau_update_enable_inverse_telecine, this );
  }

  this->honor_progressive = config->register_bool( config, "video.output.vdpau_honor_progressive", 0,
        _("vdpau: disable deinterlacing when progressive_frame flag is set"),
        _("Set to true if you want to trust the progressive_frame stream's flag.\n"
          "This flag is not always reliable.\n\n"),
        10, vdpau_honor_progressive_flag, this );

  if ( this->skip_chroma_is_supported ) {
    this->skip_chroma = config->register_bool( config, "video.output.vdpau_skip_chroma_deinterlace", 0,
        _("vdpau: disable advanced deinterlacers chroma filter"),
        _("Setting to true may help if your video card isn't able to run advanced deinterlacers.\n\n"),
        10, vdpau_set_skip_chroma, this );
  }

  if ( this->background_is_supported ) {
    this->background = config->register_num( config, "video.output.vdpau_background_color", 0,
        _("vdpau: color of none video area in output window"),
        _("Displaying 4:3 images on 16:9 plasma TV sets lets the inactive pixels outside the video age slower than the pixels in the active area. Setting a different background color (e. g. 8421504) makes all pixels age similarly. The number to enter for a certain color can be derived from its 6 digit hexadecimal RGB value.\n\n"),
        10, vdpau_set_background, this );
  }

  this->sd_only_properties = config->register_enum( config, "video.output.vdpau_sd_only_properties", 0, vdpau_sd_only_properties,
        _("vdpau: restrict enabling video properties for SD video only"),
        _("none\n"
          "No restrictions\n\n"
          "noise\n"
          "Restrict noise reduction property.\n\n"
          "sharpness\n"
          "Restrict sharpness property.\n\n"
          "noise+sharpness"
          "Restrict noise and sharpness properties.\n\n"),
        10, vdpau_update_sd_only_properties, this );

  /* number of video frames from config - register it with the default value. */
  int frame_num = config->register_num (config, "engine.buffers.video_num_frames", 15, /* default */
       _("default number of video frames"),
       _("The default number of video frames to request "
         "from xine video out driver. Some drivers will "
         "override this setting with their own values."),
      20, NULL, this);

  /* now make sure we have at least 22 frames, to prevent
   * locks with vdpau_h264 */
  if(frame_num < 22)
    config->update_num(config,"engine.buffers.video_num_frames",22);

  this->capabilities = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP | VO_CAP_UNSCALED_OVERLAY | VO_CAP_CUSTOM_EXTENT_OVERLAY | VO_CAP_ARGB_LAYER_OVERLAY | VO_CAP_VIDEO_WINDOW_OVERLAY;

  this->capabilities |= VO_CAP_HUE;
  this->capabilities |= VO_CAP_SATURATION;
  this->capabilities |= VO_CAP_CONTRAST;
  this->capabilities |= VO_CAP_BRIGHTNESS;
  if (this->sharpness_is_supported)
    this->capabilities |= VO_CAP_SHARPNESS;
  if (this->noise_reduction_is_supported)
    this->capabilities |= VO_CAP_NOISE_REDUCTION;

  ok = 0;
  uint32_t mw, mh, ml, mr;
  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_H264_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting h264_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support h264.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_H264;

  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_VC1_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting vc1_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support vc1.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_VC1;

  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_MPEG2_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting mpeg12_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support mpeg1/2.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_MPEG12;

#ifdef VDP_DECODER_PROFILE_MPEG4_PART2_ASP
  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_MPEG4_PART2_ASP, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting mpeg4-part2_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support mpeg4-part2.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_MPEG4;
#endif

  for ( i=0; i<NUM_FRAMES_BACK; i++)
    this->back_frame[i] = NULL;

  this->capabilities |= VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE;

  this->hue = 0;
  this->saturation = 128;
  this->contrast = 128;
  this->brightness = 0;
  this->sharpness = 0;
  this->noise = 0;
  this->deinterlace = 0;

  this->update_csc = 1;
  this->color_matrix = 10;
  cm_init (this);

  this->allocated_surfaces = 0;

  this->vdp_runtime_nr = 1;

  this->pending_grab_request = NULL;
  pthread_mutex_init(&this->grab_lock, NULL);
  pthread_cond_init(&this->grab_cond, NULL);

  return &this->vo_driver;
}

/*
 * class functions
 */

static void *vdpau_init_class (xine_t *xine, void *visual_gen)
{
  vdpau_class_t *this = (vdpau_class_t *) calloc(1, sizeof(vdpau_class_t));

  this->driver_class.open_plugin     = vdpau_open_plugin;
  this->driver_class.identifier      = "vdpau";
  this->driver_class.description     = N_("xine video output plugin using VDPAU hardware acceleration");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;

  return this;
}



static const vo_info_t vo_info_vdpau = {
  11,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "vdpau", XINE_VERSION_CODE, &vo_info_vdpau, vdpau_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
