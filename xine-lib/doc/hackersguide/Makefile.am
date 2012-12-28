include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

hackersguide_docbook = hackersguide.docbook \
		    intro.docbook \
		    library.docbook \
		    overview.docbook \
		    internals.docbook \
		    stream.docbook \
		    output.docbook
hackersguide_svg = architecture.svg \
		   library.svg \
		   overlays.svg \
		   post_frame.svg

hackersguidedir = $(htmldir)/hackersguide
hackersguide_DATA = hackersguide.html \
            architecture.png library.png overlays.png post_frame.png

EXTRA_DIST = README $(hackersguide_docbook) $(hackersguide_svg) \
	 $(hackersguide_DATA)

DISTCLEANFILES = $(hackersguide_DATA)

SUFFIXES = .png .fig

docs: $(hackersguide_DATA)

clean-docs:
	rm -f $(hackersguide_DATA)

dist-hook:
	@if test x"$(distcheck_lax)" = x ; then \
	  $(MAKE) fail_if_missing=yes docs ; \
	else \
	  $(MAKE) docs ; \
	fi

distclean-local: clean-docs

AM_V_FAKE = $(am__v_FAKE_$(V))
am__v_FAKE_ = $(am__v_FAKE_$(AM_DEFAULT_VERBOSITY))
am__v_FAKE_0 = @echo "  FAKE  " $@;

if HAVE_XMLTO
hackersguide.html: $(hackersguide_docbook)
	$(AM_V_GEN)xmlto xhtml-nochunks "$(srcdir)/hackersguide.docbook"
else
hackersguide.html: $(hackersguide_docbook)
	$(AM_V_FAKE)if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install xmlto."; \
	  exit 1; \
	fi; \
	if test x"$(fail_if_missing)" != x"yes"; then \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif
	$(AM_V_at)$(SED) -i -e '/<img [^>]*alt=/! s/<img /<img alt="" /g' $@ # we want valid XHTML 1.1

if HAVE_RSVG
if HAVE_RSVG_CONVERT
.svg.png:
	$(AM_V_GEN)rsvg-convert -f png -o $@ $^
else
.svg.png:
	$(AM_V_GEN)rsvg -f png $^ $@
endif
if HAVE_OPTIPNG
	$(AM_V_at)optipng -quiet $@
endif
else
.svg.png:
	$(AM_V_FAKE)if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install rsvg-convert."; \
	  exit 1; \
	fi; \
	if test x"$(fail_if_missing)" != x"yes"; then \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif
