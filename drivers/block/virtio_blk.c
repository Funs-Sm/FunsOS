#include "virtio_blk.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sync.h"
#include "stdio.h"
#include "string.h"

static virtio_blk_dev_t blk_dev;
static mutex_t blk_lock;

static inline uint32_t virtio_mmio_read(volatile uint8_t *mmio, uint32_t offset) {
    volatile uint32_t *ptr = (volatile uint32_t *)(mmio + offset);
    return *ptr;
}

static inline void virtio_mmio_write(volatile uint8_t *mmio, uint32_t offset, uint32_t value) {
    volatile uint32_t *ptr = (volatile uint32_t *)(mmio + offset);
    *ptr = value;
}

static int virtio_vq_init(virtio_vq_t *vq, volatile uint8_t *mmio, uint32_t qidx, uint32_t qsize) {
    virtio_mmio_write(mmio, VIRTIO_MMIO_QUEUE_SEL, qidx);

    uint32_t num_max = virtio_mmio_read(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qsize > num_max) qsize = num_max;
    if (qsize > VIRTIO_VQ_MAX_SIZE) qsize = VIRTIO_VQ_MAX_SIZE;

    virtio_mmio_write(mmio, VIRTIO_MMIO_QUEUE_NUM, qsize);

    uint32_t desc_size = qsize * sizeof(virtio_desc_t);
    uint32_t avail_size = sizeof(virtio_avail_t) + qsize * sizeof(uint16_t);
    uint32_t used_size = sizeof(virtio_used_t) + qsize * sizeof(virtio_used_elem_t);
    uint32_t total_size = desc_size + avail_size + used_size;

    /* Align to page boundary */
    total_size = (total_size + 4095) & ~4095;

    void *vq_mem = pmm_alloc_pages((total_size + 4095) / 4096);
    if (!vq_mem) return -1;
    memset(vq_mem, 0, total_size);

    vq->desc = (virtio_desc_t *)vq_mem;
    vq->avail = (virtio_avail_t *)((uint8_t *)vq_mem + desc_size);
    vq->used = (virtio_used_t *)((uint8_t *)vq_mem + desc_size + avail_size);
    vq->num = qsize;
    vq->last_used_idx = 0;
    vq->mmio = mmio;
    vq->notify_offset = qidx;

    /* Initialize descriptor chain */
    for (uint32_t i = 0; i < qsize; i++) {
        vq->desc[i].next = (uint16_t)(i + 1);
    }
    vq->desc[qsize - 1].next = 0;
    vq->free_head = 0;

    uint64_t pfn = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)vq_mem) / 4096;
    virtio_mmio_write(mmio, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)pfn);

    return 0;
}

static uint16_t virtio_vq_alloc_desc(virtio_vq_t *vq) {
    if (vq->free_head == vq->num) return 0xFFFF;
    uint16_t desc = vq->free_head;
    vq->free_head = vq->desc[desc].next;
    return desc;
}

static void virtio_vq_free_desc(virtio_vq_t *vq, uint16_t desc) {
    vq->desc[desc].next = vq->free_head;
    vq->free_head = desc;
}

static uint16_t virtio_vq_add_buf(virtio_vq_t *vq, virtio_desc_t *descs, uint32_t count) {
    if (count == 0) return 0xFFFF;

    uint16_t head = virtio_vq_alloc_desc(vq);
    if (head == 0xFFFF) return 0xFFFF;

    uint16_t current = head;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&vq->desc[current], &descs[i], sizeof(virtio_desc_t));
        if (i < count - 1) {
            uint16_t next = virtio_vq_alloc_desc(vq);
            vq->desc[current].flags |= VIRTIO_DESC_F_NEXT;
            vq->desc[current].next = next;
            current = next;
        }
    }

    uint16_t avail_idx = vq->avail->idx % vq->num;
    vq->avail->ring[avail_idx] = head;
    __asm__ volatile("mfence" ::: "memory");
    vq->avail->idx++;

    return head;
}

static void virtio_vq_kick(virtio_vq_t *vq) {
    __asm__ volatile("mfence" ::: "memory");
    virtio_mmio_write(vq->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, vq->notify_offset);
}

static int virtio_vq_get_buf(virtio_vq_t *vq, uint32_t *len) {
    if (vq->last_used_idx == vq->used->idx) {
        return -1;
    }

    uint16_t used_idx = vq->last_used_idx % vq->num;
    virtio_used_elem_t *elem = &vq->used->ring[used_idx];

    if (len) *len = elem->len;

    /* Free the descriptor chain */
    uint16_t desc = (uint16_t)elem->id;
    while (desc != 0xFFFF) {
        uint16_t next = 0xFFFF;
        if (vq->desc[desc].flags & VIRTIO_DESC_F_NEXT) {
            next = vq->desc[desc].next;
        }
        virtio_vq_free_desc(vq, desc);
        desc = next;
    }

    vq->last_used_idx++;
    return 0;
}

static int virtio_blk_do_request(uint32_t type, uint64_t sector, uint32_t count, void *buf) {
    mutex_lock(&blk_lock);

    virtio_blk_req_header_t header;
    header.type = type;
    header.reserved = 0;
    header.sector = sector;

    uint8_t status = 0xFF;
    uint32_t data_size = count * blk_dev.blk_size;

    virtio_desc_t descs[3];
    memset(descs, 0, sizeof(descs));

    /* Header descriptor (device-readable) */
    descs[0].addr = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)&header);
    descs[0].len = sizeof(virtio_blk_req_header_t);
    descs[0].flags = VIRTIO_DESC_F_NEXT;

    /* Data descriptor */
    descs[1].addr = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)buf);
    descs[1].len = data_size;
    if (type == VIRTIO_BLK_T_IN) {
        descs[1].flags = VIRTIO_DESC_F_NEXT | VIRTIO_DESC_F_WRITE;
    } else {
        descs[1].flags = VIRTIO_DESC_F_NEXT;
    }

    /* Status descriptor (device-writable) */
    descs[2].addr = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)&status);
    descs[2].len = sizeof(uint8_t);
    descs[2].flags = VIRTIO_DESC_F_WRITE;

    if (virtio_vq_add_buf(&blk_dev.vq, descs, 3) == 0xFFFF) {
        mutex_unlock(&blk_lock);
        return -1;
    }

    virtio_vq_kick(&blk_dev.vq);

    /* Wait for completion */
    uint32_t timeout = 1000000;
    while (timeout > 0) {
        if (virtio_vq_get_buf(&blk_dev.vq, 0) == 0) {
            break;
        }
        timeout--;
        __asm__ volatile("pause");
    }

    mutex_unlock(&blk_lock);

    if (timeout == 0) return -1;
    return (status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

int32_t virtio_blk_read(uint64_t sector, uint32_t count, void *buf) {
    if (!blk_dev.initialized) return -1;
    if (!buf) return -1;
    return virtio_blk_do_request(VIRTIO_BLK_T_IN, sector, count, buf);
}

int32_t virtio_blk_write(uint64_t sector, uint32_t count, const void *buf) {
    if (!blk_dev.initialized) return -1;
    if (blk_dev.read_only) return -1;
    if (!buf) return -1;
    return virtio_blk_do_request(VIRTIO_BLK_T_OUT, sector, count, (void *)buf);
}

int32_t virtio_blk_flush(void) {
    if (!blk_dev.initialized) return -1;

    mutex_lock(&blk_lock);

    virtio_blk_req_header_t header;
    header.type = VIRTIO_BLK_T_FLUSH;
    header.reserved = 0;
    header.sector = 0;

    uint8_t status = 0xFF;

    virtio_desc_t descs[2];
    memset(descs, 0, sizeof(descs));

    descs[0].addr = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)&header);
    descs[0].len = sizeof(virtio_blk_req_header_t);
    descs[0].flags = VIRTIO_DESC_F_NEXT;

    descs[1].addr = (uint64_t)(uintptr_t)vmm_get_physical(vmm_get_current_dir(),
        (uint32_t)(uintptr_t)&status);
    descs[1].len = sizeof(uint8_t);
    descs[1].flags = VIRTIO_DESC_F_WRITE;

    if (virtio_vq_add_buf(&blk_dev.vq, descs, 2) == 0xFFFF) {
        mutex_unlock(&blk_lock);
        return -1;
    }

    virtio_vq_kick(&blk_dev.vq);

    uint32_t timeout = 1000000;
    while (timeout > 0) {
        if (virtio_vq_get_buf(&blk_dev.vq, 0) == 0) break;
        timeout--;
        __asm__ volatile("pause");
    }

    mutex_unlock(&blk_lock);

    if (timeout == 0) return -1;
    return (status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

int32_t virtio_blk_discard(uint64_t sector, uint32_t count) {
    if (!blk_dev.initialized) return -1;
    if (!blk_dev.discard_supported) return -1;
    return virtio_blk_do_request(VIRTIO_BLK_T_DISCARD, sector, count, 0);
}

int32_t virtio_blk_write_zeroes(uint64_t sector, uint32_t count) {
    if (!blk_dev.initialized) return -1;
    if (!blk_dev.write_zeroes_supported) return -1;
    return virtio_blk_do_request(VIRTIO_BLK_T_WRITE_ZEROES, sector, count, 0);
}

uint64_t virtio_blk_get_capacity(void) {
    return blk_dev.capacity;
}

uint32_t virtio_blk_get_block_size(void) {
    return blk_dev.blk_size;
}

int32_t virtio_blk_get_geometry(uint16_t *cyl, uint8_t *heads, uint8_t *sectors) {
    if (!blk_dev.initialized) return -1;
    if (cyl) *cyl = blk_dev.config.geometry_cylinders;
    if (heads) *heads = blk_dev.config.geometry_heads;
    if (sectors) *sectors = blk_dev.config.geometry_sectors;
    return 0;
}

static uint32_t virtio_blk_read_config(volatile uint8_t *mmio, uint32_t offset, uint32_t size) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t byte = virtio_mmio_read(mmio, VIRTIO_MMIO_CONFIG + offset + i);
        value |= (byte << (i * 8));
    }
    return value;
}

static void virtio_blk_negotiate_features(volatile uint8_t *mmio) {
    uint32_t device_features = virtio_mmio_read(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
    blk_dev.device_features = device_features;

    uint32_t driver_features = 0;
    if (device_features & VIRTIO_BLK_F_FLUSH) {
        driver_features |= VIRTIO_BLK_F_FLUSH;
        blk_dev.flush_supported = 1;
    }
    if (device_features & VIRTIO_BLK_F_DISCARD) {
        driver_features |= VIRTIO_BLK_F_DISCARD;
        blk_dev.discard_supported = 1;
    }
    if (device_features & VIRTIO_BLK_F_WRITE_ZEROES) {
        driver_features |= VIRTIO_BLK_F_WRITE_ZEROES;
        blk_dev.write_zeroes_supported = 1;
    }
    if (device_features & VIRTIO_BLK_F_BLK_SIZE) {
        driver_features |= VIRTIO_BLK_F_BLK_SIZE;
    }
    if (device_features & VIRTIO_BLK_F_RO) {
        driver_features |= VIRTIO_BLK_F_RO;
        blk_dev.read_only = 1;
    }

    blk_dev.driver_features = driver_features;
    virtio_mmio_write(mmio, VIRTIO_MMIO_DRIVER_FEATURES, driver_features);
}

void virtio_blk_init(void) {
    memset(&blk_dev, 0, sizeof(virtio_blk_dev_t));
    mutex_init(&blk_lock);

    pci_device_t *pci_dev = pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEVICE_BLK);
    if (!pci_dev) {
        pci_dev = pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEVICE_BLK_TRANS);
    }
    if (!pci_dev) {
        printf("VirtIO block device not found\n");
        return;
    }

    blk_dev.pci_bus = pci_dev->bus;
    blk_dev.pci_dev = pci_dev->device;
    blk_dev.pci_func = pci_dev->function;

    uint32_t bar0 = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x10);
    bar0 &= 0xFFFFFFF0;

    blk_dev.mmio = (volatile uint8_t *)vmm_map_physical(bar0, 0x1000);
    if (!blk_dev.mmio) {
        printf("Failed to map VirtIO MMIO\n");
        return;
    }

    uint32_t magic = virtio_mmio_read(blk_dev.mmio, VIRTIO_MMIO_MAGIC);
    if (magic != 0x74726976) {
        printf("VirtIO magic mismatch: 0x%X\n", magic);
        return;
    }

    uint32_t version = virtio_mmio_read(blk_dev.mmio, VIRTIO_MMIO_VERSION);
    if (version < 1) {
        printf("VirtIO version too old: %d\n", version);
        return;
    }

    /* Reset device */
    virtio_mmio_write(blk_dev.mmio, VIRTIO_MMIO_STATUS, 0);

    /* Acknowledge device */
    uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_mmio_write(blk_dev.mmio, VIRTIO_MMIO_STATUS, status);

    /* Set driver status */
    status |= VIRTIO_STATUS_DRIVER;
    virtio_mmio_write(blk_dev.mmio, VIRTIO_MMIO_STATUS, status);

    /* Feature negotiation */
    virtio_blk_negotiate_features(blk_dev.mmio);

    /* Features OK */
    status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_mmio_write(blk_dev.mmio, VIRTIO_MMIO_STATUS, status);

    /* Verify features accepted */
    if (!(virtio_mmio_read(blk_dev.mmio, VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        printf("VirtIO feature negotiation failed\n");
        return;
    }

    /* Initialize virtqueue */
    if (virtio_vq_init(&blk_dev.vq, blk_dev.mmio, 0, VIRTIO_VQ_MAX_SIZE) != 0) {
        printf("VirtIO virtqueue init failed\n");
        return;
    }

    /* Driver OK */
    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_mmio_write(blk_dev.mmio, VIRTIO_MMIO_STATUS, status);

    /* Read config */
    blk_dev.capacity = virtio_blk_read_config(blk_dev.mmio, 0, 8);
    blk_dev.blk_size = VIRTIO_BLK_SECTOR_SIZE;
    if (blk_dev.driver_features & VIRTIO_BLK_F_BLK_SIZE) {
        uint32_t bs = virtio_blk_read_config(blk_dev.mmio, 20, 4);
        if (bs > 0) blk_dev.blk_size = bs;
    }

    blk_dev.initialized = 1;

    printf("VirtIO block: %llu sectors, %u bytes/sector, %s%s\n",
        blk_dev.capacity, blk_dev.blk_size,
        blk_dev.read_only ? "read-only " : "",
        blk_dev.flush_supported ? "flush" : "");
}