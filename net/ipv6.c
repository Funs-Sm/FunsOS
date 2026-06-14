#include "ipv6.h"
#include "ethernet.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"
#include "stdio.h"
#include "timer.h"
#include "sync.h"

/* ================================================================ */
/*  全局状态                                                          */
/* ================================================================ */

static neighbor_entry_t *g_neighbors = NULL;
static ipv6_route_entry_t *g_routes = NULL;
static ipv6_addr_t g_link_local;
static ipv6_addr_t g_global_addr;
static uint8_t g_prefix_len = 0;
static int g_has_global_addr = 0;
static int g_initialized = 0;
static ipv6_stats_t g_stats;
static spinlock_t g_neighbor_lock;
static spinlock_t g_route_lock;

/* ================================================================ */
/*  工具函数                                                          */
/* ================================================================ */

static uint32_t ipv6_addr_hash(const ipv6_addr_t *addr) {
    uint32_t h = 5381;
    for (int i = 0; i < 16; i++) {
        h = ((h << 5) + h) + addr->addr[i];
    }
    return h;
}

int ipv6_addr_compare(const ipv6_addr_t *a, const ipv6_addr_t *b) {
    for (int i = 0; i < 16; i++) {
        if (a->addr[i] != b->addr[i]) return 0;
    }
    return 1;
}

int ipv6_addr_is_multicast(const ipv6_addr_t *addr) {
    return addr->addr[0] == IPV6_MULTICAST_PREFIX;
}

int ipv6_addr_is_link_local(const ipv6_addr_t *addr) {
    return addr->addr[0] == 0xFE && (addr->addr[1] & 0xC0) == 0x80;
}

int ipv6_addr_is_unspecified(const ipv6_addr_t *addr) {
    for (int i = 0; i < 16; i++) {
        if (addr->addr[i] != 0) return 0;
    }
    return 1;
}

int ipv6_addr_is_loopback(const ipv6_addr_t *addr) {
    for (int i = 0; i < 15; i++) {
        if (addr->addr[i] != 0) return 0;
    }
    return addr->addr[15] == 1;
}

int ipv6_addr_is_site_local(const ipv6_addr_t *addr) {
    return addr->addr[0] == 0xFE && (addr->addr[1] & 0xC0) == 0xC0;
}

void ipv6_addr_to_str(const ipv6_addr_t *addr, char *buf, int bufsize) {
    int pos = 0;
    for (int i = 0; i < 16; i += 2) {
        if (i > 0) {
            if (pos < bufsize - 1) buf[pos++] = ':';
        }
        if (pos + 4 < bufsize) {
            pos += snprintf(buf + pos, bufsize - pos, "%02x%02x",
                           addr->addr[i], addr->addr[i + 1]);
        }
    }
    buf[pos] = '\0';
}

static int hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ipv6_addr_from_str(ipv6_addr_t *addr, const char *str) {
    if (!str || !addr) return -1;

    memset(addr, 0, 16);
    int byte_idx = 0;
    const char *p = str;

    while (*p && byte_idx < 16) {
        if (*p == ':') {
            p++;
            continue;
        }
        int hi = hex_char_to_val(*p);
        if (hi < 0) return -1;
        p++;
        int lo = hex_char_to_val(*p);
        if (lo < 0) return -1;
        p++;
        addr->addr[byte_idx++] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

void ipv6_solicited_node_mcast(const ipv6_addr_t *addr, ipv6_addr_t *mcast) {
    memset(mcast, 0, 16);
    mcast->addr[0] = 0xFF;
    mcast->addr[1] = 0x02;
    mcast->addr[11] = 0x01;
    mcast->addr[12] = 0xFF;
    mcast->addr[13] = addr->addr[13];
    mcast->addr[14] = addr->addr[14];
    mcast->addr[15] = addr->addr[15];
}

uint16_t ipv6_checksum(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                       uint8_t next_header, const void *data, uint16_t len) {
    uint32_t sum = 0;

    /* 伪头部 */
    for (int i = 0; i < 16; i += 2) {
        sum += ((uint16_t)src->addr[i] << 8) | src->addr[i + 1];
    }
    for (int i = 0; i < 16; i += 2) {
        sum += ((uint16_t)dst->addr[i] << 8) | dst->addr[i + 1];
    }

    sum += len;
    sum += next_header;

    /* 数据 */
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t data_len = len;
    while (data_len > 1) {
        sum += *ptr++;
        data_len -= 2;
    }
    if (data_len == 1) {
        sum += *(const uint8_t *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* ================================================================ */
/*  邻居缓存                                                          */
/* ================================================================ */

void ipv6_set_link_local(const uint8_t *mac) {
    g_link_local.addr[0] = 0xFE;
    g_link_local.addr[1] = 0x80;
    g_link_local.addr[2] = 0x00;
    g_link_local.addr[3] = 0x00;
    g_link_local.addr[4] = 0x00;
    g_link_local.addr[5] = 0x00;
    g_link_local.addr[6] = 0x00;
    g_link_local.addr[7] = 0x00;

    /* EUI-64: 从 MAC 地址生成接口 ID */
    g_link_local.addr[8]  = mac[0] ^ 0x02;  /* 翻转 global/local 位 */
    g_link_local.addr[9]  = mac[1];
    g_link_local.addr[10] = mac[2];
    g_link_local.addr[11] = 0xFF;
    g_link_local.addr[12] = 0xFE;
    g_link_local.addr[13] = mac[3];
    g_link_local.addr[14] = mac[4];
    g_link_local.addr[15] = mac[5];

    klog_info("IPv6: link-local address configured");
}

static neighbor_entry_t *neighbor_alloc_entry(const ipv6_addr_t *addr) {
    neighbor_entry_t *entry = (neighbor_entry_t *)kcalloc(1, sizeof(neighbor_entry_t));
    if (!entry) return NULL;
    entry->addr = *addr;
    entry->state = NEIGHBOR_STATE_INCOMPLETE;
    entry->timer = timer_get_ticks() * 10U;
    return entry;
}

static void neighbor_insert(neighbor_entry_t *entry) {
    entry->next = g_neighbors;
    g_neighbors = entry;
}

neighbor_entry_t *ipv6_neighbor_lookup(const ipv6_addr_t *addr) {
    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = g_neighbors;
    while (entry) {
        if (ipv6_addr_compare(&entry->addr, addr)) {
            spinlock_unlock(&g_neighbor_lock);
            return entry;
        }
        entry = entry->next;
    }
    spinlock_unlock(&g_neighbor_lock);
    return NULL;
}

static neighbor_entry_t *neighbor_get_or_create(const ipv6_addr_t *addr) {
    neighbor_entry_t *entry = ipv6_neighbor_lookup(addr);
    if (entry) return entry;

    entry = neighbor_alloc_entry(addr);
    if (!entry) return NULL;

    spinlock_lock(&g_neighbor_lock);
    neighbor_insert(entry);
    spinlock_unlock(&g_neighbor_lock);
    return entry;
}

int ipv6_neighbor_resolve(net_interface_t *iface, const ipv6_addr_t *addr,
                          uint8_t *mac) {
    neighbor_entry_t *entry = ipv6_neighbor_lookup(addr);
    if (entry && entry->state == NEIGHBOR_STATE_REACHABLE) {
        memcpy(mac, entry->mac, 6);
        return 0;
    }

    /* 发送邻居请求 */
    spinlock_lock(&g_neighbor_lock);
    if (!entry) {
        spinlock_unlock(&g_neighbor_lock);
        entry = neighbor_get_or_create(addr);
        if (!entry) return -1;
        spinlock_lock(&g_neighbor_lock);
    }
    entry->state = NEIGHBOR_STATE_INCOMPLETE;
    entry->timer = timer_get_ticks() * 10U;
    spinlock_unlock(&g_neighbor_lock);

    ipv6_neighbor_solicit(iface, addr);
    return -1;
}

int ipv6_neighbor_solicit(net_interface_t *iface, const ipv6_addr_t *target) {
    uint8_t packet[128];
    memset(packet, 0, sizeof(packet));

    ndp_ns_header_t *ns = (ndp_ns_header_t *)packet;
    ns->type = ICMPV6_TYPE_NEIGHBOR_SOLICIT;
    ns->code = 0;
    ns->checksum = 0;
    ns->reserved = 0;
    memcpy(ns->target, target->addr, 16);

    /* 添加源链路层地址选项 */
    uint8_t *opt = packet + sizeof(ndp_ns_header_t);
    opt[0] = 1;  /* Source Link-Layer Address */
    opt[1] = 1;  /* Length = 1 (8-byte units) */
    memcpy(opt + 2, iface->mac.bytes, 6);

    uint16_t total_len = sizeof(ndp_ns_header_t) + 8;

    /* 计算校验和 */
    ipv6_addr_t dst_mcast;
    ipv6_solicited_node_mcast(target, &dst_mcast);
    ns->checksum = ipv6_checksum(&g_link_local, &dst_mcast,
                                  IPV6_PROTO_ICMPV6, packet, total_len);

    int ret = ipv6_send_packet_raw(iface, &dst_mcast, IPV6_PROTO_ICMPV6,
                                    packet, total_len, 255);
    if (ret == 0) g_stats.nd_solicits_sent++;
    return ret;
}

int ipv6_neighbor_advertise(net_interface_t *iface, const ipv6_addr_t *target,
                            const uint8_t *mac) {
    uint8_t packet[128];
    memset(packet, 0, sizeof(packet));

    ndp_na_header_t *na = (ndp_na_header_t *)packet;
    na->type = ICMPV6_TYPE_NEIGHBOR_ADVERT;
    na->code = 0;
    na->checksum = 0;
    na->flags = 0x60;  /* Solicited | Override */
    memcpy(na->target, target->addr, 16);

    /* 添加目标链路层地址选项 */
    uint8_t *opt = packet + sizeof(ndp_na_header_t);
    opt[0] = 2;  /* Target Link-Layer Address */
    opt[1] = 1;  /* Length = 1 (8-byte units) */
    memcpy(opt + 2, mac, 6);

    uint16_t total_len = sizeof(ndp_na_header_t) + 8;

    /* 确定目标地址 */
    ipv6_addr_t dst_addr;
    neighbor_entry_t *entry = ipv6_neighbor_lookup(target);
    if (entry && entry->state == NEIGHBOR_STATE_INCOMPLETE) {
        /* 响应请求：直接单播给请求者 */
        memcpy(dst_addr.addr, entry->addr.addr, 16);
    } else {
        /* 非请求通告：全节点多播 */
        memset(&dst_addr, 0, 16);
        dst_addr.addr[0] = 0xFF;
        dst_addr.addr[1] = 0x02;
        dst_addr.addr[15] = 0x01;
    }

    na->checksum = ipv6_checksum(target, &dst_addr,
                                  IPV6_PROTO_ICMPV6, packet, total_len);

    int ret = ipv6_send_packet_raw(iface, &dst_addr, IPV6_PROTO_ICMPV6,
                                    packet, total_len, 255);
    if (ret == 0) g_stats.nd_adverts_sent++;
    return ret;
}

/* ================================================================ */
/*  路由                                                             */
/* ================================================================ */

int ipv6_route_add(const ipv6_addr_t *network, uint8_t prefix_len,
                   const ipv6_addr_t *gateway, net_interface_t *iface,
                   uint32_t metric) {
    if (prefix_len > 128) return -1;

    ipv6_route_entry_t *entry = (ipv6_route_entry_t *)kmalloc(sizeof(ipv6_route_entry_t));
    if (!entry) return -1;

    entry->network = *network;
    entry->prefix_len = prefix_len;
    entry->gateway = *gateway;
    entry->iface = iface;
    entry->metric = metric;

    spinlock_lock(&g_route_lock);
    entry->next = g_routes;
    g_routes = entry;
    spinlock_unlock(&g_route_lock);

    klog_info("IPv6: route added (prefix_len=%d)", prefix_len);
    return 0;
}

int ipv6_route_lookup(const ipv6_addr_t *dst, ipv6_addr_t *gateway,
                      net_interface_t **iface) {
    spinlock_lock(&g_route_lock);
    int best_prefix = -1;
    ipv6_route_entry_t *best = NULL;

    ipv6_route_entry_t *entry = g_routes;
    while (entry) {
        /* 检查前缀匹配 */
        int byte_count = entry->prefix_len / 8;
        int bit_remain = entry->prefix_len % 8;
        int match = 1;

        for (int i = 0; i < byte_count; i++) {
            if (dst->addr[i] != entry->network.addr[i]) {
                match = 0;
                break;
            }
        }
        if (match && bit_remain > 0) {
            uint8_t mask = (uint8_t)(0xFF << (8 - bit_remain));
            if ((dst->addr[byte_count] & mask) !=
                (entry->network.addr[byte_count] & mask)) {
                match = 0;
            }
        }

        if (match && (int)entry->prefix_len > best_prefix) {
            best_prefix = entry->prefix_len;
            best = entry;
        }
        entry = entry->next;
    }

    if (best) {
        if (gateway) *gateway = best->gateway;
        if (iface) *iface = best->iface;
        spinlock_unlock(&g_route_lock);
        return 0;
    }

    /* 回退：检查是否在同一子网 */
    if (g_has_global_addr && ipv6_addr_is_link_local(dst)) {
        /* 链路本地地址使用默认接口 */
        for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
            net_interface_t *niface = net_get_interface(i);
            if (niface && niface->up) {
                if (iface) *iface = niface;
                if (gateway) *gateway = *dst;
                spinlock_unlock(&g_route_lock);
                return 0;
            }
        }
    }

    spinlock_unlock(&g_route_lock);
    g_stats.no_route++;
    return -1;
}

int ipv6_route_del(const ipv6_addr_t *network, uint8_t prefix_len) {
    spinlock_lock(&g_route_lock);
    ipv6_route_entry_t **pp = &g_routes;
    while (*pp) {
        if (ipv6_addr_compare(&(*pp)->network, network) &&
            (*pp)->prefix_len == prefix_len) {
            ipv6_route_entry_t *del = *pp;
            *pp = del->next;
            kfree(del);
            spinlock_unlock(&g_route_lock);
            return 0;
        }
        pp = &(*pp)->next;
    }
    spinlock_unlock(&g_route_lock);
    return -1;
}

void ipv6_route_flush(void) {
    spinlock_lock(&g_route_lock);
    ipv6_route_entry_t *entry = g_routes;
    while (entry) {
        ipv6_route_entry_t *next = entry->next;
        kfree(entry);
        entry = next;
    }
    g_routes = NULL;
    spinlock_unlock(&g_route_lock);
}

/* ================================================================ */
/*  地址配置                                                          */
/* ================================================================ */

int ipv6_configure_address(const ipv6_addr_t *addr, uint8_t prefix_len) {
    g_global_addr = *addr;
    g_prefix_len = prefix_len;
    g_has_global_addr = 1;
    klog_info("IPv6: global address configured");
    return 0;
}

int ipv6_configure_address_on_iface(net_interface_t *iface,
                                     const ipv6_addr_t *addr, uint8_t prefix_len) {
    (void)iface;
    return ipv6_configure_address(addr, prefix_len);
}

int ipv6_slaac_configure(void) {
    /* SLAAC: 从路由器通告获取前缀，结合接口ID组成地址
     * 这里提供基础实现：生成链路本地地址并使用它 */
    net_interface_t *iface = net_get_default_interface();
    if (!iface || !iface->up) {
        klog_warn("IPv6: SLAAC requires an active interface");
        return -1;
    }

    /* 发送路由器请求 */
    uint8_t packet[16];
    memset(packet, 0, sizeof(packet));
    icmpv6_header_t *icmp = (icmpv6_header_t *)packet;
    icmp->type = ICMPV6_TYPE_ROUTER_SOLICIT;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->data = 0;

    ipv6_addr_t all_routers;
    memset(&all_routers, 0, 16);
    all_routers.addr[0] = 0xFF;
    all_routers.addr[1] = 0x02;
    all_routers.addr[15] = 0x02;

    icmp->checksum = ipv6_checksum(&g_link_local, &all_routers,
                                    IPV6_PROTO_ICMPV6, packet, sizeof(icmpv6_header_t));

    int ret = ipv6_send_packet_raw(iface, &all_routers, IPV6_PROTO_ICMPV6,
                                    packet, sizeof(icmpv6_header_t), 255);
    if (ret == 0) {
        g_stats.rs_sent++;
        klog_info("IPv6: router solicitation sent");
    }
    return ret;
}

/* ================================================================ */
/*  发送                                                             */
/* ================================================================ */

int ipv6_send_packet(net_interface_t *iface, const ipv6_addr_t *dst,
                     uint8_t protocol, const void *data, uint16_t len) {
    return ipv6_send_packet_raw(iface, dst, protocol, data, len,
                                IPV6_DEFAULT_HOP_LIMIT);
}

int ipv6_send_packet_raw(net_interface_t *iface, const ipv6_addr_t *dst,
                         uint8_t protocol, const void *data, uint16_t len,
                         uint8_t hop_limit) {
    if (!iface) {
        /* 尝试路由查找 */
        ipv6_addr_t gateway;
        net_interface_t *riface = NULL;
        if (ipv6_route_lookup(dst, &gateway, &riface) == 0) {
            iface = riface;
        } else {
            iface = net_get_default_interface();
        }
    }
    if (!iface || !iface->up) return -1;

    ipv6_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* 版本=6, 流量类别=0, 流标签=0 */
    hdr.ver_tc_fl = 0x60000000;
    hdr.payload_len = len;
    hdr.next_header = protocol;
    hdr.hop_limit = hop_limit;

    /* 源地址 */
    if (g_has_global_addr) {
        hdr.src = g_global_addr;
    } else {
        hdr.src = g_link_local;
    }
    hdr.dst = *dst;

    uint16_t total = sizeof(ipv6_header_t) + len;
    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;

    memcpy(packet, &hdr, sizeof(ipv6_header_t));
    if (len) memcpy(packet + sizeof(ipv6_header_t), data, len);

    /* 确定目标 MAC 地址 */
    mac_addr_t dst_mac;
    uint8_t mac_buf[6];

    if (ipv6_addr_is_multicast(dst)) {
        /* 多播 MAC: 33:33:xx:xx:xx:xx */
        dst_mac.bytes[0] = 0x33;
        dst_mac.bytes[1] = 0x33;
        dst_mac.bytes[2] = dst->addr[12];
        dst_mac.bytes[3] = dst->addr[13];
        dst_mac.bytes[4] = dst->addr[14];
        dst_mac.bytes[5] = dst->addr[15];
    } else {
        /* 查找邻居缓存 */
        neighbor_entry_t *neighbor = ipv6_neighbor_lookup(dst);
        if (neighbor && neighbor->state == NEIGHBOR_STATE_REACHABLE) {
            memcpy(dst_mac.bytes, neighbor->mac, 6);
        } else {
            /* 发送邻居请求并丢弃当前包 */
            ipv6_neighbor_solicit(iface, dst);
            kfree(packet);
            return -1;
        }
    }

    int result = ethernet_send(iface, dst_mac, ETH_P_IPV6, packet, total);
    kfree(packet);

    if (result == 0) g_stats.packets_sent++;
    return result;
}

/* ================================================================ */
/*  接收 / ICMPv6 处理                                                 */
/* ================================================================ */

static void ipv6_handle_icmpv6(net_interface_t *iface, const ipv6_addr_t *src,
                                const ipv6_addr_t *dst, const void *data,
                                uint16_t len) {
    if (len < sizeof(icmpv6_header_t)) return;

    icmpv6_header_t *icmp = (icmpv6_header_t *)data;

    switch (icmp->type) {
    case ICMPV6_TYPE_ECHO_REQUEST: {
        /* 发送 Echo Reply */
        icmpv6_header_t reply;
        reply.type = ICMPV6_TYPE_ECHO_REPLY;
        reply.code = 0;
        reply.checksum = 0;
        reply.data = icmp->data;
        reply.checksum = ipv6_checksum(dst, src, IPV6_PROTO_ICMPV6,
                                        &reply, sizeof(icmpv6_header_t));
        ipv6_send_packet_raw(iface, src, IPV6_PROTO_ICMPV6,
                            &reply, sizeof(icmpv6_header_t), 64);
        break;
    }
    case ICMPV6_TYPE_NEIGHBOR_SOLICIT: {
        if (len < sizeof(ndp_ns_header_t)) return;
        ndp_ns_header_t *ns = (ndp_ns_header_t *)data;
        ipv6_addr_t target;
        memcpy(target.addr, ns->target, 16);

        /* 检查目标地址是否属于本机 */
        if (ipv6_addr_compare(&target, &g_link_local) ||
            (g_has_global_addr && ipv6_addr_compare(&target, &g_global_addr))) {
            ipv6_neighbor_advertise(iface, &target, iface->mac.bytes);

            /* 更新邻居缓存 */
            spinlock_lock(&g_neighbor_lock);
            neighbor_entry_t *entry = ipv6_neighbor_lookup(src);
            if (!entry) {
                entry = neighbor_alloc_entry(src);
                if (entry) neighbor_insert(entry);
            }
            if (entry) {
                memcpy(entry->mac, iface->mac.bytes, 6);
                entry->state = NEIGHBOR_STATE_REACHABLE;
                entry->timer = timer_get_ticks() * 10U;
            }
            spinlock_unlock(&g_neighbor_lock);
        }
        g_stats.nd_solicits_rcvd++;
        break;
    }
    case ICMPV6_TYPE_NEIGHBOR_ADVERT: {
        if (len < sizeof(ndp_na_header_t)) return;
        ndp_na_header_t *na = (ndp_na_header_t *)data;
        ipv6_addr_t target;
        memcpy(target.addr, na->target, 16);

        spinlock_lock(&g_neighbor_lock);
        neighbor_entry_t *entry = ipv6_neighbor_lookup(&target);
        if (!entry) {
            entry = neighbor_alloc_entry(&target);
            if (entry) neighbor_insert(entry);
        }
        if (entry) {
            /* 提取目标链路层地址选项 */
            if (len >= sizeof(ndp_na_header_t) + 8) {
                uint8_t *opt = (uint8_t *)data + sizeof(ndp_na_header_t);
                if (opt[0] == 2) {  /* Target Link-Layer Address */
                    memcpy(entry->mac, opt + 2, 6);
                }
            }
            entry->state = NEIGHBOR_STATE_REACHABLE;
            entry->timer = timer_get_ticks() * 10U;
        }
        spinlock_unlock(&g_neighbor_lock);
        g_stats.nd_adverts_rcvd++;
        break;
    }
    case ICMPV6_TYPE_ROUTER_ADVERT: {
        g_stats.ra_rcvd++;
        if (len < sizeof(ndp_ra_header_t)) return;
        ndp_ra_header_t *ra = (ndp_ra_header_t *)data;
        (void)ra;

        /* 解析前缀选项以进行 SLAAC */
        uint8_t *opt = (uint8_t *)data + sizeof(ndp_ra_header_t);
        uint16_t remaining = len - sizeof(ndp_ra_header_t);
        while (remaining >= 2) {
            uint8_t opt_type = opt[0];
            uint8_t opt_len = opt[1];
            if (opt_len == 0) break;
            uint16_t opt_bytes = (uint16_t)opt_len * 8;
            if (opt_bytes > remaining || opt_bytes < 2) break;

            if (opt_type == 3 && opt_bytes >= 32) {  /* Prefix Information */
                uint8_t prefix_len = opt[2];
                if (prefix_len <= 128 && (opt[3] & 0xC0) == 0xC0) {
                    /* 自治前缀 */
                    ipv6_addr_t prefix;
                    memcpy(prefix.addr, opt + 16, 16);
                    /* 清除主机位 */
                    int byte_count = prefix_len / 8;
                    int bit_remain = prefix_len % 8;
                    if (bit_remain) {
                        prefix.addr[byte_count] &= (uint8_t)(0xFF << (8 - bit_remain));
                    }
                    for (int i = byte_count + (bit_remain ? 1 : 0); i < 16; i++) {
                        prefix.addr[i] = 0;
                    }
                    /* 用接口 ID 组成全局地址 */
                    ipv6_addr_t global_addr = prefix;
                    global_addr.addr[8]  = g_link_local.addr[8];
                    global_addr.addr[9]  = g_link_local.addr[9];
                    global_addr.addr[10] = g_link_local.addr[10];
                    global_addr.addr[11] = g_link_local.addr[11];
                    global_addr.addr[12] = g_link_local.addr[12];
                    global_addr.addr[13] = g_link_local.addr[13];
                    global_addr.addr[14] = g_link_local.addr[14];
                    global_addr.addr[15] = g_link_local.addr[15];
                    ipv6_configure_address(&global_addr, prefix_len);
                }
            }
            opt += opt_bytes;
            remaining -= opt_bytes;
        }
        break;
    }
    case ICMPV6_TYPE_ECHO_REPLY:
    case ICMPV6_TYPE_DEST_UNREACH:
    case ICMPV6_TYPE_PACKET_TOO_BIG:
    case ICMPV6_TYPE_TIME_EXCEEDED:
    default:
        break;
    }
}

int ipv6_receive_packet(net_interface_t *iface, const void *data, uint16_t len) {
    if (len < sizeof(ipv6_header_t)) return -1;

    const ipv6_header_t *hdr = (const ipv6_header_t *)data;

    /* 验证版本 */
    if (((hdr->ver_tc_fl >> 28) & 0x0F) != 6) {
        return -1;
    }

    /* 验证跳数限制 */
    if (hdr->hop_limit == 0) {
        g_stats.hop_limit_expired++;
        return -1;
    }

    /* 检查目标地址 */
    if (!ipv6_addr_is_multicast(&hdr->dst) &&
        !ipv6_addr_compare(&hdr->dst, &g_link_local) &&
        !(g_has_global_addr && ipv6_addr_compare(&hdr->dst, &g_global_addr))) {
        return -1;
    }

    g_stats.packets_rcvd++;

    const uint8_t *payload = (const uint8_t *)data + sizeof(ipv6_header_t);
    uint16_t payload_len = hdr->payload_len;
    if (payload_len > len - sizeof(ipv6_header_t)) {
        payload_len = len - sizeof(ipv6_header_t);
    }

    switch (hdr->next_header) {
    case IPV6_PROTO_ICMPV6:
        ipv6_handle_icmpv6(iface, &hdr->src, &hdr->dst, payload, payload_len);
        break;
    case IPV6_PROTO_UDP:
    case IPV6_PROTO_TCP:
        /* 传递给上层协议栈处理 */
        klog_debug("IPv6: received protocol %d packet (%d bytes)",
                   hdr->next_header, payload_len);
        break;
    default:
        break;
    }

    return 0;
}

/* ================================================================ */
/*  初始化 / 定时器                                                    */
/* ================================================================ */

void ipv6_init(void) {
    if (g_initialized) return;

    memset(&g_stats, 0, sizeof(g_stats));
    memset(&g_link_local, 0, sizeof(g_link_local));
    memset(&g_global_addr, 0, sizeof(g_global_addr));
    g_neighbors = NULL;
    g_routes = NULL;
    g_has_global_addr = 0;
    g_prefix_len = 0;

    spinlock_init(&g_neighbor_lock);
    spinlock_init(&g_route_lock);

    g_initialized = 1;
    klog_info("IPv6: initialized");
}

void ipv6_tick(uint32_t now_ms) {
    if (!g_initialized) return;

    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = g_neighbors;
    while (entry) {
        switch (entry->state) {
        case NEIGHBOR_STATE_INCOMPLETE:
            if ((uint32_t)(now_ms - entry->timer) > NEIGHBOR_TIMEOUT_MS) {
                /* 超时：标记为失败并清除 */
                entry->state = NEIGHBOR_STATE_INCOMPLETE;
                entry->timer = now_ms;
            }
            break;
        case NEIGHBOR_STATE_REACHABLE:
            if ((uint32_t)(now_ms - entry->timer) > NEIGHBOR_REACHABLE_MS) {
                entry->state = NEIGHBOR_STATE_STALE;
                entry->timer = now_ms;
            }
            break;
        case NEIGHBOR_STATE_STALE:
            if ((uint32_t)(now_ms - entry->timer) > NEIGHBOR_STALE_MS) {
                entry->state = NEIGHBOR_STATE_PROBE;
                entry->timer = now_ms;
            }
            break;
        case NEIGHBOR_STATE_PROBE:
            if ((uint32_t)(now_ms - entry->timer) > NEIGHBOR_PROBE_MS) {
                /* 探测超时：重新发送邻居请求 */
                entry->timer = now_ms;
                net_interface_t *iface = net_get_default_interface();
                if (iface) {
                    ipv6_neighbor_solicit(iface, &entry->addr);
                }
            }
            break;
        default:
            break;
        }
        entry = entry->next;
    }
    spinlock_unlock(&g_neighbor_lock);
}

const ipv6_stats_t *ipv6_get_stats(void) {
    return &g_stats;
}

/* ================================================================ */
/*  扩展功能: SLAAC DAD + EUI-64                                     */
/* ================================================================ */

int ipv6_slaac_generate_eui64(const uint8_t *mac, ipv6_addr_t *out) {
    if (!mac || !out) return -1;

    memset(out, 0, 16);
    out->addr[0] = 0xFE;
    out->addr[1] = 0x80;
    out->addr[2] = 0x00;
    out->addr[3] = 0x00;
    out->addr[4] = 0x00;
    out->addr[5] = 0x00;
    out->addr[6] = 0x00;
    out->addr[7] = 0x00;

    out->addr[8]  = mac[0] ^ 0x02;
    out->addr[9]  = mac[1];
    out->addr[10] = mac[2];
    out->addr[11] = 0xFF;
    out->addr[12] = 0xFE;
    out->addr[13] = mac[3];
    out->addr[14] = mac[4];
    out->addr[15] = mac[5];
    return 0;
}

int ipv6_slaac_perform_dad(net_interface_t *iface, const ipv6_addr_t *addr) {
    /* Duplicate Address Detection (RFC 4862 §5.4)
     * Send neighbor solicitation with unspecified source.
     * If we receive a neighbor advertisement in response,
     * the address is already in use. */
    if (!iface) return -1;

    uint8_t packet[32];
    memset(packet, 0, sizeof(packet));

    ndp_ns_header_t *ns = (ndp_ns_header_t *)packet;
    ns->type = ICMPV6_TYPE_NEIGHBOR_SOLICIT;
    ns->code = 0;
    ns->checksum = 0;
    ns->reserved = 0;
    memcpy(ns->target, addr->addr, 16);

    /* Use unspecified address as source */
    ipv6_addr_t unspec;
    memset(&unspec, 0, 16);

    ipv6_addr_t dst_mcast;
    ipv6_solicited_node_mcast(addr, &dst_mcast);

    ns->checksum = ipv6_checksum(&unspec, &dst_mcast,
                                  IPV6_PROTO_ICMPV6, packet, sizeof(ndp_ns_header_t));

    /* Send DAD probe */
    ipv6_send_packet_raw(iface, &dst_mcast, IPV6_PROTO_ICMPV6,
                         packet, sizeof(ndp_ns_header_t), 255);

    /* In a real implementation, we'd wait with a timer for responses.
     * For this embedded system, perform a quick poll. */
    uint32_t deadline = timer_get_ticks() * 10U + 2000; /* 2 second timeout */
    while ((uint32_t)((int32_t)(timer_get_ticks() * 10U) - (int32_t)deadline) < 0) {
        /* Check if any neighbor advertisement was received for this address */
        neighbor_entry_t *n = ipv6_neighbor_lookup(addr);
        if (n && n->state == NEIGHBOR_STATE_REACHABLE) {
            klog_warn("IPv6: DAD failed - duplicate address detected");
            return -1;
        }
    }

    klog_info("IPv6: DAD passed - address is unique");
    return 0;
}

/* ================================================================ */
/*  扩展功能: 路由                                                    */
/* ================================================================ */

int ipv6_route_add_default(const ipv6_addr_t *gateway, net_interface_t *iface) {
    ipv6_addr_t all_zeros;
    memset(&all_zeros, 0, 16);
    return ipv6_route_add(&all_zeros, 0, gateway, iface, 1);
}

int ipv6_route_add_blackhole(const ipv6_addr_t *network, uint8_t prefix_len) {
    if (prefix_len > 128) return -1;

    ipv6_route_entry_t *entry = (ipv6_route_entry_t *)kmalloc(sizeof(ipv6_route_entry_t));
    if (!entry) return -1;

    entry->network = *network;
    entry->prefix_len = prefix_len;
    memset(&entry->gateway, 0xFF, 16); /* blackhole marker */
    entry->iface = NULL;
    entry->metric = 0;

    spinlock_lock(&g_route_lock);
    entry->next = g_routes;
    g_routes = entry;
    spinlock_unlock(&g_route_lock);
    return 0;
}

void ipv6_routes_dump(char *buf, uint32_t buf_size) {
    if (!buf || buf_size < 2) { return; }
    buf[0] = '\0';
    int pos = 0;

    spinlock_lock(&g_route_lock);
    ipv6_route_entry_t *entry = g_routes;
    while (entry && pos < (int)buf_size - 128) {
        char net_str[40], gw_str[40];
        ipv6_addr_to_str(&entry->network, net_str, sizeof(net_str));
        ipv6_addr_to_str(&entry->gateway, gw_str, sizeof(gw_str));
        pos += snprintf(buf + pos, buf_size - pos,
                        "%s/%d via %s iface=%s metric=%d\n",
                        net_str, entry->prefix_len, gw_str,
                        entry->iface ? entry->iface->name : "null",
                        entry->metric);
        entry = entry->next;
    }
    spinlock_unlock(&g_route_lock);
}

int ipv6_get_route_count(void) {
    int count = 0;
    spinlock_lock(&g_route_lock);
    ipv6_route_entry_t *entry = g_routes;
    while (entry) { count++; entry = entry->next; }
    spinlock_unlock(&g_route_lock);
    return count;
}

/* ================================================================ */
/*  扩展功能: 邻居发现                                                 */
/* ================================================================ */

int ipv6_neighbor_add_static(const ipv6_addr_t *addr, const uint8_t *mac) {
    if (!addr || !mac) return -1;

    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = neighbor_get_or_create(addr);
    if (entry) {
        memcpy(entry->mac, mac, 6);
        entry->state = NEIGHBOR_STATE_REACHABLE;
        entry->timer = timer_get_ticks() * 10U;
    }
    spinlock_unlock(&g_neighbor_lock);
    return entry ? 0 : -1;
}

int ipv6_neighbor_del(const ipv6_addr_t *addr) {
    if (!addr) return -1;

    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t **pp = &g_neighbors;
    while (*pp) {
        if (ipv6_addr_compare(&(*pp)->addr, addr)) {
            neighbor_entry_t *del = *pp;
            *pp = del->next;
            kfree(del);
            spinlock_unlock(&g_neighbor_lock);
            return 0;
        }
        pp = &(*pp)->next;
    }
    spinlock_unlock(&g_neighbor_lock);
    return -1;
}

void ipv6_neighbor_flush(void) {
    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = g_neighbors;
    while (entry) {
        neighbor_entry_t *next = entry->next;
        kfree(entry);
        entry = next;
    }
    g_neighbors = NULL;
    spinlock_unlock(&g_neighbor_lock);
}

void ipv6_neighbors_dump(char *buf, uint32_t buf_size) {
    if (!buf || buf_size < 2) { return; }
    buf[0] = '\0';
    int pos = 0;

    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = g_neighbors;
    while (entry && pos < (int)buf_size - 100) {
        char addr_str[40];
        ipv6_addr_to_str(&entry->addr, addr_str, sizeof(addr_str));
        const char *state_str = "unknown";
        switch (entry->state) {
        case NEIGHBOR_STATE_INCOMPLETE: state_str = "INCOMPLETE"; break;
        case NEIGHBOR_STATE_REACHABLE:  state_str = "REACHABLE"; break;
        case NEIGHBOR_STATE_STALE:      state_str = "STALE"; break;
        case NEIGHBOR_STATE_PROBE:      state_str = "PROBE"; break;
        }
        pos += snprintf(buf + pos, buf_size - pos,
                        "%s %02X:%02X:%02X:%02X:%02X:%02X %s\n",
                        addr_str,
                        entry->mac[0], entry->mac[1], entry->mac[2],
                        entry->mac[3], entry->mac[4], entry->mac[5],
                        state_str);
        entry = entry->next;
    }
    spinlock_unlock(&g_neighbor_lock);
}

int ipv6_ndp_get_router_mtu(net_interface_t *iface, uint32_t *mtu) {
    /* MTU option from Router Advertisement (NDP type 5, RFC 4861 §4.6.4) */
    if (!iface || !mtu) return -1;
    *mtu = iface->mtu;
    return 0;
}

int ipv6_get_neighbor_count(void) {
    int count = 0;
    spinlock_lock(&g_neighbor_lock);
    neighbor_entry_t *entry = g_neighbors;
    while (entry) { count++; entry = entry->next; }
    spinlock_unlock(&g_neighbor_lock);
    return count;
}

/* ================================================================ */
/*  扩展功能: 数据包转发 + ICMPv6 错误                                  */
/* ================================================================ */

int ipv6_forward_packet(net_interface_t *in_iface, const void *data, uint16_t len) {
    if (!data || len < sizeof(ipv6_header_t)) return -1;

    const ipv6_header_t *hdr = (const ipv6_header_t *)data;

    /* Don't forward link-local or multicast */
    if (ipv6_addr_is_link_local(&hdr->dst) || ipv6_addr_is_multicast(&hdr->dst) ||
        ipv6_addr_is_link_local(&hdr->src)) {
        return -1;
    }

    /* Decrement hop limit */
    uint8_t new_hop = hdr->hop_limit;
    if (new_hop <= 1) {
        /* Send Time Exceeded ICMPv6 error */
        ipv6_send_error(in_iface, &hdr->src,
                        ICMPV6_TYPE_TIME_EXCEEDED, 0,
                        data, len);
        g_stats.hop_limit_expired++;
        return -1;
    }
    new_hop--;

    /* Route lookup */
    ipv6_addr_t gateway;
    net_interface_t *out_iface = NULL;
    if (ipv6_route_lookup(&hdr->dst, &gateway, &out_iface) < 0 || !out_iface) {
        /* Destination unreachable */
        ipv6_send_error(in_iface, &hdr->src,
                        ICMPV6_TYPE_DEST_UNREACH, 0,
                        data, len);
        g_stats.no_route++;
        return -1;
    }

    /* Check MTU */
    uint32_t payload_len = hdr->payload_len;
    if (payload_len + sizeof(ipv6_header_t) > out_iface->mtu) {
        /* Packet Too Big */
        ipv6_send_error(in_iface, &hdr->src,
                        ICMPV6_TYPE_PACKET_TOO_BIG, 0,
                        data, len);
        /* Set MTU field in ICMPv6 error to out_iface->mtu */
        return -1;
    }

    /* Forward packet */
    int ret = ipv6_send_packet_raw(out_iface, &hdr->dst,
                                    hdr->next_header,
                                    (const uint8_t *)data + sizeof(ipv6_header_t),
                                    (uint16_t)payload_len, new_hop);
    return ret;
}

int ipv6_send_error(net_interface_t *iface, const ipv6_addr_t *dst,
                    uint8_t type, uint8_t code,
                    const void *orig, uint16_t orig_len) {
    if (!iface || !dst || !orig) return -1;

    /* Build ICMPv6 error message including as much of the original packet
     * as will fit without exceeding 1280 bytes (IPv6 minimum MTU) */
    uint32_t max_payload = 1280 - sizeof(ipv6_header_t) - 8;
    uint32_t include_len = orig_len;
    if (include_len > max_payload) include_len = max_payload;

    uint32_t total = 8 + include_len;
    uint8_t *packet = (uint8_t *)kcalloc(1, total);
    if (!packet) return -1;

    /* ICMPv6 error header */
    packet[0] = type;
    packet[1] = code;
    packet[2] = 0; packet[3] = 0; /* checksum */
    packet[4] = 0; packet[5] = 0; packet[6] = 0; packet[7] = 0; /* unused */

    /* Copy as much original packet as fits */
    memcpy(packet + 8, orig, include_len);

    /* Compute checksum */
    uint16_t csum = ipv6_checksum(&g_link_local, dst,
                                   IPV6_PROTO_ICMPV6, packet, total);
    packet[2] = (uint8_t)(csum >> 8);
    packet[3] = (uint8_t)(csum & 0xFF);

    int ret;
    if (type == ICMPV6_TYPE_PACKET_TOO_BIG) {
        /* Set MTU field */
        uint32_t mtu = 0;
        ipv6_ndp_get_router_mtu(iface, &mtu);
        packet[4] = (uint8_t)(mtu >> 24);
        packet[5] = (uint8_t)(mtu >> 16);
        packet[6] = (uint8_t)(mtu >> 8);
        packet[7] = (uint8_t)(mtu);
    }

    ret = ipv6_send_packet_raw(iface, dst, IPV6_PROTO_ICMPV6,
                               packet, total, 64);
    kfree(packet);
    return ret;
}

void ipv6_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}