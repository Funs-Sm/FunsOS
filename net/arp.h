#ifndef ARP_H
#define ARP_H

#include "net.h"
#include "stdint.h"

#define ARP_HW_ETHERNET  1
#define ARP_PROTO_IP     0x0800
#define ARP_OP_REQUEST   1
#define ARP_OP_REPLY     2

#define ARP_CACHE_SIZE   256
#define ARP_CACHE_TTL_MS 300000U   /* 5 minutes */
#define ARP_PENDING_TTL 2000U      /* 2 s   */
#define ARP_MAX_PENDING  64

typedef struct {
    ipv4_addr_t ip;
    mac_addr_t mac;
    uint32_t timestamp;
    uint32_t last_used;
    uint8_t  valid;
    uint8_t  static_entry;
    uint8_t  failed_lookups;
} arp_entry_t;

/* Pending resolution with back-off.  Used by ip_send() so that
 * outbound packets can be queued and retried once ARP replies
 * arrive. */
typedef struct arp_pending {
    ipv4_addr_t          ip;
    net_interface_t     *iface;
    uint32_t             sent_ms;
    uint8_t              retries;
    struct arp_pending  *next;
} arp_pending_t;

typedef struct {
    /* Statistics (RFC 826 / RFC 5227 friendly) */
    uint32_t requests_sent;
    uint32_t replies_sent;
    uint32_t requests_rcvd;
    uint32_t replies_rcvd;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t entries_added;
    uint32_t entries_expired;
} arp_stats_t;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t opcode;
    mac_addr_t sender_mac;
    ipv4_addr_t sender_ip;
    mac_addr_t target_mac;
    ipv4_addr_t target_ip;
} arp_header_t;

void arp_init(void);
void arp_send_request(net_interface_t *iface, ipv4_addr_t target_ip);
void arp_send_reply(net_interface_t *iface, arp_header_t *request);
void arp_receive(net_buffer_t *buf);
int  arp_resolve(net_interface_t *iface, ipv4_addr_t ip, mac_addr_t *mac);
void arp_add_entry(ipv4_addr_t ip, mac_addr_t mac);
int  arp_lookup(ipv4_addr_t ip, mac_addr_t *mac);
int  arp_remove_entry(ipv4_addr_t ip);
int  arp_purge(void);
void arp_age(uint32_t now_ms);
const arp_entry_t   *arp_get_entries(uint32_t *count);
const arp_stats_t   *arp_get_stats(void);
const arp_pending_t *arp_get_pending(uint32_t *count);
void arp_table_show(char *buf, uint32_t buf_size);
void arp_table_flush(void);

#endif
