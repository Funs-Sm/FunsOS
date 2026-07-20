#ifndef VIRTIO_H
#define VIRTIO_H

#include "stdint.h"

/* ============================================================
 * VirtIO - 虚拟IO框架
 *
 * 提供虚拟化环境下的标准设备接口，支持网络、块设备、
 * 控制台、GPU、输入设备等。
 * ============================================================ */

/* VirtIO 设备ID */
#define VIRTIO_ID_NET            1
#define VIRTIO_ID_BLOCK          2
#define VIRTIO_ID_CONSOLE        3
#define VIRTIO_ID_RNG            4
#define VIRTIO_ID_BALLOON        5
#define VIRTIO_ID_IOMEM          6
#define VIRTIO_ID_RPMSG          7
#define VIRTIO_ID_SCSI           8
#define VIRTIO_ID_9P             9
#define VIRTIO_ID_MAC80211      10
#define VIRTIO_ID_RPROC_SERIAL  11
#define VIRTIO_ID_CAIF          12
#define VIRTIO_ID_GPU           16
#define VIRTIO_ID_CLOCK         17
#define VIRTIO_ID_INPUT         18
#define VIRTIO_ID_VSOCK         19
#define VIRTIO_ID_CRYPTO        20

#define VIRTIO_MAX_DEVICES       32
#define VIRTIO_MAX_DRIVERS       16
#define VIRTIO_MAX_VRINGS         4
#define VIRTIO_NAME_MAX          32
#define VIRTIO_VRING_SIZE        256

/* VirtIO 状态标志 */
#define VIRTIO_S_ACK             1
#define VIRTIO_S_DRIVER          2
#define VIRTIO_S_DRIVER_OK       4
#define VIRTIO_S_FEATURES_OK     8
#define VIRTIO_S_NEEDS_RESET    64
#define VIRTIO_S_FAILED        128

struct virtio_device;
struct virtqueue;

typedef void (*vq_callback_t)(struct virtqueue *vq);
typedef int (*vdev_probe_t)(struct virtio_device *vdev);
typedef void (*vdev_remove_t)(struct virtio_device *vdev);

typedef struct virtqueue {
    uint32_t             index;
    int                  used;
    char                 name[VIRTIO_NAME_MAX];
    uint16_t             num;          /* 队列深度 */
    uint32_t             num_free;     /* 空闲描述符 */
    uint64_t             desc_addr;
    uint64_t             avail_addr;
    uint64_t             used_addr;
    vq_callback_t        callback;
    void                *priv;
    struct virtio_device *vdev;
    uint64_t             tx_packets;
    uint64_t             rx_packets;
    uint64_t             tx_bytes;
    uint64_t             rx_bytes;
} virtqueue_t;

typedef struct virtio_driver {
    int                  id;
    int                  used;
    uint32_t             device_id;
    char                 name[VIRTIO_NAME_MAX];
    vdev_probe_t         probe;
    vdev_remove_t        remove;
    uint64_t             devices_attached;
} virtio_driver_t;

typedef struct virtio_device {
    int                  id;
    int                  used;
    uint32_t             device_id;
    char                 name[VIRTIO_NAME_MAX];
    uint8_t              status;
    uint64_t             features;
    uint32_t             vendor;
    uint32_t             vring_count;
    virtqueue_t          vrings[VIRTIO_MAX_VRINGS];
    virtio_driver_t     *driver;
    uint64_t             config_reads;
    uint64_t             config_writes;
    uint64_t             intr_count;
} virtio_device_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int virtio_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

virtio_device_t *virtio_device_register(uint32_t device_id, const char *name);
void virtio_device_unregister(virtio_device_t *vdev);
int virtio_driver_register(uint32_t device_id, const char *name,
                            vdev_probe_t probe, vdev_remove_t remove);
int virtio_add_vring(virtio_device_t *vdev, uint32_t index,
                     vq_callback_t cb, void *priv);
virtqueue_t *virtio_find_vq(virtio_device_t *vdev, uint32_t index);
int virtio_device_ready(virtio_device_t *vdev);
void virtio_device_reset(virtio_device_t *vdev);
virtio_device_t *virtio_find_device(const char *name);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void virtio_print_stats(void);

#endif /* VIRTIO_H */
