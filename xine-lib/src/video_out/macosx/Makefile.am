include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS    = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_OBJCFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)

xineinclude_HEADERS = video_window.h XineOpenGLView.h XineVideoWindow.h

lib_LTLIBRARIES = libxineMacOSXVideo.la

libxineMacOSXVideo_la_SOURCES = XineOpenGLView.m XineVideoWindow.m
libxineMacOSXVideo_la_LDFLAGS = $(AM_LDFLAGS) -framework Cocoa -framework OpenGL \
				-version-info $(XINE_LT_CURRENT):$(XINE_LT_REVISION):$(XINE_LT_AGE)
