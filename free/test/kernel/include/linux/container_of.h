/* SPDX-License-Identifier: GPL-2.0 */
/* Stub container_of.h for free-cc kernel compilation testing */
#ifndef _LINUX_CONTAINER_OF_H
#define _LINUX_CONTAINER_OF_H

#include <linux/stddef.h>

#ifndef container_of
#define container_of(ptr, type, member) ({              \
    void *__mptr = (void *)(ptr);                       \
    ((type *)(__mptr - offsetof(type, member))); })
#endif

#define typeof_member(T, m) typeof(((T *)0)->m)

#endif /* _LINUX_CONTAINER_OF_H */
