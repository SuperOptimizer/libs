/* SPDX-License-Identifier: GPL-2.0 */
/* Stub compiler_types.h for free-cc kernel compilation testing */
#ifndef _LINUX_COMPILER_TYPES_H
#define _LINUX_COMPILER_TYPES_H

/* Attributes - stub out for freestanding compiler */
#define __section(s)
#define __used
#define __maybe_unused
#define __always_unused
#define __aligned(x)
#define __always_inline inline
#define __attribute_const__
#define __pure
#define __packed
#define __weak
#define __visible
#define __cold
#define __hot
#define __noreturn
#define __must_check
#define __force
#define __iomem
#define __user
#define __kernel
#define __percpu
#define __rcu
#define __bitwise
#define __deprecated
#define __init
#define __exit
#define __initdata
#define __exitdata
#define __initconst
#define __read_mostly
#define __cacheline_aligned
#define ____cacheline_aligned
#define __randomize_layout
#define __no_randomize_layout
#define __designated_init
#define __noinline
#define noinline
#define noinline_for_stack
#define __ro_after_init
#define __fallthrough
#define fallthrough do {} while (0)

/* Compiler builtins */
#define __builtin_expect(x, y) (x)
#define __builtin_constant_p(x) 0
#define __has_builtin(x) 0

/* Stringify */
#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)

/* BTF */
#define BTF_TYPE_TAG(value)

/* likely/unlikely */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* compiletime assertions */
#define __compiletime_assert(condition, msg, prefix, suffix)
#define __compiletime_warning(message)
#define __compiletime_error(message)

/* Type checks */
#define __same_type(a, b) 1
#define __native_word(t) (sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
    sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

/* Static assert - map to _Static_assert which the compiler supports */
#define static_assert(expr, ...) _Static_assert(expr, "" __VA_ARGS__)

/* __printf attribute */
#define __printf(a, b)

/* Copy/paste safety */
#define __diag_push()
#define __diag_pop()
#define __diag(string)
#define __diag_ignore(compiler, version, option, comment)
#define __diag_ignore_all(option, comment)

/* Overflow checks (stub) */
#define check_mul_overflow(a, b, d) ({*(d) = (a) * (b); 0;})
#define check_add_overflow(a, b, d) ({*(d) = (a) + (b); 0;})
#define check_sub_overflow(a, b, d) ({*(d) = (a) - (b); 0;})

#endif /* _LINUX_COMPILER_TYPES_H */
