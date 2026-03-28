/* Kernel pattern: GCC attributes */
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kernel.h>

/* Section attributes */
static int __attribute__((section(".data.init"))) init_data = 42;
static const char __attribute__((section(".rodata.banner"))) banner[] = "Kernel v1.0";

/* Alignment attributes */
static int __attribute__((aligned(64))) cache_aligned_var;
struct __attribute__((aligned(4096))) page_aligned_struct {
    unsigned long entries[512];
};

/* Pure/const functions */
static int __attribute__((pure)) pure_compute(int a, int b)
{
    return a * a + b * b;
}

static int __attribute__((const)) const_func(int x)
{
    return x * 3 + 1;
}

/* Packed structures */
struct __attribute__((packed)) wire_protocol {
    u8 magic;
    u32 length;
    u16 checksum;
    u8 payload[0];
};

/* Weak symbols */
void __attribute__((weak)) optional_hook(int event)
{
    (void)event;
}

/* Noinline for stack trace accuracy */
static __attribute__((noinline)) int noinline_func(int x)
{
    return x + 1;
}

/* Unused suppressor */
static int __attribute__((unused)) unused_helper(void)
{
    return 0;
}

/* Noreturn */
static void __attribute__((noreturn)) panic_handler(const char *msg)
{
    (void)msg;
    for (;;) ;
}

/* Cold/hot paths */
static __attribute__((cold)) void error_path(int code)
{
    (void)code;
}

static __attribute__((hot)) void fast_path(int *data, int n)
{
    int i;
    for (i = 0; i < n; i++)
        data[i] = data[i] * 2 + 1;
}

/* Deprecated */
static int __attribute__((deprecated)) old_api(void)
{
    return -1;
}

void test_attributes(void)
{
    int result;
    struct wire_protocol proto;

    result = pure_compute(3, 4);
    (void)result;

    result = const_func(10);
    (void)result;

    result = noinline_func(99);
    (void)result;

    fast_path(&result, 1);

    optional_hook(1);

    proto.magic = 0xFF;
    proto.length = 100;
    proto.checksum = 0x1234;
    (void)proto;

    error_path(42);

    (void)init_data;
    (void)banner;
    (void)cache_aligned_var;
    (void)old_api;
    (void)panic_handler;
}
