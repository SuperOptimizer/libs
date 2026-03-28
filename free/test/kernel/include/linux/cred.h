/* SPDX-License-Identifier: GPL-2.0 */
/* Stub cred.h for free-cc kernel compilation testing */
#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#include <linux/types.h>

typedef struct { uid_t val; } kuid_t;
typedef struct { gid_t val; } kgid_t;

struct cred {
    kuid_t uid;
    kgid_t euid;
    kgid_t gid;
    kgid_t egid;
};

static inline const struct cred *current_cred(void)
{
    return (const struct cred *)0;
}

static inline bool uid_eq(kuid_t left, kuid_t right)
{
    return left.val == right.val;
}

static inline bool gid_eq(kgid_t left, kgid_t right)
{
    return left.val == right.val;
}

#endif /* _LINUX_CRED_H */
