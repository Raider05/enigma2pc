dnl -----------------
dnl Audio out plugins
dnl -----------------
AC_DEFUN([XINE_AUDIO_OUT_PLUGINS], [
    dnl Setup defaults for the target operating system.  For example, alsa is
    dnl only ever available on Linux, so don't bother checking for it unless
    dnl explicitly requested to do so on other operating systems.
    dnl Notes:
    dnl - Alsa is Linux only
    dnl - CoreAudio is Mac OS X only
    dnl - EsounD is reported to be available on most platforms
    dnl - FusionSound is Linux only, but don't enable it by default
    dnl - Jack is Linux and Mac OS X primarily
    dnl - OSS is most unix variants
    dnl - PulseAudio has been tested on Linux, Solaris, FreeBSD, Windows
    dnl - SunAudio is NetBSD, Solaris (anything else?)
    dnl - sndio is OpenBSD only

    default_enable_coreaudio=no
    default_enable_irixal=no
    default_enable_oss=yes
    default_enable_sunaudio=no
    default_enable_sndio=no

    default_with_alsa=no
    default_with_esound=yes
    default_with_fusionsound=no
    default_with_jack=no
    default_with_pulseaudio=no

    case "$host_os" in
        cygwin* | mingw*)
            default_enable_oss=no
            default_with_pulseaudio=yes
            ;;
        darwin*)
            default_enable_coreaudio=yes
            default_with_jack=yes
            default_enable_oss=no
            ;;
        freebsd*|kfreebsd*)
            default_with_pulseaudio=yes
            default_with_jack=yes
            ;;
        gnu*)
            default_with_pulseaudio=yes
            default_with_jack=yes
            ;;
        irix*)
            default_enable_irixal=yes
            default_enable_oss=no
            ;;
        linux*)
            default_with_alsa=yes
            default_with_jack=yes
            default_with_pulseaudio=yes
            ;;
        netbsd*)
            default_enable_sunaudio=yes
            ;;
        openbsd*)
            default_enable_oss=no
            default_enable_sndio=yes
            ;;
        solaris*)
            default_with_pulseaudio=yes
            default_enable_sunaudio=yes
            ;;
    esac


    dnl Alsa support
    XINE_ARG_WITH([alsa], [Build with ALSA audio output support])
    if test x"$with_alsa" != x"no"; then
        PKG_CHECK_MODULES([ALSA], [alsa >= 0.9.0], [have_alsa=yes], [have_alsa=no])
        if test x"$hard_with_alsa" = x"yes" && test x"$have_alsa" != x"yes"; then
            AC_MSG_ERROR([ALSA support requested but not found.])
        elif test x"$have_alsa" = x"yes"; then
            dnl This is needed by src/input/input_v4l.c
            AC_DEFINE([HAVE_ALSA], 1, [Define this if you have ALSA installed])
        fi
    fi
    AM_CONDITIONAL([ENABLE_ALSA], [test x"$have_alsa" = x"yes"])


    dnl CoreAudio for Mac OS X
    XINE_ARG_ENABLE([coreaudio], [Enable support for Mac OS X CoreAudio])
    if test x"$enable_coreaudio" != x"no"; then
        AC_MSG_CHECKING([for CoreAudio frameworks])
        ac_save_LIBS="$LIBS" LIBS="$LIBS -framework CoreAudio -framework AudioUnit"
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[return 0]])], [have_coreaudio=yes], [have_coreaudio=no])
        LIBS="$ac_save_LIBS"
        AC_MSG_RESULT([$have_coreaudio])
        if test x"hard_$enable_coreaudio" = x"yes" && test x"$have_coreaudio" != x"yes"; then
            AC_MSG_ERROR([CoreAudio support requested, but CoreAudio not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_COREAUDIO], [test x"$have_coreaudio" = x"yes"])


    dnl EsounD support
    XINE_ARG_WITH([esound], [Build with EsounD audio output support])
    if test x"$with_esound" != x"no"; then
        PKG_CHECK_MODULES([ESD], [esound], [have_esound=yes], [have_esound=no])
        if test x"$hard_with_esound" = x"yes" && test x"$have_esound" != x"yes"; then
            AC_MSG_ERROR([EsounD support requested, but EsounD not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_ESD], [test x"$have_esound" = x"yes"])


    dnl FusionSound support
    XINE_ARG_WITH([fusionsound], [Build with FunsionSound audio output support])
    if test x"$with_fusionsound" != x"no"; then
        PKG_CHECK_MODULES([FUSIONSOUND], [fusionsound >= 0.9.23], [have_fusionsound=yes], [have_fusionsound=no])
        if test x"$hard_with_fusionsound" = x"yes" && test x"$have_fusionsound" != x"yes"; then
            AC_MSG_ERROR([FusionSound support requested, but FusionSound not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_FUSIONSOUND], [test x"$have_fusionsound" = x"yes"])


    dnl IRIX style audio interface
    XINE_ARG_ENABLE([irixal], [Enable support for IRIX libaudio])
    if test x"$enable_irixal" != x"no"; then
        AC_CACHE_CHECK([for IRIX libaudio support], [am_cv_have_irixal],
                       [AC_CHECK_HEADER([dmedia/audio.h],
                       [am_cv_have_irixal=yes], [am_cv_have_irixal=no])])
        if test x"$am_cv_have_irixal" = x"yes"; then
            AC_DEFINE([HAVE_IRIXAL], 1, [Define this if you have a usable IRIX al interface available])
            IRIXAL_LIBS="-laudio"
            IRIXAL_STATIC_LIB="/usr/lib/libaudio.a"
            AC_SUBST(IRIXAL_LIBS)
            AC_SUBST(IRIXAL_STATIC_LIB)
        fi
    fi
    AM_CONDITIONAL([ENABLE_IRIXAL], [test x"$am_cv_have_irixal" = x"yes"])


    dnl JACK support
    XINE_ARG_WITH([jack], [Build with Jack support])
    if test x"$with_jack" != x"no"; then
        PKG_CHECK_MODULES([JACK], [jack >= 0.100], [have_jack=yes], [have_jack=no])
        if test x"$hard_with_jack" = x"yes" && test x"$have_jack" != x"yes"; then
            AC_MSG_ERROR([Jack support requested, but Jack not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_JACK], [test x"$have_jack" = x"yes"])


    dnl OSS (Open Sound System)
    XINE_ARG_ENABLE([oss], [Enable OSS (Open Sound System) support])
    if test x"$enable_oss" != x"no"; then

   case "$host" in
     *-*-netbsd*)
       OSS_LIBS="-lossaudio"
       ;;
     *)
       OSS_LIBS=""
       ;;
   esac

        AC_CHECK_HEADERS([sys/soundcard.h machine/soundcard.h soundcard.h], [break])
        AC_CHECK_DECL([SNDCTL_DSP_SETFRAGMENT], [have_oss=yes], [have_oss=no],
                      [#ifdef __NetBSD__
                       #include <sys/ioctl.h>
                       #endif
                       #ifdef HAVE_SYS_SOUNDCARD_H
                       # include <sys/soundcard.h>
                       #endif
                       #ifdef HAVE_MACHINE_SOUNDCARD_H
                       # include <sys/soundcard.h>
                       #endif
                       #ifdef HAVE_SOUNDCARD_H
                       # include <soundcard.h>
                       #endif])
        if test x"$hard_enable_oss" = x"yes" && test x"$have_oss" != x"yes"; then
            AC_MSG_ERROR([OSS support requested, but OSS not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_OSS], [test x"$have_oss" = x"yes"])
    AC_SUBST(OSS_LIBS)


    dnl PulseAudio
    XINE_ARG_WITH([pulseaudio], [Build with PulseAudio support])
    if test x"$with_pulseaudio" != x"no"; then
        PKG_CHECK_MODULES([PULSEAUDIO], [libpulse], [have_pulseaudio="yes"], [have_pulseaudio="no"])
        if test x"$hard_with_pulseaudio" = x"yes" && test x"$have_pulseaudio" != x"yes"; then
            AC_MSG_ERROR([PulseAudio support requested, but PulseAudio not found])
        fi
        if test x"$have_pulseaudio" = xyes; then
            AC_MSG_CHECKING([for pulseaudio >= 0.9.7])
            PKG_CHECK_EXISTS([libpulse >= 0.9.7],
                [have_pulseaudio_0_9_7="yes"],
                [have_pulseaudio_0_9_7="no"])
            AC_MSG_RESULT([$have_pulseaudio_0_9_7])
            if test x"$have_pulseaudio_0_9_7" = xyes; then
                AC_DEFINE([HAVE_PULSEAUDIO_0_9_7], 1, [define this if you have pulseaudio >= 0.9.7])
            fi
        fi
    fi
    AM_CONDITIONAL([ENABLE_PULSEAUDIO], [test x"$have_pulseaudio" = x"yes"])


    dnl SUN style audio interface
    XINE_ARG_ENABLE([sunaudio], [Enable Sun audio support])
    if test x"$enable_sunaudio" != x"no"; then
        AC_MSG_CHECKING([for Sun audio support])
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
                                             #include <sys/audioio.h>]],
                                           [[audio_info_t audio_info; AUDIO_INITINFO(&audio_info)]])],
                          [have_sunaudio=yes], [have_sunaudio=no])
        AC_MSG_RESULT([$have_sunaudio])
        if test x"$hard_enable_sunaudio" = x"yes" && test x"$have_sunaudio" != x"yes"; then
            AC_MSG_ERROR([Sun audio support requested, but Sun audio not found])
        elif test x"$have_sunaudio" = x"yes"; then
           dnl NetBSD doesn't have this, but check for it rather than
           dnl assuming that it doesn't happen elsewhere.
           AC_CHECK_MEMBERS([audio_info_t.output_muted])
        fi
    fi
    AM_CONDITIONAL([ENABLE_SUNAUDIO], [test x"$have_sunaudio" = x"yes"])


    dnl sndio support
    XINE_ARG_ENABLE([sndio], [Enable sndio support])
    if test x"$enable_sndio" != "xno"; then
	AC_CHECK_LIB([sndio], [sio_open], [SNDIO_LIBS=-lsndio; have_sndio=yes],
		     [have_sndio=no])
	if test "x$hard_enable_sndio" = "xyes" && test "x$have_sndio" = "xno"; then
	    AC_MSG_ERROR([sndio support requested, but sndio not found])
	fi
    fi
    AM_CONDITIONAL([ENABLE_SNDIO], [test "x$have_sndio" = "xyes"])
    AC_SUBST([SNDIO_CFLAGS])
    AC_SUBST([SNDIO_LIBS])
])dnl XINE_AUDIO_OUT_PLUGINS
