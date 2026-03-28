/* SPDX-License-Identifier: GPL-2.0 */
/* Stub ioport.h for free-cc kernel compilation testing */
#ifndef _LINUX_IOPORT_H
#define _LINUX_IOPORT_H

#include <linux/types.h>

struct resource {
    resource_size_t start;
    resource_size_t end;
    const char *name;
    unsigned long flags;
    unsigned long desc;
    struct resource *parent, *sibling, *child;
};

#define IORESOURCE_BITS   0x000000ff
#define IORESOURCE_TYPE_BITS 0x00001f00
#define IORESOURCE_IO     0x00000100
#define IORESOURCE_MEM    0x00000200
#define IORESOURCE_REG    0x00000300
#define IORESOURCE_IRQ    0x00000400
#define IORESOURCE_DMA    0x00000800
#define IORESOURCE_BUS    0x00001000

#define IORESOURCE_PREFETCH  0x00002000
#define IORESOURCE_READONLY  0x00004000
#define IORESOURCE_CACHEABLE 0x00008000
#define IORESOURCE_SIZEALIGN 0x00040000
#define IORESOURCE_STARTALIGN 0x00080000
#define IORESOURCE_MEM_64    0x00100000
#define IORESOURCE_EXCLUSIVE 0x08000000
#define IORESOURCE_DISABLED  0x10000000
#define IORESOURCE_BUSY      0x80000000

static inline resource_size_t resource_size(const struct resource *res)
{
    return res->end - res->start + 1;
}

static inline unsigned long resource_type(const struct resource *res)
{
    return res->flags & IORESOURCE_TYPE_BITS;
}

extern struct resource *request_mem_region(resource_size_t start,
                                          resource_size_t n, const char *name);
extern void release_mem_region(resource_size_t start, resource_size_t n);

extern struct resource ioport_resource;
extern struct resource iomem_resource;

#endif /* _LINUX_IOPORT_H */
