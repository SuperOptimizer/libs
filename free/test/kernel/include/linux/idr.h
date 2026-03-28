/* SPDX-License-Identifier: GPL-2.0 */
/* Stub idr.h for free-cc kernel compilation testing */
#ifndef _LINUX_IDR_H
#define _LINUX_IDR_H

#include <linux/types.h>
#include <linux/xarray.h>

struct idr {
    struct xarray idr_rt;
    unsigned int idr_base;
    unsigned int idr_next;
};

#define IDR_INIT_BASE(name, base) { \
    .idr_rt = XARRAY_INIT(name.idr_rt, XA_FLAGS_ALLOC), \
    .idr_base = (base), \
    .idr_next = 0, \
}

#define IDR_INIT(name) IDR_INIT_BASE(name, 0)
#define DEFINE_IDR(name) struct idr name = IDR_INIT(name)

extern int idr_alloc(struct idr *, void *ptr, int start, int end, gfp_t);
extern int idr_alloc_u32(struct idr *, void *ptr, u32 *nextid,
                         unsigned long max, gfp_t);
extern int idr_alloc_cyclic(struct idr *, void *ptr, int start, int end, gfp_t);
extern void *idr_remove(struct idr *, unsigned long id);
extern void *idr_find(const struct idr *, unsigned long id);
extern void *idr_replace(struct idr *, void *, unsigned long id);
extern void idr_destroy(struct idr *);
extern void idr_init_base(struct idr *, int base);
extern void idr_init(struct idr *);

static inline bool idr_is_empty(const struct idr *idr)
{
    return xa_empty(&idr->idr_rt);
}

#define idr_for_each_entry(idr, entry, id) \
    for (id = 0; (entry = idr_find(idr, id)) != NULL || id < 100; ++id) \
        if (entry)

#define idr_for_each_entry_ul(idr, entry, tmp, id) \
    for (tmp = 0, id = 0; tmp < 100; ++tmp, ++id) \
        if ((entry = idr_find(idr, id)) != NULL)

extern int ida_alloc_range(struct ida *, unsigned int min,
                           unsigned int max, gfp_t);
extern void ida_free(struct ida *, unsigned int id);
extern void ida_destroy(struct ida *);

struct ida {
    struct xarray xa;
};

#define IDA_INIT_FLAGS(name, flags) { .xa = XARRAY_INIT(name.xa, flags) }
#define IDA_INIT(name) IDA_INIT_FLAGS(name, XA_FLAGS_ALLOC)
#define DEFINE_IDA(name) struct ida name = IDA_INIT(name)

static inline int ida_alloc(struct ida *ida, gfp_t gfp)
{
    return ida_alloc_range(ida, 0, ~0U, gfp);
}

static inline int ida_alloc_min(struct ida *ida, unsigned int min, gfp_t gfp)
{
    return ida_alloc_range(ida, min, ~0U, gfp);
}

static inline int ida_alloc_max(struct ida *ida, unsigned int max, gfp_t gfp)
{
    return ida_alloc_range(ida, 0, max, gfp);
}

static inline bool ida_is_empty(const struct ida *ida)
{
    return xa_empty(&ida->xa);
}

#endif /* _LINUX_IDR_H */
