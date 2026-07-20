#ifndef DMABUF_H
#define DMABUF_H

#include "stdint.h"

#define DMABUF_NAME_LEN 32
#define DMABUF_MAX_BUFS 16
#define DMABUF_MAX_ATTACHMENTS 8
#define DMABUF_MAX_SG_ENTRIES 16

struct dma_buf;
struct dma_buf_attachment;

struct dma_buf_ops {
    int (*map_dma_buf)(struct dma_buf_attachment *attach);
    void (*unmap_dma_buf)(struct dma_buf_attachment *attach);
    void (*release)(struct dma_buf *buf);
    void *(*vmap)(struct dma_buf *buf);
    void (*vunmap)(struct dma_buf *buf, void *vaddr);
};

struct sg_entry {
    uint32_t phys_addr;
    uint32_t size;
};

struct sg_table {
    struct sg_entry entries[DMABUF_MAX_SG_ENTRIES];
    uint32_t nents;
    uint32_t orig_nents;
};

struct dma_buf_attachment {
    struct dma_buf *dmabuf;
    void *dev;
    struct sg_table sg_table;
    uint8_t mapped;
    uint8_t dma_mapped;
    struct dma_buf_attachment *next;
};

struct dma_buf {
    char name[DMABUF_NAME_LEN];
    uint32_t size;
    uint32_t fd;
    const struct dma_buf_ops *ops;
    void *priv;
    uint32_t refcount;
    struct dma_buf_attachment *attachments;
    uint32_t n_attachments;
    void *vmap_ptr;
    uint8_t vmapped;
    uint64_t map_count;
    struct dma_buf *next;
};

int dmabuf_init(void);
struct dma_buf *dma_buf_export(const char *name, void *priv, const struct dma_buf_ops *ops, uint32_t size);
int dma_buf_fd(struct dma_buf *buf, uint32_t flags);
struct dma_buf *dma_buf_get(int fd);
void dma_buf_put(struct dma_buf *buf);
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *buf, void *dev);
void dma_buf_detach(struct dma_buf *buf, struct dma_buf_attachment *attach);
int dma_buf_map_attachment(struct dma_buf_attachment *attach);
void dma_buf_unmap_attachment(struct dma_buf_attachment *attach);
void *dma_buf_vmap(struct dma_buf *buf);
void dma_buf_vunmap(struct dma_buf *buf, void *vaddr);
void dmabuf_print_stats(void);

#endif
