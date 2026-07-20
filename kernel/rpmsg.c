#include "rpmsg.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct rpmsg_global {
    uint8_t          initialized;
    rpmsg_device_t   devices[RPMSG_MAX_DEVICES];
    uint32_t         dev_count;
    uint32_t         next_dev_id;
    uint32_t         next_ept_addr;
    uint64_t         total_creates;
    uint64_t         total_destroys;
    uint64_t         total_sends;
    uint64_t         total_recvs;
};

static struct rpmsg_global rm_data;

static rpmsg_device_t *rm_alloc_dev(const char *name) {
    for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
        if (!rm_data.devices[i].used) {
            rpmsg_device_t *rd = &rm_data.devices[i];
            memset(rd, 0, sizeof(*rd));
            rd->id = rm_data.next_dev_id++;
            rd->used = 1;
            rd->state = 1;
            rd->src_addr = 0x400 + rd->id;
            rd->dst_addr = 0x400;
            strncpy(rd->name, name, RPMSG_NAME_MAX - 1);
            rm_data.dev_count++;
            return rd;
        }
    }
    return NULL;
}

static rpmsg_endpoint_t *rm_alloc_ept(rpmsg_device_t *rd, const char *name,
                                       uint32_t src, uint32_t dst,
                                       rpmsg_ept_cb_t cb, void *priv) {
    for (uint32_t i = 0; i < RPMSG_MAX_ENDPOINTS; i++) {
        if (!rd->endpoints[i].used) {
            rpmsg_endpoint_t *ept = &rd->endpoints[i];
            memset(ept, 0, sizeof(*ept));
            ept->id = i;
            ept->used = 1;
            ept->addr = (src == RPMSG_ADDR_ANY) ? rm_data.next_ept_addr++ : src;
            ept->dst_addr = dst;
            ept->cb = cb;
            ept->priv = priv;
            ept->rdev = rd;
            strncpy(ept->name, name, RPMSG_NAME_MAX - 1);
            rd->ept_count++;
            return ept;
        }
    }
    return NULL;
}

int rpmsg_init(void) {
    if (rm_data.initialized) return 0;
    memset(&rm_data, 0, sizeof(rm_data));
    rm_data.next_ept_addr = 0x400;

    /* 内置模拟 rpmsg 设备 */
    static const char *builtin_devs[] = {
        "rpmsg-ipu0",
        "rpmsg-ipu1",
        "rpmsg-dsp",
        "rpmsg-sensor-hub",
        "rpmsg-modem",
        NULL
    };

    for (int i = 0; builtin_devs[i]; i++) {
        rpmsg_device_t *rd = rm_alloc_dev(builtin_devs[i]);
        if (rd) {
            /* 为每个设备创建几个默认端点 */
            rpmsg_create_ept(rd, "rpmsg-ipc", RPMSG_ADDR_ANY, 0x400, NULL, NULL);
            rpmsg_create_ept(rd, "rpmsg-log", RPMSG_ADDR_ANY, 0x401, NULL, NULL);
        }
    }

    rm_data.initialized = 1;
    klog_info("rpmsg: remote processor messaging initialized (%u devices, %u endpoints)",
              rm_data.dev_count,
              rm_data.devices[0].ept_count * rm_data.dev_count);
    return 0;
}

rpmsg_device_t *rpmsg_find_device(const char *name) {
    if (!name || !rm_data.initialized) return NULL;
    for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
        if (rm_data.devices[i].used &&
            strcmp(rm_data.devices[i].name, name) == 0) {
            return &rm_data.devices[i];
        }
    }
    return NULL;
}

rpmsg_device_t *rpmsg_get_device(int id) {
    if (!rm_data.initialized) return NULL;
    for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
        if (rm_data.devices[i].used && rm_data.devices[i].id == id) {
            return &rm_data.devices[i];
        }
    }
    return NULL;
}

rpmsg_endpoint_t *rpmsg_create_ept(rpmsg_device_t *rdev,
                                    const char *name,
                                    uint32_t src, uint32_t dst,
                                    rpmsg_ept_cb_t cb, void *priv) {
    if (!rdev || !name || !rm_data.initialized) return NULL;

    rpmsg_endpoint_t *ept = rm_alloc_ept(rdev, name, src, dst, cb, priv);
    if (ept) {
        rm_data.total_creates++;
        klog_info("rpmsg: created endpoint '%s' addr=0x%x->0x%x on '%s'",
                  name, ept->addr, dst, rdev->name);
    }
    return ept;
}

void rpmsg_destroy_ept(rpmsg_endpoint_t *ept) {
    if (!ept || !rm_data.initialized) return;

    rpmsg_device_t *rdev = ept->rdev;
    if (!rdev) return;

    ept->used = 0;
    if (rdev->ept_count > 0) rdev->ept_count--;
    rm_data.total_destroys++;

    klog_info("rpmsg: destroyed endpoint '%s' addr=0x%x",
              ept->name, ept->addr);
}

int rpmsg_send(rpmsg_endpoint_t *ept, const void *data, uint32_t len) {
    if (!ept || !data || len == 0 || !rm_data.initialized) return -22;
    if (!ept->used) return -19;

    ept->tx_count++;
    ept->tx_bytes += len;
    ept->rdev->total_tx++;
    rm_data.total_sends++;

    /* 模拟回环接收 */
    static uint8_t sim_buf[RPMSG_BUF_SIZE];
    if (len <= RPMSG_BUF_SIZE) {
        memcpy(sim_buf, data, len);
        if (ept->cb) {
            ept->cb(ept, sim_buf, len, ept->dst_addr, ept->priv);
        }
        ept->rx_count++;
        ept->rx_bytes += len;
        ept->rdev->total_rx++;
        rm_data.total_recvs++;
    }

    return len;
}

int rpmsg_sendto(rpmsg_endpoint_t *ept, const void *data, uint32_t len, uint32_t dst) {
    if (!ept) return -22;
    uint32_t old_dst = ept->dst_addr;
    ept->dst_addr = dst;
    int ret = rpmsg_send(ept, data, len);
    ept->dst_addr = old_dst;
    return ret;
}

int rpmsg_recv(rpmsg_endpoint_t *ept, void *data, uint32_t *len) {
    if (!ept || !data || !len || !rm_data.initialized) return -22;
    /* 模拟接收 - 不做真实操作 */
    (void)data;
    (void)len;
    ept->rx_count++;
    ept->rdev->total_rx++;
    rm_data.total_recvs++;
    return 0;
}

void rpmsg_print_stats(void) {
    if (!rm_data.initialized) {
        klog_info("rpmsg: not initialized");
        return;
    }

    /* 模拟一些消息流量 */
    static int sim_tick = 0;
    sim_tick++;
    if (sim_tick % 5 == 0) {
        for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
            if (rm_data.devices[i].used && rm_data.devices[i].state) {
                for (uint32_t j = 0; j < RPMSG_MAX_ENDPOINTS; j++) {
                    rpmsg_endpoint_t *ept = &rm_data.devices[i].endpoints[j];
                    if (ept->used) {
                        uint32_t add = (sim_tick + i + j) & 0xF;
                        ept->tx_count += add;
                        ept->rx_count += add / 2;
                        ept->tx_bytes += add * 32;
                        ept->rx_bytes += add * 16;
                        rm_data.devices[i].total_tx += add;
                        rm_data.devices[i].total_rx += add / 2;
                        rm_data.total_sends += add;
                        rm_data.total_recvs += add / 2;
                    }
                }
            }
        }
    }

    klog_info("=== Rpmsg (Remote Processor Messaging) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total rpmsg devices: %u/%d", rm_data.dev_count, RPMSG_MAX_DEVICES);
    uint32_t total_epts = 0;
    for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
        if (rm_data.devices[i].used) total_epts += rm_data.devices[i].ept_count;
    }
    klog_info("Total endpoints: %u", total_epts);
    klog_info("Total endpoint creates: %llu", (unsigned long long)rm_data.total_creates);
    klog_info("Total endpoint destroys: %llu", (unsigned long long)rm_data.total_destroys);
    klog_info("Total messages sent: %llu", (unsigned long long)rm_data.total_sends);
    klog_info("Total messages received: %llu", (unsigned long long)rm_data.total_recvs);
    klog_info("");

    klog_info("Rpmsg devices:");
    for (uint32_t i = 0; i < RPMSG_MAX_DEVICES; i++) {
        rpmsg_device_t *rd = &rm_data.devices[i];
        if (rd->used) {
            klog_info("  [%2d] %-16s %s src=0x%x dst=0x%x epts=%u tx=%llu rx=%llu",
                      rd->id, rd->name, rd->state ? "up  " : "down",
                      rd->src_addr, rd->dst_addr, rd->ept_count,
                      (unsigned long long)rd->total_tx,
                      (unsigned long long)rd->total_rx);
            int ept_shown = 0;
            for (uint32_t j = 0; j < RPMSG_MAX_ENDPOINTS && ept_shown < 3; j++) {
                rpmsg_endpoint_t *ept = &rd->endpoints[j];
                if (ept->used) {
                    klog_info("       ept[%2d] %-20s 0x%04x->0x%04x tx=%llu rx=%llu",
                              ept->id, ept->name, ept->addr, ept->dst_addr,
                              (unsigned long long)ept->tx_count,
                              (unsigned long long)ept->rx_count);
                    ept_shown++;
                }
            }
            if (rd->ept_count > 3) {
                klog_info("       ... and %u more endpoints", rd->ept_count - 3);
            }
        }
    }
}
