include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = $(ZLIB_CPPFLAGS)
AM_LDFLAGS  = $(xineplug_ldflags)

xineplug_LTLIBRARIES = xineplug_decode_ff.la

ff_generated = \
	avcodec_video.list avcodec_audio.list \
	ff_video_list.h ff_audio_list.h

BUILT_SOURCES = $(ff_generated)
DISTCLEANFILES = $(ff_generated)

EXTRA_DIST = xine_video.list xine_audio.list mkcodeclist.pl

noinst_HEADERS = ffmpeg_compat.h

xineplug_decode_ff_la_SOURCES = ffmpeg_decoder.c ff_audio_decoder.c ff_video_decoder.c \
                                ff_mpeg_parser.c ffmpeg_decoder.h ff_mpeg_parser.h

nodist_xineplug_decode_ff_la_SOURCES = ffmpeg_config.h

xineplug_decode_ff_la_CFLAGS = $(AM_CFLAGS) $(FFMPEG_CFLAGS) $(FFMPEG_POSTPROC_CFLAGS)
xineplug_decode_ff_la_LIBADD = $(XINE_LIB) $(MLIB_LIBS) -lm $(ZLIB_LIBS) \
	$(FFMPEG_LIBS) $(AVUTIL_LIBS) $(FFMPEG_POSTPROC_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_decode_ff_la_LDFLAGS = $(AM_LDFLAGS) $(IMPURE_TEXT_LDFLAGS)

if ENABLE_AVFORMAT
xineplug_decode_ff_la_SOURCES += input_avio.c demux_avformat.c
xineplug_decode_ff_la_CFLAGS += $(AVFORMAT_CFLAGS)
xineplug_decode_ff_la_LIBADD += $(AVFORMAT_LIBS)
endif

# Generation of ffmpeg->xine codec mapping lists (see xine_*.list).

AV_CPP = $(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AVUTIL_CFLAGS)

# Extract some CODEC_ID_* from avcodec.h. Requires some sed mangling.
avcodec_audio.list: AV_CODECS:=/CODEC_ID_PCM_S16LE/,/CODEC_ID_DVD_SUBTITLE/
avcodec_video.list: AV_CODECS:=/CODEC_ID_MPEG1VIDEO/,/CODEC_ID_PCM_S16LE/

# that weird shell call just yields full path of the avcodec.h file
avcodec_audio.list avcodec_video.list: $(shell \
 echo '#include "$(srcdir)/ffmpeg_decoder.h"' | $(AV_CPP) -M - |\
 sed -e '/avcodec\.h/! d; s%^.* \([^ ]*/avcodec\.h\) .*%\1%')
	$(AM_V_GEN)echo '#include "$(srcdir)/ffmpeg_decoder.h"' | $(AV_CPP) - |\
	  sed -e $(AV_CODECS)'! d; s/^[ \t]*//; s/[=,].*//; /^$$/ d' >$@

# Generate the mappings. These are #included where needed.
ff_%_list.h: $(srcdir)/mkcodeclist.pl avcodec_%.list $(srcdir)/xine_%.list
	$(AM_V_GEN)$(PERL) $^ $@

ff_audio_decoder.c: ff_audio_list.h
ff_video_decoder.c: ff_video_list.h

# 'make report' prints tokens corresponding to any unhandled codecs.
report: avcodec_audio.list avcodec_video.list
	@$(top_srcdir)/src/combined/ffmpeg/mkcodeclist.pl avcodec_audio.list xine_audio.list - audio
	@$(top_srcdir)/src/combined/ffmpeg/mkcodeclist.pl avcodec_video.list xine_video.list - video

.PHONY: report
