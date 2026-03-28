/* Kernel pattern: exchange operations and barriers */
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/kernel.h>

struct cache_line {
    unsigned long data;
    unsigned long version;
    int valid;
    int dirty;
};

static unsigned long xchg_value(unsigned long *ptr, unsigned long new_val)
{
    unsigned long old = *ptr;
    *ptr = new_val;
    return old;
}

static int cmpxchg_value(int *ptr, int old, int new)
{
    int prev = *ptr;
    if (prev == old)
        *ptr = new;
    return prev;
}

static void update_cache(struct cache_line *cl, unsigned long data)
{
    unsigned long old_version;

    old_version = xchg_value(&cl->version, cl->version + 1);
    cl->data = data;
    cl->dirty = 1;
    (void)old_version;
}

static int try_invalidate(struct cache_line *cl)
{
    return cmpxchg_value(&cl->valid, 1, 0) == 1;
}

static void flush_cache(struct cache_line *cl)
{
    if (cl->dirty) {
        /* Write back */
        cl->dirty = 0;
    }
}

void test_xchg(void)
{
    struct cache_line cl;
    unsigned long old;
    int success;

    cl.data = 0x1234;
    cl.version = 0;
    cl.valid = 1;
    cl.dirty = 0;

    update_cache(&cl, 0x5678);

    old = xchg_value(&cl.data, 0xABCD);
    (void)old;

    success = try_invalidate(&cl);
    (void)success;

    flush_cache(&cl);
}
