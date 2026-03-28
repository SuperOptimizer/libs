/* SPDX-License-Identifier: GPL-2.0 */
/* Stub refcount.h for free-cc kernel compilation testing */
#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <linux/types.h>
#include <linux/bug.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

typedef struct refcount_struct {
    atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)    { .refs = ATOMIC_INIT(n) }
#define REFCOUNT_MAX        INT_MAX
#define REFCOUNT_SATURATED  (INT_MIN / 2)

static inline void refcount_set(refcount_t *r, int n)
{
    atomic_set(&r->refs, n);
}

static inline unsigned int refcount_read(const refcount_t *r)
{
    return atomic_read(&r->refs);
}

extern int refcount_add_not_zero(int i, refcount_t *r);
extern void refcount_add(int i, refcount_t *r);
extern int refcount_inc_not_zero(refcount_t *r);
extern void refcount_inc(refcount_t *r);
extern int refcount_sub_and_test(int i, refcount_t *r);
extern int refcount_dec_and_test(refcount_t *r);
extern void refcount_dec(refcount_t *r);
extern int refcount_dec_if_one(refcount_t *r);
extern int refcount_dec_not_one(refcount_t *r);
extern int refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock);
extern int refcount_dec_and_lock(refcount_t *r, spinlock_t *lock);
extern int refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
                                         unsigned long *flags);

#endif /* _LINUX_REFCOUNT_H */
