/* SPDX-License-Identifier: GPL-2.0 */
/* Stub gfp.h for free-cc kernel compilation testing */
#ifndef _LINUX_GFP_H
#define _LINUX_GFP_H

#include <linux/types.h>

/* GFP flags already defined in slab.h, just re-export */
#ifndef GFP_KERNEL
#define GFP_KERNEL   0x0001
#define GFP_ATOMIC   0x0002
#define GFP_NOWAIT   0x0004
#define __GFP_ZERO   0x0100
#define __GFP_NOWARN 0x0200
#endif

extern unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order);
extern void free_pages(unsigned long addr, unsigned int order);
extern struct page *alloc_pages(gfp_t gfp_mask, unsigned int order);

#define __get_free_page(gfp) __get_free_pages((gfp), 0)
#define free_page(addr) free_pages((addr), 0)

static inline void *page_address(struct page *page)
{
    (void)page;
    return NULL;
}

#endif /* _LINUX_GFP_H */
