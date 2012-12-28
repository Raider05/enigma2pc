include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(X_CFLAGS) -fno-omit-frame-pointer
AM_CPPFLAGS = -DWIN32_PATH=\"$(w32_path)\" -DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. -I$(srcdir)/../wine

noinst_HEADERS = \
	allocator.h \
	cmediasample.h \
	guids.h \
	inputpin.h \
	interfaces.h \
	iunk.h \
	outputpin.h \
	DS_AudioDecoder.h \
	DS_Filter.h \
	DS_VideoDecoder.h

noinst_LTLIBRARIES = libds_filter.la

libds_filter_la_SOURCES = \
	allocator.c \
	cmediasample.c \
	guids.c \
	inputpin.c \
	outputpin.c \
	DS_Filter.c \
	DS_AudioDecoder.c \
	DS_VideoDecoder.c
libds_filter_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
