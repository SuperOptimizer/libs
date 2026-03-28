/* Kernel pattern: notifier chain */
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/kernel.h>

#define MY_EVENT_SHUTDOWN  1
#define MY_EVENT_RESTART   2
#define MY_EVENT_SUSPEND   3

struct event_data {
    int event_type;
    unsigned long param;
    const char *reason;
};

static int handler_a(struct notifier_block *nb, unsigned long event, void *data)
{
    struct event_data *ed = data;
    (void)nb;
    (void)ed;
    if (event == MY_EVENT_SHUTDOWN)
        return NOTIFY_OK;
    return NOTIFY_DONE;
}

static int handler_b(struct notifier_block *nb, unsigned long event, void *data)
{
    (void)nb;
    (void)data;
    if (event == MY_EVENT_RESTART)
        return NOTIFY_STOP;
    return NOTIFY_DONE;
}

static struct notifier_block nb_a = {
    .notifier_call = handler_a,
    .priority = 10,
};

static struct notifier_block nb_b = {
    .notifier_call = handler_b,
    .priority = 5,
};

static ATOMIC_NOTIFIER_HEAD(my_chain);

void test_notifier(void)
{
    struct event_data ev;
    int ret;

    atomic_notifier_chain_register(&my_chain, &nb_a);
    atomic_notifier_chain_register(&my_chain, &nb_b);

    ev.event_type = MY_EVENT_SHUTDOWN;
    ev.param = 0;
    ev.reason = "test";

    ret = atomic_notifier_call_chain(&my_chain, MY_EVENT_SHUTDOWN, &ev);
    (void)ret;

    atomic_notifier_chain_unregister(&my_chain, &nb_b);
    atomic_notifier_chain_unregister(&my_chain, &nb_a);
}
