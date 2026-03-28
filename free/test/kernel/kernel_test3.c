/* Kernel-style test 3: more complex patterns */

/* ---- GCC attributes ---- */
#define __section(s)       __attribute__((section(s)))
#define __aligned(x)       __attribute__((aligned(x)))
#define __weak             __attribute__((weak))
#define __cold             __attribute__((cold))
#define __noreturn         __attribute__((noreturn))
#define __packed           __attribute__((packed))
#define __used             __attribute__((used))
#define __maybe_unused     __attribute__((unused))
#define __always_inline    inline __attribute__((always_inline))
#define __noinline         __attribute__((noinline))
#define __deprecated(msg)  __attribute__((deprecated(msg)))

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* ---- type tricks ---- */
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/* ---- BUILD_BUG_ON style ---- */
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

/* ---- ARRAY_SIZE with type checking ---- */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ---- min/max with strict type checking ---- */
#define min(x, y) ({ \
    typeof(x) _min1 = (x); \
    typeof(y) _min2 = (y); \
    (void) (&_min1 == &_min2); \
    _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({ \
    typeof(x) _max1 = (x); \
    typeof(y) _max2 = (y); \
    (void) (&_max1 == &_max2); \
    _max1 > _max2 ? _max1 : _max2; })

#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)

/* ---- swap ---- */
#define swap(a, b) \
    do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/* ---- list_head ---- */
struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static __always_inline void __list_add(struct list_head *new,
    struct list_head *prev, struct list_head *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static __always_inline void list_add(struct list_head *new,
    struct list_head *head) {
    __list_add(new, head, head->next);
}

static __always_inline void list_add_tail(struct list_head *new,
    struct list_head *head) {
    __list_add(new, head->prev, head);
}

static __always_inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

/* ---- container_of ---- */
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - __builtin_offsetof(type, member)); \
})

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/* ---- hlist ---- */
struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

/* ---- rcu read (simplified) ---- */
#define rcu_dereference(p) ({ \
    typeof(p) _________p1 = (p); \
    _________p1; \
})

/* ---- test structures ---- */
struct task_struct {
    int pid;
    int state;
    const char *comm;
    struct list_head tasks;
    struct list_head children;
    int prio;
    unsigned long flags;
};

/* ---- test it all ---- */
int main(void) {
    LIST_HEAD(task_list);
    struct task_struct init_task;
    struct task_struct task1;
    struct task_struct *found;
    struct list_head *pos;
    int arr[] = {10, 20, 30, 40, 50};
    int a, b, c;

    /* initialize tasks */
    init_task.pid = 0;
    init_task.state = 0;
    init_task.comm = "init";
    init_task.prio = 120;
    init_task.flags = 0;
    INIT_LIST_HEAD(&init_task.tasks);
    INIT_LIST_HEAD(&init_task.children);

    task1.pid = 1;
    task1.state = 1;
    task1.comm = "kthreadd";
    task1.prio = 120;
    task1.flags = 0;
    INIT_LIST_HEAD(&task1.tasks);
    INIT_LIST_HEAD(&task1.children);

    /* add to list */
    list_add_tail(&init_task.tasks, &task_list);
    list_add_tail(&task1.tasks, &task_list);

    /* container_of */
    pos = task_list.next;
    found = list_entry(pos, struct task_struct, tasks);

    /* min/max/clamp */
    a = min(arr[0], arr[4]);
    b = max(arr[1], arr[3]);
    c = clamp(arr[2], 15, 35);

    /* swap */
    swap(a, b);

    /* rcu_dereference */
    {
        struct task_struct *p;
        p = rcu_dereference(found);
        if (likely(p != 0))
            a += p->pid;
    }

    if (!list_empty(&task_list) && found->pid == 0)
        return 0;
    return a + b + c;
}
