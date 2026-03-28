/* SPDX-License-Identifier: GPL-2.0 */
/* Stub stdarg.h for free-cc kernel compilation testing */
#ifndef _LINUX_STDARG_H
#define _LINUX_STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

#endif /* _LINUX_STDARG_H */
