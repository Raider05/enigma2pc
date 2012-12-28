dnl Copyright 2007 xine project
dnl Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004, 2005, 2006, 2007
dnl Free Software Foundation, Inc.
dnl Originally by Gordon Matzigkeit <gord@gnu.ai.mit.edu>, 1996
dnl
dnl This file is free software; the Free Software Foundation gives
dnl unlimited permission to copy and/or distribute it, with or without
dnl modifications, as long as this notice is preserved.

dnl AC_PROG_AS
dnl ----------
dnl find the pathname to the GNU or non-GNU assembler
dnl based on AC_PROG_LD from libtool
AC_DEFUN([CC_PROG_AS], [
    AC_REQUIRE([LT_AC_PROG_SED])dnl
    AC_REQUIRE([AC_PROG_CC])dnl
    AC_REQUIRE([AM_PROG_AS])dnl
    AC_REQUIRE([AC_CANONICAL_HOST])dnl
    AC_REQUIRE([AC_CANONICAL_BUILD])dnl

    AC_ARG_WITH([gnu-as],
                [AS_HELP_STRING([--with-gnu-as], [assume the C compiler uses GNU as @<:@default=no@:>@])],
                [test "$withval" = no || with_gnu_as=yes], [with_gnu_as=unknown])
    if test x"$with_gnu_as" = x"unknown"; then
        dnl If CCAS is not the same as CC, check to see if it's GCC.
        if test x"$CCAS" = x"$CC"; then
            ccas_is_gnu="$GCC"
        else
            AC_CACHE_CHECK([whether $CCAS is a GNU compiler], [ac_cv_CCAS_compiler_gnu],
                           [saved_CC="$CC" CC="$CCAS"
                            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[#ifndef __GNUC__
                                                                        choke me
                                                                       #endif]])],
                                              [ac_cv_CCAS_compiler_gnu=yes], [ac_cv_CCAS_compiler_gnu=no])
                            CC="$saved_CC"])
            ccas_is_gnu="$ac_cv_CCAS_compiler_gnu"
        fi

        dnl Try to figure out the assembler command.  Fallback to as.
        ac_prog=""
        if test x"$AS" = x""; then
            AC_MSG_CHECKING([for as used by $CCAS])
            if test x"$ccas_is_gnu" = x"yes"; then
                # Check if gcc -print-prog-name=as gives a path.
                case "$host_or_hostalias" in
                    *-*-mingw*)
                        # gcc leaves a trailing carriage return which upsets mingw
                        ac_prog=`($CCAS -print-prog-name=as) 2>&5 | tr -d '\015'`
                        ;;
                    *)
                        ac_prog=`($CCAS -print-prog-name=as) 2>&5`
                        ;;
                esac

                case "$ac_prog" in
                    # Accept absolute paths.
                    [[\\/]]* | ?:[[\\/]]*)
                        re_direlt='/[[^/]][[^/]]*/\.\./'
                        # Canonicalize the pathname of as
                        ac_prog=`echo $ac_prog| $SED 's%\\\\%/%g'`
                        while echo $ac_prog | grep "$re_direlt" > /dev/null 2>&1; do
                            ac_prog=`echo $ac_prog| $SED "s%$re_direlt%/%"`
                        done
                        ;;
                    *) ac_prog="" ;;
                esac
            fi
        fi
        if test x"$ac_prog" = x""; then
            # If it fails, then pretend we aren't using GCC.
            lt_save_ifs="$IFS"; IFS=$PATH_SEPARATOR
            for ac_dir in $PATH; do
                IFS="$lt_save_ifs"
                test -z "$ac_dir" && ac_dir=.
                if test -f "$ac_dir/as" || test -f "$ac_dir/as$ac_exeext"; then
                    ac_prog="$ac_dir/as"
                    break
                fi
            done
            IFS="$lt_save_ifs"
        fi
        if test x"$ac_prog" = x""; then
            AC_MSG_RESULT([unknown])
        else
            AS="$ac_prog"
            AC_MSG_RESULT([$AS])
        fi
    fi

    test -z "$AS" && AC_MSG_ERROR([no acceptable as found in \$PATH])
    AC_CACHE_CHECK([if the assembler ($AS) is GNU as], [cc_cv_prog_gnu_as], [
        # I'd rather use --version here, but apparently some GNU as's only accept -v.
        case `"$AS" -v 2>&1 </dev/null` in
        *Apple*)
            # Apple's assembler reports itself as GNU as 1.38;
            # but it doesn't provide the functions we need.
            cc_cv_prog_gnu_as=no
            ;;
        *GNU* | *'with BFD'*)
            cc_cv_prog_gnu_as=yes
            ;;
        *)
            cc_cv_prog_gnu_as=no
            ;;
        esac])
    with_gnu_as="$cc_cv_prog_gnu_as"
])
