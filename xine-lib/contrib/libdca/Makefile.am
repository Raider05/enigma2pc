include $(top_srcdir)/misc/Makefile.quiet
AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

EXTRA_DIST = COPYING

if ENABLE_DTS
if !WITH_EXTERNAL_LIBDTS
noinst_LTLIBRARIES = libdca.la
endif
endif

libdca_la_SOURCES = dca_internal.h tables.h tables_quantization.h \
		    tables_huffman.h tables_fir.h tables_adpcm.h tables_vq.h \
		    bitstream.h bitstream.c parse.c downmix.c \
		    include/dca.h include/dts.h
libdca_la_LIBADD = -lm
libdca_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/include
