dnl Detection of the Pthread implementation flags and libraries
dnl Diego Pettenò <flameeyes-aBrp7R+bbdUdnm+yROfE0A@public.gmane.org> 2006-11-03
dnl
dnl CC_PTHREAD_FLAGS([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl This macro checks for the Pthread flags to use to build
dnl with support for PTHREAD_LIBS and PTHREAD_CFLAGS variables
dnl used in FreeBSD ports.
dnl
dnl This macro is released as public domain, but please mail
dnl to flameeyes@gmail.com if you want to add support for a
dnl new case, or if you're going to use it, so that there will
dnl always be a version available.
AC_DEFUN([CC_PTHREAD_FLAGS], [
  AC_REQUIRE([CC_CHECK_WERROR])
  AC_ARG_VAR([PTHREAD_CFLAGS], [C compiler flags for Pthread support])
  AC_ARG_VAR([PTHREAD_LIBS], [linker flags for Pthread support])

  dnl if PTHREAD_* are not set, default to -pthread (GCC)
  if test "${PTHREAD_CFLAGS-unset}" = "unset"; then
     case $host in
       *-mingw*)  PTHREAD_CFLAGS=""		;;
       *-cygwin*) PTHREAD_CFLAGS=""		;;
       *-hpux11*) PTHREAD_CFLAGS=""		;;
       *-darwin*) PTHREAD_CFLAGS=""		;;
       *-solaris*|*-linux-gnu)
                  dnl Handle Sun Studio compiler (also on Linux)
                  CC_CHECK_CFLAGS([-mt], [PTHREAD_CFLAGS="-mt"]);;

       *)	  PTHREAD_CFLAGS="-pthread"	;;
     esac
  fi
  if test "${PTHREAD_LIBS-unset}" = "unset"; then
     case $host in
       *-mingw*)  PTHREAD_LIBS="-lpthreadGC2"	;;
       *-cygwin*) PTHREAD_LIBS="-lpthread"	;;
       *-hpux11*) PTHREAD_LIBS="-lpthread"	;;
       *-darwin*) PTHREAD_LIBS=""		;;
       *-solaris*)
                  dnl Use the same libraries for gcc and Sun Studio cc
                  PTHREAD_LIBS="-lpthread -lposix4 -lrt";;
       *)	  PTHREAD_LIBS="-pthread"	;;
     esac

     dnl Again, handle Sun Studio compiler
     if test "x${PTHREAD_CFLAGS}" = "x-mt"; then
        PTHREAD_LIBS="-mt"
     fi
  fi

  AC_CACHE_CHECK([if $CC supports Pthread],
    AS_TR_SH([cc_cv_pthreads]),
    [ac_save_CFLAGS="$CFLAGS"
     ac_save_LIBS="$LIBS"
     CFLAGS="$CFLAGS $cc_cv_werror $PTHREAD_CFLAGS"
     LIBS="$LIBS $PTHREAD_LIBS"
     AC_LINK_IFELSE(
       [AC_LANG_PROGRAM(
          [[#include <pthread.h>
	    void *fakethread(void *arg) { (void)arg; return NULL; }
	    pthread_t fakevariable;
	  ]],
          [[pthread_create(&fakevariable, NULL, &fakethread, NULL);]]
        )],
       [cc_cv_pthreads=yes],
       [cc_cv_pthreads=no])
     CFLAGS="$ac_save_CFLAGS"
     LIBS="$ac_save_LIBS"
    ])

  AC_SUBST([PTHREAD_LIBS])
  AC_SUBST([PTHREAD_CFLAGS])

  if test x$cc_cv_pthreads = xyes; then
    ifelse([$1], , [:], [$1])
  else
    ifelse([$2], , [:], [$2])
  fi
])

AC_DEFUN([CC_PTHREAD_RECURSIVE_MUTEX], [
  AC_REQUIRE([CC_PTHREAD_FLAGS])
  AC_CACHE_CHECK(
    [for recursive mutex support in pthread],
    [cc_cv_pthread_recursive_mutex],
    [ac_save_CFLAGS="$CFLAGS"
     ac_save_LIBS="$LIBS"
     CFLAGS="$CFLAGS $cc_cv_werror $PTHREAD_CFLAGS"
     LIBS="$LIBS $PTHREAD_LIBS"
     AC_COMPILE_IFELSE(
       [AC_LANG_PROGRAM([
#include <pthread.h>
          ], [
    pthread_mutexattr_t attr;
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
           ])
	  ],
	  [cc_cv_pthread_recursive_mutex=yes],
	  [cc_cv_pthread_recursive_mutex=no])
     CFLAGS="$ac_save_CFLAGS"
     LIBS="$ac_save_LIBS"
    ])

  AS_IF([test x"$cc_cv_pthread_recursive_mutex" = x"yes"],
    [$1], [$2])
])
