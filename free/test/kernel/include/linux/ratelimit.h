/* SPDX-License-Identifier: GPL-2.0 */
/* Stub ratelimit.h for free-cc kernel compilation testing */
#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/printk.h>

#define RATELIMIT_MSG_ON_RELEASE    (1 << 0)

struct ratelimit_state {
    spinlock_t lock;
    int interval;
    int burst;
    int printed;
    int missed;
    unsigned long begin;
    unsigned long flags;
    atomic_t rs_n_left;
};

#define RATELIMIT_STATE_INIT(name, interval_init, burst_init) { \
    .interval = interval_init,                                   \
    .burst = burst_init,                                         \
}

#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init) \
    struct ratelimit_state name =                                \
        RATELIMIT_STATE_INIT(name, interval_init, burst_init)

extern int ___ratelimit(struct ratelimit_state *rs, const char *func);
#define __ratelimit(state) ___ratelimit(state, __func__)

static inline int ratelimit_state_reset_miss(struct ratelimit_state *rs)
{
    int missed = rs->missed;
    rs->missed = 0;
    return missed;
}

static inline void ratelimit_state_init(struct ratelimit_state *rs,
                                        int interval, int burst)
{
    rs->interval = interval;
    rs->burst = burst;
    rs->printed = 0;
    rs->missed = 0;
    rs->begin = 0;
    rs->flags = 0;
}

static inline void ratelimit_set_flags(struct ratelimit_state *rs,
                                       unsigned long flags)
{
    rs->flags = flags;
}

#endif /* _LINUX_RATELIMIT_H */
