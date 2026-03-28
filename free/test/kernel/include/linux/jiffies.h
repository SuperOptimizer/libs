/* SPDX-License-Identifier: GPL-2.0 */
/* Stub jiffies.h for free-cc kernel compilation testing */
#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <linux/types.h>

#define HZ 100

extern unsigned long volatile jiffies;

#define time_after(a,b)     ((long)((b) - (a)) < 0)
#define time_before(a,b)    time_after(b,a)
#define time_after_eq(a,b)  ((long)((a) - (b)) >= 0)
#define time_before_eq(a,b) time_after_eq(b,a)

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
    return (m + (1000 / HZ - 1)) / (1000 / HZ);
}

#endif /* _LINUX_JIFFIES_H */
