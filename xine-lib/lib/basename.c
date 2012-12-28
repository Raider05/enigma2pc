/*
 * get base name
 *
 * (adopted from sh-utils)
 */

#include "config.h"

#define FILESYSTEM_PREFIX_LEN(filename) 0
#define ISSLASH(C) ((C) == '/')

char *xine_private_basename(char *name) {
  char const *base = name + FILESYSTEM_PREFIX_LEN (name);
  char const *p;

  for (p = base; *p; p++) {
    if (ISSLASH (*p)) {
      /* Treat multiple adjacent slashes like a single slash.  */
      do p++;
      while (ISSLASH (*p));

      /* If the file name ends in slash, use the trailing slash as
         the basename if no non-slashes have been found.  */
      if (! *p) {
        if (ISSLASH (*base)) base = p - 1;
        break;
      }

      /* *P is a non-slash preceded by a slash.  */
      base = p;
    }
  }

  return (char *)base;
}
