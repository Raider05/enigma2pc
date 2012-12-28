include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(X_CFLAGS) -fno-omit-frame-pointer
AM_CPPFLAGS = -DWIN32_PATH=\"$(w32_path)\" -DNOAVIFILE_HEADERS -I$(srcdir)/.. -I$(srcdir)/../wine

noinst_HEADERS = \
	DMO_AudioDecoder.h \
	dmo_guids.h \
	dmo_interfaces.h \
	DMO_Filter.h \
	dmo.h \
	DMO_VideoDecoder.h

noinst_LTLIBRARIES = libdmo_filter.la

libdmo_filter_la_SOURCES = \
	buffer.c \
	DMO_AudioDecoder.c \
	dmo.c \
	dmo_guids.c \
	DMO_VideoDecoder.c
libdmo_filter_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
