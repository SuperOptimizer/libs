/* SPDX-License-Identifier: GPL-2.0 */
/* Stub textsearch.h for free-cc kernel compilation testing */
#ifndef _LINUX_TEXTSEARCH_H
#define _LINUX_TEXTSEARCH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/slab.h>

struct ts_config;
struct ts_state;

struct ts_ops {
    const char *name;
    struct ts_config *(*init)(const void *, unsigned int, gfp_t, int);
    unsigned int (*find)(struct ts_config *, struct ts_state *);
    void (*destroy)(struct ts_config *);
    void *(*get_pattern)(struct ts_config *);
    unsigned int (*get_pattern_len)(struct ts_config *);
    struct module *owner;
    struct list_head list;
};

struct ts_config {
    struct ts_ops *ops;
    int flags;
    unsigned int (*get_next_block)(unsigned int, const u8 **,
                                   struct ts_config *,
                                   struct ts_state *);
    void (*finish)(struct ts_config *, struct ts_state *);
};

struct ts_state {
    unsigned int offset;
    char cb[48];
};

#define TS_AUTOLOAD  1
#define TS_IGNORECASE 2

extern struct ts_config *textsearch_prepare(const char *, const void *,
                                            unsigned int, gfp_t, int);
extern void textsearch_destroy(struct ts_config *);

static inline unsigned int textsearch_find(struct ts_config *conf,
                                           struct ts_state *state)
{
    return conf->ops->find(conf, state);
}

#endif /* _LINUX_TEXTSEARCH_H */
