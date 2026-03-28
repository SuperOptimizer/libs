/* SPDX-License-Identifier: GPL-2.0 */
/* Stub llist.h for free-cc kernel compilation testing */
#ifndef _LINUX_LLIST_H
#define _LINUX_LLIST_H

#include <linux/types.h>
#include <linux/kernel.h>

struct llist_head {
    struct llist_node *first;
};

struct llist_node {
    struct llist_node *next;
};

#define LLIST_HEAD_INIT(name)  { NULL }
#define LLIST_HEAD(name)       struct llist_head name = LLIST_HEAD_INIT(name)

static inline void init_llist_head(struct llist_head *list)
{
    list->first = NULL;
}

#define llist_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define llist_for_each(pos, node) \
    for ((pos) = (node); pos; (pos) = (pos)->next)

#define llist_for_each_safe(pos, n, node) \
    for ((pos) = (node); (pos) && ((n) = (pos)->next, 1); (pos) = (n))

#define llist_for_each_entry(pos, node, member) \
    for ((pos) = llist_entry((node), typeof(*(pos)), member); \
         &(pos)->member != NULL; \
         (pos) = llist_entry((pos)->member.next, typeof(*(pos)), member))

static inline bool llist_empty(const struct llist_head *head)
{
    return READ_ONCE(head->first) == NULL;
}

/* Atomic operations - stub as non-atomic for compilation testing */
#define smp_load_acquire(p) (*(p))

#define try_cmpxchg(ptr, oldp, new) \
    ({ typeof(*(ptr)) __old = *(oldp); \
       typeof(*(ptr)) __cur = *(ptr); \
       bool __ret = (__cur == __old); \
       if (__ret) *(ptr) = (new); \
       else *(oldp) = __cur; \
       __ret; })

extern struct llist_node *llist_del_first(struct llist_head *head);
extern bool llist_del_first_this(struct llist_head *head,
                                 struct llist_node *this);
extern struct llist_node *llist_reverse_order(struct llist_node *head);

#endif /* _LINUX_LLIST_H */
