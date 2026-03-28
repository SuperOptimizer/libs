/* Kernel pattern: priority sorted list */
#include <linux/types.h>
#include <linux/plist.h>
#include <linux/kernel.h>

struct priority_task {
    struct plist_node node;
    int task_id;
    const char *name;
};

static struct plist_head task_queue = PLIST_HEAD_INIT(task_queue);

static void add_task(struct priority_task *task, int priority)
{
    plist_node_init(&task->node, priority);
    plist_add(&task->node, &task_queue);
}

static struct priority_task *get_highest_priority(void)
{
    struct plist_node *first;
    if (plist_head_empty(&task_queue))
        return NULL;
    first = plist_first(&task_queue);
    return container_of(first, struct priority_task, node);
}

static void remove_task(struct priority_task *task)
{
    plist_del(&task->node, &task_queue);
}

void test_plist(void)
{
    struct priority_task tasks[4];
    struct priority_task *highest;
    int prios[] = { 50, 10, 30, 20 };
    int i;

    for (i = 0; i < 4; i++) {
        tasks[i].task_id = i;
        tasks[i].name = "task";
        add_task(&tasks[i], prios[i]);
    }

    highest = get_highest_priority();
    if (highest)
        remove_task(highest);

    highest = get_highest_priority();
    (void)highest;
}
