dnl AC_PROG_GMSGFMT_PLURAL
dnl ----------------------
dnl Validate the GMSGFMT program found by gettext.m4; reject old versions
dnl of GNU msgfmt that do not support the "msgid_plural" extension.
AC_DEFUN([AC_PROG_GMSGFMT_PLURAL],
 [dnl AC_REQUIRE(AM_GNU_GETTEXT)

  if test "$GMSGFMT" != ":"; then
    AC_MSG_CHECKING([for plural forms in GNU msgfmt])

    changequote(,)dnl We use [ and ] in in .po test input

    dnl If the GNU msgfmt does not accept msgid_plural we define it
    dnl as : so that the Makefiles still can work.
    cat >conftest.po <<_ACEOF
msgid "channel"
msgid_plural "channels"
msgstr[0] "canal"
msgstr[1] "canal"

_ACEOF
    changequote([,])dnl

    if $GMSGFMT -o /dev/null conftest.po >/dev/null 2>&1; then
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
      AC_MSG_RESULT(
	[found GNU msgfmt program is too old, it does not support plural forms; ignore it])
      GMSGFMT=":"
    fi
    rm -f conftest.po
  fi
])dnl AC_PROG_GMSGFMT_PLURAL


# AC_PROG_LIBTOOL_SANITYCHECK
# ----------------------
# Default configuration of libtool on solaris produces non-working
# plugin modules, when gcc is used as compiler, and gcc does not
# use gnu-ld
AC_DEFUN([AC_PROG_LIBTOOL_SANITYCHECK],
 [dnl AC_REQUIRE(AC_PROG_CC)
  dnl AC_REQUIRE(AC_PROG_LD)
  dnl AC_REQUIRE(AC_PROG_LIBTOOL)

  case $host in
  *-*-solaris*)
    if test "$GCC" = yes && test "$with_gnu_ld" != yes; then
      AC_MSG_CHECKING([if libtool can build working modules])
      cat > conftest1.c <<_ACEOF
#undef NDEBUG
#include <assert.h>
int shlib_func(long long a, long long b) {
  assert(b);
  switch (a&3) {
  case 0: return a/b;
  case 1: return a%b;
  case 2: return (unsigned long long)a/b;
  case 3: return (unsigned long long)a%b;
  }
}
_ACEOF

      cat > conftest2.c <<_ACEOF
#include <dlfcn.h>
int main(){
  void *dl = dlopen(".libs/libconftest.so", RTLD_NOW);
  if (!dl) printf("%s\n", dlerror());
  exit(dl ? 0 : 1);
}
_ACEOF

      if ./libtool $CC -c conftest1.c >/dev/null 2>&1 && \
         ./libtool $CC -o libconftest.la conftest1.lo \
		 -module -avoid-version -rpath /tmp  >/dev/null 2>&1 && \
         ./libtool $CC -o conftest2 conftest2.c -ldl >/dev/null 2>&1
      then
        if ./conftest2 >/dev/null 2>&1; then
          AC_MSG_RESULT(yes)
        else
	  dnl typical problem: dlopen'ed module not self contained, because
	  dnl it wasn't linked with -lgcc
	  AC_MSG_RESULT(no)
	  if grep '^archive_cmds=.*$LD -G' libtool >/dev/null; then
            AC_MSG_CHECKING([if libtool can be fixed])

	    dnl first try to update gcc2's spec file to add the
	    dnl gcc3 -mimpure-text flag

	    libtool_specs=""

	    if $CC -dumpspecs | grep -- '-G -dy -z text' >/dev/null; then
	      $CC -dumpspecs | \
		  sed 's/-G -dy -z text/-G -dy %{!mimpure-text:-z text}/g' \
		  > gcc-libtool-specs
	      libtool_specs=" -specs=`pwd`/gcc-libtool-specs"
	    fi

	    sed -e "s,\$LD -G,\$CC${libtool_specs} -shared -mimpure-text,g" \
		-e 's/ -M / -Wl,-M,/' libtool >libtool-fixed
	    chmod +x libtool-fixed
            if ./libtool-fixed $CC -o libconftest.la conftest1.lo \
		    -module -avoid-version -rpath /tmp  >/dev/null 2>&1 && \
	       ./conftest2 >/dev/null 2>&1; then

	      dnl the fixed libtool works
	      AC_MSG_RESULT(yes)
	      mv -f libtool-fixed libtool

            else
	      AC_MSG_RESULT(no)
            fi
	  fi
        fi
      else
        AC_MSG_RESULT(no)
      fi
      rm -f conftest1.c conftest1.lo conftest1.o conftest2.c \
		libconftest.la conftest libtool-fixed
      rm -rf .libs
    fi ;;
  esac
])# AC_PROG_LIBTOOL_SANITYCHECK
