dnl _ACX_VERSION_PARSE(version)
AC_DEFUN([_ACX_VERSION_PARSE], [`echo $1 | perl -e 'my $v = <>; chomp $v;
my @v = split(" ", $v); $v = $v[[@S|@#v]]; $v =~ s/[[^0-9.]].*$//; @v = split (/\./, $v);
push @v, 0 while $[#v] < 2; print $v[[0]] * 10000 + $v[[1]] * 100 + $v[[2]], "\n"'`])

dnl ACX_VERSION_CHECK(required, actual)
AC_DEFUN([ACX_VERSION_CHECK], [
    required_version=ifelse([$1], , [0.0.0], [$1])
    required_version_parsed=_ACX_VERSION_PARSE([$required_version])
    actual_version=ifelse([$2], , [0.0.0], [$2])
    actual_version_parsed=_ACX_VERSION_PARSE([$actual_version])
    if test $required_version_parsed -le $actual_version_parsed; then
        ifelse([$3], , [:], [$3])
    else
        ifelse([$4], , [:], [$4])
    fi
])

AC_DEFUN([ASX_TR_LOWER], [m4_translit([[$1]], [ABCDEFGHIJKLMNOPQRSTUVWXYZ], [abcdefghijklmnopqrstuvwxyz])])

dnl ACX_PACKAGE_CHECK(VARIABLE-PREFIX, MINIMUM-VERSION, CONFIG-SCRIPT,
dnl     [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
AC_DEFUN([ACX_PACKAGE_CHECK], [
    AC_ARG_VAR($1_CONFIG, [Full path to $3])
    AC_ARG_WITH(ASX_TR_LOWER([$1][-prefix]),
                [AS_HELP_STRING(ASX_TR_LOWER([--with-][$1][-prefix])[=PATH], [prefix where $3 is installed (optional)])],
                [package_config_prefix="$withval"], [package_config_prefix=""])
    AC_ARG_WITH(ASX_TR_LOWER([$1][-exec-prefix]),
                [AS_HELP_STRING(ASX_TR_LOWER([--with-][$1][-exec-prefix])[=PATH], [exec prefix where $3 is installed (optional)])],
                [package_config_exec_prefix="$withval"], [package_config_exec_prefix=""])
    package_config_args=""
    if test x"$package_config_exec_prefix" != x""; then
        package_config_args="$package_config_args --exec-prefix=$package_config_exec_prefix"
        test x"${$1_CONFIG+set}" != x"set" && $1_CONFIG="$package_config_exec_prefix/bin/$3"
    fi
    if test x"$package_config_prefix" != x""; then
        package_config_args="$package_config_args --prefix=$package_config_prefix"
        test x"${$1_CONFIG+set}" != x"set" && $1_CONFIG="$package_config_prefix/bin/$3"
    fi

    min_package_version=ifelse([$2], , [0.0.0], [$2])
    AC_PATH_TOOL([$1_CONFIG], [$3], [no])
    AC_MSG_CHECKING([for $1 version >= $min_package_version])
    if test x"$$1_CONFIG" = x"no"; then
        AC_MSG_RESULT([unknown])
        AC_MSG_NOTICE([The $3 script installed by $1 could not be found.
*** If $1 was installed in PREFIX, make sure PREFIX/bin is in your path, or
*** set the $1_CONFIG environment variable to the full path to the program.])
    else
        [$1][_CFLAGS]="`$$1_CONFIG $package_config_args --cflags`"
        [$1][_LIBS]="`$$1_CONFIG $package_config_args --libs`"
        [$1][_VERSION]="`$$1_CONFIG $package_config_args --version`"
        ACX_VERSION_CHECK([$min_package_version], [$[$1][_VERSION]],
                          [package_version_ok=yes; AC_MSG_RESULT([yes -- $[$1][_VERSION]])],
                          [[$1][_CFLAGS]="" [$1][_LIBS]=""; AC_MSG_RESULT([no -- $[$1][_VERSION]])
                           AC_MSG_NOTICE([If you have already installed a sufficiently new version,
*** this error probably means that the wrong copy of the $3
*** shell script is being found. The easiest way to fix this is to remove the
*** old version, but you can also set the $1_CONFIG environment
*** variable to point to the correct copy. In this case, you will have to
*** modify your LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf
*** so that the correct libraries are found at run-time.])])
    fi

    if test x"$package_version_ok" = x"yes"; then
        ifelse([$4], , [:], [$4])
    else
        ifelse([$5], , [:], [$5])
    fi
    AC_SUBST([$1][_CFLAGS])
    AC_SUBST([$1][_LIBS])
])
