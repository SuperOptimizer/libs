/* SPDX-License-Identifier: GPL-2.0 */
/* Stub err.h for free-cc kernel compilation testing */
#ifndef _LINUX_ERR_H
#define _LINUX_ERR_H

#include <linux/types.h>
#include <linux/errno.h>

#define MAX_ERRNO   4095

#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void *ERR_PTR(long error)
{
    return (void *)error;
}

static inline long PTR_ERR(const void *ptr)
{
    return (long)ptr;
}

#define IS_ERR(ptr)    IS_ERR_VALUE((unsigned long)(ptr))
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR_VALUE((unsigned long)(ptr)))
#define ERR_CAST(ptr)  ((void *)(ptr))

static inline int PTR_ERR_OR_ZERO(const void *ptr)
{
    if (IS_ERR(ptr))
        return (int)PTR_ERR(ptr);
    return 0;
}

#endif /* _LINUX_ERR_H */
