include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/contrib/vidix \
              -I$(top_srcdir)/contrib/libdha -I$(top_builddir)/contrib/libdha
AM_LDFLAGS = -avoid-version -module

EXTRA_DIST = genfb_vid.c

noinst_HEADERS = mach64.h glint_regs.h pm3_regs.h radeon.h savage_regs.h \
                 cyberblade_regs.h unichrome_regs.h sis_defs.h sis_regs.h

BUILT_SOURCES = $(top_builddir)/contrib/libdha/pci_ids.h \
		$(top_builddir)/contrib/libdha/pci_names.h

$(BUILT_SOURCES):
	$(MAKE) -C $(dir $@) $(notdir $@)

vidix_LTLIBRARIES = \
	mach64_vid.la \
	mga_crtc2_vid.la \
	mga_vid.la \
	pm2_vid.la \
	pm3_vid.la \
	radeon_vid.la \
	rage128_vid.la \
	cyberblade_vid.la \
	unichrome_vid.la \
	nvidia_vid.la \
	sis_vid.la \
	savage_vid.la 

cyberblade_vid_la_SOURCES = cyberblade_vid.c

mach64_vid_la_SOURCES = mach64_vid.c

mga_crtc2_vid_la_SOURCES = mga_vid.c
mga_crtc2_vid_la_LIBADD = -lm
mga_crtc2_vid_la_CPPFLAGS = $(AM_CPPFLAGS) -DCRTC2

mga_vid_la_SOURCES = mga_vid.c
mga_vid_la_LIBADD = -lm

nvidia_vid_la_SOURCES = nvidia_vid.c

pm2_vid_la_SOURCES = pm2_vid.c

pm3_vid_la_SOURCES = pm3_vid.c

radeon_vid_la_SOURCES = radeon_vid.c
radeon_vid_la_LIBADD = -lm

rage128_vid_la_SOURCES = radeon_vid.c
rage128_vid_la_LIBADD = -lm
rage128_vid_la_CPPFLAGS = $(AM_CPPFLAGS) -DRAGE128

savage_vid_la_SOURCES = savage_vid.c
savage_vid_la_LIBADD = -lm

sis_vid_la_SOURCES = sis_vid.c sis_bridge.c

unichrome_vid_la_SOURCES = unichrome_vid.c
