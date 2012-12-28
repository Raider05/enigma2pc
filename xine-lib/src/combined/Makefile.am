include $(top_srcdir)/misc/Makefile.quiet
SUBDIRS = ffmpeg

include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

if ENABLE_WAVPACK
xineplug_wavpack = xineplug_wavpack.la
endif

if ENABLE_NOSEFART
xineplug_nsf = xineplug_nsf.la
endif

if ENABLE_LIBFLAC
xineplug_flac = xineplug_flac.la
endif

$(top_builddir)/contrib/nosefart/libnosefart.la:
	$(MAKE) -C $(top_builddir)/contrib/nosefart

xineplug_LTLIBRARIES = \
	$(xineplug_wavpack) \
	$(xineplug_flac) \
	$(xineplug_nsf)

xineplug_wavpack_la_SOURCES = wavpack_demuxer.c wavpack_decoder.c wavpack_combined.c wavpack_combined.h
xineplug_wavpack_la_LIBADD = $(XINE_LIB) $(WAVPACK_LIBS)
xineplug_wavpack_la_CFLAGS = $(AM_CFLAGS) $(WAVPACK_CFLAGS)
xineplug_wavpack_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/demuxers

xineplug_flac_la_SOURCES = flac_demuxer.c flac_decoder.c
xineplug_flac_la_LIBADD = $(XINE_LIB) $(LIBFLAC_LIBS)
xineplug_flac_la_CFLAGS = $(AM_CFLAGS) $(LIBFLAC_CFLAGS)

xineplug_nsf_la_SOURCES = nsf_decoder.c nsf_demuxer.c nsf_combined.c nsf_combined.h
xineplug_nsf_la_LIBADD = $(XINE_LIB) $(top_builddir)/contrib/nosefart/libnosefart.la -lm
xineplug_nsf_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_nsf_la_CPPFLAGS = $(AM_CPPFLAGS) -DNSF_PLAYER -I$(top_srcdir)/contrib/nosefart -I$(top_srcdir)/src/demuxers

xineplug_xiph_la_SOURCES = xine_ogg_demuxer.c
xineplug_xiph_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)
xineplug_xiph_la_CFLAGS = $(AM_CFLAGS)
xineplug_xiph_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/demuxers

if ENABLE_VORBIS
xineplug_LTLIBRARIES += xineplug_xiph.la
xineplug_xiph_la_SOURCES += xine_vorbis_decoder.c
xineplug_xiph_la_LIBADD += $(VORBIS_LIBS)
xineplug_xiph_la_CFLAGS += $(VORBIS_CFLAGS)
endif

if ENABLE_THEORA
xineplug_LTLIBRARIES += xineplug_xiph.la
xineplug_xiph_la_SOURCES += xine_theora_decoder.c
xineplug_xiph_la_LIBADD += $(THEORA_LIBS)
xineplug_xiph_la_CFLAGS += $(THEORA_CFLAGS)
endif

if ENABLE_SPEEX
xineplug_LTLIBRARIES += xineplug_xiph.la
xineplug_xiph_la_SOURCES += xine_speex_decoder.c
xineplug_xiph_la_LIBADD += $(SPEEX_LIBS)
xineplug_xiph_la_CFLAGS += $(SPEEX_CFLAGS)
endif
