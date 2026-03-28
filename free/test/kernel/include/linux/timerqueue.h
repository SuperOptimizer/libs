/* SPDX-License-Identifier: GPL-2.0 */
/* Stub timerqueue.h for free-cc kernel compilation testing */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/types.h>
#include <linux/rbtree.h>

typedef u64 ktime_t;

struct timerqueue_node {
    struct rb_node node;
    ktime_t expires;
};

struct timerqueue_head {
    struct rb_root_cached rb_root;
    struct timerqueue_node *next;
};

extern int timerqueue_add(struct timerqueue_head *head,
                          struct timerqueue_node *node);
extern int timerqueue_del(struct timerqueue_head *head,
                          struct timerqueue_node *node);
extern struct timerqueue_node *timerqueue_iterate_next(
                          struct timerqueue_node *node);

static inline struct timerqueue_node *timerqueue_getnext(
                          struct timerqueue_head *head)
{
    return head->next;
}

#endif /* _LINUX_TIMERQUEUE_H */
