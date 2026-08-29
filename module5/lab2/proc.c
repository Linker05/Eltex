#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("My unique License");
MODULE_AUTHOR("Linker");
MODULE_DESCRIPTION("My proc module");

#define PROC_FILE "myproc"
#define BUFFER_SIZE (10 * sizeof(char))

static size_t len;
static char *msg;

static ssize_t read_proc(struct file *filp, char *buf, size_t count, loff_t *offp) {
    if(count > (len - *offp)) {
        count = (len - *offp);
    }
    const size_t remain = copy_to_user(buf, msg + *offp, count);
    if(remain > 0) {
        return -EFAULT;
    }
    *offp += (loff_t) count;
    return (ssize_t) count;
}

static ssize_t write_proc(struct file *filp, const char *buf, const size_t count, loff_t *offp) {
    if(count > (BUFFER_SIZE - *offp)) {
        return -E2BIG;
    }
    const size_t remain = copy_from_user(msg + *offp, buf, count);
    if(remain > 0) {
        return -EFAULT;
    }
    len = *offp + count;
    *offp += (loff_t) count;
    return (ssize_t) count;
}

static const struct proc_ops proc_fops = {
    .proc_read = read_proc,
    .proc_write = write_proc,
};

static void create_new_proc_entry(void) { //use of void for no arguments is compulsory now
    proc_create(PROC_FILE, 0666, NULL, &proc_fops);
    msg = kmalloc(BUFFER_SIZE, GFP_KERNEL);
}

static int __init proc_init (void) {
    create_new_proc_entry();
    return 0;
}

static void __exit proc_cleanup(void) {
    remove_proc_entry(PROC_FILE, NULL);
    kfree(msg);
}

module_init(proc_init);
module_exit(proc_cleanup);
