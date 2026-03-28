/* Minimal kernel types for kbuild simulation */
#ifndef _KBUILD_SIM_TYPES_H
#define _KBUILD_SIM_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         s8;
typedef signed short        s16;
typedef signed int          s32;
typedef signed long long    s64;

typedef unsigned long  size_t;
typedef long           ssize_t;
typedef unsigned long  uintptr_t;

typedef int bool;
#define true  1
#define false 0

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define likely(x)   (x)
#define unlikely(x) (x)

#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)

#define __init
#define __exit
#define __initdata
#define __read_mostly

#endif /* _KBUILD_SIM_TYPES_H */
