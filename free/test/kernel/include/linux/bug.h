/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#define BUG() do {} while (1)
#define BUG_ON(condition) do { if (condition) BUG(); } while (0)
#define WARN(condition, fmt, ...) ((int)(condition))
#define WARN_ONCE(condition, fmt, ...) ((int)(condition))
#define WARN_ON(condition) ((int)(condition))
#define WARN_ON_ONCE(condition) ((int)(condition))

#define BUILD_BUG_ON(condition)
#define BUILD_BUG_ON_ZERO(e) 0
#define BUILD_BUG_ON_NULL(e) ((void *)0)

#endif /* _LINUX_BUG_H */
