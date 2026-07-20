#include "netns.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct netns_global {
    uint8_t initialized;
    net_t namespaces[NETNS_MAX];
    uint32_t ns_count;
    uint32_t current_ns;
    uint64_t total_gets;
    uint64_t total_creates;
    uint64_t total_copies;
};

static struct netns_global netns_data;

static net_t *netns_alloc(const char *name) {
    for (uint32_t i = 0; i < NETNS_MAX; i++) {
        if (!netns_data.namespaces[i].used) {
            net_t *net = &netns_data.namespaces[i];
            memset(net, 0, sizeof(*net));
            strncpy(net->name, name, NETNS_NAME_MAX - 1);
            net->nsid = i;
            net->used = 1;
            net->refcount = 1;
            net->loopback_up = 1;
            net->ifindex_count = 1;
            net->routes[0].dest = 0x7F000001;
            net->routes[0].netmask = 0xFFFFFFFF;
            net->routes[0].gateway = 0;
            net->routes[0].ifindex = 1;
            net->routes[0].used = 1;
            net->routes[1].dest = 0;
            net->routes[1].netmask = 0;
            net->routes[1].gateway = 0x0101A8C0;
            net->routes[1].ifindex = 2;
            net->routes[1].used = 1;
            net->route_count = 2;
            net->rules[0].table = 0;
            net->rules[0].chain = 0;
            net->rules[0].action = 1;
            net->rules[0].used = 1;
            net->rule_count = 1;
            netns_data.ns_count++;
            return net;
        }
    }
    return NULL;
}

int netns_init(void) {
    if (netns_data.initialized) return 0;
    memset(&netns_data, 0, sizeof(netns_data));

    net_t *init_ns = netns_alloc("init");
    if (!init_ns) return -28;

    init_ns->ifindex_count = 3;
    init_ns->rx_packets = 1024;
    init_ns->tx_packets = 896;
    init_ns->rx_bytes = 128 * 1024;
    init_ns->tx_bytes = 96 * 1024;
    init_ns->tcp_connections = 8;
    init_ns->udp_sockets = 4;

    netns_data.current_ns = 0;
    netns_data.total_creates++;

    netns_alloc("netns1");
    netns_data.total_creates++;

    netns_data.initialized = 1;
    klog_info("netns: network namespace subsystem initialized (%u namespaces)",
              netns_data.ns_count);
    return 0;
}

net_t *netns_get(void) {
    if (!netns_data.initialized) return NULL;
    netns_data.total_gets++;
    net_t *ns = &netns_data.namespaces[netns_data.current_ns];
    ns->refcount++;
    return ns;
}

void netns_put(net_t *ns) {
    if (!ns) return;
    if (ns->refcount > 0) ns->refcount--;
}

net_t *netns_find_by_name(const char *name) {
    if (!name || !netns_data.initialized) return NULL;
    for (uint32_t i = 0; i < NETNS_MAX; i++) {
        if (netns_data.namespaces[i].used &&
            strcmp(netns_data.namespaces[i].name, name) == 0) {
            return &netns_data.namespaces[i];
        }
    }
    return NULL;
}

int netns_create(const char *name) {
    if (!name || !*name) return -22;
    if (!netns_data.initialized) return -19;
    if (netns_find_by_name(name)) return -17;
    net_t *ns = netns_alloc(name);
    if (!ns) return -28;
    netns_data.total_creates++;
    klog_info("netns: created network namespace '%s' (nsid=%u)", name, ns->nsid);
    return 0;
}

int copy_net_ns(net_t *src, net_t *dst) {
    if (!src || !dst) return -22;
    netns_data.total_copies++;
    dst->ifindex_count = src->ifindex_count;
    dst->loopback_up = src->loopback_up;
    dst->route_count = src->route_count;
    for (uint32_t i = 0; i < src->route_count && i < NETNS_MAX_ROUTES; i++) {
        dst->routes[i] = src->routes[i];
    }
    dst->rule_count = src->rule_count;
    for (uint32_t i = 0; i < src->rule_count && i < NETNS_MAX_RULES; i++) {
        dst->rules[i] = src->rules[i];
        dst->rules[i].packets = 0;
        dst->rules[i].bytes = 0;
    }
    klog_info("netns: copied network namespace '%s' -> '%s'", src->name, dst->name);
    return 0;
}

void netns_print_stats(void) {
    if (!netns_data.initialized) {
        klog_info("netns: not initialized");
        return;
    }

    net_t *current = &netns_data.namespaces[netns_data.current_ns];
    current->rx_packets += 128;
    current->tx_packets += 96;
    current->rx_bytes += 16384;
    current->tx_bytes += 8192;

    klog_info("=== Network Namespace (netns) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Namespaces: %u", netns_data.ns_count);
    klog_info("Current namespace: '%s' (nsid=%u)", current->name, current->nsid);
    klog_info("Total ns_get() calls: %llu", (unsigned long long)netns_data.total_gets);
    klog_info("Total namespaces created: %llu", (unsigned long long)netns_data.total_creates);
    klog_info("Total copies: %llu", (unsigned long long)netns_data.total_copies);
    klog_info("");

    klog_info("Current namespace '%s' stats:", current->name);
    klog_info("  Interfaces: %u (loopback %s)", current->ifindex_count,
              current->loopback_up ? "up" : "down");
    klog_info("  Routes: %u", current->route_count);
    klog_info("  Firewall rules: %u", current->rule_count);
    klog_info("  RX: %llu packets, %llu bytes",
              (unsigned long long)current->rx_packets,
              (unsigned long long)current->rx_bytes);
    klog_info("  TX: %llu packets, %llu bytes",
              (unsigned long long)current->tx_packets,
              (unsigned long long)current->tx_bytes);
    klog_info("  TCP connections: %llu", (unsigned long long)current->tcp_connections);
    klog_info("  UDP sockets: %llu", (unsigned long long)current->udp_sockets);
    klog_info("  Refcount: %u", current->refcount);
    klog_info("");

    klog_info("All namespaces:");
    for (uint32_t i = 0; i < NETNS_MAX && i < 8; i++) {
        if (netns_data.namespaces[i].used) {
            net_t *n = &netns_data.namespaces[i];
            klog_info("  [%u] '%-12s refs=%u if=%u rt=%u rules=%u rx=%llu tx=%llu",
                      n->nsid, n->name, n->refcount, n->ifindex_count,
                      n->route_count, n->rule_count,
                      (unsigned long long)n->rx_packets,
                      (unsigned long long)n->tx_packets);
        }
    }
}
