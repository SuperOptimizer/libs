/*
 * printk.c - Simulated kernel printk with variadic functions
 *
 * Exercises:
 *   - Variadic functions (va_list, va_start, va_arg, va_end)
 *   - Static buffers
 *   - Function pointers for console output
 */
#include "types.h"

/* Variadic support - kernel uses __builtin_va_* */
typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_end(ap)          __builtin_va_end(ap)
#define va_copy(d, s)       __builtin_va_copy(d, s)

/* --- Log levels --- */
#define KERN_EMERG   0
#define KERN_ALERT   1
#define KERN_CRIT    2
#define KERN_ERR     3
#define KERN_WARNING 4
#define KERN_NOTICE  5
#define KERN_INFO    6
#define KERN_DEBUG   7

/* --- Ring buffer --- */
#define LOG_BUF_SIZE 1024

static char log_buf[LOG_BUF_SIZE];
static int log_pos = 0;
static int log_level_filter = KERN_DEBUG;

/* --- Console driver --- */
struct console {
    const char *name;
    void (*write)(struct console *, const char *, unsigned int);
    int index;
    struct console *next;
};

static struct console *console_list = NULL;

/* --- Internal helpers --- */

static int int_to_str(char *buf, int buflen, long val, int base)
{
    char tmp[24];
    int i = 0;
    int neg = 0;
    unsigned long uval;
    int len;

    if (val < 0 && base == 10) {
        neg = 1;
        uval = (unsigned long)(-val);
    } else {
        uval = (unsigned long)val;
    }

    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval > 0 && i < 22) {
            int digit = (int)(uval % (unsigned long)base);
            tmp[i++] = (digit < 10) ? ('0' + digit)
                                     : ('a' + digit - 10);
            uval /= (unsigned long)base;
        }
    }

    len = 0;
    if (neg && len < buflen - 1)
        buf[len++] = '-';
    while (i > 0 && len < buflen - 1)
        buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

/* Simple vsnprintf - kernel pattern */
static int kvsnprintf(char *buf, size_t size, const char *fmt,
                       va_list args)
{
    size_t pos = 0;
    const char *p;

    for (p = fmt; *p != '\0' && pos < size - 1; p++) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 'd': {
            int val = va_arg(args, int);
            pos += (size_t)int_to_str(buf + pos,
                                       (int)(size - pos),
                                       (long)val, 10);
            break;
        }
        case 'x': {
            unsigned int val = va_arg(args, unsigned int);
            pos += (size_t)int_to_str(buf + pos,
                                       (int)(size - pos),
                                       (long)val, 16);
            break;
        }
        case 'l': {
            p++;
            if (*p == 'd') {
                long val = va_arg(args, long);
                pos += (size_t)int_to_str(buf + pos,
                                           (int)(size - pos),
                                           val, 10);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (s == NULL)
                s = "(null)";
            while (*s != '\0' && pos < size - 1)
                buf[pos++] = *s++;
            break;
        }
        case 'p': {
            unsigned long val = va_arg(args, unsigned long);
            buf[pos++] = '0';
            if (pos < size - 1) buf[pos++] = 'x';
            pos += (size_t)int_to_str(buf + pos,
                                       (int)(size - pos),
                                       (long)val, 16);
            break;
        }
        case '%':
            buf[pos++] = '%';
            break;
        case '\0':
            p--;
            break;
        default:
            buf[pos++] = '%';
            if (pos < size - 1)
                buf[pos++] = *p;
            break;
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* --- Public interface --- */

int printk(int level, const char *fmt, ...)
{
    va_list args;
    char buf[256];
    int len;
    int i;
    struct console *con;

    if (level > log_level_filter)
        return 0;

    va_start(args, fmt);
    len = kvsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* Append to ring buffer */
    for (i = 0; i < len && log_pos < LOG_BUF_SIZE - 1; i++)
        log_buf[log_pos++] = buf[i];
    log_buf[log_pos] = '\0';

    /* Write to all consoles */
    for (con = console_list; con != NULL; con = con->next) {
        if (con->write != NULL)
            con->write(con, buf, (unsigned int)len);
    }

    return len;
}

void register_console(struct console *con)
{
    con->next = console_list;
    console_list = con;
}

const char *get_log_buf(void)
{
    return log_buf;
}

int get_log_pos(void)
{
    return log_pos;
}

/* Test variadic printk */
int printk_test(void)
{
    int len;

    log_pos = 0;
    log_buf[0] = '\0';

    len = printk(KERN_INFO, "boot: cpu %d freq %d MHz", 0, 1800);
    if (len <= 0)
        return 1;

    len = printk(KERN_ERR, "error code %x", 0xdead);
    if (len <= 0)
        return 2;

    /* Filtered out */
    log_level_filter = KERN_ERR;
    len = printk(KERN_DEBUG, "should not appear");
    if (len != 0)
        return 3;

    log_level_filter = KERN_DEBUG;
    return 0;
}

EXPORT_SYMBOL(printk);
