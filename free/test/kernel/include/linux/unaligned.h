/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNALIGNED_H
#define _LINUX_UNALIGNED_H

#include <linux/types.h>

static inline u16 get_unaligned_le16(const void *p)
{
    const u8 *b = (const u8 *)p;
    return b[0] | (b[1] << 8);
}

static inline u32 get_unaligned_le32(const void *p)
{
    const u8 *b = (const u8 *)p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24);
}

static inline u64 get_unaligned_le64(const void *p)
{
    const u8 *b = (const u8 *)p;
    return (u64)get_unaligned_le32(b) | ((u64)get_unaligned_le32(b+4) << 32);
}

static inline void put_unaligned_le16(u16 val, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = val;
    b[1] = val >> 8;
}

static inline void put_unaligned_le32(u32 val, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = val;
    b[1] = val >> 8;
    b[2] = val >> 16;
    b[3] = val >> 24;
}

#endif /* _LINUX_UNALIGNED_H */
