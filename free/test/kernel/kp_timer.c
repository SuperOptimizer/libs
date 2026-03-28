/* Kernel pattern: timerqueue and time operations */
#include <linux/types.h>
#include <linux/timerqueue.h>
#include <linux/kernel.h>

struct my_timer {
    struct timerqueue_node node;
    int timer_id;
    void (*handler)(struct my_timer *);
    unsigned long data;
};

static struct timerqueue_head timer_queue;

static void init_timer_subsystem(void)
{
    timerqueue_init_head(&timer_queue);
}

static void add_timer(struct my_timer *t, u64 expires)
{
    timerqueue_init(&t->node);
    t->node.expires = (ktime_t)expires;
    timerqueue_add(&timer_queue, &t->node);
}

static struct my_timer *get_next_timer(void)
{
    struct timerqueue_node *node;
    node = timerqueue_getnext(&timer_queue);
    if (!node)
        return NULL;
    return container_of(node, struct my_timer, node);
}

static void remove_timer(struct my_timer *t)
{
    timerqueue_del(&timer_queue, &t->node);
}

static void timeout_handler(struct my_timer *t)
{
    t->data++;
}

void test_timer(void)
{
    struct my_timer timers[4];
    struct my_timer *next;
    int i;

    init_timer_subsystem();

    for (i = 0; i < 4; i++) {
        timers[i].timer_id = i;
        timers[i].handler = timeout_handler;
        timers[i].data = 0;
        add_timer(&timers[i], (u64)((4 - i) * 100));
    }

    next = get_next_timer();
    if (next && next->handler)
        next->handler(next);

    remove_timer(&timers[2]);
}
