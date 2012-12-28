dnl AC_COMPILE_CHECK_SIZEOF (TYPE SUPPOSED-SIZE)
dnl abort if the given type does not have the supposed size
AC_DEFUN([AC_COMPILE_CHECK_SIZEOF], [
    AC_MSG_CHECKING(that size of $1 is $2)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[switch (0) case 0: case (sizeof ($1) == $2):;]])],
                      [AC_MSG_RESULT([yes])], [AC_MSG_ERROR([can not build a default inttypes.h])])
])

dnl
AC_DEFUN([AC_CHECK_LIRC],
  [AC_ARG_ENABLE(lirc,
     AS_HELP_STRING([--disable-lirc], [turn off LIRC support]),
     enable_lirc=$enableval, enable_lirc=yes)

  if test x"$enable_lirc" = xyes; then
     have_lirc=yes
     AC_REQUIRE_CPP
     AC_CHECK_LIB(lirc_client,lirc_init,
           AC_CHECK_HEADER(lirc/lirc_client.h, true, have_lirc=no), have_lirc=no)
     if test "$have_lirc" = "yes"; then

        if test x"$LIRC_PREFIX" != "x"; then
           lirc_libprefix="$LIRC_PREFIX/lib"
	   LIRC_INCLUDE="-I$LIRC_PREFIX/include"
        fi
        for llirc in $lirc_libprefix /lib /usr/lib /usr/local/lib; do
          AC_CHECK_FILE("$llirc/liblirc_client.a",
             LIRC_LIBS="$llirc/liblirc_client.a"
             AC_DEFINE(HAVE_LIRC),,)
        done
     else
         AC_MSG_RESULT([*** LIRC client support not available, LIRC support will be disabled ***]);
     fi
  fi

     AC_SUBST(LIRC_LIBS)
     AC_SUBST(LIRC_INCLUDE)
])

dnl AC_CHECK_GENERATE_INTTYPES_H (INCLUDE-DIRECTORY)
dnl generate a default inttypes.h if the header file does not exist already
AC_DEFUN([AC_CHECK_GENERATE_INTTYPES],
    [AC_CHECK_HEADER([inttypes.h],,
        [if test ! -d $1; then mkdir $1; fi
        AC_CHECK_HEADER([stdint.h],
            [cat >$1/inttypes.h << EOF
#ifndef _INTTYPES_H
#define _INTTYPES_H
/* helper inttypes.h for people who do not have it on their system */

#include <stdint.h>
EOF
            ],
            [AC_COMPILE_CHECK_SIZEOF([char],[1])
            AC_COMPILE_CHECK_SIZEOF([short],[2])
            AC_COMPILE_CHECK_SIZEOF([int],[4])
            AC_COMPILE_CHECK_SIZEOF([long long],[8])
        cat >$1/inttypes.h << EOF
#ifndef _INTTYPES_H
#define _INTTYPES_H
/* default inttypes.h for people who do not have it on their system */
#if (!defined __int8_t_defined) && (!defined __BIT_TYPES_DEFINED__)
#define __int8_t_defined
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
#ifdef ARCH_X86
typedef signed long long int64_t;
#endif
#endif
#if (!defined __uint8_t_defined) && (!defined _LINUX_TYPES_H)
#define __uint8_t_defined
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#ifdef ARCH_X86
typedef unsigned long long uint64_t;
#endif
#endif
EOF
            ])
        cat >>$1/inttypes.h << EOF

#ifdef WIN32
#  define PRI64_PREFIX "I64"
#else
#  define PRI64_PREFIX "l"
#endif

#ifndef PRId8
#  define PRId8 "d"
#endif
#ifndef PRId16
#  define PRId16 "d"
#endif
#ifndef PRId32
#  define PRId32 "d"
#endif
#ifndef PRId64
#  define PRId64 PRI64_PREFIX "d"
#endif

#ifndef PRIu8
#  define PRIu8 "u"
#endif
#ifndef PRIu16
#  define PRIu16 "u"
#endif
#ifndef PRIu32
#  define PRIu32 "u"
#endif
#ifndef PRIu64
#  define PRIu64 PRI64_PREFIX "u"
#endif

#ifndef PRIx8
#  define PRIx8 "x"
#endif
#ifndef PRIx16
#  define PRIx16 "x"
#endif
#ifndef PRIx32
#  define PRIx32 "x"
#endif
#ifndef PRIx64
#  define PRIx64 PRI64_PREFIX "x"
#endif

#ifndef PRIX8
#  define PRIX8 "X"
#endif
#ifndef PRIX16
#  define PRIX16 "X"
#endif
#ifndef PRIX32
#  define PRIX32 "X"
#endif
#ifndef PRIX64
#  define PRIX64 PRI64_PREFIX "X"
#endif

#ifndef PRIdFAST8
#  define PRIdFAST8 "d"
#endif
#ifndef PRIdFAST16
#  define PRIdFAST16 "d"
#endif
#ifndef PRIdFAST32
#  define PRIdFAST32 "d"
#endif
#ifndef PRIdFAST64
#  define PRIdFAST64 "d"
#endif

#ifndef PRIuFAST8
#  define PRIuFAST8 "u"
#endif
#ifndef PRIuFAST16
#  define PRIuFAST16 "u"
#endif
#ifndef PRIuFAST32
#  define PRIuFAST32 "u"
#endif
#ifndef PRIuFAST64
#  define PRIuFAST64 PRI64_PREFIX "u"
#endif

#ifndef PRIxFAST8
#  define PRIxFAST8 "x"
#endif
#ifndef PRIxFAST16
#  define PRIxFAST16 "x"
#endif
#ifndef PRIxFAST32
#  define PRIxFAST32 "x"
#endif
#ifndef PRIxFAST64
#  define PRIxFAST64 PRI64_PREFIX "x"
#endif

#ifndef SCNd8
#  define SCNd8 "hhd"
#endif
#ifndef SCNd16
#  define SCNd16 "hd"
#endif
#ifndef SCNd32
#  define SCNd32 "d"
#endif
#ifndef SCNd64
#  define SCNd64 PRI64_PREFIX "d"
#endif

#ifndef SCNu8
#  define SCNu8 "hhu"
#endif
#ifndef SCNu16
#  define SCNu16 "hu"
#endif
#ifndef SCNu32
#  define SCNu32 "u"
#endif
#ifndef SCNu64
#  define SCNu64 PRI64_PREFIX "u"
#endif

#ifndef PRIdMAX
#  define PRIdMAX PRId64
#endif
#ifndef PRIuMAX
#  define PRIuMAX PRIu64
#endif
#ifndef PRIxMAX
#  define PRIxMAX PRIx64
#endif
#ifndef SCNdMAX
#  define SCNdMAX SCNd64
#endif

#endif
EOF
        ])])


dnl Check for the type of the third argument of getsockname
AC_DEFUN([AC_CHECK_SOCKLEN_T], [
    AC_MSG_CHECKING([for socklen_t])
    AC_LANG_PUSH([C])
    AC_CACHE_VAL([ac_cv_socklen_t],
                 [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
                                                       #include <sys/socket.h>]],
                                                     [[socklen_t a=0; getsockname(0,(struct sockaddr*)0, &a)]])],
                                    [ac_cv_socklen_t=socklen_t], [ac_cv_socklen_t=''])
                  if test x"$ac_cv_socklen_t" = x""; then
                      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
                                                           #include <sys/socket.h>]],
                                                         [[int a=0; getsockname(0,(struct sockaddr*)0, &a);]])],
                                        [ac_cv_socklen_t=int], [ac_cv_socklen_t=size_t])
                  fi])
    AC_LANG_POP([C])
    AC_MSG_RESULT([$ac_cv_socklen_t])
    if test x"$ac_cv_socklen_t" != x"socklen_t"; then
        AC_DEFINE_UNQUOTED([socklen_t], [$ac_cv_socklen_t], [Define the real type of socklen_t])
    fi
])
