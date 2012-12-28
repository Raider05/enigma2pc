AC_DEFUN([MACOSX_UNIVERSAL_BINARIES], [
    AC_ARG_ENABLE([macosx-universal],
                  AS_HELP_STRING([--enable-macosx-universal], [build universal binaries for Mac OS X)]),
                  [], [enable_macosx_universal="no"])
    if test x"$enable_macosx_universal" != x"no" ; then
        case "$host_os" in
            *darwin*)
                dnl x64_64 and ppc64 binaries could also be built, but there is no
                dnl version of Mac OS X currently shipping that can run them, so
                dnl do not enable them by default for now.
                if test x"$enable_macosx_universal" = x"yes" ; then
                    UNIVERSAL_ARCHES="i386 ppc"
                else
                    UNIVERSAL_ARCHES=`echo "$enable_macosx_universal" | sed -e 's/,/ /g'`
                fi
                ;;
            *)
                AC_MSG_ERROR([Universal binaries can only be built on Darwin])
                ;;
        esac
        AC_SUBST(UNIVERSAL_ARCHES)

        CFLAGS="$CFLAGS -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
        LDFLAGS="$LDFLAGS -Wl,-syslibroot,/Developer/SDKs/MacOSX10.4u.sdk"

        if test x"$UNIVERSAL_ARCHES" != x"" ; then
            # Forcibly disable dependency tracking for Universal builds, because -M
            # does not work with multiple -arch arguments on the gcc command-line.
            ac_tool_warned=yes
            cross_compiling=yes
            enable_dependency_tracking=no
            host="`echo $host | sed -e s/$host_cpu/universal/g`"
            host_cpu=universal

            AC_DEFINE([XINE_MACOSX_UNIVERSAL_BINARY], 1, [Define this if a universal binary is being built for Mac OS X])
            for arch in $UNIVERSAL_ARCHES ; do
                UNIVERSAL_CFLAGS="$UNIVERSAL_CFLAGS -arch $arch"
                UNIVERSAL_LDFLAGS="$UNIVERSAL_LDFLAGS -arch $arch"
            done
        fi
    fi
    AM_CONDITIONAL([MACOSX_UNIVERSAL_BINARY], [test x"$enable_macosx_universal" = x"yes"])
])dnl MACOSX_UNIVERSAL_BINARIES
