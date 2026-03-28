/* Kernel-style test */
#define __GNUC__ 4
#define __always_inline inline __attribute__((always_inline))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - __builtin_offsetof(type, member)); \
})
#define BUG_ON(cond) do { if (unlikely(cond)) __builtin_trap(); } while(0)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define min(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }

static __always_inline void list_add(struct list_head *new, struct list_head *head) {
    head->next->prev = new;
    new->next = head->next;
    new->prev = head;
    head->next = new;
}

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    int m = max(arr[1], arr[3]);
    int n = min(arr[0], arr[4]);
    struct list_head head = LIST_HEAD_INIT(head);
    struct list_head node;
    list_add(&node, &head);
    if (likely(m == 4) && unlikely(n == 0))
        return 0;
    return ARRAY_SIZE(arr) + m + n;
}
