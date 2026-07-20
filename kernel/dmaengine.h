#ifndef DMAENGINE_H
#define DMAENGINE_H

#include "stdint.h"

#define DMAENGINE_NAME_LEN 32
#define DMA_MAX_CHANS 16
#define DMA_MAX_DEVS 4
#define DMA_MAX_DESCS 64

#define DMA_MEMCPY  0
#define DMA_MEMSET  1
#define DMA_SLAVE   2

#define DMA_STATUS_IDLE     0
#define DMA_STATUS_BUSY     1
#define DMA_STATUS_COMPLETE 2
#define DMA_STATUS_ERROR    3

struct dma_chan;
struct dma_device;

typedef void (*dma_callback_t)(void *completion_param);

struct dma_async_tx_descriptor {
    uint32_t trans_type;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t len;
    uint8_t fill_value;
    dma_callback_t callback;
    void *callback_param;
    uint8_t status;
    uint64_t submit_tick;
    uint64_t complete_tick;
};

typedef struct dma_async_tx_descriptor dma_desc_t;

struct dma_chan {
    uint32_t chan_id;
    char name[DMAENGINE_NAME_LEN];
    uint8_t in_use;
    dma_desc_t *current;
    dma_desc_t descs[DMA_MAX_DESCS];
    uint32_t n_descs;
    uint64_t memcpy_count;
    uint64_t memset_count;
    uint64_t slave_count;
    uint64_t bytes_transferred;
    uint64_t issue_count;
    uint64_t complete_count;
};

typedef struct dma_chan dma_chan_t;

typedef dma_chan_t *(*dma_request_chan_t)(struct dma_device *dev, uint32_t chan_id);
typedef int (*dma_terminate_all_t)(dma_chan_t *chan);
typedef void (*dma_issue_pending_t)(dma_chan_t *chan);

struct dma_device {
    char name[DMAENGINE_NAME_LEN];
    dma_chan_t channels[DMA_MAX_CHANS];
    uint32_t n_channels;
    dma_request_chan_t request_chan;
    dma_terminate_all_t terminate_all;
    dma_issue_pending_t issue_pending;
    void *data;
    uint64_t request_count;
    uint64_t terminate_count;
    uint64_t issue_count;
    uint64_t total_bytes;
    struct dma_device *next;
};

typedef struct dma_device dma_device_t;

int dmaengine_init(void);
int dma_device_register(dma_device_t *dev);
dma_chan_t *dma_request_channel(uint32_t chan_id);
int dmaengine_terminate_all(dma_chan_t *chan);
dma_desc_t *dmaengine_prep_memcpy(dma_chan_t *chan, uint32_t dst, uint32_t src, uint32_t len);
dma_desc_t *dmaengine_prep_memset(dma_chan_t *chan, uint32_t dst, uint8_t val, uint32_t len);
dma_desc_t *dmaengine_prep_slave(dma_chan_t *chan, uint32_t addr, uint32_t len, uint8_t direction);
int dmaengine_submit(dma_desc_t *desc);
void dma_async_issue_pending(dma_chan_t *chan);
void dmaengine_tick(void);
void dmaengine_print_stats(void);

#endif
