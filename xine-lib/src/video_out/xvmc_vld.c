/*
 * Copyright (C) 2000-2004 the xine project
 * Copyright (C) 2004 the Unichrome project
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
 * xvmc_vld.c, X11 decoding accelerated video extension interface for xine
 *
 * Author: Thomas Hellström, (2004)
 */

#include "xxmc.h"
#include <unistd.h>

#ifdef HAVE_VLDXVMC
void xvmc_vld_frame(struct vo_frame_s *this_gen)

{
  vo_frame_t *this = (vo_frame_t *) this_gen;
  xxmc_frame_t
    *cf = XXMC_FRAME(this);
  xine_vld_frame_t
    *vft = &(cf->xxmc_data.vld_frame);
  xxmc_frame_t
    *ff = XXMC_FRAME(vft->forward_reference_frame),
    *bf = XXMC_FRAME(vft->backward_reference_frame);
  XvMCMpegControl ctl;
  xxmc_driver_t
    *driver = (xxmc_driver_t *) cf->vo_frame.driver;
  XvMCSurface *fs=0, *bs=0;
  XvMCQMatrix qmx;

  ctl.BHMV_range = vft->mv_ranges[0][0];
  ctl.BVMV_range = vft->mv_ranges[0][1];
  ctl.FHMV_range = vft->mv_ranges[1][0];
  ctl.FVMV_range = vft->mv_ranges[1][1];
  ctl.picture_structure = vft->picture_structure;
  ctl.intra_dc_precision = vft->intra_dc_precision;
  ctl.picture_coding_type = vft->picture_coding_type;
  ctl.mpeg_coding = (vft->mpeg_coding == 0) ? XVMC_MPEG_1 : XVMC_MPEG_2;
  ctl.flags = 0;
  ctl.flags |= (vft->progressive_sequence) ?
    XVMC_PROGRESSIVE_SEQUENCE : 0 ;
  ctl.flags |= (vft->scan) ?
    XVMC_ALTERNATE_SCAN : XVMC_ZIG_ZAG_SCAN;
  ctl.flags |= (vft->pred_dct_frame) ?
    XVMC_PRED_DCT_FRAME : XVMC_PRED_DCT_FIELD;
  ctl.flags |= (this->top_field_first) ?
    XVMC_TOP_FIELD_FIRST : XVMC_BOTTOM_FIELD_FIRST;
  ctl.flags |= (vft->concealment_motion_vectors) ?
    XVMC_CONCEALMENT_MOTION_VECTORS : 0 ;
  ctl.flags |= (vft->q_scale_type) ?
    XVMC_Q_SCALE_TYPE : 0;
  ctl.flags |= (vft->intra_vlc_format) ?
    XVMC_INTRA_VLC_FORMAT : 0;
  ctl.flags |= (vft->second_field) ?
    XVMC_SECOND_FIELD : 0 ;

  if (ff) fs=ff->xvmc_surf;
  if (bf) bs=bf->xvmc_surf;

  /*
   * Below is for interlaced streams and second_field.
   */

  if (ctl.picture_coding_type == XVMC_P_PICTURE)
    bs = cf->xvmc_surf;

  if ((qmx.load_intra_quantiser_matrix = vft->load_intra_quantizer_matrix)) {
    memcpy(qmx.intra_quantiser_matrix,vft->intra_quantizer_matrix,
	   sizeof(qmx.intra_quantiser_matrix));
  }
  if ((qmx.load_non_intra_quantiser_matrix = vft->load_non_intra_quantizer_matrix)) {
    memcpy(qmx.non_intra_quantiser_matrix,vft->non_intra_quantizer_matrix,
	   sizeof(qmx.non_intra_quantiser_matrix));
  }
  qmx.load_chroma_intra_quantiser_matrix = 0;
  qmx.load_chroma_non_intra_quantiser_matrix = 0;

  XVMCLOCKDISPLAY( driver->display );
  XvMCLoadQMatrix(driver->display, &driver->context, &qmx);

  while((cf->xxmc_data.result =
	 XvMCBeginSurface(driver->display, &driver->context, cf->xvmc_surf,
			  fs, bs, &ctl)));
  XVMCUNLOCKDISPLAY( driver->display );
  driver->cpu_saver = 0.;
}

void xvmc_vld_slice(vo_frame_t *this_gen)
{
  xxmc_frame_t
    *cf = XXMC_FRAME(this_gen);
  xxmc_driver_t
    *driver = (xxmc_driver_t *) cf->vo_frame.driver;

  XVMCLOCKDISPLAY( driver->display );
  cf->xxmc_data.result =
    XvMCPutSlice2(driver->display,&driver->context,cf->xxmc_data.slice_data,
		  cf->xxmc_data.slice_data_size,cf->xxmc_data.slice_code);
  /*
   * If CPU-saving mode is enabled, sleep after every xxmc->sleep slice. This will free
   * up the cpu while the decoder is working on the slice. The value of xxmc->sleep is calculated
   * so that the decoder thread sleeps at most 50% of the frame delay,
   * assuming a 2.6 kernel clock of 1000 Hz.
   */

  XVMCUNLOCKDISPLAY( driver->display );
  if (driver->cpu_save_enabled) {
    driver->cpu_saver += 1.;
    if (driver->cpu_saver >= cf->xxmc_data.sleep) {
      usleep(1);
      driver->cpu_saver -= cf->xxmc_data.sleep;
    }
  }
}
#endif

