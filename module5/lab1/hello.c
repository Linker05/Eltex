#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros

MODULE_LICENSE("My unique License");
MODULE_AUTHOR("Linker");
MODULE_DESCRIPTION("My Hello World module");

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello ELTEX!\n");
    return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit hello_cleanup(void)
{
    printk(KERN_INFO "Cleaning up hello module.\n");
}

module_init(hello_init);
module_exit(hello_cleanup);
