/* SPDX-License-Identifier: GPL-2.0 */
/* Stub vmalloc.h for free-cc kernel compilation testing */
#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/types.h>

static inline void *vmalloc(unsigned long size)
{
    (void)size;
    return (void *)0;
}

static inline void vfree(const void *addr)
{
    (void)addr;
}

static inline void kvfree(const void *addr)
{
    (void)addr;
}

#endif /* _LINUX_VMALLOC_H */
