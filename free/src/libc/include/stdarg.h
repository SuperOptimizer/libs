#ifndef _STDARG_H
#define _STDARG_H

/*
 * stdarg.h - Variable argument list support.
 *
 * Dual-mode header:
 *   - When compiled by GCC/Clang: uses __builtin_va_* intrinsics.
 *   - When compiled by free-cc (__FREE_CC__ defined): uses the native
 *     aarch64 va_list struct and free-cc's own __builtin_va_* intrinsics.
 */

#ifdef __FREE_CC__

/*
 * free-cc native aarch64 va_list.
 * __builtin_va_list resolves to this struct in the compiler.
 */
typedef __builtin_va_list __gnuc_va_list;
typedef __builtin_va_list va_list;
#define _VA_LIST_DEFINED 1

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dest, src) \
    do { (dest) = (src); } while (0)

#else /* GCC / Clang bootstrap */

typedef __builtin_va_list __gnuc_va_list;
typedef __builtin_va_list va_list;
#define _VA_LIST_DEFINED 1

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

#endif /* __FREE_CC__ */

#endif /* _STDARG_H */
