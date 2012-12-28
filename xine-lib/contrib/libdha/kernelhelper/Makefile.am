include $(top_srcdir)/misc/Makefile.quiet
include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README dhahelper.c

KCFLAGS = -O2 -Wall -D__KERNEL__ -DMODULE -include `echo $(LINUX_INCLUDE) | sed -e 's/\-I//g'`/linux/modversions.h

KVERSION = $(shell $(SHELL) -c 'uname -r')
moddir = /lib/modules/$(KVERSION)/misc

KCOMPILE = $(CC) $(CFLAGS) $(KCFLAGS) $(INCLUDES) $(LINUX_INCLUDE)

if HAVE_LINUX
KERNEL_MODULE = dhahelper.o
endif

noinst_HEADERS = dhahelper.h

EXTRA_PROGRAMS = test
test_SOURCES = test.c

dhahelper.o:
	$(KCOMPILE) -c `test -f $*.c || echo '$(srcdir)/'`$*.c

nodes:
	$(MKNOD) -m 666 /dev/dhahelper c 252 0

all: $(KERNEL_MODULE)

install-exec-local: $(KERNEL_MODULE)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(moddir)
	@list='$(KERNEL_MODULE)'; \
	for p in $$list; do \
	  if test -f $$p; then \
	    echo "$(INSTALL) -o root -g root -m 644 $$p $(DESTDIR)$(moddir)/$$p"; \
	    $(INSTALL) -o root -g root -m 644 $$p $(DESTDIR)$(moddir)/$$p; \
	  else :; fi; \
	done; \
	$(DEPMOD) -a
	if test ! -c /dev/dhahelper; then \
	  $(MAKE) nodes; \
	fi

uninstall-local:
	@$(NORMAL_UNINSTALL)
	@list='$(KERNEL_MODULE)'; \
	for p in $$list; do \
	  echo "rm -f $(DESTDIR)$(moddir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	  rm -f $(DESTDIR)$(moddir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	done
