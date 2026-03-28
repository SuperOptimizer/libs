/* Kernel pattern: bit manipulation operations */
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/kernel.h>

#define MY_FLAG_ACTIVE  BIT(0)
#define MY_FLAG_READY   BIT(1)
#define MY_FLAG_ERROR   BIT(2)
#define MY_FLAG_DONE    BIT(3)

struct flags_holder {
    unsigned long flags;
    unsigned long bitmask[4];
};

static void set_flag(struct flags_holder *fh, unsigned long flag)
{
    fh->flags |= flag;
}

static void clear_flag(struct flags_holder *fh, unsigned long flag)
{
    fh->flags &= ~flag;
}

static int test_flag(const struct flags_holder *fh, unsigned long flag)
{
    return !!(fh->flags & flag);
}

static unsigned long count_set_bits(unsigned long val)
{
    unsigned long count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

static unsigned long rotate_left(unsigned long val, unsigned int n)
{
    unsigned int bits = sizeof(unsigned long) * 8;
    n %= bits;
    return (val << n) | (val >> (bits - n));
}

static unsigned long rotate_right(unsigned long val, unsigned int n)
{
    unsigned int bits = sizeof(unsigned long) * 8;
    n %= bits;
    return (val >> n) | (val << (bits - n));
}

static unsigned long extract_bits(unsigned long val, int start, int len)
{
    unsigned long mask = (1UL << len) - 1;
    return (val >> start) & mask;
}

void test_bitops(void)
{
    struct flags_holder fh;
    unsigned long bits;
    unsigned long rotated;
    unsigned long extracted;

    fh.flags = 0;
    set_flag(&fh, MY_FLAG_ACTIVE | MY_FLAG_READY);
    clear_flag(&fh, MY_FLAG_READY);

    bits = count_set_bits(0xDEADBEEFUL);
    rotated = rotate_left(0xFF00FF00UL, 8);
    extracted = extract_bits(0xABCD1234UL, 8, 8);

    (void)test_flag(&fh, MY_FLAG_ACTIVE);
    (void)bits;
    (void)rotated;
    (void)extracted;
    (void)rotate_right;
}
