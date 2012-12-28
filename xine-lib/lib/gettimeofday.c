/* replacement function of gettimeofday */

#include "config.h"

#include <sys/timeb.h>
#ifdef WIN32
#include <winsock.h>
#else
#include <sys/time.h>
#endif

int xine_private_gettimeofday(struct timeval *tv) {
  struct timeb tp;

  ftime(&tp);
  tv->tv_sec = tp.time;
  tv->tv_usec = tp.millitm * 1000;

  return 0;
}
