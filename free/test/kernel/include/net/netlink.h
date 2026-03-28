/* SPDX-License-Identifier: GPL-2.0 */
/* Stub net/netlink.h for free-cc kernel compilation testing */
#ifndef _NET_NETLINK_H
#define _NET_NETLINK_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kernel.h>

/* Netlink attribute header */
struct nlattr {
    __u16 nla_len;
    __u16 nla_type;
};

struct nlmsghdr {
    __u32 nlmsg_len;
    __u16 nlmsg_type;
    __u16 nlmsg_flags;
    __u32 nlmsg_seq;
    __u32 nlmsg_pid;
};

struct nla_bitfield32 {
    __u32 value;
    __u32 selector;
};

struct netlink_range_validation {
    u64 min;
    u64 max;
};

struct netlink_range_validation_signed {
    s64 min;
    s64 max;
};

/* NLA types */
enum {
    NLA_UNSPEC,
    NLA_U8,
    NLA_U16,
    NLA_U32,
    NLA_U64,
    NLA_STRING,
    NLA_FLAG,
    NLA_MSECS,
    NLA_NESTED,
    NLA_NESTED_ARRAY,
    NLA_NUL_STRING,
    NLA_BINARY,
    NLA_S8,
    NLA_S16,
    NLA_S32,
    NLA_S64,
    NLA_BITFIELD32,
    NLA_REJECT,
    NLA_BE16,
    NLA_BE32,
    NLA_SINT,
    NLA_UINT,
    __NLA_TYPE_MAX
};
#define NLA_TYPE_MAX (__NLA_TYPE_MAX - 1)

/* Validation types */
enum nla_policy_validation {
    NLA_VALIDATE_NONE,
    NLA_VALIDATE_RANGE,
    NLA_VALIDATE_RANGE_PTR,
    NLA_VALIDATE_MIN,
    NLA_VALIDATE_MAX,
    NLA_VALIDATE_MASK,
    NLA_VALIDATE_RANGE_WARN_TOO_LONG,
    NLA_VALIDATE_FUNCTION
};

struct nla_policy {
    u8 type;
    u8 validation_type;
    u16 len;
    union {
        const u32 bitfield32_valid;
        const u32 mask;
        const char *reject_message;
        const struct nla_policy *nested_policy;
        const struct netlink_range_validation *range;
        const struct netlink_range_validation_signed *range_signed;
        struct {
            s16 min, max;
        };
        int (*validate)(const struct nlattr *attr,
                        struct netlink_ext_ack *extack);
        u16 strict_start_type;
    };
};

struct netlink_ext_ack {
    const char *_msg;
    const struct nlattr *bad_attr;
    const struct nla_policy *policy;
    const struct nlattr *miss_nest;
    u16 miss_type;
    u8 cookie[20];
    u8 cookie_len;
};

#define NLA_ALIGNTO     4
#define NLA_ALIGN(len)  (((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN      ((int)NLA_ALIGN(sizeof(struct nlattr)))

#define NLMSG_ALIGNTO   4
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN    ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))

static inline int nla_attr_size(int payload)
{
    return NLA_HDRLEN + payload;
}

static inline int nla_total_size(int payload)
{
    return NLA_ALIGN(nla_attr_size(payload));
}

static inline int nla_padlen(int payload)
{
    return nla_total_size(payload) - nla_attr_size(payload);
}

static inline int nla_type(const struct nlattr *nla)
{
    return nla->nla_type & 0x3fff;
}

static inline void *nla_data(const struct nlattr *nla)
{
    return (char *)nla + NLA_HDRLEN;
}

static inline int nla_len(const struct nlattr *nla)
{
    return nla->nla_len - NLA_HDRLEN;
}

static inline int nla_ok(const struct nlattr *nla, int remaining)
{
    return remaining >= (int)sizeof(*nla) &&
           nla->nla_len >= sizeof(*nla) &&
           nla->nla_len <= remaining;
}

static inline struct nlattr *nla_next(const struct nlattr *nla, int *remaining)
{
    unsigned int totlen = NLA_ALIGN(nla->nla_len);
    *remaining -= totlen;
    return (struct nlattr *)((char *)nla + totlen);
}

extern int __nla_parse(struct nlattr **tb, int maxtype,
                       const struct nlattr *head, int len,
                       const struct nla_policy *policy, unsigned int validate,
                       struct netlink_ext_ack *extack);

extern struct nlattr *nla_find(const struct nlattr *head, int len,
                               int attrtype);

#define nla_for_each_attr(pos, head, len, rem)  \
    for (pos = head, rem = len;                 \
         nla_ok(pos, rem);                      \
         pos = nla_next(pos, &(rem)))

#define nla_for_each_nested(pos, nla, rem) \
    nla_for_each_attr(pos, nla_data(nla), nla_len(nla), rem)

extern int nla_strcmp(const struct nlattr *nla, const char *str);
extern char *nla_strdup(const struct nlattr *nla, gfp_t flags);

extern int __nla_validate(const struct nlattr *head, int len, int maxtype,
                          const struct nla_policy *policy, unsigned int validate,
                          struct netlink_ext_ack *extack);

extern struct nlattr *__nla_reserve(struct sk_buff *skb, int attrtype,
                                    int attrlen);
extern void *__nla_reserve_nohdr(struct sk_buff *skb, int attrlen);

extern struct nlattr *nla_reserve(struct sk_buff *skb, int attrtype,
                                  int attrlen);

extern int nla_put(struct sk_buff *skb, int attrtype, int attrlen,
                   const void *data);
extern int nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data);
extern int nla_append(struct sk_buff *skb, int attrlen, const void *data);

#define NL_SET_ERR_MSG(extack, msg)     do { \
    if (extack) (extack)->_msg = (msg);      \
} while (0)

#define NL_SET_ERR_MSG_ATTR(extack, attr, msg)  do { \
    if (extack) {                                     \
        (extack)->_msg = (msg);                       \
        (extack)->bad_attr = (attr);                  \
    }                                                 \
} while (0)

#define NL_SET_BAD_ATTR(extack, attr)   do { \
    if (extack) (extack)->bad_attr = (attr); \
} while (0)

#define NL_SET_ERR_MSG_ATTR_POL(extack, attr, pol, msg)  do { \
    if (extack) {                                              \
        (extack)->_msg = (msg);                                \
        (extack)->bad_attr = (attr);                           \
        (extack)->policy = (pol);                              \
    }                                                          \
} while (0)

#define NL_SET_ERR_ATTR_MISS(extack, nest, type)  do { \
    if (extack) {                                       \
        (extack)->miss_nest = (nest);                   \
        (extack)->miss_type = (type);                   \
    }                                                   \
} while (0)

#define NL_SET_ERR_MSG_ATTR_POL_FMT  NL_SET_ERR_MSG_ATTR_POL

#endif /* _NET_NETLINK_H */
