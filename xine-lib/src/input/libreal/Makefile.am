include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = -I$(top_srcdir)/src/input/librtsp $(AVUTIL_CFLAGS)

noinst_HEADERS = real.h asmrp.h rmff.h sdpplin.h

noinst_LTLIBRARIES = libreal.la

libreal_la_SOURCES = real.c asmrp.c rmff.c sdpplin.c
libreal_la_LIBADD = $(AVUTIL_LIBS)
