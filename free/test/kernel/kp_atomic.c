/* Kernel pattern: atomic operations */
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/kernel.h>

struct atomic_stats {
    atomic_t total_ops;
    atomic_t active_users;
    atomic_t error_count;
    atomic_t max_concurrent;
};

static struct atomic_stats stats;

static void stats_init(void)
{
    atomic_set(&stats.total_ops, 0);
    atomic_set(&stats.active_users, 0);
    atomic_set(&stats.error_count, 0);
    atomic_set(&stats.max_concurrent, 0);
}

static void stats_begin_op(void)
{
    int cur;
    int max;
    atomic_inc(&stats.total_ops);
    cur = atomic_inc_return(&stats.active_users);

    /* Update max if needed (simplified, not truly atomic) */
    max = atomic_read(&stats.max_concurrent);
    if (cur > max)
        atomic_set(&stats.max_concurrent, cur);
}

static void stats_end_op(void)
{
    atomic_dec(&stats.active_users);
}

static void stats_record_error(void)
{
    atomic_inc(&stats.error_count);
}

static int stats_get_total(void)
{
    return atomic_read(&stats.total_ops);
}

static int stats_get_errors(void)
{
    return atomic_read(&stats.error_count);
}

void test_atomic(void)
{
    int total;
    int errors;
    int i;

    stats_init();

    for (i = 0; i < 10; i++) {
        stats_begin_op();
        if (i % 3 == 0)
            stats_record_error();
        stats_end_op();
    }

    total = stats_get_total();
    errors = stats_get_errors();
    (void)total;
    (void)errors;
}
