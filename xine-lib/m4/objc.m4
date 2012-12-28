# Extracted from autoconf 2.61 to obtain Objective C macros.
# Only expand if the version of autoconf in use doesn't have the macro itself.
m4_ifdef([AC_PROG_OBJC], [], [

# This file is part of Autoconf.                        -*- Autoconf -*-
# Programming languages support.
# Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006 Free Software
# Foundation, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#
# As a special exception, the Free Software Foundation gives unlimited
# permission to copy, distribute and modify the configure scripts that
# are the output of Autoconf.  You need not follow the terms of the GNU
# General Public License when using or distributing such scripts, even
# though portions of the text of Autoconf appear in them.  The GNU
# General Public License (GPL) does govern all other use of the material
# that constitutes the Autoconf program.
#
# Certain portions of the Autoconf source text are designed to be copied
# (in certain cases, depending on the input) into the output of
# Autoconf.  We call these the "data" portions.  The rest of the Autoconf
# source text consists of comments plus executable code that decides which
# of the data portions to output in any given case.  We call these
# comments and executable code the "non-data" portions.  Autoconf never
# copies any of the non-data portions into its output.
#
# This special exception to the GPL applies to versions of Autoconf
# released by the Free Software Foundation.  When you make and
# distribute a modified version of Autoconf, you may extend this special
# exception to the GPL to apply to your modified version as well, *unless*
# your modified version has the potential to copy into its output some
# of the text that was the non-data portion of the version that you started
# with.  (In other words, unless your change moves or copies text from
# the non-data portions to the data portions.)  If your modification has
# such potential, you must delete any notice of this special exception
# to the GPL from your modified version.
#
# Written by David MacKenzie, with help from
# Akim Demaille, Paul Eggert,
# Franc,ois Pinard, Karl Berry, Richard Pixley, Ian Lance Taylor,
# Roland McGrath, Noah Friedman, david d zuhn, and many others.

# ------------------------------ #
# 1d. The Objective C language.  #
# ------------------------------ #


# AC_LANG(Objective C)
# --------------------
m4_define([AC_LANG(Objective C)],
[ac_ext=m
ac_cpp='$OBJCPP $CPPFLAGS'
ac_compile='$OBJC -c $OBJCFLAGS $CPPFLAGS conftest.$ac_ext >&AS_MESSAGE_LOG_FD'
ac_link='$OBJC -o conftest$ac_exeext $OBJCFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext $LIBS >&AS_MESSAGE_LOG_FD'
ac_compiler_gnu=$ac_cv_objc_compiler_gnu
])


# AC_LANG_OBJC
# ------------
AU_DEFUN([AC_LANG_OBJC], [AC_LANG(Objective C)])


# _AC_LANG_ABBREV(Objective C)
# ----------------------------
m4_define([_AC_LANG_ABBREV(Objective C)], [objc])


# _AC_LANG_PREFIX(Objective C)
# ----------------------------
m4_define([_AC_LANG_PREFIX(Objective C)], [OBJC])


# ------------------------- #
# 2d. Objective C sources.  #
# ------------------------- #

# AC_LANG_SOURCE(Objective C)(BODY)
# ---------------------------------
m4_copy([AC_LANG_SOURCE(C)], [AC_LANG_SOURCE(Objective C)])


# AC_LANG_PROGRAM(Objective C)([PROLOGUE], [BODY])
# ------------------------------------------------
m4_copy([AC_LANG_PROGRAM(C)], [AC_LANG_PROGRAM(Objective C)])


# AC_LANG_CALL(Objective C)(PROLOGUE, FUNCTION)
# ---------------------------------------------
m4_copy([AC_LANG_CALL(C)], [AC_LANG_CALL(Objective C)])


# AC_LANG_FUNC_LINK_TRY(Objective C)(FUNCTION)
# --------------------------------------------
m4_copy([AC_LANG_FUNC_LINK_TRY(C)], [AC_LANG_FUNC_LINK_TRY(Objective C)])


# AC_LANG_BOOL_COMPILE_TRY(Objective C)(PROLOGUE, EXPRESSION)
# -----------------------------------------------------------
m4_copy([AC_LANG_BOOL_COMPILE_TRY(C)], [AC_LANG_BOOL_COMPILE_TRY(Objective C)])


# AC_LANG_INT_SAVE(Objective C)(PROLOGUE, EXPRESSION)
# ---------------------------------------------------
m4_copy([AC_LANG_INT_SAVE(C)], [AC_LANG_INT_SAVE(Objective C)])


# ------------------------------ #
# 3d. The Objective C compiler.  #
# ------------------------------ #

# AC_LANG_PREPROC(Objective C)
# ----------------------------
# Find the Objective C preprocessor.  Must be AC_DEFUN'd to be AC_REQUIRE'able.
m4_defun([AC_LANG_PREPROC(Objective C)],
[AC_REQUIRE([AC_PROG_OBJCPP])])


# AC_PROG_OBJCPP
# --------------
# Find a working Objective C preprocessor.
AC_DEFUN([AC_PROG_OBJCPP],
[AC_REQUIRE([AC_PROG_OBJC])dnl
AC_ARG_VAR([OBJCPP],   [Objective C preprocessor])dnl
_AC_ARG_VAR_CPPFLAGS()dnl
AC_LANG_PUSH(Objective C)dnl
AC_MSG_CHECKING([how to run the Objective C preprocessor])
if test -z "$OBJCPP"; then
  AC_CACHE_VAL(ac_cv_prog_OBJCPP,
  [dnl
    # Double quotes because OBJCPP needs to be expanded
    for OBJCPP in "$OBJC -E" "/lib/cpp"
    do
      _AC_PROG_PREPROC_WORKS_IFELSE([break])
    done
    ac_cv_prog_OBJCPP=$OBJCPP
  ])dnl
  OBJCPP=$ac_cv_prog_OBJCPP
else
  ac_cv_prog_OBJCPP=$OBJCPP
fi
AC_MSG_RESULT([$OBJCPP])
_AC_PROG_PREPROC_WORKS_IFELSE([],
          [AC_MSG_FAILURE([Objective C preprocessor "$OBJCPP" fails sanity check])])
AC_SUBST(OBJCPP)dnl
AC_LANG_POP(Objective C)dnl
])# AC_PROG_OBJCPP


# AC_LANG_COMPILER(Objective C)
# -----------------------------
# Find the Objective C compiler.  Must be AC_DEFUN'd to be AC_REQUIRE'able.
m4_defun([AC_LANG_COMPILER(Objective C)],
[AC_REQUIRE([AC_PROG_OBJC])])



# AC_PROG_OBJC([LIST-OF-COMPILERS])
# ---------------------------------
# LIST-OF-COMPILERS is a space separated list of Objective C compilers to
# search for (if not specified, a default list is used).  This just gives
# the user an opportunity to specify an alternative search list for the
# Objective C compiler.
# objcc StepStone Objective-C compiler (also "standard" name for OBJC)
# objc  David Stes' POC.  If you installed this, you likely want it.
# cc    Native C compiler (for instance, Apple).
# CC    You never know.
AN_MAKEVAR([OBJC],  [AC_PROG_OBJC])
AN_PROGRAM([objcc],  [AC_PROG_OBJC])
AN_PROGRAM([objc],  [AC_PROG_OBJC])
AC_DEFUN([AC_PROG_OBJC],
[AC_LANG_PUSH(Objective C)dnl
AC_ARG_VAR([OBJC],      [Objective C compiler command])dnl
AC_ARG_VAR([OBJCFLAGS], [Objective C compiler flags])dnl
_AC_ARG_VAR_LDFLAGS()dnl
dnl _AC_ARG_VAR_LIBS()dnl
_AC_ARG_VAR_CPPFLAGS()dnl
_AC_ARG_VAR_PRECIOUS([OBJC])dnl
AC_CHECK_TOOLS(OBJC,
               [m4_default([$1], [gcc objcc objc cc CC])],
               gcc)
# Provide some information about the compiler.
echo "$as_me:$LINENO:" \
     "checking for _AC_LANG compiler version" >&AS_MESSAGE_LOG_FD
ac_compiler=`set X $ac_compile; echo $[2]`
_AC_EVAL([$ac_compiler --version </dev/null >&AS_MESSAGE_LOG_FD])
_AC_EVAL([$ac_compiler -v </dev/null >&AS_MESSAGE_LOG_FD])
_AC_EVAL([$ac_compiler -V </dev/null >&AS_MESSAGE_LOG_FD])

m4_expand_once([_AC_COMPILER_EXEEXT])[]dnl
m4_expand_once([_AC_COMPILER_OBJEXT])[]dnl
_AC_LANG_COMPILER_GNU
GOBJC=`test $ac_compiler_gnu = yes && echo yes`
_AC_PROG_OBJC_G
AC_LANG_POP(Objective C)dnl
])# AC_PROG_OBJC


# _AC_PROG_OBJC_G
# ---------------
# Check whether -g works, even if OBJCFLAGS is set, in case the package
# plays around with OBJCFLAGS (such as to build both debugging and
# normal versions of a library), tasteless as that idea is.
# Don't consider -g to work if it generates warnings when plain compiles don't.
m4_define([_AC_PROG_OBJC_G],
[ac_test_OBJCFLAGS=${OBJCFLAGS+set}
ac_save_OBJCFLAGS=$OBJCFLAGS
AC_CACHE_CHECK(whether $OBJC accepts -g, ac_cv_prog_objc_g,
  [ac_save_objc_werror_flag=$ac_objc_werror_flag
   ac_objc_werror_flag=yes
   ac_cv_prog_objc_g=no
   OBJCFLAGS="-g"
   _AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
     [ac_cv_prog_objc_g=yes],
     [OBJCFLAGS=""
      _AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
        [],
        [ac_objc_werror_flag=$ac_save_objc_werror_flag
         OBJCFLAGS="-g"
         _AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
           [ac_cv_prog_objc_g=yes])])])
   ac_objc_werror_flag=$ac_save_objc_werror_flag])
if test "$ac_test_OBJCFLAGS" = set; then
  OBJCFLAGS=$ac_save_OBJCFLAGS
elif test $ac_cv_prog_objc_g = yes; then
  if test "$GOBJC" = yes; then
    OBJCFLAGS="-g -O2"
  else
    OBJCFLAGS="-g"
  fi
else
  if test "$GOBJC" = yes; then
    OBJCFLAGS="-O2"
  else
    OBJCFLAGS=
  fi
fi[]dnl
])# _AC_PROG_OBJC_G
])dnl m4_ifdef([AC_PROG_OBJC])
