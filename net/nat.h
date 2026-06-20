/* nat.h - 网络地址转换 (NAT) */
#ifndef NAT_H
#define NAT_H

#include "stdint.h"
#include "stddef.h"

#define NAT_TABLE_MAX    256
#define NAT_PORT_START   49152
#define NAT_PORT_END     65535

/* NAT 条目类型 */
typedef enum {
    NAT_SNAT,            /* 源地址转换 (出站) */
    NAT_DNAT,            /* 目标地址转换 (入站) */
    NAT_FULL             /* 双向NAT */
} nat_type_t;

/* NAT 条目状态 */
typedef enum {
    NAT_NEW,
    NAT_ESTABLISHED,
    NAT_CLOSING,
    NAT_TIMEOUT
} nat_state_t;

/* NAT 映射条目 */
typedef struct nat_entry {
    uint32_t  internal_ip;     /* 内部地址 */
    uint16_t  internal_port;   /* 内部端口 */
    uint32_t  external_ip;     /* 外部地址 (NAT网关公网IP) */
    uint16_t  external_port;   /* 外部映射端口 */
    uint32_t  dest_ip;         /* 目标地址 (DNAT用) */
    uint16_t  dest_port;       /* 目标端口 (DNAT用) */

    nat_type_t type;
    nat_state_t state;

    /* 协议 */
    uint8_t   protocol;        /* IPPROTO_TCP=6, UDP=17 */

    /* 超时 */
    uint32_t  last_active;     /* 最后活跃时间戳 (tick) */
    uint32_t  timeout;         /* 超时秒数 (TCP ESTABLISHED=3600, UDP=30) */

    /* 统计 */
    uint64_t  packets_in;      /* 入向报文数 */
    uint64_t  packets_out;     /* 出向报文数 */
    uint64_t  bytes_in;        /* 入向字节数 */
    uint64_t  bytes_out;       /* 出向字节数 */

    uint32_t  used;            /* 有效标志 */
} nat_entry_t;

/* 初始化NAT表 */
void nat_init(void);

/* 添加SNAT规则 (出站流量源地址替换) */
int  nat_add_snat(uint32_t internal_ip, uint16_t internal_port,
                  uint32_t external_ip, uint8_t protocol);

/* 添加DNAT规则 (入站流量目标地址替换) */
int  nat_add_dnat(uint32_t external_ip, uint16_t external_port,
                  uint32_t internal_ip, uint16_t internal_port,
                  uint8_t protocol);

/* 查找SNAT映射 (内部->外部) */
nat_entry_t *nat_lookup_snat(uint32_t internal_ip, uint16_t internal_port,
                              uint8_t protocol);

/* 查找DNAT映射 (外部->内部) */
nat_entry_t *nat_lookup_dnat(uint32_t external_ip, uint16_t external_port,
                              uint8_t protocol);

/* 处理出站数据包 (应用SNAT) */
int  nat_outbound(uint8_t *packet, uint32_t len, uint8_t protocol);

/* 处理入站数据包 (应用DNAT) */
int  nat_inbound(uint8_t *packet, uint32_t len, uint8_t protocol);

/* 清除过期条目 */
void nat_cleanup(void);

/* 获取NAT统计信息 */
typedef struct {
    uint32_t total_entries;
    uint32_t snat_entries;
    uint32_t dnat_entries;
    uint64_t total_packets_in;
    uint64_t total_packets_out;
    uint64_t total_bytes_in;
    uint64_t total_bytes_out;
} nat_stats_t;

void nat_get_stats(nat_stats_t *out);

/* 设置NAT网关公网IP */
void nat_set_external_ip(uint32_t ip);

#endif /* NAT_H */
