/* SPDX-License-Identifier: GPL-2.0 */
/* Stub memblock.h for free-cc kernel compilation testing */
#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H

#include <linux/types.h>

extern void *memblock_alloc(phys_addr_t size, phys_addr_t align);

#endif /* _LINUX_MEMBLOCK_H */
