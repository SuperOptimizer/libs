/* SPDX-License-Identifier: GPL-2.0 */
/* Stub rculist.h for free-cc kernel compilation testing */
#ifndef _LINUX_RCULIST_H
#define _LINUX_RCULIST_H

#include <linux/list.h>
#include <linux/rcupdate.h>

static inline void list_add_rcu(struct list_head *new,
                                struct list_head *head)
{
    list_add(new, head);
}

static inline void list_add_tail_rcu(struct list_head *new,
                                     struct list_head *head)
{
    list_add_tail(new, head);
}

static inline void list_del_rcu(struct list_head *entry)
{
    __list_del_entry(entry);
    entry->prev = (void *)0x122;
}

#define list_for_each_entry_rcu(pos, head, member) \
    list_for_each_entry(pos, head, member)

#define list_entry_rcu(ptr, type, member) \
    container_of(ptr, type, member)

#define list_first_or_null_rcu(ptr, type, member) \
    (!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)

#define hlist_for_each_entry_rcu(pos, head, member) \
    for (pos = NULL; pos != NULL; )

static inline void hlist_add_head_rcu(struct hlist_node *n,
                                      struct hlist_head *h)
{
    (void)n; (void)h;
}

static inline void hlist_del_rcu(struct hlist_node *n)
{
    (void)n;
}

#endif /* _LINUX_RCULIST_H */
