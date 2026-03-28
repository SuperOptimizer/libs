/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kobject.h for free-cc kernel compilation testing */
#ifndef _LINUX_KOBJECT_H
#define _LINUX_KOBJECT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/wait.h>

typedef unsigned int kuid_t;
typedef unsigned int kgid_t;

struct kset;
struct kobj_type;

struct sysfs_dirent;
struct kernfs_node;

struct kobject {
    const char          *name;
    struct list_head    entry;
    struct kobject      *parent;
    struct kset         *kset;
    const struct kobj_type  *ktype;
    struct kernfs_node  *sd;
    struct kref         kref;
    unsigned int        state_initialized:1;
    unsigned int        state_in_sysfs:1;
    unsigned int        state_add_uevent_sent:1;
    unsigned int        state_remove_uevent_sent:1;
};

struct kobj_type {
    void (*release)(struct kobject *kobj);
    const struct sysfs_ops *sysfs_ops;
    const struct attribute_group **default_groups;
    const struct kobj_ns_type_operations *(*child_ns_type)(const struct kobject *kobj);
    const void *(*namespace)(const struct kobject *kobj);
    void (*get_ownership)(const struct kobject *kobj, kuid_t *uid, kgid_t *gid);
};

struct kobj_uevent_env {
    char *argv[3];
    char *envp[32];
    int envp_idx;
    char buf[2048];
    int buflen;
};

struct kset_uevent_ops {
    int (* const filter)(const struct kobject *kobj);
    const char *(* const name)(const struct kobject *kobj);
    int (* const uevent)(const struct kobject *kobj, struct kobj_uevent_env *env);
};

struct kset {
    struct list_head list;
    spinlock_t list_lock;
    struct kobject kobj;
    const struct kset_uevent_ops *uevent_ops;
};

extern void kobject_init(struct kobject *kobj, const struct kobj_type *ktype);
extern int kobject_add(struct kobject *kobj, struct kobject *parent,
                       const char *fmt, ...);
extern int kobject_init_and_add(struct kobject *kobj,
                                const struct kobj_type *ktype,
                                struct kobject *parent,
                                const char *fmt, ...);
extern void kobject_del(struct kobject *kobj);
extern struct kobject *kobject_get(struct kobject *kobj);
extern struct kobject *kobject_get_unless_zero(struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);
extern const char *kobject_name(const struct kobject *kobj);
extern int kobject_set_name(struct kobject *kobj, const char *fmt, ...);
extern int kobject_set_name_vargs(struct kobject *kobj, const char *fmt,
                                  va_list vargs);
extern int kobject_rename(struct kobject *kobj, const char *new_name);
extern int kobject_move(struct kobject *kobj, struct kobject *new_parent);

extern struct kobject *kobject_create(void);
extern struct kobject *kobject_create_and_add(const char *name,
                                              struct kobject *parent);

extern void kset_init(struct kset *kset);
extern int kset_register(struct kset *kset);
extern void kset_unregister(struct kset *kset);
extern struct kset *kset_create_and_add(const char *name,
                                        const struct kset_uevent_ops *u,
                                        struct kobject *parent_kobj);

static inline struct kobj_type *get_ktype(const struct kobject *kobj)
{
    return (struct kobj_type *)kobj->ktype;
}

static inline struct kset *to_kset(struct kobject *kobj)
{
    return kobj ? container_of(kobj, struct kset, kobj) : (struct kset *)0;
}

static inline struct kobject *kset_find_obj(struct kset *kset, const char *name)
{
    (void)kset; (void)name;
    return (struct kobject *)0;
}

struct kobj_ns_type_operations {
    int type;
};

#endif /* _LINUX_KOBJECT_H */
