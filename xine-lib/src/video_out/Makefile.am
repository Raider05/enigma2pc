include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS    = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS   = $(xineplug_ldflags)
AM_OBJCFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

SUBDIRS =

if ENABLE_MACOSX_VIDEO
SUBDIRS += macosx
endif

EXTRA_DIST = video_out_directx.c video_out_macosx.m color_matrix.c

noinst_HEADERS = yuv2rgb.h x11osd.h xcbosd.h xv_common.h

if HAVE_X11
X11OSD = x11osd.c
xshm_module = xineplug_vo_out_xshm.la
if HAVE_XV
xv_module = xineplug_vo_out_xv.la
endif
if ENABLE_XVMC
xvmc_module = xineplug_vo_out_xvmc.la
endif
if ENABLE_XXMC
xxmc_module = xineplug_vo_out_xxmc.la
endif
if ENABLE_OPENGL
opengl_module = xineplug_vo_out_opengl.la
if ENABLE_OPENGL2
opengl2_module = xineplug_vo_out_opengl2.la
endif
endif
if ENABLE_SUNFB
if ENABLE_SUNDGA
pgx64_module = xineplug_vo_out_pgx64.la
pgx32_module = xineplug_vo_out_pgx32.la
endif
endif
endif

if ENABLE_VDPAU
vdpau_module = xineplug_vo_out_vdpau.la
endif

if ENABLE_VAAPI
vaapi_module = xineplug_vo_out_vaapi.la
endif

if ENABLE_XCB
XCBOSD = xcbosd.c
if ENABLE_XCBSHM
xcbshm_module = xineplug_vo_out_xcbshm.la
endif
if ENABLE_XCBXV
xcbxv_module = xineplug_vo_out_xcbxv.la
endif
endif

if ENABLE_VIDIX
vidix_module = xineplug_vo_out_vidix.la
$(top_builddir)/contrib/vidix/libvidix.la:
	$(MAKE) -C $(top_builddir)/contrib/vidix libvidix.la
endif

if ENABLE_AA
aa_module = xineplug_vo_out_aa.la
endif

if ENABLE_CACA
caca_module = xineplug_vo_out_caca.la
endif

if ENABLE_FB
fb_module = xineplug_vo_out_fb.la
endif

if ENABLE_DIRECTFB
directfb_module = xineplug_vo_out_directfb.la
if HAVE_X11
directfb_module += xineplug_vo_out_xdirectfb.la
endif
endif

if ENABLE_SDL
sdl_module = xineplug_vo_out_sdl.la
endif

if ENABLE_MMAL
mmal_module = xineplug_vo_out_mmal.la
endif

if ENABLE_STK
stk_module = xineplug_vo_out_stk.la
endif

if ENABLE_DIRECTX
directx_module = xineplug_vo_out_directx.la
endif

if ENABLE_MACOSX_VIDEO
macosx_module = xineplug_vo_out_macosx.la
endif

noinst_LTLIBRARIES = libyuv2rgb.la

libyuv2rgb_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c
libyuv2rgb_la_CFLAGS = $(AM_CFLAGS) $(MLIB_CFLAGS) $(AVUTIL_CFLAGS)

# All YUV lib info here to avoid polluting the .la with this info
YUV_LIBS = libyuv2rgb.la $(XINE_LIB) $(MLIB_LIBS) $(AVUTIL_LIBS)

xineplug_LTLIBRARIES = $(xshm_module) $(xv_module) $(xvmc_module) \
                  $(opengl_module) \
                  $(opengl2_module) \
                  $(pgx64_module) $(pgx32_module)\
	          $(vidix_module) \
	          $(aa_module) \
	          $(fb_module) $(directfb_module) \
	          $(sdl_module) \
                  $(stk_module) \
                  $(directx_module) \
                  $(caca_module) \
		  $(macosx_module) \
		  $(xxmc_module) \
		  $(xcbshm_module) \
		  $(xcbxv_module) \
		  $(vdpau_module) \
		  $(vaapi_module) \
		  $(mmal_module) \
                  xineplug_vo_out_raw.la \
                  xineplug_vo_out_none.la

xineplug_vo_out_vdpau_la_SOURCES = video_out_vdpau.c
xineplug_vo_out_vdpau_la_LIBADD = $(XINE_LIB) $(AVUTIL_LIBS) $(PTHREAD_LIBS) $(X_LIBS) $(LTLIBINTL) $(VDPAU_LIBS) -lm
xineplug_vo_out_vdpau_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(VDPAU_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_vaapi_la_SOURCES = $(X11OSD) video_out_vaapi.c
xineplug_vo_out_vaapi_la_LIBADD = $(XINE_LIB) $(OPENGL_LIBS) $(FFMPEG_LIBS) $(AVUTIL_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL) $(DYNAMIC_LD_LIBS) -lGLU -lva-glx -lva-x11 -lva
xineplug_vo_out_vaapi_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(XV_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_xcbshm_la_SOURCES = video_out_xcbshm.c $(XCBOSD)
xineplug_vo_out_xcbshm_la_LIBADD = $(YUV_LIBS) $(PTHREAD_LIBS) $(XCB_LIBS) $(XCBSHM_LIBS) $(LTLIBINTL)
xineplug_vo_out_xcbshm_la_CFLAGS = $(AM_CFLAGS) $(XCB_CFLAGS) $(XCBSHM_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_xcbxv_la_SOURCES = video_out_xcbxv.c $(XCBOSD)
xineplug_vo_out_xcbxv_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) $(XCBXV_LIBS) $(XCB_LIBS)
xineplug_vo_out_xcbxv_la_CFLAGS = $(AM_CFLAGS) $(XCB_CFLAGS) $(XCBXV_CFLAGS)

xineplug_vo_out_xshm_la_SOURCES = video_out_xshm.c $(X11OSD)
xineplug_vo_out_xshm_la_LIBADD = $(YUV_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_xshm_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_xv_la_SOURCES = $(X11OSD) video_out_xv.c
xineplug_vo_out_xv_la_LIBADD = $(XINE_LIB) $(XV_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_xv_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(XV_CFLAGS)

xineplug_vo_out_xvmc_la_SOURCES = video_out_xvmc.c
xineplug_vo_out_xvmc_la_LIBADD = $(XINE_LIB) $(XVMC_LIBS) $(XV_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_xvmc_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(XV_CFLAGS) -fno-strict-aliasing

xineplug_vo_out_xxmc_la_SOURCES = $(X11OSD) video_out_xxmc.c\
				xvmc_mocomp.c xvmc_vld.c xxmc.h
xineplug_vo_out_xxmc_la_LIBADD = $(XINE_LIB) $(XXMC_LIBS) $(XV_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_xxmc_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(XV_CFLAGS) -fno-strict-aliasing

xineplug_vo_out_opengl_la_SOURCES = video_out_opengl.c myglext.h $(X11OSD)
xineplug_vo_out_opengl_la_LIBADD = $(YUV_LIBS) $(OPENGL_LIBS) $(GLUT_LIBS) \
	$(GLUT_LIBS) $(GLU_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(DYNAMIC_LD_LIBS) $(LTLIBINTL)
xineplug_vo_out_opengl_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_opengl2_la_SOURCES = video_out_opengl2.c
xineplug_vo_out_opengl2_la_LIBADD = $(XINE_LIB) $(AVUTIL_LIBS) $(OPENGL_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(DYNAMIC_LD_LIBS) $(LTLIBINTL)
xineplug_vo_out_opengl2_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_pgx64_la_SOURCES = video_out_pgx64.c
xineplug_vo_out_pgx64_la_LIBADD = $(XINE_LIB) $(X_LIBS) $(SUNDGA_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_pgx64_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS)
xineplug_vo_out_pgx64_la_CPPFLAGS = $(AM_CPPFLAGS) $(SUNDGA_CPPFLAGS)

xineplug_vo_out_pgx32_la_SOURCES = video_out_pgx32.c
xineplug_vo_out_pgx32_la_LIBADD = $(XINE_LIB) $(X_LIBS) $(SUNDGA_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_pgx32_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS)
xineplug_vo_out_pgx32_la_CPPFLAGS = $(AM_CPPFLAGS) $(SUNDGA_CPPFLAGS)

xineplug_vo_out_vidix_la_SOURCES = video_out_vidix.c $(X11OSD)
xineplug_vo_out_vidix_la_LIBADD = $(XINE_LIB)  $(X_LIBS) \
	$(top_builddir)/contrib/vidix/libvidix.la $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_vidix_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) -fno-strict-aliasing
xineplug_vo_out_vidix_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_builddir)/contrib/vidix \
	-I$(top_srcdir)/contrib/vidix


xineplug_vo_out_aa_la_SOURCES =  video_out_aa.c
xineplug_vo_out_aa_la_LIBADD = $(XINE_LIB) $(AALIB_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL) $(AVUTIL_LIBS)
xineplug_vo_out_aa_la_CFLAGS = $(AM_CFLAGS) $(AALIB_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_caca_la_SOURCES = video_out_caca.c
xineplug_vo_out_caca_la_LIBADD = $(YUV_LIBS) $(CACA_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_caca_la_CFLAGS = $(AM_CFLAGS) $(CACA_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_fb_la_SOURCES = video_out_fb.c
xineplug_vo_out_fb_la_LIBADD = $(YUV_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_fb_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_directfb_la_SOURCES = video_out_directfb.c
xineplug_vo_out_directfb_la_LIBADD = $(XINE_LIB) $(DIRECTFB_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_directfb_la_CFLAGS = $(AM_CFLAGS) $(DIRECTFB_CFLAGS) -fno-strict-aliasing

xineplug_vo_out_xdirectfb_la_SOURCES = video_out_directfb.c $(X11OSD)
xineplug_vo_out_xdirectfb_la_LIBADD = $(XINE_LIB) $(DIRECTFB_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_xdirectfb_la_CFLAGS = $(AM_CFLAGS) $(DIRECTFB_CFLAGS) -fno-strict-aliasing -DDIRECTFB_X11

xineplug_vo_out_sdl_la_SOURCES = video_out_sdl.c
xineplug_vo_out_sdl_la_LIBADD = $(XINE_LIB) $(SDL_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_sdl_la_CFLAGS = $(AM_CFLAGS) $(X_CFLAGS) $(SDL_CFLAGS)

xineplug_vo_out_mmal_la_SOURCES = video_out_mmal.c
xineplug_vo_out_mmal_la_LIBADD = $(XINE_LIB) $(MMAL_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_mmal_la_CFLAGS = $(AM_CFLAGS) $(MMAL_CFLAGS)
xineplug_vo_out_mmal_la_LDFLAGS = $(AM_LDFLAGS) $(MMAL_LDFLAGS)

xineplug_vo_out_stk_la_SOURCES = video_out_stk.c
xineplug_vo_out_stk_la_LIBADD = $(XINE_LIB) $(LIBSTK_LIBS) $(PTHREAD_LIBS)
xineplug_vo_out_stk_la_CFLAGS = $(AM_CFLAGS) $(LIBSTK_CFLAGS)

xineplug_vo_out_directx_la_SOURCES = video_out_directx.c
xineplug_vo_out_directx_la_LIBADD = $(YUV_LIBS) $(DIRECTX_VIDEO_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_directx_la_CPPFLAGS = $(AM_CPPFLAGS) $(DIRECTX_CPPFLAGS)

xineplug_vo_out_none_la_SOURCES =  video_out_none.c
xineplug_vo_out_none_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_vo_out_raw_la_SOURCES = video_out_raw.c
xineplug_vo_out_raw_la_LIBADD = $(YUV_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_vo_out_raw_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_vo_out_macosx_la_SOURCES = video_out_macosx.m
xineplug_vo_out_macosx_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS)
xineplug_vo_out_macosx_la_LDFLAGS = $(AM_LDFLAGS) -framework Cocoa -framework OpenGL
