#include "net.h"
#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "udp.h"
#include "icmp.h"
#include "loopback.h"
#include "route.h"
#include "netfilter.h"
#include "raw_sock.h"
#include "ethernet.h"
#include "dhcp.h"
#include "dns.h"
#include "igmp.h"
#include "ipv6.h"
#include "net_buf_pool.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "timer.h"
#include "stddef.h"

static net_interface_t *interfaces[NET_MAX_INTERFACES];
static uint32_t iface_count;
static net_buffer_t *rx_queue_head;
static net_buffer_t *rx_queue_tail;
static mutex_t       net_lock;
static net_stats_t   net_stats;

void net_init(void) {
    for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
        interfaces[i] = NULL;
    }
    iface_count = 0;
    mutex_init(&net_lock);
    rx_queue_head = NULL;
    rx_queue_tail = NULL;
    memset(&net_stats, 0, sizeof(net_stats));

    /* Bring up the loopback interface first so that userland can
     * always talk to itself, even when no NIC driver is loaded. */
    loopback_install();
    route_init();
    netfilter_init();
    raw_sock_init();
    dhcp_client_init();
    dns_init();
    igmp_init();

    /* New feature initialization */
    ipv6_init();               /* IPv6 stack (ICMPv6, NDP, SLAAC) */
    vlan_init();               /* VLAN management (802.1Q) */
    bridge_init();             /* Network bridge (802.1D) */
    mcast_forwarder_enable(1); /* Multicast routing (IGMP snooping) */
    qos_init();                /* QoS packet scheduling (pfifo_fast + TBF) */
    ipsec_init();              /* IPsec security (AH/ESP) */
}

void net_register_interface(net_interface_t *iface) {
    if (!iface) return;
    mutex_lock(&net_lock);
    if (iface_count < NET_MAX_INTERFACES) {
        interfaces[iface_count] = iface;
        iface_count++;
        if (iface->flags == 0) iface->flags = IFF_UP;
    }
    mutex_unlock(&net_lock);
    /* Install direct / default routes for the new interface so that
     * the routing table reflects the topology. */
    if (iface->flags & IFF_UP) {
        route_install_iface_defaults(iface);
    }
}

net_interface_t *net_get_interface(uint32_t index) {
    if (index < iface_count) {
        return interfaces[index];
    }
    return NULL;
}

uint32_t net_get_interface_count(void) {
    return iface_count;
}

net_interface_t *net_get_interface_by_name(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < iface_count; i++) {
        net_interface_t *n = interfaces[i];
        if (!n) continue;
        int match = 1;
        for (int k = 0; k < 15; k++) {
            if (n->name[k] != name[k]) { match = 0; break; }
            if (n->name[k] == 0 && name[k] == 0) break;
        }
        if (match) return n;
    }
    return NULL;
}

net_interface_t *net_get_default_interface(void) {
    for (uint32_t i = 0; i < iface_count; i++) {
        net_interface_t *n = interfaces[i];
        if (n && n->up && (n->flags & IFF_UP) && !(n->flags & IFF_LOOPBACK)) {
            return n;
        }
    }
    for (uint32_t i = 0; i < iface_count; i++) {
        if (interfaces[i] && interfaces[i]->up) return interfaces[i];
    }
    return NULL;
}

void net_set_interface_flags(net_interface_t *iface, uint32_t flags) {
    if (!iface) return;
    mutex_lock(&net_lock);
    iface->flags = flags;
    iface->up    = (flags & IFF_UP) ? 1 : 0;
    mutex_unlock(&net_lock);
}

int net_set_interface_address(net_interface_t *iface, ipv4_addr_t ip,
                              ipv4_addr_t mask, ipv4_addr_t gw) {
    if (!iface) return -1;
    mutex_lock(&net_lock);
    iface->ip      = ip;
    iface->mask    = mask;
    iface->gateway = gw;
    mutex_unlock(&net_lock);
    return 0;
}

net_buffer_t *net_alloc_buffer(void) {
    net_buffer_t *b = net_buf_pool_alloc();
    if (b) {
        memset(b, 0, sizeof(*b));
    }
    return b;
}

void net_free_buffer(net_buffer_t *buf) {
    if (buf) net_buf_pool_free(buf);
}

void net_receive(net_buffer_t *buf) {
    if (!buf) return;
    buf->next = NULL;
    mutex_lock(&net_lock);
    if (rx_queue_tail) {
        rx_queue_tail->next = buf;
    } else {
        rx_queue_head = buf;
    }
    rx_queue_tail = buf;
    net_stats.rx_packets++;
    if (buf->len > 14) net_stats.rx_bytes += buf->len;
    mutex_unlock(&net_lock);

    /* Drain synchronously. */
    net_buffer_t *current = rx_queue_head;
    while (current) {
        ethernet_receive(current);
        net_buffer_t *next = current->next;
        net_free_buffer(current);
        current = next;
    }
    rx_queue_head = NULL;
    rx_queue_tail = NULL;
}

void net_transmit(net_interface_t *iface, net_buffer_t *buf) {
    if (iface && iface->send && iface->up) {
        int r = iface->send(iface, buf->data + buf->offset, buf->len);
        if (r == 0) {
            net_stats.tx_packets++;
            net_stats.tx_bytes += buf->len;
            iface->tx_packets++;
            iface->tx_bytes   += buf->len;
        } else {
            net_stats.tx_errors++;
            iface->tx_errors++;
        }
    } else {
        net_stats.tx_dropped++;
    }
}

/* Periodic timer tick - drives TCP retransmissions, ARP aging, IP
 * reassembly, TIME_WAIT, keep-alive, zero-window probing, delayed
 * ACKs, PMTU cache aging, DHCP lease timer, DNS timeouts and IGMP
 * group membership.  Called once per kernel-tick (typically 10 ms). */
void net_tick(uint32_t now_ms) {
    tcp_tick(now_ms);
    arp_age(now_ms);
    ip_reassemble_tick(now_ms);
    ip_pmtu_age(now_ms);
    dhcp_client_tick(now_ms);
    dns_tick(now_ms);
    igmp_tick(now_ms);
    ipv6_tick(now_ms);
    bridge_tick(now_ms);
    (void)now_ms;
}

const net_stats_t *net_get_stats(void) { return &net_stats; }

void net_reset_stats(void) { memset(&net_stats, 0, sizeof(net_stats)); }
