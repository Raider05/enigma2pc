include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = build_rpms.sh \
	SlackBuild.in \
	SlackBuild \
	vga.xinefont.gz \
	xine-config \
	xine-lib.spec.in \
	xine-lib.spec \
	libxine.pc.in \
	logo/xine_logo_text.svg \
	libdvdcss-1.2.6-network.patch \
	Makefile.plugins.in \
	Makefile.common \
	fonts/cetus.ttf

bin_SCRIPTS = xine-config
bin_PROGRAMS = xine-list-@XINE_SERIES@

pkgconfig_DATA=libxine.pc

xine_list_@XINE_SERIES@_SOURCES = xine-list.c
xine_list_@XINE_SERIES@_LDADD = $(XINE_LIB)

fontdir = $(pkgdatadir)/fonts
dist_font_DATA = \
	fonts/cetus-16.xinefont.gz \
	fonts/cetus-20.xinefont.gz \
	fonts/cetus-24.xinefont.gz \
	fonts/cetus-32.xinefont.gz \
	fonts/cetus-48.xinefont.gz \
	fonts/cetus-64.xinefont.gz \
	fonts/cc-16.xinefont.gz \
	fonts/cc-20.xinefont.gz \
	fonts/cc-24.xinefont.gz \
	fonts/cc-32.xinefont.gz \
	fonts/cc-48.xinefont.gz \
	fonts/cc-64.xinefont.gz \
	fonts/cci-16.xinefont.gz \
	fonts/cci-20.xinefont.gz \
	fonts/cci-24.xinefont.gz \
	fonts/cci-32.xinefont.gz \
	fonts/cci-48.xinefont.gz \
	fonts/cci-64.xinefont.gz \
	fonts/mono-16.xinefont.gz \
	fonts/mono-20.xinefont.gz \
	fonts/mono-24.xinefont.gz \
	fonts/mono-32.xinefont.gz \
	fonts/mono-48.xinefont.gz \
	fonts/mono-64.xinefont.gz \
	fonts/sans-16.xinefont.gz \
	fonts/sans-20.xinefont.gz \
	fonts/sans-24.xinefont.gz \
	fonts/sans-32.xinefont.gz \
	fonts/sans-48.xinefont.gz \
	fonts/sans-64.xinefont.gz \
	fonts/serif-16.xinefont.gz \
	fonts/serif-20.xinefont.gz \
	fonts/serif-24.xinefont.gz \
	fonts/serif-32.xinefont.gz \
	fonts/serif-48.xinefont.gz \
	fonts/serif-64.xinefont.gz

dist_doc_DATA = fonts/README.cetus

EXTRA_PROGRAMS = xine-fontconv cdda_server

xine_fontconv_SOURCES = xine-fontconv.c
xine_fontconv_CFLAGS = $(FT2_CFLAGS)
xine_fontconv_LDFLAGS = $(GCSECTIONS)
xine_fontconv_LDADD = -lz $(FT2_LIBS)

cdda_server_SOURCES = cdda_server.c
cdda_server_LDFLAGS = $(GCSECTIONS)
cdda_server_LDADD = $(DYNAMIC_LD_LIBS)
