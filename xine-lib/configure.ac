AC_PREREQ(2.59)

m4_include([m4/objc.m4])

dnl Note that autoconf/autoheader/automake cache using autom4te, so version.sh
dnl will only be run if configure.ac has changed.  This must be done before
dnl AC_INIT so that XINE_VERSION_SPEC, which is an m4 macro, is available.
m4_esyscmd([./version.sh])

dnl Initialize autoconf, autoheader, and automake
AC_INIT([xine-lib], XINE_VERSION_SPEC, [xine-bugs@lists.sourceforge.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/xine-engine/xine.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_HEADERS([include/configure.h])
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AH_TOP([#ifndef __XINE_LIB_CONFIG_H__
#define __XINE_LIB_CONFIG_H__ 1
])
AH_BOTTOM([#ifdef ASMALIGN_1SLN
# define ASMALIGN(ZEROBITS) ".align " #ZEROBITS "\n\t"
#else
# define ASMALIGN(ZEROBITS) ".align 1<<" #ZEROBITS "\n\t"
#endif

#endif  /* __XINE_LIB_CONFIG_H__ */
])

test x"$prefix"      = x"NONE" && prefix="${ac_default_prefix}"
test x"$exec_prefix" = x"NONE" && exec_prefix='${prefix}'

dnl Use features of autoconf 2.61, but stay compatible with older versions.
if test x"$datarootdir" = x""; then
   datarootdir='${datadir}'
   AC_SUBST(datarootdir)
fi

if test x"$docdir" = x""; then
   docdir='${datarootdir}/doc/${PACKAGE}'
   AC_SUBST(docdir)
fi

if test x"$htmldir" = x""; then
   htmldir='${docdir}'
   AC_SUBST(htmldir)
fi


dnl -------------------------
dnl Setup version information
dnl -------------------------

dnl Do not change these manually; they come from running ./version.sh when
dnl autoconf runs.  This must all be done after AC_INIT is done, but running
dnl the version.sh script must be done before AC_INIT.
XINE_MAJOR=XINE_VERSION_MAJOR
AC_SUBST(XINE_MAJOR)
AC_DEFINE_UNQUOTED([XINE_MAJOR], [$XINE_MAJOR], [xine major version number])
XINE_MINOR=XINE_VERSION_MINOR
AC_SUBST(XINE_MINOR)
AC_DEFINE_UNQUOTED([XINE_MINOR], [$XINE_MINOR], [xine minor version number])
XINE_SUB=XINE_VERSION_SUB
AC_SUBST(XINE_SUB)
AC_DEFINE_UNQUOTED([XINE_SUB],   [$XINE_SUB],   [xine sub version number])
XINE_PATCH=XINE_VERSION_PATCH
AC_SUBST(XINE_PATCH)
AC_DEFINE_UNQUOTED([XINE_PATCH], [$XINE_PATCH], [xine patch version number])
XINE_SERIES=XINE_VERSION_SERIES
AC_SUBST(XINE_SERIES)

XINE_LT_CURRENT=__XINE_LT_CURRENT
AC_SUBST(XINE_LT_CURRENT)
AC_DEFINE_UNQUOTED(XINE_LT_CURRENT, $XINE_LT_CURRENT, [xine interface version number])
XINE_LT_REVISION=__XINE_LT_REVISION
AC_SUBST(XINE_LT_REVISION)
XINE_LT_AGE=__XINE_LT_AGE
AC_SUBST(XINE_LT_AGE)
AC_DEFINE_UNQUOTED(XINE_LT_AGE, $XINE_LT_AGE, [xine interface version age])

LIBNAME="libxine$(($XINE_LT_CURRENT-$XINE_LT_AGE))"
AC_SUBST(LIBNAME)
AC_DEFINE_UNQUOTED([XINE_TEXTDOMAIN], "$LIBNAME", [catalog message text domain])

dnl Always set flags to begin with -g so that debug information will be
dnl included.  In release bulids, this can be stripped out later if desired.
dnl More importantly, it prevents autoconf from initializing the defaults to
dnl include -O2, which is not desirable in a debug build, and it messes with
dnl other optimizations that we'll want to be setting ourselves later.
CFLAGS="-g $CFLAGS"
CPPFLAGS="-D_REENTRANT -DXINE_COMPILE $CPPFLAGS"
LDFLAGS="-g $LDFLAGS"
OBJCFLAGS="-g $OBJCFLAGS"


dnl ------------------------------
dnl Build modes: debug and profile
dnl ------------------------------

AC_ARG_ENABLE([debug],
              [AS_HELP_STRING([--enable-debug], [build with debugging code enabled])],
              [], [enable_debug="no"])
if test x"$enable_debug" != x"no"; then
    CPPFLAGS="-DDEBUG $CPPFLAGS"
else
    CPPFLAGS="-DNDEBUG $CPPFLAGS"
fi
AM_CONDITIONAL([DEBUG_BUILD], [test x"$enable_debug" != x"no"])

AC_ARG_ENABLE([profiling],
              [AS_HELP_STRING([--enable-profiling], [build with profiling code enabled])],
              [], [enable_profiling="no"])
if test x"$enable_profiling" != x"no"; then
    CFLAGS="-pg $CFLAGS"
    OBJCFLAGS="-pg $OBJCFLAGS"
    LDFLAGS="-pg $LDFLAGS"
fi
AM_CONDITIONAL([PROFILING_BUILD], [test x"$enable_profiling" != x"no"])


dnl --------------
dnl Build features
dnl --------------

AC_ARG_ENABLE([ipv6],
              [AS_HELP_STRING([--enable-ipv6], [enable use of IPv6])],
              [if test x"$enableval" != x"no"; then
                   AC_DEFINE([ENABLE_IPV6], 1, [Enable this when IPv6 is requested])
               fi])

AC_ARG_ENABLE([antialiasing],
              [AS_HELP_STRING([--enable-antialiasing], [enable font antialiasing])],
              [if test x"$enableval" != x"no"; then
                   AC_DEFINE([ENABLE_ANTIALIASING], 1, [Define this to 1 to enable font antialising.])    
               fi])


dnl ----------------------------------------------
dnl Cross compilation, Mac OS X Universal Binaries
dnl ----------------------------------------------

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

dnl check for Mac OS X universal binary support early, because certain flags
dnl must be set prior to looking for cc/libtool, etc.
MACOSX_UNIVERSAL_BINARIES

dnl Check to see if $host is empty.  If it is, try $host_alias instead.
dnl If $host is empty, it's because the user has run ./configure with a host
dnl parameter unknown to config.sub.  This used to be set in xv handling, but
dnl it's also used in a bunch of other places unrelated to Xwindows support,
dnl so if X is disabled, other things could go badly.
host_or_hostalias="$host"
test x"$host_or_hostalias" = x"" && host_or_hostalias="$host_alias"


dnl -------------------
dnl checks for programs
dnl -------------------

AC_PROG_CC
AM_PROG_CC_C_O
AC_GNU_SOURCE
AC_ISC_POSIX

AC_PROG_OBJC

CC_PROG_AS

AC_PROG_MAKE_SET
AC_PROG_EGREP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_AWK

AC_PATH_PROG([PERL], [perl], [no])
if test "$PERL" = no; then
  AC_MSG_ERROR([perl not found])
fi
AC_SUBST([PERL])
     
AC_CHECK_TOOL([STRINGS], [strings], [false])

PKG_PROG_PKG_CONFIG

dnl ---------------------------------------------
dnl Libtool
dnl ---------------------------------------------

AC_DISABLE_STATIC
AC_LIBTOOL_DLOPEN
AC_LIBTOOL_WIN32_DLL
AC_PATH_MAGIC
AC_PROG_LIBTOOL
AC_PROG_LIBTOOL_SANITYCHECK

dnl --------------------
dnl checks for libraries
dnl --------------------

AM_ICONV
AC_ARG_ENABLE([iconvtest],
              [AS_HELP_STRING([--disable-iconvtest], [don't require iconv library])])
if test x"$enable_iconvtest" != x"no"; then
    if test x"$am_cv_func_iconv" != x"yes"; then
        AC_MSG_ERROR([
****************************************************************
* iconv library not found. It's necessary for proper           *
* manipulation with texts so xine requires it as default.      *
*                                                              *
* You need to install iconv library or to specify prefix       *
* by option --with-libiconv-prefix.                            *
*                                                              *
* If you don't want iconv support use the option               *
* --disable-iconvtest.                                         *
****************************************************************
])
    fi
fi

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.17])
AC_PROG_GMSGFMT_PLURAL

AC_CHECK_LIB([c], [dlopen], [DYNAMIC_LD_LIBS=""],
             [AC_CHECK_LIB([dl], [dlopen], [DYNAMIC_LD_LIBS="-ldl"],
                           [AC_MSG_CHECKING(for dlopen under win32)
                            AC_LANG_PUSH([C])
                            ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="-I${srcdir}/win32/include $CPPFLAGS"
                            ac_save_LIBS="$LIBS" LIBS="$LIBS -lkernel32"
                            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stddef.h>
                                                                 #include <dlfcn.h>]], [[dlopen(NULL, 0)]])],
                                              [DYNAMIC_LD_LIBS="-lkernel32"
                                               AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no])])
                            CPPFLAGS="$ac_save_CPPFLAGS" LIBS="$ac_save_LIBS"
                            AC_LANG_POP([C])], [AC_MSG_ERROR([dynamic linker needed])])])
AC_SUBST([DYNAMIC_LD_LIBS])

AC_ARG_WITH([external-libxdg-basedir],
            [AS_HELP_STRING([--without-external-libxdg-basedir], [use internal copy of libxdg-basedir])])

have_xdg_basedir=no
if test x"$with_external_libxdg_basedir" != x"no"; then
    PKG_CHECK_MODULES([XDG_BASEDIR], [libxdg-basedir >= 1], [have_xdg_basedir=yes], [have_xdg_basedir=no])

    if test x"$have_xdg_basedir" = x"no" -a x"$with_external_libxdg_basedir" = x"yes"; then
	AC_MSG_ERROR([--with-external-libxdg-basedir used but no libxdg-basedir found])
    fi
fi
if test x"$have_xdg_basedir" = xyes; then
    XDG_BASEDIR_REQUIRES="libxdg-basedir"
else
    XDG_BASEDIR_CPPFLAGS='-I$(top_srcdir)/contrib/libxdg-basedir'
    XDG_BASEDIR_LIBS='$(top_builddir)/contrib/libxdg-basedir/libxdg-basedir.la'
    XDG_BASEDIR_DEPS='$(top_builddir)/contrib/libxdg-basedir/libxdg-basedir.la'
fi

AC_SUBST([XDG_BASEDIR_REQUIRES])
AC_SUBST([XDG_BASEDIR_CPPFLAGS])
AC_SUBST([XDG_BASEDIR_LIBS])
AC_SUBST([XDG_BASEDIR_DEPS])

AM_CONDITIONAL([EXTERNAL_LIBXDG_BASEDIR], [test x"$have_xdg_basedir" = xyes])

dnl Test for socket and network support library
AC_CHECK_LIB([socket], [socket],        [NET_LIBS="-lsocket $NET_LIBS"])
AC_CHECK_LIB([nsl],    [gethostbyname], [NET_LIBS="-lnsl $NET_LIBS"])
AC_CHECK_LIB([resolv], [hstrerror],     [NET_LIBS="-lresolv $NET_LIBS"])
AC_SUBST(NET_LIBS)

WINE_LIBS=""
AC_CHECK_LIB(i386, i386_set_ldt, WINE_LIBS="-li386 $WINE_LIBS",)
AC_SUBST(WINE_LIBS)

AC_CHECK_LIB([rt], [clock_getres],
             [RT_LIBS="-lrt"
              AC_DEFINE(HAVE_POSIX_TIMERS, 1, [Define this if you have POSIX timers.])], 
             [AC_MSG_RESULT([*** no POSIX timers available.])])
AC_SUBST(RT_LIBS)

AC_CHECK_LIB([posix4], [sched_get_priority_min])

AC_CHECK_LIB([kstat], [kstat_open],
             [KSTAT_LIBS=-lkstat
              AC_DEFINE([HAVE_KSTAT], 1, [Define this if you have kernel statistics available via kstat interface])])
AC_SUBST(KSTAT_LIBS)

AC_ARG_WITH([zlib-prefix], 
            [AS_HELP_STRING([--with-zlib-prefix=PREFIX], [path to zlib compression library])],
            [if test x"$withval" != x"no"; then
                 ZLIB_CPPFLAGS="-I$withval/include"
                 ZLIB_LIBS="-L$withval/lib"
             fi])

AC_CHECK_LIB([z], [gzsetparams],
             [ZLIB_LIBS="$ZLIB_LIBS -lz"
              ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $ZLIB_CPPFLAGS"
              AC_CHECK_HEADER([zlib.h], [have_zlib=yes], [have_zlib=no])
              CPPFLAGS="$ac_save_CPPFLAGS"],
             [have_zlib=no],
             ["$ZLIB_LIBS"])

test x"$have_zlib" != x"yes" && AC_MSG_ERROR(zlib needed)
AC_SUBST(ZLIB_CPPFLAGS)
AC_SUBST(ZLIB_LIBS)

dnl FreeType2 (optional; disabled by default)
AC_ARG_WITH([freetype],
	    [AS_HELP_STRING([--with-freetype], [Build with FreeType2 library])],
            [], [with_freetype=no])
if test x"$with_freetype" != x"no"; then
    PKG_CHECK_MODULES([FT2], [freetype2], [have_freetype=yes], [have_freetype=no])
    if test x"$have_freetype" = x"no"; then
        AC_MSG_ERROR([FreeType2 support requested but FreeType2 library not found])
    elif test x"$have_freetype" = x"yes"; then
        AC_DEFINE([HAVE_FT2], 1, [Define this if you have freetype2 library])
    fi
fi

dnl fontconfig (optional; disabled by default)
AC_ARG_WITH([fontconfig],
	    [AS_HELP_STRING([--with-fontconfig], [Build with fontconfig library])],
            [], [with_fontconfig=no])
if test x"$with_fontconfig" = x"yes"; then
    if test x"$have_freetype" != x"yes"; then
        AC_MSG_ERROR([fontconfig support requested, but FreeType2 not enabled.])
    fi
    PKG_CHECK_MODULES([FONTCONFIG], [fontconfig], [have_fontconfig=yes], [have_fontconfig=no])
    if test x"$have_fontconfig" = x"no"; then
        AC_MSG_ERROR([fontconfig support requested but fontconfig library not found])
    elif test x"$have_fontconfig" = x"yes"; then
        AC_DEFINE([HAVE_FONTCONFIG], 1, [Define this if you have fontconfig library])
    fi
fi


dnl -----------------------
dnl checks for header files
dnl -----------------------

AC_HEADER_STDC

AC_CHECK_HEADERS([alloca.h])
AC_CHECK_HEADERS([assert.h byteswap.h dirent.h errno.h execinfo.h fcntl.h glob.h])
AC_CHECK_HEADERS([libgen.h malloc.h netdb.h stdbool.h ucontext.h])
AC_CHECK_HEADERS([sys/ioctl.h sys/mixer.h sys/mman.h sys/param.h sys/times.h sys/wait.h])

dnl This is duplicative due to AC_HEADER_STDC, but src/input/vcd stuff needs to
dnl have HAVE_STDIO_H defined, or it won't compile.
AC_CHECK_HEADERS([stdio.h])

dnl cdrom ioctls (common for dvdnav and vcd)
case "$host_os" in
    linux*)
        AC_CHECK_HEADERS([linux/cdrom.h],
                         [AC_DEFINE([HAVE_LINUX_CDROM], 1, [Define 1 if you have Linux-type CD-ROM support])
                          AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <linux/cdrom.h>]],
                                                             [[struct cdrom_generic_command test; int has_timeout = sizeof(test.timeout);]])],
                                            [AC_DEFINE([HAVE_LINUX_CDROM_TIMEOUT], [1], [Define 1 if timeout is in cdrom_generic_command struct])])])
        ;;
esac
AC_CHECK_HEADERS([sys/dvdio.h sys/cdio.h sys/scsiio.h])

dnl ----------------------------------------------
dnl Check for NetBSD DTV headers
dnl ----------------------------------------------
AC_CHECK_HEADERS([dev/dtv/dtvio.h])

dnl ----------------
dnl checks for types
dnl ----------------

AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long])
AC_CHECK_GENERATE_INTTYPES([include])
AM_CONDITIONAL([GENERATED_INTTYPES_H], [test "x$ac_cv_header_inttypes_h" != x"yes"])
AC_CHECK_TYPE([ssize_t], [],
              [AC_DEFINE([ssize_t], [__int64], [define ssize_t to __int64 if it's missing in default includes])])
AC_CHECK_SOCKLEN_T

AC_CACHE_CHECK([type of request parameter for ioctl()], [ac_cv_ioctl_request], [
    for ac_ioctl_request_type in "unsigned long" "int"; do
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/ioctl.h>
                                          int ioctl(int fd, $ac_ioctl_request_type request, ...);]], [[]])],
                       [ac_cv_ioctl_request=$ac_ioctl_request_type], [])
    done
    if test x"$ac_cv_ioctl_request" = x""; then
        AC_MSG_ERROR([Unable to determine the type for ioctl() request parameter])
    fi
])
AC_DEFINE_UNQUOTED([IOCTL_REQUEST_TYPE], [$ac_cv_ioctl_request], [Type of the request parameter for ioctl()])


dnl ---------------------
dnl checks for structures
dnl ---------------------

AC_CHECK_MEMBER([struct tm.tm_gmtoff],
                [AC_DEFINE([HAVE_TM_GMTOFF], 1, [Define if struct tm has the tm_gmtoff member.])],
                [], [#include <time.h>])


dnl -----------------------------------
dnl checks for compiler characteristics
dnl -----------------------------------

if test x"$GCC" = x"yes"; then
    GCC_VERSION="`$CC -dumpversion`"
    GCC_VERSION_MAJOR="`echo "$GCC_VERSION" | cut -d '.' -f1`"
    GCC_VERSION_MINOR="`echo "$GCC_VERSION" | cut -d '.' -f2`"
    GCC_VERSION_PATCHLEVEL="`echo "$GCC_VERSION" | cut -d '.' -f3`"
fi

CC_CHECK_WERROR

CC_PTHREAD_FLAGS([], [AC_MSG_ERROR([Pthread support is needed])])
CC_PTHREAD_RECURSIVE_MUTEX([], [AC_MSG_ERROR([recursive mutex support is needed - please report])])
dnl <pthread.h> is implicitly included by xine-internals.h, so the include dir is needed everywhere
CPPFLAGS="$CPPFLAGS $PTHREAD_CFLAGS"

dnl REVISIT: AC_C_ALWAYS_INLINE removal allows ffmpeg to be more widely buildable
AC_C_BIGENDIAN
AC_C_CONST
AC_C_INLINE
AC_C_RESTRICT

dnl empty_array_size - src/input/vcd
AC_MSG_CHECKING([how to create empty arrays])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[struct { int foo; int bar[]; } baz]])],
                  [empty_array_size=""],
                  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[struct { int foo; int bar[0]; } baz]])],
                                     [empty_array_size="0"],
                                     [AC_MSG_ERROR([compiler is unable to create empty arrays])])])

AC_DEFINE_UNQUOTED([EMPTY_ARRAY_SIZE], [$empty_array_size], [what to put between the brackets for empty arrays])
AC_MSG_RESULT([[[$empty_array_size]]])

dnl ISOC99_PRAGMA - src/input/vcd/libvcd
AC_MSG_CHECKING([whether $CC supports ISOC99 _Pragma()])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[_Pragma("pack(1)")]])],
                  [ISOC99_PRAGMA=yes 
                   AC_DEFINE([HAVE_ISOC99_PRAGMA], [], [Supports ISO _Pragma() macro])],
                  [ISOC99_PRAGMA=no])
AC_MSG_RESULT([$ISOC99_PRAGMA])


dnl ASM ALIGN is power of two ?
dnl src/post/planar
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[asm(".align 3");]])],
                  [AC_DEFINE([ASMALIGN_1SLN], [1],
                             [define if '.align n' means alignment to (1 << n) - byte boundaries])])

dnl avx instruction set support
AC_MSG_CHECKING([for AVX assembler])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[asm("vmovaps %ymm1, %ymm0");]])],
                  [AC_DEFINE([HAVE_AVX], [1],
                             [define if compiler supports avx inline assembler])
			     AC_MSG_RESULT(yes)], [AC_MSG_RESULT(no)])

CC_ATTRIBUTE_ALIGNED

CC_ATTRIBUTE_VISIBILITY([protected],
                        [visibility_export="protected"],
                        [CC_ATTRIBUTE_VISIBILITY([default], [visibility_export="default"])])
CC_ATTRIBUTE_VISIBILITY([internal])

if test x"$visibility_export" != x""; then
    CC_FLAG_VISIBILITY([VISIBILITY_FLAG="-fvisibility=hidden"
                        EXPORTED='__attribute__((visibility("default")))'])
fi
AC_DEFINE_UNQUOTED([EXPORTED], [$EXPORTED], [Mark a symbol as being exported if visibility is changed])
AC_SUBST([VISIBILITY_FLAG])

CC_ATTRIBUTE_SENTINEL
CC_ATTRIBUTE_FORMAT
CC_ATTRIBUTE_FORMAT_ARG
CC_ATTRIBUTE_DEPRECATED
CC_ATTRIBUTE_UNUSED
CC_ATTRIBUTE_MALLOC
CC_ATTRIBUTE_WEAK

CC_ATTRIBUTE_PACKED([],
  [AC_MSG_WARN([Your compiler doesn't support __attribute__((packed)); xine might not work as expected.])])
CC_ATTRIBUTE_CONST

CC_CHECK_CFLAGS([-pipe], [miscflags="$miscflags -pipe"])

dnl Set warning flags for warnings that we do not want to skip.  In all cases,
dnl these warnings should be enabled.  Set these into CFLAGS and OBJCFLAGS
dnl later after all testing is done.

CC_CHECK_CFLAGS([-Wall], [warnflags="$warnflags -Wall"])

CC_CHECK_CFLAGS([-Wformat=2], [wformat="-Wformat=2"],
                [CC_CHECK_CFLAGS([-Wformat], [wformat="-Wformat"])])
if test x"$wformat" != x""; then
    CC_CHECK_CFLAGS([-Wno-format-zero-length], [wformat="$wformat -Wno-format-zero-length"])
fi
CC_CHECK_CFLAGS([-Wmissing-format-attribute], [wformat="$wformat -Wmissing-format-attribute"])
warnflags="$warnflags $wformat"

dnl WARNING: This warning flag, if set into CFLAGS now, can break some autoconf tests.
CC_CHECK_CFLAGS([-Werror-implicit-function-declaration], [warnflags="$warnflags -Werror-implicit-function-declaration"])

CC_CHECK_CFLAGS([-Wstrict-aliasing=2], [warnflags="$warnflags -Wstrict-aliasing=2"],
                [CC_CHECK_CFLAGS([-Wstrict-aliasing], [warnflags="$warnflags -Wstrict-aliasing"])])

CC_CHECK_CFLAGS([-Wchar-subscripts],      [warnflags="$warnflags -Wchar-subscripts"])
CC_CHECK_CFLAGS([-Wmissing-declarations], [warnflags="$warnflags -Wmissing-declarations"])
CC_CHECK_CFLAGS([-Wmissing-prototypes],   [warnflags="$warnflags -Wmissing-prototypes"])
CC_CHECK_CFLAGS([-Wwrite-strings],        [warnflags="$warnflags -Wwrite-strings"])

dnl Some combinations of gcc and glibc produce useless warnings on memset when
dnl compiling with -Wpointer-arith, so check for this first.
AC_MSG_CHECKING([for sane -Wpointer-arith])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <string.h>]], [[int a; memset(&a, 0, sizeof(int))]])],
                  [warnflags="$warnflags -Wpointer-arith"
                   AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no])])

CC_CHECK_LDFLAGS([-Wl,--gc-sections], [GCSECTIONS="-Wl,--gc-sections"])
AC_SUBST([GCSECTIONS])

dnl FreeBSD (et al.) does not complete linking for shared objects when pthreads
dnl are requested, as different implementations are present; to avoid problems
dnl use -Wl,-z,defs only for those platforms not behaving this way.
case "$host_or_hostalias" in
    *-freebsd*) ;;

    dnl check if we are using the cygwin, mingw or cygwin with mno-cygwin mode
    dnl in which case we are actually dealing with a mingw32 compiler
    dnl This cannot be done until AC_PROG_EGREP and AC_PROG_CC are both done.
    *-*-mingw* | *-*-cygwin*)
        CC_CHECK_LDFLAGS([-Wl,-z,defs], [NOUNDEF="-Wl,-z,defs"])
        case "$host_or_hostalias" in
            *-*-mingw32*)
                WIN32_SYS=mingw32
                ;;
            *-*-cygwin*)
                AC_EGREP_CPP([yes],
                             [#ifdef WIN32
                              yes
                              #endif],
                             [WIN32_SYS=mingw32], [WIN32_SYS=cygwin])
                ;;
        esac

        if test "$WIN32_SYS" = "mingw32"; then
            WIN32_INCLUDES='-I$(top_srcdir)/win32/include'
            LIBS="-lwinmm -lwsock32 $LIBS"
            LDFLAGS="-Wl,--enable-stdcall-fixup $LDFLAGS"
            dnl iberty has been needed only in older versions
            AC_CHECK_LIB(iberty, strncomp, [GOOM_LIBS="-liberty"])
            AC_SUBST(GOOM_LIBS)
        fi
        ;;

    *)
        CC_CHECK_LDFLAGS([-Wl,-z,defs], [NOUNDEF="-Wl,-z,defs"])
        ;;
esac

AC_SUBST(NOUNDEF)
AM_CONDITIONAL([WIN32], [test x"$WIN32_SYS" = x"mingw32"])

AC_ARG_ENABLE([altivec],
              [AS_HELP_STRING([--disable-altivec], [do not use assembly codes for Motorola 74xx CPUs])],
              [], [enable_altivec="yes"])

dnl No optimization at all.  For gcc, this is the optimization level.
O0_CFLAGS="-O0"

dnl Lowest level of optimization.  For gcc, this enables:
dnl     -fdefer-pop -fdelayed-branch -fguess-branch-probability -fcprop-registers
dnl     -floop-optimize -fif-conversion -fif-conversion2 -ftree-ccp -ftree-dce
dnl     -ftree-dce -ftree-dominator-opts -ftree-dse -ftree-ter -ftree-lrs
dnl     -ftree-sra -ftree-copyrename -ftree-fre -ftree-ch -fmerge-constants
dnl On platforms where -fomit-frame-pointer does not interfere with debugging,
dnl it is also enabled by -O1.
O1_CFLAGS="-O1"

dnl Middle level of optimization.  For gcc, this enables -O1 and:
dnl     -fthread-jumps -fcrossjumping -foptimize-sibling-calls -fcse-follow-jumps
dnl     -fcse-skip-blocks -fgcse -fgcse-lm -fexpensive-optimizations
dnl     -fstrength-reduce -frerun-cse-after-loop -frerun-loop-opt -fcaller-saves
dnl     -fforce-mem -fpeephole2 -fschedule-insns -fschedule-insns2
dnl     -fsched-interblock -fsched-spec -fregmove -fstrict-aliasing
dnl     -fdelete-null-pointer-checks -freorder-blocks -freorder-functions
dnl     -funit-at-a-time -falign-functions -falign-jumps -falign-loops
dnl     -falign-labels -ftree-pre
dnl Note that Apple's version of gcc differs slightly, because it does not enable
dnl     -fstrict-aliasing -freorder-blocks -fsched-interblock
O2_CFLAGS="-O2"

dnl Highest level of optimization.  For gcc, this enables -O2 and:
dnl     -finline-functions -funswitch-loops -fgcse-after-reload
O3_CFLAGS="-O3"

dnl gcc 3.3.5 (at least) is known to be buggy wrt optimization with
dnl -finline-functions, so use -fno-inline-functions for gcc < 3.4.0
if test x"$GCC" = x"yes"; then
    if test "$GCC_VERSION_MAJOR" -lt 3; then
        O3_CFLAGS="$O3_CFLAGS -fno-inline-functions"
    else
        if test "$GCC_VERSION_MAJOR" -eq 3 -a "$GCC_VERSION_MINOR" -lt 4; then
            O3_CFLAGS="$O3_CFLAGS -fno-inline-functions"
        fi
    fi
fi

CC_CHECK_CFLAGS([-ffast-math],               [optflags="$optflags -ffast-math"])
CC_CHECK_CFLAGS([-fexpensive-optimizations], [optflags="$optflags -fexpensive-optimizations"])

dnl initialize arch_86 as it is later tested for != "no" 
arch_x86=no

case "$host_or_hostalias" in
    alphaev56-*)
        cpuflags="-mcpu=ev56 -mieee $cpuflags"
        AC_DEFINE([ARCH_ALPHA], [], [Define this if you're running Alpha architecture])
        ;;
    alpha*)
        cpuflags="-mieee $cpuflags"
        AC_DEFINE([ARCH_ALPHA], [], [Define this if you're running Alpha architecture])
        ;;

    armv4l-*-linux*)
        cpuflags="-mcpu=strongarm1100 -ffast-math -fsigned-char $cpuflags"
        DEFAULT_OCFLAGS='$(O2_CFLAGS)'
        AC_DEFINE([ARCH_ARM], [], [Define this if you're running ARM architecture])
        ;;

    sparc*-*-linux*)
        case "`uname -m`" in
            sparc)      cpuflags="-mcpu=supersparc -mtune=supersparc" ;;
            sparc64)    cpuflags="-mcpu=ultrasparc -mtune=ultrasparc" ;;
        esac
        test x"$enable_vis" != x"no" && has_vis=yes
        AC_DEFINE([ARCH_SPARC], [], [Define this if you're running SPARC architecture])
        ;;

    sparc-*-solaris*)
        if test x"$GCC" = x"yes"; then
            case "`uname -m`" in
                sun4c) cpuflags="-mcpu=v7 -mtune=supersparc" ;;
                sun4m) cpuflags="-mcpu=v8 -mtune=supersparc" ;;
                sun4u)
                    if test x"$GCC_VERSION_MAJOR" -lt 3; then
                        # -mcpu=ultrasparc triggers a GCC 2.95.x compiler bug
                        # when compiling video_out.c:
                        #   gcc: Internal compiler error: program cc1 got fatal signal 11
                        # avoid -mcpu=ultrasparc with gcc 2.*
                        cpuflags="-mcpu=v8 -mtune=ultrasparc"
                    else
                        cpuflags="-mcpu=ultrasparc -mtune=ultrasparc"
                    fi
                    ;;
            esac
            if test "$GCC_VERSION_MAJOR" -ge 3; then
                test x"$enable_vis" != x"no" && has_vis=yes
            fi
        else
            case "`uname -m`" in
                sun4c) cpuflags="-xarch=v7" ;;
                sun4m) cpuflags="-xarch=v8" ;;
                sun4u) cpuflags="-xarch=v8plusa" ;;
            esac
            O1_CFLAGS="-fast -xCC"
            O2_CFLAGS="$O1_CFLAGS"
            O3_CFLAGS="$O1_CFLAGS"
        fi
        AC_DEFINE([ARCH_SPARC], [], [Define this if you're running SPARC architecture])
        ;;

    x86_64-*)
        arch_x86=64
        AC_DEFINE([ARCH_X86_64], [], [Define this if you're running x86 architecture 64 bits])
        ;;

    *-*-darwin*)
        case "$host_or_hostalias" in
            universal-*)
                arch_ppc=yes
                arch_x86=32
                ;;
            i386-* | x86_64-*)
                arch_x86=32
                AC_DEFINE([ARCH_X86_32], [], [Define this if you're running x86 architecture 32 bits])
                ;;
            ppc* | powerpc*)
                arch_ppc=yes
                dnl avoid ppc compilation crash
                AS="$CC"
                AC_DEFINE([ARCH_PPC], [], [Define this if you're running PowerPC architecture])

                AC_CHECK_HEADER([altivec.h], [], [enable_altivec=no])
                if test x"$enable_altivec" != x"no"; then
                    AC_DEFINE([ENABLE_ALTIVEC], [], [Define this if you want to use altivec on PowerPC CPUs])
                    cpuflags="$cpuflags -faltivec -maltivec"
                    LIBMPEG2_CFLAGS="$LIBMPEG2_CFLAGS -force_cpusubtype_ALL"
                fi
                ;;
        esac

        enable_impure_text=yes
        HOST_OS_DARWIN=1
        dnl HOST_OS_DARWIN is used by a bunch of difference stuff, including
        dnl libdvdnav, libmpeg2, and xine itself (xine-engine, xine-utils)
        AC_DEFINE([HOST_OS_DARWIN], 1, [Define this if built on Mac OS X/Darwin])
        dnl CONFIG_DARWIN is used by ffmpeg, so anything that pulls ffmpeg
        dnl headers needs to have this set.
        AC_DEFINE([CONFIG_DARWIN],  1, [Define this if built on Mac OS X/Darwin])
        CPPFLAGS="-D_INTL_REDIRECT_MACROS $CPPFLAGS"
        ;;

    ppc-*-linux* | powerpc-*) 
        arch_ppc=yes

        dnl avoid ppc compilation crash
        AS="$CC"
        AC_DEFINE([ARCH_PPC], [], [Define this if you're running PowerPC architecture])

        AC_CHECK_HEADER([altivec.h], [], [enable_altivec=no])
        if test x"$enable_altivec" != x"no"; then
            AC_DEFINE([ENABLE_ALTIVEC], [], [Define this if you want to use altivec on PowerPC CPUs])
            cpuflags="$cpuflags -maltivec"
        fi
        ;;

    i?86-* | k?-* | athlon-* | pentium*)
        arch_x86=32
        enable_impure_text=yes

        case "$host_or_hostalias" in
            *-*-cygwin* | *-*-mingw32*)
                CC_CHECK_CFLAGS([-fno-omit-frame-pointer], [W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-omit-frame-pointer"])
                CC_CHECK_CFLAGS([-fno-inline-functions],   [W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-inline-functions"])
                CC_CHECK_CFLAGS([-fno-rename-registers],   [W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-rename-registers"])
                AC_SUBST(W32_NO_OPTIMIZE)
                ;;
        esac

        AC_DEFINE([ARCH_X86_32], [], [Define this if you're running x86 architecture 32 bits])

        if test x"$GCC" = x"yes" -o x"${CC##*/}" = x"icc"; then
            if test x"$GCC" = x"yes"; then
                # GCC optimizations
                CC_CHECK_CFLAGS([-mtune=i386], [sarchopt="-mtune"],
                    [CC_CHECK_CFLAGS([-mcpu=i386], [sarchopt="-mcpu"],
                        [CC_CHECK_CFLAGS([-march=i386], [sarchopt="-march"], [sarchopt="no"])])])
                if test "$sarchopt" != "no"; then
                    case "$host_or_hostalias" in
                        i386-*)         archopt_val="i386" ;;
                        i486-*)         archopt_val="i486" ;;
                        i586-*)         archopt_val="pentium" ;;
                        pentium-mmx-*)  archopt_val="pentium-mmx" ;;
                        k6-2* | k6-3-*) archopt_val="k6-2" ;;
                        k6-*)           archopt_val="k6" ;;
                        pentium3-*)     archopt_val="pentium3" ;;
                        pentium4-*)     archopt_val="pentium4" ;;

                        athlon-4-* | athlon-xp-* | althon-mp-*) archopt_val="athlon-4" ;;
                        k7-* | athlon-tbird-* | athlon-*)       archopt_val="athlon" ;;

                        pentiumpro-* | pentium2-* | i686-*)
                            archopt_val="pentiumpro"
                            if test x"$cross_compiling" != x"yes"; then
                                if test -f /proc/cpuinfo; then
                                    modelname=`cat /proc/cpuinfo | grep "model\ name\	:" | sed -e 's/ //g' | cut -d ':' -f2`
                                    case "$modelname" in
                                        *Athlon* | *Duron* | *K7*)
                                            dnl Special check for k7 cpu CC support
                                            CC_CHECK_CFLAGS([$sarchopt=athlon],
                                                            [archopt_val="athlon"], [archopt_val="i686"])
                                            ;;
                                        VIAEzra)
                                            archopt_val="c3"
                                            ;;
                                    esac
                                fi
                            fi
                            ;;
                    esac
                    test x"$archopt_val" != x"" && cpuflags="$cpuflags $sarchopt=$archopt_val"
                fi
            else
                # Intel optimizations
                O3_CFLAGS="$O3_CFLAGS -unroll -ipo -ipo_obj"
            fi
        fi
        ;;
esac

AC_ARG_ENABLE([vis],
	      [AS_HELP_STRING([--disable-vis], [do not use assembly codes for Sun UltraSPARC CPUs])],
              [], [enable_vis="yes"])
if test "x$has_vis" = "xyes"; then
    AC_DEFINE([ENABLE_VIS], [], [Define this if you have Sun UltraSPARC CPU])
    case "$cpuflags" in
        *-mcpu=*) ;;
        *) cpuflags="$cpuflags -mcpu=v9" ;;
    esac
fi
AM_CONDITIONAL([ENABLE_VIS], test x"$has_vis" = x"yes")

O1_CFLAGS="$O1_CFLAGS $optflags $cpuflags"
O2_CFLAGS="$O2_CFLAGS $optflags $cpuflags"
O3_CFLAGS="$O3_CFLAGS $optflags $cpuflags"

dnl Allow turning off/on of optimizations.  By default, optimizations are
dnl enabled if --enable-debug is not specified.  Even with --enable-debug,
dnl optimizations can be enabled by explicitly specifying --enable-optimizations
AC_ARG_ENABLE([optimizations],
              [AS_HELP_STRING([--disable-optimizations], [Don't try to guess what optimization to enable])],
              [], [test x"$enable_debug" != x"no" && enable_optimizations="no"])
AM_CONDITIONAL([DISABLE_OPTIMIZATIONS], [test x"$enable_optimizations" = x"no"])
if test x"$enable_optimizations" = x"no"; then
    DEFAULT_OCFLAGS='$(O0_CFLAGS)'
else
    dnl For multi-pass compilation: never when cross-compiling
    if test x"$cross_compiling" != x"yes"; then
        if test x"$GCC" = x"yes"; then
            CC_CHECK_CFLAGS([-fprofile-arcs], [CC_CHECK_CFLAGS([-fbranch-probabilities],
                            [PASS1_CFLAGS="-fprofile-arcs $PASS1_CFLAGS"
                             PASS2_CFLAGS="-fbranch-probabilities $PASS2_CFLAGS"])])
        else
            pass1flags="-prof_dir \$(PWD)/\$(top_builddir)/ -prof_genx"
            pass2flags="-prof_dir \$(PWD)/\$(top_builddir)/ -prof_use"
            CC_CHECK_CFLAGS(["$pass1flags"], [CC_CHECK_CFLAGS(["$pass2flags"],
                            [PASS1_CFLAGS="$pass1flags $PASS1_CFLAGS"
                             PASS2_CFLAGS="$pass2flags $PASS2_CFLAGS"])])
        fi
    fi
fi
AC_SUBST(O0_CFLAGS)
AC_SUBST(O1_CFLAGS)
AC_SUBST(O2_CFLAGS)
AC_SUBST(O3_CFLAGS)

AC_SUBST(PASS1_CFLAGS)
AC_SUBST(PASS2_CFLAGS)

test x"$DEFAULT_OCFLAGS" = x"" && DEFAULT_OCFLAGS='$(O3_CFLAGS)'
AC_SUBST(DEFAULT_OCFLAGS)

if test x"$arch_x86" != x"no" && test x"$enable_macosx_universal" = x"no"; then
     AC_DEFINE([ARCH_X86], [], [Define this if you're running x86 architecture])
     AC_DEFINE([HAVE_MMX], [], [Define this if you can compile MMX asm instructions])
fi
AM_CONDITIONAL([ARCH_PPC], test x"$arch_ppc" = x"yes")
AM_CONDITIONAL([ARCH_X86], test x"$arch_x86" != x"no")
AM_CONDITIONAL([ARCH_X86_32], test x"$arch_x86" = x"32")
AM_CONDITIONAL([ARCH_X86_64], test x"$arch_x86" = x"64")
AM_CONDITIONAL([HAVE_MMX], test x"$arch_x86" != x"no")
AM_CONDITIONAL([HOST_OS_DARWIN], test x"$HOST_OS_DARWIN" = x"1")

if test x"$enable_impure_text" = x"yes"; then
    case "$host_or_hostalias" in
        *solaris*)
            if test "$GCC" = yes; then
                IMPURE_TEXT_LDFLAGS="-mimpure-text"
            else
                IMPURE_TEXT_LDFLAGS="-z textoff"
            fi
            ;;
        *darwin*)
            IMPURE_TEXT_LDFLAGS="-Wl,-read_only_relocs,warning"
            ;;
    esac
fi
AC_SUBST(IMPURE_TEXT_LDFLAGS)


dnl ----------------------------
dnl checks for library functions
dnl ----------------------------

dnl NLS: src/input/mms.c src/input/vcd, xine-utils
AC_CHECK_FUNCS([nl_langinfo])

dnl src/libfaad
AC_CHECK_DECL([lrintf],
              [AC_DEFINE([HAVE_LRINTF],    1, [Define this if the 'lrintf' function is declared in math.h])
	       AC_DEFINE([_ISOC9X_SOURCE], 1, [Define this if you are ISO C9X compliant])],
              [],
              [#define _ISOC9X_SOURCE
               #include <math.h>])

dnl contrib/libdca, src/video_out/vidix/drivers/mach64_vid.c
AC_CHECK_FUNCS([memalign])

dnl src/input/vcd/libcdio/portable.h
AC_CHECK_FUNCS([bzero])

dnl src/libfaad/common.h
AC_CHECK_FUNCS([memcpy])

dnl src/input/input_file.c
AC_ARG_ENABLE([mmap],
              AS_HELP_STRING([--enable-mmap], [Enable mmap() file loading (default: no)]))
if test x"$enable_mmap" = x"yes"; then
    AC_CHECK_FUNCS([mmap])
fi

AC_CHECK_FUNCS([vsscanf sigaction sigset getpwuid_r nanosleep lstat memset readlink strchr va_copy])

AC_CHECK_FUNCS([snprintf _snprintf], [have_required_function="yes"])
               test x"$have_required_function" != x"yes" && AC_MSG_ERROR([required function not found])
AC_CHECK_FUNCS([vsnprintf _vsnprintf], [have_required_function="yes"])
               test x"$have_required_function" != x"yes" && AC_MSG_ERROR([required function not found])
AC_CHECK_FUNCS([strcasecmp _stricmp], [have_required_function="yes"])
               test x"$have_required_function" != x"yes" && AC_MSG_ERROR([required function not found])
AC_CHECK_FUNCS([strncasecmp _strnicmp], [have_required_function="yes"])
               test x"$have_required_function" != x"yes" && AC_MSG_ERROR([required function not found])

AC_FUNC_FSEEKO

AC_REPLACE_FUNCS([asprintf basename gettimeofday setenv strcasestr strndup strpbrk strsep strtok_r timegm unsetenv memmem])

AC_LIBSOURCE([hstrerror.c])
saveLIBS="${LIBS}"
LIBS="${LIBS} ${NET_LIBS}"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]], [[hstrerror(0)]])],
               [AC_DEFINE([HAVE_HSTRERROR], 1, [Define to 1 if you have 'hstrerror' in <netdb.h>])],
               [AC_LIBOBJ([hstrerror])])
LIBS="${saveLIBS}"

AC_LIBSOURCE([dirent_msvc.c])
AC_CHECK_FUNC([opendir],
              [AC_DEFINE([HAVE_OPENDIR], 1, [Define to 1 if you have 'opendir' function])],
              [if test x"$WIN32_SYS" = x"mingw32"; then
                   AC_LIBOBJ([dirent_msvc])
               else
                   AC_MSG_ERROR([dirent is needed (opendir, readdir, ...)])
               fi])

XINE_CHECK_MINMAX([
    AC_DEFINE(HAVE_MAX_MACRO, 1, [Define to 1 if you have 'MAX' macro in sys/param.h])
    AC_DEFINE(HAVE_MIN_MACRO, 1, [Define to 1 if you have 'MIN' macro in sys/param.h])
  ],[])

AC_LIBSOURCE([timedlock.c])
ac_save_LIBS="$LIBS" LIBS="$LIBS $PTHREAD_LIBS"
AC_CHECK_FUNCS([pthread_mutex_timedlock],
               [AC_DEFINE([HAVE_PTHREAD_MUTEX_TIMEDLOCK], 1, [Define to 1 if you have 'pthread_mutex_timedlock' in <pthread.h>])],
               [AC_LIBOBJ([timedlock])])
LIBS="$ac_save_LIBS"

AC_GETOPT_LONG

dnl --------------------------
dnl checks for system services
dnl --------------------------

dnl Even as of 2.61, autoconf is not smart enough to find the X include
dnl and library paths on Mac OS X, so seed them automatically if they're
dnl not specified on the configure command-line.
case "$host_os" in
    darwin*)
        test x"$x_includes" = x"NONE" && x_includes="/usr/X11R6/include"
        test x"$x_libraries" = x"NONE" && x_libraries="/usr/X11R6/lib"
        ;;
esac

dnl Check for Xwindows using the autoconf AC_PATH_XTRA macro, which is an
dnl extension of AC_PATH_X that sets X_CFLAGS and X_LIBS.  It will also set
dnl X_EXTRA_LIBS and X_PRE_LIBS.
AC_PATH_XTRA
if test x"$no_x" != x"yes"; then
    X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"
    AC_CHECK_LIB([Xext], [main],
                 [X_LIBS="$X_LIBS -lXext"], [AC_MSG_ERROR([libXext is required])],
                 [$X_LIBS])
elif test x"$have_x" = x"no"; then
    PKG_CHECK_MODULES([X], [x11 xext], [have_x=yes; no_x=no], [have_x=no; no_x=yes])
fi

dnl Check for XShm support (required for xine X support)
if test x"$no_x" != x"yes"; then
    ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $X_CFLAGS"
    AC_CHECK_HEADERS([X11/extensions/XShm.h], [],
                     [AC_MSG_ERROR([XShm extension is required])],
                     [#include <X11/Xlib.h>])
    CPPFLAGS="$ac_save_CPPFLAGS"
    AC_DEFINE([HAVE_X11], 1, [Define this if you have X11R6 installed])
fi
AM_CONDITIONAL([HAVE_X11], [test x"$no_x" != x"yes"])

dnl Locate libraries needed for X health check
soname_script="/[[0-9]]$/! d; s%^.*/%%
t q
b
:q
q"
x_lib_location="`ls -1 "${x_libraries:-/usr/local/lib}/libX11.$acl_cv_shlibext"* "${x_libraries:-/usr/lib}/libX11.$acl_cv_shlibext"* 2>/dev/null | sed -e \"${soname_script}\"`"
AC_DEFINE_UNQUOTED([LIBX11_SO], "${x_lib_location:-libX11.$acl_cv_shlibext}", [The soname of libX11, needed for dlopen()])
x_lib_location="`ls -1 "${x_libraries:-/usr/local/lib}/libXv.$acl_cv_shlibext"*  "${x_libraries:-/usr/lib}/libXv.$acl_cv_shlibext"*  2>/dev/null | sed -e \"${soname_script}\"`"
AC_DEFINE_UNQUOTED([LIBXV_SO],  "${x_lib_location:-libXv.$acl_cv_shlibext}",  [The soname of libXv, needed for dlopen()])

dnl Does X11 need XLockDisplay () to prevent deadlocks?
AC_MSG_CHECKING([for thread safe X11])
x_is_thread_safe=no
if test -n "$PKG_CONFIG" ; then
    if "$PKG_CONFIG" --atleast-version 1.9 xcb && "$PKG_CONFIG" --atleast-version 1.4.99.901 x11 ; then
        x_is_thread_safe=yes
        AC_DEFINE([HAVE_THREAD_SAFE_X11], [1], [Define this if x11 does without XLockDisplay ().])
    fi
fi
AC_MSG_RESULT([$x_is_thread_safe])

dnl _FILE_OFFSET_BITS   (AC_SYS_LARGEFILE; ac_cv_sys_file_offset_bits)
dnl _LARGE_FILES        (AC_SYS_LARGEFILE; ac_cv_sys_large_files)
dnl _LARGEFILE_SOURCE   (AC_FUNC_SEEKO; ac_cv_sys_largfile_source)
dnl _LARGEFILE64_SOURCE (glibc transitional; not tested or used anywhere)
AC_SYS_LARGEFILE

dnl Add macros to the compiler command line that are also in config.h for things
dnl that do not #include config.h.
dnl known: src/input/libreal, src/input/librtsp
test x"$ac_cv_sys_file_offset_bits" != x"no" && CPPFLAGS="-D_FILE_OFFSET_BITS=64 $CPPFLAGS"
test x"$ac_cv_sys_largefile_source" != x"no" && CPPFLAGS="-D_LARGEFILE_SOURCE $CPPFLAGS"
test x"$ac_cv_sys_large_files" != x"" && test x"$ac_cv_sys_large_files" != x"no" && CPPFLAGS="-D_LARGE_FILES $CPPFLAGS"


dnl -------
dnl Plugins
dnl -------

XINE_AUDIO_OUT_PLUGINS
XINE_VIDEO_OUT_PLUGINS
XINE_INPUT_PLUGINS
XINE_DECODER_PLUGINS


dnl ---------------------------------------------
dnl XINE_ROOTDIR does not work if architecture independent files are 
dnl installed to another place than architecture dependent files !!!
dnl ---------------------------------------------

dnl
dnl installation directories and directories relative to prefix
dnl
dnl Note:
dnl   use AC_DEFINE for runtime
dnl   use AC_SUBST for installation
dnl

makeexpand () {
    local i
    local j
    i="$1"
    while test "$i" != "$j"; do j="$i"; eval i="$j"; done
    echo "$i"
}

xinelibdir='${libdir}/xine'
xinedatadir='${datadir}/xine'
pkgconfigdir='${libdir}/pkgconfig'
AC_SUBST(xinelibdir)
AC_SUBST(xinedatadir)
AC_SUBST(pkgconfigdir)

XINE_PLUGINROOT="\${xinelibdir}/plugins/$(($XINE_LT_CURRENT-$XINE_LT_AGE))"
XINE_PLUGINDIR="$XINE_PLUGINROOT.$XINE_LT_AGE"
XINE_FONTDIR="\${xinedatadir}/libxine$XINE_MAJOR/fonts"
XINE_LOCALEDIR='${datadir}/locale'
XINE_REL_PLUGINROOT="`makeexpand "$XINE_PLUGINROOT"`"
XINE_REL_PLUGINROOT="`makeexpand "$XINE_REL_PLUGINROOT" | sed -e "s,^${prefix}/,,"`"
XINE_REL_PLUGINDIR="$XINE_REL_PLUGINROOT.$XINE_LT_AGE"
XINE_REL_FONTDIR="`makeexpand "$XINE_FONTDIR" | sed -e "s,^${prefix}/,,"`"
XINE_REL_LOCALEDIR="`makeexpand "$XINE_LOCALEDIR" | sed -e "s,^${prefix}/,,"`"
XINE_PKGCONFIG_DIR="`makeexpand "$pkgconfigdir"`"
if test "x$WIN32_SYS" = "xmingw32" -o "x$WIN32_SYS" = "xcygwin"; then
    dnl polish paths (MinGW runtime accepts both \ and / anyway)
    XINE_REL_PLUGINROOT="`echo "$XINE_REL_PLUGINROOT" | sed -e 's/\\//\\\\\\\\/g'`"
    XINE_REL_PLUGINDIR="$XINE_REL_PLUGINROOT.$XINE_LT_AGE"
    XINE_REL_FONTDIR="`echo "$XINE_REL_FONTDIR" | sed -e 's/\\//\\\\\\\\/g'`"
    XINE_REL_LOCALEDIR="`echo "$XINE_REL_LOCALEDIR" | sed -e 's/\\//\\\\\\\\/g'`"
    AC_DEFINE([XINE_PLUGINROOT],[xine_get_pluginroot()],[Define this to general plugins directory location])
    AC_DEFINE([XINE_PLUGINDIR], [xine_get_plugindir()], [Define this to specific plugins directory location])
    AC_DEFINE([XINE_FONTDIR],   [xine_get_fontdir()],   [Define this to osd fonts dir location])
    AC_DEFINE([XINE_LOCALEDIR], [xine_get_localedir()], [Path where catalog files will be.])
else
    XINE_PLUGINROOTPATH="`makeexpand "$XINE_PLUGINROOT"`"
    XINE_FONTPATH="`makeexpand "$XINE_FONTDIR"`"
    XINE_LOCALEPATH="`makeexpand "$XINE_LOCALEDIR"`"
    AC_DEFINE_UNQUOTED([XINE_PLUGINROOT],["$XINE_PLUGINROOTPATH"], [Define this to general plugins directory location])
    AC_DEFINE_UNQUOTED([XINE_PLUGINDIR], ["$XINE_PLUGINROOTPATH.$XINE_LT_AGE"], [Define this to soecific plugins directory location])
    AC_DEFINE_UNQUOTED([XINE_FONTDIR],   ["$XINE_FONTPATH"],   [Define this to osd fonts dir location])
    AC_DEFINE_UNQUOTED([XINE_LOCALEDIR], ["$XINE_LOCALEPATH"], [Path where catalog files will be.])
fi
AC_DEFINE_UNQUOTED([XINE_REL_PLUGINDIR], ["$XINE_REL_PLUGINDIR"], [Define this to specific plugin directory relative to execution prefix])
AC_DEFINE_UNQUOTED([XINE_REL_PLUGINROOT],["$XINE_REL_PLUGINROOT"],[Define this to general plugin directory relative to execution prefix])
AC_DEFINE_UNQUOTED([XINE_REL_FONTDIR],   ["$XINE_REL_FONTDIR"],   [Define this to font directory relative to prefix])
AC_DEFINE_UNQUOTED([XINE_REL_LOCALEDIR], ["$XINE_REL_LOCALEDIR"], [Define this to font directory relative to prefix])
AC_SUBST(XINE_PLUGINDIR)
AC_SUBST(XINE_FONTDIR)
AC_SUBST(XINE_LOCALEDIR)
AC_SUBST(XINE_PKGCONFIG_DIR)

LIBTOOL_DESTDIR_DEFAULT=
if test "x$SYS" != "xmingw32" -a "x$SYS" != "xcygwin"; then
  LIBTOOL_DESTDIR_DEFAULT=/
fi
AC_SUBST([LIBTOOL_DESTDIR_DEFAULT])

dnl Where aclocal m4 files should be installed
xine_acflags='-I ${datarootdir}/aclocal'
XINE_ACFLAGS="-I ${datarootdir}/aclocal"
AC_DEFINE_UNQUOTED([XINE_ACFLAGS], ["$XINE_ACFLAGS"], [Path where aclocal m4 files will be.])
AC_SUBST(xine_acflags)
AC_SUBST(XINE_ACFLAGS)

dnl Where architecture independent data (e.g. logo) will/should be installed
XINE_DATADIR="\${xinedatadir}"
AC_SUBST(XINE_DATADIR)

dnl Where scripts will/should be installed.
XINE_SCRIPTPATH="\${xinedatadir}/scripts"
AC_SUBST(XINE_SCRIPTPATH)


dnl ---------------------------------------------
dnl Get where .m4 should be installed.
dnl ---------------------------------------------

dnl if test "x${ACLOCAL_DIR+set}" != xset; then
dnl   case "`id`" in
dnl     uid=0\(* )
dnl       AC_MSG_CHECKING(for aclocal directory)
dnl       if (aclocal --version) < /dev/null > /dev/null 2>&1; then
dnl         ACLOCAL_DIR="`eval $ACLOCAL --print-ac-dir`"
dnl         AC_MSG_RESULT($ACLOCAL_DIR)
dnl       else
dnl         ACLOCAL_DIR="${prefix}/share/aclocal"
dnl         AC_MSG_RESULT(none - will be installed in $ACLOCAL_DIR)
dnl       fi
dnl       escapedprefix="`echo $prefix | sed -e 's/\\//\\\\\//g'`"
dnl       ACLOCAL_DIR="`echo $ACLOCAL_DIR|sed -e 's/^'$escapedprefix/'\${prefix}'/`"
dnl       ;;
dnl   esac
dnl fi
AC_SUBST(ACLOCAL_DIR)
AM_CONDITIONAL([INSTALL_M4], [test x"$ACLOCAL_DIR" != x""])


dnl this is an internal function we should not use, but
dnl as long as neither autoconf nor automake offer an A[CM]_PROG_OBJC
dnl check we will have to call it
_AM_DEPENDENCIES([OBJC])


dnl ---------------------------------------------
dnl some include paths ( !!! DO NOT REMOVE !!! )
dnl ---------------------------------------------

INCLUDES='-I$(top_srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_builddir)/src/xine-engine -I$(top_srcdir)/src/xine-engine -I$(top_srcdir)/src/xine-utils $(INTLDIR) -I$(top_builddir)/src/input -I$(top_srcdir)/src/input $(WIN32_INCLUDES) -I$(top_builddir)/lib -I$(top_srcdir)/lib'
AC_SUBST(INCLUDES)

dnl We check for warnings here rather than with optimisations since we
dnl want them to be _always_ enabled, to make sure the code is sane
dnl enough.

CC_CHECK_CFLAGS_APPEND([-Wall -Wchar-subscripts dnl
			-Wnested-externs -Wcast-align dnl
			-Wmissing-declarations -Wmissing-prototypes dnl
			-Wmissing-format-attribute -Wno-pointer-sign])

CC_CHECK_CFLAGS_APPEND([-Wformat=2 -Wformat],
  [CC_CHECK_CFLAGS_APPEND([-Wno-format-zero-length])
   break;
  ])

CC_CHECK_CFLAGS_APPEND([-Wformat-security])

dnl check for strict aliasing problem, get the highest between =2 and
dnl normal.
CC_CHECK_CFLAGS_APPEND([-Wstrict-aliasing=2 -Wstrict-aliasing], [break;])

dnl This *has* to stay at the end as it can break some autoconf tests.
CC_CHECK_CFLAGS_APPEND([-Werror=implicit-function-declaration dnl
                        -Werror-implicit-function-declaration], [break;])

CC_NOUNDEFINED

dnl signal FreeBSD have also FreeBSD based kernel.
dnl 

AH_BOTTOM([
#if defined (__FreeBSD__) && !defined (__FreeBSD_kernel__)
#define __FreeBSD_kernel__ __FreeBSD__
#endif

#if defined (__FreeBSD_kernel__)
#include <sys/param.h>
# if defined (__FreeBSD_version) && !defined (__FreeBSD_kernel_version)
# define __FreeBSD_kernel_version __FreeBSD_version
# endif
#endif
])

dnl Common cflags for all platforms
CFLAGS="$UNIVERSAL_CFLAGS \$(MULTIPASS_CFLAGS) $miscflags $warnflags $CFLAGS"
LDFLAGS="$UNIVERSAL_LDFLAGS $LDFLAGS"
OBJCFLAGS="$UNIVERSAL_CFLAGS $miscflags $warnflags $OBJCFLAGS"

dnl Some informations about xine-lib compilation for xine-config
XINE_BUILD_CC="`$CC -v 2>&1 | tail -1 2>/dev/null`"
XINE_BUILD_OS="`uname -s -r -m`"
XINE_BUILD_DATE="`date "+%a %d %b %Y %T"`"
AC_SUBST(XINE_BUILD_CC)
AC_SUBST(XINE_BUILD_OS)
AC_SUBST(XINE_BUILD_DATE)


dnl ---------------------------------------------
dnl Check for documentation formatting tool
dnl ---------------------------------------------

AC_CHECK_PROG(XMLTO, xmlto, xmlto, no)
AM_CONDITIONAL([HAVE_XMLTO], [test "$XMLTO" != "no"])

AC_CHECK_PROG(RSVG, rsvg-convert, rsvg-convert, no)
AM_CONDITIONAL([HAVE_RSVG_CONVERT], [test "$RSVG" != "no"])
if test "$RSVG" = no; then
  AC_CHECK_PROG(RSVG, rsvg, rsvg, no)
fi
AM_CONDITIONAL([HAVE_RSVG], [test "$RSVG" != "no"])

dnl ---------------------------------------------
dnl ... and for PNG optimisation tool
dnl ---------------------------------------------

AC_CHECK_PROG(OPTIPNG, optipng, optipng, no)
AM_CONDITIONAL([HAVE_OPTIPNG], [test "$OPTIPNG" != "no"])

dnl ---------------------------------------------
dnl Output configuration files
dnl ---------------------------------------------

AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/man/Makefile
doc/man/en/Makefile
doc/hackersguide/Makefile
doc/faq/Makefile
doc/Doxyfile
contrib/Makefile
contrib/a52dec/Makefile
contrib/gsm610/Makefile
contrib/libdca/Makefile
contrib/libdha/Makefile
contrib/libdha/bin/Makefile
contrib/libdha/kernelhelper/Makefile
contrib/libdha/oth/Makefile
contrib/libdha/sysdep/Makefile
contrib/libfaad/Makefile
contrib/libmad/Makefile
contrib/libmpcdec/Makefile
contrib/libxdg-basedir/Makefile
contrib/nosefart/Makefile
contrib/vidix/Makefile
contrib/vidix/drivers/Makefile
include/Makefile
include/xine/version.h
lib/Makefile
m4/Makefile
misc/Makefile
misc/Makefile.plugins
misc/SlackBuild
misc/build_rpms.sh
misc/libxine.pc
misc/relchk.sh
misc/xine-config
misc/xine-lib.spec
po/Makevars.extra
po/Makefile.in
src/Makefile
src/audio_out/Makefile
src/audio_dec/Makefile
src/combined/Makefile
src/combined/ffmpeg/Makefile
src/demuxers/Makefile
src/dxr3/Makefile
src/input/Makefile
src/input/libdvdnav/Makefile
src/input/librtsp/Makefile
src/input/libreal/Makefile
src/input/vcd/Makefile
src/libw32dll/Makefile
src/libw32dll/wine/Makefile
src/libw32dll/DirectShow/Makefile
src/libw32dll/dmo/Makefile
src/libw32dll/qtx/Makefile
src/libw32dll/qtx/qtxsdk/Makefile
src/libreal/Makefile
src/post/Makefile
src/post/planar/Makefile
src/post/goom/Makefile
src/post/mosaico/Makefile
src/post/visualizations/Makefile
src/post/audio/Makefile
src/post/deinterlace/Makefile
src/post/deinterlace/plugins/Makefile
src/spu_dec/Makefile
src/video_dec/Makefile
src/video_dec/libmpeg2/Makefile
src/video_dec/libmpeg2new/Makefile
src/video_dec/libmpeg2new/include/Makefile
src/video_dec/libmpeg2new/libmpeg2/Makefile
src/video_dec/libvdpau/Makefile
src/video_out/Makefile
src/video_out/macosx/Makefile
src/xine-utils/Makefile
src/xine-engine/Makefile
src/vdr/Makefile
win32/Makefile])
AC_CONFIG_COMMANDS([default],[[chmod +x ./misc/SlackBuild ./misc/build_rpms.sh ./misc/relchk.sh]],[[]])
AC_OUTPUT

dnl Guard against multiple inclusion
AH_TOP([#ifndef _XINE_CONFIGURE_H_
#define _XINE_CONFIGURE_H_
])
AH_BOTTOM([#endif
])

dnl ---------------------------------------------
dnl Work around a suspected bug in libtool:
dnl
dnl Remove excessive trailing slash from search dir names in the libtool script.
dnl It occurs in dir names obtained by 'gcc -print-search-dirs' in the created
dnl configure script and causes a test on dir names in libtool to fail,
dnl leading to confusing (but harmless) 'warning: <lib> seems to be moved'.
dnl
dnl This should be fixed in the libtool package itself as all other dir names
dnl there have no trailing slash.
dnl ---------------------------------------------
dnl Work around problem due to LTCFLAGS:
dnl
dnl CFLAGS is passed to LTCFLAGS directly from configure instead of Makefiles.
dnl Fixing LTCFLAGS here will help compile xine-list on some crazy platforms.
dnl
dnl It disables multi-pass compilation of xine-list.
dnl ---------------------------------------------

dnl Note: Brackets [] must be doubled as they are treated as m4 macro quotes.
if test x"$enable_macosx_universal" = x"no"; then
    cat libtool | sed -e '/sys_lib_search_path_spec=/s/\/\([[ "]]\)/\1/g' > libtool.tmp
else
    cat libtool | sed -e 's,sys_lib_search_path_spec=.*$,sys_lib_search_path_spec=\"/Developer/SDKs/MacOSX10.4u.sdk/usr/lib\",' > libtool.tmp
fi
mv -f libtool.tmp libtool

dnl removing make-specific variable
cat libtool | sed 's/\\$(MULTIPASS_CFLAGS)//' > libtool.tmp
mv -f libtool.tmp libtool

chmod +x libtool

XINE_LIB_SUMMARY
