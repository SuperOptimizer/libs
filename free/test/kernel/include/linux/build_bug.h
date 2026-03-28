/* SPDX-License-Identifier: GPL-2.0 */
/* Stub build_bug.h for free-cc kernel compilation testing */
#ifndef _LINUX_BUILD_BUG_H
#define _LINUX_BUILD_BUG_H

#define BUILD_BUG_ON_ZERO(e) (0)
#define BUILD_BUG_ON_NULL(e) ((void *)0)
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define BUILD_BUG_ON_MSG(cond, msg) BUILD_BUG_ON(cond)
#define BUILD_BUG() BUILD_BUG_ON(1)

#endif /* _LINUX_BUILD_BUG_H */
