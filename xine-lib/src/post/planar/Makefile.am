include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(VISIBILITY_FLAG)
AM_CPPFLAGS =
AM_LDFLAGS  =

noinst_LTLIBRARIES = libpost_planar_asm.la
libpost_planar_asm_la_SOURCES = eq.c eq2.c noise.c
libpost_planar_asm_la_CFLAGS  = $(O1_CFLAGS) -fomit-frame-pointer $(AM_CFLAGS) $(AVUTIL_CFLAGS)
libpost_planar_asm_la_LIBADD  = $(AVUTIL_LIBS)

xinepost_LTLIBRARIES = xineplug_post_planar.la
xineplug_post_planar_la_SOURCES = planar.c invert.c expand.c fill.c boxblur.c \
                                  denoise3d.c unsharp.c pp.c
xineplug_post_planar_la_LIBADD  = $(XINE_LIB) $(FFMPEG_POSTPROC_LIBS) -lm $(PTHREAD_LIBS) $(LTLIBINTL) $(noinst_LTLIBRARIES)
xineplug_post_planar_la_DEPS = $(FFMPEG_POSTPROC_DEPS)
xineplug_post_planar_la_CFLAGS  = $(DEFAULT_OCFLAGS) $(AM_CFLAGS) $(FFMPEG_CFLAGS) $(FFMPEG_POSTPROC_CFLAGS)
xineplug_post_planar_la_LDFLAGS = $(AM_LDFLAGS) $(xineplug_ldflags) $(IMPURE_TEXT_LDFLAGS)
