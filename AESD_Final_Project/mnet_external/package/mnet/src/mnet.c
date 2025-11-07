#include <linux/module.h>
#include <linux/netdevice.h>

static int mnet_open(struct net_device *dev) {
    netif_start_queue(dev);
    pr_info("mnet0: opened\n");
    return 0;
}

static int mnet_stop(struct net_device *dev) {
    netif_stop_queue(dev);
    pr_info("mnet0: stopped\n");
    return 0;
}

static netdev_tx_t mnet_xmit(struct sk_buff *skb, struct net_device *dev) {
    dev_kfree_skb(skb);
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;
    pr_info("mnet0: dummy transmit\n");
    return NETDEV_TX_OK;
}

static const struct net_device_ops mnet_netdev_ops = {
    .ndo_open = mnet_open,
    .ndo_stop = mnet_stop,
    .ndo_start_xmit = mnet_xmit,
};

static void mnet_setup(struct net_device *dev) {
    ether_setup(dev);
    dev->netdev_ops = &mnet_netdev_ops;
    strcpy(dev->name, "mnet%d");
    dev->flags |= IFF_NOARP;
}

static struct net_device *mnet_dev;

static int __init mnet_init(void) {
    int ret;
    mnet_dev = alloc_netdev(0, "mnet%d", NET_NAME_UNKNOWN, mnet_setup);
    if (!mnet_dev)
        return -ENOMEM;

    ret = register_netdev(mnet_dev);
    if (ret)
        free_netdev(mnet_dev);

    pr_info("mnet: module loaded\n");
    return ret;
}

static void __exit mnet_exit(void) {
    unregister_netdev(mnet_dev);
    free_netdev(mnet_dev);
    pr_info("mnet: module unloaded\n");
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karthik Revoor & Kaif Shahid Shaikh");
MODULE_DESCRIPTION("Minimal Ethernet driver skeleton");

