include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = -I$(srcdir)/wine -DWIN32_PATH=\"$(w32_path)\"
AM_LDFLAGS  = $(xineplug_ldflags)

SUBDIRS =
if ENABLE_W32DLL
SUBDIRS += wine DirectShow dmo qtx
endif

EXTRA_DIST = common.c

noinst_HEADERS = libwin32.h w32codec.h

if ENABLE_W32DLL
xineplug_LTLIBRARIES = xineplug_decode_w32dll.la xineplug_decode_qt.la
endif

xineplug_decode_w32dll_la_SOURCES = w32codec.c
xineplug_decode_w32dll_la_DEPS = $(XDG_BASEDIR_DEPS)
xineplug_decode_w32dll_la_LIBADD = \
	$(top_builddir)/src/libw32dll/wine/libwine.la \
	$(top_builddir)/src/libw32dll/DirectShow/libds_filter.la \
	$(top_builddir)/src/libw32dll/dmo/libdmo_filter.la \
	$(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) -lm $(KSTAT_LIBS) $(XDG_BASEDIR_LIBS)
xineplug_decode_w32dll_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_decode_w32dll_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS)

xineplug_decode_qt_la_SOURCES = qt_decoder.c
xineplug_decode_qt_la_LIBADD = \
	$(top_builddir)/src/libw32dll/wine/libwine.la \
	$(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) -lm $(KSTAT_LIBS) $(XDG_BASEDIR_LIBS)
