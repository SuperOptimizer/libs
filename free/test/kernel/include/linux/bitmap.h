/* SPDX-License-Identifier: GPL-2.0 */
/* Stub bitmap.h for free-cc kernel compilation testing */
#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/find.h>

struct device;

/* Bitmap operations */
extern int __bitmap_equal(const unsigned long *bitmap1,
                          const unsigned long *bitmap2, unsigned int nbits);
extern int __bitmap_or_equal(const unsigned long *src1,
                             const unsigned long *src2,
                             const unsigned long *src3, unsigned int nbits);
extern void __bitmap_complement(unsigned long *dst, const unsigned long *src,
                                unsigned int nbits);
extern void __bitmap_shift_right(unsigned long *dst, const unsigned long *src,
                                 unsigned int shift, unsigned int nbits);
extern void __bitmap_shift_left(unsigned long *dst, const unsigned long *src,
                                unsigned int shift, unsigned int nbits);
extern void __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                         const unsigned long *bitmap2, unsigned int nbits);
extern void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                        const unsigned long *bitmap2, unsigned int nbits);
extern void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                         const unsigned long *bitmap2, unsigned int nbits);
extern int __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
                           const unsigned long *bitmap2, unsigned int nbits);
extern int __bitmap_intersects(const unsigned long *bitmap1,
                               const unsigned long *bitmap2, unsigned int nbits);
extern int __bitmap_subset(const unsigned long *bitmap1,
                           const unsigned long *bitmap2, unsigned int nbits);
extern unsigned int __bitmap_weight(const unsigned long *bitmap, unsigned int nbits);
extern unsigned int __bitmap_weight_and(const unsigned long *bitmap1,
                                        const unsigned long *bitmap2, unsigned int nbits);
extern void __bitmap_set(unsigned long *map, unsigned int start, int len);
extern void __bitmap_clear(unsigned long *map, unsigned int start, int len);

extern unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
        unsigned long size, unsigned long start, unsigned int nr,
        unsigned long align_mask, unsigned long align_offset);

extern int bitmap_parse(const char *buf, unsigned int buflen,
                        unsigned long *dst, int nbits);
extern int bitmap_parselist(const char *buf, unsigned long *maskp, int nmaskbits);

static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memset(dst, 0, len);
}

static inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memset(dst, 0xff, len);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
                                unsigned int nbits)
{
    unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
    memcpy(dst, src, len);
}

static inline int bitmap_and(unsigned long *dst, const unsigned long *src1,
                              const unsigned long *src2, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG) {
        *dst = *src1 & *src2;
        return (*dst != 0);
    }
    return __bitmap_and(dst, src1, src2, nbits);
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *src1,
                              const unsigned long *src2, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        *dst = *src1 | *src2;
    else
        __bitmap_or(dst, src1, src2, nbits);
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *src1,
                               const unsigned long *src2, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        *dst = *src1 ^ *src2;
    else
        __bitmap_xor(dst, src1, src2, nbits);
}

static inline int bitmap_empty(const unsigned long *src, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        return ! (*src & ((nbits == BITS_PER_LONG) ? ~0UL : (1UL << nbits) - 1));
    return find_first_bit(src, nbits) == nbits;
}

static inline int bitmap_full(const unsigned long *src, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        return ! (~(*src) & ((nbits == BITS_PER_LONG) ? ~0UL : (1UL << nbits) - 1));
    return find_first_zero_bit(src, nbits) == nbits;
}

static inline int bitmap_equal(const unsigned long *src1,
                                const unsigned long *src2, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        return ! ((*src1 ^ *src2) & ((nbits == BITS_PER_LONG) ? ~0UL : (1UL << nbits) - 1));
    return __bitmap_equal(src1, src2, nbits);
}

static inline int bitmap_subset(const unsigned long *src1,
                                 const unsigned long *src2, unsigned int nbits)
{
    if (nbits <= BITS_PER_LONG)
        return ! ((*src1 & ~(*src2)) & ((nbits == BITS_PER_LONG) ? ~0UL : (1UL << nbits) - 1));
    return __bitmap_subset(src1, src2, nbits);
}

static inline unsigned int bitmap_weight(const unsigned long *src, unsigned int nbits)
{
    return __bitmap_weight(src, nbits);
}

static inline void bitmap_set(unsigned long *map, unsigned int start,
                               unsigned int nbits)
{
    __bitmap_set(map, start, nbits);
}

static inline void bitmap_clear(unsigned long *map, unsigned int start,
                                 unsigned int nbits)
{
    __bitmap_clear(map, start, nbits);
}

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

#define small_const_nbits(nbits) \
    (__builtin_constant_p(nbits) && (nbits) <= BITS_PER_LONG && (nbits) > 0)

#endif /* __LINUX_BITMAP_H */
