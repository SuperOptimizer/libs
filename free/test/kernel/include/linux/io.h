/* SPDX-License-Identifier: GPL-2.0 */
/* Stub io.h for free-cc kernel compilation testing */
#ifndef _LINUX_IO_H
#define _LINUX_IO_H

#include <linux/types.h>

extern void __iowrite32_copy(void *to, const void *from, size_t count);
extern void __ioread32_copy(void *to, const void *from, size_t count);
extern void __iowrite64_copy(void *to, const void *from, size_t count);

#endif /* _LINUX_IO_H */
