/* SPDX-License-Identifier: GPL-2.0 */
/* Stub skbuff.h for free-cc kernel compilation testing */
#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H

#include <linux/types.h>

struct sk_buff {
    unsigned char *data;
    unsigned char *tail;
    unsigned char *end;
    unsigned int len;
    unsigned int data_len;
};

static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
    unsigned char *tmp = skb->tail;
    skb->tail += len;
    skb->len += len;
    return tmp;
}

static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
    return skb->tail;
}

#endif /* _LINUX_SKBUFF_H */
