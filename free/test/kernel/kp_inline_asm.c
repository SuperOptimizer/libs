/* Kernel pattern: inline assembly (aarch64 specific) */
#include <linux/types.h>
#include <linux/kernel.h>

/* Read current stack pointer */
static unsigned long read_sp(void)
{
    unsigned long sp;
    __asm__ __volatile__("mov %0, sp" : "=r" (sp));
    return sp;
}

/* Memory barrier patterns */
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

/* Simple nop for testing */
static void nop_barrier(void)
{
    __asm__ __volatile__("nop" : : : "memory");
}

/* Count leading zeros */
static unsigned int my_clz(unsigned long val)
{
    unsigned long result;
    __asm__ __volatile__("clz %0, %1" : "=r" (result) : "r" (val));
    return (unsigned int)result;
}

/* Bit reverse */
static unsigned long my_rbit(unsigned long val)
{
    unsigned long result;
    __asm__ __volatile__("rbit %0, %1" : "=r" (result) : "r" (val));
    return result;
}

void test_inline_asm(void)
{
    unsigned long sp;
    unsigned int lz;
    unsigned long rev;
    sp = read_sp();
    (void)sp;

    dmb_sy();
    dsb_sy();
    isb();
    nop_barrier();

    lz = my_clz(0x00FF0000UL);
    (void)lz;

    rev = my_rbit(1UL);
    (void)rev;
}
