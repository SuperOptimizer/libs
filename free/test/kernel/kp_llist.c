/* Kernel pattern: lockless linked list */
#include <linux/types.h>
#include <linux/llist.h>
#include <linux/kernel.h>

struct work_item {
    struct llist_node node;
    int work_type;
    unsigned long data;
    void (*handler)(struct work_item *);
};

static struct llist_head pending_work = LLIST_HEAD_INIT(pending_work);

static void submit_work(struct work_item *item)
{
    llist_add(&item->node, &pending_work);
}

static struct work_item *dequeue_all(void)
{
    struct llist_node *list;
    list = llist_del_all(&pending_work);
    if (!list)
        return NULL;
    return llist_entry(list, struct work_item, node);
}

static void process_handler(struct work_item *item)
{
    item->data *= 2;
}

void test_llist(void)
{
    struct work_item items[5];
    struct work_item *batch;
    int i;

    for (i = 0; i < 5; i++) {
        items[i].work_type = i;
        items[i].data = (unsigned long)(i * 100);
        items[i].handler = process_handler;
        submit_work(&items[i]);
    }

    batch = dequeue_all();
    if (batch && batch->handler)
        batch->handler(batch);
}
