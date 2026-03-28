/* SPDX-License-Identifier: GPL-2.0 */
/* Stub cpumask.h for free-cc kernel compilation testing */
#ifndef _LINUX_CPUMASK_H
#define _LINUX_CPUMASK_H

#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>

#define NR_CPUS 256
#define nr_cpu_ids 1
#define nr_cpumask_bits NR_CPUS

typedef struct cpumask {
    unsigned long bits[NR_CPUS / (8 * sizeof(unsigned long))];
} cpumask_t;

typedef struct cpumask cpumask_var_t[1];

extern const struct cpumask *const cpu_possible_mask;
extern const struct cpumask *const cpu_online_mask;
extern const struct cpumask *const cpu_present_mask;
extern const struct cpumask *const cpu_active_mask;

#define cpu_possible(cpu) 1
#define cpu_online(cpu) 1
#define num_online_cpus() 1
#define num_possible_cpus() 1

static inline unsigned int cpumask_check(unsigned int cpu)
{
    return cpu;
}

static inline void cpumask_set_cpu(unsigned int cpu, struct cpumask *dstp)
{
    set_bit(cpu, dstp->bits);
}

static inline void cpumask_clear_cpu(unsigned int cpu, struct cpumask *dstp)
{
    clear_bit(cpu, dstp->bits);
}

static inline int cpumask_test_cpu(int cpu, const struct cpumask *cpumask)
{
    return test_bit(cpu, cpumask->bits);
}

static inline void cpumask_clear(struct cpumask *dstp)
{
    bitmap_zero(dstp->bits, NR_CPUS);
}

static inline unsigned int cpumask_first(const struct cpumask *srcp)
{
    return find_first_bit(srcp->bits, NR_CPUS);
}

static inline unsigned int cpumask_next(int n, const struct cpumask *srcp)
{
    return find_next_bit(srcp->bits, NR_CPUS, n + 1);
}

static inline unsigned int cpumask_weight(const struct cpumask *srcp)
{
    return bitmap_weight(srcp->bits, NR_CPUS);
}

static inline bool cpumask_empty(const struct cpumask *srcp)
{
    return bitmap_empty(srcp->bits, NR_CPUS);
}

#define for_each_cpu(cpu, mask) \
    for ((cpu) = cpumask_first(mask); \
         (cpu) < NR_CPUS; \
         (cpu) = cpumask_next((cpu), (mask)))

#define for_each_possible_cpu(cpu) \
    for ((cpu) = 0; (cpu) < 1; (cpu)++)

#define for_each_online_cpu(cpu) \
    for ((cpu) = 0; (cpu) < 1; (cpu)++)

static inline bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
    (void)mask; (void)flags;
    return true;
}

static inline void free_cpumask_var(cpumask_var_t mask)
{
    (void)mask;
}

static inline bool zalloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
    (void)mask; (void)flags;
    return true;
}

#endif /* _LINUX_CPUMASK_H */
