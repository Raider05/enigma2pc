###
## Process this file with automake to produce Makefile.in
##

include $(top_srcdir)/misc/Makefile.quiet

AUTOMAKE_OPTIONS = 1.9 no-dist-gzip dist-xz

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = doc m4 po include lib src win32 contrib misc

DEBFILES = debian/README.Debian debian/changelog debian/control \
	debian/copyright debian/rules debian/compat \
	debian/shlibdeps.sh debian/libxine2-dev.install debian/libxine1.install

EXTRA_DIST = config.rpath autogen.sh \
	     configure \
	     config.guess \
	     config.sub \
	     INSTALL \
	     install-sh \
	     libtool \
	     ltmain.sh \
	     missing \
	     depcomp

CLEANFILES = a.out
CONFIG_CLEAN_FILES = libtool po/Makevars.extra

dist_doc_DATA = COPYING NEWS README TODO CREDITS ChangeLog

docs:
	@cd doc && $(MAKE) $@

distcheck-lax:
	@$(MAKE) distcheck_lax=true distcheck

pass1:
	@$(MAKE) MULTIPASS_CFLAGS='$(PASS1_CFLAGS)'

pass2:
	@$(MAKE) MULTIPASS_CFLAGS='$(PASS2_CFLAGS)'


prune-cache:
	-rm -rf config.cache autom4te.cache


release-check:
	@./config.status misc/relchk.sh
	@mv -f .cvsversion .cvsversion.tmp
	@./autogen.sh noconfig && $(SHELL) misc/relchk.sh
	@mv -f .cvsversion.tmp .cvsversion


slackbuild:
	@(cd misc && $(MAKE) SlackBuild) && \
	 PREFIX="/usr" misc/SlackBuild


install-exec-hook:
	find $(top_builddir) -name \*.so | \
	  xargs $(STRINGS) -a | \
	  $(EGREP) '^([-a-z]+/[-+.a-z0-9]+:[^:]+:[^;]+;[ \t]*)+' | \
	  sed -e 's/:[^;]\+;/\n/g' | \
	  sort -u | \
	  sed -e '1 d' >$(DESTDIR)$(XINE_PLUGINDIR)/mime.types

uninstall-hook:
	rm -f $(DESTDIR)$(XINE_PLUGINDIR)/mime.types


install-data-hook:
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*.la
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*/*.la
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*.dll.a
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*/*.dll.a
	@if test -x "$(top_srcdir)/post-install.sh" ; then \
	  $(top_srcdir)/post-install.sh ; \
	fi

dist-hook:
	cp -p $(srcdir)/version.sh $(distdir)


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*
	-rm -f $(PACKAGE)_$(VERSION).tar.gz $(PACKAGE)_$(VERSION).tar.bz2
	-rm -f $(distdir).tar.gz $(PACKAGE).tgz package_descriptions
	-rm -rf $(distdir)


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in configure acinclude.m4 aclocal.m4
	-rm -f stamp-h.in ltconfig ltmain.sh
	-rm -f config.guess config.sub install-sh missing mkinstalldirs
	-rm -f depcomp config.log

maintainer-clean-generic-hook:
	rm -f config.status

$(top_srcdir)/configure:  $(am__configure_deps)
	cd $(srcdir) && $(AUTOCONF) && $(SED) -i -e '/gnu_ld/,/;;/ s/--rpath \$${wl}/--rpath,/' $@
