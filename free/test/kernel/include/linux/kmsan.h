/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kmsan.h for free-cc kernel compilation testing */
#ifndef _LINUX_KMSAN_H
#define _LINUX_KMSAN_H

#include <linux/types.h>

#define kmsan_unpoison_memory(addr, size) do {} while (0)
#define kmsan_check_memory(addr, size) do {} while (0)

#endif /* _LINUX_KMSAN_H */
