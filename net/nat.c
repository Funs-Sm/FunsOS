/* nat.c - 网络地址转换 (NAT) 实现
 *
 * 完整实现SNAT/DNAT功能, 包括:
 *   - NAT表管理 (添加/查找/清理)
 *   - IP数据包头部修改 (源/目标地址替换)
 *   - TCP/UDP端口重写
 *   - 超时处理和统计
 */

#include "nat.h"
#include "string.h"
#include "stdlib.h"

/* ========== 内部数据结构 ========== */

/* NAT映射表 */
static nat_entry_t nat_table[NAT_TABLE_MAX];

/* NAT网关公网IP */
static uint32_t nat_external_ip = 0;

/* 下一个可分配的端口号 (动态分配) */
static uint16_t next_ephemeral_port = NAT_PORT_START;

/* 全局统计 */
static nat_stats_t global_stats;

/* ========== 内部辅助函数 ========== */

/*
 * 分配一个临时端口号 (从NAT_PORT_START到NAT_PORT_END循环使用)
 */
static uint16_t alloc_ephemeral_port(void)
{
    uint16_t port = next_ephemeral_port++;
    if (next_ephemeral_port > NAT_PORT_END || next_ephemeral_port < NAT_PORT_START) {
        next_ephemeral_port = NAT_PORT_START;
    }
    return port;
}

/*
 * 查找空闲的NAT表槽位
 */
static int find_free_slot(void)
{
    int i;
    for (i = 0; i < NAT_TABLE_MAX; i++) {
        if (!nat_table[i].used) {
            return i;
        }
    }
    return -1;  /* 表满 */
}

/*
 * 根据协议获取默认超时时间
 */
static uint32_t get_default_timeout(uint8_t protocol)
{
    switch (protocol) {
    case 6:   /* TCP */
        return 3600;  /* ESTABLISHED状态3600秒 */
    case 17:  /* UDP */
        return 30;    /* UDP 30秒超时 */
    default:
        return 60;    /* 其他协议60秒 */
    }
}

/*
 * 获取当前系统tick (简化版本, 实际应使用系统时钟)
 */
static uint32_t get_current_tick(void)
{
    static uint32_t fake_tick = 0;
    return ++fake_tick;
}

/*
 * 计算IP头部的校验和
 * 注意: 需要在修改IP头部后重新计算
 */
static uint16_t ip_checksum(const void *data, uint16_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t sum = 0;
    uint16_t i;

    /* 按16位累加 */
    for (i = 0; i + 1 < len; i += 2) {
        sum += ((uint16_t)ptr[i] << 8) | ptr[i + 1];
    }

    /* 如果长度为奇数, 补零 */
    if (len & 1) {
        sum += (uint16_t)ptr[i] << 8;
    }

    /* 折叠32位和到16位 */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/*
 * 计算TCP/UDP伪首部校验和的辅助函数
 * 伪首部包含: 源IP(4) + 目标IP(4) + 协议(1) + 长度(2)
 */
static uint16_t pseudo_checksum(uint32_t src_ip, uint32_t dst_ip,
                                uint8_t protocol, uint16_t length,
                                const uint8_t *payload, uint16_t payload_len)
{
    uint32_t sum = 0;

    /* 源IP */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;

    /* 目标IP */
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;

    /* 协议 + 长度 */
    sum += ((uint16_t)protocol << 8) | (length & 0xFF);

    /* 负载部分 */
    const uint8_t *ptr = payload;
    uint16_t i;
    for (i = 0; i + 1 < payload_len; i += 2) {
        sum += ((uint16_t)ptr[i] << 8) | ptr[i + 1];
    }
    if (payload_len & 1) {
        sum += (uint16_t)ptr[i] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* ========== 公共API实现 ========== */

/*
 * nat_init - 初始化NAT模块
 */
void nat_init(void)
{
    memset(nat_table, 0, sizeof(nat_table));
    memset(&global_stats, 0, sizeof(global_stats));
    nat_external_ip = 0;
    next_ephemeral_port = NAT_PORT_START;
}

/*
 * nat_set_external_ip - 设置NAT网关公网IP
 */
void nat_set_external_ip(uint32_t ip)
{
    nat_external_ip = ip;
}

/*
 * nat_add_snat - 添加SNAT规则
 * 用于出站流量: 将内部地址转换为外部地址
 */
int nat_add_snat(uint32_t internal_ip, uint16_t internal_port,
                 uint32_t external_ip, uint8_t protocol)
{
    int slot = find_free_slot();
    if (slot < 0)
        return -1;  /* 表满 */

    memset(&nat_table[slot], 0, sizeof(nat_entry_t));

    nat_table[slot].internal_ip = internal_ip;
    nat_table[slot].internal_port = internal_port;
    nat_table[slot].external_ip = external_ip ? external_ip : nat_external_ip;
    nat_table[slot].external_port = alloc_ephemeral_port();
    nat_table[slot].type = NAT_SNAT;
    nat_table[slot].state = NAT_NEW;
    nat_table[slot].protocol = protocol;
    nat_table[slot].last_active = get_current_tick();
    nat_table[slot].timeout = get_default_timeout(protocol);
    nat_table[slot].used = 1;

    global_stats.snat_entries++;
    global_stats.total_entries++;

    return 0;
}

/*
 * nat_add_dnat - 添加DNAT规则
 * 用于入站流量: 将外部地址转换为内部地址
 */
int nat_add_dnat(uint32_t external_ip, uint16_t external_port,
                 uint32_t internal_ip, uint16_t internal_port,
                 uint8_t protocol)
{
    int slot = find_free_slot();
    if (slot < 0)
        return -1;  /* 表满 */

    memset(&nat_table[slot], 0, sizeof(nat_entry_t));

    nat_table[slot].external_ip = external_ip;
    nat_table[slot].external_port = external_port;
    nat_table[slot].internal_ip = internal_ip;
    nat_table[slot].internal_port = internal_port;
    nat_table[slot].type = NAT_DNAT;
    nat_table[slot].state = NAT_NEW;
    nat_table[slot].protocol = protocol;
    nat_table[slot].last_active = get_current_tick();
    nat_table[slot].timeout = get_default_timeout(protocol);
    nat_table[slot].used = 1;

    global_stats.dnat_entries++;
    global_stats.total_entries++;

    return 0;
}

/*
 * nat_lookup_snat - 查找SNAT映射 (内部->外部)
 * 根据内部地址+端口+协议查找对应的外部映射
 */
nat_entry_t *nat_lookup_snat(uint32_t internal_ip, uint16_t internal_port,
                              uint8_t protocol)
{
    int i;
    for (i = 0; i < NAT_TABLE_MAX; i++) {
        if (!nat_table[i].used)
            continue;
        if (nat_table[i].type == NAT_SNAT || nat_table[i].type == NAT_FULL) {
            if (nat_table[i].internal_ip == internal_ip &&
                nat_table[i].internal_port == internal_port &&
                nat_table[i].protocol == protocol) {
                /* 更新活跃时间 */
                nat_table[i].last_active = get_current_tick();
                return &nat_table[i];
            }
        }
    }
    return NULL;
}

/*
 * nat_lookup_dnat - 查找DNAT映射 (外部->内部)
 * 根据外部地址+端口+协议查找对应的内部映射
 */
nat_entry_t *nat_lookup_dnat(uint32_t external_ip, uint16_t external_port,
                              uint8_t protocol)
{
    int i;
    for (i = 0; i < NAT_TABLE_MAX; i++) {
        if (!nat_table[i].used)
            continue;
        if (nat_table[i].type == NAT_DNAT || nat_table[i].type == NAT_FULL) {
            if (nat_table[i].external_ip == external_ip &&
                nat_table[i].external_port == external_port &&
                nat_table[i].protocol == protocol) {
                /* 更新活跃时间 */
                nat_table[i].last_active = get_current_tick();
                return &nat_table[i];
            }
        }
    }
    return NULL;
}

/*
 * IP头部结构 (用于直接操作内存中的IP包)
 * 布局符合RFC791
 */
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;       /* 版本(4bit) + 头部长度(4bit) */
    uint8_t  tos;           /* 服务类型 */
    uint16_t total_length;  /* 总长度 */
    uint16_t identification;/* 标识 */
    uint16_t flags_frag;    /* 标志(3bit) + 片偏移(13bit) */
    uint8_t  ttl;           /* 生存时间 */
    uint8_t  protocol;      /* 上层协议 */
    uint16_t checksum;      /* 头部校验和 */
    uint32_t src_addr;      /* 源地址 */
    uint32_t dst_addr;      /* 目标地址 */
} ip_hdr_t;

/* IP头部长度常量 */
#define IP_HDR_LEN 20

/*
 * TCP/UDP通用头部结构 (仅端口部分)
 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;      /* 源端口 */
    uint16_t dst_port;      /* 目标端口 */
} transport_hdr_t;

/*
 * nat_outbound - 处理出站数据包 (应用SNAT)
 *
 * 修改IP包的:
 *   1. 源IP地址 -> 外部IP
 *   2. 源端口 -> 映射的外部端口
 *   3. 重新计算IP校验和
 *   4. 重新计算TCP/UDP校验和 (因为伪首部变化)
 */
int nat_outbound(uint8_t *packet, uint32_t len, uint8_t protocol)
{
    if (!packet || len < IP_HDR_LEN + 4)  /* 至少需要IP头+2个端口字节 */
        return -1;

    ip_hdr_t *ip = (ip_hdr_t *)packet;
    uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;  /* IHL以4字节为单位 */

    if (len < ihl + 4)
        return -1;  /* 包太短 */

    transport_hdr_t *trans = (transport_hdr_t *)(packet + ihl);

    /* 查找或创建SNAT映射 */
    nat_entry_t *entry = nat_lookup_snat(ip->src_addr, trans->src_port, protocol);

    if (!entry) {
        /* 自动创建新的SNAT映射 */
        int ret = nat_add_snat(ip->src_addr, trans->src_port, 0, protocol);
        if (ret != 0)
            return -2;  /* 无法创建映射 */

        entry = nat_lookup_snat(ip->src_addr, trans->src_port, protocol);
        if (!entry)
            return -3;
    }

    /* 更新条目状态 */
    entry->state = NAT_ESTABLISHED;

    /*
     * === 执行地址转换 ===
     */

    /* 保存原始值用于日志/调试 (可选) */
    uint32_t orig_src_ip = ip->src_addr;
    uint16_t orig_src_port = trans->src_port;

    /* 1. 替换源IP地址 */
    ip->src_addr = entry->external_ip;

    /* 2. 替换源端口 */
    trans->src_port = entry->external_port;

    /* 3. 重新计算IP头部校验和 */
    /* 先将checksum字段清零 */
    ip->checksum = 0;
    ip->checksum = ip_checksum(packet, ihl);

    /* 4. 重新计算TCP/UDP校验和 */
    /* 对于TCP (protocol=6): 校验和覆盖伪首部+TCP头+数据 */
    /* 对于UDP (protocol=17): 同上, 但允许全零表示不校验 */
    if (protocol == 6 || protocol == 17) {
        /* 定位传输层校验和字段 (在端口之后2字节处) */
        uint16_t *chksum_field = (uint16_t *)(packet + ihl + 2);
        uint16_t total_len = len - ihl;

        /* 计算新校验和 */
        *chksum_field = 0;  /* 清零后重新计算 */
        *chksum_field = pseudo_checksum(
            ip->src_addr,     /* 新的源IP */
            ip->dst_addr,     /* 目标IP不变 */
            protocol,
            total_len,
            packet + ihl,
            total_len
        );

        /* 特殊情况: UDP校验和可以为0 */
        if (protocol == 17 && *chksum_field == 0x0000) {
            /* UDP允许零校验和, 但某些实现要求非零 */
            /* 这里保持计算结果 */
        }
    }

    /* 更新统计信息 */
    entry->packets_out++;
    entry->bytes_out += len;
    global_stats.total_packets_out++;
    global_stats.total_bytes_out += len;

    return 0;
}

/*
 * nat_inbound - 处理入站数据包 (应用DNAT)
 *
 * 修改IP包的:
 *   1. 目标IP地址 -> 内部IP
 *   2. 目标端口 -> 映射的内部端口
 *   3. 重新计算IP校验和
 *   4. 重新计算TCP/UDP校验和
 */
int nat_inbound(uint8_t *packet, uint32_t len, uint8_t protocol)
{
    if (!packet || len < IP_HDR_LEN + 4)
        return -1;

    ip_hdr_t *ip = (ip_hdr_t *)packet;
    uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;

    if (len < ihl + 4)
        return -1;

    transport_hdr_t *trans = (transport_hdr_t *)(packet + ihl);

    /* 查找DNAT映射 */
    nat_entry_t *entry = nat_lookup_dnat(ip->dst_addr, trans->dst_port, protocol);

    if (!entry) {
        /* 没有找到匹配的DNAT规则 */
        return -2;
    }

    /* 更新条目状态 */
    entry->state = NAT_ESTABLISHED;

    /*
     * === 执行反向地址转换 ===
     */

    /* 1. 替换目标IP地址 */
    ip->dst_addr = entry->internal_ip;

    /* 2. 替换目标端口 */
    trans->dst_port = entry->internal_port;

    /* 3. 重新计算IP头部校验和 */
    ip->checksum = 0;
    ip->checksum = ip_checksum(packet, ihl);

    /* 4. 重新计算TCP/UDP校验和 */
    if (protocol == 6 || protocol == 17) {
        uint16_t *chksum_field = (uint16_t *)(packet + ihl + 2);
        uint16_t total_len = len - ihl;

        *chksum_field = 0;
        *chksum_field = pseudo_checksum(
            ip->src_addr,     /* 源IP不变 */
            ip->dst_addr,     /* 新的目标IP */
            protocol,
            total_len,
            packet + ihl,
            total_len
        );
    }

    /* 更新统计信息 */
    entry->packets_in++;
    entry->bytes_in += len;
    global_stats.total_packets_in++;
    global_stats.total_bytes_in += len;

    return 0;
}

/*
 * nat_cleanup - 清除过期的NAT条目
 * 应该定期调用 (例如每秒一次)
 */
void nat_cleanup(void)
{
    uint32_t now = get_current_tick();
    int i;

    for (i = 0; i < NAT_TABLE_MAX; i++) {
        if (!nat_table[i].used)
            continue;

        /* 检查是否超时 */
        if ((now - nat_table[i].last_active) > nat_table[i].timeout) {
            /* 标记为超时 */
            nat_table[i].state = NAT_TIMEOUT;

            /* 更新全局统计 */
            if (nat_table[i].type == NAT_SNAT || nat_table[i].type == NAT_FULL) {
                global_stats.snat_entries--;
            }
            if (nat_table[i].type == NAT_DNAT || nat_table[i].type == NAT_FULL) {
                global_stats.dnat_entries--;
            }
            global_stats.total_entries--;

            /* 清除条目 */
            memset(&nat_table[i], 0, sizeof(nat_entry_t));
        }
    }
}

/*
 * nat_get_stats - 获取NAT统计信息
 */
void nat_get_stats(nat_stats_t *out)
{
    if (!out)
        return;

    memcpy(out, &global_stats, sizeof(nat_stats_t));
}
