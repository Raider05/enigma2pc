include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

noinst_LTLIBRARIES = libnosefart.la

libnosefart_la_SOURCES = \
	fds_snd.c \
	log.c \
	mmc5_snd.c \
	nes_apu.c \
	vrc7_snd.c \
	dis6502.c \
	fmopl.c \
	memguard.c \
	nes6502.c \
	nsf.c \
	vrcvisnd.c	

libnosefart_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
libnosefart_la_CPPFLAGS = -DNSF_PLAYER

noinst_HEADERS = \
	dis6502.h \
	fmopl.h \
	memguard.h \
	nes6502.h \
	nsf.h \
	types.h \
	vrc7_snd.h \
	fds_snd.h \
	log.h \
	mmc5_snd.h \
	nes_apu.h \
	osd.h \
	version.h \
	vrcvisnd.h
