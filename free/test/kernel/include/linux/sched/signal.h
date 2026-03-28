/* SPDX-License-Identifier: GPL-2.0 */
/* Stub sched/signal.h for free-cc kernel compilation testing */
#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/sched.h>

struct sighand_struct {
    int count;
};

struct signal_struct {
    int nr_threads;
    int group_exit_code;
    struct list_head thread_head;
};

extern int send_sig(int sig, struct task_struct *p, int priv);
extern int force_sig(int sig);

#define for_each_thread(p, t) \
    for (t = p; t != NULL; t = NULL)

#define while_each_thread(g, t) \
    while (0)

static inline struct task_struct *next_thread(const struct task_struct *p)
{
    (void)p;
    return NULL;
}

#endif /* _LINUX_SCHED_SIGNAL_H */
