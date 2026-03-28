/* Kernel pattern: 64-bit math operations */
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/kernel.h>

static u64 my_div64(u64 dividend, u32 divisor)
{
    u32 remainder;
    return div_u64_rem(dividend, divisor, &remainder);
}

static s64 my_sdiv64(s64 dividend, s32 divisor)
{
    return div_s64(dividend, divisor);
}

static u64 mul_u64_checked(u64 a, u64 b, int *overflow)
{
    u64 result = a * b;
    if (a != 0 && result / a != b)
        *overflow = 1;
    else
        *overflow = 0;
    return result;
}

static u32 log2_of(u64 val)
{
    u32 result = 0;
    while (val > 1) {
        val >>= 1;
        result++;
    }
    return result;
}

static u64 power_of_2(u32 exp)
{
    if (exp >= 64)
        return 0;
    return 1ULL << exp;
}

static u64 align_up(u64 val, u64 alignment)
{
    u64 mask = alignment - 1;
    return (val + mask) & ~mask;
}

static u64 align_down(u64 val, u64 alignment)
{
    return val & ~(alignment - 1);
}

void test_math64(void)
{
    u64 q;
    s64 sq;
    u64 mul;
    u32 lg;
    u64 p;
    u64 au, ad;
    int ovf;

    q = my_div64(1000000000ULL, 7);
    sq = my_sdiv64(-999999LL, 100);
    mul = mul_u64_checked(123456ULL, 789012ULL, &ovf);
    lg = log2_of(4096);
    p = power_of_2(20);
    au = align_up(4097, 4096);
    ad = align_down(8191, 4096);

    (void)q; (void)sq; (void)mul; (void)lg;
    (void)p; (void)au; (void)ad;
}
