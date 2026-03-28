/* Kernel-style test 6: advanced GCC builtins, type tricks, complex patterns */

#define __always_inline inline __attribute__((always_inline))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier()   asm volatile("" ::: "memory")

/* ---- type checking ---- */
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/* ---- builtin constant check ---- */
#define __is_constexpr(x) __builtin_constant_p(x)

/* ---- clz/ctz/popcount ---- */
#define fls(x) ((x) ? (int)(sizeof(int)*8 - __builtin_clz(x)) : 0)
#define ffs(x) __builtin_ffs(x)
#define hweight32(x) __builtin_popcount(x)
#define hweight64(x) __builtin_popcountll(x)

/* ---- byte swap ---- */
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
#define swab64(x) __builtin_bswap64(x)

/* cpu_to_be / be_to_cpu (noop on big-endian, bswap on little-endian) */
/* Assume little-endian (aarch64 default) */
#define cpu_to_be32(x) swab32(x)
#define be32_to_cpu(x) swab32(x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)

/* ---- overflow checking ---- */
#define check_add_overflow(a, b, d) __builtin_add_overflow(a, b, d)
#define check_sub_overflow(a, b, d) __builtin_sub_overflow(a, b, d)
#define check_mul_overflow(a, b, d) __builtin_mul_overflow(a, b, d)

/* ---- ALIGN macros ---- */
#define ALIGN(x, a)       __ALIGN_KERNEL(x, a)
#define __ALIGN_KERNEL(x, a)  __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))

#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)

/* ---- page size ---- */
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

/* ---- power of 2 check ---- */
#define is_power_of_2(n) ((n) != 0 && ((n) & ((n) - 1)) == 0)

/* ---- round up/down ---- */
#define round_up(x, y) ({ \
    typeof(x) __x = (x); \
    typeof(y) __y = (y); \
    ((__x + __y - 1) / __y) * __y; })

#define round_down(x, y) ({ \
    typeof(x) __x = (x); \
    typeof(y) __y = (y); \
    __x - (__x % __y); })

/* ---- DIV_ROUND_UP ---- */
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* ---- FIELD_SIZEOF ---- */
#define FIELD_SIZEOF(t, f) (sizeof(((t *)0)->f))

/* ---- struct with flexible array member ---- */
struct msg_header {
    unsigned int type;
    unsigned int len;
    char data[];
};

/* ---- offsetof ---- */
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

/* ---- container_of ---- */
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

/* ---- ilog2 approximation ---- */
static __always_inline int ilog2(unsigned long n) {
    if (n == 0) return -1;
    return (int)(sizeof(unsigned long) * 8 - 1 - __builtin_clzll(n));
}

/* ---- test structures ---- */
struct sk_buff {
    unsigned int len;
    unsigned int data_len;
    unsigned short protocol;
    unsigned short vlan_tci;
    unsigned char *data;
    unsigned char *head;
    unsigned char *tail;
    unsigned char *end;
};

struct net_device {
    char name[16];
    unsigned int mtu;
    unsigned long features;
    struct sk_buff *rx_queue;
};

int main(void) {
    unsigned long addr;
    unsigned long aligned_addr;
    unsigned long page;
    int result;
    int bits;
    unsigned int be_val;
    unsigned int le_val;
    int log_val;
    int over;
    int a, b, c;
    struct sk_buff skb;
    struct net_device dev;
    unsigned int *data_ptr;

    /* ALIGN */
    addr = 0x1234;
    aligned_addr = ALIGN(addr, PAGE_SIZE);
    page = PAGE_ALIGN(0xABCD);

    /* IS_ALIGNED */
    result = IS_ALIGNED(0x1000, PAGE_SIZE);

    /* is_power_of_2 */
    result = is_power_of_2(256);

    /* round_up / round_down */
    a = (int)round_up(17, 8);
    b = (int)round_down(17, 8);

    /* DIV_ROUND_UP */
    c = (int)DIV_ROUND_UP(100, 7);

    /* bit operations */
    bits = fls(0xFF);
    bits += ffs(0x80);
    bits += hweight32(0xAAAAAAAA);

    /* byte swap */
    be_val = cpu_to_be32(0x12345678);
    le_val = le32_to_cpu(0xDEADBEEF);

    /* ilog2 */
    log_val = ilog2(1024);

    /* overflow checking */
    {
        int x, y, z;
        x = 2000000000;
        y = 2000000000;
        over = check_add_overflow(x, y, &z);
    }

    /* FIELD_SIZEOF */
    result = (int)FIELD_SIZEOF(struct sk_buff, protocol);

    /* offsetof */
    result += (int)offsetof(struct net_device, mtu);

    /* container_of */
    skb.len = 1500;
    skb.data_len = 0;
    data_ptr = &skb.len;

    /* complex address calculation */
    dev.mtu = 1500;
    dev.features = 0xFFFF;

    if (likely(aligned_addr > addr) && is_power_of_2(PAGE_SIZE))
        return 0;
    return bits + (int)be_val + log_val + result + over;
}
