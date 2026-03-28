/* EXPECTED: 0 */
/* Mock kernel list.h patterns - doubly-linked circular lists */

#define NULL ((void *)0)
#define offsetof(type, member) ((unsigned long)&((type *)0)->member)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Kernel-style list_head */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static void __list_add(struct list_head *new_node,
                       struct list_head *prev,
                       struct list_head *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

static void list_add(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head, head->next);
}

static void list_add_tail(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head->prev, head);
}

static void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

static void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static int list_empty(const struct list_head *head) {
    return head->next == head;
}

static void list_move(struct list_head *entry, struct list_head *head) {
    __list_del(entry->prev, entry->next);
    list_add(entry, head);
}

static void list_move_tail(struct list_head *entry, struct list_head *head) {
    __list_del(entry->prev, entry->next);
    list_add_tail(entry, head);
}

/* list_for_each - iterate over a list */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* list_for_each_safe - iterate allowing deletion */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; \
         pos != (head); \
         pos = n, n = pos->next)

/* list_entry - get the struct containing the list_head */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/* list_first_entry */
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

/* list_for_each_entry - iterate over list of given type */
#define list_for_each_entry(pos, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_entry(pos->member.next, typeof(*pos), member))

/* ---- Test data structures ---- */
struct task_struct {
    int pid;
    const char *name;
    int state;
    struct list_head tasks;
    struct list_head children;
    struct list_head sibling;
};

/* ---- Tests ---- */
int main(void) {
    LIST_HEAD(runqueue);
    LIST_HEAD(waitqueue);
    struct task_struct t1, t2, t3, t4;
    struct list_head *pos;
    struct list_head *tmp;
    int count;
    struct task_struct *task;

    /* Initialize tasks */
    t1.pid = 1; t1.name = "init";   t1.state = 0;
    t2.pid = 2; t2.name = "kthrd";  t2.state = 0;
    t3.pid = 3; t3.name = "shell";  t3.state = 1;
    t4.pid = 4; t4.name = "worker"; t4.state = 0;
    INIT_LIST_HEAD(&t1.children);
    INIT_LIST_HEAD(&t2.children);
    INIT_LIST_HEAD(&t3.children);
    INIT_LIST_HEAD(&t4.children);
    INIT_LIST_HEAD(&t1.sibling);
    INIT_LIST_HEAD(&t2.sibling);
    INIT_LIST_HEAD(&t3.sibling);
    INIT_LIST_HEAD(&t4.sibling);

    /* Add all to runqueue */
    list_add_tail(&t1.tasks, &runqueue);
    list_add_tail(&t2.tasks, &runqueue);
    list_add_tail(&t3.tasks, &runqueue);
    list_add_tail(&t4.tasks, &runqueue);

    /* Count items */
    count = 0;
    list_for_each(pos, &runqueue) {
        count++;
    }
    if (count != 4) return 1;

    /* Move sleeping task (t3, state==1) to waitqueue */
    list_move(&t3.tasks, &waitqueue);

    /* Count runqueue */
    count = 0;
    list_for_each(pos, &runqueue) {
        count++;
    }
    if (count != 3) return 2;

    /* Count waitqueue */
    count = 0;
    list_for_each(pos, &waitqueue) {
        count++;
    }
    if (count != 1) return 3;

    /* Verify first entry */
    task = list_first_entry(&runqueue, struct task_struct, tasks);
    if (task->pid != 1) return 4;

    /* Delete t2 from runqueue */
    list_del(&t2.tasks);

    count = 0;
    list_for_each(pos, &runqueue) {
        count++;
    }
    if (count != 2) return 5;

    /* list_empty check */
    if (list_empty(&runqueue)) return 6;
    if (!list_empty(&t2.children)) return 7;

    /* list_for_each_safe with deletion */
    list_for_each_safe(pos, tmp, &runqueue) {
        task = list_entry(pos, struct task_struct, tasks);
        list_del(pos);
    }
    if (!list_empty(&runqueue)) return 8;

    /* Move t3 back, verify */
    list_move_tail(&t3.tasks, &runqueue);
    if (list_empty(&runqueue)) return 9;

    count = 0;
    list_for_each(pos, &runqueue) {
        count++;
    }
    if (count != 1) return 10;

    task = list_first_entry(&runqueue, struct task_struct, tasks);
    if (task->pid != 3) return 11;

    return 0;
}
