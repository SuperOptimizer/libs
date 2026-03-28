/* EXPECTED: 0 */
/* Test kernel-style atomic operations and barriers */

#ifndef __GNUC__
#define __GNUC__ 4
#endif

#define barrier() asm volatile("" ::: "memory")
#define smp_mb() asm volatile("dmb ish" ::: "memory")
#define smp_wmb() asm volatile("dmb ishst" ::: "memory")
#define smp_rmb() asm volatile("dmb ishld" ::: "memory")

#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile typeof(x) *)&(x) = (val))

typedef struct {
    volatile int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }
#define atomic_read(v) READ_ONCE((v)->counter)
#define atomic_set(v, i) WRITE_ONCE((v)->counter, (i))

static inline int atomic_add_return(int i, atomic_t *v)
{
    int result;
    result = v->counter + i;
    v->counter = result;
    return result;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
    int result;
    result = v->counter - i;
    v->counter = result;
    return result;
}

static inline int atomic_inc_return(atomic_t *v)
{
    return atomic_add_return(1, v);
}

#define atomic_inc(v) atomic_add_return(1, v)
#define atomic_dec(v) atomic_sub_return(1, v)

/* Test spinlock-like pattern */
typedef struct {
    volatile int locked;
} spinlock_t;

static inline void spin_lock(spinlock_t *lock)
{
    while (READ_ONCE(lock->locked))
        ;
    WRITE_ONCE(lock->locked, 1);
    smp_mb();
}

static inline void spin_unlock(spinlock_t *lock)
{
    smp_mb();
    WRITE_ONCE(lock->locked, 0);
}

int main(void)
{
    atomic_t counter = ATOMIC_INIT(0);
    spinlock_t lock = { 0 };
    int val;

    /* Test atomic operations */
    atomic_set(&counter, 10);
    if (atomic_read(&counter) != 10)
        return 1;

    val = atomic_add_return(5, &counter);
    if (val != 15)
        return 2;

    atomic_inc(&counter);
    if (atomic_read(&counter) != 16)
        return 3;

    atomic_dec(&counter);
    if (atomic_read(&counter) != 15)
        return 4;

    /* Test barrier */
    barrier();

    /* Test spinlock */
    spin_lock(&lock);
    if (!lock.locked)
        return 5;
    spin_unlock(&lock);
    if (lock.locked)
        return 6;

    return 0;
}
