/* Kernel pattern: min/max macros and clamping */
#include <linux/types.h>
#include <linux/minmax.h>
#include <linux/kernel.h>

struct range {
    int low;
    int high;
};

static int clamp_value(int val, int lo, int hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static unsigned long clamp_ulong(unsigned long val, unsigned long lo,
                                 unsigned long hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static int range_width(const struct range *r)
{
    return r->high - r->low;
}

static int ranges_overlap(const struct range *a, const struct range *b)
{
    return a->low < b->high && b->low < a->high;
}

static struct range range_intersect(const struct range *a,
                                    const struct range *b)
{
    struct range result;
    result.low = a->low > b->low ? a->low : b->low;
    result.high = a->high < b->high ? a->high : b->high;
    if (result.low > result.high)
        result.low = result.high = 0;
    return result;
}

static struct range range_union(const struct range *a,
                                const struct range *b)
{
    struct range result;
    result.low = a->low < b->low ? a->low : b->low;
    result.high = a->high > b->high ? a->high : b->high;
    return result;
}

void test_minmax(void)
{
    struct range r1 = { .low = 10, .high = 50 };
    struct range r2 = { .low = 30, .high = 70 };
    struct range isect;
    struct range uni;
    int clamped;
    unsigned long clamped_u;
    int w;

    clamped = clamp_value(100, 0, 50);
    clamped_u = clamp_ulong(5, 10, 100);
    w = range_width(&r1);
    (void)clamped; (void)clamped_u; (void)w;

    if (ranges_overlap(&r1, &r2)) {
        isect = range_intersect(&r1, &r2);
        (void)isect;
    }
    uni = range_union(&r1, &r2);
    (void)uni;
}
