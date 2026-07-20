#include "dmabuf.h"
#include "klog.h"
#include "string.h"

struct dmabuf_global {
    uint8_t initialized;
    struct dma_buf bufs[DMABUF_MAX_BUFS];
    uint32_t n_bufs;
    struct dma_buf *buf_list;
    uint64_t total_exported;
    uint64_t total_released;
    uint64_t total_maps;
    uint64_t total_attaches;
    uint32_t next_fd;
    uint8_t dummy_storage[4096 * DMABUF_MAX_BUFS];
};

static struct dmabuf_global dmabuf_data;

static int dummy_map_dma(struct dma_buf_attachment *attach) {
    if (!attach) return -1;
    struct dma_buf *buf = attach->dmabuf;
    if (!buf) return -1;
    uint32_t buf_idx = (uint32_t)(buf - dmabuf_data.bufs);
    uint32_t offset = buf_idx * 4096;

    uint32_t page_size = 4096;
    uint32_t n_pages = (buf->size + page_size - 1) / page_size;
    if (n_pages > DMABUF_MAX_SG_ENTRIES) n_pages = DMABUF_MAX_SG_ENTRIES;

    memset(&attach->sg_table, 0, sizeof(attach->sg_table));
    attach->sg_table.nents = n_pages;
    attach->sg_table.orig_nents = n_pages;
    for (uint32_t i = 0; i < n_pages; i++) {
        attach->sg_table.entries[i].phys_addr = 0x1000000 + offset + i * page_size;
        attach->sg_table.entries[i].size = page_size;
    }
    attach->dma_mapped = 1;
    return 0;
}

static void dummy_unmap_dma(struct dma_buf_attachment *attach) {
    if (!attach) return;
    attach->dma_mapped = 0;
}

static void dummy_release(struct dma_buf *buf) {
    (void)buf;
}

static void *dummy_vmap(struct dma_buf *buf) {
    if (!buf) return NULL;
    uint32_t buf_idx = (uint32_t)(buf - dmabuf_data.bufs);
    return &dmabuf_data.dummy_storage[buf_idx * 4096];
}

static void dummy_vunmap(struct dma_buf *buf, void *vaddr) {
    (void)buf;
    (void)vaddr;
}

static struct dma_buf_ops dummy_ops = {
    dummy_map_dma,
    dummy_unmap_dma,
    dummy_release,
    dummy_vmap,
    dummy_vunmap
};

int dmabuf_init(void) {
    if (dmabuf_data.initialized) return 0;
    memset(&dmabuf_data, 0, sizeof(dmabuf_data));
    dmabuf_data.next_fd = 3;
    dmabuf_data.initialized = 1;
    klog_info("DMA-BUF: buffer sharing framework initialized (max %u bufs)", DMABUF_MAX_BUFS);
    return 0;
}

struct dma_buf *dma_buf_export(const char *name, void *priv, const struct dma_buf_ops *ops, uint32_t size) {
    if (!dmabuf_data.initialized || dmabuf_data.n_bufs >= DMABUF_MAX_BUFS) return NULL;
    struct dma_buf *buf = &dmabuf_data.bufs[dmabuf_data.n_bufs];
    memset(buf, 0, sizeof(*buf));

    if (name) strncpy(buf->name, name, DMABUF_NAME_LEN - 1);
    else strncpy(buf->name, "dma_buf", DMABUF_NAME_LEN - 1);
    buf->size = size > 0 ? size : 4096;
    buf->fd = dmabuf_data.next_fd++;
    buf->ops = ops ? ops : &dummy_ops;
    buf->priv = priv;
    buf->refcount = 1;
    buf->attachments = NULL;
    buf->n_attachments = 0;
    buf->vmap_ptr = NULL;
    buf->vmapped = 0;
    buf->map_count = 0;

    buf->next = dmabuf_data.buf_list;
    dmabuf_data.buf_list = buf;
    dmabuf_data.n_bufs++;
    dmabuf_data.total_exported++;

    if (buf->ops && buf->ops->vmap) {
        buf->vmap_ptr = buf->ops->vmap(buf);
        if (buf->vmap_ptr) buf->vmapped = 1;
    }

    return buf;
}

int dma_buf_fd(struct dma_buf *buf, uint32_t flags) {
    (void)flags;
    if (!buf) return -1;
    return (int)buf->fd;
}

struct dma_buf *dma_buf_get(int fd) {
    if (!dmabuf_data.initialized || fd < 0) return NULL;
    struct dma_buf *b = dmabuf_data.buf_list;
    while (b) {
        if ((int)b->fd == fd) {
            b->refcount++;
            return b;
        }
        b = b->next;
    }
    return NULL;
}

void dma_buf_put(struct dma_buf *buf) {
    if (!buf) return;
    if (buf->refcount > 0) buf->refcount--;
    if (buf->refcount == 0) {
        if (buf->vmapped && buf->vmap_ptr && buf->ops && buf->ops->vunmap) {
            buf->ops->vunmap(buf, buf->vmap_ptr);
            buf->vmapped = 0;
            buf->vmap_ptr = NULL;
        }
        if (buf->ops && buf->ops->release) {
            buf->ops->release(buf);
        }
        dmabuf_data.total_released++;
        struct dma_buf **prev = &dmabuf_data.buf_list;
        while (*prev) {
            if (*prev == buf) {
                *prev = buf->next;
                dmabuf_data.n_bufs--;
                memset(buf, 0, sizeof(*buf));
                return;
            }
            prev = &(*prev)->next;
        }
    }
}

struct dma_buf_attachment *dma_buf_attach(struct dma_buf *buf, void *dev) {
    if (!buf || buf->n_attachments >= DMABUF_MAX_ATTACHMENTS) return NULL;
    static struct dma_buf_attachment attach_pool[DMABUF_MAX_BUFS * DMABUF_MAX_ATTACHMENTS];
    static uint32_t attach_idx = 0;

    struct dma_buf_attachment *attach = &attach_pool[attach_idx % (DMABUF_MAX_BUFS * DMABUF_MAX_ATTACHMENTS)];
    attach_idx++;
    memset(attach, 0, sizeof(*attach));
    attach->dmabuf = buf;
    attach->dev = dev;
    attach->mapped = 0;
    attach->dma_mapped = 0;

    attach->next = buf->attachments;
    buf->attachments = attach;
    buf->n_attachments++;
    dmabuf_data.total_attaches++;

    if (buf->ops && buf->ops->map_dma_buf) {
        if (buf->ops->map_dma_buf(attach) == 0) {
            attach->mapped = 1;
        }
    }
    return attach;
}

void dma_buf_detach(struct dma_buf *buf, struct dma_buf_attachment *attach) {
    if (!buf || !attach) return;
    if (attach->mapped && buf->ops && buf->ops->unmap_dma_buf) {
        buf->ops->unmap_dma_buf(attach);
    }
    struct dma_buf_attachment **prev = &buf->attachments;
    while (*prev) {
        if (*prev == attach) {
            *prev = attach->next;
            buf->n_attachments--;
            memset(attach, 0, sizeof(*attach));
            return;
        }
        prev = &(*prev)->next;
    }
}

int dma_buf_map_attachment(struct dma_buf_attachment *attach) {
    if (!attach) return -1;
    if (attach->dma_mapped) return 0;
    if (attach->dmabuf->ops && attach->dmabuf->ops->map_dma_buf) {
        int ret = attach->dmabuf->ops->map_dma_buf(attach);
        if (ret == 0) {
            attach->mapped = 1;
            attach->dma_mapped = 1;
            dmabuf_data.total_maps++;
        }
        return ret;
    }
    return -1;
}

void dma_buf_unmap_attachment(struct dma_buf_attachment *attach) {
    if (!attach || !attach->dma_mapped) return;
    if (attach->dmabuf->ops && attach->dmabuf->ops->unmap_dma_buf) {
        attach->dmabuf->ops->unmap_dma_buf(attach);
    }
    attach->dma_mapped = 0;
    attach->mapped = 0;
}

void *dma_buf_vmap(struct dma_buf *buf) {
    if (!buf) return NULL;
    dmabuf_data.total_maps++;
    if (buf->vmap_ptr) return buf->vmap_ptr;
    if (buf->ops && buf->ops->vmap) {
        buf->vmap_ptr = buf->ops->vmap(buf);
        buf->vmapped = 1;
    }
    return buf->vmap_ptr;
}

void dma_buf_vunmap(struct dma_buf *buf, void *vaddr) {
    if (!buf || !vaddr) return;
    if (buf->vmapped && buf->ops && buf->ops->vunmap) {
        buf->ops->vunmap(buf, vaddr);
        buf->vmapped = 0;
        buf->vmap_ptr = NULL;
    }
}

static void dmabuf_create_test_buffers(void) {
    static uint8_t test_buffers_created = 0;
    if (test_buffers_created) return;
    test_buffers_created = 1;

    struct dma_buf *buf1 = dma_buf_export("camera_frame", NULL, &dummy_ops, 1920 * 1080 * 2);
    if (buf1) {
        struct dma_buf_attachment *attach = dma_buf_attach(buf1, (void *)0x1000);
        if (attach) dma_buf_map_attachment(attach);
        dma_buf_put(buf1);
    }

    struct dma_buf *buf2 = dma_buf_export("gpu_buffer", NULL, &dummy_ops, 4096);
    if (buf2) {
        void *vaddr = dma_buf_vmap(buf2);
        if (vaddr) memset(vaddr, 0xAB, 256);
        dma_buf_put(buf2);
    }
}

void dmabuf_print_stats(void) {
    dmabuf_create_test_buffers();

    klog_info("=== DMA-BUF Buffer Sharing Statistics ===");
    klog_info("Initialized: %s", dmabuf_data.initialized ? "yes" : "no");
    klog_info("Active buffers: %u", dmabuf_data.n_bufs);
    klog_info("Total exported: %llu, released: %llu",
              (unsigned long long)dmabuf_data.total_exported,
              (unsigned long long)dmabuf_data.total_released);
    klog_info("Total attaches: %llu", (unsigned long long)dmabuf_data.total_attaches);
    klog_info("Total map operations: %llu", (unsigned long long)dmabuf_data.total_maps);
    klog_info("Next FD: %u", dmabuf_data.next_fd);
    klog_info("");

    struct dma_buf *b = dmabuf_data.buf_list;
    uint32_t buf_idx = 0;
    while (b && buf_idx < DMABUF_MAX_BUFS) {
        klog_info("Buffer %u: '%s'", buf_idx, b->name);
        klog_info("  fd=%u, size=%u bytes, refcount=%u", b->fd, b->size, b->refcount);
        klog_info("  attachments: %u, vmapped: %s",
                  b->n_attachments, b->vmapped ? "yes" : "no");
        klog_info("  vmap_ptr: %p", b->vmap_ptr);

        struct dma_buf_attachment *a = b->attachments;
        uint32_t att_idx = 0;
        while (a && att_idx < DMABUF_MAX_ATTACHMENTS) {
            klog_info("    attachment %u: dev=%p, mapped=%s, sg_nents=%u",
                      att_idx, a->dev, a->dma_mapped ? "yes" : "no", a->sg_table.nents);
            for (uint32_t i = 0; i < a->sg_table.nents && i < 4; i++) {
                klog_info("      sg[%u]: phys=0x%X size=%u",
                          i, a->sg_table.entries[i].phys_addr, a->sg_table.entries[i].size);
            }
            a = a->next;
            att_idx++;
        }
        klog_info("");
        b = b->next;
        buf_idx++;
    }
}
