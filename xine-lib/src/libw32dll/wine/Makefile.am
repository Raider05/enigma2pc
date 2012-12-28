include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

# disable -fomit-frame-pointer, -finline-functions, and -frename-registers
# because they cause bad behavior of wine
AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(X_CFLAGS) @W32_NO_OPTIMIZE@ \
        -fno-omit-frame-pointer -fno-inline-functions -fno-rename-registers
AM_CPPFLAGS = -DWIN32_PATH=\"$(w32_path)\" -I$(srcdir)/.. -D__WINE__ \
	-Ddbg_printf=__vprintf -DTRACE=__vprintf \
        -I$(top_srcdir)/src/xine-utils

EXTRA_DIST = stubs.s wrapper.S

noinst_HEADERS = \
	avifmt.h \
	basetsd.h \
	com.h \
	debugtools.h \
	driver.h \
	elfdll.h \
	ext.h \
	heap.h \
	ldt.h \
	ldt_keeper.h \
	loader.h \
	mmreg.h \
	module.h \
	msacm.h \
	msacmdrv.h \
	ntdef.h \
	pe_image.h \
	poppack.h \
	pshpack1.h \
	pshpack2.h \
	pshpack4.h \
	pshpack8.h \
	registry.h \
	resource.h \
	vfw.h \
	win32.h\
	wineacm.h \
	winbase.h \
	windef.h \
	windows.h \
	winerror.h \
	winestring.h \
	winnt.h \
	winreg.h \
	winuser.h \
	wrapper.h

noinst_LTLIBRARIES = libwine.la

libwine_la_SOURCES = \
	afl.c \
	driver.c \
	elfdll.c \
	ext.c \
	ldt_keeper.c \
	module.c \
	pe_image.c  \
	pe_resource.c \
	resource.c \
	registry.c \
	vfl.c \
	win32.c \
	stubs.s \
	wrapper.S
libwine_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
libwine_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS)
