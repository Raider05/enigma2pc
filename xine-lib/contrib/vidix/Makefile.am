include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

SUBDIRS = drivers

AM_CFLAGS   = $(DEFAULT_OCFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/contrib/vidix -I$(top_builddir)/contrib/libdha

EXTRA_DIST = README vidix.txt

noinst_HEADERS = fourcc.h vidix.h vidixlib.h

noinst_LTLIBRARIES = libvidix.la

$(top_builddir)/contrib/libdha/libdha.la:
	$(MAKE) -C $(top_builddir)/contrib/libdha libdha.la

libvidix_la_SOURCES = vidixlib.c
libvidix_la_LIBADD = $(DYNAMIC_LD_LIBS) $(top_builddir)/contrib/libdha/libdha.la
