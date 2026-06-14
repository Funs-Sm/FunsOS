#include "arp.h"
#include "ethernet.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"
#include "stddef.h"
#include "stdio.h"

static arp_entry_t   cache[ARP_CACHE_SIZE];
static uint32_t      cache_index;
static arp_stats_t   stats;
static arp_pending_t pending_list;
static arp_pending_t pending_pool[ARP_MAX_PENDING];
static uint8_t       pending_pool_used[ARP_MAX_PENDING];
static mutex_t       arp_lock;

static uint32_t now_ms_local(void) {
    /* Convert PIT ticks (100 Hz) to ms. */
    return timer_get_ticks() * 10U;
}

void arp_init(void) {
    mutex_init(&arp_lock);
    memset(cache, 0, sizeof(cache));
    memset(&stats, 0, sizeof(stats));
    memset(pending_pool_used, 0, sizeof(pending_pool_used));
    pending_list.next = NULL;
    cache_index = 0;
}

static arp_entry_t *find_entry_unlocked(ipv4_addr_t ip) {
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip.addr == ip.addr) {
            return &cache[i];
        }
    }
    return NULL;
}

void arp_add_entry(ipv4_addr_t ip, mac_addr_t mac) {
    if (ip.addr == 0) return;
    mutex_lock(&arp_lock);
    arp_entry_t *e = find_entry_unlocked(ip);
    if (e) {
        e->mac = mac;
        e->timestamp = now_ms_local();
        e->last_used = e->timestamp;
        e->failed_lookups = 0;
        mutex_unlock(&arp_lock);
        return;
    }
    /* Insert at a free slot or evict the oldest non-static entry. */
    int slot = -1;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) { slot = (int)i; break; }
    }
    if (slot < 0) {
        uint32_t oldest = 0xFFFFFFFF;
        int      oi     = -1;
        for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
            if (cache[i].static_entry) continue;
            if (cache[i].last_used < oldest) {
                oldest = cache[i].last_used;
                oi     = (int)i;
            }
        }
        if (oi < 0) oi = 0; /* all static: overwrite slot 0 */
        slot = oi;
    }
    cache[slot].ip            = ip;
    cache[slot].mac           = mac;
    cache[slot].timestamp     = now_ms_local();
    cache[slot].last_used     = cache[slot].timestamp;
    cache[slot].valid         = 1;
    cache[slot].static_entry  = 0;
    cache[slot].failed_lookups = 0;
    stats.entries_added++;
    mutex_unlock(&arp_lock);

    /* Wake any pending resolutions waiting for this IP. */
    arp_pending_t **pp = &pending_list.next;
    while (*pp) {
        arp_pending_t *cur = *pp;
        if (cur->ip.addr == ip.addr) {
            *pp = cur->next;
            for (uint32_t k = 0; k < ARP_MAX_PENDING; k++) {
                if (&pending_pool[k] == cur) pending_pool_used[k] = 0;
            }
        } else {
            pp = &cur->next;
        }
    }
}

int arp_lookup(ipv4_addr_t ip, mac_addr_t *mac) {
    int found = 0;
    mutex_lock(&arp_lock);
    arp_entry_t *e = find_entry_unlocked(ip);
    if (e) {
        if (mac) *mac = e->mac;
        e->last_used = now_ms_local();
        stats.cache_hits++;
        found = 1;
    } else {
        stats.cache_misses++;
    }
    mutex_unlock(&arp_lock);
    return found;
}

int arp_remove_entry(ipv4_addr_t ip) {
    int removed = 0;
    mutex_lock(&arp_lock);
    arp_entry_t *e = find_entry_unlocked(ip);
    if (e) {
        e->valid = 0;
        e->ip.addr = 0;
        removed = 1;
    }
    mutex_unlock(&arp_lock);
    return removed;
}

int arp_purge(void) {
    int removed = 0;
    mutex_lock(&arp_lock);
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && !cache[i].static_entry) {
            cache[i].valid = 0;
            cache[i].ip.addr = 0;
            removed++;
        }
    }
    mutex_unlock(&arp_lock);
    return removed;
}

void arp_age(uint32_t now_ms) {
    mutex_lock(&arp_lock);
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;
        if (cache[i].static_entry) continue;
        if ((uint32_t)(now_ms - cache[i].timestamp) > ARP_CACHE_TTL_MS) {
            cache[i].valid = 0;
            cache[i].ip.addr = 0;
            stats.entries_expired++;
        }
    }
    /* Re-send pending requests whose back-off has elapsed. */
    arp_pending_t *p = pending_list.next;
    while (p) {
        arp_pending_t *n = p->next;
        if ((uint32_t)(now_ms - p->sent_ms) > ARP_PENDING_TTL) {
            if (p->retries >= 3 || p->iface == NULL) {
                /* Drop: free the pool slot. */
                /* Remove from list */
                arp_pending_t **pp = &pending_list.next;
                while (*pp && *pp != p) pp = &(*pp)->next;
                if (*pp) *pp = p->next;
                for (uint32_t k = 0; k < ARP_MAX_PENDING; k++) {
                    if (&pending_pool[k] == p) pending_pool_used[k] = 0;
                }
            } else {
                p->sent_ms = now_ms;
                p->retries++;
            }
        }
        p = n;
    }
    mutex_unlock(&arp_lock);
}

int arp_resolve(net_interface_t *iface, ipv4_addr_t ip, mac_addr_t *mac) {
    if (ip.addr == 0xFFFFFFFF) {
        if (mac) {
            mac->bytes[0] = 0xFF; mac->bytes[1] = 0xFF;
            mac->bytes[2] = 0xFF; mac->bytes[3] = 0xFF;
            mac->bytes[4] = 0xFF; mac->bytes[5] = 0xFF;
        }
        return 1;
    }
    if (arp_lookup(ip, mac)) return 1;
    /* Broadcast resolution.  Add a pending record so we can retry. */
    mutex_lock(&arp_lock);
    int found_pending = 0;
    for (arp_pending_t *p = pending_list.next; p; p = p->next) {
        if (p->ip.addr == ip.addr && p->iface == iface) {
            found_pending = 1;
            break;
        }
    }
    if (!found_pending) {
        for (uint32_t k = 0; k < ARP_MAX_PENDING; k++) {
            if (!pending_pool_used[k]) {
                pending_pool_used[k] = 1;
                pending_pool[k].ip = ip;
                pending_pool[k].iface = iface;
                pending_pool[k].sent_ms = now_ms_local();
                pending_pool[k].retries = 0;
                pending_pool[k].next = pending_list.next;
                pending_list.next = &pending_pool[k];
                break;
            }
        }
    }
    mutex_unlock(&arp_lock);
    arp_send_request(iface, ip);
    return 0;
}

void arp_send_request(net_interface_t *iface, ipv4_addr_t target_ip) {
    if (!iface) return;
    arp_header_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = ARP_HW_ETHERNET;
    arp.ptype = ARP_PROTO_IP;
    arp.hlen = 6;
    arp.plen = 4;
    arp.opcode = ARP_OP_REQUEST;
    arp.sender_mac = iface->mac;
    arp.sender_ip = iface->ip;
    arp.target_ip = target_ip;
    arp.target_mac.bytes[0] = 0xFF; arp.target_mac.bytes[1] = 0xFF;
    arp.target_mac.bytes[2] = 0xFF; arp.target_mac.bytes[3] = 0xFF;
    arp.target_mac.bytes[4] = 0xFF; arp.target_mac.bytes[5] = 0xFF;

    mac_addr_t broadcast;
    broadcast.bytes[0] = 0xFF; broadcast.bytes[1] = 0xFF;
    broadcast.bytes[2] = 0xFF; broadcast.bytes[3] = 0xFF;
    broadcast.bytes[4] = 0xFF; broadcast.bytes[5] = 0xFF;

    ethernet_send(iface, broadcast, ETH_P_ARP, &arp, sizeof(arp_header_t));
    stats.requests_sent++;
}

void arp_send_reply(net_interface_t *iface, arp_header_t *request) {
    if (!iface || !request) return;
    arp_header_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = ARP_HW_ETHERNET;
    arp.ptype = ARP_PROTO_IP;
    arp.hlen = 6;
    arp.plen = 4;
    arp.opcode = ARP_OP_REPLY;
    arp.sender_mac = iface->mac;
    arp.sender_ip = iface->ip;
    arp.target_mac = request->sender_mac;
    arp.target_ip = request->sender_ip;

    ethernet_send(iface, request->sender_mac, ETH_P_ARP, &arp, sizeof(arp_header_t));
    stats.replies_sent++;
}

void arp_receive(net_buffer_t *buf) {
    arp_header_t *arp = (arp_header_t *)(buf->data + buf->offset);

    if (arp->htype != ARP_HW_ETHERNET || arp->ptype != ARP_PROTO_IP) {
        return;
    }
    if (arp->hlen != 6 || arp->plen != 4) return;

    arp_add_entry(arp->sender_ip, arp->sender_mac);

    if (arp->opcode == ARP_OP_REQUEST) {
        stats.requests_rcvd++;
        if (arp->target_ip.addr == buf->iface->ip.addr) {
            arp_send_reply(buf->iface, arp);
        }
    } else if (arp->opcode == ARP_OP_REPLY) {
        stats.replies_rcvd++;
        arp_add_entry(arp->sender_ip, arp->sender_mac);
    }
}

const arp_entry_t *arp_get_entries(uint32_t *count) {
    if (count) *count = ARP_CACHE_SIZE;
    return cache;
}

const arp_stats_t *arp_get_stats(void) {
    return &stats;
}

const arp_pending_t *arp_get_pending(uint32_t *count) {
    if (count) {
        uint32_t c = 0;
        for (arp_pending_t *p = pending_list.next; p; p = p->next) c++;
        *count = c;
    }
    return pending_list.next;
}

void arp_table_show(char *buf, uint32_t buf_size) {
    if (!buf || buf_size == 0) return;
    uint32_t pos = 0;
    pos += sprintf(buf + pos, "IP address      HW type  Flags   HW address\n");

    mutex_lock(&arp_lock);
    for (uint32_t i = 0; i < ARP_CACHE_SIZE && pos < buf_size - 64; i++) {
        if (!cache[i].valid) continue;
        pos += sprintf(buf + pos,
            "%u.%u.%u.%u    0x1      %s   %02X:%02X:%02X:%02X:%02X:%02X\n",
            (cache[i].ip.addr >> 24) & 0xFF,
            (cache[i].ip.addr >> 16) & 0xFF,
            (cache[i].ip.addr >>  8) & 0xFF,
            (cache[i].ip.addr      ) & 0xFF,
            cache[i].static_entry ? "S" : "D",
            cache[i].mac.bytes[0], cache[i].mac.bytes[1],
            cache[i].mac.bytes[2], cache[i].mac.bytes[3],
            cache[i].mac.bytes[4], cache[i].mac.bytes[5]);
    }
    mutex_unlock(&arp_lock);

    if (pos < buf_size)
        buf[pos] = '\0';
}

void arp_table_flush(void) {
    arp_purge();
}
