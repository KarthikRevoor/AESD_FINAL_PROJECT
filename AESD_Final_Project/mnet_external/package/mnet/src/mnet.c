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


static void mnet_process_rx(struct sk_buff *skb)
{
    struct ethhdr *eh;
    char msg[128];
    int payload_len;

    if (skb->len <= ETH_HLEN)
        return;

    eh = eth_hdr(skb);

    if (ntohs(eh->h_proto) != 0x88B5)
        return;

    payload_len = skb->len - ETH_HLEN;
    if (payload_len <= 0 || payload_len > 120)
        return;

    memcpy(msg, skb->data + ETH_HLEN, payload_len);
    msg[payload_len] = '\0';

    pr_info("mnet RX: payload = %s\n", msg);
}

/* =========================================================
 *  RX Handler: Clone frames from eth0 → mnet0
 * =========================================================*/
static rx_handler_result_t mnet_rx_handler(struct sk_buff **pskb)
{
    struct sk_buff *skb = *pskb;
    struct sk_buff *clone;

    if (!mnet_dev)
        return RX_HANDLER_PASS;

    clone = skb_clone(skb, GFP_ATOMIC);
    if (!clone)
        return RX_HANDLER_PASS;

    /* Process clone */
    mnet_process_rx(clone);

    clone->dev = mnet_dev;
    clone->protocol = eth_type_trans(clone, mnet_dev);
    clone->ip_summed = CHECKSUM_UNNECESSARY;

    netif_rx(clone);
    mnet_dev->stats.rx_packets++;
    mnet_dev->stats.rx_bytes += skb->len;

    return RX_HANDLER_PASS;
}


/* =========================================================
 *  TX Handler: mnet0 → eth0
 * =========================================================*/
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

/* =========================================================
 *  Open / Stop
 * =========================================================*/
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

/* =========================================================
 *  Netdevice Setup
 * =========================================================*/
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
 *  BME280 Temperature Reader (kernel space)
 * =========================================================*/
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
 *  Build and send custom Ethernet frame
 * =========================================================*/
static void mnet_send_temp_packet(int temp_mdegc)
{
    struct sk_buff *skb;
    unsigned char *data;
    unsigned char dest_mac[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    char payload[64];
    int payload_len;

    struct mnet_priv *priv = netdev_priv(mnet_dev);
    struct net_device *real_dev = priv->real_dev;

    if (!real_dev || !netif_running(real_dev))
        return;

    /* Build payload */
    snprintf(payload, sizeof(payload), "TEMP=%d", temp_mdegc);
    payload_len = strlen(payload);

    pr_info("mnet TX payload: %s\n", payload);   // <--- PRINT PAYLOAD HERE

    skb = alloc_skb(ETH_HLEN + payload_len, GFP_KERNEL);
    if (!skb)
        return;

    /* Payload */
    skb_reserve(skb, ETH_HLEN);
    data = skb_put(skb, payload_len);
    memcpy(data, payload, payload_len);

    /* Ethernet header */
    skb_push(skb, ETH_HLEN);
    eth_hdr(skb)->h_proto = htons(0x88B5);  // custom EtherType
    memcpy(eth_hdr(skb)->h_dest, dest_mac, ETH_ALEN);
    memcpy(eth_hdr(skb)->h_source, real_dev->dev_addr, ETH_ALEN);

    skb->dev = real_dev;
    skb->protocol = eth_hdr(skb)->h_proto;
    skb->ip_summed = CHECKSUM_NONE;

    /* Print final TX frame (header + payload) */
    print_hex_dump(KERN_INFO, "mnet TX FRAME: ", DUMP_PREFIX_NONE,
                   16, 1, skb->data, skb_headlen(skb), true);   // <--- FRAME DUMP

    dev_queue_xmit(skb);
}

/* =========================================================
 *  Kernel Sensor Thread (1 Hz)
 * =========================================================*/
static int mnet_sensor_thread(void *data)
{
    while (!kthread_should_stop()) {

        int temp = read_bme280_temp();
        if (temp > 0) {
            //pr_info("mnet: sending temp = %d mdegC\n", temp);
            mnet_send_temp_packet(temp);
        }

        msleep(1000);  // 1 Hz
    }

    return 0;
}

/* =========================================================
 *  Module Init
 * =========================================================*/
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

    /* Bind to eth0 */
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
        dev_put(eth_dev);
        free_netdev(mnet_dev);
        return ret;
    }

    ret = register_netdev(mnet_dev);
    if (ret) {
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

    pr_info("%s: registered, bridging eth0 <-> %s\n",
            DRV_NAME, mnet_dev->name);

    /* Start sensor thread */
    mnet_kthread = kthread_run(mnet_sensor_thread, NULL, "mnet_temp_thread");
    if (IS_ERR(mnet_kthread)) {
        pr_err("mnet: Failed to start sensor thread\n");
        mnet_kthread = NULL;
    }

    return 0;
}

/* =========================================================
 *  Module Exit
 * =========================================================*/
static void __exit mnet_exit(void)
{
    struct mnet_priv *priv = netdev_priv(mnet_dev);

    if (mnet_kthread)
        kthread_stop(mnet_kthread);

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
MODULE_DESCRIPTION("AESD Final Project - MNET Ethernet Bridge Driver + Sensor TX (1Hz)");
MODULE_VERSION("3.3");

