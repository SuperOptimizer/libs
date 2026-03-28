/* SPDX-License-Identifier: GPL-2.0 */
/* Stub math64.h for free-cc kernel compilation testing */
#ifndef _LINUX_MATH64_H
#define _LINUX_MATH64_H

#include <linux/types.h>

static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

static inline u64 div_u64(u64 dividend, u32 divisor)
{
    u32 remainder;
    return div_u64_rem(dividend, divisor, &remainder);
}

static inline s64 div_s64(s64 dividend, s32 divisor)
{
    return dividend / divisor;
}

/* mul_u64_u32_div: declared but not defined - let lib/math/div64.c provide it */
#ifndef __mul_u64_u32_div_defined
extern u64 mul_u64_u32_div(u64 a, u32 mul, u32 divisor);
#endif

static inline u64 mul_u64_u64_div_u64(u64 a, u64 b, u64 c)
{
    return (a * b) / c;
}

static inline u64 div64_u64(u64 dividend, u64 divisor)
{
    return dividend / divisor;
}

static inline u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

static inline s64 div64_s64(s64 dividend, s64 divisor)
{
    return dividend / divisor;
}

static inline s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

#define DIV64_U64_ROUND_UP(ll, d) \
    ({ u64 _tmp = (d); div64_u64((ll) + _tmp - 1, _tmp); })

#endif /* _LINUX_MATH64_H */
