/* Kernel pattern: module exports and init/exit */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>

static int my_module_param = 42;
module_param(my_module_param, int, 0644);
MODULE_PARM_DESC(my_module_param, "Test parameter");

static int shared_api_version = 2;

int my_exported_func(int a, int b)
{
    return a + b + my_module_param;
}
EXPORT_SYMBOL(my_exported_func);

int my_exported_func_gpl(const char *msg)
{
    (void)msg;
    return shared_api_version;
}
EXPORT_SYMBOL_GPL(my_exported_func_gpl);

static int __init my_module_init(void)
{
    return 0;
}

static void __exit my_module_exit(void)
{
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Author");
MODULE_DESCRIPTION("Test module for free-cc");
MODULE_VERSION("1.0");
