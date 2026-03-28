/* SPDX-License-Identifier: GPL-2.0 */
/* Stub printk.h for free-cc kernel compilation testing */
#ifndef _LINUX_PRINTK_H
#define _LINUX_PRINTK_H

#include <linux/compiler_types.h>
#include <linux/stdarg.h>

/* Log levels */
#define KERN_EMERG   "<0>"
#define KERN_ALERT   "<1>"
#define KERN_CRIT    "<2>"
#define KERN_ERR     "<3>"
#define KERN_WARNING "<4>"
#define KERN_NOTICE  "<5>"
#define KERN_INFO    "<6>"
#define KERN_DEBUG   "<7>"
#define KERN_CONT    "<c>"

/* Numeric log levels */
#define LOGLEVEL_EMERG   0
#define LOGLEVEL_ALERT   1
#define LOGLEVEL_CRIT    2
#define LOGLEVEL_ERR     3
#define LOGLEVEL_WARNING 4
#define LOGLEVEL_NOTICE  5
#define LOGLEVEL_INFO    6
#define LOGLEVEL_DEBUG   7

/* printk stub - variadic, just ignore */
extern int printk(const char *fmt, ...);
extern int vprintk(const char *fmt, va_list args);

#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "kernel"
#endif

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_emerg(fmt, ...)   printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...)   printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)    printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)     printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)    printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...)  printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)    printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...)   do {} while (0)
#define pr_devel(fmt, ...)   do {} while (0)
#define pr_cont(fmt, ...)    printk(KERN_CONT fmt, ##__VA_ARGS__)

/* Console */
#define console_loglevel 7

/* Deferred printk */
#define printk_deferred(fmt, ...) printk(fmt, ##__VA_ARGS__)

/* Print rate limiting */
#define printk_once(fmt, ...) printk(fmt, ##__VA_ARGS__)
#define printk_ratelimited(fmt, ...) printk(fmt, ##__VA_ARGS__)
#define pr_err_once(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#define pr_warn_once(fmt, ...) pr_warn(fmt, ##__VA_ARGS__)
#define pr_info_once(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#define pr_err_ratelimited(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...) pr_warn(fmt, ##__VA_ARGS__)

/* Hex dump */
extern void print_hex_dump(const char *level, const char *prefix_str,
    int prefix_type, int rowsize, int groupsize,
    const void *buf, size_t len, int ascii);

#define print_hex_dump_bytes(prefix_str, prefix_type, buf, len) \
    print_hex_dump(KERN_DEBUG, prefix_str, prefix_type, 16, 1, buf, len, 1)

/* Dump stack */
extern void dump_stack(void);

#endif /* _LINUX_PRINTK_H */
