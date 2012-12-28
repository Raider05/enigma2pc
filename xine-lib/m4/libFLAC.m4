# Configure paths for libFLAC
# "Inspired" by ogg.m4

dnl AM_PATH_LIBFLAC([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libFLAC, and define LIBFLAC_CFLAGS and LIBFLAC_LIBS
dnl
AC_DEFUN([AM_PATH_LIBFLAC],
[dnl
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(libFLAC-prefix, AS_HELP_STRING([--with-libFLAC-prefix=DIR], [prefix where libFLAC is installed (optional)]), libFLAC_prefix="$withval", libFLAC_prefix="")
AC_ARG_WITH(libFLAC-libraries, AS_HELP_STRING([--with-libFLAC-libraries=DIR], [directory where libFLAC library is installed (optional)]), libFLAC_libraries="$withval", libFLAC_libraries="")
AC_ARG_WITH(libFLAC-includes, AS_HELP_STRING([--with-libFLAC-includes=DIR], [directory where libFLAC header files are installed (optional)]), libFLAC_includes="$withval", libFLAC_includes="")
AC_ARG_ENABLE(libFLACtest, AS_HELP_STRING([--disable-libFLACtest], [do not try to compile and run a test libFLAC program]), enable_libFLACtest=$enableval, enable_libFLACtest=yes)

    AC_MSG_CHECKING([libdir name])
    case $host_or_hostalias in
    *-*-linux*)
     # Test if the compiler is 64bit
     echo 'int i;' > conftest.$ac_ext
     xine_cv_cc_64bit_output=no
     if AC_TRY_EVAL(ac_compile); then
     case `"$MAGIC_CMD" conftest.$ac_objext` in
     *"ELF 64"*)
       xine_cv_cc_64bit_output=yes
       ;;
     esac
     fi
     rm -rf conftest*
     ;;
    esac

    case $host_cpu:$xine_cv_cc_64bit_output in
    powerpc64:yes | s390x:yes | sparc64:yes | x86_64:yes)
     XINE_LIBNAME="lib64"
     ;;
    *:*)
     XINE_LIBNAME="lib"
     ;;
    esac
    AC_MSG_RESULT([$XINE_LIBNAME])

  if test "x$libFLAC_libraries" != "x" ; then
    LIBFLAC_LIBS="-L$libFLAC_libraries"
  elif test "x$libFLAC_prefix" != "x" ; then
    LIBFLAC_LIBS="-L$libFLAC_prefix/$XINE_LIBNAME"
  elif test "x$prefix" != "xNONE" ; then
    LIBFLAC_LIBS="-L$prefix/$XINE_LIBNAME"
  fi

  LIBFLAC_LIBS="$LIBFLAC_LIBS -lFLAC -lm"

  if test "x$libFLAC_includes" != "x" ; then
    LIBFLAC_CFLAGS="-I$libFLAC_includes"
  elif test "x$libFLAC_prefix" != "x" ; then
    LIBFLAC_CFLAGS="-I$libFLAC_prefix/include"
  elif test "$prefix" != "xNONE"; then
    LIBFLAC_CFLAGS="-I$prefix/include"
  fi

  AC_MSG_CHECKING(for libFLAC)
  no_libFLAC=""


  if test "x$enable_libFLACtest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_CXXFLAGS="$CXXFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $LIBFLAC_CFLAGS"
    CXXFLAGS="$CXXFLAGS $LIBFLAC_CFLAGS"
    LIBS="$LIBS $LIBFLAC_LIBS"
dnl
dnl Now check if the installed libFLAC is sufficiently new.
dnl
      rm -f conf.libFLACtest
      AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FLAC/format.h>

int main ()
{
  system("touch conf.libFLACtest");
  return 0;
}

]])],[],[no_libFLAC=yes],[no_libFLAC=cc])
      if test "x$no_libFLAC" = xcc; then
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
#include <FLAC/format.h>
]], [[ return 0; ]])],[no_libFLAC=''],[no_libFLAC=yes])
      fi
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_libFLAC" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])
  else
     AC_MSG_RESULT(no)
     if test -f conf.libFLACtest ; then
       :
     else
       echo "*** Could not run libFLAC test program, checking why..."
       CFLAGS="$CFLAGS $LIBFLAC_CFLAGS"
       LIBS="$LIBS $LIBFLAC_LIBS"
       AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
#include <FLAC/format.h>
]], [[ return 0; ]])],
     [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding libFLAC or finding the wrong"
       echo "*** version of libFLAC. If it is not finding libFLAC, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
     [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means libFLAC was incorrectly installed"
       echo "*** or that you have moved libFLAC since it was installed. In the latter case, you"
       echo "*** may want to edit the libFLAC-config script: $LIBFLAC_CONFIG" ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     LIBFLAC_CFLAGS=""
     LIBFLAC_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(LIBFLAC_CFLAGS)
  AC_SUBST(LIBFLAC_LIBS)
  rm -f conf.libFLACtest
])
