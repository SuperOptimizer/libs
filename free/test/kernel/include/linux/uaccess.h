/* SPDX-License-Identifier: GPL-2.0 */
/* Stub uaccess.h for free-cc kernel compilation testing */
#ifndef _LINUX_UACCESS_H
#define _LINUX_UACCESS_H

#include <linux/types.h>

#define __user

static inline unsigned long copy_from_user(void *to, const void __user *from,
                                           unsigned long n)
{
    /* stub: in real kernel, copies from userspace */
    return 0;
}

static inline unsigned long copy_to_user(void __user *to, const void *from,
                                         unsigned long n)
{
    return 0;
}

#endif /* _LINUX_UACCESS_H */
