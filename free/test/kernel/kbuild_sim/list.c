/*
 * list.c - Simulated kernel linked list implementation
 *
 * Exercises:
 *   - container_of pattern
 *   - list_head doubly-linked list
 *   - Function pointers
 *   - Static variables
 */
#include "types.h"

/* --- list_head --- */

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

static void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static void __list_add(struct list_head *new_node,
                       struct list_head *prev,
                       struct list_head *next)
{
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

static void list_add(struct list_head *new_node,
                     struct list_head *head)
{
    __list_add(new_node, head, head->next);
}

static void list_add_tail(struct list_head *new_node,
                          struct list_head *head)
{
    __list_add(new_node, head->prev, head);
}

static void __list_del(struct list_head *prev,
                       struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static int list_empty(const struct list_head *head)
{
    return head->next == head;
}

/* --- Task struct using list_head --- */

#define TASK_NAME_LEN 16

struct task_struct {
    int pid;
    int priority;
    char name[TASK_NAME_LEN];
    struct list_head tasks;
};

/* Use container_of to get task from list entry */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* --- Static task list --- */

static LIST_HEAD(task_list);
static int next_pid = 1;

/* Simple string copy */
static void kstrcpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* --- Public interface --- */

int task_create(struct task_struct *task, const char *name,
                int priority)
{
    task->pid = next_pid++;
    task->priority = priority;
    kstrcpy(task->name, name, TASK_NAME_LEN);
    INIT_LIST_HEAD(&task->tasks);
    list_add_tail(&task->tasks, &task_list);
    return task->pid;
}

void task_remove(struct task_struct *task)
{
    list_del(&task->tasks);
}

int task_count(void)
{
    struct list_head *pos;
    int count = 0;
    list_for_each(pos, &task_list) {
        count++;
    }
    return count;
}

struct task_struct *task_find_by_pid(int pid)
{
    struct list_head *pos;
    list_for_each(pos, &task_list) {
        struct task_struct *task = list_entry(pos,
                                             struct task_struct,
                                             tasks);
        if (task->pid == pid)
            return task;
    }
    return NULL;
}

/* Test the list implementation */
int list_test(void)
{
    struct task_struct t1, t2, t3;
    struct task_struct *found;

    task_create(&t1, "init", 0);
    task_create(&t2, "kthread", 5);
    task_create(&t3, "worker", 10);

    if (task_count() != 3)
        return 1;

    found = task_find_by_pid(t2.pid);
    if (found == NULL)
        return 2;
    if (found->priority != 5)
        return 3;

    task_remove(&t2);
    if (task_count() != 2)
        return 4;

    found = task_find_by_pid(t2.pid);
    if (found != NULL)
        return 5;

    /* Clean up */
    task_remove(&t1);
    task_remove(&t3);
    if (!list_empty(&task_list))
        return 6;

    return 0;
}
