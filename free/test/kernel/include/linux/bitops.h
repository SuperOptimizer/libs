/* SPDX-License-Identifier: GPL-2.0 */
/* Stub bitops.h for free-cc kernel compilation testing */
#ifndef _LINUX_BITOPS_H
#define _LINUX_BITOPS_H

#include <linux/bits.h>
#include <linux/types.h>

#define BITS_PER_LONG      64
#define BITS_PER_LONG_LONG 64
#define BITS_PER_BYTE      8
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)

#define __KERNEL_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)  __KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(long))
#define BITS_TO_U64(nr)    __KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(u64))
#define BITS_TO_U32(nr)    __KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(u32))
#define BITS_TO_BYTES(nr)  __KERNEL_DIV_ROUND_UP(nr, BITS_PER_TYPE(char))
#define BYTES_TO_BITS(nb)  ((nb) * BITS_PER_BYTE)

extern unsigned int __sw_hweight8(unsigned int w);
extern unsigned int __sw_hweight16(unsigned int w);
extern unsigned int __sw_hweight32(unsigned int w);
extern unsigned long __sw_hweight64(u64 w);

#define hweight8(w)  __sw_hweight8(w)
#define hweight16(w) __sw_hweight16(w)
#define hweight32(w) __sw_hweight32(w)
#define hweight64(w) __sw_hweight64(w)
#define hweight_long(w) hweight64(w)

static inline unsigned long __ffs(unsigned long word)
{
    int num = 0;
    if ((word & 0xffffffff) == 0) { num += 32; word >>= 32; }
    if ((word & 0xffff) == 0) { num += 16; word >>= 16; }
    if ((word & 0xff) == 0) { num += 8; word >>= 8; }
    if ((word & 0xf) == 0) { num += 4; word >>= 4; }
    if ((word & 0x3) == 0) { num += 2; word >>= 2; }
    if ((word & 0x1) == 0) num += 1;
    return num;
}

static inline unsigned long __fls(unsigned long word)
{
    int num = BITS_PER_LONG - 1;
    if (!(word & (~0UL << 32))) { num -= 32; word <<= 32; }
    if (!(word & (~0UL << (BITS_PER_LONG-16)))) { num -= 16; word <<= 16; }
    if (!(word & (~0UL << (BITS_PER_LONG-8)))) { num -= 8; word <<= 8; }
    if (!(word & (~0UL << (BITS_PER_LONG-4)))) { num -= 4; word <<= 4; }
    if (!(word & (~0UL << (BITS_PER_LONG-2)))) { num -= 2; word <<= 2; }
    if (!(word & (~0UL << (BITS_PER_LONG-1)))) num -= 1;
    return num;
}

static inline int ffs(int x)
{
    if (!x) return 0;
    return __ffs((unsigned long)x) + 1;
}

static inline int fls(unsigned int x)
{
    int r;
    if (!x) return 0;
    r = 32;
    if (!(x & 0xffff0000u)) { x <<= 16; r -= 16; }
    if (!(x & 0xff000000u)) { x <<= 8; r -= 8; }
    if (!(x & 0xf0000000u)) { x <<= 4; r -= 4; }
    if (!(x & 0xc0000000u)) { x <<= 2; r -= 2; }
    if (!(x & 0x80000000u)) { r -= 1; }
    return r;
}

static inline int fls64(u64 x)
{
    if (!x) return 0;
    if (x & ~0xffffffffULL)
        return fls((unsigned int)(x >> 32)) + 32;
    return fls((unsigned int)x);
}

static inline unsigned long rol64(u64 word, unsigned int shift)
{
    return (word << (shift & 63)) | (word >> ((-shift) & 63));
}

static inline unsigned long ror64(u64 word, unsigned int shift)
{
    return (word >> (shift & 63)) | (word << ((-shift) & 63));
}

static inline unsigned int rol32(unsigned int word, unsigned int shift)
{
    return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

static inline unsigned int ror32(unsigned int word, unsigned int shift)
{
    return (word >> (shift & 31)) | (word << ((-shift) & 31));
}

/* Bit set/clear/test operations */
static inline void set_bit(int nr, unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}

static inline void clear_bit(int nr, unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline void change_bit(int nr, unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] ^= 1UL << (nr % BITS_PER_LONG);
}

static inline int test_bit(int nr, const unsigned long *addr)
{
    return 1UL & (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG));
}

static inline int test_and_set_bit(int nr, unsigned long *addr)
{
    int old = test_bit(nr, addr);
    set_bit(nr, addr);
    return old;
}

static inline int test_and_clear_bit(int nr, unsigned long *addr)
{
    int old = test_bit(nr, addr);
    clear_bit(nr, addr);
    return old;
}

#define __set_bit set_bit
#define __clear_bit clear_bit
#define __change_bit change_bit
#define __test_and_set_bit test_and_set_bit
#define __test_and_clear_bit test_and_clear_bit

/* is_power_of_2 */
static inline int is_power_of_2(unsigned long n)
{
    return (n != 0 && ((n & (n - 1)) == 0));
}

/* order_base_2 */
static inline int ilog2(unsigned long n)
{
    return fls64(n) - 1;
}

#define order_base_2(n) \
    ((n) > 1 ? ilog2((n) - 1) + 1 : 0)

/* for_each_set_bit */
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit((addr), (size)); \
         (bit) < (size); \
         (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size) \
    for ((bit) = find_first_zero_bit((addr), (size)); \
         (bit) < (size); \
         (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

#endif /* _LINUX_BITOPS_H */
