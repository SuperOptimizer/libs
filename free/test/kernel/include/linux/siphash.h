/* SPDX-License-Identifier: GPL-2.0 */
/* Stub siphash.h for free-cc kernel compilation testing */
#ifndef _LINUX_SIPHASH_H
#define _LINUX_SIPHASH_H

#include <linux/types.h>

typedef struct {
    u64 key[2];
} siphash_key_t;

static inline u64 siphash(const void *data, size_t len,
                           const siphash_key_t *key)
{
    (void)data; (void)len; (void)key;
    return 0;
}

static inline u64 siphash_1u64(u64 a, const siphash_key_t *key)
{
    (void)a; (void)key;
    return 0;
}

static inline u64 siphash_2u64(u64 a, u64 b, const siphash_key_t *key)
{
    (void)a; (void)b; (void)key;
    return 0;
}

#endif /* _LINUX_SIPHASH_H */
