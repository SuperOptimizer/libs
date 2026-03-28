/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITS_H
#define __LINUX_BITS_H

#define BIT(nr)          (1UL << (nr))
#define BIT_ULL(nr)      (1ULL << (nr))
#define BIT_MASK(nr)     (1UL << ((nr) % 64))
#define BIT_WORD(nr)     ((nr) / 64)
#define BITS_PER_LONG    64
#define BITS_PER_LONG_LONG 64

#define GENMASK(h, l) \
    (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (64 - 1 - (h))))

#endif /* __LINUX_BITS_H */
