include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

noinst_HEADERS = alterh264_decode.h alterh264_bits_reader.h bits_reader.h dpb.h cpb.h h264_parser.h nal.h

if ENABLE_VDPAU
vdpau_h264_module = xineplug_decode_vdpau_h264.la
VDPAU_CFLAGS += -D_ISOC99_SOURCE

vdpau_h264_alter_module = xineplug_decode_vdpau_h264_alter.la

vdpau_mpeg12_module = xineplug_decode_vdpau_mpeg12.la

vdpau_vc1_module = xineplug_decode_vdpau_vc1.la

vdpau_mpeg4_module = xineplug_decode_vdpau_mpeg4.la
endif

xineplug_LTLIBRARIES = $(vdpau_h264_module) $(vdpau_h264_alter_module) $(vdpau_mpeg12_module) $(vdpau_vc1_module) $(vdpau_mpeg4_module)

xineplug_decode_vdpau_h264_alter_la_SOURCES = alterh264_decode.c
xineplug_decode_vdpau_h264_alter_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_decode_vdpau_h264_alter_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)

xineplug_decode_vdpau_h264_la_SOURCES = nal.c dpb.c cpb.c h264_parser.c vdpau_h264.c
xineplug_decode_vdpau_h264_la_CFLAGS = $(AM_CFLAGS) $(VDPAU_CFLAGS) -fno-strict-aliasing
xineplug_decode_vdpau_h264_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS) -lm

xineplug_decode_vdpau_mpeg12_la_SOURCES = vdpau_mpeg12.c
xineplug_decode_vdpau_mpeg12_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_decode_vdpau_mpeg12_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)

xineplug_decode_vdpau_vc1_la_SOURCES = vdpau_vc1.c
xineplug_decode_vdpau_vc1_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_decode_vdpau_vc1_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)

xineplug_decode_vdpau_mpeg4_la_SOURCES = vdpau_mpeg4.c
xineplug_decode_vdpau_mpeg4_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_decode_vdpau_mpeg4_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)
