/* Kernel-style test 8: edge cases */

#define __always_inline inline __attribute__((always_inline))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier()   asm volatile("" ::: "memory")

/* ---- designated initializer-like patterns ---- */
struct ops {
    int (*open)(void);
    int (*close)(void);
    int (*read)(void *, int);
    int (*write)(const void *, int);
};

static int my_open(void) { return 0; }
static int my_close(void) { return 0; }
static int my_read(void *buf, int len) { (void)buf; (void)len; return 0; }
static int my_write(const void *buf, int len) { (void)buf; (void)len; return 0; }

/* ---- function pointers in structs ---- */
static struct ops ops = {
    my_open,
    my_close,
    my_read,
    my_write
};

/* ---- nested typeof ---- */
#define typeof_member(T, m) typeof(((T *)0)->m)

/* ---- variadic macro patterns ---- */
#define pr_fmt(fmt) "test: " fmt

/* ---- multiple attributes on same declaration ---- */
static int __attribute__((unused)) __attribute__((aligned(8))) global_var;

/* ---- enum with attribute ---- */
enum power_state {
    POWER_OFF = 0,
    POWER_STANDBY = 1,
    POWER_ON = 2,
    POWER_MAX = 3
};

/* ---- complex structure layout ---- */
struct __attribute__((packed)) hw_reg {
    volatile unsigned int ctrl;
    volatile unsigned int status;
    volatile unsigned int data;
    volatile unsigned int mask;
};

/* ---- function attribute placement ---- */
__attribute__((noinline))
static int do_work(int x) {
    return x * x + 1;
}

/* ---- unsigned/signed conversions ---- */
static __always_inline unsigned int get_bits(unsigned int val,
    int start, int len) {
    return (val >> start) & ((1U << len) - 1);
}

static __always_inline unsigned int set_bits(unsigned int val,
    int start, int len, unsigned int bits) {
    unsigned int mask;
    mask = ((1U << len) - 1) << start;
    return (val & ~mask) | ((bits << start) & mask);
}

/* ---- BIT macros ---- */
#define BIT(nr) (1UL << (nr))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))

/* ---- for_each style loop ---- */
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = 0; (bit) < (size); (bit)++) \
        if (!((*(addr) >> (bit)) & 1)) continue; else

/* ---- complex initializer ---- */
struct config {
    const char *name;
    int value;
    unsigned long flags;
};

static struct config configs[] = {
    { "debug", 0, 0 },
    { "verbose", 1, BIT(0) },
    { "trace", 0, BIT(1) | BIT(2) },
};

/* ---- test ---- */
int main(void) {
    struct hw_reg reg;
    unsigned int val;
    int result;
    unsigned long bits;
    int bit;
    typeof_member(struct hw_reg, ctrl) ctrl_copy;
    int work;
    int num_configs;

    /* register access */
    reg.ctrl = 0xDEADBEEF;
    reg.status = 0;
    reg.data = 0x1234;
    reg.mask = 0xFFFF;

    /* bit manipulation */
    val = get_bits(reg.ctrl, 8, 8);
    val = set_bits(reg.ctrl, 16, 8, 0x42);

    /* BIT and GENMASK */
    bits = BIT(5) | BIT(10) | BIT(15);
    result = 0;
    for_each_set_bit(bit, &bits, 20) {
        result = result + bit;
    }

    /* function pointer call */
    ops.open();
    ops.close();

    /* typeof_member */
    ctrl_copy = reg.ctrl;

    /* attribute function */
    work = do_work(7);

    /* config array */
    num_configs = (int)(sizeof(configs) / sizeof(configs[0]));

    barrier();

    if (likely(result == 30) && val != 0)
        return 0;
    return result + work + num_configs + (int)ctrl_copy;
}
