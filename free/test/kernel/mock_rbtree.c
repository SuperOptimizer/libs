/* EXPECTED: 0 */
/* Mock kernel rbtree patterns - red-black tree insert/search/delete */

#define NULL ((void *)0)
#define offsetof(type, member) ((unsigned long)&((type *)0)->member)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Color encoding */
#define RB_RED   0
#define RB_BLACK 1

struct rb_node {
    unsigned long rb_parent_color; /* parent pointer + color bit */
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};

struct rb_root {
    struct rb_node *rb_node;
};

#define RB_ROOT { NULL }

/* Accessors - parent is stored with color in low bit */
static struct rb_node *rb_parent(const struct rb_node *n) {
    return (struct rb_node *)(n->rb_parent_color & ~3UL);
}

static int rb_color(const struct rb_node *n) {
    return (int)(n->rb_parent_color & 1);
}

static int rb_is_red(const struct rb_node *n) {
    return !rb_color(n);
}

static int rb_is_black(const struct rb_node *n) {
    return rb_color(n);
}

static void rb_set_parent(struct rb_node *n, struct rb_node *p) {
    n->rb_parent_color = (unsigned long)p | (n->rb_parent_color & 1);
}

static void rb_set_color(struct rb_node *n, int color) {
    n->rb_parent_color = (n->rb_parent_color & ~1UL) | (unsigned long)color;
}

static void rb_set_parent_color(struct rb_node *n, struct rb_node *p, int c) {
    n->rb_parent_color = (unsigned long)p | (unsigned long)c;
}

/* Rotation helpers */
static void rb_rotate_left(struct rb_node *node, struct rb_root *root) {
    struct rb_node *right;
    struct rb_node *parent;

    right = node->rb_right;
    parent = rb_parent(node);

    node->rb_right = right->rb_left;
    if (right->rb_left)
        rb_set_parent(right->rb_left, node);

    right->rb_left = node;
    rb_set_parent(right, parent);
    rb_set_parent(node, right);

    if (parent) {
        if (parent->rb_left == node)
            parent->rb_left = right;
        else
            parent->rb_right = right;
    } else {
        root->rb_node = right;
    }
}

static void rb_rotate_right(struct rb_node *node, struct rb_root *root) {
    struct rb_node *left;
    struct rb_node *parent;

    left = node->rb_left;
    parent = rb_parent(node);

    node->rb_left = left->rb_right;
    if (left->rb_right)
        rb_set_parent(left->rb_right, node);

    left->rb_right = node;
    rb_set_parent(left, parent);
    rb_set_parent(node, left);

    if (parent) {
        if (parent->rb_right == node)
            parent->rb_right = left;
        else
            parent->rb_left = left;
    } else {
        root->rb_node = left;
    }
}

/* Insert fixup - restore red-black properties after insertion */
static void rb_insert_color(struct rb_node *node, struct rb_root *root) {
    struct rb_node *parent;
    struct rb_node *gparent;
    struct rb_node *uncle;

    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        if (!gparent) break;

        if (parent == gparent->rb_left) {
            uncle = gparent->rb_right;
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }
            if (parent->rb_right == node) {
                rb_rotate_left(parent, root);
                node = parent;
                parent = rb_parent(node);
            }
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_right(gparent, root);
        } else {
            uncle = gparent->rb_left;
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }
            if (parent->rb_left == node) {
                rb_rotate_right(parent, root);
                node = parent;
                parent = rb_parent(node);
            }
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_left(gparent, root);
        }
    }
    rb_set_color(root->rb_node, RB_BLACK);
}

/* Link a new node */
static void rb_link_node(struct rb_node *node, struct rb_node *parent,
                         struct rb_node **link) {
    node->rb_parent_color = (unsigned long)parent;
    node->rb_left = NULL;
    node->rb_right = NULL;
    *link = node;
}

/* ---- Test: integer key tree ---- */
struct int_node {
    int key;
    int value;
    struct rb_node rb;
};

static void int_tree_insert(struct rb_root *root, struct int_node *data) {
    struct rb_node **link;
    struct rb_node *parent;
    struct int_node *entry;

    link = &root->rb_node;
    parent = NULL;

    while (*link) {
        parent = *link;
        entry = container_of(parent, struct int_node, rb);
        if (data->key < entry->key)
            link = &parent->rb_left;
        else
            link = &parent->rb_right;
    }

    rb_link_node(&data->rb, parent, link);
    rb_insert_color(&data->rb, root);
}

static struct int_node *int_tree_search(struct rb_root *root, int key) {
    struct rb_node *node;
    struct int_node *entry;

    node = root->rb_node;
    while (node) {
        entry = container_of(node, struct int_node, rb);
        if (key < entry->key)
            node = node->rb_left;
        else if (key > entry->key)
            node = node->rb_right;
        else
            return entry;
    }
    return NULL;
}

/* Count nodes in tree (in-order traversal) */
static int tree_count(struct rb_node *node) {
    if (!node) return 0;
    return tree_count(node->rb_left) + 1 + tree_count(node->rb_right);
}

/* ---- Tests ---- */
int main(void) {
    struct rb_root root = RB_ROOT;
    struct int_node nodes[8];
    struct int_node *found;
    int keys[8];
    int i, count;

    keys[0] = 50; keys[1] = 30; keys[2] = 70; keys[3] = 20;
    keys[4] = 40; keys[5] = 60; keys[6] = 80; keys[7] = 10;

    /* Insert nodes */
    for (i = 0; i < 8; i++) {
        nodes[i].key = keys[i];
        nodes[i].value = keys[i] * 10;
        int_tree_insert(&root, &nodes[i]);
    }

    /* Verify root exists */
    if (root.rb_node == NULL) return 1;

    /* Root should be black */
    if (!rb_is_black(root.rb_node)) return 2;

    /* Search for each key */
    for (i = 0; i < 8; i++) {
        found = int_tree_search(&root, keys[i]);
        if (found == NULL) return 10 + i;
        if (found->value != keys[i] * 10) return 20 + i;
    }

    /* Search for non-existent key */
    found = int_tree_search(&root, 99);
    if (found != NULL) return 3;

    /* Count nodes */
    count = tree_count(root.rb_node);
    if (count != 8) return 4;

    return 0;
}
