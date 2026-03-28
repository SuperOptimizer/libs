/* Kernel pattern: printk and logging */
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/kernel.h>

struct device_info {
    const char *name;
    int major;
    int minor;
    unsigned long base_addr;
};

static void log_device_info(const struct device_info *dev)
{
    pr_info("Device: %s major=%d minor=%d addr=0x%lx\n",
            dev->name, dev->major, dev->minor, dev->base_addr);
}

static void log_error(int code, const char *msg)
{
    pr_err("Error %d: %s\n", code, msg);
}

static void log_warning(const char *fmt, ...)
{
    (void)fmt;
    /* Variadic function pattern */
}

static int debug_level = 3;

static void log_debug(int level, const char *msg)
{
    if (level <= debug_level)
        pr_debug("%s\n", msg);
}

void test_printk(void)
{
    struct device_info dev = {
        .name = "test_device",
        .major = 10,
        .minor = 42,
        .base_addr = 0xFE000000UL,
    };

    log_device_info(&dev);
    log_error(-22, "invalid argument");
    log_debug(2, "subsystem initialized");
    (void)log_warning;
}
