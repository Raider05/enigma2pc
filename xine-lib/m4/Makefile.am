include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

if INSTALL_M4
m4datadir = $(ACLOCAL_DIR)
else
m4datadir = $(datadir)/aclocal
endif

m4data_DATA = xine.m4

EXTRA_DIST = $(wildcard *.m4)
