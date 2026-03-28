#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
extern void __assert_fail(const char *expr, const char *file, int line);
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__))
#endif

/* C11 _Static_assert support */
#define static_assert _Static_assert

#endif
