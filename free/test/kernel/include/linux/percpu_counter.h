/* SPDX-License-Identifier: GPL-2.0 */
/* Stub percpu_counter.h for free-cc kernel compilation testing */
#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct percpu_counter {
    raw_spinlock_t lock;
    s64 count;
    s32 *counters;
};

extern int __percpu_counter_init(struct percpu_counter *fbc, s64 amount,
                                 gfp_t gfp, struct lock_class_key *key);

#define percpu_counter_init(fbc, value, gfp) \
    __percpu_counter_init(fbc, value, gfp, NULL)

extern void percpu_counter_destroy(struct percpu_counter *fbc);
extern void percpu_counter_set(struct percpu_counter *fbc, s64 amount);
extern void percpu_counter_add_batch(struct percpu_counter *fbc,
                                     s64 amount, s32 batch);
extern s64 __percpu_counter_sum(struct percpu_counter *fbc);
extern s64 percpu_counter_sum_positive(struct percpu_counter *fbc);

static inline void percpu_counter_add(struct percpu_counter *fbc, s64 amount)
{
    percpu_counter_add_batch(fbc, amount, 32);
}

static inline s64 percpu_counter_sum(struct percpu_counter *fbc)
{
    return __percpu_counter_sum(fbc);
}

static inline s64 percpu_counter_read(struct percpu_counter *fbc)
{
    return fbc->count;
}

static inline s64 percpu_counter_read_positive(struct percpu_counter *fbc)
{
    s64 ret = fbc->count;
    if (ret < 0) ret = 0;
    return ret;
}

static inline bool percpu_counter_initialized(struct percpu_counter *fbc)
{
    return fbc->counters != NULL;
}

#endif /* _LINUX_PERCPU_COUNTER_H */
