/*
 * Copyright (C) 2012-2013 the xine project
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
 */


/*
  TJ. the output color matrix selection feature.

  This file must be included after declaration of xxxx_driver_t,
  and #define'ing CM_DRIVER_T to it.
  That struct must contain the integer value cm_state.
  Also #define CM_HAVE_YCGCO_SUPPORT if you already handle that.

  cm_from_frame () returns current (color_matrix << 1) | color_range control value.
  Having only 1 var simplifies change event handling and avoids unecessary vo
  reconfiguration. In the libyuv2rgb case, they are even handled by same code.

  In theory, HD video uses a different YUV->RGB matrix than the rest.
  It shall come closer to the human eye's brightness feel, and give
  more shades of green even without higher bit depth.

  I discussed this topic with local TV engineers earlier.
  They say their studio equipment throws around uncompressed YUV with no
  extra info attached to it. Anything smaller than 720p is assumed to be
  ITU-R 601, otherwise ITU-R 709. A rematrix filter applies whenever
  video is scaled across the above mentioned HD threshold.

  However, the weak point of their argumentation is potentially non-standard
  input material. Those machines obviously dont verify input data, and
  ocasionally they dont even respect stream info (tested by comparing TV
  against retail DVD version of same movie).

  Consumer TV sets handle this fairly inconsistent - stream info, video size,
  hard-wired matrix, user choice or so-called intelligent picture enhancers
  that effectively go way off standards.
  So I decided to provide functionality, and let the user decide if and how
  to actually use it.

  BTW. Rumour has it that proprietory ATI drivers auto switch their xv ports
  based on video size. not user configurable, and not tested...
*/

/* eveybody gets these */

/* user configuration settings */
#define CM_CONFIG_NAME   "video.output.color_matrix"
#define CM_CONFIG_SIGNAL 0
#define CM_CONFIG_SIZE   1
#define CM_CONFIG_SD     2
#define CM_CONFIG_HD     3

#define CR_CONFIG_NAME   "video.output.color_range"
#define CR_CONFIG_AUTO   0
#define CR_CONFIG_MPEG   1
#define CR_CONFIG_FULL   2

static const char * const cm_names[] = {
  "RGB",
  "RGB",
  "ITU-R 709 / HDTV",
  "full range ITU-R 709 / HDTV",
  "undefined",
  "full range, undefined",
  "ITU-R 470 BG / SDTV",
  "full range ITU-R 470 BG / SDTV",
  "FCC",
  "full range FCC",
  "ITU-R 470 BG / SDTV",
  "full range ITU-R 470 BG / SDTV",
  "SMPTE 170M",
  "full range SMPTE 170M",
  "SMPTE 240M",
  "full range SMPTE 240M"
#ifdef CM_HAVE_YCGCO_SUPPORT
  ,
  "YCgCo",
  "YCgCo", /* this is always fullrange */
  "#9",
  "fullrange #9",
  "#10",
  "fullrange #10",
  "#11",
  "fullrange #11",
  "#12",
  "fullrange #12",
  "#13",
  "fullrange #13",
  "#14",
  "fullrange #14",
  "#15",
  "fullrange #15"
#endif
};

#ifdef CM_DRIVER_T

/* this is for vo plugins only */

/* the option names */
static const char * const cm_conf_labels[] = {
  "Signal", "Signal+Size", "SD", "HD", NULL
};

static const char * const cr_conf_labels[] = {
  "Auto", "MPEG", "FULL", NULL
};

/* callback when user changes them */
static void cm_cb_config (void *this, xine_cfg_entry_t *entry) {
  *((int *)this) = (*((int *)this) & 3) | (entry->num_value << 2);
}

static void cr_cb_config (void *this, xine_cfg_entry_t *entry) {
  *((int *)this) = (*((int *)this) & 0x1c) | entry->num_value;
}

static void cm_init (CM_DRIVER_T *this) {
  /* register configuration */
  this->cm_state = this->xine->config->register_enum (
    this->xine->config,
    CM_CONFIG_NAME,
    CM_CONFIG_SIZE,
    (char **)cm_conf_labels,
    _("Output color matrix"),
    _("Tell how output colors should be calculated.\n\n"
      "Signal: Do as current stream suggests.\n"
      "        This may be wrong sometimes.\n\n"
      "Signal+Size: Same as above,\n"
      "        but assume HD color for unmarked HD streams.\n\n"
      "SD:     Force SD video standard ITU-R 470/601.\n"
      "        Try this if you get too little green.\n\n"
      "HD:     Force HD video standard ITU-R 709.\n"
      "        Try when there is too much green coming out.\n\n"),
    10,
    cm_cb_config,
    &this->cm_state
  ) << 2;
  this->cm_state |= this->xine->config->register_enum (
    this->xine->config,
    CR_CONFIG_NAME,
    CR_CONFIG_AUTO,
    (char **)cr_conf_labels,
    _("Output color range"),
    _("Tell how output colors should be ranged.\n\n"
      "Auto: Do as current stream suggests.\n"
      "      This may be wrong sometimes.\n\n"
      "MPEG: Force MPEG color range (16..235) / studio swing / video mode.\n"
      "      Try if image looks dull (no real black or white in it).\n\n"
      "FULL: Force FULL color range (0..255) / full swing / PC mode.\n"
      "      Try when flat black and white spots appear.\n\n"),
    10,
    cr_cb_config,
    &this->cm_state
  );
}

static uint8_t cm_m[] = {
  5, 1, 5, 3, 4, 5, 6, 7, 8, 5, 5, 5, 5, 5, 5, 5, /* SIGNAL */
  5, 1, 5, 3, 4, 5, 6, 7, 8, 5, 5, 5, 5, 5, 5, 5, /* SIZE */
  5, 5, 5, 5, 5, 5, 5, 5, 8, 5, 5, 5, 5, 5, 5, 5, /* SD */
  5, 1, 1, 1, 1, 1, 1, 1, 8, 1, 1, 1, 1, 1, 1, 1  /* HD */
};

static uint8_t cm_r[] = {0, 0, 1, 0}; /* AUTO, MPEG, FULL, safety */

static int cm_from_frame (vo_frame_t *frame) {
  CM_DRIVER_T *this = (CM_DRIVER_T *)frame->driver;
  int cm = VO_GET_FLAGS_CM (frame->flags);
  int cf = this->cm_state;

  cm_m[18] = (frame->height - frame->crop_top - frame->crop_bottom >= 720) ||
             (frame->width - frame->crop_left - frame->crop_right >= 1280) ? 1 : 5;
  cm_r[0] = cm & 1;
#ifdef CM_HAVE_YCGCO_SUPPORT
  return ((cm_m[((cf >> 2) << 4) | (cm >> 1)] << 1) | cm_r[cf & 2]);
#else
  return ((cm_m[((cf >> 2) << 4) | (cm >> 1)] << 1) | cm_r[cf & 2]) & 15;
#endif
}

static void cm_close (CM_DRIVER_T *this) {
  /* dont know whether this is really necessary */
  this->xine->config->unregister_callback (this->xine->config, CR_CONFIG_NAME);
  this->xine->config->unregister_callback (this->xine->config, CM_CONFIG_NAME);
}

#endif /* defined CM_DRIVER_T */
