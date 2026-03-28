/* Kernel pattern: linked list operations */
#include <linux/list.h>
#include <linux/types.h>
#include <linux/kernel.h>

struct my_item {
    int value;
    char name[32];
    struct list_head list;
};

static LIST_HEAD(my_list);

static void add_item(struct my_item *item)
{
    INIT_LIST_HEAD(&item->list);
    list_add_tail(&item->list, &my_list);
}

static void remove_item(struct my_item *item)
{
    list_del(&item->list);
}

static struct my_item *find_item(int value)
{
    struct my_item *item;
    list_for_each_entry(item, &my_list, list) {
        if (item->value == value)
            return item;
    }
    return NULL;
}

static int count_items(void)
{
    struct my_item *item;
    int count = 0;
    list_for_each_entry(item, &my_list, list) {
        count++;
    }
    return count;
}

static void splice_lists(struct list_head *from)
{
    list_splice_init(from, &my_list);
}

void test_list_operations(void)
{
    struct my_item items[4];
    struct my_item *found;
    int i;
    int cnt;

    for (i = 0; i < 4; i++) {
        items[i].value = i * 10;
        add_item(&items[i]);
    }

    found = find_item(20);
    cnt = count_items();
    (void)cnt;

    if (found)
        remove_item(found);

    (void)splice_lists;
}
