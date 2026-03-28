/*
 * time.h - Date and time functions.
 * Pure C89.
 */

#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000L

struct tm {
    int tm_sec;    /* seconds [0, 60] */
    int tm_min;    /* minutes [0, 59] */
    int tm_hour;   /* hours [0, 23] */
    int tm_mday;   /* day of month [1, 31] */
    int tm_mon;    /* month [0, 11] */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* day of week [0, 6] (Sunday = 0) */
    int tm_yday;   /* day of year [0, 365] */
    int tm_isdst;  /* daylight saving flag */
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

/* Clock IDs for clock_gettime */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

time_t  time(time_t *t);
clock_t clock(void);
double  difftime(time_t t1, time_t t0);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
time_t  mktime(struct tm *tm);
size_t  strftime(char *s, size_t maxsize, const char *fmt,
                 const struct tm *tm);

#endif
