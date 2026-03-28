/* SPDX-License-Identifier: GPL-2.0 */
/* Stub spinlock.h for free-cc kernel compilation testing */
#ifndef _LINUX_SPINLOCK_H
#define _LINUX_SPINLOCK_H

#include <linux/types.h>
#include <linux/compiler.h>

/* Raw spinlock */
typedef struct {
    int locked;
} raw_spinlock_t;

#define __RAW_SPIN_LOCK_INITIALIZER { 0 }
#define __RAW_SPIN_LOCK_UNLOCKED __RAW_SPIN_LOCK_INITIALIZER
#define DEFINE_RAW_SPINLOCK(x) raw_spinlock_t x = __RAW_SPIN_LOCK_INITIALIZER

/* spinlock_t is already typedef'd in types.h, but define ops here */
#define SPIN_LOCK_UNLOCKED { 0 }
#define __SPIN_LOCK_INITIALIZER { 0 }

#define spin_lock_init(lock) do { (lock)->dummy = 0; } while (0)
#define spin_lock(lock) do { (void)(lock); } while (0)
#define spin_unlock(lock) do { (void)(lock); } while (0)
#define spin_lock_irq(lock) do { (void)(lock); } while (0)
#define spin_unlock_irq(lock) do { (void)(lock); } while (0)
#define spin_lock_irqsave(lock, flags) do { (void)(lock); (void)(flags); flags = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
#define spin_lock_bh(lock) do { (void)(lock); } while (0)
#define spin_unlock_bh(lock) do { (void)(lock); } while (0)
#define spin_trylock(lock) ({ (void)(lock); 1; })
#define spin_is_locked(lock) ({ (void)(lock); 0; })

#define raw_spin_lock_init(lock) do { (lock)->locked = 0; } while (0)
#define raw_spin_lock(lock) do { (void)(lock); } while (0)
#define raw_spin_unlock(lock) do { (void)(lock); } while (0)
#define raw_spin_lock_irq(lock) do { (void)(lock); } while (0)
#define raw_spin_unlock_irq(lock) do { (void)(lock); } while (0)
#define raw_spin_lock_irqsave(lock, flags) do { (void)(lock); flags = 0; } while (0)
#define raw_spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)

/* RW lock stubs */
typedef struct {
    int dummy;
} rwlock_t;

#define __RW_LOCK_UNLOCKED(name) { 0 }
#define DEFINE_RWLOCK(x) rwlock_t x = __RW_LOCK_UNLOCKED(x)

#define rwlock_init(lock) do { (lock)->dummy = 0; } while (0)
#define read_lock(lock) do { (void)(lock); } while (0)
#define read_unlock(lock) do { (void)(lock); } while (0)
#define write_lock(lock) do { (void)(lock); } while (0)
#define write_unlock(lock) do { (void)(lock); } while (0)
#define read_lock_irqsave(lock, flags) do { (void)(lock); flags = 0; } while (0)
#define read_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
#define write_lock_irqsave(lock, flags) do { (void)(lock); flags = 0; } while (0)
#define write_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)

/* Mutex operations (already defined in types.h as struct) */
#define mutex_init(m) do { (m)->dummy = 0; } while (0)
#define mutex_lock(m) do { (void)(m); } while (0)
#define mutex_unlock(m) do { (void)(m); } while (0)
#define mutex_trylock(m) ({ (void)(m); 1; })
#define mutex_is_locked(m) ({ (void)(m); 0; })

/* Seqcount stubs */
typedef struct {
    unsigned int sequence;
} seqcount_t;

#define seqcount_init(s) do { (s)->sequence = 0; } while (0)
#define raw_read_seqcount(s)   ((s)->sequence)
#define read_seqcount_begin(s) ((s)->sequence)
#define read_seqcount_retry(s, start) (0)
#define write_seqcount_begin(s) do { (s)->sequence++; } while (0)
#define write_seqcount_end(s) do { (s)->sequence++; } while (0)

/* Seqlock stubs */
typedef struct {
    seqcount_t seqcount;
    spinlock_t lock;
} seqlock_t;

#define __SEQLOCK_UNLOCKED(name) { 0, { 0 } }
#define DEFINE_SEQLOCK(x) seqlock_t x = __SEQLOCK_UNLOCKED(x)

/* Local IRQ stubs */
#define local_irq_save(flags) do { flags = 0; } while (0)
#define local_irq_restore(flags) do { (void)(flags); } while (0)
#define local_irq_disable() do {} while (0)
#define local_irq_enable() do {} while (0)

/* Preemption stubs */
#define preempt_disable() do {} while (0)
#define preempt_enable() do {} while (0)

/* Lockdep stubs */
struct lock_class_key { int dummy; };
#define lockdep_init_map(lock, name, key, sub) do { (void)(lock); (void)(name); (void)(key); (void)(sub); } while (0)
#define lockdep_set_class(lock, key) do { (void)(lock); (void)(key); } while (0)
#define DEFINE_SPINLOCK(x) spinlock_t x = { 0 }
#define __SPIN_LOCK_UNLOCKED(name) { 0 }

/* num_possible_cpus stub */
#define num_possible_cpus() 1

#endif /* _LINUX_SPINLOCK_H */
