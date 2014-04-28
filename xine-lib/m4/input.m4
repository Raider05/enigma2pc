dnl -------------
dnl Input Plugins
dnl -------------
AC_DEFUN([XINE_INPUT_PLUGINS], [
    dnl Setup defaults for the target operating system.  For example, v4l is
    dnl only ever available on Linux, so don't bother checking for it unless
    dnl explicitly requested to do so on other operating systems.
    dnl Notes:
    dnl - dvb is Linux only
    dnl - v4l is Linux only

    default_enable_dvb=no
    default_enable_gnomevfs=yes
    default_enable_samba=yes
    default_enable_v4l=no
    default_enable_v4l2=no
    default_enable_libv4l=no
    default_enable_vcd=yes
    default_enable_vcdo=no
    default_enable_vdr=yes
    default_enable_bluray=yes
    default_enable_avformat=yes
    default_with_external_dvdnav=no

    case "$host_os" in
        cygwin* | mingw*)
            default_enable_gnomevfs=no
            default_enable_samba=no
            ;;
        darwin*)
            default_enable_gnomevfs=no
            default_enable_samba=no
            ;;
        freebsd*|kfreebsd*)
            default_enable_vcdo=yes
            ;;
        netbsd* | openbsd*)
            default_enable_v4l2=yes
            ;;
        linux*)
            default_enable_dvb=yes
            default_enable_v4l=yes
            default_enable_v4l2=yes
            default_enable_libv4l=yes
            default_enable_vcdo=yes
            ;;
        solaris*)
            default_enable_vcdo=yes
            default_enable_v4l2=yes
            ;;
    esac

    dnl default_enable_libv4l="$default_enable_v4l2"

    dnl dvb
    XINE_ARG_ENABLE([dvb], [Enable support for the DVB plugin (Linux only)])
    if test x"$enable_dvb" != x"no"; then
        case "$host_os" in
            linux*) have_dvb=yes ;;
            *) have_dvb=no ;;
        esac
        if test x"$hard_enable_dvb" = x"yes" && test x"$have_dvb" != x"yes"; then
            AC_MSG_ERROR([DVB support requested, but DVB not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_DVB], [test x"$have_dvb" = x"yes"])


    dnl gnome-vfs
    XINE_ARG_ENABLE([gnomevfs], [Enable support for the Gnome-VFS plugin])
    if test x"$enable_gnomevfs" != x"no"; then
        PKG_CHECK_MODULES([GNOME_VFS], [gnome-vfs-2.0], [have_gnomevfs=yes], [have_gnome_vfs=no])
        if test x"$hard_enable_gnomevfs" = x"yes" && test x"$have_gnomevfs" != x"yes"; then
            AC_MSG_ERROR([Gnome-VFS support requested, but Gnome-VFS not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_GNOME_VFS], [test x"$have_gnomevfs" = x"yes"])


    dnl libsmbclient
    XINE_ARG_ENABLE([samba], [Enable support for the Samba plugin])
    if test x"$enable_samba" != x"no"; then
        PKG_CHECK_MODULES([LIBSMBCLIENT], [smbclient],
          [have_samba=yes],
          AC_MSG_RESULT(*** All libsmbclient dependent parts will be disabled ***))
        AC_SUBST(LIBSMBCLIENT_CFLAGS)
        AC_SUBST(LIBSMBCLIENT_LIBS)
        if test x"$hard_enable_samba" = x"yes" && test x"$have_samba" != x"yes"; then
            AC_MSG_ERROR([Samba support requested, but Samba not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_LIBSMBCLIENT], [test x"$have_samba" = x"yes"])


    dnl video-for-linux (v4l)
    XINE_ARG_ENABLE([v4l], [Enable Video4Linux support])
    if test x"$enable_v4l" != x"no"; then
        have_v4l=yes
        AC_CHECK_HEADERS([linux/videodev.h], , [have_v4l=no])
        AC_CHECK_HEADERS([asm/types.h])
        if test x"$hard_enable_v4l" = x"yes" && test x"$have_v4l" != x"yes"; then
            AC_MSG_ERROR([Video4Linux support requested, but prerequisite headers not found.])
        fi
    fi
    AM_CONDITIONAL([ENABLE_V4L], [test x"$have_v4l" = x"yes"])

    XINE_ARG_ENABLE([v4l2], [Enable Video4Linux 2 support])
    if test x"$enable_v4l2" != x"no"; then
        have_v4l2=yes
	AC_CHECK_HEADERS([linux/videodev2.h sys/videoio.h sys/videodev2.h], [have_v4l2=yes], [])
        AC_CHECK_HEADERS([asm/types.h])
        if test x"$hard_enable_v4l2" = x"yes" && test x"$have_v4l2" != x"yes"; then
            AC_MSG_ERROR([Video4Linux 2 support requested, but prerequisite headers not found.])
        fi
	XINE_ARG_ENABLE([libv4l], [Enable libv4l support])
 	if test "x$enable_libv4l" != "xno"; then
	    PKG_CHECK_MODULES([V4L2], [libv4l2],
		[have_libv4l=yes
		 AC_DEFINE([HAVE_LIBV4L2_H], [1], [Define this if you have libv4l installed])],
		[have_libv4l=no])
	    if test "x$hard_enable_libv4l" = "xyes" && test "x$have_libv4l" = "xno"; then
		AC_MSG_ERROR([libv4l requested, but libv4l not found])
	    fi
	fi
    fi
    AM_CONDITIONAL([ENABLE_V4L2], [test x"$have_v4l2" = x"yes"])

    dnl dvdnav
    dnl XXX: This could be cleaned up so that code does not have to ifdef so much
    XINE_ARG_WITH([external-dvdnav], [Use external dvdnav library (not recommended)])
    if test x"$with_external_dvdnav" != x"no"; then
        PKG_CHECK_MODULES([DVDREAD], [dvdread],
                          [PKG_CHECK_MODULES([DVDNAV], [dvdnav],
                                             [AC_DEFINE([HAVE_DVDNAV], 1, [Define this if you have a suitable version of libdvdnav])],
                                             [AC_MSG_RESULT([*** no usable version of libdvdnav found, using internal copy ***])])],
                          [AC_MSG_RESULT([*** no usable version of libdvdread found, using internal libdvdnav ***])])
    else
        AC_MSG_RESULT([Using included DVDNAV support])
    fi
    AM_CONDITIONAL([WITH_EXTERNAL_DVDNAV], [test x"$with_external_dvdnav" != x"no"])


    dnl Video CD
    dnl XXX: This could be cleaned up so that code does not have it ifdef so much
    XINE_ARG_ENABLE([vcd], [Enable VCD (VideoCD) support])
    if test x"$enable_vcd" != x"no"; then
	no_vcd=no
        PKG_CHECK_MODULES([LIBCDIO], [libcdio >= 0.71],
        	[PKG_CHECK_MODULES([LIBVCDINFO], [libvcdinfo >= 0.7.23], [], [no_vcd=yes])],
		[if test x"$hard_enable_vcd" = 'xyes'; then
		   AC_MSG_ERROR([$LIBCDIO_PKG_ERRORS])
		 fi
		 no_vcd=yes]
        )
        if test "$no_vcd" = 'no'; then
	    AC_DEFINE([HAVE_VCDNAV], 1, [Define this if you use external libcdio/libvcdinfo])
	fi
    fi

    enable_vcdo=no
    test $default_enable_vcdo = yes && test x"$enable_vcd" != x"no" && enable_vcdo=yes

    AC_DEFINE([LIBCDIO_CONFIG_H], 1, [Get of rid system libcdio build configuration])
    AC_DEFINE([EXTERNAL_LIBCDIO_CONFIG_H], 1, [Get of rid system libcdio build configuration])
    AC_SUBST(LIBCDIO_CFLAGS)
    AC_SUBST(LIBCDIO_LIBS)
    AC_SUBST(LIBVCD_CFLAGS)
    AC_SUBST(LIBVCD_LIBS)
    AM_CONDITIONAL([ENABLE_VCD], [test x"$enable_vcd" != x"no"])
    AM_CONDITIONAL([ENABLE_VCDO], [test x"$enable_vcdo" != x"no"])


    dnl vdr
    XINE_ARG_ENABLE([vdr], [Enable support for the VDR plugin (default: enabled)])
    AM_CONDITIONAL([ENABLE_VDR], [test x"$enable_vdr" != x"no"])

    dnl bluray
    XINE_ARG_ENABLE([bluray], [Enable BluRay support])
    if test "x$enable_bluray" != "xno"; then
        PKG_CHECK_MODULES([LIBBLURAY], [libbluray >= 0.2.1], [have_libbluray=yes], [have_libbluray=no])
        if test x"$hard_enable_bluray" = x"yes" && test x"$have_libbluray" != x"yes"; then
            AC_MSG_ERROR([BluRay support requested, but libbluray not found])
        fi
        AC_SUBST(LIBBLURAY_CFLAGS)
        AC_SUBST(LIBBLURAY_LIBS)
    fi
    AM_CONDITIONAL(ENABLE_BLURAY, test "x$have_libbluray" = "xyes")

    dnl libavformat
    XINE_ARG_ENABLE([avformat], [Enable libavformat support])
    if test "x$enable_avformat" != "xno"; then
        PKG_CHECK_MODULES([AVFORMAT], [libavformat >= 53.21.1], [have_avformat=yes], [have_avformat=no])
        if test x"$hard_enable_avformat" = x"yes" && test x"$have_avformat" != x"yes"; then
            AC_MSG_ERROR([libavformat support requested, but library not found])
        fi
        if test x"$have_avformat" = x"yes"; then
            AC_DEFINE([HAVE_AVFORMAT], 1, [Define this if you have libavformat installed])
        fi
    fi
    AM_CONDITIONAL([ENABLE_AVFORMAT], [test x"$have_avformat" = x"yes"])

])
