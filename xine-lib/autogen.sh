#!/bin/sh -e
#
# Copyright (C) 2000-2003 the xine project
#
# This file is part of xine, a unix video player.
# 
# xine is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# xine is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
#
# Maintained by Stephen Torri <storri@users.sourceforge.net>
#
# run this to generate all the initial makefiles, etc.

PROG=xine-lib

# Minimum value required to build
export WANT_AUTOMAKE_1_9=1
export WANT_AUTOMAKE=1.9
AUTOMAKE_MIN=1.9.0
AUTOCONF_MIN=2.59
LIBTOOL_MIN=1.5.20

# Check how echo works in this /bin/sh
case `echo -n` in
-n)     _echo_n=   _echo_c='\c';;
*)      _echo_n=-n _echo_c=;;
esac

srcdir="`dirname "$0"`"

detect_configure_ac() {

  test -z "$srcdir" && srcdir=.

  (test -f "$srcdir"/configure.ac) || {
    echo $_echo_n "*** Error ***: Directory "\`$srcdir\`" does not look like the"
    echo " top-level directory"
    exit 1
  }
}

parse_version_no() {
  # version no. is extended/truncated to three parts; only digits are handled
  perl -e 'my $v = <>;
	   chomp $v;
	   my @v = split (" ", $v);
	   $v = $v[$#v];
	   $v =~ s/[^0-9.].*$//;
	   @v = split (/\./, $v);
	   push @v, 0 while $#v < 2;
	   print $v[0] * 10000 + $v[1] * 100 + $v[2], "\n"'
}

#--------------------
# AUTOCONF
#-------------------
detect_autoconf() {
  set -- `type autoconf 2>/dev/null`
  RETVAL=$?
  NUM_RESULT=$#
  RESULT_FILE=$3
  if [ $RETVAL -eq 0 -a $NUM_RESULT -eq 3 -a -f "$RESULT_FILE" ]; then
    AC="`autoconf --version | parse_version_no`"
    if [ `expr $AC` -ge "`echo $AUTOCONF_MIN | parse_version_no`" ]; then
      autoconf_ok=yes
    fi
  else
    echo
    echo "**Error**: You must have \`autoconf' >= $AUTOCONF_MIN installed to" 
    echo "           compile $PROG. Download the appropriate package"
    echo "           for your distribution or source from ftp.gnu.org."
    exit 1
  fi
}

run_autoheader () {
  if test x"$autoconf_ok" != x"yes"; then
    echo
    echo "**Warning**: Version of autoconf is less than $AUTOCONF_MIN."
    echo "             Some warning message might occur from autoconf"
    echo
  fi

  echo $_echo_n " + Running autoheader: $_echo_c";
    autoheader;
  echo "done."
}

run_autoconf () {
  if test x"$autoconf_ok" != x"yes"; then
    echo
    echo "**Warning**: Version of autoconf is less than $AUTOCONF_MIN."
    echo "             Some warning message might occur from autoconf"
    echo
  fi

  echo $_echo_n " + Running autoconf: $_echo_c";
    autoconf;
    sed -i -e '/gnu_ld/,/;;/ s/--rpath \${wl}/--rpath,/' configure
  echo "done."
}

#--------------------
# LIBTOOL
#-------------------
try_libtool_executable() {
  libtool=$1
  set -- `type $libtool 2>/dev/null`
  RETVAL=$?
  NUM_RESULT=$#
  RESULT_FILE=$3
  if [ $RETVAL -eq 0 -a $NUM_RESULT -eq 3 -a -f "$RESULT_FILE" ]; then
    LT="`$libtool --version | awk '{ print $4 }' | parse_version_no`"
    if [ `expr $LT` -ge "`echo $LIBTOOL_MIN | parse_version_no`" ]; then
      libtool_ok=yes
    fi
  fi
}

detect_libtool() {
  # try glibtool first, then libtool
  try_libtool_executable 'glibtool'
  if [ "x$libtool_ok" != "xyes" ]; then
    try_libtool_executable 'libtool'
    if [ "x$libtool_ok" != "xyes" ]; then
      echo
      echo "**Error**: You must have \`libtool' >= $LIBTOOL_MIN installed to" 
      echo "           compile $PROG. Download the appropriate package"
      echo "           for your distribution or source from ftp.gnu.org."
      exit 1
    fi
  fi
}

run_libtoolize() {
  if test x"$libtool_ok" != x"yes"; then
    echo
    echo "**Warning**: Version of libtool is less than $LIBTOOL_MIN."
    echo "             Some warning message might occur from libtool"
    echo
  fi

  echo $_echo_n " + Running libtoolize: $_echo_c";
    "${libtool}ize" --force --copy >/dev/null 2>&1;
  echo "done."
}

#--------------------
# AUTOMAKE
#--------------------
detect_automake() {
  #
  # expected output from 'type' is
  #   automake is /usr/local/bin/automake
  #
  set -- `type automake 2>/dev/null`
  RETVAL=$?
  NUM_RESULT=$#
  RESULT_FILE=$3
  if [ $RETVAL -eq 0 -a $NUM_RESULT -eq 3 -a -f "$RESULT_FILE" ]; then
    AM="`automake --version | parse_version_no`"
    if [ `expr $AM` -ge "`echo $AUTOMAKE_MIN | parse_version_no`" ]; then
      automake_ok=yes
    fi
  else
    echo
    echo "**Error**: You must have \`automake' >= $AUTOMAKE_MIN installed to" 
    echo "           compile $PROG. Download the appropriate package"
    echo "           for your distribution or source from ftp.gnu.org."
    exit 1
  fi
}

run_automake () {
  if test x"$automake_ok" != x"yes"; then
    echo
    echo "**Warning**: Version of automake is less than $AUTOMAKE_MIN."
    echo "             Some warning message might occur from automake"
    echo
  fi

  echo $_echo_n " + Running automake: $_echo_c";

  automake --gnu --add-missing --copy -Wno-portability;
  echo "done."
}

#--------------------
# ACLOCAL
#-------------------
detect_aclocal() {

  # if no automake, don't bother testing for aclocal
  set -- `type aclocal 2>/dev/null`
  RETVAL=$?
  NUM_RESULT=$#
  RESULT_FILE=$3
  if [ $RETVAL -eq 0 -a $NUM_RESULT -eq 3 -a -f "$RESULT_FILE" ]; then
    AC="`aclocal --version | parse_version_no`"
    if [ `expr $AC` -ge "`echo $AUTOMAKE_MIN | parse_version_no`" ]; then
      aclocal_ok=yes
    fi
  else
    echo
    echo "**Error**: You must have \`aclocal' >= $AUTOMAKE_MIN installed to" 
    echo "           compile $PROG. Download the appropriate package"
    echo "           for your distribution or source from ftp.gnu.org."
    exit 1
  fi
}

run_aclocal () {

  if test x"$aclocal_ok" != x"yes"; then
    echo
    echo "**Warning**: Version of aclocal is less than $AUTOMAKE_MIN."
    echo "             Some warning message might occur from aclocal"
    echo
  fi
  
  echo $_echo_n " + Running autopoint: $_echo_c"
  
  autopoint
  echo "done." 

  echo $_echo_n " + Running aclocal: $_echo_c"

  aclocal -I m4
  echo "done." 
}

#--------------------
# CONFIGURE
#-------------------
run_configure () {
  rm -f config.cache
  echo " + Running 'configure $@':"
  if [ -z "$*" ]; then
    echo "   ** If you wish to pass arguments to ./configure, please"
    echo "   ** specify them on the command line."
  fi
  if test -f configure; then
    ./configure "$@"
  else
    "$srcdir"/configure "$@"
  fi
}


#---------------
# MAIN
#---------------
detect_configure_ac
cd "$srcdir"
detect_autoconf
detect_libtool
detect_automake
detect_aclocal


#   help: print out usage message
#   *) run aclocal, autoheader, automake, autoconf, configure
case "$1" in
  aclocal)
    run_aclocal
    ;;
  autoheader)
    run_autoheader
    ;;
  automake)
    run_aclocal
    run_automake
    ;;
  autoconf)
    run_aclocal
    run_autoconf
    ;;
  libtoolize)
    run_libtoolize
    ;;
  noconfig)
    run_libtoolize
    run_aclocal
    run_autoheader
    run_automake
    run_autoconf
    ;;
  *)
    run_libtoolize
    run_aclocal
    run_autoheader
    run_automake
    run_autoconf
    # return to our original directory
    cd - >/dev/null
    run_configure "$@"
    ;;
esac
