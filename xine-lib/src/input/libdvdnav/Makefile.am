include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = -DDVDNAV_COMPILE -DHAVE_DLFCN_H
AM_LDFLAGS = $(xineplug_ldflags)

noinst_LTLIBRARIES = libdvdnav.la

libdvdnav_la_SOURCES = \
	decoder.c \
	dvdnav.c \
	highlight.c \
	navigation.c \
	read_cache.c \
	remap.c \
	searching.c \
	settings.c \
	vm.c \
	vmcmd.c \
	ifo_read.c \
	md5.c \
	nav_print.c \
	nav_read.c \
	dvd_reader.c \
	dvd_input.c \
	dvd_udf.c
libdvdnav_la_LIBADD = $(PTHREAD_LIBS)

noinst_HEADERS = \
	decoder.h \
	dvdnav.h \
	dvdnav_events.h \
	dvdnav_internal.h \
	dvdread_internal.h \
	remap.h \
	vm.h \
	vmcmd.h \
	read_cache.h \
	dvd_types.h \
	ifo_read.h \
	ifo_types.h \
	md5.h \
	nav_print.h \
	nav_read.h \
	nav_types.h \
	dvd_reader.h \
	dvd_input.h \
	dvd_udf.h \
	bswap.h 
