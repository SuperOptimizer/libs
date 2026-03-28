/* SPDX-License-Identifier: GPL-2.0 */
/* Stub asm/atomic.h for free-cc kernel compilation testing (aarch64) */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/types.h>

/*
 * Atomic operations using simple non-atomic stubs.
 * These are sufficient for single-threaded kernel stub compilation.
 * The core atomic_t ops are in linux/types.h; this adds the
 * architecture-specific variants.
 */

#define atomic_cmpxchg(v, old, new) ({ \
    int __ret = (v)->counter; \
    if (__ret == (old)) (v)->counter = (new); \
    __ret; \
})

#define atomic_xchg(v, new) ({ \
    int __ret = (v)->counter; \
    (v)->counter = (new); \
    __ret; \
})

#define atomic_add_return(i, v) ((v)->counter += (i), (v)->counter)
#define atomic_sub_return(i, v) ((v)->counter -= (i), (v)->counter)
#define atomic_inc_and_test(v) (++(v)->counter == 0)
#define atomic_sub_and_test(i, v) (((v)->counter -= (i)) == 0)
#define atomic_add_negative(i, v) (((v)->counter += (i)) < 0)
#define atomic_fetch_add(i, v) ({ int __o = (v)->counter; (v)->counter += (i); __o; })
#define atomic_fetch_sub(i, v) ({ int __o = (v)->counter; (v)->counter -= (i); __o; })
#define atomic_fetch_and(i, v) ({ int __o = (v)->counter; (v)->counter &= (i); __o; })
#define atomic_fetch_or(i, v) ({ int __o = (v)->counter; (v)->counter |= (i); __o; })

/* 64-bit atomics */
#define atomic64_read(v) ((v)->counter)
#define atomic64_set(v, i) ((v)->counter = (i))
#define atomic64_add(i, v) ((v)->counter += (i))
#define atomic64_sub(i, v) ((v)->counter -= (i))
#define atomic64_inc(v) ((v)->counter++)
#define atomic64_dec(v) ((v)->counter--)
#define atomic64_inc_return(v) (++(v)->counter)
#define atomic64_dec_return(v) (--(v)->counter)
#define atomic64_add_return(i, v) ((v)->counter += (i), (v)->counter)
#define atomic64_cmpxchg(v, old, new) ({ \
    long __ret = (v)->counter; \
    if (__ret == (old)) (v)->counter = (new); \
    __ret; \
})

#endif /* _ASM_ATOMIC_H */
