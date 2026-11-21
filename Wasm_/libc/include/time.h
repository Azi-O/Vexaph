#ifndef _VX_TIME_H
#define _VX_TIME_H
typedef long time_t;
typedef int clockid_t;
struct timespec { time_t tv_sec; long tv_nsec; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(clockid_t, struct timespec *);
time_t time(time_t *);
#endif
