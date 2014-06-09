SUBDIRS = countries fonts defaults extensions keymaps skin_default hw_info

dist_pkgdata_DATA = \
	defaultsatlists.xml \
	defaultwizard.xml \
	dreampackagewizard.xml \
	encoding.conf \
	keymap.xml \
	menu.xml \
	prev.png \
	radio.mvi \
	black.mvi \
	rcpositions.xml \
	setup.xml \
	skin_default.xml \
	skin_subtitles.xml \
	skin.xml \
	startwizard.xml \
	freesat.t1 \
	freesat.t2 \
	unicable.xml \
	xine.conf

if HAVE_TEXTLCD
dist_pkgdata_DATA += skin_text.xml
else
if HAVE_COLORLCD
dist_pkgdata_DATA += skin_display96.xml
else
dist_pkgdata_DATA += skin_display.xml
endif
endif

skin_display.xml: skin_display_default.xml
	test -f skin_display_$(BOXTYPE).xml && cp skin_display_$(BOXTYPE).xml skin_display.xml || cp skin_display_default.xml skin_display.xml
