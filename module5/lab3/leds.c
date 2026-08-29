#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/timer.h>
#include <linux/console_struct.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linker");
MODULE_DESCRIPTION("My leds+sysfs module");

#define LEDS_FILE "leds"

#define USER_LED_CAPS 1
#define USER_LED_NUM (1 << 1)
#define USER_LED_SCROLL (1 << 2)

#define LED_RESTORE 0xFF
#define BLINK_DELAY (HZ / 2)

static struct timer_list toggle_timer;
static struct kobject *leds_kobj;
static long status = 0;
static long status_back = 0;

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%ld\n", status);
}

static int leds_update(struct tty_struct *fg_tty) {
    if (!fg_tty->ops || !fg_tty->ops->ioctl)
        return -ENODEV;

    int code = 0;
    if (status & USER_LED_CAPS)
        code |= LED_CAP;
    if (status & USER_LED_NUM)
        code |= LED_NUM;
    if (status & USER_LED_SCROLL)
        code |= LED_SCR;

    return fg_tty->ops->ioctl(fg_tty, KDSETLED, code);
}

static void leds_toggle_timer(struct timer_list *timer) {
    struct tty_struct *fg_tty = vc_cons[fg_console].d->port.tty;
    if (!fg_tty)
        return;
    tty_kref_get(fg_tty);

    const long tmp = status;
    status = status_back;
    status_back = tmp;

    leds_update(fg_tty);
    tty_kref_put(fg_tty);

    timer->expires = jiffies + BLINK_DELAY;
    add_timer(timer);
}

static ssize_t status_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, const size_t count) {
    long tmp = 0;
    int error = kstrtol(buf, 10, &tmp);
    if (error)
        return error;

    if (tmp < 0 || tmp > 7)
        return -EINVAL;

    struct tty_struct *fg_tty = vc_cons[fg_console].d->port.tty;
    if (!fg_tty)
        return -ENODEV;
    tty_kref_get(fg_tty);
    status = tmp;
    status_back = 0;

    error = leds_update(fg_tty);
    tty_kref_put(fg_tty);
    if (error)
        return error;

    return (ssize_t) count;
}

static struct kobj_attribute status_attr = __ATTR(status, 0660, status_show, status_store);

static int __init leds_init (void) {
    leds_kobj = kobject_create_and_add(LEDS_FILE, kernel_kobj);
    if (!leds_kobj) {
        return -ENOMEM;
    }

    const int error = sysfs_create_file(leds_kobj, &status_attr.attr);
    if (error) {
        kobject_put(leds_kobj);
        return error;
    }

    timer_setup(&toggle_timer, leds_toggle_timer, 0);
    toggle_timer.expires = jiffies + BLINK_DELAY;
    add_timer(&toggle_timer);

    return 0;
}

static void __exit leds_cleanup(void) {
    sysfs_remove_file(leds_kobj, &status_attr.attr);
    timer_delete(&toggle_timer);
    kobject_put(leds_kobj);
    struct tty_struct *fg_tty = vc_cons[fg_console].d->port.tty;
    if (!fg_tty || !fg_tty->ops || !fg_tty->ops->ioctl)
        return;
    tty_kref_get(fg_tty);
    fg_tty->ops->ioctl(fg_tty, KDSETLED, LED_RESTORE);
    tty_kref_put(fg_tty);
}

module_init(leds_init);
module_exit(leds_cleanup);
