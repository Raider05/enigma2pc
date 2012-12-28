dnl ---------------------------------------------
dnl Check for GNU getopt_long()
dnl ---------------------------------------------

AC_DEFUN([AC_GETOPT_LONG], [
  AC_MSG_CHECKING(for GNU getopt_long)
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

static struct option long_options[] = {
  {"help"    , no_argument, 0, 1 },
  {"version" , no_argument, 0, 2 },
  {0         , no_argument, 0, 0 }
};

int main (int argc, char **argv) {
  int option_index = 0;
  int c;
  opterr = 0;
  while ((c = getopt_long (argc, argv, "?hv",
			   long_options, &option_index)) != EOF)
    ;
  return 0;
}
	]])],
	[AC_MSG_RESULT(yes);
	 ac_getopt_long=yes;
	 AC_DEFINE(HAVE_GETOPT_LONG,,[Define this if you have GNU getopt_long() implemented])],
	[AC_MSG_RESULT(no); ac_getopt_long=no],
	[AC_MSG_RESULT(no); ac_getopt_long=no])
  AM_CONDITIONAL(HAVE_GETOPT_LONG, test x"$ac_getopt_long" = "xyes")
])
