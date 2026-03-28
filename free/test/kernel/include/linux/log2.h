/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOG2_H
#define _LINUX_LOG2_H

#include <linux/types.h>
#include <linux/bitops.h>

static inline int ilog2_u64(u64 n) { return fls64(n) - 1; }
static inline int ilog2_u32(u32 n) { return fls(n) - 1; }

#define ilog2(n) \
    (sizeof(n) <= 4 ? ilog2_u32(n) : ilog2_u64(n))

#define roundup_pow_of_two(n) \
    (1UL << (ilog2((n) - 1) + 1))

#define rounddown_pow_of_two(n) \
    (1UL << ilog2(n))

#define order_base_2(n) ilog2(roundup_pow_of_two(n))

#endif
