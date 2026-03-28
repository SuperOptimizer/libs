/* SPDX-License-Identifier: GPL-2.0 */
/* Stub interval_tree_generic.h for free-cc kernel compilation testing */
#ifndef _LINUX_INTERVAL_TREE_GENERIC_H
#define _LINUX_INTERVAL_TREE_GENERIC_H

#include <linux/rbtree_augmented.h>

/*
 * Simplified INTERVAL_TREE_DEFINE macro.
 * In the real kernel this generates typed interval tree functions.
 * For compilation testing, we provide a stub version.
 */
#define INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE,          \
                             ITSTART, ITLAST, ITSTATIC, ITPREFIX)        \
                                                                          \
ITSTATIC void ITPREFIX ## _insert(ITSTRUCT *node,                        \
                                  struct rb_root_cached *root)            \
{                                                                         \
    struct rb_node **link = &root->rb_root.rb_node;                       \
    struct rb_node *rb_parent = NULL;                                     \
    ITTYPE start = ITSTART(node);                                         \
    bool leftmost = true;                                                 \
                                                                          \
    while (*link) {                                                       \
        ITSTRUCT *parent = rb_entry(*link, ITSTRUCT, ITRB);              \
        rb_parent = *link;                                                \
        if (parent->ITSUBTREE < ITLAST(node))                            \
            parent->ITSUBTREE = ITLAST(node);                            \
        if (start < ITSTART(parent))                                      \
            link = &parent->ITRB.rb_left;                                 \
        else {                                                            \
            link = &parent->ITRB.rb_right;                                \
            leftmost = false;                                             \
        }                                                                 \
    }                                                                     \
    node->ITSUBTREE = ITLAST(node);                                      \
    rb_link_node(&node->ITRB, rb_parent, link);                          \
    rb_insert_color_cached(&node->ITRB, root, leftmost);                 \
}                                                                         \
                                                                          \
ITSTATIC void ITPREFIX ## _remove(ITSTRUCT *node,                        \
                                  struct rb_root_cached *root)            \
{                                                                         \
    rb_erase_cached(&node->ITRB, root);                                  \
}                                                                         \
                                                                          \
ITSTATIC ITSTRUCT *ITPREFIX ## _iter_first(                              \
    struct rb_root_cached *root, ITTYPE start, ITTYPE last)              \
{                                                                         \
    ITSTRUCT *node;                                                       \
    struct rb_node *rb;                                                   \
    (void)start; (void)last;                                              \
    rb = rb_first_cached(root);                                           \
    if (!rb) return NULL;                                                 \
    node = rb_entry(rb, ITSTRUCT, ITRB);                                 \
    return node;                                                          \
}                                                                         \
                                                                          \
ITSTATIC ITSTRUCT *ITPREFIX ## _iter_next(                               \
    ITSTRUCT *node, ITTYPE start, ITTYPE last)                           \
{                                                                         \
    struct rb_node *rb;                                                   \
    (void)start; (void)last;                                              \
    rb = rb_next(&node->ITRB);                                           \
    if (!rb) return NULL;                                                 \
    return rb_entry(rb, ITSTRUCT, ITRB);                                 \
}

#endif /* _LINUX_INTERVAL_TREE_GENERIC_H */
