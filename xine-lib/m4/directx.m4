dnl
dnl autoconf script for DirectX
dnl
dnl written by Frantisek Dvorak <valtri@users.sourceforge.net>
dnl
dnl
dnl AM_PATH_DIRECTX([ACTION IF FOUND [, ACTION IF NOT FOUND]]))
dnl
dnl It looks for DirectX, defines DIRECTX_CPPFLAGS, DIRECTX_AUDIO_LIBS and
dnl DIRECTX_VIDEO_LIBS.
dnl
AC_DEFUN([AM_PATH_DIRECTX], [
    AC_ARG_WITH([dxheaders],
                [AS_HELP_STRING([--with-dxheaders], [specify location of DirectX headers])],
                [dxheaders_prefix="$withval"], [dxheaders_prefix="no"])
    if test x"$dxheaders_prefix" != x"no"; then
        DIRECTX_CPPFLAGS="-I$dxheaders_prefix $DIRECTX_CPPFLAGS"
    fi

    AC_MSG_CHECKING([for DirectX])
    DIRECTX_AUDIO_LIBS="$DIRECTX_LIBS -ldsound"
    DIRECTX_VIDEO_LIBS="$DIRECTX_LIBS -lgdi32 -lddraw"

    AC_LANG_PUSH([C])
    ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $DIRECTX_CPPFLAGS"
    ac_save_LIBS="$LIBS" LIBS="$LIBS $DIRECTX_VIDEO_LIBS $DIRECTX_AUDIO_LIBS"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stddef.h>
                                         #include <windows.h>
                                         #include <ddraw.h>
                                         #include <dsound.h>]],
                                       [[DirectDrawCreate(0, NULL, 0); DirectSoundCreate(0, NULL, 0)]])],
                      [have_directx=yes], [have_directx=no])
    CPPFLAGS="$ac_save_CPPFLAGS"
    LIBS="$ac_save_LIBS"
    AC_LANG_POP([C])

    AC_SUBST(DIRECTX_CPPFLAGS)
    AC_SUBST(DIRECTX_AUDIO_LIBS)
    AC_SUBST(DIRECTX_VIDEO_LIBS)
    AM_CONDITIONAL([ENABLE_DIRECTX], [test x"$have_directx" = x"yes"])

    AC_MSG_RESULT([$have_directx])
    if test x"$have_directx" = x"yes"; then
        AC_DEFINE([HAVE_DIRECTX], 1, [Define this if you have DirectX])
        ifelse([$1], , :, [$1])
    else
        ifelse([$2], , :, [$2])
    fi
])
