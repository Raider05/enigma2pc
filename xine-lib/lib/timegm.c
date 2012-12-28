#include "config.h"

#include <time.h>
#include <stdlib.h>

time_t xine_private_timegm(struct tm *tm) {
  time_t ret;
#if defined(HAVE_TZSET)
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
#endif
  ret = mktime(tm);
#if defined(HAVE_TZSET)
  if (tz) setenv("TZ", tz, 1);
  else unsetenv("TZ");
  tzset();
#endif

  return ret;
}
