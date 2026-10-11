#ifndef TIMING_H
#define TIMING_H

#include <time.h>

/* Wall-clock seconds as a double (C11 timespec_get). */
static inline double now_seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#endif
