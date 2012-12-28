include $(top_srcdir)/misc/Makefile.quiet
SUBDIRS = libmpcdec libxdg-basedir libdca nosefart gsm610 libmad a52dec \
	libfaad


if BUILD_DHA_KMOD
SUBDIRS += libdha
endif

# vidix depends on portions of dha
if ENABLE_VIDIX
if !BUILD_DHA_KMOD
SUBDIRS += libdha
endif
SUBDIRS += vidix
endif

srcdir = $(shell cd @srcdir@; pwd)

EXTRA_DIST = README.contrib
