/* Kernel pattern: spinlock and synchronization */
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>

struct guarded_counter {
    spinlock_t lock;
    unsigned long count;
    unsigned long max_seen;
};

static DEFINE_SPINLOCK(global_lock);
static unsigned long global_value;

static void init_counter(struct guarded_counter *gc)
{
    spin_lock_init(&gc->lock);
    gc->count = 0;
    gc->max_seen = 0;
}

static void increment_counter(struct guarded_counter *gc, unsigned long val)
{
    unsigned long flags;
    spin_lock_irqsave(&gc->lock, flags);
    gc->count += val;
    if (gc->count > gc->max_seen)
        gc->max_seen = gc->count;
    spin_unlock_irqrestore(&gc->lock, flags);
}

static unsigned long read_counter(struct guarded_counter *gc)
{
    unsigned long val;
    unsigned long flags;
    spin_lock_irqsave(&gc->lock, flags);
    val = gc->count;
    spin_unlock_irqrestore(&gc->lock, flags);
    return val;
}

static void update_global(unsigned long new_val)
{
    spin_lock(&global_lock);
    global_value = new_val;
    spin_unlock(&global_lock);
}

void test_spinlocks(void)
{
    struct guarded_counter gc;
    unsigned long val;
    init_counter(&gc);
    increment_counter(&gc, 42);
    increment_counter(&gc, 58);
    val = read_counter(&gc);
    update_global(val);
    (void)val;
}
