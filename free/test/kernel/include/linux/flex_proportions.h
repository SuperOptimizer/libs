/* SPDX-License-Identifier: GPL-2.0 */
/* Stub flex_proportions.h for free-cc kernel compilation testing */
#ifndef _LINUX_FLEX_PROPORTIONS_H
#define _LINUX_FLEX_PROPORTIONS_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct fprop_global {
    unsigned int period;
    seqcount_t sequence;
    unsigned long events;
};

struct fprop_local_single {
    unsigned long events;
    unsigned int period;
    raw_spinlock_t lock;
};

struct fprop_local_percpu {
    unsigned long events;
    unsigned int period;
    raw_spinlock_t lock;
};

extern int fprop_global_init(struct fprop_global *p, gfp_t gfp);
extern void fprop_global_destroy(struct fprop_global *p);
extern bool fprop_new_period(struct fprop_global *p, int periods);

extern void __fprop_inc_single(struct fprop_global *p,
                               struct fprop_local_single *pl);
static inline void fprop_inc_single(struct fprop_global *p,
                                    struct fprop_local_single *pl)
{
    __fprop_inc_single(p, pl);
}
extern void fprop_fraction_single(struct fprop_global *p,
                                  struct fprop_local_single *pl,
                                  unsigned long *numerator,
                                  unsigned long *denominator);

extern int fprop_local_init_single(struct fprop_local_single *pl);
extern void fprop_local_destroy_single(struct fprop_local_single *pl);

extern int fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp);
extern void fprop_local_destroy_percpu(struct fprop_local_percpu *pl);
extern void __fprop_inc_percpu(struct fprop_global *p,
                               struct fprop_local_percpu *pl);
extern void fprop_fraction_percpu(struct fprop_global *p,
                                  struct fprop_local_percpu *pl,
                                  unsigned long *numerator,
                                  unsigned long *denominator);
extern void __fprop_inc_percpu_max(struct fprop_global *p,
                                   struct fprop_local_percpu *pl,
                                   int max_frac);

#endif /* _LINUX_FLEX_PROPORTIONS_H */
