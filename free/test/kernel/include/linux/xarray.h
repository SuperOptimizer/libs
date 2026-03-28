/* SPDX-License-Identifier: GPL-2.0 */
/* Stub xarray.h for free-cc kernel compilation testing */
#ifndef _LINUX_XARRAY_H
#define _LINUX_XARRAY_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/compiler.h>

/* XArray entry flags */
#define BITS_PER_XA_VALUE (BITS_PER_LONG - 1)
#define XA_ZERO_ENTRY    ((void *)0x100)
#define XA_RETRY_ENTRY   ((void *)0x200)

#define xa_mk_value(v) ((void *)((unsigned long)(v) << 1 | 1))
#define xa_to_value(entry) ((unsigned long)(entry) >> 1)
#define xa_is_value(entry) ((unsigned long)(entry) & 1)
#define xa_is_zero(entry) ((entry) == XA_ZERO_ENTRY)
#define xa_is_retry(entry) ((entry) == XA_RETRY_ENTRY)
#define xa_is_err(entry) (unlikely(xa_err(entry) != 0))
#define xa_mk_internal(v) ((void *)((v) << 2 | 2))
#define xa_is_internal(entry) (((unsigned long)(entry) & 3) == 2)
#define xa_to_internal(entry) ((unsigned long)(entry) >> 2)
#define xa_is_node(entry) (xa_is_internal(entry) && (unsigned long)(entry) > 4096)

static inline int xa_err(void *entry)
{
    if (xa_is_internal(entry) && (unsigned long)entry >= (unsigned long)(-4095UL << 2))
        return (int)((unsigned long)entry >> 2) | ~0UL;
    return 0;
}

#define XA_FLAGS_TRACK_FREE (1U << 0)
#define XA_FLAGS_ZERO_BUSY  (1U << 1)
#define XA_FLAGS_ALLOC_WRAPPED (1U << 2)
#define XA_FLAGS_ACCOUNT    (1U << 3)
#define XA_FLAGS_ALLOC (XA_FLAGS_TRACK_FREE | XA_FLAGS_ZERO_BUSY)
#define XA_FLAGS_LOCK_IRQ   (1U << 4)
#define XA_FLAGS_LOCK_BH    (1U << 5)
#define XA_FLAGS_MARK(mark) (1U << (16 + (mark)))

typedef unsigned __bitwise xa_mark_t;
#define XA_MARK_0  ((__force xa_mark_t)0U)
#define XA_MARK_1  ((__force xa_mark_t)1U)
#define XA_MARK_2  ((__force xa_mark_t)2U)
#define XA_PRESENT ((__force xa_mark_t)8U)
#define XA_MARK_MAX XA_MARK_2
#define XA_FREE_MARK XA_MARK_0

#define XA_CHUNK_SHIFT 6
#define XA_CHUNK_SIZE  (1UL << XA_CHUNK_SHIFT)
#define XA_CHUNK_MASK  (XA_CHUNK_SIZE - 1)
#define XA_MAX_MARKS   3

struct xa_node {
    unsigned char shift;
    unsigned char offset;
    unsigned char count;
    unsigned char nr_values;
    struct xa_node *parent;
    struct xarray *array;
    union {
        struct list_head private_list;
        struct rcu_head rcu_head;
    };
    void *slots[XA_CHUNK_SIZE];
    union {
        unsigned long tags[XA_MAX_MARKS][((XA_CHUNK_SIZE) + BITS_PER_LONG - 1) / BITS_PER_LONG];
        unsigned long marks[XA_MAX_MARKS][((XA_CHUNK_SIZE) + BITS_PER_LONG - 1) / BITS_PER_LONG];
    };
};

struct xarray {
    spinlock_t xa_lock;
    gfp_t xa_flags;
    void *xa_head;
};

#define XARRAY_INIT(name, flags) { \
    .xa_lock = __SPIN_LOCK_UNLOCKED(name.xa_lock), \
    .xa_flags = flags, \
    .xa_head = NULL, \
}

#define DEFINE_XARRAY_FLAGS(name, flags) \
    struct xarray name = XARRAY_INIT(name, flags)
#define DEFINE_XARRAY(name) DEFINE_XARRAY_FLAGS(name, 0)
#define DEFINE_XARRAY_ALLOC(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC)
#define DEFINE_XARRAY_ALLOC1(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC1)

extern void xa_init_flags(struct xarray *, gfp_t flags);
extern void xa_destroy(struct xarray *);
extern void *xa_load(struct xarray *, unsigned long index);
extern void *xa_store(struct xarray *, unsigned long index, void *entry, gfp_t);
extern void *xa_erase(struct xarray *, unsigned long index);
extern void *xa_store_range(struct xarray *, unsigned long first,
                            unsigned long last, void *entry, gfp_t);
extern bool xa_get_mark(struct xarray *, unsigned long index, xa_mark_t);
extern void xa_set_mark(struct xarray *, unsigned long index, xa_mark_t);
extern void xa_clear_mark(struct xarray *, unsigned long index, xa_mark_t);
extern void *xa_find(struct xarray *, unsigned long *index,
                     unsigned long max, xa_mark_t);
extern void *xa_find_after(struct xarray *, unsigned long *index,
                           unsigned long max, xa_mark_t);
extern int xa_alloc(struct xarray *, u32 *id, void *entry,
                    struct xa_limit, gfp_t);
extern int xa_alloc_cyclic(struct xarray *, u32 *id, void *entry,
                           struct xa_limit, u32 *next, gfp_t);
extern int xa_reserve(struct xarray *, unsigned long index, gfp_t);
extern void xa_release(struct xarray *, unsigned long index);

struct xa_limit {
    u32 min;
    u32 max;
};

#define XA_LIMIT(lo, hi) (struct xa_limit){ .min = lo, .max = hi }
#define xa_limit_32b XA_LIMIT(0, 0xFFFFFFFF)
#define xa_limit_31b XA_LIMIT(0, 0x7FFFFFFF)

/* XA state for multi-index operations */
typedef struct {
    struct xarray *xa;
    unsigned long xa_index;
    unsigned char xa_shift;
    unsigned char xa_sibs;
    unsigned char xa_offset;
    unsigned char xa_pad;
    struct xa_node *xa_node;
    struct xa_node *xa_alloc;
    void *xa_update;
} XA_STATE_TYPE;

#define XA_STATE(name, array, index) \
    XA_STATE_TYPE name = { \
        .xa = array, \
        .xa_index = index, \
        .xa_shift = 0, \
        .xa_sibs = 0, \
        .xa_offset = 0, \
        .xa_pad = 0, \
        .xa_node = NULL, \
        .xa_alloc = NULL, \
        .xa_update = NULL, \
    }

extern void *xas_load(XA_STATE_TYPE *);
extern void *xas_store(XA_STATE_TYPE *, void *entry);
extern void *xas_find(XA_STATE_TYPE *, unsigned long max);
extern void *xas_find_marked(XA_STATE_TYPE *, unsigned long max, xa_mark_t);
extern void *xas_find_conflict(XA_STATE_TYPE *);
extern void xas_set_mark(XA_STATE_TYPE *, xa_mark_t);
extern void xas_clear_mark(XA_STATE_TYPE *, xa_mark_t);
extern void xas_init_marks(XA_STATE_TYPE *);
extern bool xas_nomem(XA_STATE_TYPE *, gfp_t);
extern void xas_create_range(XA_STATE_TYPE *);
extern void xas_pause(XA_STATE_TYPE *);
extern void *xas_create(XA_STATE_TYPE *, bool);
extern void xas_destroy(XA_STATE_TYPE *);
extern int xas_get_mark(const XA_STATE_TYPE *, xa_mark_t);
extern void xas_split(XA_STATE_TYPE *, void *entry, unsigned int order);
extern void xas_split_alloc(XA_STATE_TYPE *, void *entry,
                            unsigned int order, gfp_t);

static inline bool xas_valid(const XA_STATE_TYPE *xas)
{
    return xas->xa_node != (void *)-1UL;
}

static inline bool xas_is_node(const XA_STATE_TYPE *xas)
{
    return xas->xa_node && xas->xa_node != (void *)-1UL
        && xas->xa_node != (void *)-2UL;
}

static inline bool xas_not_node(struct xa_node *node)
{
    return ((unsigned long)node & 3) || !node;
}

static inline bool xas_frozen(struct xa_node *node)
{
    return (unsigned long)node & 2;
}

static inline bool xas_top(struct xa_node *node)
{
    return node <= (struct xa_node *)((void *)0x100);
}

static inline bool xas_error(const XA_STATE_TYPE *xas)
{
    return xas->xa_node == (void *)-1UL;
}

static inline int xas_invalid(const XA_STATE_TYPE *xas)
{
    return (unsigned long)xas->xa_node & 3;
}

static inline void xas_set_err(XA_STATE_TYPE *xas, long err)
{
    xas->xa_node = (void *)-1UL;
    (void)err;
}

static inline void xas_reset(XA_STATE_TYPE *xas)
{
    xas->xa_node = NULL;
}

static inline void xas_set(XA_STATE_TYPE *xas, unsigned long index)
{
    xas->xa_index = index;
    xas->xa_node = NULL;
}

static inline void xas_advance(XA_STATE_TYPE *xas, unsigned long index)
{
    xas->xa_index = index;
}

static inline void xas_set_order(XA_STATE_TYPE *xas, unsigned long index,
                                 unsigned int order)
{
    xas->xa_index = index;
    xas->xa_shift = order;
    (void)order;
}

static inline void xas_set_update(XA_STATE_TYPE *xas, void *update)
{
    xas->xa_update = update;
}

static inline void *xas_next_entry(XA_STATE_TYPE *xas, unsigned long max)
{
    return xas_find(xas, max);
}

static inline unsigned int xas_find_chunk(XA_STATE_TYPE *xas, bool advance,
                                          xa_mark_t mark)
{
    (void)xas; (void)advance; (void)mark;
    return 0;
}

static inline void *xa_entry(const struct xarray *xa,
                             const struct xa_node *node, unsigned int offset)
{
    (void)xa;
    return node->slots[offset];
}

static inline void *xa_entry_locked(const struct xarray *xa,
                                    const struct xa_node *node,
                                    unsigned int offset)
{
    (void)xa;
    return node->slots[offset];
}

static inline struct xa_node *xa_parent(const struct xarray *xa,
                                        const struct xa_node *node)
{
    (void)xa;
    return node->parent;
}

static inline struct xa_node *xa_parent_locked(const struct xarray *xa,
                                               const struct xa_node *node)
{
    (void)xa;
    return node->parent;
}

static inline void *xa_head(const struct xarray *xa)
{
    return xa->xa_head;
}

static inline void *xa_head_locked(const struct xarray *xa)
{
    return xa->xa_head;
}

#define xa_for_each(xa, index, entry) \
    for (entry = xa_find(xa, &index, ULONG_MAX, XA_PRESENT); \
         entry; \
         entry = xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT))

#define xa_for_each_start(xa, index, entry, start) \
    for (index = start, \
         entry = xa_find(xa, &index, ULONG_MAX, XA_PRESENT); \
         entry; \
         entry = xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT))

#define xa_for_each_marked(xa, index, entry, filter) \
    for (entry = xa_find(xa, &index, ULONG_MAX, filter); \
         entry; \
         entry = xa_find_after(xa, &index, ULONG_MAX, filter))

static inline bool xa_empty(const struct xarray *xa)
{
    return xa->xa_head == NULL;
}

#define xas_for_each(xas, entry, max) \
    for (entry = xas_find(xas, max); entry; \
         entry = xas_next_entry(xas, max))

#define xas_for_each_marked(xas, entry, max, mark) \
    for (entry = xas_find_marked(xas, max, mark); entry; \
         entry = xas_find_marked(xas, max, mark))

#define xas_for_each_conflict(xas, entry) \
    for (entry = xas_find_conflict(xas); entry; \
         entry = xas_find_conflict(xas))

static inline void xas_lock(XA_STATE_TYPE *xas)
{
    spin_lock(&xas->xa->xa_lock);
}

static inline void xas_unlock(XA_STATE_TYPE *xas)
{
    spin_unlock(&xas->xa->xa_lock);
}

static inline void xas_lock_irq(XA_STATE_TYPE *xas)
{
    spin_lock(&xas->xa->xa_lock);
}

static inline void xas_unlock_irq(XA_STATE_TYPE *xas)
{
    spin_unlock(&xas->xa->xa_lock);
}

#endif /* _LINUX_XARRAY_H */
