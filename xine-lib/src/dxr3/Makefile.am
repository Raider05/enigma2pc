include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS =
AM_LDFLAGS  = $(xineplug_ldflags)

if HAVE_X11
AM_CFLAGS  += $(X_CFLAGS)
link_x_libs = $(X_LIBS) -lXext
endif
if HAVE_LIBFAME
AM_CFLAGS += $(LIBFAME_CFLAGS)
link_fame  = $(LIBFAME_LIBS)
endif
if HAVE_LIBRTE
link_rte = -lrte
endif

EXTRA_DIST = compat.c

noinst_HEADERS = em8300.h dxr3.h dxr3_scr.h video_out_dxr3.h

if ENABLE_DXR3
xineplug_LTLIBRARIES =	xineplug_decode_dxr3_video.la \
			xineplug_decode_dxr3_spu.la \
			xineplug_vo_out_dxr3.la
endif

xineplug_decode_dxr3_video_la_SOURCES = dxr3_decode_video.c
xineplug_decode_dxr3_video_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

if WITH_EXTERNAL_DVDNAV
xineplug_decode_dxr3_spu_la_SOURCES = dxr3_decode_spu.c
xineplug_decode_dxr3_spu_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(DVDNAV_LIBS)
xineplug_decode_dxr3_spu_la_CFLAGS = $(AM_CFLAGS) $(DVDNAV_CFLAGS)
else
xineplug_decode_dxr3_spu_la_SOURCES = dxr3_decode_spu.c nav_read.c
xineplug_decode_dxr3_spu_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)
xineplug_decode_dxr3_spu_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/input/libdvdnav
endif

xineplug_vo_out_dxr3_la_SOURCES = \
	dxr3_mpeg_encoders.c \
	ffmpeg_encoder.c \
	dxr3_spu_encoder.c \
	dxr3_scr.c \
	video_out_dxr3.c

xineplug_vo_out_dxr3_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS) $(FFMPEG_CFLAGS)
xineplug_vo_out_dxr3_la_LIBADD = $(XINE_LIB) $(link_fame) $(link_rte) $(link_x_libs) $(LTLIBINTL) $(AVUTIL_LIBS) $(FFMPEG_LIBS) -lm
