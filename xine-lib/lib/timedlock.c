#include "config.h"

#include <errno.h>

#define _x_min(a, b) ((a) < (b) ? (a) : (b))

int xine_private_pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                         const struct timespec *abs_timeout)
{
    int             pthread_rc;
    struct timespec remaining, slept, ts;

    remaining = *abs_timeout;
    while ((pthread_rc = pthread_mutex_trylock(mutex)) == EBUSY) {
        ts.tv_sec  = 0;
        ts.tv_nsec = (remaining.tv_sec > 0 ? 10000000
                                           : _x_min(remaining.tv_nsec, 10000000));
        nanosleep(&ts, &slept);
        ts.tv_nsec -= slept.tv_nsec;
        if (ts.tv_nsec <= remaining.tv_nsec) {
            remaining.tv_nsec -= ts.tv_nsec;
        }
        else {
            remaining.tv_sec--;
            remaining.tv_nsec = (1000000 - (ts.tv_nsec - remaining.tv_nsec));
        }
        if (remaining.tv_sec < 0 || (!remaining.tv_sec && remaining.tv_nsec <= 0)) {
            return ETIMEDOUT;
        }
    }

    return pthread_rc;
}
