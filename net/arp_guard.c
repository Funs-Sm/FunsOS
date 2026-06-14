#include "arp_guard.h"
#include "arp.h"
#include "klog.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"
#include "stdio.h"

static arp_guard_entry_t guard_table[ARP_GUARD_MAX_ENTRIES];
static uint32_t guard_count = 0;
static arp_guard_stats_t guard_stats;
static mutex_t guard_lock;

static uint32_t guard_now_ms(void) {
    return timer_get_ticks() * 10U;
}

static int mac_equal(const mac_addr_t *a, const mac_addr_t *b) {
    for (int i = 0; i < 6; i++) {
        if (a->bytes[i] != b->bytes[i]) return 0;
    }
    return 1;
}

static arp_guard_entry_t *find_entry(ipv4_addr_t ip) {
    for (uint32_t i = 0; i < guard_count; i++) {
        if (guard_table[i].ip.addr == ip.addr) {
            return &guard_table[i];
        }
    }
    return NULL;
}

void arp_guard_init(void) {
    mutex_init(&guard_lock);
    memset(guard_table, 0, sizeof(guard_table));
    memset(&guard_stats, 0, sizeof(guard_stats));
    guard_count = 0;
}

int arp_guard_add_static(ipv4_addr_t ip, mac_addr_t mac) {
    mutex_lock(&guard_lock);

    /* Check if entry already exists */
    arp_guard_entry_t *e = find_entry(ip);
    if (e) {
        /* Update existing entry to static */
        e->mac = mac;
        e->is_static = 1;
        e->suspicious = 0;
        e->last_seen_ms = guard_now_ms();
        mutex_unlock(&guard_lock);
        return 0;
    }

    /* Add new static entry */
    if (guard_count >= ARP_GUARD_MAX_ENTRIES) {
        mutex_unlock(&guard_lock);
        return -1;
    }

    e = &guard_table[guard_count];
    e->ip = ip;
    e->mac = mac;
    e->is_static = 1;
    e->suspicious = 0;
    e->change_count = 0;
    e->last_seen_ms = guard_now_ms();
    guard_count++;

    mutex_unlock(&guard_lock);
    return 0;
}

int arp_guard_check(ipv4_addr_t ip, mac_addr_t mac) {
    mutex_lock(&guard_lock);
    guard_stats.total_checks++;

    arp_guard_entry_t *e = find_entry(ip);
    if (!e) {
        /* Unknown IP - learn it */
        if (guard_count < ARP_GUARD_MAX_ENTRIES) {
            e = &guard_table[guard_count];
            e->ip = ip;
            e->mac = mac;
            e->is_static = 0;
            e->suspicious = 0;
            e->change_count = 0;
            e->last_seen_ms = guard_now_ms();
            guard_count++;
        }
        mutex_unlock(&guard_lock);
        return 1; /* OK - no prior mapping to conflict with */
    }

    /* Entry exists - check MAC */
    if (mac_equal(&e->mac, &mac)) {
        /* MAC matches - all good */
        e->last_seen_ms = guard_now_ms();
        mutex_unlock(&guard_lock);
        return 1;
    }

    /* MAC mismatch! */
    e->change_count++;
    guard_stats.mac_changes++;

    if (e->is_static) {
        /* Violation of a static (trusted) entry - definitely suspicious */
        e->suspicious = 1;
        guard_stats.suspicious_detected++;
        guard_stats.static_violations++;

        klog_warn("ARP GUARD: spoofing detected for %u.%u.%u.%u - "
                  "expected %02X:%02X:%02X:%02X:%02X:%02X, got %02X:%02X:%02X:%02X:%02X:%02X",
                  (ip.addr >> 24) & 0xFF, (ip.addr >> 16) & 0xFF,
                  (ip.addr >> 8) & 0xFF, ip.addr & 0xFF,
                  e->mac.bytes[0], e->mac.bytes[1], e->mac.bytes[2],
                  e->mac.bytes[3], e->mac.bytes[4], e->mac.bytes[5],
                  mac.bytes[0], mac.bytes[1], mac.bytes[2],
                  mac.bytes[3], mac.bytes[4], mac.bytes[5]);

        mutex_unlock(&guard_lock);
        return 0; /* Suspicious */
    }

    /* Dynamic entry with MAC change - could be legitimate (DHCP reassignment)
     * but flag if changes are frequent */
    if (e->change_count >= 3) {
        e->suspicious = 1;
        guard_stats.suspicious_detected++;

        klog_warn("ARP GUARD: frequent MAC change for %u.%u.%u.%u (%u changes)",
                  (ip.addr >> 24) & 0xFF, (ip.addr >> 16) & 0xFF,
                  (ip.addr >> 8) & 0xFF, ip.addr & 0xFF,
                  e->change_count);

        mutex_unlock(&guard_lock);
        return 0; /* Suspicious */
    }

    /* Update the learned MAC for dynamic entries */
    e->mac = mac;
    e->last_seen_ms = guard_now_ms();

    mutex_unlock(&guard_lock);
    return 1; /* OK - first or second change, allow it */
}

int arp_guard_remove(ipv4_addr_t ip) {
    mutex_lock(&guard_lock);
    int found = 0;

    for (uint32_t i = 0; i < guard_count; i++) {
        if (guard_table[i].ip.addr == ip.addr) {
            /* Shift remaining entries down */
            for (uint32_t j = i; j < guard_count - 1; j++) {
                guard_table[j] = guard_table[j + 1];
            }
            guard_count--;
            found = 1;
            break;
        }
    }

    mutex_unlock(&guard_lock);
    return found ? 0 : -1;
}

void arp_guard_on_arp_reply(ipv4_addr_t sender_ip, mac_addr_t sender_mac) {
    /* Check the reply against our guard table */
    int result = arp_guard_check(sender_ip, sender_mac);

    if (!result) {
        /* Suspicious - log was already emitted by arp_guard_check */
        return;
    }

    /* If OK, also update the kernel ARP cache */
    arp_add_entry(sender_ip, sender_mac);
}

const arp_guard_stats_t *arp_guard_get_stats(void) {
    return &guard_stats;
}

uint32_t arp_guard_get_entries(arp_guard_entry_t *out, uint32_t max) {
    mutex_lock(&guard_lock);
    uint32_t count = guard_count < max ? guard_count : max;
    if (out) {
        memcpy(out, guard_table, count * sizeof(arp_guard_entry_t));
    }
    mutex_unlock(&guard_lock);
    return count;
}
