/* EXPECTED: 0 */
/* Test kernel-style bit operations */

#define BITS_PER_LONG 64
#define BIT(nr) (1UL << (nr))
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)

#define GENMASK(h, l) \
    (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#define FIELD_GET(mask, reg) \
    ((typeof(mask))(((reg) & (mask)) >> __builtin_ctzl(mask)))

#define FIELD_PREP(mask, val) \
    ((typeof(mask))(((val) << __builtin_ctzl(mask)) & (mask)))

static inline int fls(unsigned int x)
{
    if (x == 0)
        return 0;
    return 32 - __builtin_clz(x);
}

static inline int ffs_func(unsigned int x)
{
    if (x == 0)
        return 0;
    return __builtin_ffs(x);
}

static inline int hweight32(unsigned int w)
{
    return __builtin_popcount(w);
}

static inline unsigned long hweight64(unsigned long w)
{
    return __builtin_popcountl(w);
}

/* Bitmap operations */
static inline void set_bit(int nr, unsigned long *addr)
{
    addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

static inline void clear_bit(int nr, unsigned long *addr)
{
    addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}

static inline int test_bit(int nr, const unsigned long *addr)
{
    return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG - 1)));
}

int main(void)
{
    unsigned long bitmap[2];
    unsigned long mask;

    /* Test BIT macro */
    if (BIT(0) != 1UL) return 1;
    if (BIT(3) != 8UL) return 2;
    if (BIT(63) != (1UL << 63)) return 3;

    /* Test GENMASK */
    mask = GENMASK(7, 0);
    if (mask != 0xFFUL) return 4;

    mask = GENMASK(15, 8);
    if (mask != 0xFF00UL) return 5;

    /* Test FIELD_GET/FIELD_PREP */
    mask = GENMASK(11, 8);
    if (FIELD_GET(mask, 0xA00UL) != 0xAUL) return 6;
    if (FIELD_PREP(mask, 0xBUL) != 0xB00UL) return 7;

    /* Test fls */
    if (fls(0) != 0) return 8;
    if (fls(1) != 1) return 9;
    if (fls(128) != 8) return 10;

    /* Test popcount */
    if (hweight32(0xFF) != 8) return 11;
    if (hweight32(0) != 0) return 12;

    /* Test bitmap */
    bitmap[0] = 0;
    bitmap[1] = 0;

    set_bit(5, bitmap);
    if (!test_bit(5, bitmap)) return 13;
    if (test_bit(6, bitmap)) return 14;

    set_bit(70, bitmap);
    if (!test_bit(70, bitmap)) return 15;

    clear_bit(5, bitmap);
    if (test_bit(5, bitmap)) return 16;

    return 0;
}
