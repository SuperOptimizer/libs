/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kasan-checks.h for free-cc kernel compilation testing */
#ifndef _LINUX_KASAN_CHECKS_H
#define _LINUX_KASAN_CHECKS_H

static inline void kasan_check_read(const volatile void *p, unsigned int size)
{
    (void)p; (void)size;
}

static inline void kasan_check_write(const volatile void *p, unsigned int size)
{
    (void)p; (void)size;
}

#endif /* _LINUX_KASAN_CHECKS_H */
