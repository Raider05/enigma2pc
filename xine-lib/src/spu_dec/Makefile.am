include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

xineplug_LTLIBRARIES = \
	xineplug_decode_spucc.la \
	xineplug_decode_spucmml.la \
	xineplug_decode_spu.la \
	xineplug_decode_spudvb.la \
	xineplug_decode_spuhdmv.la \
	xineplug_sputext.la

xineplug_decode_spucc_la_SOURCES = cc_decoder.c cc_decoder.h xine_cc_decoder.c
xineplug_decode_spucc_la_LIBADD = $(XINE_LIB)
xineplug_decode_spucc_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing

xineplug_decode_spucmml_la_SOURCES = cmml_decoder.c
xineplug_decode_spucmml_la_LIBADD  = $(XINE_LIB) $(LTLIBINTL)

if WITH_EXTERNAL_DVDNAV
external_dvdnav_libs = $(DVDNAV_LIBS)
internal_dvdnav_sources =
else
external_dvdnav_libs =
internal_dvdnav_sources = nav_read.c
endif

xineplug_decode_spu_la_SOURCES = $(internal_dvdnav_sources) spudec.c spudec.h spu_decoder.c
xineplug_decode_spu_la_LIBADD  = $(XINE_LIB) $(external_dvdnav_libs) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_decode_spu_la_CFLAGS  = $(AM_CFLAGS) $(DVDNAV_CFLAGS) -I$(top_srcdir)/src/input/libdvdnav

xineplug_decode_spudvb_la_SOURCES = spudvb_decoder.c
xineplug_decode_spudvb_la_LIBADD  = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_decode_spuhdmv_la_SOURCES = spuhdmv_decoder.c
xineplug_decode_spuhdmv_la_LIBADD  = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_sputext_la_SOURCES = sputext_demuxer.c sputext_decoder.c
xineplug_sputext_la_LIBADD  = $(XINE_LIB) $(LTLIBINTL)
