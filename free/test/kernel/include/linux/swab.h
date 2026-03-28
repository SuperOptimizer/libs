/* SPDX-License-Identifier: GPL-2.0 */
/* Stub swab.h for free-cc kernel compilation testing */
#ifndef _LINUX_SWAB_H
#define _LINUX_SWAB_H

#include <linux/types.h>

static inline u16 __swab16(u16 val)
{
    return (val << 8) | (val >> 8);
}

static inline u32 __swab32(u32 val)
{
    return ((val & (u32)0x000000ffUL) << 24) |
           ((val & (u32)0x0000ff00UL) << 8)  |
           ((val & (u32)0x00ff0000UL) >> 8)  |
           ((val & (u32)0xff000000UL) >> 24);
}

static inline u64 __swab64(u64 val)
{
    return ((val & (u64)0x00000000000000ffULL) << 56) |
           ((val & (u64)0x000000000000ff00ULL) << 40) |
           ((val & (u64)0x0000000000ff0000ULL) << 24) |
           ((val & (u64)0x00000000ff000000ULL) << 8)  |
           ((val & (u64)0x000000ff00000000ULL) >> 8)  |
           ((val & (u64)0x0000ff0000000000ULL) >> 24) |
           ((val & (u64)0x00ff000000000000ULL) >> 40) |
           ((val & (u64)0xff00000000000000ULL) >> 56);
}

#define swab16 __swab16
#define swab32 __swab32
#define swab64 __swab64

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
#define cpu_to_be16(x) __swab16(x)
#define cpu_to_be32(x) __swab32(x)
#define cpu_to_be64(x) __swab64(x)
#define be16_to_cpu(x) __swab16(x)
#define be32_to_cpu(x) __swab32(x)
#define be64_to_cpu(x) __swab64(x)

#endif /* _LINUX_SWAB_H */
