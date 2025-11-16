#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>

#define DRV_NAME "mnet"

struct mnet_priv {
    struct net_device_stats stats;
    spinlock_t lock;
    struct napi_struct napi;
    unsigned int napi_counter;
};

static struct dentry *mnet_debug_dir;

/* -------------------- NAPI Poll -------------------- */
static int mnet_poll(struct napi_struct *napi, int budget)
{
    struct mnet_priv *priv = container_of(napi, struct mnet_priv, napi);
    struct net_device *dev = priv->napi.dev;

    // Placeholder for real RX logic (e.g., from PHY/DMA)
    napi_complete_done(napi, 0);
    netif_wake_queue(dev);
    return 0;
}

/* -------------------- TX Handler -------------------- */
static netdev_tx_t mnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    unsigned long flags;
    int len = skb->len;

    pr_info("%s: TX start len=%d\n", dev->name, len);

    spin_lock_irqsave(&priv->lock, flags);
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += len;
    spin_unlock_irqrestore(&priv->lock, flags);

    // In hardware phase, you’ll push to PHY or DMA queue here
    dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

/* -------------------- Open / Stop -------------------- */
static int mnet_open(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    napi_enable(&priv->napi);
    netif_start_queue(dev);
    priv->napi_counter = 0;
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

/* -------------------- Stats -------------------- */
static struct net_device_stats *mnet_get_stats(struct net_device *dev)
{
    return &dev->stats;
}

/* -------------------- Setup -------------------- */
static const struct net_device_ops mnet_netdev_ops = {
    .ndo_open       = mnet_open,
    .ndo_stop       = mnet_stop,
    .ndo_start_xmit = mnet_start_xmit,
    .ndo_get_stats  = mnet_get_stats,
};

static void mnet_setup(struct net_device *dev)
{
    ether_setup(dev);
    dev->netdev_ops = &mnet_netdev_ops;
    eth_hw_addr_random(dev);
    dev->flags |= IFF_NOARP;
    dev->features |= NETIF_F_HW_CSUM;
}

/* -------------------- Init / Exit -------------------- */
static struct net_device *mnet_dev;

static int __init mnet_init(void)
{
    int ret;
    struct mnet_priv *priv;

    mnet_dev = alloc_netdev(sizeof(struct mnet_priv), "mnet%d",
                            NET_NAME_UNKNOWN, mnet_setup);
    if (!mnet_dev)
        return -ENOMEM;

    priv = netdev_priv(mnet_dev);
    spin_lock_init(&priv->lock);
    netif_napi_add(mnet_dev, &priv->napi, mnet_poll);

    ret = register_netdev(mnet_dev);
    if (ret) {
        pr_err(DRV_NAME ": register_netdev failed (%d)\n", ret);
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

    pr_info("%s: registered successfully as %s (hardware phase)\n",
            DRV_NAME, mnet_dev->name);
    return 0;
}

static void __exit mnet_exit(void)
{
    debugfs_remove_recursive(mnet_debug_dir);
    unregister_netdev(mnet_dev);
    free_netdev(mnet_dev);
    pr_info("%s: module unloaded\n", DRV_NAME);
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_AUTHOR("Karthik Revoor");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AESD Final Project - MNET Ethernet Driver (Hardware-ready NAPI Version)");
MODULE_VERSION("3.0");

