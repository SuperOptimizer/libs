/* EXPECTED: 0 */
/* Test kernel-style C patterns that free-cc must handle */

/* Compiler detection macros */
#ifndef __GNUC__
#define __GNUC__ 4
#endif
#ifndef __GNUC_MINOR__
#define __GNUC_MINOR__ 0
#endif

/* Common kernel attributes */
#define __always_inline inline __attribute__((always_inline))
#define __noinline __attribute__((noinline))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __section(s) __attribute__((section(s)))
#define __used __attribute__((used))
#define __unused __attribute__((unused))
#define __weak __attribute__((weak))
#define __noreturn __attribute__((noreturn))
#define __pure __attribute__((pure))
#define __cold __attribute__((cold))

/* Branch prediction */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* container_of using statement expression + typeof */
#define offsetof(type, member) __builtin_offsetof(type, member)
#define container_of(ptr, type, member) ({          \
    const typeof(((type *)0)->member) *__mptr = (ptr);  \
    (type *)((char *)__mptr - offsetof(type, member));  \
})

/* Min/max with typeof */
#define min(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })

/* BUG_ON */
#define BUG_ON(cond) do { if (unlikely(cond)) __builtin_trap(); } while(0)

/* READ_ONCE / WRITE_ONCE (simplified) */
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile typeof(x) *)&(x) = (val))

/* Linked list */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list)
{
    WRITE_ONCE(list->next, list);
    list->prev = list;
}

static __always_inline void __list_add(struct list_head *new,
                                       struct list_head *prev,
                                       struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    WRITE_ONCE(prev->next, new);
}

static __always_inline void list_add(struct list_head *new,
                                     struct list_head *head)
{
    __list_add(new, head, head->next);
}

static __always_inline int list_empty(const struct list_head *head)
{
    return READ_ONCE(head->next) == head;
}

/* Test struct with bitfields */
struct page_flags {
    unsigned int type : 4;
    unsigned int order : 4;
    unsigned int flags : 24;
};

/* Test struct for container_of */
struct task_struct {
    int pid;
    int state;
    struct list_head tasks;
    char comm[16];
};

/* Overflow check */
static int safe_add(int a, int b, int *result)
{
    return __builtin_add_overflow(a, b, result);
}

int main(void)
{
    int arr[] = {5, 3, 8, 1, 9, 2, 7};
    int m, n, result;
    struct task_struct task;
    struct list_head *ptr;
    struct task_struct *found;
    struct page_flags pf;
    LIST_HEAD(head);

    /* Test min/max */
    m = max(arr[0], arr[4]);
    n = min(arr[1], arr[3]);

    /* Test likely/unlikely */
    if (likely(m == 9) && unlikely(n == 0))
        return 1;

    /* Test ARRAY_SIZE */
    if (ARRAY_SIZE(arr) != 7)
        return 2;

    /* Test list operations */
    task.pid = 42;
    task.state = 0;
    INIT_LIST_HEAD(&task.tasks);
    list_add(&task.tasks, &head);

    if (list_empty(&head))
        return 3;

    /* Test container_of */
    ptr = head.next;
    found = container_of(ptr, struct task_struct, tasks);
    if (found->pid != 42)
        return 4;

    /* Test bitfields */
    pf.type = 3;
    pf.order = 5;
    pf.flags = 0xABCDE;
    if (pf.type != 3 || pf.order != 5)
        return 5;

    /* Test overflow */
    if (safe_add(1, 2, &result))
        return 6;
    if (result != 3)
        return 7;

    /* Test BUG_ON (should not trigger) */
    BUG_ON(0);

    return 0;
}
