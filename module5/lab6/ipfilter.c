#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/tty.h>
#include <linux/inet.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/spinlock.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linker");
MODULE_DESCRIPTION("My IP filter module");

#define FILTER_FILE "ipfilter"

#define FILTER_CMD_LENGTH 3
#define FILTER_IP_LENGTH 15

#define FILTER_CMD_SIZE (FILTER_CMD_LENGTH + 1)
#define FILTER_IP_SIZE (FILTER_IP_LENGTH + 1)

#define FILTER_CMD_FORMAT "%" __stringify(FILTER_CMD_LENGTH) "s %" __stringify(FILTER_IP_LENGTH) "s"

#define IPLIST_MAX_SIZE 32

static struct kobject *filter_kobj;
static struct nf_hook_ops nfin;

static __be32 filtered[IPLIST_MAX_SIZE];
static size_t filtered_count = 0;

static DEFINE_SPINLOCK(filter_lock);

static ssize_t filtered_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    unsigned long flags;
    size_t len = 0;

    spin_lock_irqsave(&filter_lock, flags);

    for (size_t i = 0; i < filtered_count; i++) { // %pI4 - kernel extension for
        len += sysfs_emit_at(buf, (int)len, "%pI4\n", &filtered[i]);
    }

    spin_unlock_irqrestore(&filter_lock, flags);

    return (ssize_t)len;
}

static int filter_add(const __be32 ip) {
    if (filtered_count == IPLIST_MAX_SIZE)
        return -ENOBUFS;
    for (int i = 0; i < filtered_count; i++)
        if (filtered[i] == ip)
            return 0;
    filtered[filtered_count] = ip;
    filtered_count++;
    return 0;
}

static int filter_remove(const __be32 ip) {
    int i = 0;
    for (; i < filtered_count; i++)
        if (filtered[i] == ip)
            break;
    if (i == filtered_count)
        return -ENOENT;
    for (; i < filtered_count - 1; i++)
        filtered[i] = filtered[i + 1];
    filtered_count--;
    return 0;
}

static ssize_t filtered_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, const size_t count) {
    char cmd[FILTER_CMD_SIZE];
    char ip_str[FILTER_IP_SIZE];
    __be32 ip = 0;

    sscanf(buf, FILTER_CMD_FORMAT, cmd, ip_str);
    const int success = in4_pton(ip_str, (int)strlen(ip_str), (u8*) &ip, '\n', NULL);
    if (!success) {
        pr_err("Invalid ip address\n");
        return -EINVAL;
    }

    unsigned long flags;
    spin_lock_irqsave(&filter_lock, flags);

    int error = 0;
    if (strcmp(cmd, "on") == 0) {
        error = filter_add(ip);
    } else if (strcmp(cmd, "off") == 0) {
        error = filter_remove(ip);
    } else {
        pr_err("Invalid command\n");
        spin_unlock_irqrestore(&filter_lock, flags);
        return -EINVAL;
    }
    spin_unlock_irqrestore(&filter_lock, flags);

    if (error)
        return error;

    return (ssize_t) count;
}

static unsigned int packet_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
    struct iphdr *ip = ip_hdr(skb);
    if (!ip || data_race(filtered_count) == 0)
        return NF_ACCEPT;

    unsigned long flags;
    spin_lock_irqsave(&filter_lock, flags);
    for (int i = 0; i < filtered_count; i++)
        if (filtered[i] == ip->daddr) {
            spin_unlock_irqrestore(&filter_lock, flags);
            if (skb->sk) { // Report error if socket known
                skb->sk->sk_err = EPERM;
                skb->sk->sk_error_report(skb->sk);
            }
            return NF_DROP;
        }
    spin_unlock_irqrestore(&filter_lock, flags);
    return NF_ACCEPT;
}

static struct kobj_attribute status_attr = __ATTR(filtered, 0660, filtered_show, filtered_store);

static int __init ipfilter_init(void) {
    filter_kobj = kobject_create_and_add(FILTER_FILE, kernel_kobj);
    if (!filter_kobj) {
        return -ENOMEM;
    }

    int error = sysfs_create_file(filter_kobj, &status_attr.attr);
    if (error) {
        kobject_put(filter_kobj);
        return error;
    }

    nfin.hook = packet_hook;
    nfin.hooknum = NF_INET_LOCAL_OUT;
    nfin.pf = NFPROTO_IPV4;
    nfin.priority = NF_IP_PRI_FIRST;
    error = nf_register_net_hook(&init_net, &nfin);
    if (error)
        return error;

    return 0;
}

static void __exit ipfilter_cleanup(void) {
    nf_unregister_net_hook(&init_net, &nfin);
    sysfs_remove_file(filter_kobj, &status_attr.attr);
    kobject_put(filter_kobj);
}

module_init(ipfilter_init);
module_exit(ipfilter_cleanup);
