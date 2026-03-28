/*
 * time.c - Date and time functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <time.h>
#include <stddef.h>
#include <string.h>

/* syscall interface (from syscall.S) */
long __syscall(long num, long a1, long a2, long a3,
               long a4, long a5, long a6);

#define SYS_CLOCK_GETTIME 113

/* static struct tm for localtime/gmtime (not thread-safe, per C89) */
static struct tm _tm_buf;

/* days in each month (non-leap year) */
static const int _days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int _is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int _days_in_year(int year)
{
    return _is_leap_year(year) ? 366 : 365;
}

static int _month_days(int mon, int year)
{
    if (mon == 1 && _is_leap_year(year)) {
        return 29;
    }
    return _days_in_month[mon];
}

/* ------------------------------------------------------------------ */
/* time - get current calendar time                                    */
/* ------------------------------------------------------------------ */

time_t time(time_t *t)
{
    struct timespec ts;
    long ret;

    ret = __syscall(SYS_CLOCK_GETTIME, (long)CLOCK_REALTIME,
                    (long)&ts, 0, 0, 0, 0);
    if (ret < 0) {
        return (time_t)-1;
    }
    if (t != NULL) {
        *t = ts.tv_sec;
    }
    return ts.tv_sec;
}

/* ------------------------------------------------------------------ */
/* clock - processor time used                                         */
/* ------------------------------------------------------------------ */

clock_t clock(void)
{
    struct timespec ts;
    long ret;

    ret = __syscall(SYS_CLOCK_GETTIME, (long)CLOCK_MONOTONIC,
                    (long)&ts, 0, 0, 0, 0);
    if (ret < 0) {
        return (clock_t)-1;
    }
    return (clock_t)(ts.tv_sec * CLOCKS_PER_SEC +
                     ts.tv_nsec / 1000);
}

/* ------------------------------------------------------------------ */
/* difftime - difference between two times                             */
/* ------------------------------------------------------------------ */

double difftime(time_t t1, time_t t0)
{
    return (double)(t1 - t0);
}

/* ------------------------------------------------------------------ */
/* gmtime - convert time_t to broken-down UTC time                     */
/* ------------------------------------------------------------------ */

struct tm *gmtime(const time_t *t)
{
    time_t rem;
    int year;
    int mon;
    int days;
    int dy;

    if (t == NULL) {
        return NULL;
    }

    rem = *t;

    /* seconds, minutes, hours */
    _tm_buf.tm_sec = (int)(rem % 60);
    rem /= 60;
    _tm_buf.tm_min = (int)(rem % 60);
    rem /= 60;
    _tm_buf.tm_hour = (int)(rem % 24);
    rem /= 24;

    /* rem is now days since epoch (1970-01-01, which was a Thursday) */
    days = (int)rem;
    _tm_buf.tm_wday = (days + 4) % 7; /* Thursday = 4 */
    if (_tm_buf.tm_wday < 0) {
        _tm_buf.tm_wday += 7;
    }

    /* find year */
    year = 1970;
    if (days >= 0) {
        while (days >= _days_in_year(year)) {
            days -= _days_in_year(year);
            year++;
        }
    } else {
        while (days < 0) {
            year--;
            days += _days_in_year(year);
        }
    }
    _tm_buf.tm_year = year - 1900;
    _tm_buf.tm_yday = days;

    /* find month and day */
    mon = 0;
    while (mon < 11) {
        dy = _month_days(mon, year);
        if (days < dy) {
            break;
        }
        days -= dy;
        mon++;
    }
    _tm_buf.tm_mon = mon;
    _tm_buf.tm_mday = days + 1;
    _tm_buf.tm_isdst = 0;

    return &_tm_buf;
}

/* ------------------------------------------------------------------ */
/* localtime - same as gmtime (no timezone support)                    */
/* ------------------------------------------------------------------ */

struct tm *localtime(const time_t *t)
{
    /* no timezone support - treat as UTC */
    return gmtime(t);
}

/* ------------------------------------------------------------------ */
/* mktime - convert broken-down time to time_t                         */
/* ------------------------------------------------------------------ */

time_t mktime(struct tm *tm)
{
    time_t result;
    int year;
    int mon;
    int i;

    if (tm == NULL) {
        return (time_t)-1;
    }

    /* normalize month */
    while (tm->tm_mon < 0) {
        tm->tm_mon += 12;
        tm->tm_year--;
    }
    while (tm->tm_mon >= 12) {
        tm->tm_mon -= 12;
        tm->tm_year++;
    }

    year = tm->tm_year + 1900;
    mon = tm->tm_mon;

    /* days from epoch to start of year */
    result = 0;
    if (year > 1970) {
        for (i = 1970; i < year; i++) {
            result += _days_in_year(i);
        }
    } else {
        for (i = 1969; i >= year; i--) {
            result -= _days_in_year(i);
        }
    }

    /* add days for months */
    for (i = 0; i < mon; i++) {
        result += _month_days(i, year);
    }

    /* add day of month (1-based) */
    result += tm->tm_mday - 1;

    /* convert to seconds and add time */
    result = result * 86400 + tm->tm_hour * 3600 +
             tm->tm_min * 60 + tm->tm_sec;

    /* fill in computed fields */
    gmtime(&result);
    tm->tm_wday = _tm_buf.tm_wday;
    tm->tm_yday = _tm_buf.tm_yday;
    tm->tm_isdst = 0;

    return result;
}

/* ------------------------------------------------------------------ */
/* strftime - format date/time string                                  */
/* ------------------------------------------------------------------ */

static void _append_str(char *s, size_t max, size_t *pos, const char *src)
{
    while (*src != '\0' && *pos + 1 < max) {
        s[*pos] = *src;
        (*pos)++;
        src++;
    }
}

static void _append_int(char *s, size_t max, size_t *pos,
                         int val, int width)
{
    char buf[16];
    int len;
    int i;
    int v;

    /* render digits in reverse */
    v = val < 0 ? -val : val;
    len = 0;
    do {
        buf[len++] = '0' + (v % 10);
        v /= 10;
    } while (v > 0);

    /* zero-pad */
    while (len < width) {
        buf[len++] = '0';
    }

    /* output in correct order */
    for (i = len - 1; i >= 0; i--) {
        if (*pos + 1 < max) {
            s[*pos] = buf[i];
            (*pos)++;
        }
    }
}

static const char *_wday_short[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *_mon_short[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

size_t strftime(char *s, size_t maxsize, const char *fmt,
                const struct tm *tm)
{
    size_t pos;

    if (maxsize == 0) {
        return 0;
    }

    pos = 0;
    while (*fmt != '\0' && pos + 1 < maxsize) {
        if (*fmt != '%') {
            s[pos++] = *fmt++;
            continue;
        }
        fmt++; /* skip '%' */

        switch (*fmt) {
        case 'Y': /* 4-digit year */
            _append_int(s, maxsize, &pos, tm->tm_year + 1900, 4);
            break;
        case 'm': /* month 01-12 */
            _append_int(s, maxsize, &pos, tm->tm_mon + 1, 2);
            break;
        case 'd': /* day 01-31 */
            _append_int(s, maxsize, &pos, tm->tm_mday, 2);
            break;
        case 'H': /* hour 00-23 */
            _append_int(s, maxsize, &pos, tm->tm_hour, 2);
            break;
        case 'M': /* minute 00-59 */
            _append_int(s, maxsize, &pos, tm->tm_min, 2);
            break;
        case 'S': /* second 00-60 */
            _append_int(s, maxsize, &pos, tm->tm_sec, 2);
            break;
        case 'a': /* abbreviated weekday */
            if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                _append_str(s, maxsize, &pos, _wday_short[tm->tm_wday]);
            }
            break;
        case 'b': /* abbreviated month */
            if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                _append_str(s, maxsize, &pos, _mon_short[tm->tm_mon]);
            }
            break;
        case 'j': /* day of year 001-366 */
            _append_int(s, maxsize, &pos, tm->tm_yday + 1, 3);
            break;
        case '%':
            s[pos++] = '%';
            break;
        case '\0':
            /* format string ends with lone '%' */
            s[pos] = '\0';
            return pos;
        default:
            /* unknown specifier: output as-is */
            s[pos++] = '%';
            if (pos + 1 < maxsize) {
                s[pos++] = *fmt;
            }
            break;
        }
        fmt++;
    }

    s[pos] = '\0';
    return pos;
}
