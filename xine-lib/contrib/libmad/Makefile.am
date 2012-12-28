include $(top_srcdir)/misc/Makefile.quiet
AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_CPPFLAGS = -DOPT_SPEED

EXTRA_DIST = COPYING

if ENABLE_MAD
if !WITH_EXTERNAL_MAD
noinst_LTLIBRARIES = libmad.la
endif
endif

##
## libmad - MPEG audio decoder library
## Copyright (C) 2000-2004 Underbit Technologies, Inc.
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
##
## $Id: Makefile.am,v 1.23 2004/02/17 02:02:03 rob Exp $
##

exported_headers =	version.h fixed.h bit.h timer.h stream.h frame.h  \
			synth.h decoder.h

headers =		$(exported_headers)  \
			global.h layer12.h layer3.h huffman.h

data_includes =		D.dat imdct_s.dat qc_table.dat rq_table.dat  \
			sf_table.dat

libmad_la_SOURCES =	version.c fixed.c bit.c timer.c stream.c frame.c  \
			synth.c decoder.c layer12.c layer3.c huffman.c  \
			$(headers) $(data_includes)
