/* SPDX-License-Identifier: GPL-2.0 */
/* Stub sched.h for free-cc kernel compilation testing */
#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <linux/types.h>
#include <linux/list.h>

#define cond_resched() do {} while (0)
#define might_sleep() do {} while (0)

/* Forward declarations */
struct mm_struct;
struct signal_struct;
struct files_struct;
struct nsproxy;
struct cred;

/* Minimal task_struct */
struct task_struct {
    volatile long state;
    int pid;
    int tgid;
    unsigned int flags;
    char comm[16];
    struct mm_struct *mm;
    struct mm_struct *active_mm;
    struct signal_struct *signal;
    struct files_struct *files;
    struct nsproxy *nsproxy;
    const struct cred *cred;
    const struct cred *real_cred;
    struct list_head tasks;
    struct list_head thread_group;
    struct list_head thread_node;
};

/* Minimal mm_struct */
struct mm_struct {
    atomic_t mm_users;
    atomic_t mm_count;
    unsigned long start_code, end_code;
    unsigned long start_data, end_data;
    unsigned long start_brk, brk;
    unsigned long start_stack;
    unsigned long mmap_base;
    unsigned long total_vm;
    unsigned long locked_vm;
    unsigned long pinned_vm;
    unsigned long data_vm;
    unsigned long exec_vm;
    unsigned long stack_vm;
};

extern struct task_struct *current;

/* PF flags */
#define PF_EXITING 0x00000004
#define PF_KTHREAD 0x00200000
#define PF_NOFREEZE 0x00008000

/* Task state */
#define TASK_RUNNING          0
#define TASK_INTERRUPTIBLE    1
#define TASK_UNINTERRUPTIBLE  2

#define MAX_SCHEDULE_TIMEOUT LONG_MAX

extern long schedule_timeout(long timeout);
extern void schedule(void);

extern pid_t task_pid_nr(struct task_struct *tsk);

#endif /* _LINUX_SCHED_H */
