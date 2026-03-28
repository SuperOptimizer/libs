/* SPDX-License-Identifier: GPL-2.0 */
/* Stub logic_iomem.h for free-cc kernel compilation testing */
#ifndef _LINUX_LOGIC_IOMEM_H
#define _LINUX_LOGIC_IOMEM_H

#include <linux/types.h>

struct resource;

struct logic_iomem_region_ops {
    void (*unmap)(void *priv);
    unsigned long (*read)(void *priv, unsigned int offset, int size);
    void (*write)(void *priv, unsigned int offset, int size,
                  unsigned long val);
    void (*read_bulk)(void *priv, unsigned int offset, void *buf,
                      size_t count, int size);
    void (*write_bulk)(void *priv, unsigned int offset, const void *buf,
                       size_t count, int size);
    void (*set)(void *priv, unsigned int offset, u8 value,
                size_t count);
    void (*copy_from)(void *priv, unsigned int offset, void *to,
                      size_t count);
    void (*copy_to)(void *priv, unsigned int offset, const void *from,
                    size_t count);
};

struct logic_iomem_area {
    const struct logic_iomem_region_ops *ops;
    void *priv;
};

extern int logic_iomem_add_region(struct resource *resource,
                                  const struct logic_iomem_region_ops *ops);

#endif /* _LINUX_LOGIC_IOMEM_H */
