#include "ethernet.h"
#include "net.h"
#include "arp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"

/* Minimal mutex shim (single-core kernel: IRQ-based locking) */
typedef int mutex_t;
static inline void mutex_init(mutex_t *m) { *m = 0; }
static inline void mutex_lock(mutex_t *m) { (void)m; asm volatile("cli"); }
static inline void mutex_unlock(mutex_t *m) { (void)m; asm volatile("sti"); }

int ethernet_send_vlan(net_interface_t *iface, mac_addr_t dst,
                       uint16_t type, uint16_t vlan_id,
                       const void *payload, uint32_t len) {
    net_buffer_t *buf = net_alloc_buffer();
    if (!buf) return -1;

    uint32_t hdr_len = sizeof(ethernet_header_t);
    if (vlan_id) hdr_len += sizeof(vlan_tag_t);

    ethernet_header_t *hdr = (ethernet_header_t *)buf->data;
    hdr->dst = dst;
    hdr->src = iface->mac;
    hdr->ethertype = vlan_id ? ETH_P_VLAN : type;

    uint8_t *p = buf->data + sizeof(ethernet_header_t);
    if (vlan_id) {
        vlan_tag_t *vt = (vlan_tag_t *)p;
        vt->tci       = (uint16_t)(0xE000U | (vlan_id & 0x0FFFU));
        vt->ethertype = type;
        p += sizeof(vlan_tag_t);
    }
    memcpy(p, payload, len);
    uint32_t total_len = hdr_len - sizeof(ethernet_header_t) + hdr_len + len;
    /* Recompute properly: header is hdr_len bytes, payload is len. */
    total_len = hdr_len + len;

    if (total_len < 60) {
        memset(buf->data + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    buf->len = total_len;
    buf->offset = 0;
    buf->iface = iface;

    net_transmit(iface, buf);
    net_free_buffer(buf);
    return 0;
}

int ethernet_send(net_interface_t *iface, mac_addr_t dst, uint16_t type,
                  const void *payload, uint32_t len) {
    return ethernet_send_vlan(iface, dst, type, 0, payload, len);
}

void ethernet_receive(net_buffer_t *buf) {
    ethernet_header_t *hdr = (ethernet_header_t *)(buf->data + buf->offset);
    uint16_t type = hdr->ethertype;

    buf->offset += sizeof(ethernet_header_t);
    buf->len -= sizeof(ethernet_header_t);

    /* Handle 802.1Q VLAN tags.  Skip the inner tag and recurse. */
    if (type == ETH_P_VLAN) {
        if (buf->len < (int)sizeof(vlan_tag_t)) return;
        vlan_tag_t *vt = (vlan_tag_t *)(buf->data + buf->offset);
        type = vt->ethertype;
        buf->offset += sizeof(vlan_tag_t);
        buf->len    -= sizeof(vlan_tag_t);
    }

    switch (type) {
    case ETH_P_ARP:
        arp_receive(buf);
        break;
    case ETH_P_IP:
        ip_receive(buf);
        break;
    default:
        break;
    }
}

void ethernet_print_mac(mac_addr_t mac) {
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
        mac.bytes[0], mac.bytes[1], mac.bytes[2],
        mac.bytes[3], mac.bytes[4], mac.bytes[5]);
}

/* ========================================================================= */
/*  VLAN Management (802.1Q)                                                 */
/* ========================================================================= */

static vlan_interface_t vlan_interfaces[VLAN_MAX];
static vlan_filter_t    vlan_filter;
static vlan_stats_t     vlan_stats;
static uint32_t         vlan_iface_count;
static mutex_t          vlan_lock;

void vlan_init(void) {
    memset(vlan_interfaces, 0, sizeof(vlan_interfaces));
    memset(&vlan_filter, 0, sizeof(vlan_filter));
    memset(&vlan_stats, 0, sizeof(vlan_stats));
    vlan_iface_count = 0;
    vlan_filter.default_policy = 1;  /* allow all by default */
    mutex_init(&vlan_lock);
}

int vlan_create(uint16_t vlan_id, net_interface_t *parent, const char *name) {
    if (!parent || vlan_id == 0 || vlan_id > VLAN_ID_MAX) return -1;
    mutex_lock(&vlan_lock);
    if (vlan_iface_count >= VLAN_MAX) { mutex_unlock(&vlan_lock); return -1; }

    /* Check for duplicate VLAN ID */
    for (uint32_t i = 0; i < VLAN_MAX; i++) {
        if (vlan_interfaces[i].used && vlan_interfaces[i].vlan_id == vlan_id) {
            mutex_unlock(&vlan_lock);
            return -1;
        }
    }

    for (uint32_t i = 0; i < VLAN_MAX; i++) {
        if (!vlan_interfaces[i].used) {
            vlan_interface_t *v = &vlan_interfaces[i];
            v->used    = 1;
            v->vlan_id = vlan_id;
            v->pcp     = VLAN_PCP_BE;
            v->dei     = 0;
            v->parent  = parent;
            v->up      = 1;
            memcpy(v->mac.bytes, parent->mac.bytes, 6);
            if (name) {
                uint32_t j;
                for (j = 0; j < 15 && name[j]; j++) v->name[j] = name[j];
                v->name[j] = 0;
            }
            v->tx_packets = 0;
            v->rx_packets = 0;
            vlan_iface_count++;
            vlan_stats.vlan_created++;
            mutex_unlock(&vlan_lock);
            return 0;
        }
    }
    mutex_unlock(&vlan_lock);
    return -1;
}

int vlan_delete(uint16_t vlan_id) {
    mutex_lock(&vlan_lock);
    for (uint32_t i = 0; i < VLAN_MAX; i++) {
        if (vlan_interfaces[i].used && vlan_interfaces[i].vlan_id == vlan_id) {
            vlan_interfaces[i].used = 0;
            vlan_iface_count--;
            vlan_stats.vlan_deleted++;
            mutex_unlock(&vlan_lock);
            return 0;
        }
    }
    mutex_unlock(&vlan_lock);
    return -1;
}

vlan_interface_t *vlan_get(uint16_t vlan_id) {
    for (uint32_t i = 0; i < VLAN_MAX; i++) {
        if (vlan_interfaces[i].used && vlan_interfaces[i].vlan_id == vlan_id)
            return &vlan_interfaces[i];
    }
    return NULL;
}

/* ---- VLAN Filtering ---- */

int vlan_filter_add(uint16_t vlan_id) {
    if (vlan_filter.count >= 16) return -1;
    vlan_filter.vlan_ids[vlan_filter.count++] = vlan_id;
    return 0;
}

int vlan_filter_remove(uint16_t vlan_id) {
    for (uint8_t i = 0; i < vlan_filter.count; i++) {
        if (vlan_filter.vlan_ids[i] == vlan_id) {
            vlan_filter.vlan_ids[i] = vlan_filter.vlan_ids[vlan_filter.count - 1];
            vlan_filter.count--;
            return 0;
        }
    }
    return -1;
}

int vlan_filter_test(uint16_t vlan_id) {
    if (vlan_filter.default_policy) return 1;
    for (uint8_t i = 0; i < vlan_filter.count; i++) {
        if (vlan_filter.vlan_ids[i] == vlan_id) return 1;
    }
    vlan_stats.vlan_filter_drops++;
    return 0;
}

void vlan_filter_set_default(int allow) {
    vlan_filter.default_policy = allow ? 1 : 0;
}

void vlan_filter_flush(void) {
    vlan_filter.count = 0;
}

/* ---- VLAN receive path ---- */

void vlan_receive(net_buffer_t *buf, uint16_t vlan_id) {
    if (!buf || vlan_id == 0) return;
    if (!vlan_filter_test(vlan_id)) return;

    vlan_interface_t *v = vlan_get(vlan_id);
    if (!v) {
        vlan_stats.vlan_untagged_rcvd++;
        return;
    }

    v->rx_packets++;
    v->rx_bytes += buf->len;
    vlan_stats.vlan_tagged_rcvd++;

    /* Forward to IP layer via parent interface */
    buf->iface = v->parent;
    ip_receive(buf);
}

const vlan_stats_t *vlan_get_stats(void) { return &vlan_stats; }
uint32_t vlan_count(void) { return vlan_iface_count; }

/* ========================================================================= */
/*  Network Bridge (802.1D MAC Learning + Basic STP)                         */
/* ========================================================================= */

static network_bridge_t bridges[BRIDGE_MAX];
static bridge_stats_t   bridge_stats_data;
static mutex_t          bridge_lock;

/* STP BPDU ethertype */
#define ETH_P_STP 0x0026

void bridge_init(void) {
    memset(bridges, 0, sizeof(bridges));
    memset(&bridge_stats_data, 0, sizeof(bridge_stats_data));
    mutex_init(&bridge_lock);
}

int bridge_create(const char *name) {
    if (!name || !name[0]) return -1;
    mutex_lock(&bridge_lock);
    for (uint32_t i = 0; i < BRIDGE_MAX; i++) {
        if (!bridges[i].used) {
            network_bridge_t *br = &bridges[i];
            memset(br, 0, sizeof(*br));
            br->used = 1;
            uint32_t j;
            for (j = 0; j < 15 && name[j]; j++) br->name[j] = name[j];
            br->name[j] = 0;
            br->id.priority = 32768;
            br->max_age = 20000;
            br->hello_time = 2000;
            br->forward_delay = 15000;
            br->root_path_cost = 0;
            br->port_count = 0;
            br->fdb_count = 0;
            bridge_stats_data.bridges_active++;
            mutex_unlock(&bridge_lock);
            return 0;
        }
    }
    mutex_unlock(&bridge_lock);
    return -1;
}

int bridge_delete(const char *name) {
    if (!name) return -1;
    mutex_lock(&bridge_lock);
    for (uint32_t i = 0; i < BRIDGE_MAX; i++) {
        if (bridges[i].used && strcmp(bridges[i].name, name) == 0) {
            bridges[i].used = 0;
            if (bridge_stats_data.bridges_active > 0)
                bridge_stats_data.bridges_active--;
            mutex_unlock(&bridge_lock);
            return 0;
        }
    }
    mutex_unlock(&bridge_lock);
    return -1;
}

network_bridge_t *bridge_get(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < BRIDGE_MAX; i++) {
        if (bridges[i].used && strcmp(bridges[i].name, name) == 0)
            return &bridges[i];
    }
    return NULL;
}

int bridge_add_port(const char *name, net_interface_t *iface) {
    if (!name || !iface) return -1;
    mutex_lock(&bridge_lock);
    network_bridge_t *br = bridge_get(name);
    if (!br || br->port_count >= BRIDGE_MAX_PORTS) {
        mutex_unlock(&bridge_lock);
        return -1;
    }

    bridge_port_t *port = &br->ports[br->port_count];
    port->used       = 1;
    port->index      = br->port_count;
    port->iface      = iface;
    port->stp_state  = BRIDGE_STP_FORWARDING;  /* simplified: no STP initially */
    port->stp_role   = BRIDGE_STP_ROLE_DESIGNATED;
    port->priority   = 128;
    port->path_cost  = 4;  /* 1 Gbps = 4 */
    port->tx_packets = 0;
    port->rx_packets = 0;
    port->tx_dropped = 0;
    br->port_count++;

    /* Set bridge MAC to the first port's MAC */
    if (br->port_count == 1) {
        memcpy(br->mac.bytes, iface->mac.bytes, 6);
        memcpy(br->id.mac.bytes, iface->mac.bytes, 6);
        memcpy(br->root_id.mac.bytes, iface->mac.bytes, 6);
        br->root_id.priority = br->id.priority;
    }
    mutex_unlock(&bridge_lock);
    return 0;
}

int bridge_del_port(const char *name, net_interface_t *iface) {
    if (!name || !iface) return -1;
    mutex_lock(&bridge_lock);
    network_bridge_t *br = bridge_get(name);
    if (!br) { mutex_unlock(&bridge_lock); return -1; }

    for (uint8_t i = 0; i < br->port_count; i++) {
        if (br->ports[i].used && br->ports[i].iface == iface) {
            /* Remove the port by shifting remaining ports */
            for (uint8_t j = i; j + 1 < br->port_count; j++) {
                br->ports[j] = br->ports[j + 1];
                br->ports[j].index = j;
            }
            br->ports[br->port_count - 1].used = 0;
            br->port_count--;
            mutex_unlock(&bridge_lock);
            return 0;
        }
    }
    mutex_unlock(&bridge_lock);
    return -1;
}

/* ---- Forwarding Database (MAC Learning) ---- */

static uint32_t fdb_hash(mac_addr_t mac) {
    uint32_t h = 0;
    for (int i = 0; i < 6; i++) h = h * 31U + mac.bytes[i];
    return h % BRIDGE_FDB_MAX;
}

int bridge_fdb_add(const char *name, mac_addr_t mac, uint8_t port, int is_static) {
    network_bridge_t *br = bridge_get(name);
    if (!br || port >= br->port_count) return -1;

    uint32_t h = fdb_hash(mac);
    bridge_fdb_entry_t *e = (bridge_fdb_entry_t *)kmalloc(sizeof(bridge_fdb_entry_t));
    if (!e) return -1;
    e->used      = 1;
    e->mac       = mac;
    e->port      = port;
    e->age_ms    = timer_get_ticks() * 10U;
    e->is_static = is_static ? 1 : 0;
    e->next      = br->fdb[h];
    br->fdb[h]   = e;
    br->fdb_count++;
    br->total_learned++;
    bridge_stats_data.mac_learned++;
    return 0;
}

int bridge_fdb_del(const char *name, mac_addr_t mac) {
    network_bridge_t *br = bridge_get(name);
    if (!br) return -1;

    uint32_t h = fdb_hash(mac);
    bridge_fdb_entry_t **pp = &br->fdb[h];
    while (*pp) {
        if (memcmp((*pp)->mac.bytes, mac.bytes, 6) == 0) {
            bridge_fdb_entry_t *d = *pp;
            *pp = d->next;
            br->fdb_count--;
            kfree(d);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

static bridge_fdb_entry_t *bridge_fdb_lookup(network_bridge_t *br, mac_addr_t mac) {
    uint32_t h = fdb_hash(mac);
    for (bridge_fdb_entry_t *e = br->fdb[h]; e; e = e->next) {
        if (memcmp(e->mac.bytes, mac.bytes, 6) == 0) return e;
    }
    return NULL;
}

void bridge_fdb_age(const char *name, uint32_t now_ms) {
    network_bridge_t *br = bridge_get(name);
    if (!br) return;

    for (uint32_t i = 0; i < BRIDGE_FDB_MAX; i++) {
        bridge_fdb_entry_t **pp = &br->fdb[i];
        while (*pp) {
            bridge_fdb_entry_t *e = *pp;
            if (!e->is_static && (now_ms - e->age_ms) > BRIDGE_FDB_AGE_MS) {
                *pp = e->next;
                br->fdb_count--;
                br->total_aged++;
                bridge_stats_data.mac_aged++;
                kfree(e);
            } else {
                pp = &(*pp)->next;
            }
        }
    }
}

/* ---- Bridge receive path (MAC learning + forwarding) ---- */

void bridge_receive(const char *name, net_buffer_t *buf, uint8_t port) {
    network_bridge_t *br = bridge_get(name);
    if (!br || !buf || port >= br->port_count) return;
    if (buf->len < (int)sizeof(ethernet_header_t)) return;

    ethernet_header_t *hdr = (ethernet_header_t *)(buf->data + buf->offset);
    bridge_port_t *bp = &br->ports[port];
    bp->rx_packets++;
    bp->rx_bytes += buf->len;

    /* MAC learning: learn source MAC → port */
    bridge_fdb_entry_t *existing = bridge_fdb_lookup(br, hdr->src);
    if (!existing) {
        bridge_fdb_add(name, hdr->src, port, 0);
    } else {
        existing->age_ms = timer_get_ticks() * 10U;
        if (existing->port != port) {
            existing->port = port;
        }
    }

    /* MAC forwarding: look up destination MAC */
    bridge_fdb_entry_t *dst_entry = bridge_fdb_lookup(br, hdr->dst);

    if (dst_entry && dst_entry->port != port) {
        /* Unicast forward to known port */
        bridge_port_t *out = &br->ports[dst_entry->port];
        if (out->stp_state == BRIDGE_STP_FORWARDING) {
            out->tx_packets++;
            out->tx_bytes += buf->len;
            bridge_stats_data.frames_forwarded++;
            /* Transmit via the output port's interface */
            net_transmit(out->iface, buf);
        } else {
            br->ports[port].tx_dropped++;
            bridge_stats_data.frames_dropped++;
        }
    } else {
        /* Flood to all ports except the one it came from */
        bridge_stats_data.frames_flooded++;
        br->total_flooded++;
        for (uint8_t i = 0; i < br->port_count; i++) {
            if (i == port) continue;
            bridge_port_t *bp_out = &br->ports[i];
            if (bp_out->stp_state == BRIDGE_STP_FORWARDING && bp_out->iface) {
                bp_out->tx_packets++;
                bp_out->tx_bytes += buf->len;
                net_transmit(bp_out->iface, buf);
            }
        }
    }
}

/* ---- STP tick ---- */

void bridge_stp_tick(const char *name, uint32_t now_ms) {
    network_bridge_t *br = bridge_get(name);
    if (!br) return;

    /* Simple STP: if root bridge, all ports are designated and forwarding.
     * In production, this would process BPDUs and run the full STP algorithm. */
    for (uint8_t i = 0; i < br->port_count; i++) {
        bridge_port_t *bp = &br->ports[i];
        if (bp->stp_state == BRIDGE_STP_LISTENING) {
            if ((now_ms - bp->tx_packets * 1000) > br->forward_delay) {
                bp->stp_state = BRIDGE_STP_LEARNING;
                bridge_stats_data.stp_changes++;
            }
        } else if (bp->stp_state == BRIDGE_STP_LEARNING) {
            if ((now_ms - bp->tx_packets * 1000) > br->forward_delay) {
                bp->stp_state = BRIDGE_STP_FORWARDING;
                bridge_stats_data.stp_changes++;
            }
        }
    }
}

void bridge_tick(uint32_t now_ms) {
    for (uint32_t i = 0; i < BRIDGE_MAX; i++) {
        if (bridges[i].used) {
            bridge_fdb_age(bridges[i].name, now_ms);
            bridge_stp_tick(bridges[i].name, now_ms);
        }
    }
}

const bridge_stats_t *bridge_get_stats(void) { return &bridge_stats_data; }
