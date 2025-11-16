#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>

#define DRV_NAME "mnet"
#define NUM_DEVS 2

struct mnet_priv {
    struct net_device_stats stats;
    spinlock_t lock;
    struct napi_struct napi;
    struct sk_buff *rx_skb;
    struct net_device *peer;
    unsigned int napi_counter;
};

static struct net_device *mnet_devs[NUM_DEVS];
static struct dentry *mnet_debug_dir;

/* -------------------- NAPI Poll -------------------- */
static int mnet_poll(struct napi_struct *napi, int budget)
{
    struct mnet_priv *priv = container_of(napi, struct mnet_priv, napi);
    struct net_device *dev = priv->napi.dev;

    if (priv->rx_skb) {
        netif_receive_skb(priv->rx_skb);
        priv->rx_skb = NULL;

        dev->stats.rx_packets++;
        dev->stats.rx_bytes += 64;

        priv->napi_counter++;
        if (priv->napi_counter % 10 == 0)
            pr_info("%s: NAPI heartbeat — %u packets processed\n",
                    dev->name, priv->napi_counter);
    }

    napi_complete_done(napi, 1);
    netif_wake_queue(dev);
    return 1;
}

/* -------------------- TX Handler -------------------- */
static netdev_tx_t mnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    struct net_device *peer = priv->peer;
    struct mnet_priv *peer_priv;
    unsigned long flags;
    int len = skb->len;

    if (!peer) {
        pr_warn("%s: no peer device, dropping packet\n", dev->name);
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    pr_info("%s: TX → %s (len=%d)\n", dev->name, peer->name, len);

    spin_lock_irqsave(&priv->lock, flags);
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += len;
    spin_unlock_irqrestore(&priv->lock, flags);

    /* Enqueue for peer RX (loopback) */
    peer_priv = netdev_priv(peer);
    spin_lock_irqsave(&peer_priv->lock, flags);

    if (!peer_priv->rx_skb) {
        peer_priv->rx_skb = netdev_alloc_skb(peer, len + 2);
        if (peer_priv->rx_skb) {
            skb_reserve(peer_priv->rx_skb, 2);
            memcpy(skb_put(peer_priv->rx_skb, len), skb->data, len);
            peer_priv->rx_skb->dev = peer;
            peer_priv->rx_skb->protocol = eth_type_trans(peer_priv->rx_skb, peer);
            peer_priv->rx_skb->ip_summed = CHECKSUM_UNNECESSARY;

            napi_schedule(&peer_priv->napi);
            netif_stop_queue(peer);
        } else {
            pr_warn("%s: RX alloc failed\n", peer->name);
            peer->stats.rx_dropped++;
        }
    }

    spin_unlock_irqrestore(&peer_priv->lock, flags);
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
static int __init mnet_init(void)
{
    int i, ret = 0;

    for (i = 0; i < NUM_DEVS; i++) {
        mnet_devs[i] = alloc_netdev(sizeof(struct mnet_priv),
                                    kasprintf(GFP_KERNEL, "mnet%d", i),
                                    NET_NAME_UNKNOWN, mnet_setup);
        if (!mnet_devs[i])
            return -ENOMEM;

        struct mnet_priv *priv = netdev_priv(mnet_devs[i]);
        spin_lock_init(&priv->lock);
        netif_napi_add(mnet_devs[i], &priv->napi, mnet_poll);

        ret = register_netdev(mnet_devs[i]);
        if (ret) {
            pr_err("%s: register_netdev failed for mnet%d (%d)\n",
                   DRV_NAME, i, ret);
            goto fail;
        }
    }

    /* Link peers for loopback */
    ((struct mnet_priv *)netdev_priv(mnet_devs[0]))->peer = mnet_devs[1];
    ((struct mnet_priv *)netdev_priv(mnet_devs[1]))->peer = mnet_devs[0];

    /* DebugFS */
    mnet_debug_dir = debugfs_create_dir("mnet", NULL);
    if (mnet_debug_dir) {
        for (i = 0; i < NUM_DEVS; i++) {
            char name[16];
            snprintf(name, sizeof(name), "tx_packets%d", i);
            debugfs_create_ulong(name, 0444, mnet_debug_dir,
                                 &mnet_devs[i]->stats.tx_packets);

            snprintf(name, sizeof(name), "rx_packets%d", i);
            debugfs_create_ulong(name, 0444, mnet_debug_dir,
                                 &mnet_devs[i]->stats.rx_packets);
        }
    }

    pr_info("%s: two virtual loopback interfaces registered\n", DRV_NAME);
    return 0;

fail:
    while (--i >= 0) {
        unregister_netdev(mnet_devs[i]);
        free_netdev(mnet_devs[i]);
    }
    return ret;
}

static void __exit mnet_exit(void)
{
    int i;
    debugfs_remove_recursive(mnet_debug_dir);
    for (i = 0; i < NUM_DEVS; i++) {
        if (mnet_devs[i]) {
            unregister_netdev(mnet_devs[i]);
            free_netdev(mnet_devs[i]);
        }
    }
    pr_info("%s: unloaded\n", DRV_NAME);
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_AUTHOR("Karthik Revoor");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AESD Final Project — Dual Interface NAPI Loopback Driver");
MODULE_VERSION("3.0");

