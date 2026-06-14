#ifndef ARP_GUARD_H
#define ARP_GUARD_H

#include "stdint.h"
#include "net.h"

#define ARP_GUARD_MAX_ENTRIES 128

typedef struct {
    ipv4_addr_t ip;
    mac_addr_t  mac;
    uint8_t     is_static;    /* 1 = static (trusted), 0 = learned */
    uint8_t     suspicious;   /* 1 = flagged as suspicious */
    uint32_t    change_count; /* Number of MAC changes detected */
    uint32_t    last_seen_ms; /* Last time this entry was seen */
} arp_guard_entry_t;

typedef struct {
    uint32_t total_checks;
    uint32_t suspicious_detected;
    uint32_t static_violations;
    uint32_t mac_changes;
} arp_guard_stats_t;

void arp_guard_init(void);
int  arp_guard_add_static(ipv4_addr_t ip, mac_addr_t mac);
int  arp_guard_check(ipv4_addr_t ip, mac_addr_t mac);
int  arp_guard_remove(ipv4_addr_t ip);
void arp_guard_on_arp_reply(ipv4_addr_t sender_ip, mac_addr_t sender_mac);
const arp_guard_stats_t *arp_guard_get_stats(void);
uint32_t arp_guard_get_entries(arp_guard_entry_t *out, uint32_t max);

#endif
