/* Kernel pattern: variadic functions */
#include <linux/types.h>
#include <linux/stdarg.h>
#include <linux/kernel.h>

static int sum_args(int count, ...)
{
    va_list ap;
    int total = 0;
    int i;

    va_start(ap, count);
    for (i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);

    return total;
}

static long max_of(int count, ...)
{
    va_list ap;
    long max_val;
    long val;
    int i;

    va_start(ap, count);
    max_val = va_arg(ap, long);
    for (i = 1; i < count; i++) {
        val = va_arg(ap, long);
        if (val > max_val)
            max_val = val;
    }
    va_end(ap);

    return max_val;
}

struct log_entry {
    int level;
    char message[256];
};

static int format_message(char *buf, size_t size, const char *fmt, ...)
{
    /* Simple format: just copy the format string */
    size_t i;
    va_list ap;

    va_start(ap, fmt);
    for (i = 0; fmt[i] && i < size - 1; i++)
        buf[i] = fmt[i];
    buf[i] = '\0';
    va_end(ap);

    return (int)i;
}

static void log_message(struct log_entry *entry, int level,
                        const char *fmt, ...)
{
    va_list ap;
    size_t i;

    entry->level = level;
    va_start(ap, fmt);
    for (i = 0; fmt[i] && i < sizeof(entry->message) - 1; i++)
        entry->message[i] = fmt[i];
    entry->message[i] = '\0';
    va_end(ap);
}

void test_variadic(void)
{
    int total;
    long max_val;
    struct log_entry entry;
    char buf[128];
    int len;

    total = sum_args(5, 10, 20, 30, 40, 50);
    (void)total;

    max_val = max_of(4, 100L, 42L, 999L, 7L);
    (void)max_val;

    log_message(&entry, 3, "test message %d", 42);
    len = format_message(buf, sizeof(buf), "hello %s", "world");
    (void)len;
}
