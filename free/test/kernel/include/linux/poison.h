/* SPDX-License-Identifier: GPL-2.0 */
/* Stub poison.h for free-cc kernel compilation testing */
#ifndef _LINUX_POISON_H
#define _LINUX_POISON_H

#define LIST_POISON1 ((void *) 0x100)
#define LIST_POISON2 ((void *) 0x122)
#define TIMER_ENTRY_STATIC ((void *) 0x300)

#define POISON_POINTER_DELTA 0
#define POISON_FREE 0x6b
#define POISON_INUSE 0x5a
#define POISON_END  0xa5

#endif /* _LINUX_POISON_H */
