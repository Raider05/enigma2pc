include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

EXTRA_DIST = fooaudio.c
noinst_HEADERS = ff_dvdata.h

if ENABLE_MUSEPACK
musepack_module = xineplug_decode_mpc.la
endif

if ENABLE_DTS
dts_module = xineplug_decode_dts.la
endif

if ENABLE_MAD
mad_module = xineplug_decode_mad.la
endif

if ENABLE_A52DEC
a52_module = xineplug_decode_a52.la
endif

if ENABLE_FAAD
faad_module = xineplug_decode_faad.la
endif

$(top_builddir)/contrib/a52dec/liba52.la:
	$(MAKE) -C $(top_builddir)/contrib/a52dec

$(top_builddir)/contrib/libfaad/libfaad.la:
	$(MAKE) -C $(top_builddir)/contrib/libfaad

$(top_builddir)/contrib/libmad/libmad.la:
	$(MAKE) -C $(top_builddir)/contrib/libmad

$(top_builddir)/contrib/libmpcdec/libmpcdec.la:
	$(MAKE) -C $(top_builddir)/contrib/libmpcdec

$(top_builddir)/contrib/libdca/libdca.la:
	$(MAKE) -C $(top_builddir)/contrib/libdca

$(top_builddir)/contrib/gsm610/libgsm610.la:
	$(MAKE) -C $(top_builddir)/contrib/gsm610

xineplug_LTLIBRARIES = \
	xineplug_decode_gsm610.la \
	xineplug_decode_lpcm.la \
	xineplug_decode_dvaudio.la \
	$(musepack_module) \
	$(dts_module) \
	$(mad_module) \
	$(a52_module) \
	$(faad_module)

xineplug_decode_gsm610_la_SOURCES = gsm610.c
xineplug_decode_gsm610_la_LIBADD = $(XINE_LIB) $(top_builddir)/contrib/gsm610/libgsm610.la
xineplug_decode_gsm610_la_CPPFLAGS = -I$(top_srcdir)/contrib/gsm610

xineplug_decode_lpcm_la_SOURCES = xine_lpcm_decoder.c
xineplug_decode_lpcm_la_LIBADD = $(XINE_LIB)

xineplug_decode_mpc_la_SOURCES = xine_musepack_decoder.c
xineplug_decode_mpc_la_DEPENDENCIES = $(MPCDEC_DEPS)
xineplug_decode_mpc_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(MPCDEC_LIBS)
xineplug_decode_mpc_la_CFLAGS = $(AM_CFLAGS) $(MPCDEC_CFLAGS)

xineplug_decode_dts_la_SOURCES = xine_dts_decoder.c
xineplug_decode_dts_la_DEPENDENCIES = $(LIBDTS_DEPS)
xineplug_decode_dts_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(LIBDTS_LIBS)
xineplug_decode_dts_la_CFLAGS = $(AM_CFLAGS) $(LIBDTS_CFLAGS)

xineplug_decode_mad_la_SOURCES = xine_mad_decoder.c
xineplug_decode_mad_la_DEPENDENCIES = $(LIBMAD_DEPS)
xineplug_decode_mad_la_LIBADD = $(XINE_LIB) $(LIBMAD_LIBS)
xineplug_decode_mad_la_CFLAGS = $(AM_CFLAGS) $(LIBMAD_CFLAGS)

xineplug_decode_a52_la_SOURCES = xine_a52_decoder.c
xineplug_decode_a52_la_DEPENDENCIES = $(A52DEC_DEPS)
xineplug_decode_a52_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(A52DEC_LIBS) $(AVUTIL_LIBS) -lm
xineplug_decode_a52_la_CFLAGS = $(AM_CFLAGS) $(A52DEC_CFLAGS) $(AVUTIL_CFLAGS)

xineplug_decode_faad_la_SOURCES = xine_faad_decoder.c
xineplug_decode_faad_la_DEPENDENCIES = $(FAAD_DEPS)
xineplug_decode_faad_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(FAAD_LIBS) -lm
xineplug_decode_faad_la_CFLAGS = $(FAAD_CFLAGS)

xineplug_decode_dvaudio_la_SOURCES  = ff_dvaudio_decoder.c
xineplug_decode_dvaudio_la_LIBADD   = $(XINE_LIB) $(LTLIBINTL)
xineplug_decode_dvaudio_la_CPPFLAGS = $(AM_CPPFLAGS) $(FFMPEG_CFLAGS)
