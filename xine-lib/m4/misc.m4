dnl Miscellaneous M4 macros for configure
dnl Copyright (c) 2008 Diego Pettenò <flameeyes@gmail.com>
dnl Copyright (c) 2008 xine project
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
dnl As a special exception, the copyright owners of the
dnl macro gives unlimited permission to copy, distribute and modify the
dnl configure scripts that are the output of Autoconf when processing the
dnl Macro. You need not follow the terms of the GNU General Public
dnl License when using or distributing such scripts, even though portions
dnl of the text of the Macro appear in them. The GNU General Public
dnl License (GPL) does govern all other use of the material that
dnl constitutes the Autoconf Macro.
dnl
dnl This special exception to the GPL applies to versions of the
dnl Autoconf Macro released by this project. When you make and
dnl distribute a modified version of the Autoconf Macro, you may extend
dnl this special exception to the GPL to apply to your modified version as
dnl well.

AC_DEFUN([XINE_CHECK_MINMAX], [
  AC_CHECK_HEADERS([sys/param.h])
  AC_CACHE_CHECK([for MIN()/MAX() macros],
    xine_cv_minmax,
    [
     AC_LINK_IFELSE([
        AC_LANG_PROGRAM([
	  #ifdef HAVE_SYS_PARAM_H
          # include <sys/param.h>
          #endif
        ], [
          int a = MIN(1, 3);
          int b = MAX(2, 3);
	])],
        [xine_cv_minmax=yes],
        [xine_cv_minmax=no])
    ])

  AS_IF([test x$xine_cv_minmax = xyes],
    [$1], [$2])
])
