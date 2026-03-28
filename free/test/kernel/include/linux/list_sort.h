/* SPDX-License-Identifier: GPL-2.0 */
/* Stub list_sort.h for free-cc kernel compilation testing */
#ifndef _LINUX_LIST_SORT_H
#define _LINUX_LIST_SORT_H

struct list_head;

typedef int __attribute__((nonnull(2,3))) (*list_cmp_func_t)(void *,
        const struct list_head *, const struct list_head *);

void list_sort(void *priv, struct list_head *head, list_cmp_func_t cmp);

#endif /* _LINUX_LIST_SORT_H */
