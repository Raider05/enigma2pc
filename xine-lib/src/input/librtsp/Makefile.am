include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG) $(AVUTIL_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/src/input/libreal

noinst_HEADERS = rtsp.h rtsp_session.h

noinst_LTLIBRARIES = librtsp.la

librtsp_la_SOURCES = rtsp.c rtsp_session.c
