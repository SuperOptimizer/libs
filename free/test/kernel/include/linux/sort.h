/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H

#include <linux/types.h>

typedef int (*cmp_func_t)(const void *, const void *);
typedef int (*cmp_r_func_t)(const void *, const void *, const void *);
typedef void (*swap_func_t)(void *, void *, size_t);
typedef void (*swap_r_func_t)(void *, void *, size_t, const void *);

void sort(void *base, size_t num, size_t size,
          cmp_func_t cmp_func,
          swap_func_t swap_func);

void sort_r(void *base, size_t num, size_t size,
            cmp_func_t cmp_func,
            swap_func_t swap_func,
            const void *priv);

#endif /* _LINUX_SORT_H */
