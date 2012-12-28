include $(top_srcdir)/misc/Makefile.quiet
if !EXTERNAL_LIBXDG_BASEDIR
noinst_LTLIBRARIES = libxdg-basedir.la
endif

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

libxdg_basedir_la_SOURCES = basedir.c basedir.h basedir_fs.h
