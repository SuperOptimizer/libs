/* SPDX-License-Identifier: GPL-2.0 */
/* Stub wordpart.h for free-cc kernel compilation testing */
#ifndef _LINUX_WORDPART_H
#define _LINUX_WORDPART_H

#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((u32)(n))

#define REPEAT_BYTE(x)  ((unsigned long)((x) * (~0UL / 0xff)))

#endif /* _LINUX_WORDPART_H */
