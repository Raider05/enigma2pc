include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

# plugins/Makefile.am distributes the plugins that come with tvtime.
# Copyright (C) 2002, 2003, 2004  Billy Biggs
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# libpostproc is here so we can use their nice mangle.h
AM_CFLAGS   = $(VISIBILITY_FLAG)
AM_CPPFLAGS = -I$(top_srcdir)/src/post/deinterlace \
              -I$(top_srcdir)/src/xine-utils

EXTRA_DIST = greedy2frame_template.c greedy2frame_template_sse2.c greedyh.asm \
	tomsmocomp/SearchLoop0A.inc tomsmocomp/SearchLoopBottom.inc \
	tomsmocomp/SearchLoopEdgeA.inc tomsmocomp/SearchLoopEdgeA8.inc \
	tomsmocomp/SearchLoopOddA.inc tomsmocomp/SearchLoopOddA2.inc \
	tomsmocomp/SearchLoopOddA6.inc tomsmocomp/SearchLoopOddAH.inc \
	tomsmocomp/SearchLoopOddAH2.inc tomsmocomp/SearchLoopTop.inc \
	tomsmocomp/SearchLoopVA.inc tomsmocomp/SearchLoopVAH.inc \
	tomsmocomp/StrangeBob.inc tomsmocomp/TomsMoCompAll.inc \
	tomsmocomp/TomsMoCompAll2.inc tomsmocomp/WierdBob.inc \
	tomsmocomp/tomsmocompmacros.h x86-64_macros.inc

noinst_HEADERS = plugins.h greedyhmacros.h

if DEBUG_BUILD
debug_sources   = greedy2frame.c
nodebug_sources =
else
debug_sources   =
nodebug_sources = greedy2frame.c
endif

# per-object CFLAGS -- drop optimization on kdetv_greedyh.c so that gcc
# doesn't run out of general registers trying to compile it.

noinst_LTLIBRARIES = libdeinterlacepluginsO1.la libdeinterlaceplugins.la
libdeinterlacepluginsO1_la_SOURCES = kdetv_greedyh.c $(debug_sources)
libdeinterlacepluginsO1_la_CFLAGS  = $(O1_CFLAGS) $(AM_CFLAGS)

libdeinterlaceplugins_la_SOURCES = double.c greedy.c linear.c linearblend.c \
				   vfir.c weave.c scalerbob.c kdetv_tomsmocomp.c \
				   $(nodebug_sources)
libdeinterlaceplugins_la_LIBADD  = $(XINE_LIB) libdeinterlacepluginsO1.la
libdeinterlaceplugins_la_CFLAGS  = $(DEFAULT_OCFLAGS) $(AM_CFLAGS) $(AVUTIL_CFLAGS)
libdeinterlaceplugins_la_LDFLAGS = $(AM_LDFLAGS) $(xineplug_ldflags)
