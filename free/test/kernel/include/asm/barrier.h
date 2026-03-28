/* SPDX-License-Identifier: GPL-2.0 */
/* Stub asm/barrier.h for free-cc kernel compilation testing (aarch64) */
#ifndef _ASM_BARRIER_H
#define _ASM_BARRIER_H

/* AArch64 memory barriers */
#define dmb(opt) do {} while (0)
#define dsb(opt) do {} while (0)
#define isb()    do {} while (0)

/* Full system barriers */
#define mb()     dmb(sy)
#define rmb()    dmb(ld)
#define wmb()    dmb(st)

/* SMP barriers */
#define smp_mb()  dmb(ish)
#define smp_rmb() dmb(ishld)
#define smp_wmb() dmb(ishst)

/* Store/load barriers */
#define smp_store_release(p, v) do { mb(); *(p) = (v); } while (0)
#define smp_load_acquire(p)     ({ typeof(*(p)) ___p = *(p); mb(); ___p; })

/* Read/write barrier depends */
#define smp_read_barrier_depends() do {} while (0)

/* Speculation barrier */
#define spec_bar() do {} while (0)

#endif /* _ASM_BARRIER_H */
