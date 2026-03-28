/* SPDX-License-Identifier: GPL-2.0 */
/* Stub reciprocal_div.h for free-cc kernel compilation testing */
#ifndef _LINUX_RECIPROCAL_DIV_H
#define _LINUX_RECIPROCAL_DIV_H

#include <linux/types.h>

struct reciprocal_value {
    u32 m;
    u8 sh1;
    u8 sh2;
};

struct reciprocal_value_adv {
    u32 m;
    u8 sh;
    u8 exp;
    u8 is_wide_m;
};

extern struct reciprocal_value reciprocal_value(u32 d);
extern struct reciprocal_value_adv reciprocal_value_adv(u32 d, u8 prec);

static inline u32 reciprocal_divide(u32 a, struct reciprocal_value R)
{
    u32 t = (u32)(((u64)a * R.m) >> 32);
    return (t + ((a - t) >> R.sh1)) >> R.sh2;
}

#endif /* _LINUX_RECIPROCAL_DIV_H */
