#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "stdint.h"

/* VirtIO PCI vendor/device IDs */
#define VIRTIO_PCI_VENDOR_ID        0x1AF4
#define VIRTIO_PCI_DEVICE_BLK       0x1001
#define VIRTIO_PCI_DEVICE_BLK_TRANS 0x1042

/* VirtIO MMIO registers */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03C
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_CONFIG          0x100

/* VirtIO status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_NEEDS_RESET   0x40
#define VIRTIO_STATUS_FAILED        0x80

/* VirtIO block feature bits */
#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)
#define VIRTIO_BLK_F_SEG_MAX        (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY       (1 << 4)
#define VIRTIO_BLK_F_RO             (1 << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1 << 6)
#define VIRTIO_BLK_F_FLUSH          (1 << 9)
#define VIRTIO_BLK_F_TOPOLOGY       (1 << 10)
#define VIRTIO_BLK_F_CONFIG_WCE     (1 << 11)
#define VIRTIO_BLK_F_MQ             (1 << 12)
#define VIRTIO_BLK_F_DISCARD        (1 << 13)
#define VIRTIO_BLK_F_WRITE_ZEROES   (1 << 14)

/* VirtIO block request types */
#define VIRTIO_BLK_T_IN             0x00000000
#define VIRTIO_BLK_T_OUT            0x00000001
#define VIRTIO_BLK_T_FLUSH          0x00000004
#define VIRTIO_BLK_T_DISCARD        0x0000000B
#define VIRTIO_BLK_T_WRITE_ZEROES   0x0000000D

/* VirtIO block response status */
#define VIRTIO_BLK_S_OK             0x00
#define VIRTIO_BLK_S_IOERR          0x01
#define VIRTIO_BLK_S_UNSUPP         0x02

/* Virtqueue limits */
#define VIRTIO_VQ_MAX_SIZE          256
#define VIRTIO_BLK_MAX_SEGMENTS     128
#define VIRTIO_BLK_SECTOR_SIZE      512

/* VirtIO block config structure */
typedef struct {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    uint16_t geometry_cylinders;
    uint8_t  geometry_heads;
    uint8_t  geometry_sectors;
    uint32_t blk_size;
    uint8_t  physical_block_exp;
    uint8_t  alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t  num_queues;
    uint8_t  writeback;
    uint8_t  reserved[3];
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t  write_zeroes_may_unmap;
    uint8_t  reserved2[3];
} __attribute__((packed)) virtio_blk_config_t;

/* VirtIO descriptor */
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtio_desc_t;

/* Descriptor flags */
#define VIRTIO_DESC_F_NEXT     0x01
#define VIRTIO_DESC_F_WRITE    0x02
#define VIRTIO_DESC_F_INDIRECT 0x04

/* VirtIO available ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_VQ_MAX_SIZE];
    uint16_t used_event;
} __attribute__((packed)) virtio_avail_t;

/* VirtIO used ring element */
typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtio_used_elem_t;

/* VirtIO used ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtio_used_elem_t ring[VIRTIO_VQ_MAX_SIZE];
    uint16_t avail_event;
} __attribute__((packed)) virtio_used_t;

/* Virtqueue */
typedef struct {
    uint32_t num;
    uint32_t free_head;
    uint16_t last_used_idx;
    virtio_desc_t *desc;
    virtio_avail_t *avail;
    virtio_used_t *used;
    uint16_t *next_avail;
    uint32_t notify_offset;
    volatile uint8_t *mmio;
} virtio_vq_t;

/* VirtIO block request header */
typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_header_t;

/* VirtIO block discard */
typedef struct {
    uint64_t sector;
    uint32_t num_sectors;
    uint32_t reserved;
} __attribute__((packed)) virtio_blk_discard_t;

/* VirtIO block device */
typedef struct {
    uint32_t pci_bus;
    uint32_t pci_dev;
    uint32_t pci_func;
    volatile uint8_t *mmio;
    uint32_t device_features;
    uint32_t driver_features;
    uint64_t capacity;
    uint32_t blk_size;
    uint8_t  read_only;
    uint8_t  flush_supported;
    uint8_t  discard_supported;
    uint8_t  write_zeroes_supported;
    virtio_blk_config_t config;
    virtio_vq_t vq;
    uint8_t  initialized;
} virtio_blk_dev_t;

/* VirtIO block API */
void virtio_blk_init(void);
int32_t virtio_blk_read(uint64_t sector, uint32_t count, void *buf);
int32_t virtio_blk_write(uint64_t sector, uint32_t count, const void *buf);
int32_t virtio_blk_flush(void);
int32_t virtio_blk_discard(uint64_t sector, uint32_t count);
int32_t virtio_blk_write_zeroes(uint64_t sector, uint32_t count);
uint64_t virtio_blk_get_capacity(void);
uint32_t virtio_blk_get_block_size(void);
int32_t virtio_blk_get_geometry(uint16_t *cyl, uint8_t *heads, uint8_t *sectors);

#endif