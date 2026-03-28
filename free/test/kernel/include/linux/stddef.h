/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#include <linux/types.h>

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)

#endif /* _LINUX_STDDEF_H */
