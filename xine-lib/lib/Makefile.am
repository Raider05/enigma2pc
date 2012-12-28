include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS)

EXTRA_DIST = Makefile.common

noinst_LTLIBRARIES = libxineposix.la
noinst_HEADERS = os_internal.h

libxineposix_la_SOURCES =
libxineposix_la_LIBADD = @LTLIBOBJS@
