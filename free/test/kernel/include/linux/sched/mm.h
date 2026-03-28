/* SPDX-License-Identifier: GPL-2.0 */
/* Stub sched/mm.h for free-cc kernel compilation testing */
#ifndef _LINUX_SCHED_MM_H
#define _LINUX_SCHED_MM_H

#include <linux/sched.h>
#include <linux/mm.h>

extern struct mm_struct *get_task_mm(struct task_struct *task);
extern void mmput(struct mm_struct *mm);

#endif /* _LINUX_SCHED_MM_H */
