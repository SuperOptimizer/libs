/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kernel.h for free-cc kernel compilation testing */
#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/minmax.h>
#include <linux/hex.h>
#include <linux/printk.h>
#include <linux/sprintf.h>
#include <linux/panic.h>
#include <linux/bug.h>
#include <linux/bitops.h>
#include <linux/jump_label.h>

/* Container of */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

/* Array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Alignment */
#define ALIGN(x, a)          (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a)     ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a)     (((x) & ((a) - 1)) == 0)
#define PTR_ALIGN(p, a)      ((typeof(p))ALIGN((unsigned long)(p), (a)))

/* Rounding */
#define DIV_ROUND_UP(n, d)   (((n) + (d) - 1) / (d))
#define DIV_ROUND_DOWN_ULL(ll, d) ({ unsigned long long _tmp = (ll); _tmp / (d); })
#define DIV_ROUND_UP_ULL(ll, d) DIV_ROUND_UP((unsigned long long)(ll), (unsigned long long)(d))
#define roundup(x, y)        ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y)      ((x) - ((x) % (y)))
#define round_up(x, y)       ((((x) - 1) | ((y) - 1)) + 1)
#define round_down(x, y)     ((x) & ~((y) - 1))

/* abs */
#define abs(x) ((x) < 0 ? -(x) : (x))

/* Swap bytes */
#define upper_32_bits(n)  ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n)  ((u32)(n))

/* might_sleep stub */
#define might_sleep() do {} while (0)
#define might_resched() do {} while (0)

/* typecheck */
#define typecheck(type, x) \
    ({ type __dummy; typeof(x) __dummy2; (void)(&__dummy == &__dummy2); 1; })

/* Number of elements in struct member */
/* swap macro */
#define swap(a, b) \
    do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))

/* struct_size helper */
#define struct_size(p, member, n) \
    (sizeof(*(p)) + sizeof(*((p)->member)) * (n))

/* ERR_PTR / IS_ERR - include from err.h */
#include <linux/err.h>

#endif /* _LINUX_KERNEL_H */
