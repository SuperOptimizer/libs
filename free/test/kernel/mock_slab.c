/* EXPECTED: 0 */
/* Mock kernel slab allocator patterns - kmalloc/kfree, kmem_cache */

#define NULL ((void *)0)
#define offsetof(type, member) ((unsigned long)&((type *)0)->member)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

/* GFP flags */
#define GFP_KERNEL   0x0001
#define GFP_ATOMIC   0x0002
#define GFP_ZERO     0x0004
#define __GFP_ZERO   GFP_ZERO

typedef unsigned int gfp_t;
typedef unsigned long size_t;

/* ---- Simple heap for testing ---- */
#define HEAP_SIZE 65536
static char heap[HEAP_SIZE];
static int heap_pos;

/* Bump allocator */
static void *heap_alloc(size_t size) {
    void *p;
    int aligned;
    aligned = (int)((size + 15) & ~15UL);
    if (heap_pos + aligned > HEAP_SIZE) return NULL;
    p = &heap[heap_pos];
    heap_pos += aligned;
    return p;
}

/* Zero memory */
static void *memset_simple(void *s, int c, size_t n) {
    char *p;
    size_t i;
    p = (char *)s;
    for (i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

/* ---- kmalloc / kfree mock ---- */
static void *kmalloc(size_t size, gfp_t flags) {
    void *p;
    p = heap_alloc(size);
    if (p && (flags & __GFP_ZERO))
        memset_simple(p, 0, size);
    return p;
}

static void *kzalloc(size_t size, gfp_t flags) {
    return kmalloc(size, flags | __GFP_ZERO);
}

static void kfree(void *p) {
    /* No-op in bump allocator */
    (void)p;
}

/* ---- kmem_cache mock ---- */
struct kmem_cache {
    const char *name;
    size_t object_size;
    size_t align;
    unsigned long flags;
    void (*ctor)(void *);
    int num_active;
};

#define SLAB_HWCACHE_ALIGN 0x0001
#define SLAB_PANIC         0x0002

static struct kmem_cache *
kmem_cache_create(const char *name, size_t size, size_t align,
                  unsigned long flags, void (*ctor)(void *)) {
    struct kmem_cache *cache;
    cache = (struct kmem_cache *)kmalloc(sizeof(*cache), GFP_KERNEL);
    if (!cache) return NULL;
    cache->name = name;
    cache->object_size = size;
    cache->align = align;
    cache->flags = flags;
    cache->ctor = ctor;
    cache->num_active = 0;
    return cache;
}

static void *kmem_cache_alloc(struct kmem_cache *cache, gfp_t flags) {
    void *obj;
    size_t alloc_size;
    alloc_size = cache->object_size;
    if (cache->align > 0 && alloc_size < cache->align)
        alloc_size = cache->align;
    obj = kmalloc(alloc_size, flags);
    if (obj && cache->ctor)
        cache->ctor(obj);
    cache->num_active++;
    return obj;
}

static void kmem_cache_free(struct kmem_cache *cache, void *obj) {
    kfree(obj);
    cache->num_active--;
}

static void kmem_cache_destroy(struct kmem_cache *cache) {
    kfree(cache);
}

/* ---- Linked list for slab tracking ---- */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

static void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static void list_add(struct list_head *new_node, struct list_head *head) {
    struct list_head *next;
    next = head->next;
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = head;
    head->next = new_node;
}

static int list_empty(const struct list_head *head) {
    return head->next == head;
}

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_entry(ptr, type, member) container_of(ptr, type, member)

/* ---- Test structures ---- */
struct inode {
    unsigned long i_ino;
    int i_mode;
    int i_nlink;
    struct list_head i_list;
};

static struct kmem_cache *inode_cache;
static struct list_head inode_list;

static void inode_init_once(void *obj) {
    struct inode *inode;
    inode = (struct inode *)obj;
    inode->i_ino = 0;
    inode->i_mode = 0;
    inode->i_nlink = 0;
    INIT_LIST_HEAD(&inode->i_list);
}

static struct inode *alloc_inode(unsigned long ino) {
    struct inode *inode;
    inode = (struct inode *)kmem_cache_alloc(inode_cache, GFP_KERNEL);
    if (unlikely(!inode)) return NULL;
    inode->i_ino = ino;
    inode->i_nlink = 1;
    list_add(&inode->i_list, &inode_list);
    return inode;
}

/* ---- Test ---- */
int main(void) {
    struct inode *i1, *i2, *i3;
    struct list_head *pos;
    void *raw;
    int count;

    /* Initialize */
    INIT_LIST_HEAD(&inode_list);
    inode_cache = kmem_cache_create("inode_cache",
                                    sizeof(struct inode),
                                    0,
                                    SLAB_HWCACHE_ALIGN,
                                    inode_init_once);
    if (!inode_cache) return 1;

    /* Allocate inodes */
    i1 = alloc_inode(100);
    i2 = alloc_inode(200);
    i3 = alloc_inode(300);
    if (!i1 || !i2 || !i3) return 2;

    /* Verify ino values */
    if (i1->i_ino != 100) return 3;
    if (i2->i_ino != 200) return 4;
    if (i3->i_ino != 300) return 5;

    /* Verify nlink */
    if (i1->i_nlink != 1) return 6;

    /* Count inodes on list */
    count = 0;
    list_for_each(pos, &inode_list) {
        count++;
    }
    if (count != 3) return 7;

    /* Verify cache tracking */
    if (inode_cache->num_active != 3) return 8;

    /* Free one inode */
    kmem_cache_free(inode_cache, i2);
    if (inode_cache->num_active != 2) return 9;

    /* Test kzalloc */
    raw = kzalloc(64, GFP_KERNEL);
    if (!raw) return 10;
    /* Verify it's zeroed */
    {
        char *bytes;
        int i;
        bytes = (char *)raw;
        for (i = 0; i < 64; i++) {
            if (bytes[i] != 0) return 11;
        }
    }
    kfree(raw);

    /* Cleanup */
    kmem_cache_destroy(inode_cache);

    return 0;
}
