/*
 * cx_log.h - Logging framework with levels and timestamps.
 * Part of libcx. Pure C89.
 */

#ifndef CX_LOG_H
#define CX_LOG_H

#include <stdio.h>

#define CX_LOG_DEBUG 0
#define CX_LOG_INFO  1
#define CX_LOG_WARN  2
#define CX_LOG_ERROR 3

void cx_log(int level, const char *file, int line, const char *fmt, ...);
void cx_log_set_level(int level);
void cx_log_set_file(FILE *fp);

/* Convenience macros use the function directly since C89 lacks variadic macros.
 * Usage: cx_log(CX_LOG_INFO, __FILE__, __LINE__, "msg %d", val);
 */

#endif
