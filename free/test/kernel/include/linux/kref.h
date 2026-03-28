/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kref.h for free-cc kernel compilation testing */
#ifndef _LINUX_KREF_H
#define _LINUX_KREF_H

#include <linux/types.h>
#include <linux/refcount.h>

struct kref {
    refcount_t refcount;
};

#define KREF_INIT(n) { .refcount = REFCOUNT_INIT(n) }

static inline void kref_init(struct kref *kref)
{
    refcount_set(&kref->refcount, 1);
}

static inline unsigned int kref_read(const struct kref *kref)
{
    return refcount_read(&kref->refcount);
}

static inline void kref_get(struct kref *kref)
{
    refcount_inc(&kref->refcount);
}

static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
    if (refcount_dec_and_test(&kref->refcount)) {
        release(kref);
        return 1;
    }
    return 0;
}

#endif /* _LINUX_KREF_H */
