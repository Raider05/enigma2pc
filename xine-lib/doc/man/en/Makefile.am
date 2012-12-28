include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

STATICMANS = xine-config.1 xine.5
DYNAMICMANS = xine-list-@XINE_SERIES@.1

man_MANS = $(STATICMANS) $(DYNAMICMANS)

BUILT_SOURCES = $(DYNAMICMANS)
DISTCLEANFILES = $(DYNAMICMANS)
EXTRA_DIST = $(STATICMANS) xine-list.1.in

xine-list-@XINE_SERIES@.1: xine-list.1.in
	$(SED) -e 's/@XL@/xine\-list\-@XINE_SERIES@/' $< >$@
