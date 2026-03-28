/*
 * log.c - Logging framework implementation.
 * Part of libcx. Pure C89.
 */

#include "cx_log.h"
#include <stdarg.h>
#include <time.h>

static int cx_log_level = CX_LOG_INFO;
static FILE *cx_log_fp = NULL;

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void cx_log_set_level(int level)
{
    cx_log_level = level;
}

void cx_log_set_file(FILE *fp)
{
    cx_log_fp = fp;
}

void cx_log(int level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    FILE *out;
    time_t t;
    struct tm *tm_info;
    char timebuf[32];

    if (level < cx_log_level) return;

    out = cx_log_fp ? cx_log_fp : stderr;

    /* Timestamp */
    t = time(NULL);
    tm_info = localtime(&t);
    if (tm_info) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        timebuf[0] = '\0';
    }

    /* Header */
    if (level <= CX_LOG_DEBUG) {
        fprintf(out, "%s [%s] %s:%d: ", timebuf, level_names[level],
                file, line);
    } else {
        fprintf(out, "%s [%s] ", timebuf,
                (level >= 0 && level <= 3) ? level_names[level] : "???");
    }

    /* Message */
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fprintf(out, "\n");
    fflush(out);
}
