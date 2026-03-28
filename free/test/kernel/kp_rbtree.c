/* Kernel pattern: red-black tree usage */
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/kernel.h>

struct rb_item {
    unsigned long key;
    int data;
    struct rb_node node;
};

static struct rb_root mytree = RB_ROOT;

static struct rb_item *rb_search(struct rb_root *root, unsigned long key)
{
    struct rb_node *n = root->rb_node;
    while (n) {
        struct rb_item *item = rb_entry(n, struct rb_item, node);
        if (key < item->key)
            n = n->rb_left;
        else if (key > item->key)
            n = n->rb_right;
        else
            return item;
    }
    return NULL;
}

static int rb_insert(struct rb_root *root, struct rb_item *new)
{
    struct rb_node **link = &root->rb_node;
    struct rb_node *parent = NULL;

    while (*link) {
        struct rb_item *item = rb_entry(*link, struct rb_item, node);
        parent = *link;
        if (new->key < item->key)
            link = &(*link)->rb_left;
        else if (new->key > item->key)
            link = &(*link)->rb_right;
        else
            return -1;
    }
    rb_link_node(&new->node, parent, link);
    rb_insert_color(&new->node, root);
    return 0;
}

static void rb_remove(struct rb_root *root, struct rb_item *item)
{
    rb_erase(&item->node, root);
}

void test_rbtree(void)
{
    struct rb_item items[8];
    struct rb_item *found;
    int i;

    for (i = 0; i < 8; i++) {
        items[i].key = (unsigned long)(i * 7 + 3);
        items[i].data = i;
        rb_insert(&mytree, &items[i]);
    }

    found = rb_search(&mytree, 10);
    if (found)
        rb_remove(&mytree, found);
}
