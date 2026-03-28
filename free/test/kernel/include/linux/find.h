/* SPDX-License-Identifier: GPL-2.0 */
/* Stub find.h for free-cc kernel compilation testing */
#ifndef _LINUX_FIND_H
#define _LINUX_FIND_H

#include <linux/types.h>

extern unsigned long _find_first_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_next_bit(const unsigned long *addr, unsigned long size,
                                    unsigned long offset);
extern unsigned long _find_next_zero_bit(const unsigned long *addr, unsigned long size,
                                         unsigned long offset);
extern unsigned long _find_last_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_first_and_bit(const unsigned long *addr1,
                                         const unsigned long *addr2, unsigned long size);

#define find_first_bit(addr, size)          _find_first_bit(addr, size)
#define find_first_zero_bit(addr, size)     _find_first_zero_bit(addr, size)
#define find_next_bit(addr, size, off)      _find_next_bit(addr, size, off)
#define find_next_zero_bit(addr, size, off) _find_next_zero_bit(addr, size, off)
#define find_last_bit(addr, size)           _find_last_bit(addr, size)
#define find_first_and_bit(a1, a2, size)    _find_first_and_bit(a1, a2, size)

#endif /* _LINUX_FIND_H */
