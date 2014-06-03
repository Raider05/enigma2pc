dnl ---------------------------
dnl Decoder and Demuxer Plugins
dnl ---------------------------
AC_DEFUN([XINE_DECODER_PLUGINS], [
    dnl a52dec (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([a52dec],
                  [AS_HELP_STRING([--enable-a52dec], [Enable support for a52dec decoding library (default: enabled, internal: use internal copy)])])
    if test x"$enable_a52dec" != x"no"; then
        if test x"$enable_a52dec" != x"internal"; then
            AC_CHECK_LIB([a52], [a52_init],
                         [AC_CHECK_HEADERS([a52dec/a52.h], [have_external_a52dec=yes], [have_external_a52dec=no],
                                           [#ifdef HAVE_SYS_TYPES_H
                                            # include <sys/types.h>
                                            #endif
                                            #ifdef HAVE_INTTYPES_H
                                            # include <inttypes.h>
                                            #endif
                                            #ifdef HAVE_STDINT_H
                                            # include <stdint.h>
                                            #endif
                                            #include <a52dec/a52.h>])], [have_external_a52dec=no], [-lm])
            if test x"$have_external_a52dec" = x"no"; then
                AC_MSG_RESULT([*** no usable version of a52dec found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included a52dec support])
        fi
        if test x"$have_external_a52dec" = x"yes"; then
            A52DEC_CFLAGS=''
            A52DEC_LIBS='-la52'
            A52DEC_DEPS=''
	else
            A52DEC_CFLAGS='-I$(top_srcdir)/contrib/a52dec'
            A52DEC_LIBS='$(top_builddir)/contrib/a52dec/liba52.la'
            A52DEC_DEPS='$(top_builddir)/contrib/a52dec/liba52.la'
        fi
        AC_SUBST(A52DEC_CFLAGS)
        AC_SUBST(A52DEC_DEPS)
        AC_SUBST(A52DEC_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_A52DEC], [test x"$enable_a52dec" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_A52DEC], [test x"$have_external_a52dec" = x"yes"])


    dnl ASF (optional; enabled by default)
    AC_ARG_ENABLE([asf],
                  [AS_HELP_STRING([--enable-asf], [Enable support for ASF demuxer (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_asf="yes"])
    AM_CONDITIONAL([ENABLE_ASF], [test x"$enable_asf" != x"no"])

    dnl Nosefart (optional, enabled by default)
    AC_ARG_ENABLE([nosefart],
                  [AS_HELP_STRING([--enable-nosefart], [Enable support for nosefart player (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_nosefart="yes"])
    AM_CONDITIONAL([ENABLE_NOSEFART], [test "x$enable_nosefart" != "xno"])

    dnl FAAD (optional; enabled by default)
    AC_ARG_ENABLE([faad],
                  [AS_HELP_STRING([--enable-faad], [Enable support for FAAD decoder (default: enabled, internal: use internal copy)])])
    if test x"$enable_faad" != x"no"; then
        if test x"$enable_faad" != x"internal"; then
            AC_CHECK_LIB([faad], [NeAACDecInit],
                         [AC_CHECK_HEADERS([neaacdec.h], [have_external_faad=yes], [have_external_faad=no],
                                           [#include <neaacdec.h>])], [have_external_faad=no], [-lm])
            if test x"$have_external_faad" = x"no"; then
                AC_MSG_RESULT([*** no usable version of libfaad found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included libfaad support])
        fi
        if test x"$have_external_faad" = x"yes"; then
            FAAD_CFLAGS=''
            FAAD_LIBS='-lfaad'
            FAAD_DEPS=''
	else
            FAAD_CFLAGS='-I$(top_srcdir)/contrib/libfaad'
            FAAD_LIBS='$(top_builddir)/contrib/libfaad/libfaad.la'
            FAAD_DEPS='$(top_builddir)/contrib/libfaad/libfaad.la'
        fi
        AC_SUBST(FAAD_CFLAGS)
        AC_SUBST(FAAD_DEPS)
        AC_SUBST(FAAD_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_FAAD], [test x"$enable_faad" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_FAAD], [test x"$have_external_faad" = x"yes"])

    dnl ffmpeg external version required
    PKG_CHECK_MODULES([FFMPEG], [libavcodec >= 51.68.0])
    PKG_CHECK_MODULES([AVUTIL], [libavutil >= 49.6.0])
    PKG_CHECK_MODULES([FFMPEG_POSTPROC], [libpostproc])
    AC_DEFINE([HAVE_FFMPEG], 1, [Define this if you have ffmpeg library])

	dnl Check presence of ffmpeg/avutil.h to see if it's old or new
	dnl style for headers. The new style would be preferred actually...
	ac_save_CFLAGS="$CFLAGS" CFLAGS="$CFLAGS $FFMPEG_CFLAGS"
	ac_save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CFLAGS $FFMPEG_CFLAGS $AVUTIL_CFLAGS"
	AC_CHECK_HEADERS([ffmpeg/avutil.h])
	AC_CHECK_HEADERS([libavutil/avutil.h])
	AC_CHECK_HEADERS([libavutil/sha1.h])
	AC_CHECK_HEADERS([libavutil/sha.h])
	if test "$ac_cv_header_ffmpeg_avutil_h" = "yes" && test "$ac_cv_header_libavutil_avutil_h" = "yes"; then
	    AC_MSG_ERROR([old & new ffmpeg headers found - you need to clean up!])
	fi
	CPPFLAGS="$ac_save_CPPFLAGS"
        CFLAGS="$ac_save_CFLAGS"

    dnl gdk-pixbuf (optional; enabled by default)
    AC_ARG_ENABLE([gdkpixbuf],
                  [AS_HELP_STRING([--enable-gdkpixbuf], [Enable GdkPixbuf support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_gdkpixbuf="yes"])
    if test x"$enable_gdkpixbuf" != x"no"; then
        PKG_CHECK_MODULES([GDK_PIXBUF], [gdk-pixbuf-2.0], [have_gdkpixbuf=yes], [have_gdkpixbuf=no])
        if test x"$enable_gdkpixbuf" = x"yes" && test x"$have_gdkpixbuf" != x"yes"; then
            AC_MSG_ERROR([GdkPixbuf support requested, but GdkPixbuf not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_GDK_PIXBUF], [test x"$have_gdkpixbuf" = x"yes"])

    dnl libjpeg (optional; enabled by default)
    AC_ARG_ENABLE([libjpeg],
                  [AS_HELP_STRING([--enable-libjpeg], [Enable libjpeg support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_libjpeg="yes"])
    if test x"$enable_libjpeg" != x"no"; then
        AC_CHECK_LIB([jpeg], [jpeg_start_decompress],
                     [AC_CHECK_HEADERS([jpeglib.h], [have_libjpeg=yes], [have_libjpeg=no])], [have_libjpeg=no])
        if test x"$enable_libjpeg" = x"yes" && test x"$have_libjpeg" != x"yes"; then
             AC_MSG_ERROR([libjpeg support requested, but libjpeg not found])
        elif test x"$have_libjpeg" = x"yes"; then
            JPEG_LIBS="-ljpeg"
            AC_SUBST(JPEG_LIBS)
        fi
    fi
    AM_CONDITIONAL([ENABLE_LIBJPEG], [test x"$have_libjpeg" = x"yes"])

    dnl ImageMagick (optional; enabled by default)
    AC_ARG_WITH([imagemagick],
                [AS_HELP_STRING([--with-imagemagick], [Enable ImageMagick image decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_imagemagick="yes"])
    if test x"$with_imagemagick" != x"no"; then
        PKG_CHECK_MODULES([WAND], [Wand], [have_imagemagick=yes], [have_imagemagick=no])
        if test "x$have_imagemagick" = 'xno'; then
            PKG_CHECK_MODULES([MAGICKWAND], [MagickWand], [have_imagemagick=yes], [have_imagemagick=no])
            dnl Avoid $(WAND_FLAGS) $(MAGICKWAND_FLAGS) ...
            WAND_CFLAGS="$MAGICKWAND_CFLAGS"
            WAND_LIBS="$MAGICKWAND_LIBS"
        fi
        if test "x$have_imagemagick" = 'xno'; then
            PKG_CHECK_MODULES([GRAPHICSMAGICK], [ImageMagick], [have_imagemagick=yes], [have_imagemagick=no])
            PKG_CHECK_MODULES([GRAPHICSMAGICKWAND], [GraphicsMagickWand], [have_imagemagick=yes], [have_imagemagick=no])
            dnl The following assignments are safe, since they include
            dnl the flags for plain GraphicsMagick
            WAND_CFLAGS="$GRAPHICSMAGICKWAND_CFLAGS"
            WAND_LIBS="$GRAPHICSMAGICKWAND_LIBS"
            AC_DEFINE([HAVE_GRAPHICSMAGICK], [1], [Define this if you have GraphicsMagick installed])
        fi
        if test x"$with_imagemagick" = x"yes" && test x"$have_imagemagick" = x"no"; then
            AC_MSG_ERROR([ImageMagick support requested, but neither Wand, MagickWand, nor GraphicsMagick were found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_IMAGEMAGICK], [test x"$have_imagemagick" = x"yes"])


    dnl libdts (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([dts],
                  [AS_HELP_STRING([--enable-dts], [Enable support for DTS decoding library (default: enabled, internal: use internal copy)])])
    if test x"$enable_dts" != x"no"; then
        if test x"$enable_dts" != x"internal"; then
            PKG_CHECK_MODULES([LIBDTS], [libdts], [have_external_dts=yes], [have_external_dts=no])
            if test x"$have_external_dts" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libdts found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included libdts support])
        fi
        if test x"$have_external_dts" != x"yes"; then
            LIBDTS_CFLAGS='-I$(top_srcdir)/contrib/libdca/include'
            LIBDTS_DEPS='$(top_builddir)/contrib/libdca/libdca.la'
            LIBDTS_LIBS='$(top_builddir)/contrib/libdca/libdca.la'
            AC_SUBST(LIBDTS_DEPS)
        fi
    fi
    AM_CONDITIONAL([ENABLE_DTS], [test x"$enable_dts" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_LIBDTS], [test x"$have_external_dts" = x"yes"])


    dnl libFLAC (optional; enabled by default)
    AC_ARG_WITH([libflac],
                [AS_HELP_STRING([--with-libflac], [build libFLAC-based decoder and demuxer (default: enabled)])],
                [test x"$withval" != x"no" && with_libflac="yes"])
    AC_ARG_WITH([libFLAC-prefix],
                [AS_HELP_STRING([--with-libFLAC-prefix=DIR], [prefix where libFLAC is installed (optional)])])
    AC_ARG_WITH([libFLAC-libraries],
                [AS_HELP_STRING([--with-libFLAC-libraries=DIR], [directory where libFLAC library is installed (optional)])])
    AC_ARG_WITH([libFLAC-includes],
                [AS_HELP_STRING([--with-libFLAC-includes=DIR], [directory where libFLAC header files are installed (optional)])])
    if test x"$with_libflac" != x"no"; then
        AC_MSG_CHECKING([libdir name])
        case "$host_or_hostalias" in
            *-*-linux*)
                # Test if the compiler is 64bit
                echo 'int i;' > conftest.$ac_ext
                xine_cv_cc_64bit_output=no
                if AC_TRY_EVAL(ac_compile); then
                    case `"$MAGIC_CMD" conftest.$ac_objext` in
                        *"ELF 64"*) xine_cv_cc_64bit_output=yes ;;
                    esac
                fi
                rm -rf conftest*
                ;;
        esac
        case "$host_cpu:$xine_cv_cc_64bit_output" in
            powerpc64:yes | s390x:yes | sparc64:yes | x86_64:yes)
                XINE_LIBDIRNAME="lib64" ;;
            *:*)
                XINE_LIBDIRNAME="lib" ;;
        esac
        AC_MSG_RESULT([$XINE_LIBDIRNAME])

        if test x"$with_libFLAC_includes" != x""; then
            LIBFLAC_CFLAGS="-I$with_libFLAC_includes"
        elif test x"$with_libFLAC_prefix" != x""; then
            LIBFLAC_CFLAGS="-I$with_libFLAC_prefix/include"
        elif test x"$prefix" != x"NONE"; then
            LIBFLAC_CFLAGS="-I$prefix/include"
        fi
        AC_SUBST(LIBFLAC_CFLAGS)

        if test x"$with_libFLAC_libraries" != x""; then
            LIBFLAC_LIBS="-L$with_libFLAC_libraries"
        elif test x"$with_libFLAC_prefix" != x""; then
            LIBFLAC_LIBS="-L$with_libFLAC_prefix/$XINE_LIBDIRNAME"
        elif test x"$prefix" != x"NONE"; then
            LIBFLAC_LIBS="-L$prefix/$XINE_LIBDIRNAME"
        fi
        AC_SUBST(LIBFLAC_LIBS)

        ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $LIBFLAC_CFLAGS"
        AC_CHECK_LIB([FLAC], [FLAC__stream_decoder_new],
                     [AC_CHECK_HEADERS([FLAC/stream_decoder.h],
                                       [have_libflac=yes LIBFLAC_LIBS="$LIBFLAC_LIBS -lFLAC -lm"],
                                       [have_libflac=no])],
                     [have_libflac=no], [-lm])
        CPPFLAGS="$ac_save_CPPFLAGS"

        if test x"$with_libflac" = x"yes" && test x"$have_libflac" != x"yes"; then
            AC_MSG_ERROR([libFLAC-based decoder support requested, but libFLAC not found])
        elif test x"$have_libflac" != x"yes"; then
            LIBFLAC_CFLAGS="" LIBFLAC_LIBS=""
        fi
    fi
    AM_CONDITIONAL([ENABLE_LIBFLAC], [test x"$have_libflac" = x"yes"])


    dnl libmad (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([mad],
                  [AS_HELP_STRING([--enable-mad], [Enable support for MAD decoding library (default: enabled, internal: use external copy)])])
    if test x"$enable_mad" != x"no"; then
        if test x"$enable_mad" != x"internal"; then
            PKG_CHECK_MODULES([LIBMAD], [mad],
                              [AC_CHECK_HEADERS([mad.h], [have_external_libmad=yes], [have_external_libmad=no])],
                              [have_external_libmad=no])
            if test x"$have_external_libmad" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libmad found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included libmad support])
        fi
        if test x"$have_external_libmad" != x"yes"; then
            case "$host_or_hostalias" in
                i?86-* | k?-* | athlon-* | pentium*-)
                    AC_DEFINE([FPM_INTEL], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                x86_64-*)
                    AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                ppc-* | powerpc-*)
                    AC_DEFINE([FPM_PPC], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                sparc*-*)
                    if test "$GCC" = yes; then
                        AC_DEFINE([FPM_SPARC], 1, [Define to select libmad fixed point arithmetic implementation])
                    else
                        AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    fi
                    ;;
                mips-*)
                    AC_DEFINE([FPM_MIPS], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                alphaev56-* | alpha* | ia64-* | hppa*-linux-*)
                    AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                arm*-*)
                    AC_DEFINE([FPM_ARM], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                universal-*)
                    ;;
                *)
                    AC_DEFINE([FPM_DEFAULT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
            esac
            LIBMAD_CFLAGS='-I$(top_srcdir)/contrib/libmad'
            LIBMAD_LIBS='$(top_builddir)/contrib/libmad/libmad.la'
            LIBMAD_DEPS='$(top_builddir)/contrib/libmad/libmad.la'
        fi
        AC_SUBST(LIBMAD_CFLAGS)
        AC_SUBST(LIBMAD_DEPS)
        AC_SUBST(LIBMAD_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_MAD], [test x"$enable_mad" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_MAD], [test x"$have_external_libmad" = x"yes"])


    dnl libmodplug (optional; enabled by default)
    AC_ARG_ENABLE([modplug],
                  [AS_HELP_STRING([--enable-modplug], [Enable MODPlug support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_modplug="yes"])
    if test x"$enable_modplug" != x"no"; then
        PKG_CHECK_MODULES([LIBMODPLUG], [libmodplug >= 0.7], [have_modplug=yes], [have_modplug=no])
        if test x"$enable_modplug" = x"yes" && test x"$have_modplug" != x"yes"; then
            AC_MSG_ERROR([MODPlug support requested, but MODPlug not found])
        fi
	if test "`"$PKG_CONFIG" --modversion libmodplug`" = 0.8.8; then
	    AC_MSG_ERROR([you have a broken version of libmodplug (0.8.8); cowardly refusing to use it])
	fi
    fi
    AM_CONDITIONAL([ENABLE_MODPLUG], [test x"$have_modplug" = x"yes"])


    dnl libmpeg2new (optional; disabled by default)
    AC_ARG_ENABLE([libmpeg2new],
	AS_HELP_STRING([--enable-libmpeg2new], [build the newer MPEG2 decoder (buggy)]))
    AM_CONDITIONAL([ENABLE_MPEG2NEW], [test "x$enable_libmpeg2new" = "xyes"])


    dnl libmpcdec (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([musepack],
                  [AS_HELP_STRING([--enable-musepack], [Enable support for Musepack decoding (default: enabled, internal: use external copy)])])
    if test x"$enable_musepack" != x"no"; then
        if test x"$enable_musepack" != x"internal"; then
	    AC_CHECK_LIB([mpcdec], [mpc_demux_decode],
			 [AC_CHECK_HEADERS([mpc/mpcdec.h], [have_external_libmpcdec=yes], [have_external_libmpcdec=no])],
			 [AC_CHECK_LIB([mpcdec], [mpc_decoder_decode],
				       [AC_CHECK_HEADERS([mpcdec/mpcdec.h], [have_external_libmpcdec=yes], [have_external_libmpcdec=no])],
							 [have_external_libmpcdec=no])])
            if test x"$have_external_libmpcdec" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libmpcdec found, using internal copy ***])
            else
                MPCDEC_CFLAGS=""
                MPCDEC_DEPS=""
                MPCDEC_LIBS="-lmpcdec"
            fi
        else
            AC_MSG_RESULT([Using included libmpcdec (Musepack)])
        fi
        if test x"$have_external_libmpcdec" != x"yes"; then
            MPCDEC_CFLAGS='-I$(top_srcdir)/contrib/libmpcdec'
            MPCDEC_LIBS='$(top_builddir)/contrib/libmpcdec/libmpcdec.la'
            MPCDEC_DEPS='$(top_builddir)/contrib/libmpcdec/libmpcdec.la'
        fi
        AC_SUBST(MPCDEC_CFLAGS)
        AC_SUBST(MPCDEC_DEPS)
        AC_SUBST(MPCDEC_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_MUSEPACK], [test x"$enable_musepack" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_LIBMPCDEC], [test x"$have_external_libmpcdec" = x"yes"])


    dnl mlib
    AC_ARG_ENABLE([mlib],
	          [AS_HELP_STRING([--enable-mlib], [build Sun mediaLib support (default: disabled)])],
                  [test x"$enableval" != x"no" && enable_mlib="yes"], [enable_lib="no"])
    AC_ARG_ENABLE([mlib-lazyload],
                  [AS_HELP_STRING([--enable-mlib-lazyload], [check for Sun mediaLib at runtime])],
                  [test x"$enableval" != x"no" && enable_mlib_lazyload="yes"], [enable_mlib_lazyload="no"])
    if test x"$enable_mlib" != x"no"; then
        mlibhome="$MLIBHOME" test x"$mlibhome" = x"" && mlibhome="/opt/SUNWmlib"
        AC_CHECK_LIB([mlib], [mlib_VideoAddBlock_U8_S16],
                     [saved_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS -I$mlibhome/include"
                      AC_CHECK_HEADERS([mlib_video.h],
                          [if test x"$enable_mlib_lazyload" != x"no"; then
                               if test "$GCC" = yes; then
                                   MLIB_LIBS="-L$mlibhome/lib -Wl,-z,lazyload,-lmlib,-z,nolazyload"
                               else
                                   MLIB_LIBS="-L$mlibhome/lib -z lazyload -lmlib -z nolazyload"
                               fi
                               AC_DEFINE([MLIB_LAZYLOAD], 1, [Define this if you want to load mlib lazily])
                           else
                               MLIB_LIBS="-L$mlibhome/lib -lmlib"
                           fi
                           MLIB_CFLAGS="-I$mlibhome/include"
                           AC_SUBST(MLIB_LIBS)
                           AC_SUBST(MLIB_CFLAGS)
                           dnl TODO: src/video_out/yuv2rgb.c and src/xine-utils/cpu_accel.c should be changed to use LIBMPEG2_MLIB
                           dnl       and HAVE_MLIB should go away.
                           AC_DEFINE([HAVE_MLIB], 1, [Define this if you have mlib installed])
                           AC_DEFINE([LIBMPEG2_MLIB], 1, [Define this if you have mlib installed])
                           have_mlib=yes])
                      CPPFLAGS="$saved_CPPFLAGS"], [], ["-L$mlibhome/lib"])
    fi
    AM_CONDITIONAL([HAVE_MLIB], [test x"$have_mlib" = x"yes"])


    dnl mng (optional; enabled by default)
    AC_ARG_ENABLE([mng],
                  [AS_HELP_STRING([--enable-mng], [Enable MNG decoder support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_mng="yes"])
    if test x"$enable_mng" != x"no"; then
        AC_CHECK_LIB([mng], [mng_initialize],
                     [AC_CHECK_HEADERS([libmng.h], [have_mng=yes], [have_mng=no])], [have_mng=no])
        if test x"$enable_mng" = x"yes" && test x"$have_mng" != x"yes"; then
            AC_MSG_ERROR([MNG support requested, but libmng not found])
        elif test x"$have_mng" = x"yes"; then
            MNG_LIBS="-lmng"
            AC_SUBST(MNG_LIBS)
        fi
    fi
    AM_CONDITIONAL([ENABLE_MNG], [test x"$have_mng" = x"yes"])


    dnl Ogg/Speex (optional; enabled by default; external)
    AC_ARG_WITH([speex],
                [AS_HELP_STRING([--with-speex], [Enable Speex audio decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_speex="yes"])
    if test x"$with_speex" != x"no"; then
        PKG_CHECK_MODULES([SPEEX], [ogg speex], [have_speex=yes], [have_speex=no])
        if test x"$with_speex" = x"yes" && test x"$have_speex" != x"yes"; then
            AC_MSG_ERROR([Speex support requested, but libspeex and/or libogg not found])
        elif test x"$have_speex" = x"yes"; then
            AC_DEFINE([HAVE_SPEEX], 1, [Define this if you have speex])
        fi
    fi
    AM_CONDITIONAL([ENABLE_SPEEX], [test x"$have_speex" = x"yes"])


    dnl Ogg/Theora (optional; enabled by default; external)
    AC_ARG_WITH([theora],
                [AS_HELP_STRING([--with-theora], [Enable Theora video decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_theora="yes"])
    if test x"$with_theora" != x"no"; then
        PKG_CHECK_MODULES([THEORA], [ogg theora], [have_theora=yes], [have_theora=no])
        if test x"$with_theora" = x"yes" && test x"$have_theora" = x"no"; then
            AC_MSG_ERROR([Theora support requested, but libtheora and/or libogg not found])
        elif test x"$have_theora" = x"yes"; then
            AC_DEFINE([HAVE_THEORA], 1, [Define this if you have theora])
        fi
    fi
    AM_CONDITIONAL([ENABLE_THEORA], [test x"$have_theora" = x"yes"])


    dnl Ogg/Vorbis (optional; enabled by default; external)
    AC_ARG_WITH([vorbis],
                [AS_HELP_STRING([--with-vorbis], [Enable Vorbis audio decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_vorbis="yes"])
    if test x"$with_vorbis" != x"no"; then
        PKG_CHECK_MODULES([VORBIS], [ogg vorbis], [have_vorbis=yes], [have_vorbis=no])
        if test x"$with_vorbis" = x"yes" && test x"$have_vorbis" = "xno"; then
            AC_MSG_ERROR([Vorbis support requested, but libvorbis and/or libogg not found])
        elif test x"$have_vorbis" = x"yes"; then
            AC_DEFINE([HAVE_VORBIS], 1, [Define this if you have vorbis])
        fi
    fi
    AM_CONDITIONAL([ENABLE_VORBIS], [test x"$have_vorbis" = x"yes"])


    dnl real (optional; enabled by default)
    dnl On some systems, we cannot enable Real codecs support to begin with.
    dnl This includes Darwin, because it uses Mach-O rather than ELF.
    AC_ARG_ENABLE([real-codecs],
                  [AS_HELP_STRING([--enable-real-codecs], [Enable Real binary codecs support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_real_codecs="yes"])
    AC_ARG_WITH([real-codecs-path],
                [AS_HELP_STRING([--with-real-codecs-path=PATH], [Specify directory for Real binary codecs])])
    if test x"$enable_real_codecs" != x"no"; then
        case "$host_os" in
            darwin*) have_real_codecs=no ;;
            *)
                have_real_codecs=yes

                dnl For those that have a replacement, break at the first one found
                AC_CHECK_SYMBOLS([__environ _environ environ], [break], [need_weak_aliases=yes])
                AC_CHECK_SYMBOLS([stderr __stderrp], [break], [need_weak_aliases=yes])

                dnl For these there are no replacements
                AC_CHECK_SYMBOLS([___brk_addr __ctype_b])

                if test x"$need_weak_aliases" = x"yes"; then
                    CC_ATTRIBUTE_ALIAS([], [have_real_codecs=no])
                fi
                ;;
        esac
        if test x"$enable_real_codecs" = x"yes" && test x"$have_real_codecs" != x"yes"; then
            AC_MSG_ERROR([Binary Real codec support requested, but it is not available])
        elif test x"$have_real_codecs" = x"yes"; then
            if test "${with_real_codecs_path+set}" = "set"; then
                AC_DEFINE_UNQUOTED([REAL_CODEC_PATH], ["$with_real_codecs_path"], [Default path in which to find Real binary codecs])
            fi
        fi
    fi
    AM_CONDITIONAL([ENABLE_REAL], [test x"$have_real_codecs" = x"yes"])


    dnl w32dll (optional; x86 only; enabled if using GNU as; GNU as required)
    AC_ARG_ENABLE([w32dll],
                  [AS_HELP_STRING([--enable-w32dll], [Enable Win32 DLL support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_w32dll="yes"],
                  [test x"$with_gnu_as" != x"yes" && enable_w32dll="no"])
    AC_ARG_WITH([w32-path],
                [AS_HELP_STRING([--with-w32-path=PATH], [location of Win32 binary codecs])],
                [w32_path="$withval"], [w32_path="/usr/lib/codecs"])
    if test x"$enable_w32dll" != x"no"; then
        case "$host_or_hostalias" in
            *-mingw* | *-cygwin) have_w32dll=no ;;
            i?86-* | k?-* | athlon-* | pentium*-) have_w32dll="$with_gnu_as" ;;
            *) enable_w32dll=no ;;
        esac
        if test x"$enable_w32dll" = x"yes" && test x"$have_w32dll" != x"yes"; then
            AC_MSG_ERROR([Win32 DLL support requested, but Win32 DLL support is not available])
        fi
    fi
    AC_SUBST(w32_path)
    AM_CONDITIONAL([ENABLE_W32DLL], [test x"$have_w32dll" = x"yes"])


    dnl wavpack (optional; disabled by default)
    AC_ARG_WITH([wavpack],
                [AS_HELP_STRING([--with-wavpack], [Enable Wavpack decoder (requires libwavpack)])],
                [test x"$withval" != x"no" && with_wavpack="yes"], [with_wavpack="no"])
    if test x"$with_wavpack" != x"no"; then
        PKG_CHECK_MODULES([WAVPACK], [wavpack], [have_wavpack=yes], [have_wavpack=no])
        if test x"$with_wavpack" = x"yes" && test x"$have_wavpack" != x"yes"; then
            AC_MSG_ERROR([Wavpack decoder support requested, but libwavpack not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_WAVPACK], [test x"$have_wavpack" = x"yes"])

    dnl libvpx decoder plugin
    AC_ARG_ENABLE([vpx],
                  [AS_HELP_STRING([--enable-vpx], [Enable libvpx VP8/VP9 decoder support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_vpx="yes"])
    if test x"$enable_vpx" != x"no"; then
        PKG_CHECK_MODULES([VPX], [vpx] , [have_vpx=yes], [have_vpx=no])
        if test x"$enable_vpx" = x"yes" && test x"$have_vpx" != x"yes"; then
            AC_MSG_ERROR([VP8/VP9 support requested, but libvpx not found])
        fi
        AC_CHECK_LIB([vpx],[vpx_codec_vp9_dx], [
                AC_DEFINE([HAVE_VPX_VP9_DECODER], 1, [Define this if you have VP9 support in libvpx])
            ], [], [${VPX_LIBS}])
    fi
    AM_CONDITIONAL([ENABLE_VPX], [test x"$have_vpx" = x"yes"])

    dnl Broadcom MMAL (Multi Media Abstraction Layer) decoder plugin for RPi
    AC_ARG_ENABLE([mmal],
                  [AS_HELP_STRING([--enable-mmal], [Enable libmmal HW decoder and video output plugin for Raspberry Pi (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_mmal="yes"])
    if test x"$enable_mmal" != "no"; then
        saved_CPPFLAGS="$CPPFLAGS"
        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="${LDFLAGS} -L/opt/vc/lib"
        CPPFLAGS="${CPPFLAGS} -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads"

        AC_CHECK_LIB([bcm_host], [bcm_host_init], [have_mmal=yes], [have_mmal=no])
        if test x"$enable_mmal" = x"yes" && test x"$have_mmal" != x"yes"; then
            AC_MSG_ERROR([Cannot find bcm library])
        else
            AC_CHECK_HEADERS([interface/mmal/mmal.h], [have_mmal=yes], [have_mmal=no])
            if test x"$enable_mmal" = x"yes" && test x"$have_mmal" != x"yes"; then
                AC_MSG_ERROR([Cannot find MMAL headers])
            fi
        fi

        if test x"$have_mmal" = x"yes"; then
            MMAL_LIBS="-lbcm_host -lmmal -lmmal_core -lmmal_util"
            MMAL_LDFLAGS="-L/opt/vc/lib"
            MMAL_CFLAGS="-I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux"
            AC_SUBST(MMAL_LIBS)
            AC_SUBST(MMAL_LDFLAGS)
            AC_SUBST(MMAL_CFLAGS)
            AC_DEFINE([HAVE_MMAL], 1, [Define this if you have MMAL installed])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
    fi
    AM_CONDITIONAL([ENABLE_MMAL], [test x"$have_mmal" = x"yes"])

    dnl Only enable building dmx image if either gdk_pixbuf or ImageMagick are enabled
    AM_CONDITIONAL([BUILD_DMX_IMAGE], [test x"$have_imagemagick" = x"yes" -o x"$have_gdkpixbuf" = x"yes"])
])
