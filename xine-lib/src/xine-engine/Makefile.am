include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common
include $(top_srcdir)/lib/Makefile.common

AM_CFLAGS   = -I$(top_srcdir)/src/video_out $(DEFAULT_OCFLAGS) $(X_CFLAGS) $(FT2_CFLAGS) $(FONTCONFIG_CFLAGS) \
              $(AVUTIL_CFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = $(XDG_BASEDIR_CPPFLAGS) $(ZLIB_CPPFLAGS) -DXINE_LIBRARY_COMPILE

XINEUTILS_LIB = $(top_builddir)/src/xine-utils/libxineutils.la

YUV_LIB = $(top_builddir)/src/video_out/libyuv2rgb.la

# FIXME: these are currently unused:
EXTRA_DIST = lrb.c lrb.h accel_vaapi.h accel_vdpau.h accel_xvmc.h

if WIN32
DEF_FILE = libxine-$(XINE_MAJOR).def
def_ldflags=-Wl,--output-def,$(DEF_FILE)
else
DEF_FILE =
endif

noinst_HEADERS = bswap.h ffmpeg_bswap.h xine_private.h

lib_LTLIBRARIES = libxine.la

# Technically, this is a lie; it just makes sure that we get a static lib.
noinst_LTLIBRARIES = libxine-interface.la

libxine_la_SOURCES = xine.c metronom.c configfile.c buffer.c \
	load_plugins.c video_decoder.c buffer_types.c \
	audio_decoder.c video_out.c audio_out.c resample.c events.c \
	video_overlay.c osd.c spu.c scratch.c demux.c vo_scale.c \
	xine_interface.c post.c broadcaster.c io_helper.c \
	input_rip.c input_cache.c info_helper.c refcounter.c \
	alphablend.c \
	xine_private.h

libxine_la_DEPENDENCIES = $(XINEUTILS_LIB) $(YUV_LIB) $(XDG_BASEDIR_DEPS) \
                          $(pthread_dep) $(LIBXINEPOSIX) \
                          libxine-interface.la
libxine_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
libxine_la_LIBADD = $(PTHREAD_LIBS) $(DYNAMIC_LD_LIBS) $(LTLIBINTL) $(ZLIB_LIBS) \
		    -lm $(XINEUTILS_LIB) $(YUV_LIB) $(LTLIBICONV) $(FT2_LIBS) $(FONTCONFIG_LIBS) \
                    $(LIBXINEPOSIX) $(RT_LIBS) $(NET_LIBS) $(XDG_BASEDIR_LIBS) \
                    $(AVUTIL_LIBS) $(MLIB_LIBS)

libxine_la_LDFLAGS = $(AM_LDFLAGS) $(def_ldflags) $(GCSECTIONS) \
	$(LDFLAGS_NOUNDEFINED) \
	-weak libxine-interface.la \
	-version-info $(XINE_LT_CURRENT):$(XINE_LT_REVISION):$(XINE_LT_AGE)

libxine_interface_la_SOURCES =

libxine_interface_la_LDFLAGS = $(AM_LDFLAGS) $(def_ldflags) \
	-version-info $(XINE_LT_CURRENT):$(XINE_LT_REVISION):$(XINE_LT_AGE)

# Yes, we need to install this.
install-exec-hook: libxine-interface.la
	$(INSTALL_DATA) libxine-interface.la "$(DESTDIR)$(libdir)"/libxine-interface.la

clean-local:
	rm -f libxine-interface.la

$(XINEUTILS_LIB):
	$(MAKE) -C $(top_builddir)/src/xine-utils libxineutils.la

$(YUV_LIB):
	$(MAKE) -C $(top_builddir)/src/video_out libyuv2rgb.la

if WIN32
install-exec-local:
	-cp -p $(DEF_FILE) $(DESTDIR)$(libdir)
endif

uninstall-local:
	rm -f "$(DESTDIR)$(libdir)"/libxine-interface.la
	-rm -f $(DESTDIR)$(libdir)/$(DEF_FILE)

