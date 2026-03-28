/* SPDX-License-Identifier: GPL-2.0 */
/* Stub rbtree.h for free-cc kernel compilation testing */
#ifndef _LINUX_RBTREE_H
#define _LINUX_RBTREE_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>

struct rb_node {
    unsigned long  __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};

struct rb_root {
    struct rb_node *rb_node;
};

struct rb_root_cached {
    struct rb_root rb_root;
    struct rb_node *rb_leftmost;
};

#define RB_ROOT (struct rb_root) { NULL, }
#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }

#define rb_parent(r)   ((struct rb_node *)((r)->__rb_parent_color & ~3))

#define rb_entry(ptr, type, member) container_of(ptr, type, member)
#define rb_entry_safe(ptr, type, member) \
    ({ typeof(ptr) ____ptr = (ptr); \
       ____ptr ? rb_entry(____ptr, type, member) : NULL; \
    })

static inline void rb_link_node(struct rb_node *node, struct rb_node *parent,
                                struct rb_node **rb_link)
{
    node->__rb_parent_color = (unsigned long)parent;
    node->rb_left = node->rb_right = NULL;
    *rb_link = node;
}

extern void rb_insert_color(struct rb_node *, struct rb_root *);
extern void rb_erase(struct rb_node *, struct rb_root *);

extern struct rb_node *rb_next(const struct rb_node *);
extern struct rb_node *rb_prev(const struct rb_node *);
extern struct rb_node *rb_first(const struct rb_root *);
extern struct rb_node *rb_last(const struct rb_root *);

extern struct rb_node *rb_first_postorder(const struct rb_root *);
extern struct rb_node *rb_next_postorder(const struct rb_node *);

extern void rb_replace_node(struct rb_node *victim, struct rb_node *new,
                            struct rb_root *root);
extern void rb_replace_node_cached(struct rb_node *victim, struct rb_node *new,
                                   struct rb_root_cached *root);

static inline void rb_insert_color_cached(struct rb_node *node,
                                          struct rb_root_cached *root,
                                          bool leftmost)
{
    if (leftmost)
        root->rb_leftmost = node;
    rb_insert_color(node, &root->rb_root);
}

static inline void rb_erase_cached(struct rb_node *node,
                                   struct rb_root_cached *root)
{
    if (root->rb_leftmost == node)
        root->rb_leftmost = rb_next(node);
    rb_erase(node, &root->rb_root);
}

static inline struct rb_node *rb_first_cached(const struct rb_root_cached *root)
{
    return root->rb_leftmost;
}

#define rb_for_each(node, root) \
    for (node = rb_first(root); node; node = rb_next(node))

#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
    for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
         pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
                                     typeof(*pos), field); 1; }); \
         pos = n)

#endif /* _LINUX_RBTREE_H */
