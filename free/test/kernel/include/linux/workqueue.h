/* SPDX-License-Identifier: GPL-2.0 */
/* Stub workqueue.h for free-cc kernel compilation testing */
#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/types.h>
#include <linux/list.h>

typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
    unsigned long data;
    struct list_head entry;
    work_func_t func;
};

struct delayed_work {
    struct work_struct work;
    unsigned long timer_expires;
};

#define __WORK_INITIALIZER(n, f) { \
    .data = 0, \
    .entry = { &(n).entry, &(n).entry }, \
    .func = (f), \
}

#define DECLARE_WORK(n, f) \
    struct work_struct n = __WORK_INITIALIZER(n, f)

#define INIT_WORK(_work, _func) do { \
    (_work)->data = 0; \
    (_work)->func = (_func); \
} while (0)

#define INIT_DELAYED_WORK(_work, _func) \
    INIT_WORK(&(_work)->work, (_func))

extern struct workqueue_struct *system_wq;

extern bool schedule_work(struct work_struct *work);
extern bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
extern bool queue_delayed_work(struct workqueue_struct *wq,
                               struct delayed_work *dwork,
                               unsigned long delay);
extern bool cancel_work_sync(struct work_struct *work);
extern bool cancel_delayed_work_sync(struct delayed_work *dwork);
extern void flush_work(struct work_struct *work);
extern void destroy_workqueue(struct workqueue_struct *wq);
extern struct workqueue_struct *alloc_ordered_workqueue(const char *fmt, unsigned int flags, ...);

struct workqueue_struct;

#endif /* _LINUX_WORKQUEUE_H */
