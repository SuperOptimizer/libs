/* Kernel pattern: per-cpu variables (simplified) */
#include <linux/types.h>
#include <linux/kernel.h>

/* Simplified per-cpu - just use regular variables for compilation test */
#define DEFINE_PER_CPU(type, name) type name
#define per_cpu(var, cpu) (var)
#define this_cpu_ptr(ptr) (ptr)
#define __this_cpu_read(var) (var)
#define __this_cpu_write(var, val) do { (var) = (val); } while (0)
#define __this_cpu_inc(var) do { (var)++; } while (0)
#define __this_cpu_dec(var) do { (var)--; } while (0)
#define __this_cpu_add(var, val) do { (var) += (val); } while (0)

struct cpu_stats {
    unsigned long interrupts;
    unsigned long context_switches;
    unsigned long page_faults;
    unsigned long syscalls;
};

static DEFINE_PER_CPU(struct cpu_stats, cpu_stats);
static DEFINE_PER_CPU(unsigned long, irq_count);

static void record_interrupt(void)
{
    __this_cpu_inc(irq_count);
    __this_cpu_inc(cpu_stats.interrupts);
}

static void record_context_switch(void)
{
    __this_cpu_inc(cpu_stats.context_switches);
}

static void record_page_fault(void)
{
    __this_cpu_inc(cpu_stats.page_faults);
}

static void record_syscall(void)
{
    __this_cpu_inc(cpu_stats.syscalls);
}

static unsigned long get_total_interrupts(void)
{
    return __this_cpu_read(cpu_stats.interrupts);
}

void test_percpu(void)
{
    unsigned long total;
    int i;

    for (i = 0; i < 100; i++) {
        record_interrupt();
        if (i % 5 == 0)
            record_context_switch();
        if (i % 10 == 0)
            record_page_fault();
        record_syscall();
    }

    total = get_total_interrupts();
    (void)total;
}
