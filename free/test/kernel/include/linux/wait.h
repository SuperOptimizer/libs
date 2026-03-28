/* SPDX-License-Identifier: GPL-2.0 */
/* Stub wait.h for free-cc kernel compilation testing */
#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct wait_queue_head {
    spinlock_t lock;
    struct list_head head;
};

typedef struct wait_queue_head wait_queue_head_t;

#define DECLARE_WAIT_QUEUE_HEAD(name) \
    wait_queue_head_t name

static inline void init_waitqueue_head(wait_queue_head_t *wq_head)
{
    (void)wq_head;
}

static inline void wake_up(wait_queue_head_t *wq_head)
{
    (void)wq_head;
}

#endif /* _LINUX_WAIT_H */
