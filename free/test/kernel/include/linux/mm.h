/* SPDX-License-Identifier: GPL-2.0 */
/* Stub mm.h for free-cc kernel compilation testing */
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/types.h>
#include <linux/slab.h>

#define PAGE_SIZE   4096
#define PAGE_SHIFT  12
#define PAGE_MASK   (~(PAGE_SIZE - 1))

struct page;
struct vm_area_struct;
struct mm_struct;

static inline void *memdup_user_nul(const void *src, size_t len)
{
    (void)src;
    (void)len;
    return (void *)0;
}

#endif /* _LINUX_MM_H */
