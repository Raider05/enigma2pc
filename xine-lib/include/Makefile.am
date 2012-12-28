include $(top_srcdir)/misc/Makefile.quiet
EXTRA_DIST = xine/version.h.in

if GENERATED_INTTYPES_H
inttypes_h = inttypes.h
endif

nobase_include_HEADERS = xine.h \
	xine/alphablend.h	\
	xine/array.h		\
	xine/attributes.h	\
	xine/audio_decoder.h	\
	xine/audio_out.h	\
	xine/broadcaster.h	\
	xine/buffer.h		\
	xine/compat.h		\
	xine/configfile.h	\
	xine/demux.h		\
	xine/info_helper.h	\
	xine/input_plugin.h	\
	xine/io_helper.h	\
	xine/list.h		\
	xine/metronom.h		\
	xine/os_types.h		\
	xine/osd.h		\
	xine/plugin_catalog.h	\
	xine/pool.h		\
	xine/post.h		\
	xine/refcounter.h	\
	xine/resample.h		\
	xine/ring_buffer.h	\
	xine/scratch.h		\
	xine/sorted_array.h	\
	xine/spu.h		\
	xine/spu_decoder.h	\
	xine/vdr.h		\
	xine/version.h		\
	xine/video_decoder.h	\
	xine/video_out.h	\
	xine/video_overlay.h	\
	xine/vo_scale.h		\
	xine/xine_buffer.h	\
	xine/xine_internal.h	\
	xine/xine_plugin.h	\
	xine/xineintl.h		\
	xine/xineutils.h	\
	xine/xmllexer.h		\
	xine/xmlparser.h

noinst_HEADERS = config.h
nodist_noinst_HEADERS = configure.h

nodist_include_HEADERS = $(inttypes_h)

CONFIG_CLEAN_FILES = $(inttypes_h)
DISTCLEANFILES = xine/version.h
