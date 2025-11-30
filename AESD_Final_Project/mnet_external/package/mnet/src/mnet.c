#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/if_ether.h>

#define DRV_NAME "mnet"

/* -------------------- Private Struct -------------------- */
struct mnet_priv {
    struct net_device_stats stats;
    spinlock_t lock;
    struct napi_struct napi;
    struct net_device *real_dev;  // points to eth0
};

static struct net_device *mnet_dev;
static struct dentry *mnet_debug_dir;
static struct task_struct *mnet_kthread;
static int mnet_last_temp_mdegc = 0;

/* =========================================================
 * Parse custom frames (for receiver Pi)
 * ========================================================= */
/* =========================================================
 * Parse custom frames (for receiver Pi)
 * ========================================================= */
static void mnet_process_rx(struct sk_buff *skb)
{
    struct ethhdr *eh;
    char msg[128];
    int payload_len;
    unsigned char *payload;

    /* * Get pointer to Ethernet header.
     * Even though skb->data has moved, skb->mac_header is set, 
     * so this macro correctly finds the header in the headroom.
     */
    eh = eth_hdr(skb);

    /* Verify it is our custom protocol */
    if (eh->h_proto != htons(0x88B5))
        return;

    /* * FIX: The driver (eth0) has already called eth_type_trans(), which
     * performs skb_pull(skb, ETH_HLEN).
     * * Therefore:
     * - skb->data ALREADY points to the payload.
     * - skb->len is ALREADY the payload length.
     * * We do NOT need to add ETH_HLEN again.
     */
    payload = skb->data;           
    payload_len = skb->len;        

    /* Safety check */
    if (payload_len <= 0 || payload_len >= sizeof(msg))
        return;

    memcpy(msg, payload, payload_len);
    msg[payload_len] = '\0';       // Null-terminate

   // pr_info("mnet RX: payload = %s\n", msg);
     {
        int t;
        if (sscanf(msg, "TEMP=%d", &t) == 1) {
            mnet_last_temp_mdegc = t;
            // pr_info("mnet RX: parsed TEMP=%d mdegC\n", mnet_last_temp_mdegc);
        }
    }
}
/* =========================================================
 * RX Handler from eth0 → mnet0
 * ========================================================= */
static rx_handler_result_t mnet_rx_handler(struct sk_buff **pskb)
{
    struct sk_buff *skb = *pskb;
    struct sk_buff *clone;

    if (!mnet_dev)
        return RX_HANDLER_PASS;

    mnet_process_rx(skb);  // CUSTOM PACKET CHECK

    clone = skb_clone(skb, GFP_ATOMIC);
    if (!clone)
        return RX_HANDLER_PASS;

    clone->dev = mnet_dev;
    clone->protocol = eth_type_trans(clone, mnet_dev);
    clone->ip_summed = CHECKSUM_UNNECESSARY;

    netif_rx(clone);
    mnet_dev->stats.rx_packets++;
    mnet_dev->stats.rx_bytes += skb->len;

    return RX_HANDLER_PASS;
}

/* =========================================================
 * TX Handler mnet0 → eth0
 * ========================================================= */
static netdev_tx_t mnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    struct net_device *real_dev = priv->real_dev;

    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    if (real_dev && netif_running(real_dev)) {
        skb->dev = real_dev;
        dev_queue_xmit(skb);
    } else {
        dev_kfree_skb(skb);
    }

    return NETDEV_TX_OK;
}

/* =========================================================
 * Open / Stop
 * ========================================================= */
static int mnet_open(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    napi_enable(&priv->napi);
    netif_start_queue(dev);
    return 0;
}

static int mnet_stop(struct net_device *dev)
{
    struct mnet_priv *priv = netdev_priv(dev);
    napi_disable(&priv->napi);
    netif_stop_queue(dev);
    return 0;
}

/* =========================================================
 * Device Setup
 * ========================================================= */
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

/* =========================================================
 * BME280 Temperature Reader
 * ========================================================= */
static int read_bme280_temp(void)
{
    struct file *f;
    char buf[32] = {0};
    int temp = -1;
    loff_t pos = 0;
    ssize_t ret;

    f = filp_open("/sys/bus/i2c/devices/1-0076/temp_mdegc", O_RDONLY, 0);
    if (IS_ERR(f))
        return -1;

    ret = kernel_read(f, buf, sizeof(buf) - 1, &pos);
    filp_close(f, NULL);

    if (ret <= 0)
        return -1;

    if (kstrtoint(buf, 10, &temp) != 0)
        return -1;

    return temp;
}

/* =========================================================
 * Construct and send custom Ethernet frame
 * ========================================================= */

static void mnet_send_temp_packet(int temp_mdegc)
{
    struct sk_buff *skb;
    unsigned char dest_mac[ETH_ALEN] =
            {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    char payload[64];
    int payload_len;

    struct mnet_priv *priv = netdev_priv(mnet_dev);
    struct net_device *real_dev = priv->real_dev;

    if (!real_dev || !netif_running(real_dev))
        return;

    snprintf(payload, sizeof(payload), "TEMP=%d", temp_mdegc);
    payload_len = strlen(payload);

    /* Allocate SKB with headroom for Ethernet header */
    skb = alloc_skb(payload_len + ETH_HLEN + 2, GFP_KERNEL);
    if (!skb)
        return;

    skb_reserve(skb, ETH_HLEN);          // leave space for ethhdr
    memcpy(skb_put(skb, payload_len), payload, payload_len);

    /* Push Ethernet header */
    skb_push(skb, ETH_HLEN);

    struct ethhdr *eh = (struct ethhdr *)skb->data;
    memcpy(eh->h_dest, dest_mac, ETH_ALEN);
    memcpy(eh->h_source, real_dev->dev_addr, ETH_ALEN);
    eh->h_proto = htons(0x88B5);         // <-- CUSTOM ETHER TYPE

    /* Tell kernel where this packet will go */
    skb->dev = real_dev;
    skb->protocol = eh->h_proto;
    skb->ip_summed = CHECKSUM_NONE;

    pr_info("mnet: TX frame [%d bytes] %s\n",payload_len, payload);

    dev_queue_xmit(skb);
}


/* =========================================================
 * Sensor Thread (TX @ 1 Hz)
 * ========================================================= */
static int mnet_sensor_thread(void *data)
{
    while (!kthread_should_stop()) {
        int temp = read_bme280_temp();
        if (temp > 0)
            mnet_send_temp_packet(temp);

        msleep(1000);  // 1 Hz
    }

    return 0;
}

/* =========================================================
 * Module Init
 * ========================================================= */
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
    if (!eth_dev)
        return -ENODEV;

    priv->real_dev = eth_dev;

    rtnl_lock();
    ret = netdev_rx_handler_register(eth_dev, mnet_rx_handler, NULL);
    rtnl_unlock();
    if (ret)
        return ret;

    ret = register_netdev(mnet_dev);
    if (ret)
        return ret;

    mnet_debug_dir = debugfs_create_dir("mnet", NULL);
    if (mnet_debug_dir) {
        debugfs_create_u32("tx_packets", 0444, mnet_debug_dir,
                           (u32 *)&mnet_dev->stats.tx_packets);
        debugfs_create_u32("rx_packets", 0444, mnet_debug_dir,
                           (u32 *)&mnet_dev->stats.rx_packets);
        debugfs_create_u32("temp_mdegc_rx", 0444, mnet_debug_dir,
                       (u32 *)&mnet_last_temp_mdegc);
    }

    mnet_kthread = kthread_run(mnet_sensor_thread, NULL, "mnet_temp_thread");

    return 0;
}

/* =========================================================
 * Module Exit
 * ========================================================= */
static void __exit mnet_exit(void)
{
    struct mnet_priv *priv = netdev_priv(mnet_dev);

    if (mnet_kthread)
        kthread_stop(mnet_kthread);

    rtnl_lock();
    netdev_rx_handler_unregister(priv->real_dev);
    rtnl_unlock();

    debugfs_remove_recursive(mnet_debug_dir);
    unregister_netdev(mnet_dev);
    free_netdev(mnet_dev);
}

module_init(mnet_init);
module_exit(mnet_exit);

MODULE_AUTHOR("Karthik Revoor");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AESD Final Project - MNET Ethernet Bridge + Sensor TX/RX");
MODULE_VERSION("3.4");

