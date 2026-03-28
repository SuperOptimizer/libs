/* Kernel-style test 4: inline asm, volatile, complex macros */

#define __always_inline inline __attribute__((always_inline))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier()   asm volatile("" ::: "memory")

/* ---- READ_ONCE / WRITE_ONCE with volatile cast ---- */
#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile typeof(x) *)&(x) = (val))

/* ---- memory-mapped I/O ---- */
static __always_inline void writel(unsigned int val, volatile void *addr) {
    asm volatile("str %w0, [%1]" : : "r" (val), "r" (addr));
    barrier();
}

static __always_inline unsigned int readl(const volatile void *addr) {
    unsigned int val;
    asm volatile("ldr %w0, [%1]" : "=r" (val) : "r" (addr));
    return val;
}

/* ---- atomic operations with inline asm ---- */
typedef struct { int counter; } atomic_t;

static __always_inline int atomic_read(const atomic_t *v) {
    return READ_ONCE(v->counter);
}

static __always_inline void atomic_set(atomic_t *v, int i) {
    WRITE_ONCE(v->counter, i);
}

static __always_inline int atomic_add_return(int i, atomic_t *v) {
    int result;
    int tmp;
    asm volatile(
        "1: ldaxr %w0, [%2]\n"
        "   add %w0, %w0, %w3\n"
        "   stlxr %w1, %w0, [%2]\n"
        "   cbnz %w1, 1b"
        : "=&r" (result), "=&r" (tmp)
        : "r" (&v->counter), "r" (i)
        : "memory");
    return result;
}

/* ---- cmpxchg ---- */
static __always_inline int cmpxchg(int *ptr, int old, int new) {
    int prev;
    int tmp;
    asm volatile(
        "1: ldaxr %w0, [%2]\n"
        "   cmp %w0, %w3\n"
        "   b.ne 2f\n"
        "   stlxr %w1, %w4, [%2]\n"
        "   cbnz %w1, 1b\n"
        "2:"
        : "=&r" (prev), "=&r" (tmp)
        : "r" (ptr), "r" (old), "r" (new)
        : "memory", "cc");
    return prev;
}

/* ---- bit operations ---- */
static __always_inline void set_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask;
    mask = 1UL << (nr & 63);
    asm volatile(
        "1: ldaxr x2, [%0]\n"
        "   orr x2, x2, %1\n"
        "   stlxr w3, x2, [%0]\n"
        "   cbnz w3, 1b"
        : : "r" (addr), "r" (mask) : "x2", "w3", "memory");
}

static __always_inline void clear_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask;
    mask = 1UL << (nr & 63);
    asm volatile(
        "1: ldaxr x2, [%0]\n"
        "   bic x2, x2, %1\n"
        "   stlxr w3, x2, [%0]\n"
        "   cbnz w3, 1b"
        : : "r" (addr), "r" (mask) : "x2", "w3", "memory");
}

static __always_inline int test_bit(int nr, const volatile unsigned long *addr) {
    return 1 & (*addr >> (nr & 63));
}

/* ---- offsetof and container_of ---- */
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

/* ---- BUG/WARN macros ---- */
#define BUG() do { __builtin_trap(); } while (0)
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)
#define WARN_ON(condition) ({ \
    int __ret_warn_on = !!(condition); \
    if (unlikely(__ret_warn_on)) \
        __builtin_trap(); \
    unlikely(__ret_warn_on); \
})

/* ---- test structures ---- */
struct __attribute__((packed)) pci_header {
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned short command;
    unsigned short status;
    unsigned char revision;
    unsigned char prog_if;
    unsigned char subclass;
    unsigned char class_code;
    unsigned char cache_line_size;
    unsigned char latency_timer;
    unsigned char header_type;
    unsigned char bist;
    unsigned int bar[6];
};

struct device_node {
    const char *name;
    const char *type;
    unsigned long flags;
    struct device_node *parent;
    struct device_node *child;
    struct device_node *sibling;
};

int main(void) {
    atomic_t counter;
    unsigned long bits;
    int old_val;
    int new_val;
    struct pci_header hdr;
    struct device_node root;
    struct device_node child;

    /* atomic operations */
    atomic_set(&counter, 0);
    atomic_add_return(5, &counter);
    BUG_ON(atomic_read(&counter) != 5);

    /* cmpxchg */
    old_val = 5;
    new_val = cmpxchg(&counter.counter, old_val, 10);

    /* bit operations */
    bits = 0;
    set_bit(3, &bits);
    set_bit(7, &bits);
    clear_bit(3, &bits);
    BUG_ON(!test_bit(7, &bits));

    /* WARN_ON */
    WARN_ON(bits == 0);

    /* packed struct access */
    hdr.vendor_id = 0x8086;
    hdr.device_id = 0x1234;
    hdr.command = 0x0007;
    hdr.bar[0] = 0xFE000000;

    /* READ_ONCE / WRITE_ONCE */
    WRITE_ONCE(root.flags, 0x1234);
    {
        unsigned long f;
        f = READ_ONCE(root.flags);
        BUG_ON(f != 0x1234);
    }

    /* barrier */
    barrier();

    /* device tree traversal */
    root.name = "soc";
    root.type = "simple-bus";
    root.child = &child;
    root.parent = (void *)0;
    root.sibling = (void *)0;

    child.name = "uart";
    child.type = "serial";
    child.parent = &root;
    child.child = (void *)0;
    child.sibling = (void *)0;

    if (likely(root.child != (void *)0))
        return 0;
    return atomic_read(&counter) + (int)bits;
}
