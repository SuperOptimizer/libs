/* SPDX-License-Identifier: GPL-2.0 */
/* Stub compiler.h for free-cc kernel compilation testing */
#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#include <linux/compiler_types.h>

/* WRITE_ONCE / READ_ONCE stubs */
#define WRITE_ONCE(x, val) ((x) = (val))
#define READ_ONCE(x) (x)

/* Optimization barriers */
#define barrier() do {} while (0)
#define barrier_data(ptr) do {} while (0)

/* __ADDRESSABLE stub */
#define __ADDRESSABLE(sym)

/* Unreachable */
#define unreachable() do {} while (1)

/* Data dependency barrier */
#define smp_read_barrier_depends() do {} while (0)
#define smp_store_mb(var, value) do { (var) = (value); barrier(); } while (0)
#define smp_mb__before_atomic() barrier()
#define smp_mb__after_atomic() barrier()

/* Annotate */
#define data_race(expr) (expr)
#define __uninitialized
#define __designated_init
#define __no_sanitize_or_inline inline

/* Prevent tail call */
#define __no_tail_call

/* Compiler-specific checks */
#define compiletime_assert(condition, msg)
#define compiletime_assert_atomic_type(t)

#endif /* __LINUX_COMPILER_H */
