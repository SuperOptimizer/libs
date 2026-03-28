/* SPDX-License-Identifier: GPL-2.0 */
/* Stub sched/task.h for free-cc kernel compilation testing */
#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

#include <linux/sched.h>

extern struct task_struct init_task;
extern void put_task_struct(struct task_struct *t);
extern void get_task_struct(struct task_struct *t);

#endif /* _LINUX_SCHED_TASK_H */
