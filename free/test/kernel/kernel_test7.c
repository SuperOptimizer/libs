/* Kernel-style test 7: __extension__, complex typeof, nested macros */

#define __always_inline inline __attribute__((always_inline))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* ---- __extension__ usage ---- */
__extension__ typedef unsigned long long __u64;
__extension__ typedef long long __s64;

/* ---- types with __extension__ ---- */
typedef __u64 u64;
typedef __s64 s64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;
typedef short s16;
typedef signed char s8;

/* ---- size_t ---- */
typedef unsigned long size_t;

/* ---- NULL ---- */
#define NULL ((void *)0)

/* ---- errno values ---- */
#define ENOMEM 12
#define EINVAL 22
#define ENODEV 19

/* ---- error pointer encoding ---- */
#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static __always_inline void *ERR_PTR(long error) {
    return (void *)error;
}

static __always_inline long PTR_ERR(const void *ptr) {
    return (long)ptr;
}

static __always_inline int IS_ERR(const void *ptr) {
    return IS_ERR_VALUE((unsigned long)ptr);
}

/* ---- per-CPU basics (simplified) ---- */
#define __percpu /* marker */

/* ---- atomic_t ---- */
typedef struct { int counter; } atomic_t;
typedef struct { long counter; } atomic_long_t;

#define ATOMIC_INIT(i) { (i) }

/* ---- rcu ---- */
#define rcu_dereference(p) ({ \
    typeof(p) _________p1 = (p); \
    asm volatile("" : : : "memory"); \
    _________p1; \
})

#define rcu_assign_pointer(p, v) do { \
    asm volatile("dmb ish" ::: "memory"); \
    (p) = (v); \
} while (0)

/* ---- kref (kernel reference counting) ---- */
struct kref {
    atomic_t refcount;
};

static __always_inline void kref_init(struct kref *kref) {
    kref->refcount.counter = 1;
}

static __always_inline int kref_get(struct kref *kref) {
    kref->refcount.counter++;
    return kref->refcount.counter;
}

static __always_inline int kref_put(struct kref *kref) {
    kref->refcount.counter--;
    return kref->refcount.counter == 0;
}

/* ---- wait queue ---- */
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }

struct wait_queue_head {
    int lock;
    struct list_head head;
};

#define __WAIT_QUEUE_HEAD_INITIALIZER(name) { \
    .lock = 0, \
    .head = LIST_HEAD_INIT(name.head), \
}

/* ---- container_of ---- */
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

/* ---- EXPORT_SYMBOL (simplified) ---- */
#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define MODULE_LICENSE(license)
#define MODULE_AUTHOR(author)
#define MODULE_DESCRIPTION(desc)

/* ---- test structures ---- */
struct kobject {
    const char *name;
    struct kref kref;
    void *parent;
};

struct cdev {
    struct kobject kobj;
    unsigned int dev;
    unsigned int count;
};

/* ---- function with multiple attributes ---- */
static __attribute__((noinline)) __attribute__((cold)) void error_handler(int code) __attribute__((unused));
static void error_handler(int code) {
    (void)code;
    __builtin_trap();
}

/* ---- test ---- */
int main(void) {
    struct kobject kobj;
    struct cdev cdev;
    void *p;
    long err;
    struct kref *kr;
    u64 big;
    s64 neg;
    int rc;

    /* kref operations */
    kref_init(&kobj.kref);
    kobj.name = "test_kobj";
    kobj.parent = NULL;
    kref_get(&kobj.kref);
    kref_get(&kobj.kref);

    /* container_of */
    cdev.kobj = kobj;
    cdev.dev = 42;
    cdev.count = 1;
    kr = &cdev.kobj.kref;

    /* error pointers */
    p = ERR_PTR(-ENOMEM);
    if (IS_ERR(p)) {
        err = PTR_ERR(p);
        (void)err;
    }

    /* rcu */
    {
        struct kobject *rcu_ptr;
        rcu_ptr = NULL;
        rcu_assign_pointer(rcu_ptr, &kobj);
        p = rcu_dereference(rcu_ptr);
    }

    /* 64-bit types */
    big = 0x123456789ABCDEFULL;
    neg = -12345678LL;
    (void)big;
    (void)neg;

    /* kref_put */
    rc = kref_put(&kobj.kref);
    rc = kref_put(&kobj.kref);

    if (likely(kobj.kref.refcount.counter == 1))
        return 0;
    return kobj.kref.refcount.counter;
}
