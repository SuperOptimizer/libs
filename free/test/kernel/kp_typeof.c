/* Kernel pattern: typeof and statement expressions */
#include <linux/types.h>
#include <linux/kernel.h>

/* Custom swap macro using typeof */
#define MY_SWAP(a, b) do { \
    typeof(a) __tmp = (a); \
    (a) = (b); \
    (b) = __tmp; \
} while (0)

/* Type-safe max using typeof */
#define TYPE_MAX(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a > _b ? _a : _b; \
})

/* Type-safe min */
#define TYPE_MIN(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _a < _b ? _a : _b; \
})

/* Absolute value using typeof */
#define TYPE_ABS(x) ({ \
    typeof(x) _x = (x); \
    _x < 0 ? -_x : _x; \
})

/* Round up to multiple */
#define ROUND_UP(x, mult) ({ \
    typeof(x) _x = (x); \
    typeof(mult) _m = (mult); \
    ((_x + _m - 1) / _m) * _m; \
})

/* Array size */
#define MY_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct pair {
    int first;
    int second;
};

static void sort_pair(struct pair *p)
{
    if (p->first > p->second)
        MY_SWAP(p->first, p->second);
}

void test_typeof(void)
{
    int a = 10, b = 20;
    long la = -100L, lb = 50L;
    unsigned long ua = 42UL, ub = 99UL;
    struct pair p = { .first = 50, .second = 10 };
    int arr[] = { 1, 2, 3, 4, 5 };
    int mx;
    long mn;
    unsigned long umx;
    long ab;
    int rounded;
    int count;

    MY_SWAP(a, b);
    mx = TYPE_MAX(a, b);
    mn = TYPE_MIN(la, lb);
    umx = TYPE_MAX(ua, ub);
    ab = TYPE_ABS(la);
    rounded = ROUND_UP(a, 8);
    count = (int)MY_ARRAY_SIZE(arr);

    sort_pair(&p);

    (void)mx; (void)mn; (void)umx; (void)ab;
    (void)rounded; (void)count;
}
