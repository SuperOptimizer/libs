/* SPDX-License-Identifier: GPL-2.0 */
/* Stub sprintf.h for free-cc kernel compilation testing */
#ifndef _LINUX_SPRINTF_H
#define _LINUX_SPRINTF_H

#include <linux/types.h>

extern int sprintf(char *buf, const char *fmt, ...);
extern int snprintf(char *buf, size_t size, const char *fmt, ...);
extern int scnprintf(char *buf, size_t size, const char *fmt, ...);
extern int sscanf(const char *buf, const char *fmt, ...);

/* va_list versions */
struct __va_list_tag;
typedef struct __va_list_tag va_list_stub;

extern int vsprintf(char *buf, const char *fmt, ...);
extern int vsnprintf(char *buf, size_t size, const char *fmt, ...);
extern int vscnprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* _LINUX_SPRINTF_H */
