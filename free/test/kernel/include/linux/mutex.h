/* SPDX-License-Identifier: GPL-2.0 */
/* Stub mutex.h for free-cc kernel compilation testing */
#ifndef _LINUX_MUTEX_H
#define _LINUX_MUTEX_H

#include <linux/types.h>

struct mutex {
    long count;
};

#define DEFINE_MUTEX(name) \
    struct mutex name = { 1 }

static inline void mutex_init(struct mutex *m) { m->count = 1; }
static inline void mutex_lock(struct mutex *m) { (void)m; }
static inline void mutex_unlock(struct mutex *m) { (void)m; }
static inline int mutex_trylock(struct mutex *m) { (void)m; return 1; }

#endif /* _LINUX_MUTEX_H */
