#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* VirtIO PCI IDs */
#define VIRTIO_VENDOR_ID    0x1AF4
#define VIRTIO_NET_DEVICE_ID 0x1000

/* VirtIO PCI configuration offsets */
#define VIRTIO_PCI_HOST_FEATURES  0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN      0x08
#define VIRTIO_PCI_QUEUE_NUM      0x0C
#define VIRTIO_PCI_QUEUE_SEL      0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY   0x10
#define VIRTIO_PCI_STATUS         0x12
#define VIRTIO_PCI_ISR            0x13
#define VIRTIO_PCI_CONFIG         0x14

/* VirtIO status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FAILED       0x80

/* Feature bits */
#define VIRTIO_NET_F_CSUM         0
#define VIRTIO_NET_F_MAC         5

/* Queue sizes */
#define VIRTIO_NET_RX_RING_SIZE  256
#define VIRTIO_NET_TX_RING_SIZE  256

/* VirtIO net header */
typedef struct {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} virtio_net_hdr_t;

/* VirtQueue descriptor */
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

/* VirtQueue available ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

/* VirtQueue used element */
typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

/* VirtQueue used ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

void virtio_net_init(uint8_t bus, uint8_t dev, uint8_t func);
int virtio_net_send(net_interface_t *iface, const void *data, uint32_t len);
void virtio_net_irq_handler(regs_t *regs);

#endif
