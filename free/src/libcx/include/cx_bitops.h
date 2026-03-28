/*
 * cx_bitops.h - Bit manipulation utilities (header-only).
 * Part of libcx. Pure C89.
 */

#ifndef CX_BITOPS_H
#define CX_BITOPS_H

/* Count leading zeros (32-bit) */
static int cx_clz(unsigned long x)
{
    int n = 0;
    if (x == 0) return 32;
    if ((x & 0xFFFF0000UL) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000UL) == 0) { n += 8;  x <<= 8;  }
    if ((x & 0xF0000000UL) == 0) { n += 4;  x <<= 4;  }
    if ((x & 0xC0000000UL) == 0) { n += 2;  x <<= 2;  }
    if ((x & 0x80000000UL) == 0) { n += 1; }
    return n;
}

/* Count trailing zeros (32-bit) */
static int cx_ctz(unsigned long x)
{
    int n = 0;
    if (x == 0) return 32;
    if ((x & 0x0000FFFFUL) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FFUL) == 0) { n += 8;  x >>= 8;  }
    if ((x & 0x0000000FUL) == 0) { n += 4;  x >>= 4;  }
    if ((x & 0x00000003UL) == 0) { n += 2;  x >>= 2;  }
    if ((x & 0x00000001UL) == 0) { n += 1; }
    return n;
}

/* Population count (number of set bits, 32-bit) */
static int cx_popcount(unsigned long x)
{
    x = x - ((x >> 1) & 0x55555555UL);
    x = (x & 0x33333333UL) + ((x >> 2) & 0x33333333UL);
    x = (x + (x >> 4)) & 0x0F0F0F0FUL;
    return (int)((x * 0x01010101UL) >> 24);
}

/* Byte swap 16-bit */
static unsigned int cx_bswap16(unsigned int x)
{
    return ((x & 0xFF00u) >> 8) | ((x & 0x00FFu) << 8);
}

/* Byte swap 32-bit */
static unsigned long cx_bswap32(unsigned long x)
{
    x = ((x & 0xFF00FF00UL) >> 8) | ((x & 0x00FF00FFUL) << 8);
    return (x >> 16) | (x << 16);
}

/* Byte swap 64-bit (using two 32-bit swaps) */
static unsigned long cx_bswap64(unsigned long x)
{
    unsigned long hi, lo;
    lo = x & 0xFFFFFFFFUL;
    hi = (x >> 32) & 0xFFFFFFFFUL;
    lo = cx_bswap32(lo);
    hi = cx_bswap32(hi);
    return (lo << 32) | hi;
}

/* Check if x is a power of 2 */
static int cx_is_pow2(unsigned long x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

/* Round up to the next power of 2 (32-bit) */
static unsigned long cx_next_pow2(unsigned long x)
{
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/* Align x up to alignment a (a must be power of 2) */
static unsigned long cx_align_up(unsigned long x, unsigned long a)
{
    return (x + a - 1) & ~(a - 1);
}

#endif
