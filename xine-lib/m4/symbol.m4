dnl Macros to check the presence of generic (non-typed) symbols.
dnl Copyright (c) 2007 xine project
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2, or (at your option)
dnl any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
dnl 02110-1301, USA.
dnl
dnl As a special exception, the xine project, as copyright owner of the
dnl macro gives unlimited permission to copy, distribute and modify the
dnl configure scripts that are the output of Autoconf when processing the
dnl Macro. You need not follow the terms of the GNU General Public
dnl License when using or distributing such scripts, even though portions
dnl of the text of the Macro appear in them. The GNU General Public
dnl License (GPL) does govern all other use of the material that
dnl constitutes the Autoconf Macro.
dnl
dnl This special exception to the GPL applies to versions of the
dnl Autoconf Macro released by the xine project. When you make and
dnl distribute a modified version of the Autoconf Macro, you may extend
dnl this special exception to the GPL to apply to your modified version as
dnl well.

dnl AC_CHECK_SYMBOL - Check for a single symbol
dnl Usage: AC_CHECK_SYMBOL([symbol], [action-if-found], [action-if-not-found])
dnl Default action, defines HAVE_SYMBOL (with symbol capitalised) if the
dnl symbol is present at link time.
AC_DEFUN([AC_CHECK_SYMBOL], [
  AC_CACHE_CHECK([for $1 symbol presence],
    AS_TR_SH([ac_cv_symbol_$1]),
    [AC_TRY_LINK([extern void *$1;], [void *tmp = $1;],
       [eval "AS_TR_SH([ac_cv_symbol_$1])=yes"],
       [eval "AS_TR_SH([ac_cv_symbol_$1])=no"])
    ])

  if eval test [x$]AS_TR_SH([ac_cv_symbol_$1]) = xyes; then
    ifelse([$2], , [AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_$1]), [1],
      [Define to 1 if you have the $1 symbol.])], [$2])
  else
    ifelse([$3], , [:], [$3])
  fi
])

dnl AC_CHECK_SYMBOLS - Check for multiple symbols
dnl Usage: AC_CHECK_SYMBOLS([symbol1 symbol2], [action-if-found], [action-if-not-found])
AC_DEFUN([AC_CHECK_SYMBOLS], [
  AH_CHECK_SYMBOLS([$1])

  for ac_symbol in $1
  do
    AC_CHECK_SYMBOL($ac_symbol, [AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_$ac_symbol])) $2], [$3])
  done
])

m4_ifdef([m4_foreach_w], [], [
  # m4_foreach_w(VARIABLE, LIST, EXPRESSION)
  # ----------------------------------------
  #
  # Like m4_foreach, but the list is whitespace separated.
  #
  # This macro is robust to active symbols:
  #    m4_foreach_w([Var], [ active
  #    b	act\
  #    ive  ], [-Var-])end
  #    => -active--b--active-end
  #
  m4_define([m4_foreach_w],
  [m4_foreach([$1], m4_split(m4_normalize([$2])), [$3])])
  m4_define([m4_foreach_w_is_compatibility])
])

m4_define([AH_CHECK_SYMBOLS], [
  m4_foreach_w([AC_Symbol], [$1],
   [AH_TEMPLATE(AS_TR_CPP([HAVE_]m4_defn([AC_Symbol])),
      [Define to 1 if you have the ]m4_defn([AC_Symbol])[ symbol.])])
])
