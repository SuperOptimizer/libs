/* SPDX-License-Identifier: GPL-2.0 */
/* Stub assoc_array_priv.h for free-cc kernel compilation testing */
#ifndef _LINUX_ASSOC_ARRAY_PRIV_H
#define _LINUX_ASSOC_ARRAY_PRIV_H

#include <linux/types.h>

#define ASSOC_ARRAY_FAN_OUT      16
#define ASSOC_ARRAY_FAN_MASK     (ASSOC_ARRAY_FAN_OUT - 1)
#define ASSOC_ARRAY_LEVEL_STEP   (sizeof(unsigned long) > 4 ? 4 : 3)
#define ASSOC_ARRAY_LEVEL_STEP_LOG2 (sizeof(unsigned long) > 4 ? 2 : 1)
#define ASSOC_ARRAY_KEY_CHUNK_SIZE (sizeof(unsigned long))

struct assoc_array_ptr;

struct assoc_array_node {
    struct assoc_array_ptr *back_pointer;
    u8 parent_slot;
    struct assoc_array_ptr *slots[ASSOC_ARRAY_FAN_OUT];
    unsigned long nr_leaves_on_branch;
};

struct assoc_array_shortcut {
    struct assoc_array_ptr *back_pointer;
    int parent_slot;
    int skip_to_level;
    struct assoc_array_ptr *next_node;
    unsigned long index_key[];
};

struct assoc_array {
    struct assoc_array_ptr *root;
    unsigned long nr_leaves_on_tree;
};

struct assoc_array_ops {
    unsigned long (*get_key_chunk)(const void *index_key, int level);
    unsigned long (*get_object_key_chunk)(const void *object, int level);
    bool (*compare_object)(const void *object, const void *index_key);
    int (*diff_objects)(const void *object, const void *index_key);
    void (*free_object)(void *object);
};

struct assoc_array_edit {
    struct rcu_head rcu;
    struct assoc_array *array;
    const struct assoc_array_ops *ops;
    const struct assoc_array_ops *ops_for_excised_subtree;
    struct assoc_array_ptr *leaf;
    struct assoc_array_ptr **leaf_p;
    struct assoc_array_ptr *dead_leaf;
    struct assoc_array_ptr *new_meta[3];
    struct assoc_array_ptr *excised_meta[1];
    struct assoc_array_ptr *excised_subtree;
    struct assoc_array_ptr **set_backpointers[ASSOC_ARRAY_FAN_OUT];
    struct assoc_array_ptr *set_backpointers_to;
    struct assoc_array_node *adjust_count_on;
    long adjust_count_by;
    struct {
        struct assoc_array_ptr **ptr;
        struct assoc_array_ptr *to;
    } set[2 * ASSOC_ARRAY_FAN_OUT + 2];
    struct {
        u8 *p;
        u8 to;
    } set_parent_slot[ASSOC_ARRAY_FAN_OUT];
    u8 segment_cache[ASSOC_ARRAY_FAN_OUT + 1];
};

static inline bool assoc_array_ptr_is_meta(const struct assoc_array_ptr *x)
{
    return (unsigned long)x & 1;
}

static inline bool assoc_array_ptr_is_leaf(const struct assoc_array_ptr *x)
{
    return (unsigned long)x & 2;
}

static inline bool assoc_array_ptr_is_shortcut(const struct assoc_array_ptr *x)
{
    return (unsigned long)x & 3;
}

static inline bool assoc_array_ptr_is_node(const struct assoc_array_ptr *x)
{
    return !((unsigned long)x & 3);
}

static inline struct assoc_array_node *
assoc_array_ptr_to_node(const struct assoc_array_ptr *x)
{
    return (struct assoc_array_node *)((unsigned long)x & ~3UL);
}

static inline struct assoc_array_shortcut *
assoc_array_ptr_to_shortcut(const struct assoc_array_ptr *x)
{
    return (struct assoc_array_shortcut *)((unsigned long)x & ~3UL);
}

static inline void *assoc_array_ptr_to_leaf(const struct assoc_array_ptr *x)
{
    return (void *)((unsigned long)x & ~3UL);
}

static inline struct assoc_array_ptr *
assoc_array_node_to_ptr(const struct assoc_array_node *x)
{
    return (struct assoc_array_ptr *)((unsigned long)x | 1);
}

static inline struct assoc_array_ptr *
assoc_array_shortcut_to_ptr(const struct assoc_array_shortcut *x)
{
    return (struct assoc_array_ptr *)((unsigned long)x | 3);
}

static inline struct assoc_array_ptr *
assoc_array_leaf_to_ptr(const void *x)
{
    return (struct assoc_array_ptr *)((unsigned long)x | 2);
}

#endif /* _LINUX_ASSOC_ARRAY_PRIV_H */
