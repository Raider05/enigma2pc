include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

SUBDIRS = include libmpeg2

if ENABLE_MPEG2NEW
mpeg2new_module = xineplug_decode_mpeg2new.la
endif

xineplug_LTLIBRARIES = $(mpeg2new_module)

xineplug_decode_mpeg2new_la_SOURCES = \
	xine_mpeg2new_decoder.c

xineplug_decode_mpeg2new_la_LIBADD = $(XINE_LIB) libmpeg2/libmpeg2.la 
xineplug_decode_mpeg2new_la_CFLAGS = $(AM_CFLAGS) $(MLIB_CFLAGS)
