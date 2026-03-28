/* Kernel-style test 5: function/variable attributes, sections, complex macros */

/* ---- compiler attributes ---- */
#define __section(s)         __attribute__((section(s)))
#define __used               __attribute__((used))
#define __maybe_unused       __attribute__((unused))
#define __always_inline      inline __attribute__((always_inline))
#define __noinline           __attribute__((noinline))
#define __weak               __attribute__((weak))
#define __cold               __attribute__((cold))
#define __packed             __attribute__((packed))
#define __aligned(x)         __attribute__((aligned(x)))
#define __noreturn           __attribute__((noreturn))
#define __constructor        __attribute__((constructor))
#define __destructor         __attribute__((destructor))
#define __deprecated(msg)    __attribute__((deprecated(msg)))
#define __pure               __attribute__((pure))
#define __const              __attribute__((const))
#define likely(x)            __builtin_expect(!!(x), 1)
#define unlikely(x)          __builtin_expect(!!(x), 0)

/* ---- typeof / statement expression tricks ---- */
#define min(x, y) ({ \
    typeof(x) _min1 = (x); \
    typeof(y) _min2 = (y); \
    _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({ \
    typeof(x) _max1 = (x); \
    typeof(y) _max2 = (y); \
    _max1 > _max2 ? _max1 : _max2; })

/* ---- ARRAY_SIZE ---- */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ---- READ_ONCE / WRITE_ONCE ---- */
#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile typeof(x) *)&(x) = (val))

/* ---- barriers ---- */
#define barrier() asm volatile("" ::: "memory")
#define smp_mb()  asm volatile("dmb ish" ::: "memory")

/* ---- container_of ---- */
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - __builtin_offsetof(type, member)); \
})

/* ---- list_head ---- */
struct list_head { struct list_head *next, *prev; };

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static __always_inline void __list_add(struct list_head *new,
    struct list_head *prev, struct list_head *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static __always_inline void list_add(struct list_head *new,
    struct list_head *head) {
    __list_add(new, head, head->next);
}

static __always_inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* ---- do_div (simplified) ---- */
#define do_div(n, base) ({ \
    unsigned long __base = (base); \
    unsigned long __rem = (n) % __base; \
    (n) = (n) / __base; \
    __rem; \
})

/* ---- printk-style format attribute ---- */
int printk(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* ---- weak function ---- */
__weak int optional_init(void) { return 0; }

/* ---- deprecated ---- */
__deprecated("use new_api instead")
int old_api(void) { return -1; }

/* ---- pure function ---- */
static __pure int compute_hash(int x) {
    return x * 2654435761;
}

/* ---- bitfield structure ---- */
struct __packed irq_desc {
    unsigned int irq;
    unsigned int status;
    unsigned long flags;
    unsigned int depth  : 8;
    unsigned int wake   : 1;
    unsigned int pending: 1;
    unsigned int disable: 1;
    unsigned int masked : 1;
    unsigned int pad    : 20;
};

/* ---- struct with aligned member ---- */
struct __aligned(64) cache_line {
    unsigned long data[8];
};

/* ---- nested struct with anonymous union (C11) ---- */
struct event {
    int type;
    union {
        struct { int x; int y; } mouse;
        struct { int key; int mod; } keyboard;
        struct { int w; int h; } resize;
    };
};

/* ---- test it ---- */
int main(void) {
    LIST_HEAD(work_list);
    struct irq_desc desc;
    struct cache_line cl;
    struct event ev;
    int arr[] = {100, 200, 300, 400, 500};
    int a, b, c;
    struct list_head *pos;
    unsigned long n;
    unsigned long rem;
    int hash;

    /* init irq desc */
    desc.irq = 42;
    desc.status = 0;
    desc.flags = 0;
    desc.depth = 1;
    desc.wake = 0;
    desc.pending = 0;
    desc.disable = 0;
    desc.masked = 0;

    /* cache line */
    cl.data[0] = 0xDEAD;
    cl.data[7] = 0xBEEF;

    /* event union */
    ev.type = 1;
    ev.mouse.x = 100;
    ev.mouse.y = 200;

    /* min/max */
    a = min(arr[0], arr[4]);
    b = max(arr[1], arr[3]);
    c = (int)ARRAY_SIZE(arr);

    /* do_div */
    n = 1000000UL;
    rem = do_div(n, 7);

    /* hash */
    hash = compute_hash(42);

    /* READ/WRITE_ONCE */
    WRITE_ONCE(desc.flags, 0xFF);
    {
        unsigned long f;
        f = READ_ONCE(desc.flags);
        (void)f;
    }

    /* barriers */
    barrier();
    smp_mb();

    /* weak function */
    optional_init();

    if (likely(a == 100) && desc.depth == 1)
        return 0;
    return a + b + c + (int)rem + hash;
}
