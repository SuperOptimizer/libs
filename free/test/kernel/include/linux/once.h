/* SPDX-License-Identifier: GPL-2.0 */
/* Stub once.h for free-cc kernel compilation testing */
#ifndef _LINUX_ONCE_H
#define _LINUX_ONCE_H

#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/workqueue.h>

#define DO_ONCE(func, ...)  do { func(__VA_ARGS__); } while (0)
#define DO_ONCE_LITE(func, ...) do { func(__VA_ARGS__); } while (0)

struct static_key_true;
struct module;

extern bool __do_once_start(bool *done, unsigned long *flags);
extern void __do_once_done(bool *done, struct static_key_true *once_key,
                           unsigned long *flags, struct module *mod);

#define get_random_once(buf, nbytes) do {} while (0)

#endif /* _LINUX_ONCE_H */
