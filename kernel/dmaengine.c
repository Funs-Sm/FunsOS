#include "dmaengine.h"
#include "klog.h"
#include "string.h"

struct dma_global {
    uint8_t initialized;
    dma_device_t devices[DMA_MAX_DEVS];
    uint32_t n_devices;
    dma_device_t *dev_list;
    dma_device_t *default_dev;
    uint64_t total_requests;
    uint64_t total_terminates;
    uint64_t total_issues;
    uint64_t total_bytes;
    uint64_t total_memcpy;
    uint64_t total_memset;
    uint64_t total_slave;
};

static struct dma_global dma_data;

static dma_chan_t *virt_dma_request_chan(dma_device_t *dev, uint32_t chan_id) {
    if (!dev || chan_id >= dev->n_channels) return NULL;
    dma_chan_t *chan = &dev->channels[chan_id];
    if (chan->in_use) return NULL;
    chan->in_use = 1;
    chan->n_descs = 0;
    chan->current = NULL;
    return chan;
}

static int virt_dma_terminate_all(dma_chan_t *chan) {
    if (!chan) return -1;
    chan->current = NULL;
    chan->n_descs = 0;
    for (uint32_t i = 0; i < DMA_MAX_DESCS; i++) {
        chan->descs[i].status = DMA_STATUS_IDLE;
    }
    return 0;
}

static void virt_dma_simulate_transfer(dma_desc_t *desc) {
    if (!desc) return;
    desc->complete_tick = desc->submit_tick + 1;
    desc->status = DMA_STATUS_COMPLETE;
    if (desc->callback) desc->callback(desc->callback_param);
}

static void virt_dma_issue_pending(dma_chan_t *chan) {
    if (!chan) return;
    for (uint32_t i = 0; i < chan->n_descs; i++) {
        dma_desc_t *desc = &chan->descs[i];
        if (desc->status == DMA_STATUS_BUSY) {
            chan->current = desc;
            virt_dma_simulate_transfer(desc);
            chan->bytes_transferred += desc->len;
            chan->complete_count++;
            if (desc->trans_type == DMA_MEMCPY) chan->memcpy_count++;
            else if (desc->trans_type == DMA_MEMSET) chan->memset_count++;
            else if (desc->trans_type == DMA_SLAVE) chan->slave_count++;
        }
    }
    chan->issue_count++;
    chan->n_descs = 0;
}

static void dma_init_virtual_device(dma_device_t *dev, const char *name, uint32_t n_chans) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, name, DMAENGINE_NAME_LEN - 1);
    dev->n_channels = n_chans;
    dev->request_chan = virt_dma_request_chan;
    dev->terminate_all = virt_dma_terminate_all;
    dev->issue_pending = virt_dma_issue_pending;
    for (uint32_t i = 0; i < n_chans; i++) {
        memset(&dev->channels[i], 0, sizeof(dma_chan_t));
        dev->channels[i].chan_id = i;
        strncpy(dev->channels[i].name, "dmachan", DMAENGINE_NAME_LEN - 1);
    }
}

int dmaengine_init(void) {
    if (dma_data.initialized) return 0;
    memset(&dma_data, 0, sizeof(dma_data));

    dma_init_virtual_device(&dma_data.devices[0], "virt_dma", 8);
    dma_data.devices[0].next = NULL;
    dma_data.dev_list = &dma_data.devices[0];
    dma_data.default_dev = &dma_data.devices[0];
    dma_data.n_devices = 1;

    dma_data.initialized = 1;
    klog_info("DMAENGINE: DMA engine subsystem initialized (%u devices, %u channels)",
              dma_data.n_devices, 8);
    return 0;
}

int dma_device_register(dma_device_t *dev) {
    if (!dma_data.initialized || !dev || dma_data.n_devices >= DMA_MAX_DEVS) return -1;
    memcpy(&dma_data.devices[dma_data.n_devices], dev, sizeof(*dev));
    dma_device_t *new_dev = &dma_data.devices[dma_data.n_devices];
    new_dev->next = dma_data.dev_list;
    dma_data.dev_list = new_dev;
    dma_data.n_devices++;
    klog_info("DMAENGINE: registered device '%s' (%u channels)",
              new_dev->name, new_dev->n_channels);
    return 0;
}

dma_chan_t *dma_request_channel(uint32_t chan_id) {
    if (!dma_data.initialized || !dma_data.default_dev) return NULL;
    dma_chan_t *chan = dma_data.default_dev->request_chan(dma_data.default_dev, chan_id);
    if (chan) {
        dma_data.default_dev->request_count++;
        dma_data.total_requests++;
    }
    return chan;
}

int dmaengine_terminate_all(dma_chan_t *chan) {
    if (!chan) return -1;
    if (!chan->in_use) return -1;
    if (dma_data.default_dev->terminate_all) {
        int ret = dma_data.default_dev->terminate_all(chan);
        dma_data.default_dev->terminate_count++;
        dma_data.total_terminates++;
        chan->in_use = 0;
        return ret;
    }
    return -1;
}

dma_desc_t *dmaengine_prep_memcpy(dma_chan_t *chan, uint32_t dst, uint32_t src, uint32_t len) {
    if (!chan || !chan->in_use || chan->n_descs >= DMA_MAX_DESCS) return NULL;
    dma_desc_t *desc = &chan->descs[chan->n_descs];
    memset(desc, 0, sizeof(*desc));
    desc->trans_type = DMA_MEMCPY;
    desc->src_addr = src;
    desc->dst_addr = dst;
    desc->len = len;
    desc->status = DMA_STATUS_BUSY;
    desc->submit_tick = dma_data.total_issues;
    chan->n_descs++;
    dma_data.total_memcpy++;
    return desc;
}

dma_desc_t *dmaengine_prep_memset(dma_chan_t *chan, uint32_t dst, uint8_t val, uint32_t len) {
    if (!chan || !chan->in_use || chan->n_descs >= DMA_MAX_DESCS) return NULL;
    dma_desc_t *desc = &chan->descs[chan->n_descs];
    memset(desc, 0, sizeof(*desc));
    desc->trans_type = DMA_MEMSET;
    desc->dst_addr = dst;
    desc->fill_value = val;
    desc->len = len;
    desc->status = DMA_STATUS_BUSY;
    desc->submit_tick = dma_data.total_issues;
    chan->n_descs++;
    dma_data.total_memset++;
    return desc;
}

dma_desc_t *dmaengine_prep_slave(dma_chan_t *chan, uint32_t addr, uint32_t len, uint8_t direction) {
    (void)direction;
    if (!chan || !chan->in_use || chan->n_descs >= DMA_MAX_DESCS) return NULL;
    dma_desc_t *desc = &chan->descs[chan->n_descs];
    memset(desc, 0, sizeof(*desc));
    desc->trans_type = DMA_SLAVE;
    desc->src_addr = addr;
    desc->dst_addr = addr;
    desc->len = len;
    desc->status = DMA_STATUS_BUSY;
    desc->submit_tick = dma_data.total_issues;
    chan->n_descs++;
    dma_data.total_slave++;
    return desc;
}

int dmaengine_submit(dma_desc_t *desc) {
    if (!desc) return -1;
    (void)desc;
    return 0;
}

void dma_async_issue_pending(dma_chan_t *chan) {
    if (!chan || !dma_data.default_dev || !dma_data.default_dev->issue_pending) return;
    dma_data.default_dev->issue_pending(chan);
    dma_data.default_dev->issue_count++;
    dma_data.default_dev->total_bytes += chan->bytes_transferred;
    dma_data.total_issues++;
    dma_data.total_bytes += chan->bytes_transferred;
}

void dmaengine_tick(void) {
    if (!dma_data.initialized) return;
}

void dmaengine_print_stats(void) {
    klog_info("=== DMA Engine Subsystem Statistics ===");
    klog_info("Initialized: %s", dma_data.initialized ? "yes" : "no");
    klog_info("DMA devices: %u", dma_data.n_devices);
    klog_info("Total channel requests: %llu", (unsigned long long)dma_data.total_requests);
    klog_info("Total terminates: %llu", (unsigned long long)dma_data.total_terminates);
    klog_info("Total issues: %llu", (unsigned long long)dma_data.total_issues);
    klog_info("Total bytes transferred: %llu", (unsigned long long)dma_data.total_bytes);
    klog_info("  memcpy ops: %llu", (unsigned long long)dma_data.total_memcpy);
    klog_info("  memset ops: %llu", (unsigned long long)dma_data.total_memset);
    klog_info("  slave ops: %llu", (unsigned long long)dma_data.total_slave);
    klog_info("");

    klog_info("DMA devices:");
    dma_device_t *dev = dma_data.dev_list;
    uint32_t didx = 0;
    while (dev && didx < DMA_MAX_DEVS) {
        klog_info("  [%u] %s (%u channels)", didx, dev->name, dev->n_channels);
        klog_info("    ops: req=%llu term=%llu issue=%llu bytes=%llu",
                  (unsigned long long)dev->request_count,
                  (unsigned long long)dev->terminate_count,
                  (unsigned long long)dev->issue_count,
                  (unsigned long long)dev->total_bytes);
        klog_info("    channels:");
        for (uint32_t c = 0; c < dev->n_channels; c++) {
            dma_chan_t *ch = &dev->channels[c];
            klog_info("      chan%u: %s memcpy=%llu memset=%llu slave=%llu bytes=%llu complete=%llu",
                      c, ch->in_use ? "in-use" : "free",
                      (unsigned long long)ch->memcpy_count,
                      (unsigned long long)ch->memset_count,
                      (unsigned long long)ch->slave_count,
                      (unsigned long long)ch->bytes_transferred,
                      (unsigned long long)ch->complete_count);
        }
        klog_info("");
        dev = dev->next;
        didx++;
    }
}
