include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

if ENABLE_MPEG2NEW
mpeg2new_libs = libmpeg2.la libmpeg2arch.la
endif

noinst_LTLIBRARIES = $(mpeg2new_libs)

libmpeg2_la_SOURCES = alloc.c header.c decode.c slice.c motion_comp.c idct.c
libmpeg2_la_LIBADD = libmpeg2arch.la

AM_CPPFLAGS = -I$(srcdir)/../include

libmpeg2arch_la_SOURCES = motion_comp_mmx.c idct_mmx.c \
                          motion_comp_altivec.c idct_altivec.c \
                          motion_comp_alpha.c idct_alpha.c \
                          motion_comp_vis.c \
                          cpu_accel.c cpu_state.c

EXTRA_DIST = mpeg2_internal.h vlc.h
