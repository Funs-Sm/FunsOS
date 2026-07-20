#ifndef RPMSG_H
#define RPMSG_H

#include "stdint.h"

/* ============================================================
 * Rpmsg - Remote Processor Messaging 远程处理器消息传递
 *
 * 提供与远程处理器（协处理器）之间的通信通道，支持
 * 创建/销毁端点、发送/接收消息。
 * ============================================================ */

#define RPMSG_MAX_DEVICES        16
#define RPMSG_MAX_ENDPOINTS      64
#define RPMSG_NAME_MAX           64
#define RPMSG_ADDR_ANY         0xFFFFFFFF
#define RPMSG_BUF_SIZE         512

struct rpmsg_device;
struct rpmsg_endpoint;

typedef int (*rpmsg_ept_cb_t)(struct rpmsg_endpoint *ept, void *data,
                              uint32_t len, uint32_t src, void *priv);
typedef void (*rpmsg_ns_cb_t)(struct rpmsg_device *rdev, uint32_t id,
                              const char *name, uint32_t dest);

typedef struct rpmsg_endpoint {
    int                  id;
    int                  used;
    char                 name[RPMSG_NAME_MAX];
    uint32_t             addr;
    uint32_t             dst_addr;
    rpmsg_ept_cb_t       cb;
    void                *priv;
    struct rpmsg_device *rdev;
    uint64_t             tx_count;
    uint64_t             rx_count;
    uint64_t             tx_bytes;
    uint64_t             rx_bytes;
} rpmsg_endpoint_t;

typedef struct rpmsg_device {
    int                  id;
    int                  used;
    char                 name[RPMSG_NAME_MAX];
    uint32_t             src_addr;
    uint32_t             dst_addr;
    int                  state;             /* 0=down, 1=up */
    rpmsg_ns_cb_t        ns_cb;
    rpmsg_endpoint_t     endpoints[RPMSG_MAX_ENDPOINTS];
    uint32_t             ept_count;
    uint64_t             total_tx;
    uint64_t             total_rx;
} rpmsg_device_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int rpmsg_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

struct rpmsg_endpoint *rpmsg_create_ept(struct rpmsg_device *rdev,
                                        const char *name,
                                        uint32_t src, uint32_t dst,
                                        rpmsg_ept_cb_t cb, void *priv);
void rpmsg_destroy_ept(struct rpmsg_endpoint *ept);
int rpmsg_send(struct rpmsg_endpoint *ept, const void *data, uint32_t len);
int rpmsg_sendto(struct rpmsg_endpoint *ept, const void *data, uint32_t len, uint32_t dst);
int rpmsg_recv(struct rpmsg_endpoint *ept, void *data, uint32_t *len);
rpmsg_device_t *rpmsg_find_device(const char *name);
rpmsg_device_t *rpmsg_get_device(int id);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void rpmsg_print_stats(void);

#endif /* RPMSG_H */
