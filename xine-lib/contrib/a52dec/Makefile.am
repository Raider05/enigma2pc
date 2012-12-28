include $(top_srcdir)/misc/Makefile.quiet
AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

if ENABLE_A52DEC
if !WITH_EXTERNAL_A52DEC
noinst_LTLIBRARIES = liba52.la
endif
endif

liba52_la_SOURCES = bitstream.c bit_allocate.c downmix.c imdct.c parse.c \
	a52.h a52_internal.h bitstream.h tables.h crc.c
liba52_la_LIBADD = -lm $(AVUTIL_LIBS)
liba52_la_CPPFLAGS = $(AM_CPPFLAGS) $(AVUTIL_CFLAGS)
