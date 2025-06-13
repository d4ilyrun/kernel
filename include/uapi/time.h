#ifndef UAPI_TIME_H
#define UAPI_TIME_H

#include <uapi/types.h>

/* The number of clock ticks in a second (1KHz). */
#define CLOCK_PER_SECOND 1000

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#endif /* UAPI_TIME_H */
