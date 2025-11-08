#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#define DRV_NAME "mnet"

struct mnet_priv {
    struct net_device_stats stats;
    spinlock_t lock;
};

/* ------------------------ TX Handler ------------------------ */
static netdev_tx_t mnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    unsigned long flags;
    int len = skb->len;
    char *data = skb->data;

    /* Loopback implementation: TX → RX */
    spin_lock_irqsave(&priv->lock, flags);

    /* Update TX stats */
    priv->stats.tx_packets++;
    priv->stats.tx_bytes += len;

    /* Simulate RX */
    {
        struct sk_buff *rx_skb = netdev_alloc_skb(dev, len + 2);
        if (rx_skb) {
            skb_reserve(rx_skb, 2);
            memcpy(skb_put(rx_skb, len), data, len);
            rx_skb->dev = dev;
            rx_skb->protocol = eth_type_trans(rx_skb, dev);
            rx_skb->ip_summed = CHECKSUM_UNNECESSARY;

            priv->stats.rx_packets++;
            priv->stats.rx_bytes += len;

            netif_rx(rx_skb);
        } else {
            pr_warn(DRV_NAME ": RX skb allocation failed\n");
            priv->stats.rx_dropped++;
        }
    }

    spin_unlock_irqrestore(&priv->lock, flags);

    dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

/* ------------------------ Open / Stop ------------------------ */
static int mnet_open(struct net_device *dev)
{
    netif_start_queue(dev);
    pr_info("%s: device opened\n", dev->name);
    return 0;
}

static int mnet_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    pr_info("%s: device stopped\n", dev->name);
    return 0;
}

/* ------------------------ Stats ------------------------ */
static struct net_device_stats *mnet_get_stats(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    return &priv->stats;
}

/* ------------------------ Setup ------------------------ */
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

    /* Random MAC for now */
    eth_hw_addr_random(dev);

    dev->flags |= IFF_NOARP;
    dev->features |= NETIF_F_HW_CSUM;
}

/* ------------------------ Module Init / Exit ------------------------ */
static struct net_device *mnet_dev;

static int __init mnet_init(void)
{
    int ret;

    mnet_dev = alloc_netdev(sizeof(struct mnet_priv), "mnet%d", NET_NAME_UNKNOWN, mnet_setup);
    if (!mnet_dev)
        return -ENOMEM;

    spin_lock_init(&((struct mnet_priv *)netdev_priv(mnet_dev))->lock);

    ret = register_netdev(mnet_dev);
    if (ret) {
        pr_err(DRV_NAME ": failed to register device (%d)\n", ret);
        free_netdev(mnet_dev);
        return ret;
    }

    pr_info(DRV_NAME ": virtual net device registered as %s\n", mnet_dev->name);
    return 0;
}

static void __exit mnet_exit(void)
{
    unregister_netdev(mnet_dev);
    free_netdev(mnet_dev);
    pr_info(DRV_NAME ": module unloaded\n");
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_AUTHOR("Karthik Revoor");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AESD Final Project - MNET Ethernet Driver");
MODULE_VERSION("1.0");

