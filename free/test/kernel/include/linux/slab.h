/* SPDX-License-Identifier: GPL-2.0 */
/* Stub slab.h for free-cc kernel compilation testing */
#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <linux/types.h>
#include <linux/bug.h>

/* Memory allocation flags */
#define GFP_KERNEL   0x0001
#define GFP_ATOMIC   0x0002
#define GFP_NOWAIT   0x0004
#define GFP_DMA      0x0008
#define GFP_DMA32    0x0010
#define GFP_HIGHUSER 0x0020
#define GFP_USER     0x0040
#define __GFP_ZERO   0x0100
#define __GFP_NOWARN 0x0200
#define __GFP_NORETRY 0x0400

/* Slab flags */
#define SLAB_HWCACHE_ALIGN  0x00002000UL
#define SLAB_PANIC          0x00040000UL
#define SLAB_RECLAIM_ACCOUNT 0x00020000UL
#define SLAB_MEM_SPREAD     0x00100000UL

extern void *kmalloc(size_t size, gfp_t flags);
extern void *kzalloc(size_t size, gfp_t flags);
extern void *kcalloc(size_t n, size_t size, gfp_t flags);
extern void *krealloc(const void *p, size_t new_size, gfp_t flags);
extern void kfree(const void *p);
extern void kvfree(const void *p);

extern void *vmalloc(unsigned long size);
extern void vfree(const void *addr);

extern void *kvmalloc(size_t size, gfp_t flags);

static inline void *kvzalloc(size_t size, gfp_t flags)
{
    return kzalloc(size, flags);
}

static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
    return kcalloc(n, size, flags);
}

#define kmalloc_track_caller(size, flags) kmalloc(size, flags)
extern char *kstrdup(const char *s, gfp_t gfp);
extern const char *kstrdup_const(const char *s, gfp_t gfp);
#define kfree_const(x) kfree(x)

/* kmem_cache stubs */
struct kmem_cache;

extern struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
    unsigned int align, unsigned long flags,
    void (*ctor)(void *));

extern void kmem_cache_destroy(struct kmem_cache *s);
extern void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags);
extern void kmem_cache_free(struct kmem_cache *cachep, void *objp);

/* KMEM_CACHE helper */
#define KMEM_CACHE(__struct, __flags) \
    kmem_cache_create(#__struct, sizeof(struct __struct), \
        __alignof__(struct __struct), (__flags), NULL)

/* Size helpers */
static inline size_t ksize(const void *obj)
{
    (void)obj;
    return 0;
}

/* kmalloc_obj: allocate memory for a single struct */
#define kmalloc_obj(obj, flags) kmalloc(sizeof(obj), flags)

#endif /* _LINUX_SLAB_H */
