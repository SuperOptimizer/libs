/* SPDX-License-Identifier: GPL-2.0 */
/* Stub module.h for free-cc kernel compilation testing */
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/types.h>
#include <linux/export.h>
#include <linux/init.h>

struct module;

#define THIS_MODULE ((struct module *)0)

struct kernel_param;

struct kernel_param_ops {
    int (*set)(const char *val, const struct kernel_param *kp);
    int (*get)(char *buffer, const struct kernel_param *kp);
    void (*free)(void *arg);
};

struct kernel_param {
    const char *name;
    const struct kernel_param_ops *ops;
    void *arg;
    unsigned int perm;
};

#define core_param(name, var, type, perm)
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define module_param_cb(name, ops, arg, perm)
#define __core_param_cb(name, ops, arg, perm)
#define module_param_string(name, string, len, perm)

extern int param_set_int(const char *val, const struct kernel_param *kp);
extern int param_get_int(char *buffer, const struct kernel_param *kp);
extern int param_set_ulong(const char *val, const struct kernel_param *kp);
extern int param_get_ulong(char *buffer, const struct kernel_param *kp);
extern int param_set_bool(const char *val, const struct kernel_param *kp);
extern int param_get_bool(char *buffer, const struct kernel_param *kp);

extern const struct kernel_param_ops param_ops_int;
extern const struct kernel_param_ops param_ops_uint;
extern const struct kernel_param_ops param_ops_ulong;
extern const struct kernel_param_ops param_ops_bool;
extern const struct kernel_param_ops param_ops_charp;

#define MODULE_AUTHOR(name)
#define MODULE_DESCRIPTION(desc)
#define MODULE_LICENSE(license)
#define MODULE_ALIAS(alias)
#define MODULE_VERSION(version)
#define MODULE_FIRMWARE(fw)
#define MODULE_SOFTDEP(dep)
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_PARM_DESC(parm, desc)

/* Module state */
#define module_put(m) do {} while (0)
#define __module_get(m) do {} while (0)
#define try_module_get(m) 1
#define module_refcount(m) 1

#endif /* _LINUX_MODULE_H */
