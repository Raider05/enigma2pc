include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags) $(IMPURE_TEXT_LDFLAGS)

SUBDIRS = plugins

xinepost_LTLIBRARIES = xineplug_post_tvtime.la

xineplug_post_tvtime_la_SOURCES = xine_plugin.c \
	deinterlace.c pulldown.c speedy.c tvtime.c
xineplug_post_tvtime_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_post_tvtime_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(PTHREAD_LIBS) \
	$(top_builddir)/src/post/deinterlace/plugins/libdeinterlaceplugins.la

noinst_HEADERS = deinterlace.h pulldown.h speedtools.h speedy.h tvtime.h
