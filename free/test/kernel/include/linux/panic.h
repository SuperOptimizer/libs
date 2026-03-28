/* Stub panic.h for free-cc kernel compilation testing */
#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

#include <linux/types.h>

#define TAINT_FLAGS_COUNT 18

struct taint_flag {
    char c_true;
    char c_false;
    const char *desc;
};

extern const struct taint_flag taint_flags[TAINT_FLAGS_COUNT];

extern void panic(const char *fmt, ...);
extern int panic_timeout;
extern unsigned long panic_print;
extern int pause_on_oops;
extern int panic_on_warn;

#define PANIC_CPU_INVALID -1

#endif
