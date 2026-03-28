/* SPDX-License-Identifier: GPL-2.0 */
/* Stub net/checksum.h for free-cc kernel compilation testing */
#ifndef _NET_CHECKSUM_H
#define _NET_CHECKSUM_H

#include <linux/types.h>

typedef __u16 __sum16;
typedef __u32 __wsum;

static inline __wsum csum_add(__wsum csum, __wsum addend)
{
    __u32 res = (__u32)csum + (__u32)addend;
    return (__wsum)(res + (res < (__u32)csum));
}

static inline __wsum csum_sub(__wsum csum, __wsum addend)
{
    return csum_add(csum, ~addend);
}

extern __wsum csum_partial(const void *buff, int len, __wsum wsum);
extern __sum16 csum_fold(__wsum csum);

static inline __wsum csum_block_add(__wsum csum, __wsum csum2, int offset)
{
    (void)offset;
    return csum_add(csum, csum2);
}

extern __wsum csum_partial_copy_nocheck(const void *src, void *dst,
                                        int len);

#endif /* _NET_CHECKSUM_H */
