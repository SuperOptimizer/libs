/* SPDX-License-Identifier: GPL-2.0 */
/* Stub jhash.h for free-cc kernel compilation testing */
#ifndef _LINUX_JHASH_H
#define _LINUX_JHASH_H

#include <linux/types.h>
#include <linux/unaligned.h>

static inline u32 __jhash_mix(u32 a, u32 b, u32 c)
{
    a -= c; a ^= (c << 4) | (c >> 28); c += b;
    b -= a; b ^= (a << 6) | (a >> 26); a += c;
    c -= b; c ^= (b << 8) | (b >> 24); b += a;
    a -= c; a ^= (c << 16) | (c >> 16); c += b;
    b -= a; b ^= (a << 19) | (a >> 13); a += c;
    c -= b; c ^= (b << 4) | (b >> 28); b += a;
    (void)a; (void)b;
    return c;
}

static inline u32 jhash(const void *key, u32 length, u32 initval)
{
    (void)key; (void)length;
    return initval;
}

static inline u32 jhash2(const u32 *k, u32 length, u32 initval)
{
    (void)k; (void)length;
    return initval;
}

#define JHASH_INITVAL 0xdeadbeef

static inline u32 jhash_1word(u32 a, u32 initval)
{
    return a ^ initval;
}

static inline u32 jhash_2words(u32 a, u32 b, u32 initval)
{
    return a ^ b ^ initval;
}

static inline u32 jhash_3words(u32 a, u32 b, u32 c, u32 initval)
{
    return a ^ b ^ c ^ initval;
}

#endif /* _LINUX_JHASH_H */
