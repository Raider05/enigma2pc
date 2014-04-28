/*
 * Copyright (C) 2000-2014 the xine project
 * Copyright (C) 2004 the Unichrome project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
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
 * video_out_xxmc.c, X11 decoding accelerated video extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * Xv image support by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 *
 * overlay support by James Courtier-Dutton <James@superbug.demon.co.uk> - July 2001
 * X11 unscaled overlay support by Miguel Freitas - Nov 2003
 * XvMC VLD implementation by Thomas Hellström - 2004, 2005.
 * XvMC merge by Thomas Hellström - Sep 2004
 */


#include "xxmc.h"
#include <unistd.h>
#include "xv_common.h"


static int gX11Fail;
static void xxmc_frame_updates(xxmc_driver_t *driver, xxmc_frame_t *frame,
			       int init_macroblocks);
static void dispose_ximage (xxmc_driver_t *this, XShmSegmentInfo *shminfo,
			    XvImage *myimage);

VIDEO_DEVICE_XV_DECL_BICUBIC_TYPES;
VIDEO_DEVICE_XV_DECL_PREFER_TYPES;

/*
 * Acceleration level priority. Static for now. It may well turn out that IDCT
 * is more efficient than VLD.
 */

static const unsigned int accel_priority[] = {
#ifdef HAVE_VLDXVMC
				    XINE_XVMC_ACCEL_VLD,
#endif
				    XINE_XVMC_ACCEL_IDCT,
				    XINE_XVMC_ACCEL_MOCOMP};
#define NUM_ACCEL_PRIORITY (sizeof(accel_priority)/sizeof(accel_priority[0]))

#ifndef XVMC_VLD
  #define XVMC_VLD 0
#endif

/*
 * Additional thread safety, since the plugin may decide to destroy a context
 * while it's surfaces are still active in the video-out loop.
 * When / If XvMC libs are reasonably thread-safe, the locks can be made
 * more efficient by allowing multiple threads in that do not destroy
 * the context or surfaces that may be active in other threads.
 */

static void init_context_lock(context_lock_t *c)
{
  pthread_cond_init(&c->cond,NULL);
  pthread_mutex_init(&c->mutex,NULL);
  c->num_readers = 0;
}

static void free_context_lock(context_lock_t *c)
{
  pthread_mutex_destroy(&c->mutex);
  pthread_cond_destroy(&c->cond);
}

void xvmc_context_reader_lock(context_lock_t *c)
{
  pthread_mutex_lock(&c->mutex);
#ifdef XVMC_THREAD_SAFE
  c->num_readers++;
  pthread_mutex_unlock(&c->mutex);
#endif
}

void xvmc_context_reader_unlock(context_lock_t *c)
{
#ifdef XVMC_THREAD_SAFE
  pthread_mutex_lock(&c->mutex);
  if (c->num_readers > 0) {
    if (--(c->num_readers) == 0) {
      pthread_cond_broadcast(&c->cond);
    }
  }
#endif
  pthread_mutex_unlock(&c->mutex);
}

static void xvmc_context_writer_lock(context_lock_t *c)
{
  pthread_mutex_lock(&c->mutex);
#ifdef XVMC_THREAD_SAFE
  while(c->num_readers) {
    pthread_cond_wait(&c->cond, &c->mutex);
  }
#endif
}

static void xvmc_context_writer_unlock(context_lock_t *c)
{
  pthread_mutex_unlock(&c->mutex);
}

/*
 * A number of simple surface allocator functions that implements the
 * notion that a surface may be invalid if it is asynchronously
 * destroyed. Both surfaces and subpictures are handled this way.
 */



static void xxmc_xvmc_dump_surfaces(xxmc_driver_t *this )
{
  int
    i;
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;

  for (i=0; i<XVMC_MAX_SURFACES; ++i) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "%d %d;",handler->surfInUse[i],
	    handler->surfValid[i]);
  }
  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "\n");
}

static void xxmc_xvmc_dump_subpictures(xxmc_driver_t *this)
{
  int
    i;
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;

  for (i=0; i<XVMC_MAX_SUBPICTURES; ++i) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "%d %d;",handler->subInUse[i],
	    handler->subValid[i]);
  }
  xprintf(this->xine, XINE_VERBOSITY_DEBUG, "\n");
}


static void xxmc_xvmc_surface_handler_construct(xxmc_driver_t *this)
{
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;

  pthread_mutex_init(&handler->mutex,NULL);
  memset(handler->surfInUse, 0, sizeof(*handler->surfInUse)*XVMC_MAX_SURFACES);
  memset(handler->surfValid, 0, sizeof(*handler->surfValid)*XVMC_MAX_SURFACES);
  memset(handler->subInUse, 0, sizeof(*handler->subInUse)*XVMC_MAX_SUBPICTURES);
  memset(handler->subValid, 0, sizeof(*handler->subValid)*XVMC_MAX_SUBPICTURES);
}

static void xxmc_xvmc_destroy_surfaces(xxmc_driver_t *this)
{
  int i;
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;

  pthread_mutex_lock(&handler->mutex);
  for (i=0; i < XVMC_MAX_SURFACES; ++i) {
    XVMCLOCKDISPLAY( this->display );
    if (handler->surfValid[i]) {
      XvMCFlushSurface( this->display , handler->surfaces+i);
      XvMCSyncSurface( this->display, handler->surfaces+i );
      XvMCHideSurface( this->display, handler->surfaces+i );
      XvMCDestroySurface( this->display, handler->surfaces+i );
    }
    XVMCUNLOCKDISPLAY( this->display );
    handler->surfValid[i] = 0;
  }
  pthread_mutex_unlock(&handler->mutex);

}

static void xxmc_xvmc_destroy_subpictures(xxmc_driver_t *this)
{
  int i;
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;

  pthread_mutex_lock(&handler->mutex);
  for (i=0; i < XVMC_MAX_SUBPICTURES; ++i) {
    XVMCLOCKDISPLAY( this->display );
    if (handler->subValid[i]) {
      XvMCFlushSubpicture( this->display , handler->subpictures+i);
      XvMCSyncSubpicture( this->display, handler->subpictures+i );
      XvMCDestroySubpicture( this->display, handler->subpictures+i );
    }
    XVMCUNLOCKDISPLAY( this->display );
    handler->subValid[i] = 0;
  }
  pthread_mutex_unlock(&handler->mutex);
}

static XvMCSurface *xxmc_xvmc_alloc_surface(xxmc_driver_t *this,
					    XvMCContext *context)
{
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;
  int i;

  pthread_mutex_lock(&handler->mutex);
  xxmc_xvmc_dump_surfaces(this);
  for (i=0; i<XVMC_MAX_SURFACES; ++i) {
    if (handler->surfValid[i] && !handler->surfInUse[i]) {
      handler->surfInUse[i] = 1;
      xxmc_xvmc_dump_surfaces(this);
      pthread_mutex_unlock(&handler->mutex);
      return handler->surfaces + i;
    }
  }
  for (i=0; i<XVMC_MAX_SURFACES; ++i) {
    if (!handler->surfInUse[i]) {
      XVMCLOCKDISPLAY( this->display );
      if (Success != XvMCCreateSurface( this->display, context,
					handler->surfaces + i)) {
	XVMCUNLOCKDISPLAY( this->display );
	pthread_mutex_unlock(&handler->mutex);
	return NULL;
      }
      XVMCUNLOCKDISPLAY( this->display );
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       LOG_MODULE ": Created surface %d\n",i);
      handler->surfInUse[i] = 1;
      handler->surfValid[i] = 1;
      pthread_mutex_unlock(&handler->mutex);
      return handler->surfaces + i;
    }
  }
  pthread_mutex_unlock(&handler->mutex);
  return NULL;
}

static void xxmc_xvmc_free_surface(xxmc_driver_t *this, XvMCSurface *surf)
{
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;
  unsigned
    index = surf - handler->surfaces;

  if (index >= XVMC_MAX_SURFACES) return;
  pthread_mutex_lock(&handler->mutex);
  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   LOG_MODULE ": Disposing of surface %d\n",index);
  handler->surfInUse[index]--;
  xxmc_xvmc_dump_surfaces(this);
  pthread_mutex_unlock(&handler->mutex);
}

int xxmc_xvmc_surface_valid(xxmc_driver_t *this, XvMCSurface *surf)
{
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;
  unsigned
    index = surf - handler->surfaces;
  int ret;

  if (index >= XVMC_MAX_SURFACES) return 0;
  pthread_mutex_lock(&handler->mutex);
  ret = handler->surfValid[index];
  pthread_mutex_unlock(&handler->mutex);
  return ret;
}

static XvMCSubpicture *xxmc_xvmc_alloc_subpicture
                         (xxmc_driver_t *this,
			  XvMCContext *context, unsigned short width,
			  unsigned short height, int xvimage_id)
{
  int i;
  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;
  int status;

  pthread_mutex_lock(&handler->mutex);
  xxmc_xvmc_dump_subpictures(this);
  for (i=0; i<XVMC_MAX_SUBPICTURES; ++i) {
    if (handler->subValid[i] && !handler->subInUse[i]) {
      XVMCLOCKDISPLAY( this->display );
      if (XvMCGetSubpictureStatus( this->display, handler->subpictures + i,
				   &status)) {
	XVMCUNLOCKDISPLAY( this->display );
	continue;
      }
      XVMCUNLOCKDISPLAY( this->display );
      if (status & XVMC_DISPLAYING)
	continue;
      handler->subInUse[i] = 1;
      xxmc_xvmc_dump_subpictures(this);
      pthread_mutex_unlock(&handler->mutex);
      return handler->subpictures + i;
    }
  }
  for (i=0; i<XVMC_MAX_SUBPICTURES; ++i) {
    if (!handler->subInUse[i]) {
      XVMCLOCKDISPLAY( this->display );
      if (Success != XvMCCreateSubpicture( this->display, context,
					   handler->subpictures + i,
					   width, height, xvimage_id)) {
	XVMCUNLOCKDISPLAY( this->display );
	pthread_mutex_unlock(&handler->mutex);
	return NULL;
      }
      XVMCUNLOCKDISPLAY( this->display );
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       LOG_MODULE ": Created subpicture %d\n",i);
      handler->subInUse[i] = 1;
      handler->subValid[i] = 1;
      pthread_mutex_unlock(&handler->mutex);
      return handler->subpictures + i;
    }
  }
  pthread_mutex_unlock(&handler->mutex);
  return NULL;
}

static void xxmc_xvmc_free_subpicture(xxmc_driver_t *this, XvMCSubpicture *sub)
{

  xvmc_surface_handler_t *handler = &this->xvmc_surf_handler;
  unsigned
    index = sub - handler->subpictures;

  if (index >= XVMC_MAX_SUBPICTURES) return;
  pthread_mutex_lock(&handler->mutex);
  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   LOG_MODULE ": Disposing of subpicture %d\n",index);
  handler->subInUse[index] = 0;
  xxmc_xvmc_dump_subpictures(this);
  pthread_mutex_unlock(&handler->mutex);

}

/*
 * Callback used by decoder to check that surfaces are still valid,
 * and to lock the context so that it won't get destroyed during
 * decoding.
 */


static int xxmc_lock_and_validate_surfaces(vo_frame_t *cur_frame,
					   vo_frame_t *fw_frame,
					   vo_frame_t *bw_frame,
					   unsigned pc_type)
{
  xxmc_driver_t
    *driver = (xxmc_driver_t *) cur_frame->driver;
  xxmc_frame_t
    *frame;

  xvmc_context_reader_lock( &driver->xvmc_lock );

  switch(pc_type) {
  case XINE_PICT_B_TYPE:
    frame = XXMC_FRAME(bw_frame);
    if (!xxmc_xvmc_surface_valid( driver, frame->xvmc_surf)) break;
    /* fall through */
  case XINE_PICT_P_TYPE:
    frame = XXMC_FRAME(fw_frame);
    if (!xxmc_xvmc_surface_valid( driver, frame->xvmc_surf)) break;
    /* fall through */
  default:
    frame = XXMC_FRAME(cur_frame);
    if (!xxmc_xvmc_surface_valid( driver, frame->xvmc_surf)) break;
    return 0;
  }

  xvmc_context_reader_unlock( &driver->xvmc_lock );
  return -1;
}

/*
 * Callback for decoder. Decoding temporarily halted. Release the context.
 */

static void xxmc_unlock_surfaces(vo_driver_t *this_gen)
{
  xxmc_driver_t
    *driver = (xxmc_driver_t *) this_gen;

  xvmc_context_reader_unlock( &driver->xvmc_lock );
}

/*
 * Callback for decoder.
 * Check that the surface is vaid and
 * flush outstanding rendering requests on this surface.
 */

static void xvmc_flush(vo_frame_t *this_gen)
{

  xxmc_frame_t
    *frame = XXMC_FRAME(this_gen);
  xxmc_driver_t
    *driver = (xxmc_driver_t *) this_gen->driver;

  xvmc_context_reader_lock( &driver->xvmc_lock );

  if ( ! xxmc_xvmc_surface_valid( driver, frame->xvmc_surf)) {
    frame->xxmc_data.result = 128;
    xvmc_context_reader_unlock( &driver->xvmc_lock );
    return;
  }

  XVMCLOCKDISPLAY( driver->display );
  frame->xxmc_data.result = XvMCFlushSurface( driver->display, frame->xvmc_surf );
  XVMCUNLOCKDISPLAY( driver->display );

  xvmc_context_reader_unlock( &driver->xvmc_lock );

}


/*
 * Callback function for the VO-loop to duplicate frame data.
 * YV12 and YUY2 formats are taken care of in the xine-engine.
 * This one only deals with hardware surfaces and duplicates them
 * using a call to XvMCBlendSubpicture2 with a blank subpicture.
 */

static void xxmc_duplicate_frame_data(vo_frame_t *this_gen,
				      vo_frame_t *original)
{
  xxmc_frame_t *this = (xxmc_frame_t *) this_gen,
    *orig = (xxmc_frame_t *) original;
  xxmc_driver_t *driver = (xxmc_driver_t *) this_gen->driver;
  xine_t *xine = driver->xine;
  xine_xxmc_t *xxmc;
  XvMCSubpicture *tmp;
  int need_dummy;

  if (original->format != XINE_IMGFMT_XXMC)
    return;
  xxmc = &orig->xxmc_data;
  xvmc_context_writer_lock( &driver->xvmc_lock);
  if (!xxmc_xvmc_surface_valid(driver,orig->xvmc_surf)) {
    xvmc_context_writer_unlock( &driver->xvmc_lock );
    return;
  }
  this->xxmc_data = *xxmc;
  this->xxmc_data.xvmc.vo_frame = &this->vo_frame;
  this->width = original->width;
  this->height = original->height;
  this->format = original->format;
  this->ratio = original->ratio;

  xxmc_frame_updates(driver,this,0);

  /*
   * Allocate a dummy subpicture and copy using
   * XvMCBlendsubpicture2. VLD implementations can do blending with a
   * NULL subpicture. Use that if possible.
   */

  need_dummy = (xxmc->acceleration != XINE_XVMC_ACCEL_VLD);
  tmp = NULL;
  if (need_dummy) {
    tmp = xxmc_xvmc_alloc_subpicture( driver, &driver->context,
				      this->width, this->height,
				      driver->xvmc_cap
				      [driver->xvmc_cur_cap].subPicType.id);
  }
  if (tmp || !need_dummy) {
    XVMCLOCKDISPLAY( driver->display );
    if (tmp) XvMCClearSubpicture(driver->display, tmp , 0,0, this->width,
				 this->height, 0);
    if (Success == XvMCBlendSubpicture2( driver->display, orig->xvmc_surf,
					 this->xvmc_surf, tmp,
					 0,0,this->width, this->height,
					 0,0,this->width, this->height)) {
      this->xxmc_data.decoded = 1;
    }
    XVMCUNLOCKDISPLAY( driver->display );
    if (tmp) xxmc_xvmc_free_subpicture( driver, tmp);
  }

  xvmc_context_writer_unlock( &driver->xvmc_lock );
  xprintf(xine, XINE_VERBOSITY_DEBUG, "Duplicated XvMC frame %d %d.\n",
	  this->width,this->height);
}

static uint32_t xxmc_get_capabilities (vo_driver_t *this_gen) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  return this->capabilities;
}


static void xxmc_frame_field (vo_frame_t *vo_img, int which_field)
{
  lprintf ("xvmc_frame_field\n");
}

static void xxmc_frame_dispose (vo_frame_t *vo_img) {
  xxmc_frame_t  *frame = (xxmc_frame_t *) vo_img ;
  xxmc_driver_t *this  = (xxmc_driver_t *) vo_img->driver;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "Disposing of frame\n");

  xvmc_context_writer_lock( &this->xvmc_lock );
  if (this->xvmc_cap && frame->xvmc_surf) {
    xxmc_xvmc_free_surface( this, frame->xvmc_surf );
    frame->xvmc_surf = 0;
  }
  xvmc_context_writer_unlock( &this->xvmc_lock );

  if (frame->image) {

    if (this->use_shm) {
      XLockDisplay (this->display);
      XShmDetach (this->display, &frame->shminfo);
      XFree (frame->image);
      XUnlockDisplay (this->display);

      shmdt (frame->shminfo.shmaddr);
      shmctl (frame->shminfo.shmid, IPC_RMID, NULL);
    }
    else {
      if (frame->image->data) free(frame->image->data);
      XLockDisplay (this->display);
      XFree (frame->image);
      XUnlockDisplay (this->display);
    }
  }
  free (frame);
}

/*
 * Note that this one does NOT allocate the XvMC surface. That is
 * done by an additional decoder callback.
 */

static vo_frame_t *xxmc_alloc_frame (vo_driver_t *this_gen) {
  xxmc_driver_t  *this = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame ;

  frame = calloc(1, sizeof (xxmc_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);
  frame->xvmc_surf = NULL;

  /*
   * supply required functions
   */

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.proc_duplicate_frame_data = NULL;
  frame->vo_frame.field      = xxmc_frame_field;
  frame->vo_frame.dispose    = xxmc_frame_dispose;
  frame->vo_frame.driver     = this_gen;
  frame->last_sw_format      = 0;
  frame->vo_frame.accel_data = &frame->xxmc_data;
  frame->xxmc_data.xvmc.vo_frame = &frame->vo_frame;
  frame->image               = NULL;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, "Allocating frame\n");

  return (vo_frame_t *) frame;
}

static int HandleXError (Display *display, XErrorEvent *xevent) {
  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);
  printf ("received X error event: %s\n", str);
  gX11Fail = 1;

  return 0;
}

/* called xlocked */
static void x11_InstallXErrorHandler (xxmc_driver_t *this) {
  this->x11_old_error_handler = XSetErrorHandler (HandleXError);
  XSync(this->display, False);
}

/* called xlocked */
static void x11_DeInstallXErrorHandler (xxmc_driver_t *this) {
  XSetErrorHandler (this->x11_old_error_handler);
  XSync(this->display, False);
  this->x11_old_error_handler = NULL;
}

/* called xlocked */
static XvImage *create_ximage (xxmc_driver_t *this, XShmSegmentInfo *shminfo,
			       int width, int height, int format) {
  unsigned int  xv_format;
  XvImage      *image = NULL;

  if (this->use_pitch_alignment) {
    width = (width + 7) & ~0x7;
  }

  switch (format) {
  case XINE_IMGFMT_YV12:
    xv_format = this->xv_format_yv12;
    break;
  case XINE_IMGFMT_YUY2:
    xv_format = this->xv_format_yuy2;
    break;
  case FOURCC_IA44:
  case FOURCC_AI44:
    xv_format = format;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
    _x_abort();
  }

  if (this->use_shm) {

    /*
     * try shm
     */

    gX11Fail = 0;
    x11_InstallXErrorHandler (this);

    image = XvShmCreateImage(this->display, this->xv_port, xv_format, 0,
			     width, height, shminfo);

    if (image == NULL )  {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: XvShmCreateImage failed\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmid = shmget(IPC_PRIVATE, image->data_size, IPC_CREAT | 0777);

    if (image->data_size==0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: XvShmCreateImage returned a zero size\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmid < 0 ) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: shared memory error in shmget: %s\n"), LOG_MODULE, strerror(errno));
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);

    if (shminfo->shmaddr == NULL) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      LOG_MODULE ": shared memory error (address error NULL)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmaddr == ((char *) -1)) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	      LOG_MODULE ": shared memory error (address error)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->readOnly = False;
    image->data       = shminfo->shmaddr;

    XShmAttach(this->display, shminfo);

    XSync(this->display, False);
    shmctl(shminfo->shmid, IPC_RMID, 0);

    if (gX11Fail) {
      shmdt (shminfo->shmaddr);
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: x11 error during shared memory XImage creation\n"), LOG_MODULE);
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: => not using MIT Shared Memory extension.\n"), LOG_MODULE);
      this->use_shm  = 0;
      goto finishShmTesting;
    }

    /*
     * Now that the Xserver has learned about and attached to the
     * shared memory segment,  delete it.  It's actually deleted by
     * the kernel when all users of that segment have detached from
     * it.  Gives an automatic shared memory cleanup in case we crash.
     */
    shmctl (shminfo->shmid, IPC_RMID, 0);
    shminfo->shmid = -1;

  finishShmTesting:
    x11_DeInstallXErrorHandler(this);
  }


  /*
   * fall back to plain Xv if necessary
   */

  if (!this->use_shm) {
    char *data;

    switch (format) {
    case XINE_IMGFMT_YV12:
      data = malloc (width * height * 3/2);
      break;
    case XINE_IMGFMT_YUY2:
      data = malloc (width * height * 2);
      break;
    case FOURCC_IA44:
    case FOURCC_AI44:
      data = malloc (width * height);
      break;
    default:
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "create_ximage: unknown format %08x\n",format);
      _x_abort();
    }

    image = XvCreateImage (this->display, this->xv_port,
			   xv_format, data, width, height);
  }
  return image;
}


/*
 * Utility functions for the main update surface callback.
 */


static void xxmc_dispose_context(xxmc_driver_t *driver)
{
  if (driver->contextActive) {
    if (driver->xvmc_accel & (XINE_XVMC_ACCEL_MOCOMP | XINE_XVMC_ACCEL_IDCT)) {
      xvmc_macroblocks_t *macroblocks = &driver->macroblocks;

      XvMCDestroyMacroBlocks( driver->display, &macroblocks->macro_blocks );
      XvMCDestroyBlocks( driver->display , &macroblocks->blocks );
    }

    xprintf(driver->xine, XINE_VERBOSITY_LOG,
	    LOG_MODULE ": Freeing up XvMC Surfaces and subpictures.\n");
    if (driver->xvmc_palette) free(driver->xvmc_palette);
    _x_dispose_xx44_palette( &driver->palette );
    xxmc_xvmc_destroy_subpictures( driver );
    xxmc_xvmc_destroy_surfaces( driver );
    xprintf(driver->xine, XINE_VERBOSITY_LOG,
	    LOG_MODULE ": Freeing up XvMC Context.\n");
    XLockDisplay (driver->display);
    if (driver->subImage)
      dispose_ximage(driver, &driver->subShmInfo, driver->subImage);
    driver->subImage = NULL;
    XUnlockDisplay (driver->display);
    XVMCLOCKDISPLAY( driver->display );
    XvMCDestroyContext( driver->display, &driver->context);
    XVMCUNLOCKDISPLAY( driver->display );
    driver->contextActive = 0;
    driver->hwSubpictures = 0;
    driver->xvmc_accel = 0;
  }
}

/*
 * Find a suitable XvMC Context according to the acceleration request
 * passed to us in the xxmc variable, and to the acceleration type
 * priority set up in this plugin. Result is returned in
 * driver->xvmc_cur_cap.
 */

static int xxmc_find_context(xxmc_driver_t *driver, xine_xxmc_t *xxmc,
			     unsigned width, unsigned height)
{
  int i,k,found;
  xvmc_capabilities_t *curCap;
  unsigned request_mpeg_flags, request_accel_flags;

  request_mpeg_flags = xxmc->mpeg;
  found = 0;
  curCap = NULL;

  for (k = 0; k < NUM_ACCEL_PRIORITY; ++k) {
    request_accel_flags = xxmc->acceleration & accel_priority[k];
    if (!request_accel_flags) continue;

    curCap = driver->xvmc_cap;
    for (i =0; i < driver->xvmc_num_cap; ++i) {
      xprintf(driver->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ": Surface type %d. Capabilities 0x%8x 0x%8x\n",i,
	      curCap->mpeg_flags,curCap->accel_flags);
      xprintf(driver->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ":   Requests: 0x%8x 0x%8x\n",
	      request_mpeg_flags,request_accel_flags);
      if (((curCap->mpeg_flags & request_mpeg_flags) == request_mpeg_flags) &&
	  ((curCap->accel_flags & request_accel_flags)) &&
	  (width <= curCap->max_width) &&
	  (height <= curCap->max_height)) {
	found = 1;
	break;
      }
      curCap++;
    }
    if ( found ) {
      driver->xvmc_cur_cap = i;
      break;
    }
  }
  if ( found ) {
    driver->xvmc_accel = request_accel_flags;
    driver->unsigned_intra = curCap->flags & XVMC_INTRA_UNSIGNED;
    return 1;
  }
  driver->xvmc_accel = 0;
  return 0;
}

static int xxmc_create_context(xxmc_driver_t *driver, unsigned width, unsigned height)
{
  xvmc_capabilities_t *curCap;

  curCap = driver->xvmc_cap + driver->xvmc_cur_cap;
  xprintf(driver->xine, XINE_VERBOSITY_LOG,
	  LOG_MODULE ": Creating new XvMC Context %d\n",curCap->type_id);
  XVMCLOCKDISPLAY( driver->display );
  if (Success == XvMCCreateContext( driver->display, driver->xv_port,
				    curCap->type_id, width,
				    height, driver->context_flags,
				    &driver->context)) {
    driver->xvmc_mpeg = curCap->mpeg_flags;
    driver->xvmc_width = width;
    driver->xvmc_height = height;
    driver->contextActive = 1;
  }
  XVMCUNLOCKDISPLAY( driver->display );
  return driver->contextActive;
}

static void xxmc_setup_subpictures(xxmc_driver_t *driver, unsigned width, unsigned height)
{
  xvmc_capabilities_t *curCap;
  XvMCSubpicture *sp;

  if (driver->contextActive) {

    /*
     * Determine if we can use hardware subpictures, and in that case, set up an
     * XvImage that we can use for blending.
     */
    curCap = driver->xvmc_cap + driver->xvmc_cur_cap;

    if ((width > curCap->sub_max_width) ||
	(height > curCap->sub_max_height)) return;

    if ((driver->xvmc_backend_subpic = (curCap->flags & XVMC_BACKEND_SUBPICTURE)))
      xprintf(driver->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ": Using Backend subpictures.\n");

    if (!driver->subImage) {
      /*
       * Note: If other image formats than xx44 are to be used here, they must be
       * translated to XINE_IMGFMT_XXX, since that is what create_ximage
       * expects.
       */

      XLockDisplay (driver->display);

      driver->subImage =
	create_ximage(driver, &driver->subShmInfo, width, height, curCap->subPicType.id);
      XUnlockDisplay (driver->display);
      if (NULL == driver->subImage) {
	xprintf(driver->xine, XINE_VERBOSITY_LOG,
		LOG_MODULE ": Failed allocating XvImage for supbictures.\n");
	return;
      }
    }

    sp = xxmc_xvmc_alloc_subpicture( driver, &driver->context, width,
				     height, curCap->subPicType.id);
    if (sp == NULL) return;

    _x_init_xx44_palette( &driver->palette, sp->num_palette_entries);
    driver->xvmc_palette = (char *) xine_xmalloc(sp->num_palette_entries
						 * sp->entry_bytes);
    xxmc_xvmc_free_subpicture( driver, sp);
    if (driver->xvmc_palette == NULL) return;
    driver->hwSubpictures = 1;
  }
}

static int xxmc_mocomp_create_macroblocks(xxmc_driver_t *driver,
					  xxmc_frame_t *frame,
					  int slices)
{
    Status ret;
    xvmc_macroblocks_t *macroblocks = &driver->macroblocks;
    xine_xxmc_t *xxmc = (xine_xxmc_t *) frame->vo_frame.accel_data;

    slices = (slices * driver->xvmc_width) / 16;
    ret = XvMCCreateMacroBlocks(driver->display, &driver->context, slices,
				&macroblocks->macro_blocks);
    if (ret) return 0;
    ret = XvMCCreateBlocks(driver->display, &driver->context, slices*6,
			   &macroblocks->blocks);
    if (ret) return 0;

    macroblocks->xine_mc.blockbaseptr = macroblocks->blocks.blocks;
    macroblocks->xine_mc.blockptr = macroblocks->xine_mc.blockbaseptr;
    macroblocks->num_blocks = 0;
    macroblocks->macroblockbaseptr = macroblocks->macro_blocks.macro_blocks;
    macroblocks->macroblockptr = macroblocks->macroblockbaseptr;
    macroblocks->slices = slices;
    xxmc->xvmc.macroblocks = (xine_macroblocks_t *)macroblocks;

    return 1;
}

static void xvmc_check_colorkey_properties(xxmc_driver_t *driver)
{
  int num,i;
  XvAttribute *xvmc_attributes;
  Atom ap;

  /*
   * Determine if the context is of "Overlay" type. If so,
   * check whether we can autopaint.
   */

  driver->have_xvmc_autopaint = 0;
  if (driver->context_flags & XVMC_OVERLAID_SURFACE) {
    XVMCLOCKDISPLAY( driver->display );
    xvmc_attributes = XvMCQueryAttributes( driver->display,
					   &driver->context,
					   &num);
    if (xvmc_attributes) {
      for (i=0; i<num; ++i) {
	if (strcmp("XV_AUTOPAINT_COLORKEY", xvmc_attributes[i].name) == 0) {
	  ap = XInternAtom (driver->display, "XV_AUTOPAINT_COLORKEY", False);
	  XvMCSetAttribute(driver->display, &driver->context,ap,
			   driver->props[VO_PROP_AUTOPAINT_COLORKEY].value);
	  driver->have_xvmc_autopaint = 1;
	}
      }
    }
    XFree(xvmc_attributes);
    XVMCUNLOCKDISPLAY( driver->display );
    driver->xvmc_xoverlay_type = X11OSD_COLORKEY;
  } else {
    driver->xvmc_xoverlay_type = X11OSD_SHAPED;
  }
}


static int xxmc_xvmc_update_context(xxmc_driver_t *driver, xxmc_frame_t *frame,
				    uint32_t width, uint32_t height, int frame_format_xxmc)
{
  xine_xxmc_t *xxmc = &frame->xxmc_data;

  /*
   * Are we at all capable of doing XvMC ?
   */


  if (driver->xvmc_cap == 0)
    return 0;

  xprintf(driver->xine, XINE_VERBOSITY_LOG,
	  LOG_MODULE ": New format. Need to change XvMC Context.\n"
	  LOG_MODULE ": width: %d height: %d", width, height);
  if (frame_format_xxmc) {
    xprintf(driver->xine, XINE_VERBOSITY_LOG,
	  " mpeg: %d acceleration: %d", xxmc->mpeg, xxmc->acceleration);
  }
  xprintf(driver->xine, XINE_VERBOSITY_LOG, "\n");

  if (frame->xvmc_surf)
    xxmc_xvmc_free_surface( driver , frame->xvmc_surf);
  frame->xvmc_surf = NULL;

  xxmc_dispose_context( driver );

  if (frame_format_xxmc && xxmc_find_context( driver, xxmc, width, height )) {
    xxmc_create_context( driver, width, height);
    xvmc_check_colorkey_properties( driver );
    xxmc_setup_subpictures(driver, width, height);
    if ((driver->xvmc_accel &
	 (XINE_XVMC_ACCEL_MOCOMP | XINE_XVMC_ACCEL_IDCT))) {
      if (!xxmc_mocomp_create_macroblocks(driver, frame, 1)) {
	printf(LOG_MODULE ": ERROR: Macroblock allocation failed\n");
	xxmc_dispose_context( driver );
      }
    }
  }

  if (!driver->contextActive) {
    printf(LOG_MODULE ": Using software decoding for this stream.\n");
    driver->xvmc_accel = 0;
  } else {
    printf(LOG_MODULE ": Using hardware decoding for this stream.\n");
  }

  driver->xvmc_mpeg = xxmc->mpeg;
  driver->xvmc_width = width;
  driver->xvmc_height = height;
  return driver->contextActive;
}

static void xxmc_frame_updates(xxmc_driver_t *driver,
			       xxmc_frame_t *frame,
			       int init_macroblocks)
{
  xine_xxmc_t *xxmc = &frame->xxmc_data;

  /*
   * If we have changed context since the surface was updated, xvmc_surf
   * is either NULL or invalid. If it is invalid. Set it to NULL.
   * Also if there are other users of this surface, deregister our use of
   * it and later try to allocate a new, fresh one.
   */

  if (frame->xvmc_surf) {
    if (! xxmc_xvmc_surface_valid( driver, frame->xvmc_surf )) {
      xxmc_xvmc_free_surface(driver , frame->xvmc_surf);
      frame->xvmc_surf = NULL;
    }
  }

  /*
   * If it is NULL create a new surface.
   */

  if (frame->xvmc_surf == NULL) {
    if (NULL == (frame->xvmc_surf =
		 xxmc_xvmc_alloc_surface( driver, &driver->context))) {
      fprintf(stderr, LOG_MODULE ": ERROR: Accelerated surface allocation failed.\n"
	      LOG_MODULE ": You are probably out of framebuffer memory.\n"
	      LOG_MODULE ": Falling back to software decoding.\n");
      driver->xvmc_accel = 0;
      xxmc_dispose_context( driver );
      return;
    }
    xxmc->xvmc.macroblocks = (xine_macroblocks_t *) &driver->macroblocks;
    xxmc->xvmc.macroblocks->xvmc_accel = (driver->unsigned_intra) ?
      0 : XINE_VO_SIGNED_INTRA;
    switch(driver->xvmc_accel) {
    case XINE_XVMC_ACCEL_IDCT:
      xxmc->xvmc.macroblocks->xvmc_accel |= XINE_VO_IDCT_ACCEL;
      break;
    case XINE_XVMC_ACCEL_MOCOMP:
      xxmc->xvmc.macroblocks->xvmc_accel |= XINE_VO_MOTION_ACCEL;
      break;
    default:
      xxmc->xvmc.macroblocks->xvmc_accel = 0;
    }


    xxmc->proc_xxmc_flush = xvmc_flush;
    xxmc->proc_xxmc_lock_valid = xxmc_lock_and_validate_surfaces;
    xxmc->proc_xxmc_unlock = xxmc_unlock_surfaces;

    xxmc->xvmc.proc_macro_block = xxmc_xvmc_proc_macro_block;
    frame->vo_frame.proc_duplicate_frame_data = xxmc_duplicate_frame_data;
#ifdef HAVE_VLDXVMC
    xxmc->proc_xxmc_begin = xvmc_vld_frame;
    xxmc->proc_xxmc_slice = xvmc_vld_slice;
#endif
  }

  if (init_macroblocks) {
    driver->macroblocks.num_blocks = 0;
    driver->macroblocks.macroblockptr = driver->macroblocks.macroblockbaseptr;
    driver->macroblocks.xine_mc.blockptr =
      driver->macroblocks.xine_mc.blockbaseptr;
  }
  xxmc->acceleration = driver->xvmc_accel;
}


/* called xlocked */
static void dispose_ximage (xxmc_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XvImage *myimage) {

  if (this->use_shm) {

    XShmDetach (this->display, shminfo);
    XFree (myimage);
    shmdt (shminfo->shmaddr);
    if (shminfo->shmid >= 0) {
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
    }

  }
  else {
    if (myimage->data) free(myimage->data);
    XFree (myimage);
  }

}


static void xxmc_do_update_frame_xv(vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags)
{
  xxmc_driver_t  *this  = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame = (xxmc_frame_t *) frame_gen;

  if (this->use_pitch_alignment) {
    width = (width + 7) & ~0x7;
  }

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->last_sw_format != format)) {

    frame->last_sw_format = format;
    XLockDisplay (this->display);

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {
      dispose_ximage (this, &frame->shminfo, frame->image);
      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo, width, height, format);

    if(format == XINE_IMGFMT_YUY2) {
      frame->vo_frame.pitches[0] = frame->image->pitches[0];
      frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
    }
    else {
      frame->vo_frame.pitches[0] = frame->image->pitches[0];
      frame->vo_frame.pitches[1] = frame->image->pitches[2];
      frame->vo_frame.pitches[2] = frame->image->pitches[1];
      frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
      frame->vo_frame.base[1] = frame->image->data + frame->image->offsets[2];
      frame->vo_frame.base[2] = frame->image->data + frame->image->offsets[1];
    }

    XUnlockDisplay (this->display);
  }

  frame->ratio = ratio;
  frame->width  = width;
  frame->height = height;
  frame->format = format;
  frame->vo_frame.format = frame->format;
}

/*
 * Check if we need to change XvMC context due to an
 * acceleration request change.
 */

static int xxmc_accel_update(xxmc_driver_t *driver,
			     uint32_t last_request,
			     uint32_t new_request)
{
  int k;

  /*
   * Same acceleration request. No need to change.
   */

  if (last_request == new_request) return 0;

  /*
   * Current acceleration not valid. Change.
   */

  if ((driver->xvmc_accel & new_request) == 0) return 1;

  /*
   * Test for possible use of a higher acceleration level.
   */

  for (k = 0; k < NUM_ACCEL_PRIORITY; ++k) {
    if (last_request & accel_priority[k]) return 0;
    if (new_request & accel_priority[k]) return 1;
  }

  /*
   * Should never get here.
   */

  return 0;
}


static void xxmc_do_update_frame(vo_driver_t *this_gen,
				 vo_frame_t *frame_gen,
				 uint32_t width, uint32_t height,
				 double ratio, int format, int flags) {

  xxmc_driver_t  *this  = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame = XXMC_FRAME(frame_gen);

  if ( XINE_IMGFMT_XXMC == format ) {
    xine_xxmc_t *xxmc = &frame->xxmc_data;
    vo_frame_t orig_frame_content;

    if (frame_gen != &frame->vo_frame) {
      /* this is an intercepted frame, so we need to detect and propagate any
       * changes on the original vo_frame to all the intercepted frames */
       xine_fast_memcpy(&orig_frame_content, &frame->vo_frame, sizeof (vo_frame_t));
    }

    xvmc_context_writer_lock( &this->xvmc_lock);
    if (xxmc_accel_update(this, this->last_accel_request, xxmc->acceleration) ||
	(this->xvmc_mpeg != xxmc->mpeg) ||
	(this->xvmc_width != width) ||
	(this->xvmc_height != height)) {
      this->last_accel_request = xxmc->acceleration;
      xxmc_xvmc_update_context(this, frame, width, height, 1);
    } else {
      this->last_accel_request = xxmc->acceleration;
    }

    if (this->contextActive)
      xxmc_frame_updates(this, frame, 1);

    xxmc_do_update_frame_xv(this_gen, &frame->vo_frame, width, height, ratio,
			    xxmc->fallback_format, flags);

    if (!this->contextActive) {
      xxmc->acceleration = 0;
      xxmc->xvmc.macroblocks = 0;
      frame->vo_frame.proc_duplicate_frame_data = NULL;
    } else {
      frame->format = format;
      frame->vo_frame.format = format;
    }

    xvmc_context_writer_unlock( &this->xvmc_lock);

    if (frame_gen != &frame->vo_frame) {
      /* this is an intercepted frame, so we need to detect and propagate any
       * changes on the original vo_frame to all the intercepted frames */
      unsigned char *p0 = (unsigned char *)&orig_frame_content;
      unsigned char *p1 = (unsigned char *)&frame->vo_frame;
      int i;
      for (i = 0; i < sizeof (vo_frame_t); i++) {
        if (*p0 != *p1) {
          /* propagate the change */
          vo_frame_t *f = frame_gen;
          while (f->next) {
            /* serveral restrictions apply when intercepting XXMC frames. So let's check
             * the intercepted frames before modifing them and fail otherwise. */
            unsigned char *p = (unsigned char *)f + i;
            if (*p != *p0) {
              xprintf(this->xine, XINE_VERBOSITY_DEBUG, "xxmc_do_update_frame: a post plugin violates the restrictions on intercepting XXMC frames\n");
              _x_abort();
            }

            *p = *p1;
            f = f->next;
          }
        }
        p0++;
        p1++;
      }
    }
  } else {
    /* switch back to an unaccelerated context */
    if (this->last_accel_request != 0xFFFFFFFF) {
      this->last_accel_request = 0xFFFFFFFF;
      xxmc_xvmc_update_context(this, frame, width, height, 0);
    }
    frame->vo_frame.proc_duplicate_frame_data = NULL;
    xxmc_do_update_frame_xv(this_gen, &frame->vo_frame, width, height, ratio,
			    format, flags);
  }
}

static void xxmc_update_frame_format(vo_driver_t *this_gen,
				     vo_frame_t *frame_gen,
				     uint32_t width, uint32_t height,
				     double ratio, int format, int flags)
{

  if (format != XINE_IMGFMT_XXMC) {
    xxmc_do_update_frame(this_gen, frame_gen, width, height,
			 ratio, format, flags);
  } else {

    /*
     * More parameters are needed to xxmc_do_update_frame().
     * Register the function as a callback and return.
     * The decoder needs to call the callback with more parameters
     * in the xine_xxmc_t structure.
     */

    xine_xxmc_t *xxmc = (xine_xxmc_t *)frame_gen->accel_data;
    xxmc->decoded = 0;
    xxmc->proc_xxmc_update_frame = xxmc_do_update_frame;
    frame_gen->proc_duplicate_frame_data = xxmc_duplicate_frame_data;
  }
}

/*
 * From Xv.
 */

static int xxmc_clean_output_area (xxmc_driver_t *this, int xvmc_active) {
  int i, autopainting, ret;

  XLockDisplay (this->display);

  XSetForeground (this->display, this->gc, this->black.pixel);

  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h ) {
      XFillRectangle(this->display, this->drawable, this->gc,
		     this->sc.border[i].x, this->sc.border[i].y,
		     this->sc.border[i].w, this->sc.border[i].h);
    }
  }

  /*
   * XvMC does not support autopainting regardless of whether there's an
   * Xv attribute for it. However, if there is an XvMC attribute for
   * autopainting, we should be able to assume it is supported.
   * That support is checked whenever a context is changed.
   */

  autopainting = (this->props[VO_PROP_AUTOPAINT_COLORKEY].value == 1);
  if ((xvmc_active &&
       (this->context_flags & XVMC_OVERLAID_SURFACE) &&
       (! this->have_xvmc_autopaint ||
	! autopainting)) ||
      (! xvmc_active && !autopainting)) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height);
    ret = 1;
  } else {
    ret = 0;
  }

  if (this->xoverlay) {
    x11osd_resize (this->xoverlay, this->sc.gui_width, this->sc.gui_height);
    this->ovl_changed = 1;
  }

  XUnlockDisplay (this->display);
  return ret;
}

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

static void xxmc_compute_ideal_size (xxmc_driver_t *this) {
  _x_vo_scale_compute_ideal_size( &this->sc );
}


/*
 * make ideal width/height "fit" into the gui
 */

static void xxmc_compute_output_size (xxmc_driver_t *this) {

  _x_vo_scale_compute_output_size( &this->sc );

}


static void xxmc_check_xoverlay_type(xxmc_driver_t *driver, xxmc_frame_t *frame)

{
  int
    new_overlay_type = (frame->format == XINE_IMGFMT_XXMC) ?
    driver->xvmc_xoverlay_type : driver->xv_xoverlay_type;
  if (driver->xoverlay_type != new_overlay_type) {
    printf("Warning! Changing xoverlay\n");
    x11osd_destroy( driver->xoverlay );
    driver->xoverlay = x11osd_create( driver->xine, driver->display,
				      driver->screen, driver->drawable,
				      new_overlay_type);
    driver->xoverlay_type = new_overlay_type;
  }
}


static void xxmc_overlay_begin (vo_driver_t *this_gen,
				vo_frame_t *frame_gen, int changed) {
  xxmc_driver_t  *this = (xxmc_driver_t *) this_gen;
  xxmc_frame_t *frame = (xxmc_frame_t *) frame_gen;


  this->ovl_changed += changed;

  xvmc_context_reader_lock( &this->xvmc_lock );
  if ((frame->format == XINE_IMGFMT_XXMC) &&
      !xxmc_xvmc_surface_valid(this, frame->xvmc_surf)) {
    xvmc_context_reader_unlock( &this->xvmc_lock );
    return;
  }

  if( this->ovl_changed && this->xoverlay ) {

    XLockDisplay (this->display);
    xxmc_check_xoverlay_type(this, frame);
    x11osd_clear(this->xoverlay);
    XUnlockDisplay (this->display);
  }
  if (this->ovl_changed && (frame->format == XINE_IMGFMT_XXMC) &&
      this->hwSubpictures ) {

    this->new_subpic = xxmc_xvmc_alloc_subpicture
      ( this, &this->context, this->xvmc_width,
	this->xvmc_height,
	this->xvmc_cap[this->xvmc_cur_cap].subPicType.id);

    if (this->new_subpic) {
      this->first_overlay = 1;
      XVMCLOCKDISPLAY( this->display );
      XvMCClearSubpicture(this->display, this->new_subpic, 0,0,
			  this->xvmc_width,
			  this->xvmc_height, 0x00);
      XVMCUNLOCKDISPLAY( this->display );
      _x_clear_xx44_palette(&this->palette);
    }
  }
  xvmc_context_reader_unlock( &this->xvmc_lock );

  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
}

static void xxmc_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img)
{
  xxmc_driver_t  *this = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame = (xxmc_frame_t *) vo_img;


  if( this->ovl_changed && this->xoverlay ) {
    XLockDisplay (this->display);
    x11osd_expose(this->xoverlay);
    XUnlockDisplay (this->display);
  }
  if ((frame->format == XINE_IMGFMT_XXMC) && this->hwSubpictures) {
    LOCK_AND_SURFACE_VALID( this, frame->xvmc_surf );
    if (this->ovl_changed) {
      if (this->old_subpic) {
	xxmc_xvmc_free_subpicture(this, this->old_subpic);
	this->old_subpic = NULL;
      }
      if (this->new_subpic) {
	this->old_subpic = this->new_subpic;
	this->new_subpic = NULL;
	_x_xx44_to_xvmc_palette( &this->palette, this->xvmc_palette,
			      0, this->old_subpic->num_palette_entries,
			      this->old_subpic->entry_bytes,
			      this->reverse_nvidia_palette ? "YVU" :
			      this->old_subpic->component_order);
	XVMCLOCKDISPLAY( this->display );
	XvMCSetSubpicturePalette( this->display, this->old_subpic,
				  this->xvmc_palette);
	XvMCFlushSubpicture( this->display , this->old_subpic);
	XvMCSyncSubpicture( this->display, this->old_subpic );
	XVMCUNLOCKDISPLAY( this->display );
      }
    }
    if (this->old_subpic && (! this->first_overlay)) {
      XVMCLOCKDISPLAY( this->display );
      if (this->xvmc_backend_subpic ) {
	XvMCBlendSubpicture( this->display, frame->xvmc_surf,
			     this->old_subpic,0,0,this->xvmc_width,
			     this->xvmc_height, 0, 0,
			     this->xvmc_width, this->xvmc_height );
      } else {
	XvMCBlendSubpicture2( this->display, frame->xvmc_surf,
			      frame->xvmc_surf,
			      this->old_subpic, 0,0,this->xvmc_width,
			      this->xvmc_height,0,0,this->xvmc_width,
			      this->xvmc_height);
      }
      XVMCUNLOCKDISPLAY( this->display );
    }
    xvmc_context_reader_unlock(&this->xvmc_lock );
  }
  this->ovl_changed = 0;
}


static void xxmc_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
				vo_overlay_t *overlay)
{
  xxmc_driver_t  *this = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame = (xxmc_frame_t *) frame_gen;

  if (overlay->rle) {
    this->scaled_osd_active = !overlay->unscaled;
    if( overlay->unscaled ) {
      if( this->ovl_changed && this->xoverlay ) {
        XLockDisplay (this->display);
        x11osd_blend(this->xoverlay, overlay);
        XUnlockDisplay (this->display);
      }
    } else if (frame->format == XINE_IMGFMT_XXMC) {
      if (this->ovl_changed && this->hwSubpictures) {
	if (this->new_subpic) {
          int x0, y0, x1, y1, w, h;
	  LOCK_AND_SURFACE_VALID( this, frame->xvmc_surf );
	  if (this->first_overlay) {
	    memset(this->subImage->data,0,this->subImage->width*
		   this->subImage->height);
	    this->first_overlay = 0;
	  }
	  _x_blend_xx44(this->subImage->data, overlay, this->subImage->width,
		     this->subImage->height, this->subImage->width,
                     &this->alphablend_extra_data,
		     &this->palette, (this->subImage->id == FOURCC_IA44));

          /* clip overlay against sub image like in _x_blend_xx44() */
          x0 = overlay->x;
          y0 = overlay->y;
          x1 = x0 + overlay->width;
          y1 = y0 + overlay->height;
          w = this->subImage->width;
          h = this->subImage->height;

          x0 = (x0 < 0) ? 0 : ((x0 > w) ? w : x0);
          y0 = (y0 < 0) ? 0 : ((y0 > h) ? h : y0);
          x1 = (x1 < 0) ? 0 : ((x1 > w) ? w : x1);
          y1 = (y1 < 0) ? 0 : ((y1 > h) ? h : y1);

          /* anything left after clipping? */
          if (x0 != x1 && y0 != y1) {
	    XVMCLOCKDISPLAY( this->display );
	    XvMCCompositeSubpicture( this->display, this->new_subpic,
				     this->subImage,
				     x0, y0, x1 - x0, y1 - y0,
				     x0, y0);
	    XVMCUNLOCKDISPLAY( this->display );
          }
	  xvmc_context_reader_unlock( &this->xvmc_lock );
        }
      }
    } else {
      if (frame->format == XINE_IMGFMT_YV12) {
        _x_blend_yuv(frame->vo_frame.base, overlay,
		  frame->width, frame->height, frame->vo_frame.pitches,
                  &this->alphablend_extra_data);
      } else {
	_x_blend_yuy2(frame->vo_frame.base[0], overlay,
		   frame->width, frame->height, frame->vo_frame.pitches[0],
                   &this->alphablend_extra_data);
      }
    }
  }
}

static void xxmc_add_recent_frame (xxmc_driver_t *this, xxmc_frame_t *frame)
{
  int i;
  i = VO_NUM_RECENT_FRAMES-1;
  if( this->recent_frames[i] ) {
    this->recent_frames[i]->vo_frame.free
      (&this->recent_frames[i]->vo_frame);
  }
  for( ; i ; i-- )
    this->recent_frames[i] = this->recent_frames[i-1];

  this->recent_frames[0] = frame;
}

static int xxmc_redraw_needed (vo_driver_t *this_gen)
{
  xxmc_driver_t  *this = (xxmc_driver_t *) this_gen;
  int           ret  = 0;

  if( this->cur_frame ) {

    this->sc.delivered_height = this->cur_frame->height;
    this->sc.delivered_width  = this->cur_frame->width;
    this->sc.delivered_ratio  = this->cur_frame->ratio;

    this->sc.crop_left        = this->cur_frame->vo_frame.crop_left;
    this->sc.crop_right       = this->cur_frame->vo_frame.crop_right;
    this->sc.crop_top         = this->cur_frame->vo_frame.crop_top;
    this->sc.crop_bottom      = this->cur_frame->vo_frame.crop_bottom;

    xxmc_compute_ideal_size(this);

    if( _x_vo_scale_redraw_needed( &this->sc ) ) {

      xxmc_compute_output_size (this);

      xxmc_clean_output_area
	(this, (this->cur_frame->format == XINE_IMGFMT_XXMC));

      ret = 1;
    }
  }
  else
    ret = 1;

  return ret;
}

static void xxmc_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  xxmc_driver_t  *this  = (xxmc_driver_t *) this_gen;
  xxmc_frame_t   *frame = (xxmc_frame_t *) frame_gen;
  xine_xxmc_t *xxmc = &frame->xxmc_data;
  int first_field;
  int disable_deinterlace = 0;
  struct timeval tv_top;

  /*
   * take time to calculate the time to sleep for the bottom field
   */
  gettimeofday(&tv_top, 0);

  /*
   * bob deinterlacing doesn't make much sense for still images or at replay speeds
   * other than 100 %, so let's disable deinterlacing at all for this frame
   */
  if (this->deinterlace_enabled && this->bob) {
    disable_deinterlace = (this->disable_bob_for_progressive_frames && frame->vo_frame.progressive_frame)
      || (this->disable_bob_for_scaled_osd && this->scaled_osd_active)
      || !frame->vo_frame.stream
      || xine_get_param(frame->vo_frame.stream, XINE_PARAM_FINE_SPEED) != XINE_FINE_SPEED_NORMAL;
    if (!disable_deinterlace) {
      int vo_bufs_in_fifo = 0;
      _x_query_buffer_usage(frame->vo_frame.stream, NULL, NULL, &vo_bufs_in_fifo, NULL);
      disable_deinterlace = (vo_bufs_in_fifo <= 0);
    }
  }

  /*
   * reset this flag now -- it will be set again before the next call to
   * xxmc_display_frame() as long as there is a scaled OSD active on screen.
   */
  this->scaled_osd_active = 0;

  /*
   * queue frames (deinterlacing)
   * free old frames
   */

  xvmc_context_reader_lock( &this->xvmc_lock );

  /*
   * the current implementation doesn't need recent frames for deinterlacing,
   * but we need to hold references on the frame we are about to show and to
   * the previous frame which is currently shown on screen. Otherwise, the
   * frame on screen will be immediately reused for decoding which will then
   * most often result in mixed images on screen, especially when decoding
   * is faster than sending the image to the monitor, and/or when exchanging
   * the overlay image is synced to retrace.
   */
  xxmc_add_recent_frame (this, frame); /* deinterlacing */

  if ((frame->format == XINE_IMGFMT_XXMC) &&
      (!xxmc->decoded || !xxmc_xvmc_surface_valid(this, frame->xvmc_surf))) {
    xvmc_context_reader_unlock( &this->xvmc_lock );
    return;
  }

  this->cur_frame = frame;


  /*
   * let's see if this frame is different in size / aspect
   * ratio from the previous one
   */
  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio)
       || (frame->vo_frame.crop_left != this->sc.crop_left)
       || (frame->vo_frame.crop_right != this->sc.crop_right)
       || (frame->vo_frame.crop_top != this->sc.crop_top)
       || (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    lprintf("frame format changed\n");
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */

  first_field = (frame->vo_frame.top_field_first) ? XVMC_TOP_FIELD : XVMC_BOTTOM_FIELD;
  first_field = (this->bob) ? first_field : XVMC_TOP_FIELD;
  this->cur_field = (this->deinterlace_enabled && !disable_deinterlace) ? first_field : XVMC_FRAME_PICTURE;

  xxmc_redraw_needed (this_gen);
  if (frame->format == XINE_IMGFMT_XXMC) {
    XVMCLOCKDISPLAY( this->display );
    XvMCSyncSurface( this->display, frame->xvmc_surf );
    XLockDisplay( this->display ); /* blocks XINE_GUI_SEND_DRAWABLE_CHANGED from changing drawable */
    XvMCPutSurface( this->display, frame->xvmc_surf , this->drawable,
		    this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		    this->sc.displayed_width, this->sc.displayed_height,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height,
		    this->cur_field);
    XUnlockDisplay( this->display ); /* unblocks XINE_GUI_SEND_DRAWABLE_CHANGED from changing drawable */
    XVMCUNLOCKDISPLAY( this->display );
    if (this->deinterlace_enabled && !disable_deinterlace && this->bob) {
      struct timeval tv_middle;
      long us_spent_so_far, us_per_field = frame->vo_frame.duration * 50 / 9;

      gettimeofday(&tv_middle, 0);
      us_spent_so_far = (tv_middle.tv_sec - tv_top.tv_sec) * 1000000 + (tv_middle.tv_usec - tv_top.tv_usec);
      if (us_spent_so_far < 0)
        us_spent_so_far = 0;

      /*
       * typically, the operations above take just a few milliseconds, but when the
       * driver actively waits to sync on the next field, we better skip showing the
       * other field as it would lead to further busy waiting
       * so display the other field only if we've spent less than 75 % of the per
       * field time so far
       */
      if (4 * us_spent_so_far < 3 * us_per_field) {
        long us_delay = (us_per_field - 2000) - us_spent_so_far;
        if (us_delay > 0) {
          xvmc_context_reader_unlock( &this->xvmc_lock );
          xine_usec_sleep(us_delay);
          LOCK_AND_SURFACE_VALID( this, frame->xvmc_surf );
        }

        this->cur_field = (frame->vo_frame.top_field_first) ? XVMC_BOTTOM_FIELD : XVMC_TOP_FIELD;

        XVMCLOCKDISPLAY( this->display );
        XLockDisplay( this->display ); /* blocks XINE_GUI_SEND_DRAWABLE_CHANGED from changing drawable */
        XvMCPutSurface( this->display, frame->xvmc_surf , this->drawable,
		        this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		        this->sc.displayed_width, this->sc.displayed_height,
		        this->sc.output_xoffset, this->sc.output_yoffset,
		        this->sc.output_width, this->sc.output_height,
		        this->cur_field);
        XUnlockDisplay( this->display ); /* unblocks XINE_GUI_SEND_DRAWABLE_CHANGED from changing drawable */
        XVMCUNLOCKDISPLAY( this->display );
      }
    }
  } else {
    XLockDisplay (this->display);
    if (this->use_shm) {
      XvShmPutImage(this->display, this->xv_port,
		    this->drawable, this->gc, frame->image,
		    this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		    this->sc.displayed_width, this->sc.displayed_height,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height, True);

    } else {
      XvPutImage(this->display, this->xv_port,
		 this->drawable, this->gc, frame->image,
		 this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		 this->sc.displayed_width, this->sc.displayed_height,
		 this->sc.output_xoffset, this->sc.output_yoffset,
		 this->sc.output_width, this->sc.output_height);
    }
    XSync(this->display, False);
    XUnlockDisplay (this->display);
  }
  xvmc_context_reader_unlock( &this->xvmc_lock );
}

static int xxmc_get_property (vo_driver_t *this_gen, int property) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  switch (property) {
  case VO_PROP_WINDOW_WIDTH:
    this->props[property].value = this->sc.gui_width;
    break;
  case VO_PROP_WINDOW_HEIGHT:
    this->props[property].value = this->sc.gui_height;
    break;
  case VO_PROP_OUTPUT_WIDTH:
    this->props[property].value = this->sc.output_width;
    break;
  case VO_PROP_OUTPUT_HEIGHT:
    this->props[property].value = this->sc.output_height;
    break;
  case VO_PROP_OUTPUT_XOFFSET:
    this->props[property].value = this->sc.output_xoffset;
    break;
  case VO_PROP_OUTPUT_YOFFSET:
    this->props[property].value = this->sc.output_yoffset;
    break;
  }

  lprintf("%s: property #%d = %d\n", LOG_MODULE, property, this->props[property].value);

  return this->props[property].value;
}

static void xxmc_property_callback (void *property_gen, xine_cfg_entry_t *entry) {
  xxmc_property_t *property = (xxmc_property_t *) property_gen;
  xxmc_driver_t   *this = property->this;

  xvmc_context_reader_lock( &this->xvmc_lock );
  XLockDisplay (this->display);
  XvSetPortAttribute (this->display, this->xv_port,
		      property->atom,
		      entry->num_value);
  XUnlockDisplay (this->display);
  if (this->contextActive) {
    XVMCLOCKDISPLAY( this->display );
    XvMCSetAttribute(this->display, &this->context,
		     property->atom,
		     entry->num_value);
    XVMCUNLOCKDISPLAY( this->display );
  }
  xvmc_context_reader_unlock( &this->xvmc_lock );
}

static int xxmc_set_property (vo_driver_t *this_gen,
			      int property, int value) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if (this->props[property].atom != None) {

    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;
    xvmc_context_reader_lock( &this->xvmc_lock );
    if (this->contextActive) {
      XVMCLOCKDISPLAY( this->display );
      XvMCSetAttribute(this->display, &this->context,
		       this->props[property].atom,
		       value);
      XVMCUNLOCKDISPLAY( this->display );
    }
    xvmc_context_reader_unlock( &this->xvmc_lock );

    XLockDisplay (this->display);
    XvSetPortAttribute (this->display, this->xv_port,
			this->props[property].atom, value);
    XvGetPortAttribute (this->display, this->xv_port,
			this->props[property].atom,
			&this->props[property].value);
    XUnlockDisplay (this->display);

    if (this->props[property].entry)
      this->props[property].entry->num_value = this->props[property].value;

    return this->props[property].value;
  }
  else {
    switch (property) {

    case VO_PROP_INTERLACED:
      this->props[property].value = value;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ": VO_PROP_INTERLACED(%d)\n", this->props[property].value);
      this->deinterlace_enabled = value;
      break;

    case VO_PROP_ASPECT_RATIO:
      if (value>=XINE_VO_ASPECT_NUM_RATIOS)
	value = XINE_VO_ASPECT_AUTO;

      this->props[property].value = value;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      LOG_MODULE ": VO_PROP_ASPECT_RATIO(%d)\n", this->props[property].value);
      this->sc.user_ratio = value;

      xxmc_compute_ideal_size (this);

      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      break;

    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
	xprintf(this->xine, XINE_VERBOSITY_LOG,
		LOG_MODULE ": VO_PROP_ZOOM_X = %d\n", this->props[property].value);

	this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;

	xxmc_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;

    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->props[property].value = value;
	xprintf(this->xine, XINE_VERBOSITY_LOG,
		LOG_MODULE ": VO_PROP_ZOOM_Y = %d\n", this->props[property].value);

	this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;

	xxmc_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    }
  }
  return value;
}

static void xxmc_get_property_min_max (vo_driver_t *this_gen,
				       int property, int *min, int *max) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) {
    *min = *max = 0;
    return;
  }
  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int xxmc_gui_data_exchange (vo_driver_t *this_gen,
				   int data_type, void *data) {

  xxmc_driver_t     *this = (xxmc_driver_t *) this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT: {
    /* XExposeEvent * xev = (XExposeEvent *) data; */

    if (this->cur_frame) {
      xxmc_frame_t *frame = this->cur_frame;
      xine_xxmc_t *xxmc = &frame->xxmc_data;

      xvmc_context_reader_lock( &this->xvmc_lock );
      if ((frame->format == XINE_IMGFMT_XXMC) &&
	  (!xxmc->decoded || !xxmc_xvmc_surface_valid(this, frame->xvmc_surf))) {
	xvmc_context_reader_unlock( &this->xvmc_lock );
	if (! xxmc_redraw_needed (this_gen))
	  xxmc_clean_output_area(this, (frame->format == XINE_IMGFMT_XXMC));
	break;
      }

      if (!xxmc_redraw_needed (this_gen) && !this->xoverlay)
	xxmc_clean_output_area(this,(frame->format == XINE_IMGFMT_XXMC));
      if (frame->format == XINE_IMGFMT_XXMC) {
	XVMCLOCKDISPLAY( this->display );
        XvMCSyncSurface( this->display, frame->xvmc_surf );
	XvMCPutSurface( this->display, frame->xvmc_surf, this->drawable,
			this->sc.displayed_xoffset, this->sc.displayed_yoffset,
			this->sc.displayed_width, this->sc.displayed_height,
			this->sc.output_xoffset, this->sc.output_yoffset,
			this->sc.output_width, this->sc.output_height,
			this->cur_field);
	XVMCUNLOCKDISPLAY( this->display );
      } else {
	XLockDisplay (this->display);
	if (this->use_shm) {
	  XvShmPutImage(this->display, this->xv_port,
			this->drawable, this->gc, frame->image,
			this->sc.displayed_xoffset, this->sc.displayed_yoffset,
			this->sc.displayed_width, this->sc.displayed_height,
			this->sc.output_xoffset, this->sc.output_yoffset,
			this->sc.output_width, this->sc.output_height, True);
	} else {
	  XvPutImage(this->display, this->xv_port,
		     this->drawable, this->gc, frame->image,
		     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		     this->sc.displayed_width, this->sc.displayed_height,
		     this->sc.output_xoffset, this->sc.output_yoffset,
		     this->sc.output_width, this->sc.output_height);
	}
	XSync(this->display, False);
	XUnlockDisplay (this->display);
      }
      xvmc_context_reader_unlock( &this->xvmc_lock );
    }
    if(this->xoverlay)
      x11osd_expose(this->xoverlay);

  }

    break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    XLockDisplay (this->display);
    this->drawable = (Drawable) data;
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC (this->display, this->drawable, 0, NULL);
    if(this->xoverlay)
      x11osd_drawable_changed(this->xoverlay, this->drawable);
    this->ovl_changed = 1;
    XUnlockDisplay (this->display);
    this->sc.force_redraw = 1;
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y,
				      &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h,
				      &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;

    }
    break;

  default:
    return -1;
  }

  return 0;
}

static void xxmc_dispose (vo_driver_t *this_gen) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;
  int          i;


  if (this->xvmc_cap) {
    xvmc_context_writer_lock( &this->xvmc_lock );
    xxmc_dispose_context( this );
    if (this->old_subpic) {
      xxmc_xvmc_free_subpicture(this, this->old_subpic);
      this->old_subpic = NULL;
    }
    if (this->new_subpic) {
      xxmc_xvmc_free_subpicture(this, this->new_subpic);
      this->new_subpic = NULL;
    }
    xvmc_context_writer_unlock( &this->xvmc_lock );
  }

  XLockDisplay (this->display);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": xxmc_exit: XvUngrabPort() failed.\n");
  }
  XFreeGC(this->display, this->gc);
  XUnlockDisplay (this->display);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.free
	(&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  if( this->xoverlay ) {
    XLockDisplay (this->display);
    x11osd_destroy (this->xoverlay);
    XUnlockDisplay (this->display);
  }
  free_context_lock(&this->xvmc_lock);

  _x_alphablend_free(&this->alphablend_extra_data);

  free (this);
}

/* called xlocked */
static int xxmc_check_yv12 (Display *display, XvPortID port) {
  XvImageFormatValues *formatValues;
  int                  formats;
  int                  i;

  formatValues = XvListImageFormats (display, port, &formats);

  for (i = 0; i < formats; i++)
    if ((formatValues[i].id == XINE_IMGFMT_YV12) &&
	(! (strcmp (formatValues[i].guid, "YV12")))) {
      XFree (formatValues);
      return 0;
    }

  XFree (formatValues);
  return 1;
}

/* called xlocked */
static void xxmc_check_capability (xxmc_driver_t *this,
				   int property, XvAttribute attr, int base_id,
				   const char *config_name,
				   const char *config_desc,
				   const char *config_help) {
  int          int_default;
  cfg_entry_t *entry;
  const char  *str_prop = attr.name;

  if (VO_PROP_COLORKEY && (attr.max_value == ~0))
    attr.max_value = 2147483615;

  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);

  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": port attribute %s (%d) value is %d\n", str_prop, property, int_default);

  /*
   * We enable autopaint by default.
   */
  if(strcmp(str_prop, "XV_AUTOPAINT_COLORKEY") == 0)
    int_default = 1;

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr.min_value == 0) && (attr.max_value == 1)) {
      this->config->register_bool (this->config, config_name, int_default,
				   config_desc,
				   config_help, 20, xxmc_property_callback, &this->props[property]);

    } else {
      this->config->register_range (this->config, config_name, int_default,
				    this->props[property].min, this->props[property].max,
				    config_desc,
				    config_help, 20, xxmc_property_callback, &this->props[property]);
    }

    entry = this->config->lookup_entry (this->config, config_name);

    if((entry->num_value < this->props[property].min) ||
       (entry->num_value > this->props[property].max)) {

      this->config->update_num(this->config, config_name,
			       ((this->props[property].min + this->props[property].max) >> 1));

      entry = this->config->lookup_entry (this->config, config_name);
    }

    this->props[property].entry = entry;

    xxmc_set_property (&this->vo_driver, property, entry->num_value);


    if (strcmp(str_prop, "XV_COLORKEY") == 0) {
      this->use_colorkey |= 1;
      this->colorkey = entry->num_value;
    } else if(strcmp(str_prop, "XV_AUTOPAINT_COLORKEY") == 0) {
      if(entry->num_value==1)
        this->use_colorkey |= 2;
    }
  } else
    this->props[property].value  = int_default;
}

static void xxmc_update_attr (void *this_gen, xine_cfg_entry_t *entry,
			    const char *atomstr, const char *debugstr)
{
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;
  Atom atom;

  XLockDisplay(this->display);
  atom = XInternAtom (this->display, atomstr, False);
  XvSetPortAttribute (this->display, this->xv_port, atom, entry->num_value);
  XUnlockDisplay(this->display);

  xprintf(this->xine, XINE_VERBOSITY_DEBUG,
	  LOG_MODULE ": %s = %d\n", debugstr, entry->num_value);
}

static void xxmc_update_XV_FILTER(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_update_attr (this_gen, entry, "XV_FILTER", "bilinear scaling mode");
}

static void xxmc_update_XV_DOUBLE_BUFFER(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_update_attr (this_gen, entry, "XV_DOUBLE_BUFFER", "double buffering mode");
}

static void xxmc_update_XV_BICUBIC(void *this_gen, xine_cfg_entry_t *entry)
{
  xxmc_update_attr (this_gen, entry, "XV_BICUBIC", "bicubic filtering mode");
}

static void xxmc_update_xv_pitch_alignment(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->use_pitch_alignment = entry->num_value;
}

static void xxmc_update_cpu_save(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->cpu_save_enabled = entry->num_value;
}

static void xxmc_update_nvidia_fix(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->reverse_nvidia_palette = entry->num_value;
}

static void xxmc_update_bob(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->bob = entry->num_value;
}

static void xxmc_update_disable_bob_for_progressive_frames(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->disable_bob_for_progressive_frames = entry->num_value;
}

static void xxmc_update_disable_bob_for_scaled_osd(void *this_gen, xine_cfg_entry_t *entry) {
  xxmc_driver_t *this = (xxmc_driver_t *) this_gen;

  this->disable_bob_for_scaled_osd = entry->num_value;
}

static int xxmc_open_port (xxmc_driver_t *this, XvPortID port) {
  int ret;
  x11_InstallXErrorHandler (this);
  ret = ! xxmc_check_yv12(this->display, port)
    && XvGrabPort(this->display, port, 0) == Success;
  x11_DeInstallXErrorHandler (this);
  return ret;
}

static unsigned int
xxmc_find_adaptor_by_port (int port, unsigned int adaptors,
			 XvAdaptorInfo *adaptor_info)
{
  unsigned int an;
  for (an = 0; an < adaptors; an++)
    if (adaptor_info[an].type & XvImageMask)
      if (port >= adaptor_info[an].base_id &&
	  port < adaptor_info[an].base_id + adaptor_info[an].num_ports)
	return an;
  return 0; /* shouldn't happen */
}

static XvPortID xxmc_autodetect_port(xxmc_driver_t *this,
				   unsigned int adaptors,
				   XvAdaptorInfo *adaptor_info,
				   unsigned int *adaptor_num,
				   XvPortID base,
				   xv_prefertype prefer_type)
{
  unsigned int an, j;

  for (an = 0; an < adaptors; an++)
    if (adaptor_info[an].type & XvImageMask &&
        (prefer_type == xv_prefer_none ||
         strcasestr (adaptor_info[an].name, prefer_substrings[prefer_type])))
      for (j = 0; j < adaptor_info[an].num_ports; j++) {
	XvPortID port = adaptor_info[an].base_id + j;
	if (port >= base && xxmc_open_port(this, port)) {
	  *adaptor_num = an;
	  return port;
	}
      }

  return 0;
}


static void checkXvMCCap( xxmc_driver_t *this, XvPortID xv_port)
{
  int
    numSurf,numSub,i,j;
  XvMCSurfaceInfo
    *surfaceInfo,*curInfo;
  XvMCContext
    c;
  xvmc_capabilities_t
    *curCap;
  XvImageFormatValues
    *formatValues;

  this->xvmc_cap = 0;
  init_context_lock( &this->xvmc_lock );
  xvmc_context_writer_lock( &this->xvmc_lock );
  this->old_subpic = NULL;
  this->new_subpic = NULL;
  this->contextActive = 0;
  this->subImage = NULL;
  this->hwSubpictures = 0;
  this->xvmc_palette = NULL;

  XVMCLOCKDISPLAY( this->display );

  if ( !XvMCQueryExtension(this->display, &this->xvmc_eventbase,
			   &this->xvmc_errbase)) {
    XVMCUNLOCKDISPLAY( this->display );
    xvmc_context_writer_unlock( &this->xvmc_lock );
    return;
  }
  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   LOG_MODULE ": XvMC extension present.\n");

  surfaceInfo = XvMCListSurfaceTypes(this->display, xv_port, &numSurf);
  if (0 == surfaceInfo) {
    XVMCUNLOCKDISPLAY( this->display );
    xvmc_context_writer_unlock( &this->xvmc_lock );
    return;
  }
  this->xvmc_cap = (xvmc_capabilities_t *)
    xine_xmalloc(numSurf * sizeof(xvmc_capabilities_t));
  if (NULL == this->xvmc_cap) return;
  this->xvmc_num_cap = numSurf;
  curInfo = surfaceInfo;
  curCap = this->xvmc_cap;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   LOG_MODULE ": Found %d XvMC surface types\n", numSurf);

  for (i=0; i< numSurf; ++i) {
    curCap->mpeg_flags = 0;
    curCap->accel_flags = 0;
    if (curInfo->chroma_format == XVMC_CHROMA_FORMAT_420) {
      curCap->mpeg_flags |= ((curInfo->mc_type & XVMC_MPEG_1) ?
			     XINE_XVMC_MPEG_1 : 0);
      curCap->mpeg_flags |= ((curInfo->mc_type & XVMC_MPEG_2) ?
			     XINE_XVMC_MPEG_2 : 0);
      curCap->mpeg_flags |= ((curInfo->mc_type & XVMC_MPEG_4) ?
			     XINE_XVMC_MPEG_4 : 0);
      curCap->accel_flags |= ((curInfo->mc_type & XVMC_VLD) ?
			      XINE_XVMC_ACCEL_VLD : 0);
      curCap->accel_flags |= ((curInfo->mc_type & XVMC_IDCT) ?
			      XINE_XVMC_ACCEL_IDCT : 0);
      curCap->accel_flags |= ((curInfo->mc_type & (XVMC_VLD | XVMC_IDCT)) ?
			      0 : XINE_XVMC_ACCEL_MOCOMP);
      curCap->max_width = curInfo->max_width;
      curCap->max_height = curInfo->max_height;
      curCap->sub_max_width = curInfo->subpicture_max_width;
      curCap->sub_max_height = curInfo->subpicture_max_height;
      curCap->flags = curInfo->flags;
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       LOG_MODULE ": Surface type %d: Max size: %d %d.\n",
	       i,curCap->max_width,curCap->max_height);
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	       LOG_MODULE ": Surface type %d: Max subpic size: %d %d.\n",
	       i,curCap->sub_max_width,curCap->sub_max_height);

      curCap->type_id = curInfo->surface_type_id;
      formatValues = XvMCListSubpictureTypes( this->display, xv_port,
					      curCap->type_id, &numSub);
      curCap->subPicType.id = 0;
      if (formatValues) {
	xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		 LOG_MODULE ": Surface type %d: Found %d XvMC subpicture types\n",i,numSub);
	for (j = 0; j<numSub; ++j) {
	  if (formatValues[j].id == FOURCC_IA44) {
	    curCap->subPicType = formatValues[j];
	    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		     LOG_MODULE ": Surface type %d: Detected and using IA44 subpicture type.\n",i);
	    /* Prefer IA44 */
	    break;
	  } else if (formatValues[j].id == FOURCC_AI44) {
	    curCap->subPicType = formatValues[j];
	    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
		     LOG_MODULE ": Surface type %d: Detected AI44 subpicture type.\n",i);
	  }
	}
      }

      XFree(formatValues);

      curInfo++;
      curCap++;
    }
  }
  XFree(surfaceInfo);

  /*
   * Try to create a direct rendering context. This will fail if we are not
   * on the displaying computer or an indirect context is not available.
   */

  curCap = this->xvmc_cap;
  if (Success == XvMCCreateContext( this->display, xv_port, curCap->type_id,
				    curCap->max_width,curCap->max_height,
				    XVMC_DIRECT, &c)) {
    this->context_flags = XVMC_DIRECT;
  } else if (Success == XvMCCreateContext( this->display, xv_port, curCap->type_id,
					   curCap->max_width,curCap->max_height,
					   0, &c)) {
    this->context_flags = 0;
  } else {
    free(this->xvmc_cap);
    this->xvmc_cap = 0;
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	     LOG_MODULE ": Apparent attempt to use a direct XvMC context on a remote display.\n"
	     LOG_MODULE ": Falling back to Xv.\n");
    XVMCUNLOCKDISPLAY( this->display );
    xvmc_context_writer_unlock( &this->xvmc_lock );
    return;
  }
  XvMCDestroyContext( this->display, &c);
  xxmc_xvmc_surface_handler_construct(this);
  this->capabilities |= VO_CAP_XXMC;
  XVMCUNLOCKDISPLAY( this->display );
  _x_init_xx44_palette( &this->palette , 0);
  this->last_accel_request = 0xFFFFFFFF;
  xvmc_context_writer_unlock( &this->xvmc_lock );
  return;

}




static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  xxmc_class_t           *class = (xxmc_class_t *) class_gen;
  config_values_t      *config = class->config;
  xxmc_driver_t          *this;
  int                   i, formats;
  XvAttribute          *attr;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XColor                dummy;
  XvImage              *myimage;
  unsigned int          adaptors;
  unsigned int          ver,rel,req,ev,err;
  XShmSegmentInfo       myshminfo;
  XvPortID              xv_port;
  XvAdaptorInfo        *adaptor_info;
  unsigned int          adaptor_num;
  xv_prefertype		prefer_type;
  cfg_entry_t          *entry;
  int                   use_more_frames;
  int                   use_unscaled;

  this = calloc(1, sizeof (xxmc_driver_t));
  if (!this)
    return NULL;

  _x_alphablend_init(&this->alphablend_extra_data, class->xine);

  this->display           = visual->display;
  this->screen            = visual->screen;
  this->config            = config;

  /*
   * check for Xvideo support
   */

  XLockDisplay(this->display);
  if (Success != XvQueryExtension(this->display, &ver,&rel, &req, &ev,&err)) {
    xprintf (class->xine, XINE_VERBOSITY_LOG, _("%s: Xv extension not present.\n"), LOG_MODULE);
    XUnlockDisplay(this->display);
    return NULL;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if (Success != XvQueryAdaptors(this->display,DefaultRootWindow(this->display), &adaptors, &adaptor_info))  {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": XvQueryAdaptors failed.\n");
    XUnlockDisplay(this->display);
    return NULL;
  }

  xv_port = config->register_num (config, "video.device.xv_port", 0,
				  VIDEO_DEVICE_XV_PORT_HELP,
				  20, NULL, NULL);
  prefer_type = config->register_enum (config, "video.device.xv_preferred_method", 0,
				       (char **)prefer_labels, VIDEO_DEVICE_XV_PREFER_TYPE_HELP,
				       10, NULL, NULL);

  if (xv_port != 0) {
    if (! xxmc_open_port(this, xv_port)) {
      xprintf(class->xine, XINE_VERBOSITY_NONE,
	      _("%s: could not open Xv port %lu - autodetecting\n"),
	      LOG_MODULE, (unsigned long)xv_port);
      xv_port = xxmc_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, xv_port, prefer_type);
    } else
      adaptor_num = xxmc_find_adaptor_by_port (xv_port, adaptors, adaptor_info);
  }
  if (!xv_port)
    xv_port = xxmc_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, 0, prefer_type);
  if (!xv_port)
  {
    if (prefer_type)
      xprintf(class->xine, XINE_VERBOSITY_NONE,
	      _("%s: no available ports of type \"%s\", defaulting...\n"),
	      LOG_MODULE, prefer_labels[prefer_type]);
    xv_port = xxmc_autodetect_port(this, adaptors, adaptor_info, &adaptor_num, 0, xv_prefer_none);
  }

  if (!xv_port) {
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: Xv extension is present but I couldn't find a usable yuv12 port.\n"
	      "\tLooks like your graphics hardware driver doesn't support Xv?!\n"),
	    LOG_MODULE);

    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    XUnlockDisplay(this->display);
    return NULL;
  }
  else
    xprintf(class->xine, XINE_VERBOSITY_LOG,
	    _("%s: using Xv port %ld from adaptor %s for hardware "
	      "colour space conversion and scaling.\n"), LOG_MODULE, xv_port,
            adaptor_info[adaptor_num].name);

  XUnlockDisplay(this->display);

  this->xv_port           = xv_port;

  _x_vo_scale_init (&this->sc, 1, 0, config );
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  this->drawable                = visual->d;
  XLockDisplay (this->display);
  this->gc                      = XCreateGC (this->display, this->drawable, 0, NULL);
  XUnlockDisplay (this->display);
  this->capabilities            = VO_CAP_CROP | VO_CAP_ZOOM_X | VO_CAP_ZOOM_Y;
  this->use_shm                 = 1;
  this->use_colorkey            = 0;
  this->colorkey                = 0;
  this->xoverlay                = NULL;
  this->ovl_changed             = 0;
  this->x11_old_error_handler   = NULL;
  this->xine                    = class->xine;

  XLockDisplay (this->display);
  XAllocNamedColor (this->display,
		    DefaultColormap(this->display, this->screen),
		    "black", &this->black, &dummy);
  XUnlockDisplay (this->display);

  this->vo_driver.get_capabilities     = xxmc_get_capabilities;
  this->vo_driver.alloc_frame          = xxmc_alloc_frame;
  this->vo_driver.update_frame_format  = xxmc_update_frame_format;
  this->vo_driver.overlay_begin        = xxmc_overlay_begin;
  this->vo_driver.overlay_blend        = xxmc_overlay_blend;
  this->vo_driver.overlay_end          = xxmc_overlay_end;
  this->vo_driver.display_frame        = xxmc_display_frame;
  this->vo_driver.get_property         = xxmc_get_property;
  this->vo_driver.set_property         = xxmc_set_property;
  this->vo_driver.get_property_min_max = xxmc_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xxmc_gui_data_exchange;
  this->vo_driver.dispose              = xxmc_dispose;
  this->vo_driver.redraw_needed        = xxmc_redraw_needed;

  /*
   * init properties
   */

  for (i = 0; i < VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = None;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  this->props[VO_PROP_INTERLACED].value      = 0;
  this->sc.user_ratio                        =
    this->props[VO_PROP_ASPECT_RATIO].value  = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value          = 100;
  this->props[VO_PROP_ZOOM_Y].value          = 100;

  /*
   * check this adaptor's capabilities
   */

  XLockDisplay (this->display);
  attr = XvQueryPortAttributes(this->display, xv_port, &nattr);
  if(attr && nattr) {
    int k;

    for(k = 0; k < nattr; k++) {
      if((attr[k].flags & XvSettable) && (attr[k].flags & XvGettable)) {
	const char *const name = attr[k].name;
	if(!strcmp(name, "XV_HUE")) {
	  if (!strncmp(adaptor_info[adaptor_num].name, "NV", 2)) {
            xprintf (this->xine, XINE_VERBOSITY_NONE, LOG_MODULE ": ignoring broken XV_HUE settings on NVidia cards\n");
	  } else {
	    this->capabilities |= VO_CAP_HUE;
	    xxmc_check_capability (this, VO_PROP_HUE, attr[k],
				   adaptor_info[adaptor_num].base_id,
				   NULL, NULL, NULL);
	  }
	} else if(!strcmp(name, "XV_SATURATION")) {
	  this->capabilities |= VO_CAP_SATURATION;
	  xxmc_check_capability (this, VO_PROP_SATURATION, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_BRIGHTNESS")) {
	  this->capabilities |= VO_CAP_BRIGHTNESS;
	  xxmc_check_capability (this, VO_PROP_BRIGHTNESS, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);
	} else if(!strcmp(name, "XV_CONTRAST")) {
	  this->capabilities |= VO_CAP_CONTRAST;
	  xxmc_check_capability (this, VO_PROP_CONTRAST, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);

	} else if(!strcmp(name, "XV_GAMMA")) {
	  this->capabilities |= VO_CAP_GAMMA;
	  xxmc_check_capability (this, VO_PROP_GAMMA, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 NULL, NULL, NULL);

	} else if(!strcmp(name, "XV_COLORKEY")) {
	  this->capabilities |= VO_CAP_COLORKEY;
	  xxmc_check_capability (this, VO_PROP_COLORKEY, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 "video.device.xv_colorkey",
				 VIDEO_DEVICE_XV_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_AUTOPAINT_COLORKEY")) {
	  this->capabilities |= VO_CAP_AUTOPAINT_COLORKEY;
	  xxmc_check_capability (this, VO_PROP_AUTOPAINT_COLORKEY, attr[k],
				 adaptor_info[adaptor_num].base_id,
				 "video.device.xv_autopaint_colorkey",
				 VIDEO_DEVICE_XV_AUTOPAINT_COLORKEY_HELP);
	} else if(!strcmp(name, "XV_FILTER")) {
	  int xv_filter;
	  /* This setting is specific to Permedia 2/3 cards. */
	  xv_filter = config->register_range (config, "video.device.xv_filter", 0,
					      attr[k].min_value, attr[k].max_value,
					      VIDEO_DEVICE_XV_FILTER_HELP,
					      20, xxmc_update_XV_FILTER, this);
	  config->update_num(config,"video.device.xv_filter",xv_filter);
	} else if(!strcmp(name, "XV_DOUBLE_BUFFER")) {
	  int xv_double_buffer =
	    config->register_bool (config, "video.device.xv_double_buffer", 1,
				   VIDEO_DEVICE_XV_DOUBLE_BUFFER_HELP,
				   20, xxmc_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.device.xv_double_buffer",xv_double_buffer);
	} else if(!strcmp(name, "XV_BICUBIC")) {
	  int xv_bicubic =
	    config->register_enum (config, "video.device.xv_bicubic", 2,
				   (char **)bicubic_types, VIDEO_DEVICE_XV_BICUBIC_HELP,
				   20, xxmc_update_XV_BICUBIC, this);
	  config->update_num(config,"video.device.xv_bicubic",xv_bicubic);
	}
      }
    }
    XFree(attr);
  }
  else
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": no port attributes defined.\n");
  XvFreeAdaptorInfo(adaptor_info);

  /*
   * check XvMC capabilities
   */

  checkXvMCCap( this, xv_port );

  /*
   * check supported image formats
   */


  fo = XvListImageFormats(this->display, this->xv_port, (int*)&formats);
  XUnlockDisplay (this->display);

  this->xv_format_yv12 = 0;
  this->xv_format_yuy2 = 0;

  for(i = 0; i < formats; i++) {
    lprintf ("Xv image format: 0x%x (%4.4s) %s\n",
	     fo[i].id, (char*)&fo[i].id,
	     (fo[i].format == XvPacked) ? "packed" : "planar");

    switch (fo[i].id) {
    case XINE_IMGFMT_YV12:
      this->xv_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YV12");
      break;
    case XINE_IMGFMT_YUY2:
      this->xv_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      xprintf(this->xine, XINE_VERBOSITY_LOG,
	      _("%s: this adaptor supports the %s format.\n"), LOG_MODULE, "YUY2");
      break;
    default:
      break;
    }
  }

  if(fo) {
    XLockDisplay(this->display);
    XFree(fo);
    XUnlockDisplay(this->display);
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works, using supported format
   */

  XLockDisplay (this->display);
  myimage = create_ximage (this, &myshminfo, 100, 100,
			   (this->xv_format_yv12 != 0) ? XINE_IMGFMT_YV12 : XINE_IMGFMT_YUY2);
  dispose_ximage (this, &myshminfo, myimage);
  XUnlockDisplay (this->display);

  this->use_pitch_alignment =
    config->register_bool (config, "video.device.xv_pitch_alignment", 0,
			   VIDEO_DEVICE_XV_PITCH_ALIGNMENT_HELP,
			   10, xxmc_update_xv_pitch_alignment, this);

  use_more_frames=
    config->register_bool (config, "video.device.xvmc_more_frames", 0,
			   _("Make XvMC allocate more frames for better buffering."),
			   _("Some XvMC implementations allow more than 8 frames.\n"
			     "This option, when turned on, makes the driver try to\n"
			     "allocate 15 frames. A must for unichrome and live VDR.\n"),
			   10, NULL, this);
  this->cpu_save_enabled =
    config->register_bool (config, "video.device.unichrome_cpu_save", 0,
			   _("Unichrome cpu save"),
			   _("Saves CPU time by sleeping while decoder works.\n"
			     "Only for Linux kernel 2.6 series or 2.4 with multimedia patch.\n"
			     "Experimental.\n"),
			   10, xxmc_update_cpu_save, this);
  this->reverse_nvidia_palette =
    config->register_bool (config, "video.device.xvmc_nvidia_color_fix", 0,
			   _("Fix buggy NVIDIA XvMC subpicture colours"),
			   _("There's a bug in NVIDIA's XvMC lib that makes red OSD colours\n"
			     "look blue and vice versa. This option provides a workaround.\n"),
			   10, xxmc_update_nvidia_fix, this);
  this->bob =
    config->register_bool (config, "video.device.xvmc_bob_deinterlacing", 0,
			   _("Use bob as accelerated deinterlace method."),
			   _("When interlacing is enabled for hardware accelerated frames,\n"
			     "alternate between top and bottom field at double the frame rate.\n"),
			   10, xxmc_update_bob, this);

  this->disable_bob_for_progressive_frames =
    config->register_bool (config, "video.device.xvmc_disable_bob_deinterlacing_for_progressive_frames", 0,
			   _("Don't use bob deinterlacing for progressive frames."),
			   _("Progressive frames don't need deinterlacing, so disabling it on\n"
			     "demand should result in a better picture.\n"),
			   10, xxmc_update_disable_bob_for_progressive_frames, this);

  this->disable_bob_for_scaled_osd =
    config->register_bool (config, "video.device.xvmc_disable_bob_deinterlacing_for_scaled_osd", 0,
			   _("Don't use bob deinterlacing while a scaled OSD is active."),
			   _("Bob deinterlacing adds some noise to horizontal lines, so disabling it\n"
                             "on demand should result in a better OSD picture.\n"),
			   10, xxmc_update_disable_bob_for_scaled_osd, this);

  this->deinterlace_enabled = 0;
  this->cur_field = XVMC_FRAME_PICTURE;

#ifdef HAVE_VLDXVMC
  printf("%s: Unichrome CPU saving is %s.\n", LOG_MODULE,
	 (this->cpu_save_enabled) ? "on":"off");
#else
  printf("%s: warning - compiled with no vld extensions.\n", LOG_MODULE);
#endif
  this->props[VO_PROP_MAX_NUM_FRAMES].value  = (use_more_frames) ? 15:8;
  this->cpu_saver = 0.;

  this->xoverlay = NULL;

  use_unscaled = 1;
  entry = this->config->lookup_entry (this->config, "gui.osd_use_unscaled");
  if (entry) use_unscaled = entry->num_value;
  if (use_unscaled) {
    XLockDisplay (this->display);
    if( this->use_colorkey ) {
      this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
				      this->drawable, X11OSD_COLORKEY);
      this->xv_xoverlay_type = X11OSD_COLORKEY;
      if(this->xoverlay)
	x11osd_colorkey(this->xoverlay, this->colorkey, &this->sc);
      this->xoverlay_type = X11OSD_COLORKEY;
    } else {
      this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
				      this->drawable, X11OSD_SHAPED);
      this->xv_xoverlay_type = X11OSD_SHAPED;
      this->xoverlay_type = X11OSD_SHAPED;
    }
    XUnlockDisplay (this->display);
  }

  if( this->xoverlay ) {
    this->capabilities |= VO_CAP_UNSCALED_OVERLAY;
  }
  return &this->vo_driver;
}

/*
 * class functions
 */
static void *init_class (xine_t *xine, void *visual_gen) {
  xxmc_class_t        *this = calloc(1, sizeof (xxmc_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.identifier      = "XxMC";
  this->driver_class.description     = N_("xine video output plugin using the MIT X video extension");
  this->driver_class.dispose         = default_video_driver_class_dispose;

  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static const vo_info_t vo_info_xxmc = {
  /* keep priority lower than Xv for now. we may increase this
   * when the xxmc driver is more mature/tested.
   */
  5,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "xxmc", XINE_VERSION_CODE, &vo_info_xxmc, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

