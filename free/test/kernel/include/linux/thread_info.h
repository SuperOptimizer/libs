/* SPDX-License-Identifier: GPL-2.0 */
/* Stub thread_info.h for free-cc kernel compilation testing */
#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <linux/types.h>

struct thread_info {
    unsigned long flags;
};

static inline unsigned long __must_check
raw_copy_from_user(void *to, const void *from, unsigned long n)
{
    (void)to; (void)from;
    return n;
}

static inline unsigned long __must_check
raw_copy_to_user(void *to, const void *from, unsigned long n)
{
    (void)to; (void)from;
    return n;
}

#define user_access_begin(ptr, len) 1
#define user_access_end() do { } while (0)
#define unsafe_get_user(x, ptr, label) do { (x) = *(ptr); } while (0)

#endif /* _LINUX_THREAD_INFO_H */
