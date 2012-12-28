include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

noinst_LTLIBRARIES = libgsm610.la

libgsm610_la_SOURCES = \
	add.c \
	decode.c \
	gsm_create.c \
	gsm_decode.c \
	gsm_destroy.c \
	long_term.c \
	lpc.c \
	rpe.c \
	short_term.c \
	table.c


noinst_HEADERS = \
	gsm_config.h \
	gsm.h \
	private.h \
	proto.h \
	unproto.h
