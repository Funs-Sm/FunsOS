#include "virtio.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct virtio_global {
    uint8_t              initialized;
    virtio_device_t      devices[VIRTIO_MAX_DEVICES];
    virtio_driver_t      drivers[VIRTIO_MAX_DRIVERS];
    uint32_t             dev_count;
    uint32_t             drv_count;
    uint32_t             next_dev_id;
    uint32_t             next_drv_id;
    uint64_t             total_dev_reg;
    uint64_t             total_dev_unreg;
    uint64_t             total_drv_reg;
    uint64_t             total_vring_add;
    uint64_t             total_queues;
};

static struct virtio_global vio_data;

static const char *vio_id_to_name(uint32_t id) {
    switch (id) {
        case VIRTIO_ID_NET:          return "net";
        case VIRTIO_ID_BLOCK:        return "block";
        case VIRTIO_ID_CONSOLE:      return "console";
        case VIRTIO_ID_RNG:          return "rng";
        case VIRTIO_ID_BALLOON:      return "balloon";
        case VIRTIO_ID_IOMEM:        return "iomem";
        case VIRTIO_ID_RPMSG:        return "rpmsg";
        case VIRTIO_ID_SCSI:         return "scsi";
        case VIRTIO_ID_9P:           return "9p";
        case VIRTIO_ID_GPU:          return "gpu";
        case VIRTIO_ID_CLOCK:        return "clock";
        case VIRTIO_ID_INPUT:        return "input";
        case VIRTIO_ID_VSOCK:        return "vsock";
        case VIRTIO_ID_CRYPTO:       return "crypto";
        default:                     return "unknown";
    }
}

static virtio_driver_t *vio_find_driver(uint32_t device_id) {
    for (uint32_t i = 0; i < VIRTIO_MAX_DRIVERS; i++) {
        if (vio_data.drivers[i].used && vio_data.drivers[i].device_id == device_id) {
            return &vio_data.drivers[i];
        }
    }
    return NULL;
}

static const char *vio_status_str(uint8_t s) {
    if (s & VIRTIO_S_DRIVER_OK) return "OK";
    if (s & VIRTIO_S_NEEDS_RESET) return "RESET";
    if (s & VIRTIO_S_FAILED) return "FAILED";
    if (s & VIRTIO_S_FEATURES_OK) return "FEATURES";
    if (s & VIRTIO_S_DRIVER) return "DRIVER";
    if (s & VIRTIO_S_ACK) return "ACK";
    return "RESET";
}

int virtio_init(void) {
    if (vio_data.initialized) return 0;
    memset(&vio_data, 0, sizeof(vio_data));

    /* 注册一些内置驱动 */
    virtio_driver_register(VIRTIO_ID_NET,     "virtio-net",     NULL, NULL);
    virtio_driver_register(VIRTIO_ID_BLOCK,   "virtio-blk",     NULL, NULL);
    virtio_driver_register(VIRTIO_ID_CONSOLE, "virtio-console", NULL, NULL);
    virtio_driver_register(VIRTIO_ID_RNG,     "virtio-rng",     NULL, NULL);
    virtio_driver_register(VIRTIO_ID_GPU,     "virtio-gpu",     NULL, NULL);
    virtio_driver_register(VIRTIO_ID_INPUT,   "virtio-input",   NULL, NULL);

    /* 模拟注册一些设备 */
    struct builtin_vdev {
        uint32_t id;
        const char *name;
        uint32_t vrings;
    };
    static struct builtin_vdev builtins[] = {
        { VIRTIO_ID_NET,     "virtio-net0",     2 },
        { VIRTIO_ID_BLOCK,   "virtio-blk0",     1 },
        { VIRTIO_ID_BLOCK,   "virtio-blk1",     1 },
        { VIRTIO_ID_CONSOLE, "virtio-console0", 2 },
        { VIRTIO_ID_RNG,     "virtio-rng0",     1 },
        { VIRTIO_ID_GPU,     "virtio-gpu0",     2 },
        { VIRTIO_ID_INPUT,   "virtio-input0",   1 },
        { VIRTIO_ID_VSOCK,   "virtio-vsock0",   2 },
        { 0, NULL, 0 }
    };

    for (int i = 0; builtins[i].name; i++) {
        virtio_device_t *vdev = virtio_device_register(builtins[i].id, builtins[i].name);
        if (vdev) {
            for (uint32_t q = 0; q < builtins[i].vrings; q++) {
                virtio_add_vring(vdev, q, NULL, NULL);
            }
            virtio_device_ready(vdev);
        }
    }

    vio_data.initialized = 1;
    klog_info("virtio: VirtIO framework initialized (%u devices, %u drivers)",
              vio_data.dev_count, vio_data.drv_count);
    return 0;
}

virtio_device_t *virtio_device_register(uint32_t device_id, const char *name) {
    if (!name || !vio_data.initialized) {
        /* init 过程中也会调用，做兼容 */
        if (!vio_data.initialized && name) {
            /* allow during init */
        } else {
            return NULL;
        }
    }

    for (uint32_t i = 0; i < VIRTIO_MAX_DEVICES; i++) {
        if (!vio_data.devices[i].used) {
            virtio_device_t *vdev = &vio_data.devices[i];
            memset(vdev, 0, sizeof(*vdev));
            vdev->id = vio_data.next_dev_id++;
            vdev->used = 1;
            vdev->device_id = device_id;
            vdev->vendor = 0x1AF4; /* Red Hat/QEMU vendor ID */
            vdev->features = 0x1ULL << 32 | 0xFF; /* some feature bits */
            vdev->status = VIRTIO_S_ACK | VIRTIO_S_DRIVER;
            strncpy(vdev->name, name, VIRTIO_NAME_MAX - 1);

            vdev->driver = vio_find_driver(device_id);
            if (vdev->driver) {
                vdev->driver->devices_attached++;
            }

            vio_data.dev_count++;
            vio_data.total_dev_reg++;

            klog_info("virtio: registered '%s' type=%s (id=%u)",
                      name, vio_id_to_name(device_id), device_id);
            return vdev;
        }
    }
    return NULL;
}

void virtio_device_unregister(virtio_device_t *vdev) {
    if (!vdev || !vdev->used) return;
    if (vdev->driver) {
        if (vdev->driver->remove) vdev->driver->remove(vdev);
        if (vdev->driver->devices_attached > 0)
            vdev->driver->devices_attached--;
    }
    vdev->used = 0;
    if (vio_data.dev_count > 0) vio_data.dev_count--;
    vio_data.total_dev_unreg++;
}

int virtio_driver_register(uint32_t device_id, const char *name,
                            vdev_probe_t probe, vdev_remove_t remove) {
    if (!name) return -22;

    for (uint32_t i = 0; i < VIRTIO_MAX_DRIVERS; i++) {
        if (!vio_data.drivers[i].used) {
            virtio_driver_t *drv = &vio_data.drivers[i];
            memset(drv, 0, sizeof(*drv));
            drv->id = vio_data.next_drv_id++;
            drv->used = 1;
            drv->device_id = device_id;
            drv->probe = probe;
            drv->remove = remove;
            strncpy(drv->name, name, VIRTIO_NAME_MAX - 1);
            vio_data.drv_count++;
            vio_data.total_drv_reg++;

            klog_info("virtio: registered driver '%s' for type=%s",
                      name, vio_id_to_name(device_id));
            return 0;
        }
    }
    return -12;
}

int virtio_add_vring(virtio_device_t *vdev, uint32_t index,
                     vq_callback_t cb, void *priv) {
    if (!vdev || !vdev->used) return -22;
    if (index >= VIRTIO_MAX_VRINGS) return -22;

    virtqueue_t *vq = &vdev->vrings[index];
    if (vq->used) return -16;

    memset(vq, 0, sizeof(*vq));
    vq->index = index;
    vq->used = 1;
    vq->num = VIRTIO_VRING_SIZE;
    vq->num_free = VIRTIO_VRING_SIZE;
    vq->callback = cb;
    vq->priv = priv;
    vq->vdev = vdev;
    vq->desc_addr = 0xE0000000ULL + (vdev->id * 0x10000ULL) + (index * 0x4000ULL);
    vq->avail_addr = vq->desc_addr + 0x1000;
    vq->used_addr = vq->avail_addr + 0x1000;
    strncpy(vq->name, index == 0 ? "rx" : (index == 1 ? "tx" : "ctrl"),
            VIRTIO_NAME_MAX - 1);

    vdev->vring_count++;
    vio_data.total_vring_add++;
    vio_data.total_queues++;

    return 0;
}

virtqueue_t *virtio_find_vq(virtio_device_t *vdev, uint32_t index) {
    if (!vdev || index >= VIRTIO_MAX_VRINGS) return NULL;
    return vdev->vrings[index].used ? &vdev->vrings[index] : NULL;
}

int virtio_device_ready(virtio_device_t *vdev) {
    if (!vdev || !vdev->used) return -22;
    vdev->status |= VIRTIO_S_FEATURES_OK | VIRTIO_S_DRIVER_OK;
    if (vdev->driver && vdev->driver->probe) {
        return vdev->driver->probe(vdev);
    }
    return 0;
}

void virtio_device_reset(virtio_device_t *vdev) {
    if (!vdev || !vdev->used) return;
    vdev->status = 0;
    for (uint32_t i = 0; i < VIRTIO_MAX_VRINGS; i++) {
        if (vdev->vrings[i].used) {
            vdev->vrings[i].num_free = VIRTIO_VRING_SIZE;
        }
    }
}

virtio_device_t *virtio_find_device(const char *name) {
    if (!name || !vio_data.initialized) return NULL;
    for (uint32_t i = 0; i < VIRTIO_MAX_DEVICES; i++) {
        if (vio_data.devices[i].used &&
            strcmp(vio_data.devices[i].name, name) == 0) {
            return &vio_data.devices[i];
        }
    }
    return NULL;
}

void virtio_print_stats(void) {
    if (!vio_data.initialized) {
        klog_info("virtio: not initialized");
        return;
    }

    /* 模拟设备流量 */
    static int sim_tick = 0;
    sim_tick++;
    for (uint32_t i = 0; i < VIRTIO_MAX_DEVICES; i++) {
        virtio_device_t *vdev = &vio_data.devices[i];
        if (vdev->used && (vdev->status & VIRTIO_S_DRIVER_OK)) {
            vdev->config_reads += (sim_tick + i) & 0x3;
            vdev->intr_count++;
            for (uint32_t q = 0; q < VIRTIO_MAX_VRINGS; q++) {
                virtqueue_t *vq = &vdev->vrings[q];
                if (vq->used) {
                    uint32_t pkts = (sim_tick + i + q) & 0x7;
                    if (q == 0) { /* rx */
                        vq->rx_packets += pkts;
                        vq->rx_bytes += pkts * 1500;
                    } else if (q == 1) { /* tx */
                        vq->tx_packets += pkts;
                        vq->tx_bytes += pkts * 1500;
                    }
                }
            }
        }
    }

    klog_info("=== VirtIO (Virtual IO Framework) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total devices: %u/%d", vio_data.dev_count, VIRTIO_MAX_DEVICES);
    klog_info("Total drivers: %u/%d", vio_data.drv_count, VIRTIO_MAX_DRIVERS);
    klog_info("Total device registers: %llu", (unsigned long long)vio_data.total_dev_reg);
    klog_info("Total device unregisters: %llu", (unsigned long long)vio_data.total_dev_unreg);
    klog_info("Total driver registers: %llu", (unsigned long long)vio_data.total_drv_reg);
    klog_info("Total vrings added: %llu", (unsigned long long)vio_data.total_vring_add);
    klog_info("Total active queues: %llu", (unsigned long long)vio_data.total_queues);
    klog_info("");

    klog_info("VirtIO devices:");
    for (uint32_t i = 0; i < VIRTIO_MAX_DEVICES; i++) {
        virtio_device_t *vdev = &vio_data.devices[i];
        if (vdev->used) {
            klog_info("  [%2d] %-18s type=%-8s [%s] vrings=%u intrs=%llu",
                      vdev->id, vdev->name, vio_id_to_name(vdev->device_id),
                      vio_status_str(vdev->status), vdev->vring_count,
                      (unsigned long long)vdev->intr_count);
            for (uint32_t q = 0; q < VIRTIO_MAX_VRINGS; q++) {
                virtqueue_t *vq = &vdev->vrings[q];
                if (vq->used) {
                    klog_info("       vq[%u] %-8s desc=0x%llx tx=%llu rx=%llu free=%u",
                              vq->index, vq->name,
                              (unsigned long long)vq->desc_addr,
                              (unsigned long long)vq->tx_packets,
                              (unsigned long long)vq->rx_packets,
                              vq->num_free);
                }
            }
        }
    }

    klog_info("");
    klog_info("VirtIO drivers:");
    for (uint32_t i = 0; i < VIRTIO_MAX_DRIVERS; i++) {
        virtio_driver_t *drv = &vio_data.drivers[i];
        if (drv->used) {
            klog_info("  [%2d] %-18s type=%-8s attached=%llu",
                      drv->id, drv->name, vio_id_to_name(drv->device_id),
                      (unsigned long long)drv->devices_attached);
        }
    }
}
