include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

if ENABLE_VDR
xineplug_LTLIBRARIES = xineplug_vdr.la
endif

xineplug_vdr_la_SOURCES = combined_vdr.c combined_vdr.h input_vdr.c post_vdr_video.c post_vdr_audio.c
xineplug_vdr_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_vdr_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS)
