/* SPDX-License-Identifier: GPL-2.0 */
/* Stub dynamic_queue_limits.h for free-cc kernel compilation testing */
#ifndef _LINUX_DQL_H
#define _LINUX_DQL_H

#include <linux/types.h>

#define DQL_HIST_LEN        4
#define DQL_HIST_ENT(dql, idx)  ((dql)->history[(idx) % DQL_HIST_LEN])

struct dql {
    unsigned int    num_queued;
    unsigned int    adj_limit;
    unsigned int    last_obj_cnt;

    unsigned short  stall_thrs;

    unsigned long   history_head;
    unsigned long   history[DQL_HIST_LEN];

    unsigned int    limit;
    unsigned int    num_completed;

    unsigned int    prev_ovlimit;
    unsigned int    prev_num_queued;
    unsigned int    prev_last_obj_cnt;

    unsigned int    lowest_slack;
    unsigned long   slack_start_time;

    unsigned int    max_limit;
    unsigned int    min_limit;
    unsigned int    slack_hold_time;

    unsigned short  stall_max;
    unsigned long   last_reap;
    unsigned long   stall_cnt;
};

static inline int dql_avail(const struct dql *dql)
{
    return (int)(dql->adj_limit - dql->num_queued);
}

static inline void dql_queued(struct dql *dql, unsigned int count)
{
    dql->last_obj_cnt = count;
    dql->num_queued += count;
}

extern void dql_completed(struct dql *dql, unsigned int count);
extern void dql_reset(struct dql *dql);
extern void dql_init(struct dql *dql, unsigned int hold_time);

#endif /* _LINUX_DQL_H */
