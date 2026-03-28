/* SPDX-License-Identifier: GPL-2.0 */
/* Stub dcache.h for free-cc kernel compilation testing */
#ifndef _LINUX_DCACHE_H
#define _LINUX_DCACHE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

struct qstr {
    union {
        struct {
            u32 hash;
            u32 len;
        };
        u64 hash_len;
    };
    const unsigned char *name;
};

#define QSTR_INIT(n, l) { .name = (n), { { .len = (l) } } }

struct dentry {
    unsigned int d_flags;
    struct dentry *d_parent;
    struct qstr d_name;
    struct inode *d_inode;
    unsigned char d_iname[32];
    struct list_head d_child;
    struct list_head d_subdirs;
    spinlock_t d_lock;
    const struct dentry_operations *d_op;
    struct super_block *d_sb;
    unsigned long d_time;
    void *d_fsdata;
    struct rcu_head d_rcu;
};

struct dentry_operations {
    int (*d_revalidate)(struct dentry *, unsigned int);
    int (*d_weak_revalidate)(struct dentry *, unsigned int);
    int (*d_hash)(const struct dentry *, struct qstr *);
    int (*d_compare)(const struct dentry *, unsigned int,
                     const char *, const struct qstr *);
    int (*d_delete)(const struct dentry *);
    void (*d_release)(struct dentry *);
    void (*d_prune)(struct dentry *);
    char *(*d_dname)(struct dentry *, char *, int);
};

static inline struct dentry *dget(struct dentry *dentry)
{
    return dentry;
}

static inline void dput(struct dentry *dentry)
{
    (void)dentry;
}

static inline bool d_is_dir(const struct dentry *dentry)
{
    return (dentry->d_flags & 0x0040) != 0;
}

#endif /* _LINUX_DCACHE_H */
