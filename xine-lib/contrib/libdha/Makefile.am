include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

SUBDIRS = bin oth sysdep

if BUILD_DHA_KMOD
SUBDIRS += kernelhelper
endif

AM_CFLAGS = $(DEFAULT_OCFLAGS)

EXTRA_DIST = README pci_db2c.awk

awk_generated = pci_dev_ids.c pci_ids.h pci_names.c pci_names.h pci_vendors.h
CLEANFILES = $(awk_generated)

noinst_HEADERS = AsmMacros.h libdha.h pci_ids.h pci_names.h pci_vendors.h

if ENABLE_VIDIX
noinst_LTLIBRARIES = libdha.la
endif

libdha_la_SOURCES = libdha.c mtrr.c pci.c mmi.c ports.c irq.c cpu_flush.c
nodist_libdha_la_SOURCES = pci_names.c 

EXTRA_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = libdha.la

## for OpenBSD LIBS += -li386

## We have to create some files, on the fly, this is why this rule is needed.
pci_db2c.awk:
oth/pci.db:

$(awk_generated): pci_db2c.awk oth/pci.db
	LC_ALL=C $(AWK) -f $(srcdir)/pci_db2c.awk \
		$(srcdir)/oth/pci.db

pci_names.lo: $(awk_generated)
	source='$*.c' object='$@' libtool=yes \
	depfile='$(DEPDIR)/$*.Plo' tmpdepfile='$(DEPDIR)/$*.TPlo' \
	$(CCDEPMODE) $(depcomp) \
	$(LTCOMPILE) -c -o $@ `test -f $*.c || echo '$(srcdir)/'`$*.c
