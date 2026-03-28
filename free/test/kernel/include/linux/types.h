/* SPDX-License-Identifier: GPL-2.0 */
/* Stub types.h for free-cc kernel compilation testing */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <linux/compiler_types.h>

typedef unsigned char       __u8;
typedef unsigned short      __u16;
typedef unsigned int        __u32;
typedef unsigned long long  __u64;
typedef unsigned __int128   __u128;
typedef signed char         __s8;
typedef signed short        __s16;
typedef signed int          __s32;
typedef signed long long    __s64;
typedef __int128            __s128;

typedef __u8  u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;
typedef __u128 u128;
typedef __s8  s8;
typedef __s16 s16;
typedef __s32 s32;
typedef __s64 s64;
typedef __s128 s128;

typedef unsigned long size_t;
typedef long          ssize_t;
typedef long          ptrdiff_t;
typedef unsigned long uintptr_t;

typedef unsigned long __kernel_size_t;
typedef long          __kernel_ssize_t;
typedef long          __kernel_ptrdiff_t;
typedef long long     __kernel_loff_t;
typedef int           __kernel_pid_t;
typedef unsigned int  __kernel_uid_t;
typedef unsigned int  __kernel_gid_t;

typedef int           bool;
#define true  1
#define false 0

typedef __u16 __le16;
typedef __u16 __be16;
typedef __u32 __le32;
typedef __u32 __be32;
typedef __u64 __le64;
typedef __u64 __be64;

typedef __u8  uint8_t;
typedef __u16 uint16_t;
typedef __u32 uint32_t;
typedef __u64 uint64_t;
typedef __s8  int8_t;
typedef __s16 int16_t;
typedef __s32 int32_t;
typedef __s64 int64_t;

typedef unsigned long phys_addr_t;
typedef unsigned long dma_addr_t;

typedef unsigned int gfp_t;
typedef unsigned int fmode_t;

typedef long long loff_t;
typedef int       pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int dev_t;
typedef unsigned short umode_t;
typedef unsigned long sector_t;
typedef unsigned long blkcnt_t;
typedef long          clock_t;
typedef long long     time64_t;

/* Function pointer types */
typedef int (*cmp_func_t)(const void *a, const void *b);
typedef void (*swap_func_t)(void *a, void *b, int size);

/* Atomic types */
typedef struct { int counter; } atomic_t;
typedef struct { long counter; } atomic64_t;
typedef struct { long counter; } atomic_long_t;
#define ATOMIC_INIT(i) { (i) }
#define ATOMIC64_INIT(i) { (i) }
#define atomic_read(v) ((v)->counter)
#define atomic_set(v, i) ((v)->counter = (i))
#define atomic_inc(v) ((v)->counter++)
#define atomic_dec(v) ((v)->counter--)
#define atomic_add(i, v) ((v)->counter += (i))
#define atomic_sub(i, v) ((v)->counter -= (i))
#define atomic_inc_return(v) (++(v)->counter)
#define atomic_dec_return(v) (--(v)->counter)
#define atomic_dec_and_test(v) ((v)->counter-- == 1)

#define NULL ((void *)0)

/* Locking types (stubs) */
struct mutex { int dummy; };
struct spinlock { int dummy; };
typedef struct spinlock spinlock_t;
#define DEFINE_MUTEX(m) struct mutex m = { 0 }
#define DEFINE_SPINLOCK(s) spinlock_t s = { 0 }
#define __SPIN_LOCK_UNLOCKED(name) { 0 }

/* Linked list */
struct list_head { struct list_head *next, *prev; };
struct hlist_head { struct hlist_node *first; };
struct hlist_node { struct hlist_node *next, **pprev; };

/* Notifier */
struct notifier_block;
struct atomic_notifier_head { spinlock_t lock; struct notifier_block *head; };

/* __printf attribute */
#define __printf(a, b)
#define __scanf(a, b)

/* Kernel pointer annotations */
#define __user
#define __kernel
#define __safe
#define __force
#define __nocast
#define __iomem
#define __chk_user_ptr(x) (void)0
#define __chk_io_ptr(x) (void)0
#define __builtin_warning(x, ...) (1)
#define __must_hold(x)
#define __acquires(x)
#define __releases(x)
#define __acquire(x) (void)0
#define __release(x) (void)0
#define __cond_lock(x,c) (c)

/* Callbacks */
typedef void (*ctor_fn_t)(void);

/* Bitmap declaration */
#define DECLARE_BITMAP(name, bits) \
    unsigned long name[((bits) + 8 * sizeof(long) - 1) / (8 * sizeof(long))]

#endif /* _LINUX_TYPES_H */
