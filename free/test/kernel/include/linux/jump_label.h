/* SPDX-License-Identifier: GPL-2.0 */
/* Stub jump_label.h for free-cc kernel compilation testing */
#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H

struct static_key_true {
    int key;
};

struct static_key_false {
    int key;
};

#define STATIC_KEY_TRUE_INIT  { .key = 1 }
#define STATIC_KEY_FALSE_INIT { .key = 0 }

#define DEFINE_STATIC_KEY_TRUE(name) \
    struct static_key_true name = STATIC_KEY_TRUE_INIT

#define DEFINE_STATIC_KEY_FALSE(name) \
    struct static_key_false name = STATIC_KEY_FALSE_INIT

#define static_branch_likely(x)   ((x)->key)
#define static_branch_unlikely(x) ((x)->key)
#define static_branch_enable(x)   do { (x)->key = 1; } while (0)
#define static_branch_disable(x)  do { (x)->key = 0; } while (0)
#define static_key_enabled(x)     ((x)->key)

#endif /* _LINUX_JUMP_LABEL_H */
