/* Kernel-style test 2: READ_ONCE/WRITE_ONCE, barriers, bitfields */

/* ---- compiler hints ---- */
#define __always_inline inline __attribute__((always_inline))
#define __always_unused __attribute__((unused))
#define __packed __attribute__((packed))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier() asm volatile("" ::: "memory")

/* ---- READ_ONCE / WRITE_ONCE ---- */
#define READ_ONCE(x) ({ \
    typeof(x) __val; \
    asm volatile("" : "=r" (__val) : "0" (x)); \
    __val; \
})
#define WRITE_ONCE(x, val) do { \
    typeof(x) __val = (val); \
    asm volatile("" : "=r" (__val) : "0" (__val)); \
    (x) = __val; \
} while (0)

/* ---- smp barriers ---- */
#define smp_mb()  asm volatile("dmb ish" ::: "memory")
#define smp_rmb() asm volatile("dmb ishld" ::: "memory")
#define smp_wmb() asm volatile("dmb ishst" ::: "memory")

/* ---- atomic operations ---- */
typedef struct {
    int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }
#define atomic_read(v) READ_ONCE((v)->counter)
#define atomic_set(v, i) WRITE_ONCE((v)->counter, (i))

/* ---- bitfield-heavy structure ---- */
struct page_flags {
    unsigned long locked    : 1;
    unsigned long error     : 1;
    unsigned long referenced: 1;
    unsigned long uptodate  : 1;
    unsigned long dirty     : 1;
    unsigned long lru       : 1;
    unsigned long active    : 1;
    unsigned long slab      : 1;
    unsigned long reserved  : 8;
    unsigned long zone      : 4;
    unsigned long node      : 12;
} __packed;

/* ---- spinlock (simplified) ---- */
typedef struct {
    volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0 }

static __always_inline void spin_lock_init(spinlock_t *lock) {
    lock->lock = 0;
}

static __always_inline void spin_lock(spinlock_t *lock) {
    while (__builtin_expect(lock->lock, 0))
        barrier();
    lock->lock = 1;
    smp_mb();
}

static __always_inline void spin_unlock(spinlock_t *lock) {
    smp_mb();
    lock->lock = 0;
}

/* ---- container_of ---- */
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - __builtin_offsetof(type, member)); \
})

/* ---- test structures ---- */
struct device {
    int id;
    const char *name;
    spinlock_t lock;
    atomic_t refcount;
};

struct driver {
    const char *name;
    struct device dev;
    int flags;
};

/* ---- test function ---- */
int main(void) {
    struct device dev;
    struct driver drv;
    struct page_flags pf;
    int ref;
    int *p;

    /* init */
    dev.id = 42;
    dev.name = "test";
    spin_lock_init(&dev.lock);
    atomic_set(&dev.refcount, 1);

    /* bitfield access */
    pf.locked = 1;
    pf.dirty = 0;
    pf.zone = 3;
    pf.node = 7;

    /* atomic read */
    ref = atomic_read(&dev.refcount);

    /* barriers */
    barrier();
    smp_mb();

    /* container_of */
    drv.dev = dev;
    p = &drv.dev.id;

    /* spinlock */
    spin_lock(&dev.lock);
    dev.id = 100;
    spin_unlock(&dev.lock);

    if (likely(ref == 1) && pf.locked)
        return 0;
    return dev.id + ref;
}
