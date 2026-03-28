/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#define USHRT_MAX  ((unsigned short)~0U)
#define SHRT_MAX   ((short)(USHRT_MAX >> 1))
#define SHRT_MIN   ((short)(-SHRT_MAX - 1))
#define INT_MAX    ((int)(~0U >> 1))
#define INT_MIN    (-INT_MAX - 1)
#define UINT_MAX   (~0U)
#define LONG_MAX   ((long)(~0UL >> 1))
#define LONG_MIN   (-LONG_MAX - 1L)
#define ULONG_MAX  (~0UL)
#define LLONG_MAX  ((long long)(~0ULL >> 1))
#define LLONG_MIN  (-LLONG_MAX - 1LL)
#define ULLONG_MAX (~0ULL)

#define CHAR_BIT   8
#define SCHAR_MAX  127
#define SCHAR_MIN  (-128)
#define UCHAR_MAX  255

#define U8_MAX     ((u8)~(u8)0)
#define S8_MAX     ((s8)(U8_MAX >> 1))
#define U16_MAX    ((u16)~(u16)0)
#define S16_MAX    ((s16)(U16_MAX >> 1))
#define U32_MAX    ((u32)~(u32)0)
#define S32_MAX    ((s32)(U32_MAX >> 1))
#define U64_MAX    ((u64)~(u64)0)
#define S64_MAX    ((s64)(U64_MAX >> 1))

#define SIZE_MAX   (~(size_t)0)

#define PATH_MAX   4096
#define PAGE_SIZE  4096

#endif /* _LINUX_LIMITS_H */
