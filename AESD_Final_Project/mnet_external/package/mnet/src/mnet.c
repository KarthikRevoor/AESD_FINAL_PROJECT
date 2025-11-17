#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>

#define DRV_NAME "mnet"

struct mnet_priv {
    struct net_device_stats stats;
    spinlock_t lock;
    struct napi_struct napi;
    struct net_device *real_dev;  // points to eth0
};

static struct net_device *mnet_dev;
static struct dentry *mnet_debug_dir;

/* -------------------- RX Handler -------------------- */
static rx_handler_result_t mnet_rx_handler(struct sk_buff **pskb)
{
    struct sk_buff *skb = *pskb;
    struct sk_buff *clone;

    if (!mnet_dev)
        return RX_HANDLER_PASS;

    clone = skb_clone(skb, GFP_ATOMIC);
    if (!clone)
        return RX_HANDLER_PASS;

    clone->dev = mnet_dev;
    clone->protocol = eth_type_trans(clone, mnet_dev);
    clone->ip_summed = CHECKSUM_UNNECESSARY;

    netif_rx(clone);
    mnet_dev->stats.rx_packets++;
    mnet_dev->stats.rx_bytes += skb->len;

    pr_info("%s: RX packet len=%d from eth0\n", mnet_dev->name, skb->len);
    return RX_HANDLER_PASS;
}

/* -------------------- TX Handler -------------------- */
static netdev_tx_t mnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    struct net_device *real_dev = priv->real_dev;

    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    if (real_dev && netif_running(real_dev)) {
        skb->dev = real_dev;
        dev_queue_xmit(skb);
        pr_info("%s: TX via %s len=%d\n", dev->name, real_dev->name, skb->len);
    } else {
        pr_warn("%s: eth0 not ready, dropping TX\n", dev->name);
        dev_kfree_skb(skb);
    }

    return NETDEV_TX_OK;
}

/* -------------------- Open / Stop -------------------- */
static int mnet_open(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    napi_enable(&priv->napi);
    netif_start_queue(dev);
    pr_info("%s: device opened\n", dev->name);
    return 0;
}

static int mnet_stop(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    napi_disable(&priv->napi);
    netif_stop_queue(dev);
    pr_info("%s: device stopped\n", dev->name);
    return 0;
}

/* -------------------- Setup -------------------- */
static const struct net_device_ops mnet_netdev_ops = {
    .ndo_open       = mnet_open,
    .ndo_stop       = mnet_stop,
    .ndo_start_xmit = mnet_start_xmit,
};

static void mnet_setup(struct net_device *dev)
{
    ether_setup(dev);
    dev->netdev_ops = &mnet_netdev_ops;
    eth_hw_addr_random(dev);
    dev->flags |= IFF_NOARP;
}

/* -------------------- Init / Exit -------------------- */
static int __init mnet_init(void)
{
    int ret;
    struct mnet_priv *priv;
    struct net_device *eth_dev;

    mnet_dev = alloc_netdev(sizeof(struct mnet_priv), "mnet%d",
                            NET_NAME_UNKNOWN, mnet_setup);
    if (!mnet_dev)
        return -ENOMEM;

    priv = netdev_priv(mnet_dev);
    spin_lock_init(&priv->lock);
    netif_napi_add(mnet_dev, &priv->napi, NULL);

    eth_dev = dev_get_by_name(&init_net, "eth0");
    if (!eth_dev) {
        pr_err("%s: eth0 not found\n", DRV_NAME);
        free_netdev(mnet_dev);
        return -ENODEV;
    }

    priv->real_dev = eth_dev;

    rtnl_lock();
    ret = netdev_rx_handler_register(eth_dev, mnet_rx_handler, NULL);
    rtnl_unlock();

    if (ret) {
        pr_err("%s: failed to attach RX handler (%d)\n", DRV_NAME, ret);
        dev_put(eth_dev);
        free_netdev(mnet_dev);
        return ret;
    }

    ret = register_netdev(mnet_dev);
    if (ret) {
        pr_err("%s: register_netdev failed (%d)\n", DRV_NAME, ret);
        netdev_rx_handler_unregister(eth_dev);
        dev_put(eth_dev);
        free_netdev(mnet_dev);
        return ret;
    }

    mnet_debug_dir = debugfs_create_dir("mnet", NULL);
    if (mnet_debug_dir) {
        debugfs_create_u32("tx_packets", 0444, mnet_debug_dir,
                           (u32 *)&mnet_dev->stats.tx_packets);
        debugfs_create_u32("rx_packets", 0444, mnet_debug_dir,
                           (u32 *)&mnet_dev->stats.rx_packets);
    }

    pr_info("%s: registered successfully, bridging eth0 <-> %s\n",
            DRV_NAME, mnet_dev->name);
    return 0;
}

static void __exit mnet_exit(void)
{
    struct mnet_priv *priv = netdev_priv(mnet_dev);

    if (priv->real_dev) {
        rtnl_lock();
        netdev_rx_handler_unregister(priv->real_dev);
        rtnl_unlock();
        dev_put(priv->real_dev);
    }

    debugfs_remove_recursive(mnet_debug_dir);
    unregister_netdev(mnet_dev);
    free_netdev(mnet_dev);
    pr_info("%s: module unloaded\n", DRV_NAME);
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_AUTHOR("Karthik Revoor");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AESD Final Project - MNET Ethernet Bridge Driver (Real RX via eth0)");
MODULE_VERSION("3.2");

