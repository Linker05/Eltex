#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/tty.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include "shared.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linker");
MODULE_DESCRIPTION("My netlink module");

struct sock *nl_sock = NULL;

static void netlink_recv_msg(struct sk_buff *skb) {
    struct nlmsghdr *nlh = (struct nlmsghdr *) skb->data;
    const u32 pid = nlh->nlmsg_pid; /* pid of sending process */
    char *msg = nlmsg_data(nlh);
    const size_t msg_size = nlmsg_len(nlh);
    if (msg_size > MAX_PAYLOAD) {
        pr_err("Too large message");
        return;
    }
    if (msg_size == 0) {
        pr_info("Empty message");
        return;
    }
    msg[msg_size - 1] = '\0';
    const char *prefix = "Hello, ";
    const size_t prefix_size = strlen(prefix);


    pr_info("Received from pid %d: %s\n", pid, msg);

    const size_t reply_size = prefix_size + msg_size;

    // create reply
    struct sk_buff *skb_out = nlmsg_new(reply_size, GFP_KERNEL);
    if (!skb_out) {
        pr_err("Failed to allocate new skb\n");
        return;
    }

    // put received message into reply
    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, (int) reply_size, 0);
    if (!nlh) {
        nlmsg_free(skb_out);
        pr_err("Failed to put message to skb\n");
        return;
    }
    NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */

    snprintf(nlmsg_data(nlh), reply_size, "%s%s", prefix, msg);

    pr_info("Send %s\n", (char*)nlmsg_data(nlh));

    const int res = nlmsg_unicast(nl_sock, skb_out, pid);
    if (res < 0)
        pr_err("Error while sending skb to user\n");
}

static int __init netlink_init(void) {
    pr_info("Init module\n");

    struct netlink_kernel_cfg cfg = {
        .input = netlink_recv_msg,
    };

    nl_sock = netlink_kernel_create(&init_net, NETLINK_PROTO, &cfg);
    if (!nl_sock) {
        pr_alert("Error creating socket.\n");
        return -10;
    }

    return 0;
}

static void __exit netlink_exit(void) {
    pr_info("Exit module\n");

    netlink_kernel_release(nl_sock);
}

module_init(netlink_init);
module_exit(netlink_exit);
