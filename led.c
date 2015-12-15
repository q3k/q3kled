#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/amba/xilinx_dma.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define LED_DRIVER "xilinx-dma"
#define LED_DEVICE_ID 0

#define LED_XRES 128
#define LED_YRES 128
#define BUFFER_SIZE (LED_XRES * LED_YRES * 4)

static bool led_dma_filter(struct dma_chan *chan, void *param)
{
    struct device *dma_dev, *chan_dev;
    struct device_node *dma_node, *iter_node;
    int err, channel_count = 0;
    uint32_t device_id;
    bool device_id_valid = false;

    dma_dev = chan->device->dev;
    if (strncmp(dma_dev->driver->name, LED_DRIVER, strlen(LED_DRIVER)) != 0) {
        return false;
    }
    chan_dev = &chan->dev->device;
    if (chan_dev == NULL) {
        return false;
    }

    // We're looking for a DMA device with a single channel (the one we're
    // iterating over).
    dma_node = dma_dev->of_node;
    if (dma_node == NULL) {
        return false;
    }
    for_each_child_of_node(dma_node, iter_node) {
        channel_count++;
        err = of_property_read_u32(iter_node, "xlnx,device-id", &device_id);
        if (!err) {
            device_id_valid = true;
        }
    }
    if (channel_count != 1 || !device_id_valid) {
        return false;
    }

    if (device_id == LED_DEVICE_ID) {
        printk(KERN_INFO "q3kled: found dma channel dma%uchan%u\n", chan->dev->dev_id, chan->chan_id);
        return true;
    }
   
    return false;
}

static char *tx_buffer = NULL;
static uint32_t tx_buffer_phy;
static dma_cookie_t tx_cookie;

static void init_buffer(void)
{
    int x, y;
    for (x = 0; x < LED_XRES; x++) {
        for (y = 0; y < LED_YRES; y++) {
            tx_buffer[y*LED_YRES*4 + x*4] = x ^ y;
        }
    }
}

static void axidma_sync_callback(void *completion)
{
    printk(KERN_INFO "q3kled: sent!\n");
}

struct dma_chan *tx_chan = NULL;
struct dma_async_tx_descriptor *chan_desc = NULL;

static int transmit(void *data)
{
    while (!kthread_should_stop()) {
        struct dma_async_tx_descriptor *chan_desc;
        uint32_t buf_size = BUFFER_SIZE;
        enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

        chan_desc = dmaengine_prep_slave_single(tx_chan, tx_buffer_phy, buf_size, DMA_DEV_TO_MEM, flags);
        if (!chan_desc) {
            printk(KERN_ERR "q3kled: dmaengine_prep_slave_single error\n");
            return -EBUSY;
        }

        chan_desc->callback = axidma_sync_callback;
        tx_cookie = dmaengine_submit(chan_desc);
        if (dma_submit_error(tx_cookie)) {
            printk(KERN_ERR "q3kled: submit error\n");
            return -EBUSY;
        }

        dma_async_issue_pending(tx_chan);
        printk(KERN_INFO "q3kled: issued\n");
        dma_wait_for_async_tx(chan_desc);
        printk(KERN_INFO "q3kled: waited\n");
        msleep(1000);

    }
    printk("q3kled: stopping worker\n");
    return 0;
}

struct task_struct *task = NULL;

static int __init led_init(void)
{
    dma_cap_mask_t mask;

    dma_cap_zero(mask);
    dma_cap_set(DMA_SLAVE | DMA_PRIVATE, mask);

    tx_chan = dma_request_channel(mask, led_dma_filter, NULL);
    if (tx_chan == NULL) {
        pr_err("q3kled: could not allocate TX channel\n");
        return -ENODEV;
    }

    tx_buffer = dma_alloc_coherent(tx_chan->device->dev, PAGE_ALIGN(BUFFER_SIZE), &tx_buffer_phy, GFP_KERNEL);
    if (tx_buffer == NULL) {
        pr_err("q3kled: could not allocate TX memory\n");
        dma_release_channel(tx_chan);
        tx_chan = NULL;
        return -ENOMEM;
    }
    printk(KERN_INFO "q3kled: buffer @ %08x/%08x\n", tx_buffer, tx_buffer_phy);

    init_buffer();
    smp_wmb();
    
    task = kthread_run(transmit, NULL, "xD");
    return 0;
}

static void __exit led_exit(void)
{
    enum dma_status status;
    struct dma_tx_state state;

    if (task) {
        kthread_stop(task);
    }
    if (tx_chan) {
        status = dmaengine_tx_status(tx_chan, tx_cookie, &state);
        printk(KERN_INFO "q3kled: unload status %i\n", status);
        dma_release_channel(tx_chan);
    }
    if (tx_buffer) {
        dma_free_coherent(tx_chan->device->dev, PAGE_ALIGN(BUFFER_SIZE), tx_buffer, tx_buffer_phy);
    }
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("Dual BSD/GPL");
