include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

EXTRA_DIST = fooviz.c

noinst_HEADERS = fft.h visualizations.h

xinepost_LTLIBRARIES = xineplug_post_visualizations.la

xineplug_post_visualizations_la_SOURCES = \
        visualizations.c fft.c fftscope.c oscope.c fftgraph.c
xineplug_post_visualizations_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) -lm
