/* EXPECTED: 0 */
/* Mock kernel workqueue patterns - function pointer dispatch, callbacks */

#define NULL ((void *)0)
#define offsetof(type, member) ((unsigned long)&((type *)0)->member)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

typedef unsigned long size_t;

/* ---- Linked list (minimal) ---- */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

static void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static void list_add_tail(struct list_head *new_node, struct list_head *head) {
    struct list_head *prev;
    prev = head->prev;
    prev->next = new_node;
    new_node->prev = prev;
    new_node->next = head;
    head->prev = new_node;
}

static void list_del(struct list_head *entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = NULL;
    entry->prev = NULL;
}

static int list_empty(const struct list_head *head) {
    return head->next == head;
}

#define list_first_entry(ptr, type, member) \
    container_of((ptr)->next, type, member)

/* ---- Work struct ---- */
typedef void (*work_func_t)(struct work_struct *);

struct work_struct {
    unsigned long data;
    struct list_head entry;
    work_func_t func;
};

#define INIT_WORK(w, f) do { \
    (w)->data = 0; \
    INIT_LIST_HEAD(&(w)->entry); \
    (w)->func = (f); \
} while (0)

/* ---- Delayed work ---- */
struct delayed_work {
    struct work_struct work;
    unsigned long delay;
};

#define INIT_DELAYED_WORK(dw, f) do { \
    INIT_WORK(&(dw)->work, (f)); \
    (dw)->delay = 0; \
} while (0)

/* ---- Workqueue ---- */
#define WQ_MAX_ACTIVE 16

struct workqueue_struct {
    const char *name;
    struct list_head worklist;
    int num_active;
    int max_active;
    unsigned long flags;
};

static struct workqueue_struct *
alloc_workqueue(const char *name, unsigned long flags, int max_active) {
    /* Use static storage for simplicity */
    static struct workqueue_struct wqs[4];
    static int wq_count;
    struct workqueue_struct *wq;

    if (wq_count >= 4) return NULL;
    wq = &wqs[wq_count++];
    wq->name = name;
    wq->flags = flags;
    wq->max_active = max_active ? max_active : WQ_MAX_ACTIVE;
    wq->num_active = 0;
    INIT_LIST_HEAD(&wq->worklist);
    return wq;
}

static int queue_work(struct workqueue_struct *wq, struct work_struct *work) {
    if (wq->num_active >= wq->max_active)
        return 0;
    list_add_tail(&work->entry, &wq->worklist);
    wq->num_active++;
    return 1;
}

static int queue_delayed_work(struct workqueue_struct *wq,
                              struct delayed_work *dwork,
                              unsigned long delay) {
    dwork->delay = delay;
    return queue_work(wq, &dwork->work);
}

/* Process all pending work items */
static int flush_workqueue(struct workqueue_struct *wq) {
    int processed;
    struct work_struct *work;

    processed = 0;
    while (!list_empty(&wq->worklist)) {
        work = list_first_entry(&wq->worklist, struct work_struct, entry);
        list_del(&work->entry);
        wq->num_active--;
        /* dispatch through function pointer */
        work->func(work);
        processed++;
    }
    return processed;
}

/* ---- Tasklet mock (simpler callback mechanism) ---- */
struct tasklet_struct {
    struct tasklet_struct *next;
    unsigned long state;
    unsigned long count;
    void (*func)(unsigned long);
    unsigned long data;
};

#define TASKLET_STATE_SCHED  0
#define TASKLET_STATE_RUN    1

static void tasklet_init(struct tasklet_struct *t,
                         void (*func)(unsigned long),
                         unsigned long data) {
    t->next = NULL;
    t->state = 0;
    t->count = 0;
    t->func = func;
    t->data = data;
}

static void tasklet_schedule(struct tasklet_struct *t) {
    t->state |= (1UL << TASKLET_STATE_SCHED);
}

static void tasklet_run(struct tasklet_struct *t) {
    if (t->state & (1UL << TASKLET_STATE_SCHED)) {
        t->state |= (1UL << TASKLET_STATE_RUN);
        t->state &= ~(1UL << TASKLET_STATE_SCHED);
        t->func(t->data);
        t->state &= ~(1UL << TASKLET_STATE_RUN);
    }
}

/* ---- Test callbacks ---- */
static int work_counter;
static int tasklet_counter;

static void my_work_func(struct work_struct *work) {
    work->data++;
    work_counter++;
}

static void my_delayed_func(struct work_struct *work) {
    struct delayed_work *dw;
    dw = container_of(work, struct delayed_work, work);
    work->data += dw->delay;
    work_counter++;
}

static void my_tasklet_func(unsigned long data) {
    tasklet_counter += (int)data;
}

/* ---- Test ---- */
int main(void) {
    struct workqueue_struct *wq;
    struct work_struct w1, w2;
    struct delayed_work dw1;
    struct tasklet_struct tl;
    int processed;

    /* Create workqueue */
    wq = alloc_workqueue("test_wq", 0, 4);
    if (!wq) return 1;

    /* Init work items */
    INIT_WORK(&w1, my_work_func);
    INIT_WORK(&w2, my_work_func);
    INIT_DELAYED_WORK(&dw1, my_delayed_func);

    /* Queue work */
    if (!queue_work(wq, &w1)) return 2;
    if (!queue_work(wq, &w2)) return 3;
    if (!queue_delayed_work(wq, &dw1, 100)) return 4;

    /* Verify queue state */
    if (wq->num_active != 3) return 5;
    if (list_empty(&wq->worklist)) return 6;

    /* Flush - this calls function pointers */
    work_counter = 0;
    processed = flush_workqueue(wq);
    if (processed != 3) return 7;
    if (work_counter != 3) return 8;

    /* Verify work data was updated */
    if (w1.data != 1) return 9;
    if (w2.data != 1) return 10;
    if (dw1.work.data != 100) return 11;

    /* Queue is now empty */
    if (!list_empty(&wq->worklist)) return 12;
    if (wq->num_active != 0) return 13;

    /* Test tasklet */
    tasklet_counter = 0;
    tasklet_init(&tl, my_tasklet_func, 42);
    tasklet_schedule(&tl);
    tasklet_run(&tl);
    if (tasklet_counter != 42) return 14;

    /* Tasklet should not be scheduled anymore */
    if (tl.state & (1UL << TASKLET_STATE_SCHED)) return 15;
    if (tl.state & (1UL << TASKLET_STATE_RUN)) return 16;

    return 0;
}
