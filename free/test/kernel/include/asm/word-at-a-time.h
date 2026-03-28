/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <linux/types.h>

/* Minimal word-at-a-time implementation for string.c */

struct word_at_a_time {
    unsigned long one_bits;
    unsigned long high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { 0x0101010101010101UL, 0x8080808080808080UL }

static inline unsigned long has_zero(unsigned long val, unsigned long *bits, const struct word_at_a_time *c)
{
    unsigned long mask = ((val) - c->one_bits) & ~(val) & c->high_bits;
    *bits = mask;
    return mask;
}

static inline unsigned long prep_zero_mask(unsigned long val, unsigned long bits, const struct word_at_a_time *c)
{
    (void)val;
    (void)c;
    return bits;
}

static inline unsigned long create_zero_mask(unsigned long bits)
{
    bits = (bits - 1) & ~bits;
    return bits >> 7;
}

static inline unsigned long find_zero(unsigned long mask)
{
    unsigned long count = 0;
    while (mask) {
        count++;
        mask >>= 8;
    }
    return count;
}

#define zero_bytemask(mask) (~1UL << (find_zero(mask) * 8))

#endif /* _ASM_WORD_AT_A_TIME_H */
