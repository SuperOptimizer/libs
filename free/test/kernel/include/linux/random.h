/* SPDX-License-Identifier: GPL-2.0 */
/* Stub random.h for free-cc kernel compilation testing */
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/types.h>

extern u32 get_random_u32(void);
extern u64 get_random_u64(void);

#define get_random_u32_below(ceil) (get_random_u32() % (ceil))
#define get_random_u32_inclusive(lo, hi) ((lo) + get_random_u32_below((hi) - (lo) + 1))
#define prandom_u32_max(ceil) get_random_u32_below(ceil)

#endif /* _LINUX_RANDOM_H */
