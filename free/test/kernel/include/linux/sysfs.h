/* Stub sysfs.h for free-cc kernel compilation testing */
#ifndef _LINUX_SYSFS_H
#define _LINUX_SYSFS_H

#include <linux/types.h>

struct kobject;
struct kobj_attribute {
    int dummy;
    ssize_t (*show)(struct kobject *, struct kobj_attribute *, char *);
    ssize_t (*store)(struct kobject *, struct kobj_attribute *, const char *, size_t);
};

extern int sysfs_emit(char *buf, const char *fmt, ...);

#define __ATTR_RO(_name) { 0 }

#endif
