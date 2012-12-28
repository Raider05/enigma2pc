include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

noinst_HEADERS = real_common.h

if ENABLE_REAL
xineplug_LTLIBRARIES = xineplug_decode_real.la
endif

xineplug_decode_real_la_SOURCES = xine_real_video_decoder.c real_common.c xine_real_audio_decoder.c
xineplug_decode_real_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS) $(LTLIBINTL)
