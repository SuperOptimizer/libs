/* SPDX-License-Identifier: GPL-2.0 */
/* Stub rwsem.h for free-cc kernel compilation testing */
#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/types.h>

struct rw_semaphore {
    long count;
};

#define DECLARE_RWSEM(name) \
    struct rw_semaphore name = { 0 }

static inline void down_read(struct rw_semaphore *sem) { (void)sem; }
static inline void up_read(struct rw_semaphore *sem) { (void)sem; }
static inline void down_write(struct rw_semaphore *sem) { (void)sem; }
static inline void up_write(struct rw_semaphore *sem) { (void)sem; }

#endif /* _LINUX_RWSEM_H */
