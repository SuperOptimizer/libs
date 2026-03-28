/* SPDX-License-Identifier: GPL-2.0 */
/* Stub math.h for free-cc kernel compilation testing */
#ifndef _LINUX_MATH_H
#define _LINUX_MATH_H

#include <linux/types.h>

#define DIV_ROUND_UP(n, d)       (((n) + (d) - 1) / (d))
#define DIV_ROUND_DOWN_ULL(ll, d) ((ll) / (d))
#define DIV_ROUND_UP_ULL(ll, d)  DIV_ROUND_UP((unsigned long long)(ll), (unsigned long long)(d))
#define DIV_ROUND_CLOSEST(x, d)  (((x) + ((d) / 2)) / (d))

#define roundup(x, y)    ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y)  ((x) - ((x) % (y)))
#define round_up(x, y)   roundup(x, y)
#define round_down(x, y) rounddown(x, y)

#define abs(x) ((x) < 0 ? -(x) : (x))

#define mult_frac(x, numer, denom) \
    ((x) / (denom) * (numer) + ((x) % (denom)) * (numer) / (denom))

static inline u64 mul_u64_u32_div(u64 a, u32 mul, u32 divisor)
{
    return (a * mul) / divisor;
}

#endif /* _LINUX_MATH_H */
