/*
 * main.c - Kernel main entry, calls subsystem tests
 *
 * Exercises:
 *   - __attribute__((section(".init.text"))) via macro
 *   - Inline assembly (barriers, system register reads)
 *   - Static variables and function pointers
 *   - container_of usage
 */
#include "types.h"

/* --- Inline assembly barriers (kernel patterns) --- */

static void dmb_sy(void)
{
    __asm__ __volatile__("dmb sy" : : : "memory");
}

static void dsb_sy(void)
{
    __asm__ __volatile__("dsb sy" : : : "memory");
}

static void isb(void)
{
    __asm__ __volatile__("isb" : : : "memory");
}

/* Read cycle counter */
static u64 read_cntfrq(void)
{
    u64 val;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/* smp_mb() - full memory barrier */
#define smp_mb()  dmb_sy()
#define smp_wmb() __asm__ __volatile__("dmb ishst" : : : "memory")
#define smp_rmb() __asm__ __volatile__("dmb ishld" : : : "memory")

/* --- Subsystem init table (function pointer array) --- */

typedef int (*initcall_t)(void);

/* External test functions */
extern int fs_check_inode(void);
extern int list_test(void);
extern int printk_test(void);
extern int fs_get_open_count(void);

/* Init function table - simulates __initcall array */
struct init_entry {
    const char *name;
    initcall_t fn;
    int level;
};

static const struct init_entry init_table[] = {
    { .name = "fs",     .fn = fs_check_inode, .level = 1 },
    { .name = "list",   .fn = list_test,      .level = 2 },
    { .name = "printk", .fn = printk_test,    .level = 3 }
};

/* --- container_of usage --- */

struct device {
    int id;
    const char *name;
};

struct platform_device {
    struct device dev;
    int irq;
    void *platform_data;
};

static int test_container_of(void)
{
    struct platform_device pdev;
    struct device *d;
    struct platform_device *found;

    pdev.dev.id = 42;
    pdev.dev.name = "uart0";
    pdev.irq = 33;
    pdev.platform_data = NULL;

    d = &pdev.dev;
    found = container_of(d, struct platform_device, dev);

    if (found->irq != 33)
        return 1;
    if (found->dev.id != 42)
        return 2;
    return 0;
}

/* --- Kernel main --- */

int kernel_main(void)
{
    int i;
    int pass = 0;
    int fail = 0;
    int ret;
    u64 freq;

    /* Memory barriers */
    smp_mb();
    dsb_sy();
    isb();

    /* Read system counter frequency */
    freq = read_cntfrq();
    (void)freq;

    /* container_of test */
    ret = test_container_of();
    if (ret == 0)
        pass++;
    else
        fail++;

    /* Run init table */
    for (i = 0; i < (int)ARRAY_SIZE(init_table); i++) {
        ret = init_table[i].fn();
        if (ret == 0)
            pass++;
        else
            fail++;
    }

    /* Return number of failures (0 = all passed) */
    (void)pass;
    return fail;
}
